#pragma once

// ============================================================================
// PlayerRigVisual.hpp - ScadRig soldier visual for the player, following the
// FRigPreview / FScadRigVisual single-instance pattern. The rig lives under a
// world node whose TRS tracks the controller. Injection lifecycle mirrors:
//   OnInit LoadRig -> BeforeSceneRebuild InjectAssets -> OnSceneLoaded Instantiate
// OnSceneUnloaded only clears runtime pointers (never the injected assets).
//
// The animation graph itself is Core/SurvivorPoseGraph, shared with the Web
// client; this class places it in the scene and dresses it (clothing, weapon
// and holster models, attachments, camera fade).
// ============================================================================

#include <array>
#include <memory>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "Engine/Assets/Data/RigAsset.hpp"
#include "Application/Game/NextDayz/Core/SurvivorPoseGraph.hpp"

#include "Application/Game/NextDayz/NextDayzConfig.hpp"
#include "Application/Game/NextDayz/Player/FirstPersonArms.hpp"
#include "Application/Game/NextDayz/Player/PlayerState.hpp"
#include "Application/Game/NextDayz/Weapons/WeaponDefs.hpp"
#include "Application/Game/NextDayz/Weapons/WeaponAttachmentVisuals.hpp"

namespace Assets
{
    class Scene;
    class Node;
    class Model;
    struct FMaterial;
}

namespace NextDayz
{
    class PlayerRigVisual
    {
    public:
        bool LoadRig(const std::string& scadPath);
        void Configure(const FAnimationConfig& config, float weaponScale)
        {
            animationConfig_ = config;
            weaponScale_ = weaponScale;
        }

        void InjectAssets(std::vector<Assets::Model>& models, std::vector<Assets::FMaterial>& materials);
        // The arm parts (skin and every sleeve variant) as injected above, for
        // the first-person arms. Valid after InjectAssets.
        FFirstPersonArmAssets FirstPersonArmAssets() const;
        void SetWeaponAssets(const std::array<uint32_t, kWeapons.size()>& modelIds,
                             const std::array<FWeaponMaterialSlots, kWeapons.size()>& materialIds);
        void OnSceneLoaded(Assets::Scene& scene);
        void SetAttachmentAssets(const FAttachmentVisualAssets& assets) { attachmentAssets_ = assets; }
        int HeldAttachmentCount() const { return heldAttachments_.VisibleCount(); }
        void OnSceneUnloaded();

        void SetVisible(bool visible);
        void UpdateCameraFade(const glm::vec3& cameraPosition, float nearDistance, float farDistance, bool enabled);
        void SetClothing(const std::vector<std::string>& equipped);
        // Actual render state, also exposed to the interactive validation runner.
        std::string ClothingVisuals() const;
        bool IsBodyPartVisible(std::string_view boneName) const;
        bool IsHeldWeaponVisible() const { return weaponVisible_; }
        bool IsHolsteredWeaponVisible(int slot) const { return holsteredWeaponVisible_[slot]; }
        glm::vec3 HolsterPosition(int slot) const;
        float WeaponDistanceFromHolster(int slot) const;
        float HolsteredMuzzleVertical(int slot) const;
        float HandHeightInTorso(int slot) const { return graph_.HandHeightInTorso(slot); }

        // Drives world transform + animation for the frame. The body yaw lags
        // state.aimYawRadians; the upper body twists to cover the difference.
        void Update(const glm::vec3& feetPosition, const FPlayerPresentationState& state, float deltaSeconds);
        void UpdateSeated(const glm::vec3& origin, const FPlayerPresentationState& equipment, float deltaSeconds);
        void TriggerRecoil(float scale);

        bool HasRig() const { return hasRig_; }
        const std::shared_ptr<Assets::Node>& WorldRoot() const { return worldNode_; }
        bool IsVisible() const { return visible_; }
        float CameraOpacity() const { return cameraOpacity_; }
        const std::string& CurrentBaseClipName() const { return graph_.CurrentBaseClipName(); }
        const std::string& CurrentWeaponActionClipName() const { return graph_.CurrentWeaponActionClipName(); }
        float AimWeight() const { return graph_.AimWeight(); }
        float WeaponActionWeight() const { return graph_.WeaponActionWeight(); }
        float BodyYaw() const { return graph_.BodyYaw(); }
        float LeanRollDegrees() const { return graph_.LeanRollDegrees(); }
        bool TurningInPlace() const { return graph_.TurningInPlace(); }
        bool RecoilActive() const { return graph_.RecoilActive(); }
        // World-space barrel tip and direction of the held weapon; false when none.
        bool WeaponMuzzle(glm::vec3& outPosition, glm::vec3& outForward) const;
        // Distance from the right hand to its grip target (m); 0 when no weapon.
        float GripError() const { return graph_.GripError(); }
        // Actual pose deviation from a forward-only elbow hinge (degrees).
        float ElbowConstraintErrorDegrees() const { return graph_.ElbowConstraintErrorDegrees(); }
        // Worst distance (m) of a fully planted traversal hand from its lip target; 0 when none is planted.
        float PlantError() const { return graph_.PlantError(); }

    private:
        Assets::Node* BoneNode(std::string_view boneName);
        void ResolveRig();
        void UpdateAttachments(const FPlayerPresentationState& state);
        void ApplyClothing();

        struct FClothingPart
        {
            std::string itemId;
            int32_t bone = -1;
            bool harness = false;
            float backExtent = 0.0f;
            float frontExtent = 0.0f;
            float halfWidth = 0.0f;
            uint32_t modelId = 0;
            std::array<uint32_t, 16> materialIds{};
            std::shared_ptr<Assets::Node> node;
        };

        Assets::FRigAsset asset_{};
        Assets::FRigAsset clothingAsset_{};
        std::vector<FClothingPart> clothingParts_;
        std::vector<std::string> equippedClothing_;
        bool hasRig_ = false;

        // injection results (recorded in InjectAssets, valid for the next scene)
        std::vector<uint32_t> partModelIds_;
        std::vector<std::array<uint32_t, 16>> partMaterialIds_;
        uint32_t tintMaterialId_ = 0;
        bool injected_ = false;

        // runtime scene state
        Assets::Scene* scene_ = nullptr;
        std::shared_ptr<Assets::Node> worldNode_;
        std::vector<Assets::Node*> boneNodes_;
        std::vector<Assets::Node*> bodyPartNodes_;
        std::shared_ptr<Assets::Node> weaponNode_;
        FAttachmentVisualAssets attachmentAssets_;
        WeaponAttachmentVisuals heldAttachments_;
        std::array<WeaponAttachmentVisuals, kWeaponSlotCount> holsteredAttachments_;
        FWeaponAttachments heldLoadout_{};
        std::array<std::shared_ptr<Assets::Node>, kWeaponSlotCount> holsteredWeaponNodes_;
        FSurvivorPoseGraph graph_;
        bool visible_ = true;
        float cameraOpacity_ = 1.0f;
        FAnimationConfig animationConfig_{};
        float weaponScale_ = 1.0f;
        std::array<uint32_t, kWeapons.size()> weaponModelIds_{};
        std::array<FWeaponMaterialSlots, kWeapons.size()> weaponMaterialIds_{};
        int weaponVisualIndex_ = -1;
        std::array<int, kWeaponSlotCount> holsteredWeaponVisualIndices_{{-1, -1, -1}};
        bool weaponAssetsSet_ = false;
        bool weaponVisible_ = false;
        std::array<bool, kWeaponSlotCount> holsteredWeaponVisible_{};

    };
}
