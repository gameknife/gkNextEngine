"""Review shots of a NextDayz geographic map draft: overview + every settlement.

Writes a tiny wrapper scene per view (a camera first, then the draft scene by
absolute path, so the source tree is rendered without syncing the runtime copy)
and captures it with `gnb shot`. Use it after every layout / kit change and look
at each image: the overview judges land cover and settlement placement, the
aerial and street views judge planning, regional buildings and vegetation.

    python tools/nextdayz/geomap_shots.py --map cz_loket_riverbend
    python tools/nextdayz/geomap_shots.py --map cz_loket_riverbend --sites old_town,north_village --views aerial,street

Images land in out/nextdayz/shots/<map>/<view>.jpg (overwritten each run).
"""
import argparse
import json
import math
import re
import shutil
import struct
import subprocess
import sys
from pathlib import Path

from PIL import Image, ImageStat

ROOT = Path(__file__).resolve().parents[2]


def is_review_image(path):
    """Reject a clear-only frame captured during the first GPU scene upload."""
    if not path.exists():
        return False
    with Image.open(path) as img:
        return max(ImageStat.Stat(img.convert("RGB")).stddev) > 1.0


def scad_array(text, name):
    clean = re.sub(r"//[^\n]*", "", text)
    m = re.search(r"\b" + re.escape(name) + r"\s*=\s*(\[.*?\])\s*;", clean, re.S)
    return json.loads(m.group(1)) if m else []


def height_sampler(path):
    blob = path.read_bytes()
    _, cols, rows = struct.unpack("<III", blob[4:16])
    ox, oy, cx, cy, scale, bias = struct.unpack("<6f", blob[16:40])
    raw = struct.unpack("<%dh" % (cols * rows), blob[40:40 + 2 * cols * rows])

    def h(x, y):
        i = max(0, min(cols - 1, round((x - ox) / cx)))
        j = max(0, min(rows - 1, round((y - oy) / cy)))
        return raw[j * cols + i] * scale + bias
    return h


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--map", default="cz_loket_riverbend")
    ap.add_argument("--sites", default="", help="comma-separated site ids (default: all)")
    ap.add_argument("--views", default="overview,aerial,street", help="overview, aerial, street")
    ap.add_argument("--frames", type=int, default=50)
    args = ap.parse_args()

    mdir = ROOT / "assets/scad/proc/nextdayz" / args.map
    layout = (mdir / "layout.scad").read_text(encoding="utf-8")
    sites = scad_array(layout, "PCG_SITES")
    wanted = set(filter(None, args.sites.split(",")))
    views = set(args.views.split(","))
    h = height_sampler(mdir / "terrain.hmap")
    out = ROOT / "out/nextdayz/shots" / args.map
    out.mkdir(parents=True, exist_ok=True)
    scene = (mdir / "scene.scad").resolve().as_posix()
    camlib = (ROOT / "assets/scad/lib/gk_camera.scad").resolve().as_posix()

    shots = []
    if "overview" in views and not wanted:
        shots.append(("overview", [-560, -640, 430], [0, 0, 25], 46))
        shots.append(("overview_north", [420, 640, 360], [0, 40, 25], 46))
    for s in sites:
        if wanted and s[0] not in wanted:
            continue
        (x, y), (w, d), a = s[3], s[4], math.radians(s[5])
        r = max(w, d)
        g = h(x, y)
        if "aerial" in views:
            shots.append((f"{s[0]}_aerial", [x - 0.55 * r, y - 0.85 * r, g + 0.62 * r + 12], [x, y, g + 3], 52))
        if "street" in views:
            # Eye height inside the public space of each settlement kind (site-local
            # eye and target), never inside a building row.
            lane = -(max(20, min(36, d * 0.26)) / 2 - 3)
            local = {
                "square": ((-w * 0.18, -d * 0.12), (w * 0.3, d * 0.1)),
                "naves": ((-w * 0.38, lane + 1.5), (w * 0.3, 0)),
                "street": ((-w * 0.45, 0), (w * 0.3, 0)),
                "suburb": ((-w * 0.47, 0), (w * 0.3, 0)),
                "villas": ((-w * 0.47, 0), (w * 0.3, 0)),
                "chaty": ((-w * 0.45, 0), (w * 0.2, d * 0.1)),
                "hokkaido_lane": ((-w * 0.45, 0), (w * 0.3, 0)),
                "shotengai": ((-w * 0.45, 0), (w * 0.3, 0)),
                "canal": ((-w * 0.43, -9), (w * 0.3, -4)),
                "portyard": ((-w * 0.43, 0), (w * 0.3, 0)),
                "shrine": ((0, -d * 0.43), (0, 12)),
            }.get(s[2], ((0, -d / 2 - 12), (0, 0)))
            (lx, ly), (tx, ty) = local
            ex, ey = x + lx * math.cos(a) - ly * math.sin(a), y + lx * math.sin(a) + ly * math.cos(a)
            px_, py_ = x + tx * math.cos(a) - ty * math.sin(a), y + tx * math.sin(a) + ty * math.cos(a)
            shots.append((f"{s[0]}_street", [ex, ey, h(ex, ey) + 1.8], [px_, py_, h(px_, py_) + 3.0], 62))

    gnb = ROOT / ("gnb.bat" if sys.platform == "win32" else "gnb.sh")
    capture = ROOT / "out/build" / ("windows" if sys.platform == "win32" else "linux") / "screenshots/agent_validation.jpg"
    failed = []
    for name, eye, target, fov in shots:
        wrapper = out / f"{name}.scad"
        wrapper.write_text(f'use <{camlib}>\ngk_camera_lookat({eye},{target},"{name}",{fov});\ninclude <{scene}>\n', encoding="utf-8")
        for attempt in range(3):
            frames = args.frames if attempt == 0 else max(180, args.frames * (attempt + 1))
            res = subprocess.run([str(gnb), "shot", "--scene", wrapper.resolve().as_posix(), "--frames", str(frames),
                                  "--reason", f"geomap review {args.map} {name}"], cwd=ROOT, capture_output=True, text=True)
            if res.returncode != 0 or is_review_image(capture):
                break
            print(f"{name}: clear-only capture; retrying after more stable frames")
        warn = [ln for ln in res.stdout.splitlines() if "SCAD: loaded" in ln or "[error]" in ln]
        if res.returncode != 0 or not is_review_image(capture):
            print(f"{name}: FAILED\n{res.stdout[-2000:]}{res.stderr[-2000:]}")
            failed.append(name)
            continue
        shutil.copyfile(capture, out / f"{name}.jpg")
        print(f"{out / (name + '.jpg')}  {warn[-1].split('SCAD: ')[-1] if warn else ''}")
    if failed:
        raise SystemExit(f"Review incomplete: {', '.join(failed)}")


if __name__ == "__main__":
    main()
