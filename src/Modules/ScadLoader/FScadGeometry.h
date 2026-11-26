#pragma once

// ============================================================================
// FScadGeometry.h - Primitive tessellation for the SCAD loader.
//
// All builders emit a triangle soup (groups of 3 vertices) in native SCAD
// object space (right-handed, Z-up, double precision). Triangles are wound CCW
// as seen from outside so a face normal of cross(v1-v0, v2-v0) points outward.
// The loader bakes these into engine vertices (Y-up + scale + flat normals).
// ============================================================================

#include <vector>

#include <glm/glm.hpp>

namespace Assets::Scad
{
    using TriSoup = std::vector<glm::dvec3>;

    class ScadGeometry
    {
    public:
        // Number of facets for a circle of radius r, replicating OpenSCAD's
        // get_fragments_from_r($fn, $fa, $fs). fn<=0 falls back to fa/fs.
        static int Fragments(double r, double fn, double fa, double fs);

        // size is the full extent on each axis; center toggles origin centering.
        static void BuildCube(const glm::dvec3& size, bool center, TriSoup& out);

        static void BuildSphere(double radius, int fragments, TriSoup& out);

        // Cylinder/cone along +Z. r1 = bottom radius, r2 = top radius.
        static void BuildCylinder(double height, double r1, double r2, bool center, int fragments, TriSoup& out);

        // points indexed by faces; each face is a polygon (OpenSCAD clockwise-from-outside).
        static void BuildPolyhedron(const std::vector<glm::dvec3>& points,
                                    const std::vector<std::vector<int>>& faces,
                                    TriSoup& out);

        // Extrudes a closed 2D outline (XY) along +Z by height. center offsets to [-h/2, h/2].
        static void ExtrudePolygon(const std::vector<glm::dvec2>& outline, double height, bool center, TriSoup& out);

        // Revolves closed 2D profiles (XY, x = radius >= 0) around the Z axis by
        // angleDeg degrees using `segments` angular divisions. Profiles must be
        // CCW. angleDeg >= ~360 produces a closed surface; partial sweeps add caps.
        static void BuildRotateExtrude(const std::vector<std::vector<glm::dvec2>>& profiles,
                                       double angleDeg,
                                       int segments,
                                       TriSoup& out);

        // gk_round_box: a closed box whose edges and corners are rounded with
        // `arcSegments` steps per quarter arc. Each face owns its flat part and
        // the arcs up to 45 deg on its side, so all six faces share exact seam
        // vertices (watertight, and smooth normals meet across the seams).
        // r is clamped to 0.49 * min(size); power != 2 squares the corners
        // into a superellipsoid (larger = squarer). outNormals, when given,
        // receives the exact surface normal of every corner, parallel to out.
        static void BuildRoundBox(const glm::dvec3& size, double r, int arcSegments, double power, bool center,
                                  TriSoup& out, TriSoup* outNormals = nullptr);

        enum class SweepCap
        {
            None,
            Flat,
            Round
        };

        // gk_sweep: a tube through `points` (chordal Catmull-Rom), radius
        // interpolated from `radii` (one per point). Rings ride a parallel
        // transport (rotation-minimising) frame, so the tube never flips at
        // an inflection; `twistDeg` turns the rings progressively along it.
        // detail scales the number of samples per bend. outNormals, when given,
        // receives exact normals (taper-aware sides, sphere caps, flat caps).
        static void BuildSweep(const std::vector<glm::dvec3>& points, const std::vector<double>& radii, int sides,
                               SweepCap cap, double twistDeg, double detail, TriSoup& out,
                               TriSoup* outNormals = nullptr);
    };
} // namespace Assets::Scad
