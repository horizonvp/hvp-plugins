"""Import the HVPText font tools' output into a project: the letter materials and one font asset per
font. Runs inside the editor, headless or not:

    UnrealEditor-Cmd.exe <Project>.uproject -run=pythonscript -script="<this file> [options]" -nohmd

Options (all optional):
    --fonts-folder=/Game/HVPText/Fonts         where the per-font assets go (one subfolder per font)
    --font-folder=/Game/.../Gotham-500          the exact folder for a single font (with --only=<it>)
    --materials-folder=/Game/HVPText/Materials  where M_LetterMSDF and M_LetterMesh go
    --generated=<path>                          the tools' output (default <Project>/Art/Generated)
    --only=Cinzel-700,Inter-800                 just these fonts
    --materials-only                            rebuild the two materials, nothing else
    --rebuild-materials                         rebuild them as part of a full import too
    --layout-only                               refresh line metrics and kerning on existing assets
    --meshes-only                               re-import glyph meshes (of --only, or every font)

Run build_fonts.py (and build_glyph_meshes.py for glyph meshes) first; build_fonts.py --metrics-only
for a font drawn only as glyph meshes, which then gets no atlas. Idempotent: re-running re-imports
and rewrites every font asset. The materials are only made when missing (or asked for), so edits to
them survive; a font only needs the material of each technique it has. Per font, under
<fonts-folder>/<Font>/:
    <Font>_MSDF_T          MTSDF atlas, linear, uncompressed (compression ruins distance fields)
    MI_LetterMSDF_<Font>   M_LetterMSDF bound to the atlas
    Mesh/<Font>_<U+code>_SM  extruded glyph with 3 LODs, where build_glyph_meshes.py made them
    <Font>_DA              the LetterFontAsset tying it together

Then point Project Settings > Plugins > HVP Text at them: Default Font, and Glyph Mesh Material
(M_LetterMesh). The script prints both paths.
"""
import json
import pathlib
import sys

import unreal

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
log = []


def option(name, default=None):
    """--name=value from the script's arguments; True for a bare --name."""
    for arg in sys.argv[1:]:
        if arg == f"--{name}":
            return True
        if arg.startswith(f"--{name}="):
            return arg.split("=", 1)[1]
    return default


PROJECT = pathlib.Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
GEN = pathlib.Path(option("generated", str(PROJECT / "Art" / "Generated")))
FONT_DIR = option("fonts-folder", "/Game/HVPText/Fonts").rstrip("/")
MAT_DIR = option("materials-folder", "/Game/HVPText/Materials").rstrip("/")
ONLY = [f for f in (option("only") or "").split(",") if f]
FONT_FOLDER = (option("font-folder") or "").rstrip("/")
RESULT = None  # what main() emitted, for a caller running this file with runpy


def font_folder(fid):
    return FONT_FOLDER or f"{FONT_DIR}/{fid}"

# LOD screen sizes for an ~8 cm letter: LOD1 from about 2 m, LOD2 from about 4 m.
GLYPH_LOD_SCREEN_SIZES = [1.0, 0.06, 0.025]


def emit(payload):
    """One line a caller can find in the log."""
    global RESULT
    RESULT = payload
    unreal.log_warning("### HVPTEXT_IMPORT: " + json.dumps(payload, default=str))


def import_file(src: pathlib.Path, dest_path: str, dest_name: str):
    task = unreal.AssetImportTask()
    task.filename = str(src)
    task.destination_path = dest_path
    task.destination_name = dest_name
    task.automated = True
    task.replace_existing = True
    task.save = False
    tools.import_asset_tasks([task])
    return EAL.load_asset(f"{dest_path}/{dest_name}")


def ensure(asset_path: str, cls, factory):
    if EAL.does_asset_exist(asset_path):
        return EAL.load_asset(asset_path)
    folder, name = asset_path.rsplit("/", 1)
    return tools.create_asset(name, folder, cls, factory)


def clear_material(mat):
    """Empty a material's graph before it is rebuilt. delete_all_material_expressions alone leaves
    some of the previous build's nodes behind, disconnected: delete whatever it leaves, one by one."""
    MEL.delete_all_material_expressions(mat)
    for e in list(MEL.get_material_expressions(mat)):
        MEL.delete_material_expression(mat, e)


def vec4f(values):
    """unreal.Vector4f takes no constructor arguments; fill it field by field."""
    v = unreal.Vector4f()
    for name, value in zip("xyzw", values):
        v.set_editor_property(name, float(value))
    return v


# --- materials --------------------------------------------------------------------------------

MSDF_HLSL = r"""
// The engine plane's UVs run across the quad transposed relative to the glyph frame the
// technique lays it out in, so swap them (checked against a viewport capture).
float2 t = UV.yx;
if (FlipU > 0.5) t.x = 1.0 - t.x;
if (FlipV > 0.5) t.y = 1.0 - t.y;
float2 uv = lerp(Rect.xy, Rect.zw, t);
float4 s = Texture2DSample(Atlas, AtlasSampler, uv);
// Multi-channel distance: the median of RGB keeps corners sharp.
float sd = max(min(s.r, s.g), min(max(s.r, s.g), s.b));
// Screen-space antialiasing: how many screen pixels one unit of distance spans.
float2 unitRange = PxRange / AtlasSize;
float2 screenTexSize = 1.0 / max(fwidth(uv), 1e-6);
float spr = max(0.5 * dot(unitRange, screenTexSize), 1.0);
float body = saturate(spr * (sd - 0.5) + 0.5);
// Glow from the true distance field in alpha: smooth, reaches the quad's padding.
float g = saturate((s.a - (0.5 - GlowWidth)) / GlowWidth);
float glow = g * g * (1.0 - body);
float3 rgb = Tint * (body * CoreBoost + glow * GlowIntensity);
float a = saturate(body + glow * GlowIntensity * GlowOpacity);
return float4(rgb, a);
"""


def build_msdf_material():
    """Translucent, unlit. The glyph's atlas rect comes from its component's custom primitive data
    0-3 (HVPText draws each glyph as its own instanced component); the colour from per instance
    custom data 0-2. Every other custom data float is the project's."""
    path = f"{MAT_DIR}/M_LetterMSDF"
    mat = ensure(path, unreal.Material, unreal.MaterialFactoryNew())
    clear_material(mat)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property("used_with_instanced_static_meshes", True)
    mat.set_editor_property("two_sided", False)

    def scalar(name, value, x, y):
        e = MEL.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, x, y)
        e.set_editor_property("parameter_name", name)
        e.set_editor_property("default_value", value)
        return e

    def cd(index, x, y):
        e = MEL.create_material_expression(mat, unreal.MaterialExpressionPerInstanceCustomData, x, y)
        e.set_editor_property("data_index", index)
        return e

    def cpd(name, index, x, y):
        e = MEL.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, x, y)
        e.set_editor_property("parameter_name", name)
        e.set_editor_property("use_custom_primitive_data", True)
        e.set_editor_property("primitive_data_index", index)
        return e

    custom = MEL.create_material_expression(mat, unreal.MaterialExpressionCustom, 0, 0)
    custom.set_editor_property("code", MSDF_HLSL)
    custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT4)
    inputs = []
    for n in ["UV", "Rect", "Atlas", "PxRange", "AtlasSize", "Tint", "GlowWidth", "GlowIntensity",
              "GlowOpacity", "CoreBoost", "FlipU", "FlipV"]:
        ci = unreal.CustomInput()
        ci.set_editor_property("input_name", n)
        inputs.append(ci)
    custom.set_editor_property("inputs", inputs)

    uv = MEL.create_material_expression(mat, unreal.MaterialExpressionTextureCoordinate, -900, -300)
    rect_a = MEL.create_material_expression(mat, unreal.MaterialExpressionAppendVector, -700, -200)
    rect_b = MEL.create_material_expression(mat, unreal.MaterialExpressionAppendVector, -700, -120)
    rect = MEL.create_material_expression(mat, unreal.MaterialExpressionAppendVector, -500, -160)
    MEL.connect_material_expressions(cpd("GlyphRectLeft", 0, -900, -220), "", rect_a, "A")
    MEL.connect_material_expressions(cpd("GlyphRectTop", 1, -900, -180), "", rect_a, "B")
    MEL.connect_material_expressions(cpd("GlyphRectRight", 2, -900, -140), "", rect_b, "A")
    MEL.connect_material_expressions(cpd("GlyphRectBottom", 3, -900, -100), "", rect_b, "B")
    MEL.connect_material_expressions(rect_a, "", rect, "A")
    MEL.connect_material_expressions(rect_b, "", rect, "B")

    atlas = MEL.create_material_expression(mat, unreal.MaterialExpressionTextureObjectParameter, -700, 0)
    atlas.set_editor_property("parameter_name", "Atlas")
    atlas.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)

    tint_a = MEL.create_material_expression(mat, unreal.MaterialExpressionAppendVector, -700, 120)
    tint = MEL.create_material_expression(mat, unreal.MaterialExpressionAppendVector, -500, 140)
    MEL.connect_material_expressions(cd(0, -900, 100), "", tint_a, "A")
    MEL.connect_material_expressions(cd(1, -900, 140), "", tint_a, "B")
    MEL.connect_material_expressions(tint_a, "", tint, "A")
    MEL.connect_material_expressions(cd(2, -900, 180), "", tint, "B")

    MEL.connect_material_expressions(uv, "", custom, "UV")
    MEL.connect_material_expressions(rect, "", custom, "Rect")
    MEL.connect_material_expressions(atlas, "", custom, "Atlas")
    MEL.connect_material_expressions(scalar("PxRange", 16.0, -400, 60), "", custom, "PxRange")
    MEL.connect_material_expressions(scalar("AtlasSize", 1024.0, -400, 100), "", custom, "AtlasSize")
    MEL.connect_material_expressions(tint, "", custom, "Tint")
    MEL.connect_material_expressions(scalar("GlowWidth", 0.35, -400, 180), "", custom, "GlowWidth")
    MEL.connect_material_expressions(scalar("GlowIntensity", 1.2, -400, 220), "", custom, "GlowIntensity")
    MEL.connect_material_expressions(scalar("GlowOpacity", 0.6, -400, 260), "", custom, "GlowOpacity")
    MEL.connect_material_expressions(scalar("CoreBoost", 1.6, -400, 300), "", custom, "CoreBoost")
    MEL.connect_material_expressions(scalar("FlipU", 1.0, -400, 340), "", custom, "FlipU")  # mirrored otherwise (capture-checked)
    MEL.connect_material_expressions(scalar("FlipV", 0.0, -400, 380), "", custom, "FlipV")

    rgb = MEL.create_material_expression(mat, unreal.MaterialExpressionComponentMask, 300, -40)
    for ch, on in (("r", True), ("g", True), ("b", True), ("a", False)):
        rgb.set_editor_property(ch, on)
    alpha = MEL.create_material_expression(mat, unreal.MaterialExpressionComponentMask, 300, 60)
    # A new mask defaults to R and G on: clear them, or Opacity gets a float3.
    for ch, on in (("r", False), ("g", False), ("b", False), ("a", True)):
        alpha.set_editor_property(ch, on)
    MEL.connect_material_expressions(custom, "", rgb, "")
    MEL.connect_material_expressions(custom, "", alpha, "")
    MEL.connect_material_property(rgb, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MEL.connect_material_property(alpha, "", unreal.MaterialProperty.MP_OPACITY)
    MEL.recompile_material(mat)
    EAL.save_asset(path, only_if_is_dirty=False)
    log.append(path)
    return mat


def build_mesh_material():
    """Glyph meshes: unlit, colour from per instance custom data 0-2. Faces turned toward the viewer
    read brighter than the side walls, so the extrusion shows without lights."""
    path = f"{MAT_DIR}/M_LetterMesh"
    mat = ensure(path, unreal.Material, unreal.MaterialFactoryNew())
    clear_material(mat)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property("used_with_instanced_static_meshes", True)
    # The shading uses abs(facing), so it is right whichever way the triangles face.
    mat.set_editor_property("two_sided", True)

    def cd(index, x, y):
        e = MEL.create_material_expression(mat, unreal.MaterialExpressionPerInstanceCustomData, x, y)
        e.set_editor_property("data_index", index)
        return e

    rg = MEL.create_material_expression(mat, unreal.MaterialExpressionAppendVector, -600, -100)
    rgb = MEL.create_material_expression(mat, unreal.MaterialExpressionAppendVector, -400, -80)
    MEL.connect_material_expressions(cd(0, -800, -140), "", rg, "A")
    MEL.connect_material_expressions(cd(1, -800, -100), "", rg, "B")
    MEL.connect_material_expressions(rg, "", rgb, "A")
    MEL.connect_material_expressions(cd(2, -800, -60), "", rgb, "B")

    normal = MEL.create_material_expression(mat, unreal.MaterialExpressionVertexNormalWS, -800, 100)
    view = MEL.create_material_expression(mat, unreal.MaterialExpressionCameraVectorWS, -800, 180)
    dot = MEL.create_material_expression(mat, unreal.MaterialExpressionDotProduct, -600, 140)
    MEL.connect_material_expressions(normal, "", dot, "A")
    MEL.connect_material_expressions(view, "", dot, "B")
    facing = MEL.create_material_expression(mat, unreal.MaterialExpressionAbs, -450, 140)
    MEL.connect_material_expressions(dot, "", facing, "")
    shade = MEL.create_material_expression(mat, unreal.MaterialExpressionLinearInterpolate, -300, 140)
    shade.set_editor_property("const_a", 0.3)
    shade.set_editor_property("const_b", 1.2)
    MEL.connect_material_expressions(facing, "", shade, "Alpha")
    mul = MEL.create_material_expression(mat, unreal.MaterialExpressionMultiply, -100, 0)
    MEL.connect_material_expressions(rgb, "", mul, "A")
    MEL.connect_material_expressions(shade, "", mul, "B")
    MEL.connect_material_property(mul, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MEL.recompile_material(mat)
    EAL.save_asset(path, only_if_is_dirty=False)
    log.append(path)
    return mat


# --- per font ---------------------------------------------------------------------------------

def font_ids():
    available = sorted(p.stem for p in (GEN / "MSDF").glob("*.json"))
    return [f for f in available if not ONLY or f in ONLY]


def set_layout(da, meta):
    """Line metrics and kerning, for HVPText's layout. All em."""
    m = meta["metrics"]
    da.set_editor_property("line_height", m["lineHeight"])
    da.set_editor_property("ascender", -m["ascender"])  # the atlas JSON is y down
    da.set_editor_property("descender", m["descender"])
    # Quads are padded by half the distance range on every side (msdf-atlas-gen's plane bounds).
    da.set_editor_property("quad_padding", meta["pxRange"] * 0.5 / meta["emSize"])
    # Keyed as ULetterFontAsset::KerningKey: first character in the high 16 bits.
    da.set_editor_property("kerning", {(ord(pair[0]) & 0xFFFF) << 16 | (ord(pair[1]) & 0xFFFF): value
                                       for pair, value in meta.get("kerning", {}).items()})


def add_mesh_glyph_metrics(fid, glyphs, meta):
    """Glyphs that have a mesh but are not in the MSDF atlas (the dashes and quotes beyond ASCII)
    still need an advance and an ink box for layout. They get a glyph entry built from the mesh
    summary: the quad padded like the atlas's, and no atlas rect, which MSDF reads as "not drawable"."""
    summary_path = GEN / "Mesh" / f"{fid}.json"
    if not summary_path.exists():
        return glyphs
    pad = meta["pxRange"] * 0.5 / meta["emSize"]
    for ch, g in json.loads(summary_path.read_text(encoding="utf-8"))["glyphs"].items():
        if ord(ch) in glyphs or "inkEm" not in g:
            continue
        left, bottom, right, top = g["inkEm"]
        glyph = unreal.LetterGlyph()
        glyph.set_editor_property("uv_rect", vec4f([0, 0, 0, 0]))
        glyph.set_editor_property("quad", vec4f([left - pad, -top - pad, right + pad, -bottom + pad]))  # em, y down
        glyph.set_editor_property("advance", g["advanceEm"])
        glyphs[ord(ch)] = glyph
    return glyphs


def import_glyph_meshes(fid, material):
    """Extruded glyphs from build_glyph_meshes.py: LOD0 imported as the mesh, LOD1/2 imported as
    temporary meshes and copied in (more predictable than importing OBJ files as LODs)."""
    src = GEN / "Mesh" / fid
    if not src.exists():
        return {}
    # The editor subsystem is not available in a commandlet; the older library has the same calls.
    sms = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem) or unreal.EditorStaticMeshLibrary
    folder = f"{font_folder(fid)}/Mesh"
    temp = f"{font_folder(fid)}/_GlyphLodTemp"
    meshes = {}
    for lod0 in sorted(src.glob(f"{fid}_*_LOD0.obj")):
        tag = lod0.stem.split("_")[-2]  # U0041
        ch = chr(int(tag[1:], 16))
        mesh = import_file(lod0, folder, f"{fid}_{tag}_SM")
        if not mesh:
            continue
        for lod in (1, 2):
            lod_file = src / f"{fid}_{tag}_LOD{lod}.obj"
            if lod_file.exists():
                tmp = import_file(lod_file, temp, f"{fid}_{tag}_LOD{lod}")
                if tmp:
                    sms.set_lod_from_static_mesh(mesh, lod, tmp, 0, True)
        if hasattr(sms, "set_lod_screen_sizes"):
            sms.set_lod_screen_sizes(mesh, GLYPH_LOD_SCREEN_SIZES)
        mesh.set_material(0, material)
        EAL.save_asset(mesh.get_path_name(), only_if_is_dirty=False)
        meshes[ord(ch)] = mesh
    if EAL.does_directory_exist(temp):
        EAL.delete_directory(temp)
    return meshes


def delete_unreferenced(paths):
    """Assets a re-import no longer uses (only the font asset referenced them)."""
    for path in paths:
        package = path.split(".")[0]
        if EAL.does_asset_exist(package):
            EAL.delete_asset(package)


def import_font(fid, material):
    meta = json.loads((GEN / "MSDF" / f"{fid}.json").read_text(encoding="utf-8"))
    folder = font_folder(fid)
    da_path = f"{folder}/{fid}_DA"
    old = EAL.load_asset(da_path) if EAL.does_asset_exist(da_path) else None
    stale = set()

    # No atlas: built with build_fonts.py --metrics-only, for glyph meshes alone.
    atlas = mi = None
    png = GEN / "MSDF" / f"{fid}.png"
    if png.exists():
        atlas = import_file(png, folder, f"{fid}_MSDF_T")
        atlas.set_editor_property("srgb", False)
        atlas.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_VECTOR_DISPLACEMENTMAP)
        atlas.set_editor_property("never_stream", True)
        EAL.save_asset(atlas.get_path_name(), only_if_is_dirty=False)

        mi_path = f"{folder}/MI_LetterMSDF_{fid}"
        mi = ensure(mi_path, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
        MEL.set_material_instance_parent(mi, material("msdf"))
        MEL.set_material_instance_texture_parameter_value(mi, "Atlas", atlas)
        MEL.set_material_instance_scalar_parameter_value(mi, "AtlasSize", float(meta["atlas"][0]))
        MEL.set_material_instance_scalar_parameter_value(mi, "PxRange", float(meta["pxRange"]))
        EAL.save_asset(mi_path, only_if_is_dirty=False)
    elif old:
        stale |= {a.get_path_name() for a in (old.get_editor_property("msdf_atlas"), old.get_editor_property("msdf_material")) if a}

    glyphs = {}
    for ch, g in meta["glyphs"].items():
        glyph = unreal.LetterGlyph()
        glyph.set_editor_property("uv_rect", vec4f(g.get("uv", [0, 0, 0, 0])))
        glyph.set_editor_property("quad", vec4f(g.get("quad", [0, 0, 0, 0])))
        glyph.set_editor_property("advance", g["advance"])
        glyphs[ord(ch)] = glyph

    da = old or ensure(da_path, unreal.LetterFontAsset, unreal.DataAssetFactory())
    if old:
        stale |= {m.get_path_name() for m in old.get_editor_property("glyph_meshes").values()}
    da.set_editor_property("display_name", fid)
    da.set_editor_property("cap_height", meta["capHeight"])
    da.set_editor_property("msdf_atlas", atlas)
    da.set_editor_property("msdf_material", mi)
    glyph_meshes = import_glyph_meshes(fid, material("mesh")) if (GEN / "Mesh" / fid).exists() else {}
    da.set_editor_property("glyph_meshes", glyph_meshes)
    da.set_editor_property("glyphs", add_mesh_glyph_metrics(fid, glyphs, meta))
    set_layout(da, meta)
    EAL.save_asset(da_path, only_if_is_dirty=False)
    delete_unreferenced(stale - {m.get_path_name() for m in glyph_meshes.values()})
    log.append(f"{da_path}: {len(glyphs)} glyphs, {len(glyph_meshes)} meshes, {len(meta.get('kerning', {}))} kerning pairs")
    return da


def load_or_build(path, build):
    return EAL.load_asset(path) if EAL.does_asset_exist(path) else build()


# --- modes ------------------------------------------------------------------------------------

def run():
    if option("materials-only"):
        build_msdf_material()
        build_mesh_material()
        return
    if option("layout-only"):
        for fid in font_ids():
            da_path = f"{font_folder(fid)}/{fid}_DA"
            if EAL.does_asset_exist(da_path):
                da = EAL.load_asset(da_path)
                set_layout(da, json.loads((GEN / "MSDF" / f"{fid}.json").read_text(encoding="utf-8")))
                EAL.save_asset(da_path, only_if_is_dirty=False)
                log.append(f"{fid}: {len(da.get_editor_property('kerning'))} kerning pairs")
        return
    if option("meshes-only"):
        mesh_mat = load_or_build(f"{MAT_DIR}/M_LetterMesh", build_mesh_material)
        for fid in font_ids():
            meta = json.loads((GEN / "MSDF" / f"{fid}.json").read_text(encoding="utf-8"))
            da_path = f"{font_folder(fid)}/{fid}_DA"
            da = EAL.load_asset(da_path)
            old = {v.get_path_name() for v in da.get_editor_property("glyph_meshes").values()}
            meshes = import_glyph_meshes(fid, mesh_mat)
            da.set_editor_property("glyph_meshes", meshes)
            da.set_editor_property("glyphs", add_mesh_glyph_metrics(fid, dict(da.get_editor_property("glyphs").items()), meta))
            EAL.save_asset(da_path, only_if_is_dirty=False)
            # Meshes replaced under other names are unreferenced now.
            delete_unreferenced(old - {m.get_path_name() for m in meshes.values()})
            log.append(f"{fid}: {len(meshes)} glyph meshes")
        return
    # Each material only when a font needs it, and only built when missing (or asked for).
    builders = {"msdf": (f"{MAT_DIR}/M_LetterMSDF", build_msdf_material),
                "mesh": (f"{MAT_DIR}/M_LetterMesh", build_mesh_material)}
    made = {}

    def material(kind):
        if kind not in made:
            path, build = builders[kind]
            made[kind] = build() if option("rebuild-materials") else load_or_build(path, build)
        return made[kind]

    if FONT_FOLDER and len(font_ids()) != 1:
        raise RuntimeError("--font-folder is the folder of one font: pass --only=<that font>")
    fonts = [import_font(fid, material) for fid in font_ids()]
    if fonts:
        settings = f"Project Settings > Plugins > HVP Text: Default Font = {fonts[0].get_path_name()}"
        if "mesh" in made:
            settings += f", Glyph Mesh Material = {made['mesh'].get_path_name()}"
        if "msdf" in made:
            settings += ", Quad Mesh = /Engine/BasicShapes/Plane"
        log.append(settings)
    return {"fonts": [f.get_path_name() for f in fonts],
            "glyphMeshMaterial": made["mesh"].get_path_name() if "mesh" in made else None,
            "msdf": "msdf" in made}


def main():
    if not (GEN / "MSDF").exists():
        emit({"ok": False, "error": f"no {GEN / 'MSDF'}: run build_fonts.py first, or pass --generated"})
        return
    try:
        emit({"ok": True, "log": log, **(run() or {})})
    except Exception as e:  # noqa: BLE001 - report, don't swallow
        import traceback
        emit({"ok": False, "error": repr(e), "trace": traceback.format_exc()[-1500:], "log": log})


main()
