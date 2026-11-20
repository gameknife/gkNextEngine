#pragma once

#include "Application/Game/NextDayz/Combat/CombatEvents.hpp"
#include "Engine/Assets/Data/RigDefinition.hpp"

#include <optional>
#include <span>

namespace NextDayz
{
    std::optional<float> RayCapsuleDistance(const glm::vec3& origin, const glm::vec3& direction,
                                            const FHitCapsule& capsule);
    size_t BuildInfectedHitboxes(const Assets::FRigDefinition& rig, std::span<const glm::mat4> transforms,
                                 const glm::mat4& world, std::span<FHitCapsule> capsules);
}
