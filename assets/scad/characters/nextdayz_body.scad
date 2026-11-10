// ============================================================================
// nextdayz_body.scad - The survivor's body forms. Metres, Z-up, front = -Y.
//
// Shared by the rig (nextdayz_survivor.scad draws these on its bones) and the
// wardrobe (nextdayz_clothing.scad fits garments over the same sections), so a
// sleeve follows the arm it covers instead of being a box around it.
//
// Every form is authored in its bone's local frame with the bone pivot at the
// origin; limbs hang along -Z and are modelled for the LEFT side (+X is the
// outside of the body), the rig mirrors them for the right. Sections are
// rig_forms rings [z, w, d, x, y]. Limb ends are rounded and reach past the
// pivot into the neighbouring part: that overlap is what keeps a bent elbow,
// knee or shoulder closed with no skinning.
//
// Hand frame: fingers along -Z curling toward -Y (the palm faces -Y, where the
// weapon socket and IK grips sit), thumb on the outside.
//
// use <> drops top-level assignments: constants are zero-argument functions.
// ============================================================================

use <../lib/rig_forms.scad>

function ndb_skin() = [0.72, 0.52, 0.38];
function ndb_skin_shade() = [0.60, 0.42, 0.31];
function ndb_lip() = [0.50, 0.30, 0.25];
function ndb_hair() = [0.075, 0.055, 0.04];
function ndb_eye() = [0.035, 0.035, 0.03];
function ndb_shorts() = [0.85, 0.85, 0.82];
function ndb_shorts_band() = [0.65, 0.65, 0.63];

// Sides and superellipse exponent per form: limbs are crisp octagons, the
// trunk and head take a few more sides so their planes turn more gently.
function ndb_limb_m() = 8;
function ndb_limb_n() = 2.6;
function ndb_trunk_m() = 12;
function ndb_trunk_n() = 3.2;

// ---- sections ---------------------------------------------------------------

function ndb_torso() = [
    [-0.030, 0.290, 0.180, 0, 0.004],     // tucked into the pelvis
    [0.050, 0.300, 0.190, 0, 0.002],      // waist
    [0.170, 0.326, 0.206, 0, -0.002],     // lower ribs
    [0.300, 0.372, 0.228, 0, -0.008],     // chest
    [0.410, 0.404, 0.222, 0, -0.004],     // armpit line
    [0.475, 0.392, 0.184, 0, 0.004],      // shoulder line
    [0.515, 0.250, 0.150, 0, 0.012],      // trapezius
    [0.545, 0.150, 0.124, 0, 0.012]];     // neck root

function ndb_pelvis() = [
    [-0.125, 0.160, 0.136, 0, 0.004],     // crotch
    [-0.085, 0.300, 0.190, 0, 0.004],
    [-0.020, 0.340, 0.210, 0, 0.004],     // hips
    [0.070, 0.312, 0.196, 0, 0.004],
    [0.115, 0.296, 0.184, 0, 0.004]];

function ndb_upperarm() = [
    [-0.348, 0.070, 0.076, 0, 0.004],
    [-0.315, 0.100, 0.108, 0, 0.004],     // elbow
    [-0.240, 0.104, 0.114, 0, 0.000],
    [-0.130, 0.122, 0.134, 0.002, -0.004],// biceps
    [-0.035, 0.142, 0.152, 0.006, 0],     // deltoid
    [0.040, 0.126, 0.136, 0.004, 0],
    [0.072, 0.072, 0.080, 0, 0]];

function ndb_forearm() = [
    [-0.300, 0.066, 0.056, 0, 0],         // wrist, flattened
    [-0.270, 0.072, 0.062, 0, 0],
    [-0.170, 0.094, 0.090, 0, 0],
    [-0.075, 0.112, 0.112, 0.002, -0.002],
    [0.000, 0.102, 0.110, 0, 0.002],      // elbow
    [0.038, 0.070, 0.076, 0, 0.004]];

function ndb_thigh() = [
    [-0.472, 0.084, 0.090, 0, -0.004],
    [-0.440, 0.118, 0.126, 0, -0.004],    // knee
    [-0.370, 0.122, 0.132, 0, -0.002],
    [-0.220, 0.154, 0.172, 0.002, -0.008],// quadriceps
    [-0.070, 0.172, 0.188, 0.004, 0],
    [0.020, 0.160, 0.176, 0.004, 0.004],
    [0.060, 0.096, 0.106, 0, 0.004]];

function ndb_calf() = [
    [-0.425, 0.076, 0.082, 0, 0.004],     // ankle
    [-0.330, 0.086, 0.094, 0, 0.006],
    [-0.160, 0.122, 0.138, 0, 0.014],     // calf muscle sits behind the bone
    [-0.040, 0.118, 0.130, 0, 0.004],
    [0.000, 0.118, 0.126, 0, -0.004],     // knee
    [0.048, 0.080, 0.086, 0, -0.006]];

// The head pivot is the base of the neck; the face looks along -Y.
function ndb_skull() = [
    [0.006, 0.074, 0.070, 0, -0.058],     // chin
    [0.032, 0.126, 0.134, 0, -0.031],     // jaw
    [0.074, 0.160, 0.186, 0, -0.008],     // mouth line, jaw hinge
    [0.120, 0.180, 0.208, 0, 0.000],      // cheekbones
    [0.168, 0.186, 0.218, 0, 0.004],      // brow
    [0.212, 0.174, 0.208, 0, 0.008],
    [0.238, 0.128, 0.158, 0, 0.010],      // crown
    [0.248, 0.064, 0.084, 0, 0.010]];

function ndb_neck() = [
    [-0.100, 0.132, 0.130, 0, 0.016],
    [0.000, 0.124, 0.126, 0, 0.010],
    [0.060, 0.120, 0.130, 0, 0.002]];

// ---- parts ------------------------------------------------------------------

module ndb_limb(rings) { rf_loft(rings, m = ndb_limb_m(), n = ndb_limb_n()); }
module ndb_trunk(rings) { rf_loft(rings, m = ndb_trunk_m(), n = ndb_trunk_n()); }

module ndb_part_torso()
{
    color(ndb_skin()) ndb_trunk(ndb_torso());
}

module ndb_part_pelvis()
{
    // The only permanent garment. Clothing hides covered body render parts,
    // never their bones, so removing trousers restores these white shorts.
    color(ndb_shorts()) ndb_trunk(ndb_pelvis());
    color(ndb_shorts_band()) ndb_trunk(rf_span(rf_grow(ndb_pelvis(), 0.008), 0.075, 0.112));
}

module ndb_part_head()
{
    color(ndb_skin())
    {
        // The neck belongs to the head, so hiding the torso under a coat keeps it.
        rf_loft(ndb_neck(), m = 8, n = 2.2);
        rf_loft(ndb_skull(), m = 10, n = 2.8);
        // Brow ridge: the eyes sit in its shadow.
        hull()
        {
            translate([0, -0.100, 0.158]) rf_box([0.134, 0.020, 0.016], 0.006);
            translate([0, -0.096, 0.176]) rf_box([0.120, 0.014, 0.010], 0.004);
        }
        // Nose: bridge to tip.
        hull()
        {
            translate([0, -0.102, 0.150]) cube([0.018, 0.012, 0.010], center = true);
            translate([0, -0.126, 0.104]) rf_box([0.034, 0.024, 0.024], 0.008);
        }
        // Cheekbones give the face planes to catch light.
        for (s = [-1, 1])
            hull()
            {
                translate([s * 0.058, -0.086, 0.118]) rf_box([0.040, 0.020, 0.024], 0.008);
                translate([s * 0.046, -0.080, 0.074]) rf_box([0.030, 0.016, 0.020], 0.006);
            }
        for (s = [-1, 1])
            translate([s * 0.095, 0.016, 0.122]) rf_box([0.018, 0.036, 0.056], 0.007);
    }
    color(ndb_lip()) translate([0, -0.1015, 0.064]) cube([0.044, 0.008, 0.006], center = true);
    // Eyes are small and dark under the brow; no whites at this scale.
    for (s = [-1, 1])
        color(ndb_eye()) translate([s * 0.042, -0.1035, 0.140]) cube([0.024, 0.008, 0.009], center = true);
    // Short crop, tilted so the hairline clears the forehead and reaches the nape;
    // the sides fade down in front of the ears.
    color(ndb_hair())
    {
        translate([0, 0.012, 0.206]) rotate([-22, 0, 0])
            rf_loft([[-0.036, 0.192, 0.224, 0, 0.004], [0.006, 0.196, 0.230, 0, 0.004],
                     [0.036, 0.176, 0.208, 0, 0.004], [0.050, 0.110, 0.140, 0, 0.004]], m = 10, n = 2.8);
        for (s = [-1, 1])
            hull()
            {
                translate([s * 0.089, -0.020, 0.176]) rf_box([0.012, 0.050, 0.030], 0.004);
                translate([s * 0.088, -0.034, 0.132]) rf_box([0.008, 0.022, 0.030], 0.003);
            }
    }
}

module ndb_part_upperarm() { color(ndb_skin()) ndb_limb(ndb_upperarm()); }
module ndb_part_forearm() { color(ndb_skin()) ndb_limb(ndb_forearm()); }

module ndb_part_hand()
{
    color(ndb_skin())
    {
        // Wrist into the back of the hand.
        hull()
        {
            translate([0, 0.004, -0.010]) rf_box([0.064, 0.046, 0.020], 0.007);
            translate([0, 0.004, -0.074]) rf_box([0.096, 0.040, 0.030], 0.009);
        }
        // Fingers, loosely curled toward the palm in two knuckles.
        hull()
        {
            translate([0, 0.002, -0.088]) rf_box([0.094, 0.034, 0.020], 0.008);
            translate([0, -0.030, -0.128]) rf_box([0.090, 0.032, 0.026], 0.009);
        }
        hull()
        {
            translate([0, -0.032, -0.129]) rf_box([0.090, 0.032, 0.024], 0.009);
            translate([0, -0.062, -0.106]) rf_box([0.086, 0.024, 0.028], 0.008);
        }
        // Thumb on the outside, pointing along the fingers.
        hull()
        {
            translate([0.042, -0.004, -0.038]) rf_box([0.032, 0.034, 0.032], 0.009);
            translate([0.054, -0.036, -0.082]) rf_box([0.026, 0.026, 0.032], 0.008);
        }
    }
}

module ndb_part_thigh()
{
    color(ndb_skin()) ndb_limb(ndb_thigh());
    color(ndb_shorts()) ndb_limb(rf_span(rf_grow(ndb_thigh(), 0.014), -0.135, 0.030));
}

module ndb_part_calf() { color(ndb_skin()) ndb_limb(ndb_calf()); }

module ndb_part_foot()
{
    // Pivot = ankle on the sole plane; nothing goes below z = 0.
    color(ndb_skin())
    {
        hull()
        {
            translate([0, 0.035, 0.030]) rf_box([0.074, 0.090, 0.060], 0.014);
            translate([0, 0.006, 0.086]) rf_box([0.066, 0.076, 0.030], 0.012);
        }
        hull()
        {
            translate([0, 0.000, 0.070]) rf_box([0.066, 0.070, 0.040], 0.012);
            translate([0, -0.090, 0.030]) rf_box([0.088, 0.090, 0.050], 0.012);
            translate([0.004, -0.180, 0.018]) rf_box([0.094, 0.060, 0.034], 0.010);
        }
    }
}

// ---- garment shells ----------------------------------------------------------
// The layer envelopes of the wardrobe (coat depth .28, sleeve .158 x .168,
// trouser leg .19 x .205 ...) were authored as boxes, and every pocket, zip and
// strap sits on those faces. The shells keep exactly that envelope where
// details live, and spend their shape where a box has none: a waist under the
// chest, chamfered shoulders up to a collar plateau, sleeves and legs that
// taper and close over the joint, a seat that tucks under. Geometry only: the
// caller wraps it in its fabric.

function ndb_coat() = [
    [-0.025, 0.352, 0.244, 0, 0],         // hem, turned under
    [-0.010, 0.370, 0.260, 0, 0],
    [0.105, 0.370, 0.260, 0, 0],          // waist
    [0.245, 0.425, 0.280, 0, 0],          // chest
    [0.455, 0.425, 0.280, 0, 0],
    [0.497, 0.340, 0.226, 0, 0.004]];     // shoulder chamfer to the collar plateau

function ndb_sleeve_upper() = [
    [-0.340, 0.104, 0.112, 0, 0.004],     // elbow, rounded under
    [-0.316, 0.146, 0.156, 0, 0.002],
    [-0.200, 0.154, 0.164, 0, 0],
    [-0.040, 0.160, 0.170, 0.002, 0],     // shoulder seam
    [0.030, 0.150, 0.160, 0.002, 0],
    [0.070, 0.090, 0.100, 0, 0]];         // shoulder cap

function ndb_sleeve_fore() = [
    [-0.299, 0.112, 0.106, 0, 0],         // cuff opening
    [-0.285, 0.122, 0.118, 0, 0],
    [-0.170, 0.134, 0.140, 0, 0],
    [-0.060, 0.140, 0.150, 0.002, 0],
    [0.000, 0.134, 0.146, 0, 0.002],      // elbow
    [0.044, 0.094, 0.104, 0, 0.004]];

function ndb_trouser_seat() = [
    [-0.1175, 0.250, 0.200, 0, 0],        // tucks under between the legs
    [-0.085, 0.366, 0.240, 0, 0],
    [0.000, 0.375, 0.245, 0, 0],
    [0.1175, 0.368, 0.240, 0, 0]];

// Trousers are cut straight: the leg keeps its full width down to the knee and
// only the end rounds under, so knee pads and cargo pockets stay on the cloth.
function ndb_trouser_thigh() = [
    [-0.474, 0.120, 0.130, 0, -0.004],    // knee, rounded under
    [-0.446, 0.180, 0.194, 0, -0.002],
    [-0.300, 0.186, 0.201, 0, 0],
    [-0.100, 0.190, 0.205, 0.002, 0],
    [0.020, 0.184, 0.200, 0.002, 0.002],
    [0.066, 0.120, 0.130, 0, 0.004]];

function ndb_trouser_calf() = [
    [-0.429, 0.150, 0.162, 0, 0.004],
    [-0.300, 0.158, 0.172, 0, 0.004],
    [-0.150, 0.165, 0.180, 0, 0.004],
    [-0.030, 0.163, 0.176, 0, 0.002],
    [0.000, 0.158, 0.168, 0, -0.004],     // knee
    [0.050, 0.110, 0.118, 0, -0.006]];

// grow > 0 gives a second layer over the shell (quilting, a liner, a band).
module ndb_shell_coat(grow = 0) { rf_loft(rf_grow(ndb_coat(), grow), m = 12, n = 10); }
module ndb_shell_seat(grow = 0) { rf_loft(rf_grow(ndb_trouser_seat(), grow), m = 12, n = 8); }
module ndb_shell_sleeve(forearm = false, grow = 0)
{
    rf_loft(rf_grow(forearm ? ndb_sleeve_fore() : ndb_sleeve_upper(), grow), m = 8, n = 4);
}
module ndb_shell_leg(calf = false, grow = 0)
{
    rf_loft(rf_grow(calf ? ndb_trouser_calf() : ndb_trouser_thigh(), grow), m = 8, n = 4);
}
// A band around a shell between two heights: cuff, quilt chamber, reflective tape.
module ndb_band(rings, z0, z1, grow, m = 8, n = 4) { rf_loft(rf_span(rf_grow(rings, grow), z0, z1), m = m, n = n); }

// Laced boot. Pivot = ankle on the sole plane (z >= 0), toe toward -Y. The
// upper is one form from the heel counter to a rounded toe box; `shaft` is the
// height of the collar. Geometry only: call inside the leather / rubber colour.
module ndb_boot_upper(shaft = 0.205)
{
    hull()
    {
        translate([0, 0.038, 0.085]) rf_box([0.150, 0.100, 0.120], 0.020);
        translate([0, -0.075, 0.075]) rf_box([0.166, 0.130, 0.100], 0.022);
        translate([0, -0.178, 0.058]) rf_box([0.150, 0.090, 0.066], 0.024);
    }
    rf_loft([[0.110, 0.146, 0.166, 0, 0.008], [shaft - 0.012, 0.158, 0.172, 0, 0.004],
             [shaft, 0.150, 0.164, 0, 0.004]], m = 8, n = 4);
}
module ndb_boot_sole()
{
    hull()
    {
        translate([0, 0.035, 0.014]) rf_box([0.160, 0.110, 0.028], 0.006);
        translate([0, -0.100, 0.012]) rf_box([0.178, 0.160, 0.024], 0.006);
        translate([0, -0.190, 0.012]) rf_box([0.150, 0.076, 0.024], 0.008);
    }
    translate([0, 0.040, 0.020]) rf_box([0.164, 0.090, 0.040], 0.006);
}
module ndb_boot_laces(shaft = 0.205)
{
    for (i = [0 : 4])
        translate([0, -0.118 + i * 0.022, 0.118 + i * 0.006]) rotate([-28, 0, 0])
            cube([0.070, 0.008, 0.007], center = true);
    for (z = [0.150 : 0.022 : shaft - 0.02])
        translate([0, -0.080, z]) cube([0.070, 0.010, 0.007], center = true);
}
