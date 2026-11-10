// NextDayz training-ground kit: buildings, range furniture, displays and
// obstacle course; all doors and target faces turn toward the camera (-y).
use <../lib/kit_training.scad>
use <../lib/gk_camera.scad>
$fn=12;
color([0.24,0.30,0.18]) translate([0,10,-0.1]) cube([150,110,0.2],center=true);
// row 1: buildings
translate([-55,40,0]) tg_bldg_hq(3,20,10);
translate([-28,40,0]) tg_bldg_armory(5,16,10);
translate([-2,40,0]) tg_bldg_nissen(7,16,8);
translate([24,40,0]) tg_bldg_barracks(9,24,9);
translate([50,40,0]) tg_tent(11,10,5.5);
translate([64,40,0]) tg_bldg_guardhouse(13);
// row 2: range
translate([-50,12,0]) tg_bldg_range_tower(17);
translate([-22,12,0]) tg_firing_shed(19,4,4.0,5);
translate([0,14,0]) tg_berm(14,3,2,8);
translate([12,10,0]) { tg_target_frame(21,"bull",1.5); translate([0,0,1.5]) tg_face_bull(0.4); }
translate([16,10,0]) { tg_target_frame(23,"figure",1.2); translate([0,0,1.2]) tg_face_figure(); }
translate([20,10,0]) { tg_plate_stand(25,1.0,0.3); translate([0,0,1.0]) tg_face_plate(0.3); }
translate([24,10,0]) tg_distance_marker(100);
translate([28,10,0]) tg_shooting_bench(27);
translate([36,12,0]) tg_hesco(29,3);
translate([44,12,0]) tg_sandbag_row(31,3,3);
translate([54,12,0]) tg_vehicle_shed(33,12,7);
// row 3: displays and furniture
translate([-55,-12,0]) tg_table_display(35,2.4,1.2);
translate([-50,-12,0]) tg_rifle_rack(37,6);
translate([-45,-12,0]) tg_shelf(39,2.0,0.6,4);
translate([-41,-12,0]) tg_mannequin(41);
translate([-38,-12,0]) tg_clothes_rail(43);
translate([-33,-12,0]) tg_notice_board(45);
translate([-28,-12,0]) tg_flagpole(47,9);
translate([-24,-12,0]) tg_barrier(49,6,0);
translate([-12,-12,0]) tg_fence_panel(51,2);
translate([-2,-12,0]) tg_fence_mesh(53,2);
translate([6,-12,0]) tg_ammo_box(55);
translate([8,-12,0]) tg_crate(57);
translate([12,-12,0]) tg_floodlight(59);
translate([18,-12,0]) tg_fuel_point(61);
translate([28,-12,0]) tg_camo_net(63,8,6);
// row 4: obstacle course and shoot house
translate([-50,-32,0]) tg_ob_wall(65);
translate([-44,-32,0]) tg_ob_beam(67);
translate([-36,-32,0]) tg_ob_bars(69);
translate([-27,-32,0]) tg_ob_net(71);
translate([-18,-32,0]) tg_ob_tires(73);
translate([-10,-32,0]) tg_ob_ladder(75);
translate([-2,-32,0]) tg_ob_trench(77);
translate([10,-32,0]) tg_ply_wall(79,4,2.4,1.0);
translate([16,-32,0]) tg_kerb(4);
translate([20,-32,0]) tg_white_rock(81);
gk_camera_lookat([-60,-95,55],[0,8,3],"training_kit",50);
