// ============================================================================
// rig_humanoid.scad - Procedural clip authoring for articulated ScadRig humanoids.
//
// Clips are data (anim_* lists of keys), but hand-typing joint angles for a
// 17-bone body produces stiff, drifting motion: a planted boot skates, a hand
// floats off the rifle as soon as two poses blend. This library authors clips
// from intent instead:
//   - gaits from a few numbers (cycle, speed, duty, bob, sway...), feet placed
//     on the ground with 2-bone leg IK, heel-strike / toe-off rolling;
//   - weapon holds from a weapon frame in chest space, both arms solved onto
//     the grip and handguard with 2-bone arm IK;
//   - full-body keys (jump, vault, loot) from pelvis / foot / weapon targets;
//   - unarmed upper bodies (rh_free_*) with each arm either FK angles or a
//     wrist target IK-solved toward an elbow pole, for characters with no gun.
// The output is ordinary anim_* data, so the loader, ScadLibrary's clip editor
// and every runtime keep working unchanged.
//
// Skeleton contract (all SCAD space: Z up, the character faces -Y, its LEFT
// side is +X, RIGHT side is -X; side = +1 left / -1 right):
//   bone_root -> bone_pelvis -> bone_torso -> bone_head
//                                         -> bone_upperarm_* -> bone_forearm_* -> bone_hand_*
//                                         -> bone_ik_weapon -> bone_ik_hand_r / bone_ik_hand_l
//                            -> bone_thigh_* -> bone_calf_* -> bone_foot_*
// Bind rotations are identity except bone_ik_*; legs and arms hang along -Z,
// the foot pivot sits at the ankle on the sole plane. The empty bone_ik_*
// bones are runtime IK targets: bone_ik_weapon is the weapon frame (muzzle
// -Y), bone_ik_hand_r/l are the wrist frames on the grip / handguard.
//
// Dictionaries are lists of [key, value] pairs read with rh_get; skeletons and
// gaits are passed resolved (rh_skeleton / rh_gait_params, see below).
// use <> drops top-level assignments, so everything here is a function.
// ============================================================================

// --- dictionaries -----------------------------------------------------------

function rh_get(d, key, fallback = 0) =
    let(hits = [for (kv = d) if (kv[0] == key) kv[1]]) len(hits) > 0 ? hits[0] : fallback;

// --- scalar / vector math ---------------------------------------------------

function rh_clamp(x, lo, hi) = min(max(x, lo), hi);
function rh_mix(a, b, t) = a + (b - a) * t;
function rh_smooth(t) = let(x = rh_clamp(t, 0, 1)) x * x * (3 - 2 * x);
function rh_fract(x) = x - floor(x);
function rh_hermite(p0, p1, m0, m1, u) =
    (2 * u * u * u - 3 * u * u + 1) * p0 + (u * u * u - 2 * u * u + u) * m0 +
    (-2 * u * u * u + 3 * u * u) * p1 + (u * u * u - u * u) * m1;

function rh_dot(a, b) = a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
function rh_cross(a, b) = [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
function rh_len(v) = sqrt(rh_dot(v, v));
function rh_unit(v, fallback = [0, 0, 1]) = let(l = rh_len(v)) l > 1e-6 ? v / l : fallback;

// --- rotation matrices (rows), OpenSCAD rotate([x, y, z]) = Rz * Ry * Rx ------

function rh_rx(a) = [[1, 0, 0], [0, cos(a), -sin(a)], [0, sin(a), cos(a)]];
function rh_ry(a) = [[cos(a), 0, sin(a)], [0, 1, 0], [-sin(a), 0, cos(a)]];
function rh_rz(a) = [[cos(a), -sin(a), 0], [sin(a), cos(a), 0], [0, 0, 1]];
function rh_mv(m, v) = [m[0][0] * v[0] + m[0][1] * v[1] + m[0][2] * v[2],
                         m[1][0] * v[0] + m[1][1] * v[1] + m[1][2] * v[2],
                         m[2][0] * v[0] + m[2][1] * v[1] + m[2][2] * v[2]];
// Spelled out: a tree-walking evaluator pays per comprehension step, and
// these run thousands of times per clip.
function rh_mm(a, b) = [
    [a[0][0] * b[0][0] + a[0][1] * b[1][0] + a[0][2] * b[2][0],
     a[0][0] * b[0][1] + a[0][1] * b[1][1] + a[0][2] * b[2][1],
     a[0][0] * b[0][2] + a[0][1] * b[1][2] + a[0][2] * b[2][2]],
    [a[1][0] * b[0][0] + a[1][1] * b[1][0] + a[1][2] * b[2][0],
     a[1][0] * b[0][1] + a[1][1] * b[1][1] + a[1][2] * b[2][1],
     a[1][0] * b[0][2] + a[1][1] * b[1][2] + a[1][2] * b[2][2]],
    [a[2][0] * b[0][0] + a[2][1] * b[1][0] + a[2][2] * b[2][0],
     a[2][0] * b[0][1] + a[2][1] * b[1][1] + a[2][2] * b[2][1],
     a[2][0] * b[0][2] + a[2][1] * b[1][2] + a[2][2] * b[2][2]]];
function rh_mt(m) = [[m[0][0], m[1][0], m[2][0]], [m[0][1], m[1][1], m[2][1]], [m[0][2], m[1][2], m[2][2]]];
function rh_cols(c0, c1, c2) = [[c0[0], c1[0], c2[0]], [c0[1], c1[1], c2[1]], [c0[2], c1[2], c2[2]]];
function rh_m(e) =
    let(cx = cos(e[0]), sx = sin(e[0]), cy = cos(e[1]), sy = sin(e[1]), cz = cos(e[2]), sz = sin(e[2]))
    [[cz * cy, cz * sy * sx - sz * cx, cz * sy * cx + sz * sx],
     [sz * cy, sz * sy * sx + cz * cx, sz * sy * cx - cz * sx],
     [-sy, cy * sx, cy * cx]];
function rh_e(m) = [atan2(m[2][1], m[2][2]), asin(rh_clamp(-m[2][0], -1, 1)), atan2(m[1][0], m[0][0])];

// --- frames: [position, rotation matrix] ------------------------------------

function rh_frame(p, e) = [p, rh_m(e)];
function rh_fx(a, b) = [a[0] + rh_mv(a[1], b[0]), rh_mm(a[1], b[1])];
function rh_finv(a) = let(mt = rh_mt(a[1])) [-rh_mv(mt, a[0]), mt];
// A clip key for a bone whose bind frame is `bind`: [pos, rot] offsets so that
// L_bind * T(pos) * R(rot) reproduces `f` (both in the parent's space).
function rh_key_for(bind, f) = let(bt = rh_mt(bind[1])) [rh_mv(bt, f[0] - bind[0]), rh_e(rh_mm(bt, f[1]))];

// --- resolved parameters ----------------------------------------------------
// Dictionary lookups are linear scans and a gait clip does thousands of them,
// so skeletons and gaits are resolved once into fixed vectors read by index.
// The character file passes rh_skeleton([...]) / rh_gait_params([...]).

function rh_skeleton_keys() = ["pelvis_z", "hip", "thigh", "calf", "heel", "ball", "torso_z", "shoulder", "upperarm", "forearm"];
function rh_skeleton(d) = [for (k = rh_skeleton_keys()) rh_get(d, k)];
function rs_pelvis_z(sk) = sk[0];
function rs_hip(sk) = sk[1];
function rs_thigh(sk) = sk[2];
function rs_calf(sk) = sk[3];
function rs_heel(sk) = sk[4];
function rs_ball(sk) = sk[5];
function rs_torso_z(sk) = sk[6];
function rs_shoulder(sk) = sk[7];
function rs_upperarm(sk) = sk[8];
function rs_forearm(sk) = sk[9];

function rh_gait_keys() = ["keys", "cycle", "speed", "duty", "stride_bias", "step_height", "lift_skew", "swing_tangent",
    "swing_out", "heel_strike", "heel_until", "toe_off", "toe_from", "swing_pitch", "width", "pelvis",
    "bob", "bob_sign", "sway", "hip_pitch", "hip_yaw", "hip_roll", "strafe_turn", "torso_lean",
    "torso_bounce", "torso_counter", "strafe_lean", "head_level", "gaze_pitch", "arm_base", "arm_swing", "arm_out",
    "elbow_base", "elbow_swing", "limp"];
function rh_gait_params(d) =
    [for (k = rh_gait_keys()) rh_get(d, k, k == "keys" ? 16 : (k == "pelvis" ? [0, 0, 0] : 0))];
function rg_keys(g) = g[0];
function rg_cycle(g) = g[1];
function rg_speed(g) = g[2];
function rg_duty(g) = g[3];
function rg_stride_bias(g) = g[4];
function rg_step_height(g) = g[5];
function rg_lift_skew(g) = g[6];
function rg_swing_tangent(g) = g[7];
function rg_swing_out(g) = g[8];
function rg_heel_strike(g) = g[9];
function rg_heel_until(g) = g[10];
function rg_toe_off(g) = g[11];
function rg_toe_from(g) = g[12];
function rg_swing_pitch(g) = g[13];
function rg_width(g) = g[14];
function rg_pelvis(g) = g[15];
function rg_bob(g) = g[16];
function rg_bob_sign(g) = g[17];
function rg_sway(g) = g[18];
function rg_hip_pitch(g) = g[19];
function rg_hip_yaw(g) = g[20];
function rg_hip_roll(g) = g[21];
function rg_strafe_turn(g) = g[22];
function rg_torso_lean(g) = g[23];
function rg_torso_bounce(g) = g[24];
function rg_torso_counter(g) = g[25];
function rg_strafe_lean(g) = g[26];
function rg_head_level(g) = g[27];
function rg_gaze_pitch(g) = g[28];
function rg_arm_base(g) = g[29];
function rg_arm_swing(g) = g[30];
function rg_arm_out(g) = g[31];
function rg_elbow_base(g) = g[32];
function rg_elbow_swing(g) = g[33];
// 0..1: the right leg is lame. It is dragged through the swing with little
// lift, and the body drops and lurches onto it while it bears weight.
function rg_limp(g) = g[34];

// --- skeleton accessors -----------------------------------------------------

function rh_hip(sk, side) = let(h = rs_hip(sk)) [h[0] * side, h[1], h[2]];
function rh_shoulder(sk, side) = let(s = rs_shoulder(sk)) [s[0] * side, s[1], s[2]];
function rh_pelvis_frame(sk, pos, rot) = [[0, 0, rs_pelvis_z(sk)] + pos, rh_m(rot)];
function rh_torso_frame(sk, pos, rot, torsoRot) =
    rh_fx(rh_pelvis_frame(sk, pos, rot), [[0, 0, rs_torso_z(sk)], rh_m(torsoRot)]);

// --- 2-bone IK --------------------------------------------------------------

// Leg: hip pivot and ankle target in root space, pm = pelvis rotation, fm =
// desired foot rotation in root space. Knee bends forward (-Y). Returns
// [thigh, calf, foot] rotations for keys.
function rh_leg_ik(sk, hip, ankle, pm, fm) =
    let(l1 = rs_thigh(sk), l2 = rs_calf(sk),
        v = rh_mv(rh_mt(pm), ankle - hip),
        b = atan2(-v[0], -v[2]),
        h = sqrt(v[0] * v[0] + v[2] * v[2]),
        d = rh_clamp(sqrt(v[1] * v[1] + h * h), abs(l1 - l2) + 0.001, l1 + l2 - 0.0005),
        phi = atan2(v[1], h),
        alpha = acos(rh_clamp((l1 * l1 + d * d - l2 * l2) / (2 * l1 * d), -1, 1)),
        k = 180 - acos(rh_clamp((l1 * l1 + l2 * l2 - d * d) / (2 * l1 * l2), -1, 1)),
        a = phi - alpha,
        lm = rh_mm(pm, rh_mm(rh_ry(b), rh_rx(a + k))))
    [[a, b, 0], [k, 0, 0], rh_e(rh_mm(rh_mt(lm), fm))];

// Arm: shoulder pivot, wrist target frame and pole point, all in the torso
// frame. The elbow bends toward the pole; the forearm folds forward (-X rot).
// Returns [upperarm, forearm, hand] rotations for keys.
function rh_arm_ik(sk, s, w, pole) =
    let(l1 = rs_upperarm(sk), l2 = rs_forearm(sk),
        v = w[0] - s,
        dir = rh_unit(v, [0, 0, -1]),
        d = rh_clamp(rh_len(v), abs(l1 - l2) + 0.001, l1 + l2 - 0.0005),
        pv = pole - s,
        bend = rh_unit(pv - dir * rh_dot(pv, dir), rh_unit(rh_cross(dir, [1, 0, 0]), [0, 1, 0])),
        ca = rh_clamp((l1 * l1 + d * d - l2 * l2) / (2 * l1 * d), -1, 1),
        sa = sqrt(max(0, 1 - ca * ca)),
        u = dir * ca + bend * sa,
        p = bend * ca - dir * sa,
        um = rh_cols(rh_cross(u, p), p, -u),
        flex = 180 - acos(rh_clamp((l1 * l1 + l2 * l2 - d * d) / (2 * l1 * l2), -1, 1)),
        fm = rh_mm(um, rh_rx(-flex)))
    [rh_e(um), [-flex, 0, 0], rh_e(rh_mm(rh_mt(fm), w[1]))];

// --- feet -------------------------------------------------------------------

// Ankle offset from the ground contact point for a foot pitched by `th`
// (+ = toe down). Toe-down rolls about the ball, toe-up about the heel, so the
// sole never sinks below the contact point.
function rh_roll(sk, th, back) =
    th < 0 ? back * (rs_heel(sk) * (1 - cos(th))) + [0, 0, -rs_heel(sk) * sin(th)]
           : back * (-rs_ball(sk) * (1 - cos(th))) + [0, 0, rs_ball(sk) * sin(th)];

// [ankle position, foot rotation] for a foot whose contact point is `c`.
function rh_foot(sk, c, yaw, th, extra = [0, 0, 0]) =
    let(ym = rh_rz(yaw)) [c + rh_roll(sk, th, rh_mv(ym, [0, 1, 0])) + extra, rh_mm(ym, rh_rx(th))];

// --- gaits ------------------------------------------------------------------
// dir: 0 forward, 90 left, -90 right, 180 back. Phase 0 = left heel strike,
// 0.5 = right heel strike, so every direction and gait shares foot timing and
// can be blended on one normalized phase.

function rh_strafe_turn(dir, m) = let(a = abs(dir)) (a <= 90 ? dir / 90 : sign(dir) * (180 - a) / 90) * m;

// Stance foot pitch over normalized stance time s: heel strike, flat, toe-off.
function rh_stance_pitch(g, s) =
    let(hu = rg_heel_until(g), tf = rg_toe_from(g))
    s < hu ? -rg_heel_strike(g) * (1 - rh_smooth(s / max(hu, 1e-4)))
           : (s > tf ? rg_toe_off(g) * pow((s - tf) / (1 - tf), 1.5) : 0);

// Swing foot pitch over normalized swing time u: toe-off, relaxed, heel first.
function rh_swing_pitch(g, u) =
    let(to = rg_toe_off(g), sp = rg_swing_pitch(g), hs = rg_heel_strike(g))
    u < 0.35 ? rh_mix(to, sp, rh_smooth(u / 0.35)) : rh_mix(sp, -hs, rh_smooth((u - 0.35) / 0.65));

function rh_gait_foot(sk, g, dir, side, phase) =
    let(duty = rg_duty(g),
        S = rg_speed(g) * duty * rg_cycle(g),
        D = [sin(dir), -cos(dir), 0],
        // Backpedalling is a forward stride played in reverse: land on the
        // ball, roll off the heel, and bias the sweep the other way.
        rev = cos(dir) < -0.1,
        bias = (rev ? 1 : -1) * rg_stride_bias(g) * S,
        turn = rh_strafe_turn(dir, rg_strafe_turn(g)),
        ym = rh_rz(turn),
        back = rh_mv(ym, [0, 1, 0]),
        outward = rh_mv(ym, [side, 0, 0]),
        base = outward * rg_width(g) + D * bias,
        q = rh_fract(phase - (side > 0 ? 0 : 0.5)),
        hs = rg_heel_strike(g), to = rg_toe_off(g))
    q < duty
        ? let(s = q / duty,
              th = rh_stance_pitch(g, rev ? 1 - s : s))
          rh_foot(sk, base + D * (S / 2 - S * s), turn, th)
        : let(u = (q - duty) / (1 - duty),
              ur = rev ? 1 - u : u,
              m = -S * (1 - duty) / duty * rg_swing_tangent(g),
              along = rh_hermite(-S / 2, S / 2, m, m, u),
              lame = side < 0 ? rg_limp(g) : 0,
              th0 = rh_swing_pitch(g, ur),
              // A lame foot is dragged: the toe stays down and scuffs forward.
              th = lame > 0 ? rh_mix(th0, to * 0.6, lame * 0.8 * sin(180 * u)) : th0,
              // Carry the toe-off / heel-strike roll through the swing so the
              // ankle path stays continuous at both contacts. The dragged foot
              // rolls about whatever touches the ground at its own pitch.
              roll = lame > 0 ? rh_roll(sk, th, back)
                              : rh_mix(rh_roll(sk, to, back), rh_roll(sk, -hs, back), rh_smooth(ur)),
              lift = rg_step_height(g) * (1 - 0.75 * lame) * sin(180 * pow(ur, rg_lift_skew(g))),
              out = outward * (rg_swing_out(g) * sin(180 * u)))
          [base + D * along + roll + [0, 0, lift] + out, rh_mm(ym, rh_rx(th))];

function rh_gait_bones() = [
    ["bone_pelvis", "pos"], ["bone_pelvis", "rot"], ["bone_torso", "rot"], ["bone_head", "rot"],
    ["bone_thigh_l", "rot"], ["bone_calf_l", "rot"], ["bone_foot_l", "rot"],
    ["bone_thigh_r", "rot"], ["bone_calf_r", "rot"], ["bone_foot_r", "rot"],
    ["bone_upperarm_l", "rot"], ["bone_forearm_l", "rot"], ["bone_upperarm_r", "rot"], ["bone_forearm_r", "rot"]];

function rh_gait_pose(sk, g, dir, phase) =
    let(duty = rg_duty(g),
        mid = duty / 2,
        fwd = cos(dir),
        lat = sin(dir),
        turn = rh_strafe_turn(dir, rg_strafe_turn(g)),
        pb = rg_pelvis(g),
        lame = rg_limp(g),
        // 1 at the middle of the right (lame) leg's stance, 0 off it.
        lameLoad = lame * max(0, cos(360 * (phase - 0.5 - mid))),
        pelvisPos = [pb[0] + rg_sway(g) * cos(360 * (phase - mid)),
                     pb[1],
                     pb[2] + rg_bob_sign(g) * rg_bob(g) * cos(720 * (phase - mid)) - 0.035 * lameLoad],
        yawOsc = -rg_hip_yaw(g) * cos(360 * phase) * fwd,
        roll = -rg_hip_roll(g) * cos(360 * (phase - mid)) - 8 * lameLoad,
        pelvisRot = [rg_hip_pitch(g), roll, turn + yawOsc],
        pf = rh_pelvis_frame(sk, pelvisPos, pelvisRot),
        footL = rh_gait_foot(sk, g, dir, 1, phase),
        footR = rh_gait_foot(sk, g, dir, -1, phase),
        legL = rh_leg_ik(sk, rh_fx(pf, [rh_hip(sk, 1), pf[1]])[0], footL[0], pf[1], footL[1]),
        legR = rh_leg_ik(sk, rh_fx(pf, [rh_hip(sk, -1), pf[1]])[0], footR[0], pf[1], footR[1]),
        torsoRot = [rg_torso_lean(g) + rg_torso_bounce(g) * cos(720 * (phase - mid)),
                    -roll * 0.7 + rg_strafe_lean(g) * lat - 14 * lameLoad,
                    -turn - yawOsc * rg_torso_counter(g)],
        lvl = rg_head_level(g),
        headRot = [rg_gaze_pitch(g) - (pelvisRot[0] + torsoRot[0]) * lvl,
                   -(pelvisRot[1] + torsoRot[1]) * lvl,
                   -(pelvisRot[2] + torsoRot[2]) * lvl],
        amp = max(rg_arm_swing(g), 1e-4),
        swing = rg_arm_swing(g) * cos(360 * phase) * fwd,
        ab = rg_arm_base(g), ao = rg_arm_out(g),
        eb = rg_elbow_base(g), es = rg_elbow_swing(g))
    [pelvisPos, pelvisRot, torsoRot, headRot,
     legL[0], legL[1], legL[2], legR[0], legR[1], legR[2],
     [-(ab - swing), -ao, 0], [-(eb + es * max(0, -swing) / amp), 0, 0],
     [-(ab + swing), ao, 0], [-(eb + es * max(0, swing) / amp), 0, 0]];

// A whole looping gait clip, sampled at "keys" evenly spaced keys.
function rh_gait_clip(sk, g, dir) =
    let(n = rg_keys(g),
        cycle = rg_cycle(g),
        poses = [for (i = [0 : n]) rh_gait_pose(sk, g, dir, i / n)])
    rh_clip_from_poses(rh_gait_bones(), [for (i = [0 : n]) cycle * i / n], poses, true);

function rh_clip_from_poses(channels, times, poses, loop) =
    concat([["loop", loop]],
           [for (c = [0 : len(channels) - 1])
               [channels[c][0], channels[c][1], [for (i = [0 : len(times) - 1]) [times[i], poses[i][c]]]]]);

// --- dense keys -------------------------------------------------------------
// Joint angles interpolated between two IK solutions do not keep a planted foot
// on the ground or a hand on its target; interpolating the intent does.
// rh_dense_keys resamples a key list every `step` seconds by mixing the keys'
// targets, so each sample is solved rather than slerped. Values present in both
// keys are mixed linearly; anything else (FK arm vs wrist target) switches
// halfway.

function rh_key_norm(name, v) =
    v == undef ? v
  : (name == "foot_l" || name == "foot_r") && len(v) == 4 ? concat(v, [0])
  : (name == "arm_l" || name == "arm_r") && len(v) == 2 ? concat(v, [[0, 0, 0]])
  : v;

function rh_key_mix(a, b, t) =
    concat([for (kv = a)
               let(name = kv[0], va = rh_key_norm(name, kv[1]), vb = rh_key_norm(name, rh_get(b, name, undef)))
               [name, va == undef || vb == undef ? (t < 0.5 ? va : vb) : va + (vb - va) * t]],
           t < 0.5 ? [] : [for (kv = b) if (len([for (ka = a) if (ka[0] == kv[0]) 1]) == 0) kv]);

function rh_dense_keys(keys, step) =
    let(n = len(keys))
    concat([for (i = [0 : n - 2])
               let(t0 = keys[i][0], t1 = keys[i + 1][0], m = max(1, ceil((t1 - t0) / step - 1e-6)))
               for (j = [0 : m - 1]) [t0 + (t1 - t0) * j / m, rh_key_mix(keys[i][1], keys[i + 1][1], j / m)]],
           [keys[n - 1]]);

// --- full-body and upper-body keys ------------------------------------------
// rig: weapon-hold constants
//   "weapon_bind": [pos, euler] bind frame of bone_ik_weapon (torso space)
//   "grip_r" / "grip_l": [pos, euler] bind frames of bone_ik_hand_r/l (weapon space)
//   "pole_r" / "pole_l": elbow pole points (torso space)
// key: one instant
//   "pelvis", "pelvis_rot", "torso", "head"            body
//   "foot_l" / "foot_r": [x, y, yaw, pitch(, lift)]     contact point on the ground
//   "weapon": [pos, euler]                              weapon frame (torso space)
//   "hand_l": [pos, euler]                              left wrist, weapon space (default grip_l)
//   "hand_l_root": [pos, euler]                         left wrist in root space (hand off the gun)
//   "pole_l" / "pole_r"                                 per-key elbow pole override

function rh_upper_bones() = [
    ["bone_torso", "rot"], ["bone_head", "rot"],
    ["bone_ik_weapon", "pos"], ["bone_ik_weapon", "rot"], ["bone_ik_hand_l", "pos"], ["bone_ik_hand_l", "rot"],
    ["bone_upperarm_l", "rot"], ["bone_forearm_l", "rot"], ["bone_hand_l", "rot"],
    ["bone_upperarm_r", "rot"], ["bone_forearm_r", "rot"], ["bone_hand_r", "rot"]];

function rh_lower_bones() = [
    ["bone_pelvis", "pos"], ["bone_pelvis", "rot"],
    ["bone_thigh_l", "rot"], ["bone_calf_l", "rot"], ["bone_foot_l", "rot"],
    ["bone_thigh_r", "rot"], ["bone_calf_r", "rot"], ["bone_foot_r", "rot"]];

function rh_upper_pose(sk, rig, key, torsoRoot = undef) =
    let(wb = rh_get(rig, "weapon_bind"),
        wbind = rh_frame(wb[0], wb[1]),
        gr = rh_get(rig, "grip_r"), gl = rh_get(rig, "grip_l"),
        grip_r = rh_frame(gr[0], gr[1]), grip_l = rh_frame(gl[0], gl[1]),
        wk = rh_get(key, "weapon", wb),
        wf = rh_frame(wk[0], wk[1]),
        hk = rh_get(key, "hand_l", gl),
        hr = rh_get(key, "hand_l_root", undef),
        handL = hr == undef || torsoRoot == undef
            ? rh_frame(hk[0], hk[1])
            : rh_fx(rh_finv(wf), rh_fx(rh_finv(torsoRoot), rh_frame(hr[0], hr[1]))),
        wristL = rh_fx(wf, handL),
        wristR = rh_fx(wf, grip_r),
        armL = rh_arm_ik(sk, rh_shoulder(sk, 1), wristL, rh_get(key, "pole_l", rh_get(rig, "pole_l"))),
        armR = rh_arm_ik(sk, rh_shoulder(sk, -1), wristR, rh_get(key, "pole_r", rh_get(rig, "pole_r"))),
        wkey = rh_key_for(wbind, wf),
        hkey = rh_key_for(grip_l, handL))
    [rh_get(key, "torso", [0, 0, 0]), rh_get(key, "head", [0, 0, 0]),
     wkey[0], wkey[1], hkey[0], hkey[1],
     armL[0], armL[1], armL[2], armR[0], armR[1], armR[2]];

function rh_lower_pose(sk, key) =
    let(pp = rh_get(key, "pelvis", [0, 0, 0]),
        pr = rh_get(key, "pelvis_rot", [0, 0, 0]),
        pf = rh_pelvis_frame(sk, pp, pr),
        fl = rh_get(key, "foot_l"), fr = rh_get(key, "foot_r"),
        footL = rh_foot(sk, [fl[0], fl[1], 0], fl[2], fl[3], [0, 0, len(fl) > 4 ? fl[4] : 0]),
        footR = rh_foot(sk, [fr[0], fr[1], 0], fr[2], fr[3], [0, 0, len(fr) > 4 ? fr[4] : 0]),
        legL = rh_leg_ik(sk, rh_fx(pf, [rh_hip(sk, 1), pf[1]])[0], footL[0], pf[1], footL[1]),
        legR = rh_leg_ik(sk, rh_fx(pf, [rh_hip(sk, -1), pf[1]])[0], footR[0], pf[1], footR[1]))
    [pp, pr, legL[0], legL[1], legL[2], legR[0], legR[1], legR[2]];

// keys: [[time, key], ...]. Upper-body clips leave pelvis and legs untouched so
// they layer over any locomotion.
function rh_upper_clip(sk, rig, keys, loop) =
    rh_clip_from_poses(rh_upper_bones(), [for (k = keys) k[0]],
                       [for (k = keys) rh_upper_pose(sk, rig, k[1])], loop);

function rh_body_clip(sk, rig, keys, loop) =
    rh_clip_from_poses(concat(rh_lower_bones(), rh_upper_bones()), [for (k = keys) k[0]],
                       [for (k = keys)
                           let(key = k[1],
                               tr = rh_torso_frame(sk, rh_get(key, "pelvis", [0, 0, 0]),
                                                   rh_get(key, "pelvis_rot", [0, 0, 0]),
                                                   rh_get(key, "torso", [0, 0, 0])))
                           concat(rh_lower_pose(sk, key), rh_upper_pose(sk, rig, key, tr))], loop);

// --- unarmed upper body -----------------------------------------------------
// For characters with nothing in their hands. Per key, each arm is either
//   "arm_l" / "arm_r": [upperarm, forearm(, hand)] local euler angles (FK), or
//   "wrist_l" / "wrist_r": [pos, euler] wrist frame in torso space, solved with
//                          2-bone IK toward "pole_l" / "pole_r" (torso space).
// An arm with neither hangs at bind.

function rh_free_arm(sk, side, key) =
    let(name = side > 0 ? "l" : "r",
        wrist = rh_get(key, str("wrist_", name), undef),
        fk = rh_get(key, str("arm_", name), undef))
    wrist != undef
        ? rh_arm_ik(sk, rh_shoulder(sk, side), rh_frame(wrist[0], wrist[1]),
                    rh_get(key, str("pole_", name), [0.45 * side, 0.30, -0.10]))
        : fk != undef ? [fk[0], fk[1], len(fk) > 2 ? fk[2] : [0, 0, 0]]
                      : [[0, 0, 0], [0, 0, 0], [0, 0, 0]];

function rh_free_upper_bones() = [
    ["bone_torso", "rot"], ["bone_head", "rot"],
    ["bone_upperarm_l", "rot"], ["bone_forearm_l", "rot"], ["bone_hand_l", "rot"],
    ["bone_upperarm_r", "rot"], ["bone_forearm_r", "rot"], ["bone_hand_r", "rot"]];

function rh_free_upper_pose(sk, key) =
    let(l = rh_free_arm(sk, 1, key), r = rh_free_arm(sk, -1, key))
    [rh_get(key, "torso", [0, 0, 0]), rh_get(key, "head", [0, 0, 0]), l[0], l[1], l[2], r[0], r[1], r[2]];

function rh_free_upper_clip(sk, keys, loop) =
    rh_clip_from_poses(rh_free_upper_bones(), [for (k = keys) k[0]],
                       [for (k = keys) rh_free_upper_pose(sk, k[1])], loop);

// Full body: rh_lower_pose keys (pelvis, pelvis_rot, foot_l/r) plus the above.
function rh_free_body_clip(sk, keys, loop) =
    rh_clip_from_poses(concat(rh_lower_bones(), rh_free_upper_bones()), [for (k = keys) k[0]],
                       [for (k = keys) concat(rh_lower_pose(sk, k[1]), rh_free_upper_pose(sk, k[1]))], loop);
