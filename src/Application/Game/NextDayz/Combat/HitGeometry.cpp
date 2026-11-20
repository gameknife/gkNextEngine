#include "Engine/Common/CoreMinimal.hpp"
#include "HitGeometry.hpp"

#include <glm/geometric.hpp>

namespace NextDayz
{
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

    size_t BuildInfectedHitboxes(const Assets::FRigDefinition& rig, std::span<const glm::mat4> transforms,
                                 const glm::mat4& world, std::span<FHitCapsule> capsules)
    {
        size_t count = 0;
        const auto add = [&](std::string_view name, const glm::vec3& a, const glm::vec3& b, float radius, EHitZone zone)
        {
            const int32_t bone = rig.FindBone(name);
            if (bone < 0 || bone >= static_cast<int32_t>(transforms.size()) || count >= capsules.size()) return;
            const glm::mat4 m = world * transforms[bone];
            capsules[count++] = {glm::vec3(m * glm::vec4(a, 1.0f)), glm::vec3(m * glm::vec4(b, 1.0f)), radius, zone};
        };
        add("bone_head", {0.0f, 0.07f, 0.0f}, {0.0f, 0.21f, 0.0f}, 0.12f, EHitZone::Head);
        add("bone_torso", {0.0f, 0.10f, 0.0f}, {0.0f, 0.40f, 0.0f}, 0.17f, EHitZone::Torso);
        add("bone_pelvis", {0.0f, -0.02f, 0.0f}, {0.0f, 0.05f, 0.0f}, 0.16f, EHitZone::Torso);
        add("bone_upperarm_l", {0.0f, -0.02f, 0.0f}, {0.0f, -0.30f, 0.0f}, 0.065f, EHitZone::Limb);
        add("bone_upperarm_r", {0.0f, -0.02f, 0.0f}, {0.0f, -0.30f, 0.0f}, 0.065f, EHitZone::Limb);
        add("bone_forearm_l", {0.0f, -0.01f, 0.0f}, {0.0f, -0.36f, 0.0f}, 0.055f, EHitZone::Limb);
        add("bone_forearm_r", {0.0f, -0.01f, 0.0f}, {0.0f, -0.36f, 0.0f}, 0.055f, EHitZone::Limb);
        add("bone_thigh_l", {0.0f, -0.03f, 0.0f}, {0.0f, -0.42f, 0.0f}, 0.09f, EHitZone::Leg);
        add("bone_thigh_r", {0.0f, -0.03f, 0.0f}, {0.0f, -0.42f, 0.0f}, 0.09f, EHitZone::Leg);
        add("bone_calf_l", {0.0f, -0.02f, 0.0f}, {0.0f, -0.40f, 0.0f}, 0.075f, EHitZone::Leg);
        add("bone_calf_r", {0.0f, -0.02f, 0.0f}, {0.0f, -0.40f, 0.0f}, 0.075f, EHitZone::Leg);
        return count;
    }
}
