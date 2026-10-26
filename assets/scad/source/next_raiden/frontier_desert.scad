// Reserve desert slice: the utility road separates extraction and processing.
// Decorative sample only. No waves, target metadata or stage registration.
use <../../lib/kit_raiden_frontier.scad>
use <../../lib/gk_camera.scad>
$fn = 8;

for (row = [-5:5], col = [-4:4])
    translate([col*16,row*16,0])
    {
        seed = 131+row*113+col*37;
        if (col == 0) rf_ground_serviceway(seed,variant = row == 1 ? 1 : 0);
        else if (col >= 3) rf_ground_saltflat(seed);
        else rf_ground_desert(seed,variant = abs(col) == 4 ? 1 : 0);
    }

translate([-16,22,0.02]) rf_bldg_refinery(variant = 1);
translate([17,27,0.02]) rf_bldg_coolingplant(variant = 1);
translate([-16,-12,0.02]) rf_bldg_drillsite(variant = 2);
translate([16,-17,0.02]) rf_bldg_desertcompound(variant = 2);
translate([-14,48,0.02]) rf_bldg_refinery(variant = 2);
translate([17,49,0.02]) rf_bldg_coolingplant();

translate([-17,4,0.02]) rf_prop_pumpjack();
translate([-27,-6,0.02]) rf_prop_pumpjack(variant = 2);
translate([16,4,0.02]) rf_prop_solararray(variant = 1);
translate([28,-7,0.02]) rf_prop_solararray(variant = 2);
translate([-1,-10,0.075]) rf_veh_haultruck(variant = 1);
translate([9,16,0.1]) rotate([0,0,90]) rf_veh_haultruck();

for (y = [-16,0,16,32]) translate([-33,y,0.02]) rf_prop_pipeline();
translate([-33,48,0.02]) rf_prop_pipeline(variant = 1);
translate([-17,48,0.02]) rotate([0,0,-90]) rf_prop_pipeline(variant = 1);

translate([-41,27,0.02]) rf_nature_mesa(seed = 21,variant = 1);
translate([40,-28,0.02]) rf_nature_mesa(seed = 81,variant = 2);
translate([-30,-40,0.02]) rf_nature_mesa(seed = -7);
translate([35,45,0.02]) rf_bldg_desertcompound(variant = 1);
translate([14,-42,0.02]) rf_prop_solararray(variant = 1);

gk_camera_lookat([0,-20.1,75],[0,0,0], name = "01_Gameplay_75m", fov = 50);
gk_camera_lookat([45,-83,105],[0,8,0], name = "02_Whole_slice", fov = 50);
gk_camera_lookat([-1,-3,23],[-16,18,0], name = "03_Refinery_pumpjack", fov = 50);
