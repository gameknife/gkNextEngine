#!/usr/bin/env python3
"""Steady-state temporal noise for assets/agentscripts/upscaler-flicker.agentscript.json.

Each phase of that script renders a frozen playground.glb with a static camera, lets the upscaler
converge, captures 8 consecutive frames (`<phase>_s0` .. `<phase>_s7`) and a progressive ground
truth (`<phase>_ref`, 256 accumulated frames). For every phase this prints

  flicker   RMS of the frame-to-frame difference, averaged over the 7 consecutive pairs: what the
            eye reads as boiling / shimmering on a still image
  ref RMSE  RMSE of each frame against the reference, averaged: residual noise plus bias

    python tools/upscaler_flicker_rmse.py <validation run dir or its screenshots dir>
    python tools/upscaler_flicker_rmse.py <run dir> --baseline <older run dir>

Requires numpy and Pillow.
"""

import argparse
import pathlib
import re
import sys

import numpy as np
from PIL import Image


def load(path: pathlib.Path) -> np.ndarray:
    return np.asarray(Image.open(path).convert("RGB"), dtype=np.float32) / 255.0


def rmse(a: np.ndarray, b: np.ndarray) -> float:
    return float(np.sqrt(np.mean((a - b) ** 2)))


def find_shots(directory: pathlib.Path) -> dict:
    """Maps phase -> {'s': {index: path}, 'ref': path}; gnb prefixes each shot with its step number."""
    if (directory / "screenshots").is_dir():
        directory = directory / "screenshots"
    phases = {}
    for path in directory.iterdir():
        if path.suffix.lower() not in (".png", ".jpg", ".jpeg", ".bmp"):
            continue
        match = re.match(r"^(?:\d+_)?(.+?)_(ref|s(\d+))$", path.stem)
        if not match:
            continue
        phase = phases.setdefault(match.group(1), {"s": {}, "ref": None})
        if match.group(2) == "ref":
            phase["ref"] = path
        else:
            phase["s"][int(match.group(3))] = path
    return phases


def score(directory: pathlib.Path) -> dict:
    result = {}
    for name, shots in sorted(find_shots(directory).items()):
        frames = [load(shots["s"][i]) for i in sorted(shots["s"])]
        if len(frames) < 2:
            continue
        flicker = float(np.mean([rmse(frames[i], frames[i + 1]) for i in range(len(frames) - 1)]))
        reference = None
        if shots["ref"] is not None:
            ref = load(shots["ref"])
            reference = float(np.mean([rmse(frame, ref) for frame in frames]))
        result[name] = {"flicker": flicker, "ref": reference}
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("run", type=pathlib.Path)
    parser.add_argument("--baseline", type=pathlib.Path)
    args = parser.parse_args()

    current = score(args.run)
    if not current:
        print(f"no upscaler-flicker shots under {args.run}", file=sys.stderr)
        return 1
    baseline = score(args.baseline) if args.baseline else {}

    def fmt(value):
        return "     -" if value is None else f"{value:.4f}"

    header = f"{'phase':<16}{'flicker':>9}{'ref RMSE':>10}"
    if baseline:
        header += f"{'base flicker':>14}{'base ref':>10}{'flicker d%':>12}"
    print(header)
    for name, values in current.items():
        line = f"{name:<16}{fmt(values['flicker']):>9}{fmt(values['ref']):>10}"
        if name in baseline:
            base = baseline[name]
            change = (values["flicker"] / base["flicker"] - 1.0) * 100.0 if base["flicker"] > 0 else 0.0
            line += f"{fmt(base['flicker']):>14}{fmt(base['ref']):>10}{change:>11.1f}%"
        print(line)
    return 0


if __name__ == "__main__":
    sys.exit(main())
