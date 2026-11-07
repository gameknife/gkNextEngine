// First-person hands for NextDayz (FirstPersonArms).
//
// The third-person rig's hands are 10 cm mittens: fine at three metres, a
// wall of skin 15 cm from the eye. These are hand-sized and actually wrap the
// weapon. Each is authored in weapon-model space with engine axes
// (+X the shooter's left, +Y up, +Z toward the muzzle) and converted to SCAD
// (+Z up, -Y muzzle) by E(), like nextdayz_attachments.scad.
//
//   bone_fps_hand_grip     right hand round a vertical pistol grip through the
//                          origin; index finger forward along the trigger.
//                          Wrist centre at WRIST_GRIP.
//   bone_fps_hand_support  left hand under a handguard running along +Z
//                          through the origin, fingers up its left side, thumb
//                          up its right. Wrist centre at WRIST_SUPPORT.
//
// FirstPersonArms places them per weapon (grip rake, handguard station) and
// aims the forearm IK at the wrist point. Keep WRIST_* in step with
// FirstPersonArms.cpp.

SKIN = [0.72, 0.52, 0.38];
NAIL = [0.80, 0.62, 0.52];

WRIST_GRIP = [-0.012, -0.030, -0.070];
WRIST_SUPPORT = [0.012, -0.062, -0.050];

function E(p) = [p[0], -p[2], p[1]];
module ball(p, r) translate(E(p)) sphere(r = r, $fn = 12);
module seg(a, b, ra, rb) hull() { ball(a, ra); ball(b, rb); }
// A finger as capsules through its joints, tapering from r0 to r1.
module finger(points, r0, r1)
{
    n = len(points) - 1;
    for (i = [0 : n - 1])
        seg(points[i], points[i + 1], r0 + (r1 - r0) * i / n, r0 + (r1 - r0) * (i + 1) / n);
}

module fps_hand_grip()
{
    color(SKIN)
    {
        // Back of the hand and palm: from the wrist round the backstrap to
        // the knuckle line on the right-front of the grip.
        hull()
        {
            ball([-0.020, -0.020, -0.068], 0.014);
            ball([-0.004, -0.040, -0.066], 0.014);
            ball([-0.020, -0.045, -0.062], 0.013);
            ball([-0.006, -0.015, -0.070], 0.013);
            ball([-0.023, 0.016, 0.008], 0.010);
            ball([-0.022, -0.046, 0.006], 0.010);
            ball([-0.006, 0.014, -0.024], 0.010);
            ball([-0.005, -0.044, -0.024], 0.010);
        }
        // Index finger straight forward along the right of the trigger guard.
        finger([[-0.023, 0.016, 0.010], [-0.020, 0.024, 0.034], [-0.015, 0.026, 0.054], [-0.010, 0.024, 0.066]],
               0.0088, 0.0078);
        // Middle, ring and little fingers wrap the front strap to the left side.
        for (f = [[-0.002, 0.0092], [-0.021, 0.0088], [-0.038, 0.0076]])
            finger([[-0.022, f[0], 0.010], [-0.006, f[0], 0.029], [0.012, f[0] - 0.002, 0.025],
                    [0.020, f[0] - 0.004, 0.008]], f[1], f[1] * 0.86);
        // Thumb over the top of the grip to the left side, under the receiver.
        finger([[-0.016, 0.016, -0.030], [-0.004, 0.032, -0.012], [0.012, 0.035, 0.006], [0.020, 0.030, 0.024]],
               0.0105, 0.0088);
    }
    color(NAIL) ball([0.021, 0.034, 0.028], 0.0055);
}

module fps_hand_support()
{
    color(SKIN)
    {
        // Palm cupped under the handguard, heel back toward the wrist.
        hull()
        {
            ball([-0.012, -0.030, -0.030], 0.010);
            ball([0.018, -0.034, -0.030], 0.010);
            ball([-0.012, -0.030, 0.030], 0.010);
            ball([0.020, -0.034, 0.030], 0.010);
            ball([0.004, -0.058, -0.050], 0.013);
            ball([0.022, -0.064, -0.048], 0.013);
            ball([0.010, -0.070, -0.040], 0.012);
            ball([0.014, -0.052, -0.058], 0.012);
        }
        // Four fingers up the left side of the handguard.
        for (f = [[0.030, 0.0088], [0.012, 0.0092], [-0.006, 0.0088], [-0.022, 0.0076]])
            finger([[0.024, -0.028, f[0]], [0.034, -0.006, f[0]], [0.031, 0.014, f[0] + 0.002],
                    [0.021, 0.026, f[0] + 0.004]], f[1], f[1] * 0.86);
        // Thumb up the right side.
        finger([[-0.014, -0.032, -0.018], [-0.031, -0.014, 0.000], [-0.031, 0.006, 0.018], [-0.025, 0.018, 0.032]],
               0.0105, 0.0088);
    }
}

module bone_fps_hand_grip() { gk_flatten() fps_hand_grip(); }
module bone_fps_hand_support() { gk_flatten() fps_hand_support(); }
module bone_fps_hands() { bone_fps_hand_grip(); bone_fps_hand_support(); }
bone_fps_hands();
