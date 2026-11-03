#include "ZombieRigVisual.hpp"

#include <algorithm>
#include <cmath>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <spdlog/spdlog.h>

#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Runtime/Components/PhysicsComponent.hpp"
#include "Engine/Runtime/Scene/NodeUtils.hpp"
#include "Gameplay/Rig/RigPoseOps.h"

namespace NextDayz
{
    namespace
    {
        constexpr float kParkedY = -1000.0f;
        const glm::vec3 kAxisX(1.0f, 0.0f, 0.0f);
        const glm::vec3 kAxisY(0.0f, 1.0f, 0.0f);
        const glm::vec3 kAxisZ(0.0f, 0.0f, 1.0f);

        float Approach(float current, float target, float seconds, float dt)
        {
            return seconds > 0.0f ? glm::mix(current, target, 1.0f - std::exp(-dt / seconds)) : target;
        }

        float WrapAngle(float radians)
        {
            return std::remainder(radians, glm::two_pi<float>());
        }

        glm::quat Euler(const glm::vec3& pitchYawRoll)
        {
            return glm::angleAxis(pitchYawRoll.y, kAxisY) * glm::angleAxis(pitchYawRoll.x, kAxisX) *
                   glm::angleAxis(pitchYawRoll.z, kAxisZ);
        }

        void StepSpring(glm::vec3& value, glm::vec3& velocity, float spring, float damping, float dt)
        {
            velocity += (-spring * value - damping * velocity) * dt;
            value += velocity * dt;
        }

        void StepSpring(glm::vec2& state, float spring, float damping, float dt)
        {
            state.y += (-spring * state.x - damping * state.y) * dt;
            state.x += state.y * dt;
        }

        glm::vec3 ClampLength(const glm::vec3& v, float maxLength)
        {
            const float length = glm::length(v);
            return length > maxLength && length > 0.0f ? v * (maxLength / length) : v;
        }
    }

    ZombieRigVisual::FClips ZombieRigVisual::FClips::Resolve(const Assets::FRigAsset& asset)
    {
        const auto clip = [&asset](const char* name)
        {
            const Assets::FRigClip* found = asset.FindClip(name);
            if (!found)
            {
                SPDLOG_WARN("[NextDayz] infected rig is missing clip '{}'", name);
            }
            return found;
        };
        FClips clips;
        clips.idle = clip("idle");
        clips.walk = clip("walk");
        clips.walkLimp = clip("walk_limp");
        clips.run = clip("run");
        clips.hobble = clip("hobble");
        clips.reach = clip("reach");
        clips.alert = clip("alert");
        clips.attackLunge = clip("attack_lunge");
        clips.attackSwipe = clip("attack_swipe");
        clips.stagger = clip("stagger");
        clips.deathBack = clip("death_back");
        clips.deathForward = clip("death_forward");
        clips.deathCrumple = clip("death_crumple");
        return clips;
    }

    bool ZombieRigVisual::Instantiate(Assets::Scene& scene, const Assets::FRigAsset& asset, const FClips& clips,
                                      const NextGameplay::FRigInstanceDesc& desc, const std::string& name)
    {
        asset_ = &asset;
        clips_ = &clips;
        worldNode_ = Assets::Node::CreateNode(name, glm::vec3(0.0f, kParkedY, 0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
                                              glm::vec3(1.0f), scene.GenerateInstanceId());
        auto physics = std::make_shared<Runtime::PhysicsComponent>();
        physics->SetMobility(Runtime::ENodeMobility::Dynamic);
        worldNode_->AddComponent(physics);
        scene.AddNode(worldNode_);

        std::vector<Assets::Node*> boneNodes;
        std::shared_ptr<Assets::Node> rigRoot = NextGameplay::FRigInstance::Instantiate(scene, asset, desc, boneNodes);
        if (!rigRoot)
        {
            worldNode_.reset();
            return false;
        }
        rigRoot->SetParent(worldNode_);
        animator_ = {};
        animator_.Bind(&asset, std::move(boneNodes), worldNode_.get());

        bones_.root = asset.FindBone("bone_root");
        bones_.pelvis = asset.FindBone("bone_pelvis");
        bones_.torso = asset.FindBone("bone_torso");
        bones_.head = asset.FindBone("bone_head");
        bones_.upperArmL = asset.FindBone("bone_upperarm_l");
        bones_.foreArmL = asset.FindBone("bone_forearm_l");
        bones_.upperArmR = asset.FindBone("bone_upperarm_r");
        bones_.foreArmR = asset.FindBone("bone_forearm_r");
        bones_.thighL = asset.FindBone("bone_thigh_l");
        bones_.calfL = asset.FindBone("bone_calf_l");
        bones_.thighR = asset.FindBone("bone_thigh_r");
        bones_.calfR = asset.FindBone("bone_calf_r");

        using NextGameplay::ERigLayerBlendMode;
        using NextGameplay::FRigBoneMask;
        FRigBoneMask arms = FRigBoneMask::FromSubtree(asset, "bone_upperarm_l");
        for (const char* bone : {"bone_upperarm_r", "bone_forearm_r", "bone_hand_r"})
        {
            arms.SetBoneWeight(asset, bone, 1.0f);
        }
        baseLayer_ = animator_.CreateLayer("base", ERigLayerBlendMode::Override, FRigBoneMask::FullBody(asset));
        reachLayer_ = animator_.CreateLayer("reach", ERigLayerBlendMode::Override, std::move(arms));
        actionLayer_ = animator_.CreateLayer("action", ERigLayerBlendMode::Override, FRigBoneMask::FullBody(asset));
        deathLayer_ = animator_.CreateLayer("death", ERigLayerBlendMode::Override, FRigBoneMask::FullBody(asset));
        animator_.SetLayerWeight(reachLayer_, 0.0f);
        animator_.SetLayerWeight(actionLayer_, 0.0f);
        animator_.SetLayerWeight(deathLayer_, 0.0f);

        Assets::NodeUtils::SetVisibleRecursive(worldNode_, false);
        // Presentation only: rounds are traced against Hitboxes(), never the parts.
        Assets::NodeUtils::SetRayCastVisibleRecursive(worldNode_, false);
        visible_ = false;
        return true;
    }

    void ZombieRigVisual::Release()
    {
        worldNode_.reset();
        animator_ = {};
        asset_ = nullptr;
        clips_ = nullptr;
        hitboxCount_ = 0;
        visible_ = false;
    }

    void ZombieRigVisual::Reset(const FZombieRuntime& zombie)
    {
        yaw_ = std::atan2(zombie.forward.x, zombie.forward.z);
        yawRate_ = 0.0f;
        turnLean_ = 0.0f;
        lookWeight_ = 0.0f;
        reachWeight_ = 0.0f;
        actionActive_ = false;
        deathActive_ = false;
        torsoFlinch_ = {};
        headFlinch_ = {};
        pelvisDrop_ = glm::vec2(0.0f);
        armKickL_ = glm::vec2(0.0f);
        armKickR_ = glm::vec2(0.0f);
        hitboxCount_ = 0;
        if (!worldNode_ || !clips_)
        {
            return;
        }
        animator_.SetLayerWeight(reachLayer_, 0.0f);
        animator_.SetLayerWeight(actionLayer_, 0.0f);
        animator_.SetLayerWeight(deathLayer_, 0.0f);
        animator_.SetLoopBlend(baseLayer_, {{clips_->idle, 1.0f}}, "gait", 1.0f, 0.0f);
        // Desynchronise a crowd: each body starts somewhere else in its cycle.
        animator_.Evaluate(zombie.variant.phase * (clips_->idle ? clips_->idle->duration : 1.0f));
    }

    void ZombieRigVisual::AddHit(const FDamageEvent& hit, const FZombieConfig& config)
    {
        if (!worldNode_)
        {
            return;
        }
        const glm::quat inverseYaw = glm::angleAxis(-yaw_, kAxisY);
        glm::vec3 direction = inverseYaw * hit.direction;
        direction.y = 0.0f;
        const float directionLength = glm::length(direction);
        direction = directionLength > 1.0e-4f ? direction / directionLength : glm::vec3(0.0f, 0.0f, -1.0f);
        const glm::vec3 point = inverseYaw * (hit.worldPoint - worldNode_->Translation());
        const float kick = std::min(hit.impact * config.FlinchPerImpact, config.FlinchMaxSpeed);

        // Pitch follows the round (from the front -> rocked back), roll pushes
        // the top sideways, and an off-centre hit twists the chest.
        const glm::vec3 lever = point - glm::vec3(0.0f, 1.2f, 0.0f);
        const glm::vec3 torque(kick * direction.z, kick * 4.0f * (lever.z * direction.x - lever.x * direction.z),
                               -kick * direction.x);
        float torsoShare = 1.0f;
        switch (hit.hitZone)
        {
        case EHitZone::Head:
            headFlinch_.velocity += torque * config.HeadFlinchScale;
            torsoShare = 0.4f;
            break;
        case EHitZone::Leg:
            // The leg folds under it for a moment and it lurches forward.
            pelvisDrop_.y += config.LegBuckleSpeed * std::min(1.0f, hit.impact / 20.0f);
            torsoFlinch_.velocity.x += kick * 0.6f;
            torsoShare = 0.3f;
            break;
        case EHitZone::Limb:
            (point.x >= 0.0f ? armKickL_ : armKickR_).y += kick * 2.5f * direction.z;
            torsoShare = 0.5f;
            break;
        case EHitZone::Torso:
            headFlinch_.velocity += torque * 0.5f;
            break;
        }
        torsoFlinch_.velocity += torque * torsoShare;
        torsoFlinch_.velocity = ClampLength(torsoFlinch_.velocity, config.FlinchMaxSpeed);
        headFlinch_.velocity = ClampLength(headFlinch_.velocity, config.FlinchMaxSpeed * config.HeadFlinchScale);
    }

    void ZombieRigVisual::Hide()
    {
        hitboxCount_ = 0;
        if (!worldNode_)
        {
            return;
        }
        worldNode_->Translation() = glm::vec3(0.0f, kParkedY, 0.0f);
        worldNode_->RecalcTransform(true);
        if (visible_)
        {
            Assets::NodeUtils::SetVisibleRecursive(worldNode_, false);
            visible_ = false;
        }
    }

    float ZombieRigVisual::FlinchDegrees() const
    {
        return glm::degrees(std::max(glm::length(torsoFlinch_.value), glm::length(headFlinch_.value)));
    }

    void ZombieRigVisual::UpdateLayers(const FZombieRuntime& zombie, const glm::vec3& lookTarget,
                                       const FZombieConfig& config, float dt)
    {
        const FClips& clips = *clips_;

        // Base: idle -> walk -> run by the simulated speed.
        const bool lame = zombie.Lame();
        const Assets::FRigClip* walk = lame ? clips.walkLimp : clips.walk;
        const Assets::FRigClip* run = lame ? clips.hobble : clips.run;
        const float walkSpeed = lame ? config.LimpAuthoredSpeed : config.WalkAuthoredSpeed;
        const float runSpeed = lame ? config.HobbleAuthoredSpeed : config.RunAuthoredSpeed;
        const float speed = zombie.Speed();
        float idleWeight = 0.0f;
        float walkWeight = 0.0f;
        float runWeight = 0.0f;
        if (speed < walkSpeed)
        {
            walkWeight = speed / walkSpeed;
            idleWeight = 1.0f - walkWeight;
        }
        else if (speed < runSpeed)
        {
            runWeight = (speed - walkSpeed) / (runSpeed - walkSpeed);
            walkWeight = 1.0f - runWeight;
        }
        else
        {
            runWeight = 1.0f;
        }
        // Cadence-match the gait clips to the ground speed so the feet plant.
        const float gaitWeight = walkWeight + runWeight;
        float playRate = 1.0f;
        if (gaitWeight > 1.0e-3f && walk && run)
        {
            const float cadence = (walkWeight / walk->duration + runWeight / run->duration) / gaitWeight;
            const float authored = (walkWeight * walkSpeed + runWeight * runSpeed) / gaitWeight;
            const float overspeed = std::clamp(speed / std::max(authored, 0.01f), config.MinPlayRate, config.MaxPlayRate);
            const Assets::FRigClip* dominant =
                idleWeight > std::max(walkWeight, runWeight) ? clips.idle : (runWeight > walkWeight ? run : walk);
            playRate = cadence * overspeed * (dominant ? dominant->duration : 1.0f);
        }
        animator_.SetLoopBlend(baseLayer_, {{clips.idle, idleWeight}, {walk, walkWeight}, {run, runWeight}}, "gait",
                               playRate, 0.2f);
        baseClipName_ = runWeight > std::max(walkWeight, idleWeight)
            ? (lame ? "hobble" : "run")
            : (walkWeight > idleWeight ? (lame ? "walk_limp" : "walk") : "idle");

        // Reach: the arms come up as a chase closes in.
        const float distance = glm::length(glm::vec2(lookTarget.x - zombie.position.x, lookTarget.z - zombie.position.z));
        const float closing = 1.0f - std::clamp(distance / std::max(config.ReachDistance, 0.1f), 0.0f, 1.0f);
        const float reachTarget = zombie.state == EZombieState::Chase ? zombie.variant.reach * std::sqrt(closing) : 0.0f;
        reachWeight_ = Approach(reachWeight_, reachTarget, 0.18f, dt);
        animator_.SetLoopBlend(reachLayer_, {{clips.reach, 1.0f}}, "reach", 1.0f, 0.0f);
        animator_.SetLayerWeight(reachLayer_, reachWeight_);

        // Actions: sampled by the simulation's timers.
        const Assets::FRigClip* action = nullptr;
        float actionTime = 0.0f;
        switch (zombie.state)
        {
        case EZombieState::Alert:
            action = clips.alert;
            actionTime = zombie.stateSeconds / std::max(config.AlertSeconds, 1.0e-3f);
            actionClipName_ = "alert";
            break;
        case EZombieState::Attack:
        {
            const bool lunge = zombie.attack == EZombieAttack::Lunge;
            action = lunge ? clips.attackLunge : clips.attackSwipe;
            actionTime = zombie.attackTimer / std::max(lunge ? config.LungeSeconds : config.SwipeSeconds, 1.0e-3f);
            actionClipName_ = lunge ? "attack_lunge" : "attack_swipe";
            break;
        }
        case EZombieState::Stagger:
            action = clips.stagger;
            actionTime = zombie.stateSeconds / std::max(config.StaggerSeconds, 1.0e-3f);
            actionClipName_ = "stagger";
            break;
        default:
            break;
        }
        if (action)
        {
            animator_.SetManualBlend(actionLayer_, {{action, 1.0f}}, std::clamp(actionTime, 0.0f, 1.0f),
                                     actionActive_ ? config.ActionFadeSeconds : 0.0f);
            if (!actionActive_)
            {
                animator_.SetLayerWeight(actionLayer_, 1.0f, config.ActionFadeSeconds);
                actionActive_ = true;
            }
        }
        else if (actionActive_)
        {
            animator_.SetLayerWeight(actionLayer_, 0.0f, 0.18f);
            actionActive_ = false;
            actionClipName_ = "";
        }

        // Death: the fall the simulation picked, held once it lands.
        if (zombie.state == EZombieState::Dead)
        {
            const Assets::FRigClip* death = zombie.death == EZombieDeath::Forward
                ? clips.deathForward
                : (zombie.death == EZombieDeath::Crumple ? clips.deathCrumple : clips.deathBack);
            const float duration = death && death->duration > 0.0f ? death->duration : 1.0f;
            animator_.SetManualBlend(deathLayer_, {{death, 1.0f}},
                                     std::clamp(zombie.corpseSeconds / duration, 0.0f, 1.0f), 0.0f);
            if (!deathActive_)
            {
                animator_.SetLayerWeight(deathLayer_, 1.0f, 0.07f);
                deathActive_ = true;
            }
        }
    }

    void ZombieRigVisual::UpdateProcedural(const FZombieRuntime& zombie, const glm::vec3& lookTarget,
                                           const FZombieConfig& config, float dt)
    {
        using namespace NextGameplay;
        const Assets::FRigAsset& asset = *asset_;
        FRigPose& pose = animator_.MutablePose();
        ComputeRigComponentTransforms(asset, pose, transforms_);
        const auto valid = [this](int32_t bone) { return bone >= 0 && bone < static_cast<int32_t>(transforms_.size()); };
        const bool dead = zombie.state == EZombieState::Dead;

        // Lean into turns like a runner banking.
        if (valid(bones_.root) && std::abs(turnLean_) > 1.0e-3f)
        {
            RotateRigBoneInComponentSpace(asset, pose, transforms_, bones_.root,
                                          glm::angleAxis(glm::radians(turnLean_), kAxisZ));
        }

        // Leg-hit buckle.
        if (valid(bones_.pelvis) && std::abs(pelvisDrop_.x) > 1.0e-4f)
        {
            SetRigBoneComponentPosition(asset, pose, transforms_, bones_.pelvis,
                                        RigBonePosition(transforms_, bones_.pelvis) - kAxisY * pelvisDrop_.x);
        }

        // Posture: every body carries itself a little differently.
        const float alive = dead ? 0.0f : 1.0f;
        if (valid(bones_.torso))
        {
            RotateRigBoneInComponentSpace(asset, pose, transforms_, bones_.torso,
                                          glm::angleAxis(glm::radians(zombie.variant.hunchDegrees) * alive, kAxisX));
        }
        if (valid(bones_.head))
        {
            RotateRigBoneInComponentSpace(asset, pose, transforms_, bones_.head,
                                          glm::angleAxis(glm::radians(zombie.variant.headTiltDegrees) * alive, kAxisZ));
        }

        // Head and chest track the prey.
        if (lookWeight_ > 1.0e-3f && valid(bones_.head) && valid(bones_.torso))
        {
            const glm::vec3 local = glm::angleAxis(-yaw_, kAxisY) * (lookTarget - worldNode_->Translation());
            const glm::vec3 toTarget = local - RigBonePosition(transforms_, bones_.head);
            const glm::vec3 headForward = RigBoneRotation(transforms_, bones_.head) * kAxisZ;
            const float targetYaw = std::atan2(toTarget.x, toTarget.z);
            const float targetPitch = std::atan2(toTarget.y, glm::length(glm::vec2(toTarget.x, toTarget.z)));
            const float headYaw = std::atan2(headForward.x, headForward.z);
            const float headPitch = std::atan2(headForward.y, glm::length(glm::vec2(headForward.x, headForward.z)));
            const float yawLimit = glm::radians(config.LookYawLimitDegrees);
            const float pitchLimit = glm::radians(config.LookPitchLimitDegrees);
            const float yaw = std::clamp(WrapAngle(targetYaw - headYaw), -yawLimit, yawLimit) * lookWeight_;
            const float pitch = std::clamp(targetPitch - headPitch, -pitchLimit, pitchLimit) * lookWeight_;
            RotateRigBoneInComponentSpace(asset, pose, transforms_, bones_.torso,
                                          glm::angleAxis(yaw * config.LookTorsoShare, kAxisY));
            RotateRigBoneInComponentSpace(asset, pose, transforms_, bones_.head,
                                          glm::angleAxis(yaw * (1.0f - config.LookTorsoShare), kAxisY) *
                                              glm::angleAxis(-pitch, kAxisX));
        }

        // Flinches.
        if (valid(bones_.torso) && glm::length(torsoFlinch_.value) > 1.0e-4f)
        {
            RotateRigBoneInComponentSpace(asset, pose, transforms_, bones_.torso, Euler(torsoFlinch_.value));
        }
        if (valid(bones_.head) && glm::length(headFlinch_.value) > 1.0e-4f)
        {
            RotateRigBoneInComponentSpace(asset, pose, transforms_, bones_.head, Euler(headFlinch_.value));
        }
        if (valid(bones_.upperArmL) && std::abs(armKickL_.x) > 1.0e-4f)
        {
            RotateRigBoneInComponentSpace(asset, pose, transforms_, bones_.upperArmL,
                                          glm::angleAxis(armKickL_.x, kAxisX));
        }
        if (valid(bones_.upperArmR) && std::abs(armKickR_.x) > 1.0e-4f)
        {
            RotateRigBoneInComponentSpace(asset, pose, transforms_, bones_.upperArmR,
                                          glm::angleAxis(armKickR_.x, kAxisX));
        }
    }

    void ZombieRigVisual::UpdateHitboxes()
    {
        hitboxCount_ = 0;
        const glm::mat4 world = glm::translate(glm::mat4(1.0f), worldNode_->Translation()) *
                                glm::mat4_cast(glm::angleAxis(yaw_, kAxisY));
        const auto add = [&](int32_t bone, const glm::vec3& a, const glm::vec3& b, float radius, EHitZone zone)
        {
            if (bone < 0 || bone >= static_cast<int32_t>(transforms_.size()) || hitboxCount_ >= hitboxes_.size())
            {
                return;
            }
            const glm::mat4 m = world * transforms_[bone];
            hitboxes_[hitboxCount_++] = {glm::vec3(m * glm::vec4(a, 1.0f)), glm::vec3(m * glm::vec4(b, 1.0f)), radius,
                                         zone};
        };
        add(bones_.head, {0.0f, 0.07f, 0.0f}, {0.0f, 0.21f, 0.0f}, 0.12f, EHitZone::Head);
        add(bones_.torso, {0.0f, 0.10f, 0.0f}, {0.0f, 0.40f, 0.0f}, 0.17f, EHitZone::Torso);
        add(bones_.pelvis, {0.0f, -0.02f, 0.0f}, {0.0f, 0.05f, 0.0f}, 0.16f, EHitZone::Torso);
        add(bones_.upperArmL, {0.0f, -0.02f, 0.0f}, {0.0f, -0.30f, 0.0f}, 0.065f, EHitZone::Limb);
        add(bones_.upperArmR, {0.0f, -0.02f, 0.0f}, {0.0f, -0.30f, 0.0f}, 0.065f, EHitZone::Limb);
        add(bones_.foreArmL, {0.0f, -0.01f, 0.0f}, {0.0f, -0.36f, 0.0f}, 0.055f, EHitZone::Limb);
        add(bones_.foreArmR, {0.0f, -0.01f, 0.0f}, {0.0f, -0.36f, 0.0f}, 0.055f, EHitZone::Limb);
        add(bones_.thighL, {0.0f, -0.03f, 0.0f}, {0.0f, -0.42f, 0.0f}, 0.09f, EHitZone::Leg);
        add(bones_.thighR, {0.0f, -0.03f, 0.0f}, {0.0f, -0.42f, 0.0f}, 0.09f, EHitZone::Leg);
        add(bones_.calfL, {0.0f, -0.02f, 0.0f}, {0.0f, -0.40f, 0.0f}, 0.075f, EHitZone::Leg);
        add(bones_.calfR, {0.0f, -0.02f, 0.0f}, {0.0f, -0.40f, 0.0f}, 0.075f, EHitZone::Leg);
    }

    size_t ZombieRigVisual::StandingHitboxes(const glm::vec3& position, const glm::vec3& forward,
                                             std::array<FHitCapsule, kHitCapsuleCount>& out)
    {
        const glm::vec3 right = glm::normalize(glm::cross(forward, kAxisY));
        size_t count = 0;
        out[count++] = {position + glm::vec3(0.0f, 1.58f, 0.0f), position + glm::vec3(0.0f, 1.72f, 0.0f), 0.12f,
                        EHitZone::Head};
        out[count++] = {position + glm::vec3(0.0f, 0.95f, 0.0f), position + glm::vec3(0.0f, 1.36f, 0.0f), 0.18f,
                        EHitZone::Torso};
        for (const float side : {-1.0f, 1.0f})
        {
            const glm::vec3 hip = position + right * (0.1f * side);
            out[count++] = {hip + glm::vec3(0.0f, 0.10f, 0.0f), hip + glm::vec3(0.0f, 0.80f, 0.0f), 0.09f, EHitZone::Leg};
            const glm::vec3 shoulder = position + right * (0.25f * side);
            out[count++] = {shoulder + glm::vec3(0.0f, 0.85f, 0.0f), shoulder + glm::vec3(0.0f, 1.38f, 0.0f), 0.065f,
                            EHitZone::Limb};
        }
        return count;
    }

    void ZombieRigVisual::Update(const FZombieRuntime& zombie, const glm::vec3& lookTarget,
                                 const FZombieConfig& config, float deltaSeconds)
    {
        if (!worldNode_ || !clips_ || !asset_)
        {
            return;
        }
        const float dt = std::max(deltaSeconds, 0.0f);
        if (!visible_)
        {
            Assets::NodeUtils::SetVisibleRecursive(worldNode_, true);
            visible_ = true;
        }

        // Facing comes from the simulation, which already limits turn rate.
        const float yaw = std::atan2(zombie.forward.x, zombie.forward.z);
        const float rate = dt > 0.0f ? WrapAngle(yaw - yaw_) / dt : 0.0f;
        yawRate_ = Approach(yawRate_, rate, 0.08f, dt);
        yaw_ = yaw;
        const bool dead = zombie.state == EZombieState::Dead;
        const float leanLimit = config.TurnLeanMaxDegrees;
        turnLean_ = Approach(turnLean_,
                             dead ? 0.0f
                                  : std::clamp(-glm::degrees(yawRate_ * zombie.Speed() * config.TurnLeanPerRadPerMps),
                                               -leanLimit, leanLimit),
                             0.12f, dt);

        worldNode_->Translation() = zombie.position;
        worldNode_->Rotation() = glm::angleAxis(yaw_, kAxisY);
        worldNode_->RecalcTransform(true);

        const bool tracking = zombie.state == EZombieState::Alert || zombie.state == EZombieState::Chase ||
                              zombie.state == EZombieState::Attack;
        lookWeight_ = Approach(lookWeight_,
                               tracking ? 1.0f : (zombie.state == EZombieState::Stagger ? 0.4f : 0.0f), 0.15f, dt);

        // Flinch springs, sub-stepped so a hitch cannot blow them up.
        for (float remaining = dt; remaining > 0.0f;)
        {
            const float step = std::min(remaining, 1.0f / 90.0f);
            StepSpring(torsoFlinch_.value, torsoFlinch_.velocity, config.FlinchSpring, config.FlinchDamping, step);
            StepSpring(headFlinch_.value, headFlinch_.velocity, config.FlinchSpring * 1.3f, config.FlinchDamping, step);
            StepSpring(pelvisDrop_, config.FlinchSpring * 0.6f, config.FlinchDamping, step);
            StepSpring(armKickL_, config.FlinchSpring, config.FlinchDamping * 0.8f, step);
            StepSpring(armKickR_, config.FlinchSpring, config.FlinchDamping * 0.8f, step);
            remaining -= step;
        }

        UpdateLayers(zombie, lookTarget, config, dt);
        animator_.Evaluate(dt);
        UpdateProcedural(zombie, lookTarget, config, dt);
        animator_.Apply();
        if (dead)
        {
            hitboxCount_ = 0;
        }
        else
        {
            UpdateHitboxes();
        }
    }
}
