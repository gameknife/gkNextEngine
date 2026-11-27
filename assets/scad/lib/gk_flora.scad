// ============================================================================
// gk_flora.scad - Shared tree and shrub building blocks (prefix gf_).
//
// The regional kits (kit_bohemia, kit_savanna, kit_coldwar, kit_hokkaido) keep
// their own species, proportions and palettes and build them from these:
//
//   gf_trunk / gf_limb   swept stems (gk_sweep): a root collar that flares into
//                        the ground and a gentle bend, instead of a straight
//                        cylinder standing on the terrain.
//   gf_crown             a broadleaf crown as one soft volume: a core ellipsoid
//                        plus clumps on its upper surface, all under
//                        gk_puff_normals so the overlaps shade as one mass (no
//                        black seams), lit through by gk_foliage, coloured on a
//                        three-step ramp from cool and dark underneath to warm
//                        and bright on top. gk_crease(70) smooths each clump
//                        first: puffing normals that are still split at 45
//                        degree facets leaves the facets showing.
//   gf_conifer           tiers that droop at the rim like real spruce boughs,
//                        on the same ramp and the same puffed volume, instead of
//                        stacked paper cones.
//
// Colours come in as the species' mid tone; gf_low / gf_high derive the ramp.
// use <> drops top-level assignments, so everything is a function or module.
// ============================================================================

function gf_rnd(seed, i = 0) = let(x = sin(seed * 12.9898 + i * 78.233 + 0.5) * 43758.5453) x - floor(x);
function gf_rf(seed, a, b, i = 0) = a + (b - a) * gf_rnd(seed, i);
function gf_clamp(x, a = 0, b = 1) = min(b, max(a, x));
function gf_mul(c, k) = [c[0] * k[0], c[1] * k[1], c[2] * k[2]];

// Underside: darker and a little cooler. Top: lighter and warmer.
function gf_low(c) = gf_mul(c, [0.70, 0.76, 0.86]);
function gf_high(c) = gf_mul(c, [1.22, 1.17, 1.00]);
// Three steps along the ramp keep a crown to three materials.
function gf_ramp(c, t) = let(s = round(gf_clamp(t) * 2) / 2)
    s < 0.25 ? gf_low(c) : s < 0.75 ? c : gf_high(c);

// Bark: matte with the bark print's relief.
module gf_bark(c, s = 0.8) gk_material(c, roughness = 1) gk_texture("bark", scale = s) children();
module gf_leaf(c, transmission = 0.55, thickness = 0.16) gk_foliage(c, transmission = transmission, thickness = thickness) children();

// Stem from the ground to height h: radius r0 at the base, r1 at the top,
// the top displaced by bend = [dx, dy]. The collar flares out by `flare` and
// sinks a little below z = 0 so it never floats on a slope.
module gf_trunk(h, r0, r1, bend = [0, 0], flare = 1.5, fn = 7)
    gk_sweep([[0, 0, -0.10], [0, 0, min(0.35, h * 0.06)], [bend[0] * 0.3, bend[1] * 0.3, h * 0.45], [bend[0], bend[1], h]],
             [r0 * flare, r0, (r0 + r1) * 0.55, r1], fn = fn, cap = "flat");

// A limb from p0 to p1 that arches up by `lift` along the way.
module gf_limb(p0, p1, r0, r1, lift = 0.12, fn = 5)
    let(m = (p0 + p1) / 2, l = norm(p1 - p0))
    gk_sweep([p0, m + [0, 0, l * lift], p1], [r0, (r0 + r1) / 2, r1], fn = fn, cap = "round", $gk_detail = 0.6);

// Limbs fanning out from height z on the stem to the crown: n limbs reaching
// out to radius `reach` and up to zTop.
module gf_limbs(seed, n, z, reach, zTop, r0, fn = 5)
    for (i = [0 : n - 1]) let(a = i * 360 / n + gf_rf(seed, -25, 25, i), rr = reach * gf_rf(seed, 0.7, 1.0, i + 11))
        gf_limb([0, 0, z], [rr * cos(a), rr * sin(a), zTop * gf_rf(seed, 0.92, 1.04, i + 23)], r0, r0 * 0.3, fn = fn);

// Broadleaf crown centred at `centre`: a core ellipsoid of radius R squashed
// to `squash` in height, and n clumps of radius R * [0.40 .. 0.62] spread over
// its upper surface (lean pushes them sideways). fn is the clump resolution.
module gf_crown(seed, centre, R, c, squash = 0.85, n = 7, spread = 0.72, lean = [0, 0],
                transmission = 0.55, thickness = 0.16, fn = 10, puff = 0.7)
{
    zLo = centre[2] - R * squash;
    zHi = centre[2] + R * squash * 1.15;
    gk_crease(70) gk_puff_normals(strength = puff, upBias = 0.3)
    {
        gf_leaf(gf_ramp(c, 0.5), transmission, thickness)
            translate(centre) scale([1, 1, squash]) sphere(r = R, $fn = fn);
        for (i = [0 : n - 1])
            let(a = i * 137.5 + gf_rf(seed, 0, 40, i), el = gf_rf(seed, -15, 55, i + 31),
                d = R * spread, r = R * gf_rf(seed, 0.40, 0.62, i + 57),
                p = centre + [d * cos(el) * cos(a) + lean[0] * R, d * cos(el) * sin(a) + lean[1] * R, d * sin(el) * squash])
                gf_leaf(gf_ramp(c, (p[2] - zLo) / (zHi - zLo)), transmission, thickness)
                    translate(p) scale([1, 1, squash * 0.95]) sphere(r = r, $fn = fn - 2);
    }
}

// A loose shrub: n clumps of radius ~r scattered over a footprint of radius
// `spread`, sitting on the ground, on the same ramp.
module gf_shrub(seed, r, c, n = 5, spread = 0.6, squash = 0.8, transmission = 0.5, fn = 8)
    gk_crease(70) gk_puff_normals(strength = 0.65, upBias = 0.35)
        for (i = [0 : n - 1])
            let(rr = r * gf_rf(seed, 0.55, 0.85, i + 7), x = gf_rf(seed, -spread, spread, i + 13) * r,
                y = gf_rf(seed, -spread, spread, i + 19) * r, z = rr * squash * gf_rf(seed, 0.75, 1.15, i + 29))
                gf_leaf(gf_ramp(c, z / (r * squash * 1.6)), transmission, 0.14)
                    translate([x, y, z]) scale([1, 1, squash]) sphere(r = rr, $fn = fn);

// One bough tier: a lathed skirt that rises from radius 0 at the top to r at
// the rim and droops down under it, height t.
module gf_tier(r, t, droop = 0.30, fn = 9)
    rotate_extrude($fn = fn)
        polygon([[0, t], [r * 0.38, t * 0.62], [r * 0.82, t * 0.16], [r, -t * droop * 0.25],
                 [r * 0.80, -t * droop * 0.55], [r * 0.45, t * 0.05], [0, t * 0.12]]);

// Spruce / fir silhouette: trunk to h, `tiers` bough skirts from crownBase up
// to the tip, radius rBase at the bottom tapering to a point, overlapped so
// the tiers read as one dense cone with drooping edges.
module gf_conifer(seed, h, rBase, c, trunk, tiers = 6, crownBase = 0.18, droop = 0.30,
                  overlap = 1.7, fn = 9, rTrunk = -1, puff = 0.45)
{
    rt = rTrunk > 0 ? rTrunk : h * 0.017;
    cb = h * crownBase;
    gf_bark(trunk) gf_trunk(h * 0.93, rt, rt * 0.25, fn = 6);
    gk_crease(70) gk_puff_normals(strength = puff, upBias = 0.2)
        for (i = [0 : tiers - 1]) let(f = i / tiers, z = cb + (h - cb) * f, r = rBase * (1 - f * 0.86) + h * 0.012)
            gf_leaf(gf_ramp(c, f * 1.15), 0.45, 0.14)
                translate([0, 0, z]) rotate([gf_rf(seed, -3, 3, i), gf_rf(seed, -3, 3, i + 5), i * 31 + gf_rf(seed, 0, 30, i + 9)])
                    gf_tier(r, (h - cb) / tiers * overlap, droop, fn);
}
