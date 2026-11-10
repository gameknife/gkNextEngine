// Static vertical-shooter art target. Not an application or gameplay implementation.
// Blue player at +Y heads -Y; enemies rotate 180 degrees to head toward the player.
// Flight deck z=7m, scenery z=0, wreck pieces rest on actual ground surfaces.
use <../../lib/kit_raiden.scad>
use <../../lib/raiden_parts.scad>
use <../../lib/gk_camera.scad>
$fn = 8;
for (y = [-24,-8,8,24])
{
    translate([0,y,0]) nr_ground_runway();
    translate([-16,y,0]) nr_ground_grass(seed = y+40);
    translate([16,y,0]) nr_ground_grass(seed = y+61);
}
translate([-16,-23,0.02]) nr_bldg_hangar();
translate([-16,-12,0.02]) nr_bldg_hangar(paint = 1);
translate([17,-24,0.02]) nr_bldg_controltower();
translate([16,-13,0.02]) nr_bldg_fueldepot();
translate([-17,6,0.02]) nr_bldg_bunker();
translate([17,5,0.02]) nr_prop_radar();
translate([13,16,0.02]) nr_veh_launcher();
translate([-5,-6,0.04]) rotate([0,0,180]) nr_veh_tank();
translate([5,7,0.04]) rotate([0,0,180]) nr_prop_turret();
for (s = [-1,1], i = [0:4]) translate([s*(19+nr_rnd(i,3)),12+i*4,0.02]) nr_nature_tree(seed = i);
for (y = [-28,-18,-8,2,12,22]) translate([-9,y,0.02]) nr_prop_barrier();

translate([0,19,7]) nr_veh_player();
translate([-5,22,7]) nr_veh_wingman();
translate([5,22,7]) nr_veh_wingman();
translate([-7,-9,7]) rotate([0,0,180]) nr_veh_interceptor();
translate([6,-14,7]) rotate([0,0,180]) nr_veh_bomber();
translate([-4,-23,7]) rotate([0,0,180]) nr_veh_gunship();
translate([8,1,7]) rotate([0,0,180]) nr_veh_interceptor(damage = 1);
translate([10,4,7.3]) nr_fx_blast();
translate([7,4,2.8]) rotate([20,30,35]) nr_part_engine();
translate([9,7,1.5]) rotate([15,-25,75]) nr_part_wing();
translate([13,26,0.02]) nr_prop_wreck(seed = 5);
translate([13,26,0.03]) nr_fx_crater();
for (y = [4,8,12]) for (x = [-0.8,0.8]) translate([x,y,7.5]) nr_wpn_bolt();
for (i = [-2:2]) translate([i*2,-1+abs(i)*0.6,7.2]) nr_wpn_orb();
translate([-3,12,7]) nr_item_power();
translate([4,10,7]) nr_item_medal();
gk_camera_lookat([0,30,83], [0,0,0], name = "01_Vertical_battlefield", fov = 51);
gk_camera_lookat([26,36,34], [0,8,4], name = "02_Action_and_falling_parts", fov = 53);
gk_camera_lookat([-5,2,21], [-13,-15,0], name = "03_Airbase", fov = 53);
