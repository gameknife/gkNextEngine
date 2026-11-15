---
name: nextdayz-geomap
description: Create, improve or regenerate a NextDayz geographic expedition map -- a real place turned into a playable 1 km level -- to regional quality. Use when asked to make a NextDayz map for a new location (a town, valley, coast, savanna...), to open a new region or climate (its own kit, settlement forms, land cover and ground palette), when an existing map looks generic (identical buildings everywhere, villages that read as grids or military camps, uniformly sprinkled trees), or to regenerate / publish one. Not for engine work unrelated to these maps or for other games.
---

# NextDayz geographic map to regional quality

A map is judged by whether a player who knows the region would recognise it:
its buildings, the way settlements are laid out, what grows where, and the
colour of the ground. The pipeline (`gnb geo expedition`) only assembles what
the layout and the regional kit describe, so quality is decided in steps 1-5.
Paths are relative to the repository root.

A level that stands for no real place (the training ground) skips steps 1-2 and
the geocache: its recipe carries a `synthetic` terrain block instead, and
everything from the layout on is the same pipeline (design doc,
*无经纬度关卡*).

Rules and file roles: `docs/projects/nextdayz/nextdayz-world-map-design.md`
(read it first, especially *新地点初始地图的强制规则* and *新地域接入清单*).
Terrain operators (`paint`, `river`, plot pads): `docs/AGENT_GUIDE/ScadTerrain.md`.
Worked examples: Loket (Central Europe, an existing region),
`assets/scad/proc/nextdayz/cz_loket_riverbend/layout.scad`; Talek (East African
savanna, a region built from nothing), `assets/scad/proc/nextdayz/ke_talek_mara/`.

## 1. Research the region (before any SCAD)

Write down, in a few lines each, from real knowledge of the place:

- **Architecture families**: roof shape and material (gable / half-hip / hip /
  mono-pitch / cone; clay tile / slate / corrugated iron / thatch), wall finish
  and colour range, window and door style, landmark types.
- **Settlement morphology**: which form each real built-up area of the tile is
  (green village, town square, duka street, thorn-ringed boma, homestead,
  mission compound, camp...). If none of the existing kinds fits, that is a new
  morph, not a reason to reuse a near one.
- **Land cover**: what defines the landscape. In a forest region it is species
  and stand structure; in a savanna it is *tree spacing* (open plain, woodland,
  thicket, river gallery) and where the ground is grazed bare.
- **Ground and water**: soil and road colour (brown earth, red murram), whether
  roads are sealed, whether the rivers hold water all year or are sand beds.

## 2. Choose the tile with the scout view

```bash
gnb.bat geo fetch --name <mapId> --at <lat>,<lon> --size 1000
gnb.bat geo build --name <mapId> --at <lat>,<lon> --size 1000
python tools/nextdayz/geomap_preview.py --map <mapId> --scout    # out/nextdayz/<mapId>_scout.png
```

The scout view needs no recipe or layout. It draws real-metre relief, OSM
buildings, roads, landuse, waterways and named places, and prints the relief
range with a suggested `reliefScale`. A coordinate from memory is rarely the
best square: move `--at` and re-fetch (delete `external/geocache/<mapId>` first)
until the features you want - the river bend, the town, the gate - share the
tile. One centre shift is normal.

Read the scout output for two traps:

- **`water polygons 0` with a river on the picture.** Outside dense cities a
  river is one OSM centreline. Name its class in the recipe and the exporter
  carves it: `"waterways": {"river": {"width": 16, "depth": 2.2}, "stream":
  {"width": 9, "dry": true}}`. A wet class becomes a TERR `river` (water follows
  the bed, no global water level); a dry class becomes a sand bed painted over
  the land cover. Classes left out are ignored.
- **Flat relief.** Below ~30 m use `reliefScale: 1.0`; scale down only real
  mountains.

## 3. Regional kit (new region: also the checklist in the design doc)

Each region needs its own shells: `assets/scad/lib/kit_<region>.scad`
(Bohemia `kit_bohemia.scad` / `bh_`; savanna `kit_savanna.scad` / `sv_`). Every
building kind the settlement planner emits must map to a regional shell in the
region's dispatch (`nd_pcg_building` -> `nd_sv_building`, ...), and the legacy
encounter kinds (`police`, `medical`, `warehouse`...) too, so no site of the
region can fall back to another region's shells - that fallback is exactly what
made earlier maps look identical.

Kit contract: base z=0, front=-y, real door/window voids, walk-in door on the
front axis with nothing standing on that axis (veranda posts and tent poles go
in even numbers), roofs with closed gables, colours < 0.4 albedo for sunlit
walls (0.5 already reads white outdoors at noon). Surfaces go through per-class
`gk_material` wrappers; glass is opaque low-roughness. Porous materials are
diffuse (`roughness=1`) with a low-contrast `gk_texture` print; give the
`plaster` print a second palette colour close to the wall colour and
`relief <= 0.5`, or the spalling reads as holes in game. Interiors are part of
the shell and stay clear of the furniture and loot points of the `nd_*` wrapper
that hosts it; a shape the stock wrappers do not fit (round hut, tent, nave
along y) gets its own wrapper in `nextdayz_<region>.scad`.

Mesh budget: anything placed by the hundred metre (fences, hedges, thorn rings)
must be a few dozen triangles a metre. A first thorn fence of spheres cost
90 000 triangles per boma.

Render the showcase (`gnb shot --scene <abs path to source/<region>_showcase.scad>`)
and fix what reads wrong before using the kit in a map. The showcase is lit
harder than the game: judge silhouettes there, colours in step 5.

## 4. Plan the layout against the real place

Write the recipe (`assets/scad/specs/nextdayz/<mapId>.json`) and its controls
template first, then check everything in one pass - the exporter stops at the
first invalid site, this lists them all and works before the first export:

```bash
python tools/nextdayz/geomap_check.py --map <mapId>             # every site, spawn, extraction, bridge
python tools/nextdayz/geomap_check.py --map <mapId> --probe x,y,w,h,rot
python tools/nextdayz/geomap_check.py --map <mapId> --rivers    # centreline points for bridges / gallery polygons
```

In `layout.scad`:

- One settlement site per real built-up area, using the morph that matches the
  real pattern, rotated to the real street, green or river. Legacy kinds
  (`military`, `checkpoint`, `medical`) are for gameplay compounds only.
- Size sites by the exporter's rules (it prints the minimum size).
- `PCG_LANDCOVER` polygons for every stand, field and pasture; waterside kinds
  (`riparian`, `gallery`) only grow near water, so draw them as ribbons hugging
  the river rather than one big polygon. `PCG_ALLEYS` for roadside rows and
  hedges. `PCG_VEGETATION` only for a sparse shrub layer.
- A carved river blocks movement: every bank that holds a site or an extraction
  needs a crossing. OSM bridges come for free; add `PCG_BRIDGES` and a
  `PCG_CLEAR` corridor at each head.
- Recipe: `roadClasses` without service/footway clutter for towns (camps and
  farms often *are* service roads - keep them there), `buildingBudget` = sum of
  site counts (~100-120 per km²).

## 5. Generate, validate, review

```bash
gnb.bat geo expedition --recipe assets/scad/specs/nextdayz/<mapId>.json
gnb.bat build Assets --reconfigure      # first time only: terrain.hmap must reach the runtime mirror
python tools/nextdayz/geomap_preview.py --map <mapId>              # plan view with all control layers
python tools/nextdayz/geomap_shots.py --map <mapId> --views overview
python tools/nextdayz/geomap_shots.py --map <mapId> --sites a,b --views aerial,street
```

`--sites` skips the overview; ask for it on its own. A flat, featureless first
render with `hmap cannot read ... terrain.hmap` in the run log means the
runtime mirror is stale (`gnb build Assets`).

Open every image (out/nextdayz/shots/<mapId>/) and check:

- **Overview**: distinct masses - forest or gallery, fields, pasture, bare
  ground. No uniform sprinkle of trees; no settlement that is a grey rectangle.
- **Aerial**: buildings line streets, greens or rings; yards are earth, squares
  paved, gardens green; every settlement has its landmarks.
- **Street (eye height)**: regional silhouettes and colours, open entrances, no
  floating or sunk floors, props on the ground.

`gnb shot` uses a fixed exposure: colours and interiors are judged in the game,
with a `gnb validate` script that teleports and screenshots
(`nextdayz-talek-bridge.agentscript.json` is a short model: wait for
`game.sceneReady`, `nextdayz.validation.teleport "x,y"`, `lookYaw` 180 = north,
270 = west). A full map takes 20-25 s to parse on the reference machine (Loket
24.7 s, Talek 22.8 s); compare against that baseline, not against an absolute
number.

## 6. Tests and publish

```bash
gnb.bat build gkNextUnitTests NextDayz
out/build/windows/bin/gkNextUnitTests.exe "[NextDayz]"   # add the map to MapCheck and bump the registry count
gnb.bat validate --script assets/agentscripts/nextdayz-<map>-<route>.agentscript.json
gnb.bat geo expedition --recipe assets/scad/specs/nextdayz/<mapId>.json --publish   # new version only
gnb.bat build NextDayz
python tools/nextdayz/check_region_upgrade.py --map <mapId>
```

Register the map in `assets/configs/nextdayz_maps.json` as `"status": "draft"`
with `scene` = the draft scene as soon as it loads: it is then selectable in the
safehouse (`ENTER SELECTED MAP (TEST)`) and covered by the unit tests, but stays
out of random dispatch until someone has played it. Publishing follows that
playtest, never precedes it. Bump the recipe `version` before publishing;
published versions are frozen (the snapshot copies every library the scene
reaches through `use`/`include`, so a new regional kit needs no exporter
change). Update the registry description and the map table in the design doc.
