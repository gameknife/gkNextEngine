// NextRaiden boss. All 43 chunks remain independent bones in the animated asset.
// Doors pivot at their inner edges. Their open pose exposes the existing reactor.
// No fake destruction animation: gameplay will detach bones into physical fragments.
use <../lib/raiden_parts.scad>
use <../lib/raiden_airframes.scad>
$fn = 8;
FRAME = nr_fortress();
module piece(i) { nr_piece(FRAME[i], 1); }
module at(i) { r = FRAME[i]; translate(r[1]) rotate(r[8]) children(); }
module bone_core_front() { piece(0); }
module bone_core_rear() { piece(1); }
module bone_nose() { piece(2); }
module bone_bridge() { piece(3); }
module bone_tail() { piece(4); }
module bone_fin() { piece(5); }
module bone_door_l() { translate([-0.46,0,0]) piece(6); }
module bone_door_r() { translate([0.46,0,0]) piece(7); }
module bone_reactor() { piece(8); }
module bone_wing_l0() { piece(9); }
module bone_wing_l1() { piece(10); }
module bone_wing_l2() { piece(11); }
module bone_wing_l3() { piece(12); }
module bone_wing_l4() { piece(13); }
module bone_engine_l0() { piece(14); }
module bone_engine_l1() { piece(15); }
module bone_engine_l2() { piece(16); }
module bone_armour_l0() { piece(17); }
module bone_armour_l1() { piece(18); }
module bone_armour_l2() { piece(19); }
module bone_gun_l() { piece(20); }
module bone_pod_l() { piece(21); }
module bone_tailplane_l() { piece(22); }
module bone_outrigger_l() { piece(23); }
module bone_nose_plate_l() { piece(24); }
module bone_tail_plate_l() { piece(25); }
module bone_wing_r0() { piece(26); }
module bone_wing_r1() { piece(27); }
module bone_wing_r2() { piece(28); }
module bone_wing_r3() { piece(29); }
module bone_wing_r4() { piece(30); }
module bone_engine_r0() { piece(31); }
module bone_engine_r1() { piece(32); }
module bone_engine_r2() { piece(33); }
module bone_armour_r0() { piece(34); }
module bone_armour_r1() { piece(35); }
module bone_armour_r2() { piece(36); }
module bone_gun_r() { piece(37); }
module bone_pod_r() { piece(38); }
module bone_tailplane_r() { piece(39); }
module bone_outrigger_r() { piece(40); }
module bone_nose_plate_r() { piece(41); }
module bone_tail_plate_r() { piece(42); }
module bone_root()
{
    at(0) bone_core_front(); at(1) bone_core_rear(); at(2) bone_nose();
    at(3) bone_bridge(); at(4) bone_tail(); at(5) bone_fin();
    translate([-0.12,0,1.20]) bone_door_l(); translate([0.12,0,1.20]) bone_door_r();
    at(8) bone_reactor();
    at(9) bone_wing_l0(); at(10) bone_wing_l1(); at(11) bone_wing_l2();
    at(12) bone_wing_l3(); at(13) bone_wing_l4();
    at(14) bone_engine_l0(); at(15) bone_engine_l1(); at(16) bone_engine_l2();
    at(17) bone_armour_l0(); at(18) bone_armour_l1(); at(19) bone_armour_l2();
    at(20) bone_gun_l(); at(21) bone_pod_l(); at(22) bone_tailplane_l();
    at(23) bone_outrigger_l(); at(24) bone_nose_plate_l(); at(25) bone_tail_plate_l();
    at(26) bone_wing_r0(); at(27) bone_wing_r1(); at(28) bone_wing_r2();
    at(29) bone_wing_r3(); at(30) bone_wing_r4();
    at(31) bone_engine_r0(); at(32) bone_engine_r1(); at(33) bone_engine_r2();
    at(34) bone_armour_r0(); at(35) bone_armour_r1(); at(36) bone_armour_r2();
    at(37) bone_gun_r(); at(38) bone_pod_r(); at(39) bone_tailplane_r();
    at(40) bone_outrigger_r(); at(41) bone_nose_plate_r(); at(42) bone_tail_plate_r();
}
bone_root();
anim_idle = [["bone_root","pos",[[0,[0,0,0]], [1.5,[0,0,0.14]], [3,[0,0,0]]]]];
anim_open = [["loop",false],
    ["bone_door_l","rot",[[0,[0,0,0]], [0.8,[0,108,0]], [1.1,[0,108,0]]]],
    ["bone_door_r","rot",[[0,[0,0,0]], [0.8,[0,-108,0]], [1.1,[0,-108,0]]]]];
anim_close = [["loop",false],
    ["bone_door_l","rot",[[0,[0,108,0]], [0.8,[0,0,0]]]],
    ["bone_door_r","rot",[[0,[0,-108,0]], [0.8,[0,0,0]]]]];
anim_core_exposed = [["loop",false],
    ["bone_door_l","rot",[[0,[0,108,0]]]], ["bone_door_r","rot",[[0,[0,-108,0]]]]];
anim_fire = [["loop",false],
    ["bone_gun_l","pos",[[0,[0,0,0]], [0.09,[0,0.28,0]], [0.35,[0,0,0]]]],
    ["bone_gun_r","pos",[[0,[0,0,0]], [0.09,[0,0.28,0]], [0.35,[0,0,0]]]]];
