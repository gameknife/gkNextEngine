// ============================================================================
// brotato_survivor.scad - The Brotato3D hero: a road-trip survivor on the
// NextDayz articulated skeleton.
//
// Same contract as nextdayz_survivor.scad (1 unit = 1 m, Z-up, faces -Y, the
// _r bones at -X, weapon frame / IK target / socket bones), so the weapon
// models of brotato_weapons.scad drop straight into the firing hand. The body
// is dressed from the nextdayz_body.scad garment shells; the jacket is
// ROLECOLOR and takes the selected character colour at runtime.
//
// Runtime contract (Brotato3D FPlayerRigVisual):
//   locomotion  stand_idle, stand_run_f / b / l / r  (one sync group; the body
//               faces the aim, the legs go where the stick points)
//   hold        weapon_ready, weapon_aim  (arms + IK bones; torso / head additive)
//   recoil      recoil_rifle  (additive, retriggered per shot)
//   action      dash  (a single lunging pose, blended in for the dash)
// Locomotion is in-place; the game owns translation.
// ============================================================================

use <../lib/rig_humanoid.scad>
use <nextdayz_body.scad>

$fn = 8;

// --- Skeleton ----------------------------------------------------------------
// Must match the bone_* translations below; the clip generators read it.

SKEL = rh_skeleton([
    ["pelvis_z", 0.94],
    ["hip", [0.105, 0, -0.08]],
    ["thigh", 0.44],
    ["calf", 0.42],
    ["heel", 0.085],
    ["ball", 0.21],
    ["torso_z", 0.02],
    ["shoulder", [0.235, 0, 0.48]],
    ["upperarm", 0.31],
    ["forearm", 0.29]
]);

// --- Weapon hold ---------------------------------------------------------------
// Weapon frame = the weapon model origin; muzzle -Y, top +Z. The rifle stock
// butt sits STOCK behind it. Holds are built from the shoulder pocket so the
// butt stays in the firing-side shoulder whatever the blade and pitch.

POCKET = [-0.14, -0.12, 0.42];
STOCK  = 0.34;

// Right wrist on the grip (inverse of the socket below) and left wrist under
// the handguard, both in weapon space.
GRIP_R = [[0, 0.0837, 0.0547], [-20, 0, 0]];
GRIP_L = [[0.045, -0.20, -0.075], [-160, -30, 0]];

// blade: torso yaw away from the target (right shoulder back), cancelled in
// the weapon frame so the muzzle stays on the body axis. pitch: + = muzzle down.
function nd_shoulder_hold(blade, pitch, pocket = POCKET, inward = 5) =
    let(e = [pitch, 0, blade + inward])
    [pocket - rh_mv(rh_m(e), [0, STOCK, 0]), e];

READY_BLADE = 20;
AIM_BLADE   = 30;
HOLD_READY  = nd_shoulder_hold(READY_BLADE, 14);
HOLD_AIM    = nd_shoulder_hold(AIM_BLADE, 0, [-0.13, -0.12, 0.47]);
HOLD_UP     = nd_shoulder_hold(AIM_BLADE, -38, [-0.13, -0.11, 0.48]);
HOLD_DOWN   = nd_shoulder_hold(AIM_BLADE, 38, [-0.13, -0.14, 0.44]);
// Sprint: rifle across the chest, muzzle up toward the support shoulder.
HOLD_SPRINT = [[0.0, -0.30, 0.30], [-50, 0, 45]];
// Lowered to the firing side, muzzle down (switching, traversal, looting).
HOLD_LOW    = [[-0.24, -0.22, 0.14], [68, 0, -18]];

RIG = [
    ["weapon_bind", HOLD_READY],
    ["grip_r", GRIP_R],
    ["grip_l", GRIP_L],
    ["pole_r", [-0.50, 0.08, 0.12]],
    ["pole_l", [0.45, -0.15, 0.08]]
];

// --- Outfit ------------------------------------------------------------------

ROLECOLOR = [1.00, 0.00, 1.00];
PANTS  = [0.20, 0.21, 0.17];
PANTS_D = [0.14, 0.15, 0.12];
BELT   = [0.10, 0.08, 0.06];
BOOT   = [0.13, 0.10, 0.08];
SOLE   = [0.06, 0.06, 0.06];
PACK   = [0.34, 0.36, 0.22];
PACK_D = [0.24, 0.26, 0.16];
ROLL   = [0.62, 0.50, 0.32];
CAP    = [0.12, 0.13, 0.15];
ZIP    = [0.78, 0.76, 0.70];

module part_pelvis()
{
    color(PANTS) ndb_shell_seat();
    color(BELT) ndb_band(ndb_trouser_seat(), 0.070, 0.112, 0.008, m = 12, n = 8);
    color(ZIP) translate([0, -0.124, 0.092]) cube([0.046, 0.012, 0.034], center = true);
}

module part_torso()
{
    color(ROLECOLOR) ndb_shell_coat();
    // Zip, a darker hem and collar so the jacket reads from above.
    color(ZIP) translate([0, -0.141, 0.260]) cube([0.012, 0.008, 0.440], center = true);
    color(PANTS_D) ndb_band(ndb_coat(), -0.020, 0.030, 0.006, m = 12, n = 10);
    color(PANTS_D) translate([0, 0.004, 0.500]) rf_box([0.230, 0.180, 0.034], 0.012);
    // Road-trip pack with a bedroll: the strongest read from the top-down camera.
    color(PACK) translate([0, 0.205, 0.250]) rf_box([0.290, 0.150, 0.360], 0.030);
    color(PACK_D) translate([0, 0.292, 0.190]) rf_box([0.210, 0.040, 0.170], 0.016);
    color(ROLL) translate([0, 0.200, 0.470]) rotate([0, 90, 0]) cylinder(h = 0.360, r = 0.062, center = true, $fn = 10);
    color(PACK_D) for (x = [-0.115, 0.115]) translate([x, 0.200, 0.470]) rotate([0, 90, 0]) cylinder(h = 0.022, r = 0.066, center = true, $fn = 10);
    color(PACK_D) for (x = [-0.120, 0.120]) translate([x, -0.020, 0.300]) rf_box([0.044, 0.300, 0.370], 0.010);
}

module part_head()
{
    ndb_part_head();
    color(CAP) translate([0, 0.012, 0.226]) rf_box([0.204, 0.236, 0.066], 0.026);
    color(CAP) translate([0, -0.150, 0.204]) rf_box([0.170, 0.110, 0.016], 0.006);
    color(ROLECOLOR) translate([0, -0.104, 0.232]) cube([0.070, 0.008, 0.030], center = true);
}

module part_upperarm() { color(ROLECOLOR) ndb_shell_sleeve(false); }
module part_forearm()
{
    color(ROLECOLOR) ndb_shell_sleeve(true);
    color(PANTS_D) ndb_band(ndb_sleeve_fore(), -0.299, -0.262, 0.006);
}
module part_hand() { ndb_part_hand(); }
module part_thigh()
{
    color(PANTS) ndb_shell_leg(false);
    color(PANTS_D) translate([0.098, 0, -0.210]) rf_box([0.030, 0.120, 0.150], 0.010);
}
module part_calf() { color(PANTS) ndb_shell_leg(true); }
module part_foot()
{
    color(BOOT) ndb_boot_upper();
    color(SOLE) ndb_boot_sole();
}

// --- Bones -------------------------------------------------------------------

module bone_weapon_socket()
{
    // Empty on purpose: the runtime weapon hangs here. Local -Y is the muzzle.
}

module bone_ik_hand_r() {}
module bone_ik_hand_l() {}

module bone_ik_weapon()
{
    translate(GRIP_R[0]) rotate(GRIP_R[1]) bone_ik_hand_r();
    translate(GRIP_L[0]) rotate(GRIP_L[1]) bone_ik_hand_l();
}

module bone_hand_l() { part_hand(); }
module bone_hand_r()
{
    mirror([1, 0, 0]) part_hand();
    translate([0, -0.06, -0.08]) rotate([20, 0, 0]) bone_weapon_socket();
}

module bone_forearm_l()
{
    part_forearm();
    translate([0, 0, -0.29]) bone_hand_l();
}
module bone_forearm_r()
{
    mirror([1, 0, 0]) part_forearm();
    translate([0, 0, -0.29]) bone_hand_r();
}

module bone_upperarm_l()
{
    part_upperarm();
    translate([0, 0, -0.31]) bone_forearm_l();
}
module bone_upperarm_r()
{
    mirror([1, 0, 0]) part_upperarm();
    translate([0, 0, -0.31]) bone_forearm_r();
}

module bone_head() { part_head(); }

module bone_torso()
{
    part_torso();
    translate([0, 0, 0.58]) bone_head();
    translate([ 0.235, 0, 0.48]) bone_upperarm_l();
    translate([-0.235, 0, 0.48]) bone_upperarm_r();
    translate(HOLD_READY[0]) rotate(HOLD_READY[1]) bone_ik_weapon();
}

module bone_foot_l() { part_foot(); }
module bone_foot_r() { mirror([1, 0, 0]) part_foot(); }

module bone_calf_l()
{
    part_calf();
    translate([0, 0, -0.42]) bone_foot_l();
}
module bone_calf_r()
{
    mirror([1, 0, 0]) part_calf();
    translate([0, 0, -0.42]) bone_foot_r();
}

module bone_thigh_l()
{
    part_thigh();
    translate([0, 0, -0.44]) bone_calf_l();
}
module bone_thigh_r()
{
    mirror([1, 0, 0]) part_thigh();
    translate([0, 0, -0.44]) bone_calf_r();
}

module bone_pelvis()
{
    part_pelvis();
    translate([0, 0, 0.02]) bone_torso();
    translate([ 0.105, 0, -0.08]) bone_thigh_l();
    translate([-0.105, 0, -0.08]) bone_thigh_r();
}

module bone_root()
{
    translate([0, 0, 0.94]) bone_pelvis();
}

bone_root();

// --- Gaits ---------------------------------------------------------------------
// One gait at the base move speed of the game; the runtime cadence-matches it to
// the real speed (move-speed upgrades) instead of blending through walk / sprint.

GAIT_COMMON = [
    ["keys", 32], ["head_level", 0.85], ["gaze_pitch", -2]
];

GAIT_RUN = rh_gait_params(concat(GAIT_COMMON, [
    ["cycle", 0.62], ["speed", 5.0], ["duty", 0.32], ["stride_bias", 0.12],
    ["step_height", 0.18], ["lift_skew", 0.6], ["swing_tangent", 0.35], ["swing_out", 0.02],
    ["heel_strike", 6], ["heel_until", 0.10], ["toe_off", 58], ["toe_from", 0.45], ["swing_pitch", 36],
    ["width", 0.09], ["pelvis", [0, -0.02, -0.11]], ["bob", 0.034], ["bob_sign", -1], ["sway", 0.012],
    ["hip_pitch", 7], ["hip_yaw", 9], ["hip_roll", 4], ["strafe_turn", 45],
    ["torso_lean", 9], ["torso_bounce", 2], ["torso_counter", 1.3], ["strafe_lean", 6],
    ["arm_base", 10], ["arm_swing", 35], ["arm_out", 8], ["elbow_base", 75], ["elbow_swing", 25]
]));

anim_stand_run_f = rh_gait_clip(SKEL, GAIT_RUN, 0);
anim_stand_run_b = rh_gait_clip(SKEL, GAIT_RUN, 180);
anim_stand_run_l = rh_gait_clip(SKEL, GAIT_RUN, 90);
anim_stand_run_r = rh_gait_clip(SKEL, GAIT_RUN, -90);

// --- Stance --------------------------------------------------------------------

function nd_level_head(pelvisRot, torsoRot, gaze = [-2, 0, 0]) =
    [gaze[0] - (pelvisRot[0] + torsoRot[0]) * 0.85,
     gaze[1] - (pelvisRot[1] + torsoRot[1]) * 0.85,
     gaze[2] - (pelvisRot[2] + torsoRot[2]) * 0.85];

function nd_breath_key(base, phase, depth) =
    let(b = sin(360 * phase),
        pelvisPos = rh_get(base, "pelvis") + [0, 0, -0.006 * b * depth],
        pelvisRot = rh_get(base, "pelvis_rot"),
        torsoRot = rh_get(base, "torso") + [1.2 * b * depth, 0, 0.4 * b * depth])
    concat([["pelvis", pelvisPos], ["pelvis_rot", pelvisRot], ["torso", torsoRot],
            ["head", nd_level_head(pelvisRot, torsoRot)]], base);

function nd_breath_clip(base, cycle, depth) =
    rh_clip_from_poses(concat(rh_lower_bones(), [["bone_torso", "rot"], ["bone_head", "rot"]],
                              [["bone_upperarm_l", "rot"], ["bone_forearm_l", "rot"],
                               ["bone_upperarm_r", "rot"], ["bone_forearm_r", "rot"]]),
                       [for (i = [0 : 8]) cycle * i / 8],
                       [for (i = [0 : 8])
                           let(key = nd_breath_key(base, i / 8, depth))
                           concat(rh_lower_pose(SKEL, key), [rh_get(key, "torso"), rh_get(key, "head")],
                                  rh_get(base, "arms"))],
                       true);

STAND_BASE = [
    ["pelvis", [0, 0.02, -0.06]], ["pelvis_rot", [3, 0, -10]], ["torso", [5, 0, 8]],
    ["foot_l", [0.13, -0.08, 12, 0]], ["foot_r", [-0.15, 0.17, -28, 0]],
    ["arms", [[-8, -6, 0], [-22, 0, 0], [-6, 6, 0], [-18, 0, 0]]]
];

anim_stand_idle = nd_breath_clip(STAND_BASE, 3.2, 1.0);

// --- Weapon holds (upper body) -------------------------------------------------

function nd_hold(weapon, blade, extra = []) =
    concat(extra, [["weapon", weapon], ["torso", [0, 0, -blade]], ["head", [0, 0, blade * 0.9]]]);

anim_weapon_ready = rh_upper_clip(SKEL, RIG, [[0, nd_hold(HOLD_READY, READY_BLADE)]], true);
anim_weapon_aim   = rh_upper_clip(SKEL, RIG, [[0, nd_hold(HOLD_AIM, AIM_BLADE,
                                                   [["head", [6, -8, AIM_BLADE * 0.9]]])]], true);

// Additive kick: starts and ends at identity so it can be retriggered per shot.
anim_recoil_rifle = [
    ["loop", false],
    ["bone_torso", "rot", [[0, [0, 0, 0]], [0.04, [-3.5, 0, -1.2]], [0.11, [-1.0, 0, -0.3]],
                           [0.15, [0.6, 0, 0.2]], [0.2, [0, 0, 0]]]],
    ["bone_head", "rot", [[0, [0, 0, 0]], [0.04, [-2.0, 0, 0]], [0.11, [-0.5, 0, 0]], [0.15, [0.3, 0, 0]],
                          [0.2, [0, 0, 0]]]],
    ["bone_ik_weapon", "pos", [[0, [0, 0, 0]], [0.04, [0, 0.035, 0.008]], [0.11, [0, 0.010, 0.003]],
                               [0.15, [0, -0.004, 0]], [0.2, [0, 0, 0]]]],
    ["bone_ik_weapon", "rot", [[0, [0, 0, 0]], [0.04, [-4.5, 0, 1.0]], [0.11, [-1.2, 0, 0.3]],
                               [0.15, [0.5, 0, 0]], [0.2, [0, 0, 0]]]]
];

// --- Dash ----------------------------------------------------------------------
// A committed lunge: weight thrown forward, rear leg trailing. Held while the
// dash lasts and blended out on the recovery.

HOLD_SQUARE = nd_shoulder_hold(0, 14);

function nd_air_key(pelvis, pelvisRot, torso, footL, footR) =
    [["pelvis", pelvis], ["pelvis_rot", pelvisRot], ["torso", torso],
     ["head", nd_level_head(pelvisRot, torso)], ["foot_l", footL], ["foot_r", footR],
     ["weapon", HOLD_SQUARE]];

anim_dash = rh_body_clip(SKEL, RIG, [
    [0.0, nd_air_key([0, -0.10, -0.26], [30, 0, -6], [16, 0, 6], [0.12, -0.42, 8, 6], [-0.12, 0.46, -14, 62, 0.20])],
    [0.3, nd_air_key([0, -0.10, -0.25], [30, 0, -6], [16, 0, 6], [0.12, -0.42, 8, 6], [-0.12, 0.46, -14, 62, 0.20])]
], true);
