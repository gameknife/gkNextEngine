// AmbientBakeBatch: the small types the ambient bake queue shares between the CPU brick table
// (CPUAccelerationStructure) and the renderer that dispatches the bake.
#pragma once
#include <cstdint>
#include <vector>

namespace Assets::CPU
{
// Bake passes after which a brick leaves the bake queue. The GPU keeps averaging past this (its
// accumulation weight is capped at 1/255), but further passes no longer change what is visible.
inline constexpr uint32_t kAmbientBakeConvergencePasses = 32u;

// One frame of ambient bake work: a set of storage bricks of one cascade.
struct FAmbientBakeBatch
{
    uint32_t cascade = 0;
    std::vector<uint32_t> bricks;
    // Brick-table generation the batch was taken from. A batch spread over several frames is
    // dropped once the table it indexes has been rebuilt.
    uint64_t generation = 0;
};

struct FAmbientBakeQueueStats
{
    uint32_t allocatedBricks = 0;
    // Allocated bricks with fewer than kAmbientBakeConvergencePasses passes.
    uint32_t pendingBricks = 0;
    // Pending bricks still below the publish threshold, which the gather does not use yet.
    uint32_t unpublishedBricks = 0;
    uint64_t completedPasses = 0;
    uint64_t totalPasses = 0;
};
}
