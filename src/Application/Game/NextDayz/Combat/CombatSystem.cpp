#include "Engine/Common/CoreMinimal.hpp"

#include "CombatSystem.hpp"

#include <glm/geometric.hpp>

namespace NextDayz
{
    void FHitProxyRegistry::Register(uint32_t instanceId, FZombieHandle zombie, EHitZone zone)
    {
        if (instanceId != 0 && zombie.IsValid())
        {
            proxies_[instanceId] = {zombie, zone};
        }
    }

    void FHitProxyRegistry::Unregister(FZombieHandle zombie)
    {
        std::erase_if(proxies_, [zombie](const auto& entry) { return entry.second.zombie == zombie; });
    }

    void FHitProxyRegistry::Clear()
    {
        proxies_.clear();
    }

    std::optional<FHitProxy> FHitProxyRegistry::Resolve(uint32_t instanceId) const
    {
        const auto it = proxies_.find(instanceId);
        return it == proxies_.end() ? std::nullopt : std::optional<FHitProxy>(it->second);
    }

    std::optional<float> RayCapsuleDistance(const glm::vec3& origin, const glm::vec3& direction,
                                            const FHitCapsule& capsule)
    {
        // Cylinder body first, then whichever end cap the ray meets.
        const glm::vec3 ba = capsule.b - capsule.a;
        const glm::vec3 oa = origin - capsule.a;
        const float baba = glm::dot(ba, ba);
        const float bard = glm::dot(ba, direction);
        const float baoa = glm::dot(ba, oa);
        const float rdoa = glm::dot(direction, oa);
        const float oaoa = glm::dot(oa, oa);
        const float r2 = capsule.radius * capsule.radius;
        const float a = baba - bard * bard;
        if (a > 1.0e-8f)
        {
            const float b = baba * rdoa - baoa * bard;
            const float c = baba * oaoa - baoa * baoa - r2 * baba;
            const float h = b * b - a * c;
            if (h < 0.0f)
            {
                return std::nullopt;
            }
            const float t = (-b - std::sqrt(h)) / a;
            const float y = baoa + t * bard;
            if (y > 0.0f && y < baba)
            {
                return t >= 0.0f ? std::optional<float>(t) : std::nullopt;
            }
        }
        const auto sphere = [&](const glm::vec3& center) -> std::optional<float>
        {
            const glm::vec3 oc = origin - center;
            const float b = glm::dot(direction, oc);
            const float c = glm::dot(oc, oc) - r2;
            const float h = b * b - c;
            if (h < 0.0f)
            {
                return std::nullopt;
            }
            const float t = -b - std::sqrt(h);
            return t >= 0.0f ? std::optional<float>(t) : std::nullopt;
        };
        const std::optional<float> capA = sphere(capsule.a);
        const std::optional<float> capB = sphere(capsule.b);
        if (capA && capB)
        {
            return std::min(*capA, *capB);
        }
        return capA ? capA : capB;
    }

    CombatSystem::CombatSystem(ZombieSystem& zombies)
        : zombies_(zombies), hitboxes_(zombies.Capacity())
    {
    }

    void CombatSystem::SetHitboxes(FZombieHandle zombie, std::span<const FHitCapsule> capsules)
    {
        if (!zombie.IsValid() || zombie.index >= hitboxes_.size())
        {
            return;
        }
        FHitboxSet& set = hitboxes_[zombie.index];
        set.owner = zombie;
        set.count = static_cast<uint32_t>(std::min(capsules.size(), set.capsules.size()));
        glm::vec3 lo(1.0e9f);
        glm::vec3 hi(-1.0e9f);
        for (uint32_t i = 0; i < set.count; ++i)
        {
            const FHitCapsule& capsule = capsules[i];
            set.capsules[i] = capsule;
            lo = glm::min(lo, glm::min(capsule.a, capsule.b) - glm::vec3(capsule.radius));
            hi = glm::max(hi, glm::max(capsule.a, capsule.b) + glm::vec3(capsule.radius));
        }
        set.center = set.count > 0 ? (lo + hi) * 0.5f : glm::vec3(0.0f);
        set.radius = set.count > 0 ? glm::length(hi - lo) * 0.5f : 0.0f;
    }

    void CombatSystem::ClearHitboxes(FZombieHandle zombie)
    {
        if (zombie.IsValid() && zombie.index < hitboxes_.size() && hitboxes_[zombie.index].owner == zombie)
        {
            hitboxes_[zombie.index] = {};
        }
    }

    std::span<const FHitCapsule> CombatSystem::Hitboxes(FZombieHandle zombie) const
    {
        if (!zombie.IsValid() || zombie.index >= hitboxes_.size() || hitboxes_[zombie.index].owner != zombie)
        {
            return {};
        }
        const FHitboxSet& set = hitboxes_[zombie.index];
        return {set.capsules.data(), set.count};
    }

    std::optional<FHitboxHit> CombatSystem::TraceHitboxes(const glm::vec3& origin, const glm::vec3& direction,
                                                          float maxDistance) const
    {
        std::optional<FHitboxHit> best;
        float bestDistance = maxDistance;
        for (const FHitboxSet& set : hitboxes_)
        {
            if (set.count == 0)
            {
                continue;
            }
            const FZombieRuntime* zombie = zombies_.Resolve(set.owner);
            if (!zombie || zombie->state == EZombieState::Dead)
            {
                continue;
            }
            // Bounding-sphere reject before the capsules.
            const glm::vec3 oc = origin - set.center;
            const float b = glm::dot(direction, oc);
            const float c = glm::dot(oc, oc) - set.radius * set.radius;
            if (c > 0.0f && (b > 0.0f || b * b - c < 0.0f))
            {
                continue;
            }
            for (uint32_t i = 0; i < set.count; ++i)
            {
                const FHitCapsule& capsule = set.capsules[i];
                const std::optional<float> distance = RayCapsuleDistance(origin, direction, capsule);
                if (!distance || *distance >= bestDistance)
                {
                    continue;
                }
                bestDistance = *distance;
                const glm::vec3 point = origin + direction * *distance;
                const glm::vec3 axis = capsule.b - capsule.a;
                const float axisLength2 = glm::dot(axis, axis);
                const float t = axisLength2 > 1.0e-8f
                    ? std::clamp(glm::dot(point - capsule.a, axis) / axisLength2, 0.0f, 1.0f)
                    : 0.0f;
                const glm::vec3 outward = point - (capsule.a + axis * t);
                const float outwardLength = glm::length(outward);
                best = FHitboxHit{set.owner, capsule.zone, *distance, point,
                                  outwardLength > 1.0e-5f ? outward / outwardLength : -direction};
            }
        }
        return best;
    }

    bool CombatSystem::Apply(FZombieHandle target, const FZombieDamage& damage, uint64_t sequence)
    {
        const FZombieDamageResult result = zombies_.ApplyDamage(target, damage);
        if (!result.applied)
        {
            return false;
        }
        lastHitZombie_ = target;
        lastHitZone_ = damage.zone;
        if (damage.zone == EHitZone::Head)
        {
            ++headshotCount_;
        }
        FDamageEvent event;
        event.sequence = sequence;
        event.target = target;
        event.hitZone = damage.zone;
        event.amount = result.dealt;
        event.impact = damage.impact;
        event.worldPoint = damage.point;
        event.direction = damage.direction;
        event.killed = result.killed;
        event.staggered = result.staggered;
        event.crippled = result.crippled;
        damageEvents_.push_back(event);
        return true;
    }

    bool CombatSystem::ProcessHit(const FWeaponHitEvent& event, float maximumDistance, float minimumDamageScale)
    {
        FZombieHandle target = event.zombie;
        EHitZone zone = event.zone;
        if (!target.IsValid())
        {
            const std::optional<FHitProxy> proxy = hitProxies_.Resolve(event.hitInstanceId);
            if (!proxy)
            {
                return false;
            }
            target = proxy->zombie;
            zone = proxy->zone;
        }
        if (!zombies_.Resolve(target))
        {
            return false;
        }
        const float distance = glm::distance(event.origin, event.hitPoint);
        const float distanceScale = glm::clamp(1.0f - distance / std::max(maximumDistance, 0.01f),
                                               glm::clamp(minimumDamageScale, 0.0f, 1.0f), 1.0f);
        FZombieDamage damage;
        damage.amount = event.baseDamage * distanceScale;
        damage.zone = zone;
        damage.direction = event.direction;
        damage.point = event.hitPoint;
        damage.source = event.origin;
        damage.impact = damage.amount * event.impactScale;
        return Apply(target, damage, event.sequence);
    }

    bool CombatSystem::ProcessMelee(uint64_t attackSequence, FZombieHandle target, float damage, EHitZone zone)
    {
        if (attackSequence == 0 || attackSequence == lastMeleeSequence_)
        {
            return false;
        }
        lastMeleeSequence_ = attackSequence;
        const FZombieRuntime* zombie = zombies_.Resolve(target);
        if (!zombie)
        {
            return false;
        }
        FZombieDamage hit;
        hit.amount = damage;
        hit.zone = zone;
        hit.direction = -zombie->forward;
        hit.point = zombie->position + glm::vec3(0.0f, 1.4f, 0.0f);
        hit.source = zombie->position + zombie->forward;
        hit.impact = damage;
        return Apply(target, hit, attackSequence);
    }

    std::vector<FDamageEvent> CombatSystem::ConsumeDamageEvents()
    {
        std::vector<FDamageEvent> events;
        events.swap(damageEvents_);
        return events;
    }

    void CombatSystem::Reset()
    {
        hitProxies_.Clear();
        for (FHitboxSet& set : hitboxes_)
        {
            set = {};
        }
        damageEvents_.clear();
        lastHitZombie_ = {};
        lastHitZone_ = EHitZone::Torso;
        lastMeleeSequence_ = 0;
        headshotCount_ = 0;
    }
}
