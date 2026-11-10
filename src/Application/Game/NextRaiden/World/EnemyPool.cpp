#include "Application/Game/NextRaiden/World/EnemyPool.hpp"

#include <algorithm>
#include <cmath>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include "Application/Game/NextRaiden/Combat/BulletPool.hpp"
#include "Application/Game/NextRaiden/Level/PrefabRack.hpp"
#include "Application/Game/NextRaiden/Player/ScrollCamera.hpp"
#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Runtime/Scene/NodeUtils.hpp"

namespace NextRaiden
{
    namespace
    {
        constexpr float kDespawnMargin = 12.0f;
        /// Enemies hold fire until they are actually on screen; otherwise the first thing a
        /// wave does is snipe the player from somewhere they cannot see.
        constexpr float kFireWindowMargin = 2.0f;

        const char* PrefabForKind(EEnemyKind kind)
        {
            switch (kind)
            {
            case EEnemyKind::Scout: return Prefabs::kScout;
            case EEnemyKind::Interceptor: return Prefabs::kInterceptor;
            case EEnemyKind::Bomber: return Prefabs::kBomber;
            default: return Prefabs::kScout;
            }
        }
    }

    void FEnemyPool::Create(FPrefabRack& rack, std::vector<std::shared_ptr<Assets::Node>>& nodes, int perKind)
    {
        Clear();
        for (size_t k = 0; k < kinds_.size(); ++k)
        {
            FKindPool& pool = kinds_[k];
            // The bomber keeps its chunk hierarchy for P2's real-part shedding, so it is far
            // heavier per instance than the flattened light aircraft; a stage never needs many.
            const int count = static_cast<EEnemyKind>(k) == EEnemyKind::Bomber ? std::max(2, perKind / 4) : perKind;
            pool.Nodes = rack.Clone(PrefabForKind(static_cast<EEnemyKind>(k)), count, nodes,
                                    FPrefabRack::ParkPosition(static_cast<int>(k) + 1));
            pool.Free.resize(pool.Nodes.size());
            for (size_t i = 0; i < pool.Nodes.size(); ++i)
            {
                pool.Free[i] = static_cast<int>(pool.Nodes.size() - 1 - i);
            }
        }
    }

    void FEnemyPool::Clear()
    {
        for (FKindPool& pool : kinds_)
        {
            pool.Nodes.clear();
            pool.Free.clear();
        }
        enemies_.clear();
        aliveCount_ = 0;
    }

    void FEnemyPool::SetStats(EEnemyKind kind, const FEnemyStats& stats)
    {
        kinds_[static_cast<size_t>(kind)].Stats = stats;
    }

    const FEnemyStats& FEnemyPool::Stats(EEnemyKind kind) const
    {
        return kinds_[static_cast<size_t>(kind)].Stats;
    }

    bool FEnemyPool::Spawn(const FEnemySpawn& request)
    {
        FKindPool& pool = kinds_[static_cast<size_t>(request.Kind)];
        if (pool.Free.empty())
        {
            return false;
        }
        const int slot = pool.Free.back();
        pool.Free.pop_back();

        FEnemy* enemy = nullptr;
        for (FEnemy& candidate : enemies_)
        {
            if (!candidate.Active)
            {
                enemy = &candidate;
                break;
            }
        }
        if (!enemy)
        {
            enemies_.push_back(FEnemy{});
            enemy = &enemies_.back();
        }

        const FEnemyStats& stats = pool.Stats;
        *enemy = FEnemy{};
        enemy->Kind = request.Kind;
        enemy->Path = request.Path;
        enemy->Spawn = request.Position;
        enemy->Position = request.Position;
        enemy->Speed = request.Speed > 0.0f ? request.Speed : stats.Speed;
        enemy->PathAmplitude = request.PathAmplitude;
        enemy->PathPeriod = std::max(0.2f, request.PathPeriod);
        enemy->HoverDepth = request.HoverDepth;
        enemy->HoverSeconds = request.HoverSeconds;
        enemy->HitPoints = stats.HitPoints;
        enemy->Radius = stats.Radius;
        enemy->Score = stats.Score;
        enemy->DebrisPieces = stats.DebrisPieces;
        enemy->Fire = request.Fire;
        enemy->CanFire = request.CanFire;
        enemy->Drop = request.Drop;
        enemy->FireTimer = request.Fire.IntervalSeconds * 0.5f; // stagger the first volley
        enemy->Slot = slot;
        enemy->Active = true;
        ++aliveCount_;

        Assets::NodeUtils::SetVisibleRecursive(pool.Nodes[static_cast<size_t>(slot)], true);
        return true;
    }

    void FEnemyPool::Release(FEnemy& enemy)
    {
        if (!enemy.Active)
        {
            return;
        }
        FKindPool& pool = kinds_[static_cast<size_t>(enemy.Kind)];
        if (enemy.Slot >= 0 && enemy.Slot < static_cast<int>(pool.Nodes.size()))
        {
            Assets::NodeUtils::SetVisibleRecursive(pool.Nodes[static_cast<size_t>(enemy.Slot)], false);
            pool.Free.push_back(enemy.Slot);
        }
        enemy.Slot = -1;
        enemy.Active = false;
        --aliveCount_;
    }

    glm::vec3 FEnemyPool::EvaluatePath(const FEnemy& enemy, float age, float centreZ) const
    {
        const float forward = enemy.Speed * age;
        switch (enemy.Path)
        {
        case EPathKind::Straight:
            return enemy.Spawn + glm::vec3(0.0f, 0.0f, forward);

        case EPathKind::Sine:
        {
            const float phase = glm::two_pi<float>() * age / enemy.PathPeriod;
            return enemy.Spawn + glm::vec3(enemy.PathAmplitude * std::sin(phase), 0.0f, forward);
        }

        case EPathKind::ArcInOut:
        {
            // Sweeps sideways while descending, so a pair spawned left and right crosses in
            // front of the player instead of just falling past them.
            const float phase = glm::two_pi<float>() * age / (enemy.PathPeriod * 2.0f);
            const float side = enemy.Spawn.x >= 0.0f ? -1.0f : 1.0f;
            return enemy.Spawn + glm::vec3(side * enemy.PathAmplitude * (1.0f - std::cos(phase)), 0.0f,
                                           forward + enemy.PathAmplitude * 0.35f * std::sin(phase));
        }

        case EPathKind::Dive:
            // Accelerating: slow entry the player can read, then a committed rush.
            return enemy.Spawn + glm::vec3(0.0f, 0.0f, forward * (1.0f + age * 0.6f));

        case EPathKind::Strafe:
        {
            const float side = enemy.Spawn.x >= 0.0f ? -1.0f : 1.0f;
            return enemy.Spawn + glm::vec3(side * enemy.Speed * age, 0.0f, enemy.Speed * 0.22f * age);
        }

        case EPathKind::Hover:
        {
            // The only path that is not a function of age alone: "hold a position on screen"
            // means moving with the camera, so it needs the camera to evaluate.
            const float holdZ = centreZ - enemy.HoverDepth;
            const float approachZ = enemy.Spawn.z + forward;
            if (approachZ < holdZ)
            {
                return glm::vec3(enemy.Spawn.x, enemy.Spawn.y, approachZ);
            }
            const float holdAge = age - (holdZ - enemy.Spawn.z) / std::max(0.1f, enemy.Speed);
            if (holdAge < enemy.HoverSeconds)
            {
                return glm::vec3(enemy.Spawn.x, enemy.Spawn.y, holdZ);
            }
            // Retreat up the screen, accelerating away.
            const float retreat = holdAge - enemy.HoverSeconds;
            return glm::vec3(enemy.Spawn.x, enemy.Spawn.y, holdZ - enemy.Speed * retreat * (1.0f + retreat));
        }

        case EPathKind::Count:
        default:
            return enemy.Spawn + glm::vec3(0.0f, 0.0f, forward);
        }
    }

    void FEnemyPool::Step(float deltaSeconds, const FScrollCamera& camera, const glm::vec3& playerPosition,
                          const glm::vec3& playerVelocity, FBulletPool& bullets, std::vector<FEnemyDeath>& outDeaths)
    {
        const float centreZ = camera.CentreZ();
        const float nearZ = camera.FlightNearZ();
        const float farZ = camera.FlightFarZ();

        FBulletSeed seeds[32];

        for (FEnemy& enemy : enemies_)
        {
            if (!enemy.Active)
            {
                continue;
            }

            const glm::vec3 previous = enemy.Position;
            enemy.Age += deltaSeconds;
            enemy.Position = EvaluatePath(enemy, enemy.Age, centreZ);
            enemy.Velocity = deltaSeconds > 0.0f ? (enemy.Position - previous) / deltaSeconds : glm::vec3(0.0f);
            enemy.HitFlash = std::max(0.0f, enemy.HitFlash - deltaSeconds);

            // Left the play area under its own power: no score, no debris, it simply got away.
            if (enemy.Position.z > nearZ + kDespawnMargin || enemy.Position.z < farZ - kDespawnMargin * 4.0f ||
                std::abs(enemy.Position.x) > 95.0f)
            {
                Release(enemy);
                continue;
            }

            if (!enemy.CanFire)
            {
                continue;
            }

            enemy.PhaseDegrees += enemy.Fire.RateDegreesPerSec * deltaSeconds;
            if (enemy.PhaseDegrees > 360.0f)
            {
                enemy.PhaseDegrees -= 360.0f;
            }

            const bool onScreen =
                enemy.Position.z < nearZ - kFireWindowMargin && enemy.Position.z > farZ + kFireWindowMargin;
            if (!onScreen)
            {
                continue;
            }

            FPatternContext context;
            context.Origin = enemy.Position;
            context.TargetPosition = playerPosition;
            context.TargetVelocity = playerVelocity;
            context.VolleyIndex = enemy.VolleyIndex;
            context.PhaseDegrees = enemy.PhaseDegrees;

            // A burst in progress owns the gun until it is spent.
            if (enemy.BurstLeft > 0)
            {
                enemy.BurstTimer -= deltaSeconds;
                if (enemy.BurstTimer <= 0.0f)
                {
                    const int written = EvaluatePattern(enemy.Fire, context, seeds);
                    for (int i = 0; i < written; ++i)
                    {
                        bullets.Emit(seeds[i], true, 0.32f);
                    }
                    --enemy.BurstLeft;
                    enemy.BurstTimer = enemy.Fire.BurstGapSeconds;
                }
                continue;
            }

            enemy.FireTimer -= deltaSeconds;
            if (enemy.FireTimer > 0.0f)
            {
                continue;
            }
            enemy.FireTimer = std::max(0.08f, enemy.Fire.IntervalSeconds);
            ++enemy.VolleyIndex;

            if (enemy.Fire.Kind == EPatternKind::Stream && enemy.Fire.Burst > 1)
            {
                enemy.BurstLeft = enemy.Fire.Burst;
                enemy.BurstTimer = 0.0f;
                continue;
            }

            const int written = EvaluatePattern(enemy.Fire, context, seeds);
            for (int i = 0; i < written; ++i)
            {
                bullets.Emit(seeds[i], true, 0.32f);
            }
        }

        (void)outDeaths;
    }

    bool FEnemyPool::ApplyDamage(FEnemy& enemy, float damage, std::vector<FEnemyDeath>& outDeaths)
    {
        if (!enemy.Active)
        {
            return false;
        }
        enemy.HitPoints -= damage;
        enemy.HitFlash = 0.08f;
        if (enemy.HitPoints > 0.0f)
        {
            return false;
        }

        FEnemyDeath death;
        death.Position = enemy.Position;
        death.Velocity = enemy.Velocity;
        death.Score = enemy.Score;
        death.DebrisPieces = enemy.DebrisPieces;
        death.Drop = enemy.Drop;
        outDeaths.push_back(death);
        Release(enemy);
        return true;
    }

    void FEnemyPool::DamageAll(float damage, std::vector<FEnemyDeath>& outDeaths)
    {
        for (FEnemy& enemy : enemies_)
        {
            if (enemy.Active)
            {
                ApplyDamage(enemy, damage, outDeaths);
            }
        }
    }

    void FEnemyPool::Apply()
    {
        for (const FEnemy& enemy : enemies_)
        {
            if (!enemy.Active || enemy.Slot < 0)
            {
                continue;
            }
            FKindPool& pool = kinds_[static_cast<size_t>(enemy.Kind)];
            const std::shared_ptr<Assets::Node>& node = pool.Nodes[static_cast<size_t>(enemy.Slot)];
            // Faces where it is going. At yaw 0 a kit airframe already looks at the player,
            // so an enemy flying straight down the screen needs no rotation at all - the
            // arcs and strafes are what this is for.
            const float speedSq = enemy.Velocity.x * enemy.Velocity.x + enemy.Velocity.z * enemy.Velocity.z;
            const float yaw = speedSq > 0.01f ? std::atan2(enemy.Velocity.x, enemy.Velocity.z) : 0.0f;
            node->SetTransform(enemy.Position, glm::angleAxis(yaw, glm::vec3(0.0f, 1.0f, 0.0f)), glm::vec3(1.0f));
        }
    }
}
