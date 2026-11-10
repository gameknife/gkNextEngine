#pragma once

// ============================================================================
// ProceduralTexture.hpp - Tileable surface prints baked on the CPU at load time.
//
// Some surfaces are a pattern, not a shape: a woodland or digital camouflage
// print, a rain-stroke uniform, the weave of canvas, the grain of leather.
// Modelling those as geometry costs thousands of slivers and still reads as
// stickers. Instead an asset names a print + palette (SCAD: gk_texture()),
// the loader gives each triangle a box-projected UV in the object's own space
// (so the print sticks to a garment as its bone moves), and the material
// samples the baked image as an ordinary sRGB diffuse texture: the final
// albedo is material color x texture, exactly like a glTF baseColorFactor.
//
// A pattern also describes its surface, not only its colour: a relief (mortar
// joints, roof-tile courses, thread crowns, stone cushions) baked into a
// tangent-space normal map, and where it is glossier or bare metal (worn
// stone crowns, chipped paint, rust) baked into a glTF-style MRA map whose G
// and B multiply the material's roughness and metalness. The loader writes
// tangents that match the box projection, so the relief lights correctly on
// every face. Relief strength is set per pattern as a slope (degrees at the
// 90th percentile), not a depth: the facets of a low-poly model still carry
// the shape, the relief only grains the light inside each facet.
//
// What a print looks like is not C++: every print is a texture graph
// (TextureGraph.hpp) written in SCAD -- the named ones are the tex_<name>()
// functions of assets/scad/lib/gk_texgraph.scad, resolved by the SCAD loader.
// This file only bakes a graph into its maps, uploads them and projects UVs.
//
// Bakes are deterministic, wrap seamlessly in both axes, and are keyed by
// content, so the same graph/palette/seed is uploaded once per process.
// ============================================================================

#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Assets/Loaders/TextureGraph.hpp"

#include <glm/vec2.hpp>
#include <glm/mat3x3.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Assets
{
    struct FProceduralTexture
    {
        std::string pattern;            // the print's name (gk_texture("plaster")), or "graph" for an inline graph
        std::shared_ptr<const FTextureGraph> graph; // what is baked; a texture without one has no maps
        std::vector<glm::vec3> palette; // linear RGB, the same space as SCAD color(); empty = pattern default
        float scale = 0.5f;             // object-space units covered by one tile
        uint32_t seed = 0;
        float relief = 1.0f;            // x the pattern's own relief slope; 0 = flat (no normal map)

        uint64_t Hash() const;
        bool operator==(const FProceduralTexture& other) const;
    };
    using FProceduralTextureRef = std::shared_ptr<const FProceduralTexture>;

    // The three bakes of one print, size x size RGBA8 each, row-major (v down
    // the rows), all wrapping seamlessly.
    struct FProceduralMaps
    {
        std::vector<uint8_t> albedo; // sRGB colour
        std::vector<uint8_t> mra;    // linear: R ambient occlusion (1), G roughness factor, B metalness factor
        std::vector<uint8_t> normal; // linear tangent-space normal, +Z out of the surface, +Y along +v
        bool hasMra = false;         // false: the print is uniformly rough and dielectric-neutral
        bool hasNormal = false;      // false: relief 0 or a flat pattern
    };

    // Global texture-pool indices of a print's maps; -1 where there is none.
    struct FProceduralTextureIds
    {
        int32_t albedo = -1;
        int32_t mra = -1;
        int32_t normal = -1;
    };

    namespace ProceduralTexture
    {
        // Bump when a bake changes, so content-keyed caches (item icons, texture names) refresh.
        inline constexpr uint32_t kBakeVersion = 2;
        inline constexpr uint32_t kDefaultSize = 256;

        // The palette a bake actually uses: the author's colors, padded with the
        // graph's tx_palette() defaults when fewer are given.
        std::vector<glm::vec3> ResolvePalette(const FProceduralTexture& texture);

        // size x size RGBA8, sRGB-encoded, row-major; wraps seamlessly in u and v.
        std::vector<uint8_t> Bake(const FProceduralTexture& texture, uint32_t size = kDefaultSize);
        // Albedo plus the surface maps (relief normal, roughness/metalness factors).
        FProceduralMaps BakeMaps(const FProceduralTexture& texture, uint32_t size = kDefaultSize);

        // Registers (or finds) the bake in the global texture pool and returns its
        // index; -1 when there is no GPU pool (CPU-only tools and tests). On the
        // pool's owner (main) thread the upload is synchronous, so anything rendered
        // right after the material exists (item icons) already sees the print.
        int32_t Acquire(const FProceduralTexture& texture);
        // Acquire for all three maps (albedo, MRA, normal), baked once together.
        FProceduralTextureIds AcquireMaps(const FProceduralTexture& texture);
        // Content hash of a texture index returned by Acquire, 0 for anything else.
        uint64_t ContentHashOfTexture(int32_t textureId);

        // Box projection for a triangle soup (three vertices per triangle) in the
        // object's own space with +Z up: each triangle takes the plane its normal
        // faces most, so side faces keep v along +Z (vertical prints stay vertical).
        // UVs are in tiles: position / scale.
        void BoxProjectUVs(std::span<const glm::dvec3> triangleSoup, float scale, std::span<glm::vec2> outUVs);
        // Tangent frames matching BoxProjectUVs: xyz = the direction of +u
        // mapped by `toRender` (object space -> render-mesh space) and made
        // orthogonal to that vertex's render normal, w = handedness so that
        // w * cross(normal, tangent) runs along +v. Feeds the normal map.
        void BoxProjectTangents(std::span<const glm::dvec3> triangleSoup, const glm::mat3& toRender,
                                std::span<const glm::vec3> renderNormals, std::span<glm::vec4> outTangents);
    }
}
