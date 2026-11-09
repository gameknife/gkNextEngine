#pragma once

// ============================================================================
// WorldSim.hpp - The authoritative world of one NextDayz map, independent of
// how many survivors walk it: infected population (spawns, AI, combat), noise,
// world anchors and the infected navigation grid. Survivors enter it only as
// FSurvivorPresence each frame; what happens to them comes back as events
// (ConsumeSurvivorHits). No rendering, UI, audio or input here: the single-
// player client and (later) the dedicated server drive the same object.
// See docs/projects/nextdayz/nextdayz-multiplayer-design.md.
// ============================================================================

#include "Engine/Common/CoreMinimal.hpp"

#include <glm/vec3.hpp>

#include <future>
#include <span>

#include "Application/Game/NextDayz/NextDayzConfig.hpp"
#include "Application/Game/NextDayz/Combat/CombatSystem.hpp"
#include "Application/Game/NextDayz/Combat/NoiseSystem.hpp"
#include "Application/Game/NextDayz/World/WorldAnchorRegistry.hpp"
#include "Application/Game/NextDayz/World/ZombieSpawnDirector.hpp"
#include "Application/Game/NextDayz/Zombies/ZombieSystem.hpp"
#include "Engine/Assets/GPU/UniformBuffer.hpp"
#include "Gameplay/AI/NavGrid.h"

namespace Assets
{
    class Scene;
    namespace CPU
    {
        class FCPUAccelerationStructure;
    }
}

namespace Runtime
{
    class TerrainComponent;
}

namespace NextDayz
{
    // A survivor as the world sees it this frame.
    struct FSurvivorPresence
    {
        uint32_t id = 0;                         // non-zero; single player is kLocalPlayerTargetId
        glm::vec3 feet{};
        glm::vec3 eye{};
        glm::vec3 forward{0.0f, 0.0f, 1.0f};
        bool alive = true;
        float sightScale = 1.0f;                 // lantern / crouch; night and fog are world-wide
    };

    class WorldSim
    {
    public:
        // Scene collision for line of sight and infected movement. Empty: an
        // open field with nothing in the way (unit tests).
        using FRayCast = std::function<Assets::RayCastResult(const glm::vec3& origin, const glm::vec3& direction)>;

        WorldSim() = default;
        WorldSim(const WorldSim&) = delete;
        WorldSim& operator=(const WorldSim&) = delete;
        ~WorldSim();

        void Configure(const FZombieConfig& config);
        void SetRayCast(FRayCast rayCast) { rayCast_ = std::move(rayCast); }

        // Clears the infected, combat and noise, reseeds the spawn director and
        // rebuilds its points from the zombie anchors.
        void ResetPopulation(uint64_t spawnSeed);
        void ConfigureSpawnPoints();

        // --- survivors ---
        void UpsertSurvivor(const FSurvivorPresence& survivor);
        void RemoveSurvivor(uint32_t id);
        void ClearSurvivors() { survivors_.clear(); }
        std::span<const FSurvivorPresence> Survivors() const { return survivors_; }
        const FSurvivorPresence* FindSurvivor(uint32_t id) const;

        // --- frame, in this order ---
        // Navigation upkeep and the spawn director (nothing spawns in anyone's view).
        void TickSpawns(float deltaSeconds);
        // Infected perception, steering and attacks against every survivor.
        void TickInfected(float deltaSeconds);
        // Strikes that landed this frame; FPlayerDamageRequest::targetId says on whom.
        std::vector<FPlayerDamageRequest> ConsumeSurvivorHits() { return zombies.ConsumePlayerDamageRequests(); }
        // Night and fog: how far the infected see, for everyone.
        void SetWorldSightScale(float scale) { zombies.SetSightScale(scale); }

        // True when a survivor at `eye` looking along `forward` would see `position`.
        static bool InView(const glm::vec3& eye, const glm::vec3& forward, const glm::vec3& position);
        bool VisibleToAnySurvivor(const glm::vec3& position) const;

        // --- navigation ---
        // Infected navigate on windows of NavigationHalfSize around survivors.
        // Each window has an owner survivor and follows it; a survivor within
        // NavigationRecenterDistance of a window's centre shares it, anyone
        // else gets a window of their own (up to kMaxNavigationWindows), and a
        // window whose owner joins another window or leaves the world goes away.
        // BuildNavigation installs the scene and builds the first windows now;
        // later ones build in the background. `adaptToTerrain` fits the
        // sampling band to the terrain under each window (geographic maps).
        static constexpr size_t kMaxNavigationWindows = 8;
        void BuildNavigation(Assets::Scene& scene, bool background, bool adaptToTerrain);
        // Same, from the collision and (optional) terrain directly.
        void BuildNavigation(Assets::CPU::FCPUAccelerationStructure& collision, Runtime::TerrainComponent* terrain,
                             bool background, bool adaptToTerrain);
        // Waits for every running build and puts it into service.
        void JoinNavigation();
        void ClearNavigation();
        bool NavigationBuilding() const;
        // Every window built and none rebuilding.
        bool NavigationReady() const;
        size_t NavigationWindowCount() const { return navWindows_.size(); }
        // The window of the first survivor (single player: the player's); an
        // empty grid when there is none.
        const NextGameplay::FNavGrid& Navigation() const;
        // The built window that holds `point`, if any.
        const NextGameplay::FNavGrid* NavigationAt(const glm::vec3& point) const;
        // Walkable on the window that holds it. With no window built anywhere
        // yet everything counts as navigable (infected walk straight lines).
        bool IsNavigable(const glm::vec3& point) const;
        std::vector<glm::vec3> FindPath(const glm::vec3& from, const glm::vec3& to) const;
        uint64_t NavigationBuildCount() const { return navBuildCount_; }
        double NavigationBuildMs() const { return navBuildMs_; }
        double NavigationApplyPeakMs() const { return navApplyPeakMs_; }
        void ResetNavigationStats() { navApplyPeakMs_ = 0.0; }

        // World systems. The client and debugger read them; gameplay mutates
        // them through their own transactions (spawn, damage, emit).
        ZombieSystem zombies;
        CombatSystem combat{zombies};
        NoiseSystem noise;
        ZombieSpawnDirector spawns;
        WorldAnchorRegistry anchors;

    private:
        struct FNavigationBuild
        {
            NextGameplay::FNavGrid grid;
            double elapsedMs = 0.0;
        };
        struct FNavigationWindow
        {
            uint32_t ownerId = 0;
            glm::vec3 center{};                  // lattice-snapped centre of the latest build
            NextGameplay::FNavGrid grid;         // serves paths; immutable while a build runs
            NextGameplay::FNavGrid spare;
            std::future<FNavigationBuild> build;
        };
        // Starts (or, without `background`, completes) a build of `window`
        // around `feet`; `shifted` reuses the window's current cells.
        void BuildWindow(FNavigationWindow& window, const glm::vec3& feet, bool background, bool shifted);
        void ApplyNavigation(FNavigationWindow& window, FNavigationBuild result);
        void UpdateNavigation();
        bool Covers(const FNavigationWindow& window, const glm::vec3& feet) const;
        Assets::RayCastResult RayCast(const glm::vec3& origin, const glm::vec3& direction) const;

        FZombieConfig config_{};
        FRayCast rayCast_;
        std::vector<FSurvivorPresence> survivors_;

        Assets::CPU::FCPUAccelerationStructure* navCollision_ = nullptr;
        Runtime::TerrainComponent* navTerrain_ = nullptr;
        bool navAdaptToTerrain_ = false;
        std::vector<std::unique_ptr<FNavigationWindow>> navWindows_;
        NextGameplay::FNavGrid emptyGrid_;
        uint64_t navBuildCount_ = 0;
        double navBuildMs_ = 0.0;
        double navApplyPeakMs_ = 0.0;
    };
}
