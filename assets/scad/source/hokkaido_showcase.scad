// Hokkaido regional shells and street furniture; all doors face the camera.
use <../lib/kit_hokkaido.scad>
use <../lib/gk_camera.scad>
$fn=12;
color([0.24,0.30,0.18]) translate([0,8,-0.1]) cube([130,95,0.2],center=true);
translate([-42,28,0]) hk_bldg_warehouse(7,24,14);
translate([-8,28,0]) hk_bldg_office(13,22,13);
translate([24,28,0]) hk_bldg_workshop(19,20,12);
translate([49,28,0]) hk_bldg_apartment(31,16,11);
translate([-42,0,0]) hk_bldg_house(41,11,9);
translate([-20,0,0]) hk_bldg_rowhouse(43,10,10);
translate([2,0,0]) hk_bldg_shop(47,10,11);
translate([25,0,0]) hk_bldg_clinic(53,15,10);
translate([48,0,0]) hk_bldg_koban(59,10,8);
translate([-40,-25,0]) hk_bldg_shrine(61,11,9);
translate([-40,-34,0]) hk_prop_torii();
translate([-22,-25,0]) hk_prop_lantern();
translate([-14,-25,0]) hk_prop_vending(3);
translate([-6,-25,0]) hk_prop_pole();
translate([4,-25,0]) hk_prop_bollard();
translate([18,-23,0]) hk_nature_birch(7);
translate([34,-23,0]) hk_nature_spruce(11);
translate([50,-23,0]) hk_nature_elm(13);
gk_camera_lookat([-82,-102,65],[0,8,3],"regional_kit",48);
