#pragma once

// ============================================================================
// ProceduralTexture.hpp - Tileable surface prints baked on the CPU at load time.
//
// Some surfaces are a pattern, not a shape: a woodland or digital camouflage
// print, a rain-stroke uniform, the weave of canvas, the grain of leather.
// Modelling those as geometry costs thousands of slivers and still reads as
// stickers. Instead an asset names a pattern + palette (SCAD: gk_texture()),
// the loader gives each triangle a box-projected UV in the object's own space
// (so the print sticks to a garment as its bone moves), and the material
// samples the baked image as an ordinary sRGB diffuse texture: the final
// albedo is material color x texture, exactly like a glTF baseColorFactor.
//
// Bakes are deterministic, wrap seamlessly in both axes, and are keyed by
// content, so the same pattern/palette/seed is uploaded once per process.
// ============================================================================

#include "Engine/Common/CoreMinimal.hpp"

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Assets
{
    struct FProceduralTexture
    {
        std::string pattern;            // one of ProceduralTexture::PatternNames()
        std::vector<glm::vec3> palette; // linear RGB, the same space as SCAD color(); empty = pattern default
        float scale = 0.5f;             // object-space units covered by one tile
        uint32_t seed = 0;

        uint64_t Hash() const;
        bool operator==(const FProceduralTexture&) const = default;
    };
    using FProceduralTextureRef = std::shared_ptr<const FProceduralTexture>;

    namespace ProceduralTexture
    {
        // Bump when a bake changes, so content-keyed caches (item icons, texture names) refresh.
        inline constexpr uint32_t kBakeVersion = 1;
        inline constexpr uint32_t kDefaultSize = 256;

        std::span<const std::string_view> PatternNames();
        bool IsKnownPattern(std::string_view pattern);
        // The palette a bake actually uses: the author's colors, padded with the
        // pattern's defaults when fewer are given.
        std::vector<glm::vec3> ResolvePalette(const FProceduralTexture& texture);

        // size x size RGBA8, sRGB-encoded, row-major; wraps seamlessly in u and v.
        std::vector<uint8_t> Bake(const FProceduralTexture& texture, uint32_t size = kDefaultSize);

        // Registers (or finds) the bake in the global texture pool and returns its
        // index; -1 when there is no GPU pool (CPU-only tools and tests). On the
        // pool's owner (main) thread the upload is synchronous, so anything rendered
        // right after the material exists (item icons) already sees the print.
        int32_t Acquire(const FProceduralTexture& texture);
        // Content hash of a texture index returned by Acquire, 0 for anything else.
        uint64_t ContentHashOfTexture(int32_t textureId);

        // Box projection for a triangle soup (three vertices per triangle) in the
        // object's own space with +Z up: each triangle takes the plane its normal
        // faces most, so side faces keep v along +Z (vertical prints stay vertical).
        // UVs are in tiles: position / scale.
        void BoxProjectUVs(std::span<const glm::dvec3> triangleSoup, float scale, std::span<glm::vec2> outUVs);
    }
}
