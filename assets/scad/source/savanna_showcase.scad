// Showcase of kit_savanna.scad: every building, prop and plant of the East
// African savanna kit on one swept compound. Review with
//   gnb shot --scene assets/scad/source/savanna_showcase.scad
use <../lib/gk_camera.scad>
use <../lib/kit_savanna.scad>
$fn=12;

gk_material([0.40,0.25,0.15],roughness=1) translate([0,0,-0.2]) cube([150,110,0.4],center=true);

// Row 1: trading centre.
translate([-52,24,0]) sv_bldg_duka(1,7,8,"DUKA");
translate([-42,24,0]) sv_bldg_duka(2,9,8,"HOTEL");
translate([-31,24,0]) sv_bldg_duka(5,6,8,"");
translate([-18,24,0]) sv_bldg_store(3,12,8);
translate([-2,24,0]) sv_bldg_house(1,8,6,"block");
translate([8,24,0]) sv_bldg_house(2,8,6,"mud");
translate([18,24,0]) sv_bldg_house(3,8,6,"iron");
translate([34,26,0]) sv_bldg_church(4,9,16);
translate([54,24,0]) rotate([0,0,-20]) sv_bldg_ranger(5,11,8);

// Row 2: school, homestead, boma.
translate([-44,0,0]) sv_bldg_block(6,20,7,"school");
translate([-20,0,0]) sv_bldg_block(7,13,7,"clinic");
translate([-4,0,0]) sv_bldg_rondavel(8,6.5,6.5);
translate([5,0,0]) sv_bldg_rondavel(9,6,6);
translate([12,1,0]) sv_prop_granary(3);
translate([22,0,0]) sv_bldg_manyatta(10,6.5,5);
translate([31,0,0]) sv_bldg_manyatta(11,7,5.2);
translate([44,0,0]) sv_prop_kraal(12,6);
translate([58,0,0]) sv_bldg_uniport(13);

// Row 3: safari camp and furniture.
translate([-50,-26,0]) sv_bldg_banda(14,12,9);
translate([-36,-26,0]) sv_bldg_tent(15,5,7);
translate([-28,-26,0]) sv_bldg_tent(16,5,7);
translate([-20,-28,0]) sv_prop_firepit(1);
translate([-10,-26,0]) sv_prop_gate_arch(2,7,"TALEK GATE");
translate([2,-26,0]) sv_prop_tank(1);
translate([8,-26,0]) sv_prop_tank_tower(2);
translate([14,-26,0]) sv_prop_trough(1);
translate([20,-26,0]) sv_prop_stall(1);
translate([24,-26,0]) sv_prop_stall(2);
translate([28,-26,0]) sv_prop_latrine(1);
translate([31,-26,0]) sv_prop_boda(1);
translate([34,-26,0]) sv_prop_jerrycans(2);
translate([37,-26,0]) sv_prop_sacks(1);
translate([41,-26,0]) sv_prop_termite(4);
translate([47,-26,0]) sv_prop_rocks(2);
translate([52,-26,0]) sv_prop_bones(1);
translate([55,-26,0]) sv_prop_flagpole(1);
translate([58,-26,0]) sv_prop_pole(1);
translate([63,-26,0]) sv_prop_mast(1);
translate([40,-18,0]) sv_prop_thorn(3,8);

// Row 4: flora.
translate([-56,-44,0]) sv_nature_acacia(1);
translate([-44,-44,0]) sv_nature_acacia(2,10);
translate([-32,-44,0]) sv_nature_fever(3);
translate([-18,-44,0]) sv_nature_fig(4);
translate([-4,-44,0]) sv_nature_balanites(5);
translate([6,-44,0]) sv_nature_euphorbia(6);
translate([14,-44,0]) sv_nature_croton(7);
translate([20,-44,0]) sv_nature_thornbush(8);
translate([24,-44,0]) sv_nature_grass(9);
translate([28,-44,0]) sv_nature_sisal(3);
translate([33,-44,0]) sv_nature_banana(11);
translate([38,-44,0]) sv_nature_maize(12);
translate([45,-44,0]) sv_nature_dead(13);
translate([54,-44,0]) sv_nature_palm(14);

gk_camera_lookat([-10,-105,55],[0,-8,2],"overview",44);
gk_camera_lookat([-58,2,7],[-30,22,2],"street",55);
