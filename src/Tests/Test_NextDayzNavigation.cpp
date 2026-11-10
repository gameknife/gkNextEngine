#include "Engine/Common/CoreMinimal.hpp"
#include "Test_CPUAccelerationAccess.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "Gameplay/AI/NavGrid.h"
#include "Engine/Runtime/Components/TerrainComponent.hpp"
#include "Zombies/ZombieMovement.hpp"
#include "Zombies/ZombieSystem.hpp"
#include "Sim/WorldSim.hpp"

namespace
{
    // Real CPU BVH, without a renderer/window: room, doorway, roof and ledges.
    class FNavigationGeometry
    {
    public:
        Assets::CPU::FCPUAccelerationStructure collision;

        void Box(const glm::vec3& low, const glm::vec3& high)
        {
            bounds_.push_back({low, high});
            const glm::vec3 p[] = {
                {low.x, low.y, low.z}, {high.x, low.y, low.z},
                {high.x, high.y, low.z}, {low.x, high.y, low.z},
                {low.x, low.y, high.z}, {high.x, low.y, high.z},
                {high.x, high.y, high.z}, {low.x, high.y, high.z}};
            for (const auto& face : {std::array{0, 1, 2, 3}, std::array{4, 7, 6, 5},
                                     std::array{0, 4, 5, 1}, std::array{3, 2, 6, 7},
                                     std::array{0, 3, 7, 4}, std::array{1, 5, 6, 2}})
            {
                for (int index : {0, 2, 1, 0, 3, 2})
                {
                    const auto& v = p[face[index]];
                    triangles_.emplace_back(v.x, v.y, v.z, 0.0f);
                }
            }
        }

        void Build()
        {
            using namespace Assets::CPU;
            auto blasSet = std::make_shared<FCPUBLASSet>();
            blasSet->contexts.resize(bounds_.size());
            auto snapshot = std::make_shared<FCPUTLASSnapshot>();
            snapshot->blasSet = blasSet;
            for (size_t box = 0; box < bounds_.size(); ++box)
            {
                auto& blas = blasSet->contexts[box];
                blas.triangles.assign(triangles_.begin() + box * 36, triangles_.begin() + (box + 1) * 36);
                for (size_t index = 0; index < blas.triangles.size(); index += 3)
                {
                    const auto v = [&](size_t i)
                    {
                        return glm::vec3(blas.triangles[i].x, blas.triangles[i].y, blas.triangles[i].z);
                    };
                    blas.extinfos.push_back({glm::normalize(glm::cross(v(index + 1) - v(index), v(index + 2) - v(index))), 0});
                }
                blas.bvh.Build(blas.triangles.data(), static_cast<uint32_t>(blas.extinfos.size()));
                blasSet->list.push_back(&blas.bvh);
                tinybvh::BLASInstance instance;
                instance.blasIdx = static_cast<uint32_t>(box);
                const glm::mat4 identity(1.0f);
                std::memcpy(instance.transform, &identity[0], sizeof(identity));
                snapshot->instances.push_back(instance);
                FCPUTLASInstanceInfo info{};
                info.nodeId = static_cast<uint32_t>(box + 1);
                info.worldBoundsMin = bounds_[box].first;
                info.worldBoundsMax = bounds_[box].second;
                snapshot->contexts.push_back(info);
            }
            const auto count = static_cast<uint32_t>(snapshot->instances.size());
            snapshot->tlas.Build(snapshot->instances.data(), count, blasSet->list.data(), count);
            FCPUAccelerationStructureTestAccess::Publish(collision, snapshot);
        }

        glm::vec3 Move(const glm::vec3& from, const glm::vec3& to)
        {
            return NextDayz::ResolveZombieMovement(from, to, NextDayz::FZombieConfig{},
                [this](const glm::vec3& origin, const glm::vec3& direction)
                {
                    return collision.RayCastInCPU(origin, direction);
                });
        }

    private:
        std::vector<tinybvh::bvhvec4> triangles_;
        std::vector<std::pair<glm::vec3, glm::vec3>> bounds_;
    };
}

TEST_CASE("NextDayz navigation enters a roofed room through its doorway", "[Unit][NextDayz][Navigation]")
{
    FNavigationGeometry geometry;
    geometry.Box({-10.0f, -0.2f, -10.0f}, {10.0f, 0.0f, 10.0f});
    geometry.Box({-3.0f, 0.0f, 0.0f}, {3.0f, 0.15f, 6.0f});
    geometry.Box({-3.0f, 0.0f, 0.0f}, {-0.475f, 3.0f, 0.2f});
    geometry.Box({0.475f, 0.0f, 0.0f}, {3.0f, 3.0f, 0.2f});
    geometry.Box({-3.0f, 2.3f, 0.0f}, {3.0f, 3.0f, 0.2f});
    geometry.Box({-3.0f, 0.0f, 0.0f}, {-2.8f, 3.0f, 6.0f});
    geometry.Box({2.8f, 0.0f, 0.0f}, {3.0f, 3.0f, 6.0f});
    geometry.Box({-3.0f, 0.0f, 5.8f}, {3.0f, 3.0f, 6.0f});
    geometry.Box({-3.0f, 3.0f, 0.0f}, {3.0f, 3.2f, 6.0f});
    geometry.Box({-4.8f, 0.0f, 1.0f}, {-3.2f, 3.0f, 4.0f}); // solid, no interior
    geometry.Build();

    NextGameplay::FNavGridSettings settings;
    settings.cellSize = NextDayz::FZombieConfig{}.NavigationCellSize;
    settings.agentRadius = NextDayz::FZombieConfig{}.ControllerRadius;
    settings.maxStepHeight = NextDayz::FZombieConfig{}.MaxStepHeight;
    settings.clearanceHeight = NextDayz::FZombieConfig{}.ControllerHeight;
    settings.sampleFloorHeight = [](float, float) { return 0.0f; };
    settings.worldMin = {-5.0f, 0.0f, -5.0f};
    settings.worldMax = {5.0f, 0.0f, 8.0f};
    NextGameplay::FNavGrid grid;
    grid.Build(geometry.collision, settings);
    CHECK_FALSE(grid.IsWalkable({-4.0f, 0.0f, 2.0f}));
    float floor = -1.0f;
    REQUIRE(grid.SampleGroundHeight({0.25f, 0.0f, 4.25f}, floor));
    CHECK(floor == Catch::Approx(0.15f));
    const glm::vec3 start(-2.0f, 0.0f, -3.0f);
    const glm::vec3 target(0.25f, 0.15f, 4.25f);
    REQUIRE_FALSE(grid.FindPath(start, target, 0.0f).empty());

    NextDayz::ZombieSystem zombies(1);
    const auto handle = zombies.Spawn(NextDayz::EZombieProfile::Civilian, start);
    auto* zombie = zombies.Resolve(handle);
    zombie->aware = true;
    zombie->state = NextDayz::EZombieState::Investigate;
    zombie->lastKnownPlayerPosition = target;
    zombie->variant.lame = false;
    const auto path = [&](const glm::vec3& from, const glm::vec3& to) { return grid.FindPath(from, to, from.y); };
    const auto move = [&](const glm::vec3& from, const glm::vec3& to) { return geometry.Move(from, to); };
    for (int tick = 0; tick < 1200 && zombie->position.z < 3.5f; ++tick)
    {
        zombies.Update(1.0f / 60.0f, target, {}, false, {}, {}, path, move);
        CHECK(zombie->position.y <= 0.16f); // never rides the roof or door lintel
    }
    CHECK(zombie->position.z > 3.5f);
    CHECK(zombie->position.y == Catch::Approx(0.15f).margin(0.01f));
    // Walking straight at the solid front wall cannot enter the room.
    CHECK(geometry.Move({-2.0f, 0.0f, -1.0f}, {-2.0f, 0.0f, 2.0f}).z < -0.25f);
}

TEST_CASE("NextDayz movement steps over thresholds but never snaps up ledges", "[Unit][NextDayz][Navigation]")
{
    FNavigationGeometry geometry;
    geometry.Box({-10.0f, -0.2f, -10.0f}, {10.0f, 0.0f, 10.0f});
    geometry.Box({-2.0f, 0.0f, 0.0f}, {2.0f, 0.20f, 2.0f});
    geometry.Box({-2.0f, 0.0f, 4.0f}, {2.0f, 0.8f, 7.0f});
    geometry.Build();
    const auto threshold = geometry.Move({0.0f, 0.0f, -1.0f}, {0.0f, 0.0f, 1.0f});
    CHECK(threshold.z == Catch::Approx(1.0f));
    CHECK(threshold.y == Catch::Approx(0.20f));
    for (float travel : {0.1f, 1.0f, 5.0f})
    {
        const auto ledge = geometry.Move({0.0f, 0.0f, 3.0f}, {0.0f, 0.0f, 3.0f + travel});
        CHECK(ledge.z < 3.73f);
        CHECK(ledge.y == Catch::Approx(0.0f).margin(0.001f));
    }
}

TEST_CASE("NextDayz ground sampling preserves high bridge decks over water", "[Unit][NextDayz][Navigation]")
{
    FNavigationGeometry geometry;
    geometry.Box({-10.0f, -3.2f, -4.0f}, {10.0f, -3.0f, 4.0f});
    geometry.Box({-10.0f, -3.0f, -4.0f}, {-3.0f, 2.5f, 4.0f});
    geometry.Box({3.0f, -3.0f, -4.0f}, {10.0f, 2.5f, 4.0f});
    geometry.Box({-3.5f, 2.3f, -1.0f}, {3.5f, 2.5f, 1.0f});
    geometry.Build();
    NextGameplay::FNavGridSettings settings;
    settings.cellSize = NextDayz::FZombieConfig{}.NavigationCellSize;
    settings.agentRadius = NextDayz::FZombieConfig{}.ControllerRadius;
    settings.maxStepHeight = NextDayz::FZombieConfig{}.MaxStepHeight;
    settings.sampleFloorHeight = [](float x, float) { return std::abs(x) < 3.0f ? 50.0f : 2.5f; };
    settings.worldMin = {-8.0f, 0.0f, -3.0f};
    settings.worldMax = {8.0f, 0.0f, 3.0f};
    NextGameplay::FNavGrid grid;
    grid.Build(geometry.collision, settings);
    grid.MaskUnwalkable([](const glm::vec3& p) { return std::abs(p.x) < 3.0f && p.y < 0.05f; });
    REQUIRE_FALSE(grid.FindPath({-6.0f, 2.5f, 0.0f}, {6.0f, 2.5f, 0.0f}, 2.5f).empty());
    float height = 0.0f;
    REQUIRE(grid.SampleGroundHeight({0.0f, 2.5f, 0.0f}, height));
    CHECK(height == Catch::Approx(2.5f));
    CHECK_FALSE(grid.IsWalkable({0.0f, 0.0f, 2.0f}));
}

TEST_CASE("Navigation windows reuse overlap and match a full rebuild", "[Unit][NextDayz][Navigation]")
{
    FNavigationGeometry geometry;
    geometry.Box({-30.0f, -0.2f, -30.0f}, {30.0f, 0.0f, 30.0f});
    geometry.Box({0.0f, 0.0f, -1.0f}, {2.0f, 2.5f, 1.0f});
    geometry.Build();
    NextGameplay::FNavGridSettings settings;
    settings.cellSize = 0.35f;
    settings.agentRadius = 0.6f; // also exercise erosion at the old window edge
    settings.sampleFloorHeight = [](float, float) { return 0.0f; };
    settings.isUnwalkable = [](const glm::vec3& p) { return p.x < -0.5f && p.z > 1.0f; };
    settings.maxSamplingWorkers = 2;
    settings.worldMin = {-5.0f, 0.0f, -5.0f};
    settings.worldMax = {5.0f, 0.0f, 5.0f};
    NextGameplay::FNavGrid previous;
    previous.Build(geometry.collision, settings);
    for (const auto shift : {glm::ivec2(3, 0), {-4, 0}, {0, 5}, {0, -3}, {3, -2}, {-2, 4}, {0, 0}, {40, 0}})
    {
        INFO("shift " << shift.x << ", " << shift.y);
        auto moved = settings;
        const glm::vec3 offset(shift.x * settings.cellSize, 0.0f, shift.y * settings.cellSize);
        moved.worldMin += offset;
        moved.worldMax += offset;
        NextGameplay::FNavGrid incremental;
        const size_t sampled = incremental.BuildShifted(geometry.collision, moved, previous);
        const int width = previous.GetWidth();
        const int height = previous.GetHeight();
        const size_t overlap = static_cast<size_t>(std::max(0, width - std::abs(shift.x))) *
                               std::max(0, height - std::abs(shift.y));
        CHECK(sampled == static_cast<size_t>(width) * height - overlap);
        NextGameplay::FNavGrid full;
        full.Build(geometry.collision, moved);
        REQUIRE(incremental.GetWidth() == full.GetWidth());
        REQUIRE(incremental.GetHeight() == full.GetHeight());
        for (int z = 0; z < full.GetHeight(); ++z)
        {
            for (int x = 0; x < full.GetWidth(); ++x)
            {
                CHECK(incremental.IsCellWalkable(x, z) == full.IsCellWalkable(x, z));
                CHECK(incremental.GetCellWorldPosition(x, z).y ==
                      Catch::Approx(full.GetCellWorldPosition(x, z).y).margin(0.001f));
            }
        }
    }
    // Changing agent dimensions must invalidate cached geometry samples.
    auto changed = settings;
    changed.agentRadius = 0.3f;
    NextGameplay::FNavGrid invalidated;
    CHECK(invalidated.BuildShifted(geometry.collision, changed, previous) ==
          static_cast<size_t>(previous.GetWidth()) * previous.GetHeight());
}

TEST_CASE("Terrain navigation shortcut matches geometry near obstacles and window edges", "[Unit][NextDayz][Navigation]")
{
    FNavigationGeometry geometry;
    geometry.Box({-10.0f, -0.2f, -10.0f}, {10.0f, 0.0f, 10.0f});
    geometry.Box({-0.1f, 0.0f, -1.0f}, {0.1f, 3.0f, 1.0f});
    geometry.Box({2.0f, 0.0f, 2.0f}, {4.0f, 0.2f, 4.0f});
    geometry.Build();
    auto data = std::make_shared<Runtime::TerrainComponent::FGridData>();
    data->cellsX = data->cellsY = 1;
    data->sizeX = data->sizeY = 20.0;
    data->verts = {{-10, -10, 0}, {10, -10, 0}, {-10, 10, 0}, {10, 10, 0}};
    data->diagFlip = {0};
    data->flags = {0};
    data->localToWorld = glm::dmat4(0.0);
    data->localToWorld[0] = {1, 0, 0, 0};
    data->localToWorld[1] = {0, 0, -1, 0};
    data->localToWorld[2] = {0, 1, 0, 0};
    data->localToWorld[3] = {0, 0, 0, 1};
    data->worldToLocal = glm::inverse(data->localToWorld);
    Runtime::TerrainComponent terrain;
    terrain.SetGridData(data);
    NextGameplay::FNavGridSettings settings;
    settings.cellSize = 0.35f;
    settings.agentRadius = 0.28f;
    settings.maxStepHeight = 0.25f;
    settings.maxSamplingWorkers = 2;
    settings.sampleFloorHeight = [](float, float) { return 0.0f; };
    settings.worldMin = {-11.0f, 0.0f, -11.0f};
    settings.worldMax = {11.0f, 0.0f, 11.0f};
    NextGameplay::FNavGrid exact;
    exact.Build(geometry.collision, settings);
    settings.terrain = &terrain;
    settings.terrainInstanceIds = {1};
    NextGameplay::FNavGrid accelerated;
    accelerated.Build(geometry.collision, settings);
    CHECK(accelerated.GeometrySampledCellCount() < accelerated.SampledCellCount() / 10);
    for (int z = 0; z < exact.GetHeight(); ++z)
        for (int x = 0; x < exact.GetWidth(); ++x)
        {
            CHECK(accelerated.IsCellWalkable(x, z) == exact.IsCellWalkable(x, z));
            if (exact.IsCellWalkable(x, z))
            {
                CHECK(accelerated.GetCellWorldPosition(x, z).y ==
                      Catch::Approx(exact.GetCellWorldPosition(x, z).y).margin(0.001f));
            }
        }
}

TEST_CASE("NextDayz world sim keeps one navigation window per survivor cluster",
          "[Unit][NextDayz][Navigation][Multiplayer]")
{
    FNavigationGeometry geometry;
    geometry.Box({-150.0f, -0.2f, -150.0f}, {150.0f, 0.0f, 150.0f});
    geometry.Box({1.0f, 0.0f, 4.0f}, {3.0f, 2.0f, 6.0f}); // something to walk around
    geometry.Build();

    NextDayz::FZombieConfig config;
    config.NavigationHalfSize = 15.0f;
    config.NavigationRecenterDistance = 6.0f;
    config.NavigationCellSize = 0.5f;
    config.NavigationWorkers = 1;
    NextDayz::WorldSim world;
    world.Configure(config);
    world.UpsertSurvivor({1, {0.0f, 0.0f, 0.0f}, {0.0f, 1.6f, 0.0f}});
    world.UpsertSurvivor({2, {100.0f, 0.0f, 0.0f}, {100.0f, 1.6f, 0.0f}});

    // Two survivors far apart: a window each, built at load.
    world.BuildNavigation(geometry.collision, nullptr, false, false);
    REQUIRE(world.NavigationWindowCount() == 2);
    REQUIRE(world.NavigationReady());
    CHECK_FALSE(world.FindPath({0.5f, 0.0f, 0.5f}, {2.0f, 0.0f, 9.0f}).empty());
    CHECK(world.NavigationAt({50.0f, 0.0f, 0.0f}) == nullptr);
    CHECK_FALSE(world.IsNavigable({50.0f, 0.0f, 0.0f}));
    CHECK(world.IsNavigable({100.0f, 0.0f, 3.0f}));

    // Survivor 2 walks into survivor 1's window: its own window goes.
    world.UpsertSurvivor({2, {3.0f, 0.0f, 0.0f}, {3.0f, 1.6f, 0.0f}});
    world.TickSpawns(0.01f);
    CHECK(world.NavigationWindowCount() == 1);

    // Survivor 1 leaves: the window stays for survivor 2, who is inside it.
    world.RemoveSurvivor(1);
    world.TickSpawns(0.01f);
    REQUIRE(world.NavigationWindowCount() == 1);
    CHECK(world.Navigation().IsBuilt());

    // Its new owner walks off: the window follows.
    world.UpsertSurvivor({2, {40.0f, 0.0f, 0.0f}, {40.0f, 1.6f, 0.0f}});
    world.TickSpawns(0.01f);
    world.JoinNavigation();
    CHECK(world.NavigationWindowCount() == 1);
    CHECK(world.NavigationAt({40.0f, 0.0f, 10.0f}) != nullptr);
    CHECK(world.NavigationAt({0.0f, 0.0f, 0.0f}) == nullptr);

    // A newcomer far away gets a window of their own, built in the background.
    world.UpsertSurvivor({3, {-100.0f, 0.0f, 0.0f}, {-100.0f, 1.6f, 0.0f}});
    world.TickSpawns(0.01f);
    CHECK(world.NavigationWindowCount() == 2);
    world.JoinNavigation();
    CHECK(world.NavigationReady());
    CHECK(world.IsNavigable({-100.0f, 0.0f, 5.0f}));
}
