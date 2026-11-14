// FCPUProbeBaker: ambient cube voxelization, Chebyshev (L-infinity) distance field
// and GPU upload for the software GI probe cascades.
// Split from CPUAccelerationStructure.cpp; same namespace, separate TU.
#include "Engine/Assets/Acceleration/CPUAccelerationStructure.hpp"
#include "Engine/Assets/Acceleration/CPUAccelerationStructure.Internal.hpp"
#include "Engine/Runtime/Subsystems/TaskCoordinator.hpp"
#include "Engine/Vulkan/MemoryAndShader.hpp"
#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Runtime/Components/RenderComponent.hpp"
#include "Engine/Runtime/Components/PhysicsComponent.hpp"
#include "Engine/Assets/GPU/TextureImage.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Assets/Core/Scene.hpp"

namespace Assets::CPU
{

namespace
{
    uint32_t SetPackedNibbleX(uint32_t packedValue, uint8_t value)
    {
        return (packedValue & 0xFFFFFFF0u) | (value & 0xFu);
    }

    struct FChamferNeighbor
    {
        int dx;
        int dy;
        int dz;
        float weight;
    };

    // All 26 neighbours carry weight 1, which makes the two-sweep chamfer produce an exact
    // Chebyshev (L-infinity) distance transform rather than a Euclidean approximation. The
    // shader consumes this field as "every cell within `d` of me in every axis is empty", and
    // only L-infinity supports that reading: with Euclidean weights a diagonal ray can leave
    // the guaranteed-empty region after d/sqrt(3) cells, which is how the skip used to tunnel
    // through thin geometry. See TraceRayDDA in common/RayTracers.slang.
    constexpr std::array<FChamferNeighbor, 13> kForwardChamferNeighbors = {
        FChamferNeighbor{-1, -1, -1, 1.0f},
        FChamferNeighbor{0, -1, -1, 1.0f},
        FChamferNeighbor{1, -1, -1, 1.0f},
        FChamferNeighbor{-1, -1, 0, 1.0f},
        FChamferNeighbor{0, -1, 0, 1.0f},
        FChamferNeighbor{1, -1, 0, 1.0f},
        FChamferNeighbor{-1, -1, 1, 1.0f},
        FChamferNeighbor{0, -1, 1, 1.0f},
        FChamferNeighbor{1, -1, 1, 1.0f},
        FChamferNeighbor{-1, 0, -1, 1.0f},
        FChamferNeighbor{0, 0, -1, 1.0f},
        FChamferNeighbor{1, 0, -1, 1.0f},
        FChamferNeighbor{-1, 0, 0, 1.0f},
    };

    constexpr std::array<FChamferNeighbor, 13> kBackwardChamferNeighbors = {
        FChamferNeighbor{1, 0, 0, 1.0f},
        FChamferNeighbor{-1, 0, 1, 1.0f},
        FChamferNeighbor{0, 0, 1, 1.0f},
        FChamferNeighbor{1, 0, 1, 1.0f},
        FChamferNeighbor{-1, 1, -1, 1.0f},
        FChamferNeighbor{0, 1, -1, 1.0f},
        FChamferNeighbor{1, 1, -1, 1.0f},
        FChamferNeighbor{-1, 1, 0, 1.0f},
        FChamferNeighbor{0, 1, 0, 1.0f},
        FChamferNeighbor{1, 1, 0, 1.0f},
        FChamferNeighbor{-1, 1, 1, 1.0f},
        FChamferNeighbor{0, 1, 1, 1.0f},
        FChamferNeighbor{1, 1, 1, 1.0f},
    };

    // One raster sweep over a dense box of cells (x fastest, then z, then y), so the inner loop never
    // goes through the ring-addressed storage.
    void ApplyChamferPass(std::vector<float>& field, const glm::ivec3& dims,
                          const std::array<FChamferNeighbor, 13>& neighbors, bool reverseSweep)
    {
        const auto index = [&dims](int x, int y, int z)
        {
            return (static_cast<size_t>(y) * dims.z + static_cast<size_t>(z)) * dims.x + static_cast<size_t>(x);
        };
        const int xStart = reverseSweep ? dims.x - 1 : 0;
        const int xEnd = reverseSweep ? -1 : dims.x;
        const int xStep = reverseSweep ? -1 : 1;
        const int yStart = reverseSweep ? dims.y - 1 : 0;
        const int yEnd = reverseSweep ? -1 : dims.y;
        const int yStep = reverseSweep ? -1 : 1;
        const int zStart = reverseSweep ? dims.z - 1 : 0;
        const int zEnd = reverseSweep ? -1 : dims.z;
        const int zStep = reverseSweep ? -1 : 1;

        for (int y = yStart; y != yEnd; y += yStep)
        {
            for (int z = zStart; z != zEnd; z += zStep)
            {
                for (int x = xStart; x != xEnd; x += xStep)
                {
                    float bestDistance = field[index(x, y, z)];
                    for (const FChamferNeighbor& neighbor : neighbors)
                    {
                        const int nx = x + neighbor.dx;
                        const int ny = y + neighbor.dy;
                        const int nz = z + neighbor.dz;
                        if (nx < 0 || nx >= dims.x || ny < 0 || ny >= dims.y || nz < 0 || nz >= dims.z)
                        {
                            continue;
                        }
                        bestDistance = std::min(bestDistance, field[index(nx, ny, nz)] + neighbor.weight);
                    }
                    field[index(x, y, z)] = bestDistance;
                }
            }
        }
    }

    // A cell's distance only depends on solids within kMaxDistanceFieldSeed cells; two bricks covers
    // that with room to spare and keeps the boxes brick-aligned.
    constexpr int kDistanceFieldReach = 2 * static_cast<int>(Assets::GPU_SCENE_AMBIENT_BRICK_EDGE);
    static_assert(kDistanceFieldReach > kMaxDistanceFieldSeed);

    const glm::ivec3 kGridMaxCell(Assets::CUBE_SIZE_XY - 1, Assets::CUBE_SIZE_Z - 1, Assets::CUBE_SIZE_XY - 1);
}

using namespace Assets;

uint PackNibbles(glm::u32vec4 low, glm::u32vec4 high)
{
    return (low.x & 0xFu) |
           ((low.y & 0xFu) << 4) |
           ((low.z & 0xFu) << 8) |
           ((low.w & 0xFu) << 12) |
           ((high.x & 0xFu) << 16) |
           ((high.y & 0xFu) << 20) |
           ((high.z & 0xFu) << 24) |
           ((high.w & 0xFu) << 28);
}

#define FLOAT2 vec2
#define FLOAT3 vec3
#define FLOAT4 vec4

bool InsideGeometry(const FCPUTLASSnapshot& snapshot, FLOAT3& origin, FLOAT3 rayDir, VoxelData& outCube,
                    float& distance, float cubeUnit)
{
    // Intersection test.
    vec3 outNormal;
    float outRayDist;
    uint tempMaterialId;
    uint tempInstanceId;

    // Only a hit within one cell changes the voxel: anything farther clamps to the same "clear" 1.0
    // in VoxelizeCube whether it is found or not. Tracing a hair past one cell instead of 64 keeps
    // every output bit identical while the BVH walk stops at the first cell.
    if (TraceRay(snapshot, origin, rayDir, cubeUnit * 1.01f, outNormal, tempMaterialId, outRayDist, tempInstanceId))
    {
        distance = outRayDist;
        if (distance <= cubeUnit)
        {
            outCube.matId = tempMaterialId;

            Material::Enum materialModel = Material::Enum::Lambertian;
            if (snapshot.materialTable && !snapshot.materialTable->entries.empty())
            {
                const size_t materialIndex = tempMaterialId % snapshot.materialTable->entries.size();
                materialModel = snapshot.materialTable->entries[materialIndex].materialModel;
            }

            // A back-face hit indicates solid geometry; push the light probe outside it.
            if (dot(outNormal, rayDir) > 0.0 || materialModel == Material::Enum::DiffuseLight)
            {
                distance = 0;
                return true;
            }
        }
    }
    return false;
}

void VoxelizeCube(const FCPUTLASSnapshot& snapshot, VoxelData& cube, FLOAT3 origin, float cubeUnit)
{
    // just write matid and solid status
    cube.matId = 0;

    float distPY = 255.0f;
    float distNY = 255.0f;
    float distPX = 255.0f;
    float distNX = 255.0f;
    float distPZ = 255.0f;
    float distNZ = 255.0f;

    // Cast six axis-aligned rays and retain their distances for later sampling decisions.
    InsideGeometry(snapshot, origin, FLOAT3(0, 1, 0), cube, distPY, cubeUnit);
    InsideGeometry(snapshot, origin, FLOAT3(0, -1, 0), cube, distNY, cubeUnit);
    InsideGeometry(snapshot, origin, FLOAT3(1, 0, 0), cube, distPX, cubeUnit);
    InsideGeometry(snapshot, origin, FLOAT3(-1, 0, 0), cube, distNX, cubeUnit);
    InsideGeometry(snapshot, origin, FLOAT3(0, 0, 1), cube, distPZ, cubeUnit);
    InsideGeometry(snapshot, origin, FLOAT3(0, 0, -1), cube, distNZ, cubeUnit);

    // Each voxel now has a distance field that can be used for fast skipping.
    distPY = glm::fclamp(distPY / cubeUnit, 0.0f, 1.0f);
    distNY = glm::fclamp(distNY / cubeUnit, 0.0f, 1.0f);
    distPX = glm::fclamp(distPX / cubeUnit, 0.0f, 1.0f);
    distNX = glm::fclamp(distNX / cubeUnit, 0.0f, 1.0f);
    distPZ = glm::fclamp(distPZ / cubeUnit, 0.0f, 1.0f);
    distNZ = glm::fclamp(distNZ / cubeUnit, 0.0f, 1.0f);

    float inside = distPY * distNY * distPX * distNX * distPZ * distNZ;

    // Nibble x is the L-infinity distance field. It is a whole-cascade sweep, so only a
    // placeholder goes in here; RebuildDistanceField() overwrites every voxel once the cascade
    // has finished voxelizing.
    cube.distanceToSolid = PackNibbles(
        glm::u32vec4(kMaxDistanceFieldSeed,
                     static_cast<uint>(inside * 15.0f),
                     static_cast<uint>(distPZ * 15.0f),
                     static_cast<uint>(distNZ * 15.0f)),
        glm::u32vec4(static_cast<uint>(distPX * 15.0f),
                     static_cast<uint>(distNX * 15.0f),
                     static_cast<uint>(distPY * 15.0f),
                     static_cast<uint>(distNY * 15.0f)));
}

#undef float2
#undef float3
#undef float4

void FCPUProbeBaker::Init(uint32_t cascadeIdx, float unitSize, vec3 offset, glm::ivec3 wrap)
{
    cascadeIndex = cascadeIdx;
    UNIT_SIZE = unitSize;
    CUBE_OFFSET = offset;
    BRICK_WRAP = wrap;
    voxels.assign(kCascadeVoxelCount, VoxelData{});
    distanceToSolidSeeds.assign(kCascadeVoxelCount, kMaxDistanceFieldSeed);
    ClearVoxels();
}

uint32_t FCPUProbeBaker::VoxelAddress(int x, int y, int z) const
{
    return GetVoxelAddress(x, y, z, BRICK_WRAP);
}

glm::ivec3 FCPUProbeBaker::WindowBrick(uint32_t storageBrick) const
{
    return AmbientWindowBrick(AmbientBrickFromLinearIndex(storageBrick), BRICK_WRAP);
}

void FCPUProbeBaker::VoxelizeBrick(uint32_t storageBrick, const FCPUTLASSnapshot& snapshot)
{
    const glm::ivec3 firstCell = WindowBrick(storageBrick) * static_cast<int>(kVoxelBrickEdge);
    const size_t base = static_cast<size_t>(storageBrick) * kVoxelBrickVolume;
    for (uint32_t local = 0; local < kVoxelBrickVolume; ++local)
    {
        // Same in-brick order as GetVoxelAddress: x fastest, then z, then y.
        const glm::ivec3 cell = firstCell + glm::ivec3(static_cast<int>(local % kVoxelBrickEdge),
                                                       static_cast<int>(local / (kVoxelBrickEdge * kVoxelBrickEdge)),
                                                       static_cast<int>((local / kVoxelBrickEdge) % kVoxelBrickEdge));
        VoxelData& voxel = voxels[base + local];
        VoxelizeCube(snapshot, voxel, vec3(cell) * UNIT_SIZE + CUBE_OFFSET, UNIT_SIZE);
        // Seed the transform from occupancy alone. The DDA only ever reports a hit on a voxel with
        // matId != 0, so the distance to the nearest such voxel is exactly the clearance a ray is
        // allowed to skip -- an axis-ray distance seed could only make the field smaller than that
        // and cost traversal steps for nothing.
        distanceToSolidSeeds[base + local] = voxel.matId > 0 ? uint8_t{0} : kMaxDistanceFieldSeed;
    }
}

void FCPUProbeBaker::ClearVoxels()
{
    for (size_t voxelIndex = 0; voxelIndex < voxels.size(); ++voxelIndex)
    {
        VoxelData& voxel = voxels[voxelIndex];
        voxel = {};
        voxel.distanceToSolid = SetPackedNibbleX(voxel.distanceToSolid, kMaxDistanceFieldSeed);
        distanceToSolidSeeds[voxelIndex] = kMaxDistanceFieldSeed;
    }
}

std::vector<uint32_t> FCPUProbeBaker::StorageBricksInBox(const glm::ivec3& cellMin, const glm::ivec3& cellMax) const
{
    const glm::ivec3 edge(static_cast<int>(kVoxelBrickEdge));
    const glm::ivec3 brickMin = glm::clamp(cellMin, glm::ivec3(0), kGridMaxCell) / edge;
    const glm::ivec3 brickMax = glm::clamp(cellMax, glm::ivec3(0), kGridMaxCell) / edge;
    std::vector<uint32_t> bricks;
    for (int y = brickMin.y; y <= brickMax.y; ++y)
        for (int z = brickMin.z; z <= brickMax.z; ++z)
            for (int x = brickMin.x; x <= brickMax.x; ++x)
                bricks.push_back(AmbientBrickLinearIndex(AmbientStorageBrick(glm::ivec3(x, y, z), BRICK_WRAP)));
    return bricks;
}

void FCPUProbeBaker::UploadBricks(Vulkan::DeviceMemory& voxelGpuMemory, size_t byteBaseOffset, uint32_t elementOffset,
                                  const std::vector<uint32_t>& storageBricks)
{
    if (storageBricks.empty())
    {
        return;
    }
    const size_t cascadeOffset = byteBaseOffset + static_cast<size_t>(elementOffset) * sizeof(VoxelData);
    auto* mapped = static_cast<std::byte*>(voxelGpuMemory.Map(cascadeOffset, sizeof(VoxelData) * voxels.size()));
    constexpr size_t brickBytes = static_cast<size_t>(kVoxelBrickVolume) * sizeof(VoxelData);
    for (uint32_t brick : storageBricks)
    {
        std::memcpy(mapped + static_cast<size_t>(brick) * brickBytes,
                    voxels.data() + static_cast<size_t>(brick) * kVoxelBrickVolume, brickBytes);
    }
    voxelGpuMemory.Unmap();
}

void FCPUProbeBaker::UploadGPU(Vulkan::DeviceMemory& voxelGpuMemory, size_t byteBaseOffset, uint32_t elementOffset)
{
    const size_t byteOffset = byteBaseOffset + static_cast<size_t>(elementOffset) * sizeof(VoxelData);
    VoxelData* data = reinterpret_cast<VoxelData*>(voxelGpuMemory.Map(byteOffset, sizeof(VoxelData) * voxels.size()));
    std::memcpy(data, voxels.data(), voxels.size() * sizeof(VoxelData));
    voxelGpuMemory.Unmap();
}

void FCPUProbeBaker::RebuildDistanceField()
{
    RebuildDistanceField(glm::ivec3(0), kGridMaxCell);
}

std::pair<glm::ivec3, glm::ivec3> FCPUProbeBaker::RebuildDistanceField(const glm::ivec3& changedMin,
                                                                       const glm::ivec3& changedMax)
{
    const glm::ivec3 outMin = glm::clamp(changedMin - kDistanceFieldReach, glm::ivec3(0), kGridMaxCell);
    const glm::ivec3 outMax = glm::clamp(changedMax + kDistanceFieldReach, glm::ivec3(0), kGridMaxCell);
    if (voxels.empty() || distanceToSolidSeeds.size() != voxels.size())
    {
        return {outMin, outMax};
    }

    // Every output cell sees all solids within kMaxDistanceFieldSeed of it inside this domain, and
    // values near the domain's own border can only come out too large, never too small, so the
    // output box matches a whole-cascade rebuild exactly.
    const glm::ivec3 domainMin = glm::clamp(outMin - kDistanceFieldReach, glm::ivec3(0), kGridMaxCell);
    const glm::ivec3 domainMax = glm::clamp(outMax + kDistanceFieldReach, glm::ivec3(0), kGridMaxCell);
    const glm::ivec3 dims = domainMax - domainMin + 1;
    const auto local = [&dims, &domainMin](int x, int y, int z)
    {
        return (static_cast<size_t>(y - domainMin.y) * dims.z + static_cast<size_t>(z - domainMin.z)) * dims.x +
               static_cast<size_t>(x - domainMin.x);
    };

    std::vector<float> field(static_cast<size_t>(dims.x) * dims.y * dims.z);
    for (int y = domainMin.y; y <= domainMax.y; ++y)
        for (int z = domainMin.z; z <= domainMax.z; ++z)
            for (int x = domainMin.x; x <= domainMax.x; ++x)
                field[local(x, y, z)] = static_cast<float>(distanceToSolidSeeds[VoxelAddress(x, y, z)]);

    constexpr int kChamferPassCount = 2;
    for (int passIndex = 0; passIndex < kChamferPassCount; ++passIndex)
    {
        ApplyChamferPass(field, dims, kForwardChamferNeighbors, false);
        ApplyChamferPass(field, dims, kBackwardChamferNeighbors, true);
    }

    for (int y = outMin.y; y <= outMax.y; ++y)
        for (int z = outMin.z; z <= outMax.z; ++z)
            for (int x = outMin.x; x <= outMax.x; ++x)
            {
                const float clampedDistance = glm::clamp(field[local(x, y, z)], 0.0f, 15.0f);
                VoxelData& voxel = voxels[VoxelAddress(x, y, z)];
                voxel.distanceToSolid =
                    SetPackedNibbleX(voxel.distanceToSolid, static_cast<uint8_t>(glm::floor(clampedDistance)));
            }
    return {outMin, outMax};
}
}
