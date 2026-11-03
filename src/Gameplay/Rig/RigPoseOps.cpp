#include "Gameplay/Rig/RigPoseOps.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>

namespace NextGameplay
{
    namespace
    {
        constexpr float kEpsilon = 1.0e-6f;
        const glm::quat kIdentity(1.0f, 0.0f, 0.0f, 0.0f);

        bool ValidBone(const std::vector<glm::mat4>& transforms, int32_t bone)
        {
            return bone >= 0 && bone < static_cast<int32_t>(transforms.size());
        }

        glm::mat4 ParentTransform(const Assets::FRigAsset& asset, const std::vector<glm::mat4>& transforms,
                                  int32_t bone)
        {
            const int32_t parent = asset.bones[bone].parent;
            return ValidBone(transforms, parent) ? transforms[parent] : glm::mat4(1.0f);
        }

        glm::quat RotationOf(const glm::mat4& matrix)
        {
            const glm::mat3 basis(glm::normalize(glm::vec3(matrix[0])), glm::normalize(glm::vec3(matrix[1])),
                                  glm::normalize(glm::vec3(matrix[2])));
            return glm::normalize(glm::quat_cast(basis));
        }

        glm::vec3 AnyPerpendicular(const glm::vec3& v)
        {
            const glm::vec3 axis = std::abs(v.x) < 0.9f ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
            return glm::normalize(glm::cross(v, axis));
        }
    }

    glm::mat4 RigBoneLocalMatrix(const Assets::FRigBone& bone, const FRigBonePose& pose)
    {
        const glm::vec3 translation = bone.bindT + bone.bindR * (bone.bindS * pose.t);
        const glm::quat rotation = glm::normalize(bone.bindR * pose.r);
        return glm::translate(glm::mat4(1.0f), translation) * glm::mat4_cast(rotation) *
               glm::scale(glm::mat4(1.0f), bone.bindS * pose.s);
    }

    void ComputeRigComponentTransforms(const Assets::FRigAsset& asset, const FRigPose& pose,
                                       std::vector<glm::mat4>& out)
    {
        out.resize(std::min(asset.bones.size(), pose.bones.size()));
        RefreshRigComponentTransforms(asset, pose, 0, out);
    }

    void RefreshRigComponentTransforms(const Assets::FRigAsset& asset, const FRigPose& pose, int32_t bone,
                                       std::vector<glm::mat4>& transforms)
    {
        const int32_t count = static_cast<int32_t>(std::min(transforms.size(), pose.bones.size()));
        for (int32_t i = std::max(bone, 0); i < count; ++i)
        {
            transforms[i] = ParentTransform(asset, transforms, i) * RigBoneLocalMatrix(asset.bones[i], pose.bones[i]);
        }
    }

    glm::vec3 RigBonePosition(const std::vector<glm::mat4>& transforms, int32_t bone)
    {
        return ValidBone(transforms, bone) ? glm::vec3(transforms[bone][3]) : glm::vec3(0.0f);
    }

    glm::quat RigBoneRotation(const std::vector<glm::mat4>& transforms, int32_t bone)
    {
        return ValidBone(transforms, bone) ? RotationOf(transforms[bone]) : kIdentity;
    }

    glm::quat RigRotationBetween(const glm::vec3& from, const glm::vec3& to)
    {
        const float fromLength = glm::length(from);
        const float toLength = glm::length(to);
        if (fromLength < kEpsilon || toLength < kEpsilon)
        {
            return kIdentity;
        }
        const glm::vec3 a = from / fromLength;
        const glm::vec3 b = to / toLength;
        const float cosine = glm::dot(a, b);
        if (cosine > 1.0f - 1.0e-6f)
        {
            return kIdentity;
        }
        if (cosine < -1.0f + 1.0e-6f)
        {
            return glm::angleAxis(glm::pi<float>(), AnyPerpendicular(a));
        }
        const glm::vec3 axis = glm::cross(a, b);
        return glm::normalize(glm::quat(1.0f + cosine, axis.x, axis.y, axis.z));
    }

    void SetRigBoneComponentRotation(const Assets::FRigAsset& asset, FRigPose& pose,
                                     std::vector<glm::mat4>& transforms, int32_t bone,
                                     const glm::quat& componentRotation, float weight)
    {
        if (!ValidBone(transforms, bone) || weight <= kEpsilon)
        {
            return;
        }
        const Assets::FRigBone& rigBone = asset.bones[bone];
        const glm::quat parentRotation = RotationOf(ParentTransform(asset, transforms, bone));
        const glm::quat localRotation = glm::inverse(parentRotation) * componentRotation;
        glm::quat target = glm::normalize(glm::inverse(rigBone.bindR) * localRotation);

        FRigBonePose& bonePose = pose.bones[bone];
        if (glm::dot(bonePose.r, target) < 0.0f)
        {
            target = -target;
        }
        bonePose.r = weight >= 1.0f ? target : glm::normalize(glm::slerp(bonePose.r, target, weight));
        bonePose.Mark(ERigPoseComponent::Rotation);
        RefreshRigComponentTransforms(asset, pose, bone, transforms);
    }

    void RotateRigBoneInComponentSpace(const Assets::FRigAsset& asset, FRigPose& pose,
                                       std::vector<glm::mat4>& transforms, int32_t bone,
                                       const glm::quat& componentDelta, float weight)
    {
        if (!ValidBone(transforms, bone))
        {
            return;
        }
        SetRigBoneComponentRotation(asset, pose, transforms, bone,
                                    glm::normalize(componentDelta * RigBoneRotation(transforms, bone)), weight);
    }

    void SetRigBoneComponentPosition(const Assets::FRigAsset& asset, FRigPose& pose,
                                     std::vector<glm::mat4>& transforms, int32_t bone,
                                     const glm::vec3& componentPosition, float weight)
    {
        if (!ValidBone(transforms, bone) || weight <= kEpsilon)
        {
            return;
        }
        const Assets::FRigBone& rigBone = asset.bones[bone];
        const glm::vec3 localPosition =
            glm::vec3(glm::inverse(ParentTransform(asset, transforms, bone)) * glm::vec4(componentPosition, 1.0f));
        const glm::vec3 bindScale = glm::max(glm::abs(rigBone.bindS), glm::vec3(kEpsilon)) * glm::sign(rigBone.bindS);
        const glm::vec3 target = (glm::inverse(rigBone.bindR) * (localPosition - rigBone.bindT)) / bindScale;

        FRigBonePose& bonePose = pose.bones[bone];
        bonePose.t = weight >= 1.0f ? target : glm::mix(bonePose.t, target, weight);
        bonePose.Mark(ERigPoseComponent::Position);
        RefreshRigComponentTransforms(asset, pose, bone, transforms);
    }

    FRigTwoBoneIKResult SolveRigTwoBoneIK(const Assets::FRigAsset& asset, FRigPose& pose,
                                          std::vector<glm::mat4>& transforms, int32_t upper, int32_t lower,
                                          int32_t end, const glm::vec3& target, const glm::vec3& poleTarget,
                                          float weight)
    {
        FRigTwoBoneIKResult result;
        if (!ValidBone(transforms, upper) || !ValidBone(transforms, lower) || !ValidBone(transforms, end) ||
            weight <= kEpsilon)
        {
            return result;
        }

        const glm::vec3 a = RigBonePosition(transforms, upper);
        const glm::vec3 b = RigBonePosition(transforms, lower);
        const glm::vec3 c = RigBonePosition(transforms, end);
        const float upperLength = glm::length(b - a);
        const float lowerLength = glm::length(c - b);
        const glm::vec3 toTarget = target - a;
        const float targetDistance = glm::length(toTarget);
        if (upperLength < kEpsilon || lowerLength < kEpsilon || targetDistance < kEpsilon)
        {
            return result;
        }

        const float minReach = std::abs(upperLength - lowerLength) + 1.0e-4f;
        const float maxReach = upperLength + lowerLength - 1.0e-4f;
        const float distance = std::clamp(targetDistance, minReach, maxReach);
        const glm::vec3 direction = toTarget / targetDistance;

        // Bend plane: the pole projected off the reach axis; fall back to the
        // pose's current bend when the pole is on the axis.
        glm::vec3 bend = (poleTarget - a) - direction * glm::dot(poleTarget - a, direction);
        if (glm::length(bend) < 1.0e-4f)
        {
            bend = (b - a) - direction * glm::dot(b - a, direction);
        }
        bend = glm::length(bend) < 1.0e-4f ? AnyPerpendicular(direction) : glm::normalize(bend);

        const float cosine = std::clamp(
            (upperLength * upperLength + distance * distance - lowerLength * lowerLength) /
                (2.0f * upperLength * distance),
            -1.0f, 1.0f);
        const float sine = std::sqrt(std::max(0.0f, 1.0f - cosine * cosine));
        const glm::vec3 desiredJoint = a + direction * (upperLength * cosine) + bend * (upperLength * sine);
        const glm::vec3 desiredEnd = a + direction * distance;

        const glm::quat upperBefore = pose.bones[upper].r;
        const glm::quat lowerBefore = pose.bones[lower].r;
        const bool upperHadRotation = pose.bones[upper].Has(ERigPoseComponent::Rotation);
        const bool lowerHadRotation = pose.bones[lower].Has(ERigPoseComponent::Rotation);

        RotateRigBoneInComponentSpace(asset, pose, transforms, upper, RigRotationBetween(b - a, desiredJoint - a));
        const glm::vec3 joint = RigBonePosition(transforms, lower);
        const glm::vec3 effector = RigBonePosition(transforms, end);
        RotateRigBoneInComponentSpace(asset, pose, transforms, lower,
                                      RigRotationBetween(effector - joint, desiredEnd - joint));

        if (weight < 1.0f)
        {
            const auto blendBack = [&](int32_t bone, const glm::quat& before, bool hadRotation)
            {
                FRigBonePose& bonePose = pose.bones[bone];
                glm::quat from = hadRotation ? before : kIdentity;
                if (glm::dot(from, bonePose.r) < 0.0f)
                {
                    from = -from;
                }
                bonePose.r = glm::normalize(glm::slerp(from, bonePose.r, weight));
            };
            blendBack(upper, upperBefore, upperHadRotation);
            blendBack(lower, lowerBefore, lowerHadRotation);
            RefreshRigComponentTransforms(asset, pose, std::min(upper, lower), transforms);
        }

        result.solved = true;
        result.reached = targetDistance <= maxReach + 1.0e-3f && targetDistance >= minReach - 1.0e-3f;
        return result;
    }
} // namespace NextGameplay
