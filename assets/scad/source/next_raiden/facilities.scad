// NextRaiden scenery catalogue: 75m gameplay-camera scale, no combat targets.
// Default variants above; selected structural alternatives along the south edge.
use <../../lib/kit_raiden.scad>
use <../../lib/raiden_parts.scad>
use <../../lib/gk_camera.scad>
$fn = 8;

color([0.16,0.22,0.18]) translate([0,-7,-0.5]) nr_box([80,100,0.1]);

translate([-28,30,0]) nr_bldg_shelter();
translate([-14,30,0]) nr_bldg_workshop();
translate([0,30,0]) nr_bldg_barracks();
translate([14,30,0]) nr_bldg_warehouse();
translate([28,30,0]) nr_bldg_substation();

translate([-28,12,0]) nr_bldg_silos();
translate([-14,12,0]) nr_bldg_greenhouse();
translate([0,12,0]) nr_bldg_watertower();
translate([14,12,0]) nr_bldg_pumpstation();
translate([28,12,0]) nr_prop_loadingdock();

translate([-28,-6,0]) nr_prop_helipad();
translate([-14,-6,0]) nr_prop_servicecart();
translate([0,-6,0]) nr_prop_supplyyard();
translate([14,-6,0]) nr_prop_containerstack();
translate([28,-6,0]) nr_prop_gantry();

translate([-28,-24,0]) nr_prop_pipeyard();
translate([-14,-24,0]) nr_prop_buoy();
translate([0,-24,0]) nr_prop_dockfender();
translate([14,-24,0]) nr_nature_grove();
translate([30,-24,0]) nr_ground_apron(variant = 1);

translate([-28,-44,0]) nr_bldg_shelter(variant = 1);
translate([-14,-44,0]) nr_bldg_shelter(variant = 2);
translate([0,-44,0]) nr_bldg_workshop(variant = 1);
translate([14,-44,0]) nr_bldg_warehouse(variant = 2);
translate([28,-44,0]) nr_prop_containerstack(seed = 47, variant = 2);

gk_camera_lookat([26,-66,117], [0,-6,0], name = "01_Scenery_catalogue", fov = 50);
gk_camera_lookat([0,1.9,75], [0,22,0], name = "02_Gameplay_scale", fov = 50);
gk_camera_lookat([-12,9,24], [-20,30,0], name = "03_Airfield_roofs", fov = 50);
gk_camera_lookat([29,-22,25], [20,-4,0], name = "04_Port_yards", fov = 50);
