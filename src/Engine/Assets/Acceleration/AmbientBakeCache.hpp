// AmbientBakeCache: single-file disk cache for the ambient GI bake.
//
// The bake a scene runs on startup (CPU voxelization + distance field, then 32 GPU convergence
// passes) is a pure function of the scene's initial structure. This caches its output under one
// XXH64 key: same structure -> same key -> the whole bake is replaced by one file read.
//
// A miss, a corrupt file, or any mismatch falls back to the full bake. The cache is only ever an
// accelerator, never a correctness dependency.
#pragma once
#include "Engine/Assets/AssetsFwd.hpp"
#include "Engine/Assets/GPU/UniformBuffer.hpp"
#include "Engine/Common/CoreMinimal.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace Assets::CPU
{
    // Grid and pool parameters the bake was produced with. They go into the key rather than into a
    // post-load validation because a mismatch has no useful recovery: the payload is simply for a
    // different grid.
    struct FAmbientBakeKeyInputs
    {
        float baseUnit = Assets::CUBE_UNIT;
        glm::vec3 offsetBias{0.0f};
        uint32_t cascadeCount = 1;
        float cascadeRatio = 2.0f;
        // Where each cascade's grid sits. In fit-scene mode this, not the ratio setting, decides
        // which world cells a cascade holds.
        std::array<float, Assets::CUBE_CASCADE_MAX> cascadeUnits{};
        std::array<glm::vec3, Assets::CUBE_CASCADE_MAX> cascadeOrigins{};
        uint32_t poolBricksPerCascade = 0;
        bool hardwareBake = false;
    };

    // Everything a converged bake leaves behind. distanceToSolidSeeds is not stored: it is exactly
    // `matId > 0 ? 0 : kMaxDistanceFieldSeed` and is rebuilt in one pass on load.
    struct FAmbientBakeCachePayload
    {
        // Voxels follow the CPU bakers (one per active cascade); the brick table, active list and
        // cube pool follow the arena, which is laid out for the allocated cascade capacity. The two
        // are normally equal, but they are stored separately so a mismatch cannot silently reshape
        // the brick -> pool slot mapping the cubes were baked into.
        uint32_t cascadeCount = 0;
        uint32_t cascadeCapacity = 0;
        uint32_t voxelCountPerCascade = 0;
        uint32_t poolBricksPerCascade = 0;
        std::vector<Assets::VoxelData> voxels;          // cascadeCount * voxelCountPerCascade
        std::vector<uint32_t> brickTable;               // cascadeCapacity * BRICKS_PER_CASCADE
        std::vector<uint32_t> activeBrickList;          // cascadeCapacity * poolBricksPerCascade
        std::vector<uint32_t> activeBricksPerCascade;   // cascadeCapacity
        std::vector<Assets::PageIndex> pages;           // ACGI_PAGE_COUNT^2
        std::vector<Assets::AmbientCube> cubes;         // cascadeCapacity * poolBricks * BRICK_VOLUME
    };

    // Reads r.ambientCube.diskCache. False disables both load and save.
    bool IsAmbientBakeDiskCacheEnabled();

    // Hashes the scene's initial structure: grid config, GI-participating instances and their
    // transforms, model geometry, materials, lights, sun/sky and the bake backend. Uses the same
    // instance filter as the CPU TLAS capture, so the key covers exactly the geometry that gets
    // baked.
    uint64_t ComputeAmbientBakeKey(const Assets::Scene& scene, const FAmbientBakeKeyInputs& inputs);

    std::string GetAmbientBakeCachePath(uint64_t key);
    bool LoadAmbientBakeCache(uint64_t key, FAmbientBakeCachePayload& outPayload);
    bool SaveAmbientBakeCache(uint64_t key, const FAmbientBakeCachePayload& payload);
}
