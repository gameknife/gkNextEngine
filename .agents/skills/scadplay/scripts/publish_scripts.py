"""Republishes ScadPlay's C# gameplay into the running game and waits for the hot reload.

    python3 .agents/skills/scadplay/scripts/publish_scripts.py [timeout-seconds]

Compiles projects/ScadPlay/Scripts and publishes it where the running game loaded it from
(scripts.publish_dir in the context). The game notices the new assembly within half a second,
saves its state, reloads, rebuilds the level and puts the player back. Prints compile errors on
failure (exit 1); on success prints the reload/build summary, including any C# exceptions the new
code threw (exit 0). Exit 2: the game is not running (the code is still compiled and published,
so it is what the next launch uses) or the reload timed out.
"""

import glob
import json
import os
import shutil
import subprocess
import sys
import time

from scadplay_context import PROJECT, REPO, game_running, read_context, summarize


def find_dotnet():
    names = ["dotnet.exe", "dotnet"] if os.name == "nt" else ["dotnet"]
    for name in names:
        bundled = os.path.join(REPO, "external", "dotnet", name)
        if os.path.isfile(bundled):
            return bundled
    found = shutil.which("dotnet")
    if found:
        return found
    sys.exit("publish_scripts.py: no dotnet SDK (run `gnb dotnet setup`, or put dotnet on PATH)")


def default_publish_dir():
    # Newest build tree that already has ScadPlay published, i.e. the one `gnb build ScadPlay` made.
    candidates = glob.glob(os.path.join(REPO, "out", "build", "*", "bin", "csharp", "scadplay", "ScadPlay.dll"))
    if not candidates:
        return ""
    return os.path.dirname(max(candidates, key=os.path.getmtime))


def compile_errors(output):
    seen = []
    for line in output.splitlines():
        if (": error " in line or ": warning " in line) and line.strip() not in seen:
            seen.append(line.strip())
    return seen


def main():
    timeout = float(sys.argv[1]) if len(sys.argv) > 1 else 40.0
    context = read_context()
    running = game_running(context)
    publish_dir = ((context or {}).get("scripts") or {}).get("publish_dir") if running else ""
    publish_dir = publish_dir or default_publish_dir()
    if not publish_dir:
        sys.exit("publish_scripts.py: ScadPlay has never been built here; run `./gnb.sh build ScadPlay` first")

    before_scripts = ((context or {}).get("scripts") or {}).get("serial", -1)
    before_build = ((context or {}).get("build") or {}).get("serial", -1)

    command = [find_dotnet(), "publish", PROJECT, "-c", "Release", "-o", publish_dir, "--nologo",
               "-v", "quiet", "-clp:NoSummary"]
    environment = dict(os.environ, DOTNET_CLI_TELEMETRY_OPTOUT="1", DOTNET_NOLOGO="1")
    result = subprocess.run(command, capture_output=True, text=True, env=environment, cwd=REPO)
    if result.returncode != 0:
        errors = compile_errors(result.stdout + result.stderr)
        print("C# build failed; the running game keeps the previous code.")
        print("\n".join(errors) if errors else (result.stdout + result.stderr)[-4000:])
        return 1
    print("published to %s" % publish_dir)

    if not running:
        print("ScadPlay is not running; the new code loads next time it starts.")
        return 2

    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        context = read_context()
        scripts = (context or {}).get("scripts") or {}
        build = (context or {}).get("build") or {}
        if scripts.get("serial", -1) > before_scripts and build.get("serial", -1) > before_build \
                and not build.get("building"):
            print(json.dumps(summarize(context), ensure_ascii=False, indent=2))
            return 0 if build.get("ok") else 1
        if context is not None and not game_running(context):
            print("ScadPlay exited during the reload")
            return 2
        time.sleep(0.25)

    print("timed out after %.0fs waiting for the hot reload (scripts.serial still %s). Is the game "
          "running with hot reload (CoreCLR backend, manifest hotReload: true)?"
          % (timeout, ((read_context() or {}).get("scripts") or {}).get("serial")))
    return 2


if __name__ == "__main__":
    sys.exit(main())
