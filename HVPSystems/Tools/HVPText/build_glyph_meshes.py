"""Extruded 3D glyph meshes with LODs, for HVPText's glyph mesh technique.

    python build_glyph_meshes.py                     # every font
    python build_glyph_meshes.py --only Cinzel-700
    python build_glyph_meshes.py --fonts <ttf folder> --out <generated folder>

For each font and each character of MESH_CHARS the font has, writes
<out>/Mesh/<Font>/<Font>_U<code>_LOD<n>.obj (U0041 for A: named by code point, as Windows and Unreal
names ignore case and some characters cannot be file names) and a <Font>.json summary with each
glyph's advance and ink box, so text can be laid out with glyphs the MSDF atlas lacks.
import_fonts.py turns them into static meshes (LOD0 plus imported LOD1/2) and records them on the
font asset.

Geometry is in the letter frame, in Unreal axes: +X is toward the viewer (the front face), +Y is the
glyph's right as seen by the viewer (checked on device: -Y mirrored it), +Z is up. 1 em = 100 units,
so an instance scales by its em size in cm / 100. The origin is the centre of the glyph's ink box
horizontally and the middle of the capital height vertically, matching where MSDF centres a letter.

Requires fonttools, numpy and mapbox-earcut (requirements.txt).
"""
import argparse
import json
import math
import pathlib

import mapbox_earcut as earcut
import numpy as np
from fontTools.pens.basePen import BasePen
from fontTools.ttLib import TTFont

import paths

DEPTH_EM = 0.12                        # extrusion depth, em (about 17% of a 0.7 em capital)
LOD_TOLERANCE_EM = [0.002, 0.008, 0.03]  # max curve deviation per LOD, em
SMOOTH_ANGLE_DEG = 35.0                # side-wall edges meeting flatter than this share a normal
UNITS_PER_EM = 100.0

# What a keyboard types (printable ASCII; the space has no ink), plus the few typographic marks text
# commonly needs beyond it: en and em dash, curly quotes, ellipsis, bullet, middle dot, degree, times.
MESH_CHARS = "".join(chr(c) for c in range(0x21, 0x7F)) + "–—‘’“”…•·°×"


class FlatteningPen(BasePen):
    """Collects contours as point lists, flattening curves to a deviation tolerance (font units)."""

    def __init__(self, glyph_set, tolerance):
        super().__init__(glyph_set)
        self.tol = tolerance
        self.contours = []
        self.current = None

    def _moveTo(self, p):
        self.current = [p]

    def _lineTo(self, p):
        self.current.append(p)

    def _qCurveToOne(self, p1, p2):
        p0 = self.current[-1]
        d = math.hypot(p0[0] - 2 * p1[0] + p2[0], p0[1] - 2 * p1[1] + p2[1])
        n = max(1, min(48, math.ceil(math.sqrt(d / (4 * self.tol))))) if d > 0 else 1
        for i in range(1, n + 1):
            t = i / n
            a, b, c = (1 - t) ** 2, 2 * (1 - t) * t, t * t
            self.current.append((a * p0[0] + b * p1[0] + c * p2[0], a * p0[1] + b * p1[1] + c * p2[1]))

    def _curveToOne(self, p1, p2, p3):
        p0 = self.current[-1]
        d = max(math.hypot(p0[0] - 2 * p1[0] + p2[0], p0[1] - 2 * p1[1] + p2[1]),
                math.hypot(p1[0] - 2 * p2[0] + p3[0], p1[1] - 2 * p2[1] + p3[1]))
        n = max(1, min(48, math.ceil(math.sqrt(3 * d / (4 * self.tol))))) if d > 0 else 1
        for i in range(1, n + 1):
            t = i / n
            mt = 1 - t
            a, b, c, e = mt ** 3, 3 * mt * mt * t, 3 * mt * t * t, t ** 3
            self.current.append((a * p0[0] + b * p1[0] + c * p2[0] + e * p3[0],
                                 a * p0[1] + b * p1[1] + c * p2[1] + e * p3[1]))

    def _closePath(self):
        pts = self.current
        if len(pts) > 1 and math.isclose(pts[0][0], pts[-1][0]) and math.isclose(pts[0][1], pts[-1][1]):
            pts = pts[:-1]
        # Drop consecutive duplicates; earcut and the side walls both dislike zero-length edges.
        clean = [pts[0]]
        for p in pts[1:]:
            if math.hypot(p[0] - clean[-1][0], p[1] - clean[-1][1]) > 1e-6:
                clean.append(p)
        if len(clean) >= 3:
            self.contours.append(np.array(clean, dtype=np.float64))
        self.current = None

    _endPath = _closePath


def signed_area(c):
    x, y = c[:, 0], c[:, 1]
    return 0.5 * float(np.dot(x, np.roll(y, -1)) - np.dot(np.roll(x, -1), y))


def point_in_polygon(p, poly):
    x, y = p
    inside = False
    n = len(poly)
    for i in range(n):
        x1, y1 = poly[i]
        x2, y2 = poly[(i + 1) % n]
        if (y1 > y) != (y2 > y) and x < (x2 - x1) * (y - y1) / (y2 - y1) + x1:
            inside = not inside
    return inside


def group_contours(contours):
    """Outer contours with their holes. Classified by winding direction, the way the font defines
    them: outers share the orientation of the largest contour, holes run the other way. (Even-odd
    containment fails on glyphs drawn as overlapping strokes, such as Cormorant's Y, where every
    contour sits inside another.) Outers come back counter-clockwise and holes clockwise (y up)."""
    areas = [signed_area(c) for c in contours]
    outer_sign = math.copysign(1.0, areas[max(range(len(areas)), key=lambda i: abs(areas[i]))])
    shapes, holes = [], []
    for c, a in zip(contours, areas):
        if math.copysign(1.0, a) == outer_sign:
            shapes.append({"outer": c if a > 0 else c[::-1].copy(), "holes": [], "area": abs(a)})
        else:
            holes.append(c if a < 0 else c[::-1].copy())
    for hole in holes:
        owners = [s for s in shapes if point_in_polygon(hole[0], s["outer"])]
        if owners:
            min(owners, key=lambda s: s["area"])["holes"].append(hole)
    return shapes


class MeshWriter:
    def __init__(self):
        self.v, self.vn, self.vt, self.faces = [], [], [], []

    def vert(self, pos, normal, uv):
        self.v.append(pos); self.vn.append(normal); self.vt.append(uv)
        return len(self.v)  # OBJ is 1-based

    def tri(self, a, b, c, want_normal):
        # Wind so the geometric normal (right-handed cross product) agrees with the intended one.
        pa, pb, pc = (np.array(self.v[i - 1]) for i in (a, b, c))
        if np.dot(np.cross(pb - pa, pc - pa), want_normal) < 0:
            b, c = c, b
        self.faces.append((a, b, c))

    def write(self, path):
        with open(path, "w", encoding="ascii") as f:
            for p in self.v:
                f.write("v %.5f %.5f %.5f\n" % p)
            for t in self.vt:
                f.write("vt %.5f %.5f\n" % t)
            for n in self.vn:
                f.write("vn %.5f %.5f %.5f\n" % n)
            for a, b, c in self.faces:
                f.write(f"f {a}/{a}/{a} {b}/{b}/{b} {c}/{c}/{c}\n")


def build_glyph(shapes, cx, cy, upm):
    """Glyph-space (font units, y up) shapes -> letter-frame mesh."""
    s = UNITS_PER_EM / upm
    half = DEPTH_EM * UNITS_PER_EM * 0.5
    to3 = lambda gx, gy, x: (x, (gx - cx) * s, (gy - cy) * s)
    mesh = MeshWriter()
    cos_smooth = math.cos(math.radians(SMOOTH_ANGLE_DEG))

    for shape in shapes:
        rings = [shape["outer"]] + shape["holes"]
        flat = np.concatenate(rings)
        ends = np.cumsum([len(r) for r in rings]).astype(np.uint32)
        idx = earcut.triangulate_float64(flat, ends).reshape(-1, 3)
        for side, nx in ((half, 1.0), (-half, -1.0)):
            base = [mesh.vert(to3(px, py, side), (nx, 0.0, 0.0), (px / upm, py / upm)) for px, py in flat]
            for a, b, c in idx:
                mesh.tri(base[a], base[b], base[c], np.array([nx, 0.0, 0.0]))

        for ring in rings:
            n = len(ring)
            edge_n = []
            for i in range(n):
                (x1, y1), (x2, y2) = ring[i], ring[(i + 1) % n]
                dx, dy = x2 - x1, y2 - y1
                L = math.hypot(dx, dy) or 1.0
                edge_n.append((dy / L, -dx / L))  # outward for CCW outers and CW holes
            u = 0.0
            for i in range(n):
                j = (i + 1) % n
                ne = edge_n[i]

                def vertex_normal(k, this_edge, other_edge):
                    if this_edge[0] * other_edge[0] + this_edge[1] * other_edge[1] > cos_smooth:
                        mx, my = this_edge[0] + other_edge[0], this_edge[1] + other_edge[1]
                        L = math.hypot(mx, my) or 1.0
                        return (mx / L, my / L)
                    return this_edge

                n0 = vertex_normal(i, ne, edge_n[i - 1])
                n1 = vertex_normal(j, ne, edge_n[j])
                (x1, y1), (x2, y2) = ring[i], ring[j]
                seg = math.hypot(x2 - x1, y2 - y1) / upm
                to_n = lambda g: (0.0, g[0], g[1])
                a = mesh.vert(to3(x1, y1, half), to_n(n0), (u, 0.0))
                b = mesh.vert(to3(x2, y2, half), to_n(n1), (u + seg, 0.0))
                c = mesh.vert(to3(x2, y2, -half), to_n(n1), (u + seg, DEPTH_EM))
                d = mesh.vert(to3(x1, y1, -half), to_n(n0), (u, DEPTH_EM))
                want = np.array(to_n(ne))
                mesh.tri(a, b, c, want)
                mesh.tri(a, c, d, want)
                u += seg
    return mesh


def build_font(ttf: pathlib.Path, out: pathlib.Path):
    fid = ttf.stem
    out = out / "Mesh"
    tt = TTFont(str(ttf))
    upm = tt["head"].unitsPerEm
    cap = tt["OS/2"].sCapHeight
    gs = tt.getGlyphSet()
    cmap = tt.getBestCmap()
    out_dir = out / fid
    out_dir.mkdir(parents=True, exist_ok=True)
    for stale in out_dir.glob("*.obj"):  # earlier runs may have named files differently
        stale.unlink()
    hmtx = tt["hmtx"]
    summary = {"font": fid, "depthEm": DEPTH_EM, "unitsPerEm": UNITS_PER_EM, "capHeight": cap / upm,
               "lodToleranceEm": LOD_TOLERANCE_EM, "glyphs": {}}
    for ch in MESH_CHARS:
        name = cmap.get(ord(ch))
        if not name:
            continue
        tag = f"U{ord(ch):04X}"
        tris = []
        bounds = None
        for lod, tol_em in enumerate(LOD_TOLERANCE_EM):
            pen = FlatteningPen(gs, tol_em * upm)
            gs[name].draw(pen)
            if not pen.contours:
                break
            allpts = np.concatenate(pen.contours)
            if bounds is None:  # centre on LOD0's ink box so every LOD shares one origin
                bounds = (allpts[:, 0].min(), allpts[:, 0].max())
                ink_y = (allpts[:, 1].min(), allpts[:, 1].max())
            cx = 0.5 * (bounds[0] + bounds[1])
            mesh = build_glyph(group_contours(pen.contours), cx, cap * 0.5, upm)
            mesh.write(out_dir / f"{fid}_{tag}_LOD{lod}.obj")
            tris.append(len(mesh.faces))
        if bounds is not None:
            summary["glyphs"][ch] = {
                "file": tag, "widthEm": float(bounds[1] - bounds[0]) / upm, "triangles": tris,
                "advanceEm": hmtx[name][0] / upm,
                # Ink box from the pen position on the baseline, em, y up: left, bottom, right, top.
                "inkEm": [float(bounds[0]) / upm, float(ink_y[0]) / upm, float(bounds[1]) / upm, float(ink_y[1]) / upm]}
    (out / f"{fid}.json").write_text(json.dumps(summary, indent=1), encoding="utf-8")
    t = [g["triangles"] for g in summary["glyphs"].values()]
    return {"font": fid, "glyphs": len(t), "avgTris": [round(sum(x[i] for x in t) / len(t)) for i in range(3)],
            "maxTris": [max(x[i] for x in t) for i in range(3)]}


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--only", nargs="*", help="font ids (file stems) to build")
    ap.add_argument("--fonts", type=pathlib.Path, default=paths.default_fonts(), help="folder of TTFs (any depth)")
    ap.add_argument("--out", type=pathlib.Path, default=paths.default_out(), help="generated output folder")
    args = ap.parse_args()
    ttfs = paths.find_ttfs(args.fonts, args.only)
    if not ttfs:
        raise SystemExit(f"no TTFs under {args.fonts}")
    print(json.dumps([build_font(t, args.out) for t in ttfs], indent=1))
