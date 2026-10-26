// Close inspection of small mechanisms and all three structural alternatives.
use <../../lib/kit_raiden_frontier.scad>
use <../../lib/raiden_parts.scad>
use <../../lib/gk_camera.scad>
$fn = 8;

color([0.16,0.21,0.19]) translate([0,0,-0.15]) nr_box([50,66,0.15]);
for (v = [0:2])
{
    x = (v-1)*15;
    translate([x,25,0]) rf_prop_pumpjack(variant = v);
    translate([x,15,0]) rf_veh_haultruck(variant = v);
    translate([x,5,0]) rf_veh_freightcar(variant = v);
    translate([x,-5,0]) rf_prop_cableyard(variant = v);
    translate([x,-15,0]) rf_prop_supplycache(variant = v);
    translate([x,-25,0]) rf_veh_snowcat(variant = v);
}

gk_camera_lookat([15,-51,75],[0,0,0], name = "01_Three_variants", fov = 50);
gk_camera_lookat([5,10,13],[0,24,2], name = "02_Pump_linkage", fov = 50);
gk_camera_lookat([4,-5,12],[0,5,1], name = "03_Wagon_and_wheels", fov = 50);
gk_camera_lookat([20,-37,13],[15,-23,1], name = "04_Snowcat_and_roof_supports", fov = 50);
