---
name: nextdayz-geomap
description: Create, improve or regenerate a NextDayz geographic expedition map -- a real place turned into a playable 1 km level -- to regional quality. Use when asked to make a NextDayz map for a new location (a town, valley, coast...), when an existing map looks generic (identical buildings everywhere, villages that read as grids or military camps, uniformly sprinkled trees), or to regenerate / publish one. Not for engine work unrelated to these maps or for other games.
---

# NextDayz geographic map to regional quality

A map is judged by whether a player who knows the region would recognise it:
its buildings, the way settlements are laid out, and what grows where. The
pipeline (`gnb geo expedition`) only assembles what the layout and the
regional kit describe, so quality is decided in steps 1-4. Paths are relative
to the repository root.

Rules and file roles: `docs/projects/nextdayz/nextdayz-world-map-design.md`
(read it first, especially *新地点初始地图的强制规则*). Terrain operators
(`paint`, plot pads): `docs/AGENT_GUIDE/ScadTerrain.md`. Worked example: Loket
v4, `assets/scad/proc/nextdayz/cz_loket_riverbend/layout.scad`.

## 1. Research the region (before any SCAD)

Write down, in a few lines each, from real knowledge of the place:

- **Architecture families**: roof shape and material (gable / half-hip /
  hip / mansard; clay tile / slate / metal), gable treatment (folk-baroque
  scrolls, half-timbering, boards), wall finish and colour range, window
  style, landmark types (church tower shape, chapel, town hall, castle).
- **Settlement morphology**: green villages, street villages, town squares
  with continuous frontage, scattered farmsteads, cottage colonies, collective
  farms, suburbs of detached houses, industrial yards. Which one each real
  built-up area of the tile is.
- **Land cover**: forest species and structure (spruce monoculture, mixed,
  beech, pine), disturbances (bark-beetle stands, clear-cuts), riparian belts,
  field system (large fields, strips, hedgerows), orchards and road alleys,
  meadows, village greens.

## 2. Regional kit

Each region needs its own shells: `assets/scad/lib/kit_<region>.scad`
(Bohemia: `kit_bohemia.scad`, prefix `bh_`). Every building kind the
settlement planner emits must map to a regional shell in `nd_pcg_building`
(`assets/scad/lib/nextdayz_pcg.scad`): farmhouse, egerland/regional variant,
barn, stable, burgher, shop/pharmacy/pub, townhall, church, chapel,
firestation, villa, family house (kostka), cottage (chata), cowshed, factory,
castle -- plus flora species for `nd_bh_flora`-style dispatch. Falling back to
another region's shells is exactly what made earlier maps look identical.

Kit contract: base z=0, front=-y, real door/window voids, walk-in door on the
front axis, roofs with closed gables, colours < 0.5 albedo. Surfaces go through
per-class `gk_material` wrappers (Bohemia: `bh_m_*` — render, tile, slate,
paint, zinc, gilt, varnish, ceramic...); glass is opaque low-roughness, since a
transmissive dielectric turns black under an overcast sky. Interiors are part
of the shell: floor finish, inner wall skin with dado, puddles under the roof
holes, shards under broken windows — placed clear of the coldwar furniture and
loot points of the `nd_*` wrapper that hosts the shell. Water material comes
from the recipe (`gameplay.water`). Render a showcase
(`gnb shot --scene <abs path to a .scad using the kit>`) and fix what reads
wrong before using it in a map.

## 3. Plan the layout against the real place

```bash
python tools/nextdayz/geomap_preview.py --map <mapId>   # out/nextdayz/<mapId>_plan.png
```

The plan view shows relief, slope, exact water, OSM buildings/roads and every
control layer. In `layout.scad`:

- One settlement site per real built-up area, using the morph that matches
  the real pattern (`naves`, `square`, `street`, `suburb`, `villas`,
  `homestead`, `chaty`, `jzd`, `factory`, `hillcastle`), rotated to the real
  street or green axis. Legacy kinds (`military`, `checkpoint`, `medical`) are
  for gameplay compounds only.
- Size sites by the exporter's rules (it prints the minimum size). Find dry,
  gentle ground numerically rather than by eye; terraced kinds tolerate 20 m
  of relief, single-pad kinds (square, jzd, factory, castle) 12 m.
- `PCG_LANDCOVER` polygons for every forest, field, meadow, orchard and
  riparian corridor; `PCG_ALLEYS` for roadside fruit/linden alleys and
  hedgerows. `PCG_VEGETATION` only for a sparse ruderal shrub layer.
- Recipe: `roadClasses` without service/footway clutter for towns,
  `buildingBudget` = sum of site counts (~100-120 per km²).

## 4. Generate, validate, review

```bash
gnb.bat geo expedition --recipe assets/scad/specs/nextdayz/<mapId>.json
python tools/nextdayz/geomap_shots.py --map <mapId>                 # overview + aerial + street
python tools/nextdayz/geomap_shots.py --map <mapId> --sites a,b --views aerial,street
```

Open every image (out/nextdayz/shots/<mapId>/) and check:

- **Overview**: distinct masses -- dark forest, striped fields, green
  meadows, riparian belts on the banks. No uniform sprinkle of trees; no
  settlement that is a grey rectangle.
- **Aerial**: buildings line streets and greens; yards earth, squares paved,
  gardens green with trees between plots; every settlement has its landmarks
  (church/chapel, column, pond, fire station, memorial, silo...).
- **Street (eye height)**: regional silhouettes and colours, open entrances,
  no floating or sunk floors, props on the ground.

Interiors render near-black under `gnb shot` (fixed exposure); review them
with `gnb validate` and a script that sets `r.exposure.auto` true and
`r.exposure.maxEV` 3 like NextDayz, after a `wait-ms` long enough for the
draft to parse (a full map takes ~15 s).

Iterate on layout and kit until all three pass. Parse time of the draft
should stay below ~10 s (`parsed scene ... on cpu` in the run log).

## 5. Tests and publish

```bash
gnb.bat build Assets --reconfigure        # new .scad files enter the runtime mirror
gnb.bat build gkNextUnitTests NextDayz
out/build/windows/bin/gkNextUnitTests.exe "[NextDayz]"   # update MapCheck counts for the map
gnb.bat validate --script assets/agentscripts/nextdayz-<map>-entrance.agentscript.json
gnb.bat geo expedition --recipe assets/scad/specs/nextdayz/<mapId>.json --publish   # new version only
gnb.bat build NextDayz
python tools/nextdayz/check_region_upgrade.py --map <mapId>
```

Bump the recipe `version` before publishing; published versions are frozen.
Update the map's registry description and the map table in the design doc.
