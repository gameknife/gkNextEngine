#include "Engine/Common/CoreMinimal.hpp"

#include "ZombieSpawnDirector.hpp"

#include <glm/geometric.hpp>

namespace NextDayz
{
    void ZombieSpawnDirector::Reset(uint64_t seed)
    {
        points_.clear();
        rng_.Reset(seed);
        worldSeconds_ = 0.0;
        evaluationAccumulator_ = 0.0f;
        spawnCount_ = 0;
        rejectedVisible_ = 0;
        recycledCount_ = 0;
    }

    void ZombieSpawnDirector::SetPoints(std::vector<FZombieSpawnPoint> points)
    {
        points_ = std::move(points);
        for (FZombieSpawnPoint& point : points_)
        {
            point.groupSize = std::clamp(point.groupSize, 1, static_cast<int>(FZombieSpawnPoint::kMaxGroup));
        }
    }

    void ZombieSpawnDirector::Update(float deltaSeconds, const glm::vec3& playerPosition, ZombieSystem& zombies,
                                     const std::function<bool(const glm::vec3&)>& isVisible,
                                     const std::function<bool(const glm::vec3&)>& isNavigable)
    {
        UpdateForSurvivors(deltaSeconds, std::span<const glm::vec3>(&playerPosition, 1), zombies, isVisible, isNavigable);
    }

    void ZombieSpawnDirector::UpdateForSurvivors(float deltaSeconds, std::span<const glm::vec3> playerPositions,
                                     ZombieSystem& zombies, const std::function<bool(const glm::vec3&)>& isVisible,
                                     const std::function<bool(const glm::vec3&)>& isNavigable)
    {
        // Distances are to the nearest survivor: a place spawns when nobody is too
        // close and somebody is near enough, and recycles only when everyone is far.
        const auto nearestPlayer = [playerPositions](const glm::vec3& position)
        {
            float nearest = std::numeric_limits<float>::max();
            for (const glm::vec3& player : playerPositions)
            {
                nearest = std::min(nearest, glm::distance(position, player));
            }
            return nearest;
        };
        const float delta = std::max(deltaSeconds, 0.0f);
        const float interval = std::max(tuning_.evaluationSeconds, 0.05f);
        worldSeconds_ += delta;
        evaluationAccumulator_ += delta;
        if (evaluationAccumulator_ < interval)
        {
            return;
        }
        evaluationAccumulator_ = std::fmod(evaluationAccumulator_, interval);

        for (FZombieSpawnPoint& point : points_)
        {
            point.playerDistance = nearestPlayer(point.position);
            bool anyActive = false;
            bool allCalmAndFar = true;
            bool lostMember = false;
            for (int member = 0; member < point.groupSize; ++member)
            {
                FZombieHandle& handle = point.active[member];
                if (!handle.IsValid())
                {
                    continue;
                }
                const FZombieRuntime* zombie = zombies.Resolve(handle);
                if (!zombie)
                {
                    handle = {};
                    lostMember = true;
                    continue;
                }
                anyActive = true;
                const bool calm = zombie->state == EZombieState::Wander || zombie->state == EZombieState::Dormant;
                if (!calm || nearestPlayer(zombie->position) <= tuning_.recycleDistance)
                {
                    allCalmAndFar = false;
                }
            }
            if (lostMember && !anyActive)
            {
                // The whole group is gone: the place stays quiet for a while.
                const uint32_t jitter = static_cast<uint32_t>(std::max(1.0, tuning_.respawnJitterSeconds));
                point.cooldownUntil = worldSeconds_ + tuning_.respawnMinSeconds + static_cast<double>(rng_.Range(jitter));
                point.farCalmSeconds = 0.0f;
                point.verdict = ESpawnVerdict::Cooldown;
                continue;
            }
            if (!anyActive)
            {
                // Empty: the spawn pass below says why it stays that way. A
                // point it never reaches is either cooling down or waiting on the cap.
                point.verdict = worldSeconds_ < point.cooldownUntil ? ESpawnVerdict::Cooldown : ESpawnVerdict::CapReached;
                continue;
            }
            point.verdict = ESpawnVerdict::Populated;
            point.farCalmSeconds = allCalmAndFar ? point.farCalmSeconds + interval : 0.0f;
            if (point.farCalmSeconds >= tuning_.recycleCalmSeconds)
            {
                for (FZombieHandle& handle : point.active)
                {
                    if (handle.IsValid())
                    {
                        zombies.Recycle(handle);
                        handle = {};
                        ++recycledCount_;
                    }
                }
                point.cooldownUntil = worldSeconds_ + 15.0;
                point.farCalmSeconds = 0.0f;
                point.verdict = ESpawnVerdict::Recycled;
            }
        }
        if (zombies.ActiveCount() >= globalCap_ || points_.empty())
        {
            return;
        }

        const size_t start = rng_.Range(static_cast<uint32_t>(points_.size()));
        for (size_t offset = 0; offset < points_.size() && zombies.ActiveCount() < globalCap_; ++offset)
        {
            FZombieSpawnPoint& point = points_[(start + offset) % points_.size()];
            const bool occupied = std::any_of(point.active.begin(), point.active.end(),
                                              [](const FZombieHandle& handle) { return handle.IsValid(); });
            if (occupied || worldSeconds_ < point.cooldownUntil)
            {
                continue;
            }
            const float distance = point.playerDistance;
            if (distance < tuning_.minimumDistance || distance > tuning_.maximumDistance)
            {
                point.verdict = distance < tuning_.minimumDistance ? ESpawnVerdict::TooClose : ESpawnVerdict::TooFar;
                continue;
            }
            if (isVisible && isVisible(point.position))
            {
                ++rejectedVisible_;
                point.verdict = ESpawnVerdict::Visible;
                continue;
            }
            if (isNavigable && !isNavigable(point.position))
            {
                point.verdict = ESpawnVerdict::NotNavigable;
                continue;
            }
            point.verdict = ESpawnVerdict::Spawned;
            for (int member = 0; member < point.groupSize && zombies.ActiveCount() < globalCap_; ++member)
            {
                glm::vec3 position = point.position;
                if (member > 0)
                {
                    const float angle = rng_.UnitFloat() * 6.2831853f;
                    const float radius = tuning_.groupSpread * (0.5f + 0.5f * rng_.UnitFloat());
                    const glm::vec3 spread = position + glm::vec3(std::cos(angle), 0.0f, std::sin(angle)) * radius;
                    if (!isNavigable || isNavigable(spread))
                    {
                        position = spread;
                    }
                }
                point.active[member] = zombies.Spawn(point.profile, position);
                if (point.active[member].IsValid())
                {
                    ++spawnCount_;
                }
            }
        }
    }
}
