#!/usr/bin/env python3
"""RMSE curves for assets/agentscripts/upscaler-disocclusion.agentscript.json.

Each phase of that script animates playground.glb, freezes the animation, then captures frames
1, 4, 8, 16 and 64 after the freeze with a static camera, and finally a progressive ground truth
of the frozen scene (`<phase>_ref`, 256 accumulated frames). For every captured frame this prints
the RMSE against the ground truth over the whole image and over the "trail": the pixels that
changed between the last moving shot and the reference, i.e. what the moving objects covered or
uncovered. That is where disocclusion noise lives. Without a `_ref` shot, frame 64 stands in.

    python tools/upscaler_disocclusion_rmse.py <validation run dir or its screenshots dir>
    python tools/upscaler_disocclusion_rmse.py <run dir> --json out.json

Requires numpy and Pillow.
"""

import argparse
import json
import pathlib
import re
import sys

import numpy as np
from PIL import Image

FRAMES = [1, 4, 8, 16, 64]
REFERENCE = 64


def load(path: pathlib.Path) -> np.ndarray:
    return np.asarray(Image.open(path).convert("RGB"), dtype=np.float32) / 255.0


def box_blur(image: np.ndarray, radius: int) -> np.ndarray:
    """Separable box blur of a 2D array, edge-clamped."""
    out = image
    for axis in (0, 1):
        padded = np.pad(out, [(radius, radius) if a == axis else (0, 0) for a in range(2)], mode="edge")
        cumsum = np.cumsum(padded, axis=axis, dtype=np.float64)
        cumsum = np.concatenate(
            [np.zeros_like(np.take(cumsum, [0], axis=axis)), cumsum], axis=axis)
        width = 2 * radius + 1
        out = (np.take(cumsum, range(width, cumsum.shape[axis]), axis=axis) -
               np.take(cumsum, range(0, cumsum.shape[axis] - width), axis=axis)) / width
    return out.astype(np.float32)


def find_shots(directory: pathlib.Path) -> dict:
    """Maps '<phase>_<suffix>' to a file; gnb prefixes each shot with its step number."""
    if (directory / "screenshots").is_dir():
        directory = directory / "screenshots"
    shots = {}
    for path in directory.iterdir():
        if path.suffix.lower() not in (".png", ".jpg", ".jpeg", ".bmp"):
            continue
        match = re.match(r"^(?:\d+_)?(.+?)_(moving|ref|f\d\d)$", path.stem)
        if match:
            shots[f"{match.group(1)}_{match.group(2)}"] = path
    return shots


def rmse(a: np.ndarray, b: np.ndarray, mask=None) -> float:
    squared = ((a - b) ** 2).mean(axis=2)
    if mask is not None:
        if not mask.any():
            return float("nan")
        squared = squared[mask]
    return float(np.sqrt(squared.mean()))


def analyse(shots: dict, phase: str, threshold: float) -> dict:
    reference = load(shots.get(f"{phase}_ref", shots[f"{phase}_f{REFERENCE:02d}"]))
    moving = load(shots[f"{phase}_moving"])
    # Blur before thresholding so per-pixel noise does not leak into the mask, then dilate a
    # little: noise spills a few pixels past the geometric edge of the trail.
    change = box_blur(np.abs(moving - reference).mean(axis=2), 3)
    mask = box_blur((change > threshold).astype(np.float32), 4) > 0.0
    result = {"phase": phase, "trailFraction": float(mask.mean()), "frames": {}}
    for frame in FRAMES:
        key = f"{phase}_f{frame:02d}"
        if key not in shots or (frame == REFERENCE and f"{phase}_ref" not in shots):
            continue
        image = load(shots[key])
        result["frames"][frame] = {
            "full": rmse(image, reference),
            "trail": rmse(image, reference, mask),
        }
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("run", type=pathlib.Path)
    parser.add_argument("--threshold", type=float, default=0.06,
                        help="blurred mean |moving - reference| above which a pixel is trail")
    parser.add_argument("--json", type=pathlib.Path)
    args = parser.parse_args()

    shots = find_shots(args.run)
    phases = sorted({key.rsplit("_", 1)[0] for key in shots
                     if key.endswith(f"_f{REFERENCE:02d}") and key.rsplit("_", 1)[0] + "_moving" in shots})
    if not phases:
        print(f"no complete phases found in {args.run}", file=sys.stderr)
        return 1

    results = [analyse(shots, phase, args.threshold) for phase in phases]
    for result in results:
        print(f"{result['phase']}  (trail = {100.0 * result['trailFraction']:.1f}% of image)")
        print("  frame   full RMSE   trail RMSE")
        for frame, values in result["frames"].items():
            print(f"  {frame:5d}   {values['full']:9.4f}   {values['trail']:10.4f}")
    if args.json:
        args.json.write_text(json.dumps(results, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
