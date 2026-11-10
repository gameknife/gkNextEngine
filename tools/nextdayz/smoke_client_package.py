#!/usr/bin/env python3
"""Check a relocated NextDayz client; optionally validate lobby and training UI."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile

from smoke_server_package import unpack


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    parser.add_argument("--launch", action="store_true")
    parser.add_argument("--gnb", type=Path, help="source-built native CLI used to drive the extracted executable")
    parser.add_argument("--timeout", type=int, default=300, help="seconds per scene")
    args = parser.parse_args()
    archive = args.archive.resolve()
    expected = Path(str(archive) + ".sha256").read_text().split()[0]
    with archive.open("rb") as payload:
        if hashlib.file_digest(payload, "sha256").hexdigest() != expected:
            raise RuntimeError("archive checksum mismatch")
    fixture = Path(tempfile.mkdtemp(prefix="nextdayz-client-smoke-"))
    print(f"Smoke evidence: {fixture}", flush=True)
    # gnb's normal launcher locates out/build/<preset>/bin. Extract the WHOLE
    # package there, so the engine still resolves assets beside its actual bin/.
    # No symlink, source assets or source-repository path is available at runtime.
    root = fixture / "out" / "build" / "package"
    root.mkdir(parents=True)
    unpack(archive, root)
    manifest = json.loads((root / "nextdayz-client.json").read_text())
    assert manifest["preset"] == "nextdayz-client"
    for name, digest in manifest["files"].items():
        with (root / name).open("rb") as payload:
            if hashlib.file_digest(payload, "sha256").hexdigest() != digest:
                raise RuntimeError(f"payload checksum mismatch: {name}")
    assets = set((root / "runtime-assets.txt").read_text().splitlines())
    for name in ("assets/scad/scenes/nextdayz_helicopter_lobby.scad", "assets/configs/nextdayz/world_land.json",
                 "assets/sounds/nextdayz/shot_rifle_1.wav", "assets/fonts/Roboto-Regular.ttf"):
        assert name in assets, f"missing client asset: {name}"
    assert any(name.endswith(".spv") for name in assets), "missing shaders"
    servers = json.loads((root / "assets" / "configs" / "nextdayz_servers.json").read_text())
    assert servers["version"] == 1 and servers["servers"], "empty server directory"
    suffix = ".exe" if os.name == "nt" else ""
    assert not (root / "bin" / ("gnb" + suffix)).exists(), "client unexpectedly contains the Master CLI"
    assert (root / ("start-client.bat" if os.name == "nt" else "start-client.sh")).is_file()
    env = os.environ.copy()
    env.pop("GNB_REPO_ROOT", None)
    env["LD_LIBRARY_PATH"] = str(root / "bin") + (":" + env["LD_LIBRARY_PATH"] if env.get("LD_LIBRARY_PATH") else "")
    result = subprocess.run([str(root / "bin" / ("NextDayz" + suffix)), "--help"], cwd=fixture,
                            env=env, capture_output=True, timeout=30)
    (fixture / "client-help.log").write_bytes(result.stdout + result.stderr)
    if result.returncode != 0 or b"--load-scene" not in result.stdout + result.stderr:
        raise RuntimeError(f"client --help failed ({result.returncode}); see {fixture}")
    print("PASS client checksums, assets, server directory and runtime dependencies", flush=True)
    if not args.launch:
        return
    gnb = args.gnb or Path(__file__).resolve().parents[2] / ("gnb" + suffix)
    gnb = gnb.resolve()
    (fixture / "gnb.toml").write_text("", encoding="utf-8")
    script = {
        "name": "nextdayz-client-package", "target": "NextDayz",
        "scene": "assets/scad/scenes/nextdayz_helicopter_lobby.scad",
        "viewport": {"width": 1600, "height": 900},
        "defaults": {"stepTimeoutMs": args.timeout * 1000},
        "args": ["--cvar", "r.rendererType 4", "--cvar", "r.upscaler.type 0", "--cvar", "nextdayz.net.mode offline",
                 "--cvar", "nextdayz.validation.campaign true", "--cvar", "nextdayz.validation.campaignFresh true",
                 "--cvar", "nextdayz.validation.campaignPath out/package-smoke-campaign.json",
                 "--cvar", "nextdayz.ui.sessionMenu true"],
        "steps": [
            {"type": "wait-until", "query": "game.sceneReady", "op": "eq", "value": True, "fatal": True},
            {"type": "assert", "query": "game.cabinScene", "op": "eq", "value": True, "fatal": True},
            {"type": "assert", "query": "game.sessionMenuOpen", "op": "eq", "value": True, "fatal": True},
            {"type": "wait-frames", "n": 8},
            {"type": "screenshot", "out": "screenshots/client-lobby", "ui": True},
            {"type": "click", "at": {"norm": [0.5, 0.34]}, "button": "left"},
            {"type": "wait-until", "query": "game.sessionMode", "op": "eq", "value": "training", "fatal": True},
            {"type": "wait-until", "query": "game.activeMapId", "op": "eq", "value": "training_ground", "fatal": True},
            {"type": "wait-until", "query": "game.sceneReady", "op": "eq", "value": True, "fatal": True},
            {"type": "assert", "query": "game.cabinScene", "op": "eq", "value": False, "fatal": True},
            {"type": "wait-frames", "n": 8},
            {"type": "screenshot", "out": "screenshots/client-training", "ui": True},
            {"type": "quit"},
        ],
    }
    script_path = fixture / "client.agentscript.json"
    script_path.write_text(json.dumps(script, indent=2), encoding="utf-8")
    command = [str(gnb), "--repo-root", str(fixture), "--preset", "package", "validate", "--script", str(script_path)]
    with (fixture / "client-validation.log").open("wb") as log:
        result = subprocess.run(command, cwd=fixture, env=env, stdout=log, stderr=subprocess.STDOUT,
                                timeout=2 * args.timeout + 90)
    if result.returncode != 0:
        tail = (fixture / "client-validation.log").read_text(encoding="utf-8", errors="replace")[-5000:]
        raise RuntimeError(f"relocated client validation failed; see {fixture}\n{tail}")
    print("PASS relocated client lobby, training interaction, rendering and screenshots", flush=True)


if __name__ == "__main__":
    main()
