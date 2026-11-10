// Strike character parts, namespace so_. Metres, Z-up, faces -Y.
// Parts use bone-pivot origins (limbs extend down -Z), not floor anchors.
// Team 0: helmeted police / blue armour; team 1: red jacket / scarf / beanie.
function so_CLOTH(team=0) = team==0 ? [0.085,0.17,0.25] : [0.42,0.105,0.075];
function so_PANTS(team=0) = team==0 ? [0.10,0.16,0.21] : [0.20,0.25,0.22];
function so_ARMOR() = [0.075,0.10,0.115];
function so_SKIN() = [0.49,0.30,0.20];
function so_STEEL() = [0.16,0.20,0.22];
function so_BOOT() = [0.045,0.052,0.055];
function so_PATCH(team=0) = team==0 ? [0.12,0.47,0.65] : [0.70,0.42,0.08];
module so_box(s=[0.2,0.2,0.2]) { cube(s,center=true); }
module so_part_taper(w=0.4,d=0.25,h=0.4,top=0.85)
{
    hull()
    {
        translate([0,0,0.01]) so_box([w,d,0.02]);
        translate([0,0,h-0.01]) so_box([w*top,d*top,0.02]);
    }
}
module so_torso_pelvis(team=0)
{
    color(so_PANTS(team)) so_box([0.36,0.23,0.22]);
    color(so_ARMOR()) translate([0,0,0.055]) so_box([0.39,0.25,0.075]);
    color(so_STEEL()) translate([0,-0.135,0.055]) so_box([0.065,0.025,0.06]);
    color(so_ARMOR()) translate([0.22,0.02,-0.055]) so_box([0.08,0.12,0.19]);
}
module so_torso_jacket(team=0)
{
    color(so_SKIN()) translate([0,0,0.51]) cylinder(h=0.16,r=0.07,$fn=8);
    color(so_CLOTH(team)) so_part_taper(0.35,0.24,0.53,1.2);
    color(so_ARMOR()) translate([0,-0.142,0.12]) so_part_taper(0.36,0.08,0.32,0.9);
    color(so_ARMOR()) translate([0,0.145,0.26]) so_box([0.32,0.08,0.36]);
    for(s=[-1,1])
    {
        color(so_ARMOR()) translate([s*0.145,-0.03,0.43]) so_box([0.07,0.30,0.10]);
        color(so_CLOTH(team)) translate([s*0.095,-0.205,0.20]) so_box([0.08,0.065,0.16]);
        color(so_STEEL()) translate([s*0.095,-0.24,0.24]) so_box([0.055,0.014,0.02]);
    }
    color(so_PATCH(team)) translate([0,-0.19,0.385]) so_box([0.20,0.018,0.065]);
    color([0.60,0.63,0.60]) translate([-0.084,-0.207,0.372]) rotate([90,0,0])
        linear_extrude(height=0.006) text(team==0 ? "POLICE" : "R-07",size=0.042);
    if(team==1)
    {
        color([0.33,0.30,0.22]) translate([0,0,0.49]) scale([1.2,1,0.45]) sphere(r=0.15,$fn=8);
        color([0.33,0.30,0.22]) translate([-0.08,-0.155,0.40]) rotate([0,-12,0]) so_box([0.1,0.055,0.22]);
    }
    else
    {
        color(so_STEEL()) translate([0.15,0.19,0.4]) so_box([0.07,0.06,0.13]);
        color(so_BOOT()) translate([0.15,0.19,0.46]) cylinder(h=0.17,r=0.009,$fn=6);
    }
}
module so_head_operator(team=0)
{
    color(so_SKIN()) translate([0,0,0.125]) scale([0.91,0.91,1.05]) sphere(r=0.14,$fn=10);
    color(so_SKIN()) translate([0,-0.127,0.12]) so_box([0.045,0.045,0.055]);
    color(so_ARMOR()) translate([0,-0.10,0.054]) so_box([0.19,0.07,0.07]);
    if(team==0)
    {
        color(so_CLOTH(team)) translate([0,0.01,0.22]) scale([1.06,1.06,0.65]) sphere(r=0.16,$fn=10);
        color(so_ARMOR()) translate([0,-0.10,0.218]) so_box([0.27,0.15,0.045]);
        color([0.08,0.30,0.37]) translate([0,-0.135,0.153]) so_box([0.205,0.047,0.07]);
        color(so_STEEL()) for(s=[-1,1]) translate([s*0.143,0,0.14]) so_box([0.045,0.09,0.12]);
        color(so_PATCH()) translate([0,-0.17,0.26]) so_box([0.06,0.025,0.034]);
    }
    else
    {
        color(so_ARMOR()) translate([0,0.01,0.238]) scale([1,1,0.55]) sphere(r=0.145,$fn=10);
        color([0.33,0.30,0.22]) translate([0,-0.09,0.054]) so_box([0.23,0.10,0.10]);
        color(so_BOOT()) for(s=[-1,1]) translate([s*0.055,-0.133,0.16]) so_box([0.073,0.025,0.045]);
    }
}
module so_arm_upper(team=0)
{
    color(so_CLOTH(team)) translate([0,0,-0.30]) so_part_taper(0.135,0.145,0.30,1.15);
    color(so_PATCH(team)) translate([-0.078,0, -0.105]) so_box([0.018,0.07,0.085]);
}
module so_arm_forearm(team=0)
{
    color(so_CLOTH(team)) translate([0,0,-0.28]) so_part_taper(0.105,0.12,0.28,1.15);
    color(so_ARMOR()) translate([0,0.065,-0.045]) so_box([0.115,0.055,0.11]);
    color(so_BOOT()) translate([0,0,-0.25]) so_box([0.115,0.13,0.045]);
}
module so_arm_hand()
{
    color(so_BOOT()) translate([0,-0.014,-0.052]) so_box([0.105,0.115,0.105]);
    color(so_STEEL()) translate([0,-0.077,-0.045]) so_box([0.077,0.018,0.055]);
}
module so_leg_thigh(team=0)
{
    color(so_PANTS(team)) translate([0,0,-0.42]) so_part_taper(0.15,0.17,0.42,1.15);
    color(so_CLOTH(team)) translate([-0.085,0.015,-0.19]) so_box([0.045,0.115,0.15]);
}
module so_leg_calf(team=0)
{
    color(so_PANTS(team)) translate([0,0,-0.40]) so_part_taper(0.125,0.15,0.40,1.15);
    color(so_ARMOR()) translate([0,-0.095,-0.04]) so_box([0.135,0.045,0.13]);
    color(so_BOOT()) translate([0,0,-0.34]) so_box([0.145,0.17,0.12]);
}
module so_leg_boot()
{
    color(so_BOOT()) translate([0,-0.07,0.06]) so_box([0.17,0.31,0.12]);
    color(so_STEEL()) translate([0,-0.07,0.015]) so_box([0.175,0.315,0.03]);
    color([0.19,0.20,0.18]) for(y=[-0.10,-0.055,0]) translate([0,y,0.123]) so_box([0.09,0.014,0.01]);
}
// Socket weapon: upright, muzzle -Y, origin at receiver. Not a loot part.
module so_wpn_carbine(team=0)
{
    color(so_ARMOR()) translate([0,-0.05,0]) so_box([0.075,0.32,0.095]);
    color(team==0 ? so_STEEL() : [0.28,0.16,0.085]) translate([0,-0.30,0]) so_box([0.075,0.20,0.07]);
    color(so_STEEL()) translate([0,-0.40,0]) rotate([90,0,0]) cylinder(h=0.23,r=0.016,$fn=8);
    color(so_BOOT()) translate([0,0.20,-0.01]) so_box([0.075,0.20,0.105]);
    color(so_BOOT()) translate([0,0.015,-0.075]) rotate([-16,0,0]) so_box([0.055,0.06,0.14]);
    color(so_STEEL()) translate([0,-0.115,-0.12]) rotate([12,0,0]) so_box([0.055,0.08,0.18]);
    color(so_BOOT()) translate([0,-0.065,0.08]) so_box([0.05,0.10,0.05]);
    color(so_PATCH(team)) translate([0,-0.121,0.08]) so_box([0.025,0.015,0.025]);
}
