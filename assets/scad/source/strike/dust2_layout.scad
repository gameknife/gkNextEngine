// Dust2-inspired gameplay blockout, not a measured BSP reconstruction.
// Metres, Z-up, north=+Y. B northwest, A northeast, attackers south.
// Exact reference-version timings and grenade lineups are not claimed.
use <../../lib/kit_strike.scad>

// [name, polygon, walking Z]. Floors share edges; no overlapping detail skins.
function ds_floors() = [
    ["t_spawn",[[10,6],[69,6],[69,20],[56,20],[56,28],[44,28],[44,20],[10,20]],2],
    ["outside_tunnels",[[10,20],[32,20],[32,47],[24,47],[24,55],[12,55],[12,47],[10,47]],2],
    ["upper_tunnel",[[12,55],[24,55],[24,65],[30,65],[30,75],[24,75],[24,85],[12,85]],2],
    ["b_site",[[6,85],[36,85],[36,112],[6,112]],2],
    ["lower_tunnel",[[34,58],[44,58],[44,65],[34,65]],0],
    ["top_mid",[[44,28],[56,28],[56,36],[44,36]],2],
    ["bottom_mid",[[44,58],[56,58],[56,85],[44,85]],0],
    ["catwalk",[[56,36],[63,36],[63,66],[70,66],[70,73],[56,73]],2],
    ["short_landing",[[70,66],[78,66],[78,78],[70,78]],2],
    ["short_a",[[70,86],[78,86],[78,95],[76,95],[76,92],[70,92]],4],
    ["ct_spawn",[[44,85],[88,85],[88,100],[44,100]],0],
    ["b_doors_landing",[[36,86],[38,86],[38,94],[36,94]],2],
    ["b_window_approach",[[36,94],[44,94],[44,112],[36,112]],0],
    ["a_site",[[76,95],[100,95],[100,112],[76,112]],4],
    ["t_long_landing",[[60,20],[63,20],[63,32],[60,32]],2],
    ["outside_long",[[69,20],[82,20],[82,32],[69,32]],0],
    ["long_doors",[[70,32],[82,32],[82,40],[88,40],[88,48],[70,48]],0],
    ["long",[[88,35],[100,35],[100,80],[88,80]],0],
    ["pit",[[88,23],[100,23],[100,27],[96,27],[96,35],[88,35]],-2]
];

// Names and positions are future gameplay authoring data, not active spawners.
function ds_spawns_t() = [[24,12,2],[27,12,2],[30,12,2],[33,12,2],[36,12,2]];
function ds_spawns_ct() = [[54,94,0],[57,94,0],[60,94,0],[63,94,0],[66,94,0]];
function ds_sites() = [["A",[86,104,4],[10,9]],["B",[19,100,2],[10,9]]];
function ds_callouts() = [
    ["T SPAWN",[33,12,2]],["OUTSIDE TUNNELS",[21,36,2]],
    ["UPPER TUNNELS",[18,70,2]],["LOWER TUNNELS",[38,62,0]],
    ["B",[19,100,2]],["B DOORS",[36,90,2]],["B WINDOW",[36,105,3.1]],
    ["TOP MID",[50,31,2]],["MID",[50,60,0.59]],["MID DOORS",[50,79,0]],
    ["CATWALK",[59.5,53,2]],["SHORT",[74,87,4]],
    ["CT SPAWN",[60,94,0]],["A",[86,104,4]],
    ["A RAMP",[94,90,2.67]],["LONG",[94,62,0]],
    ["PIT",[91,27,-2]],["LONG DOORS",[76,39,0]]
];

module ds_floor(poly,z,c=sk_CONCRETE())
{
    color(c) translate([0,0,z-0.30]) linear_extrude(height=0.30) polygon(points=poly);
}
module ds_wall(a,b,z=0,h=5,c=sk_LIGHT(),t=0.5)
{
    dx=b[0]-a[0]; dy=b[1]-a[1];
    translate([(a[0]+b[0])/2,(a[1]+b[1])/2,z]) rotate([0,0,atan2(dy,dx)])
        sk_bldg_wall(sqrt(dx*dx+dy*dy)+0.02,h,t,c);
}
module ds_wallchain(points,z=0,h=5,c=sk_LIGHT(),t=0.5)
{
    for(i=[0:len(points)-2]) ds_wall(points[i],points[i+1],z,h,c,t);
}
module ds_boxblock(x0,y0,x1,y1,h=8,seed=0,z=0)
{
    translate([(x0+x1)/2,(y0+y1)/2,z]) sk_bldg_block(x1-x0,y1-y0,h,seed);
}
module ds_surfaces()
{
    for(i=[0:len(ds_floors())-1])
    {
        f=ds_floors()[i];
        ds_floor(f[1],f[2],i==3 ? [0.31,0.36,0.33] : i==14 ? [0.27,0.31,0.32] : sk_CONCRETE());
    }
    // Ramps start 4cm below their walking plane, so joints have no lips.
    translate([50,58,-0.04]) rotate([0,0,180]) sk_ground_ramp(12,22,2);
    translate([34,61.5,0]) rotate([0,0,90]) sk_ground_stairs(7,10,2,14);
    translate([44,90,-0.04]) rotate([0,0,90]) sk_ground_ramp(8,6,2);
    translate([69,26,-0.04]) rotate([0,0,90]) sk_ground_ramp(12,6,2);
    translate([74,78,2]) sk_ground_stairs(8,8,2,14);
    translate([94,80,-0.04]) sk_ground_ramp(12,15,4);
    translate([98,27,-2.04]) sk_ground_ramp(4,8,2);
    // A short bridge: CT can rotate UNDER it (3.65m clear), not through a solid podium.
    // CT-to-A access at the east end of the underpass.
    translate([85,85,-0.04]) sk_ground_ramp(6,10,4);
}

module ds_structure(roofs=true)
{
    // Outer edge is continuous, including the sunken pit.
    ds_wallchain([[10,20],[10,6],[69,6],[69,20]],2,5.2);
    ds_wallchain([[69,20],[100,20],[100,112]],-2,9);
    ds_wallchain([[10,20],[10,47],[12,47],[12,55]],2,5.8);
    ds_wallchain([[12,55],[12,85],[6,85],[6,112],[36,112]],2,6.2);
    ds_wall([76,112],[100,112],4,5.5,sk_RED());
    ds_wall([36,112],[44,112],0,6);
    // Infill defines route occlusion. None of these blocks occupies a route floor.
    ds_boxblock(32,20,44,58,8,0,2);
    ds_boxblock(24,47,32,58,7,1,2);
    ds_boxblock(30,65,44,85,8,2,0);
    ds_boxblock(24,75,30,85,7,1,2);
    ds_boxblock(56,20,60,36,7,3,2);
    ds_boxblock(63,36,70,66,8,5,2);
    ds_boxblock(70,48,88,66,9,4,0);
    ds_boxblock(78,66,88,83.5,9,6,0);
    ds_boxblock(63,73,70,85,7,7,0);
    ds_boxblock(56,73.5,63,85,6.5,1,0);
    ds_boxblock(60,32,70,36,7,2,2);
    ds_boxblock(44,100,76,112,8,8,0);
    ds_boxblock(82,20,88,40,8,1,0);
    // Upper/lower tunnel partition and its B exit.
    ds_wall([24,55],[24,58],2,4.8,sk_TEAL());
    ds_wall([24,65],[30,65],2,4.8,sk_TEAL());
    ds_wall([24,75],[24,85],2,4.8,sk_TEAL());
    ds_wall([12,55],[24,55],5.8,1.0,sk_TEAL());
    translate([18,84.8,2]) sk_bldg_arch(12,5.3,4.2);
    // Mid doors and B doors: openings are the only breaks in the separators.
    translate([50,79,0]) sk_bldg_gate(12,4.2,5.8,4.0,0.65,76);
    ds_wall([36,85],[36,86.5],2,6,sk_TEAL());
    translate([36,90,2]) rotate([0,0,90]) sk_bldg_gate(7,4.0,6,4.0);
    ds_wall([36,93.5],[36,100],2,6,sk_TEAL());
    translate([36,106,2]) rotate([0,0,90]) sk_bldg_window_wall(12,6,0.65,1.1,2.6);
    // Catwalk overlooks mid behind a low parapet. High wall ends before the crossing.
    ds_wall([56,38],[56,72],0,2.75,sk_LIGHT(),0.32);
    ds_wall([56,73],[70,73],2,0.85,sk_LIGHT(),0.35);
    ds_wall([70,78],[70,85],2,1,sk_LIGHT(),0.35);
    // Long double doors are deliberately offset by a ninety-degree dogleg.
    translate([76,32,0]) sk_bldg_gate(12,4.1,5.8,4.0);
    ds_wall([70,32],[70,48],0,6,sk_TEAL());
    ds_wall([70,48],[88,48],0,6,sk_TEAL());
    translate([88,44,0]) rotate([0,0,90]) sk_bldg_gate(8,4.1,5.8,4.0);
    ds_wall([88,48],[88,80],0,7.8,sk_LIGHT());
    ds_wall([88,35],[96,35],-2,2.25,sk_CONCRETE(),0.4);
    ds_wall([88,23],[100,23],-2,4.5,sk_TEAL());
    ds_boxblock(88,20,100,23,5,0,-2);
    ds_wall([88,23],[88,35],-2,3.3,sk_TEAL());
    // CT under A / short, with clear columns outside the walking centreline.
    for(p=[[72,86],[78,91],[87,87]]) translate([p[0],p[1],0]) color(sk_CONCRETE()) sk_slab(0.55,0.55,3.7);
    ds_wall([76,99],[76,112],4,1,sk_LIGHT());
    // Guard the drop off the T/long ramp and the unoccupied tunnel corners.
    ds_wall([63,32],[69,32],0,3,sk_TEAL(),0.3);
    ds_wall([24,55],[32,55],2,4.8,sk_TEAL());
    ds_wall([30,75],[30,85],2,4.8,sk_TEAL());
    ds_wall([44,100],[44,112],0,6,sk_TEAL());
    if(roofs)
    {
        ds_floor([[12,55],[24,55],[24,65],[30,65],[30,75],[24,75],[24,85],[12,85]],7.4,sk_TEAL());
        ds_floor([[24,58],[44,58],[44,65],[24,65]],5.6,sk_TEAL());
        ds_floor([[70,32],[82,32],[82,40],[88,40],[88,48],[70,48]],6.1,sk_TEAL());
    }
}
