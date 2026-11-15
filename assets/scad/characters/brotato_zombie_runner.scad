// ============================================================================
// brotato_zombie_runner.scad - Brotato3D zombie: a hooded sprinter that charges (enemy id "charger").
//
// Geometry comes from lib/kit_bzombie.scad (variant "runner"); the bone tree below
// is the shared Brotato3D zombie skeleton.
// No clips here: they are adopted from brotato_zombie_walker.scad at load time.
// ============================================================================

use <../lib/kit_bzombie.scad>

$fn = 8;

V = "runner";

// --- Bones -------------------------------------------------------------------
// Pivots are the shared Brotato3D zombie skeleton: do not move them per variant,
// the clips in brotato_zombie_walker.scad are solved for exactly these.

module bone_jaw() { bz_part_jaw(V); }

module bone_head()
{
    bz_part_head(V);
    translate([0, 0.03, 0.06]) bone_jaw();
}

// The hands are part of the forearm meshes; the bones stay so clips can key them.
module bone_hand_l() {}
module bone_hand_r() {}

module bone_forearm_l()
{
    bz_part_forearm(V);
    translate([0, 0, -0.30]) bone_hand_l();
}
module bone_forearm_r()
{
    mirror([1, 0, 0]) bz_part_forearm(V, bitten = false);
    translate([0, 0, -0.30]) bone_hand_r();
}

module bone_upperarm_l()
{
    bz_part_upperarm(V);
    translate([0, 0, -0.32]) bone_forearm_l();
}
module bone_upperarm_r()
{
    mirror([1, 0, 0]) bz_part_upperarm(V, torn = false);
    translate([0, 0, -0.32]) bone_forearm_r();
}

module bone_torso()
{
    bz_part_torso(V);
    translate([0, 0, 0.56]) bone_head();
    translate([ 0.22, 0, 0.46]) bone_upperarm_l();
    translate([-0.22, 0, 0.46]) bone_upperarm_r();
}

module bone_foot_l() { bz_part_foot(V); }
module bone_foot_r() { mirror([1, 0, 0]) bz_part_foot(V, bare = true); }

module bone_calf_l()
{
    bz_part_calf(V);
    translate([0, 0, -0.42]) bone_foot_l();
}
module bone_calf_r()
{
    mirror([1, 0, 0]) bz_part_calf(V, torn = true);
    translate([0, 0, -0.42]) bone_foot_r();
}

module bone_thigh_l()
{
    bz_part_thigh(V);
    translate([0, 0, -0.43]) bone_calf_l();
}
module bone_thigh_r()
{
    mirror([1, 0, 0]) bz_part_thigh(V, bloody = false);
    translate([0, 0, -0.43]) bone_calf_r();
}

module bone_pelvis()
{
    bz_part_pelvis(V);
    translate([0, 0, 0.02]) bone_torso();
    translate([ 0.10, 0, -0.08]) bone_thigh_l();
    translate([-0.10, 0, -0.08]) bone_thigh_r();
}

module bone_root()
{
    translate([0, 0, 0.93]) bone_pelvis();
}

bone_root();
