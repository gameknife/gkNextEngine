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
#include "Application/Game/NextDayz/Player/WeaponRigHold.hpp"

namespace NextDayz
{
    namespace
    {
        constexpr float kParkedY = -1000.0f;
        const glm::vec3 kAxisX(1.0f, 0.0f, 0.0f);
        const glm::vec3 kAxisY(0.0f, 1.0f, 0.0f);
        const glm::vec3 kAxisZ(0.0f, 0.0f, 1.0f);

        // Weapons that are not held like the rifle the rig's grips were built for.
        FWeaponRigHold HoldFor(std::string_view weaponId) { return WeaponRigHoldFor(weaponId); }
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
        // The pose graph resolves bones and clips itself; say here what the rig lacks.
        const auto clip = [this](const std::string& name)
        {
            if (!asset_.FindClip(name)) SPDLOG_WARN("[NextDayz] rig is missing clip '{}'", name);
        };
        for (const auto name : survivorLocomotionNames) clip(std::string(name));
        for (const char* name : {"weapon_ready", "aim_rifle_center", "weapon_sprint", "recoil_rifle"}) clip(name);
        for (const char* weapon : {"knife", "crowbar", "axe"})
        {
            for (const char* action : {"low", "ready", "attack"})
            {
                clip(fmt::format("melee_{}_{}", weapon, action));
            }
        }
        if (asset_.FindBone("bone_ik_weapon") < 0 || asset_.FindBone("bone_ik_hand_l") < 0 ||
            asset_.FindBone("bone_ik_hand_r") < 0)
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

    FFirstPersonArmAssets PlayerRigVisual::FirstPersonArmAssets() const
    {
        FFirstPersonArmAssets arms;
        if (!hasRig_ || !injected_) return arms;
        arms.rig = &asset_;
        arms.weaponScale = weaponScale_;
        const auto isArm = [this](int32_t bone)
        {
            if (bone < 0) return false;
            for (const char* name : {"bone_upperarm_l", "bone_forearm_l", "bone_hand_l",
                                     "bone_upperarm_r", "bone_forearm_r", "bone_hand_r"})
                if (bone == asset_.FindBone(name)) return true;
            return false;
        };
        for (size_t index = 0; index < asset_.parts.size() && index < partModelIds_.size(); ++index)
        {
            const Assets::FRigPart& part = asset_.parts[index];
            if (!isArm(part.bone)) continue;
            FFirstPersonArmPart arm{part.bone, partModelIds_[index], partMaterialIds_[index], {}};
            for (size_t section = 0; section < part.sectionTintable.size() && section < arm.materials.size(); ++section)
            {
                if (part.sectionTintable[section]) arm.materials[section] = tintMaterialId_;
            }
            arms.parts.push_back(std::move(arm));
        }
        for (const FClothingPart& part : clothingParts_)
        {
            if (part.harness || !isArm(part.bone)) continue;
            arms.parts.push_back({part.bone, part.modelId, part.materialIds, part.itemId});
        }
        return arms;
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
                    const bool waist = slot == kSidearmSlot;
                    const FSurvivorPoseGraph::FHolster rest = FSurvivorPoseGraph::RestingHolster(slot);
                    holsteredWeaponNodes_[slot] = Assets::SceneBuilder::CreateRenderNode(
                        fmt::format("nd_player/weapon_holster_{}", slot), rest.position,
                        glm::vec3(weaponScale_), scene.GenerateInstanceId(), weaponModelIds_[0],
                        weaponMaterialIds_[0], false, rest.rotation, false);
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
        graph_.Bind(asset_, animationConfig_, [this](NextGameplay::FRigLayeredAnimator& animator)
        {
            animator.Bind(&asset_, boneNodes_, worldNode_.get());
        });
        graph_.Animator().Apply();
        scene.MarkDirty();
    }

    void PlayerRigVisual::OnSceneUnloaded()
    {
        // Only clear per-scene runtime state; injected models/materials belong to
        // the incoming scene rebuild and must survive (the AirportSim lesson).
        graph_.Unbind();
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
                graph_.SetRestingHolster(static_cast<int>(slot), slot == kSidearmSlot
                    ? glm::vec3(coat ? -0.26f : -0.235f, 0.08f, 0.04f)
                    : glm::vec3(slot == 0 ? stowedSide : -stowedSide, 0.24f, -stowedDepth));
                holsteredWeaponNodes_[slot]->Translation() = graph_.Resting(static_cast<int>(slot)).position;
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
        return graph_.Resting(slot).position;
    }

    float PlayerRigVisual::WeaponDistanceFromHolster(int slot) const
    {
        return holsteredWeaponNodes_[slot]
            ? glm::distance(holsteredWeaponNodes_[slot]->Translation(), HolsterPosition(slot)) : 0.0f;
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
        graph_.TriggerRecoil(scale);
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
            // At rest on the body, or travelling between holster and hand during a switch.
            holstered->Translation() = graph_.Holsters()[slot].position;
            holstered->Rotation() = graph_.Holsters()[slot].rotation;
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

    void PlayerRigVisual::UpdateSeated(const glm::vec3& origin, const FPlayerPresentationState& equipment,
                                      float deltaSeconds)
    {
        if (!graph_.IsBound() || !worldNode_) return;
        worldNode_->Translation() = origin;
        worldNode_->Rotation() = glm::quat(1,0,0,0);
        worldNode_->RecalcTransform(true);
        graph_.Seated(deltaSeconds);
        // Stow every weapon while seated. The Switch presentation flag exposes
        // all holsters without moving the hands away from the seated clip.
        auto stowed = equipment;
        stowed.hasWeapon = false;
        stowed.weaponAction = EWeaponPresentationAction::Switch;
        UpdateAttachments(stowed);
        graph_.Animator().Apply();
        if (scene_) scene_->MarkTransformDirty();
    }

    void PlayerRigVisual::Update(const glm::vec3& feetPosition, const FPlayerPresentationState& state,
                                 float deltaSeconds)
    {
        if (!graph_.IsBound() || !worldNode_)
        {
            return;
        }
        // The pose is the shared graph's (the Web client runs the same one);
        // here it is placed in the scene and written onto the bone nodes.
        graph_.Advance(feetPosition, state, deltaSeconds);
        const glm::vec3 pos = visible_ ? feetPosition : glm::vec3(feetPosition.x, kParkedY, feetPosition.z);
        worldNode_->Translation() = pos;
        worldNode_->Rotation() = glm::angleAxis(graph_.BodyYaw(), kAxisY);
        worldNode_->RecalcTransform(true);
        UpdateAttachments(state);
        graph_.Animator().Apply();
        if (scene_)
        {
            scene_->MarkTransformDirty();
        }
    }
}
