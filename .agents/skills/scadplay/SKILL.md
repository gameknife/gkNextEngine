---
name: scadplay
description: Change the running ScadPlay game -- its SCAD level or its C# gameplay -- from a request phrased in plain words relative to the player, such as "在前面加一座桥", "把刚才那个挪远一点", "踩两个踏板才开门", "加一种碰到会弹飞的蘑菇" or "put a tower ahead of me". Use when the user is playing ScadPlay (or wants a ScadPlay level/mechanic built) and asks to add, move, wire, change or remove something in the level, or to change a rule. Do not use for engine C++ work or for other games.
---

# Edit ScadPlay while it runs

ScadPlay's level is a `.scad` file and its gameplay is C#. The running game rebuilds the level
whenever the file (or a kit it uses) is saved, hot reloads the C# when it is republished, keeps
the player and every mechanism's state across both, and reports what it sees in one JSON file.
The protocol and the kit contract are in `docs/AGENT_GUIDE/ScadPlay.md`.

Paths are relative to the repository root (the folder with `AGENTS.md`).

1. **Protocol.** If you have not read it in this session, read `docs/AGENT_GUIDE/ScadPlay.md`.
   It defines the context fields, where "in front / to the left" is, the kit modules and their
   wiring (`id` / `on` / `any`), and how to add gameplay in C#.

2. **Context.** Read `out/scadplay/context.json` fresh for every request -- the player moves.
   Note `build.serial` (and `scripts.serial` if you will touch C#). If the file is missing, or its
   `pid` is not a running process, the game is not running: make the edit anyway when it does not
   depend on where the player is, say it applies at the next launch (`./gnb.sh run ScadPlay`), and
   skip the waiting steps.

3. **Level edits** go into `scene.source`:
   - Positions from `player.pos` / `player.forward` (the character, not the camera, unless the
     request points at something: then `view.hit`). Heights from `player.ground_z`. Coordinates in
     the context are already SCAD space -- use them as they are.
   - **Append** after the `// ==== Agent additions` line: one module per object, called once, with
     a `// agent: "<the request>"` comment; build it around its own origin with its base at z = 0
     and put position / rotation on the call.
   - Prefer `kit_scadplay.scad` pieces (`sp_*`; header of the kit is the reference) and other kits
     the file already uses. Give every gameplay piece an `id`; wire with `on` / `any`.
   - To change something that exists, re-read the file and edit that statement in place;
     `gameplay.mechanisms[].line` tells you where each piece is.

4. **Gameplay (C#) edits**, when the request is a rule or a new kind of piece rather than layout:
   change or add `projects/ScadPlay/Scripts/Mechanisms/<Name>.cs` (a `Mechanism` subclass), register
   a new module in `Gameplay/MechanismTypes.cs`, and add or change its SCAD module (with an `id`
   parameter; moving parts as direct `sp_part_*` children). A module the context lists under
   `gameplay.unbound` is waiting for its class. Then:

   ```bash
   python3 .agents/skills/scadplay/scripts/publish_scripts.py
   ```

   It compiles, publishes where the game loaded the assembly from, and waits for the hot reload.
   On a compile error it prints the errors and the game keeps the old code: fix and run it again.

5. **Confirm a level edit** with the serial from step 2:

   ```bash
   python3 .agents/skills/scadplay/scripts/wait_build.py <build.serial>
   ```

   Exit 0 = built; 1 = failed (`build.error` names the file and line -- the last good level is still
   on screen; fix it and run again with the serial it printed); 2 = timed out / not running. Also
   read what it prints under `gameplay_warnings` (wiring to an id nobody has, duplicate ids),
   `unbound` and `faults` (a C# exception with file:line; that piece is parked, the rest runs).

6. **Reply in a line or two**: what changed and where (SCAD coordinates or file:line), that it built
   or reloaded, and anything the player should try ("站上新踏板试试"). Do not paste the code unless
   asked. If something you placed might block the way or float, say where it is.
