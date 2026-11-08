#include "Engine/Common/CoreMinimal.hpp"

#include "Engine/Assets/Loaders/ProceduralTexture.hpp"

#include "Engine/Assets/GPU/Texture.hpp"
#include "Engine/Assets/GPU/TextureImage.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <stb_image_write.h>
#include <xxhash.h>

#include <array>
#include <cmath>
#include <mutex>
#include <unordered_map>

namespace Assets
{
    // Named (not anonymous) so unity builds cannot collide these helpers with
    // another translation unit's statics in the same batch.
    namespace ProceduralTextureDetail
    {
        constexpr std::array<std::string_view, 23> kPatterns = {
            // printed and woven cloth, leather
            "woodland", "digital", "desert", "flora", "rain", "canvas", "twill", "grain",
            // ground: terrain biomes and road surfaces
            "meadow", "dirt", "gravel", "asphalt", "rock",
            // building surfaces
            "plaster", "brick", "tiles", "planks", "stone", "concrete",
            // worn metal: their palettes are absolute colours (use a white color())
            "chipped", "rust", "worn",
            // varnished or oiled wood with continuous grain along +u (gun stocks, furniture)
            "woodgrain"};

        // Default palettes, linear RGB like SCAD color(). Camouflage defaults are the
        // historical prints (M81, EMR summer, DBDU six-color, VSR-98, Strichtarn);
        // fabric/leather defaults are white because those bakes only modulate the
        // material color they are multiplied with.
        std::vector<glm::vec3> DefaultPalette(std::string_view pattern)
        {
            if (pattern == "woodland")
                return {{0.36f, 0.35f, 0.23f}, {0.15f, 0.20f, 0.12f}, {0.24f, 0.17f, 0.11f}, {0.045f, 0.045f, 0.04f}};
            if (pattern == "digital")
                return {{0.34f, 0.36f, 0.29f}, {0.16f, 0.21f, 0.13f}, {0.21f, 0.16f, 0.11f}, {0.05f, 0.05f, 0.045f}};
            if (pattern == "desert")
                return {{0.60f, 0.50f, 0.38f}, {0.45f, 0.43f, 0.31f}, {0.40f, 0.29f, 0.19f},
                        {0.24f, 0.17f, 0.12f}, {0.05f, 0.05f, 0.05f}, {0.82f, 0.81f, 0.78f}};
            if (pattern == "flora")
                return {{0.31f, 0.34f, 0.27f}, {0.13f, 0.20f, 0.12f}, {0.23f, 0.18f, 0.12f}};
            if (pattern == "rain")
                return {{0.37f, 0.38f, 0.30f}, {0.25f, 0.19f, 0.13f}};
            // Ground and building prints modulate the surface colour they are
            // multiplied with; a second entry tints their accents (dry patches,
            // peeled render showing brick, moss, lichen).
            if (pattern == "meadow") return {{1.0f, 1.0f, 1.0f}, {1.0f, 0.93f, 0.72f}};
            if (pattern == "plaster") return {{1.0f, 1.0f, 1.0f}, {0.90f, 0.68f, 0.56f}};
            if (pattern == "tiles") return {{1.0f, 1.0f, 1.0f}, {0.74f, 0.90f, 0.62f}};
            if (pattern == "stone") return {{1.0f, 1.0f, 1.0f}, {0.90f, 0.95f, 0.76f}};
            // Worn metal prints carry the colours themselves, because paint and
            // the steel under it differ in metalness, not only in tint.
            if (pattern == "chipped") return {{0.21f, 0.23f, 0.14f}, {0.52f, 0.52f, 0.53f}};
            if (pattern == "rust") return {{0.50f, 0.50f, 0.51f}, {0.24f, 0.085f, 0.03f}, {0.09f, 0.04f, 0.02f}};
            if (pattern == "worn") return {{0.17f, 0.175f, 0.19f}, {0.55f, 0.55f, 0.56f}};
            return {{1.0f, 1.0f, 1.0f}};
        }

        uint32_t Mix32(uint32_t x)
        {
            x ^= x >> 16;
            x *= 0x7feb352dU;
            x ^= x >> 15;
            x *= 0x846ca68bU;
            x ^= x >> 16;
            return x;
        }

        float Rand01(uint32_t a, uint32_t b, uint32_t seed)
        {
            const uint32_t h = Mix32(a * 0x9E3779B1U ^ Mix32(b + 0x632BE5ABU) ^ Mix32(seed * 0x85EBCA77U + 0x27D4EB2FU));
            return static_cast<float>(h >> 8) / 16777216.0f;
        }

        int WrapIndex(int i, int n)
        {
            const int m = i % n;
            return m < 0 ? m + n : m;
        }

        float WrapDelta(float d)
        {
            return d - std::round(d);
        }

        // Periodic value noise on the unit torus: pu x pv lattice cells per tile.
        // Fewer cells along an axis stretches the features along it.
        float ValueNoise(float u, float v, int pu, int pv, uint32_t seed)
        {
            const float x = u * static_cast<float>(pu);
            const float y = v * static_cast<float>(pv);
            const float fx = std::floor(x);
            const float fy = std::floor(y);
            const int ix = static_cast<int>(fx);
            const int iy = static_cast<int>(fy);
            const float tx = (x - fx) * (x - fx) * (3.0f - 2.0f * (x - fx));
            const float ty = (y - fy) * (y - fy) * (3.0f - 2.0f * (y - fy));
            const auto at = [&](int dx, int dy)
            {
                return Rand01(static_cast<uint32_t>(WrapIndex(ix + dx, pu)), static_cast<uint32_t>(WrapIndex(iy + dy, pv)), seed);
            };
            return glm::mix(glm::mix(at(0, 0), at(1, 0), tx), glm::mix(at(0, 1), at(1, 1), tx), ty);
        }

        float Fbm(float u, float v, int pu, int pv, int octaves, uint32_t seed)
        {
            float sum = 0.0f;
            float amplitude = 0.5f;
            float norm = 0.0f;
            for (int octave = 0; octave < octaves; ++octave)
            {
                sum += amplitude * ValueNoise(u, v, pu << octave, pv << octave, seed + 1013U * static_cast<uint32_t>(octave));
                norm += amplitude;
                amplitude *= 0.5f;
            }
            return sum / norm;
        }

        // A scalar field over the tile plus the cut that leaves `fraction` of it above.
        struct FField
        {
            uint32_t size = 0;
            std::vector<float> values;

            template <class TFunc>
            static FField Sample(uint32_t size, TFunc&& func)
            {
                FField field;
                field.size = size;
                field.values.resize(static_cast<size_t>(size) * size);
                for (uint32_t y = 0; y < size; ++y)
                    for (uint32_t x = 0; x < size; ++x)
                        field.values[static_cast<size_t>(y) * size + x] =
                            func((static_cast<float>(x) + 0.5f) / static_cast<float>(size),
                                 (static_cast<float>(y) + 0.5f) / static_cast<float>(size));
                return field;
            }

            // Thresholds by rank, so coverage is exact whatever the noise statistics.
            float CutForTop(float fraction) const
            {
                std::vector<float> sorted = values;
                const size_t rank = std::min(sorted.size() - 1, static_cast<size_t>(
                    std::clamp(1.0f - fraction, 0.0f, 1.0f) * static_cast<float>(sorted.size())));
                std::nth_element(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(rank), sorted.end());
                return sorted[rank];
            }

            float At(uint32_t x, uint32_t y) const { return values[static_cast<size_t>(y) * size + x]; }
        };

        // A few flat tones instead of a continuous ramp: the prints stay
        // painterly next to flat-shaded low-poly facets rather than photographic.
        float Posterize(float v, int levels)
        {
            const float l = static_cast<float>(levels);
            return (std::floor(std::clamp(v, 0.0f, 0.9999f) * l) + 0.5f) / l;
        }

        // Nearest jittered feature point on a wrapping n x n grid (cellular noise).
        struct FCellHit
        {
            float dist = 1.0f;   // to the nearest point, in cell units
            float edge = 1.0f;   // second-nearest minus nearest: small near a border
            uint32_t id = 0;
        };
        FCellHit Cellular(float u, float v, int n, uint32_t seed)
        {
            const float x = u * static_cast<float>(n);
            const float y = v * static_cast<float>(n);
            const int ix = static_cast<int>(std::floor(x));
            const int iy = static_cast<int>(std::floor(y));
            float best = 1e9f;
            float second = 1e9f;
            uint32_t bestId = 0;
            for (int oy = -1; oy <= 1; ++oy)
                for (int ox = -1; ox <= 1; ++ox)
                {
                    const uint32_t cx = static_cast<uint32_t>(WrapIndex(ix + ox, n));
                    const uint32_t cy = static_cast<uint32_t>(WrapIndex(iy + oy, n));
                    const float px = static_cast<float>(ix + ox) + 0.15f + 0.7f * Rand01(cx, cy, seed);
                    const float py = static_cast<float>(iy + oy) + 0.15f + 0.7f * Rand01(cx, cy, seed + 7U);
                    const float d = std::hypot(x - px, y - py);
                    if (d < best) { second = best; best = d; bestId = cy * static_cast<uint32_t>(n) + cx; }
                    else if (d < second) second = d;
                }
            return {best, second - best, bestId};
        }

        // Courses of units (bricks, roof tiles, ashlar): which unit (row, column)
        // a point lies in and where inside it, with alternate rows shifted.
        struct FCourseHit
        {
            int row = 0;
            int column = 0;
            float fu = 0.0f; // 0..1 across the unit
            float fv = 0.0f; // 0..1 up the unit
        };
        FCourseHit Course(float u, float v, int rows, int columns, float shift, uint32_t seed, bool randomShift)
        {
            FCourseHit hit;
            const float y = v * static_cast<float>(rows);
            hit.row = WrapIndex(static_cast<int>(std::floor(y)), rows);
            hit.fv = y - std::floor(y);
            const float offset = randomShift ? Rand01(static_cast<uint32_t>(hit.row), 3U, seed)
                                             : static_cast<float>(hit.row & 1) * shift;
            const float x = u * static_cast<float>(columns) + offset;
            hit.column = WrapIndex(static_cast<int>(std::floor(x)), columns);
            hit.fu = x - std::floor(x);
            return hit;
        }

        // Plain (2/1) or twill (4-shaft) weave relief in [0, 1]: 1 on a thread crown.
        float Weave(float u, float v, float threads, bool twill)
        {
            const float tx = u * threads;
            const float ty = v * threads;
            const int ix = static_cast<int>(std::floor(tx));
            const int iy = static_cast<int>(std::floor(ty));
            const float fx = tx - std::floor(tx);
            const float fy = ty - std::floor(ty);
            const bool warpOver = twill ? WrapIndex(ix + iy, 4) < 2 : ((ix + iy) & 1) == 0;
            return std::sin(glm::pi<float>() * (warpOver ? fx : fy));
        }

        uint8_t EncodeSrgb(float linear)
        {
            const float c = std::clamp(linear, 0.0f, 1.0f);
            const float s = c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
            return static_cast<uint8_t>(std::lround(s * 255.0f));
        }

        std::mutex& AcquireMutex()
        {
            static std::mutex mutex;
            return mutex;
        }

        std::unordered_map<int32_t, uint64_t>& HashByTexture()
        {
            static std::unordered_map<int32_t, uint64_t> hashes;
            return hashes;
        }

        std::unordered_map<uint64_t, FProceduralTextureIds>& IdsByHash()
        {
            static std::unordered_map<uint64_t, FProceduralTextureIds> ids;
            return ids;
        }

        // One bake before encoding: colour plus the surface it describes.
        struct FSurface
        {
            uint32_t size = 0;
            std::vector<glm::vec3> color; // linear
            std::vector<float> height;    // relief, 0..1; only its slopes matter
            std::vector<float> rough;     // roughness factor, 1 = the material's roughness
            std::vector<float> metal;     // metalness factor, 1 = the material's metalness
        };

        // How strongly a pattern's relief tilts the normal: the slope (degrees)
        // reached by the steepest tenth of the tile, and the bevel blur in
        // texels. Moderate on purpose: grain inside a facet, never a new shape.
        struct FReliefStyle
        {
            float tilt90 = 0.0f;
            int blur = 1;
        };
        FReliefStyle ReliefStyle(std::string_view pattern)
        {
            if (pattern == "woodland" || pattern == "digital" || pattern == "desert" || pattern == "flora" ||
                pattern == "rain")
                return {8.0f, 0};
            if (pattern == "canvas") return {12.0f, 0};
            if (pattern == "twill") return {10.0f, 0};
            if (pattern == "grain") return {12.0f, 1};
            if (pattern == "meadow") return {8.0f, 2};
            if (pattern == "dirt") return {9.0f, 1};
            if (pattern == "gravel") return {11.0f, 1};
            if (pattern == "asphalt") return {5.0f, 1};
            if (pattern == "rock") return {15.0f, 2};
            if (pattern == "plaster") return {7.0f, 1};
            if (pattern == "brick") return {14.0f, 1};
            if (pattern == "tiles") return {12.0f, 1};
            if (pattern == "planks") return {10.0f, 1};
            if (pattern == "stone") return {13.0f, 2};
            if (pattern == "concrete") return {6.0f, 1};
            if (pattern == "chipped") return {12.0f, 1};
            if (pattern == "rust") return {11.0f, 1};
            if (pattern == "worn") return {7.0f, 1};
            if (pattern == "woodgrain") return {8.0f, 1};
            return {};
        }

        FSurface BakeSurface(const FProceduralTexture& texture, uint32_t size)
        {
            size = std::clamp(size, 16U, 2048U);
            const std::vector<glm::vec3> palette = ProceduralTexture::ResolvePalette(texture);
            const std::string_view pattern = texture.pattern;
            const uint32_t s = Mix32(texture.seed * 0x2545F491U + 0x9E37U);
            FSurface surface;
            surface.size = size;
            const size_t texels = static_cast<size_t>(size) * size;
            surface.color.assign(texels, palette[0]);
            surface.height.assign(texels, 0.5f);
            surface.rough.assign(texels, 1.0f);
            surface.metal.assign(texels, 1.0f);
            const auto pixelUV = [size](uint32_t x, uint32_t y)
            {
                return glm::vec2((static_cast<float>(x) + 0.5f) / static_cast<float>(size),
                                 (static_cast<float>(y) + 0.5f) / static_cast<float>(size));
            };
            const auto at = [size](uint32_t x, uint32_t y) { return static_cast<size_t>(y) * size + x; };
            const auto put = [&](uint32_t x, uint32_t y, const glm::vec3& c) { surface.color[at(x, y)] = c; };
            // Relief height in [0, 1] (only its slopes matter), roughness and
            // metalness factors in [0, 1] (1 = the material's own value).
            const auto shape = [&](uint32_t x, uint32_t y, float h, float r = 1.0f, float m = 1.0f)
            {
                const size_t i = at(x, y);
                surface.height[i] = h;
                surface.rough[i] = r;
                surface.metal[i] = m;
            };

            if (pattern == "woodland" || pattern == "digital")
            {
                // Two interlocking blotch layers (green, brown) over the light ground,
                // then the black branches. The digital print evaluates the same fields
                // on a pixel grid with a finer jittered sub-grid, giving the stepped,
                // two-scale edges of EMR/MARPAT-type prints.
                const bool digital = pattern == "digital";
                // Square camouflage pixels of 4 texels, with a 2-texel jitter grid for the ragged edges.
                const float cells = static_cast<float>(size / 4U);
                const int jitterCells = static_cast<int>(size / 2U);
                const auto domain = [&](float u, float v)
                {
                    if (digital)
                    {
                        u = (std::floor(u * cells) + 0.5f) / cells;
                        v = (std::floor(v * cells) + 0.5f) / cells;
                    }
                    const float wu = (Fbm(u, v, 4, 4, 3, s + 3U) - 0.5f) * 0.14f;
                    const float wv = (Fbm(u, v, 4, 4, 3, s + 7U) - 0.5f) * 0.14f;
                    return glm::vec2(u + wu, v + wv);
                };
                const auto jitter = [&](float u, float v)
                {
                    if (!digital) return 0.0f;
                    const int ju = WrapIndex(static_cast<int>(std::floor(u * static_cast<float>(jitterCells))), jitterCells);
                    const int jv = WrapIndex(static_cast<int>(std::floor(v * static_cast<float>(jitterCells))), jitterCells);
                    return (Rand01(static_cast<uint32_t>(ju), static_cast<uint32_t>(jv), s + 99U) - 0.5f) * 0.10f;
                };
                // Woodland blotches are broad and isotropic with thin black branches;
                // the Russian digital ("digital flora") leans vertical.
                const int gu = digital ? 6 : 3, gv = digital ? 3 : 3;
                const int bu = digital ? 7 : 3, bv = digital ? 3 : 4;
                const FField green = FField::Sample(size, [&](float u, float v)
                    { const glm::vec2 d = domain(u, v); return Fbm(d.x, d.y, gu, gv, 4, s + 11U) + jitter(u, v); });
                const FField brown = FField::Sample(size, [&](float u, float v)
                    { const glm::vec2 d = domain(u, v); return Fbm(d.x, d.y, bu, bv, 4, s + 23U) + jitter(u + 0.37f, v); });
                const FField black = FField::Sample(size, [&](float u, float v)
                {
                    const glm::vec2 d = domain(u, v);
                    // A band around the median of a warped field draws winding branches.
                    const float branch = 1.0f - std::abs(Fbm(d.x, d.y, 5, 5, 4, s + 31U) - 0.5f) * 4.0f;
                    return branch * 0.6f + Fbm(d.x, d.y, 4, 4, 3, s + 37U) * 0.4f + jitter(u, v + 0.61f);
                });
                const float greenCut = green.CutForTop(0.58f);
                const float brownCut = brown.CutForTop(digital ? 0.30f : 0.34f);
                const float blackCut = black.CutForTop(digital ? 0.10f : 0.09f);
                for (uint32_t y = 0; y < size; ++y)
                    for (uint32_t x = 0; x < size; ++x)
                    {
                        glm::vec3 c = palette[0];
                        if (green.At(x, y) > greenCut) c = palette[1];
                        if (brown.At(x, y) > brownCut) c = palette[2];
                        if (black.At(x, y) > blackCut) c = palette[3];
                        put(x, y, c);
                    }
            }
            else if (pattern == "desert")
            {
                // DBDU six-color: tan ground, wavy khaki and brown bands with dark
                // brown cores, and the black "chocolate chip" rocks with a white chip
                // cast beside each one.
                const FField khaki = FField::Sample(size, [&](float u, float v)
                    { return Fbm(u + (Fbm(u, v, 3, 3, 2, s + 5U) - 0.5f) * 0.2f, v, 2, 3, 4, s + 13U); });
                const FField brown = FField::Sample(size, [&](float u, float v)
                    { return Fbm(u, v + (Fbm(u, v, 3, 3, 2, s + 9U) - 0.5f) * 0.2f, 3, 2, 4, s + 17U); });
                const float khakiCut = khaki.CutForTop(0.32f);
                const float brownCut = brown.CutForTop(0.30f);
                const float coreCut = brown.CutForTop(0.09f);
                for (uint32_t y = 0; y < size; ++y)
                    for (uint32_t x = 0; x < size; ++x)
                    {
                        glm::vec3 c = palette[0];
                        if (khaki.At(x, y) > khakiCut) c = palette[1];
                        if (brown.At(x, y) > brownCut) c = palette[2];
                        if (brown.At(x, y) > coreCut) c = palette[3];
                        put(x, y, c);
                    }
                constexpr int rockCells = 9;
                for (int cy = 0; cy < rockCells; ++cy)
                    for (int cx = 0; cx < rockCells; ++cx)
                    {
                        const uint32_t cell = static_cast<uint32_t>(cy * rockCells + cx);
                        if (Rand01(cell, 1U, s + 41U) > 0.62f) continue;
                        const glm::vec2 center((static_cast<float>(cx) + Rand01(cell, 2U, s + 41U)) / rockCells,
                                               (static_cast<float>(cy) + Rand01(cell, 3U, s + 41U)) / rockCells);
                        const float radius = 0.010f + 0.010f * Rand01(cell, 4U, s + 41U);
                        const glm::vec2 whiteOffset(0.014f, -0.009f);
                        const int reach = static_cast<int>(std::ceil((radius * 1.6f + 0.02f) * static_cast<float>(size)));
                        const int px = static_cast<int>(center.x * static_cast<float>(size));
                        const int py = static_cast<int>(center.y * static_cast<float>(size));
                        for (int dy = -reach; dy <= reach; ++dy)
                            for (int dx = -reach; dx <= reach; ++dx)
                            {
                                const uint32_t x = static_cast<uint32_t>(WrapIndex(px + dx, static_cast<int>(size)));
                                const uint32_t y = static_cast<uint32_t>(WrapIndex(py + dy, static_cast<int>(size)));
                                const glm::vec2 uv = pixelUV(x, y);
                                const auto lumpy = [&](const glm::vec2& c0, float r)
                                {
                                    const glm::vec2 d(WrapDelta(uv.x - c0.x), WrapDelta(uv.y - c0.y));
                                    const float wobble = 0.75f + 0.5f * ValueNoise(uv.x, uv.y, 64, 64, s + cell);
                                    return glm::length(d * glm::vec2(1.0f, 1.25f)) < r * wobble;
                                };
                                if (lumpy(center, radius)) put(x, y, palette[4]);
                                else if (lumpy(center + whiteOffset, radius * 0.85f)) put(x, y, palette[5]);
                            }
                    }
            }
            else if (pattern == "flora")
            {
                // VSR-98: wavy vertical green stripes ("watermelon") with brown flecks
                // on a pale green-grey ground. Few lattice cells along v stretch the
                // features vertically.
                const FField green = FField::Sample(size, [&](float u, float v)
                    { return Fbm(u + (Fbm(u, v, 2, 6, 2, s + 3U) - 0.5f) * 0.10f, v, 8, 2, 4, s + 11U); });
                const FField brown = FField::Sample(size, [&](float u, float v)
                    { return Fbm(u + (Fbm(u, v, 2, 6, 2, s + 5U) - 0.5f) * 0.10f, v, 10, 3, 4, s + 19U); });
                const float greenCut = green.CutForTop(0.42f);
                const float brownCut = brown.CutForTop(0.17f);
                for (uint32_t y = 0; y < size; ++y)
                    for (uint32_t x = 0; x < size; ++x)
                    {
                        glm::vec3 c = palette[0];
                        if (green.At(x, y) > greenCut) c = palette[1];
                        if (brown.At(x, y) > brownCut) c = palette[2];
                        put(x, y, c);
                    }
            }
            else if (pattern == "rain")
            {
                // Strichtarn: a faintly mottled ground crossed by short vertical rain
                // strokes laid out in columns ("one stroke, no stroke").
                constexpr int columns = 46;
                struct FDash { float begin; float end; };
                std::vector<std::vector<FDash>> dashes(columns);
                std::vector<float> phase(columns);
                for (int c = 0; c < columns; ++c)
                {
                    const uint32_t col = static_cast<uint32_t>(c);
                    phase[static_cast<size_t>(c)] = (Rand01(col, 0U, s + 51U) - 0.5f) * 0.5f;
                    float v = Rand01(col, 1U, s + 51U) * 0.05f;
                    for (uint32_t k = 2; v < 1.0f; k += 3)
                    {
                        const float length = 0.012f + 0.040f * Rand01(col, k, s + 51U);
                        if (Rand01(col, k + 1U, s + 51U) < 0.72f)
                            dashes[static_cast<size_t>(c)].push_back({v, v + length});
                        v += length + 0.010f + 0.045f * Rand01(col, k + 2U, s + 51U);
                    }
                }
                for (uint32_t y = 0; y < size; ++y)
                    for (uint32_t x = 0; x < size; ++x)
                    {
                        const glm::vec2 uv = pixelUV(x, y);
                        const float mottle = 0.95f + 0.10f * Fbm(uv.x, uv.y, 6, 6, 3, s + 61U);
                        glm::vec3 c = palette[0] * mottle;
                        const float column = uv.x * columns;
                        const int index = WrapIndex(static_cast<int>(std::floor(column)), columns);
                        const float local = column - std::floor(column) - 0.5f - phase[static_cast<size_t>(index)];
                        if (std::abs(local) < 0.21f)
                        {
                            for (const FDash& dash : dashes[static_cast<size_t>(index)])
                            {
                                if ((uv.y >= dash.begin && uv.y < dash.end) || (uv.y + 1.0f >= dash.begin && uv.y + 1.0f < dash.end))
                                {
                                    c = palette[1];
                                    break;
                                }
                            }
                        }
                        put(x, y, c);
                    }
            }
            else if (pattern == "canvas" || pattern == "twill")
            {
                // Woven cloth: thread crowns catch light, the gaps fall dark, and a
                // slow slub variation keeps a large panel from looking printed.
                // The relief is the same weave, so crowns and shading agree.
                const bool twill = pattern == "twill";
                for (uint32_t y = 0; y < size; ++y)
                    for (uint32_t x = 0; x < size; ++x)
                    {
                        const glm::vec2 uv = pixelUV(x, y);
                        const float relief = Weave(uv.x, uv.y, twill ? 64.0f : 48.0f, twill);
                        const float slub = 0.93f + 0.14f * Fbm(uv.x, uv.y, 12, 12, 3, s + 71U);
                        put(x, y, palette[0] * ((0.80f + 0.20f * relief) * slub));
                        shape(x, y, relief * 0.8f + slub * 0.6f);
                    }
            }
            else if (pattern == "grain")
            {
                // Leather: soft mottling with scattered pores, and a network of
                // creases. Crowns between creases are worn smoother (glossier).
                for (uint32_t y = 0; y < size; ++y)
                    for (uint32_t x = 0; x < size; ++x)
                    {
                        const glm::vec2 uv = pixelUV(x, y);
                        const float mottle = Fbm(uv.x, uv.y, 20, 20, 3, s + 81U);
                        float shade = 0.86f + 0.20f * mottle;
                        const bool pore = Rand01(x, y, s + 83U) > 0.94f;
                        if (pore) shade -= 0.07f;
                        const FCellHit crease = Cellular(uv.x, uv.y, 18, s + 85U);
                        const float fold = std::clamp(crease.edge / 0.10f, 0.0f, 1.0f);
                        put(x, y, palette[0] * (shade * (0.94f + 0.06f * fold)));
                        shape(x, y, 0.35f + 0.45f * fold + 0.2f * mottle, pore ? 1.0f : 0.70f + 0.30f * (1.0f - fold));
                    }
            }

            else if (pattern == "meadow")
            {
                // Grass ground seen from walking height: broad posterized mottling,
                // darker clumps, and sun-dried patches tinted by palette[1]. The
                // relief is tussocks: posterized domes, terraced like the facets.
                for (uint32_t y = 0; y < size; ++y)
                    for (uint32_t x = 0; x < size; ++x)
                    {
                        const glm::vec2 uv = pixelUV(x, y);
                        const float mottle = Posterize(Fbm(uv.x, uv.y, 4, 4, 4, s + 91U), 4);
                        float shade = 0.84f + 0.20f * mottle;
                        if (Fbm(uv.x, uv.y, 20, 20, 2, s + 93U) > 0.64f) shade *= 0.90f;
                        const bool dry = Fbm(uv.x, uv.y, 3, 3, 3, s + 95U) > 0.66f;
                        put(x, y, (dry ? palette[1] : palette[0]) * std::min(shade, 1.0f));
                        const FCellHit tuft = Cellular(uv.x, uv.y, 24, s + 97U);
                        shape(x, y, 0.4f * mottle + 0.6f * Posterize(std::max(0.0f, 1.0f - tuft.dist / 0.65f), 3));
                    }
            }
            else if (pattern == "dirt" || pattern == "rock")
            {
                // Earth (clods, pebbles) or rock (horizontal strata on cliff faces,
                // whose side projection keeps v vertical, plus cracks). Rock strata
                // are stepped in the relief too: ledges, not a smooth ramp.
                const bool rock = pattern == "rock";
                for (uint32_t y = 0; y < size; ++y)
                    for (uint32_t x = 0; x < size; ++x)
                    {
                        const glm::vec2 uv = pixelUV(x, y);
                        const float layer = rock ? Posterize(Fbm(uv.x, uv.y, 2, 9, 4, s + 101U), 4)
                                                 : Posterize(Fbm(uv.x, uv.y, 6, 6, 4, s + 103U), 3);
                        float shade = rock ? 0.80f + 0.22f * layer : 0.84f + 0.18f * layer;
                        const FCellHit grain = Cellular(uv.x, uv.y, rock ? 10 : 40, s + 105U);
                        const bool crack = rock && grain.edge < 0.06f;
                        if (crack) shade *= 0.72f;
                        if (!rock && grain.dist < 0.22f) shade *= Rand01(grain.id, 1U, s) > 0.5f ? 0.86f : 1.06f;
                        put(x, y, palette[0] * std::min(shade, 1.0f));
                        if (rock)
                            shape(x, y, crack ? layer * 0.5f : layer);
                        else
                            shape(x, y, 0.45f * layer + 0.55f * std::max(0.0f, 1.0f - grain.dist * grain.dist * 1.6f));
                    }
            }
            else if (pattern == "gravel")
            {
                // Packed pebbles: cellular stones of a few tones with dark gaps;
                // each stone a dome, a little smoother than the grit between.
                for (uint32_t y = 0; y < size; ++y)
                    for (uint32_t x = 0; x < size; ++x)
                    {
                        const glm::vec2 uv = pixelUV(x, y);
                        const FCellHit stone = Cellular(uv.x, uv.y, 36, s + 111U);
                        float shade = 0.80f + 0.20f * Posterize(Rand01(stone.id, 2U, s + 111U), 3);
                        const bool gap = stone.edge < 0.10f;
                        if (gap) shade = 0.70f;
                        put(x, y, palette[0] * shade);
                        const float dome = std::sqrt(std::clamp(1.0f - stone.dist * stone.dist * 1.6f, 0.0f, 1.0f));
                        shape(x, y, gap ? 0.1f : 0.35f + 0.65f * dome, gap ? 1.0f : 0.85f);
                    }
            }
            else if (pattern == "asphalt")
            {
                // Old country asphalt: fine aggregate speckle, darker repair
                // patches (raised, smoother) and a few hairline cracks.
                const FField patch = FField::Sample(size, [&](float u, float v) { return Fbm(u, v, 3, 3, 3, s + 121U); });
                const float patchCut = patch.CutForTop(0.14f);
                for (uint32_t y = 0; y < size; ++y)
                    for (uint32_t x = 0; x < size; ++x)
                    {
                        const glm::vec2 uv = pixelUV(x, y);
                        const float swell = Posterize(Fbm(uv.x, uv.y, 8, 8, 2, s + 123U), 2);
                        float shade = 0.92f + 0.08f * swell;
                        const float speck = Rand01(x, y, s + 125U);
                        if (speck > 0.90f) shade *= 0.88f;
                        else if (speck < 0.04f) shade = std::min(1.0f, shade * 1.08f);
                        const bool patched = patch.At(x, y) > patchCut;
                        if (patched) shade *= 0.84f;
                        const bool crack = std::abs(Fbm(uv.x, uv.y, 5, 5, 3, s + 127U) - 0.5f) < 0.006f;
                        if (crack) shade *= 0.70f;
                        put(x, y, palette[0] * shade);
                        shape(x, y, crack ? 0.1f : (patched ? 0.75f : 0.5f) + 0.1f * swell, patched ? 0.9f : 1.0f);
                    }
            }
            else if (pattern == "plaster")
            {
                // Lime render on a decaying house: soft blotches, vertical rain
                // streaks, and spalled patches where the brick shows (palette[1]),
                // sunk below the render with a raised broken rim.
                const FField peel = FField::Sample(size, [&](float u, float v) { return Fbm(u, v, 5, 5, 4, s + 131U); });
                const float peelCut = peel.CutForTop(0.035f);
                const float rimCut = peel.CutForTop(0.055f);
                for (uint32_t y = 0; y < size; ++y)
                    for (uint32_t x = 0; x < size; ++x)
                    {
                        const glm::vec2 uv = pixelUV(x, y);
                        const float blotch = Posterize(Fbm(uv.x, uv.y, 3, 3, 3, s + 133U), 3);
                        float shade = 0.90f + 0.10f * blotch;
                        if (Fbm(uv.x, uv.y, 14, 2, 3, s + 135U) > 0.62f) shade *= 0.92f;
                        glm::vec3 c = palette[0] * shade;
                        float h = 0.75f + 0.1f * blotch;
                        if (peel.At(x, y) > peelCut)
                        {
                            const float chip = Rand01(x / 6U, y / 3U, s + 137U);
                            c = palette[1] * (0.90f + 0.10f * chip);
                            h = 0.2f + 0.1f * chip;
                        }
                        else if (peel.At(x, y) > rimCut)
                        {
                            c = palette[0];
                            h = 0.95f;
                        }
                        put(x, y, c);
                        shape(x, y, h);
                    }
            }
            else if (pattern == "brick" || pattern == "tiles" || pattern == "stone")
            {
                // Coursed units with dark joints: bricks in half-bond, roof tiles
                // with a shadowed lower lip and moss, rubble-coursed stone with
                // uneven rows and lichen. Each unit takes one of a few tones.
                // Relief: bricks proud of recessed joints; each tile course a
                // wedge stepping up at its lip; stones cushioned (pillowed) with
                // crowns worn smoother than the joints.
                const bool brick = pattern == "brick";
                const bool tiles = pattern == "tiles";
                const int rows = brick ? 16 : tiles ? 10 : 8;
                const int columns = brick ? 5 : tiles ? 14 : 4;
                const FField growth = FField::Sample(size, [&](float u, float v) { return Fbm(u, v, 4, 4, 4, s + 141U); });
                const float growthCut = growth.CutForTop(tiles ? 0.12f : 0.06f);
                // Unit proportions, to measure the distance to a joint in one unit.
                const float aspect = static_cast<float>(rows) / static_cast<float>(columns);
                for (uint32_t y = 0; y < size; ++y)
                    for (uint32_t x = 0; x < size; ++x)
                    {
                        const glm::vec2 uv = pixelUV(x, y);
                        // Phase by a fraction of a unit: joints stay off the tile edge,
                        // where the repeat would otherwise line one up along every row.
                        const FCourseHit unit = Course(uv.x + 0.25f / static_cast<float>(columns),
                                                       uv.y + 0.5f / static_cast<float>(rows), rows, columns, 0.5f,
                                                       s + 143U, !brick && !tiles);
                        const uint32_t unitId = static_cast<uint32_t>(unit.row * 64 + unit.column);
                        const float tone = Rand01(unitId, 5U, s + 145U);
                        float shade = 0.80f + 0.20f * Posterize(tone, brick ? 4 : 3);
                        const float jointU = brick ? 0.06f : tiles ? 0.05f : 0.05f + 0.04f * Rand01(unitId, 6U, s);
                        const float jointV = brick ? 0.16f : tiles ? 0.0f : 0.09f;
                        const bool joint = unit.fu < jointU || unit.fv < jointV;
                        if (joint) shade = brick ? 0.62f : 0.58f;
                        if (tiles && unit.fv < 0.18f) shade *= 0.74f;   // lip shadow of the course above
                        glm::vec3 c = palette[0] * shade;
                        const bool grown = !brick && growth.At(x, y) > growthCut;
                        if (grown) c *= palette[1];
                        put(x, y, c);

                        float h = 0.0f;
                        float r = 1.0f;
                        if (brick)
                        {
                            h = joint ? 0.1f : 0.8f + 0.2f * tone;
                        }
                        else if (tiles)
                        {
                            // Up the roof (+v) each tile sinks toward its head, under
                            // the next course; the step at fv = 0 is the lip.
                            h = unit.fu < jointU ? 0.35f : 1.0f - 0.75f * unit.fv;
                            r = grown ? 1.0f : 0.8f;
                        }
                        else
                        {
                            // Distance to the nearest joint in unit-heights, so the
                            // cushion is round on long stones as on short ones.
                            const float du = std::min(unit.fu - jointU, 1.0f - unit.fu) * aspect;
                            const float dv = std::min(unit.fv - jointV, 1.0f - unit.fv);
                            const float edge = std::max(0.0f, std::min(du, dv));
                            const float cushion = joint ? 0.0f : std::sqrt(std::min(1.0f, edge / 0.22f));
                            h = 0.1f + 0.9f * cushion * (0.85f + 0.15f * tone);
                            r = joint || grown ? 1.0f : 0.95f - 0.2f * cushion;
                        }
                        shape(x, y, h, r);
                    }
            }
            else if (pattern == "planks")
            {
                // Vertical boards: one tone per board, dark gaps, grain streaks
                // along the board and the odd knot. Boards cup a little.
                constexpr int boards = 8;
                for (uint32_t y = 0; y < size; ++y)
                    for (uint32_t x = 0; x < size; ++x)
                    {
                        const glm::vec2 uv = pixelUV(x, y);
                        const float bx = uv.x * boards + 0.5f; // gaps off the tile edge
                        const int board = WrapIndex(static_cast<int>(std::floor(bx)), boards);
                        const float fu = bx - std::floor(bx);
                        const float tone = Rand01(static_cast<uint32_t>(board), 7U, s + 151U);
                        float shade = 0.80f + 0.18f * Posterize(tone, 3);
                        const bool streak = Fbm(uv.x, uv.y, 48, 3, 2, s + 153U) > 0.6f;
                        if (streak) shade *= 0.93f;
                        const bool gap = fu < 0.07f;
                        if (gap) shade = 0.55f;
                        const FCellHit knot = Cellular(uv.x, uv.y, 6, s + 155U);
                        const bool knotted = knot.dist < 0.06f && Rand01(knot.id, 8U, s) > 0.6f;
                        if (knotted) shade *= 0.75f;
                        put(x, y, palette[0] * shade);
                        const float across = (fu - 0.535f) / 0.465f;
                        shape(x, y, gap ? 0.05f : 0.8f + 0.1f * tone - 0.25f * across * across - (streak ? 0.06f : 0.0f) -
                                                      (knotted ? 0.2f : 0.0f));
                    }
            }
            else if (pattern == "concrete")
            {
                // Cast concrete: mottling, faint formwork lines, run-off stains,
                // and the small blowholes of an unfinished pour.
                for (uint32_t y = 0; y < size; ++y)
                    for (uint32_t x = 0; x < size; ++x)
                    {
                        const glm::vec2 uv = pixelUV(x, y);
                        const float mottle = Posterize(Fbm(uv.x, uv.y, 5, 5, 3, s + 161U), 3);
                        float shade = 0.88f + 0.12f * mottle;
                        const float course = uv.y * 4.0f + 0.5f - std::floor(uv.y * 4.0f + 0.5f);
                        const bool line = course < 0.02f;
                        if (line) shade *= 0.86f;
                        if (Fbm(uv.x, uv.y, 16, 2, 3, s + 163U) > 0.64f) shade *= 0.90f;
                        const FCellHit pit = Cellular(uv.x, uv.y, 20, s + 165U);
                        const bool hole = pit.dist < 0.10f && Rand01(pit.id, 9U, s) > 0.55f;
                        if (hole) shade *= 0.85f;
                        put(x, y, palette[0] * shade);
                        shape(x, y, line ? 0.25f : hole ? 0.3f : 0.7f + 0.1f * mottle);
                    }
            }
            else if (pattern == "chipped")
            {
                // Painted steel, worn: the paint (palette[0], dielectric, matte)
                // flakes off in chips that show bright metal (palette[1], fully
                // metallic and glossier), each chip sunk by the paint's thickness
                // with a scuffed lighter ring of thinned paint around it.
                const FField wear = FField::Sample(size, [&](float u, float v)
                    { return Fbm(u, v, 6, 6, 4, s + 171U) * 0.7f + Fbm(u, v, 24, 24, 2, s + 173U) * 0.3f; });
                const float chipCut = wear.CutForTop(0.055f);
                const float scuffCut = wear.CutForTop(0.095f);
                for (uint32_t y = 0; y < size; ++y)
                    for (uint32_t x = 0; x < size; ++x)
                    {
                        const glm::vec2 uv = pixelUV(x, y);
                        const float w = wear.At(x, y);
                        const float mottle = Fbm(uv.x, uv.y, 8, 8, 2, s + 175U);
                        if (w > chipCut)
                        {
                            put(x, y, palette[1] * (0.9f + 0.1f * mottle));
                            shape(x, y, 0.1f, 0.45f, 1.0f);
                        }
                        else if (w > scuffCut)
                        {
                            put(x, y, glm::mix(palette[0], glm::vec3(1.0f), 0.12f));
                            shape(x, y, 0.75f, 0.9f, 0.0f);
                        }
                        else
                        {
                            put(x, y, palette[0] * (0.94f + 0.06f * Posterize(mottle, 3)));
                            shape(x, y, 0.9f, 1.0f, 0.0f);
                        }
                    }
            }
            else if (pattern == "rust")
            {
                // Bare steel (palette[0], metallic, fairly smooth) blooming into
                // rust (palette[1], dielectric, rough, pitted and raised) with
                // dark scale at the cores (palette[2]) and run-off streaks below.
                const FField bloom = FField::Sample(size, [&](float u, float v)
                    { return Fbm(u, v, 5, 5, 4, s + 181U) * 0.8f + Fbm(u, v, 20, 20, 2, s + 183U) * 0.2f; });
                const float rustCut = bloom.CutForTop(0.40f);
                const float scaleCut = bloom.CutForTop(0.12f);
                for (uint32_t y = 0; y < size; ++y)
                    for (uint32_t x = 0; x < size; ++x)
                    {
                        const glm::vec2 uv = pixelUV(x, y);
                        const float b = bloom.At(x, y);
                        const float pits = Fbm(uv.x, uv.y, 24, 24, 2, s + 185U);
                        if (b > scaleCut)
                        {
                            put(x, y, palette[2] * (0.9f + 0.2f * pits));
                            shape(x, y, 0.55f + 0.35f * Posterize(pits, 3), 1.0f, 0.0f);
                        }
                        else if (b > rustCut)
                        {
                            put(x, y, palette[1] * (0.88f + 0.2f * Posterize(pits, 3)));
                            shape(x, y, 0.45f + 0.3f * Posterize(pits, 3), 1.0f, 0.0f);
                        }
                        else
                        {
                            const bool streak = Fbm(uv.x, uv.y, 14, 2, 3, s + 187U) > 0.62f;
                            const float brushed = Fbm(uv.x, uv.y, 2, 32, 2, s + 189U);
                            put(x, y, streak ? glm::mix(palette[0], palette[1], 0.45f) : palette[0] * (0.92f + 0.08f * brushed));
                            shape(x, y, 0.3f, streak ? 0.85f : 0.45f + 0.15f * brushed, streak ? 0.4f : 1.0f);
                        }
                    }
            }

            else if (pattern == "worn")
            {
                // A metal finish (bluing, parkerizing, anodizing; palette[0])
                // rubbed through to bright steel (palette[1]) in patches, with
                // fine handling scratches along +u. All of it stays metal; the
                // rubbed steel is the glossiest part.
                const FField wear = FField::Sample(size, [&](float u, float v)
                    { return Fbm(u, v, 10, 10, 3, s + 191U) * 0.6f + Fbm(u, v, 32, 32, 2, s + 193U) * 0.4f; });
                // Sparse: handling wear is a few small rubs, not a pattern.
                const float rubCut = wear.CutForTop(0.04f);
                for (uint32_t y = 0; y < size; ++y)
                    for (uint32_t x = 0; x < size; ++x)
                    {
                        const glm::vec2 uv = pixelUV(x, y);
                        const float mottle = Fbm(uv.x, uv.y, 8, 8, 2, s + 195U);
                        const bool scratch = std::abs(Fbm(uv.x, uv.y, 2, 40, 2, s + 197U) - 0.5f) < 0.012f;
                        if (wear.At(x, y) > rubCut)
                        {
                            put(x, y, glm::mix(palette[0], palette[1], 0.75f + 0.25f * mottle));
                            shape(x, y, 0.35f, 0.5f, 1.0f);
                        }
                        else if (scratch)
                        {
                            put(x, y, glm::mix(palette[0], palette[1], 0.3f));
                            shape(x, y, 0.3f, 0.7f, 1.0f);
                        }
                        else
                        {
                            put(x, y, palette[0] * (0.95f + 0.05f * Posterize(mottle, 3)));
                            shape(x, y, 0.5f + 0.05f * mottle, 1.0f, 1.0f);
                        }
                    }
            }
            else if (pattern == "woodgrain")
            {
                // Continuous wood grain running along +u: warped annual-ring
                // bands in a few flat tones, darker open pores in the late wood.
                // Varnish fills the early wood (smoother); pores stay rough.
                for (uint32_t y = 0; y < size; ++y)
                    for (uint32_t x = 0; x < size; ++x)
                    {
                        const glm::vec2 uv = pixelUV(x, y);
                        const float warp = (Fbm(uv.x, uv.y, 2, 4, 3, s + 201U) - 0.5f) * 0.12f;
                        const float rings = (uv.y + warp) * 14.0f;
                        const float ring = rings - std::floor(rings);
                        const float late = std::clamp((ring - 0.62f) / 0.3f, 0.0f, 1.0f);
                        const bool pore = late > 0.2f && Rand01(x / 3U, y, s + 203U) > 0.82f;
                        float shade = 0.88f + 0.12f * Posterize(Fbm(uv.x, uv.y, 3, 6, 2, s + 205U), 3);
                        shade *= 1.0f - 0.16f * Posterize(late, 3);
                        if (pore) shade *= 0.85f;
                        put(x, y, palette[0] * shade);
                        shape(x, y, pore ? 0.2f : 0.7f - 0.3f * late, pore ? 1.0f : 0.75f + 0.25f * late);
                    }
            }

            // Camouflage is printed on twill: a light weave relief under the print.
            const bool printedCloth = pattern == "woodland" || pattern == "digital" || pattern == "desert" ||
                                      pattern == "flora" || pattern == "rain";
            if (printedCloth)
            {
                for (uint32_t y = 0; y < size; ++y)
                    for (uint32_t x = 0; x < size; ++x)
                    {
                        const glm::vec2 uv = pixelUV(x, y);
                        const float weave = Weave(uv.x, uv.y, 64.0f, true);
                        surface.color[at(x, y)] *= 0.92f + 0.08f * weave;
                        surface.height[at(x, y)] = weave;
                    }
            }
            return surface;
        }

        std::vector<uint8_t> EncodeAlbedo(const FSurface& surface)
        {
            std::vector<uint8_t> rgba(surface.color.size() * 4);
            for (size_t i = 0; i < surface.color.size(); ++i)
            {
                const glm::vec3& c = surface.color[i];
                rgba[i * 4 + 0] = EncodeSrgb(c.r);
                rgba[i * 4 + 1] = EncodeSrgb(c.g);
                rgba[i * 4 + 2] = EncodeSrgb(c.b);
                rgba[i * 4 + 3] = 255;
            }
            return rgba;
        }

        uint8_t EncodeUnit(float v)
        {
            return static_cast<uint8_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f));
        }

        // Wrapping separable box blur: softens the height's hard steps into
        // narrow bevels, so the normal map draws a lit edge and a shaded edge
        // rather than a one-texel line that would shimmer without mips.
        std::vector<float> BlurWrapped(const std::vector<float>& values, uint32_t size, int radius)
        {
            if (radius <= 0) return values;
            const int n = static_cast<int>(size);
            const float norm = 1.0f / static_cast<float>(2 * radius + 1);
            std::vector<float> pass(values.size());
            std::vector<float> out(values.size());
            for (int y = 0; y < n; ++y)
                for (int x = 0; x < n; ++x)
                {
                    float sum = 0.0f;
                    for (int k = -radius; k <= radius; ++k) sum += values[static_cast<size_t>(y * n + WrapIndex(x + k, n))];
                    pass[static_cast<size_t>(y * n + x)] = sum * norm;
                }
            for (int y = 0; y < n; ++y)
                for (int x = 0; x < n; ++x)
                {
                    float sum = 0.0f;
                    for (int k = -radius; k <= radius; ++k) sum += pass[static_cast<size_t>(WrapIndex(y + k, n) * n + x)];
                    out[static_cast<size_t>(y * n + x)] = sum * norm;
                }
            return out;
        }

        // Tangent-space normals from the relief: central differences (wrapping)
        // scaled so the 90th-percentile slope is the pattern's tilt. Returns
        // false when the relief is flat or switched off.
        bool EncodeNormal(const FSurface& surface, std::string_view pattern, float relief, std::vector<uint8_t>& out)
        {
            const FReliefStyle style = ReliefStyle(pattern);
            relief = std::clamp(relief, 0.0f, 3.0f);
            if (relief <= 0.0f || style.tilt90 <= 0.0f) return false;
            const uint32_t size = surface.size;
            const int n = static_cast<int>(size);
            const std::vector<float> h = BlurWrapped(surface.height, size, style.blur);
            std::vector<glm::vec2> gradient(h.size());
            std::vector<float> magnitude(h.size());
            for (int y = 0; y < n; ++y)
                for (int x = 0; x < n; ++x)
                {
                    const auto H = [&](int px, int py) { return h[static_cast<size_t>(WrapIndex(py, n) * n + WrapIndex(px, n))]; };
                    const glm::vec2 g(H(x + 1, y) - H(x - 1, y), H(x, y + 1) - H(x, y - 1));
                    gradient[static_cast<size_t>(y * n + x)] = g;
                    magnitude[static_cast<size_t>(y * n + x)] = glm::length(g);
                }
            std::vector<float> sorted = magnitude;
            const size_t rank = sorted.size() * 9 / 10;
            std::nth_element(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(rank), sorted.end());
            const float p90 = sorted[rank];
            if (p90 < 1e-5f) return false;
            const float k = std::tan(glm::radians(style.tilt90)) * relief / p90;
            const float maxSlope = std::tan(glm::radians(40.0f));
            out.assign(h.size() * 4, 255);
            for (size_t i = 0; i < h.size(); ++i)
            {
                glm::vec2 slope = gradient[i] * k;
                const float len = glm::length(slope);
                if (len > maxSlope) slope *= maxSlope / len;
                // Height rising along +u tilts the normal toward -u.
                const glm::vec3 normal = glm::normalize(glm::vec3(-slope.x, -slope.y, 1.0f));
                out[i * 4 + 0] = EncodeUnit(normal.x * 0.5f + 0.5f);
                out[i * 4 + 1] = EncodeUnit(normal.y * 0.5f + 0.5f);
                out[i * 4 + 2] = EncodeUnit(normal.z * 0.5f + 0.5f);
            }
            return true;
        }

        // glTF MRA: R occlusion (unused, 1), G roughness factor, B metalness
        // factor. Returns false when both are 1 everywhere: no map needed.
        bool EncodeMra(const FSurface& surface, std::vector<uint8_t>& out)
        {
            bool varies = false;
            out.assign(surface.rough.size() * 4, 255);
            for (size_t i = 0; i < surface.rough.size(); ++i)
            {
                out[i * 4 + 1] = EncodeUnit(surface.rough[i]);
                out[i * 4 + 2] = EncodeUnit(surface.metal[i]);
                varies = varies || out[i * 4 + 1] < 254 || out[i * 4 + 2] < 254;
            }
            return varies;
        }

        std::vector<uint8_t> EncodePng(const std::vector<uint8_t>& pixels, uint32_t size)
        {
            std::vector<uint8_t> png;
            stbi_write_png_to_func(
                [](void* context, void* data, int bytes)
                {
                    auto* out = static_cast<std::vector<uint8_t>*>(context);
                    const auto* begin = static_cast<const uint8_t*>(data);
                    out->insert(out->end(), begin, begin + bytes);
                },
                &png, static_cast<int>(size), static_cast<int>(size), 4, pixels.data(), static_cast<int>(size * 4));
            return png;
        }

        // Registers one bake, or finds it. sRGB for colour only: the normal and
        // MRA maps hold linear data. The name carries a hash of the pixels:
        // the off-thread upload path keeps a compressed copy on disk keyed by
        // name, so a bake that changes with the code (not with its parameters)
        // must change its name too, or a stale cached image would come back.
        int32_t Register(GlobalTexturePool& pool, const std::string& baseName, const std::vector<uint8_t>& pixels,
                         uint32_t size, bool srgb)
        {
            const std::string name = fmt::format("{}-{:016x}", baseName, XXH64(pixels.data(), pixels.size(), 0));
            if (const uint32_t existing = pool.TryGetTextureIndex(name); existing != static_cast<uint32_t>(-1))
            {
                return static_cast<int32_t>(existing);
            }
            uint32_t index = static_cast<uint32_t>(-1);
            if (pool.IsOwnerThread())
            {
                Vulkan::CommandPool& commandPool = pool.GetMainThreadCommandPool();
                auto image = std::make_unique<TextureImage>(commandPool, size, size, 1,
                                                            srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM,
                                                            pixels.data(), static_cast<uint32_t>(pixels.size()));
                image->MainThreadPostLoading(commandPool);
                index = pool.RegisterTexture(name, std::move(image), ETextureLifetime::ETL_Persistent);
            }
            else
            {
                // Off the main thread the regular decode/upload worker path takes it.
                const std::vector<uint8_t> png = EncodePng(pixels, size);
                index = pool.RequestNewTextureMemAsync(name, "image/png", false, png.data(), png.size(), srgb,
                                                       ETextureLifetime::ETL_Persistent);
            }
            return index == static_cast<uint32_t>(-1) ? -1 : static_cast<int32_t>(index);
        }
    }

    uint64_t FProceduralTexture::Hash() const
    {
        XXH64_state_t* state = XXH64_createState();
        XXH64_reset(state, ProceduralTexture::kBakeVersion);
        XXH64_update(state, pattern.data(), pattern.size());
        const std::vector<glm::vec3> resolved = ProceduralTexture::ResolvePalette(*this);
        XXH64_update(state, resolved.data(), resolved.size() * sizeof(glm::vec3));
        XXH64_update(state, &seed, sizeof(seed));
        const float clampedRelief = std::clamp(relief, 0.0f, 3.0f);
        XXH64_update(state, &clampedRelief, sizeof(clampedRelief));
        const uint64_t hash = XXH64_digest(state);
        XXH64_freeState(state);
        // Scale only changes UVs, not the bake, so it stays out of the content key.
        return hash;
    }

    namespace ProceduralTexture
    {
        using namespace ProceduralTextureDetail;

        std::span<const std::string_view> PatternNames()
        {
            return kPatterns;
        }

        bool IsKnownPattern(std::string_view pattern)
        {
            return std::find(kPatterns.begin(), kPatterns.end(), pattern) != kPatterns.end();
        }

        std::vector<glm::vec3> ResolvePalette(const FProceduralTexture& texture)
        {
            std::vector<glm::vec3> palette = DefaultPalette(texture.pattern);
            for (size_t i = 0; i < texture.palette.size() && i < palette.size(); ++i)
            {
                palette[i] = glm::clamp(texture.palette[i], glm::vec3(0.0f), glm::vec3(1.0f));
            }
            return palette;
        }

        std::vector<uint8_t> Bake(const FProceduralTexture& texture, uint32_t size)
        {
            return EncodeAlbedo(BakeSurface(texture, size));
        }

        FProceduralMaps BakeMaps(const FProceduralTexture& texture, uint32_t size)
        {
            const FSurface surface = BakeSurface(texture, size);
            FProceduralMaps maps;
            maps.albedo = EncodeAlbedo(surface);
            maps.hasMra = EncodeMra(surface, maps.mra);
            maps.hasNormal = EncodeNormal(surface, texture.pattern, texture.relief, maps.normal);
            if (!maps.hasMra) maps.mra.clear();
            if (!maps.hasNormal) maps.normal.clear();
            return maps;
        }

        FProceduralTextureIds AcquireMaps(const FProceduralTexture& texture)
        {
            FProceduralTextureIds ids;
            GlobalTexturePool* pool = GlobalTexturePool::GetInstance();
            if (!pool || !IsKnownPattern(texture.pattern))
            {
                return ids;
            }
            std::scoped_lock lock(AcquireMutex());
            const uint64_t hash = texture.Hash();
            const std::string base = fmt::format("procedural/{}-{:016x}", texture.pattern, hash);
            // The pool owns the images (persistent lifetime); the id cache only
            // skips re-baking a print this process has already registered.
            if (const auto found = IdsByHash().find(hash); found != IdsByHash().end())
            {
                if (found->second.albedo >= 0 &&
                    static_cast<uint32_t>(found->second.albedo) < pool->TotalTextures())
                {
                    return found->second;
                }
                IdsByHash().erase(found);
            }

            const FProceduralMaps maps = BakeMaps(texture, kDefaultSize);
            ids.albedo = Register(*pool, base, maps.albedo, kDefaultSize, true);
            if (ids.albedo < 0)
            {
                return ids;
            }
            if (maps.hasMra) ids.mra = Register(*pool, base + "-mra", maps.mra, kDefaultSize, false);
            if (maps.hasNormal) ids.normal = Register(*pool, base + "-normal", maps.normal, kDefaultSize, false);
            HashByTexture()[ids.albedo] = hash;
            IdsByHash()[hash] = ids;
            return ids;
        }

        int32_t Acquire(const FProceduralTexture& texture)
        {
            return AcquireMaps(texture).albedo;
        }

        uint64_t ContentHashOfTexture(int32_t textureId)
        {
            std::scoped_lock lock(AcquireMutex());
            const auto found = HashByTexture().find(textureId);
            return found == HashByTexture().end() ? 0 : found->second;
        }

        void BoxProjectUVs(std::span<const glm::dvec3> triangleSoup, float scale, std::span<glm::vec2> outUVs)
        {
            const double inverseScale = 1.0 / std::max(static_cast<double>(scale), 1e-4);
            for (size_t i = 0; i + 2 < triangleSoup.size() && i + 2 < outUVs.size(); i += 3)
            {
                const glm::dvec3 normal = glm::cross(triangleSoup[i + 1] - triangleSoup[i], triangleSoup[i + 2] - triangleSoup[i]);
                const glm::dvec3 a = glm::abs(normal);
                for (size_t k = 0; k < 3; ++k)
                {
                    const glm::dvec3& p = triangleSoup[i + k];
                    glm::dvec2 uv = a.x >= a.y && a.x >= a.z ? glm::dvec2(p.y, p.z)
                                  : a.y >= a.z               ? glm::dvec2(p.x, p.z)
                                                             : glm::dvec2(p.x, p.y);
                    outUVs[i + k] = glm::vec2(uv * inverseScale);
                }
            }
        }

        void BoxProjectTangents(std::span<const glm::dvec3> triangleSoup, const glm::mat3& toRender,
                                std::span<const glm::vec3> renderNormals, std::span<glm::vec4> outTangents)
        {
            const size_t count = std::min({triangleSoup.size(), renderNormals.size(), outTangents.size()});
            for (size_t i = 0; i + 2 < count; i += 3)
            {
                // The same dominant-axis choice as BoxProjectUVs: +u and +v are
                // object axes, so the frame is exact and constant per triangle.
                const glm::dvec3 normal = glm::cross(triangleSoup[i + 1] - triangleSoup[i], triangleSoup[i + 2] - triangleSoup[i]);
                const glm::dvec3 a = glm::abs(normal);
                glm::vec3 du, dv;
                if (a.x >= a.y && a.x >= a.z) { du = {0, 1, 0}; dv = {0, 0, 1}; }
                else if (a.y >= a.z)          { du = {1, 0, 0}; dv = {0, 0, 1}; }
                else                          { du = {1, 0, 0}; dv = {0, 1, 0}; }
                const glm::vec3 tangent = glm::normalize(toRender * du);
                const glm::vec3 bitangent = glm::normalize(toRender * dv);
                for (size_t k = 0; k < 3; ++k)
                {
                    const glm::vec3 n = renderNormals[i + k];
                    glm::vec3 t = tangent - n * glm::dot(n, tangent);
                    // A smoothed normal can lean onto +u; fall back to the frame
                    // rebuilt from +v so the basis never collapses.
                    if (glm::dot(t, t) < 1e-8f) t = glm::cross(bitangent, n);
                    t = glm::normalize(t);
                    const float w = glm::dot(glm::cross(n, t), bitangent) < 0.0f ? -1.0f : 1.0f;
                    outTangents[i + k] = glm::vec4(t, w);
                }
            }
        }
    }
}
