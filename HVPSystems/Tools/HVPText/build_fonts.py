"""MSDF atlases for HVPText: every TTF under the fonts folder -> an atlas and its metrics.

    python build_fonts.py                      # every font
    python build_fonts.py --only Cinzel-700 Inter-800
    python build_fonts.py --kerning-only       # refresh kerning, keep the atlases
    python build_fonts.py --fonts <ttf folder> --out <generated folder> --msdf <msdf-atlas-gen.exe>

Writes <out>/MSDF/<Font>.png and <Font>.json. One MTSDF atlas per font: RGB is a multi-channel
distance field (sharp corners), A is a true distance field (smooth, far-reaching falloff for glow).
The JSON holds per-glyph atlas UVs, quad bounds in em and advances, the font's line metrics and its
pair kerning, so letters are laid out and kerned without the TTF. import_fonts.py makes the font
assets from it.

Requires fonttools (requirements.txt) and msdf-atlas-gen (see paths.py).
"""
import argparse
import json
import pathlib
import subprocess

from fontTools.ttLib import TTFont

import paths
from kerning import gpos_kerning

# Printable ASCII: enough for words, numbers and punctuation.
CHARSET = "".join(chr(c) for c in range(0x20, 0x7F))

# MSDF atlas settings. EM_SIZE is the glyph size in atlas pixels; PX_RANGE is the distance-field
# range in atlas pixels. A wide range is what makes a soft glow possible from the alpha channel:
# the field only carries information PX_RANGE/2 pixels beyond the edge.
EM_SIZE = 64
PX_RANGE = 16


def build_msdf(ttf: pathlib.Path, out: pathlib.Path, exe: pathlib.Path) -> dict:
    out_dir = out / "MSDF"
    out_dir.mkdir(parents=True, exist_ok=True)
    name = ttf.stem
    png, raw_json = out_dir / f"{name}.png", out_dir / f"{name}.raw.json"
    charset_file = out_dir / "_charset.txt"
    # msdf-atlas-gen charset syntax: a quoted string of characters, with " and \ escaped.
    charset_file.write_text('"' + CHARSET.replace("\\", "\\\\").replace('"', '\\"') + '"', encoding="utf-8")
    cmd = [str(exe), "-font", str(ttf), "-charset", str(charset_file),
           "-type", "mtsdf", "-format", "png", "-imageout", str(png), "-json", str(raw_json),
           "-size", str(EM_SIZE), "-pxrange", str(PX_RANGE), "-pots", "-yorigin", "top"]
    subprocess.run(cmd, check=True, capture_output=True)

    raw = json.loads(raw_json.read_text(encoding="utf-8"))
    w, h = raw["atlas"]["width"], raw["atlas"]["height"]
    glyphs = {}
    for g in raw["glyphs"]:
        ch = chr(g["unicode"])
        entry = {"advance": g["advance"]}
        if "atlasBounds" in g:  # whitespace has no quad
            a, p = g["atlasBounds"], g["planeBounds"]
            entry["uv"] = [a["left"] / w, a["top"] / h, a["right"] / w, a["bottom"] / h]
            entry["quad"] = [p["left"], p["top"], p["right"], p["bottom"]]  # em units, y down
        glyphs[ch] = entry
    # msdf-atlas-gen reads only the legacy 'kern' table, empty for modern fonts: take GPOS too.
    kerning = {f'{chr(k["unicode1"])}{chr(k["unicode2"])}': k["advance"] for k in raw.get("kerning", [])}
    kerning = kerning or gpos_kerning(ttf, CHARSET)
    # Cap height in em, so a letter is sized by its capital height in cm.
    tt = TTFont(str(ttf))
    cap_em = tt["OS/2"].sCapHeight / tt["head"].unitsPerEm
    result = {"font": name, "atlas": [w, h], "emSize": EM_SIZE, "pxRange": PX_RANGE, "capHeight": cap_em,
              "metrics": raw["metrics"], "glyphs": glyphs, "kerning": kerning}
    (out_dir / f"{name}.json").write_text(json.dumps(result, indent=1), encoding="utf-8")
    raw_json.unlink()
    return {"font": name, "atlas": f"{w}x{h}", "glyphs": len(glyphs), "kerning_pairs": len(kerning)}


def patch_kerning(ttf: pathlib.Path, out: pathlib.Path) -> dict:
    """Rewrite only the kerning of an existing MSDF JSON, leaving its atlas untouched."""
    path = out / "MSDF" / f"{ttf.stem}.json"
    data = json.loads(path.read_text(encoding="utf-8"))
    data["kerning"] = gpos_kerning(ttf, CHARSET)
    path.write_text(json.dumps(data, indent=1), encoding="utf-8")
    return {"font": ttf.stem, "kerning_pairs": len(data["kerning"])}


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--only", nargs="*", help="font ids (file stems) to build")
    ap.add_argument("--kerning-only", action="store_true", help="only refresh the kerning in existing MSDF JSONs")
    ap.add_argument("--fonts", type=pathlib.Path, default=paths.default_fonts(), help="folder of TTFs (any depth)")
    ap.add_argument("--out", type=pathlib.Path, default=paths.default_out(), help="generated output folder")
    ap.add_argument("--msdf", help="msdf-atlas-gen executable")
    args = ap.parse_args()

    ttfs = paths.find_ttfs(args.fonts, args.only)
    if not ttfs:
        raise SystemExit(f"no TTFs under {args.fonts}")
    if args.kerning_only:
        print(json.dumps([patch_kerning(t, args.out) for t in ttfs], indent=1))
        return
    exe = paths.msdf_atlas_gen(args.msdf)
    print(json.dumps([build_msdf(t, args.out, exe) for t in ttfs], indent=1))


if __name__ == "__main__":
    main()
