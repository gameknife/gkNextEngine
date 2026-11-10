"""Waits for the running ScadPlay game to rebuild its level after an edit.

    python3 .agents/skills/scadplay/scripts/wait_build.py <build-serial-before-the-edit> [timeout-seconds]

Prints the build result (and any gameplay warnings or C# faults) once build.serial has moved past
the one given and no build is in flight. Exit code: 0 built, 1 build failed, 2 timed out or no
game running.
"""

import json
import sys
import time

from scadplay_context import CONTEXT, game_running, read_context, summarize


def main():
    before = int(sys.argv[1]) if len(sys.argv) > 1 else -1
    timeout = float(sys.argv[2]) if len(sys.argv) > 2 else 20.0

    deadline = time.monotonic() + timeout
    context = None
    while time.monotonic() < deadline:
        context = read_context()
        build = (context or {}).get("build") or {}
        if build.get("serial", -1) > before and not build.get("building"):
            print(json.dumps(summarize(context), ensure_ascii=False, indent=2))
            return 0 if build.get("ok") else 1
        if context is not None and not game_running(context):
            break
        time.sleep(0.2)

    if context is None:
        print("no context file: ScadPlay is not running (%s)" % CONTEXT)
    elif not game_running(context):
        print("ScadPlay (pid %s) is not running any more; the edit applies next time it starts" % context.get("pid"))
    else:
        print("timed out after %.0fs; build serial is still %s. The game may be paused or minimised, "
              "or the file was saved without a change." % (timeout, (context.get("build") or {}).get("serial")))
    return 2


if __name__ == "__main__":
    sys.exit(main())
