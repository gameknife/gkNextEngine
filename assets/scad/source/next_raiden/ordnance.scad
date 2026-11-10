// Full-size, top-readable ordnance/reward and VFX mesh inspection.
use <../../lib/kit_raiden.scad>
use <../../lib/raiden_parts.scad>
use <../../lib/gk_camera.scad>
$fn = 8;
color([0.19,0.25,0.23]) translate([0,0,-0.2]) nr_box([18,14,0.2]);
translate([-7,4,0]) nr_wpn_bolt();
translate([-5,4,0]) nr_wpn_bolt(hostile = true);
translate([-3,4,0]) nr_wpn_orb();
translate([-1,4,0]) nr_wpn_needle();
translate([1,4,0]) nr_wpn_missile();
translate([3,4,0]) nr_wpn_bomb();
translate([6,4,0]) rotate([0,0,90]) nr_wpn_laser(length = 3);
translate([-6,0,0]) nr_item_power();
translate([-3,0,0]) nr_item_medal();
translate([0,0,0]) nr_item_repair();
translate([3,0,0]) nr_item_bomb();
translate([6,0,0]) nr_item_option();
translate([-6,-4,0]) nr_fx_exhaust();
translate([-3,-4,0]) nr_fx_flash();
translate([0,-4,0]) nr_fx_blast();
translate([3,-4,0]) nr_fx_smoke();
translate([6,-4,0]) nr_fx_crater();
gk_camera_lookat([3,-11,18], [0,0,0], name = "01_Ordnance", fov = 49);
gk_camera_lookat([0,-1,18], [0,0,0], name = "02_Top_readability", fov = 49);
