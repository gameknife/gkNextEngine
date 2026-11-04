#include "Engine/Common/CoreMinimal.hpp"
#include "PlayerRigVisual.hpp"

#include <algorithm>
#include <cmath>

#include <glm/gtc/quaternion.hpp>

#include <fmt/format.h>
#include <spdlog/spdlog.h>

#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Assets/Data/Material.hpp"
#include "Engine/Assets/Loaders/FProcModel.hpp"
#include "Engine/Runtime/Components/PhysicsComponent.hpp"
#include "Engine/Runtime/Components/RenderComponent.hpp"
#include "Engine/Runtime/Scene/NodeUtils.hpp"
#include "Engine/Runtime/Scene/SceneBuilder.hpp"
#include "Gameplay/Rig/RigInstance.h"
#include "Gameplay/Rig/RigPoseOps.h"
#include "Modules/ScadLoader/FScadRig.h"

namespace NextDayz
{
    namespace
    {
        constexpr float kParkedY = -1000.0f;
        const glm::vec3 kAxisX(1.0f, 0.0f, 0.0f);
        const glm::vec3 kAxisY(0.0f, 1.0f, 0.0f);
        const glm::vec3 kAxisZ(0.0f, 0.0f, 1.0f);

        // Elbow pole points in torso space (engine axes: +Z forward, -X right):
        // firing elbow down and out, support elbow down under the handguard.
        const glm::vec3 kPoleRight(-0.50f, 0.12f, -0.08f);
        const glm::vec3 kPoleLeft(0.45f, 0.08f, 0.15f);

        // Weapons that are not held like the rifle the rig's grips were built for.
        struct FWeaponRigHold
        {
            glm::vec3 modelOffset{0.0f};   // weapon model under the socket
            bool overrideSupport = false;
            glm::vec3 supportWrist{0.0f};  // left wrist in bone_ik_weapon space
            float extend = 0.0f;           // push the frame along the muzzle (m)
        };

        FWeaponRigHold HoldFor(std::string_view weaponId)
        {
            if (weaponId == "pistol")
            {
                // Grip into the palm, support hand cupping the firing hand, arms extended.
                return {glm::vec3(0.0f, 0.10f, -0.065f), true, glm::vec3(0.07f, -0.03f, -0.02f), 0.12f};
            }
            return {};
        }

        float Approach(float current, float target, float seconds, float dt)
        {
            return seconds > 0.0f ? glm::mix(current, target, 1.0f - std::exp(-dt / seconds)) : target;
        }

        float WrapAngle(float radians)
        {
            return std::remainder(radians, glm::two_pi<float>());
        }

        // Weights for {idle, a, b, c} anchors along speed.
        std::array<float, 4> SpeedWeights(float speed, float a, float b, float c)
        {
            if (speed <= 0.0f)
            {
                return {1.0f, 0.0f, 0.0f, 0.0f};
            }
            if (speed < a)
            {
                const float t = speed / a;
                return {1.0f - t, t, 0.0f, 0.0f};
            }
            if (speed < b)
            {
                const float t = (speed - a) / (b - a);
                return {0.0f, 1.0f - t, t, 0.0f};
            }
            if (speed < c)
            {
                const float t = (speed - b) / (c - b);
                return {0.0f, 0.0f, 1.0f - t, t};
            }
            return {0.0f, 0.0f, 0.0f, 1.0f};
        }
    }

    bool PlayerRigVisual::LoadRig(const std::string& scadPath)
    {
        std::string error;
        std::vector<std::string> warnings;
        hasRig_ = Assets::FScadRigLoader::LoadRig(scadPath, {}, asset_, error, &warnings);
        if (!hasRig_)
        {
            SPDLOG_ERROR("[NextDayz] failed to load soldier rig '{}': {}", scadPath, error);
        }
        for (const std::string& warning : warnings)
        {
            SPDLOG_WARN("[NextDayz] rig: {}", warning);
        }
        if (hasRig_)
        {
            ResolveRig();
        }
        return hasRig_;
    }

    void PlayerRigVisual::ResolveRig()
    {
        bones_.root = asset_.FindBone("bone_root");
        bones_.pelvis = asset_.FindBone("bone_pelvis");
        bones_.torso = asset_.FindBone("bone_torso");
        bones_.head = asset_.FindBone("bone_head");
        bones_.upperArmL = asset_.FindBone("bone_upperarm_l");
        bones_.foreArmL = asset_.FindBone("bone_forearm_l");
        bones_.handL = asset_.FindBone("bone_hand_l");
        bones_.upperArmR = asset_.FindBone("bone_upperarm_r");
        bones_.foreArmR = asset_.FindBone("bone_forearm_r");
        bones_.handR = asset_.FindBone("bone_hand_r");
        bones_.ikWeapon = asset_.FindBone("bone_ik_weapon");
        bones_.ikHandL = asset_.FindBone("bone_ik_hand_l");
        bones_.ikHandR = asset_.FindBone("bone_ik_hand_r");

        clips_ = {};
        const auto clip = [this](const std::string& name)
        {
            const Assets::FRigClip* found = asset_.FindClip(name);
            if (!found)
            {
                SPDLOG_WARN("[NextDayz] rig is missing clip '{}'", name);
            }
            return found;
        };
        clips_.locomotion.push_back(clip("stand_idle"));
        for (const char* gait : {"stand_walk", "stand_run", "stand_sprint"})
        {
            for (const char* direction : {"_f", "_b", "_l", "_r"})
            {
                clips_.locomotion.push_back(clip(fmt::format("{}{}", gait, direction)));
            }
        }
        clips_.locomotion.push_back(clip("crouch_idle"));
        for (const char* direction : {"_f", "_b", "_l", "_r"})
        {
            clips_.locomotion.push_back(clip(fmt::format("crouch_walk{}", direction)));
        }
        clips_.ready = clip("weapon_ready");
        clips_.aim = clip("aim_rifle_center");
        clips_.sprint = clip("weapon_sprint");
        clips_.recoil = clip("recoil_rifle");
        for (const char* weapon : {"knife", "crowbar", "axe"})
        {
            for (const char* action : {"low", "ready", "attack"})
            {
                clip(fmt::format("melee_{}_{}", weapon, action));
            }
        }
        if (bones_.ikWeapon < 0 || bones_.ikHandL < 0 || bones_.ikHandR < 0)
        {
            SPDLOG_WARN("[NextDayz] rig has no bone_ik_* targets; arms follow the clips only");
        }
    }

    void PlayerRigVisual::SetWeaponAssets(const std::array<uint32_t, kWeapons.size()>& modelIds,
                                          const std::array<uint32_t, kWeapons.size()>& materialIds)
    {
        weaponModelIds_ = modelIds;
        weaponMaterialIds_ = materialIds;
        weaponAssetsSet_ = true;
    }

    void PlayerRigVisual::InjectAssets(std::vector<Assets::Model>& models, std::vector<Assets::FMaterial>& materials)
    {
        if (!hasRig_)
        {
            return;
        }

        partModelIds_.clear();
        partMaterialIds_.clear();
        for (const Assets::FRigPart& part : asset_.parts)
        {
            std::array<uint32_t, 16> sectionMaterials = {0};
            for (size_t section = 0; section < part.sectionColors.size() && section < sectionMaterials.size();
                 ++section)
            {
                if (!part.sectionTintable[section])
                {
                    sectionMaterials[section] =
                        Assets::SceneBuilder::AddLambertianMaterial(materials, glm::vec3(part.sectionColors[section]));
                }
            }
            partMaterialIds_.push_back(sectionMaterials);
            models.push_back(asset_.partModels[part.modelIndex]);
            partModelIds_.push_back(static_cast<uint32_t>(models.size() - 1));
        }

        // Faction tint for magenta placeholder sections (olive drab soldier).
        tintMaterialId_ = Assets::SceneBuilder::AddLambertianMaterial(materials, glm::vec3(0.28f, 0.32f, 0.24f));

        // Clothing attachment meshes (helmet, backpack) + a shared dark material.
        models.push_back(Assets::FProcModel::CreateBox(glm::vec3(-0.14f, -0.02f, -0.15f), glm::vec3(0.14f, 0.14f, 0.16f)));
        helmetModelId_ = static_cast<uint32_t>(models.size() - 1);
        models.push_back(Assets::FProcModel::CreateBox(glm::vec3(-0.15f, -0.20f, -0.18f), glm::vec3(0.15f, 0.22f, -0.02f)));
        backpackModelId_ = static_cast<uint32_t>(models.size() - 1);
        clothingMaterialId_ = Assets::SceneBuilder::AddLambertianMaterial(materials, glm::vec3(0.10f, 0.11f, 0.10f));

        injected_ = true;
    }

    Assets::Node* PlayerRigVisual::BoneNode(std::string_view boneName)
    {
        const int32_t index = asset_.FindBone(boneName);
        if (index < 0 || index >= static_cast<int32_t>(boneNodes_.size()))
        {
            return nullptr;
        }
        return boneNodes_[index];
    }

    void PlayerRigVisual::OnSceneLoaded(Assets::Scene& scene)
    {
        if (!hasRig_ || !injected_)
        {
            return;
        }
        scene_ = &scene;

        // World node the whole rig hangs under; a Dynamic physics component makes
        // the engine re-upload its transform each frame (no collider bound).
        worldNode_ = Assets::Node::CreateNode("nd_player_rig", glm::vec3(0.0f, kParkedY, 0.0f),
                                              glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f),
                                              scene.GenerateInstanceId());
        auto physics = std::make_shared<Runtime::PhysicsComponent>();
        physics->SetMobility(Runtime::ENodeMobility::Dynamic);
        worldNode_->AddComponent(physics);
        scene.AddNode(worldNode_);

        // Resolve tint sections to the faction material.
        NextGameplay::FRigInstanceDesc desc;
        desc.namePrefix = "nd_player";
        desc.partModelIds = partModelIds_;
        desc.partMaterialIds = partMaterialIds_;
        for (size_t partIndex = 0; partIndex < asset_.parts.size(); ++partIndex)
        {
            const Assets::FRigPart& part = asset_.parts[partIndex];
            for (size_t section = 0; section < part.sectionTintable.size() && section < 16; ++section)
            {
                if (part.sectionTintable[section])
                {
                    desc.partMaterialIds[partIndex][section] = tintMaterialId_;
                }
            }
        }

        boneNodes_.clear();
        auto rigRoot = NextGameplay::FRigInstance::Instantiate(scene, asset_, desc, boneNodes_);
        if (rigRoot)
        {
            rigRoot->SetParent(worldNode_);
        }

        // Clothing attachment nodes (hidden until worn).
        std::array<uint32_t, 16> clothingMats = {0};
        clothingMats[0] = clothingMaterialId_;
        if (Assets::Node* head = BoneNode("bone_head"))
        {
            helmetNode_ = Assets::SceneBuilder::CreateRenderNode("nd_player/helmet", glm::vec3(0.0f, 0.16f, 0.0f),
                                                                 glm::vec3(1.0f), scene.GenerateInstanceId(),
                                                                 helmetModelId_, clothingMats, false);
            helmetNode_->SetParent(head->shared_from_this());
        }
        if (Assets::Node* torso = BoneNode("bone_torso"))
        {
            backpackNode_ = Assets::SceneBuilder::CreateRenderNode("nd_player/backpack", glm::vec3(0.0f, 0.28f, -0.12f),
                                                                   glm::vec3(1.0f), scene.GenerateInstanceId(),
                                                                   backpackModelId_, clothingMats, false);
            backpackNode_->SetParent(torso->shared_from_this());
        }
        if (weaponAssetsSet_)
        {
            if (Assets::Node* socket = BoneNode("bone_weapon_socket"))
            {
                std::array<uint32_t, 16> weaponMats = {0};
                weaponMats[0] = weaponMaterialIds_[0];
                weaponNode_ = Assets::SceneBuilder::CreateRenderNode(
                    "nd_player/weapon", glm::vec3(0.0f), glm::vec3(weaponScale_), scene.GenerateInstanceId(),
                    weaponModelIds_[0], weaponMats, true, glm::quat(1.0f, 0.0f, 0.0f, 0.0f), false);
                weaponNode_->SetParent(socket->shared_from_this());
                scene.AddNode(weaponNode_);
                weaponVisible_ = true;
                weaponVisualIndex_ = 0;
            }
            if (Assets::Node* torso = BoneNode("bone_torso"))
            {
                for (int slot = 0; slot < static_cast<int>(holsteredWeaponNodes_.size()); ++slot)
                {
                    std::array<uint32_t, 16> weaponMats = {0};
                    weaponMats[0] = weaponMaterialIds_[0];
                    const float side = slot == 0 ? -1.0f : 1.0f;
                    const glm::quat rotation =
                        glm::angleAxis(glm::radians(side * 24.0f), glm::vec3(0.0f, 0.0f, 1.0f)) *
                        glm::angleAxis(glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
                    holsteredWeaponNodes_[slot] = Assets::SceneBuilder::CreateRenderNode(
                        fmt::format("nd_player/weapon_back_{}", slot),
                        glm::vec3(side * 0.11f, 0.24f, -0.22f - slot * 0.025f),
                        glm::vec3(weaponScale_), scene.GenerateInstanceId(), weaponModelIds_[0],
                        weaponMats, false, rotation, false);
                    holsteredWeaponNodes_[slot]->SetParent(torso->shared_from_this());
                    scene.AddNode(holsteredWeaponNodes_[slot]);
                }
            }
        }

        // Presentation only: the controller capsule is the player's body. A
        // raycast-visible rig blocks the infected's line of sight with its own
        // rifle and catches the player's own aim ray.
        Assets::NodeUtils::SetRayCastVisibleRecursive(worldNode_, false);
        worldNode_->RecalcTransform(true);
        animator_.Bind(&asset_, boneNodes_, worldNode_.get());
        using NextGameplay::ERigLayerBlendMode;
        using NextGameplay::FRigBoneMask;
        locomotionLayer_ =
            animator_.CreateLayer("locomotion", ERigLayerBlendMode::Override, FRigBoneMask::FullBody(asset_));

        // Holds own the arms and the IK frames; their torso/head channels (the
        // blade) are added on top of the gait so the walk keeps its swing.
        FRigBoneMask armsMask = FRigBoneMask::FromSubtree(asset_, "bone_ik_weapon");
        for (const char* bone : {"bone_upperarm_l", "bone_forearm_l", "bone_hand_l", "bone_upperarm_r",
                                 "bone_forearm_r", "bone_hand_r", "bone_weapon_socket"})
        {
            armsMask.SetBoneWeight(asset_, bone, 1.0f);
        }
        FRigBoneMask bodyMask = FRigBoneMask::FromSubtree(asset_, "bone_head");
        bodyMask.SetBoneWeight(asset_, "bone_torso", 1.0f);

        holdLayer_ = animator_.CreateLayer("hold", ERigLayerBlendMode::Override, armsMask);
        holdBodyLayer_ = animator_.CreateLayer("hold_body", ERigLayerBlendMode::Additive, bodyMask);
        weaponActionLayer_ = animator_.CreateLayer("weapon_action", ERigLayerBlendMode::Override, armsMask);
        weaponActionBodyLayer_ = animator_.CreateLayer("weapon_action_body", ERigLayerBlendMode::Additive, bodyMask);

        FRigBoneMask recoilMask = FRigBoneMask::FromSubtree(asset_, "bone_torso");
        recoilMask.SetBoneWeight(asset_, "bone_head", 0.0f);
        recoilLayer_ = animator_.CreateLayer("recoil", ERigLayerBlendMode::Additive, recoilMask);
        actionLayer_ = animator_.CreateLayer("action", ERigLayerBlendMode::Override, FRigBoneMask::FullBody(asset_));

        for (const NextGameplay::FRigLayerHandle layer :
             {holdLayer_, holdBodyLayer_, weaponActionLayer_, weaponActionBodyLayer_, recoilLayer_, actionLayer_})
        {
            animator_.SetLayerWeight(layer, 0.0f);
        }
        if (clips_.locomotion.front())
        {
            animator_.SetLoopBlend(locomotionLayer_, {{clips_.locomotion.front(), 1.0f}}, "locomotion", 1.0f, 0.0f);
            baseClipName_ = clips_.locomotion.front()->name;
        }
        animator_.Update(0.0f);
        bound_ = true;
        yawInitialized_ = false;
        aimWeight_ = 0.0f;
        sprintHoldWeight_ = 0.0f;
        holdWeight_ = 0.0f;
        weaponWeight_ = 0.0f;
        freeHandWeight_ = 0.0f;
        traverseWeight_ = 0.0f;
        weaponActionWeight_ = 0.0f;
        weaponActionClipName_.clear();
        actionWeight_ = 0.0f;
        movingWeight_ = 0.0f;
        lean_ = glm::vec2(0.0f);
        leanVelocity_ = glm::vec2(0.0f);
        turnSway_ = 0.0f;
        recoilActive_ = false;
        scene.MarkDirty();
    }

    void PlayerRigVisual::OnSceneUnloaded()
    {
        // Only clear per-scene runtime state; injected models/materials belong to
        // the incoming scene rebuild and must survive (the AirportSim lesson).
        animator_ = NextGameplay::FRigLayeredAnimator{};
        locomotionLayer_ = NextGameplay::invalidRigLayerHandle;
        holdLayer_ = NextGameplay::invalidRigLayerHandle;
        holdBodyLayer_ = NextGameplay::invalidRigLayerHandle;
        weaponActionLayer_ = NextGameplay::invalidRigLayerHandle;
        weaponActionBodyLayer_ = NextGameplay::invalidRigLayerHandle;
        recoilLayer_ = NextGameplay::invalidRigLayerHandle;
        actionLayer_ = NextGameplay::invalidRigLayerHandle;
        bound_ = false;
        baseClipName_.clear();
        weaponActionClipName_.clear();
        scene_ = nullptr;
        worldNode_.reset();
        helmetNode_.reset();
        backpackNode_.reset();
        weaponNode_.reset();
        holsteredWeaponNodes_ = {};
        weaponVisualIndex_ = -1;
        holsteredWeaponVisualIndices_ = {{-1, -1}};
        weaponVisible_ = false;
        holsteredWeaponVisible_ = {{false, false}};
        boneNodes_.clear();
        transforms_.clear();
        aimWeight_ = 0.0f;
        weaponActionWeight_ = 0.0f;
        actionWeight_ = 0.0f;
        recoilActive_ = false;
    }

    void PlayerRigVisual::SetVisible(bool visible)
    {
        visible_ = visible;
    }

    void PlayerRigVisual::UpdateCameraFade(const glm::vec3& cameraPosition, float nearDistance,
                                          float farDistance, bool enabled)
    {
        cameraOpacity_ = 1.0f;
        const auto fade = [&](auto&& self, Assets::Node* node) -> void
        {
            if (!node) return;
            if (auto render = node->GetComponent<Runtime::RenderComponent>())
            {
                const float distance = glm::distance(cameraPosition, node->WorldTranslation());
                const float opacity = enabled ? glm::smoothstep(nearDistance, farDistance, distance) : 1.0f;
                render->SetMainOpacity(opacity);
                if (render->GetVisible()) cameraOpacity_ = std::min(cameraOpacity_, opacity);
            }
            for (const auto& child : node->Children()) self(self, child.get());
        };
        fade(fade, worldNode_.get());
        // Update already marks the animated scene dirty; opacity travels with its proxies.
    }

    void PlayerRigVisual::SetClothing(const std::string& clothingId, bool on)
    {
        std::shared_ptr<Assets::Node> node;
        if (clothingId == "helmet")
        {
            node = helmetNode_;
        }
        else if (clothingId == "backpack")
        {
            node = backpackNode_;
        }
        if (!node)
        {
            return;
        }
        if (auto render = node->GetComponent<Runtime::RenderComponent>())
        {
            render->SetVisible(on);
        }
        if (scene_)
        {
            scene_->MarkDirty();
        }
    }

    void PlayerRigVisual::TriggerRecoil(float scale)
    {
        if (!bound_)
        {
            return;
        }
        animator_.SetLayerWeight(recoilLayer_, glm::clamp(scale, 0.0f, 1.0f));
        animator_.PlayOneShot(recoilLayer_, clips_.recoil, 1.0f, 0.0f, animationConfig_.RecoilFadeOutSeconds, true);
        recoilActive_ = true;
    }

    bool PlayerRigVisual::WeaponMuzzle(glm::vec3& outPosition, glm::vec3& outForward) const
    {
        if (!weaponNode_ || !weaponVisible_ || weaponVisualIndex_ < 0 ||
            weaponVisualIndex_ >= static_cast<int>(kWeapons.size()))
        {
            return false;
        }
        const glm::mat4& world = weaponNode_->WorldTransform();
        outPosition = glm::vec3(
            world * glm::vec4(0.0f, 0.0f, WeaponMuzzleDistance(kWeapons[weaponVisualIndex_].id), 1.0f));
        outForward = glm::normalize(glm::vec3(world[2]));
        return true;
    }

    bool PlayerRigVisual::RecoilActive() const
    {
        return recoilActive_;
    }

    void PlayerRigVisual::UpdateLocomotion(const FPlayerPresentationState& state, float dt)
    {
        const FPlayerLocomotionState& locomotion = state.locomotion;
        const FAnimationConfig& anim = animationConfig_;

        if (locomotion.traversalAction != EPlayerTraversalAction::None)
        {
            // TraversalActionName matches the clip names one to one.
            const char* name = TraversalActionName(locomotion.traversalAction);
            animator_.SetManualBlend(locomotionLayer_, {{asset_.FindClip(name), 1.0f}}, locomotion.traversalTime01,
                                     anim.JumpFadeSeconds);
            baseClipName_ = name;
            return;
        }
        if (locomotion.jumpPhase == EPlayerJumpPhase::Up || locomotion.jumpPhase == EPlayerJumpPhase::Down)
        {
            const char* name = locomotion.jumpPhase == EPlayerJumpPhase::Up ? "jump_up" : "jump_down";
            animator_.SetManualBlend(locomotionLayer_, {{asset_.FindClip(name), 1.0f}}, locomotion.jumpPhaseTime01,
                                     anim.JumpFadeSeconds);
            baseClipName_ = name;
            return;
        }
        if (locomotion.jumpPhase == EPlayerJumpPhase::AirLoop)
        {
            animator_.SetLoopBlend(locomotionLayer_, {{asset_.FindClip("jump_air_loop"), 1.0f}}, "jump_air", 1.0f,
                                   anim.JumpFadeSeconds);
            baseClipName_ = "jump_air_loop";
            return;
        }

        // Blend space. Velocity is measured, so walls, acceleration and stops
        // all show up in the legs; it is read in the body frame because the
        // lower body is what the clips move.
        const glm::vec3 bodyForward(std::sin(bodyYaw_), 0.0f, std::cos(bodyYaw_));
        const glm::vec3 bodyRight(-std::cos(bodyYaw_), 0.0f, std::sin(bodyYaw_));
        const glm::vec2 local(glm::dot(locomotion.worldVelocity, bodyRight),
                              glm::dot(locomotion.worldVelocity, bodyForward));
        float speed = glm::length(local);
        if (speed < anim.MoveThreshold)
        {
            speed = 0.0f;
        }
        else
        {
            const glm::vec2 target = local / glm::length(local);
            const glm::vec2 blended = glm::mix(moveDirection_, target,
                                               1.0f - std::exp(-dt / std::max(anim.DirectionSmoothingSeconds, 1e-3f)));
            moveDirection_ = glm::length(blended) > 1.0e-3f ? glm::normalize(blended) : target;
        }

        const float right = std::max(moveDirection_.x, 0.0f);
        const float left = std::max(-moveDirection_.x, 0.0f);
        const float forward = std::max(moveDirection_.y, 0.0f);
        const float backward = std::max(-moveDirection_.y, 0.0f);
        const float directionSum = std::max(right + left + forward + backward, 1.0e-3f);
        const std::array<float, 4> direction = {forward / directionSum, backward / directionSum, left / directionSum,
                                                right / directionSum};

        const std::array<float, 4> stand = SpeedWeights(speed, anim.StandWalkAuthoredSpeed,
                                                        anim.StandRunAuthoredSpeed, anim.StandSprintAuthoredSpeed);
        const std::array<float, 4> crouch = SpeedWeights(speed, anim.CrouchWalkAuthoredSpeed,
                                                         anim.CrouchWalkAuthoredSpeed * 2.0f,
                                                         anim.CrouchWalkAuthoredSpeed * 3.0f);
        const float standing = 1.0f - crouchWeight_;

        std::vector<NextGameplay::FRigClipBlendSample> samples;
        samples.reserve(clips_.locomotion.size());
        const auto clipAt = [this](size_t index)
        {
            return index < clips_.locomotion.size() ? clips_.locomotion[index] : nullptr;
        };
        samples.push_back({clipAt(0), standing * stand[0]});
        float cadence = 0.0f;
        float cadenceWeight = 0.0f;
        for (size_t gait = 0; gait < 3; ++gait)
        {
            const float gaitWeight = standing * stand[gait + 1];
            for (size_t dir = 0; dir < 4; ++dir)
            {
                samples.push_back({clipAt(1 + gait * 4 + dir), gaitWeight * direction[dir]});
            }
            if (const Assets::FRigClip* clip = clipAt(1 + gait * 4); clip && clip->duration > 0.0f && gaitWeight > 0.0f)
            {
                cadence += gaitWeight / clip->duration;
                cadenceWeight += gaitWeight;
            }
        }
        samples.push_back({clipAt(13), crouchWeight_ * crouch[0]});
        const float crouchWalk = crouchWeight_ * (1.0f - crouch[0]);
        for (size_t dir = 0; dir < 4; ++dir)
        {
            samples.push_back({clipAt(14 + dir), crouchWalk * direction[dir]});
        }
        if (const Assets::FRigClip* clip = clipAt(14); clip && clip->duration > 0.0f && crouchWalk > 0.0f)
        {
            cadence += crouchWalk / clip->duration;
            cadenceWeight += crouchWalk;
        }

        // Past the last anchor the clips are sped up, not stretched.
        float overspeed = 1.0f;
        if (crouchWeight_ < 0.5f && speed > anim.StandSprintAuthoredSpeed)
        {
            overspeed = speed / anim.StandSprintAuthoredSpeed;
        }
        else if (crouchWeight_ >= 0.5f && speed > anim.CrouchWalkAuthoredSpeed)
        {
            overspeed = speed / anim.CrouchWalkAuthoredSpeed;
        }
        overspeed = glm::clamp(overspeed, anim.MinPlayRate, anim.MaxPlayRate);

        // The animator advances phase by the dominant clip's duration; convert
        // the blended cadence (cycles/s) into its play rate.
        const NextGameplay::FRigClipBlendSample* dominant = nullptr;
        for (const NextGameplay::FRigClipBlendSample& sample : samples)
        {
            if (sample.clip && (!dominant || sample.weight > dominant->weight))
            {
                dominant = &sample;
            }
        }
        float playRate = 1.0f;
        if (dominant && dominant->clip->duration > 0.0f && cadenceWeight > 1.0e-3f)
        {
            playRate = cadence / cadenceWeight * overspeed * dominant->clip->duration;
        }
        movingWeight_ = Approach(movingWeight_, glm::clamp(speed / anim.StandRunAuthoredSpeed, 0.0f, 1.0f), 0.1f, dt);

        animator_.SetLoopBlend(locomotionLayer_, std::move(samples), "locomotion", playRate,
                               anim.LocomotionFadeSeconds);
        if (const Assets::FRigClip* clip = animator_.DominantClip(locomotionLayer_))
        {
            baseClipName_ = clip->name;
        }
    }

    void PlayerRigVisual::UpdateProcedural(const FPlayerPresentationState& state, float dt)
    {
        using namespace NextGameplay;
        const FAnimationConfig& anim = animationConfig_;
        FRigPose& pose = animator_.MutablePose();
        ComputeRigComponentTransforms(asset_, pose, transforms_);
        const auto valid = [this](int32_t bone) { return bone >= 0 && bone < static_cast<int32_t>(transforms_.size()); };

        // Whole-body lean, pivoting at the feet.
        if (valid(bones_.root) && (lean_.x != 0.0f || lean_.y != 0.0f))
        {
            RotateRigBoneInComponentSpace(asset_, pose, transforms_, bones_.root,
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
            RotateRigBoneInComponentSpace(asset_, pose, transforms_, bones_.torso,
                                          offset(twist * anim.TwistTorsoShare, pitch * anim.AimPitchTorsoShare));
        }
        if (valid(bones_.head))
        {
            RotateRigBoneInComponentSpace(asset_, pose, transforms_, bones_.head,
                                          offset(twist * anim.TwistHeadShare, pitch * anim.AimPitchHeadShare));
        }

        gripError_ = 0.0f;
        // Traversal hand plants, world -> component space. The clip already
        // places the hands near the lip; this pins them onto the real top
        // whatever its height, so palms never float or sink into the wall.
        const FPlayerLocomotionState& locomotion = state.locomotion;
        const bool planting = locomotion.traversalAction != EPlayerTraversalAction::None;
        const glm::mat4 toComponent = glm::inverse(worldNode_->WorldTransform());
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
            SolveRigTwoBoneIK(asset_, pose, transforms_, upper, lower, hand, goal, pole, 1.0f);
            SetRigBoneComponentRotation(asset_, pose, transforms_, hand, rotation, 1.0f);
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
        // The firing hand plants with the weapon in it: slide the weapon frame
        // so its grip lands on the plant, the hand IK below follows the grip.
        if (plantWeight[1] > 1.0e-3f)
        {
            const glm::vec3 gripLocal = glm::vec3(glm::inverse(transforms_[bones_.ikWeapon]) *
                                                  glm::vec4(RigBonePosition(transforms_, bones_.ikHandR), 1.0f));
            const glm::vec3 grip = weaponPosition + weaponRotation * gripLocal;
            weaponPosition += (plantTarget[1] - grip) * plantWeight[1];
        }
        SetRigBoneComponentRotation(asset_, pose, transforms_, bones_.ikWeapon, weaponRotation);
        SetRigBoneComponentPosition(asset_, pose, transforms_, bones_.ikWeapon, weaponPosition);
        if (hold.overrideSupport)
        {
            SetRigBoneComponentPosition(asset_, pose, transforms_, bones_.ikHandL,
                                        glm::vec3(transforms_[bones_.ikWeapon] * glm::vec4(hold.supportWrist, 1.0f)));
        }

        // Both hands onto the weapon frame. The socket's bind is the inverse
        // of the right grip, so a solved right hand carries the weapon exactly.
        const glm::mat4 torso = valid(bones_.torso) ? transforms_[bones_.torso] : glm::mat4(1.0f);
        const glm::vec3 gripR = RigBonePosition(transforms_, bones_.ikHandR);
        const glm::quat gripRotationR = RigBoneRotation(transforms_, bones_.ikHandR);
        const glm::vec3 gripL = RigBonePosition(transforms_, bones_.ikHandL);
        const glm::quat gripRotationL = RigBoneRotation(transforms_, bones_.ikHandL);

        SolveRigTwoBoneIK(asset_, pose, transforms_, bones_.upperArmR, bones_.foreArmR, bones_.handR, gripR,
                          glm::vec3(torso * glm::vec4(kPoleRight, 1.0f)), weaponWeight_);
        SetRigBoneComponentRotation(asset_, pose, transforms_, bones_.handR, gripRotationR, weaponWeight_);
        gripError_ = glm::length(RigBonePosition(transforms_, bones_.handR) - gripR);

        const float support = weaponWeight_ * (1.0f - freeHandWeight_);
        SolveRigTwoBoneIK(asset_, pose, transforms_, bones_.upperArmL, bones_.foreArmL, bones_.handL, gripL,
                          glm::vec3(torso * glm::vec4(kPoleLeft, 1.0f)), support);
        SetRigBoneComponentRotation(asset_, pose, transforms_, bones_.handL, gripRotationL, support);
        plantHand(bones_.upperArmL, bones_.foreArmL, bones_.handL, 0);
        measurePlants();
    }

    void PlayerRigVisual::UpdateAttachments(const FPlayerPresentationState& state)
    {
        if (weaponNode_)
        {
            const int weaponIndex = WeaponIndex(state.weaponId);
            if (weaponIndex >= 0 && weaponIndex != weaponVisualIndex_)
            {
                weaponVisualIndex_ = weaponIndex;
                if (auto render = weaponNode_->GetComponent<Runtime::RenderComponent>())
                {
                    render->SetModelId(weaponModelIds_[weaponIndex]);
                    std::array<uint32_t, 16> materials = {0};
                    materials[0] = weaponMaterialIds_[weaponIndex];
                    render->SetMaterials(materials);
                }
                weaponNode_->Translation() = HoldFor(state.weaponId).modelOffset;
                if (scene_)
                {
                    scene_->MarkDirty();
                }
            }
            const bool showWeapon = visible_ && state.hasWeapon;
            if (showWeapon != weaponVisible_)
            {
                weaponVisible_ = showWeapon;
                if (auto render = weaponNode_->GetComponent<Runtime::RenderComponent>())
                {
                    render->SetVisible(showWeapon);
                }
                if (scene_)
                {
                    scene_->MarkDirty();
                }
            }
        }

        for (int slot = 0; slot < static_cast<int>(holsteredWeaponNodes_.size()); ++slot)
        {
            const std::shared_ptr<Assets::Node>& holstered = holsteredWeaponNodes_[slot];
            if (!holstered)
            {
                continue;
            }
            const int weaponIndex = WeaponIndex(state.slotWeaponIds[slot]);
            if (weaponIndex >= 0 && weaponIndex != holsteredWeaponVisualIndices_[slot])
            {
                holsteredWeaponVisualIndices_[slot] = weaponIndex;
                if (auto render = holstered->GetComponent<Runtime::RenderComponent>())
                {
                    render->SetModelId(weaponModelIds_[weaponIndex]);
                    std::array<uint32_t, 16> materials = {0};
                    materials[0] = weaponMaterialIds_[weaponIndex];
                    render->SetMaterials(materials);
                }
                if (scene_)
                {
                    scene_->MarkDirty();
                }
            }
            const bool showHolstered = visible_ && weaponIndex >= 0 && slot != state.activeWeaponSlot;
            if (showHolstered != holsteredWeaponVisible_[slot])
            {
                holsteredWeaponVisible_[slot] = showHolstered;
                if (auto render = holstered->GetComponent<Runtime::RenderComponent>())
                {
                    render->SetVisible(showHolstered);
                }
                if (scene_)
                {
                    scene_->MarkDirty();
                }
            }
        }
    }

    void PlayerRigVisual::Update(const glm::vec3& feetPosition, const FPlayerPresentationState& state,
                                 float deltaSeconds)
    {
        if (!bound_ || !worldNode_)
        {
            return;
        }
        const float dt = std::max(deltaSeconds, 0.0f);
        const FAnimationConfig& anim = animationConfig_;
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

        const glm::vec3 pos = visible_ ? feetPosition : glm::vec3(feetPosition.x, kParkedY, feetPosition.z);
        worldNode_->Translation() = pos;
        worldNode_->Rotation() = glm::angleAxis(bodyYaw_, kAxisY);
        worldNode_->RecalcTransform(true);

        // Layer weights.
        crouchWeight_ = Approach(crouchWeight_, locomotion.actualStance == EPlayerStance::Crouched ? 1.0f : 0.0f,
                                 anim.StanceBlendSeconds * 0.5f, dt);
        const bool holding = state.hasWeapon && !looting && !traversing;
        weaponWeight_ = Approach(weaponWeight_, state.hasWeapon ? 1.0f : 0.0f, 0.06f, dt);
        holdWeight_ = Approach(holdWeight_, holding ? 1.0f : 0.0f, anim.HoldFadeSeconds * 0.5f, dt);
        freeHandWeight_ = Approach(freeHandWeight_, looting || traversing ? 1.0f : 0.0f, 0.05f, dt);
        traverseWeight_ = Approach(traverseWeight_, traversing ? 1.0f : 0.0f, traversing ? 0.04f : 0.12f, dt);
        const bool weaponActionActive = holding && state.weaponAction != EWeaponPresentationAction::None;
        aimWeight_ = Approach(aimWeight_, holding && !weaponActionActive ? glm::clamp(state.aimWeight, 0.0f, 1.0f) : 0.0f,
                              anim.AimFadeSeconds * 0.5f, dt);
        sprintHoldWeight_ = Approach(sprintHoldWeight_, glm::clamp(state.sprintAlpha, 0.0f, 1.0f),
                                     anim.SprintHoldFadeSeconds * 0.3f, dt) ;
        weaponActionWeight_ = Approach(weaponActionWeight_, weaponActionActive ? 1.0f : 0.0f,
                                       anim.HoldFadeSeconds * 0.5f, dt);
        actionWeight_ = Approach(actionWeight_, looting ? 1.0f : 0.0f, anim.LocomotionFadeSeconds * 0.5f, dt);

        const FWeaponDef* weapon = FindWeaponDef(state.weaponId);
        const bool melee = weapon && weapon->IsMelee();
        const float sprint = sprintHoldWeight_ * (1.0f - aimWeight_);
        std::vector<NextGameplay::FRigClipBlendSample> holdSamples = {
            {clips_.ready, std::max(0.0f, 1.0f - aimWeight_ - sprint)},
            {clips_.aim, aimWeight_},
            {clips_.sprint, sprint},
        };
        if (melee)
        {
            holdSamples = {
                {asset_.FindClip(fmt::format("melee_{}_low", state.weaponId)), 1.0f - aimWeight_},
                {asset_.FindClip(fmt::format("melee_{}_ready", state.weaponId)), aimWeight_},
            };
        }
        animator_.SetStaticBlend(holdLayer_, holdSamples, 0.0f);
        animator_.SetStaticBlend(holdBodyLayer_, std::move(holdSamples), 0.0f);
        animator_.SetLayerWeight(holdLayer_, holdWeight_);
        animator_.SetLayerWeight(holdBodyLayer_, holdWeight_ * (1.0f - weaponActionWeight_));

        if (weaponActionActive)
        {
            const std::string clipName = state.weaponAction == EWeaponPresentationAction::Melee
                ? fmt::format("melee_{}_attack", state.weaponId)
                : state.weaponAction == EWeaponPresentationAction::Reload ? "reload_rifle" : "switch_weapon";
            weaponActionClipName_ = clipName;
            const float time = glm::clamp(state.weaponActionTime01, 0.0f, 1.0f);
            animator_.SetManualBlend(weaponActionLayer_, {{asset_.FindClip(clipName), 1.0f}}, time);
            animator_.SetManualBlend(weaponActionBodyLayer_, {{asset_.FindClip(clipName), 1.0f}}, time);
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
            animator_.SetManualBlend(actionLayer_, {{asset_.FindClip("loot_ground"), 1.0f}}, state.actionTime01);
            animator_.SetLayerWeight(recoilLayer_, 0.0f);
            recoilActive_ = false;
        }

        UpdateAttachments(state);
        UpdateLocomotion(state, dt);

        animator_.Evaluate(dt);
        UpdateProcedural(state, dt);
        animator_.Apply();
        if (recoilActive_ && animator_.IsOneShotComplete(recoilLayer_))
        {
            recoilActive_ = false;
        }
        if (scene_)
        {
            scene_->MarkTransformDirty();
        }
    }
}
