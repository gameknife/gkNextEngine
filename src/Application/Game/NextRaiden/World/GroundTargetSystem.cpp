#include "Application/Game/NextRaiden/World/GroundTargetSystem.hpp"

#include <algorithm>
#include <cmath>

#include "Application/Game/NextRaiden/Combat/BulletPool.hpp"
#include "Application/Game/NextRaiden/Combat/DebrisSystem.hpp"
#include "Application/Game/NextRaiden/Combat/RaidenRandom.hpp"
#include "Application/Game/NextRaiden/Player/ScrollCamera.hpp"
#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Runtime/Scene/NodeUtils.hpp"

namespace NextRaiden
{
    namespace
    {
        /// Targets wake a little before they are visible so a turret is already tracking when
        /// it slides into view, and stop firing once they are behind the player.
        constexpr float kActivateMargin = 6.0f;
        constexpr float kReleaseMargin = 4.0f;
        /// The last band never comes off: something has to be left on the ground to read as a
        /// wreck rather than as a deleted object.
        constexpr int kMaxSheddableStage = 2;
    }

    void FGroundTargetSystem::Configure(EGroundKind kind, const FGroundStats& stats)
    {
        stats_[static_cast<size_t>(kind)] = stats;
    }

    void FGroundTargetSystem::Bind(FStageIndex* index)
    {
        index_ = index;
        active_.clear();
        windowEnd_ = 0;
        destroyed_ = 0;
        runtime_.clear();
        if (!index_)
        {
            return;
        }
        runtime_.resize(index_->Targets.size());
        for (size_t i = 0; i < index_->Targets.size(); ++i)
        {
            runtime_[i].HitPoints = stats_[static_cast<size_t>(index_->Targets[i].Kind)].HitPoints;
        }
    }

    void FGroundTargetSystem::Unbind()
    {
        index_ = nullptr;
        runtime_.clear();
        active_.clear();
        windowEnd_ = 0;
    }

    void FGroundTargetSystem::ShedStage(size_t targetIndex, int stage, FDebrisSystem& debris, FRaidenRandom& random)
    {
        FStageTarget& target = index_->Targets[targetIndex];
        for (FStageChunk& chunk : target.Chunks)
        {
            if (chunk.Detached || chunk.ReleaseStage != stage || !chunk.Node)
            {
                continue;
            }
            chunk.Detached = true;
            // The piece that falls is the piece that was there: Detach takes the real node off
            // the vehicle. If the budget is full it just goes away instead, which reads as a
            // hit rather than as nothing happening.
            if (!debris.Detach(chunk.Node, glm::vec3(0.0f), random))
            {
                Assets::NodeUtils::SetVisibleRecursive(chunk.Node, false);
            }
        }
    }

    bool FGroundTargetSystem::ApplyDamage(size_t targetIndex, float damage, FDebrisSystem& debris,
                                          FRaidenRandom& random, std::vector<FGroundDeath>& outDeaths)
    {
        if (!index_ || targetIndex >= runtime_.size())
        {
            return false;
        }
        FRuntime& state = runtime_[targetIndex];
        if (state.Destroyed || !state.Activated)
        {
            return false;
        }

        FStageTarget& target = index_->Targets[targetIndex];
        const FGroundStats& stats = stats_[static_cast<size_t>(target.Kind)];
        state.HitPoints -= damage;

        // Two thresholds on the way down, each taking one releaseStage band with it. The bands
        // are the asset author's, not ours: chunk metadata says what is outer armour and what
        // is structure (design section 7.4).
        const float fraction = stats.HitPoints > 0.0f ? state.HitPoints / stats.HitPoints : 0.0f;
        while (state.Stage < kMaxSheddableStage && fraction <= 1.0f - static_cast<float>(state.Stage + 1) / 3.0f)
        {
            ++state.Stage;
            ShedStage(targetIndex, state.Stage, debris, random);
        }

        if (state.HitPoints > 0.0f)
        {
            return false;
        }

        // Dead: everything still attached comes off, and the hull goes with it.
        for (int stage = 1; stage <= 3; ++stage)
        {
            ShedStage(targetIndex, stage, debris, random);
        }
        Assets::NodeUtils::SetVisibleRecursive(target.Node, false);
        if (stats.DebrisPieces > 0)
        {
            debris.Spawn(target.Position + glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f), stats.DebrisPieces, random);
        }

        state.Destroyed = true;
        ++destroyed_;

        FGroundDeath death;
        death.Position = target.Position;
        death.Score = stats.Score;
        outDeaths.push_back(death);

        // A fuel depot takes its neighbours with it. Recursive by construction - a depot next
        // to a depot chains - which is the point of putting two of them together.
        if (stats.BlastRadius > 0.0f)
        {
            for (size_t other : active_)
            {
                if (other == targetIndex || runtime_[other].Destroyed)
                {
                    continue;
                }
                const glm::vec3 delta = index_->Targets[other].Position - target.Position;
                if (delta.x * delta.x + delta.z * delta.z <= stats.BlastRadius * stats.BlastRadius)
                {
                    ApplyDamage(other, stats.BlastDamage, debris, random, outDeaths);
                }
            }
        }
        return true;
    }

    void FGroundTargetSystem::Step(float deltaSeconds, const FScrollCamera& camera, const glm::vec3& playerPosition,
                                   const glm::vec3& playerVelocity, FBulletPool& bullets, FDebrisSystem& debris,
                                   FRaidenRandom& random, std::vector<FGroundDeath>& outDeaths)
    {
        if (!index_)
        {
            return;
        }

        const float activateZ = camera.GroundFarZ() - kActivateMargin;
        const float releaseZ = camera.GroundNearZ() + kReleaseMargin;

        // Walk the window forward. The list is sorted nearest-first, so everything that has
        // come into range is a prefix, and the cursor only ever moves one way.
        while (windowEnd_ < index_->Targets.size() && index_->Targets[windowEnd_].Position.z >= activateZ)
        {
            if (!runtime_[windowEnd_].Destroyed)
            {
                runtime_[windowEnd_].Activated = true;
                active_.push_back(windowEnd_);
            }
            ++windowEnd_;
        }

        FBulletSeed seeds[32];

        for (size_t i = active_.size(); i-- > 0;)
        {
            const size_t targetIndex = active_[i];
            FRuntime& state = runtime_[targetIndex];
            const FStageTarget& target = index_->Targets[targetIndex];

            if (state.Destroyed || target.Position.z > releaseZ)
            {
                state.Activated = false;
                active_.erase(active_.begin() + static_cast<ptrdiff_t>(i));
                continue;
            }

            const FGroundStats& stats = stats_[static_cast<size_t>(target.Kind)];
            if (!stats.CanFire)
            {
                continue;
            }

            state.PhaseDegrees += stats.Fire.RateDegreesPerSec * deltaSeconds;
            if (state.PhaseDegrees > 360.0f)
            {
                state.PhaseDegrees -= 360.0f;
            }

            FPatternContext context;
            // Fired from the flight plane, not from the ground: a shot that starts at y=0 is
            // visually below everything it is supposed to threaten.
            context.Origin = glm::vec3(target.Position.x, playerPosition.y, target.Position.z);
            context.TargetPosition = playerPosition;
            context.TargetVelocity = playerVelocity;
            context.VolleyIndex = state.VolleyIndex;
            context.PhaseDegrees = state.PhaseDegrees;

            if (state.BurstLeft > 0)
            {
                state.BurstTimer -= deltaSeconds;
                if (state.BurstTimer <= 0.0f)
                {
                    const int written = EvaluatePattern(stats.Fire, context, seeds);
                    for (int s = 0; s < written; ++s)
                    {
                        bullets.Emit(seeds[s], true, 0.32f);
                    }
                    --state.BurstLeft;
                    state.BurstTimer = stats.Fire.BurstGapSeconds;
                }
                continue;
            }

            state.FireTimer -= deltaSeconds;
            if (state.FireTimer > 0.0f)
            {
                continue;
            }
            state.FireTimer = std::max(0.1f, stats.Fire.IntervalSeconds);
            ++state.VolleyIndex;

            if (stats.Fire.Kind == EPatternKind::Stream && stats.Fire.Burst > 1)
            {
                state.BurstLeft = stats.Fire.Burst;
                state.BurstTimer = 0.0f;
                continue;
            }

            const int written = EvaluatePattern(stats.Fire, context, seeds);
            for (int s = 0; s < written; ++s)
            {
                bullets.Emit(seeds[s], true, 0.32f);
            }
        }

        (void)outDeaths;
    }
}
