// Asset inspection, NOT a physical simulation. Left-to-right: intact / stripped /
// exposed core / exploded diagram. Bottom row contains their exact source fragments.
use <../../lib/kit_raiden.scad>
use <../../lib/raiden_parts.scad>
use <../../lib/raiden_airframes.scad>
use <../../lib/gk_camera.scad>
$fn = 8;
color([0.20,0.27,0.23]) translate([0,-1,-0.3]) nr_box([44,29,0.3]);
translate([-15,6,1.1]) nr_veh_interceptor();
translate([-5,6,1.1]) nr_veh_interceptor(damage = 1);
translate([5,6,1.1]) nr_veh_interceptor(damage = 2);
translate([15,6,1.1]) nr_veh_interceptor(explode = 0.75);
translate([-14,-5,0.03]) nr_prop_wreck(stage = 1);
translate([0,-5,0.03]) nr_prop_wreck(stage = 3, paint = 1);
translate([13,-5,0.03]) nr_fx_crater();
translate([13,-5,0.08]) nr_veh_interceptor(damage = 2);
translate([11,-7,0.03]) nr_part_engine();
translate([16,-5,0.03]) rotate([0,0,55]) nr_part_wing();
translate([13,-5,0.85]) nr_fx_smoke(seed = 9);
gk_camera_lookat([8,-30,40], [0,0,0], name = "01_Damage_stages", fov = 51);
gk_camera_lookat([19,-1,14], [15,6,1.2], name = "02_Exploded", fov = 48);
gk_camera_lookat([4,-13,13], [0,-5,0], name = "03_Individual_debris", fov = 49);
