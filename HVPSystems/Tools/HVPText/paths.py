"""Where the HVPText font tools read and write. Shared by the scripts in this folder.

By default a project keeps its TTFs under Art/Fonts (any depth, one file per weight, named by the
font id: Cinzel-700.ttf) and the tools write to Art/Generated. Every script takes --fonts and --out
to point elsewhere.
"""
import os
import pathlib
import shutil

TOOLS = pathlib.Path(__file__).resolve().parent


def project_root() -> pathlib.Path:
    """The nearest folder above the tools holding a .uproject; else the working directory."""
    for folder in TOOLS.parents:
        if any(folder.glob("*.uproject")):
            return folder
    return pathlib.Path.cwd()


def default_fonts() -> pathlib.Path:
    return project_root() / "Art" / "Fonts"


def default_out() -> pathlib.Path:
    return project_root() / "Art" / "Generated"


def find_ttfs(fonts: pathlib.Path, only=None) -> list:
    """Every TTF under fonts, or those whose id (file stem) is in only."""
    ttfs = sorted(fonts.rglob("*.ttf"))
    return [t for t in ttfs if not only or t.stem in only]


def msdf_atlas_gen(explicit=None) -> pathlib.Path:
    """msdf-atlas-gen (MIT, Viktor Chlumsky, https://github.com/Chlumsky/msdf-atlas-gen). v1.4 for
    Windows is committed in bin/msdf-atlas-gen/ with its licence; --msdf or MSDF_ATLAS_GEN picks
    another, and the PATH is the last resort."""
    candidates = [explicit, os.environ.get("MSDF_ATLAS_GEN"),
                  TOOLS / "bin" / "msdf-atlas-gen" / "msdf-atlas-gen.exe",
                  TOOLS / "bin" / "msdf-atlas-gen.exe",
                  shutil.which("msdf-atlas-gen")]
    for c in candidates:
        if c and pathlib.Path(c).is_file():
            return pathlib.Path(c)
    raise SystemExit("msdf-atlas-gen not found: download v1.4 from https://github.com/Chlumsky/msdf-atlas-gen/releases "
                     f"into {TOOLS / 'bin'}, or pass --msdf / set MSDF_ATLAS_GEN")
