"""Check every control of a NextDayz map layout against the terrain in one pass.

`gnb geo expedition` stops at the first invalid site, so placing fifteen sites by
trial costs fifteen runs. This reads the same data (the geocache tile and the
recipe) and reports all of them at once, with the numbers needed to fix each:

    python tools/nextdayz/geomap_check.py --map ke_talek_mara
    python tools/nextdayz/geomap_check.py --map ke_talek_mara --probe 120,-80,60,46,30
    python tools/nextdayz/geomap_check.py --map ke_talek_mara --rivers

Per site: height span over the pad (limit 12 m, or 20 m for terraced kinds),
distance to the nearest carved river, distance to the map edge and to the other
sites. Spawns and extractions get the same test on their own pads. --probe tests
one candidate rectangle x,y,w,h,rotation without editing the layout; --rivers
prints the river centrelines the recipe will carve. Works before the first
expedition run: the layout is then read from the recipe's `controls` template.
"""
import argparse
import json
import math
import re
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TERRACED = {"naves", "homestead", "street", "suburb", "villas", "chaty", "hokkaido_lane", "shotengai", "canal",
            "portyard", "shrine", "sv_trading", "sv_plots", "sv_camp"}


def scad_array(text, name):
    clean = re.sub(r"//[^\n]*", "", text)
    m = re.search(r"\b" + re.escape(name) + r"\s*=\s*(\[.*?\])\s*;", clean, re.S)
    return json.loads(m.group(1)) if m else []


def read_hmap(path):
    blob = path.read_bytes()
    _, cols, rows = struct.unpack("<III", blob[4:16])
    ox, oy, cx, cy, scale, bias = struct.unpack("<6f", blob[16:40])
    raw = struct.unpack("<%dh" % (cols * rows), blob[40:40 + 2 * cols * rows])

    def h(x, y):
        fx = max(0.0, min(cols - 1.001, (x - ox) / cx))
        fy = max(0.0, min(rows - 1.001, (y - oy) / cy))
        i, j = int(fx), int(fy)
        u, v = fx - i, fy - j
        s = lambda a, b: raw[b * cols + a] * scale + bias
        return (s(i, j) * (1 - u) + s(i + 1, j) * u) * (1 - v) + (s(i, j + 1) * (1 - u) + s(i + 1, j + 1) * u) * v
    return h


def seg_dist(p, a, b):
    dx, dy = b[0] - a[0], b[1] - a[1]
    l2 = dx * dx + dy * dy
    t = 0 if l2 == 0 else max(0, min(1, ((p[0] - a[0]) * dx + (p[1] - a[1]) * dy) / l2))
    return math.hypot(p[0] - a[0] - t * dx, p[1] - a[1] - t * dy)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--map", required=True)
    ap.add_argument("--probe", default="", help="x,y,w,h,rotation of one candidate pad")
    ap.add_argument("--rivers", action="store_true", help="print the carved river centrelines")
    args = ap.parse_args()

    recipe = json.loads((ROOT / "assets/scad/specs/nextdayz" / f"{args.map}.json").read_text(encoding="utf-8"))
    gp = recipe["gameplay"]
    relief = gp.get("reliefScale", 1.0)
    osm = json.loads((ROOT / "external/geocache" / args.map / "parts/p0_0/part.json").read_text(encoding="utf-8"))
    half = osm.get("sizeM", 1000) / 2
    height = read_hmap(ROOT / "assets/geo" / args.map / "terrain.hmap")
    rules = gp.get("waterways", {})
    wet = [(w["pts"], rules[w["class"]]["width"], w.get("name") or w["class"]) for w in (osm.get("waterways") or [])
           if w["class"] in rules and not rules[w["class"]].get("dry")]
    terrain = osm.get("terrain") or {}
    water_level = terrain.get("waterLevel") if terrain.get("hasWater") else None

    def river(p):
        return min([min(seg_dist(p, a, b) for a, b in zip(pts, pts[1:])) - width * 0.55 for pts, width, _ in wet], default=math.inf)

    if args.rivers:
        for pts, width, name in wet:
            inside = [[round(p[0]), round(p[1])] for p in pts if abs(p[0]) <= half and abs(p[1]) <= half]
            print(f"{name} (width {width}): {inside[::max(1, len(inside) // 40)]}")
        return

    def pad(at, size, rot, limit):
        a = math.radians(rot)
        lo, hi, near, issues = math.inf, -math.inf, math.inf, []
        for i in range(-2, 3):
            for j in range(-2, 3):
                u, v = i * size[0] / 4, j * size[1] / 4
                p = (at[0] + u * math.cos(a) - v * math.sin(a), at[1] + u * math.sin(a) + v * math.cos(a))
                if abs(p[0]) > half - 8 or abs(p[1]) > half - 8:
                    issues.append("outside map")
                z = height(*p)
                lo, hi, near = min(lo, z), max(hi, z), min(near, river(p))
                if water_level is not None and z < water_level + 0.25 / relief:
                    issues.append("in water")
        span = (hi - lo) * relief
        if span > limit:
            issues.append(f"span {span:.1f} m > {limit}")
        if near < 4:
            issues.append(f"river {near:.0f} m")
        return span, near, sorted(set(issues))

    if args.probe:
        x, y, w, h, rot = [float(v) for v in args.probe.split(",")]
        for limit in (12, 20):
            span, near, issues = pad((x, y), (w, h), rot, limit)
            print(f"probe {w:.0f}x{h:.0f}@{rot:.0f} at {x:.0f},{y:.0f}: span {span:.1f} m (limit {limit}), river {near:.0f} m  {'; '.join(issues) or 'ok'}")
        return

    layout = ROOT / "assets/scad/proc/nextdayz" / args.map / "layout.scad"
    if not layout.exists():
        layout = ROOT / gp["controls"]
    text = layout.read_text(encoding="utf-8")
    sites = scad_array(text, "PCG_SITES")
    bad = 0
    print(f"{layout.relative_to(ROOT)}: relief x{relief}, {len(wet)} carved river(s), {sum(s[6] for s in sites)} buildings "
          f"(recipe budget {gp.get('buildingBudget')})")
    for i, s in enumerate(sites):
        span, near, issues = pad(s[3], s[4], s[5], 20 if s[2] in TERRACED else 12)
        ex = lambda q: ((q[4][0] * abs(math.cos(math.radians(q[5]))) + q[4][1] * abs(math.sin(math.radians(q[5])))) / 2,
                        (q[4][0] * abs(math.sin(math.radians(q[5]))) + q[4][1] * abs(math.cos(math.radians(q[5])))) / 2)
        for q in sites[:i]:
            (px, py), (qx, qy) = ex(s), ex(q)
            if abs(s[3][0] - q[3][0]) < px + qx + 5 and abs(s[3][1] - q[3][1]) < py + qy + 5:
                issues.append(f"overlaps {q[0]}")
        bad += bool(issues)
        print(f"  {'!!' if issues else 'ok'} {s[0]:<16} {s[2]:<12} x{s[6]:<2} span {span:4.1f} m  river {near:5.0f} m  {'; '.join(issues)}")
    for name, size, clear in (("PCG_SPAWNS", 14, 35), ("PCG_EXTRACTIONS", 42, 0)):
        for p in scad_array(text, name):
            span, near, issues = pad(p, (size, size), 0, 12)
            if clear:
                for s in sites:
                    if math.hypot(p[0] - s[3][0], p[1] - s[3][1]) < math.hypot(s[4][0], s[4][1]) / 2 + clear:
                        issues.append(f"too close to {s[0]}")
            bad += bool(issues)
            print(f"  {'!!' if issues else 'ok'} {name[4:-1].lower():<16} {str(p):<15} span {span:4.1f} m  river {near:5.0f} m  {'; '.join(issues)}")
    for b in scad_array(text, "PCG_BRIDGES"):
        ends = [river(b[0]), river(b[1])]
        ok = min(ends) >= 2
        bad += not ok
        print(f"  {'ok' if ok else '!!'} bridge {b[0]} -> {b[1]} span {math.hypot(b[0][0] - b[1][0], b[0][1] - b[1][1]):.0f} m, banks {ends[0]:.0f} / {ends[1]:.0f} m from the channel")
    print("all controls fit" if not bad else f"{bad} control(s) need moving")


if __name__ == "__main__":
    main()
