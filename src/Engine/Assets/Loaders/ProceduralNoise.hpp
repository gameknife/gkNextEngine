#pragma once

// ============================================================================
// ProceduralNoise.hpp - Tile-periodic noise primitives: the building blocks
// the texture graph nodes (TextureGraph.cpp) evaluate, plus the wrapping
// helpers the bake shares (ProceduralTexture.cpp). Every function wraps on the unit torus when its
// lattice counts are integers, so whatever is built from them tiles cleanly.
// ============================================================================

#include "Engine/Common/CoreMinimal.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include <cmath>

namespace Assets
{
    namespace ProceduralNoise
    {
        inline uint32_t Mix32(uint32_t x)
        {
            x ^= x >> 16;
            x *= 0x7feb352dU;
            x ^= x >> 15;
            x *= 0x846ca68bU;
            x ^= x >> 16;
            return x;
        }

        inline float Rand01(uint32_t a, uint32_t b, uint32_t seed)
        {
            const uint32_t h = Mix32(a * 0x9E3779B1U ^ Mix32(b + 0x632BE5ABU) ^ Mix32(seed * 0x85EBCA77U + 0x27D4EB2FU));
            return static_cast<float>(h >> 8) / 16777216.0f;
        }

        inline int WrapIndex(int i, int n)
        {
            const int m = i % n;
            return m < 0 ? m + n : m;
        }

        inline float WrapDelta(float d)
        {
            return d - std::round(d);
        }

        // Periodic value noise on the unit torus: pu x pv lattice cells per tile.
        // Fewer cells along an axis stretches the features along it.
        inline float ValueNoise(float u, float v, int pu, int pv, uint32_t seed)
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

        inline float Fbm(float u, float v, int pu, int pv, int octaves, uint32_t seed)
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

        // A few flat tones instead of a continuous ramp: the prints stay
        // painterly next to flat-shaded low-poly facets rather than photographic.
        inline float Posterize(float v, int levels)
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
        inline FCellHit Cellular(float u, float v, int n, uint32_t seed)
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
        inline FCourseHit Course(float u, float v, int rows, int columns, float shift, uint32_t seed, bool randomShift)
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
        inline float Weave(float u, float v, float threads, bool twill)
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

    }
}
