#pragma once

// Byte layout of the ambient arena and the memory budget that decides how many cascades it gets.
// The device-capability probes (Engine, VulkanBaseRenderer) and the arena allocation (Scene) all
// size the arena here, so the cascade count they agree on is the one that is actually allocated.

#include "Engine/Assets/GPU/UniformBuffer.hpp"

#include <vulkan/vulkan.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Assets
{
    inline constexpr VkDeviceSize kAmbientVoxelsPerCascade =
        static_cast<VkDeviceSize>(CUBE_SIZE_XY) * CUBE_SIZE_XY * CUBE_SIZE_Z;

    // Cubes and Voxels scale with the cascade capacity; CubesPong holds one cascade's pool (the
    // cascade being baked); the brick table and the active brick list are per cascade.
    struct FAmbientArenaLayout
    {
        VkDeviceSize cubesOffset;
        VkDeviceSize voxelsOffset;
        VkDeviceSize pongOffset;
        VkDeviceSize brickTableOffset;
        VkDeviceSize activeBrickListOffset;
        VkDeviceSize totalSize;
    };

    // poolBricksPerCascade sizes the sparse cube pool (Cubes) and its ping-pong copy (CubesPong);
    // Voxels stay dense. With poolBricksPerCascade == BRICKS_PER_CASCADE the pool equals the grid.
    constexpr FAmbientArenaLayout ComputeAmbientArenaLayout(uint32_t cascadeCapacity, uint32_t poolBricksPerCascade)
    {
        const VkDeviceSize poolCubesPerCascade =
            static_cast<VkDeviceSize>(poolBricksPerCascade) * GPU_SCENE_AMBIENT_BRICK_VOLUME;
        FAmbientArenaLayout layout{};
        layout.cubesOffset = 0;
        layout.voxelsOffset = layout.cubesOffset +
            static_cast<VkDeviceSize>(GPU_SCENE_AMBIENT_CUBE_SIZE) * poolCubesPerCascade * cascadeCapacity;
        layout.pongOffset = layout.voxelsOffset +
            static_cast<VkDeviceSize>(GPU_SCENE_VOXEL_DATA_SIZE) * kAmbientVoxelsPerCascade * cascadeCapacity;
        layout.brickTableOffset = layout.pongOffset +
            static_cast<VkDeviceSize>(GPU_SCENE_AMBIENT_CUBE_SIZE) * poolCubesPerCascade;
        layout.activeBrickListOffset = layout.brickTableOffset +
            sizeof(uint32_t) * static_cast<VkDeviceSize>(GPU_SCENE_AMBIENT_BRICKS_PER_CASCADE) * cascadeCapacity;
        layout.totalSize = layout.activeBrickListOffset +
            sizeof(uint32_t) * static_cast<VkDeviceSize>(poolBricksPerCascade) * cascadeCapacity;
        return layout;
    }

    inline uint32_t ResolveAmbientPoolBricksPerCascade(float poolBrickRatio)
    {
        const float clampedRatio = std::clamp(poolBrickRatio, 0.0f, 1.0f);
        const auto requestedBricks = static_cast<uint32_t>(
            std::ceil(static_cast<float>(GPU_SCENE_AMBIENT_BRICKS_PER_CASCADE) * clampedRatio));
        return std::clamp(requestedBricks, 1u, static_cast<uint32_t>(GPU_SCENE_AMBIENT_BRICKS_PER_CASCADE));
    }

    // Share of the largest device-local heap the arena may take. A discrete GPU's heap is its own
    // VRAM, which the arena may fill as it always could. A phone reports most of the shared system
    // memory as one device-local heap, which the OS, the app and every other GPU resource also
    // live in, so the arena is held to an eighth of it there.
#if IOS || ANDROID
    inline constexpr double kAmbientArenaHeapShare = 0.125;
#else
    inline constexpr double kAmbientArenaHeapShare = 1.0;
#endif

    inline VkDeviceSize LargestDeviceLocalHeapBytes(VkPhysicalDevice physicalDevice)
    {
        VkPhysicalDeviceMemoryProperties memoryProperties = {};
        vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);
        VkDeviceSize largest = 0;
        for (uint32_t heapIndex = 0; heapIndex < memoryProperties.memoryHeapCount; ++heapIndex)
        {
            if ((memoryProperties.memoryHeaps[heapIndex].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0)
            {
                largest = std::max(largest, memoryProperties.memoryHeaps[heapIndex].size);
            }
        }
        return largest;
    }

    inline VkDeviceSize AmbientArenaBudgetBytes(VkDeviceSize largestDeviceLocalHeap)
    {
        return static_cast<VkDeviceSize>(static_cast<double>(largestDeviceLocalHeap) * kAmbientArenaHeapShare);
    }

    // The most cascades, up to `requested`, whose arena fits the budget; 0 when not even one does
    // (the device then only gets the no-ambient renderers). Fewer cascades never coarsen the finest
    // grid: a fixed-anchor layout reaches less far, a fit-to-scene layout spreads a larger ratio.
    inline uint32_t FitAmbientCascadeCapacity(uint32_t requested, uint32_t poolBricksPerCascade, VkDeviceSize budget)
    {
        for (uint32_t count = std::min(requested, static_cast<uint32_t>(CUBE_CASCADE_MAX)); count > 0u; --count)
        {
            if (ComputeAmbientArenaLayout(count, poolBricksPerCascade).totalSize <= budget)
            {
                return count;
            }
        }
        return 0u;
    }
}
