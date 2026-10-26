#pragma once

// ============================================================================
// EnemyPool.hpp - Air enemies: their path, their gun, their death.
//
// A spawn request carries everything the wave table said about one aircraft;
// the pool turns it into a live enemy driven by a path function and a pattern
// spec. Paths are closed-form on the enemy's own age, so a flight path cannot
// drift with accumulated float error however long the enemy lives. Hover is the
// one exception and says so: "hold a position on screen" means moving with the
// camera, which no function of age alone can express.
// ============================================================================

#include <array>
#include <memory>
#include <vector>

#include <glm/glm.hpp>

#include "Application/Game/NextRaiden/Combat/BulletPattern.hpp"
#include "Application/Game/NextRaiden/NextRaidenTypes.hpp"
#include "Engine/Assets/AssetsFwd.hpp"

namespace NextRaiden
{
    class FPrefabRack;
    class FBulletPool;
    class FScrollCamera;

    /// Per-kind stats, from gameplay.json.
    struct FEnemyStats
    {
        float HitPoints = 12.0f;
        float Radius = 1.7f;
        float Speed = 14.0f;
        int Score = 100;
        int DebrisPieces = 2;
    };

    struct FEnemySpawn
    {
        EEnemyKind Kind = EEnemyKind::Scout;
        EPathKind Path = EPathKind::Straight;
        glm::vec3 Position{0.0f};
        float Speed = 0.0f;       // 0 = use the kind's default
        float PathAmplitude = 6.0f;
        float PathPeriod = 2.4f;
        float HoverDepth = 12.0f; // Hover: how far in front of the camera centre to stop
        float HoverSeconds = 3.0f;
        FPatternSpec Fire{};
        bool CanFire = true;
        EItemKind Drop = EItemKind::None;
    };

    struct FEnemy
    {
        EEnemyKind Kind = EEnemyKind::Scout;
        EPathKind Path = EPathKind::Straight;
        glm::vec3 Spawn{0.0f};
        glm::vec3 Position{0.0f};
        glm::vec3 Velocity{0.0f};
        float Speed = 14.0f;
        float PathAmplitude = 6.0f;
        float PathPeriod = 2.4f;
        float HoverDepth = 12.0f;
        float HoverSeconds = 3.0f;
        float Age = 0.0f;
        float HitPoints = 12.0f;
        float Radius = 1.7f;
        float FireTimer = 0.0f;
        float PhaseDegrees = 0.0f;
        int VolleyIndex = 0;
        int BurstLeft = 0;
        float BurstTimer = 0.0f;
        int Score = 100;
        int DebrisPieces = 2;
        FPatternSpec Fire{};
        bool CanFire = true;
        EItemKind Drop = EItemKind::None;
        bool Active = false;
        int Slot = -1;
        float HitFlash = 0.0f;
    };

    /// What the pool wants the outside world to do about an enemy that just died.
    struct FEnemyDeath
    {
        glm::vec3 Position{0.0f};
        glm::vec3 Velocity{0.0f};
        int Score = 0;
        int DebrisPieces = 0;
        EItemKind Drop = EItemKind::None;
    };

    class FEnemyPool
    {
    public:
        void Create(FPrefabRack& rack, std::vector<std::shared_ptr<Assets::Node>>& nodes, int perKind);
        void Clear();
        void SetStats(EEnemyKind kind, const FEnemyStats& stats);
        const FEnemyStats& Stats(EEnemyKind kind) const;

        bool Spawn(const FEnemySpawn& request);

        /// Advances paths and guns. Bullets go straight into `bullets`; deaths are appended to
        /// `outDeaths` for the caller to turn into score, debris and drops - the pool has no
        /// business knowing what any of those are.
        void Step(float deltaSeconds, const FScrollCamera& camera, const glm::vec3& playerPosition,
                  const glm::vec3& playerVelocity, FBulletPool& bullets, std::vector<FEnemyDeath>& outDeaths);
        void Apply();

        /// Returns true when the hit killed it.
        bool ApplyDamage(FEnemy& enemy, float damage, std::vector<FEnemyDeath>& outDeaths);
        /// Damages every live enemy (a bomb). Deaths append as usual.
        void DamageAll(float damage, std::vector<FEnemyDeath>& outDeaths);

        std::vector<FEnemy>& Enemies() { return enemies_; }
        const std::vector<FEnemy>& Enemies() const { return enemies_; }
        int AliveCount() const { return aliveCount_; }

    private:
        struct FKindPool
        {
            std::vector<std::shared_ptr<Assets::Node>> Nodes;
            std::vector<int> Free;
            FEnemyStats Stats{};
        };

        void Release(FEnemy& enemy);
        glm::vec3 EvaluatePath(const FEnemy& enemy, float age, float centreZ) const;

        std::array<FKindPool, static_cast<size_t>(EEnemyKind::Count)> kinds_{};
        std::vector<FEnemy> enemies_;
        int aliveCount_ = 0;
    };
}
