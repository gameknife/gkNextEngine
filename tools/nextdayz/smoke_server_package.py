#!/usr/bin/env python3
"""Verify a server archive outside the checkout; optionally dispatch real DSs."""
import argparse
from contextlib import ExitStack
import hashlib
import json
import os
from pathlib import Path
import signal
import socket
import subprocess
import tarfile
import tempfile
import time
import urllib.error
import urllib.request
import zipfile


def unpack(archive, root):
    def check(name):
        path = (root / name).resolve()
        if not path.is_relative_to(root.resolve()):
            raise RuntimeError(f"unsafe archive path: {name}")

    if archive.suffix == ".zip":
        with zipfile.ZipFile(archive) as package:
            for name in package.namelist():
                check(name)
            package.extractall(root)
    else:
        with tarfile.open(archive, "r:gz") as package:
            for member in package.getmembers():
                check(member.name)
                if not member.isfile() and not member.isdir():
                    raise RuntimeError(f"unexpected archive entry: {member.name}")
            package.extractall(root, filter="data")


BROWSER_PASSWORD = "local-package-smoke-password"


def seed_browser_account(root, baked):
    """Resume at the training site's extraction pad to keep the browser test short."""
    directory = root / "web" / "game-assets"
    file = baked["maps"]["training_ground"]["files"]["anchors.json"]["path"]
    anchors = json.loads((directory / file).read_text())["anchors"]
    site = next(anchor for anchor in anchors if anchor["type"] == "extraction")
    x, y, z = site["position"]
    data = root / "data"
    data.mkdir(exist_ok=True)
    (data / "master.json").write_text(json.dumps({
        "version": 1, "nextId": 2, "nextSortie": 2,
        "accounts": {"1": {"id": 1, "name": "package-browser", "storage": [], "sorties": 1,
                           "character": {"map": "training_ground", "position": [x, y + 1.25, z],
                                         "sortie": 1, "inventory": [{"id": "water_bottle_empty", "count": 1}]}}},
        "worlds": {},
    }), encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    parser.add_argument("--launch-ds", action="store_true", help="dispatch every published map and wait for its DS heartbeat")
    parser.add_argument("--web", action="store_true", help="play the production Web bundle with a real Chromium client (Linux)")
    parser.add_argument("--timeout", type=int, default=300, help="seconds per map")
    args = parser.parse_args()
    archive = args.archive.resolve()
    expected = Path(str(archive) + ".sha256").read_text().split()[0]
    with archive.open("rb") as payload:
        if hashlib.file_digest(payload, "sha256").hexdigest() != expected:
            raise RuntimeError("archive checksum mismatch")
    # Intentionally outside the checkout: repository discovery must not mask a
    # missing bundle path, missing asset or non-relocatable library dependency.
    root = Path(tempfile.mkdtemp(prefix="nextdayz-server-smoke-"))
    print(f"Smoke evidence: {root}", flush=True)
    unpack(archive, root)
    manifest = json.loads((root / "nextdayz-server.json").read_text())
    assert manifest["preset"] == "nextdayz-server"
    for name, digest in manifest["files"].items():
        with (root / name).open("rb") as payload:
            if hashlib.file_digest(payload, "sha256").hexdigest() != digest:
                raise RuntimeError(f"payload checksum mismatch: {name}")
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        port = listener.getsockname()[1]
    # Reserve disjoint UDP ranges while choosing the ports.
    def udp_range(held, count):
        while True:
            with ExitStack() as attempt:
                try:
                    first = attempt.enter_context(socket.socket(socket.AF_INET, socket.SOCK_DGRAM))
                    first.bind(("127.0.0.1", 0))
                    base = first.getsockname()[1]
                    if base + count > 65536:
                        continue
                    for port in range(base + 1, base + count):
                        udp = attempt.enter_context(socket.socket(socket.AF_INET, socket.SOCK_DGRAM))
                        udp.bind(("127.0.0.1", port))
                except OSError:
                    continue
                held.enter_context(attempt.pop_all())
                return base

    with ExitStack() as held:
        udp_base = udp_range(held, 4)
        rtc_base = udp_range(held, 4)
    password = None
    if (root / "web" / "release.json").is_file():
        password = BROWSER_PASSWORD
    if args.web:
        assert password is not None, "Linux archive is missing the Web release"
        baked = json.loads((root / "web" / "game-assets" / "manifest.json").read_text())
        seed_browser_account(root, baked)
    suffix = ".exe" if os.name == "nt" else ""
    env = os.environ.copy()
    for name in ("GNB_REPO_ROOT", "GK_RUNTIME_ROOT"):
        env.pop(name, None)
    env["LD_LIBRARY_PATH"] = str(root / "bin") + (":" + env["LD_LIBRARY_PATH"] if env.get("LD_LIBRARY_PATH") else "")
    command = [str(root / "bin" / ("gnb" + suffix)), "master", "serve", "--listen", f"0.0.0.0:{port}",
               "--public-host", "127.0.0.1", "--port-base", str(udp_base), "--max-servers", "4"]
    if password is not None:
        # The launcher applies Web/password defaults, with an isolated test UDP range.
        command = [str(root / "start-master.sh"), "--listen", f"127.0.0.1:{port}",
                   "--public-host", "127.0.0.1", "--port-base", str(udp_base),
                   "--rtc-port-base", str(rtc_base), "--max-servers", "4"]
    log = (root / "master.log").open("w", encoding="utf-8")
    process = subprocess.Popen(command, cwd=root, env=env, stdout=log, stderr=subprocess.STDOUT,
                               creationflags=subprocess.CREATE_NEW_PROCESS_GROUP if os.name == "nt" else 0)
    base_url = f"http://127.0.0.1:{port}"
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))

    def call(path, body=None, token=None):
        headers = {"Content-Type": "application/json"}
        if token:
            headers["Authorization"] = f"Bearer {token}"
        request = urllib.request.Request(base_url + path, json.dumps(body or {}).encode(), headers)
        with opener.open(request, timeout=10) as response:
            return json.load(response)

    def login(name):
        body = {"name": name}
        if password is not None:
            body["password"] = password
        return call("/api/login", body)

    def wait_for(check, timeout):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if process.poll() is not None:
                raise RuntimeError(f"Master exited with {process.returncode}; see {root}")
            try:
                value = check()
                if value:
                    return value
            except (urllib.error.URLError, OSError):
                pass
            time.sleep(0.5)
        raise RuntimeError(f"timed out; see logs in {root}")

    completed = False
    try:
        wait_for(lambda: call("/admin/status"), 30)
        session = login("package-smoke")
        profile = call("/api/profile", token=session["token"])
        assert profile["maps"], "packaged map registry is empty"
        if args.launch_ds:
            for index, map_info in enumerate(profile["maps"]):
                account = login(f"package-map-{index}")
                deploy = call("/api/deploy", {"map": map_info["id"]}, account["token"])
                assert deploy["host"] == "127.0.0.1"
                server_id = deploy["server"]

                def ready():
                    servers = call("/admin/status")["servers"]
                    server = next(s for s in servers if s["id"] == server_id)
                    if server["state"] == "dead":
                        raise RuntimeError(f"DS {server_id} failed; see {root / 'data' / 'logs'}")
                    return server["state"] == "ready"

                wait_for(ready, args.timeout)
                print(f"PASS {map_info['id']}: {server_id} registered and ready on UDP {deploy['port']}", flush=True)
                if args.web or index + 1 < len(profile["maps"]):
                    call("/admin/stop", {"server": server_id})
                    wait_for(lambda: any(s["id"] == server_id and s["state"] == "dead" for s in call("/admin/status")["servers"]), 45)
        if args.web:
            web_test = Path(__file__).resolve().parents[2] / "web" / "nextdayz" / "test" / "server-package.mjs"
            subprocess.run(["node", str(web_test), base_url, str(root)], cwd=root, check=True,
                           env={**env, "ND_PACKAGE_TEST_PASSWORD": password}, timeout=420)
        assert (root / "data" / "master.json").is_file(), "default persistent data path is missing"
        print("PASS standalone Master, login/profile, assets and checksums", flush=True)
        completed = True
    finally:
        if process.poll() is None:
            if os.name == "nt":
                process.send_signal(signal.CTRL_BREAK_EVENT)
            else:
                process.terminate()
            try:
                process.wait(timeout=40)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
        log.close()
    if completed:
        if process.returncode != 0:
            raise RuntimeError(f"Master did not shut down gracefully: {process.returncode}; see {root}")
        if args.launch_ds:
            master_log = (root / "master.log").read_text(encoding="utf-8")
            if "master shutting down" not in master_log or "killed at shutdown" in master_log:
                raise RuntimeError(f"Master failed to stop its live DS gracefully; see {root}")
        # Restart the same extracted bundle and read the persistent account.
        with (root / "master-restart.log").open("w", encoding="utf-8") as restart_log:
            process = subprocess.Popen(command, cwd=root, env=env, stdout=restart_log, stderr=subprocess.STDOUT,
                                       creationflags=subprocess.CREATE_NEW_PROCESS_GROUP if os.name == "nt" else 0)
            try:
                wait_for(lambda: call("/admin/status"), 30)
                restored = call("/api/profile", token=session["token"])
                assert restored["playerId"] == session["playerId"], "account was not persisted across restart"
            finally:
                if process.poll() is None:
                    if os.name == "nt":
                        process.send_signal(signal.CTRL_BREAK_EVENT)
                    else:
                        process.terminate()
                    try:
                        process.wait(timeout=40)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()
            if process.returncode != 0:
                raise RuntimeError(f"restarted Master failed: {process.returncode}; see {root}")
        print("PASS graceful shutdown and persisted account after restart", flush=True)


if __name__ == "__main__":
    main()
