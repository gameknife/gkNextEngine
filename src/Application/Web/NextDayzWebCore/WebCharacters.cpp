#include "Engine/Common/CoreMinimal.hpp"
#include "WebCharacters.hpp"

#include <algorithm>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "Application/Game/NextDayz/Player/WeaponRigHold.hpp"

namespace NextDayz
{
    using namespace NextGameplay;

    namespace
    {
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

    void FWebSurvivorPose::Bind(const Assets::FRigDefinition& asset, const FAnimationConfig& animation, float weaponScale)
    {
        asset_ = &asset;
        weaponScale_ = weaponScale;
        socket_ = asset.FindBone("bone_weapon_socket");
        for (int slot = 0; slot < kWeaponSlotCount; ++slot) holsterParents_[slot] = FSurvivorPoseGraph::HolsterParentBone(asset, slot);
        graph_.Bind(asset, animation, [&asset](FRigLayeredAnimator& animator) { animator.BindPose(&asset); });
    }

    FWebCharacterPose FWebSurvivorPose::Advance(const glm::vec3& feet, const FPlayerPresentationState& state, float dt,
                                                std::vector<float>& output)
    {
        graph_.Advance(feet, state, dt);
        ComputeRigComponentTransforms(*asset_, graph_.Animator().Pose(), transforms_);
        const auto append = [&output](const glm::mat4& matrix)
        {
            const auto* data = glm::value_ptr(matrix);
            output.insert(output.end(), data, data + 16);
        };
        FWebCharacterPose pose{output.size(), graph_.BodyYaw()};
        for (const auto& matrix : transforms_) append(matrix);

        // The weapon models hang where the desktop rig hangs them: under the
        // socket with the weapon's own offset, in the holsters, at rig scale.
        pose.gearOffset = output.size();
        const auto at = [this](int32_t bone)
        {
            return bone >= 0 && bone < static_cast<int32_t>(transforms_.size()) ? transforms_[bone] : glm::mat4(1.f);
        };
        const glm::mat4 scale = glm::scale(glm::mat4(1.f), glm::vec3(weaponScale_));
        append(at(socket_) * glm::translate(glm::mat4(1.f), WeaponRigHoldFor(state.weaponId).modelOffset) * scale);
        for (int slot = 0; slot < kWeaponSlotCount; ++slot)
        {
            const auto& holster = graph_.Holsters()[slot];
            append(at(holsterParents_[slot]) * glm::translate(glm::mat4(1.f), holster.position) *
                   glm::mat4_cast(holster.rotation) * scale);
        }
        const bool switching = state.weaponAction == EWeaponPresentationAction::Switch;
        pose.heldVisible = state.hasWeapon && !switching;
        for (int slot = 0; slot < kWeaponSlotCount; ++slot)
            pose.holsterVisible[slot] = !state.slotWeaponIds[slot].empty() && (switching || slot != state.activeWeaponSlot);
        return pose;
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
