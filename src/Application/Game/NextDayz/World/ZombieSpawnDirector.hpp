#pragma once

// ============================================================================
// ZombieSpawnDirector.hpp - Brings the infected into the world at the map
// anchors (nd_spawn_zombie_<profile>): out of sight, at a safe distance,
// never over the global cap. An anchor can hold a small group; a group that
// dies comes back after a few minutes; calm infected far from the player are
// recycled so the cap follows the player around the map.
// ============================================================================

#include "Engine/Common/CoreMinimal.hpp"

#include <glm/vec3.hpp>

#include "Application/Game/NextDayz/Data/DeterministicRng.hpp"
#include "Application/Game/NextDayz/Data/ZombieDefs.hpp"
#include "Application/Game/NextDayz/Zombies/ZombieSystem.hpp"

namespace NextDayz
{
    struct FZombieSpawnTuning
    {
        float evaluationSeconds = 1.0f;
        float minimumDistance = 30.0f;
        float maximumDistance = 160.0f;
        double respawnMinSeconds = 3.0 * 60.0;
        double respawnJitterSeconds = 3.0 * 60.0;
        float recycleDistance = 200.0f;
        float recycleCalmSeconds = 30.0f;
        float groupSpread = 2.5f;
    };

    struct FZombieSpawnPoint
    {
        static constexpr size_t kMaxGroup = 4;

        glm::vec3 position{};
        EZombieProfile profile = EZombieProfile::Civilian;
        int groupSize = 1;
        std::array<FZombieHandle, kMaxGroup> active{};
        double cooldownUntil = 0.0;
        float farCalmSeconds = 0.0f;
    };

    class ZombieSpawnDirector
    {
    public:
        void Configure(const FZombieSpawnTuning& tuning) { tuning_ = tuning; }
        void Reset(uint64_t seed);
        void SetPoints(std::vector<FZombieSpawnPoint> points);
        void SetGlobalCap(int cap) { globalCap_ = std::max(cap, 0); }
        int GlobalCap() const { return globalCap_; }
        void Update(float deltaSeconds, const glm::vec3& playerPosition, ZombieSystem& zombies,
                    const std::function<bool(const glm::vec3&)>& isVisible,
                    const std::function<bool(const glm::vec3&)>& isNavigable);

        int SpawnCount() const { return spawnCount_; }
        int RejectedVisible() const { return rejectedVisible_; }
        const std::vector<FZombieSpawnPoint>& Points() const { return points_; }

    private:
        FZombieSpawnTuning tuning_{};
        std::vector<FZombieSpawnPoint> points_;
        FDeterministicRng rng_;
        double worldSeconds_ = 0.0;
        float evaluationAccumulator_ = 0.0f;
        int spawnCount_ = 0;
        int rejectedVisible_ = 0;
        int globalCap_ = 40;
    };
}
