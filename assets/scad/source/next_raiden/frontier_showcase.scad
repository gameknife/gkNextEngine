// All 36 reserve modules, grouped north-to-south: desert, rail, polar.
// This is an authoring gallery, not a playable stage or a target registry.
use <../../lib/kit_raiden_frontier.scad>
use <../../lib/raiden_parts.scad>
use <../../lib/gk_camera.scad>
$fn = 8;

color([0.14,0.19,0.18]) translate([0,0,-0.5]) nr_box([132,128,0.5]);

// Raise the tile bases onto the display board; placed scenery keeps its z=0 origin.
translate([-50,50,0.4]) rf_ground_desert();
translate([-30,50,0.4]) rf_ground_saltflat();
translate([-10,50,0.4]) rf_ground_serviceway();
translate([10,50,0]) rf_bldg_refinery();
translate([30,50,0]) rf_bldg_coolingplant();
translate([50,50,0]) rf_bldg_drillsite();
translate([-50,30,0]) rf_prop_pumpjack();
translate([-30,30,0]) rf_prop_solararray();
translate([-10,30,0]) rf_prop_pipeline();
translate([10,30,0]) rf_bldg_desertcompound();
translate([30,30,0]) rf_nature_mesa();
translate([50,30,0]) rf_veh_haultruck();

translate([-50,10,0.4]) rf_ground_rail();
translate([-30,10,0.4]) rf_ground_industrial();
translate([-10,10,0]) rf_bldg_roundhouse();
translate([10,10,0]) rf_bldg_freightterminal();
translate([30,10,0]) rf_bldg_machinehall();
translate([50,10,0]) rf_bldg_cityblock();
translate([-50,-10,0]) rf_prop_signalbridge();
translate([-30,-10,0]) rf_prop_railbuffer();
translate([-10,-10,0]) rf_veh_locomotive();
translate([10,-10,0]) rf_veh_freightcar();
translate([30,-10,0]) rf_prop_cableyard();
translate([50,-10,0]) rf_prop_scrapyard();

translate([-50,-30,0.4]) rf_ground_snow();
translate([-30,-30,0.4]) rf_ground_ice();
translate([-10,-30,0.4]) rf_ground_frozenroad();
translate([10,-30,0]) rf_bldg_polarstation();
translate([30,-30,0]) rf_bldg_radome();
translate([50,-30,0]) rf_prop_disharray();
translate([-50,-50,0]) rf_bldg_generatorbank();
translate([-30,-50,0]) rf_bldg_weatherlab();
translate([-10,-50,0]) rf_prop_supplycache();
translate([10,-50,0]) rf_veh_snowcat();
translate([30,-50,0]) rf_nature_iceberg();
translate([50,-50,0]) rf_nature_snowpines();

gk_camera_lookat([15,-90,166],[0,0,0], name = "01_All_36_modules", fov = 50);
gk_camera_lookat([0,19.9,75],[0,40,0], name = "02_Desert_gameplay_scale", fov = 50);
gk_camera_lookat([0,-20.1,75],[0,0,0], name = "03_Rail_gameplay_scale", fov = 50);
gk_camera_lookat([0,-60.1,75],[0,-40,0], name = "04_Polar_gameplay_scale", fov = 50);
