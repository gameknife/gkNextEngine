#!/usr/bin/env python3
"""Require all Win64/Linux client/server archives and checksums before release."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import tarfile
import zipfile


def verify_packages(directory: Path, version: str) -> list[Path]:
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]*", version):
        raise ValueError(f"invalid package version: {version!r}")
    expected = {}
    for role in ("server", "client"):
        for platform, system, suffix in (("win64", "windows", ".zip"), ("linux64", "linux", ".tar.gz")):
            name = f"NextDayz-{role}_{platform}_{version}{suffix}"
            expected[name] = (role, system)
    required = set(expected) | {name + ".sha256" for name in expected}
    actual = {path.name for path in directory.iterdir()}
    if required != actual:
        raise ValueError(f"incomplete Release assets: missing={sorted(required - actual)}, unexpected={sorted(actual - required)}")
    for name, (role, system) in expected.items():
        archive = directory / name
        checksum = (directory / (name + ".sha256")).read_text(encoding="utf-8").split()
        if len(checksum) != 2 or checksum[1] != name or not re.fullmatch(r"[0-9a-fA-F]{64}", checksum[0]):
            raise ValueError(f"invalid SHA-256 sidecar for {name}")
        with archive.open("rb") as payload:
            if hashlib.file_digest(payload, "sha256").hexdigest() != checksum[0].lower():
                raise ValueError(f"SHA-256 mismatch: {name}")
        marker = f"nextdayz-{role}.json"
        if archive.suffix == ".zip":
            with zipfile.ZipFile(archive) as package:
                manifest = json.loads(package.read(marker))
        else:
            with tarfile.open(archive, "r:gz") as package:
                member = package.extractfile(marker)
                if member is None:
                    raise ValueError(f"missing manifest: {name}")
                with member:
                    manifest = json.load(member)
        for key, value in {"formatVersion": 1, "preset": f"nextdayz-{role}", "version": version, "os": system, "arch": "amd64"}.items():
            if manifest.get(key) != value:
                raise ValueError(f"{name}: {key}={manifest.get(key)!r}, expected {value!r}")
    return [directory / name for name in sorted(required)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--version", required=True)
    args = parser.parse_args()
    files = verify_packages(args.directory, args.version)
    print(f"PASS complete Release: 4 archives, 4 verified SHA-256 files ({len(files)} assets)")


if __name__ == "__main__":
    main()
