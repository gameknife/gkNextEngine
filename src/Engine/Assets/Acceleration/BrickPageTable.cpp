// FCPUBrickTable: brick classification, cube-pool slot allocation and the bake queue for the
// software GI brick pool.
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

using namespace Assets;

bool FCPUBrickTable::UpdateData(const std::vector<FCPUProbeBaker>& bakers, uint32_t cascadeCapacity,
                                uint32_t poolBricksPerCascade, const glm::vec3& focus)
{
    const uint32_t BPC = static_cast<uint32_t>(Assets::GPU_SCENE_AMBIENT_BRICKS_PER_CASCADE);
    const uint32_t kInvalid = Assets::GPU_SCENE_AMBIENT_BRICK_INVALID;

    const std::vector<uint32_t> oldBrickTable = brickTable;
    const std::vector<uint8_t> oldBakePasses = bakePasses;
    brickTable.assign(static_cast<size_t>(cascadeCapacity) * BPC, kInvalid);
    bakePasses.assign(static_cast<size_t>(cascadeCapacity) * BPC, 0u);
    activeBricksPerCascade.assign(cascadeCapacity, 0u);
    candidateBricksPerCascade.assign(cascadeCapacity, 0u);
    overflowBricksPerCascade.assign(cascadeCapacity, 0u);
    slotsToClear.clear();

    const uint32_t cascadesToProcess = std::min<uint32_t>(cascadeCapacity, static_cast<uint32_t>(bakers.size()));
    uint32_t totalActive = 0;
    std::vector<uint8_t> wanted(BPC, 0);
    std::vector<uint32_t> oldOwner(poolBricksPerCascade, kInvalid);
    std::vector<uint32_t> newOwner(poolBricksPerCascade, kInvalid);
    std::vector<uint8_t> usedSlots(poolBricksPerCascade, 0u);
    std::vector<std::pair<float, uint32_t>> pending;
    pending.reserve(BPC);
    for (uint32_t c = 0; c < cascadesToProcess; ++c)
    {
        const FCPUProbeBaker& baker = bakers[c];
        const uint32_t shellCells = ResolveAmbientBrickShellCells(baker.UNIT_SIZE);

        // A brick is wanted when some empty voxel in it lies within the surface shell. Voxel storage
        // is brick-swizzled, so brick b is the contiguous run [b * volume, (b + 1) * volume).
        // Empty voxels only: a brick made entirely of solid holds no probe the gather can use.
        uint32_t candidateCount = 0;
        for (uint32_t b = 0; b < BPC; ++b)
        {
            wanted[b] = 0u;
            const size_t first = static_cast<size_t>(b) * kVoxelBrickVolume;
            if (first + kVoxelBrickVolume > baker.voxels.size())
            {
                continue;
            }
            for (uint32_t v = 0; v < kVoxelBrickVolume; ++v)
            {
                const VoxelData& voxel = baker.voxels[first + v];
                if (voxel.matId == 0 && (voxel.distanceToSolid & 0xFu) <= shellCells)
                {
                    wanted[b] = 1u;
                    ++candidateCount;
                    break;
                }
            }
        }
        candidateBricksPerCascade[c] = candidateCount;

        // Wanted bricks keep the slot they had: their probes are already baked.
        const size_t cascadeBase = static_cast<size_t>(c) * BPC;
        std::fill(oldOwner.begin(), oldOwner.end(), kInvalid);
        std::fill(newOwner.begin(), newOwner.end(), kInvalid);
        std::fill(usedSlots.begin(), usedSlots.end(), uint8_t(0));
        if (oldBrickTable.size() == brickTable.size())
        {
            for (uint32_t b = 0; b < BPC; ++b)
            {
                const uint32_t oldSlot = oldBrickTable[cascadeBase + b];
                if (oldSlot >= poolBricksPerCascade)
                {
                    continue;
                }
                oldOwner[oldSlot] = b;
                if (wanted[b])
                {
                    brickTable[cascadeBase + b] = oldSlot;
                    newOwner[oldSlot] = b;
                    usedSlots[oldSlot] = 1u;
                    if (cascadeBase + b < oldBakePasses.size())
                    {
                        bakePasses[cascadeBase + b] = oldBakePasses[cascadeBase + b];
                    }
                }
            }
        }

        // The rest take free slots nearest the focus first, so a pool too small for the whole
        // cascade spends itself where the camera is. Ties fall back to the brick index, which keeps
        // the assignment deterministic.
        pending.clear();
        const float brickSize = baker.UNIT_SIZE * static_cast<float>(kVoxelBrickEdge);
        for (uint32_t b = 0; b < BPC; ++b)
        {
            if (!wanted[b] || brickTable[cascadeBase + b] != kInvalid)
            {
                continue;
            }
            const glm::vec3 center = baker.CUBE_OFFSET + (glm::vec3(baker.WindowBrick(b)) + 0.5f) * brickSize;
            const glm::vec3 delta = center - focus;
            pending.emplace_back(glm::dot(delta, delta), b);
        }
        std::sort(pending.begin(), pending.end());

        uint32_t nextFreeSlot = 0u;
        for (const auto& entry : pending)
        {
            const uint32_t b = entry.second;
            while (nextFreeSlot < poolBricksPerCascade && usedSlots[nextFreeSlot])
            {
                ++nextFreeSlot;
            }
            if (nextFreeSlot >= poolBricksPerCascade)
            {
                ++overflowBricksPerCascade[c];
                continue;
            }
            brickTable[cascadeBase + b] = nextFreeSlot;
            newOwner[nextFreeSlot] = b;
            usedSlots[nextFreeSlot] = 1u;
            // A new owner starts the bake from zero (bakePasses was reset above).
        }

        // A slot that changed owner still holds the old owner's radiance; it is zeroed before the
        // new owner is shaded (and its zero sample count keeps it unpublished until it is rebaked).
        for (uint32_t slot = 0; slot < poolBricksPerCascade; ++slot)
        {
            if (oldOwner[slot] != newOwner[slot] && oldOwner[slot] != kInvalid)
            {
                slotsToClear.push_back(c * poolBricksPerCascade + slot);
            }
        }

        activeBricksPerCascade[c] = static_cast<uint32_t>(std::count_if(
            brickTable.begin() + static_cast<std::ptrdiff_t>(cascadeBase),
            brickTable.begin() + static_cast<std::ptrdiff_t>(cascadeBase + BPC),
            [](uint32_t slot) { return slot != Assets::GPU_SCENE_AMBIENT_BRICK_INVALID; }));
        totalActive += activeBricksPerCascade[c];
        SPDLOG_INFO("[AmbientBricks] cascade {} ({:.3f} m, shell {} cells): {} wanted, {} overflow",
                    c, baker.UNIT_SIZE, shellCells, candidateCount, overflowBricksPerCascade[c]);
    }

    activeBricksLastBuild = totalActive;
    return oldBrickTable != brickTable;
}

FAmbientBakeQueueStats FCPUBrickTable::QueueStats(uint32_t publishPasses) const
{
    FAmbientBakeQueueStats stats;
    for (size_t index = 0; index < brickTable.size() && index < bakePasses.size(); ++index)
    {
        if (brickTable[index] == Assets::GPU_SCENE_AMBIENT_BRICK_INVALID)
        {
            continue;
        }
        const uint32_t passes = std::min<uint32_t>(bakePasses[index], kAmbientBakeConvergencePasses);
        ++stats.allocatedBricks;
        stats.completedPasses += passes;
        stats.totalPasses += kAmbientBakeConvergencePasses;
        stats.pendingBricks += passes < kAmbientBakeConvergencePasses ? 1u : 0u;
        stats.unpublishedBricks += passes < publishPasses ? 1u : 0u;
    }
    return stats;
}

bool FCPUBrickTable::SelectBakeBatch(const std::vector<FCPUProbeBaker>& bakers, uint32_t maxBricks,
                                     uint32_t publishPasses, const glm::vec3& focus, FAmbientBakeBatch& outBatch) const
{
    outBatch.bricks.clear();
    const uint32_t BPC = static_cast<uint32_t>(Assets::GPU_SCENE_AMBIENT_BRICKS_PER_CASCADE);
    const uint32_t cascadeCount = std::min<uint32_t>(static_cast<uint32_t>(bakers.size()),
                                                     static_cast<uint32_t>(brickTable.size() / BPC));
    if (maxBricks == 0u || cascadeCount == 0u || bakePasses.size() != brickTable.size())
    {
        return false;
    }

    // Unpublished bricks first, so an under-budget bake shows coarser light rather than none; within
    // that, finer cascades first. Once everything is published the rest tops up the same way.
    uint32_t chosen = cascadeCount;
    for (uint32_t pass = 0; pass < 2u && chosen == cascadeCount; ++pass)
    {
        const uint32_t limit = pass == 0u ? std::min(publishPasses, kAmbientBakeConvergencePasses)
                                          : kAmbientBakeConvergencePasses;
        for (uint32_t c = 0; c < cascadeCount && chosen == cascadeCount; ++c)
        {
            for (uint32_t b = 0; b < BPC; ++b)
            {
                const size_t index = static_cast<size_t>(c) * BPC + b;
                if (brickTable[index] != Assets::GPU_SCENE_AMBIENT_BRICK_INVALID && bakePasses[index] < limit)
                {
                    chosen = c;
                    break;
                }
            }
        }
    }
    if (chosen == cascadeCount)
    {
        return false;
    }

    // Within the cascade: fewest passes first, which keeps the bricks moving together, then nearest
    // the focus; the brick index breaks ties so the order is deterministic.
    const FCPUProbeBaker& baker = bakers[chosen];
    const float brickSize = baker.UNIT_SIZE * static_cast<float>(Assets::GPU_SCENE_AMBIENT_BRICK_EDGE);
    struct FCandidate
    {
        uint32_t passes;
        float distanceSquared;
        uint32_t brick;
    };
    std::vector<FCandidate> candidates;
    for (uint32_t b = 0; b < BPC; ++b)
    {
        const size_t index = static_cast<size_t>(chosen) * BPC + b;
        if (brickTable[index] == Assets::GPU_SCENE_AMBIENT_BRICK_INVALID ||
            bakePasses[index] >= kAmbientBakeConvergencePasses)
        {
            continue;
        }
        const glm::vec3 center = baker.CUBE_OFFSET + (glm::vec3(baker.WindowBrick(b)) + 0.5f) * brickSize;
        const glm::vec3 delta = center - focus;
        candidates.push_back({bakePasses[index], glm::dot(delta, delta), b});
    }
    const size_t take = std::min<size_t>(maxBricks, candidates.size());
    std::partial_sort(candidates.begin(), candidates.begin() + static_cast<std::ptrdiff_t>(take), candidates.end(),
                      [](const FCandidate& lhs, const FCandidate& rhs)
                      {
                          return std::tie(lhs.passes, lhs.distanceSquared, lhs.brick) <
                                 std::tie(rhs.passes, rhs.distanceSquared, rhs.brick);
                      });
    outBatch.cascade = chosen;
    outBatch.bricks.reserve(take);
    for (size_t i = 0; i < take; ++i)
    {
        outBatch.bricks.push_back(candidates[i].brick);
    }
    return !outBatch.bricks.empty();
}

void FCPUBrickTable::CompleteBakeBatch(const FAmbientBakeBatch& batch)
{
    const size_t base = static_cast<size_t>(batch.cascade) * Assets::GPU_SCENE_AMBIENT_BRICKS_PER_CASCADE;
    for (uint32_t brick : batch.bricks)
    {
        const size_t index = base + brick;
        if (index < bakePasses.size() && bakePasses[index] < kAmbientBakeConvergencePasses)
        {
            ++bakePasses[index];
        }
    }
}

void FCPUBrickTable::ResetBakePasses()
{
    std::fill(bakePasses.begin(), bakePasses.end(), uint8_t(0));
}

void FCPUBrickTable::MarkConverged()
{
    bakePasses.assign(brickTable.size(), static_cast<uint8_t>(kAmbientBakeConvergencePasses));
}

void FCPUBrickTable::UploadGPU(Vulkan::DeviceMemory& deviceMemory, size_t tableByteOffset) const
{
    if (brickTable.empty())
    {
        return;
    }
    const size_t bytes = brickTable.size() * sizeof(uint32_t);
    void* mapped = deviceMemory.Map(static_cast<VkDeviceSize>(tableByteOffset), static_cast<VkDeviceSize>(bytes));
    std::memcpy(mapped, brickTable.data(), bytes);
    deviceMemory.Unmap();
}

}
