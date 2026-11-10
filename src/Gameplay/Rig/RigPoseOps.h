#pragma once

// ============================================================================
// RigPoseOps.h - Component-space pose math for rigid-body rigs.
//
// FRigLayeredAnimator::Evaluate leaves a blended clip pose in bind-relative
// offsets; these helpers run procedural steps on that pose before Apply:
//   forward kinematics -> component-space bone transforms
//   rotation / position edits expressed in component space
//   two-bone IK (shoulder-elbow-wrist, hip-knee-ankle)
//
// "Component space" is the space the rig root bone's parent lives in (the
// character's world node). The edits assume unscaled chains; bind scale is
// honoured by forward kinematics but never solved for.
// ============================================================================

#include "Engine/Assets/Data/RigAsset.hpp"
#include "Gameplay/Rig/RigLayeredAnimator.h"

#include <vector>

namespace NextGameplay
{
    glm::mat4 RigBoneLocalMatrix(const Assets::FRigBone& bone, const FRigBonePose& pose);

    // Fills `out` (one matrix per bone) from the pose.
    void ComputeRigComponentTransforms(const Assets::FRigAsset& asset, const FRigPose& pose,
                                       std::vector<glm::mat4>& out);

    // Recomputes `bone` and every later bone. Bones are stored parents-first, so
    // this refreshes the subtree after an edit without a child walk.
    void RefreshRigComponentTransforms(const Assets::FRigAsset& asset, const FRigPose& pose, int32_t bone,
                                       std::vector<glm::mat4>& transforms);

    glm::vec3 RigBonePosition(const std::vector<glm::mat4>& transforms, int32_t bone);
    glm::quat RigBoneRotation(const std::vector<glm::mat4>& transforms, int32_t bone);

    // Shortest-arc rotation taking direction `from` onto direction `to`.
    glm::quat RigRotationBetween(const glm::vec3& from, const glm::vec3& to);

    // Pose edits. Each marks the component authored, then refreshes the subtree.
    void SetRigBoneComponentRotation(const Assets::FRigAsset& asset, FRigPose& pose,
                                     std::vector<glm::mat4>& transforms, int32_t bone,
                                     const glm::quat& componentRotation, float weight = 1.0f);
    void RotateRigBoneInComponentSpace(const Assets::FRigAsset& asset, FRigPose& pose,
                                       std::vector<glm::mat4>& transforms, int32_t bone,
                                       const glm::quat& componentDelta, float weight = 1.0f);
    void SetRigBoneComponentPosition(const Assets::FRigAsset& asset, FRigPose& pose,
                                     std::vector<glm::mat4>& transforms, int32_t bone,
                                     const glm::vec3& componentPosition, float weight = 1.0f);

    struct FRigTwoBoneIKResult
    {
        bool solved = false;   // false = degenerate chain, pose untouched
        bool reached = false;  // target within reach (otherwise the chain points at it, fully extended)
    };

    // Places `end`'s pivot at `target` by rotating `upper` and `lower`. The chain
    // bends toward `poleTarget` (a point, e.g. behind the elbow). `end`'s own
    // rotation is not touched; set it afterwards when the hand/foot must match.
    FRigTwoBoneIKResult SolveRigTwoBoneIK(const Assets::FRigAsset& asset, FRigPose& pose,
                                          std::vector<glm::mat4>& transforms, int32_t upper, int32_t lower,
                                          int32_t end, const glm::vec3& target, const glm::vec3& poleTarget,
                                          float weight = 1.0f);
} // namespace NextGameplay
