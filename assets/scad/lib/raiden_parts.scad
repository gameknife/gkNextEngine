// NextRaiden shared geometry and airframe authoring data. No top-level geometry.
// Z-up, metres, aircraft front = -Y (ScadRig, NOT the +X car convention).
// Record: [id, pivot, size, shape, tone, releaseStage, massKg, hitPoints, rotation, group].
// Pivots: bottom-centres; rotor blades use their inner end. Physics COM: bounds-centre.
// IDs are unique WITHIN an aircraft instance; use instance + id as the runtime key.
// Stages: 1 outer armour, 2 systems/inner structure, 3 last core.
// Metadata is an authoring proposal, NOT automatic damage/physics behaviour.
// One nr_chunk = one detachable Node/Model; flatten ONLY inside the chunk.
// Shapes: 0 brick, 1 wing, 2 canopy, 3 engine, 4 core, 5 gun, 6 fin, 7 blade, 8 pod.

function nr_WHITE() = [0.53, 0.56, 0.55];
function nr_DARK() = [0.045, 0.060, 0.067];
function nr_STEEL() = [0.19, 0.24, 0.25];
function nr_BLUE() = [0.035, 0.24, 0.49];
function nr_RED() = [0.48, 0.065, 0.075];
function nr_TEAL() = [0.035, 0.36, 0.30];
function nr_YELLOW() = [0.68, 0.43, 0.045];
function nr_GLASS() = [0.055, 0.46, 0.57];
function nr_GRASS() = [0.20, 0.33, 0.16];
function nr_SOIL() = [0.26, 0.20, 0.13];
function nr_CONCRETE() = [0.29, 0.32, 0.30];
function nr_WATER() = [0.025, 0.24, 0.29];
function nr_sq(x) = (x * x + x * 677 + 71) % 65521;
function nr_rnd(s, m) = nr_sq(nr_sq(((s % 65521) + 65521) % 65521) + 43) % m;
function nr_paint(paint = 1) = [nr_BLUE(), nr_RED(), nr_TEAL(), nr_YELLOW(), [0.27, 0.19, 0.32]][paint % 5];
function nr_tone(tone, paint) = tone == 0 ? nr_paint(paint) :
    (tone == 1 ? nr_WHITE() : (tone == 2 ? nr_DARK() : (tone == 3 ? nr_STEEL() : nr_YELLOW())));
function nr_spec(id, p, s, shape = 0, tone = 0, stage = 1, mass = 12, hp = 20,
                 rot = [0, 0, 0], group = "hull") = [id, p, s, shape, tone, stage, mass, hp, rot, group];

module nr_box(s = [1, 1, 1]) { translate([0, 0, s[2] / 2]) cube(s, center = true); }
module nr_bevel(s = [1, 1, 0.4])
{
    b = min(0.12, s[0] * 0.12, s[1] * 0.12, s[2] * 0.3);
    hull()
    {
        nr_box([s[0], s[1], s[2] - b]);
        translate([0, 0, s[2] - b]) nr_box([s[0] - b * 2, s[1] - b * 2, b]);
    }
}
module nr_surface(c = [0.4, 0.1, 0.1]) { gk_material(c, roughness = 0.38, metalness = 0.08) children(); }

module nr_chunk(id = "armour", size = [1, 1, 0.35], shape = 0, tone = 0, paint = 1,
                releaseStage = 1, massKg = 12, hitPoints = 20, group = "hull", collider = "convex")
{
    gk_flatten()
    {
        c = nr_tone(tone, paint);
        if (shape == 1)
        {
            nr_surface(c) linear_extrude(height = size[2])
                polygon([[-size[0]/2, -size[1]*0.42], [size[0]*0.27, -size[1]/2],
                         [size[0]/2, size[1]*0.30], [size[0]*0.24, size[1]/2],
                         [-size[0]/2, size[1]*0.34]]);
            nr_surface(nr_WHITE()) translate([-size[0]*0.12, 0, size[2]])
                nr_box([size[0]*0.15, size[1]*0.64, 0.035]);
        }
        else if (shape == 2)
        {
            nr_surface(nr_DARK()) nr_bevel([size[0], size[1], size[2]*0.24]);
            gk_material(nr_GLASS(), roughness = 0.12) hull()
            {
                translate([0, 0, size[2]*0.20]) nr_box([size[0]*0.92, size[1]*0.93, 0.05]);
                translate([0, size[1]*0.12, size[2]*0.83]) nr_box([size[0]*0.57, size[1]*0.56, size[2]*0.17]);
            }
            nr_surface(nr_WHITE()) translate([0, size[1]*0.10, size[2]]) nr_box([0.09, size[1]*0.56, 0.03]);
        }
        else if (shape == 3)
        {
            nr_surface(c) nr_bevel(size);
            nr_surface(nr_DARK()) translate([0, size[1]/2 + 0.015, size[2]*0.53])
                rotate([90, 0, 0]) cylinder(h = 0.06, r = min(size[0], size[2])*0.35, $fn = 8);
            nr_surface(nr_YELLOW()) translate([0, size[1]/2 + 0.025, size[2]*0.53])
                rotate([90, 0, 0]) cylinder(h = 0.04, r = min(size[0], size[2])*0.19, $fn = 8);
            for (i = [-1, 0, 1])
                nr_surface(nr_DARK()) translate([0, i*size[1]*0.18, size[2]]) nr_box([size[0]*0.65, 0.09, 0.025]);
        }
        else if (shape == 4)
        {
            nr_surface(nr_STEEL()) nr_bevel(size);
            nr_surface(nr_YELLOW()) translate([0, 0, size[2]]) nr_box([size[0]*0.64, size[1]*0.70, 0.06]);
            nr_surface(nr_GLASS()) translate([0, 0, size[2]+0.06])
                cylinder(h = 0.14, r = min(size[0], size[1])*0.26, $fn = 6);
            for (s = [-1, 1]) nr_surface(nr_DARK()) translate([s*size[0]*0.38, 0, size[2]])
                nr_box([size[0]*0.12, size[1]*0.82, 0.07]);
        }
        else if (shape == 5)
        {
            nr_surface(nr_STEEL()) nr_bevel([size[0], size[1]*0.45, size[2]]);
            nr_surface(nr_DARK()) translate([0, size[1]*0.18, size[2]*0.55]) rotate([90, 0, 0])
                cylinder(h = size[1]*0.68, r = min(size[0], size[2])*0.30, $fn = 6);
            nr_surface(nr_YELLOW()) translate([0, -size[1]*0.50, size[2]*0.55]) rotate([90, 0, 0])
                cylinder(h = 0.06, r = min(size[0], size[2])*0.21, $fn = 6);
        }
        else if (shape == 6)
        {
            nr_surface(c) hull()
            {
                nr_box([size[0], size[1], 0.04]);
                translate([0, size[1]*0.2, size[2]-0.04]) nr_box([size[0]*0.68, size[1]*0.40, 0.04]);
            }
        }
        else if (shape == 7)
        {
            nr_surface(nr_DARK()) translate([size[0]/2, 0, 0]) nr_bevel(size);
            nr_surface(nr_YELLOW()) translate([size[0]*0.87, 0, size[2]]) nr_box([size[0]*0.18, size[1]*0.9, 0.025]);
        }
        else if (shape == 8)
        {
            nr_surface(c) nr_bevel(size);
            for (s = [-1, 1]) nr_surface(nr_YELLOW())
                translate([s*size[0]*0.23, -size[1]/2-0.02, size[2]*0.54]) rotate([90, 0, 0])
                    cylinder(h = 0.08, r = min(size[0]*0.17, size[2]*0.3), $fn = 6);
        }
        else
        {
            nr_surface(c) nr_bevel(size);
            if (size[0] > 0.5 && size[1] > 0.6)
                for (y = [-0.22, 0.22]) nr_surface(c)
                    translate([0, y*size[1], size[2]]) cylinder(h = 0.06, r = min(0.13, size[0]*0.16), $fn = 8);
        }
    }
}
module nr_piece(r, paint = 1)
{
    nr_chunk(id = r[0], size = r[2], shape = r[3], tone = r[4], paint = paint,
             releaseStage = r[5], massKg = r[6], hitPoints = r[7], group = r[9]);
}
module nr_assemble(parts, paint = 1, damage = 0, explode = 0)
{
    for (r = parts)
        if (r[5] > damage)
            translate([r[1][0]*(1+explode), r[1][1]*(1+explode), r[1][2]+explode*(0.3+r[1][2])])
                rotate(r[8]) nr_piece(r, paint);
}

// Rotation steps <180 degrees: 0->360 alone becomes an invisible quaternion loop.
function nr_spin(bone, period = 0.64) = [bone, "rot", [
    [0,[0,0,0]], [period/4,[0,0,90]], [period/2,[0,0,180]],
    [period*3/4,[0,0,270]], [period,[0,0,360]]]];
function nr_bank(angle = 12) = [["loop",false], ["bone_root","rot",[
    [0,[0,0,0]], [0.22,[0,angle,0]], [0.7,[0,angle,0]], [1,[0,0,0]]]]];
