"""Shared helpers for the scadplay skill scripts: find the repository, read the game's context."""

import json
import os
import sys


def find_repo_root():
    # Walk up from this script, so the skill works from any working directory and when the skill
    # folder is copied under another agent's skills directory.
    here = os.path.dirname(os.path.abspath(__file__))
    while True:
        if os.path.isfile(os.path.join(here, "AGENTS.md")) and os.path.isdir(os.path.join(here, "projects")):
            return here
        parent = os.path.dirname(here)
        if parent == here:
            sys.exit("scadplay: no repository root (AGENTS.md + projects/) above %s" % os.path.abspath(__file__))
        here = parent


REPO = find_repo_root()
CONTEXT = os.path.join(REPO, "out", "scadplay", "context.json")
PROJECT = os.path.join(REPO, "projects", "ScadPlay", "Scripts", "ScadPlay.csproj")


def read_context():
    # The game replaces the file by rename, so a read gets the old or the new one, never half.
    try:
        with open(CONTEXT, encoding="utf-8") as handle:
            return json.load(handle)
    except (OSError, ValueError):
        return None


def game_running(context):
    if not context or not context.get("pid"):
        return False
    pid = int(context["pid"])
    if os.name == "nt":
        import ctypes
        handle = ctypes.windll.kernel32.OpenProcess(0x1000, False, pid)
        if not handle:
            return False
        ctypes.windll.kernel32.CloseHandle(handle)
        return True
    try:
        os.kill(pid, 0)
        return True
    except ProcessLookupError:
        return False
    except PermissionError:
        return True


def summarize(context):
    """The parts of the context an agent reports back after an edit."""
    gameplay = context.get("gameplay", {})
    faults = [m for m in gameplay.get("mechanisms", []) if m.get("fault")]
    return {
        "build": context.get("build"),
        "scripts": {k: context.get("scripts", {}).get(k) for k in ("serial", "loaded_at")},
        "gameplay_warnings": gameplay.get("warnings", []),
        "unbound": gameplay.get("unbound", []),
        "faults": [{"key": m["key"], "fault": m["fault"]} for m in faults],
        "recent_events": gameplay.get("events", [])[-5:],
    }
