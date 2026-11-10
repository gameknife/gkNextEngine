"""Exercise a published region upgrade without touching the player's campaign."""
import argparse
import copy
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--map", default="cz_loket_riverbend", dest="map_id")
    args = parser.parse_args()
    registry = json.loads((ROOT / "assets/configs/nextdayz_maps.json").read_text())
    region = next(m for m in registry["maps"] if m["id"] == args.map_id)
    other_region = next(m for m in registry["maps"] if m["id"] != args.map_id)
    previous = region["mapVersion"] - 1
    assert previous >= 1
    fixture = json.loads((ROOT / "assets/agentscripts/fixtures/nextdayz-riverland-v1.json").read_text())
    # Exercise a region-version upgrade independently of legacy inventory
    # conversion. The Riverland seed predates spatial pockets and attachments;
    # give this fixture current, explicit placements so strict equality below
    # still checks every carried item, attachment and storage record.
    fixture["version"] = 5
    old = fixture["state"]
    old["weaponSlots"].append(0)
    old["previousSlot"] = -1
    for container, size in zip(old["inventory"]["containers"], [(6, 1), (2, 2), (4, 2), (4, 2)]):
        container.pop("capacity")
        container.update(width=size[0], height=size[1])
    for item in old["inventory"]["instances"]:
        item.update(attachments=[0] * 5, gridX=0, gridY=0, rotated=True)
    for item in old["campaign"]["storage"]:
        item.update(attachments=[0] * 5, gridX=-1, gridY=-1, rotated=False)
    old["mapVersion"] = previous
    old["map"] = region["scene"].replace(f"/v{region['mapVersion']}/", f"/v{previous}/")
    old["campaign"].update(mapSorties={region["id"]: 3}, lastMapId=region["id"], lastOutcome="", sortieId=4)
    other = {"world": copy.deepcopy(old["world"]), "time": copy.deepcopy(old["time"]), "version": other_region["mapVersion"]}
    other["world"]["seconds"] = 77
    old["maps"] = {other_region["id"]: other}
    save_name = f"region-upgrade-{args.map_id}-v{region['mapVersion']}.json"
    save = ROOT / "out/build/windows/bin/out/nextdayz-validation" / save_name
    save.parent.mkdir(parents=True, exist_ok=True)
    save.write_text(json.dumps(fixture))
    script = json.loads((ROOT / "assets/agentscripts/nextdayz-startup-migration.agentscript.json").read_text())
    script["name"] = "nextdayz-region-upgrade"
    initial = next(m for m in registry["maps"] if m["status"] == "published")
    script["scene"] = initial["scene"]
    for step in script["steps"]:
        if step.get("name") == "nextdayz.validation.campaignPath":
            step["set"] = f"out/nextdayz-validation/{save_name}"
        if step.get("query") == "game.activeMapId":
            step["value"] = initial["id"]
        if step.get("type") == "wait-frames":
            step.clear()
            step.update(type="wait-until", query="game.mapLoading", op="eq", value=False, timeoutMs=180000)
        if step.get("query") == "game.sceneGeneration":
            step["value"] = 1 + (initial["id"] != args.map_id)
        if step.get("query") == "game.currentMapSorties":
            step["value"] = 3
    script["steps"].insert(6, {"type": "assert", "query": "game.activeMapId", "op": "eq", "value": args.map_id})
    script_path = ROOT / "out/nextdayz-region-upgrade.agentscript.json"
    script_path.write_text(json.dumps(script))
    subprocess.run([str(ROOT / "gnb.bat"), "validate", "--script", str(script_path)], cwd=ROOT, check=True)
    upgraded = json.loads(save.read_text())["state"]
    assert upgraded["map"] == region["scene"]
    assert upgraded["mapVersion"] == region["mapVersion"]
    assert not upgraded["campaign"]["inField"]
    assert upgraded["inventory"] == old["inventory"]
    assert upgraded["campaign"]["storage"] == old["campaign"]["storage"]
    assert upgraded["campaign"]["gold"] == 37
    assert upgraded["maps"][f"{region['id']}@v{previous}"]["world"]["seconds"] == 123
    assert upgraded["maps"][other_region["id"]] == other
    backup = Path(str(save) + f".before-{region['id']}-v{previous}")
    assert json.loads(backup.read_text())["state"]["map"] == old["map"]
    print("Region upgrade passed; gear, storage, other regions and original backup preserved.")


if __name__ == "__main__":
    main()
