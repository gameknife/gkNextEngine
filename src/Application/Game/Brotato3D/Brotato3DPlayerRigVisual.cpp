#include "Brotato3DPlayerRigVisual.hpp"

#include <algorithm>
#include <cmath>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>
#include <spdlog/spdlog.h>

#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Runtime/Components/PhysicsComponent.hpp"
#include "Engine/Runtime/Components/RenderComponent.hpp"
#include "Engine/Runtime/Scene/NodeUtils.hpp"
#include "Engine/Runtime/Scene/SceneBuilder.hpp"
#include "Modules/ScadLoader/FScadRig.h"

namespace Brotato3D
{
    namespace
    {
        // Slightly larger than life so the hero reads among the horde from the high camera.
        constexpr float rigScale = 1.12f;
        constexpr float parkedY = -100.0f;
        constexpr float authoredRunSpeed = 5.0f;     // GAIT_RUN in brotato_survivor.scad
        constexpr float idleBelowSpeed = 1.4f;
        constexpr float turnRate = 16.0f;            // rad/s the body follows the aim at
        constexpr float maxLeanRadians = 0.16f;
        const std::string weaponBonePrefix = "bone_wpn_";
        const glm::vec3 axisY(0.0f, 1.0f, 0.0f);

        float Approach(float current, float target, float seconds, float dt)
        {
            return seconds > 0.0f ? glm::mix(current, target, 1.0f - std::exp(-dt / seconds)) : target;
        }
    }

    bool FPlayerRigVisual::LoadRig(const std::string& rigPath, const std::string& weaponCatalogPath)
    {
        std::string error;
        std::vector<std::string> warnings;
        hasRig_ = ::Assets::FScadRigLoader::LoadRig(rigPath, {}, asset_, error, &warnings);
        if (!hasRig_)
        {
            SPDLOG_ERROR("[Brotato3D] failed to load player rig '{}': {}", rigPath, error);
        }
        for (const std::string& warning : warnings)
        {
            SPDLOG_WARN("[Brotato3D] player rig: {}", warning);
        }

        warnings.clear();
        hasWeapons_ = ::Assets::FScadRigLoader::LoadRig(weaponCatalogPath, {}, weaponAsset_, error, &warnings);
        if (!hasWeapons_)
        {
            SPDLOG_WARN("[Brotato3D] failed to load weapon catalog '{}': {}", weaponCatalogPath, error);
        }
        for (const std::string& warning : warnings)
        {
            SPDLOG_WARN("[Brotato3D] weapon catalog: {}", warning);
        }

        if (hasRig_)
        {
            const auto clip = [this](const char* name)
            {
                const ::Assets::FRigClip* found = asset_.FindClip(name);
                if (!found)
                {
                    SPDLOG_WARN("[Brotato3D] player rig is missing clip '{}'", name);
                }
                return found;
            };
            idleClip_ = clip("stand_idle");
            runClips_ = {clip("stand_run_f"), clip("stand_run_b"), clip("stand_run_l"), clip("stand_run_r")};
            readyClip_ = clip("weapon_ready");
            aimClip_ = clip("weapon_aim");
            recoilClip_ = clip("recoil_rifle");
            dashClip_ = clip("dash");
        }
        return hasRig_;
    }

    void FPlayerRigVisual::InjectAssets(std::vector<::Assets::Model>& models,
                                       std::vector<::Assets::FMaterial>& materials,
                                       uint32_t defaultTintMaterialId)
    {
        injected_ = false;
        if (!hasRig_)
        {
            return;
        }

        tintMaterialId_ = defaultTintMaterialId;
        partModelIds_.clear();
        partMaterialIds_.clear();
        for (const ::Assets::FRigPart& part : asset_.parts)
        {
            std::array<uint32_t, 16> sectionMaterials{};
            for (size_t section = 0; section < part.sectionColors.size() && section < sectionMaterials.size(); ++section)
            {
                sectionMaterials[section] = part.sectionTintable[section]
                                                ? tintMaterialId_
                                                : ::Assets::SceneBuilder::AddRigSectionMaterial(materials, part, section);
            }
            partMaterialIds_.push_back(sectionMaterials);
            models.push_back(asset_.partModels[part.modelIndex]);
            partModelIds_.push_back(static_cast<uint32_t>(models.size() - 1));
        }

        // The catalog packs one weapon per bone; each becomes a model that hangs on the socket.
        for (auto& [id, weapon] : weapons_)
        {
            weapon.node.reset();
        }
        weapons_.clear();
        if (hasWeapons_)
        {
            for (const ::Assets::FRigPart& part : weaponAsset_.parts)
            {
                if (part.bone < 0 || part.bone >= static_cast<int32_t>(weaponAsset_.bones.size()))
                {
                    continue;
                }
                const std::string& boneName = weaponAsset_.bones[part.bone].name;
                if (boneName.rfind(weaponBonePrefix, 0) != 0 || weapons_.contains(boneName.substr(weaponBonePrefix.size())))
                {
                    continue;
                }
                FWeaponModel weapon;
                const ::Assets::Model& model = weaponAsset_.partModels[part.modelIndex];
                models.push_back(model);
                weapon.modelId = static_cast<uint32_t>(models.size() - 1);
                for (size_t section = 0; section < part.sectionColors.size() && section < weapon.materials.size(); ++section)
                {
                    weapon.materials[section] = ::Assets::SceneBuilder::AddRigSectionMaterial(materials, part, section);
                }
                // The muzzle is the far end of the model along its barrel (engine +Z).
                weapon.muzzleDistance = std::max(0.2f, model.GetLocalAABBMax().z);
                weapons_.emplace(boneName.substr(weaponBonePrefix.size()), std::move(weapon));
            }
        }
        injected_ = true;
    }

    void FPlayerRigVisual::OnSceneLoaded(::Assets::Scene& scene)
    {
        if (!hasRig_ || !injected_)
        {
            return;
        }

        scene_ = &scene;
        worldNode_ = ::Assets::Node::CreateNode(
            "Brotato3D_PlayerRig", glm::vec3(0.0f, parkedY, 0.0f),
            glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(rigScale), scene.GenerateInstanceId());
        auto physics = std::make_shared<Runtime::PhysicsComponent>();
        physics->SetMobility(Runtime::ENodeMobility::Dynamic);
        worldNode_->AddComponent(physics);
        scene.AddNode(worldNode_);

        NextGameplay::FRigInstanceDesc desc;
        desc.namePrefix = "Brotato3D_PlayerRig";
        desc.partModelIds = partModelIds_;
        desc.partMaterialIds = partMaterialIds_;
        boneNodes_.clear();
        const auto rigRoot = NextGameplay::FRigInstance::Instantiate(scene, asset_, desc, boneNodes_);
        if (!rigRoot)
        {
            worldNode_.reset();
            scene_ = nullptr;
            return;
        }
        rigRoot->SetParent(worldNode_);

        partNodes_.assign(asset_.parts.size(), nullptr);
        for (size_t partIndex = 0; partIndex < asset_.parts.size(); ++partIndex)
        {
            const int32_t boneIndex = asset_.parts[partIndex].bone;
            if (boneIndex < 0 || boneIndex >= static_cast<int32_t>(boneNodes_.size()))
            {
                continue;
            }
            const std::string expectedName = fmt::format(
                "Brotato3D_PlayerRig/{}__part{}", asset_.bones[boneIndex].name, partIndex);
            for (const auto& child : boneNodes_[boneIndex]->Children())
            {
                if (child->GetName() == expectedName)
                {
                    partNodes_[partIndex] = child.get();
                    break;
                }
            }
        }

        // Weapons hang on the socket under the firing hand; only the equipped one is shown.
        const int32_t socketBone = asset_.FindBone("bone_weapon_socket");
        socketNode_ = socketBone >= 0 ? boneNodes_[socketBone] : nullptr;
        if (socketNode_)
        {
            for (auto& [id, weapon] : weapons_)
            {
                weapon.node = ::Assets::SceneBuilder::CreateRenderNode(
                    fmt::format("Brotato3D_PlayerWeapon_{}", id), glm::vec3(0.0f), glm::vec3(1.0f),
                    scene.GenerateInstanceId(), weapon.modelId, weapon.materials[0], false);
                if (auto render = weapon.node->GetComponent<Runtime::RenderComponent>())
                {
                    render->SetMaterials(weapon.materials);
                    render->SetRayCastVisible(false);
                }
                weapon.node->SetParent(socketNode_->shared_from_this());
                scene.AddNode(weapon.node);
            }
        }

        using NextGameplay::ERigLayerBlendMode;
        using NextGameplay::FRigBoneMask;
        FRigBoneMask arms = FRigBoneMask::FromSubtree(asset_, "bone_upperarm_l");
        for (const char* bone : {"bone_upperarm_r", "bone_forearm_r", "bone_hand_r", "bone_weapon_socket",
                                 "bone_ik_weapon", "bone_ik_hand_r", "bone_ik_hand_l"})
        {
            arms.SetBoneWeight(asset_, bone, 1.0f);
        }
        FRigBoneMask spine = FRigBoneMask::FromSubtree(asset_, "bone_head");
        spine.SetBoneWeight(asset_, "bone_torso", 1.0f);
        FRigBoneMask upper = arms;
        upper.SetBoneWeight(asset_, "bone_torso", 1.0f);

        animator_ = {};
        animator_.Bind(&asset_, boneNodes_, worldNode_.get());
        locomotionLayer_ = animator_.CreateLayer("locomotion", ERigLayerBlendMode::Override, FRigBoneMask::FullBody(asset_));
        holdLayer_ = animator_.CreateLayer("hold", ERigLayerBlendMode::Override, std::move(arms));
        holdBodyLayer_ = animator_.CreateLayer("hold_body", ERigLayerBlendMode::Additive, std::move(spine));
        recoilLayer_ = animator_.CreateLayer("recoil", ERigLayerBlendMode::Additive, std::move(upper));
        actionLayer_ = animator_.CreateLayer("action", ERigLayerBlendMode::Override, FRigBoneMask::FullBody(asset_));
        animator_.SetLoopBlend(locomotionLayer_, {{idleClip_, 1.0f}}, "gait", 1.0f, 0.0f);
        animator_.SetLayerWeight(actionLayer_, 0.0f);
        if (dashClip_)
        {
            animator_.SetLoopBlend(actionLayer_, {{dashClip_, 1.0f}}, "dash", 1.0f, 0.0f);
        }

        bound_ = true;
        dashing_ = false;
        localMove_ = glm::vec2(0.0f);
        aimWeight_ = 0.0f;
        SetVisible(visible_);
        ApplyTintMaterials();
        scene.MarkDirty();
    }

    void FPlayerRigVisual::OnSceneUnloaded()
    {
        animator_ = NextGameplay::FRigLayeredAnimator{};
        bound_ = false;
        scene_ = nullptr;
        worldNode_.reset();
        socketNode_ = nullptr;
        boneNodes_.clear();
        partNodes_.clear();
        for (auto& [id, weapon] : weapons_)
        {
            weapon.node.reset();
        }
    }

    void FPlayerRigVisual::SetVisible(bool visible)
    {
        visible_ = visible;
        for (::Assets::Node* node : partNodes_)
        {
            if (node)
            {
                if (auto render = node->GetComponent<Runtime::RenderComponent>())
                {
                    render->SetVisible(visible);
                }
            }
        }
        ApplyWeaponVisibility();
        if (!visible && worldNode_)
        {
            worldNode_->Translation().y = parkedY;
            worldNode_->RecalcTransform(true);
        }
    }

    void FPlayerRigVisual::ApplyWeaponVisibility()
    {
        for (auto& [id, weapon] : weapons_)
        {
            if (weapon.node)
            {
                ::Assets::NodeUtils::SetVisible(weapon.node, visible_ && id == weaponId_);
            }
        }
    }

    void FPlayerRigVisual::SetWeapon(const std::string& weaponId)
    {
        if (weaponId_ == weaponId)
        {
            return;
        }
        weaponId_ = weaponId;
        ApplyWeaponVisibility();
    }

    void FPlayerRigVisual::NotifyFired()
    {
        if (bound_ && recoilClip_)
        {
            animator_.PlayOneShot(recoilLayer_, recoilClip_, 1.0f, 0.0f, 0.03f, true);
        }
    }

    bool FPlayerRigVisual::MuzzleWorldPosition(glm::vec3& outPosition) const
    {
        const auto it = weapons_.find(weaponId_);
        if (!bound_ || !visible_ || it == weapons_.end() || !it->second.node)
        {
            return false;
        }
        const glm::mat4& world = it->second.node->WorldTransform();
        outPosition = glm::vec3(world * glm::vec4(0.0f, 0.03f, it->second.muzzleDistance, 1.0f));
        return true;
    }

    void FPlayerRigVisual::SetTintMaterial(uint32_t materialId)
    {
        tintMaterialId_ = materialId;
        ApplyTintMaterials();
    }

    void FPlayerRigVisual::ApplyTintMaterials()
    {
        for (size_t partIndex = 0; partIndex < asset_.parts.size() && partIndex < partNodes_.size(); ++partIndex)
        {
            ::Assets::Node* node = partNodes_[partIndex];
            if (!node)
            {
                continue;
            }
            auto render = node->GetComponent<Runtime::RenderComponent>();
            if (!render)
            {
                continue;
            }
            std::array<uint32_t, 16> materials = partMaterialIds_[partIndex];
            const ::Assets::FRigPart& part = asset_.parts[partIndex];
            for (size_t section = 0; section < part.sectionTintable.size() && section < materials.size(); ++section)
            {
                if (part.sectionTintable[section])
                {
                    materials[section] = tintMaterialId_;
                }
            }
            render->SetMaterials(materials);
        }
        if (scene_)
        {
            scene_->MarkDirty();
        }
    }

    void FPlayerRigVisual::ResetFacing(const glm::vec3& direction)
    {
        if (glm::length(glm::vec2(direction.x, direction.z)) > 0.05f)
        {
            bodyYaw_ = std::atan2(direction.x, direction.z);
        }
        localMove_ = glm::vec2(0.0f);
        smoothedVelocity_ = glm::vec3(0.0f);
        lean_ = glm::vec2(0.0f);
    }

    void FPlayerRigVisual::Update(const FPlayerRigInput& input, float deltaSeconds)
    {
        if (!bound_ || !worldNode_)
        {
            return;
        }
        const float dt = std::max(deltaSeconds, 0.0f);

        // The whole body turns to the aim; the legs take up the difference by strafing.
        if (glm::length(glm::vec2(input.facingDirection.x, input.facingDirection.z)) > 0.05f)
        {
            const float targetYaw = std::atan2(input.facingDirection.x, input.facingDirection.z);
            const float delta = std::remainder(targetYaw - bodyYaw_, glm::two_pi<float>());
            bodyYaw_ += std::clamp(delta, -turnRate * dt, turnRate * dt);
        }

        // Velocity in the body frame (+Z forward, +X the character's left), in run-speed units.
        const glm::vec3 flatVelocity(input.velocity.x, 0.0f, input.velocity.z);
        const glm::vec3 local = glm::angleAxis(-bodyYaw_, axisY) * flatVelocity;
        const glm::vec2 targetMove = input.dashing ? glm::vec2(0.0f) : glm::vec2(local.x, local.z) / authoredRunSpeed;
        localMove_ = glm::mix(localMove_, targetMove, 1.0f - std::exp(-14.0f * dt));
        const float speed = glm::length(localMove_) * authoredRunSpeed;

        const float moving = std::clamp(speed / idleBelowSpeed, 0.0f, 1.0f);
        float forward = std::max(localMove_.y, 0.0f);
        float backward = std::max(-localMove_.y, 0.0f);
        float left = std::max(localMove_.x, 0.0f);
        float right = std::max(-localMove_.x, 0.0f);
        const float sum = forward + backward + left + right;
        if (sum > 1.0e-4f)
        {
            const float normalize = moving / sum;
            forward *= normalize;
            backward *= normalize;
            left *= normalize;
            right *= normalize;
        }
        // Cadence follows the real speed (move-speed upgrades), within what a run can carry.
        const float playRate = moving > 0.5f ? std::clamp(speed / authoredRunSpeed, 0.7f, 1.7f) : 1.0f;
        animator_.SetLoopBlend(locomotionLayer_,
                               {{idleClip_, 1.0f - moving},
                                {runClips_[0], forward},
                                {runClips_[1], backward},
                                {runClips_[2], left},
                                {runClips_[3], right}},
                               "gait", playRate, 0.15f);

        const bool armed = weapons_.contains(weaponId_);
        aimWeight_ = Approach(aimWeight_, input.aiming ? 1.0f : 0.0f, 0.09f, dt);
        animator_.SetStaticBlend(holdLayer_, {{readyClip_, 1.0f - aimWeight_}, {aimClip_, aimWeight_}}, 0.0f);
        animator_.SetStaticBlend(holdBodyLayer_, {{readyClip_, 1.0f - aimWeight_}, {aimClip_, aimWeight_}}, 0.0f);
        animator_.SetLayerWeight(holdLayer_, armed ? 1.0f : 0.0f);
        animator_.SetLayerWeight(holdBodyLayer_, armed ? 1.0f : 0.0f);

        if (input.dashing != dashing_)
        {
            dashing_ = input.dashing;
            animator_.SetLayerWeight(actionLayer_, dashing_ ? 1.0f : 0.0f, dashing_ ? 0.05f : 0.16f);
        }

        // Lean into acceleration: starts dig in, stops rock back.
        const glm::vec3 acceleration = dt > 1.0e-5f ? (flatVelocity - smoothedVelocity_) / std::max(dt, 1.0f / 120.0f) : glm::vec3(0.0f);
        smoothedVelocity_ = glm::mix(smoothedVelocity_, flatVelocity, 1.0f - std::exp(-12.0f * dt));
        const glm::vec2 leanTarget = glm::clamp(glm::vec2(acceleration.x, acceleration.z) * 0.006f +
                                                    glm::vec2(flatVelocity.x, flatVelocity.z) * 0.012f,
                                                glm::vec2(-maxLeanRadians), glm::vec2(maxLeanRadians));
        lean_ = glm::mix(lean_, input.dashing ? glm::vec2(0.0f) : leanTarget, 1.0f - std::exp(-9.0f * dt));
        // Tilting toward +X is a rotation about -Z; toward +Z about +X.
        const glm::quat tilt = glm::angleAxis(lean_.y, glm::vec3(1.0f, 0.0f, 0.0f)) *
                               glm::angleAxis(-lean_.x, glm::vec3(0.0f, 0.0f, 1.0f));

        // The dash lunge faces where the dash goes, not where the gun points.
        float yaw = bodyYaw_;
        if (input.dashing && glm::length(flatVelocity) > 0.5f)
        {
            yaw = std::atan2(flatVelocity.x, flatVelocity.z);
        }
        worldNode_->Translation() = visible_ ? input.feetPosition
                                             : glm::vec3(input.feetPosition.x, parkedY, input.feetPosition.z);
        worldNode_->Rotation() = tilt * glm::angleAxis(yaw, axisY);
        animator_.Update(dt);
    }
}
