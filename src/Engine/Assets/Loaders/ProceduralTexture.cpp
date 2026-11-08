#include "Engine/Common/CoreMinimal.hpp"

#include "Engine/Assets/Loaders/ProceduralTexture.hpp"
#include "Engine/Assets/Loaders/ProceduralNoise.hpp"

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
        using namespace ProceduralNoise;

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

        // How strongly a print's relief tilts the normal (from its graph's
        // `surface` node): the slope in degrees reached by the steepest tenth
        // of the tile, and the bevel blur in texels.
        struct FReliefStyle
        {
            float tilt90 = 0.0f;
            int blur = 1;
        };
        // Every print is a texture graph (built-in names resolve to the SCAD
        // print library); a texture without one bakes a flat white tile.
        FSurface BakeSurface(const FProceduralTexture& texture, uint32_t size)
        {
            size = std::clamp(size, 16U, 2048U);
            FSurface surface;
            surface.size = size;
            const size_t texels = static_cast<size_t>(size) * size;
            if (!texture.graph)
            {
                surface.color.assign(texels, glm::vec3(1.0f));
                surface.height.assign(texels, 0.5f);
                surface.rough.assign(texels, 1.0f);
                surface.metal.assign(texels, 1.0f);
                return surface;
            }
            const std::vector<glm::vec3> palette = ProceduralTexture::ResolvePalette(texture);
            FTextureGraphBake bake;
            texture.graph->Bake(size, palette, texture.seed, bake);
            surface.color = std::move(bake.color);
            surface.height = std::move(bake.height);
            surface.rough = std::move(bake.rough);
            surface.metal = std::move(bake.metal);
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
        bool EncodeNormal(const FSurface& surface, const FReliefStyle& style, float relief, std::vector<uint8_t>& out)
        {
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
        if (graph)
        {
            XXH64_update(state, graph->Canonical().data(), graph->Canonical().size());
        }
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

    bool FProceduralTexture::operator==(const FProceduralTexture& other) const
    {
        const bool sameGraph = graph == other.graph ||
                               (graph && other.graph && graph->Canonical() == other.graph->Canonical());
        return pattern == other.pattern && sameGraph && palette == other.palette && scale == other.scale &&
               seed == other.seed && relief == other.relief;
    }

    namespace ProceduralTexture
    {
        using namespace ProceduralTextureDetail;

        std::vector<glm::vec3> ResolvePalette(const FProceduralTexture& texture)
        {
            std::vector<glm::vec3> palette =
                texture.graph ? texture.graph->DefaultPalette() : std::vector<glm::vec3>{};
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
            const FReliefStyle style = texture.graph
                ? FReliefStyle{texture.graph->ReliefTilt(), texture.graph->ReliefBlur()}
                : FReliefStyle{};
            maps.hasNormal = EncodeNormal(surface, style, texture.relief, maps.normal);
            if (!maps.hasMra) maps.mra.clear();
            if (!maps.hasNormal) maps.normal.clear();
            return maps;
        }

        FProceduralTextureIds AcquireMaps(const FProceduralTexture& texture)
        {
            FProceduralTextureIds ids;
            GlobalTexturePool* pool = GlobalTexturePool::GetInstance();
            if (!pool || !texture.graph)
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
