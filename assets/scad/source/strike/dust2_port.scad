// Main playable architecture asset. No gameplay or static character obstacles.
use <../../lib/kit_strike.scad>
use <../../lib/gk_camera.scad>
use <dust2_layout.scad>
$fn=12;

module ds_cover()
{
    // A: default, double stack, goose, short box and long-side car.
    translate([86,104,4.018]) sk_ground_site("A",10,9,sk_RED());
    translate([83,105,4]) sk_prop_cargo(3.2,2.7,2.5,sk_RED());
    translate([86.4,105,4]) sk_prop_crate(2.6,2.6,1.5);
    translate([83,105,6.55]) sk_prop_crate(2.9,2.3,1.8,sk_TEAL());
    translate([79,109,4]) sk_prop_crate(2,1.8,1.4);
    translate([73.8,89,4]) sk_prop_crate(1.4,1.4,1.1);
    translate([96.7,77,0]) rotate([0,0,90]) sk_veh_hatchback(sk_TEAL());
    // B: back platform, default crates, tunnel-entry box and car corner.
    translate([19,100,2.018]) sk_ground_site("B",10,9,sk_TEAL());
    translate([18,108.5,2]) sk_ground_paving(16,6,0.6,sk_LIGHT());
    translate([18,108.5,2.6]) sk_prop_cargo(3.8,2.5,2.4,sk_TEAL());
    translate([20,99,2]) sk_prop_crate(2.7,2.6,1.6);
    translate([22.8,99,2]) sk_prop_crate(2.7,2.6,2.7);
    translate([28.5,94,2]) sk_prop_cargo(2.8,2.5,2.2,sk_RED());
    translate([9,89,2]) rotate([0,0,85]) sk_veh_hatchback(sk_RED());
    translate([13,88,2]) sk_prop_crate(2.2,1.8,1.6);
    translate([33.8,106,2]) sk_prop_crate(1.4,2,0.65);
    // B window boost: explicit climbable steps on the CT side.
    for(i=[0:3]) translate([40.8-i,106,0]) sk_ground_paving(1,2.2,(i+1)*0.78,sk_CONCRETE());
    // Mid Xbox and right-hand close cover, leaving the centreline open.
    translate([53.3,65,0]) sk_prop_crate(2.4,2.8,1.45);
    translate([53.3,68,0]) sk_prop_crate(1.4,1.2,0.72);
    translate([46,74,0]) sk_prop_crate(1.6,1.5,1.6);
    translate([40,87,1.33]) sk_prop_barrel();
    // T approach, outside-long blue container, tunnel pillars and cover.
    translate([14,29,2]) sk_prop_cargo(3,2.4,2.7,sk_RED());
    translate([29,44,2]) sk_prop_crate(2.4,2,2.2);
    translate([14,68,2]) sk_prop_crate(1.7,2,1.4);
    translate([27.8,72,2]) sk_prop_crate(1.7,1.7,2.2);
    translate([79.5,27.5,0]) sk_prop_cargo(3,2.5,2.7,sk_TEAL());
    translate([89.8,49.8,0]) sk_prop_barrel(sk_TEAL());
    translate([90.5,30,-2]) sk_prop_crate(1.5,1.4,0.8);
}

module ds_details()
{
    // Long: a recognizable freight street, relief stays within 0.5m of walls.
    for(y=[54,63,72])
    {
        translate([88.34,y,0]) rotate([0,0,90]) sk_bldg_shutter(4,3.2,sk_TEAL());
        translate([99.64,y,0]) rotate([0,0,-90]) sk_bldg_shutter(3,2.8,[0.30,0.34,0.36]);
        translate([88.36,y,4.5]) rotate([0,0,90]) sk_bldg_window(1.4,1.8);
        translate([99.64,y,4.1]) rotate([0,0,-90]) sk_bldg_window(1.2,1.5);
    }
    for(y=[50,59,68,77])
    {
        translate([88.48,y,0]) sk_prop_downpipe(7.4);
        translate([99.56,y,0]) sk_prop_downpipe(6.5);
    }
    translate([88.52,58,1.0]) rotate([0,0,90]) sk_prop_electrical();
    translate([88.6,63,3.7]) rotate([0,0,90]) sk_prop_sign("PORT 02",5.5,sk_TEAL());
    translate([99.5,71,3.4]) rotate([0,0,-90]) sk_prop_sign("FREIGHT",5.5,sk_RED());
    for(x=[89,99]) translate([x,63,0]) rotate([0,0,90]) sk_ground_edge(32,0.55);
    // Mid and CT carry a distinct set of shuttered workshops and utility boxes.
    for(y=[31,40,49])
    {
        translate([43.97,y,5]) rotate([0,0,90]) sk_bldg_window();
        translate([43.95,y,2.1]) rotate([0,0,90]) sk_bldg_shutter(3.3,2.5,sk_RED());
    }
    for(x=[50,58,66])
    {
        translate([x,99.8,0]) sk_bldg_shutter(4,3.1,sk_TEAL());
        translate([x+2.7,99.6,0]) sk_prop_downpipe(7.4);
    }
    translate([58,99.5,4.1]) sk_prop_sign("CUSTOMS",7,sk_TEAL());
    translate([8,100,2]) rotate([0,0,90]) sk_ground_edge(15,0.7);
    translate([98.8,104,4]) rotate([0,0,90]) sk_ground_edge(14,0.65);
    for(y=[92,99,106]) translate([6.36,y,5]) rotate([0,0,90]) sk_bldg_window();
    for(x=[11,20,29]) translate([x,111.62,5]) sk_bldg_window(1.5,1.8);
    // Signage at decisions, not across the walkable openings.
    translate([79,111.65,6]) sk_prop_sign("A",1.5,sk_RED());
    translate([10,111.65,4.5]) sk_prop_sign("B",1.5,sk_TEAL());
    translate([44.35,88,2.3]) rotate([0,0,-90]) sk_prop_sign("B",1.4,sk_TEAL());
    translate([87.65,64,2.5]) rotate([0,0,90]) sk_prop_sign("A",1.4,sk_RED());
    translate([33,6.35,4]) rotate([0,0,180]) sk_prop_sign("T",1.4,sk_RED());
    translate([60,99.6,2.2]) sk_prop_sign("CT",2,sk_TEAL());
    for(p=[[33,20,7],[43.6,40,5],[79,48.3,3.6],[66,100,4]])
        translate(p) sk_bldg_awning(5,1.2);
    for(p=[[36,40,10],[83,55,9],[49,107,8],[65,79,7]]) translate(p) sk_bldg_roof_vent();
    for(p=[[39,29,10],[67,56,10],[59,107,8]]) translate(p) sk_bldg_tank();
    for(p=[[12,8,2],[67,8,2],[8,110,2],[98,110,4],[98,52,0],[46,97,0]]) translate(p) sk_prop_lamp();
    for(p=[[16,8,2],[65,8,2],[8,106,2],[97,108,4],[46,96,0]]) translate(p) sk_nature_planter();
    for(p=[[48,104,8],[71,108,8]]) translate(p) sk_nature_cypress(4);
    sk_prop_cable([44,38,7],[56,38,7],0.7);
    sk_prop_cable([88,55,6],[100,55,6],0.7);
    sk_prop_cable([12,21,6],[32,21,7],1);
    for(p=[[19,83,2],[50,77,0],[94,60,0],[76,30,0]]) translate(p) sk_ground_drain(3.5);
    for(p=[[11,39,2],[8,97,2],[97,100,4],[55,98,0]]) translate(p) sk_prop_pallet();
    // Thin high ceiling beams preserve the tunnel silhouette and clearance.
    for(y=[58,65,72,79]) translate([18,y,6.6]) color(sk_STEEL()) sk_box([12,0.22,0.24]);
}

module ds_map(roofs=true)
{
    ds_surfaces();
    ds_structure(roofs);
    ds_cover();
    ds_details();
}

ds_map();
gk_camera_lookat([122,-40,115],[53,60,0],"01 Port overview",48);
gk_camera_lookat([54,58,165],[54,58,0],"02 Tactical plan",45);
gk_camera_lookat([94,42,1.7],[93,99,5],"03 Long to A",70);
gk_camera_lookat([50,32,3.7],[50,80,1.7],"04 Top mid",70);
gk_camera_lookat([18,82,3.7],[22,103,3.7],"05 Tunnel to B",72);
gk_camera_lookat([58,96,1.7],[80,89,2.7],"06 CT underpass",70);
gk_camera_lookat([74,86,5.7],[87,105,5.7],"07 Short to A",70);
gk_camera_lookat_key([122,-40,115],[53,60,0],"Port flyover",0,48);
gk_camera_lookat_key([145,98,85],[55,67,0],"Port flyover",12,48);
gk_camera_lookat_key([28,135,75],[52,75,0],"Port flyover",24,48);
