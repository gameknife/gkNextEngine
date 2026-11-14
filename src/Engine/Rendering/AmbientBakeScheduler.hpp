#pragma once

// Dispatch controller for the optional ambient-bake implementation.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

namespace Vulkan::AmbientBake
{
    inline constexpr uint32_t minGroupsPerFrame = 1u;
    inline constexpr uint32_t maxGroupsPerFrame = 1u << 20u;

    // Keep a little headroom for progressive baking even when the foreground renderer itself is
    // right on the requested rate. The target is a quality-of-service goal, not a hard cap, and
    // this turns a 60 FPS target into a 17.5 ms GPU budget rather than a strict 16.7 ms limit.
    inline constexpr double targetFrameTimeHeadroom = 1.05;

    // While probes near the camera are unpublished (a window just scrolled), the bake keeps at least
    // this share of the frame even when the frame is already over budget. Otherwise a scene that
    // runs below the target rate would fall to one group per frame and never publish what it scrolled
    // in. At about 2 us a group this is a few thousand groups on a 25 ms frame.
    inline constexpr double publishMinFrameShare = 0.1;

    inline uint32_t PublishFloorGroups(double millisecondsPerGroup, double nonBakeMilliseconds)
    {
        if (!std::isfinite(millisecondsPerGroup) || millisecondsPerGroup <= 0.0 ||
            !std::isfinite(nonBakeMilliseconds) || nonBakeMilliseconds <= 0.0)
        {
            return minGroupsPerFrame;
        }
        const double groups = std::floor(nonBakeMilliseconds * publishMinFrameShare / millisecondsPerGroup);
        return static_cast<uint32_t>(std::clamp(groups, static_cast<double>(minGroupsPerFrame),
                                                static_cast<double>(maxGroupsPerFrame)));
    }

    // What the stable pong copy of the cube pool holds. The bake of cascade c reads its own cascade
    // from pong while writing ping, so pong must equal ping for that cascade before every dispatch.
    // Copying the whole cascade (tens of MB) each time is the simple way; when the previous dispatch
    // baked the same cascade against the same brick table, only the slots it wrote can differ.
    struct FPongSyncState
    {
        bool valid = false;
        uint32_t cascade = 0;
        uint64_t generation = 0;
        uint64_t arena = 0;
        std::vector<uint32_t> dirtySlots;
    };

    // True when the whole cascade must be copied. Otherwise `runs` gets (first slot, slot count)
    // ranges covering every dirty slot, adjacent slots merged into one copy region.
    inline bool PlanPongSync(const FPongSyncState& state, uint32_t cascade, uint64_t generation, uint64_t arena,
                             std::vector<std::pair<uint32_t, uint32_t>>& runs)
    {
        runs.clear();
        if (!state.valid || state.cascade != cascade || state.generation != generation || state.arena != arena)
        {
            return true;
        }
        std::vector<uint32_t> slots = state.dirtySlots;
        std::sort(slots.begin(), slots.end());
        slots.erase(std::unique(slots.begin(), slots.end()), slots.end());
        for (uint32_t slot : slots)
        {
            if (!runs.empty() && runs.back().first + runs.back().second == slot)
            {
                ++runs.back().second;
            }
            else
            {
                runs.emplace_back(slot, 1u);
            }
        }
        return false;
    }

    // Plan from GPU timestamp measurements. Wall-clock frame time is deliberately not used: in a
    // FIFO swapchain it contains the driver's vblank wait, which is idle GPU time that baking can
    // safely use. nonBakeMilliseconds is the previous frame's total GPU duration minus its bake.
    inline uint32_t PlanNextDispatchGroups(uint32_t currentGroups, double millisecondsPerGroup,
                                           double nonBakeMilliseconds, uint32_t targetFps)
    {
        const uint32_t current = std::clamp(currentGroups, minGroupsPerFrame, maxGroupsPerFrame);
        if (!std::isfinite(millisecondsPerGroup) || millisecondsPerGroup <= 0.0 ||
            !std::isfinite(nonBakeMilliseconds) || nonBakeMilliseconds < 0.0 || targetFps == 0u)
        {
            return current;
        }

        const double targetMilliseconds = 1000.0 / static_cast<double>(targetFps) * targetFrameTimeHeadroom;
        // Always retain one group: a busy foreground frame must slow baking down, not starve it.
        const double bakeBudgetMilliseconds = std::max(0.0, targetMilliseconds - nonBakeMilliseconds);
        const double requestedGroups = std::min(
            static_cast<double>(maxGroupsPerFrame),
            std::floor(bakeBudgetMilliseconds / millisecondsPerGroup));
        const uint32_t proposed = std::max(
            minGroupsPerFrame,
            static_cast<uint32_t>(requestedGroups));

        const uint32_t lower = std::max(minGroupsPerFrame, current / 2u);
        const uint32_t upper = current > maxGroupsPerFrame / 2u
            ? maxGroupsPerFrame
            : std::max(minGroupsPerFrame, current * 2u);
        return std::clamp(proposed, lower, upper);
    }
}
