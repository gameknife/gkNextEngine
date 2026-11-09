#include "Engine/Common/CoreMinimal.hpp"
#include "Application/Game/NextDayz/Sim/WorldSim.hpp"

#include "Application/Game/NextDayz/Zombies/ZombieMovement.hpp"
#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Runtime/Components/TerrainComponent.hpp"

#include <glm/geometric.hpp>

namespace NextDayz
{
    WorldSim::~WorldSim()
    {
        JoinNavigation();
    }

    void WorldSim::Configure(const FZombieConfig& config)
    {
        config_ = config;
        zombies.Configure(config);
    }

    void WorldSim::ResetPopulation(uint64_t spawnSeed)
    {
        zombies.Reset();
        combat.Reset();
        noise.Reset();
        spawns.Reset(spawnSeed);
        ConfigureSpawnPoints();
    }

    void WorldSim::ConfigureSpawnPoints()
    {
        std::vector<FZombieSpawnPoint> points;
        for (const FWorldAnchorHandle handle : anchors.Find(EWorldAnchorType::Zombie))
        {
            const FWorldAnchor* anchor = anchors.Resolve(handle);
            if (!anchor)
            {
                continue;
            }
            EZombieProfile profile = EZombieProfile::Civilian;
            if (anchor->profile == "military") profile = EZombieProfile::Military;
            else if (anchor->profile == "industrial") profile = EZombieProfile::Industrial;
            else if (anchor->profile == "wilderness") profile = EZombieProfile::Wilderness;
            FZombieSpawnPoint point{anchor->worldPos, profile};
            point.groupSize = anchor->group;
            points.push_back(point);
        }
        spawns.SetPoints(std::move(points));
    }

    void WorldSim::UpsertSurvivor(const FSurvivorPresence& survivor)
    {
        for (FSurvivorPresence& existing : survivors_)
        {
            if (existing.id == survivor.id)
            {
                existing = survivor;
                return;
            }
        }
        survivors_.push_back(survivor);
    }

    void WorldSim::RemoveSurvivor(uint32_t id)
    {
        std::erase_if(survivors_, [id](const FSurvivorPresence& survivor) { return survivor.id == id; });
    }

    const FSurvivorPresence* WorldSim::FindSurvivor(uint32_t id) const
    {
        for (const FSurvivorPresence& survivor : survivors_)
        {
            if (survivor.id == id) return &survivor;
        }
        return nullptr;
    }

    bool WorldSim::InView(const glm::vec3& eye, const glm::vec3& forward, const glm::vec3& position)
    {
        const glm::vec3 delta = position - eye;
        const float distance = glm::length(delta);
        return distance > 0.01f && distance < 150.0f && glm::dot(delta / distance, forward) > 0.45f;
    }

    bool WorldSim::VisibleToAnySurvivor(const glm::vec3& position) const
    {
        return std::any_of(survivors_.begin(), survivors_.end(), [&](const FSurvivorPresence& survivor)
        {
            return InView(survivor.eye, survivor.forward, position);
        });
    }

    Assets::RayCastResult WorldSim::RayCast(const glm::vec3& origin, const glm::vec3& direction) const
    {
        return rayCast_ ? rayCast_(origin, direction) : Assets::RayCastResult{};
    }

    void WorldSim::TickSpawns(float deltaSeconds)
    {
        UpdateNavigation();
        std::vector<glm::vec3> feet;
        feet.reserve(survivors_.size());
        for (const FSurvivorPresence& survivor : survivors_) feet.push_back(survivor.feet);
        spawns.UpdateForSurvivors(deltaSeconds, feet, zombies,
                                  [this](const glm::vec3& point) { return VisibleToAnySurvivor(point); },
                                  [this](const glm::vec3& point) { return IsNavigable(point); });
    }

    void WorldSim::TickInfected(float deltaSeconds)
    {
        std::vector<FZombieTarget> targets;
        targets.reserve(survivors_.size());
        for (const FSurvivorPresence& survivor : survivors_)
        {
            targets.push_back({survivor.id, survivor.feet, survivor.alive, survivor.sightScale});
        }
        const auto lineOfSight = [this](const glm::vec3& from, const glm::vec3& to)
        {
            const glm::vec3 delta = to - from;
            const float distance = glm::length(delta);
            if (distance <= 0.01f)
            {
                return true;
            }
            const Assets::RayCastResult result = RayCast(from, delta / distance);
            return !(result.Hit && result.T < distance - 0.35f);
        };
        const auto resolvePath = [this](const glm::vec3& from, const glm::vec3& to)
        {
            if (!navGrid_.IsBuilt())
            {
                return std::vector<glm::vec3>{to};
            }
            return navGrid_.FindPath(from, to, from.y, config_.MaxPathExpansions);
        };
        const auto resolveMovement = [this](const glm::vec3& from, const glm::vec3& to)
        {
            const glm::vec3 result = ResolveZombieMovement(from, to, config_,
                [this](const glm::vec3& origin, const glm::vec3& direction) { return RayCast(origin, direction); });
            // Keep the semantic water veto even for close-range steering/pushes.
            return IsNavigable(result) ? result : from;
        };
        zombies.Update(deltaSeconds, targets, lineOfSight, noise.Events(), resolvePath, resolveMovement);
    }

    void WorldSim::BuildNavigation(Assets::Scene& scene, bool background, bool adaptToTerrain)
    {
        navScene_ = &scene;
        navAdaptToTerrain_ = adaptToTerrain;
        NextGameplay::FNavGridSettings settings;
        settings.cellSize = config_.NavigationCellSize;
        // Loading may use more cores; the live window update keeps a small budget.
        settings.maxSamplingWorkers = background ? config_.NavigationWorkers : 8;
        settings.agentRadius = config_.ControllerRadius;
        settings.maxSlopeAngle = 48.0f;
        settings.clearanceHeight = config_.ControllerHeight;
        settings.maxStepHeight = config_.MaxStepHeight;
        settings.sampleCeiling = 100.0f;
        settings.floorHeightTolerance = 2.0f;
        const float halfSize = config_.NavigationHalfSize;
        navCenter_ = survivors_.empty() ? glm::vec3(0.0f) : survivors_.front().feet;
        // Keep lattice coordinates stable as the window moves, including doorways
        // sitting on a cell boundary. Existing waypoints remain world positions.
        navCenter_.x = std::floor(navCenter_.x / settings.cellSize) * settings.cellSize;
        navCenter_.z = std::floor(navCenter_.z / settings.cellSize) * settings.cellSize;
        settings.worldMin = {navCenter_.x - halfSize, 0.0f, navCenter_.z - halfSize};
        settings.worldMax = {navCenter_.x + halfSize, 0.0f, navCenter_.z + halfSize};
        Runtime::TerrainComponent* terrain = nullptr;
        for (Runtime::TerrainComponent* candidate : scene.Components<Runtime::TerrainComponent>())
        {
            terrain = candidate;
            break;
        }
        if (terrain && adaptToTerrain)
        {
            float lowest = std::numeric_limits<float>::max();
            float highest = -lowest;
            for (int y = 0; y <= 12; ++y)
                for (int x = 0; x <= 12; ++x)
                {
                    const float height = terrain->SampleHeight(settings.worldMin.x + x * halfSize / 6.0f,
                                                                settings.worldMin.z + y * halfSize / 6.0f);
                    lowest = std::min(lowest, height);
                    highest = std::max(highest, height);
                }
            settings.sampleCeiling = highest + 60.0f;
            settings.floorHeightTolerance = highest - lowest + 12.0f;
        }
        if (terrain)
        {
            settings.terrain = terrain;
            const auto collectTerrainIds = [&](auto&& self, const Assets::Node* node) -> void
            {
                if (!node) return;
                settings.terrainInstanceIds.push_back(node->GetInstanceId());
                for (const auto& child : node->Children()) self(self, child.get());
            };
            collectTerrainIds(collectTerrainIds, terrain->GetOwner());
            const float waterProbeHeight = settings.sampleCeiling - settings.floorSearchHeight;
            settings.sampleFloorHeight = [terrain, waterProbeHeight](float x, float z)
            {
                // Over water retain the top deck, including bridges several
                // metres above the river. Land samples must start below roofs.
                return terrain->IsWater(x, z)
                    ? waterProbeHeight
                    : terrain->SampleHeight(x, z);
            };
            settings.isUnwalkable = [terrain](const glm::vec3& point)
            {
                return terrain->IsWater(point.x, point.z) &&
                       point.y < terrain->WaterSurface(point.x, point.z) + 0.05f;
            };
        }
        auto* collision = &scene.GetCPUAccelerationStructure();
        // The active grid is immutable until this single worker has completed;
        // scene unload also joins it. Reuse the other buffer entirely on the
        // worker, including allocation/copying and disposal of old cell storage.
        const auto* previous = background ? &navGrid_ : nullptr;
        auto build = [collision, settings, previous, grid = std::move(navSpare_)]() mutable
        {
            const auto buildStart = std::chrono::steady_clock::now();
            if (previous)
            {
                grid.BuildShifted(*collision, settings, *previous);
            }
            else
            {
                grid.Build(*collision, settings);
            }
            const double buildMs =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - buildStart).count();
            SPDLOG_INFO("[NextDayz] infected navigation ready: {}x{} cells at {:.2f} m, sampled {} cells ({} geometry), {} workers in {:.1f} ms",
                        grid.GetWidth(), grid.GetHeight(), grid.GetCellSize(), grid.SampledCellCount(),
                        grid.GeometrySampledCellCount(), settings.maxSamplingWorkers, buildMs);
            return FNavigationBuild{std::move(grid), buildMs};
        };
        if (background)
        {
            navBuild_ = std::async(std::launch::async, std::move(build));
        }
        else
        {
            ApplyNavigation(build());
        }
    }

    void WorldSim::ApplyNavigation(FNavigationBuild result)
    {
        const auto start = std::chrono::steady_clock::now();
        std::swap(navGrid_, result.grid);
        navSpare_ = std::move(result.grid);
        navBuildMs_ = result.elapsedMs;
        ++navBuildCount_;
        navApplyPeakMs_ = std::max(navApplyPeakMs_,
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
    }

    void WorldSim::UpdateNavigation()
    {
        if (navBuild_.valid())
        {
            if (navBuild_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
            ApplyNavigation(navBuild_.get());
        }
        if (!navScene_ || survivors_.empty())
        {
            return;
        }
        const glm::vec3 offset = survivors_.front().feet - navCenter_;
        if (std::max(std::abs(offset.x), std::abs(offset.z)) >= config_.NavigationRecenterDistance)
        {
            // Retain the current grid throughout the build so a chase does not
            // pause just because the survivor crossed a navigation window boundary.
            BuildNavigation(*navScene_, true, navAdaptToTerrain_);
        }
    }

    void WorldSim::JoinNavigation()
    {
        // The worker samples the scene's immutable geometry and terrain: join it
        // before either is destroyed or a different scene is installed.
        if (navBuild_.valid()) navBuild_.get();
    }

    void WorldSim::ClearNavigation()
    {
        JoinNavigation();
        navScene_ = nullptr;
        navGrid_ = {};
        navSpare_ = {};
    }
}
