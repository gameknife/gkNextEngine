#pragma once

// ============================================================================
// FScadEvaluator.h - Evaluates a parsed SCAD program into colored geometry.
//
// MVP semantics:
//   * union/group/hull/minkowski  -> concatenate all child geometry
//   * difference/intersection      -> keep only the first child (degraded; a
//                                     true boolean backend (Manifold) is a
//                                     later phase). Emits a one-time warning.
// Evaluate() keeps the legacy flat output grouped by leaf user-module instance
// (falling back to the root instance label when no user module is active) +
// quantized color. EvaluateScene() additionally reconstructs the user-module
// hierarchy so the loader can emit parent/child Nodes and merge direct geometry
// on each logical module node.
// Triangles are in native SCAD world space (Z-up); the loader converts to Y-up.
// ============================================================================

#include <cmath>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "Engine/Assets/Data/Material.hpp"
#include "Engine/Assets/Loaders/ProceduralTexture.hpp"
#include "Modules/ScadLoader/FScadTypes.h"

namespace Assets::Scad
{
    struct FTerrainData; // FScadTerrain.h

    // Surface parameters carried by the SCAD evaluator into an engine material.
    // The defaults intentionally preserve the historical color() behavior.
    struct ScadMaterialProperties
    {
        float roughness = 1.0f;
        float metalness = 0.0f;
        bool explicitParameters = false;
        // gk_texture() print for this geometry (null = plain color). Shared, so
        // copying properties through CSG and buckets stays cheap.
        FProceduralTextureRef texture;
        // gk_material(..., ior, subsurface, sheen) / gk_foliage(...): see Material::Surface.
        FSurfaceStyle style;
    };

    // Folds a gk_texture() print and a surface style into a color/surface bucket
    // key: geometry with the same color but a different print or style must land
    // in its own material slot.
    inline uint64_t MixSurfaceIntoKey(uint64_t key, const ScadMaterialProperties& material)
    {
        const auto mix = [&key](uint64_t h)
        {
            h ^= h >> 33u;
            h *= 0xff51afd7ed558ccdULL;
            h ^= h >> 33u;
            key ^= (h | 1ULL);
        };
        if (material.texture)
        {
            mix(material.texture->Hash() ^ (static_cast<uint64_t>(material.texture->scale * 4096.0f) << 1u));
        }
        if (!material.style.IsDefault())
        {
            const FSurfaceStyle& s = material.style;
            const auto q = [](float v) { return static_cast<uint64_t>(std::lround((v + 2.0f) * 1000.0f)) & 0xFFFFu; };
            mix(0x5157A11E00000000ULL ^ (q(s.ior) << 48u) ^ (q(s.subsurface) << 32u) ^ (q(s.sheen) << 16u) ^
                q(s.foliageTransmission) ^ (q(s.foliageThickness) << 8u));
        }
        return key;
    }

    struct ColorBucket
    {
        glm::vec4 color = glm::vec4(0.78f, 0.78f, 0.78f, 1.0f);
        ScadMaterialProperties material;
        std::string groupName;
        std::vector<glm::dvec3> tris; // triangle soup (3N), SCAD world space (Z-up)
        bool faceted = false; // skip vertex-normal smoothing (terrain)
        std::vector<ScadNormalSpan> normalSpans; // gk_crease / gk_puff_normals runs
        std::vector<glm::dvec3> normals; // authored normals parallel to tris (empty = none, zero = compute)
    };

    struct BucketKey
    {
        std::string groupName;
        uint64_t groupInstanceId = 0;
        uint64_t materialKey = 0;

        bool operator<(const BucketKey& other) const
        {
            if (groupName != other.groupName)
            {
                return groupName < other.groupName;
            }
            if (groupInstanceId != other.groupInstanceId)
            {
                return groupInstanceId < other.groupInstanceId;
            }
            return materialKey < other.materialKey;
        }
    };

    struct EvalResult
    {
        std::map<BucketKey, ColorBucket> buckets; // ordered by group label + instance + color for determinism
        int warningCount = 0;
        size_t triangleCount = 0;
    };

    struct SceneMeshBucket
    {
        glm::vec4 color = glm::vec4(0.78f, 0.78f, 0.78f, 1.0f);
        ScadMaterialProperties material;
        std::string materialName;
        std::vector<glm::dvec3> tris; // triangle soup (3N), local to the owning scene node
        bool faceted = false; // skip vertex-normal smoothing (terrain)
        // Triangle runs with their own normal shaping (gk_crease / gk_puff_normals);
        // positions in the spans are in the same space as tris. Compute normals
        // with ScadComputeShadedNormals (FScadShared.h).
        std::vector<ScadNormalSpan> normalSpans;
        // Authored normals parallel to tris, same space (gk_round_box / gk_sweep
        // know their exact surface). Empty when nothing in the bucket authored
        // any; a zero entry means "compute this corner".
        std::vector<glm::dvec3> normals;
        bool terrainDecal = false; // draped overlay (road ribbons): own node, no collision / raycast
        bool terrainWater = false; // translucent terrain water surface (split to a
                                   // dedicated node, no raycast visibility)
    };

    struct SceneNode
    {
        std::string name;
        uint64_t instanceId = 0;
        glm::dmat4 localTransform = glm::dmat4(1.0);
        int sourceLine = 0;
        size_t sourceOffset = 0;
        std::vector<std::pair<std::string, Value>> parameters;
        glm::dvec4 callColor = glm::dvec4(0.78, 0.78, 0.78, 1.0);
        bool hasCallColor = false;
        std::vector<SceneMeshBucket> meshes;
        std::vector<SceneNode> children;
    };

    // One gk_terrain() instance evaluated during the scene run. `xform` is the
    // accumulated SCAD-space transform at the call site (local to the owning
    // scene node); the loader bakes it together with the Z-up -> Y-up
    // conversion and the node's world transform when exposing terrain queries.
    struct SceneTerrain
    {
        std::shared_ptr<const FTerrainData> data;
        glm::dmat4 xform = glm::dmat4(1.0);
        uint64_t ownerInstanceId = 0; // scene node the terrain geometry lands in
    };

    struct SceneEvalResult
    {
        std::vector<SceneNode> roots;
        // Final top-level variable bindings (snapshot of the global scope after
        // the main file's top level runs). Lets rig/animation loaders read data
        // variables (e.g. anim_*) without re-evaluating expressions.
        std::map<std::string, Value> topLevelVariables;
        std::vector<SceneTerrain> terrains;
        int warningCount = 0;
        size_t triangleCount = 0;
        // The text of the warnings that were also logged (each kind is logged a few
        // times at most), capped at kMaxRecordedWarnings, so a host can show an author
        // *which* module was unknown instead of only how many warnings there were.
        std::vector<std::string> warnings;
        static constexpr size_t kMaxRecordedWarnings = 32;
    };

    class ScadEvaluator
    {
    public:
        // modules/functions are the merged definition tables from the use/include closure.
        // mainTopLevel is the executable top-level scope of the main file only.
        static bool Evaluate(const Scope& mainTopLevel, const std::unordered_map<std::string, StmtPtr>& modules,
                             const std::unordered_map<std::string, StmtPtr>& functions, const ScadLoadOptions& options,
                             EvalResult& outResult, std::string& outError);

        static bool EvaluateScene(const Scope& mainTopLevel, const std::unordered_map<std::string, StmtPtr>& modules,
                                  const std::unordered_map<std::string, StmtPtr>& functions,
                                  const ScadLoadOptions& options, SceneEvalResult& outResult, std::string& outError);
    };
} // namespace Assets::Scad
