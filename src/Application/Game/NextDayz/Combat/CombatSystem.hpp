#pragma once

// ============================================================================
// CombatSystem.hpp - Turns weapon hits into infected damage. Rounds are traced
// against per-infected capsule hitboxes that follow the animated skeleton
// (head / torso / arms / legs); the world raycast only decides what is in the
// way. Instance-ID proxies remain for anything registered as a render node.
// Every applied hit becomes an FDamageEvent for presentation (hit markers,
// flinches, blood, audio).
// ============================================================================

#include "Engine/Common/CoreMinimal.hpp"

#include <array>
#include <span>

#include "Application/Game/NextDayz/Combat/CombatEvents.hpp"
#include "Application/Game/NextDayz/Zombies/ZombieSystem.hpp"

namespace NextDayz
{
    struct FHitProxy
    {
        FZombieHandle zombie{};
        EHitZone zone = EHitZone::Torso;
    };

    class FHitProxyRegistry
    {
    public:
        void Register(uint32_t instanceId, FZombieHandle zombie, EHitZone zone);
        void Unregister(FZombieHandle zombie);
        void Clear();
        std::optional<FHitProxy> Resolve(uint32_t instanceId) const;
        size_t Size() const { return proxies_.size(); }

    private:
        std::unordered_map<uint32_t, FHitProxy> proxies_;
    };

    // Nearest hit of a ray against a capsule, or nothing (origin inside counts as a miss).
    std::optional<float> RayCapsuleDistance(const glm::vec3& origin, const glm::vec3& direction,
                                            const FHitCapsule& capsule);

    class CombatSystem
    {
    public:
        static constexpr size_t kMaxHitCapsules = 16;

        explicit CombatSystem(ZombieSystem& zombies);

        FHitProxyRegistry& HitProxies() { return hitProxies_; }
        const FHitProxyRegistry& HitProxies() const { return hitProxies_; }

        // Hitboxes are world-space and refreshed by the visuals every frame.
        void SetHitboxes(FZombieHandle zombie, std::span<const FHitCapsule> capsules);
        void ClearHitboxes(FZombieHandle zombie);
        std::span<const FHitCapsule> Hitboxes(FZombieHandle zombie) const;
        std::optional<FHitboxHit> TraceHitboxes(const glm::vec3& origin, const glm::vec3& direction,
                                                float maxDistance) const;

        bool ProcessHit(const FWeaponHitEvent& event, float maximumDistance, float minimumDamageScale = 0.55f);
        bool ProcessMelee(uint64_t attackSequence, FZombieHandle target, float damage, EHitZone zone);
        std::vector<FDamageEvent> ConsumeDamageEvents();
        void Reset();

        FZombieHandle LastHitZombie() const { return lastHitZombie_; }
        EHitZone LastHitZone() const { return lastHitZone_; }
        int HeadshotCount() const { return headshotCount_; }

    private:
        struct FHitboxSet
        {
            FZombieHandle owner{};
            std::array<FHitCapsule, kMaxHitCapsules> capsules{};
            uint32_t count = 0;
            glm::vec3 center{};
            float radius = 0.0f;
        };

        bool Apply(FZombieHandle target, const FZombieDamage& damage, uint64_t sequence);

        ZombieSystem& zombies_;
        FHitProxyRegistry hitProxies_;
        std::vector<FHitboxSet> hitboxes_;
        std::vector<FDamageEvent> damageEvents_;
        FZombieHandle lastHitZombie_{};
        EHitZone lastHitZone_ = EHitZone::Torso;
        uint64_t lastMeleeSequence_ = 0;
        int headshotCount_ = 0;
    };
}
