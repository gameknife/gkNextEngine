use <../../lib/kit_strike.scad>
use <../../lib/kit_strike_char.scad>
use <../../lib/gk_camera.scad>
$fn=10;
translate([0,0,-0.12]) sk_ground_paving(4,3,0.12,[0.23,0.28,0.29]);
translate([-0.7,0,0.17]) rotate([0,90,-90]) so_wpn_carbine(0);
translate([0.7,0,0.17]) rotate([0,90,-90]) so_wpn_carbine(1);
translate([-0.7,0.8,0]) so_head_operator(0);
translate([0.7,0.8,0]) so_head_operator(1);
gk_camera_lookat([1.5,-2.8,3.4],[0,0.15,0],"Equipment detail",43);
