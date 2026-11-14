#pragma once

// Private implementation details for the core CPU scene acceleration system.

// Internal declarations shared between the CPUAccelerationStructure partial
// TUs (CpuBvh / ProbeBaker / BrickPageTable / main). Not engine public API.

#include "Engine/Assets/Acceleration/CPUAccelerationStructure.hpp"

namespace Assets::CPU
{
    constexpr uint32_t kCascadeVoxelCount = Assets::CUBE_SIZE_XY * Assets::CUBE_SIZE_XY * Assets::CUBE_SIZE_Z;
    constexpr uint8_t kMaxDistanceFieldSeed = 15;

    // Surface shell a cascade keeps cube bricks for, in cells of its distance field: about 2 m of air
    // over every surface (character height) on the fine cascades, never thinner than 2 cells, and
    // never more than one brick, so the coarse cascades keep only the layer that touches the ground.
    inline uint32_t ResolveAmbientBrickShellCells(float unit)
    {
        constexpr float shellMeters = 2.0f;
        const float cells = std::ceil(shellMeters / std::max(unit, 1e-4f));
        return static_cast<uint32_t>(std::clamp(cells, 2.0f, 8.0f));
    }

    // Voxel storage is brick-swizzled: the cascade array is tiled into GPU_SCENE_AMBIENT_BRICK_EDGE^3
    // blocks rather than stored as y * W * W + z * W + x. The shader DDA walks this array one cell at
    // a time, and under the linear layout a single Y step moved the read ~2 MB away, so almost every
    // step missed cache. Tiled, a whole 8^3 neighbourhood is one contiguous 4 KB run.
    // VoxelStorageIndex() in assets/shaders/common/AmbientCube.slang must match this exactly.
    constexpr uint32_t kVoxelBrickEdge = static_cast<uint32_t>(Assets::GPU_SCENE_AMBIENT_BRICK_EDGE);
    constexpr uint32_t kVoxelBrickVolume = static_cast<uint32_t>(Assets::GPU_SCENE_AMBIENT_BRICK_VOLUME);

    // x, y, z are window cells in [0, CUBE_SIZE). The brick part goes through the cascade's ring
    // offset (AmbientStorageBrick); the cell inside the brick does not.
    inline uint32_t GetVoxelAddress(int x, int y, int z, const glm::ivec3& wrap = glm::ivec3(0))
    {
        const uint32_t ux = static_cast<uint32_t>(x);
        const uint32_t uy = static_cast<uint32_t>(y);
        const uint32_t uz = static_cast<uint32_t>(z);
        const glm::ivec3 storage = AmbientStorageBrick(
            glm::ivec3(static_cast<int>(ux / kVoxelBrickEdge), static_cast<int>(uy / kVoxelBrickEdge),
                       static_cast<int>(uz / kVoxelBrickEdge)),
            wrap);
        const uint32_t localOffset = (uy % kVoxelBrickEdge) * (kVoxelBrickEdge * kVoxelBrickEdge) +
                                     (uz % kVoxelBrickEdge) * kVoxelBrickEdge + (ux % kVoxelBrickEdge);
        return AmbientBrickLinearIndex(storage) * kVoxelBrickVolume + localOffset;
    }

    uint FetchMaterialId(const FCPUTLASSnapshot& snapshot, uint materialIdx, uint instanceId);
    bool TraceRay(const FCPUTLASSnapshot& snapshot, glm::vec3 origin, glm::vec3 rayDir, float dist,
                  glm::vec3& outNormal, uint& outMaterialId, float& outRayDist, uint& outInstanceId);
}
