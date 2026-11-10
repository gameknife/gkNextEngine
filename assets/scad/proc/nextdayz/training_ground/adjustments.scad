// Preserved gameplay additions: cover, routes, loot and encounters.
// Camp perimeter and street furniture that belong to no single site: the
// south and flank concrete fence with corner towers, lamps and finger posts
// along the camp street. Everything stands on nd_ground().
use <../../../lib/kit_training.scad>
use <../../../lib/kit_coldwar.scad>

// Concrete fence run from a to b in 4 m panels, each panel on its own ground.
module tg_adj_fence(a,b,seed=0)
{
    n=floor(norm(b-a)/4);
    d=(b-a)/norm(b-a);
    for(i=[0:n-1]) let(p=a+d*(i+0.5)*4)
        translate([p[0],p[1],nd_ground(p[0],p[1])]) rotate([0,0,atan2(d[1],d[0])]) tg_fence_panel(seed+i,1);
}

// Finger post with arrow boards: signs = [[label, yaw], ...].
module tg_adj_signpost(signs=[],seed=0)
{
    tg_m_timber(tg_wood(seed)) tg_post(3.0,0.12);
    for(i=[0:len(signs)-1]) rotate([0,0,signs[i][1]]) translate([0.95,-0.07,2.7-i*0.36])
    {
        tg_plate(signs[i][0],1.7,0.28,0.13,tg_white(),tg_black(),0.03);
        tg_m_gloss(tg_white()) translate([0.95,0,0]) rotate([0,45,0]) tg_box([0.2,0.03,0.2]);
    }
}

gk_flatten()
{
    // south boundary, broken by the gate (its own panels run x -94..-26)
    tg_adj_fence([-300,-251],[-96,-251],11);
    tg_adj_fence([-24,-251],[28,-251],12);
    // flanks up to the camp street
    tg_adj_fence([-304,-247],[-304,-95],13);
    tg_adj_fence([32,-247],[32,-142],14);
    for(p=[[-302,-249],[30,-249]]) translate([p[0],p[1],nd_ground(p[0],p[1])]) cw_bldg_guard_tower(p[0] < 0 ? 3 : 5);
    // lamps along the camp street and the range road
    for(x=[-240:40:20]) translate([x,-137,nd_ground(x,-137)]) cw_prop_lamp();
    for(y=[-110:40:150]) translate([-14,y,nd_ground(-14,y)]) cw_prop_lamp();
    // finger posts at the two main junctions
    translate([-131,-137,nd_ground(-131,-137)]) tg_adj_signpost([["ARMORY / MESS",90],["MOTOR POOL",180],["RANGE / GATE",0]],21);
    translate([34,-137,nd_ground(34,-137)]) tg_adj_signpost([["RIFLE RANGE",90],["HELIPAD",-10],["CAMP",180]],23);
}
