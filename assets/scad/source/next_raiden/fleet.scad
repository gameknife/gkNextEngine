// Aircraft-only contact sheet. The red fleet faces the viewer; blue players oppose it.
use <../../lib/kit_raiden.scad>
use <../../lib/raiden_parts.scad>
use <../../lib/gk_camera.scad>
$fn = 8;
color([0.20,0.28,0.23]) translate([0,0,-0.3]) nr_box([40,34,0.3]);
for (x = [-18:6:18]) color([0.28,0.36,0.29]) translate([x,0,0]) nr_box([0.04,32,0.012]);
for (y = [-15:6:15]) color([0.28,0.36,0.29]) translate([0,y,0]) nr_box([38,0.04,0.012]);
translate([0,10,0.1]) nr_veh_fortress();
translate([-12,10,0.1]) nr_veh_bomber();
translate([12,10,0.1]) nr_veh_gunship();
translate([-13,-1,0.1]) nr_veh_scout();
translate([-5,-1,0.1]) nr_veh_interceptor();
translate([5,-1,0.1]) nr_veh_rotorcraft();
translate([14,-1,0.1]) nr_veh_bomber(damage = 1);
translate([-6,-11,0.1]) rotate([0,0,180]) nr_veh_player();
translate([3,-11,0.1]) rotate([0,0,180]) nr_veh_wingman();
translate([11,-11,0.1]) nr_veh_interceptor(damage = 2);
gk_camera_lookat([13,-29,42], [0,1,0], name = "01_Fleet", fov = 50);
gk_camera_lookat([0,-3,27], [0,10,0], name = "02_Fortress", fov = 46);
gk_camera_lookat([0,-16,19], [0,-1,0], name = "03_Light_aircraft", fov = 55);
