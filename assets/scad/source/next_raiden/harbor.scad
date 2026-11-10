// Harbor boss encounter art target, 48x64m. Seam-aligned solid-colour tile palette.
use <../../lib/kit_raiden.scad>
use <../../lib/raiden_parts.scad>
use <../../lib/gk_camera.scad>
$fn = 8;
for (y = [-24,-8,8,24])
{
    translate([-16,y,0]) nr_ground_quay();
    translate([0,y,0]) nr_ground_water();
    translate([16,y,0]) nr_ground_water();
}
translate([-17,-23,0.03]) nr_bldg_factory();
translate([-17,-10,0.03]) nr_bldg_fueldepot();
translate([-13,1,0.03]) rotate([0,0,90]) nr_prop_crane();
for (i = [0:2]) translate([-18+i*3,14,0.03]) nr_prop_container(paint = i);
translate([-14,24,0.03]) rotate([0,0,180]) nr_prop_turret();
translate([14,-20,0.03]) rotate([0,0,180]) nr_veh_patrolboat();
translate([16,3,0.03]) rotate([0,0,180]) nr_veh_patrolboat();
translate([4,-12,8]) rotate([0,0,180]) nr_veh_fortress();
translate([-3,-1,7]) rotate([0,0,180]) nr_veh_rotorcraft();
translate([12,-4,7]) rotate([0,0,180]) nr_veh_scout();
translate([3,19,7]) nr_veh_player();
translate([-2,23,7]) nr_veh_wingman();
for (y = [3,7,11,15]) translate([3,y,7.5]) nr_wpn_bolt();
for (i = [-3:3]) translate([4+i*2.1,0.5+abs(i)*0.45,8]) nr_wpn_needle();
translate([-17,25,0.03]) nr_fx_crater();
translate([-18,25,0.05]) nr_part_engine();
translate([-16,27,0.05]) rotate([0,0,65]) nr_part_wing();
translate([-18,25,0.82]) nr_fx_smoke();
translate([8,9,7]) nr_item_repair();
gk_camera_lookat([0,30,83], [0,0,0], name = "01_Harbor_boss", fov = 51);
gk_camera_lookat([22,13,31], [2,-10,5], name = "02_Boss_closeup", fov = 51);
gk_camera_lookat([2,27,23], [-15,10,0], name = "03_Dock_detail", fov = 52);
