// Shared 17-bone Strike skeleton and in-place clips. No top-level geometry.
// Sole at zero, pelvis .90m, total height ~1.86m. Face -Y.
// The torso-owned socket keeps the carbine stable; both arms solve to its grips.
use <../lib/kit_strike_char.scad>
function so_arm_target(left=true) = left ? [0.115,-0.37,0.40] : [0.16,-0.12,0.38];
function so_arm_vector(left=true) = let(p=so_arm_target(left)) [p[0]-(left ? -0.255 : 0.255),p[1],p[2]-0.51];
function so_arm_upper_rot(left=true) = let(v=so_arm_vector(left),d=norm(v),r=sqrt(v[0]*v[0]+v[1]*v[1]))
    [-(atan2(r,-v[2])-acos((0.30*0.30+d*d-0.28*0.28)/(0.60*d))),0,atan2(v[0],-v[1])];
function so_arm_elbow(left=true) = let(v=so_arm_vector(left),d=norm(v))
    -acos((d*d-0.30*0.30-0.28*0.28)/(2*0.30*0.28));

module bone_weapon_socket(team=0) { so_wpn_carbine(team); }
module bone_hand_l() { so_arm_hand(); }
module bone_hand_r() { mirror([1,0,0]) so_arm_hand(); }
module bone_forearm_l(team=0) { so_arm_forearm(team); translate([0,0,-0.28]) bone_hand_l(); }
module bone_forearm_r(team=0) { mirror([1,0,0]) so_arm_forearm(team); translate([0,0,-0.28]) bone_hand_r(); }
module bone_upperarm_l(team=0,ready=false)
{
    so_arm_upper(team);
    translate([0,0,-0.30]) rotate([ready ? so_arm_elbow(true) : 0,0,0]) bone_forearm_l(team);
}
module bone_upperarm_r(team=0,ready=false)
{
    mirror([1,0,0]) so_arm_upper(team);
    translate([0,0,-0.30]) rotate([ready ? so_arm_elbow(false) : 0,0,0]) bone_forearm_r(team);
}
module bone_head(team=0) { so_head_operator(team); }
module bone_torso(team=0,ready=false)
{
    so_torso_jacket(team);
    translate([0,0,0.62]) bone_head(team);
    translate([-0.255,0,0.51]) rotate(ready ? so_arm_upper_rot(true) : [0,0,0]) bone_upperarm_l(team,ready);
    translate([0.255,0,0.51]) rotate(ready ? so_arm_upper_rot(false) : [0,0,0]) bone_upperarm_r(team,ready);
    translate([0.16,-0.15,0.44]) bone_weapon_socket(team);
}
module bone_foot_l() { so_leg_boot(); }
module bone_foot_r() { mirror([1,0,0]) so_leg_boot(); }
module bone_calf_l(team=0) { so_leg_calf(team); translate([0,0,-0.40]) bone_foot_l(); }
module bone_calf_r(team=0) { mirror([1,0,0]) so_leg_calf(team); translate([0,0,-0.40]) bone_foot_r(); }
module bone_thigh_l(team=0) { so_leg_thigh(team); translate([0,0,-0.42]) bone_calf_l(team); }
module bone_thigh_r(team=0) { mirror([1,0,0]) so_leg_thigh(team); translate([0,0,-0.42]) bone_calf_r(team); }
module bone_pelvis(team=0,ready=false)
{
    so_torso_pelvis(team);
    translate([0,0,0.02]) bone_torso(team,ready);
    translate([-0.105,0,-0.08]) bone_thigh_l(team);
    translate([0.105,0,-0.08]) bone_thigh_r(team);
}
module bone_root(team=0,ready=false) { translate([0,0,0.90]) bone_pelvis(team,ready); }

function so_ready() = [
    ["bone_upperarm_l","rot",[[0,so_arm_upper_rot(true)]]],
    ["bone_forearm_l","rot",[[0,[so_arm_elbow(true),0,0]]]],
    ["bone_upperarm_r","rot",[[0,so_arm_upper_rot(false)]]],
    ["bone_forearm_r","rot",[[0,[so_arm_elbow(false),0,0]]]]
];
// Two-link leg IK in the sagittal plane. Foot rotation cancels the chain.
function so_hip(y,z) = atan2(y,z)-acos(min(1,(0.42*0.42+y*y+z*z-0.40*0.40)/(0.84*sqrt(y*y+z*z))));
function so_knee(y,z) = acos(max(-1,min(1,(y*y+z*z-0.42*0.42-0.40*0.40)/(0.84*0.40))));
function so_phase(t,side) = (t+side*0.5)%1;
function so_foot_y(p,stride) = p<=0.5 ? -stride+4*stride*p : stride*cos((p-0.5)*360);
function so_foot_z(p,lift) = p<=0.5 ? 0 : lift*sin((p-0.5)*360);
function so_leg_keys(side,joint,duration,stride,drop,lift) = [for(i=[0:16])
    let(p=so_phase(i/16,side),y=so_foot_y(p,stride),z=0.82-drop-so_foot_z(p,lift),a=so_hip(y,z),b=so_knee(y,z))
    [i*duration/16,[joint==0 ? a : joint==1 ? b : -a-b,0,0]]];
function so_clip_move(duration=0.9,stride=0.18,drop=0.035,lift=0.10) = concat(so_ready(),[
    ["bone_pelvis","pos",[[0,[0,0,-drop]]]],
    ["bone_thigh_l","rot",so_leg_keys(0,0,duration,stride,drop,lift)],
    ["bone_calf_l","rot",so_leg_keys(0,1,duration,stride,drop,lift)],
    ["bone_foot_l","rot",so_leg_keys(0,2,duration,stride,drop,lift)],
    ["bone_thigh_r","rot",so_leg_keys(1,0,duration,stride,drop,lift)],
    ["bone_calf_r","rot",so_leg_keys(1,1,duration,stride,drop,lift)],
    ["bone_foot_r","rot",so_leg_keys(1,2,duration,stride,drop,lift)]
]);
function so_clip_idle() = concat(so_ready(),[
    ["bone_torso","rot",[[0,[0,0,0]],[1.2,[0.7,0,0]],[2.4,[0,0,0]]]],
    ["bone_head","rot",[[0,[0,0,-3]],[1.2,[0,0,3]],[2.4,[0,0,-3]]]]
]);
function so_clip_crouch() = concat(so_ready(),[
    ["bone_pelvis","pos",[[0,[0,0,-0.34]]]],
    ["bone_thigh_l","rot",[[0,[so_hip(0,0.48),0,0]]]],
    ["bone_calf_l","rot",[[0,[so_knee(0,0.48),0,0]]]],
    ["bone_foot_l","rot",[[0,[-so_hip(0,0.48)-so_knee(0,0.48),0,0]]]],
    ["bone_thigh_r","rot",[[0,[so_hip(0,0.48),0,0]]]],
    ["bone_calf_r","rot",[[0,[so_knee(0,0.48),0,0]]]],
    ["bone_foot_r","rot",[[0,[-so_hip(0,0.48)-so_knee(0,0.48),0,0]]]]
]);
function so_clip_fire() = concat(so_ready(),[
    ["loop",false],
    ["bone_torso","rot",[[0,[0,0,0]],[0.045,[-3,0,0]],[0.18,[0,0,0]]]],
    ["bone_weapon_socket","pos",[[0,[0,0,0]],[0.045,[0,0.035,0]],[0.18,[0,0,0]]]]
]);
function so_clip_reload() = [
    ["loop",false],
    ["bone_upperarm_r","rot",[[0,so_arm_upper_rot(false)]]],
    ["bone_forearm_r","rot",[[0,[so_arm_elbow(false),0,0]]]],
    ["bone_upperarm_l","rot",[[0,so_arm_upper_rot(true)],[0.35,[-5,0,-15]],[0.85,[-25,0,35]],[1.4,so_arm_upper_rot(true)]]],
    ["bone_forearm_l","rot",[[0,[so_arm_elbow(true),0,0]],[0.35,[-50,0,0]],[0.85,[-105,0,0]],[1.4,[so_arm_elbow(true),0,0]]]],
    ["bone_head","rot",[[0,[0,0,0]],[0.5,[15,0,-10]],[1.4,[0,0,0]]]]
];
function so_clip_use() = concat(so_clip_crouch(),[
    ["loop",false],
    ["bone_torso","rot",[[0,[0,0,0]],[0.3,[25,0,0]],[1.2,[25,0,0]],[1.5,[0,0,0]]]]
]);
