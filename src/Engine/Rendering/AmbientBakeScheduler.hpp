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
