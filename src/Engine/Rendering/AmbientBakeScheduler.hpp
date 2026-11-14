#pragma once

// Dispatch controller for the optional ambient-bake implementation.

#include <algorithm>
#include <cmath>
#include <cstdint>

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
