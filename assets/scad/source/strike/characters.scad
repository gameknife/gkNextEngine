// Static ready-pose inspection. Animated assets live under characters/.
use <../../lib/kit_strike.scad>
use <../../lib/kit_strike_char.scad>
use <../../lib/gk_camera.scad>
use <../../characters/strike_rig.scad>
$fn=10;
translate([0,0,-0.16]) sk_ground_paving(6,4,0.16,[0.24,0.29,0.30]);
translate([-1.05,0,0]) rotate([0,0,-12]) bone_root(0,true);
translate([1.05,0,0]) rotate([0,0,12]) bone_root(1,true);
translate([-1.05,-0.65,0.01]) color([0.1,0.45,0.62]) linear_extrude(height=0.008) text("POLICE",size=0.18,halign="center");
translate([1.05,-0.65,0.01]) color(sk_RED()) linear_extrude(height=0.008) text("RAIDER",size=0.18,halign="center");
gk_camera_lookat([3.4,-6.2,2.8],[0,-0.1,0.9],"Operators",38);
gk_camera_lookat([-1.8,-2.5,1.7],[-1.05,0,1.1],"Police detail",42);
gk_camera_lookat([1.5,-2.5,1.7],[1.05,0,1.1],"Raider detail",42);
