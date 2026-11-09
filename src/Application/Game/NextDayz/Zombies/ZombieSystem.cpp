#include "Engine/Common/CoreMinimal.hpp"

#include "ZombieSystem.hpp"

#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/trigonometric.hpp>

namespace NextDayz
{
    namespace
    {
        constexpr float kChaseRepathSeconds = 0.6f;
        constexpr float kRoamRepathSeconds = 3.0f;
        constexpr float kWaypointReach = 0.18f;
        constexpr float kSteerDirectDistance = 2.5f;   // inside this, chase the player, not the grid
        constexpr float kWindupCloseSpeed = 1.6f;

        uint32_t Hash(uint32_t x)
        {
            x ^= x >> 16;
            x *= 0x7feb352dU;
            x ^= x >> 15;
            x *= 0x846ca68bU;
            x ^= x >> 16;
            return x;
        }

        float Unit(uint32_t seed, uint32_t salt)
        {
            return static_cast<float>(Hash(seed + salt * 0x632BE5ABU) & 0xFFFFFFU) / 16777216.0f;
        }

        glm::vec3 Flat(glm::vec3 v)
        {
            v.y = 0.0f;
            return v;
        }

        float MoveTowards(float current, float target, float maxDelta)
        {
            return current < target ? std::min(current + maxDelta, target) : std::max(current - maxDelta, target);
        }

        bool Alive(const FZombieRuntime& zombie)
        {
            return zombie.active && zombie.state != EZombieState::Dead;
        }
    }

    float FZombieRuntime::Speed() const
    {
        return glm::length(Flat(velocity));
    }

    ZombieSystem::ZombieSystem(size_t capacity)
        : slots_(capacity), generations_(capacity, 1)
    {
        damageRequests_.reserve(capacity);
    }

    void ZombieSystem::Reset()
    {
        for (size_t index = 0; index < slots_.size(); ++index)
        {
            slots_[index] = {};
            ++generations_[index];
            if (generations_[index] == 0)
            {
                ++generations_[index];
            }
        }
        damageRequests_.clear();
        killCount_ = 0;
        staggerCount_ = 0;
    }

    FZombieVariant ZombieSystem::MakeVariant(FZombieHandle handle) const
    {
        const uint32_t seed = Hash(handle.index * 0x9E3779B9U ^ Hash(handle.generation));
        FZombieVariant variant;
        variant.speedScale = 0.88f + 0.24f * Unit(seed, 0);
        variant.hunchDegrees = -3.0f + 13.0f * Unit(seed, 1);
        variant.headTiltDegrees = (Unit(seed, 2) * 2.0f - 1.0f) * 12.0f;
        variant.reach = 0.25f + 0.75f * Unit(seed, 3);
        variant.phase = Unit(seed, 4);
        variant.lame = Unit(seed, 5) < config_.LameChance;
        return variant;
    }

    glm::vec3 ZombieSystem::NextRoamTarget(FZombieRuntime& zombie) const
    {
        const uint32_t seed = Hash(zombie.handle.index * 0x85EBCA6BU ^ Hash(zombie.handle.generation + zombie.roamCount));
        ++zombie.roamCount;
        const float angle = glm::two_pi<float>() * Unit(seed, 0);
        const float radius = config_.RoamRadius * std::sqrt(Unit(seed, 1));
        zombie.roamSeconds = glm::mix(config_.RoamMinSeconds, config_.RoamMaxSeconds, Unit(seed, 2));
        return zombie.spawnPosition + glm::vec3(std::cos(angle), 0.0f, std::sin(angle)) * radius;
    }

    FZombieHandle ZombieSystem::Spawn(EZombieProfile profile, const glm::vec3& position)
    {
        for (size_t index = 0; index < slots_.size(); ++index)
        {
            FZombieRuntime& zombie = slots_[index];
            if (zombie.active)
            {
                continue;
            }
            ++generations_[index];
            if (generations_[index] == 0)
            {
                ++generations_[index];
            }
            const FZombieHandle handle{static_cast<uint32_t>(index), generations_[index]};
            zombie = {};
            zombie.handle = handle;
            zombie.profile = profile;
            zombie.state = EZombieState::Wander;
            zombie.position = position;
            zombie.spawnPosition = position;
            zombie.lastKnownPlayerPosition = position;
            zombie.pathTarget = position;
            zombie.progressPosition = position;
            zombie.roamTarget = position;
            zombie.variant = MakeVariant(handle);
            // Stand a moment before the first stroll so a group does not set off in step.
            zombie.roamSeconds = 1.0f + 3.0f * zombie.variant.phase;
            zombie.health = ZombieDef(profile).maxHealth;
            zombie.active = true;
            return handle;
        }
        return {};
    }

    const FZombieRuntime* ZombieSystem::Resolve(FZombieHandle handle) const
    {
        if (!handle.IsValid() || handle.index >= slots_.size() || generations_[handle.index] != handle.generation)
        {
            return nullptr;
        }
        const FZombieRuntime& zombie = slots_[handle.index];
        return zombie.active ? &zombie : nullptr;
    }

    FZombieRuntime* ZombieSystem::Resolve(FZombieHandle handle)
    {
        return const_cast<FZombieRuntime*>(std::as_const(*this).Resolve(handle));
    }

    bool ZombieSystem::Recycle(FZombieHandle handle)
    {
        FZombieRuntime* zombie = Resolve(handle);
        if (!zombie)
        {
            return false;
        }
        zombie->active = false;
        ++generations_[handle.index];
        if (generations_[handle.index] == 0)
        {
            ++generations_[handle.index];
        }
        return true;
    }

    float ZombieSystem::AttackDuration(EZombieAttack attack) const
    {
        return attack == EZombieAttack::Lunge ? config_.LungeSeconds : config_.SwipeSeconds;
    }

    float ZombieSystem::AttackStrikeTime(EZombieAttack attack) const
    {
        return attack == EZombieAttack::Lunge ? config_.LungeStrikeSeconds : config_.SwipeStrikeSeconds;
    }

    FZombieDamageResult ZombieSystem::ApplyDamage(FZombieHandle handle, const FZombieDamage& damage)
    {
        FZombieDamageResult result;
        FZombieRuntime* zombie = Resolve(handle);
        if (!zombie || zombie->state == EZombieState::Dead || damage.amount <= 0.0f)
        {
            return result;
        }
        const FZombieConfig& c = config_;
        float multiplier = c.TorsoMultiplier;
        float impactScale = 1.0f;
        switch (damage.zone)
        {
        case EHitZone::Head: multiplier = c.HeadMultiplier; impactScale = c.HeadImpactScale; break;
        case EHitZone::Limb: multiplier = c.LimbMultiplier; break;
        case EHitZone::Leg: multiplier = c.LegMultiplier; impactScale = c.LegImpactScale; break;
        case EHitZone::Torso: break;
        }
        const float dealt = std::min(zombie->health, damage.amount * multiplier);
        zombie->health = std::max(0.0f, zombie->health - damage.amount * multiplier);
        result.applied = true;
        result.dealt = dealt;

        glm::vec3 push = Flat(damage.direction);
        const float pushLength = glm::length(push);
        push = pushLength > 1.0e-4f ? push / pushLength : -zombie->forward;
        const float impact = damage.impact * impactScale;

        // Getting shot gives the shooter away.
        if (!zombie->aware || zombie->state == EZombieState::Wander || zombie->state == EZombieState::Investigate)
        {
            zombie->lastKnownPlayerPosition = damage.source;
            if (damage.attackerId != 0)
            {
                zombie->targetId = damage.attackerId;
            }
        }
        zombie->aware = true;

        if (zombie->health <= 0.0f)
        {
            // The fall reads the hit: a headshot drops it where it stands, a
            // round from behind or a charging body pitches forward, a round
            // from the front knocks it back.
            const float speed = zombie->Speed();
            const float along = glm::dot(push, zombie->forward);
            if (damage.zone == EHitZone::Head && speed < 2.0f)
            {
                zombie->death = EZombieDeath::Crumple;
            }
            else if (speed > 2.2f || along > 0.2f)
            {
                zombie->death = EZombieDeath::Forward;
            }
            else
            {
                zombie->death = EZombieDeath::Back;
            }
            const float impulse = std::min(c.DeathMaxImpulse, impact * c.DeathImpulsePerImpact);
            zombie->deathVelocity = Flat(zombie->velocity) * c.DeathMomentumKeep + push * impulse;
            zombie->velocity = {};
            zombie->state = EZombieState::Dead;
            zombie->stateSeconds = 0.0f;
            zombie->corpseSeconds = 0.0f;
            ++killCount_;
            result.killed = true;
            return result;
        }

        if (damage.zone == EHitZone::Leg)
        {
            zombie->legDamage += dealt;
            if (!zombie->crippled && zombie->legDamage >= c.CrippleLegDamage)
            {
                zombie->crippled = true;
                result.crippled = true;
            }
        }
        zombie->flinchSeconds = c.FlinchSeconds;
        zombie->poise += impact;
        if ((zombie->poise >= c.PoiseMax || result.crippled) && zombie->staggerImmunity <= 0.0f &&
            zombie->state != EZombieState::Stagger)
        {
            // Breaking poise interrupts whatever it was doing, wind-ups included.
            zombie->state = EZombieState::Stagger;
            zombie->stateSeconds = 0.0f;
            zombie->staggerDirection = push;
            zombie->poise = 0.0f;
            zombie->staggerImmunity = c.StaggerImmunitySeconds;
            zombie->attackCooldown = std::max(zombie->attackCooldown, c.AttackCooldownSeconds);
            ++staggerCount_;
            result.staggered = true;
        }
        else if (zombie->state == EZombieState::Wander || zombie->state == EZombieState::Investigate ||
                 zombie->state == EZombieState::Alert)
        {
            zombie->state = EZombieState::Chase;
            zombie->stateSeconds = 0.0f;
        }
        return result;
    }

    bool ZombieSystem::CanSee(const FZombieRuntime& zombie, const FZombieTarget& target,
                              const FLineOfSight& hasLineOfSight) const
    {
        if (!target.alive)
        {
            return false;
        }
        const FZombieDef& definition = ZombieDef(zombie.profile);
        const glm::vec3 delta = Flat(target.position - zombie.position);
        const float distance = glm::length(delta);
        if (distance <= 0.01f || distance > definition.sightDistance * sightScale_ * std::max(target.sightScale, 0.05f))
        {
            return false;
        }
        const glm::vec3 direction = delta / distance;
        const float minimumDot = std::cos(glm::radians(definition.fieldOfViewDegrees * 0.5f));
        if (glm::dot(zombie.forward, direction) < minimumDot)
        {
            return false;
        }
        return !hasLineOfSight || hasLineOfSight(zombie.position + glm::vec3(0.0f, 1.4f, 0.0f),
                                                 target.position + glm::vec3(0.0f, 1.0f, 0.0f));
    }

    const FZombieTarget* ZombieSystem::Perceive(const FZombieRuntime& zombie, std::span<const FZombieTarget> targets,
                                                const FLineOfSight& hasLineOfSight) const
    {
        // Loyal to the survivor it is already after, so two players walking side
        // by side do not make it flicker between them.
        for (const FZombieTarget& target : targets)
        {
            if (target.id == zombie.targetId && zombie.targetId != 0)
            {
                if (CanSee(zombie, target, hasLineOfSight)) return &target;
                break;
            }
        }
        const FZombieTarget* best = nullptr;
        float bestDistance = std::numeric_limits<float>::max();
        for (const FZombieTarget& target : targets)
        {
            if (target.id == zombie.targetId && zombie.targetId != 0) continue;
            const float distance = glm::length(Flat(target.position - zombie.position));
            // Cheap range test first: line of sight is a ray cast.
            if (distance < bestDistance && CanSee(zombie, target, hasLineOfSight))
            {
                best = &target;
                bestDistance = distance;
            }
        }
        return best;
    }

    void ZombieSystem::Steer(FZombieRuntime& zombie, const glm::vec3& direction, float speed, float turnDegrees,
                             float deltaSeconds) const
    {
        glm::vec3 desired = Flat(direction);
        const float length = glm::length(desired);
        float alignment = 0.0f;
        if (length > 1.0e-4f)
        {
            desired /= length;
            const float current = std::atan2(zombie.forward.x, zombie.forward.z);
            const float delta = std::remainder(std::atan2(desired.x, desired.z) - current, glm::two_pi<float>());
            const float step = glm::radians(std::max(turnDegrees, 0.0f)) * deltaSeconds;
            const float yaw = current + std::clamp(delta, -step, step);
            zombie.forward = glm::vec3(std::sin(yaw), 0.0f, std::cos(yaw));
            alignment = std::clamp(glm::dot(zombie.forward, desired), 0.0f, 1.0f);
        }
        // Sharp turns bleed speed: a charging infected cannot cut a corner at full tilt.
        const float target = length > 1.0e-4f ? std::max(speed, 0.0f) * (0.25f + 0.75f * alignment) : 0.0f;
        const float current = zombie.Speed();
        const float rate = target > current ? config_.Acceleration : config_.Deceleration;
        zombie.velocity = zombie.forward * MoveTowards(current, target, rate * deltaSeconds);
    }

    void ZombieSystem::Face(FZombieRuntime& zombie, const glm::vec3& target, float turnDegrees,
                            float deltaSeconds) const
    {
        Steer(zombie, target - zombie.position, 0.0f, turnDegrees, deltaSeconds);
    }

    bool ZombieSystem::NavigateToward(FZombieRuntime& zombie, const glm::vec3& target, float deltaSeconds,
                                      const FPathResolver& resolvePath, glm::vec3& outDirection) const
    {
        outDirection = glm::vec3(0.0f);
        const float repathInterval =
            zombie.state == EZombieState::Wander ? kRoamRepathSeconds : kChaseRepathSeconds;
        zombie.repathSeconds = std::max(0.0f, zombie.repathSeconds - deltaSeconds);
        const bool targetMoved = glm::length(Flat(target - zombie.pathTarget)) >= 2.0f;
        const bool wantsRoute = resolvePath && (zombie.repathSeconds <= 0.0f || targetMoved);
        if (wantsRoute && pathBudget_ <= 0)
        {
            ++pathDeferred_;
        }
        if (wantsRoute && pathBudget_ > 0)
        {
            --pathBudget_;
            ++pathSolves_;
            zombie.path = resolvePath(zombie.position, target);
            zombie.pathWaypoint = zombie.path.size() > 1 ? 1 : 0;
            zombie.pathTarget = target;
            zombie.repathSeconds = repathInterval;
            zombie.stuckSeconds = 0.0f;
            zombie.progressPosition = zombie.position;
            ++zombie.pathSolves;
            zombie.pathFailed = zombie.path.empty();
            if (zombie.path.empty())
            {
                return false;
            }
        }

        glm::vec3 waypoint = resolvePath ? zombie.position : target;
        if (resolvePath)
        {
            while (zombie.pathWaypoint < zombie.path.size() &&
                   glm::length(Flat(zombie.path[zombie.pathWaypoint] - zombie.position)) < kWaypointReach)
            {
                ++zombie.pathWaypoint;
            }
            if (zombie.pathWaypoint < zombie.path.size())
            {
                waypoint = zombie.path[zombie.pathWaypoint];
                // Waypoints steer horizontally. Only the movement resolver may
                // change floor height, after checking the actual geometry.
            }
        }

        const glm::vec3 delta = Flat(waypoint - zombie.position);
        const float distance = glm::length(delta);
        if (distance > 0.05f)
        {
            outDirection = delta / distance;
        }

        // Dynamic blockers are not part of the static grid: no progress for a
        // while drops the cached route so the next tick re-plans.
        if (glm::length(Flat(zombie.position - zombie.progressPosition)) > 0.25f)
        {
            zombie.stuckSeconds = 0.0f;
            zombie.progressPosition = zombie.position;
        }
        else if (glm::length(Flat(target - zombie.position)) > 0.5f)
        {
            zombie.stuckSeconds += deltaSeconds;
            if (zombie.stuckSeconds >= 1.5f)
            {
                zombie.path.clear();
                zombie.pathWaypoint = 0;
                zombie.repathSeconds = 0.0f;
                zombie.stuckSeconds = 0.0f;
                zombie.progressPosition = zombie.position;
            }
        }
        return true;
    }

    void ZombieSystem::UpdateAttack(FZombieRuntime& zombie, const FZombieTarget* target, float deltaSeconds)
    {
        const FZombieConfig& c = config_;
        const FZombieDef& definition = ZombieDef(zombie.profile);
        zombie.attackTimer += deltaSeconds;
        const float strike = AttackStrikeTime(zombie.attack);
        const bool playerAlive = target && target->alive;
        const glm::vec3 playerPosition = target ? target->position : zombie.position + zombie.forward;
        const glm::vec3 toPlayer = Flat(playerPosition - zombie.position);
        const float distance = glm::length(toPlayer);
        if (zombie.attackTimer < strike - c.TrackingLockLeadSeconds)
        {
            // Wind-up: keep tracking and close the last gap so the hands land.
            const float speed = distance > c.StrikeDistance ? kWindupCloseSpeed * zombie.variant.speedScale : 0.0f;
            Steer(zombie, toPlayer, speed, c.AttackTurnDegreesPerSecond, deltaSeconds);
        }
        else
        {
            // Committed: no more tracking, the body brakes into the strike.
            Steer(zombie, zombie.forward, 0.0f, 0.0f, deltaSeconds);
        }
        if (!zombie.attackCommitted && zombie.attackTimer >= strike)
        {
            zombie.attackCommitted = true;
            const bool facing = distance <= 0.01f || glm::dot(zombie.forward, toPlayer / distance) > c.StrikeFacingDot;
            if (playerAlive && zombie.seesPlayer &&
                std::abs(playerPosition.y - zombie.position.y) <= c.MaxAttackHeight &&
                distance <= definition.attackRange + c.StrikeReachBonus && facing)
            {
                damageRequests_.push_back({target->id, zombie.handle, definition.attackDamage, zombie.position});
            }
        }
        if (zombie.attackTimer >= AttackDuration(zombie.attack))
        {
            zombie.attackCooldown = c.AttackCooldownSeconds;
            zombie.state = EZombieState::Chase;
            zombie.stateSeconds = 0.0f;
        }
    }

    void ZombieSystem::Move(FZombieRuntime& zombie, const glm::vec3& displacement,
                             const FMovementResolver& resolveMovement) const
    {
        const glm::vec3 target = zombie.position + displacement;
        zombie.position = resolveMovement ? resolveMovement(zombie.position, target) : target;
    }

    void ZombieSystem::Separate(float deltaSeconds, std::span<const FZombieTarget> targets,
                                const FMovementResolver& resolveMovement)
    {
        const float radius = config_.SeparationRadius;
        const float personal = config_.PlayerPersonalSpace;
        const float blend = std::min(1.0f, 10.0f * deltaSeconds);
        for (size_t i = 0; i < slots_.size(); ++i)
        {
            FZombieRuntime& a = slots_[i];
            if (!Alive(a))
            {
                continue;
            }
            for (const FZombieTarget& target : targets)
            {
                const glm::vec3 fromPlayer = Flat(a.position - target.position);
                const float playerDistance = glm::length(fromPlayer);
                if (std::abs(a.position.y - target.position.y) < config_.ControllerHeight &&
                    playerDistance < personal && playerDistance > 1.0e-4f)
                {
                    Move(a, fromPlayer / playerDistance * (personal - playerDistance) * blend, resolveMovement);
                }
            }
            for (size_t j = i + 1; j < slots_.size(); ++j)
            {
                FZombieRuntime& b = slots_[j];
                if (!Alive(b) || std::abs(a.position.y - b.position.y) >= config_.ControllerHeight)
                {
                    continue;
                }
                const glm::vec3 delta = Flat(a.position - b.position);
                const float distance = glm::length(delta);
                if (distance >= radius)
                {
                    continue;
                }
                const glm::vec3 normal = distance > 1.0e-4f
                    ? delta / distance
                    : glm::vec3(std::cos(static_cast<float>(i)), 0.0f, std::sin(static_cast<float>(i)));
                const glm::vec3 push = normal * ((radius - distance) * 0.5f * blend);
                Move(a, push, resolveMovement);
                Move(b, -push, resolveMovement);
            }
        }
    }

    void ZombieSystem::Update(
        float deltaSeconds, const glm::vec3& playerPosition, const glm::vec3& /*playerForward*/, bool playerAlive,
        const FLineOfSight& hasLineOfSight, std::span<const FNoiseEvent> noises, const FPathResolver& resolvePath,
        const FMovementResolver& resolveMovement)
    {
        const FZombieTarget player{kLocalPlayerTargetId, playerPosition, playerAlive, 1.0f};
        Update(deltaSeconds, std::span<const FZombieTarget>(&player, 1), hasLineOfSight, noises, resolvePath,
               resolveMovement);
    }

    void ZombieSystem::Update(
        float deltaSeconds, std::span<const FZombieTarget> targets, const FLineOfSight& hasLineOfSight,
        std::span<const FNoiseEvent> noises, const FPathResolver& resolvePath, const FMovementResolver& resolveMovement)
    {
        // The survivor an infected measures itself against when it has no sight
        // of anyone: its current target if still in the world, else the nearest.
        const auto hunted = [targets](const FZombieRuntime& zombie) -> const FZombieTarget*
        {
            const FZombieTarget* nearest = nullptr;
            float nearestDistance = std::numeric_limits<float>::max();
            for (const FZombieTarget& target : targets)
            {
                if (zombie.targetId != 0 && target.id == zombie.targetId) return &target;
                const float distance = glm::length(Flat(target.position - zombie.position));
                if (distance < nearestDistance)
                {
                    nearest = &target;
                    nearestDistance = distance;
                }
            }
            return nearest;
        };
        const float dt = std::max(deltaSeconds, 0.0f);
        const FZombieConfig& c = config_;
        // A gunshot can wake a crowd at once: spread their route planning over
        // a few frames instead of solving every path in one.
        pathBudget_ = c.MaxPathSolvesPerUpdate;
        pathSolves_ = 0;
        pathDeferred_ = 0;
        for (FZombieRuntime& zombie : slots_)
        {
            if (!zombie.active)
            {
                continue;
            }
            zombie.seesPlayer = false;
            zombie.heardNoise = 0.0f;
            zombie.stateSeconds += dt;
            zombie.attackCooldown = std::max(0.0f, zombie.attackCooldown - dt);
            zombie.flinchSeconds = std::max(0.0f, zombie.flinchSeconds - dt);
            zombie.staggerImmunity = std::max(0.0f, zombie.staggerImmunity - dt);
            zombie.poise = std::max(0.0f, zombie.poise - c.PoiseRecoverPerSecond * dt);

            if (zombie.state == EZombieState::Dead)
            {
                // Momentum carries the body as it falls, then it lies still.
                zombie.corpseSeconds += dt;
                const float slide = std::max(0.0f, 1.0f - zombie.corpseSeconds / std::max(c.DeathSlideSeconds, 1.0e-3f));
                Move(zombie, zombie.deathVelocity * slide * dt, resolveMovement);
                zombie.velocity = {};
                if (zombie.corpseSeconds >= c.CorpseSeconds)
                {
                    Recycle(zombie.handle);
                }
                continue;
            }
            if (zombie.state == EZombieState::Stagger)
            {
                // Carried back along the hit, fast at first, then finding its feet.
                const float duration = std::max(c.StaggerSeconds, 1.0e-3f);
                const float u = std::clamp(zombie.stateSeconds / duration, 0.0f, 1.0f);
                zombie.velocity = zombie.staggerDirection * (c.StaggerPushback * 2.0f * (1.0f - u) / duration);
                Move(zombie, zombie.velocity * dt, resolveMovement);
                if (zombie.stateSeconds >= duration)
                {
                    zombie.state = zombie.aware ? EZombieState::Chase : EZombieState::Investigate;
                    zombie.stateSeconds = 0.0f;
                }
                continue;
            }

            // --- perception ---
            const FZombieTarget* seen = Perceive(zombie, targets, hasLineOfSight);
            const bool seesPlayer = seen != nullptr;
            zombie.seesPlayer = seesPlayer;
            if (seen)
            {
                zombie.targetId = seen->id;
            }
            const FZombieTarget* target = seen ? seen : hunted(zombie);
            const glm::vec3 playerPosition = target ? target->position : zombie.lastKnownPlayerPosition;
            if (seesPlayer)
            {
                zombie.lastKnownPlayerPosition = playerPosition;
                zombie.lostSightSeconds = 0.0f;
                if (zombie.state == EZombieState::Wander || zombie.state == EZombieState::Investigate)
                {
                    const float distance = glm::length(Flat(playerPosition - zombie.position));
                    const bool scream = !zombie.aware && c.AlertSeconds > 0.0f && distance > c.AlertSkipDistance;
                    zombie.state = scream ? EZombieState::Alert : EZombieState::Chase;
                    zombie.stateSeconds = 0.0f;
                }
                zombie.aware = true;
            }
            else if (zombie.state == EZombieState::Chase)
            {
                zombie.lostSightSeconds += dt;
                if (zombie.lostSightSeconds >= c.LostSightSeconds)
                {
                    zombie.state = EZombieState::Investigate;
                    zombie.stateSeconds = 0.0f;
                }
            }

            float bestNoiseScore = 0.0f;
            for (const FNoiseEvent& noise : noises)
            {
                if (noise.sequence <= zombie.lastNoiseSequence)
                {
                    continue;
                }
                const float distance = glm::distance(noise.position, zombie.position);
                if (distance <= noise.radius)
                {
                    const float score = noise.strength * (1.0f - distance / std::max(noise.radius, 0.01f));
                    if (score > bestNoiseScore)
                    {
                        bestNoiseScore = score;
                        if (!seesPlayer)
                        {
                            zombie.lastKnownPlayerPosition = noise.position;
                        }
                        zombie.lastNoiseSequence = noise.sequence;
                    }
                }
            }
            zombie.heardNoise = bestNoiseScore;
            if (bestNoiseScore > 0.0f &&
                (zombie.state == EZombieState::Wander || zombie.state == EZombieState::Investigate))
            {
                zombie.state = EZombieState::Investigate;
                zombie.stateSeconds = 0.0f;
            }

            // --- behaviour ---
            const FZombieDef& definition = ZombieDef(zombie.profile);
            const float speedScale = zombie.variant.speedScale * (zombie.flinchSeconds > 0.0f ? c.FlinchSpeedScale : 1.0f);
            const glm::vec3 toPlayer = Flat(playerPosition - zombie.position);
            const float playerDistance = glm::length(toPlayer);
            switch (zombie.state)
            {
            case EZombieState::Alert:
                Face(zombie, playerPosition, c.AlertTurnDegreesPerSecond, dt);
                if (zombie.stateSeconds >= c.AlertSeconds)
                {
                    zombie.state = EZombieState::Chase;
                    zombie.stateSeconds = 0.0f;
                }
                break;
            case EZombieState::Chase:
                if (seesPlayer && std::abs(playerPosition.y - zombie.position.y) <= c.MaxAttackHeight &&
                    playerDistance <= definition.attackRange && zombie.attackCooldown <= 0.0f)
                {
                    zombie.state = EZombieState::Attack;
                    zombie.stateSeconds = 0.0f;
                    zombie.attackTimer = 0.0f;
                    zombie.attackCommitted = false;
                    zombie.attack = (zombie.attackCount + zombie.handle.index) % 2 == 0 ? EZombieAttack::Lunge
                                                                                        : EZombieAttack::Swipe;
                    ++zombie.attackCount;
                    Face(zombie, playerPosition, c.AttackTurnDegreesPerSecond, dt);
                }
                else
                {
                    glm::vec3 direction(0.0f);
                    if (!NavigateToward(zombie, zombie.lastKnownPlayerPosition, dt, resolvePath, direction))
                    {
                        zombie.state = EZombieState::Investigate;
                        zombie.stateSeconds = 0.0f;
                        Steer(zombie, zombie.forward, 0.0f, 0.0f, dt);
                        break;
                    }
                    if (seesPlayer && std::abs(playerPosition.y - zombie.position.y) <= c.MaxStepHeight &&
                        playerDistance < kSteerDirectDistance && playerDistance > 0.01f)
                    {
                        direction = toPlayer / playerDistance;
                    }
                    const float speed = playerDistance <= c.PlayerPersonalSpace
                        ? 0.0f
                        : definition.chaseSpeed * speedScale * (zombie.Lame() ? c.CrippledSpeedScale : 1.0f);
                    Steer(zombie, direction, speed, c.ChaseTurnDegreesPerSecond, dt);
                }
                break;
            case EZombieState::Attack:
                UpdateAttack(zombie, target, dt);
                break;
            case EZombieState::Investigate:
            {
                glm::vec3 direction(0.0f);
                if (!NavigateToward(zombie, zombie.lastKnownPlayerPosition, dt, resolvePath, direction))
                {
                    zombie.state = EZombieState::Wander;
                    zombie.stateSeconds = 0.0f;
                    zombie.roamSeconds = 0.0f;
                    Steer(zombie, zombie.forward, 0.0f, 0.0f, dt);
                    break;
                }
                const float remaining = glm::length(Flat(zombie.lastKnownPlayerPosition - zombie.position));
                const bool pursuing = zombie.aware && zombie.stateSeconds < c.PursuitSearchSeconds;
                const float searchSpeed = pursuing ? definition.chaseSpeed : definition.wanderSpeed * 1.35f;
                const float lameScale = pursuing ? c.CrippledSpeedScale : c.LameWanderScale;
                const float speed = remaining > 0.35f
                    ? searchSpeed * speedScale * (zombie.Lame() ? lameScale : 1.0f)
                    : 0.0f;
                Steer(zombie, direction, speed,
                      pursuing ? c.ChaseTurnDegreesPerSecond : c.InvestigateTurnDegreesPerSecond, dt);
                if (remaining < 0.5f && zombie.stateSeconds > 6.0f)
                {
                    zombie.state = EZombieState::Wander;
                    zombie.stateSeconds = 0.0f;
                    zombie.roamSeconds = 0.0f;
                }
                break;
            }
            case EZombieState::Wander:
            {
                zombie.roamSeconds -= dt;
                if (zombie.roamSeconds <= 0.0f)
                {
                    zombie.roamTarget = NextRoamTarget(zombie);
                }
                glm::vec3 direction(0.0f);
                if (glm::length(Flat(zombie.roamTarget - zombie.position)) > 0.6f &&
                    NavigateToward(zombie, zombie.roamTarget, dt, resolvePath, direction))
                {
                    Steer(zombie, direction,
                          definition.wanderSpeed * speedScale * (zombie.Lame() ? c.LameWanderScale : 1.0f),
                          c.WanderTurnDegreesPerSecond, dt);
                }
                else
                {
                    Steer(zombie, zombie.forward, 0.0f, 0.0f, dt);
                }
                break;
            }
            default:
                break;
            }
            Move(zombie, zombie.velocity * dt, resolveMovement);
        }
        Separate(dt, targets, resolveMovement);
    }

    std::vector<FPlayerDamageRequest> ZombieSystem::ConsumePlayerDamageRequests()
    {
        std::vector<FPlayerDamageRequest> result;
        result.swap(damageRequests_);
        return result;
    }

    int ZombieSystem::ActiveCount() const
    {
        return static_cast<int>(std::count_if(slots_.begin(), slots_.end(),
            [](const FZombieRuntime& zombie) { return zombie.active; }));
    }

    int ZombieSystem::AlertedCount() const
    {
        return static_cast<int>(std::count_if(slots_.begin(), slots_.end(), [](const FZombieRuntime& zombie)
        {
            return zombie.active && (zombie.state == EZombieState::Investigate || zombie.state == EZombieState::Alert ||
                                     zombie.state == EZombieState::Chase || zombie.state == EZombieState::Attack);
        }));
    }
}
