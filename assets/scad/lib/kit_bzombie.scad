// ============================================================================
// kit_bzombie.scad - Humanoid zombie parts for Brotato3D (prefix bz_).
//
// Every Brotato3D zombie is one thin rig file (characters/brotato_zombie_*.scad)
// that owns the bone tree and asks this kit for its parts by variant name. All
// variants share one skeleton (the pivots in bz_SKEL below, identical to
// nextdayz_infected.scad), so the clips authored in brotato_zombie_walker.scad
// play on every one of them; a bigger zombie is the same rig scaled at its
// world node, and the silhouette comes from the forms here.
//
// Dismemberment contract (Brotato3D FZombieRigVisual): a limb is severed by
// throwing its bone subtree, so every joint that can come apart hides a wound
// inside the neighbouring part - bz_part_stump() at the shoulders, elbows, hips,
// knees and neck. They are invisible until the part that covers them is gone.
//
// 1 unit = 1 m, Z-up, the zombie faces -Y, limbs are modelled for the left
// (+X) side and mirrored inside the right bone. use <> drops top-level
// assignments, so every constant is a zero-argument function.
// ============================================================================

use <rig_forms.scad>

function bz_TINT()   = [1.00, 0.00, 1.00];   // runtime tint placeholder (shirt)
function bz_GORE()   = [0.42, 0.04, 0.04];
function bz_GORE_D() = [0.22, 0.02, 0.03];
function bz_BONE()   = [0.80, 0.76, 0.62];
function bz_SOCKET() = [0.05, 0.03, 0.03];
function bz_TEETH()  = [0.78, 0.74, 0.60];
function bz_SHOE()   = [0.10, 0.09, 0.08];

// --- Variants ----------------------------------------------------------------
// [bulk, belly, arm, leg, head, skin, skinDark, pants, outfit, hair, hairColor]
//   bulk   torso width multiplier        belly  extra gut depth (m)
//   arm    arm girth multiplier          leg    leg girth multiplier
//   head   head scale                    outfit shirt | hoodie | gown | jersey |
//                                               burst | robe | overall | riot | bare
function bz_spec(v) =
    v == "brute"      ? [1.22, 0.02, 1.75, 1.30, 0.95, [0.50, 0.44, 0.50], [0.36, 0.30, 0.37], [0.20, 0.22, 0.30], "bare",    "bald",   [0, 0, 0]] :
    v == "spitter"    ? [0.94, 0.05, 0.85, 0.90, 1.05, [0.52, 0.62, 0.36], [0.36, 0.46, 0.24], [0.70, 0.78, 0.72], "gown",    "bald",   [0, 0, 0]] :
    v == "runner"     ? [0.92, 0.00, 0.88, 0.95, 1.00, [0.56, 0.52, 0.44], [0.42, 0.38, 0.32], [0.16, 0.16, 0.18], "hoodie",  "hood",   [0, 0, 0]] :
    v == "linebacker" ? [1.12, 0.00, 1.20, 1.15, 1.00, [0.47, 0.52, 0.42], [0.35, 0.39, 0.32], [0.86, 0.84, 0.78], "jersey",  "helmet", [0, 0, 0]] :
    v == "bloater"    ? [1.20, 0.17, 1.05, 1.10, 1.00, [0.66, 0.50, 0.40], [0.52, 0.34, 0.28], [0.26, 0.24, 0.22], "burst",   "patchy", [0.20, 0.15, 0.10]] :
    v == "witch"      ? [0.86, 0.00, 0.74, 0.80, 1.02, [0.62, 0.64, 0.66], [0.44, 0.46, 0.50], [0.14, 0.10, 0.18], "robe",    "long",   [0.07, 0.06, 0.08]] :
    v == "hurler"     ? [1.30, 0.22, 1.30, 1.25, 0.92, [0.44, 0.50, 0.40], [0.32, 0.37, 0.29], [0.16, 0.20, 0.32], "overall", "bald",   [0, 0, 0]] :
    v == "warden"     ? [1.25, 0.04, 1.50, 1.30, 0.98, [0.46, 0.42, 0.40], [0.33, 0.29, 0.28], [0.10, 0.11, 0.14], "riot",    "riot",   [0, 0, 0]] :
                        [1.00, 0.00, 1.00, 1.00, 1.04, [0.47, 0.54, 0.41], [0.35, 0.40, 0.31], [0.20, 0.24, 0.34], "shirt",   "short",  [0.10, 0.08, 0.06]];

function bz_bulk(v)    = bz_spec(v)[0];
function bz_belly(v)   = bz_spec(v)[1];
function bz_arm(v)     = bz_spec(v)[2];
function bz_leg(v)     = bz_spec(v)[3];
function bz_headk(v)   = bz_spec(v)[4];
function bz_skin(v)    = bz_spec(v)[5];
function bz_skin_d(v)  = bz_spec(v)[6];
function bz_pants(v)   = bz_spec(v)[7];
function bz_outfit(v)  = bz_spec(v)[8];
function bz_hair(v)    = bz_spec(v)[9];
function bz_hair_c(v)  = bz_spec(v)[10];

// Shirt colour: tinted at runtime for the crowd variants, fixed where the
// outfit is the identity of the type.
function bz_cloth(v) =
    let(o = bz_outfit(v))
    o == "gown"    ? [0.62, 0.78, 0.70] :
    o == "robe"    ? [0.30, 0.16, 0.44] :
    o == "overall" ? [0.18, 0.24, 0.40] :
    o == "riot"    ? [0.13, 0.14, 0.17] :
    o == "bare"    ? bz_skin(v) :
                     bz_TINT();

// --- Shared pieces -----------------------------------------------------------

// A wound hidden inside a joint: raw flesh with a splintered bone end.
module bz_part_stump(r = 0.05, dir = [0, 0, -1])
{
    color(bz_GORE_D()) scale([1, 1, 0.8]) sphere(r = r, $fn = 6);
    color(bz_GORE()) translate(dir * r * 0.2) scale([0.7, 0.7, 0.6]) sphere(r = r, $fn = 6);
    color(bz_BONE()) translate(dir * r * 0.55) cube([r * 0.42, r * 0.42, r * 0.9], center = true);
}

// --- Sections ----------------------------------------------------------------

function bz_torso_rings(v) =
    let(w = bz_bulk(v), b = bz_belly(v))
    [[-0.030, 0.300 * w + b * 0.30, 0.190 * w + b * 0.55, 0, -b * 0.20],
     [0.060, 0.310 * w + b * 0.55, 0.200 * w + b * 1.25, 0, -b * 0.55],
     [0.180, 0.335 * w + b * 0.50, 0.214 * w + b * 1.15, 0, -b * 0.50],
     [0.300, 0.380 * w + b * 0.15, 0.236 * w + b * 0.45, 0, -0.008 - b * 0.15],
     [0.410, 0.415 * w, 0.230 * w, 0, -0.004],
     [0.475, 0.400 * w, 0.190 * w, 0, 0.004],
     [0.515, 0.250 * w, 0.150 * w, 0, 0.012],
     [0.548, 0.150, 0.126, 0, 0.012]];

function bz_pelvis_rings(v) =
    let(w = (bz_bulk(v) + bz_leg(v)) / 2, b = bz_belly(v))
    [[-0.125, 0.170 * w, 0.140 * w, 0, 0.004],
     [-0.085, 0.310 * w, 0.196 * w + b * 0.3, 0, 0.004 - b * 0.1],
     [-0.020, 0.350 * w + b * 0.3, 0.216 * w + b * 0.6, 0, 0.004 - b * 0.25],
     [0.070, 0.322 * w + b * 0.4, 0.204 * w + b * 0.7, 0, 0.004 - b * 0.3],
     [0.115, 0.300 * w + b * 0.3, 0.190 * w + b * 0.6, 0, 0.004 - b * 0.25]];

function bz_upperarm_rings(k) =
    [[-0.340, 0.072 * k, 0.078 * k, 0, 0.004],
     [-0.305, 0.102 * k, 0.110 * k, 0, 0.004],
     [-0.230, 0.108 * k, 0.118 * k, 0, 0],
     [-0.125, 0.128 * k, 0.140 * k, 0.002, -0.004],
     [-0.030, 0.148 * k, 0.158 * k, 0.006, 0],
     [0.045, 0.130 * k, 0.140 * k, 0.004, 0],
     [0.078, 0.074 * k, 0.082 * k, 0, 0]];

function bz_forearm_rings(k) =
    [[-0.300, 0.070 * k, 0.062 * k, 0, 0],
     [-0.265, 0.078 * k, 0.070 * k, 0, 0],
     [-0.165, 0.100 * k, 0.096 * k, 0, 0],
     [-0.070, 0.116 * k, 0.116 * k, 0.002, -0.002],
     [0.000, 0.106 * k, 0.114 * k, 0, 0.002],
     [0.040, 0.072 * k, 0.078 * k, 0, 0.004]];

function bz_thigh_rings(k) =
    [[-0.462, 0.090 * k, 0.096 * k, 0, -0.004],
     [-0.430, 0.126 * k, 0.134 * k, 0, -0.004],
     [-0.360, 0.132 * k, 0.142 * k, 0, -0.002],
     [-0.215, 0.160 * k, 0.176 * k, 0.002, -0.008],
     [-0.070, 0.176 * k, 0.192 * k, 0.004, 0],
     [0.020, 0.164 * k, 0.180 * k, 0.004, 0.004],
     [0.062, 0.098 * k, 0.108 * k, 0, 0.004]];

function bz_calf_rings(k) =
    [[-0.410, 0.084 * k, 0.090 * k, 0, 0.004],
     [-0.320, 0.094 * k, 0.102 * k, 0, 0.006],
     [-0.160, 0.128 * k, 0.142 * k, 0, 0.012],
     [-0.040, 0.124 * k, 0.134 * k, 0, 0.004],
     [0.000, 0.122 * k, 0.130 * k, 0, -0.004],
     [0.050, 0.082 * k, 0.088 * k, 0, -0.006]];

function bz_skull_rings() =
    [[0.070, 0.150, 0.170, 0, -0.012],
     [0.112, 0.184, 0.210, 0, 0.000],
     [0.166, 0.196, 0.226, 0, 0.004],
     [0.214, 0.184, 0.216, 0, 0.008],
     [0.246, 0.136, 0.166, 0, 0.010],
     [0.258, 0.066, 0.086, 0, 0.010]];

// --- Parts -------------------------------------------------------------------

module bz_part_pelvis(v)
{
    color(bz_pants(v)) rf_loft(bz_pelvis_rings(v), m = 10, n = 3.2);
    if (bz_outfit(v) == "robe" || bz_outfit(v) == "gown")
        color(bz_cloth(v)) rf_loft(rf_grow(bz_pelvis_rings(v), 0.02), m = 10, n = 3.2);
    else
        color([0.12, 0.10, 0.08]) rf_loft(rf_span(rf_grow(bz_pelvis_rings(v), 0.008), 0.070, 0.110), m = 10, n = 3.2);
    // Hip wounds, covered by the thighs until a leg comes off.
    for (s = [-1, 1]) translate([0.10 * s, 0, -0.085]) bz_part_stump(0.070 * bz_leg(v));
}

module bz_part_torso(v)
{
    o = bz_outfit(v);
    w = bz_bulk(v);
    b = bz_belly(v);
    rings = bz_torso_rings(v);

    color(bz_cloth(v)) rf_loft(rings, m = 10, n = 3.2);

    // Shoulder and neck wounds.
    for (s = [-1, 1]) translate([0.215 * s, 0, 0.455]) bz_part_stump(min(0.078, 0.060 * bz_arm(v)), [-s, 0, 0]);
    translate([0, 0.012, 0.556]) bz_part_stump(0.058, [0, 0, 1]);

    if (o == "shirt")
    {
        // Torn open over a wound in the flank, blood down the front and back.
        color(bz_skin(v)) translate([0.080, -0.104 * w, 0.215]) cube([0.130, 0.020, 0.180], center = true);
        color(bz_GORE()) translate([0.090, -0.112 * w, 0.200]) cube([0.085, 0.016, 0.105], center = true);
        color(bz_BONE()) for (z = [0.170, 0.205, 0.240]) translate([0.090, -0.119 * w, z]) cube([0.070, 0.010, 0.012], center = true);
        color(bz_GORE_D()) translate([-0.090, -0.117 * w, 0.345]) cube([0.120, 0.012, 0.090], center = true);
        color(bz_GORE_D()) translate([-0.040, 0.112 * w, 0.200]) cube([0.160, 0.012, 0.150], center = true);
    }
    if (o == "hoodie")
    {
        // Front pocket, drawstrings, a dark soak across the chest.
        color(bz_TINT()) translate([0, -0.108 * w, 0.100]) rf_box([0.220, 0.030, 0.090], 0.010);
        color([0.86, 0.84, 0.78]) for (s = [-1, 1]) translate([0.030 * s, -0.122 * w, 0.400]) cube([0.010, 0.008, 0.110], center = true);
        color(bz_GORE_D()) translate([0.060, -0.121 * w, 0.300]) cube([0.150, 0.010, 0.120], center = true);
    }
    if (o == "gown")
    {
        // Hospital gown, open at the back; the throat sac is on the head.
        color(bz_skin_d(v)) translate([0, 0.100 * w, 0.250]) cube([0.090, 0.016, 0.420], center = true);
        color([0.34, 0.60, 0.20]) translate([0, -0.112 * w, 0.330]) cube([0.150, 0.014, 0.180], center = true);
        color([0.24, 0.44, 0.14]) translate([0.020, -0.117 * w, 0.210]) cube([0.070, 0.010, 0.120], center = true);
    }
    if (o == "jersey")
    {
        // Shoulder pads and a number block.
        color([0.90, 0.88, 0.82]) hull()
        {
            translate([0, 0, 0.470]) rf_box([0.640, 0.300, 0.060], 0.020);
            translate([0, 0, 0.535]) rf_box([0.420, 0.240, 0.030], 0.012);
        }
        color([0.92, 0.90, 0.84]) translate([0, -0.128 * w, 0.270]) cube([0.130, 0.012, 0.150], center = true);
        color(bz_GORE_D()) translate([0.110, -0.130 * w, 0.360]) cube([0.090, 0.012, 0.080], center = true);
    }
    if (o == "burst")
    {
        // The shirt has split over a distended gut covered in pustules.
        color(bz_skin(v)) translate([0, -b * 0.62, 0.130]) scale([1.0, 0.72, 0.95]) sphere(r = 0.215 * w, $fn = 10);
        color([0.92, 0.80, 0.26]) for (p = [[0.09, 0.16, 0.050], [-0.11, 0.09, 0.060], [0.02, 0.02, 0.066], [-0.04, 0.22, 0.044], [0.13, 0.04, 0.046]])
            translate([p[0], -b * 0.62 - 0.150 * w + abs(p[0]) * 0.30, 0.03 + p[1]]) sphere(r = p[2], $fn = 6);
        color(bz_GORE()) translate([0, -b * 0.62 - 0.150 * w, 0.115]) cube([0.022, 0.022, 0.240], center = true);
    }
    if (o == "robe")
    {
        // Ragged ceremonial robe with a pale sash.
        color([0.78, 0.74, 0.60]) translate([0, -0.108 * w, 0.250]) rotate([0, 28, 0]) cube([0.050, 0.014, 0.520], center = true);
        color([0.20, 0.10, 0.30]) for (x = [-0.12, -0.04, 0.05, 0.13]) translate([x, -0.098 * w, -0.060]) cube([0.060, 0.022, 0.110], center = true);
    }
    if (o == "overall")
    {
        // Bib and braces; a tank of bile grown out of the back.
        color([0.14, 0.18, 0.30]) for (s = [-1, 1]) translate([0.105 * s, -0.020, 0.455]) rf_box([0.060, 0.330 * w, 0.050], 0.010);
        color([0.80, 0.72, 0.22]) for (s = [-1, 1]) translate([0.105 * s, -0.150 * w - b * 0.2, 0.340]) cube([0.040, 0.020, 0.040], center = true);
        color([0.42, 0.62, 0.18]) translate([0, 0.170 * w, 0.300]) scale([1.05, 0.80, 1.20]) sphere(r = 0.200, $fn = 8);
        color([0.62, 0.84, 0.22]) for (p = [[0.10, 0.36], [-0.09, 0.22], [0.02, 0.46]]) translate([p[0], 0.290 * w, p[1]]) sphere(r = 0.060, $fn = 6);
    }
    if (o == "riot")
    {
        // Plate carrier, pouches and a faded stencil.
        color([0.20, 0.21, 0.25]) translate([0, 0, 0.270]) rf_box([0.450 * w, 0.300 * w, 0.360], 0.030);
        color([0.26, 0.27, 0.31]) for (x = [-0.12, 0, 0.12]) translate([x, -0.160 * w, 0.130]) rf_box([0.095, 0.050, 0.110], 0.010);
        color([0.84, 0.82, 0.74]) translate([0, -0.154 * w, 0.360]) cube([0.200, 0.010, 0.060], center = true);
        color([0.24, 0.25, 0.29]) for (s = [-1, 1]) translate([0.250 * s, 0, 0.500]) rf_box([0.200, 0.250, 0.090], 0.030);
        color(bz_GORE_D()) translate([0.100, -0.158 * w, 0.250]) cube([0.120, 0.012, 0.110], center = true);
    }
    if (o == "bare")
    {
        // Slabs of muscle, old sutures, one strap of a torn vest.
        color(bz_skin_d(v)) for (s = [-1, 1]) translate([0.085 * s, -0.112 * w, 0.345]) rf_box([0.150, 0.030, 0.110], 0.014);
        color(bz_skin_d(v)) for (z = [0.090, 0.170, 0.245]) translate([0, -0.104 * w, z]) cube([0.180, 0.014, 0.014], center = true);
        color(bz_GORE()) translate([-0.070, -0.118 * w, 0.200]) rotate([0, 20, 0]) cube([0.016, 0.014, 0.200], center = true);
        color([0.24, 0.20, 0.16]) translate([0.110, 0, 0.300]) rotate([0, -24, 0]) rf_box([0.070, 0.300 * w, 0.520], 0.010);
    }
}

module bz_part_head(v)
{
    k = bz_headk(v);
    hair = bz_hair(v);
    scale(k)
    {
        color(bz_skin(v))
        {
            translate([0, 0.010, 0.020]) rf_box([0.118, 0.118, 0.110], 0.020);   // neck
            rf_loft(bz_skull_rings(), m = 10, n = 3.0);
        }
        // Heavy brow over sunken sockets, a broken nose, the upper teeth bared.
        color(bz_skin_d(v)) translate([0, -0.108, 0.180]) rf_box([0.184, 0.026, 0.032], 0.008);
        color(bz_SOCKET()) for (s = [-1, 1]) translate([0.046 * s, -0.110, 0.150]) cube([0.052, 0.016, 0.038], center = true);
        color(bz_skin(v)) translate([0.006, -0.118, 0.116]) cube([0.036, 0.030, 0.050], center = true);
        color(bz_TEETH()) translate([0, -0.094, 0.074]) cube([0.112, 0.016, 0.020], center = true);
        color(bz_GORE_D()) translate([-0.098, -0.030, 0.124]) cube([0.010, 0.074, 0.084], center = true);

        if (hair == "short")
        {
            color(bz_hair_c(v)) translate([0.020, 0.012, 0.250]) rf_box([0.160, 0.200, 0.040], 0.014);
            color(bz_hair_c(v)) translate([0, 0.100, 0.170]) rf_box([0.190, 0.040, 0.150], 0.014);
            color(bz_GORE()) translate([-0.072, -0.020, 0.262]) cube([0.060, 0.080, 0.014], center = true);
        }
        if (hair == "patchy")
        {
            color(bz_hair_c(v)) translate([0.050, 0.060, 0.250]) rf_box([0.080, 0.090, 0.034], 0.010);
            color([0.92, 0.80, 0.26]) translate([-0.060, 0.020, 0.250]) sphere(r = 0.040, $fn = 6);
        }
        if (hair == "long")
        {
            color(bz_hair_c(v)) translate([0, 0.020, 0.248]) rf_box([0.210, 0.230, 0.050], 0.016);
            color(bz_hair_c(v)) translate([0, 0.108, 0.060]) rf_box([0.220, 0.050, 0.400], 0.016);
            color(bz_hair_c(v)) for (s = [-1, 1]) translate([0.104 * s, 0.010, 0.080]) rf_box([0.036, 0.150, 0.340], 0.012);
            // Eyes that still burn.
            color([0.95, 0.80, 1.00]) for (s = [-1, 1]) translate([0.046 * s, -0.119, 0.150]) cube([0.022, 0.006, 0.016], center = true);
        }
        if (hair == "hood")
        {
            color(bz_TINT()) difference()
            {
                translate([0, 0.026, 0.160]) rf_box([0.240, 0.250, 0.250], 0.050);
                translate([0, -0.090, 0.130]) rf_box([0.180, 0.200, 0.190], 0.030);
            }
        }
        if (hair == "helmet")
        {
            color(bz_TINT()) difference()
            {
                translate([0, 0.012, 0.170]) scale([1.0, 1.12, 1.0]) sphere(r = 0.150, $fn = 10);
                translate([0, -0.120, 0.120]) rf_box([0.170, 0.160, 0.130], 0.020);
                translate([0, 0, -0.080]) cube([0.400, 0.400, 0.200], center = true);
            }
            color([0.90, 0.88, 0.82]) translate([0, 0.012, 0.300]) rf_box([0.040, 0.300, 0.050], 0.012);
            // Face cage.
            color([0.72, 0.72, 0.70]) for (z = [0.085, 0.130]) translate([0, -0.150, z]) cube([0.180, 0.014, 0.014], center = true);
            color([0.72, 0.72, 0.70]) for (s = [-1, 0, 1]) translate([0.060 * s, -0.150, 0.108]) cube([0.014, 0.014, 0.070], center = true);
        }
        if (hair == "riot")
        {
            color([0.13, 0.14, 0.17]) difference()
            {
                translate([0, 0.012, 0.172]) scale([1.0, 1.10, 1.0]) sphere(r = 0.154, $fn = 10);
                translate([0, -0.130, 0.130]) rf_box([0.190, 0.160, 0.110], 0.020);
                translate([0, 0, -0.085]) cube([0.400, 0.400, 0.200], center = true);
            }
            // Cracked visor pushed up, one red eye under it.
            color([0.50, 0.62, 0.68]) translate([0, -0.130, 0.224]) rotate([-50, 0, 0]) rf_box([0.190, 0.012, 0.100], 0.004);
            color([1.00, 0.20, 0.10]) translate([0.046, -0.119, 0.150]) cube([0.026, 0.006, 0.018], center = true);
        }
        if (bz_outfit(v) == "gown")
        {
            // Swollen throat sac.
            color([0.56, 0.84, 0.24]) translate([0, -0.060, 0.030]) scale([1.15, 1.0, 0.90]) sphere(r = 0.092, $fn = 8);
            color([0.74, 0.94, 0.34]) translate([0.040, -0.120, 0.040]) sphere(r = 0.034, $fn = 6);
        }
    }
}

module bz_part_jaw(v)
{
    scale(bz_headk(v))
    {
        color(bz_skin(v)) translate([0, -0.074, -0.022]) rf_box([0.150, 0.130, 0.052], 0.014);
        color(bz_TEETH()) translate([0, -0.128, 0.008]) cube([0.100, 0.014, 0.018], center = true);
        color(bz_GORE()) translate([0, -0.140, -0.022]) cube([0.074, 0.012, 0.034], center = true);
    }
}

// Left upper arm. `torn` strips the sleeve to the shoulder.
module bz_part_upperarm(v, torn = false)
{
    k = bz_arm(v);
    o = bz_outfit(v);
    sleeved = !(o == "bare" || o == "burst");
    if (sleeved && !torn)
        color(bz_cloth(v)) rf_loft(bz_upperarm_rings(k * 1.06), m = 8, n = 3.0);
    else
    {
        color(bz_skin(v)) rf_loft(bz_upperarm_rings(k), m = 8, n = 2.6);
        if (sleeved) color(bz_cloth(v)) rf_loft(rf_span(bz_upperarm_rings(k * 1.10), -0.110, 0.070), m = 8, n = 3.0);
        color(bz_GORE()) translate([0.052 * k, -0.010, -0.215]) cube([0.012, 0.062, 0.074], center = true);
    }
    if (o == "riot") color([0.24, 0.25, 0.29]) translate([0.020, 0, -0.150]) rf_box([0.130 * k, 0.150 * k, 0.170], 0.020);
    // Elbow wound, covered by the forearm.
    translate([0, 0, -0.318]) bz_part_stump(0.044 * k);
}

module bz_part_forearm(v, bitten = false)
{
    k = bz_arm(v);
    o = bz_outfit(v);
    color(bz_skin(v)) rf_loft(bz_forearm_rings(k), m = 8, n = 2.6);
    if (o == "hoodie" || o == "robe" || o == "riot")
        color(bz_cloth(v)) rf_loft(rf_span(bz_forearm_rings(k * 1.10), -0.200, 0.040), m = 8, n = 3.0);
    if (bitten)
    {
        color(bz_GORE()) translate([0, -0.050 * k, -0.120]) cube([0.060, 0.012, 0.080], center = true);
        color(bz_BONE()) translate([0, -0.056 * k, -0.120]) cube([0.040, 0.008, 0.014], center = true);
    }
    // Clawed hand: folded into the forearm part so a horde costs fewer nodes.
    translate([0, 0, -0.300])
    {
        color(bz_skin_d(v)) translate([0, 0, -0.045]) rf_box([0.094 * k, 0.054 * k, 0.094], 0.012);
        color(bz_skin_d(v)) for (x = [-0.030, -0.010, 0.010, 0.030])
            translate([x * k, -0.014, -0.122]) rotate([-25, 0, 0]) cube([0.017 * k, 0.024, 0.084], center = true);
        color(bz_skin_d(v)) translate([0.050 * k, -0.020, -0.072]) rotate([0, 30, 0]) cube([0.019 * k, 0.024, 0.062], center = true);
    }
}

module bz_part_thigh(v, bloody = false)
{
    k = bz_leg(v);
    o = bz_outfit(v);
    if (o == "gown")
        color(bz_skin(v)) rf_loft(bz_thigh_rings(k * 0.92), m = 8, n = 2.6);
    else
        color(o == "robe" ? bz_cloth(v) : bz_pants(v)) rf_loft(bz_thigh_rings(k), m = 8, n = 3.4);
    if (bloody) color(bz_GORE_D()) translate([0.084 * k, -0.020, -0.250]) cube([0.012, 0.084, 0.124], center = true);
    if (o == "riot") color([0.24, 0.25, 0.29]) translate([0, -0.080 * k, -0.200]) rf_box([0.130 * k, 0.040, 0.220], 0.014);
    // Knee wound, covered by the calf.
    translate([0, 0, -0.428]) bz_part_stump(0.056 * k);
}

module bz_part_calf(v, torn = false)
{
    k = bz_leg(v);
    o = bz_outfit(v);
    bareLeg = (o == "gown") || torn;
    if (bareLeg)
    {
        color(bz_skin(v)) rf_loft(bz_calf_rings(k * 0.88), m = 8, n = 2.6);
        if (o != "gown") color(bz_pants(v)) rf_loft(rf_span(bz_calf_rings(k * 1.04), -0.150, 0.050), m = 8, n = 3.4);
    }
    else
        color(o == "robe" ? bz_cloth(v) : bz_pants(v)) rf_loft(bz_calf_rings(k), m = 8, n = 3.4);
}

// Pivot = ankle on the sole plane, toe toward -Y.
module bz_part_foot(v, bare = false)
{
    k = bz_leg(v);
    if (bare || bz_outfit(v) == "gown")
    {
        color(bz_skin_d(v)) translate([0, -0.060, 0.045]) rf_box([0.112 * k, 0.270, 0.090], 0.020);
        color(bz_skin(v)) translate([0, 0, 0.100]) rf_box([0.092 * k, 0.100, 0.100], 0.016);
    }
    else
    {
        color(bz_SHOE()) hull()
        {
            translate([0, 0.030, 0.066]) rf_box([0.140 * k, 0.110, 0.110], 0.020);
            translate([0, -0.150, 0.050]) rf_box([0.146 * k, 0.110, 0.074], 0.022);
        }
        color([0.30, 0.28, 0.25]) translate([0, -0.062, 0.008]) rf_box([0.156 * k, 0.296, 0.016], 0.004);
    }
}
