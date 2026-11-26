#include "Modules/ScadLoader/FScadGeometry.h"

#include "Modules/ScadLoader/FScadTypes.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>

namespace Assets::Scad
{
    namespace
    {
        double SignedArea2D(const std::vector<glm::dvec2>& poly)
        {
            double twice = 0.0;
            const size_t n = poly.size();
            for (size_t i = 0; i < n; ++i)
            {
                const glm::dvec2& a = poly[i];
                const glm::dvec2& b = poly[(i + 1) % n];
                twice += a.x * b.y - b.x * a.y;
            }
            return twice * 0.5;
        }
    } // namespace

    int ScadGeometry::Fragments(double r, double fn, double fa, double fs)
    {
        if (r < 1e-9)
        {
            return 3;
        }
        if (fn > 0.0)
        {
            return std::max(3, static_cast<int>(fn));
        }
        if (fa <= 0.0)
        {
            fa = 12.0;
        }
        if (fs <= 0.0)
        {
            fs = 2.0;
        }
        const double byAngle = 360.0 / fa;
        const double byLength = (r * 2.0 * kPi) / fs;
        return std::max(5, static_cast<int>(std::ceil(std::min(byAngle, byLength))));
    }

    void ScadGeometry::BuildCube(const glm::dvec3& size, bool center, TriSoup& out)
    {
        const glm::dvec3 mn = center ? -size * 0.5 : glm::dvec3(0.0);
        const glm::dvec3 mx = center ? size * 0.5 : size;

        const glm::dvec3 c[8] = {
            {mn.x, mn.y, mn.z}, // 0
            {mx.x, mn.y, mn.z}, // 1
            {mx.x, mx.y, mn.z}, // 2
            {mn.x, mx.y, mn.z}, // 3
            {mn.x, mn.y, mx.z}, // 4
            {mx.x, mn.y, mx.z}, // 5
            {mx.x, mx.y, mx.z}, // 6
            {mn.x, mx.y, mx.z}, // 7
        };

        // Each face as a quad with outward-CCW winding.
        static const int quads[6][4] = {
            {0, 3, 2, 1}, // bottom (-Z)
            {4, 5, 6, 7}, // top (+Z)
            {0, 1, 5, 4}, // front (-Y)
            {2, 3, 7, 6}, // back (+Y)
            {1, 2, 6, 5}, // right (+X)
            {0, 4, 7, 3}, // left (-X)
        };

        for (const auto& q : quads)
        {
            out.push_back(c[q[0]]);
            out.push_back(c[q[1]]);
            out.push_back(c[q[2]]);
            out.push_back(c[q[0]]);
            out.push_back(c[q[2]]);
            out.push_back(c[q[3]]);
        }
    }

    void ScadGeometry::BuildRotateExtrude(const std::vector<std::vector<glm::dvec2>>& profiles,
                                          double angleDeg,
                                          int segments,
                                          TriSoup& out)
    {
        const int segs = std::max(2, segments);
        const bool full = angleDeg >= 359.999;
        const double angleRad = angleDeg * kPi / 180.0;

        for (const std::vector<glm::dvec2>& profileIn : profiles)
        {
            if (profileIn.size() < 3) continue;
            std::vector<glm::dvec2> profile = profileIn;
            if (SignedArea2D(profile) < 0.0) std::reverse(profile.begin(), profile.end()); // force CCW

            auto vert = [&](const glm::dvec2& p, int k)
            {
                const double th = angleRad * static_cast<double>(k) / static_cast<double>(segs);
                return glm::dvec3(p.x * std::cos(th), p.x * std::sin(th), p.y);
            };

            const size_t m = profile.size();
            for (size_t i = 0; i < m; ++i)
            {
                const glm::dvec2& pi = profile[i];
                const glm::dvec2& pj = profile[(i + 1) % m];
                for (int k = 0; k < segs; ++k)
                {
                    const int k1 = (full && k == segs - 1) ? 0 : (k + 1);
                    const glm::dvec3 a = vert(pi, k);
                    const glm::dvec3 b = vert(pi, k1);
                    const glm::dvec3 c = vert(pj, k1);
                    const glm::dvec3 d = vert(pj, k);
                    out.push_back(a);
                    out.push_back(b);
                    out.push_back(c);
                    out.push_back(a);
                    out.push_back(c);
                    out.push_back(d);
                }
            }

            // End caps for partial sweeps (fan; correct for convex profiles).
            if (!full)
            {
                for (size_t k = 1; k + 1 < m; ++k)
                {
                    // Start cap at angle 0 (faces toward -sweep).
                    out.push_back(vert(profile[0], 0));
                    out.push_back(vert(profile[k + 1], 0));
                    out.push_back(vert(profile[k], 0));
                    // End cap at angle (faces toward +sweep).
                    out.push_back(vert(profile[0], segs));
                    out.push_back(vert(profile[k], segs));
                    out.push_back(vert(profile[k + 1], segs));
                }
            }
        }
    }

    void ScadGeometry::BuildSphere(double radius, int fragments, TriSoup& out)
    {
        const int sectors = std::max(3, fragments);
        const int rings = std::max(2, fragments / 2);

        // The last sector wraps to sector 0 and both poles are exact points:
        // sin(2 pi) and sin(pi) are not 0 in floating point, and a seam vertex
        // that differs in the last bit is not shared, so normal smoothing
        // would leave a hard line down the side and a dark dot at the pole.
        auto pointAt = [&](int ring, int sector)
        {
            sector %= sectors;
            const double theta = kPi * static_cast<double>(ring) / static_cast<double>(rings); // 0..pi
            const double phi = 2.0 * kPi * static_cast<double>(sector) / static_cast<double>(sectors);
            const double ringR = (ring == 0 || ring == rings) ? 0.0 : radius * std::sin(theta);
            return glm::dvec3(ringR * std::cos(phi), ringR * std::sin(phi), radius * std::cos(theta));
        };

        for (int ring = 0; ring < rings; ++ring)
        {
            for (int sector = 0; sector < sectors; ++sector)
            {
                const glm::dvec3 a = pointAt(ring, sector);
                const glm::dvec3 b = pointAt(ring, sector + 1);
                const glm::dvec3 cc = pointAt(ring + 1, sector + 1);
                const glm::dvec3 d = pointAt(ring + 1, sector);

                if (ring != 0)
                {
                    out.push_back(a);
                    out.push_back(d);
                    out.push_back(b);
                }
                if (ring != rings - 1)
                {
                    out.push_back(b);
                    out.push_back(d);
                    out.push_back(cc);
                }
            }
        }
    }

    void ScadGeometry::BuildCylinder(double height, double r1, double r2, bool center, int fragments, TriSoup& out)
    {
        const int frag = std::max(3, fragments);
        const double z0 = center ? -height * 0.5 : 0.0;
        const double z1 = center ? height * 0.5 : height;
        r1 = std::max(0.0, r1);
        r2 = std::max(0.0, r2);

        auto ring = [&](double radius, double z, int i)
        {
            // Wrap to vertex 0 exactly, so the side smooths across the seam.
            const double a = 2.0 * kPi * static_cast<double>(i % frag) / static_cast<double>(frag);
            return glm::dvec3(radius * std::cos(a), radius * std::sin(a), z);
        };

        for (int i = 0; i < frag; ++i)
        {
            const glm::dvec3 b0 = ring(r1, z0, i);
            const glm::dvec3 b1 = ring(r1, z0, i + 1);
            const glm::dvec3 t0 = ring(r2, z1, i);
            const glm::dvec3 t1 = ring(r2, z1, i + 1);

            // Side wall (skip degenerate slivers when a radius is zero).
            if (r1 > 1e-9 || r2 > 1e-9)
            {
                if (r1 > 1e-9 && r2 > 1e-9)
                {
                    out.push_back(b0);
                    out.push_back(b1);
                    out.push_back(t1);
                    out.push_back(b0);
                    out.push_back(t1);
                    out.push_back(t0);
                }
                else if (r2 <= 1e-9)
                {
                    // Cone narrowing to a top point.
                    out.push_back(b0);
                    out.push_back(b1);
                    out.push_back(glm::dvec3(0.0, 0.0, z1));
                }
                else
                {
                    // Cone narrowing to a bottom point.
                    out.push_back(glm::dvec3(0.0, 0.0, z0));
                    out.push_back(t1);
                    out.push_back(t0);
                }
            }

            // Bottom cap (normal -Z).
            if (r1 > 1e-9)
            {
                out.push_back(glm::dvec3(0.0, 0.0, z0));
                out.push_back(b1);
                out.push_back(b0);
            }
            // Top cap (normal +Z).
            if (r2 > 1e-9)
            {
                out.push_back(glm::dvec3(0.0, 0.0, z1));
                out.push_back(t0);
                out.push_back(t1);
            }
        }
    }

    void ScadGeometry::BuildPolyhedron(const std::vector<glm::dvec3>& points,
                                       const std::vector<std::vector<int>>& faces,
                                       TriSoup& out)
    {
        for (const auto& face : faces)
        {
            if (face.size() < 3)
            {
                continue;
            }
            // OpenSCAD winds faces clockwise as seen from outside; reverse to CCW.
            std::vector<int> idx(face.rbegin(), face.rend());
            bool valid = true;
            for (int v : idx)
            {
                if (v < 0 || v >= static_cast<int>(points.size()))
                {
                    valid = false;
                    break;
                }
            }
            if (!valid)
            {
                continue;
            }
            for (size_t k = 1; k + 1 < idx.size(); ++k)
            {
                out.push_back(points[idx[0]]);
                out.push_back(points[idx[k]]);
                out.push_back(points[idx[k + 1]]);
            }
        }
    }

    void ScadGeometry::ExtrudePolygon(const std::vector<glm::dvec2>& outlineIn, double height, bool center, TriSoup& out)
    {
        if (outlineIn.size() < 3)
        {
            return;
        }

        std::vector<glm::dvec2> outline = outlineIn;
        if (SignedArea2D(outline) < 0.0)
        {
            std::reverse(outline.begin(), outline.end()); // force CCW
        }

        const double z0 = center ? -height * 0.5 : 0.0;
        const double z1 = center ? height * 0.5 : height;
        const size_t n = outline.size();

        auto bottom = [&](size_t i) { return glm::dvec3(outline[i].x, outline[i].y, z0); };
        auto top = [&](size_t i) { return glm::dvec3(outline[i].x, outline[i].y, z1); };

        // Top cap (+Z), fan from vertex 0.
        for (size_t k = 1; k + 1 < n; ++k)
        {
            out.push_back(top(0));
            out.push_back(top(k));
            out.push_back(top(k + 1));
        }
        // Bottom cap (-Z), reversed fan.
        for (size_t k = 1; k + 1 < n; ++k)
        {
            out.push_back(bottom(0));
            out.push_back(bottom(k + 1));
            out.push_back(bottom(k));
        }
        // Side walls (outward for CCW outline).
        for (size_t i = 0; i < n; ++i)
        {
            const size_t j = (i + 1) % n;
            out.push_back(bottom(i));
            out.push_back(bottom(j));
            out.push_back(top(i));

            out.push_back(bottom(j));
            out.push_back(top(j));
            out.push_back(top(i));
        }
    }

    void ScadGeometry::BuildRoundBox(const glm::dvec3& size, double r, int arcSegments, double power, bool center,
                                     TriSoup& out, TriSoup* outNormals)
    {
        const double minSide = std::min(size.x, std::min(size.y, size.z));
        const double radius = std::clamp(r, 0.0, 0.49 * minSide);
        if (radius <= 1e-9)
        {
            BuildCube(size, center, out);
            return;
        }
        const glm::dvec3 half = size * 0.5;
        const glm::dvec3 inner = half - glm::dvec3(radius);
        const glm::dvec3 origin = center ? glm::dvec3(0.0) : half;
        const double p = std::clamp(power, 0.5, 16.0);

        // Arc samples of one face side as tan(angle from the face normal):
        // 0, step, 2 step, ... up to 45 deg, where the neighbouring face takes
        // over. The 45 deg sample is exactly 1 so both faces build the seam
        // vertex from identical numbers.
        const double step = 90.0 / std::max(1, arcSegments);
        std::vector<double> tans;
        for (int k = 0; k * step < 45.0 - 1e-9; ++k)
        {
            tans.push_back(std::tan(k * step * kDeg2Rad));
        }
        tans.push_back(1.0);

        // One axis of a face grid: the negative arc (45 -> 0), then the positive
        // arc (0 -> 45). The flat part is the span between the two 0 samples.
        struct Sample
        {
            double sign;
            double t;
        };
        std::vector<Sample> samples;
        for (size_t i = tans.size(); i-- > 0;)
        {
            samples.push_back({-1.0, tans[i]});
        }
        for (double t : tans)
        {
            samples.push_back({1.0, t});
        }
        const size_t m = samples.size();

        const auto vertex = [&](int axisN, double signN, int axisU, const Sample& su, int axisV, const Sample& sv)
        {
            glm::dvec3 d(0.0);
            glm::dvec3 q(0.0);
            d[axisN] = signN;
            d[axisU] = su.sign * su.t;
            d[axisV] = sv.sign * sv.t;
            q[axisN] = signN * inner[axisN];
            q[axisU] = su.sign * inner[axisU];
            q[axisV] = sv.sign * inner[axisV];
            const double len = p == 2.0 ? std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z)
                                        : std::pow(std::pow(std::abs(d.x), p) + std::pow(std::abs(d.y), p) +
                                                       std::pow(std::abs(d.z), p),
                                                   1.0 / p);
            // Exact surface normal: the gradient of |x|^p + |y|^p + |z|^p at the
            // rounding offset (d itself for a sphere).
            glm::dvec3 n = d;
            if (p != 2.0)
            {
                for (int a = 0; a < 3; ++a)
                {
                    n[a] = (d[a] < 0.0 ? -1.0 : 1.0) * std::pow(std::abs(d[a]), p - 1.0);
                }
            }
            return std::pair<glm::dvec3, glm::dvec3>(origin + q + d * (radius / len), glm::normalize(n));
        };

        std::vector<glm::dvec3> grid(m * m);
        std::vector<glm::dvec3> gridNormals(m * m);
        for (int axisN = 0; axisN < 3; ++axisN)
        {
            const int axisU = (axisN + 1) % 3;
            const int axisV = (axisN + 2) % 3; // e_u x e_v = e_n
            for (double signN : {1.0, -1.0})
            {
                for (size_t i = 0; i < m; ++i)
                {
                    for (size_t j = 0; j < m; ++j)
                    {
                        const auto [position, normal] = vertex(axisN, signN, axisU, samples[i], axisV, samples[j]);
                        grid[i * m + j] = position;
                        gridNormals[i * m + j] = normal;
                    }
                }
                for (size_t i = 0; i + 1 < m; ++i)
                {
                    for (size_t j = 0; j + 1 < m; ++j)
                    {
                        const glm::dvec3& p00 = grid[i * m + j];
                        const glm::dvec3& p10 = grid[(i + 1) * m + j];
                        const glm::dvec3& p11 = grid[(i + 1) * m + j + 1];
                        const glm::dvec3& p01 = grid[i * m + j + 1];
                        if (signN > 0.0)
                        {
                            out.insert(out.end(), {p00, p10, p11, p00, p11, p01});
                        }
                        else
                        {
                            out.insert(out.end(), {p00, p11, p10, p00, p01, p11});
                        }
                        if (outNormals)
                        {
                            const glm::dvec3& n00 = gridNormals[i * m + j];
                            const glm::dvec3& n10 = gridNormals[(i + 1) * m + j];
                            const glm::dvec3& n11 = gridNormals[(i + 1) * m + j + 1];
                            const glm::dvec3& n01 = gridNormals[i * m + j + 1];
                            if (signN > 0.0)
                                outNormals->insert(outNormals->end(), {n00, n10, n11, n00, n11, n01});
                            else
                                outNormals->insert(outNormals->end(), {n00, n11, n10, n00, n01, n11});
                        }
                    }
                }
            }
        }
    }

    namespace
    {
        // Barry-Goldman evaluation of a Catmull-Rom segment p1 -> p2 with
        // knots t0..t3 (chordal parameterisation), at t in [t1, t2].
        glm::dvec3 CatmullRom(const glm::dvec3& p0, const glm::dvec3& p1, const glm::dvec3& p2, const glm::dvec3& p3,
                              double t0, double t1, double t2, double t3, double t)
        {
            const auto lerp = [](const glm::dvec3& a, const glm::dvec3& b, double ta, double tb, double x)
            {
                const double span = tb - ta;
                return span > 1e-12 ? a + (b - a) * ((x - ta) / span) : a;
            };
            const glm::dvec3 a1 = lerp(p0, p1, t0, t1, t);
            const glm::dvec3 a2 = lerp(p1, p2, t1, t2, t);
            const glm::dvec3 a3 = lerp(p2, p3, t2, t3, t);
            const glm::dvec3 b1 = lerp(a1, a2, t0, t2, t);
            const glm::dvec3 b2 = lerp(a2, a3, t1, t3, t);
            return lerp(b1, b2, t1, t2, t);
        }

        glm::dvec3 SafeNormalize(const glm::dvec3& v, const glm::dvec3& fallback)
        {
            const double len = glm::length(v);
            return len > 1e-12 ? v / len : fallback;
        }
    } // namespace

    void ScadGeometry::BuildSweep(const std::vector<glm::dvec3>& pointsIn, const std::vector<double>& radiiIn,
                                  int sides, SweepCap cap, double twistDeg, double detail, TriSoup& out,
                                  TriSoup* outNormals)
    {
        // Drop repeated points: a zero-length chord has no direction.
        std::vector<glm::dvec3> points;
        std::vector<double> radii;
        for (size_t i = 0; i < pointsIn.size(); ++i)
        {
            if (!points.empty() && glm::length(pointsIn[i] - points.back()) < 1e-9)
            {
                continue;
            }
            const double radius = i < radiiIn.size() ? radiiIn[i] : (radiiIn.empty() ? 0.05 : radiiIn.back());
            points.push_back(pointsIn[i]);
            radii.push_back(std::max(1e-5, radius));
        }
        if (points.size() < 2)
        {
            return;
        }
        sides = std::max(3, sides);
        const size_t n = points.size();

        // Phantom end points continue the end chords, so the curve leaves the
        // first and last point along them.
        const auto control = [&](std::ptrdiff_t i) -> glm::dvec3
        {
            if (i < 0)
                return 2.0 * points[0] - points[1];
            if (i >= static_cast<std::ptrdiff_t>(n))
                return 2.0 * points[n - 1] - points[n - 2];
            return points[static_cast<size_t>(i)];
        };

        std::vector<glm::dvec3> centres;
        std::vector<double> ringRadii;
        const double stepDeg = 12.0 / std::clamp(detail, 0.05, 4.0);
        for (size_t seg = 0; seg + 1 < n; ++seg)
        {
            const auto i = static_cast<std::ptrdiff_t>(seg);
            const glm::dvec3 p0 = control(i - 1);
            const glm::dvec3 p1 = control(i);
            const glm::dvec3 p2 = control(i + 1);
            const glm::dvec3 p3 = control(i + 2);
            const double t0 = 0.0;
            const double t1 = t0 + std::max(1e-9, glm::length(p1 - p0));
            const double t2 = t1 + std::max(1e-9, glm::length(p2 - p1));
            const double t3 = t2 + std::max(1e-9, glm::length(p3 - p2));
            // Enough samples that the tube turns at most ~stepDeg per ring.
            const glm::dvec3 tangentIn = SafeNormalize(p2 - p0, glm::dvec3(0, 0, 1));
            const glm::dvec3 tangentOut = SafeNormalize(p3 - p1, glm::dvec3(0, 0, 1));
            const double turn = std::acos(std::clamp(glm::dot(tangentIn, tangentOut), -1.0, 1.0)) / kDeg2Rad;
            const int steps = n == 2 ? 1 : std::clamp(static_cast<int>(std::ceil(turn / stepDeg)), 1, 32);
            for (int k = 0; k < steps; ++k)
            {
                const double u = static_cast<double>(k) / steps;
                centres.push_back(k == 0 ? p1 : CatmullRom(p0, p1, p2, p3, t0, t1, t2, t3, t1 + (t2 - t1) * u));
                ringRadii.push_back(radii[seg] + (radii[seg + 1] - radii[seg]) * u);
            }
        }
        centres.push_back(points[n - 1]);
        ringRadii.push_back(radii[n - 1]);
        const size_t count = centres.size();

        std::vector<glm::dvec3> tangents(count);
        for (size_t k = 0; k < count; ++k)
        {
            const glm::dvec3 ahead = centres[std::min(k + 1, count - 1)];
            const glm::dvec3 behind = centres[k == 0 ? 0 : k - 1];
            tangents[k] = SafeNormalize(ahead - behind, k > 0 ? tangents[k - 1] : glm::dvec3(0, 0, 1));
        }

        // Rotation-minimising frame by double reflection (Wang et al. 2008):
        // unlike a Frenet frame it never flips 180 deg at an inflection.
        std::vector<glm::dvec3> normals(count);
        {
            const glm::dvec3& t = tangents[0];
            const glm::dvec3 ref = std::abs(t.z) < 0.9 ? glm::dvec3(0, 0, 1) : glm::dvec3(1, 0, 0);
            normals[0] = SafeNormalize(ref - t * glm::dot(ref, t), glm::dvec3(1, 0, 0));
        }
        for (size_t k = 0; k + 1 < count; ++k)
        {
            const glm::dvec3 v1 = centres[k + 1] - centres[k];
            const double c1 = glm::dot(v1, v1);
            glm::dvec3 rL = normals[k];
            glm::dvec3 tL = tangents[k];
            if (c1 > 1e-18)
            {
                rL -= (2.0 / c1) * glm::dot(v1, normals[k]) * v1;
                tL -= (2.0 / c1) * glm::dot(v1, tangents[k]) * v1;
            }
            const glm::dvec3 v2 = tangents[k + 1] - tL;
            const double c2 = glm::dot(v2, v2);
            glm::dvec3 next = c2 > 1e-18 ? rL - (2.0 / c2) * glm::dot(v2, rL) * v2 : rL;
            next -= tangents[k + 1] * glm::dot(next, tangents[k + 1]);
            normals[k + 1] = SafeNormalize(next, normals[k]);
        }

        double totalLength = 0.0;
        std::vector<double> along(count, 0.0);
        for (size_t k = 1; k < count; ++k)
        {
            totalLength += glm::length(centres[k] - centres[k - 1]);
            along[k] = totalLength;
        }

        // Radius slope along the tube: a tapered tube leans its normal back
        // toward the thin end by atan(dr/ds).
        std::vector<double> slope(count, 0.0);
        for (size_t k = 0; k < count; ++k)
        {
            const size_t a = k == 0 ? 0 : k - 1;
            const size_t b = std::min(k + 1, count - 1);
            const double ds = along[b] - along[a];
            slope[k] = ds > 1e-12 ? (ringRadii[b] - ringRadii[a]) / ds : 0.0;
        }

        // Ring k of the frame, possibly moved along the tangent (cap rings),
        // with its exact normals. `lean` is the tangent component of the normal
        // per unit radial (sphere caps: tan(phi); the side: -dr/ds). The angle
        // runs from the normal toward tangent x normal, i.e. counter-clockwise
        // seen from ahead, which makes the quads below face outward.
        struct Ring
        {
            std::vector<glm::dvec3> points;
            std::vector<glm::dvec3> normals;
        };
        const auto ring = [&](size_t k, const glm::dvec3& centre, double radius, double lean)
        {
            const glm::dvec3& normal = normals[k];
            const glm::dvec3 binormal = glm::cross(tangents[k], normal);
            const double twist = totalLength > 0.0 ? twistDeg * kDeg2Rad * along[k] / totalLength : 0.0;
            Ring result;
            result.points.resize(static_cast<size_t>(sides));
            result.normals.resize(static_cast<size_t>(sides));
            for (int j = 0; j < sides; ++j)
            {
                const double a = 2.0 * kPi * j / sides + twist;
                const glm::dvec3 radial = std::cos(a) * normal + std::sin(a) * binormal;
                result.points[static_cast<size_t>(j)] = centre + radius * radial;
                result.normals[static_cast<size_t>(j)] = glm::normalize(radial + tangents[k] * lean);
            }
            return result;
        };

        // Rings from start to end; a round cap adds hemisphere rings at both ends.
        std::vector<Ring> ringList;
        const int capSteps = std::max(2, sides / 4);
        const size_t last = count - 1;
        if (cap == SweepCap::Round)
        {
            for (int q = capSteps - 1; q >= 1; --q)
            {
                const double phi = 0.5 * kPi * q / capSteps;
                ringList.push_back(ring(0, centres[0] - tangents[0] * (ringRadii[0] * std::sin(phi)),
                                        ringRadii[0] * std::cos(phi), -std::tan(phi)));
            }
        }
        for (size_t k = 0; k < count; ++k)
        {
            ringList.push_back(ring(k, centres[k], ringRadii[k], -slope[k]));
        }
        if (cap == SweepCap::Round)
        {
            for (int q = 1; q <= capSteps - 1; ++q)
            {
                const double phi = 0.5 * kPi * q / capSteps;
                ringList.push_back(ring(last, centres[last] + tangents[last] * (ringRadii[last] * std::sin(phi)),
                                        ringRadii[last] * std::cos(phi), std::tan(phi)));
            }
        }

        const auto nextSide = [sides](int j) { return static_cast<size_t>((j + 1) % sides); };
        for (size_t k = 0; k + 1 < ringList.size(); ++k)
        {
            const Ring& a = ringList[k];
            const Ring& b = ringList[k + 1];
            for (int j = 0; j < sides; ++j)
            {
                const size_t j0 = static_cast<size_t>(j);
                const size_t j1 = nextSide(j);
                out.insert(out.end(),
                           {a.points[j0], a.points[j1], b.points[j1], a.points[j0], b.points[j1], b.points[j0]});
                if (outNormals)
                {
                    outNormals->insert(outNormals->end(), {a.normals[j0], a.normals[j1], b.normals[j1], a.normals[j0],
                                                           b.normals[j1], b.normals[j0]});
                }
            }
        }
        if (cap != SweepCap::None)
        {
            // A flat cap is a fan around the end centre with one flat normal; a
            // round cap closes on the pole one radius beyond it.
            const bool round = cap == SweepCap::Round;
            const glm::dvec3 startPole = centres[0] - (round ? tangents[0] * ringRadii[0] : glm::dvec3(0.0));
            const glm::dvec3 endPole = centres[last] + (round ? tangents[last] * ringRadii[last] : glm::dvec3(0.0));
            const Ring& firstRing = ringList.front();
            const Ring& lastRing = ringList.back();
            const glm::dvec3 startNormal = -tangents[0];
            const glm::dvec3 endNormal = tangents[last];
            for (int j = 0; j < sides; ++j)
            {
                const size_t j0 = static_cast<size_t>(j);
                const size_t j1 = nextSide(j);
                out.insert(out.end(), {startPole, firstRing.points[j1], firstRing.points[j0]});
                out.insert(out.end(), {endPole, lastRing.points[j0], lastRing.points[j1]});
                if (outNormals)
                {
                    if (round)
                    {
                        outNormals->insert(outNormals->end(), {startNormal, firstRing.normals[j1], firstRing.normals[j0],
                                                               endNormal, lastRing.normals[j0], lastRing.normals[j1]});
                    }
                    else
                    {
                        outNormals->insert(outNormals->end(),
                                           {startNormal, startNormal, startNormal, endNormal, endNormal, endNormal});
                    }
                }
            }
        }
    }
} // namespace Assets::Scad
