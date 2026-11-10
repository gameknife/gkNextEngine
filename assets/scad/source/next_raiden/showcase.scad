// NextRaiden combat kit catalogue. Ground variants and the 20 new scenery modules
// have a dedicated inspection scene in facilities.scad.
// Aircraft at rest for inspection, NOT a playable level. Tile tops are z=0.
use <../../lib/kit_raiden.scad>
use <../../lib/raiden_parts.scad>
use <../../lib/gk_camera.scad>
$fn = 8;

color([0.17,0.23,0.20]) translate([0,-8,-0.7]) nr_box([64,102,0.28]);

// Northern apron: boss + heavy aircraft, readable silhouettes and faction colours.
translate([-17,33,0]) nr_veh_bomber();
translate([0,33,0]) nr_veh_fortress();
translate([18,33,0]) nr_veh_gunship();
translate([-23,22,0]) nr_veh_player();
translate([-12,22,0]) nr_veh_wingman();
translate([-3,22,0]) nr_veh_scout();
translate([7,22,0]) nr_veh_interceptor();
translate([20,22,0]) nr_veh_rotorcraft();

// Buildings.
translate([-25,11,0]) nr_bldg_hangar();
translate([-14,11,0]) nr_bldg_controltower();
translate([-4,11,0]) nr_bldg_factory();
translate([9,11,0]) nr_bldg_fueldepot();
translate([22,11,0]) nr_bldg_bunker();

// Ground and water targets, port fixtures.
translate([-25,1,0]) nr_veh_tank();
translate([-17,1,0]) nr_veh_launcher();
translate([-9,1,0]) nr_prop_turret();
translate([0,1,0]) nr_veh_patrolboat();
translate([11,1,0]) nr_prop_container();
translate([23,1,0]) nr_prop_crane();

// Scenery and exact-source debris.
translate([-25,-9,0]) nr_prop_radar();
translate([-18,-9,0]) nr_prop_barrier();
translate([-12,-9,0]) nr_prop_crate();
translate([-5,-9,0]) nr_nature_tree();
translate([2,-9,0]) nr_nature_rock();
translate([18,-10,0]) nr_prop_wreck();

// Chunk library. Larger close-up lives in destruction.scad.
translate([-27,-18,0]) nr_part_armour();
translate([-22,-18,0]) nr_part_wing();
translate([-17,-18,0]) nr_part_canopy();
translate([-12,-18,0]) nr_part_engine();
translate([-7,-18,0]) nr_part_core();
translate([-2,-18,0]) nr_part_gun();
translate([3,-18,0]) nr_part_fin();
translate([8,-18,0]) nr_part_blade();
translate([15,-18,0]) nr_part_pod();

// Ordnance, rewards, solid-mesh effect primitives. See ordnance.scad for detail.
translate([-27,-25,0]) nr_wpn_bolt();
translate([-24,-25,0]) nr_wpn_orb();
translate([-21,-25,0]) nr_wpn_needle();
translate([-18,-25,0]) nr_wpn_missile();
translate([-15,-25,0]) nr_wpn_bomb();
translate([-11,-25,0]) rotate([0,0,90]) nr_wpn_laser();
translate([-6,-25,0]) nr_item_power();
translate([-3,-25,0]) nr_item_medal();
translate([0,-25,0]) nr_item_repair();
translate([3,-25,0]) nr_item_bomb();
translate([6,-25,0]) nr_item_option();
translate([10,-25,0]) nr_fx_exhaust();
translate([13,-25,0]) nr_fx_flash();
translate([16,-25,0]) nr_fx_smoke();
translate([20,-25,0]) nr_fx_blast();
translate([25,-25,0]) nr_fx_crater();

// Six 16m seam-compatible scrolling tiles, separated by inspection gutters.
translate([-20,-37,0]) nr_ground_grass();
translate([0,-37,0]) nr_ground_runway();
translate([20,-37,0]) nr_ground_road();
translate([-20,-55,0]) nr_ground_farmland();
translate([0,-55,0]) nr_ground_water();
translate([20,-55,0]) nr_ground_quay();

gk_camera_lookat([35,-83,111], [0,-10,0], name = "01_Kit_overview", fov = 53);
gk_camera_lookat([18,5,34], [0,29,0], name = "02_Aircraft", fov = 54);
gk_camera_lookat([22,-20,29], [0,8,0], name = "03_Ground_targets", fov = 56);
gk_camera_lookat([0,-38,14], [0,-22,0], name = "04_Parts_and_ordnance", fov = 58);
