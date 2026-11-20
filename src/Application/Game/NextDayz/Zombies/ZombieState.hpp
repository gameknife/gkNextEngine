#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>
#include <glm/vec3.hpp>
#include "Application/Game/NextDayz/Combat/CombatEvents.hpp"
#include "Application/Game/NextDayz/Data/ZombieDefs.hpp"

namespace NextDayz
{
    enum class EZombieState : uint8_t
    {
        Dormant,
        Wander,
        Investigate,
        Alert,
        Chase,
        Attack,
        Stagger,
        Dead,
    };
    inline constexpr size_t kZombieStateCount = static_cast<size_t>(EZombieState::Dead) + 1;

    inline const char* ZombieStateName(EZombieState state)
    {
        switch (state)
        {
        case EZombieState::Dormant: return "Dormant";
        case EZombieState::Wander: return "Wander";
        case EZombieState::Investigate: return "Investigate";
        case EZombieState::Alert: return "Alert";
        case EZombieState::Chase: return "Chase";
        case EZombieState::Attack: return "Attack";
        case EZombieState::Stagger: return "Stagger";
        case EZombieState::Dead: return "Dead";
        }
        return "Unknown";
    }

    enum class EZombieAttack : uint8_t
    {
        Lunge,
        Swipe,
    };

    enum class EZombieDeath : uint8_t
    {
        Back,
        Forward,
        Crumple,
    };

    inline const char* ZombieDeathName(EZombieDeath death)
    {
        switch (death)
        {
        case EZombieDeath::Forward: return "forward";
        case EZombieDeath::Crumple: return "crumple";
        case EZombieDeath::Back: break;
        }
        return "back";
    }

    // Per-spawn body language, hashed from the handle so a replay looks the same.
    struct FZombieVariant
    {
        float speedScale = 1.0f;
        float hunchDegrees = 0.0f;
        float headTiltDegrees = 0.0f;
        float reach = 0.5f;          // how eagerly the arms come up in a chase
        float phase = 0.0f;          // gait phase offset
        bool lame = false;           // born with a dragging leg
    };

    struct FZombieRuntime
    {
        FZombieHandle handle{};
        EZombieProfile profile = EZombieProfile::Civilian;
        EZombieState state = EZombieState::Dormant;
        glm::vec3 position{};
        glm::vec3 forward{0.0f, 0.0f, 1.0f};
        glm::vec3 velocity{};
        glm::vec3 spawnPosition{};
        glm::vec3 lastKnownPlayerPosition{};
        FZombieVariant variant{};
        float health = 100.0f;
        float stateSeconds = 0.0f;
        float attackTimer = 0.0f;
        float attackCooldown = 0.0f;
        float lostSightSeconds = 0.0f;
        float corpseSeconds = 0.0f;
        float repathSeconds = 0.0f;
        float stuckSeconds = 0.0f;
        float poise = 0.0f;
        float staggerImmunity = 0.0f;
        float flinchSeconds = 0.0f;
        float legDamage = 0.0f;
        float roamSeconds = 0.0f;
        uint32_t roamCount = 0;
        uint32_t attackCount = 0;
        EZombieAttack attack = EZombieAttack::Lunge;
        EZombieDeath death = EZombieDeath::Back;
        glm::vec3 staggerDirection{0.0f, 0.0f, -1.0f};
        glm::vec3 deathVelocity{};
        glm::vec3 roamTarget{};
        glm::vec3 pathTarget{};
        glm::vec3 progressPosition{};
        std::vector<glm::vec3> path;
        size_t pathWaypoint = 0;
        uint64_t lastNoiseSequence = 0;
        bool attackCommitted = false;
        bool crippled = false;
        bool aware = false;          // has seen or been shot by the player since spawning
        bool active = false;
        uint32_t targetId = 0;       // the survivor it is hunting (FZombieTarget::id), 0 = none
        // Written by Update: the last perception pass saw its target. The
        // debugger shows it; the attack rules also require it.
        bool seesPlayer = false;
        float heardNoise = 0.0f;     // loudest new noise heard in the last update (0 = none)
        uint32_t pathSolves = 0;     // routes planned since it spawned
        bool pathFailed = false;     // the last route came back empty

        bool Lame() const { return crippled || variant.lame; }
        float Speed() const;
    };
}
