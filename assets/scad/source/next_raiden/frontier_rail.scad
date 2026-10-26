// Reserve rail slice: paired tracks, assembled train, freight and repair yards.
// Rails tile continuously at 16m. Train wheels sit on the 0.18m rail heads.
use <../../lib/kit_raiden_frontier.scad>
use <../../lib/gk_camera.scad>
$fn = 8;

for (row = [-5:5], col = [-4:4])
    translate([col*16,row*16,0])
    {
        seed = 211+row*97+col*43;
        if (col == 0) rf_ground_rail(seed,variant = 1);
        else if (col == 2 && row == 4) rf_ground_rail(seed);
        else rf_ground_industrial(seed,variant = abs(col) > 2 ? 2 : 0);
    }

translate([-3,-14,0.18]) rf_veh_locomotive();
translate([-3,-5.68,0.18]) rf_veh_freightcar();
translate([-3,2.24,0.18]) rf_veh_freightcar(variant = 1);
translate([-3,10.16,0.18]) rf_veh_freightcar(variant = 2);
translate([3,27,0.18]) rotate([0,0,180]) rf_veh_locomotive(variant = 1);
translate([0,18,0]) rf_prop_signalbridge(variant = 1);
translate([0,-32,0]) rf_prop_signalbridge(variant = 2);

translate([-17,24,0.02]) rotate([0,0,90]) rf_bldg_freightterminal(variant = 1);
translate([16,8,0.02]) rf_bldg_machinehall(variant = 2);
translate([-17,-16,0.02]) rf_bldg_machinehall(variant = 1);
translate([18,-26,0.02]) rf_bldg_cityblock(variant = 1);
translate([20,40,0.02]) rf_bldg_roundhouse(variant = 1);
translate([-18,46,0.02]) rf_bldg_cityblock(variant = 2);

translate([-15,4,0.02]) rf_prop_cableyard(variant = 1);
translate([16,-8,0.02]) rf_prop_scrapyard(seed = 97,variant = 2);
translate([-29,-3,0.02]) rf_prop_scrapyard(seed = 231);
translate([-30,22,0.02]) rf_prop_cableyard(variant = 2);
translate([33,10,0.02]) rf_bldg_freightterminal(variant = 2);
translate([-30,-37,0.02]) rf_bldg_cityblock();
translate([32,-45,0.02]) rf_bldg_cityblock(variant = 2);

// A short disconnected display siding, clear of the through lines.
translate([32,69,0.18]) rf_prop_railbuffer(variant = 2);

gk_camera_lookat([0,-20.1,75],[0,0,0], name = "01_Gameplay_75m", fov = 50);
gk_camera_lookat([48,-88,110],[0,8,0], name = "02_Whole_slice", fov = 50);
gk_camera_lookat([10,-32,17],[-3,-7,1], name = "03_Train_couplings", fov = 50);
