#include "Engine/Common/CoreMinimal.hpp"
#include "SurvivorPoseGraph.hpp"

#include "Application/Game/NextDayz/Player/WeaponRigHold.hpp"
#include "Gameplay/Rig/RigPoseOps.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace NextDayz
{
    namespace
    {
        using namespace NextGameplay;

        const glm::vec3 kAxisX(1.0f, 0.0f, 0.0f);
        const glm::vec3 kAxisY(0.0f, 1.0f, 0.0f);
        const glm::vec3 kAxisZ(0.0f, 0.0f, 1.0f);
        const glm::quat kBackWeaponRotation = glm::angleAxis(glm::pi<float>(), kAxisY) *
                                              glm::angleAxis(glm::radians(-90.0f), kAxisX);
        const glm::quat kWaistWeaponRotation = glm::angleAxis(glm::radians(90.0f), kAxisX);

        // Elbow pole points in torso space (engine axes: +Z forward, -X right):
        // firing elbow down and out, support elbow down under the handguard.
        const glm::vec3 kPoleRight(-0.50f, 0.12f, -0.08f);
        const glm::vec3 kPoleLeft(0.45f, 0.08f, 0.15f);

        // Weapons that are not held like the rifle the rig's grips were built for.
        FWeaponRigHold HoldFor(std::string_view weaponId) { return WeaponRigHoldFor(weaponId); }

        float Approach(float current, float target, float seconds, float dt)
        {
            return seconds > 0.0f ? glm::mix(current, target, 1.0f - std::exp(-dt / seconds)) : target;
        }

        float WrapAngle(float radians)
        {
            return std::remainder(radians, glm::two_pi<float>());
        }
    }

    int32_t FSurvivorPoseGraph::HolsterParentBone(const Assets::FRigDefinition& asset, int slot)
    {
        return asset.FindBone(slot == kSidearmSlot ? "bone_pelvis" : "bone_torso");
    }

    FSurvivorPoseGraph::FHolster FSurvivorPoseGraph::RestingHolster(int slot)
    {
        // Character left is +X. Keep the barrels up and the grips (-Y in
        // weapon space) pointing back (-Z); the sidearm sits on the right hip.
        if (slot == kSidearmSlot) return {glm::vec3(-0.235f, 0.08f, 0.04f), kWaistWeaponRotation};
        const float side = slot == 0 ? 1.0f : -1.0f;
        return {glm::vec3(side * 0.20f, 0.24f, -0.20f), kBackWeaponRotation};
    }

    void FSurvivorPoseGraph::Bind(const Assets::FRigDefinition& asset, const FAnimationConfig& config,
                                  const std::function<void(FRigLayeredAnimator&)>& bind)
    {
        const auto rest = rest_;
        *this = {};
        rest_ = rest;
        asset_ = &asset;
        config_ = config;
        bones_.root = asset.FindBone("bone_root");
        bones_.pelvis = asset.FindBone("bone_pelvis");
        bones_.torso = asset.FindBone("bone_torso");
        bones_.head = asset.FindBone("bone_head");
        bones_.upperArmL = asset.FindBone("bone_upperarm_l");
        bones_.foreArmL = asset.FindBone("bone_forearm_l");
        bones_.handL = asset.FindBone("bone_hand_l");
        bones_.upperArmR = asset.FindBone("bone_upperarm_r");
        bones_.foreArmR = asset.FindBone("bone_forearm_r");
        bones_.handR = asset.FindBone("bone_hand_r");
        bones_.ikWeapon = asset.FindBone("bone_ik_weapon");
        bones_.ikHandL = asset.FindBone("bone_ik_hand_l");
        bones_.ikHandR = asset.FindBone("bone_ik_hand_r");
        clips_.locomotion = ResolveSurvivorLocomotionClips(asset);
        clips_.ready = asset.FindClip("weapon_ready");
        clips_.aim = asset.FindClip("aim_rifle_center");
        clips_.sprint = asset.FindClip("weapon_sprint");
        clips_.recoil = asset.FindClip("recoil_rifle");
        holsters_ = rest_;

        bind(animator_);
        locomotionLayer_ = animator_.CreateLayer("locomotion", ERigLayerBlendMode::Override, FRigBoneMask::FullBody(asset));
        // Holds own the arms and the IK frames; their torso/head channels (the
        // blade) are added on top of the gait so the walk keeps its swing.
        FRigBoneMask armsMask = FRigBoneMask::FromSubtree(asset, "bone_ik_weapon");
        for (const char* bone : {"bone_upperarm_l", "bone_forearm_l", "bone_hand_l", "bone_upperarm_r",
                                 "bone_forearm_r", "bone_hand_r", "bone_weapon_socket"})
        {
            armsMask.SetBoneWeight(asset, bone, 1.0f);
        }
        FRigBoneMask bodyMask = FRigBoneMask::FromSubtree(asset, "bone_head");
        bodyMask.SetBoneWeight(asset, "bone_torso", 1.0f);
        holdLayer_ = animator_.CreateLayer("hold", ERigLayerBlendMode::Override, armsMask);
        holdBodyLayer_ = animator_.CreateLayer("hold_body", ERigLayerBlendMode::Additive, bodyMask);
        weaponActionLayer_ = animator_.CreateLayer("weapon_action", ERigLayerBlendMode::Override, armsMask);
        weaponActionBodyLayer_ = animator_.CreateLayer("weapon_action_body", ERigLayerBlendMode::Additive, bodyMask);
        FRigBoneMask recoilMask = FRigBoneMask::FromSubtree(asset, "bone_torso");
        recoilMask.SetBoneWeight(asset, "bone_head", 0.0f);
        recoilLayer_ = animator_.CreateLayer("recoil", ERigLayerBlendMode::Additive, recoilMask);
        actionLayer_ = animator_.CreateLayer("action", ERigLayerBlendMode::Override, FRigBoneMask::FullBody(asset));
        for (const FRigLayerHandle layer :
             {holdLayer_, holdBodyLayer_, weaponActionLayer_, weaponActionBodyLayer_, recoilLayer_, actionLayer_})
        {
            animator_.SetLayerWeight(layer, 0.0f);
        }
        if (clips_.locomotion.front())
        {
            animator_.SetLoopBlend(locomotionLayer_, {{clips_.locomotion.front(), 1.0f}}, "locomotion", 1.0f, 0.0f);
            baseClipName_ = clips_.locomotion.front()->name;
        }
        animator_.Evaluate(0.0f);
        bound_ = true;
    }

    void FSurvivorPoseGraph::Unbind()
    {
        const auto rest = rest_;
        *this = {};
        rest_ = rest;
    }

    void FSurvivorPoseGraph::TriggerRecoil(float scale)
    {
        if (!bound_) return;
        animator_.SetLayerWeight(recoilLayer_, glm::clamp(scale, 0.0f, 1.0f));
        animator_.PlayOneShot(recoilLayer_, clips_.recoil, 1.0f, 0.0f, config_.RecoilFadeOutSeconds, true);
        recoilActive_ = true;
    }

    float FSurvivorPoseGraph::ElbowConstraintErrorDegrees() const
    {
        const auto& pose = animator_.Pose();
        float error = 0.0f;
        for (int32_t bone : {bones_.foreArmL, bones_.foreArmR})
        {
            if (bone < 0 || bone >= static_cast<int32_t>(pose.bones.size())) continue;
            glm::quat rotation = glm::normalize(pose.bones[bone].r);
            if (rotation.w < 0.0f) rotation = -rotation;
            const float flex = glm::degrees(-2.0f * std::atan2(rotation.x, rotation.w));
            const float offAxis = glm::degrees(std::acos(glm::clamp(glm::dot(rotation * kAxisX, kAxisX), -1.0f, 1.0f)));
            // The released hand can retain the authored near-straight hold;
            // only backwards flexion, over-folding and off-axis twist fail.
            error = std::max({error, offAxis, -flex, flex - 150.0f});
        }
        return error;
    }

    float FSurvivorPoseGraph::HandHeightInTorso(int slot) const
    {
        const int32_t hand = slot == 0 ? bones_.handL : bones_.handR;
        if (bones_.torso < 0 || hand < 0 || hand >= static_cast<int32_t>(transforms_.size())) return 0.0f;
        return glm::vec3(glm::inverse(transforms_[bones_.torso]) * transforms_[hand][3]).y;
    }

    void FSurvivorPoseGraph::Seated(float dt)
    {
        if (!bound_) return;
        for (auto layer : {holdLayer_, holdBodyLayer_, weaponActionLayer_, weaponActionBodyLayer_, recoilLayer_, actionLayer_})
            animator_.SetLayerWeight(layer, 0.0f);
        animator_.SetLayerWeight(locomotionLayer_, 1.0f);
        const auto* seated = asset_->FindClip("cabin_seated");
        animator_.SetLoopBlend(locomotionLayer_, {{seated, 1.0f}}, "cabin", 1.0f, 0.0f);
        baseClipName_ = seated ? seated->name : "";
        holsters_ = rest_;
        animator_.Evaluate(std::max(0.0f, dt));
    }

    void FSurvivorPoseGraph::Advance(const glm::vec3& feet, const FPlayerPresentationState& state, float deltaSeconds)
    {
        if (!bound_) return;
        const float dt = std::max(deltaSeconds, 0.0f);
        const FAnimationConfig& anim = config_;
        const FPlayerLocomotionState& locomotion = state.locomotion;
        const bool traversing = locomotion.traversalAction != EPlayerTraversalAction::None;
        const bool looting = state.action == EPlayerAction::LootGround;

        // Lower body follows the camera: quickly while moving, by a
        // turn-in-place step once the upper body has twisted too far.
        if (!yawInitialized_)
        {
            bodyYaw_ = state.aimYawRadians;
            previousAimYaw_ = state.aimYawRadians;
            yawInitialized_ = true;
        }
        const float yawDiff = WrapAngle(state.aimYawRadians - bodyYaw_);
        const bool moving = locomotion.horizontalSpeed > anim.MoveThreshold;
        if (traversing)
        {
            // Square up to the obstacle for the whole move, whatever the camera does.
            bodyYaw_ += WrapAngle(locomotion.traversalYaw - bodyYaw_) * (1.0f - std::exp(-dt / 0.06f));
            turningInPlace_ = false;
        }
        else if (moving || looting || !locomotion.onGround)
        {
            bodyYaw_ += yawDiff * (1.0f - std::exp(-dt / std::max(anim.BodyYawFollowSeconds, 1.0e-3f)));
            turningInPlace_ = false;
        }
        else
        {
            if (std::abs(yawDiff) > glm::radians(anim.TurnInPlaceThresholdDegrees))
            {
                turningInPlace_ = true;
            }
            if (turningInPlace_)
            {
                const float step = glm::radians(anim.TurnInPlaceRateDegrees) * dt;
                bodyYaw_ += glm::clamp(yawDiff, -step, step);
                turningInPlace_ = std::abs(WrapAngle(state.aimYawRadians - bodyYaw_)) > glm::radians(4.0f);
            }
        }
        bodyYaw_ = WrapAngle(bodyYaw_);

        const float yawRate = dt > 0.0f ? WrapAngle(state.aimYawRadians - previousAimYaw_) / dt : 0.0f;
        previousAimYaw_ = state.aimYawRadians;
        const float swayLimit = glm::radians(anim.TurnSwayMaxDegrees);
        turnSway_ = Approach(turnSway_,
                             glm::clamp(yawRate * glm::radians(anim.TurnSwayDegreesPerRadPerSecond), -swayLimit,
                                        swayLimit) *
                                 (1.0f - 0.7f * aimWeight_),
                             0.08f, dt);

        // Lean into acceleration on a damped spring: bank into turns, rock back
        // on a hard stop, settle with a little overshoot.
        const glm::vec3 velocity(locomotion.worldVelocity.x, 0.0f, locomotion.worldVelocity.z);
        const glm::vec3 acceleration = dt > 0.0f ? (velocity - previousVelocity_) / dt : glm::vec3(0.0f);
        previousVelocity_ = velocity;
        const glm::vec3 bodyForward(std::sin(bodyYaw_), 0.0f, std::cos(bodyYaw_));
        const glm::vec3 bodyRight(-std::cos(bodyYaw_), 0.0f, std::sin(bodyYaw_));
        glm::vec2 leanTarget(0.0f);
        if (locomotion.onGround && !traversing && !looting)
        {
            const float scale = 1.0f - 0.5f * aimWeight_;
            leanTarget.x = glm::clamp(glm::dot(acceleration, bodyRight) * anim.LeanRollPerMps2, -anim.LeanMaxRollDegrees,
                                      anim.LeanMaxRollDegrees) * scale;
            leanTarget.y = glm::clamp(glm::dot(acceleration, bodyForward) * anim.LeanPitchPerMps2,
                                      -anim.LeanMaxPitchDegrees, anim.LeanMaxPitchDegrees) * scale;
        }
        const float springDt = std::min(dt, 0.05f);
        leanVelocity_ += (anim.LeanSpring * (leanTarget - lean_) - anim.LeanDamping * leanVelocity_) * springDt;
        lean_ += leanVelocity_ * springDt;

        // Layer weights.
        crouchWeight_ = Approach(crouchWeight_, locomotion.actualStance == EPlayerStance::Crouched ? 1.0f : 0.0f,
                                 anim.StanceBlendSeconds * 0.5f, dt);
        const bool holding = (state.hasWeapon || state.weaponAction == EWeaponPresentationAction::Switch) &&
                             !looting && !traversing;
        weaponWeight_ = Approach(weaponWeight_, state.hasWeapon ? 1.0f : 0.0f, 0.06f, dt);
        holdWeight_ = Approach(holdWeight_, holding ? 1.0f : 0.0f, anim.HoldFadeSeconds * 0.5f, dt);
        freeHandWeight_ = Approach(freeHandWeight_, looting || traversing ? 1.0f : 0.0f, 0.05f, dt);
        traverseWeight_ = Approach(traverseWeight_, traversing ? 1.0f : 0.0f, traversing ? 0.04f : 0.12f, dt);
        const bool weaponActionActive = holding && state.weaponAction != EWeaponPresentationAction::None;
        aimWeight_ = Approach(aimWeight_, holding && !weaponActionActive ? glm::clamp(state.aimWeight, 0.0f, 1.0f) : 0.0f,
                              anim.AimFadeSeconds * 0.5f, dt);
        sprintHoldWeight_ = Approach(sprintHoldWeight_, glm::clamp(state.sprintAlpha, 0.0f, 1.0f),
                                     anim.SprintHoldFadeSeconds * 0.3f, dt);
        weaponActionWeight_ = Approach(weaponActionWeight_, weaponActionActive ? 1.0f : 0.0f,
                                       anim.HoldFadeSeconds * 0.5f, dt);
        actionWeight_ = Approach(actionWeight_, looting ? 1.0f : 0.0f, anim.LocomotionFadeSeconds * 0.5f, dt);

        const FWeaponDef* weapon = FindWeaponDef(state.weaponId);
        const bool melee = weapon && weapon->IsMelee();
        const float sprint = sprintHoldWeight_ * (1.0f - aimWeight_);
        std::vector<FRigClipBlendSample> holdSamples = {
            {clips_.ready, std::max(0.0f, 1.0f - aimWeight_ - sprint)},
            {clips_.aim, aimWeight_},
            {clips_.sprint, sprint},
        };
        if (melee)
        {
            holdSamples = {
                {asset_->FindClip("melee_" + state.weaponId + "_low"), 1.0f - aimWeight_},
                {asset_->FindClip("melee_" + state.weaponId + "_ready"), aimWeight_},
            };
        }
        animator_.SetStaticBlend(holdLayer_, holdSamples, 0.0f);
        animator_.SetStaticBlend(holdBodyLayer_, std::move(holdSamples), 0.0f);
        animator_.SetLayerWeight(holdLayer_, holdWeight_);
        animator_.SetLayerWeight(holdBodyLayer_, holdWeight_ * (1.0f - weaponActionWeight_));

        if (weaponActionActive)
        {
            const std::string clipName = state.weaponAction == EWeaponPresentationAction::Melee
                ? "melee_" + state.weaponId + "_attack"
                : state.weaponAction == EWeaponPresentationAction::Reload ? "reload_rifle" : "switch_weapon";
            weaponActionClipName_ = clipName;
            const float time = glm::clamp(state.weaponActionTime01, 0.0f, 1.0f);
            animator_.SetManualBlend(weaponActionLayer_, {{asset_->FindClip(clipName), 1.0f}}, time);
            animator_.SetManualBlend(weaponActionBodyLayer_, {{asset_->FindClip(clipName), 1.0f}}, time);
            animator_.SetLayerWeight(recoilLayer_, 0.0f);
            recoilActive_ = false;
        }
        else if (weaponActionWeight_ < 0.001f)
        {
            weaponActionClipName_.clear();
        }
        animator_.SetLayerWeight(weaponActionLayer_, weaponActionWeight_);
        animator_.SetLayerWeight(weaponActionBodyLayer_, weaponActionWeight_);

        animator_.SetLayerWeight(actionLayer_, actionWeight_);
        if (looting)
        {
            animator_.SetManualBlend(actionLayer_, {{asset_->FindClip("loot_ground"), 1.0f}}, state.actionTime01);
            animator_.SetLayerWeight(recoilLayer_, 0.0f);
            recoilActive_ = false;
        }

        UpdateSurvivorLocomotion(*asset_, animator_, locomotionLayer_, clips_.locomotion, locomotion, anim, bodyYaw_,
                                 crouchWeight_, dt, moveDirection_, movingWeight_, baseClipName_);
        animator_.Evaluate(dt);
        holsters_ = rest_;
        const glm::mat4 componentToWorld = glm::translate(glm::mat4(1.0f), feet) *
                                           glm::mat4_cast(glm::angleAxis(bodyYaw_, kAxisY));
        UpdateProcedural(state, componentToWorld);
        if (recoilActive_ && animator_.IsOneShotComplete(recoilLayer_))
        {
            recoilActive_ = false;
        }
    }

    void FSurvivorPoseGraph::UpdateSwitchPose(const FPlayerPresentationState& state)
    {
        const Assets::FRigDefinition& asset = *asset_;
        FRigPose& pose = animator_.MutablePose();
        const glm::mat4 torso = transforms_[bones_.torso];
        const glm::mat4 toTorso = glm::inverse(torso);
        const glm::quat torsoRotation = RigBoneRotation(transforms_, bones_.torso);
        const glm::vec3 readyPosition(toTorso * glm::vec4(RigBonePosition(transforms_, bones_.ikWeapon), 1.0f));
        const glm::quat readyRotation = glm::inverse(torsoRotation) * RigBoneRotation(transforms_, bones_.ikWeapon);
        const float time = state.weaponActionTime01;
        const int oldSlot = state.switchSourceSlot;
        const int newSlot = state.switchTargetSlot;
        const bool sidearmSwitch = oldSlot == kSidearmSlot || newSlot == kSidearmSlot;
        // A shared right hand must finish stowing before reaching the other
        // holster. Opposite back slots retain their short two-hand overlap.
        const float stowEnd = sidearmSwitch ? 0.44f : kWeaponStowTime;
        const float drawStart = sidearmSwitch ? 0.56f : kWeaponDrawTime;
        const int oldHand = oldSlot == 0 ? 0 : 1;
        const int newHand = newSlot == 0 ? 0 : 1;
        const glm::mat4 pelvisToTorso = toTorso * transforms_[bones_.pelvis];
        const glm::quat pelvisInTorso = glm::inverse(torsoRotation) * RigBoneRotation(transforms_, bones_.pelvis);
        struct FSwitchPose
        {
            glm::vec3 position;
            glm::quat rotation;
            float release = 0.0f;
        };
        const auto sample = [&](int slot, float travel, bool fromBelow = false)
        {
            travel = glm::clamp(travel, 0.0f, 1.0f);
            const bool waist = slot == kSidearmSlot;
            const FHolster& rest = rest_[slot];
            const glm::quat holsterRotation = waist ? pelvisInTorso * kWaistWeaponRotation : kBackWeaponRotation;
            const glm::vec3 holsterPosition = waist ? glm::vec3(pelvisToTorso * glm::vec4(rest.position, 1.0f))
                                                    : rest.position;
            const glm::vec3 backPosition = holsterPosition - holsterRotation * HoldFor(state.slotWeaponIds[slot]).modelOffset;
            // Stow over the shoulder; draw around the waist. Keep the lower
            // route outside the pack and within a bent arm's reach.
            const float side = slot == 0 ? 1.0f : -1.0f;
            const glm::vec3 waypoint = waist ? glm::mix(readyPosition, backPosition, 0.6f) + glm::vec3(-0.05f, 0.10f, 0.12f)
                : fromBelow ? glm::vec3(side * std::max(0.36f, std::abs(rest.position.x) + 0.10f), 0.12f, 0.16f)
                            : glm::vec3(side * 0.36f, 0.74f, 0.18f);
            const glm::quat waypointRotation = glm::slerp(readyRotation, holsterRotation, waist ? 0.35f : fromBelow ? 0.55f : 0.75f);
            FSwitchPose frame;
            if (travel < 0.5f)
            {
                const float blend = glm::smoothstep(0.0f, 0.5f, travel);
                frame.position = glm::mix(readyPosition, waypoint, blend);
                frame.rotation = glm::slerp(readyRotation, waypointRotation, blend);
            }
            else
            {
                const float blend = glm::smoothstep(0.5f, 1.0f, travel);
                frame.position = glm::mix(waypoint, backPosition, blend);
                frame.rotation = glm::slerp(waypointRotation, holsterRotation, blend);
            }
            // Below-waist reaching keeps the elbow down and out.
            frame.release = waist || fromBelow ? 0.0f : glm::smoothstep(0.0f, 0.5f, travel);
            return frame;
        };
        std::array<FSwitchPose, kWeaponSlotCount> frames;
        for (int slot = 0; slot < kWeaponSlotCount; ++slot) frames[slot] = sample(slot, 1.0f);
        frames[oldSlot] = sample(oldSlot, time / stowEnd);
        frames[newSlot] = sample(newSlot, (1.0f - time) / (1.0f - drawStart), true);
        // Both existing back weapons become independent moving weapons. During
        // the overlap neither model is replaced or teleported to the other hand.
        for (int slot = 0; slot < kWeaponSlotCount; ++slot)
        {
            const glm::mat4 toParent = slot == kSidearmSlot ? glm::inverse(pelvisToTorso) : glm::mat4(1.0f);
            holsters_[slot].position = glm::vec3(toParent * glm::vec4(frames[slot].position +
                frames[slot].rotation * HoldFor(state.slotWeaponIds[slot]).modelOffset, 1.0f));
            holsters_[slot].rotation = slot == kSidearmSlot
                ? glm::inverse(pelvisInTorso) * frames[slot].rotation : frames[slot].rotation;
        }
        const auto wrist = [&](const FSwitchPose& frame, int slot, bool left)
        {
            const auto& grip = asset.bones[left ? bones_.ikHandL : bones_.ikHandR];
            const FWeaponRigHold hold = HoldFor(state.slotWeaponIds[slot]);
            glm::vec3 local = left && hold.overrideSupport ? hold.supportWrist : grip.bindT;
            if (left && slot == 0 && slot == newSlot)
            {
                // Fetch the left-slot gun by its low grip too, then slide to
                // the handguard as the right hand takes the firing grip.
                local = glm::mix(asset.bones[bones_.ikHandR].bindT, local, glm::smoothstep(0.78f, 1.0f, time));
            }
            return FSwitchPose{frame.position + frame.rotation * local, frame.rotation * grip.bindR, frame.release};
        };
        std::array<FSwitchPose, 2> hands;
        hands[oldHand] = wrist(frames[oldSlot], oldSlot, oldHand == 0);
        hands[newHand] = wrist(frames[newSlot], newSlot, newHand == 0);
        // The incoming hand reaches its own socket while the other hand is
        // still stowing. Once stowed, the outgoing hand returns to support the
        // new gun. Free hands travel below the socket, clear of the torso.
        if (oldHand != newHand && time < drawStart)
        {
            FSwitchPose& hand = hands[newHand];
            hand = wrist(sample(newSlot, time / drawStart, true), newSlot, newHand == 0);
            const FSwitchPose start = wrist(sample(oldSlot, 0.0f), oldSlot, newHand == 0);
            const float blend = glm::smoothstep(0.0f, drawStart * 0.35f, time);
            hand.position = glm::mix(start.position, hand.position, blend);
            hand.rotation = glm::slerp(start.rotation, hand.rotation, blend);
        }
        if (oldHand != newHand && time > stowEnd)
        {
            FSwitchPose& hand = hands[oldHand];
            hand = wrist(sample(oldSlot, (1.0f - time) / (1.0f - stowEnd), true), oldSlot, oldHand == 0);
            const FSwitchPose support = wrist(frames[newSlot], newSlot, oldHand == 0);
            const float blend = glm::smoothstep(0.75f, 1.0f, time);
            hand.position = glm::mix(hand.position, support.position, blend);
            hand.rotation = glm::slerp(hand.rotation, support.rotation, blend);
        }
        if (oldHand == newHand)
        {
            const FSwitchPose stowed = wrist(frames[oldSlot], oldSlot, false);
            const FSwitchPose drawn = wrist(frames[newSlot], newSlot, false);
            const float transfer = glm::smoothstep(stowEnd, drawStart, time);
            hands[1] = {glm::mix(stowed.position, drawn.position, transfer),
                        glm::slerp(stowed.rotation, drawn.rotation, transfer),
                        glm::mix(stowed.release, drawn.release, transfer)};
            const FSwitchPose start = wrist(sample(oldSlot, 0.0f), oldSlot, true);
            const FSwitchPose finish = wrist(frames[newSlot], newSlot, true);
            const glm::vec3 resting(0.12f, 0.20f, 0.25f);
            const float lower = glm::smoothstep(0.0f, 0.25f, time);
            const float support = glm::smoothstep(0.75f, 1.0f, time);
            hands[0] = {glm::mix(glm::mix(start.position, resting, lower), finish.position, support),
                        glm::slerp(start.rotation, finish.rotation, support), 0.0f};
        }
        const FSwitchPose& activeFrame = frames[state.activeWeaponSlot];
        SetRigBoneComponentRotation(asset, pose, transforms_, bones_.ikWeapon, torsoRotation * activeFrame.rotation);
        SetRigBoneComponentPosition(asset, pose, transforms_, bones_.ikWeapon,
                                    glm::vec3(torso * glm::vec4(activeFrame.position, 1.0f)));
        const auto solve = [&](int32_t upper, int32_t lower, int32_t hand, int slot)
        {
            const FSwitchPose& target = hands[slot];
            const glm::vec3 goal(torso * glm::vec4(target.position, 1.0f));
            const glm::quat handRotation = torsoRotation * target.rotation;
            const glm::vec3 pole = glm::mix(slot == 0 ? kPoleLeft : kPoleRight,
                                           glm::vec3(slot == 0 ? 0.65f : -0.65f, 0.75f, 0.30f), target.release);
            const glm::vec3 shoulder = RigBonePosition(transforms_, upper);
            const float upperLength = glm::length(RigBonePosition(transforms_, lower) - shoulder);
            const float lowerLength = glm::length(RigBonePosition(transforms_, hand) - RigBonePosition(transforms_, lower));
            const glm::vec3 reach = goal - shoulder;
            if (glm::length(reach) <= 1.0e-4f || upperLength <= 1.0e-4f || lowerLength <= 1.0e-4f) return;
            const glm::vec3 direction = glm::normalize(reach);
            const glm::vec3 poleOffset = glm::vec3(torso * glm::vec4(pole, 1.0f)) - shoulder;
            glm::vec3 bend = poleOffset - direction * glm::dot(poleOffset, direction);
            if (glm::length(bend) <= 1.0e-4f)
            {
                const glm::vec3 outward = torsoRotation * glm::vec3(upper == bones_.upperArmL ? 1.0f : -1.0f, 0.0f, 0.0f);
                bend = outward - direction * glm::dot(outward, direction);
            }
            bend = glm::normalize(bend);
            // NextDayz's arm bones point down local -Y and the elbow is a
            // -X hinge, matching rh_arm_ik in rig_humanoid.scad. Rebuild the
            // upper-arm basis from the bend plane instead of preserving its
            // old twist with an unconstrained shortest-arc rotation.
            const float cosineFlex = glm::clamp(
                (glm::dot(reach, reach) - upperLength * upperLength - lowerLength * lowerLength) /
                    (2.0f * upperLength * lowerLength),
                std::cos(glm::radians(150.0f)), std::cos(glm::radians(5.0f)));
            const float distance = std::sqrt(upperLength * upperLength + lowerLength * lowerLength +
                                             2.0f * upperLength * lowerLength * cosineFlex);
            const float cosine = glm::clamp((upperLength * upperLength + distance * distance - lowerLength * lowerLength) /
                                               (2.0f * upperLength * distance), -1.0f, 1.0f);
            const float sine = std::sqrt(std::max(0.0f, 1.0f - cosine * cosine));
            const glm::vec3 upperDirection = direction * cosine + bend * sine;
            const glm::vec3 elbowPlane = bend * cosine - direction * sine;
            const glm::quat upperRotation = glm::quat_cast(glm::mat3(glm::cross(upperDirection, elbowPlane),
                                                                    -upperDirection, -elbowPlane));
            SetRigBoneComponentRotation(asset, pose, transforms_, upper, upperRotation);
            auto& forearm = pose.bones[lower];
            forearm.r = glm::angleAxis(-std::acos(cosineFlex), kAxisX);
            forearm.Mark(ERigPoseComponent::Rotation);
            RefreshRigComponentTransforms(asset, pose, lower, transforms_);
            SetRigBoneComponentRotation(asset, pose, transforms_, hand, handRotation);
            const bool carrying = (slot == oldHand && time <= stowEnd && !state.slotWeaponIds[oldSlot].empty()) ||
                                  (slot == newHand && time >= drawStart && !state.slotWeaponIds[newSlot].empty());
            if (carrying)
            {
                gripError_ = std::max(gripError_, glm::length(RigBonePosition(transforms_, hand) - goal));
            }
        };
        solve(bones_.upperArmR, bones_.foreArmR, bones_.handR, 1);
        solve(bones_.upperArmL, bones_.foreArmL, bones_.handL, 0);
    }

    void FSurvivorPoseGraph::UpdateProcedural(const FPlayerPresentationState& state, const glm::mat4& componentToWorld)
    {
        const Assets::FRigDefinition& asset = *asset_;
        const FAnimationConfig& anim = config_;
        FRigPose& pose = animator_.MutablePose();
        ComputeRigComponentTransforms(asset, pose, transforms_);
        const auto valid = [this](int32_t bone) { return bone >= 0 && bone < static_cast<int32_t>(transforms_.size()); };

        // Whole-body lean, pivoting at the feet.
        if (valid(bones_.root) && (lean_.x != 0.0f || lean_.y != 0.0f))
        {
            RotateRigBoneInComponentSpace(asset, pose, transforms_, bones_.root,
                                          glm::angleAxis(glm::radians(lean_.x), kAxisZ) *
                                              glm::angleAxis(glm::radians(lean_.y), kAxisX));
        }

        // Aim offsets: torso and head take a share of the camera pitch and of
        // the twist between camera and lower body.
        const float busy = std::max(actionWeight_, state.locomotion.traversalAction != EPlayerTraversalAction::None
                                                       ? 1.0f
                                                       : 0.0f);
        const float pitchLimit = glm::radians(std::max(anim.AimPitchLimitDegrees, 1.0f));
        const float pitch = glm::clamp(state.aimPitchRadians, -pitchLimit, pitchLimit) * (1.0f - busy) *
                            (1.0f - 0.5f * sprintHoldWeight_);
        // In a traversal the body is squared to the obstacle; the torso stops
        // turning after the camera so the hands meet the lip.
        const float twist = WrapAngle(state.aimYawRadians - bodyYaw_) * (1.0f - traverseWeight_);
        const auto offset = [](float yaw, float pitchAmount)
        {
            return glm::angleAxis(yaw, kAxisY) * glm::angleAxis(pitchAmount, kAxisX);
        };
        if (valid(bones_.torso))
        {
            RotateRigBoneInComponentSpace(asset, pose, transforms_, bones_.torso,
                                          offset(twist * anim.TwistTorsoShare, pitch * anim.AimPitchTorsoShare));
        }
        if (valid(bones_.head))
        {
            RotateRigBoneInComponentSpace(asset, pose, transforms_, bones_.head,
                                          offset(twist * anim.TwistHeadShare, pitch * anim.AimPitchHeadShare));
        }

        gripError_ = 0.0f;
        if (state.weaponAction == EWeaponPresentationAction::Switch && valid(bones_.torso) && valid(bones_.pelvis) &&
            valid(bones_.ikWeapon) && valid(bones_.ikHandR) && valid(bones_.ikHandL) &&
            state.switchSourceSlot >= 0 && state.switchTargetSlot >= 0)
        {
            UpdateSwitchPose(state);
            return;
        }
        // Traversal hand plants, world -> component space. The clip already
        // places the hands near the lip; this pins them onto the real top
        // whatever its height, so palms never float or sink into the wall.
        const FPlayerLocomotionState& locomotion = state.locomotion;
        const bool planting = locomotion.traversalAction != EPlayerTraversalAction::None;
        const glm::mat4 toComponent = glm::inverse(componentToWorld);
        const std::array<float, 2> plantWeight = {planting ? locomotion.traversalHandWeight[0] : 0.0f,
                                                  planting ? locomotion.traversalHandWeight[1] : 0.0f};
        const std::array<glm::vec3, 2> plantTarget = {
            glm::vec3(toComponent * glm::vec4(locomotion.traversalHand[0], 1.0f)),
            glm::vec3(toComponent * glm::vec4(locomotion.traversalHand[1], 1.0f))};
        const auto plantHand = [&](int32_t upper, int32_t lower, int32_t hand, int side)
        {
            const float weight = plantWeight[side];
            if (weight <= 1.0e-3f || !valid(upper) || !valid(lower) || !valid(hand))
            {
                return;
            }
            // Elbows out and back, like pushing down on a wall top.
            const glm::mat4 torsoFrame = valid(bones_.torso) ? transforms_[bones_.torso] : glm::mat4(1.0f);
            const glm::vec3 pole(torsoFrame * glm::vec4(side == 0 ? 0.60f : -0.60f, 0.05f, -0.40f, 1.0f));
            const glm::quat rotation = RigBoneRotation(transforms_, hand);
            const glm::vec3 goal = glm::mix(RigBonePosition(transforms_, hand), plantTarget[side], weight);
            SolveRigTwoBoneIK(asset, pose, transforms_, upper, lower, hand, goal, pole, 1.0f);
            SetRigBoneComponentRotation(asset, pose, transforms_, hand, rotation, 1.0f);
        };
        // Measured after every solve, so a planted hand that cannot reach shows up.
        const auto measurePlants = [&]()
        {
            plantError_ = 0.0f;
            const std::array<int32_t, 2> hands = {bones_.handL, bones_.handR};
            for (int side = 0; side < 2; ++side)
            {
                if (plantWeight[side] > 0.98f && valid(hands[side]))
                {
                    plantError_ = std::max(plantError_,
                                           glm::length(RigBonePosition(transforms_, hands[side]) - plantTarget[side]));
                }
            }
        };

        if (weaponWeight_ <= 1.0e-3f || !valid(bones_.ikWeapon) || !valid(bones_.ikHandR) || !valid(bones_.ikHandL))
        {
            // Empty hands: both plant directly.
            plantHand(bones_.upperArmL, bones_.foreArmL, bones_.handL, 0);
            plantHand(bones_.upperArmR, bones_.foreArmR, bones_.handR, 1);
            measurePlants();
            return;
        }

        // The weapon takes the rest of the aim about the firing shoulder, so
        // the muzzle lands on the crosshair however the body is turned.
        const FWeaponRigHold hold = HoldFor(state.weaponId);
        const float carry = 1.0f - sprintHoldWeight_;
        const float aimAmount = holdWeight_ * (1.0f - busy);
        const glm::quat aim = glm::angleAxis((twist * (1.0f - anim.TwistTorsoShare) - turnSway_) * aimAmount, kAxisY) *
                              glm::angleAxis(pitch * (1.0f - anim.AimPitchTorsoShare) * carry * aimAmount, kAxisX);
        const glm::vec3 pivot = RigBonePosition(transforms_, bones_.upperArmR);
        glm::vec3 weaponPosition = pivot + aim * (RigBonePosition(transforms_, bones_.ikWeapon) - pivot);
        glm::quat weaponRotation = aim * RigBoneRotation(transforms_, bones_.ikWeapon);

        // Gait bob on top of the torso's own motion: a dip per step and a sway
        // per stride; at a sprint the arms pump the carried rifle instead.
        const float phase = animator_.NormalizedTime(locomotionLayer_);
        const float stepDip = 0.5f - 0.5f * std::cos(2.0f * glm::two_pi<float>() * phase);
        const float strideSway = std::sin(glm::two_pi<float>() * phase);
        const float bob = movingWeight_ * holdWeight_;
        weaponPosition += glm::vec3(-anim.WeaponBobSide * strideSway * carry * bob,
                                    -anim.WeaponBobHeight * stepDip * carry * bob +
                                        anim.SprintPumpHeight * std::sin(2.0f * glm::two_pi<float>() * phase) *
                                            sprintHoldWeight_ * bob,
                                    0.0f);
        weaponRotation = weaponRotation * glm::angleAxis(glm::radians(anim.SprintPumpRollDegrees) * strideSway *
                                                             sprintHoldWeight_ * bob,
                                                         kAxisZ);
        weaponPosition += weaponRotation * glm::vec3(0.0f, 0.0f, hold.extend * aimWeight_);
        // Crouch locomotion pitches pelvis + torso forward by about 30 degrees.
        // Its additive body pose must not drag ADS down with it. Rotate about
        // the firing shoulder, preserving the grip reach, then solve both arms
        // onto the raised weapon. Actual camera aim retains the recoil kick.
        const float crouchAim = crouchWeight_ * aimWeight_ * (1.0f - busy) * (1.0f - weaponActionWeight_);
        if (const FWeaponDef* weapon = FindWeaponDef(state.weaponId);
            weapon && !weapon->IsMelee() && crouchAim > 1.0e-3f)
        {
            const glm::vec3 direction = glm::inverse(glm::angleAxis(bodyYaw_, kAxisY)) * state.aimDirection;
            const glm::quat correction = glm::slerp(glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
                RigRotationBetween(weaponRotation * kAxisZ, direction), crouchAim);
            weaponPosition = pivot + correction * (weaponPosition - pivot);
            weaponRotation = correction * weaponRotation;
        }
        // The firing hand plants with the weapon in it: slide the weapon frame
        // so its grip lands on the plant, the hand IK below follows the grip.
        if (plantWeight[1] > 1.0e-3f)
        {
            const glm::vec3 gripLocal = glm::vec3(glm::inverse(transforms_[bones_.ikWeapon]) *
                                                  glm::vec4(RigBonePosition(transforms_, bones_.ikHandR), 1.0f));
            const glm::vec3 grip = weaponPosition + weaponRotation * gripLocal;
            weaponPosition += (plantTarget[1] - grip) * plantWeight[1];
        }
        SetRigBoneComponentRotation(asset, pose, transforms_, bones_.ikWeapon, weaponRotation);
        SetRigBoneComponentPosition(asset, pose, transforms_, bones_.ikWeapon, weaponPosition);
        if (hold.overrideSupport)
        {
            SetRigBoneComponentPosition(asset, pose, transforms_, bones_.ikHandL,
                                        glm::vec3(transforms_[bones_.ikWeapon] * glm::vec4(hold.supportWrist, 1.0f)));
        }

        // Both hands onto the weapon frame. The socket's bind is the inverse
        // of the right grip, so a solved right hand carries the weapon exactly.
        const glm::mat4 torso = valid(bones_.torso) ? transforms_[bones_.torso] : glm::mat4(1.0f);
        const glm::vec3 gripR = RigBonePosition(transforms_, bones_.ikHandR);
        const glm::quat gripRotationR = RigBoneRotation(transforms_, bones_.ikHandR);
        const glm::vec3 gripL = RigBonePosition(transforms_, bones_.ikHandL);
        const glm::quat gripRotationL = RigBoneRotation(transforms_, bones_.ikHandL);

        SolveRigTwoBoneIK(asset, pose, transforms_, bones_.upperArmR, bones_.foreArmR, bones_.handR, gripR,
                          glm::vec3(torso * glm::vec4(kPoleRight, 1.0f)), weaponWeight_);
        SetRigBoneComponentRotation(asset, pose, transforms_, bones_.handR, gripRotationR, weaponWeight_);
        gripError_ = glm::length(RigBonePosition(transforms_, bones_.handR) - gripR);

        const float support = weaponWeight_ * (1.0f - freeHandWeight_);
        SolveRigTwoBoneIK(asset, pose, transforms_, bones_.upperArmL, bones_.foreArmL, bones_.handL, gripL,
                          glm::vec3(torso * glm::vec4(kPoleLeft, 1.0f)), support);
        SetRigBoneComponentRotation(asset, pose, transforms_, bones_.handL, gripRotationL, support);
        plantHand(bones_.upperArmL, bones_.foreArmL, bones_.handL, 0);
        measurePlants();
    }
}
