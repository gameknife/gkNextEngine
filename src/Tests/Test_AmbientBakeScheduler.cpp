#include "Engine/Rendering/AmbientBakeScheduler.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Ambient bake scheduler spends GPU slack on baking", "[Unit][AmbientBake]")
{
    SECTION("vblank time is not charged to the bake budget")
    {
        // A 60 Hz display period is 16.7 ms, but only 8 ms of this frame is GPU work outside
        // baking. The remaining GPU idle time should be available to the bake.
        CHECK(Vulkan::AmbientBake::PlanNextDispatchGroups(4, 1.0, 8.0, 60) > 4u);
    }

    SECTION("a busy foreground frame slows but never starves baking")
    {
        CHECK(Vulkan::AmbientBake::PlanNextDispatchGroups(16, 1.0, 30.0, 60) < 16u);
        CHECK(Vulkan::AmbientBake::PlanNextDispatchGroups(1, 1.0, 30.0, 60) == 1u);
    }

    SECTION("target-rate headroom still permits work at the exact target")
    {
        // At 60 FPS, the 5% QoS headroom is enough for a sub-millisecond bake group even when
        // the foreground renderer alone exactly fills its nominal 16.7 ms budget.
        CHECK(Vulkan::AmbientBake::PlanNextDispatchGroups(1, 0.25, 1000.0 / 60.0, 60) > 1u);
    }

    SECTION("target FPS controls the desired frame budget")
    {
        const uint32_t atTenFps = Vulkan::AmbientBake::PlanNextDispatchGroups(8, 1.0, 10.0, 10);
        const uint32_t atSixtyFps = Vulkan::AmbientBake::PlanNextDispatchGroups(8, 1.0, 10.0, 60);
        CHECK(atTenFps > atSixtyFps);
    }
}

TEST_CASE("Ambient bake keeps a share of an over-budget frame while probes are unpublished",
          "[Unit][AmbientBakeScheduler]")
{
    // A 25 ms frame is over a 60 FPS target, so the regular plan falls to the one-group minimum ...
    CHECK(Vulkan::AmbientBake::PlanNextDispatchGroups(1u, 0.002, 25.0, 60u) ==
          Vulkan::AmbientBake::minGroupsPerFrame);
    // ... while the publish floor still grants a tenth of the frame.
    CHECK(Vulkan::AmbientBake::PublishFloorGroups(0.002, 25.0) == 1250u);
    // Without measurements the floor is the plain minimum.
    CHECK(Vulkan::AmbientBake::PublishFloorGroups(0.0, 25.0) == Vulkan::AmbientBake::minGroupsPerFrame);
    CHECK(Vulkan::AmbientBake::PublishFloorGroups(0.002, 0.0) == Vulkan::AmbientBake::minGroupsPerFrame);
}
