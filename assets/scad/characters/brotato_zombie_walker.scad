// ============================================================================
// brotato_zombie_walker.scad - Brotato3D zombie: the common shambler (enemy id "rat").
//
// Geometry comes from lib/kit_bzombie.scad (variant "walker"); the bone tree below
// is the shared Brotato3D zombie skeleton.
//
// This file also owns the CLIPS for every Brotato3D zombie. The other variants
// carry no anim_* data: Brotato3D copies these clips onto them by bone name at
// load time (FZombieRigLibrary), which is why the pivots must not drift apart.
//   loops    idle, walk, walk_limp, run, hobble, reach, crawl
//   actions  alert (also the spit / heal / lob wind-up), attack_lunge,
//            attack_swipe, stagger
//   deaths   death_back, death_forward, death_crumple (hold their last frame)
// The gaits and full-body actions are the rig_humanoid ones the NextDayz
// infected uses (feet IK-planted on z = 0); crawl is what a zombie does once
// its legs are shot off.
// ============================================================================

use <../lib/kit_bzombie.scad>
use <../lib/rig_humanoid.scad>

$fn = 8;

// Must match the bone_* translations below; the clip generators read it.
SKEL = rh_skeleton([
    ["pelvis_z", 0.93],
    ["hip", [0.10, 0, -0.08]],
    ["thigh", 0.43],
    ["calf", 0.42],
    ["heel", 0.08],
    ["ball", 0.19],
    ["torso_z", 0.02],
    ["shoulder", [0.22, 0, 0.46]],
    ["upperarm", 0.32],
    ["forearm", 0.30]
]);

V = "walker";

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
    mirror([1, 0, 0]) bz_part_forearm(V, bitten = true);
    translate([0, 0, -0.30]) bone_hand_r();
}

module bone_upperarm_l()
{
    bz_part_upperarm(V);
    translate([0, 0, -0.32]) bone_forearm_l();
}
module bone_upperarm_r()
{
    mirror([1, 0, 0]) bz_part_upperarm(V, torn = true);
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
    mirror([1, 0, 0]) bz_part_thigh(V, bloody = true);
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

// --- Helpers -------------------------------------------------------------------

// Appends the jaw channel: keys are [[time, open degrees], ...].
function inf_jaw(clip, keys) = concat(clip, [["bone_jaw", "rot", [for (k = keys) [k[0], [k[1], 0, 0]]]]]);

// A gait clip with infected arms, a lolling head and a working jaw on top of
// rh_gait_pose's legs and spine.
//   arms = [raise_l, raise_r, swing, out, elbow_l, elbow_r, flail]  (degrees)
//   head = [nod, roll, yaw] wobble amplitudes;  jaw = [open, flap]
function inf_gait_pose(g, phase, arms, head, jaw) =
    let(p = rh_gait_pose(SKEL, g, 0, phase),
        // Arms trail the legs like pendulums; flail adds an uncoordinated twitch.
        lag = cos(360 * phase - 50),
        f1 = sin(720 * phase), f2 = sin(720 * phase + 100),
        upperL = [-(arms[0] - arms[2] * lag) - arms[6] * f1, -(arms[3] + arms[6] * 0.6 * f2), 0],
        upperR = [-(arms[1] + arms[2] * lag) - arms[6] * f2, arms[3] + arms[6] * 0.6 * f1, 0],
        foreL = [-(arms[4] + arms[2] * 0.6 * max(0, lag)), 0, 0],
        foreR = [-(arms[5] + arms[2] * 0.6 * max(0, -lag)), 0, 0],
        handL = [12 + 10 * lag, 0, -8],
        handR = [12 - 10 * lag, 0, 8],
        headRot = p[3] + [head[0] * cos(720 * phase - 40), head[1] * sin(360 * phase - 40),
                          head[2] * sin(360 * phase)],
        jawRot = [jaw[0] + jaw[1] * (0.5 + 0.5 * sin(720 * phase + 60)), 0, 0])
    [p[0], p[1], p[2], headRot, p[4], p[5], p[6], p[7], p[8], p[9], upperL, foreL, upperR, foreR,
     handL, handR, jawRot];

function inf_gait_clip(g, arms, head, jaw) =
    let(n = rg_keys(g), cycle = rg_cycle(g))
    rh_clip_from_poses(concat(rh_gait_bones(), [["bone_hand_l", "rot"], ["bone_hand_r", "rot"], ["bone_jaw", "rot"]]),
                       [for (i = [0 : n]) cycle * i / n],
                       [for (i = [0 : n]) inf_gait_pose(g, i / n, arms, head, jaw)], true);

// Full-body clips are solved every 1/30 s (see rh_dense_keys) so feet stay on
// the ground while the body falls or lunges between keys.
function inf_body_clip(keys, loop) = rh_free_body_clip(SKEL, rh_dense_keys(keys, 1 / 30), loop);

// One full-body key: arms are FK [upperarm, forearm(, hand)] unless a wrist
// target is given in `extra` (["wrist_l", [pos, euler]] ...).
function inf_key(pelvis, pelvisRot, torso, head, footL, footR, armL, armR, extra = []) =
    concat(extra, [["pelvis", pelvis], ["pelvis_rot", pelvisRot], ["torso", torso], ["head", head],
                   ["foot_l", footL], ["foot_r", footR], ["arm_l", armL], ["arm_r", armR]]);

// --- Gaits ---------------------------------------------------------------------
// Short, shuffling steps with the head hung; a lurching chase with the torso
// thrown forward; and both again on a lame right leg (rg_limp).

GAIT_SHAMBLE = rh_gait_params([
    ["keys", 24], ["cycle", 1.12], ["speed", 0.95], ["duty", 0.68], ["stride_bias", 0.04],
    ["step_height", 0.055], ["lift_skew", 1.0], ["swing_tangent", 0.2], ["swing_out", 0.035],
    ["heel_strike", 4], ["heel_until", 0.12], ["toe_off", 22], ["toe_from", 0.62], ["swing_pitch", 6],
    ["width", 0.13], ["pelvis", [0, 0.03, -0.07]], ["bob", 0.02], ["bob_sign", 1], ["sway", 0.045],
    ["hip_pitch", 6], ["hip_yaw", 6], ["hip_roll", 5],
    ["torso_lean", 16], ["torso_bounce", 3], ["torso_counter", 0.9],
    ["head_level", 0.25], ["gaze_pitch", 10]
]);

GAIT_LIMP = rh_gait_params([
    ["keys", 24], ["cycle", 1.25], ["speed", 0.80], ["duty", 0.70], ["stride_bias", 0.03],
    ["step_height", 0.06], ["lift_skew", 1.0], ["swing_tangent", 0.2], ["swing_out", 0.03],
    ["heel_strike", 4], ["heel_until", 0.12], ["toe_off", 24], ["toe_from", 0.62], ["swing_pitch", 6],
    ["width", 0.14], ["pelvis", [0, 0.03, -0.08]], ["bob", 0.018], ["bob_sign", 1], ["sway", 0.05],
    ["hip_pitch", 7], ["hip_yaw", 6], ["hip_roll", 4],
    ["torso_lean", 18], ["torso_bounce", 3], ["torso_counter", 0.8],
    ["head_level", 0.25], ["gaze_pitch", 12], ["limp", 0.85]
]);

GAIT_RUN = rh_gait_params([
    ["keys", 24], ["cycle", 0.66], ["speed", 3.2], ["duty", 0.38], ["stride_bias", 0.08],
    ["step_height", 0.13], ["lift_skew", 0.7], ["swing_tangent", 0.3], ["swing_out", 0.05],
    ["heel_strike", 4], ["heel_until", 0.10], ["toe_off", 45], ["toe_from", 0.5], ["swing_pitch", 26],
    ["width", 0.12], ["pelvis", [0, 0.02, -0.11]], ["bob", 0.035], ["bob_sign", -1], ["sway", 0.035],
    ["hip_pitch", 12], ["hip_yaw", 10], ["hip_roll", 6],
    ["torso_lean", 24], ["torso_bounce", 4], ["torso_counter", 0.6],
    ["head_level", 0.65], ["gaze_pitch", -6]
]);

GAIT_HOBBLE = rh_gait_params([
    ["keys", 24], ["cycle", 0.85], ["speed", 1.7], ["duty", 0.52], ["stride_bias", 0.05],
    ["step_height", 0.09], ["lift_skew", 0.8], ["swing_tangent", 0.25], ["swing_out", 0.04],
    ["heel_strike", 4], ["heel_until", 0.10], ["toe_off", 34], ["toe_from", 0.55], ["swing_pitch", 14],
    ["width", 0.13], ["pelvis", [0, 0.03, -0.10]], ["bob", 0.03], ["bob_sign", 1], ["sway", 0.05],
    ["hip_pitch", 10], ["hip_yaw", 8], ["hip_roll", 5],
    ["torso_lean", 22], ["torso_bounce", 3], ["torso_counter", 0.7],
    ["head_level", 0.55], ["gaze_pitch", -2], ["limp", 0.9]
]);

anim_walk      = inf_gait_clip(GAIT_SHAMBLE, [8, 22, 10, 8, 14, 30, 0], [4, 7, 5], [8, 6]);
anim_walk_limp = inf_gait_clip(GAIT_LIMP, [6, 14, 12, 10, 12, 24, 0], [5, 10, 6], [10, 6]);
anim_run       = inf_gait_clip(GAIT_RUN, [62, 78, 20, 12, 28, 22, 9], [6, 6, 4], [22, 10]);
anim_hobble    = inf_gait_clip(GAIT_HOBBLE, [40, 55, 18, 14, 30, 30, 8], [6, 8, 5], [18, 8]);

// --- Idle ----------------------------------------------------------------------
// Hunched, rocking from foot to foot, head lolling, jaw working.

function inf_idle_key(ph) =
    let(s = sin(360 * ph), c = cos(360 * ph), s2 = sin(720 * ph + 40))
    inf_key([0.025 * s, 0.03, -0.07 - 0.008 * s2], [6, 3 * s, -6 + 3 * c], [20 + 2 * s2, -3 * s, 4 - 3 * c],
            [14 + 5 * c, 10 * sin(360 * ph - 60), -6 + 8 * s],
            [0.14, -0.04, 10, 0], [-0.13, 0.08, -18, 0],
            [[-6 - 3 * s, -8, 0], [-14 - 4 * s2, 0, 0], [10, 0, -6]],
            [[-18 + 3 * s, 10, 0], [-34 - 4 * s, 0, 0], [14, 0, 8]]);

anim_idle = inf_jaw(rh_free_body_clip(SKEL, [for (i = [0 : 12]) [3.6 * i / 12, inf_idle_key(i / 12)]], true),
                    [for (i = [0 : 12]) [3.6 * i / 12, 10 + 6 * sin(1080 * i / 12)]]);

// --- Reach (arms only) ---------------------------------------------------------
// Layered over walk / run as the infected closes on its prey: both hands out
// at chest height, clutching.

function inf_reach_key(ph) =
    let(s = sin(360 * ph), c = cos(360 * ph))
    [["wrist_l", [[0.19 + 0.02 * c, -0.52 - 0.05 * s, 0.40 + 0.04 * c], [-95 + 10 * s, 0, 12]]],
     ["wrist_r", [[-0.19 + 0.02 * s, -0.54 + 0.05 * s, 0.43 - 0.04 * s], [-95 - 10 * s, 0, -12]]],
     ["pole_l", [0.50, -0.05, 0.10]], ["pole_r", [-0.50, -0.05, 0.10]]];

anim_reach = rh_free_upper_clip(SKEL, [for (i = [0 : 8]) [1.0 * i / 8, inf_reach_key(i / 8)]], true);

// --- Shared action poses -------------------------------------------------------

STAND_FEET_L = [0.13, -0.06, 8, 0];
STAND_FEET_R = [-0.13, 0.08, -14, 0];
STAND = inf_key([0, 0.02, -0.08], [6, 0, 0], [14, 0, 0], [-6, 0, 0], STAND_FEET_L, STAND_FEET_R,
                [[-20, -10, 0], [-25, 0, 0]], [[-24, 10, 0], [-30, 0, 0]]);

READY_FEET_L = [0.13, -0.10, 8, 0];
READY_FEET_R = [-0.13, 0.12, -16, 0];
READY_WRISTS = [["wrist_l", [[0.20, -0.42, 0.34], [-80, 0, 10]]], ["wrist_r", [[-0.20, -0.44, 0.38], [-80, 0, -10]]]];
READY = inf_key([0, 0.03, -0.10], [8, 0, 0], [18, 0, 0], [-10, 0, 0], READY_FEET_L, READY_FEET_R,
                undef, undef, READY_WRISTS);

// --- Alert ---------------------------------------------------------------------
// The infected notices you: rears up, throws its head back and screams, then
// drops into the chase.

anim_alert = inf_jaw(inf_body_clip([
    [0.00, inf_idle_key(0)],
    [0.12, inf_key([0, 0.0, -0.05], [0, 0, 0], [-6, 0, 0], [-28, 0, 0], [0.14, -0.04, 10, 0], [-0.13, 0.08, -18, 0],
                   [[-20, -35, 0], [-40, 0, 0], [-20, 0, 0]], [[-20, 35, 0], [-40, 0, 0], [-20, 0, 0]])],
    [0.40, inf_key([0, 0.02, -0.07], [2, 0, 0], [8, 0, 0], [-34, 0, 0], [0.14, -0.05, 10, 0], [-0.13, 0.09, -18, 0],
                   [[-50, -55, 0], [-60, 0, 0], [-30, 0, 0]], [[-50, 55, 0], [-60, 0, 0], [-30, 0, 0]])],
    [0.62, inf_key([0, 0.03, -0.09], [6, 0, 0], [16, 0, 0], [-26, 0, 0], [0.13, -0.08, 9, 0], [-0.13, 0.11, -16, 0],
                   [[-55, -30, 0], [-40, 0, 0], [-20, 0, 0]], [[-55, 30, 0], [-40, 0, 0], [-20, 0, 0]])],
    [0.80, READY]
], false), [[0.00, 10], [0.12, 30], [0.40, 42], [0.62, 36], [0.80, 18]]);

// --- Attacks -------------------------------------------------------------------
// Both strike at 0.42-0.45 s: NextDayz commits the damage on that frame.

anim_attack_lunge = inf_jaw(inf_body_clip([
    [0.00, READY],
    // Wind up: sink back, both hands drawn up beside the head.
    [0.28, inf_key([0, 0.08, -0.16], [4, 0, 0], [8, 0, 0], [-16, 0, 0], READY_FEET_L, READY_FEET_R, undef, undef,
                   [["wrist_l", [[0.30, -0.20, 0.62], [-120, 0, 20]]], ["wrist_r", [[-0.30, -0.22, 0.64], [-120, 0, -20]]]])],
    [0.36, inf_key([0, -0.02, -0.14], [10, 0, 0], [20, 0, 0], [-22, 0, 0], [0.14, -0.25, 7, 10, 0.10],
                   [-0.13, 0.13, -16, 12], undef, undef,
                   [["wrist_l", [[0.24, -0.45, 0.50], [-105, 0, 15]]], ["wrist_r", [[-0.24, -0.47, 0.52], [-105, 0, -15]]]])],
    // Lunge: a long step in, arms thrown out to grab.
    [0.45, inf_key([0, -0.16, -0.14], [12, 0, 0], [26, 0, 0], [-30, 0, 0], [0.14, -0.42, 6, 0],
                   [-0.13, 0.16, -16, 25], undef, undef,
                   [["wrist_l", [[0.14, -0.62, 0.66], [-100, 0, 10]]], ["wrist_r", [[-0.14, -0.64, 0.70], [-100, 0, -10]]]])],
    // Pull in and bite.
    [0.58, inf_key([0, -0.18, -0.17], [14, 0, 0], [30, 0, 0], [-12, 0, 0], [0.14, -0.42, 6, 0],
                   [-0.13, 0.16, -16, 22], undef, undef,
                   [["wrist_l", [[0.10, -0.46, 0.55], [-80, 0, 30]]], ["wrist_r", [[-0.10, -0.48, 0.57], [-80, 0, -30]]]])],
    [0.78, inf_key([0, -0.10, -0.13], [12, 0, 0], [26, 0, 0], [-12, 0, 0], [0.14, -0.42, 6, 0],
                   [-0.13, 0.14, -16, 10], undef, undef, READY_WRISTS)],
    [0.88, inf_key([0, -0.02, -0.11], [10, 0, 0], [22, 0, 0], [-11, 0, 0], [0.14, -0.26, 7, 0, 0.08],
                   READY_FEET_R, undef, undef, READY_WRISTS)],
    [1.00, READY]
], false), [[0.00, 18], [0.28, 26], [0.45, 36], [0.52, 38], [0.58, 2], [0.70, 8], [1.00, 18]]);

anim_attack_swipe = inf_jaw(inf_body_clip([
    [0.00, READY],
    // Wind up: twist right, the right claw raised high behind the shoulder.
    [0.26, inf_key([0, 0.04, -0.10], [6, 0, -10], [10, -4, -28], [-14, 0, 18], READY_FEET_L, READY_FEET_R,
                   undef, undef,
                   [["wrist_l", [[0.22, -0.40, 0.36], [-80, 0, 10]]], ["wrist_r", [[-0.40, 0.10, 0.62], [-150, 0, -30]]],
                    ["pole_r", [-0.55, 0.25, 0.30]]])],
    [0.34, inf_key([0, -0.02, -0.12], [9, 0, 0], [18, 0, 0], [-16, 0, 6], READY_FEET_L,
                   [-0.13, -0.04, -12, 10, 0.08], undef, undef,
                   [["wrist_l", [[0.26, -0.32, 0.34], [-75, 0, 10]]], ["wrist_r", [[-0.32, -0.30, 0.64], [-130, 0, 0]]],
                    ["pole_r", [-0.55, 0.10, 0.30]]])],
    // Strike: the claw rakes across the body.
    [0.42, inf_key([0, -0.12, -0.14], [14, 0, 10], [30, 4, 24], [-20, 0, -12], READY_FEET_L,
                   [-0.12, -0.24, -10, 0], undef, undef,
                   [["wrist_l", [[0.32, -0.15, 0.30], [-70, 0, 10]]], ["wrist_r", [[0.04, -0.60, 0.68], [-105, 0, 40]]],
                    ["pole_r", [-0.40, -0.10, 0.10]]])],
    [0.55, inf_key([0, -0.09, -0.15], [13, 0, 16], [30, 6, 38], [-14, 0, -18], READY_FEET_L,
                   [-0.12, -0.20, -10, 0], undef, undef,
                   [["wrist_l", [[0.34, -0.12, 0.28], [-70, 0, 10]]], ["wrist_r", [[0.30, -0.32, 0.22], [-70, 0, 60]]],
                    ["pole_r", [-0.20, -0.20, 0.00]]])],
    [0.75, inf_key([0, -0.03, -0.12], [9, 0, 4], [22, 0, 10], [-12, 0, -4], READY_FEET_L,
                   [-0.13, -0.02, -14, 0, 0.07], undef, undef, READY_WRISTS)],
    [0.90, READY]
], false), [[0.00, 18], [0.26, 30], [0.42, 40], [0.60, 24], [0.90, 18]]);

// --- Stagger -------------------------------------------------------------------
// Poise broken: thrown back onto the heels, two stumbling steps, recover. The
// simulation carries the root back while this plays.

anim_stagger = inf_jaw(inf_body_clip([
    [0.00, STAND],
    [0.10, inf_key([0, 0.10, -0.08], [-4, 0, 4], [-22, 0, 8], [-30, 10, 0], [0.13, -0.06, 8, -20], STAND_FEET_R,
                   [[-70, -30, 0], [-30, 0, 0]], [[-60, 40, 0], [-40, 0, 0]])],
    [0.20, inf_key([0, 0.14, -0.11], [-2, 0, 2], [-14, -4, 6], [-20, 6, 0], [0.13, -0.06, 8, -10],
                   [-0.14, 0.20, -18, 10, 0.10], [[-60, -35, 0], [-35, 0, 0]], [[-55, 40, 0], [-45, 0, 0]])],
    [0.30, inf_key([0, 0.16, -0.14], [2, 0, 0], [-8, -6, 4], [-12, 0, 0], [0.13, -0.04, 8, 0],
                   [-0.14, 0.26, -20, 0], [[-45, -30, 0], [-35, 0, 0]], [[-40, 35, 0], [-45, 0, 0]])],
    [0.42, inf_key([0, 0.14, -0.13], [6, 0, -2], [4, 0, 0], [-6, 0, 0], [0.14, 0.06, 10, 10, 0.09],
                   [-0.14, 0.26, -20, 0], [[-30, -20, 0], [-30, 0, 0]], [[-30, 25, 0], [-35, 0, 0]])],
    [0.52, inf_key([0, 0.10, -0.12], [8, 0, 0], [12, 0, 0], [-6, 0, 0], [0.14, 0.04, 10, 0],
                   [-0.14, 0.24, -18, 0], [[-25, -15, 0], [-30, 0, 0]], [[-25, 18, 0], [-32, 0, 0]])],
    [0.90, STAND]
], false), [[0.00, 10], [0.10, 30], [0.40, 20], [0.90, 10]]);

// --- Deaths --------------------------------------------------------------------
// Picked by the killing hit: shot from the front falls back, from behind (or
// at a run) pitches forward, a headshot buckles straight down. Each ends lying
// on the ground and holds there.

anim_death_back = inf_jaw(inf_body_clip([
    [0.00, STAND],
    [0.12, inf_key([0, 0.06, -0.10], [-6, 0, 4], [-18, 4, 6], [-30, 0, 10], [0.13, -0.06, 8, -25], STAND_FEET_R,
                   [[-80, -35, 0], [-20, 0, 0]], [[-70, 30, 0], [-35, 0, 0]])],
    [0.40, inf_key([0, 0.12, -0.45], [-30, 0, 8], [-14, 0, 6], [-20, 0, 15], [0.15, -0.18, 10, -30, 0.02],
                   [-0.14, -0.05, -14, -20], [[-100, -40, 0], [-30, 0, 0]], [[-90, 40, 0], [-40, 0, 0]])],
    [0.62, inf_key([0, 0.28, -0.80], [-80, 0, 10], [-8, 0, 4], [10, 0, 25], [0.17, -0.48, 14, -60],
                   [-0.16, -0.40, -16, -50], [[-60, -75, 0], [-30, 0, 0]], [[-40, 70, 0], [-50, 0, 0]])],
    [0.80, inf_key([0, 0.30, -0.78], [-84, 0, 10], [-4, 0, 4], [-6, 0, 35], [0.18, -0.50, 16, -65],
                   [-0.17, -0.42, -18, -52], [[-50, -80, 0], [-20, 0, 0]], [[-30, 75, 0], [-60, 0, 0]])],
    [1.20, inf_key([0, 0.30, -0.80], [-86, 0, 10], [-2, 0, 4], [-10, 0, 40], [0.18, -0.50, 18, -70],
                   [-0.18, -0.44, -20, -55], [[-48, -82, 0], [-18, 0, 0]], [[-28, 76, 0], [-62, 0, 0]])]
], false), [[0.00, 12], [0.12, 34], [0.62, 30], [1.20, 26]]);

anim_death_forward = inf_jaw(inf_body_clip([
    [0.00, STAND],
    [0.10, inf_key([0, -0.06, -0.10], [14, 0, -4], [26, -4, -6], [-10, 0, 0], STAND_FEET_L, [-0.13, 0.08, -14, 30],
                   [[30, -20, 0], [-10, 0, 0]], [[25, 20, 0], [-15, 0, 0]])],
    [0.35, inf_key([0, -0.18, -0.38], [35, 0, -6], [30, 0, -4], [-20, 0, 0], [0.14, -0.20, 8, 20],
                   [-0.13, 0.10, -14, 60, 0.02], [[-70, -20, 0], [-20, 0, 0]], [[-80, 20, 0], [-15, 0, 0]])],
    [0.58, inf_key([0, -0.36, -0.79], [84, 0, -8], [6, 0, 4], [-30, 0, -50], [0.16, 0.74, 10, 75],
                   [-0.17, 0.70, -16, 70], [[-150, -30, 0], [-40, 0, 0]], [[10, 30, 0], [-20, 0, 0]])],
    [0.75, inf_key([0, -0.37, -0.77], [86, 0, -8], [4, 0, 6], [-32, 0, -56], [0.17, 0.75, 11, 76],
                   [-0.18, 0.71, -18, 71], [[-152, -32, 0], [-36, 0, 0]], [[12, 30, 0], [-22, 0, 0]])],
    [1.10, inf_key([0, -0.38, -0.80], [87, 0, -8], [2, 0, 6], [-34, 0, -60], [0.18, 0.76, 12, 78],
                   [-0.20, 0.72, -20, 72], [[-154, -32, 0], [-34, 0, 0]], [[14, 30, 0], [-24, 0, 0]])]
], false), [[0.00, 12], [0.10, 30], [0.58, 24], [1.10, 20]]);

anim_death_crumple = inf_jaw(inf_body_clip([
    [0.00, STAND],
    [0.08, inf_key([0, 0.02, -0.09], [4, 0, 0], [-6, 0, 0], [-38, 0, 12], STAND_FEET_L, STAND_FEET_R,
                   [[-40, -30, 0], [-30, 0, 0]], [[-40, 30, 0], [-35, 0, 0]])],
    [0.30, inf_key([0, 0.04, -0.42], [10, 0, 0], [24, 4, 0], [30, 0, 10], [0.13, -0.06, 8, 30],
                   [-0.13, 0.08, -14, 40], [[10, -10, 0], [-10, 0, 0]], [[8, 12, 0], [-12, 0, 0]])],
    [0.50, inf_key([0, 0.10, -0.58], [20, 0, 4], [26, 4, 4], [34, 0, 14], [0.13, 0.20, 8, 70],
                   [-0.13, 0.24, -14, 72], [[14, -12, 0], [-10, 0, 0]], [[10, 14, 0], [-14, 0, 0]])],
    [0.72, inf_key([0, -0.20, -0.72], [60, 0, 10], [20, 0, 10], [-10, 0, -40], [0.14, 0.50, 12, 76],
                   [-0.16, 0.54, -12, 76], [[-30, -20, 0], [-20, 0, 0]], [[-20, 20, 0], [-25, 0, 0]])],
    [1.00, inf_key([0, -0.32, -0.80], [84, 4, 14], [4, -4, 8], [-30, 0, -55], [0.14, 0.72, 18, 80],
                   [-0.18, 0.76, -10, 78], [[20, -20, 0], [-20, 0, 0]], [[15, 25, 0], [-30, 0, 0]])]
], false), [[0.00, 12], [0.08, 36], [0.50, 30], [1.00, 34]]);

// --- Crawl ---------------------------------------------------------------------
// Legs gone (or as good as): prone, dragging itself along on its elbows. The
// simulation moves it at a fraction of its walking speed.

function bz_crawl_key(ph) =
    let(s = sin(360 * ph), c = cos(360 * ph))
    [[0, 0.02 * s, -0.80 + 0.012 * abs(c)], [84, 0, 7 * s], [-16 + 3 * abs(s), 0, -10 * s], [-46, 0, 6 * s],
     [-164 - 14 * s, -10, 0], [-34 + 26 * s, 0, 0], [-164 + 14 * s, 10, 0], [-34 - 26 * s, 0, 0],
     [3 * s, 0, 0], [6, 0, 0], [78, 0, 0], [-3 * s, 0, 0], [6, 0, 0], [78, 0, 0], [14 + 10 * abs(s), 0, 0]];

anim_crawl = rh_clip_from_poses(
    [["bone_pelvis", "pos"], ["bone_pelvis", "rot"], ["bone_torso", "rot"], ["bone_head", "rot"],
     ["bone_upperarm_l", "rot"], ["bone_forearm_l", "rot"], ["bone_upperarm_r", "rot"], ["bone_forearm_r", "rot"],
     ["bone_thigh_l", "rot"], ["bone_calf_l", "rot"], ["bone_foot_l", "rot"],
     ["bone_thigh_r", "rot"], ["bone_calf_r", "rot"], ["bone_foot_r", "rot"], ["bone_jaw", "rot"]],
    [for (i = [0 : 12]) 1.3 * i / 12], [for (i = [0 : 12]) bz_crawl_key(i / 12)], true);
