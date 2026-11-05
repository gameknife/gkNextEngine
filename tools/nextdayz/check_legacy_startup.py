"""Reproduce default startup with an old in-field Riverland save, using isolated data."""
import json
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT / "assets/agentscripts/fixtures/nextdayz-riverland-v1.json"
SAVE = ROOT / "out/build/windows/bin/out/nextdayz-validation/startup-migration.json"


def main():
    SAVE.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(FIXTURE, SAVE)
    subprocess.run([str(ROOT / "gnb.bat"), "validate", "--script",
                    "assets/agentscripts/nextdayz-startup-migration.agentscript.json"], cwd=ROOT, check=True)
    migrated = json.loads(SAVE.read_text())["state"]
    original = json.loads(Path(str(SAVE) + ".riverland-backup").read_text())["state"]
    assert original["map"].endswith("riverland_1km.scad") and original["campaign"]["inField"]
    registry = json.loads((ROOT / "assets/configs/nextdayz_maps.json").read_text())
    expected = next(m["scene"] for m in registry["maps"] if m["id"] == "cz_loket_riverbend")
    assert migrated["map"] == expected
    assert not migrated["campaign"]["inField"]
    assert migrated["campaign"]["gold"] == original["campaign"]["gold"] == 37
    assert migrated["inventory"] == original["inventory"]
    assert migrated["maps"][original["map"]]["world"]["seconds"] == 123
    print("Default startup migration passed; original backup and carried gear preserved.")


if __name__ == "__main__":
    main()
