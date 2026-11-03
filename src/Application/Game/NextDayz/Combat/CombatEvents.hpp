#pragma once

#include "Engine/Common/CoreMinimal.hpp"

#include <glm/vec3.hpp>

#include <compare>

namespace NextDayz
{
    enum class EHitZone : uint8_t
    {
        Head,
        Torso,
        Limb,   // arms
        Leg,
    };

    struct FZombieHandle
    {
        uint32_t index = 0;
        uint32_t generation = 0;

        bool IsValid() const { return generation != 0; }
        auto operator<=>(const FZombieHandle&) const = default;
    };

    // One capsule of an infected's hit body, in world space.
    struct FHitCapsule
    {
        glm::vec3 a{};
        glm::vec3 b{};
        float radius = 0.0f;
        EHitZone zone = EHitZone::Torso;
    };

    struct FHitboxHit
    {
        FZombieHandle zombie{};
        EHitZone zone = EHitZone::Torso;
        float distance = 0.0f;
        glm::vec3 point{};
        glm::vec3 normal{};
    };

    struct FWeaponHitEvent
    {
        uint64_t sequence = 0;
        std::string weaponId;
        glm::vec3 origin{};
        glm::vec3 direction{};
        uint32_t hitInstanceId = 0;
        glm::vec3 hitPoint{};
        float baseDamage = 0.0f;
        float impactScale = 1.0f;
        // Set when the round hit an infected's hitbox rather than the world.
        FZombieHandle zombie{};
        EHitZone zone = EHitZone::Torso;
        glm::vec3 normal{};
    };

    struct FWeaponTraceEvent
    {
        uint64_t sequence = 0;
        int pellet = 0;
        glm::vec3 origin{};
        glm::vec3 direction{};
        glm::vec3 endPoint{};
        uint32_t hitInstanceId = 0;
        bool hit = false;
        bool hitZombie = false;
        glm::vec3 normal{};
        bool melee = false;     // a swing: no tracer, no muzzle
    };

    struct FDamageEvent
    {
        uint64_t sequence = 0;
        FZombieHandle target{};
        EHitZone hitZone = EHitZone::Torso;
        float amount = 0.0f;
        float impact = 0.0f;
        glm::vec3 worldPoint{};
        glm::vec3 direction{};
        bool killed = false;
        bool staggered = false;
        bool crippled = false;
    };
}
