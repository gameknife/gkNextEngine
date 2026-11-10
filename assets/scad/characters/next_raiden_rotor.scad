// NextRaiden rotary gunship. One root, one bone per detachable chunk.
// Static kit and rig share all 16 records. Empty rotor pivot owns no geometry.
// Remove a chunk's render subtree before handing its world pose to physics.
// Do not release bone_rotor as one piece: its four blades are separate children.
use <../lib/raiden_parts.scad>
use <../lib/raiden_airframes.scad>
$fn = 8;
FRAME = nr_rotorcraft();
module piece(i) { nr_piece(FRAME[i], 3); }
module at(i, pivot = [0,0,0]) { r = FRAME[i]; translate(r[1]-pivot) rotate(r[8]) children(); }
module bone_core() { piece(0); }
module bone_nose() { piece(1); }
module bone_canopy() { piece(2); }
module bone_tail() { piece(3); }
module bone_tail_fin() { piece(4); }
module bone_engine_l() { piece(5); }
module bone_engine_r() { piece(6); }
module bone_pod_l() { piece(7); }
module bone_pod_r() { piece(8); }
module bone_mast() { piece(9); }
module bone_hub() { piece(10); }
module bone_chin_gun() { piece(11); }
module bone_blade_0() { piece(12); }
module bone_blade_1() { piece(13); }
module bone_blade_2() { piece(14); }
module bone_blade_3() { piece(15); }
module bone_rotor()
{
    at(10, [0,0.15,1.70]) bone_hub();
    at(12, [0,0.15,1.70]) bone_blade_0();
    at(13, [0,0.15,1.70]) bone_blade_1();
    at(14, [0,0.15,1.70]) bone_blade_2();
    at(15, [0,0.15,1.70]) bone_blade_3();
}
module bone_root()
{
    at(0) bone_core();
    at(1) bone_nose();
    at(2) bone_canopy();
    at(3) bone_tail();
    at(4) bone_tail_fin();
    at(5) bone_engine_l();
    at(6) bone_engine_r();
    at(7) bone_pod_l();
    at(8) bone_pod_r();
    at(9) bone_mast();
    at(11) bone_chin_gun();
    translate([0,0.15,1.70]) bone_rotor();
}
bone_root();
anim_idle = [nr_spin("bone_rotor", 0.8)];
anim_hover = [nr_spin("bone_rotor", 0.64),
    ["bone_root","pos",[[0,[0,0,0]], [0.32,[0,0,0.10]], [0.64,[0,0,0]]]]];
anim_fire = [["loop",false], nr_spin("bone_rotor",0.24),
    ["bone_chin_gun","pos",[[0,[0,0,0]], [0.06,[0,0.17,0]], [0.24,[0,0,0]]]]];
anim_bank_left = concat(nr_bank(-13), [nr_spin("bone_rotor",1)]);
anim_bank_right = concat(nr_bank(13), [nr_spin("bone_rotor",1)]);
