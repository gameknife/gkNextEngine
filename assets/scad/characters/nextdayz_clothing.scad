// NextDayz modular urban survival wardrobe. Metres, Z-up, front = -Y.
// Muted winter outerwear, utility webbing, orange pulls and civilian/tactical
// packs evoke a winter evacuation kit. Each mesh remains a rigid bone part.
//
// Runtime contract: bone_<item id>__<survivor bone suffix>. All pivots are
// identity: geometry is authored in the target survivor bone's LOCAL space.
// The loader collects every variant once; PlayerRigVisual attaches only its
// render parts to the live skeleton and switches visibility from inventory.
//
// Layer envelopes: skin < coat (depth .28) < carrier (back .20/front -.21)
// < pack (front .21). Sleeves/trousers overlap at articulated seams by 1 cm;
// covered skin/shorts are hidden. Pack/harness and stowed guns get runtime
// clearance for the current outer layers. No skinning or cloth simulation.

use <../lib/rig_forms.scad>
use <nextdayz_body.scad>
use <nextdayz_clothing_heritage.scad>

$fn = 8;

function ndc_zip() = [0.055, 0.06, 0.065];
function ndc_web() = [0.09, 0.10, 0.095];
function ndc_orange() = [0.65, 0.19, 0.035];
// Styles: field / hoodie / puffer / rain / denim / rescue.
function ndc_coat(style) = [[0.24, 0.19, 0.13], [0.13, 0.18, 0.23], [0.22, 0.09, 0.065],
                            [0.26, 0.25, 0.085], [0.10, 0.16, 0.23], [0.48, 0.16, 0.045]][style];
// Styles: work / cargo / denim / tactical / rain / snow.
function ndc_pants(style) = [[0.14, 0.17, 0.20], [0.16, 0.19, 0.14], [0.10, 0.15, 0.22],
                             [0.29, 0.25, 0.17], [0.075, 0.105, 0.095], [0.40, 0.41, 0.38]][style];
function ndc_pack(style) = [[0.25, 0.11, 0.075], [0.16, 0.21, 0.24], [0.18, 0.20, 0.12],
                            [0.27, 0.21, 0.13], [0.40, 0.075, 0.055], [0.07, 0.21, 0.19]][style];
function ndc_vest_color(style) = [[0.11, 0.125, 0.105], [0.055, 0.075, 0.12], [0.27, 0.23, 0.15],
                                 [0.25, 0.17, 0.10], [0.095, 0.105, 0.11], [0.55, 0.19, 0.035]][style];
function ndc_reflect() = [0.52, 0.54, 0.47];

// Chamfered boxes keep a readable low-poly silhouette without smooth skin.
module ndc_box(size, bevel = 0.012)
{
    hull()
    {
        cube([size[0] - 2 * bevel, size[1], size[2] - 2 * bevel], center = true);
        cube([size[0], size[1] - 2 * bevel, size[2] - 2 * bevel], center = true);
        cube([size[0] - 2 * bevel, size[1] - 2 * bevel, size[2]], center = true);
    }
}

module ndc_coat_torso(style = 0)
{
    color(ndc_coat(style)) ndb_shell_coat();
    // Zipper and pull sit below the carrier's front face, with low pockets
    // below its hem so both silhouettes can be worn together.
    color(ndc_zip()) translate([0, -0.144, 0.245]) cube([0.012, 0.008, 0.47], center = true);
    color(ndc_orange()) translate([0, -0.153, 0.40]) cube([0.012, 0.012, 0.035], center = true);
    if (style == 1 || style == 3)
    {
        color(ndc_coat(style) * 0.8) translate([0, -0.145, 0.075]) ndc_box([0.27, 0.035, 0.11]);
        // Folded hood, never a second head shell; it fits under the helmet.
        color(ndc_coat(style)) translate([0, 0.075, 0.50]) ndc_box([0.235, 0.18, 0.08]);
    }
    else
    {
        for (s = [-1, 1])
        {
            color(ndc_coat(style) * 0.8) translate([s * 0.115, -0.145, 0.075]) ndc_box([0.105, 0.032, 0.12]);
            color(ndc_zip()) translate([s * 0.115, -0.165, 0.12]) cube([0.09, 0.008, 0.016], center = true);
        }
        // Open stand collar leaves the neck and face visible.
        for (s = [-1, 1])
            color(ndc_coat(style)) translate([s * 0.082, 0.012, 0.51]) ndc_box([0.045, 0.17, 0.075]);
    }
    if (style == 2)
    {
        // Quilt chambers wrap the whole shell and stay inside the carrier envelope.
        for (z = [0.02, 0.11, 0.20, 0.29, 0.38])
            color(ndc_coat(style) * 1.08) ndb_band(ndb_coat(), z, z + 0.074, 0.014, m = 12, n = 10);
    }
    if (style == 3)
    {
        color(ndc_coat(style) * 0.7) translate([0, -0.146, 0.26]) ndc_box([0.04, 0.014, 0.46], 0.005);
        for (side = [-1, 1])
            color(ndc_reflect()) translate([0, side * 0.147, 0.41]) cube([0.38, 0.006, 0.015], center = true);
    }
    if (style == 4)
    {
        for (s = [-1, 1])
        {
            color(ndc_coat(style) * 1.15) translate([s * 0.108, -0.143, 0.345]) ndc_box([0.12, 0.02, 0.12], 0.005);
            color([0.38, 0.32, 0.18]) translate([s * 0.108, -0.157, 0.39]) cube([0.012, 0.008, 0.012], center = true);
            color([0.29, 0.32, 0.32]) translate([s * 0.175, -0.144, 0.25]) cube([0.006, 0.008, 0.40], center = true);
        }
    }
    if (style == 5)
    {
        for (side = [-1, 1], z = [0.13, 0.37])
            color(ndc_reflect()) translate([0, side * 0.145, z]) cube([0.38, 0.012, 0.032], center = true);
        color(ndc_web()) translate([0, 0.145, 0.44]) ndc_box([0.20, 0.015, 0.055], 0.004);
    }
}

module ndc_sleeve(style = 0, forearm = false)
{
    len = forearm ? 0.29 : 0.31;
    width = forearm ? 0.137 : 0.158;
    depth = forearm ? 0.147 : 0.168;
    rings = forearm ? ndb_sleeve_fore() : ndb_sleeve_upper();
    tape = style == 5 ? 0.018 : 0.006;
    color(ndc_coat(style)) ndb_shell_sleeve(forearm);
    if (style == 2)
        for (z = [-0.095, -0.175, -0.255])
            color(ndc_coat(style) * 1.08) ndb_band(rings, z, z + 0.066, 0.012);
    if (style == 3 || style == 5)
        color(ndc_reflect()) ndb_band(rings, -0.20 - tape, -0.20 + tape, 0.006);
    if (style == 4)
        color([0.29, 0.32, 0.32]) translate([0.02, -depth / 2 - 0.004, -len / 2]) cube([0.006, 0.008, len - 0.04], center = true);
    if (forearm)
    {
        // Knit cuff and a reinforced elbow.
        color(ndc_zip()) ndb_band(rings, -0.299, -0.262, 0.010);
        color(ndc_coat(style) * 0.86) ndb_band(rings, -0.085, 0.030, 0.008);
    }
    else
    {
        color(ndc_web()) translate([0.081, 0, -0.105]) ndc_box([0.012, 0.09, 0.065], 0.004);
        color(ndc_orange()) translate([0.09, -0.005, -0.105]) cube([0.006, 0.045, 0.012], center = true);
    }
}

module ndc_vest(style = 0)
{
    color(ndc_vest_color(style))
    {
        if (style != 2 && style != 3 && style != 4)
            translate([0, -0.184, 0.31]) ndc_box([0.34, 0.052, 0.285]);
        if (style != 2)
            translate([0, 0.174, 0.31]) ndc_box([0.32, 0.052, 0.285]);
        if (style == 2)
            for (s = [-1, 1])
                translate([s * 0.13, 0, 0.37]) ndc_box([0.036, 0.34, 0.025], 0.005);
        if (style == 3 || style == 4)
            for (s = [-1, 1])
                translate([s * 0.10, -0.184, 0.31]) ndc_box([0.15, 0.052, 0.285]);
        for (s = [-1, 1])
        {
            translate([s * 0.212, 0, 0.25]) ndc_box([0.026, 0.34, 0.12]);
            translate([s * 0.133, 0, 0.478]) ndc_box([0.05, 0.355, 0.03]);
        }
    }
    if (style == 0)
    {
        // MOLLE rows, magazine pouches and radio identify the carrier from the coat.
        for (z = [0.25, 0.30, 0.35])
            color(ndc_web()) translate([0, -0.214, z]) cube([0.29, 0.008, 0.018], center = true);
        for (x = [-0.10, 0, 0.10])
        {
            color([0.16, 0.17, 0.13]) translate([x, -0.239, 0.205]) ndc_box([0.082, 0.055, 0.115]);
            color(ndc_web()) translate([x, -0.27, 0.24]) cube([0.065, 0.012, 0.025], center = true);
        }
        color(ndc_zip()) translate([0.115, -0.23, 0.395]) ndc_box([0.06, 0.035, 0.08]);
        color(ndc_zip()) translate([0.13, -0.23, 0.47]) cylinder(h = 0.085, r = 0.005);
        color(ndc_orange()) translate([-0.09, -0.215, 0.405]) cube([0.045, 0.01, 0.018], center = true);
    }
    if (style == 1)
    {
        for (side = [-1, 1])
            color([0.27, 0.30, 0.31]) translate([0, side * 0.207, 0.39]) ndc_box([0.19, 0.018, 0.055], 0.004);
        for (x = [-0.10, 0.10])
            color(ndc_web()) translate([x, -0.235, 0.23]) ndc_box([0.09, 0.045, 0.11]);
        color([0.45, 0.34, 0.09]) translate([-0.105, -0.218, 0.32]) scale([0.75, 0.3, 1]) sphere(r = 0.035);
    }
    if (style == 2)
    {
        for (x = [-0.12, -0.04, 0.04, 0.12])
        {
            color(ndc_vest_color(style)) translate([x, -0.218, 0.25]) ndc_box([0.072, 0.10, 0.16]);
            color(ndc_web()) translate([x, -0.272, 0.30]) cube([0.03, 0.012, 0.045], center = true);
        }
        for (s = [-1, 1])
            color(ndc_web()) translate([s * 0.13, -0.19, 0.405]) ndc_box([0.035, 0.035, 0.16], 0.006);
    }
    if (style == 3 || style == 4)
    {
        for (x = [-0.11, 0.11])
            color(ndc_vest_color(style) * 0.8) translate([x, -0.235, 0.22]) ndc_box([0.12, 0.055, 0.11]);
        if (style == 3)
            for (x = [-0.13, -0.09, 0.07, 0.11, 0.15])
                color([0.42, 0.29, 0.10]) translate([x, -0.225, 0.36]) cylinder(h = 0.06, r = 0.012);
        else
        {
            color(ndc_web()) translate([-0.105, -0.23, 0.36]) ndc_box([0.10, 0.04, 0.09]);
            for (x = [0.07, 0.105, 0.14])
                color([0.31, 0.32, 0.29]) translate([x, -0.225, 0.34]) cube([0.015, 0.018, 0.13], center = true);
        }
    }
    if (style == 5)
    {
        for (side = [-1, 1], z = [0.23, 0.37])
            color(ndc_reflect()) translate([0, side * 0.212, z]) cube([0.31, 0.014, 0.035], center = true);
        for (s = [-1, 1])
            color(ndc_reflect()) translate([s * 0.13, -0.215, 0.405]) cube([0.03, 0.012, 0.09], center = true);
    }
}

module ndc_pelvis(style = 0)
{
    color(ndc_pants(style)) ndb_shell_seat();
    color(ndc_web()) translate([0, 0, 0.08]) ndc_box([0.385, 0.252, 0.035], 0.006);
    color([0.24, 0.25, 0.23]) translate([0, -0.132, 0.08]) cube([0.045, 0.013, 0.03], center = true);
}

module ndc_leg(style = 0, calf = false)
{
    len = calf ? 0.42 : 0.44;
    width = calf ? 0.165 : 0.19;
    depth = calf ? 0.18 : 0.205;
    color(ndc_pants(style)) ndb_shell_leg(calf);
    if (!calf && (style == 1 || style == 3))
    {
        color(ndc_pants(style) * 0.8) translate([0.107, 0.015, -0.20]) ndc_box([0.042, 0.15, 0.17]);
        color(ndc_web()) translate([0.132, 0.015, -0.14]) cube([0.012, 0.13, 0.025], center = true);
    }
    if (calf)
    {
        color(style == 1 || style == 3 || style == 5 ? [0.09, 0.10, 0.08] : ndc_pants(style) * 1.2)
            translate([0, -0.093, -0.045]) ndc_box([0.12, 0.018, 0.095], 0.006);
    }
    if (style == 2)
        for (s = [-1, 1])
            color([0.29, 0.29, 0.24]) translate([s * (width / 2 - 0.008), -depth / 2 - 0.004, -len / 2])
                cube([0.005, 0.007, len - 0.035], center = true);
    if (calf && (style == 3 || style == 5))
    {
        color(ndc_web()) translate([0, 0, -0.07]) ndc_box([width + 0.006, depth + 0.006, 0.026], 0.004);
        color(ndc_web()) translate([0, -0.108, -0.055]) ndc_box([0.13, 0.035, 0.105], 0.008);
    }
    if (style == 4)
    {
        color(ndc_pants(style) * 1.7) translate([0.055, -depth / 2 - 0.004, -len / 2]) cube([0.012, 0.008, len - 0.03], center = true);
        if (calf)
            color(ndc_reflect()) translate([0, 0, -0.31]) ndc_box([width + 0.005, depth + 0.005, 0.022], 0.004);
    }
    if (calf && style == 5)
        color([0.15, 0.17, 0.15]) translate([0, 0, -0.34]) ndc_box([width + 0.008, depth + 0.008, 0.14], 0.008);
}

module ndc_boot(style = 0)
{
    leather = style == 2 ? [0.18, 0.19, 0.20] : style == 3 ? [0.22, 0.18, 0.12] : [0.065, 0.06, 0.05];
    shaft = style == 2 ? 0.150 : 0.205;
    gk_material(leather, roughness = 0.6) ndb_boot_upper(shaft);
    gk_material(style == 2 ? ndc_reflect() : [0.04, 0.04, 0.038], roughness = 0.85) ndb_boot_sole();
    // Padded collar, toe cap seam and laces.
    gk_material(style == 3 ? [0.16, 0.13, 0.09] : [0.10, 0.095, 0.08], roughness = 0.7)
    {
        translate([0, 0.004, shaft - 0.012]) rf_box([0.166, 0.180, 0.030], 0.010);
        translate([0, -0.150, 0.092]) rotate([-12, 0, 0]) cube([0.150, 0.008, 0.008], center = true);
    }
    color([0.22, 0.21, 0.18]) ndb_boot_laces(shaft);
}

module ndc_helmet(shell = [0.145, 0.17, 0.13])
{
    color(shell) difference()
    {
        translate([0, 0.005, 0.205]) scale([1, 1.04, 0.82]) sphere(r = 0.18);
        translate([0, 0, 0.02]) cube([0.4, 0.4, 0.30], center = true);
        translate([0, 0.005, 0.205]) scale([1, 1.04, 0.82]) sphere(r = 0.166);
    }
    color(ndc_web()) translate([0, 0, 0.18]) difference()
    {
        ndc_box([0.365, 0.38, 0.025], 0.008);
        cube([0.285, 0.29, 0.05], center = true);
    }
    for (s = [-1, 1])
        color(ndc_web()) translate([s * 0.118, 0.005, 0.08]) cube([0.014, 0.018, 0.17], center = true);
    color(ndc_web()) translate([0, -0.045, 0.008]) ndc_box([0.24, 0.028, 0.018], 0.004);
}

module ndc_head(style = 0)
{
    if (style == 0) ndc_helmet();
    if (style == 1)
    {
        color([0.25, 0.11, 0.065]) translate([0, 0.005, 0.245]) ndc_box([0.265, 0.27, 0.13], 0.025);
        color([0.18, 0.075, 0.045]) translate([0, 0.005, 0.195]) ndc_box([0.275, 0.28, 0.04], 0.012);
        for (x = [-0.09, -0.06, -0.03, 0, 0.03, 0.06, 0.09])
            color([0.30, 0.145, 0.085]) translate([x, -0.136, 0.24]) cube([0.007, 0.005, 0.07], center = true);
        color([0.18, 0.075, 0.045]) translate([0, 0.005, 0.33]) sphere(r = 0.035);
    }
    if (style == 2 || style == 3)
    {
        c = style == 2 ? [0.075, 0.12, 0.18] : [0.25, 0.25, 0.15];
        color(c) translate([0, 0.005, 0.235]) ndc_box([0.265, 0.27, 0.12], 0.022);
        color(ndc_web()) translate([0, 0.005, 0.19]) ndc_box([0.27, 0.275, 0.027], 0.008);
        if (style == 2)
        {
            color(c) translate([0, -0.18, 0.18]) ndc_box([0.27, 0.17, 0.016], 0.006);
            color([0.35, 0.37, 0.34]) translate([0, -0.135, 0.235]) ndc_box([0.055, 0.01, 0.035], 0.003);
        }
        else
        {
            color(c) translate([0, 0.005, 0.175]) difference()
            {
                scale([1, 1.1, 1]) cylinder(h = 0.02, r = 0.215, center = true);
                cube([0.245, 0.25, 0.05], center = true);
            }
            for (s = [-1, 1])
                color([0.13, 0.15, 0.085]) translate([s * 0.138, 0.0, 0.215]) cube([0.012, 0.15, 0.018], center = true);
        }
    }
    if (style == 4)
    {
        ndc_helmet([0.065, 0.085, 0.10]);
        for (s = [-1, 1])
        {
            color(ndc_zip()) translate([s * 0.172, 0.005, 0.23]) ndc_box([0.025, 0.13, 0.045], 0.005);
            for (y = [-0.035, 0, 0.035])
                color([0.24, 0.25, 0.23]) translate([s * 0.186, y, 0.23]) cube([0.008, 0.012, 0.015], center = true);
        }
        color(ndc_zip()) translate([0, -0.175, 0.24]) ndc_box([0.055, 0.035, 0.055], 0.006);
    }
    if (style == 5)
    {
        color([0.17, 0.14, 0.11]) translate([0, 0.005, 0.245]) ndc_box([0.29, 0.30, 0.13], 0.025);
        color([0.36, 0.31, 0.24]) translate([0, -0.135, 0.22]) ndc_box([0.285, 0.06, 0.08], 0.018);
        for (s = [-1, 1])
        {
            color([0.17, 0.14, 0.11]) translate([s * 0.13, 0.02, 0.11]) ndc_box([0.055, 0.24, 0.18], 0.016);
            color([0.36, 0.31, 0.24]) translate([s * 0.16, 0.02, 0.11]) ndc_box([0.018, 0.19, 0.14], 0.006);
        }
    }
}

module ndc_pack(style = 0)
{
    w = [0.285, 0.33, 0.37, 0.37, 0.31, 0.30][style];
    h = [0.34, 0.43, 0.49, 0.29, 0.40, 0.46][style];
    d = [0.15, 0.20, 0.24, 0.13, 0.18, 0.18][style];
    center = [0, 0.21 + d / 2, 0.29];
    color(ndc_pack(style)) translate(center) ndc_box([w, d, h], 0.025);
    color(ndc_pack(style) * 0.76) translate(center + [0, d / 2 + 0.018, style == 3 ? 0.045 : -0.065])
        ndc_box([w * (style == 3 ? 0.97 : 0.79), 0.055, h * (style == 3 ? 0.63 : 0.45)]);
    color(ndc_zip()) translate(center + [0, d / 2 + 0.048, h * 0.03]) cube([w * 0.75, 0.008, 0.012], center = true);
    color(ndc_orange()) translate(center + [w * 0.28, d / 2 + 0.057, -0.012]) cube([0.012, 0.012, 0.04], center = true);
    // Padded shoulder harness is an independent part: its front clearance
    // changes with the carrier without pushing the whole strap off the body.
    color(ndc_web()) translate(center + [0, 0, h / 2 + 0.012]) ndc_box([0.12, 0.04, 0.035], 0.008);
    if (style == 1 || style == 2 || style == 4)
        for (s = [-1, 1])
        {
            color(ndc_pack(style) * 0.85) translate(center + [s * (w / 2 + 0.018), 0.015, -0.04])
                ndc_box([0.055, d * 0.65, h * 0.43]);
            color(ndc_web()) translate(center + [s * w * 0.31, d / 2 + 0.051, 0.025])
                cube([0.026, 0.014, h * 0.72], center = true);
        }
    if (style == 2)
    {
        for (z = [-0.04, 0.02, 0.08])
            color(ndc_web()) translate(center + [0, d / 2 + 0.06, z]) cube([w * 0.74, 0.014, 0.018], center = true);
        color([0.23, 0.24, 0.17]) translate(center + [0, 0.015, -h / 2 - 0.035])
            rotate([0, 90, 0]) cylinder(h = w * 0.92, r = 0.06, center = true);
    }
    if (style == 3 || style == 5)
    {
        for (s = [-1, 1])
        {
            color(ndc_web()) translate(center + [s * w * 0.30, d / 2 + 0.055, 0]) cube([0.025, 0.018, h * 0.70], center = true);
            color([0.31, 0.32, 0.29]) translate(center + [s * w * 0.30, d / 2 + 0.07, -0.045])
                ndc_box([0.04, 0.015, 0.035], 0.004);
        }
    }
    if (style == 4)
    {
        color([0.77, 0.78, 0.73]) translate(center + [0, d / 2 + 0.055, -0.06])
        {
            cube([0.13, 0.015, 0.035], center = true);
            cube([0.035, 0.015, 0.13], center = true);
        }
    }
    if (style == 5)
        color(ndc_pack(style) * 0.65) translate(center + [0, 0, h / 2 + 0.015])
            rotate([0, 90, 0]) cylinder(h = w * 1.08, r = 0.035, center = true);
}

module ndc_harness(style = 0)
{
    if (style == 3)
    {
        color(ndc_web()) translate([0, -0.222, 0.32]) rotate([0, -35, 0]) ndc_box([0.04, 0.018, 0.43], 0.006);
        color(ndc_web()) translate([-0.145, 0, 0.497]) ndc_box([0.04, 0.445, 0.024], 0.006);
        color([0.24, 0.25, 0.23]) translate([0.05, -0.237, 0.25]) rotate([0, -35, 0]) ndc_box([0.05, 0.015, 0.04], 0.004);
    }
    else for (s = [-1, 1])
    {
        color(ndc_web()) translate([s * 0.145, -0.222, 0.32]) ndc_box([0.035, 0.018, 0.34], 0.006);
        color(ndc_web()) translate([s * 0.145, 0.0, 0.497]) ndc_box([0.035, 0.445, 0.024], 0.006);
        color([0.24, 0.25, 0.23]) translate([s * 0.145, -0.237, 0.23]) ndc_box([0.045, 0.015, 0.03], 0.004);
    }
}

module bone_helmet__head() { ndc_head(0); }
module bone_hat_beanie__head() { ndc_head(1); }
module bone_cap_baseball__head() { ndc_head(2); }
module bone_hat_boonie__head() { ndc_head(3); }
module bone_helmet_tactical__head() { ndc_head(4); }
module bone_hat_ushanka__head() { ndc_head(5); }
module bone_jacket__torso() { ndc_coat_torso(0); }
module bone_jacket__upperarm_l() { ndc_sleeve(0, forearm = false); }
module bone_jacket__upperarm_r() { mirror([1, 0, 0]) ndc_sleeve(0, forearm = false); }
module bone_jacket__forearm_l() { ndc_sleeve(0, forearm = true); }
module bone_jacket__forearm_r() { mirror([1, 0, 0]) ndc_sleeve(0, forearm = true); }
module bone_hoodie__torso() { ndc_coat_torso(1); }
module bone_hoodie__upperarm_l() { ndc_sleeve(1, forearm = false); }
module bone_hoodie__upperarm_r() { mirror([1, 0, 0]) ndc_sleeve(1, forearm = false); }
module bone_hoodie__forearm_l() { ndc_sleeve(1, forearm = true); }
module bone_hoodie__forearm_r() { mirror([1, 0, 0]) ndc_sleeve(1, forearm = true); }
module bone_jacket_puffer__torso() { ndc_coat_torso(2); }
module bone_jacket_puffer__upperarm_l() { ndc_sleeve(2, forearm = false); }
module bone_jacket_puffer__upperarm_r() { mirror([1, 0, 0]) ndc_sleeve(2, forearm = false); }
module bone_jacket_puffer__forearm_l() { ndc_sleeve(2, forearm = true); }
module bone_jacket_puffer__forearm_r() { mirror([1, 0, 0]) ndc_sleeve(2, forearm = true); }
module bone_jacket_rain__torso() { ndc_coat_torso(3); }
module bone_jacket_rain__upperarm_l() { ndc_sleeve(3, forearm = false); }
module bone_jacket_rain__upperarm_r() { mirror([1, 0, 0]) ndc_sleeve(3, forearm = false); }
module bone_jacket_rain__forearm_l() { ndc_sleeve(3, forearm = true); }
module bone_jacket_rain__forearm_r() { mirror([1, 0, 0]) ndc_sleeve(3, forearm = true); }
module bone_jacket_denim__torso() { ndc_coat_torso(4); }
module bone_jacket_denim__upperarm_l() { ndc_sleeve(4, forearm = false); }
module bone_jacket_denim__upperarm_r() { mirror([1, 0, 0]) ndc_sleeve(4, forearm = false); }
module bone_jacket_denim__forearm_l() { ndc_sleeve(4, forearm = true); }
module bone_jacket_denim__forearm_r() { mirror([1, 0, 0]) ndc_sleeve(4, forearm = true); }
module bone_jacket_rescue__torso() { ndc_coat_torso(5); }
module bone_jacket_rescue__upperarm_l() { ndc_sleeve(5, forearm = false); }
module bone_jacket_rescue__upperarm_r() { mirror([1, 0, 0]) ndc_sleeve(5, forearm = false); }
module bone_jacket_rescue__forearm_l() { ndc_sleeve(5, forearm = true); }
module bone_jacket_rescue__forearm_r() { mirror([1, 0, 0]) ndc_sleeve(5, forearm = true); }
module bone_vest_military__torso() { ndc_vest(0); }
module bone_vest_police__torso() { ndc_vest(1); }
module bone_vest_chest_rig__torso() { ndc_vest(2); }
module bone_vest_hunting__torso() { ndc_vest(3); }
module bone_vest_utility__torso() { ndc_vest(4); }
module bone_vest_rescue__torso() { ndc_vest(5); }
module bone_work_pants__pelvis() { ndc_pelvis(0); }
module bone_work_pants__thigh_l() { ndc_leg(0, calf = false); }
module bone_work_pants__thigh_r() { mirror([1, 0, 0]) ndc_leg(0, calf = false); }
module bone_work_pants__calf_l() { ndc_leg(0, calf = true); }
module bone_work_pants__calf_r() { mirror([1, 0, 0]) ndc_leg(0, calf = true); }
module bone_work_pants__foot_l() { ndc_boot(0); }
module bone_work_pants__foot_r() { mirror([1, 0, 0]) ndc_boot(0); }
module bone_cargo_pants__pelvis() { ndc_pelvis(1); }
module bone_cargo_pants__thigh_l() { ndc_leg(1, calf = false); }
module bone_cargo_pants__thigh_r() { mirror([1, 0, 0]) ndc_leg(1, calf = false); }
module bone_cargo_pants__calf_l() { ndc_leg(1, calf = true); }
module bone_cargo_pants__calf_r() { mirror([1, 0, 0]) ndc_leg(1, calf = true); }
module bone_cargo_pants__foot_l() { ndc_boot(1); }
module bone_cargo_pants__foot_r() { mirror([1, 0, 0]) ndc_boot(1); }
module bone_jeans__pelvis() { ndc_pelvis(2); }
module bone_jeans__thigh_l() { ndc_leg(2, calf = false); }
module bone_jeans__thigh_r() { mirror([1, 0, 0]) ndc_leg(2, calf = false); }
module bone_jeans__calf_l() { ndc_leg(2, calf = true); }
module bone_jeans__calf_r() { mirror([1, 0, 0]) ndc_leg(2, calf = true); }
module bone_jeans__foot_l() { ndc_boot(2); }
module bone_jeans__foot_r() { mirror([1, 0, 0]) ndc_boot(2); }
module bone_pants_tactical__pelvis() { ndc_pelvis(3); }
module bone_pants_tactical__thigh_l() { ndc_leg(3, calf = false); }
module bone_pants_tactical__thigh_r() { mirror([1, 0, 0]) ndc_leg(3, calf = false); }
module bone_pants_tactical__calf_l() { ndc_leg(3, calf = true); }
module bone_pants_tactical__calf_r() { mirror([1, 0, 0]) ndc_leg(3, calf = true); }
module bone_pants_tactical__foot_l() { ndc_boot(3); }
module bone_pants_tactical__foot_r() { mirror([1, 0, 0]) ndc_boot(3); }
module bone_pants_rain__pelvis() { ndc_pelvis(4); }
module bone_pants_rain__thigh_l() { ndc_leg(4, calf = false); }
module bone_pants_rain__thigh_r() { mirror([1, 0, 0]) ndc_leg(4, calf = false); }
module bone_pants_rain__calf_l() { ndc_leg(4, calf = true); }
module bone_pants_rain__calf_r() { mirror([1, 0, 0]) ndc_leg(4, calf = true); }
module bone_pants_rain__foot_l() { ndc_boot(4); }
module bone_pants_rain__foot_r() { mirror([1, 0, 0]) ndc_boot(4); }
module bone_pants_snow__pelvis() { ndc_pelvis(5); }
module bone_pants_snow__thigh_l() { ndc_leg(5, calf = false); }
module bone_pants_snow__thigh_r() { mirror([1, 0, 0]) ndc_leg(5, calf = false); }
module bone_pants_snow__calf_l() { ndc_leg(5, calf = true); }
module bone_pants_snow__calf_r() { mirror([1, 0, 0]) ndc_leg(5, calf = true); }
module bone_pants_snow__foot_l() { ndc_boot(5); }
module bone_pants_snow__foot_r() { mirror([1, 0, 0]) ndc_boot(5); }
module bone_backpack_small__torso() { ndc_pack(0); }
module bone_backpack_small__harness() { ndc_harness(0); }
module bone_backpack__torso() { ndc_pack(1); }
module bone_backpack__harness() { ndc_harness(1); }
module bone_backpack_military__torso() { ndc_pack(2); }
module bone_backpack_military__harness() { ndc_harness(2); }
module bone_backpack_messenger__torso() { ndc_pack(3); }
module bone_backpack_messenger__harness() { ndc_harness(3); }
module bone_backpack_medical__torso() { ndc_pack(4); }
module bone_backpack_medical__harness() { ndc_harness(4); }
module bone_backpack_rolltop__torso() { ndc_pack(5); }
module bone_backpack_rolltop__harness() { ndc_harness(5); }

// ---- 有史可考的一组：几何在 nextdayz_clothing_heritage.scad，一件衣服 = 剪裁 × 面料 ----
module bone_hat_panama__head() { ndh_panama(); }
module bone_helmet_ssh68__head() { ndh_ssh68(); }
module bone_helmet_m56__head() { ndh_m56(); }
module bone_helmet_pasgt__head() { ndh_pasgt(); }
module bone_hat_boonie_emr__head() { ndh_boonie_emr(); }
module bone_shirt_telnyashka__torso() { ndh_coat_torso("telnyashka", "tel"); }
module bone_shirt_telnyashka__upperarm_l() { ndh_sleeve("telnyashka", "tel", forearm = false); }
module bone_shirt_telnyashka__upperarm_r() { mirror([1, 0, 0]) ndh_sleeve("telnyashka", "tel", forearm = false); }
module bone_shirt_telnyashka__forearm_l() { ndh_sleeve("telnyashka", "tel", forearm = true); }
module bone_shirt_telnyashka__forearm_r() { mirror([1, 0, 0]) ndh_sleeve("telnyashka", "tel", forearm = true); }
module bone_jacket_afganka__torso() { ndh_coat_torso("afganka", "afganka"); }
module bone_jacket_afganka__upperarm_l() { ndh_sleeve("afganka", "afganka", forearm = false); }
module bone_jacket_afganka__upperarm_r() { mirror([1, 0, 0]) ndh_sleeve("afganka", "afganka", forearm = false); }
module bone_jacket_afganka__forearm_l() { ndh_sleeve("afganka", "afganka", forearm = true); }
module bone_jacket_afganka__forearm_r() { mirror([1, 0, 0]) ndh_sleeve("afganka", "afganka", forearm = true); }
module bone_jacket_gorka__torso() { ndh_coat_torso("gorka", "gorka"); }
module bone_jacket_gorka__upperarm_l() { ndh_sleeve("gorka", "gorka", forearm = false); }
module bone_jacket_gorka__upperarm_r() { mirror([1, 0, 0]) ndh_sleeve("gorka", "gorka", forearm = false); }
module bone_jacket_gorka__forearm_l() { ndh_sleeve("gorka", "gorka", forearm = true); }
module bone_jacket_gorka__forearm_r() { mirror([1, 0, 0]) ndh_sleeve("gorka", "gorka", forearm = true); }
module bone_jacket_strichtarn__torso() { ndh_coat_torso("nva", "strichtarn"); }
module bone_jacket_strichtarn__upperarm_l() { ndh_sleeve("nva", "strichtarn", forearm = false); }
module bone_jacket_strichtarn__upperarm_r() { mirror([1, 0, 0]) ndh_sleeve("nva", "strichtarn", forearm = false); }
module bone_jacket_strichtarn__forearm_l() { ndh_sleeve("nva", "strichtarn", forearm = true); }
module bone_jacket_strichtarn__forearm_r() { mirror([1, 0, 0]) ndh_sleeve("nva", "strichtarn", forearm = true); }
module bone_jacket_m81__torso() { ndh_coat_torso("bdu", "m81"); }
module bone_jacket_m81__upperarm_l() { ndh_sleeve("bdu", "m81", forearm = false); }
module bone_jacket_m81__upperarm_r() { mirror([1, 0, 0]) ndh_sleeve("bdu", "m81", forearm = false); }
module bone_jacket_m81__forearm_l() { ndh_sleeve("bdu", "m81", forearm = true); }
module bone_jacket_m81__forearm_r() { mirror([1, 0, 0]) ndh_sleeve("bdu", "m81", forearm = true); }
module bone_jacket_emr__torso() { ndh_coat_torso("emr", "emr"); }
module bone_jacket_emr__upperarm_l() { ndh_sleeve("emr", "emr", forearm = false); }
module bone_jacket_emr__upperarm_r() { mirror([1, 0, 0]) ndh_sleeve("emr", "emr", forearm = false); }
module bone_jacket_emr__forearm_l() { ndh_sleeve("emr", "emr", forearm = true); }
module bone_jacket_emr__forearm_r() { mirror([1, 0, 0]) ndh_sleeve("emr", "emr", forearm = true); }
module bone_jacket_dbdu__torso() { ndh_coat_torso("bdu", "dbdu"); }
module bone_jacket_dbdu__upperarm_l() { ndh_sleeve("bdu", "dbdu", forearm = false); }
module bone_jacket_dbdu__upperarm_r() { mirror([1, 0, 0]) ndh_sleeve("bdu", "dbdu", forearm = false); }
module bone_jacket_dbdu__forearm_l() { ndh_sleeve("bdu", "dbdu", forearm = true); }
module bone_jacket_dbdu__forearm_r() { mirror([1, 0, 0]) ndh_sleeve("bdu", "dbdu", forearm = true); }
module bone_jacket_flora__torso() { ndh_coat_torso("afganka", "flora"); }
module bone_jacket_flora__upperarm_l() { ndh_sleeve("afganka", "flora", forearm = false); }
module bone_jacket_flora__upperarm_r() { mirror([1, 0, 0]) ndh_sleeve("afganka", "flora", forearm = false); }
module bone_jacket_flora__forearm_l() { ndh_sleeve("afganka", "flora", forearm = true); }
module bone_jacket_flora__forearm_r() { mirror([1, 0, 0]) ndh_sleeve("afganka", "flora", forearm = true); }
module bone_vest_type56__torso() { ndh_type56(); }
module bone_vest_6b3__torso() { ndh_6b3(); }
module bone_pants_afganka__pelvis() { ndh_pelvis("afganka", "afganka"); }
module bone_pants_afganka__thigh_l() { ndh_leg("afganka", "afganka", calf = false); }
module bone_pants_afganka__thigh_r() { mirror([1, 0, 0]) ndh_leg("afganka", "afganka", calf = false); }
module bone_pants_afganka__calf_l() { ndh_leg("afganka", "afganka", calf = true); }
module bone_pants_afganka__calf_r() { mirror([1, 0, 0]) ndh_leg("afganka", "afganka", calf = true); }
module bone_pants_afganka__foot_l() { ndh_boot("afganka", "afganka"); }
module bone_pants_afganka__foot_r() { mirror([1, 0, 0]) ndh_boot("afganka", "afganka"); }
module bone_pants_gorka__pelvis() { ndh_pelvis("gorka", "gorka"); }
module bone_pants_gorka__thigh_l() { ndh_leg("gorka", "gorka", calf = false); }
module bone_pants_gorka__thigh_r() { mirror([1, 0, 0]) ndh_leg("gorka", "gorka", calf = false); }
module bone_pants_gorka__calf_l() { ndh_leg("gorka", "gorka", calf = true); }
module bone_pants_gorka__calf_r() { mirror([1, 0, 0]) ndh_leg("gorka", "gorka", calf = true); }
module bone_pants_gorka__foot_l() { ndh_boot("gorka", "gorka"); }
module bone_pants_gorka__foot_r() { mirror([1, 0, 0]) ndh_boot("gorka", "gorka"); }
module bone_pants_strichtarn__pelvis() { ndh_pelvis("nva", "strichtarn"); }
module bone_pants_strichtarn__thigh_l() { ndh_leg("nva", "strichtarn", calf = false); }
module bone_pants_strichtarn__thigh_r() { mirror([1, 0, 0]) ndh_leg("nva", "strichtarn", calf = false); }
module bone_pants_strichtarn__calf_l() { ndh_leg("nva", "strichtarn", calf = true); }
module bone_pants_strichtarn__calf_r() { mirror([1, 0, 0]) ndh_leg("nva", "strichtarn", calf = true); }
module bone_pants_strichtarn__foot_l() { ndh_boot("nva", "strichtarn"); }
module bone_pants_strichtarn__foot_r() { mirror([1, 0, 0]) ndh_boot("nva", "strichtarn"); }
module bone_pants_m81__pelvis() { ndh_pelvis("bdu", "m81"); }
module bone_pants_m81__thigh_l() { ndh_leg("bdu", "m81", calf = false); }
module bone_pants_m81__thigh_r() { mirror([1, 0, 0]) ndh_leg("bdu", "m81", calf = false); }
module bone_pants_m81__calf_l() { ndh_leg("bdu", "m81", calf = true); }
module bone_pants_m81__calf_r() { mirror([1, 0, 0]) ndh_leg("bdu", "m81", calf = true); }
module bone_pants_m81__foot_l() { ndh_boot("bdu", "m81"); }
module bone_pants_m81__foot_r() { mirror([1, 0, 0]) ndh_boot("bdu", "m81"); }
module bone_pants_emr__pelvis() { ndh_pelvis("emr", "emr"); }
module bone_pants_emr__thigh_l() { ndh_leg("emr", "emr", calf = false); }
module bone_pants_emr__thigh_r() { mirror([1, 0, 0]) ndh_leg("emr", "emr", calf = false); }
module bone_pants_emr__calf_l() { ndh_leg("emr", "emr", calf = true); }
module bone_pants_emr__calf_r() { mirror([1, 0, 0]) ndh_leg("emr", "emr", calf = true); }
module bone_pants_emr__foot_l() { ndh_boot("emr", "emr"); }
module bone_pants_emr__foot_r() { mirror([1, 0, 0]) ndh_boot("emr", "emr"); }
module bone_pants_dbdu__pelvis() { ndh_pelvis("bdu", "dbdu"); }
module bone_pants_dbdu__thigh_l() { ndh_leg("bdu", "dbdu", calf = false); }
module bone_pants_dbdu__thigh_r() { mirror([1, 0, 0]) ndh_leg("bdu", "dbdu", calf = false); }
module bone_pants_dbdu__calf_l() { ndh_leg("bdu", "dbdu", calf = true); }
module bone_pants_dbdu__calf_r() { mirror([1, 0, 0]) ndh_leg("bdu", "dbdu", calf = true); }
module bone_pants_dbdu__foot_l() { ndh_boot("bdu", "dbdu"); }
module bone_pants_dbdu__foot_r() { mirror([1, 0, 0]) ndh_boot("bdu", "dbdu"); }
module bone_pants_flora__pelvis() { ndh_pelvis("afganka", "flora"); }
module bone_pants_flora__thigh_l() { ndh_leg("afganka", "flora", calf = false); }
module bone_pants_flora__thigh_r() { mirror([1, 0, 0]) ndh_leg("afganka", "flora", calf = false); }
module bone_pants_flora__calf_l() { ndh_leg("afganka", "flora", calf = true); }
module bone_pants_flora__calf_r() { mirror([1, 0, 0]) ndh_leg("afganka", "flora", calf = true); }
module bone_pants_flora__foot_l() { ndh_boot("afganka", "flora"); }
module bone_pants_flora__foot_r() { mirror([1, 0, 0]) ndh_boot("afganka", "flora"); }
module bone_backpack_rd54__torso() { ndh_rd54(); }
module bone_backpack_rd54__harness() { ndc_harness(0); }
module bone_backpack_abalakov__torso() { ndh_abalakov(); }
module bone_backpack_abalakov__harness() { ndh_leather_harness(); }

module bone_wardrobe()
{
    bone_helmet__head();
    bone_hat_beanie__head();
    bone_cap_baseball__head();
    bone_hat_boonie__head();
    bone_helmet_tactical__head();
    bone_hat_ushanka__head();
    bone_jacket__torso();
    bone_jacket__upperarm_l();
    bone_jacket__upperarm_r();
    bone_jacket__forearm_l();
    bone_jacket__forearm_r();
    bone_hoodie__torso();
    bone_hoodie__upperarm_l();
    bone_hoodie__upperarm_r();
    bone_hoodie__forearm_l();
    bone_hoodie__forearm_r();
    bone_jacket_puffer__torso();
    bone_jacket_puffer__upperarm_l();
    bone_jacket_puffer__upperarm_r();
    bone_jacket_puffer__forearm_l();
    bone_jacket_puffer__forearm_r();
    bone_jacket_rain__torso();
    bone_jacket_rain__upperarm_l();
    bone_jacket_rain__upperarm_r();
    bone_jacket_rain__forearm_l();
    bone_jacket_rain__forearm_r();
    bone_jacket_denim__torso();
    bone_jacket_denim__upperarm_l();
    bone_jacket_denim__upperarm_r();
    bone_jacket_denim__forearm_l();
    bone_jacket_denim__forearm_r();
    bone_jacket_rescue__torso();
    bone_jacket_rescue__upperarm_l();
    bone_jacket_rescue__upperarm_r();
    bone_jacket_rescue__forearm_l();
    bone_jacket_rescue__forearm_r();
    bone_vest_military__torso();
    bone_vest_police__torso();
    bone_vest_chest_rig__torso();
    bone_vest_hunting__torso();
    bone_vest_utility__torso();
    bone_vest_rescue__torso();
    bone_work_pants__pelvis();
    bone_work_pants__thigh_l();
    bone_work_pants__thigh_r();
    bone_work_pants__calf_l();
    bone_work_pants__calf_r();
    bone_work_pants__foot_l();
    bone_work_pants__foot_r();
    bone_cargo_pants__pelvis();
    bone_cargo_pants__thigh_l();
    bone_cargo_pants__thigh_r();
    bone_cargo_pants__calf_l();
    bone_cargo_pants__calf_r();
    bone_cargo_pants__foot_l();
    bone_cargo_pants__foot_r();
    bone_jeans__pelvis();
    bone_jeans__thigh_l();
    bone_jeans__thigh_r();
    bone_jeans__calf_l();
    bone_jeans__calf_r();
    bone_jeans__foot_l();
    bone_jeans__foot_r();
    bone_pants_tactical__pelvis();
    bone_pants_tactical__thigh_l();
    bone_pants_tactical__thigh_r();
    bone_pants_tactical__calf_l();
    bone_pants_tactical__calf_r();
    bone_pants_tactical__foot_l();
    bone_pants_tactical__foot_r();
    bone_pants_rain__pelvis();
    bone_pants_rain__thigh_l();
    bone_pants_rain__thigh_r();
    bone_pants_rain__calf_l();
    bone_pants_rain__calf_r();
    bone_pants_rain__foot_l();
    bone_pants_rain__foot_r();
    bone_pants_snow__pelvis();
    bone_pants_snow__thigh_l();
    bone_pants_snow__thigh_r();
    bone_pants_snow__calf_l();
    bone_pants_snow__calf_r();
    bone_pants_snow__foot_l();
    bone_pants_snow__foot_r();
    bone_backpack_small__torso();
    bone_backpack_small__harness();
    bone_backpack__torso();
    bone_backpack__harness();
    bone_backpack_military__torso();
    bone_backpack_military__harness();
    bone_backpack_messenger__torso();
    bone_backpack_messenger__harness();
    bone_backpack_medical__torso();
    bone_backpack_medical__harness();
    bone_backpack_rolltop__torso();
    bone_backpack_rolltop__harness();
    bone_hat_panama__head();
    bone_helmet_ssh68__head();
    bone_helmet_m56__head();
    bone_helmet_pasgt__head();
    bone_hat_boonie_emr__head();
    bone_shirt_telnyashka__torso();
    bone_shirt_telnyashka__upperarm_l();
    bone_shirt_telnyashka__upperarm_r();
    bone_shirt_telnyashka__forearm_l();
    bone_shirt_telnyashka__forearm_r();
    bone_jacket_afganka__torso();
    bone_jacket_afganka__upperarm_l();
    bone_jacket_afganka__upperarm_r();
    bone_jacket_afganka__forearm_l();
    bone_jacket_afganka__forearm_r();
    bone_jacket_gorka__torso();
    bone_jacket_gorka__upperarm_l();
    bone_jacket_gorka__upperarm_r();
    bone_jacket_gorka__forearm_l();
    bone_jacket_gorka__forearm_r();
    bone_jacket_strichtarn__torso();
    bone_jacket_strichtarn__upperarm_l();
    bone_jacket_strichtarn__upperarm_r();
    bone_jacket_strichtarn__forearm_l();
    bone_jacket_strichtarn__forearm_r();
    bone_jacket_m81__torso();
    bone_jacket_m81__upperarm_l();
    bone_jacket_m81__upperarm_r();
    bone_jacket_m81__forearm_l();
    bone_jacket_m81__forearm_r();
    bone_jacket_emr__torso();
    bone_jacket_emr__upperarm_l();
    bone_jacket_emr__upperarm_r();
    bone_jacket_emr__forearm_l();
    bone_jacket_emr__forearm_r();
    bone_jacket_dbdu__torso();
    bone_jacket_dbdu__upperarm_l();
    bone_jacket_dbdu__upperarm_r();
    bone_jacket_dbdu__forearm_l();
    bone_jacket_dbdu__forearm_r();
    bone_jacket_flora__torso();
    bone_jacket_flora__upperarm_l();
    bone_jacket_flora__upperarm_r();
    bone_jacket_flora__forearm_l();
    bone_jacket_flora__forearm_r();
    bone_vest_type56__torso();
    bone_vest_6b3__torso();
    bone_pants_afganka__pelvis();
    bone_pants_afganka__thigh_l();
    bone_pants_afganka__thigh_r();
    bone_pants_afganka__calf_l();
    bone_pants_afganka__calf_r();
    bone_pants_afganka__foot_l();
    bone_pants_afganka__foot_r();
    bone_pants_gorka__pelvis();
    bone_pants_gorka__thigh_l();
    bone_pants_gorka__thigh_r();
    bone_pants_gorka__calf_l();
    bone_pants_gorka__calf_r();
    bone_pants_gorka__foot_l();
    bone_pants_gorka__foot_r();
    bone_pants_strichtarn__pelvis();
    bone_pants_strichtarn__thigh_l();
    bone_pants_strichtarn__thigh_r();
    bone_pants_strichtarn__calf_l();
    bone_pants_strichtarn__calf_r();
    bone_pants_strichtarn__foot_l();
    bone_pants_strichtarn__foot_r();
    bone_pants_m81__pelvis();
    bone_pants_m81__thigh_l();
    bone_pants_m81__thigh_r();
    bone_pants_m81__calf_l();
    bone_pants_m81__calf_r();
    bone_pants_m81__foot_l();
    bone_pants_m81__foot_r();
    bone_pants_emr__pelvis();
    bone_pants_emr__thigh_l();
    bone_pants_emr__thigh_r();
    bone_pants_emr__calf_l();
    bone_pants_emr__calf_r();
    bone_pants_emr__foot_l();
    bone_pants_emr__foot_r();
    bone_pants_dbdu__pelvis();
    bone_pants_dbdu__thigh_l();
    bone_pants_dbdu__thigh_r();
    bone_pants_dbdu__calf_l();
    bone_pants_dbdu__calf_r();
    bone_pants_dbdu__foot_l();
    bone_pants_dbdu__foot_r();
    bone_pants_flora__pelvis();
    bone_pants_flora__thigh_l();
    bone_pants_flora__thigh_r();
    bone_pants_flora__calf_l();
    bone_pants_flora__calf_r();
    bone_pants_flora__foot_l();
    bone_pants_flora__foot_r();
    bone_backpack_rd54__torso();
    bone_backpack_rd54__harness();
    bone_backpack_abalakov__torso();
    bone_backpack_abalakov__harness();
}

bone_wardrobe();
