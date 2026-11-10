// NextRaiden tilt-wing gunship: 18 chunks plus two non-rendered hinge bones.
// Hinges are at the wing roots, not their centres. Mesh scale/mirror stays inside bones.
use <../lib/raiden_parts.scad>
use <../lib/raiden_airframes.scad>
$fn = 8;
FRAME = nr_gunship();
module piece(i) { nr_piece(FRAME[i], 4); }
module at(i, pivot = [0,0,0]) { r = FRAME[i]; translate(r[1]-pivot) rotate(r[8]) children(); }
module bone_core() { piece(0); }
module bone_nose() { piece(1); }
module bone_canopy() { piece(2); }
module bone_deck() { piece(3); }
module bone_tail_l() { piece(4); }
module bone_tail_r() { piece(5); }
module bone_wing_l() { piece(6); }
module bone_tip_l() { piece(7); }
module bone_engine_l() { piece(8); }
module bone_pod_l() { piece(9); }
module bone_gun_l() { piece(10); }
module bone_fin_l() { piece(11); }
module bone_wing_r() { piece(12); }
module bone_tip_r() { piece(13); }
module bone_engine_r() { piece(14); }
module bone_pod_r() { piece(15); }
module bone_gun_r() { piece(16); }
module bone_fin_r() { piece(17); }
module bone_hinge_l()
{
    at(6, [-0.74,0.10,0.46]) bone_wing_l();
    at(7, [-0.74,0.10,0.46]) bone_tip_l();
    at(8, [-0.74,0.10,0.46]) bone_engine_l();
}
module bone_hinge_r()
{
    at(12, [0.74,0.10,0.46]) bone_wing_r();
    at(13, [0.74,0.10,0.46]) bone_tip_r();
    at(14, [0.74,0.10,0.46]) bone_engine_r();
}
module bone_root()
{
    at(0) bone_core(); at(1) bone_nose(); at(2) bone_canopy(); at(3) bone_deck();
    at(4) bone_tail_l(); at(5) bone_tail_r();
    at(9) bone_pod_l(); at(10) bone_gun_l(); at(11) bone_fin_l();
    at(15) bone_pod_r(); at(16) bone_gun_r(); at(17) bone_fin_r();
    translate([-0.74,0.10,0.46]) bone_hinge_l();
    translate([0.74,0.10,0.46]) bone_hinge_r();
}
bone_root();
anim_idle = [["bone_root","pos",[[0,[0,0,0]], [1,[0,0,0.10]], [2,[0,0,0]]]]];
anim_deploy = [["loop",false],
    ["bone_hinge_l","rot",[[0,[0,58,0]], [0.8,[0,0,0]], [1,[0,0,0]]]],
    ["bone_hinge_r","rot",[[0,[0,-58,0]], [0.8,[0,0,0]], [1,[0,0,0]]]]];
anim_fold = [["loop",false],
    ["bone_hinge_l","rot",[[0,[0,0,0]], [0.8,[0,58,0]]]],
    ["bone_hinge_r","rot",[[0,[0,0,0]], [0.8,[0,-58,0]]]]];
anim_fire = [["loop",false],
    ["bone_gun_l","pos",[[0,[0,0,0]], [0.06,[0,0.2,0]], [0.22,[0,0,0]]]],
    ["bone_gun_r","pos",[[0,[0,0,0]], [0.11,[0,0.2,0]], [0.28,[0,0,0]]]]];
anim_bank_left = nr_bank(-14);
anim_bank_right = nr_bank(14);
