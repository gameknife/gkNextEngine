"""Top-down planning view of a NextDayz geographic map.

Draws the scaled DEM (hillshade + slope), exact water, the OSM reference
(buildings, roads, landuse) and every layout.scad control layer on one PNG so a
level designer or agent can plan settlements, land cover and routes against the
real place before touching SCAD, and review the result afterwards.

    python tools/nextdayz/geomap_preview.py --map cz_loket_riverbend
    python tools/nextdayz/geomap_preview.py --map cz_loket_riverbend --no-osm --out out/nextdayz/plan.png

Grid lines every 100 m (labels in metres, +x east, +y north, origin = tile centre).
Needs Pillow. Reads external/geocache/<map>/parts/p0_0/part.json when present.
"""
import argparse
import json
import math
import re
import struct
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[2]
LANDCOVER = {
    "spruce": (24, 64, 40), "mixed": (52, 92, 44), "beech": (70, 112, 48), "pine": (88, 100, 52),
    "beetle": (128, 92, 60), "riparian": (40, 110, 90), "orchard": (150, 170, 70), "field": (196, 176, 96),
    "meadow": (128, 170, 84), "clearing": (160, 140, 90),
}


def read_hmap(path):
    blob = path.read_bytes()
    _, cols, rows = struct.unpack("<III", blob[4:16])
    ox, oy, cx, cy, scale, bias = struct.unpack("<6f", blob[16:40])
    raw = struct.unpack("<%dh" % (cols * rows), blob[40:40 + 2 * cols * rows])
    return cols, rows, ox, oy, cx, cy, [v * scale + bias for v in raw]


def scad_array(text, name):
    clean = re.sub(r"//[^\n]*", "", text)
    m = re.search(r"\b" + re.escape(name) + r"\s*=\s*(\[.*?\])\s*;", clean, re.S)
    return json.loads(m.group(1)) if m else []


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--map", default="cz_loket_riverbend")
    ap.add_argument("--out", default="")
    ap.add_argument("--px", type=float, default=1.4, help="pixels per metre")
    ap.add_argument("--no-osm", action="store_true", help="hide the OSM reference layer")
    args = ap.parse_args()

    mdir = ROOT / "assets/scad/proc/nextdayz" / args.map
    gen = json.loads((mdir / "generation.json").read_text())
    size = gen.get("sizeM", 1000)
    half = size / 2
    px = args.px
    W = int(size * px)
    base = (mdir / "base.scad").read_text()
    m = re.search(r'"gkterr1",\s*\[[^\]]*\],\s*\[[^\]]*\],\s*-?\d+,\s*\[[^\]]*\],\s*([-\d.]+)', base)
    water = float(m.group(1)) if m else -1e9
    cols, rows, ox, oy, cx, cy, H = read_hmap(mdir / "terrain.hmap")
    lo, hi = min(H), max(H)

    def P(p):
        return ((p[0] + half) * px, (half - p[1]) * px)

    def h_at(i, j):
        return H[max(0, min(rows - 1, j)) * cols + max(0, min(cols - 1, i))]

    img = Image.new("RGB", (W, W))
    cell = max(2, int(math.ceil(cx * px)) + 1)
    for j in range(rows):
        for i in range(cols):
            h = h_at(i, j)
            dx = (h_at(i + 1, j) - h_at(i - 1, j)) / (2 * cx)
            dy = (h_at(i, j + 1) - h_at(i, j - 1)) / (2 * cy)
            slope = math.degrees(math.atan(math.hypot(dx, dy)))
            shade = max(0.35, min(1.15, 0.85 + 0.9 * (-dx * 0.7 + dy * 0.7)))
            t = (h - lo) / max(1e-6, hi - lo)
            c = (90 + 110 * t, 110 + 90 * t, 70 + 60 * t)
            if slope > 22:
                c = (150, 120, 100)
            if h < water - 0.02:
                c, shade = (50, 100, 170), 1.0
            x0, y0 = P([ox + i * cx, oy + j * cy])
            img.paste(tuple(int(min(255, v * shade)) for v in c), (int(x0 - cell / 2), int(y0 - cell / 2), int(x0 + cell / 2), int(y0 + cell / 2)))
    d = ImageDraw.Draw(img)

    part = ROOT / "external/geocache" / args.map / "parts/p0_0/part.json"
    if part.exists() and not args.no_osm:
        osm = json.loads(part.read_text())
        for a in osm.get("landuse", []):
            d.polygon([P(p) for p in a["outer"]], outline=(220, 220, 255))
        for r in osm.get("roads", []):
            major = r["class"] in ("secondary", "tertiary", "residential", "unclassified", "living_street", "primary")
            d.line([P(p) for p in r["pts"]], fill=(235, 235, 235) if major else (150, 150, 150), width=2 if major else 1)
        for b in osm.get("buildings", []):
            d.polygon([P(p) for p in b["outer"]], fill=(170, 60, 50))

    layout = (mdir / "layout.scad").read_text()
    for lc in scad_array(layout, "PCG_LANDCOVER"):
        col = LANDCOVER.get(lc[1], (200, 200, 200))
        d.polygon([P(p) for p in lc[2]], outline=col + (255,), width=3)
        cxm = sum(p[0] for p in lc[2]) / len(lc[2]); cym = sum(p[1] for p in lc[2]) / len(lc[2])
        d.text(P([cxm, cym]), f"{lc[1]}:{lc[0]}", fill=col)
    for v in scad_array(layout, "PCG_VEGETATION"):
        b = v[1]
        d.rectangle([P([b[0], b[3]]), P([b[2], b[1]])], outline=(60, 160, 60))
    for al in scad_array(layout, "PCG_ALLEYS"):
        d.line([P(p) for p in al[0]], fill=(120, 220, 90), width=3)
    for path in scad_array(layout, "PCG_PATHS"):
        d.line([P(p) for p in path[1]], fill=(255, 210, 120), width=max(2, int(path[2] * px * 0.5)))
    for c in scad_array(layout, "PCG_CLEAR"):
        d.line([P(c[0]), P(c[1])], fill=(255, 255, 255), width=max(1, int(c[2] * px)))
    for s in scad_array(layout, "PCG_SITES"):
        (x, y), (w, hgt), a = s[3], s[4], math.radians(s[5])
        pts = [(x + u * math.cos(a) - v * math.sin(a), y + u * math.sin(a) + v * math.cos(a))
               for u, v in ((-w / 2, -hgt / 2), (w / 2, -hgt / 2), (w / 2, hgt / 2), (-w / 2, hgt / 2))]
        risk = (255, 230, 0) if s[7] == 1 else (255, 140, 0) if s[7] == 2 else (255, 40, 40)
        d.polygon([P(p) for p in pts], outline=risk, width=3)
        d.text(P([x - w / 2, y + hgt / 2 + 14]), f"{s[0]} [{s[2]}] x{s[6]}", fill=risk)
    for p in scad_array(layout, "PCG_SPAWNS"):
        q = P(p); d.ellipse([q[0] - 7, q[1] - 7, q[0] + 7, q[1] + 7], outline=(80, 255, 80), width=3)
    for p in scad_array(layout, "PCG_EXTRACTIONS"):
        q = P(p); d.rectangle([q[0] - 14, q[1] - 14, q[0] + 14, q[1] + 14], outline=(0, 255, 255), width=3)
    for b in scad_array(layout, "PCG_BRIDGES"):
        d.line([P(b[0]), P(b[1])], fill=(255, 255, 255), width=4)
    for k in range(-int(half), int(half) + 1, 100):
        d.line([P([k, -half]), P([k, half])], fill=(0, 0, 0))
        d.line([P([-half, k]), P([half, k])], fill=(0, 0, 0))
        d.text(P([k + 3, -half + 14]), str(k), fill=(0, 0, 0))
        d.text(P([-half + 3, k + 12]), str(k), fill=(0, 0, 0))
    out = Path(args.out) if args.out else ROOT / "out/nextdayz" / f"{args.map}_plan.png"
    out.parent.mkdir(parents=True, exist_ok=True)
    img.save(out)
    print(f"{out}  (relief {lo:.1f}..{hi:.1f} m, water {water:.2f} m)")


if __name__ == "__main__":
    main()
