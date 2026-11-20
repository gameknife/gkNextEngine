#include "Engine/Common/CoreMinimal.hpp"
#include "WebCharacters.hpp"

#include <algorithm>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace NextDayz
{
    using namespace NextGameplay;

    namespace
    {
        float Blend(float value, float target, float seconds, float dt)
        {
            return seconds > 0.f ? glm::mix(value, target, 1.f - std::exp(-dt / seconds)) : target;
        }

        FWebCharacterPose WritePose(FRigLayeredAnimator& animator, float dt, float yaw,
                                    std::vector<glm::mat4>& transforms, std::vector<float>& output)
        {
            animator.Evaluate(dt);
            ComputeRigComponentTransforms(*animator.Asset(), animator.Pose(), transforms);
            const auto offset = output.size();
            for (const auto& matrix : transforms)
            {
                const auto* data = glm::value_ptr(matrix);
                output.insert(output.end(), data, data + 16);
            }
            return {offset, yaw};
        }
    }

    void FWebSurvivorPose::Bind(const Assets::FRigDefinition& asset)
    {
        animator_.BindPose(&asset);
        clips_ = ResolveSurvivorLocomotionClips(asset);
        locomotion_ = animator_.CreateLayer("locomotion", ERigLayerBlendMode::Override, FRigBoneMask::FullBody(asset));
        auto arms = FRigBoneMask::FromSubtree(asset, "bone_ik_weapon");
        for (const auto* bone : {"bone_upperarm_l", "bone_forearm_l", "bone_hand_l", "bone_upperarm_r",
                                 "bone_forearm_r", "bone_hand_r", "bone_weapon_socket"})
            arms.SetBoneWeight(asset, bone, 1.f);
        auto body = FRigBoneMask::FromSubtree(asset, "bone_head");
        body.SetBoneWeight(asset, "bone_torso", 1.f);
        hold_ = animator_.CreateLayer("hold", ERigLayerBlendMode::Override, arms);
        holdBody_ = animator_.CreateLayer("hold_body", ERigLayerBlendMode::Additive, body);
        weaponAction_ = animator_.CreateLayer("weapon_action", ERigLayerBlendMode::Override, std::move(arms));
        weaponActionBody_ = animator_.CreateLayer("weapon_action_body", ERigLayerBlendMode::Additive, std::move(body));
        lootAction_ = animator_.CreateLayer("loot", ERigLayerBlendMode::Override, FRigBoneMask::FullBody(asset));
        animator_.SetLoopBlend(locomotion_, {{clips_.front(), 1.f}}, "locomotion", 1.f, 0.f);
    }

    FWebCharacterPose FWebSurvivorPose::Advance(const FPlayerPresentationState& state,
        const FAnimationConfig& config, float dt, std::vector<float>& output)
    {
        const auto& asset = *animator_.Asset();
        const auto& move = state.locomotion;
        const bool traversing = move.traversalAction != EPlayerTraversalAction::None;
        if (!yawInitialized_)
        {
            yaw_ = state.aimYawRadians;
            yawInitialized_ = true;
        }
        const auto angle = [](float value) { return std::remainder(value, glm::two_pi<float>()); };
        const auto yawDiff = angle(state.aimYawRadians - yaw_);
        if (traversing)
        {
            yaw_ += Blend(0.f, angle(move.traversalYaw - yaw_), .06f, dt);
            turning_ = false;
        }
        else if (move.horizontalSpeed > config.MoveThreshold || !move.onGround)
        {
            yaw_ += Blend(0.f, yawDiff, config.BodyYawFollowSeconds, dt);
            turning_ = false;
        }
        else
        {
            if (std::abs(yawDiff) > glm::radians(config.TurnInPlaceThresholdDegrees)) turning_ = true;
            if (turning_)
            {
                const float step = glm::radians(config.TurnInPlaceRateDegrees) * dt;
                yaw_ += glm::clamp(yawDiff, -step, step);
                turning_ = std::abs(angle(state.aimYawRadians - yaw_)) > glm::radians(4.f);
            }
        }
        yaw_ = angle(yaw_);
        crouch_ = Blend(crouch_, move.actualStance == EPlayerStance::Crouched ? 1.f : 0.f, config.StanceBlendSeconds * .5f, dt);
        UpdateSurvivorLocomotion(asset, animator_, locomotion_, clips_, move, config, yaw_, crouch_, dt,
                                 direction_, moving_, baseClip_);

        const bool looting = state.action == EPlayerAction::LootGround;
        const bool holding = (state.hasWeapon || state.weaponAction == EWeaponPresentationAction::Switch) && !traversing && !looting;
        aim_ = Blend(aim_, holding ? glm::clamp(state.aimWeight, 0.f, 1.f) : 0.f, config.AimFadeSeconds * .5f, dt);
        holdWeight_ = Blend(holdWeight_, holding ? 1.f : 0.f, config.HoldFadeSeconds * .5f, dt);
        const float sprint = glm::clamp(state.sprintAlpha, 0.f, 1.f) * (1.f - aim_);
        std::vector<FRigClipBlendSample> hold = {
            {asset.FindClip("weapon_ready"), (1.f - aim_) - sprint},
            {asset.FindClip("aim_rifle_center"), aim_},
            {asset.FindClip("weapon_sprint"), sprint},
        };
        if (state.weaponId == "knife" || state.weaponId == "crowbar" || state.weaponId == "axe")
        {
            const auto prefix = "melee_" + state.weaponId;
            hold = {{asset.FindClip(prefix + "_low"), 1.f - aim_}, {asset.FindClip(prefix + "_ready"), aim_}};
        }
        animator_.SetStaticBlend(hold_, hold, 0.f);
        animator_.SetStaticBlend(holdBody_, std::move(hold), 0.f);
        animator_.SetLayerWeight(hold_, holdWeight_);
        const bool usingWeapon = holding && state.weaponAction != EWeaponPresentationAction::None;
        animator_.SetLayerWeight(holdBody_, usingWeapon ? 0.f : holdWeight_);
        animator_.SetLayerWeight(weaponAction_, usingWeapon ? 1.f : 0.f);
        animator_.SetLayerWeight(weaponActionBody_, usingWeapon ? 1.f : 0.f);
        if (usingWeapon)
        {
            const std::string clip = state.weaponAction == EWeaponPresentationAction::Melee
                ? "melee_" + state.weaponId + "_attack" : state.weaponAction == EWeaponPresentationAction::Reload
                ? "reload_rifle" : "switch_weapon";
            const auto* selected = asset.FindClip(clip);
            animator_.SetManualBlend(weaponAction_, {{selected, 1.f}}, state.weaponActionTime01);
            animator_.SetManualBlend(weaponActionBody_, {{selected, 1.f}}, state.weaponActionTime01);
        }
        animator_.SetLayerWeight(lootAction_, looting ? 1.f : 0.f);
        if (looting) animator_.SetManualBlend(lootAction_, {{asset.FindClip("loot_ground"), 1.f}}, state.actionTime01);
        return WritePose(animator_, dt, yaw_, transforms_, output);
    }

    void FWebInfectedPose::Bind(const Assets::FRigDefinition& asset)
    {
        animator_.BindPose(&asset);
        locomotion_ = animator_.CreateLayer("locomotion", ERigLayerBlendMode::Override, FRigBoneMask::FullBody(asset));
        action_ = animator_.CreateLayer("action", ERigLayerBlendMode::Override, FRigBoneMask::FullBody(asset));
        idle_ = asset.FindClip("idle");
        walk_ = asset.FindClip("walk");
        limp_ = asset.FindClip("walk_limp");
        run_ = asset.FindClip("run");
        hobble_ = asset.FindClip("hobble");
    }

    FWebCharacterPose FWebInfectedPose::Advance(const FZombieRuntime& zombie, const FZombieConfig& config, float dt,
        std::vector<float>& output)
    {
        const auto speed = glm::length(glm::vec2(zombie.velocity.x, zombie.velocity.z));
        UpdateInfectedLocomotion(animator_, locomotion_, idle_, walk_, limp_, run_, hobble_, speed, zombie.Lame(), config);
        const char* clip = nullptr;
        const auto& asset = *animator_.Asset();
        if (zombie.state == EZombieState::Attack) clip = zombie.attack == EZombieAttack::Lunge ? "attack_lunge" : "attack_swipe";
        if (zombie.state == EZombieState::Alert) clip = "alert";
        if (zombie.state == EZombieState::Stagger) clip = "stagger";
        if (zombie.state == EZombieState::Dead)
            clip = zombie.death == EZombieDeath::Forward ? "death_forward" :
                zombie.death == EZombieDeath::Crumple ? "death_crumple" : "death_back";
        const auto* action = clip ? asset.FindClip(clip) : nullptr;
        if (action)
        {
            const float time = glm::clamp(zombie.stateSeconds / std::max(action->duration, .01f), 0.f, 1.f);
            animator_.SetManualBlend(action_, {{action, 1.f}}, time);
        }
        animator_.SetLayerWeight(action_, action ? 1.f : 0.f);
        return WritePose(animator_, dt, std::atan2(zombie.forward.x, zombie.forward.z), transforms_, output);
    }
}
