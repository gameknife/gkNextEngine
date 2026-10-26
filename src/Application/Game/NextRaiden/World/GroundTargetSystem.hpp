#pragma once

// ============================================================================
// GroundTargetSystem.hpp - The tanks, turrets, launchers and depots the stage
// .scad placed by hand.
//
// These are not pooled: they exist exactly where the level author put them, they
// are destroyed once, and they do not come back. The system keeps a moving
// window over the depth-sorted index, so at any moment it only touches the
// handful of targets that are on screen.
//
// Damage is staged off the asset's own metadata: at each threshold the chunks of
// that releaseStage come off as real physics debris (design section 7.4).
// ============================================================================

#include <array>
#include <vector>

#include <glm/glm.hpp>

#include "Application/Game/NextRaiden/Combat/BulletPattern.hpp"
#include "Application/Game/NextRaiden/Level/StageIndex.hpp"

namespace NextRaiden
{
    class FBulletPool;
    class FDebrisSystem;
    class FScrollCamera;
    class FRaidenRandom;

    /// Per-kind stats and gun, from gameplay.json.
    struct FGroundStats
    {
        float HitPoints = 40.0f;
        float Radius = 2.2f;
        int Score = 300;
        int DebrisPieces = 3;
        bool CanFire = true;
        FPatternSpec Fire{};
        /// Fuel depots take the neighbourhood with them.
        float BlastRadius = 0.0f;
        float BlastDamage = 0.0f;
    };

    struct FGroundDeath
    {
        glm::vec3 Position{0.0f};
        int Score = 0;
        EItemKind Drop = EItemKind::None;
    };

    class FGroundTargetSystem
    {
    public:
        void Configure(EGroundKind kind, const FGroundStats& stats);
        void Bind(FStageIndex* index);
        void Unbind();

        void Step(float deltaSeconds, const FScrollCamera& camera, const glm::vec3& playerPosition,
                  const glm::vec3& playerVelocity, FBulletPool& bullets, FDebrisSystem& debris,
                  FRaidenRandom& random, std::vector<FGroundDeath>& outDeaths);

        /// Returns true when the hit destroyed the target. Damage is applied through here so
        /// both bullets and blasts go down the same staged-shedding path.
        bool ApplyDamage(size_t targetIndex, float damage, FDebrisSystem& debris, FRaidenRandom& random,
                         std::vector<FGroundDeath>& outDeaths);

        /// Indices of the targets currently in the activation window, for collision.
        const std::vector<size_t>& ActiveTargets() const { return active_; }
        const FStageTarget& Target(size_t index) const { return index_->Targets[index]; }
        const FGroundStats& Stats(EGroundKind kind) const { return stats_[static_cast<size_t>(kind)]; }
        int AliveCount() const { return static_cast<int>(active_.size()); }
        int DestroyedCount() const { return destroyed_; }

    private:
        struct FRuntime
        {
            float HitPoints = 0.0f;
            float FireTimer = 0.0f;
            int VolleyIndex = 0;
            int BurstLeft = 0;
            float BurstTimer = 0.0f;
            float PhaseDegrees = 0.0f;
            int Stage = 0;       // how many releaseStage bands have already come off
            bool Activated = false;
            bool Destroyed = false;
        };

        void ShedStage(size_t targetIndex, int stage, FDebrisSystem& debris, FRaidenRandom& random);

        std::array<FGroundStats, static_cast<size_t>(EGroundKind::Count)> stats_{};
        std::vector<FRuntime> runtime_;
        std::vector<size_t> active_;
        FStageIndex* index_ = nullptr;
        /// Everything before this in the depth-sorted list has already been passed.
        size_t windowEnd_ = 0;
        int destroyed_ = 0;
    };
}
