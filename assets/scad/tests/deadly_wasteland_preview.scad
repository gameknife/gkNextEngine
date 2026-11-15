// Preview for kit_deadly_wasteland.scad: a stretch of road with the clutter
// layer, a police roadblock and the survivors' bus.
//   gnb shot --scene assets/scad/tests/deadly_wasteland_preview.scad
use <../lib/kit_deadly.scad>
use <../lib/kit_deadly_wasteland.scad>

$fn = 12;

color(dd_GRASSC()) translate([0, 0, -0.1]) dd_slab(80, 50, 0.1);
dd_ground_road(L = 70, W = 10, seed = 3);
translate([0, 0, 0]) dw_ground_road_clutter(L = 70, W = 10, seed = 7, step = 5);
translate([24, 0, 0]) dw_prop_roadblock(W = 10, seed = 3);
translate([-6, 10, 0]) dw_veh_survivor_bus();
translate([-24, 9, 0]) dw_veh_police(seed = 2);
translate([10, 10, 0]) dw_veh_abandoned(seed = 5);
translate([18, 10, 0]) dw_veh_burnt(seed = 9);
for (i = [0 : 5]) translate([-30 + i * 3, -9, 0]) dw_prop_corpse(seed = i * 7, z = 0);
translate([-8, -9, 0]) dw_prop_luggage(seed = 3, z = 0);
translate([-3, -9, 0]) dw_prop_cart(seed = 1);
translate([0, -9, 0]) dw_prop_trash_bags(seed = 4);
