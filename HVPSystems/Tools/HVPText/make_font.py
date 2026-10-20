"""Make one HVPText font from one font file, inside the editor: what a Font Face's "Make HVPText
Font..." runs (HVPSystemsEditor). The steps the other tools in this folder take by hand, in one go:

    build_fonts.py (--metrics-only without MSDF) -> build_glyph_meshes.py -> import_fonts.py

    <this file> --work=<folder> --id=Gotham-500 --font-folder=/Game/.../Gotham-500
                --materials-folder=/Game/.../Materials [--msdf] [--meshes]

<work>/Fonts/<id>.ttf is the font (TTF or OTF data); <work>/Generated takes the tools' output, and
<work>/result.json the outcome: {"ok", "error", "fonts", "glyphMeshMaterial", "msdf", "log"}.

The build scripts run in the editor's own Python. Its fonttools, numpy and mapbox-earcut are
HVPSystems' PythonRequirements, which the editor installs at startup.
"""
import json
import pathlib
import runpy
import sys
import traceback

import unreal

TOOLS = pathlib.Path(__file__).resolve().parent


def option(name, default=None):
    """--name=value from the script's arguments; True for a bare --name."""
    for arg in sys.argv[1:]:
        if arg == f"--{name}":
            return True
        if arg.startswith(f"--{name}="):
            return arg.split("=", 1)[1]
    return default


def run_tool(script, args):
    """Run one of the tools as if from the command line. Their failures are SystemExit messages."""
    argv = sys.argv
    sys.argv = [str(TOOLS / script)] + args
    try:
        return runpy.run_path(str(TOOLS / script), run_name="__main__")
    except SystemExit as e:
        if e.code not in (None, 0):
            raise RuntimeError(f"{script}: {e.code}") from None
        return {}
    finally:
        sys.argv = argv


def check_packages(meshes):
    missing = []
    for module, package in [("fontTools", "fonttools")] + ([("numpy", "numpy"), ("mapbox_earcut", "mapbox-earcut")] if meshes else []):
        try:
            __import__(module)
        except ImportError:
            missing.append(package)
    if missing:
        raise RuntimeError(
            f"The font tools' Python packages are not installed ({', '.join(missing)}). HVPSystems lists them as "
            "PythonRequirements, which the editor installs when it starts (it needs network access once): restart "
            "the editor, and if they are still missing, look for pip errors from LogPython in the Output Log.")


def make():
    work = pathlib.Path(option("work"))
    fid = option("id")
    msdf, meshes = bool(option("msdf")), bool(option("meshes"))
    if not (msdf or meshes):
        raise RuntimeError("nothing to make: ask for MSDF, glyph meshes or both")
    check_packages(meshes)
    if str(TOOLS) not in sys.path:
        sys.path.insert(0, str(TOOLS))  # the tools import paths.py and kerning.py from here

    fonts, gen = work / "Fonts", work / "Generated"
    common = ["--fonts", str(fonts), "--out", str(gen), "--only", fid]
    steps = [("Building the MSDF atlas" if msdf else "Reading the font's layout",
              lambda: run_tool("build_fonts.py", common + ([] if msdf else ["--metrics-only"])))]
    if meshes:
        steps.append(("Building glyph meshes", lambda: run_tool("build_glyph_meshes.py", common)))
    imported = {}

    def import_assets():
        result = run_tool("import_fonts.py", [f"--generated={gen}", f"--only={fid}",
                                              f"--font-folder={option('font-folder')}",
                                              f"--materials-folder={option('materials-folder')}"]).get("RESULT")
        if not result or not result.get("ok"):
            raise RuntimeError((result or {}).get("error", "import_fonts.py reported nothing"))
        imported.update(result)

    steps.append(("Importing", import_assets))
    with unreal.ScopedSlowTask(len(steps), f"Making HVPText font {fid}") as task:
        task.make_dialog(False)
        for label, step in steps:
            task.enter_progress_frame(1, f"{fid}: {label}")
            step()
    return imported


def main():
    out = pathlib.Path(option("work")) / "result.json"
    try:
        result = make()
    except Exception as e:  # noqa: BLE001 - the editor shows it
        result = {"ok": False, "error": str(e), "trace": traceback.format_exc()[-1500:]}
        unreal.log_error(f"HVPText make_font: {e}")
    out.write_text(json.dumps(result, indent=1, default=str), encoding="utf-8")


main()
