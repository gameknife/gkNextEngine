#!/usr/bin/env python3
"""Verify an extracted default release against its checkout (Python 3.11+)."""
import argparse
import json
from pathlib import Path
import subprocess
import tomllib


def verify(repo: Path, staging: Path) -> None:
    config = tomllib.loads((repo / "gnb.toml").read_text(encoding="utf-8"))
    preset = config["package"]["presets"]["default"]
    manifest = json.loads((staging / "package.manifest.json").read_text(encoding="utf-8"))
    if manifest["preset"] != "default" or manifest["targets"] != preset["targets"]:
        raise ValueError("Packaged targets do not match the default release preset")

    # Compare every tracked loose asset byte-for-byte, including SCAD libraries,
    # duplicate basenames, catalog.json, all levels, and post-startup game data.
    checked = 0
    for rel in preset["extra_files"]:
        if not rel.startswith("assets/"):
            continue
        source_rel = rel.removeprefix("assets/") if rel.startswith("assets/projects/") else rel
        tracked = subprocess.check_output(
            ["git", "ls-files", "-z", "--", source_rel], cwd=repo
        ).decode("utf-8").split("\0")
        paths = [p for p in tracked if p]
        # Optional paks are downloaded by gnb setup, not tracked in Git.
        if not paths and source_rel.endswith(".pak") and (repo / source_rel).is_file():
            paths = [source_rel]
        if not paths:
            raise ValueError(f"No tracked release sources for {rel}")
        for path in paths:
            destination = "assets/" + path if path.startswith("projects/") else path
            source = repo / path
            packaged = staging / destination
            if not packaged.is_file() or source.read_bytes() != packaged.read_bytes():
                raise ValueError(f"Missing or changed loose release file: {destination}")
            checked += 1
    print(f"Verified {len(preset['targets'])} targets and {checked} loose source files")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("staging", type=Path, help="gnb smoke --keep --staging directory")
    args = parser.parse_args()
    verify(Path(__file__).resolve().parents[2], args.staging.resolve())
