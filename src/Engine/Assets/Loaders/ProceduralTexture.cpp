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
        constexpr std::array<std::string_view, 8> kPatterns = {
            "woodland", "digital", "desert", "flora", "rain", "canvas", "twill", "grain"};

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
    }

    uint64_t FProceduralTexture::Hash() const
    {
        XXH64_state_t* state = XXH64_createState();
        XXH64_reset(state, ProceduralTexture::kBakeVersion);
        XXH64_update(state, pattern.data(), pattern.size());
        const std::vector<glm::vec3> resolved = ProceduralTexture::ResolvePalette(*this);
        XXH64_update(state, resolved.data(), resolved.size() * sizeof(glm::vec3));
        XXH64_update(state, &seed, sizeof(seed));
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
            size = std::clamp(size, 16U, 2048U);
            const std::vector<glm::vec3> palette = ResolvePalette(texture);
            const std::string_view pattern = texture.pattern;
            const uint32_t s = Mix32(texture.seed * 0x2545F491U + 0x9E37U);
            std::vector<glm::vec3> linear(static_cast<size_t>(size) * size, palette[0]);
            const auto pixelUV = [size](uint32_t x, uint32_t y)
            {
                return glm::vec2((static_cast<float>(x) + 0.5f) / static_cast<float>(size),
                                 (static_cast<float>(y) + 0.5f) / static_cast<float>(size));
            };
            const auto put = [&](uint32_t x, uint32_t y, const glm::vec3& c) { linear[static_cast<size_t>(y) * size + x] = c; };

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
                const bool twill = pattern == "twill";
                for (uint32_t y = 0; y < size; ++y)
                    for (uint32_t x = 0; x < size; ++x)
                    {
                        const glm::vec2 uv = pixelUV(x, y);
                        const float relief = Weave(uv.x, uv.y, twill ? 96.0f : 64.0f, twill);
                        const float slub = 0.93f + 0.14f * Fbm(uv.x, uv.y, 12, 12, 3, s + 71U);
                        put(x, y, palette[0] * ((0.80f + 0.20f * relief) * slub));
                    }
            }
            else if (pattern == "grain")
            {
                // Leather: soft mottling with scattered pores.
                for (uint32_t y = 0; y < size; ++y)
                    for (uint32_t x = 0; x < size; ++x)
                    {
                        const glm::vec2 uv = pixelUV(x, y);
                        float shade = 0.86f + 0.20f * Fbm(uv.x, uv.y, 20, 20, 3, s + 81U);
                        if (Rand01(x, y, s + 83U) > 0.94f) shade -= 0.07f;
                        put(x, y, palette[0] * shade);
                    }
            }

            // Camouflage is printed on twill: a light weave relief under the print.
            const bool printedCloth = pattern == "woodland" || pattern == "digital" || pattern == "desert" ||
                                      pattern == "flora" || pattern == "rain";
            std::vector<uint8_t> rgba(static_cast<size_t>(size) * size * 4);
            for (uint32_t y = 0; y < size; ++y)
                for (uint32_t x = 0; x < size; ++x)
                {
                    glm::vec3 c = linear[static_cast<size_t>(y) * size + x];
                    if (printedCloth)
                    {
                        const glm::vec2 uv = pixelUV(x, y);
                        c *= 0.92f + 0.08f * Weave(uv.x, uv.y, 96.0f, true);
                    }
                    uint8_t* out = &rgba[(static_cast<size_t>(y) * size + x) * 4];
                    out[0] = EncodeSrgb(c.r);
                    out[1] = EncodeSrgb(c.g);
                    out[2] = EncodeSrgb(c.b);
                    out[3] = 255;
                }
            return rgba;
        }

        int32_t Acquire(const FProceduralTexture& texture)
        {
            GlobalTexturePool* pool = GlobalTexturePool::GetInstance();
            if (!pool || !IsKnownPattern(texture.pattern))
            {
                return -1;
            }
            std::scoped_lock lock(AcquireMutex());
            const uint64_t hash = texture.Hash();
            const std::string name = fmt::format("procedural/{}-{:016x}", texture.pattern, hash);
            if (const uint32_t existing = pool->TryGetTextureIndex(name); existing != static_cast<uint32_t>(-1))
            {
                return static_cast<int32_t>(existing);
            }

            const std::vector<uint8_t> pixels = Bake(texture, kDefaultSize);
            uint32_t index = static_cast<uint32_t>(-1);
            if (pool->IsOwnerThread())
            {
                Vulkan::CommandPool& commandPool = pool->GetMainThreadCommandPool();
                auto image = std::make_unique<TextureImage>(commandPool, kDefaultSize, kDefaultSize, 1, VK_FORMAT_R8G8B8A8_SRGB,
                                                            pixels.data(), static_cast<uint32_t>(pixels.size()));
                image->MainThreadPostLoading(commandPool);
                index = pool->RegisterTexture(name, std::move(image), ETextureLifetime::ETL_Persistent);
            }
            else
            {
                // Off the main thread the regular decode/upload worker path takes it.
                std::vector<uint8_t> png;
                stbi_write_png_to_func(
                    [](void* context, void* data, int bytes)
                    {
                        auto* out = static_cast<std::vector<uint8_t>*>(context);
                        const auto* begin = static_cast<const uint8_t*>(data);
                        out->insert(out->end(), begin, begin + bytes);
                    },
                    &png, static_cast<int>(kDefaultSize), static_cast<int>(kDefaultSize), 4, pixels.data(),
                    static_cast<int>(kDefaultSize * 4));
                index = pool->RequestNewTextureMemAsync(name, "image/png", false, png.data(), png.size(), true,
                                                        ETextureLifetime::ETL_Persistent);
            }
            if (index == static_cast<uint32_t>(-1))
            {
                return -1;
            }
            HashByTexture()[static_cast<int32_t>(index)] = hash;
            return static_cast<int32_t>(index);
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
    }
}
