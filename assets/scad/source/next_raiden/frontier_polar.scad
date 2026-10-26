// Reserve polar slice: inland research station beside a frozen coastal shelf.
// Opaque ice and subdued accents retain silhouette contrast at the flight camera.
use <../../lib/kit_raiden_frontier.scad>
use <../../lib/gk_camera.scad>
$fn = 8;

for (row = [-5:5], col = [-4:4])
    translate([col*16,row*16,0])
    {
        seed = 307+row*131+col*47;
        if (col == 0) rf_ground_frozenroad(seed,variant = row%2 == 0 ? 1 : 2);
        else if (col >= 2) rf_ground_ice(seed);
        else rf_ground_snow(seed);
    }

translate([-16,22,0.02]) rf_bldg_radome(variant = 1);
translate([15,26,0.02]) rf_prop_disharray(variant = 2);
translate([-16,-5,0.02]) rf_bldg_polarstation(variant = 2);
translate([14,-11,0.02]) rf_bldg_weatherlab(variant = 1);
translate([-15,-30,0.02]) rf_bldg_generatorbank(variant = 2);
translate([16,3,0.02]) rf_prop_supplycache(variant = 1);
translate([-28,6,0.02]) rf_prop_supplycache(variant = 2);
translate([0,-15,0.06]) rf_veh_snowcat(variant = 2);
translate([12,11,0.02]) rf_veh_snowcat(variant = 1);
translate([-16,46,0.02]) rf_bldg_radome();
translate([16,49,0.02]) rf_bldg_polarstation(variant = 1);

translate([34,10,0.02]) rf_nature_iceberg(seed = 61,variant = 2);
translate([43,-17,0.02]) rf_nature_iceberg(seed = 13);
translate([32,39,0.02]) rf_nature_iceberg(seed = 117,variant = 1);
translate([19,-41,0.02]) rf_nature_iceberg(seed = -31);
translate([-32,30,0.02]) rf_nature_snowpines(seed = 71,variant = 1);
translate([-35,-13,0.02]) rf_nature_snowpines(seed = 23);
translate([-27,-45,0.02]) rf_nature_snowpines(seed = 109,variant = 2);

gk_camera_lookat([0,-20.1,75],[0,0,0], name = "01_Gameplay_75m", fov = 50);
gk_camera_lookat([46,-87,108],[0,8,0], name = "02_Whole_slice", fov = 50);
gk_camera_lookat([11,-23,19],[15,2,1], name = "03_Field_equipment", fov = 50);
