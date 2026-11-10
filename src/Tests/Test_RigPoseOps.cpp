#include <catch2/catch_all.hpp>

#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Assets/Data/RigAsset.hpp"
#include "Gameplay/Rig/RigLayeredAnimator.h"
#include "Gameplay/Rig/RigPoseOps.h"

#include <memory>
#include <vector>

using namespace Assets;
using namespace NextGameplay;

namespace
{
    // An arm hanging down -Y: shoulder at (0, 2, 0), elbow 1 below, wrist 1 below that.
    FRigAsset MakeArmAsset()
    {
        FRigAsset asset;
        const auto add = [&asset](const char* name, int32_t parent, const glm::vec3& bindT)
        {
            FRigBone bone;
            bone.name = name;
            bone.parent = parent;
            bone.bindT = bindT;
            asset.bones.push_back(bone);
            if (parent >= 0)
            {
                asset.bones[parent].children.push_back(static_cast<int32_t>(asset.bones.size() - 1));
            }
        };
        add("bone_root", -1, glm::vec3(0.0f));
        add("bone_upper", 0, glm::vec3(0.0f, 2.0f, 0.0f));
        add("bone_lower", 1, glm::vec3(0.0f, -1.0f, 0.0f));
        add("bone_hand", 2, glm::vec3(0.0f, -1.0f, 0.0f));
        return asset;
    }

    FRigPose BindPose(const FRigAsset& asset)
    {
        FRigPose pose;
        pose.Reset(asset.bones.size());
        return pose;
    }
}

TEST_CASE("RigPoseOps forward kinematics matches the bind hierarchy", "[Unit][Rig][PoseOps]")
{
    const FRigAsset asset = MakeArmAsset();
    const FRigPose pose = BindPose(asset);
    std::vector<glm::mat4> transforms;
    ComputeRigComponentTransforms(asset, pose, transforms);

    REQUIRE(transforms.size() == 4);
    CHECK(RigBonePosition(transforms, 1).y == Catch::Approx(2.0f));
    CHECK(RigBonePosition(transforms, 2).y == Catch::Approx(1.0f));
    CHECK(RigBonePosition(transforms, 3).y == Catch::Approx(0.0f));
}

TEST_CASE("RigPoseOps two-bone IK reaches a target and bends toward the pole", "[Unit][Rig][PoseOps]")
{
    const FRigAsset asset = MakeArmAsset();
    FRigPose pose = BindPose(asset);
    std::vector<glm::mat4> transforms;
    ComputeRigComponentTransforms(asset, pose, transforms);

    // Hand forward and up: 1.2 m from the shoulder, elbow pushed down/back.
    const glm::vec3 target(0.0f, 2.2f, 1.2f);
    const glm::vec3 pole(0.0f, 0.0f, -1.0f);
    const FRigTwoBoneIKResult result = SolveRigTwoBoneIK(asset, pose, transforms, 1, 2, 3, target, pole);
    REQUIRE(result.solved);
    CHECK(result.reached);

    const glm::vec3 shoulder = RigBonePosition(transforms, 1);
    const glm::vec3 elbow = RigBonePosition(transforms, 2);
    const glm::vec3 wrist = RigBonePosition(transforms, 3);
    CHECK(glm::length(wrist - target) < 1.0e-3f);
    CHECK(glm::length(elbow - shoulder) == Catch::Approx(1.0f).margin(1.0e-4f));
    CHECK(glm::length(wrist - elbow) == Catch::Approx(1.0f).margin(1.0e-4f));

    // The elbow sits on the pole side of the shoulder-wrist line.
    const glm::vec3 axis = glm::normalize(target - shoulder);
    const glm::vec3 elbowOffset = (elbow - shoulder) - axis * glm::dot(elbow - shoulder, axis);
    const glm::vec3 poleOffset = (pole - shoulder) - axis * glm::dot(pole - shoulder, axis);
    CHECK(glm::dot(elbowOffset, poleOffset) > 0.0f);

    // The result is a pose, not a node write: refreshing from scratch agrees.
    std::vector<glm::mat4> fresh;
    ComputeRigComponentTransforms(asset, pose, fresh);
    CHECK(glm::length(RigBonePosition(fresh, 3) - target) < 1.0e-3f);
}

TEST_CASE("RigPoseOps two-bone IK extends toward unreachable targets and honours weight", "[Unit][Rig][PoseOps]")
{
    const FRigAsset asset = MakeArmAsset();

    FRigPose pose = BindPose(asset);
    std::vector<glm::mat4> transforms;
    ComputeRigComponentTransforms(asset, pose, transforms);
    const glm::vec3 outOfReach(0.0f, 2.0f, 5.0f);
    const FRigTwoBoneIKResult result =
        SolveRigTwoBoneIK(asset, pose, transforms, 1, 2, 3, outOfReach, glm::vec3(0.0f, 0.0f, -1.0f));
    REQUIRE(result.solved);
    CHECK_FALSE(result.reached);
    const glm::vec3 wrist = RigBonePosition(transforms, 3);
    CHECK(wrist.z == Catch::Approx(2.0f).margin(1.0e-3f));
    CHECK(wrist.y == Catch::Approx(2.0f).margin(1.0e-3f));

    FRigPose untouched = BindPose(asset);
    ComputeRigComponentTransforms(asset, untouched, transforms);
    SolveRigTwoBoneIK(asset, untouched, transforms, 1, 2, 3, glm::vec3(0.0f, 2.2f, 1.2f),
                      glm::vec3(0.0f, 0.0f, -1.0f), 0.0f);
    CHECK(RigBonePosition(transforms, 3).y == Catch::Approx(0.0f));
}

TEST_CASE("RigPoseOps component rotation edits survive the parent chain", "[Unit][Rig][PoseOps]")
{
    const FRigAsset asset = MakeArmAsset();
    FRigPose pose = BindPose(asset);
    std::vector<glm::mat4> transforms;
    ComputeRigComponentTransforms(asset, pose, transforms);

    RotateRigBoneInComponentSpace(asset, pose, transforms, 1,
                                  glm::angleAxis(glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f)));
    const glm::quat handTarget = glm::angleAxis(glm::radians(30.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    SetRigBoneComponentRotation(asset, pose, transforms, 3, handTarget);
    CHECK(std::abs(glm::dot(RigBoneRotation(transforms, 3), handTarget)) == Catch::Approx(1.0f).margin(1.0e-5f));

    SetRigBoneComponentPosition(asset, pose, transforms, 3, glm::vec3(0.5f, 2.0f, 2.0f));
    CHECK(glm::length(RigBonePosition(transforms, 3) - glm::vec3(0.5f, 2.0f, 2.0f)) < 1.0e-4f);
}

TEST_CASE("RigLayeredAnimator Evaluate then Apply equals Update", "[Unit][Rig][PoseOps]")
{
    FRigAsset asset = MakeArmAsset();
    FRigClip swing;
    swing.name = "swing";
    swing.duration = 1.0f;
    FRigChannel upper;
    upper.bone = 1;
    upper.rotation.Keys = {{0.0f, glm::quat(1.0f, 0.0f, 0.0f, 0.0f)},
                           {1.0f, glm::angleAxis(glm::radians(60.0f), glm::vec3(1.0f, 0.0f, 0.0f))}};
    swing.channels.push_back(upper);
    asset.clips.push_back(swing);

    std::vector<std::shared_ptr<Node>> nodes;
    std::vector<Node*> raw;
    for (size_t i = 0; i < asset.bones.size(); ++i)
    {
        nodes.push_back(Node::CreateNode(asset.bones[i].name, asset.bones[i].bindT, glm::quat(1, 0, 0, 0),
                                         glm::vec3(1.0f), static_cast<uint32_t>(100 + i)));
        if (asset.bones[i].parent >= 0)
        {
            nodes.back()->SetParent(nodes[asset.bones[i].parent]);
        }
        raw.push_back(nodes.back().get());
    }

    FRigLayeredAnimator animator;
    animator.Bind(&asset, raw, raw[0]);
    const FRigLayerHandle layer =
        animator.CreateLayer("base", ERigLayerBlendMode::Override, FRigBoneMask::FullBody(asset));
    animator.SetLoopBlend(layer, {{asset.FindClip("swing"), 1.0f}}, "base", 1.0f, 0.0f);

    animator.Evaluate(0.5f);
    std::vector<glm::mat4> transforms;
    ComputeRigComponentTransforms(asset, animator.Pose(), transforms);
    const glm::vec3 expectedWrist = RigBonePosition(transforms, 3);
    animator.Apply();

    const glm::vec3 wrist = glm::vec3(raw[3]->WorldTransform()[3]);
    CHECK(glm::length(wrist - expectedWrist) < 1.0e-4f);
    CHECK(animator.NormalizedTime(layer) == Catch::Approx(0.5f));
}
