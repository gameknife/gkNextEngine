// ============================================================================
// rig_forms.scad - Lofted forms for rigid-body characters (prefix rf_).
//
// One box per bone reads as a toy. A limb, a torso or a sleeve is instead
// lofted through a handful of cross-sections along the bone's Z axis, which
// gives rigid parts what boxes cannot: taper, a muscle belly or a chest that
// sits off the bone axis, sloped shoulders, and rounded ends that overlap the
// neighbouring part so a bent elbow or knee shows a joint instead of a gap.
// It stays low-poly: an 8-sided limb with six rings is ~100 triangles.
//
// A ring is [z, w, d(, x, y(, n))]: height on the axis, full width (X) and
// depth (Y), centre offset, and optionally its own superellipse exponent.
// Exponent n: 2 = ellipse, ~3 = organic, 6+ = a box with chamfered corners.
// Rings are listed bottom to top (ascending z); the first and last are capped.
//
// Shading follows the loader's 35 degree crease: 8 sides stay crisp facets,
// 12 or more shade smooth around the form. The default phase puts a flat
// facet, not an edge, on the +-X and +-Y axes, so fronts and sides are planes
// that details (pockets, plates, straps) can sit on.
//
// use <> drops top-level assignments, so everything is a function or module.
// ============================================================================

function rf_sgn(x) = x < 0 ? -1 : 1;
function rf_at(r, i, fallback) = len(r) > i ? r[i] : fallback;

// Point of a superellipse of full size w x d at angle a.
function rf_pt(a, w, d, n) =
    let(c = cos(a), s = sin(a), e = 2 / n)
    [w / 2 * rf_sgn(c) * pow(abs(c), e), d / 2 * rf_sgn(s) * pow(abs(s), e)];

function rf_ring(r, m, n, phase) =
    let(rn = rf_at(r, 5, n), cx = rf_at(r, 3, 0), cy = rf_at(r, 4, 0))
    [for (k = [0 : m - 1])
        let(p = rf_pt(phase + 360 * k / m, r[1], r[2], rn))
        [p[0] + cx, p[1] + cy, r[0]]];

function rf_loft_points(rings, m, n, phase) =
    let(first = rings[0], last = rings[len(rings) - 1])
    concat([for (r = rings) for (p = rf_ring(r, m, n, phase)) p],
           [[rf_at(first, 3, 0), rf_at(first, 4, 0), first[0]],
            [rf_at(last, 3, 0), rf_at(last, 4, 0), last[0]]]);

// OpenSCAD winds faces clockwise seen from outside.
function rf_loft_faces(count, m) =
    let(bottom = count * m, top = count * m + 1, o = (count - 1) * m)
    concat([for (i = [0 : count - 2]) for (k = [0 : m - 1])
               let(k1 = (k + 1) % m)
               [i * m + k, (i + 1) * m + k, (i + 1) * m + k1, i * m + k1]],
           [for (k = [0 : m - 1]) [bottom, k, (k + 1) % m]],
           [for (k = [0 : m - 1]) [top, o + (k + 1) % m, o + k]]);

module rf_loft(rings, m = 8, n = 2.6, phase = undef)
{
    polyhedron(points = rf_loft_points(rings, m, n, phase == undef ? 180 / m : phase),
               faces = rf_loft_faces(len(rings), m));
}

// The same loft scaled about its own axis: a garment layer over a body form.
function rf_grow(rings, dw, dd = undef) =
    [for (r = rings) concat([r[0], r[1] + dw, r[2] + (dd == undef ? dw : dd), rf_at(r, 3, 0), rf_at(r, 4, 0)],
                            len(r) > 5 ? [r[5]] : [])];

// Only the rings between z0 and z1, with the cut ends interpolated: a cuff,
// a band or a pad that follows the form it sits on.
function rf_mix_ring(a, b, z) =
    let(t = (z - a[0]) / (b[0] - a[0]))
    [z, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t,
     rf_at(a, 3, 0) + (rf_at(b, 3, 0) - rf_at(a, 3, 0)) * t,
     rf_at(a, 4, 0) + (rf_at(b, 4, 0) - rf_at(a, 4, 0)) * t];
function rf_ring_at(rings, z) =
    let(hits = [for (i = [0 : len(rings) - 2]) if (z >= rings[i][0] && z <= rings[i + 1][0]) i])
    len(hits) == 0 ? (z < rings[0][0] ? rf_mix_ring(rings[0], rings[1], z)
                                      : rf_mix_ring(rings[len(rings) - 2], rings[len(rings) - 1], z))
                   : rf_mix_ring(rings[hits[0]], rings[hits[0] + 1], z);
function rf_span(rings, z0, z1) =
    concat([rf_ring_at(rings, z0)],
           [for (r = rings) if (r[0] > z0 + 1e-5 && r[0] < z1 - 1e-5)
               [r[0], r[1], r[2], rf_at(r, 3, 0), rf_at(r, 4, 0)]],
           [rf_ring_at(rings, z1)]);

// Chamfered box for hard details: buckles, pouches, plates, soles.
module rf_box(size, bevel = 0.012)
{
    hull()
    {
        cube([size[0] - 2 * bevel, size[1], size[2] - 2 * bevel], center = true);
        cube([size[0], size[1] - 2 * bevel, size[2] - 2 * bevel], center = true);
        cube([size[0] - 2 * bevel, size[1] - 2 * bevel, size[2]], center = true);
    }
}
