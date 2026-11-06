#include "Engine/Common/CoreMinimal.hpp"
#include "PlayerRigVisual.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include <glm/gtc/quaternion.hpp>

#include <fmt/format.h>
#include <spdlog/spdlog.h>

#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Assets/Data/Material.hpp"
#include "Engine/Runtime/Components/PhysicsComponent.hpp"
#include "Engine/Runtime/Components/RenderComponent.hpp"
#include "Engine/Runtime/Scene/NodeUtils.hpp"
#include "Engine/Runtime/Scene/SceneBuilder.hpp"
#include "Gameplay/Rig/RigInstance.h"
#include "Gameplay/Rig/RigPoseOps.h"
#include "Modules/ScadLoader/FScadRig.h"
#include "Application/Game/NextDayz/Data/ItemDefs.hpp"

namespace NextDayz
{
    namespace
    {
        constexpr float kParkedY = -1000.0f;
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
                // Grip into the palm (the Makarov's grip hangs below its origin),
                // support hand cupping the firing hand, arms extended.
                return {glm::vec3(0.0f, 0.025f, -0.015f), true, glm::vec3(0.07f, -0.03f, -0.02f), 0.12f};
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
            warnings.clear();
            if (!Assets::FScadRigLoader::LoadRig("assets/scad/characters/nextdayz_clothing.scad", {},
                                               clothingAsset_, error, &warnings))
            {
                SPDLOG_ERROR("[NextDayz] failed to load clothing: {}", error);
                hasRig_ = false;
                return false;
            }
            for (const std::string& warning : warnings)
            {
                SPDLOG_WARN("[NextDayz] clothing: {}", warning);
            }
            clothingParts_.clear();
            for (const Assets::FRigPart& part : clothingAsset_.parts)
            {
                const std::string& name = clothingAsset_.bones[part.bone].name;
                const size_t separator = name.find("__");
                const std::string itemId = separator == std::string::npos ? "" : name.substr(5, separator - 5);
                const FItemDef* definition = FindItemDef(itemId);
                const bool harness = separator != std::string::npos && name.substr(separator + 2) == "harness";
                const int32_t bone = separator == std::string::npos ? -1 :
                    asset_.FindBone(harness ? "bone_torso" : "bone_" + name.substr(separator + 2));
                if (!definition || definition->kind != EItemKind::Clothing || bone < 0)
                {
                    SPDLOG_ERROR("[NextDayz] invalid clothing binding '{}'", name);
                    hasRig_ = false;
                    return false;
                }
                FClothingPart binding;
                binding.itemId = itemId;
                binding.bone = bone;
                binding.harness = harness;
                binding.frontExtent = std::numeric_limits<float>::max();
                for (const auto& vertex : clothingAsset_.partModels[part.modelIndex].CPUVertices())
                {
                    binding.backExtent = std::max(binding.backExtent, -vertex.Position.z);
                    binding.frontExtent = std::min(binding.frontExtent, -vertex.Position.z);
                    binding.halfWidth = std::max(binding.halfWidth, std::abs(vertex.Position.x));
                }
                clothingParts_.push_back(std::move(binding));
            }
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
                                          const std::array<FWeaponMaterialSlots, kWeapons.size()>& materialIds)
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
                    sectionMaterials[section] = Assets::SceneBuilder::AddRigSectionMaterial(materials, part, section);
                }
            }
            partMaterialIds_.push_back(sectionMaterials);
            models.push_back(asset_.partModels[part.modelIndex]);
            partModelIds_.push_back(static_cast<uint32_t>(models.size() - 1));
        }

        // Faction tint for magenta placeholder sections (olive drab soldier).
        tintMaterialId_ = Assets::SceneBuilder::AddLambertianMaterial(materials, glm::vec3(0.28f, 0.32f, 0.24f));

        // Preload every variant before GPU instance buffers are sized. Changing
        // clothes later only switches existing render parts, never reloads a scene.
        for (size_t index = 0; index < clothingAsset_.parts.size(); ++index)
        {
            const Assets::FRigPart& part = clothingAsset_.parts[index];
            FClothingPart& binding = clothingParts_[index];
            binding.materialIds.fill(0);
            for (size_t section = 0; section < part.sectionColors.size() && section < binding.materialIds.size(); ++section)
            {
                binding.materialIds[section] = Assets::SceneBuilder::AddRigSectionMaterial(materials, part, section);
            }
            models.push_back(clothingAsset_.partModels[part.modelIndex]);
            binding.modelId = static_cast<uint32_t>(models.size() - 1);
        }

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

        // Capture only the body's render children; hiding covered skin must
        // never hide its bone (or the sleeves/limbs attached to that bone).
        bodyPartNodes_.clear();
        for (Assets::Node* bone : boneNodes_)
        {
            if (!bone) continue;
            for (const auto& child : bone->Children())
            {
                if (child->GetComponent<Runtime::RenderComponent>()) bodyPartNodes_.push_back(child.get());
            }
        }
        for (FClothingPart& part : clothingParts_)
        {
            part.node = Assets::SceneBuilder::CreateRenderNode(
                fmt::format("nd_player/clothing/{}{}", part.itemId, part.harness ? "/harness" : "/" + asset_.bones[part.bone].name),
                glm::vec3(0.0f), glm::vec3(1.0f), scene.GenerateInstanceId(), part.modelId, part.materialIds,
                false, glm::quat(1.0f, 0.0f, 0.0f, 0.0f), false);
            auto partPhysics = std::make_shared<Runtime::PhysicsComponent>();
            partPhysics->SetMobility(Runtime::ENodeMobility::Dynamic);
            part.node->AddComponent(std::move(partPhysics));
            part.node->SetParent(boneNodes_[part.bone]->shared_from_this());
            scene.AddNode(part.node);
        }
        if (weaponAssetsSet_)
        {
            if (Assets::Node* socket = BoneNode("bone_weapon_socket"))
            {
                weaponNode_ = Assets::SceneBuilder::CreateRenderNode(
                    "nd_player/weapon", glm::vec3(0.0f), glm::vec3(weaponScale_), scene.GenerateInstanceId(),
                    weaponModelIds_[0], weaponMaterialIds_[0], true, glm::quat(1.0f, 0.0f, 0.0f, 0.0f), false);
                weaponNode_->SetParent(socket->shared_from_this());
                scene.AddNode(weaponNode_);
                heldAttachments_.Bind(scene, weaponNode_, attachmentAssets_, "nd_player/weapon");
                weaponVisible_ = true;
                weaponVisualIndex_ = 0;
            }
            if (Assets::Node* torso = BoneNode("bone_torso"))
            {
                for (int slot = 0; slot < static_cast<int>(holsteredWeaponNodes_.size()); ++slot)
                {
                    // Character left is +X. Keep the barrels up and the
                    // grips (-Y in weapon space) pointing back (-Z).
                    const float side = slot == 0 ? 1.0f : -1.0f;
                    const bool waist = slot == kSidearmSlot;
                    holsterPositions_[slot] = waist ? glm::vec3(-0.235f, 0.08f, 0.04f)
                                                    : glm::vec3(side * 0.20f, 0.24f, -0.20f);
                    holsteredWeaponNodes_[slot] = Assets::SceneBuilder::CreateRenderNode(
                        fmt::format("nd_player/weapon_holster_{}", slot), holsterPositions_[slot],
                        glm::vec3(weaponScale_), scene.GenerateInstanceId(), weaponModelIds_[0],
                        weaponMaterialIds_[0], false, waist ? kWaistWeaponRotation : kBackWeaponRotation, false);
                    Assets::Node* parent = waist ? BoneNode("bone_pelvis") : torso;
                    holsteredWeaponNodes_[slot]->SetParent(parent->shared_from_this());
                    scene.AddNode(holsteredWeaponNodes_[slot]);
                    holsteredAttachments_[slot].Bind(scene, holsteredWeaponNodes_[slot], attachmentAssets_,
                                                     fmt::format("nd_player/holster_{}", slot));
                }
            }
        }

        // Presentation only: the controller capsule is the player's body. A
        // raycast-visible rig blocks the infected's line of sight with its own
        // rifle and catches the player's own aim ray.
        ApplyClothing();
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
        bodyPartNodes_.clear();
        for (FClothingPart& part : clothingParts_) part.node.reset();
        weaponNode_.reset();
        heldAttachments_.Clear();
        for (auto& attachments : holsteredAttachments_) attachments.Clear();
        holsteredWeaponNodes_ = {};
        weaponVisualIndex_ = -1;
        holsteredWeaponVisualIndices_.fill(-1);
        weaponVisible_ = false;
        holsteredWeaponVisible_.fill(false);
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

    void PlayerRigVisual::SetClothing(const std::vector<std::string>& equipped)
    {
        if (equippedClothing_ == equipped) return;
        equippedClothing_ = equipped;
        ApplyClothing();
    }

    void PlayerRigVisual::ApplyClothing()
    {
        const auto wears = [this](std::string_view id)
        {
            return std::find(equippedClothing_.begin(), equippedClothing_.end(), id) != equippedClothing_.end();
        };
        bool coat = false;
        bool trousers = false;
        bool carrier = false;
        for (const std::string& id : equippedClothing_)
        {
            if (const FItemDef* definition = FindItemDef(id))
            {
                coat |= definition->equipSlot == EEquipSlot::Torso;
                trousers |= definition->equipSlot == EEquipSlot::Legs;
                carrier |= definition->equipSlot == EEquipSlot::Vest;
            }
        }
        const float packClearance = carrier ? (coat ? 0.0f : 0.04f) : coat ? 0.045f : 0.07f;
        const float harnessDepth = carrier ? (coat ? 1.0f : 0.78f) : coat ? 0.72f : 0.57f;
        float stowedDepth = carrier ? 0.24f : coat ? 0.20f : 0.17f;
        float stowedSide = 0.20f;
        for (FClothingPart& part : clothingParts_)
        {
            if (!part.node) continue;
            part.node->GetComponent<Runtime::RenderComponent>()->SetVisible(wears(part.itemId));
            // Packs were authored over the thickest layer. Pull the bag toward
            // a bare back; compress just the harness depth to keep its front
            // over the coat/carrier and its shoulder bridge on the shoulder.
            const FItemDef* definition = FindItemDef(part.itemId);
            if (definition && definition->equipSlot == EEquipSlot::Back)
            {
                part.node->Translation().z = part.harness ? 0.0f : packClearance;
                part.node->Scale().z = part.harness ? harnessDepth : 1.0f;
                if (!part.harness && wears(part.itemId))
                {
                    // Beside the pack, halfway through its depth, rather
                    // than floating behind its outermost pocket.
                    stowedDepth = (part.frontExtent + part.backExtent) * 0.5f - packClearance;
                    stowedSide = part.halfWidth + 0.045f * weaponScale_ + 0.012f;
                }
            }
            else if (definition && definition->equipSlot == EEquipSlot::Vest)
            {
                part.node->Scale().z = coat ? 1.0f : 0.78f;
            }
        }
        for (Assets::Node* node : bodyPartNodes_)
        {
            const auto* parent = node->GetParent();
            const std::string_view name = parent ? std::string_view(parent->GetName()) : std::string_view{};
            const bool upper = name.ends_with("bone_torso") || name.find("bone_upperarm_") != std::string_view::npos ||
                               name.find("bone_forearm_") != std::string_view::npos;
            const bool lower = name.ends_with("bone_pelvis") || name.find("bone_thigh_") != std::string_view::npos ||
                               name.find("bone_calf_") != std::string_view::npos || name.find("bone_foot_") != std::string_view::npos;
            node->GetComponent<Runtime::RenderComponent>()->SetVisible(!(coat && upper) && !(trousers && lower));
        }
        for (size_t slot = 0; slot < holsteredWeaponNodes_.size(); ++slot)
        {
            if (holsteredWeaponNodes_[slot])
            {
                holsterPositions_[slot] = slot == kSidearmSlot ? glm::vec3(coat ? -0.26f : -0.235f, 0.08f, 0.04f)
                    : glm::vec3(slot == 0 ? stowedSide : -stowedSide, 0.24f, -stowedDepth);
                holsteredWeaponNodes_[slot]->Translation() = holsterPositions_[slot];
            }
        }
        if (scene_)
        {
            if (worldNode_) worldNode_->RecalcTransform(true);
            scene_->MarkDirty();
        }
    }

    float PlayerRigVisual::HolsteredMuzzleVertical(int slot) const
    {
        return holsteredWeaponNodes_[slot] ? (holsteredWeaponNodes_[slot]->Rotation() * kAxisZ).y : 0.0f;
    }

    glm::vec3 PlayerRigVisual::HolsterPosition(int slot) const
    {
        return holsterPositions_[slot];
    }

    float PlayerRigVisual::WeaponDistanceFromHolster(int slot) const
    {
        return holsteredWeaponNodes_[slot]
            ? glm::distance(holsteredWeaponNodes_[slot]->Translation(), holsterPositions_[slot]) : 0.0f;
    }

    float PlayerRigVisual::HandHeightInTorso(int slot) const
    {
        const int32_t hand = slot == 0 ? bones_.handL : bones_.handR;
        if (bones_.torso < 0 || hand < 0 || hand >= static_cast<int32_t>(transforms_.size())) return 0.0f;
        return glm::vec3(glm::inverse(transforms_[bones_.torso]) * transforms_[hand][3]).y;
    }

    std::string PlayerRigVisual::ClothingVisuals() const
    {
        std::string result;
        for (const FItemDef& definition : AllItemDefs())
        {
            if (definition.kind != EItemKind::Clothing) continue;
            size_t count = 0;
            for (const FClothingPart& part : clothingParts_)
            {
                if (part.itemId == definition.id && part.node && part.node->GetComponent<Runtime::RenderComponent>()->GetVisible()) ++count;
            }
            if (count > 0) result += fmt::format("{}:{};", definition.id, count);
        }
        return result;
    }

    bool PlayerRigVisual::IsBodyPartVisible(std::string_view boneName) const
    {
        for (const Assets::Node* node : bodyPartNodes_)
        {
            const auto* parent = node->GetParent();
            if (parent && parent->GetName() == "nd_player/" + std::string(boneName))
            {
                if (node->GetComponent<Runtime::RenderComponent>()->GetVisible()) return true;
            }
        }
        return false;
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
        const FWeaponModelPoints points = WeaponModelPoints(kWeapons[weaponVisualIndex_].id);
        outPosition = glm::vec3(world * glm::vec4(0.0f, points.boreHeight,
            points.muzzleForward + (WeaponSuppressed(heldLoadout_) ? SuppressorLength(kWeapons[weaponVisualIndex_].id) : 0.0f), 1.0f));
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

    float PlayerRigVisual::ElbowConstraintErrorDegrees() const
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

    void PlayerRigVisual::UpdateSwitchPose(const FPlayerPresentationState& state)
    {
        using namespace NextGameplay;
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
            const glm::quat holsterRotation = waist ? pelvisInTorso * kWaistWeaponRotation : kBackWeaponRotation;
            const glm::vec3 holsterPosition = waist ? glm::vec3(pelvisToTorso * glm::vec4(holsterPositions_[slot], 1.0f))
                                                    : holsterPositions_[slot];
            const glm::vec3 backPosition = holsterPosition - holsterRotation * HoldFor(state.slotWeaponIds[slot]).modelOffset;
            // Stow over the shoulder; draw around the waist. Keep the lower
            // route outside the pack and within a bent arm's reach.
            const float side = slot == 0 ? 1.0f : -1.0f;
            const glm::vec3 waypoint = waist ? glm::mix(readyPosition, backPosition, 0.6f) + glm::vec3(-0.05f, 0.10f, 0.12f)
                : fromBelow ? glm::vec3(side * std::max(0.36f, std::abs(holsterPositions_[slot].x) + 0.10f), 0.12f, 0.16f)
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
        // Both existing back nodes become independent moving weapons. During
        // the overlap neither model is replaced or teleported to the other hand.
        for (int slot = 0; slot < kWeaponSlotCount; ++slot)
        {
            const glm::mat4 toParent = slot == kSidearmSlot ? glm::inverse(pelvisToTorso) : glm::mat4(1.0f);
            holsteredWeaponNodes_[slot]->Translation() = glm::vec3(toParent * glm::vec4(frames[slot].position +
                frames[slot].rotation * HoldFor(state.slotWeaponIds[slot]).modelOffset, 1.0f));
            holsteredWeaponNodes_[slot]->Rotation() = slot == kSidearmSlot
                ? glm::inverse(pelvisInTorso) * frames[slot].rotation : frames[slot].rotation;
        }
        const auto wrist = [&](const FSwitchPose& frame, int slot, bool left)
        {
            const auto& grip = asset_.bones[left ? bones_.ikHandL : bones_.ikHandR];
            const FWeaponRigHold hold = HoldFor(state.slotWeaponIds[slot]);
            glm::vec3 local = left && hold.overrideSupport ? hold.supportWrist : grip.bindT;
            if (left && slot == 0 && slot == newSlot)
            {
                // Fetch the left-slot gun by its low grip too, then slide to
                // the handguard as the right hand takes the firing grip.
                local = glm::mix(asset_.bones[bones_.ikHandR].bindT, local, glm::smoothstep(0.78f, 1.0f, time));
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
        SetRigBoneComponentRotation(asset_, pose, transforms_, bones_.ikWeapon, torsoRotation * activeFrame.rotation);
        SetRigBoneComponentPosition(asset_, pose, transforms_, bones_.ikWeapon,
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
            SetRigBoneComponentRotation(asset_, pose, transforms_, upper, upperRotation);
            auto& forearm = pose.bones[lower];
            forearm.r = glm::angleAxis(-std::acos(cosineFlex), kAxisX);
            forearm.Mark(ERigPoseComponent::Rotation);
            RefreshRigComponentTransforms(asset_, pose, lower, transforms_);
            SetRigBoneComponentRotation(asset_, pose, transforms_, hand, handRotation);
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
        if (state.weaponAction == EWeaponPresentationAction::Switch && valid(bones_.torso) &&
            valid(bones_.ikWeapon) && valid(bones_.ikHandR) && valid(bones_.ikHandL) &&
            holsteredWeaponNodes_[0] && holsteredWeaponNodes_[1] && holsteredWeaponNodes_[2] &&
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
        // Crouch locomotion pitches pelvis + torso forward by about 30 degrees.
        // Its additive body pose must not drag ADS down with it. Rotate about
        // the firing shoulder, preserving the grip reach, then solve both arms
        // onto the raised weapon. Actual camera aim retains the recoil kick.
        const float crouchAim = crouchWeight_ * aimWeight_ * (1.0f - busy) * (1.0f - weaponActionWeight_);
        if (const FWeaponDef* weapon = FindWeaponDef(state.weaponId);
            weapon && !weapon->IsMelee() && crouchAim > 1.0e-3f)
        {
            const glm::vec3 direction = glm::inverse(worldNode_->Rotation()) * state.aimDirection;
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
        const bool switching = state.weaponAction == EWeaponPresentationAction::Switch;
        heldLoadout_ = state.slotAttachments[state.activeWeaponSlot];
        if (weaponNode_)
        {
            const int weaponIndex = WeaponIndex(state.weaponId);
            if (weaponIndex >= 0 && weaponIndex != weaponVisualIndex_)
            {
                weaponVisualIndex_ = weaponIndex;
                if (auto render = weaponNode_->GetComponent<Runtime::RenderComponent>())
                {
                    render->SetModelId(weaponModelIds_[weaponIndex]);
                    render->SetMaterials(weaponMaterialIds_[weaponIndex]);
                }
                weaponNode_->Translation() = HoldFor(state.weaponId).modelOffset;
                if (scene_)
                {
                    scene_->MarkDirty();
                }
            }
            const bool showWeapon = visible_ && state.hasWeapon && !switching;
            heldAttachments_.Update(state.weaponId, heldLoadout_, showWeapon);
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
            if (!switching)
            {
                holstered->Translation() = holsterPositions_[slot];
                holstered->Rotation() = slot == kSidearmSlot ? kWaistWeaponRotation : kBackWeaponRotation;
            }
            const int weaponIndex = WeaponIndex(state.slotWeaponIds[slot]);
            if (weaponIndex >= 0 && weaponIndex != holsteredWeaponVisualIndices_[slot])
            {
                holsteredWeaponVisualIndices_[slot] = weaponIndex;
                if (auto render = holstered->GetComponent<Runtime::RenderComponent>())
                {
                    render->SetModelId(weaponModelIds_[weaponIndex]);
                    render->SetMaterials(weaponMaterialIds_[weaponIndex]);
                }
                if (scene_)
                {
                    scene_->MarkDirty();
                }
            }
            const bool showHolstered = visible_ && weaponIndex >= 0 && (switching || slot != state.activeWeaponSlot);
            holsteredAttachments_[slot].Update(state.slotWeaponIds[slot], state.slotAttachments[slot], showHolstered);
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
