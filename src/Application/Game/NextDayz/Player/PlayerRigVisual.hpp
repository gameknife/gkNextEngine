#pragma once

// ============================================================================
// PlayerRigVisual.hpp - ScadRig soldier visual for the player, following the
// FRigPreview / FScadRigVisual single-instance pattern. The rig lives under a
// world node whose TRS tracks the controller. Injection lifecycle mirrors:
//   OnInit LoadRig -> BeforeSceneRebuild InjectAssets -> OnSceneLoaded Instantiate
// OnSceneUnloaded only clears runtime pointers (never the injected assets).
//
// Animation graph, evaluated once per frame:
//   clip layers (FRigLayeredAnimator::Evaluate)
//     locomotion   full body   18-clip blend space: stance x speed x direction
//     hold         arms + IK   ready / aim / sprint carry weapon frames
//     hold_body    torso+head  the same clips, additive: blade and head turn
//     weapon_*     reload / switch, same split as hold
//     recoil       upper body  additive per-shot kick
//     action       full body   loot
//   procedural pass (RigPoseOps on the blended pose)
//     whole-body lean into acceleration, aim pitch / twist split across
//     torso, head and weapon, weapon bob and turn sway, then both arms
//     IK-solved onto bone_ik_hand_r/l so the hands never leave the weapon.
// ============================================================================

#include <array>
#include <memory>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "Engine/Assets/Data/RigAsset.hpp"
#include "Gameplay/Rig/RigLayeredAnimator.h"

#include "Application/Game/NextDayz/NextDayzConfig.hpp"
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
        float HandHeightInTorso(int slot) const;

        // Drives world transform + animation for the frame. The body yaw lags
        // state.aimYawRadians; the upper body twists to cover the difference.
        void Update(const glm::vec3& feetPosition, const FPlayerPresentationState& state, float deltaSeconds);
        void TriggerRecoil(float scale);

        bool HasRig() const { return hasRig_; }
        bool IsVisible() const { return visible_; }
        float CameraOpacity() const { return cameraOpacity_; }
        const std::string& CurrentBaseClipName() const { return baseClipName_; }
        const std::string& CurrentWeaponActionClipName() const { return weaponActionClipName_; }
        float AimWeight() const { return aimWeight_; }
        float WeaponActionWeight() const { return weaponActionWeight_; }
        float BodyYaw() const { return bodyYaw_; }
        float LeanRollDegrees() const { return lean_.x; }
        bool TurningInPlace() const { return turningInPlace_; }
        bool RecoilActive() const;
        // World-space barrel tip and direction of the held weapon; false when none.
        bool WeaponMuzzle(glm::vec3& outPosition, glm::vec3& outForward) const;
        // Distance from the right hand to its grip target (m); 0 when no weapon.
        float GripError() const { return gripError_; }
        // Actual pose deviation from a forward-only elbow hinge (degrees).
        float ElbowConstraintErrorDegrees() const;
        // Worst distance (m) of a fully planted traversal hand from its lip target; 0 when none is planted.
        float PlantError() const { return plantError_; }

    private:
        struct FBones
        {
            int32_t root = -1;
            int32_t pelvis = -1;
            int32_t torso = -1;
            int32_t head = -1;
            int32_t upperArmL = -1;
            int32_t foreArmL = -1;
            int32_t handL = -1;
            int32_t upperArmR = -1;
            int32_t foreArmR = -1;
            int32_t handR = -1;
            int32_t ikWeapon = -1;
            int32_t ikHandL = -1;
            int32_t ikHandR = -1;
        };

        struct FClips
        {
            // Blend-space order: stand idle, walk/run/sprint x (f, b, l, r), crouch idle, crouch walk x 4.
            std::vector<const Assets::FRigClip*> locomotion;
            const Assets::FRigClip* ready = nullptr;
            const Assets::FRigClip* aim = nullptr;
            const Assets::FRigClip* sprint = nullptr;
            const Assets::FRigClip* recoil = nullptr;
        };

        Assets::Node* BoneNode(std::string_view boneName);
        void ResolveRig();
        void UpdateLocomotion(const FPlayerPresentationState& state, float deltaSeconds);
        void UpdateProcedural(const FPlayerPresentationState& state, float deltaSeconds);
        void UpdateSwitchPose(const FPlayerPresentationState& state);
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
        FBones bones_{};
        FClips clips_{};

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
        std::array<glm::vec3, kWeaponSlotCount> holsterPositions_{};
        NextGameplay::FRigLayeredAnimator animator_;
        NextGameplay::FRigLayerHandle locomotionLayer_ = NextGameplay::invalidRigLayerHandle;
        NextGameplay::FRigLayerHandle holdLayer_ = NextGameplay::invalidRigLayerHandle;
        NextGameplay::FRigLayerHandle holdBodyLayer_ = NextGameplay::invalidRigLayerHandle;
        NextGameplay::FRigLayerHandle weaponActionLayer_ = NextGameplay::invalidRigLayerHandle;
        NextGameplay::FRigLayerHandle weaponActionBodyLayer_ = NextGameplay::invalidRigLayerHandle;
        NextGameplay::FRigLayerHandle recoilLayer_ = NextGameplay::invalidRigLayerHandle;
        NextGameplay::FRigLayerHandle actionLayer_ = NextGameplay::invalidRigLayerHandle;
        bool bound_ = false;
        std::vector<glm::mat4> transforms_;

        bool visible_ = true;
        float cameraOpacity_ = 1.0f;
        FAnimationConfig animationConfig_{};
        float weaponScale_ = 1.0f;
        std::string baseClipName_;
        std::string weaponActionClipName_;
        std::array<uint32_t, kWeapons.size()> weaponModelIds_{};
        std::array<FWeaponMaterialSlots, kWeapons.size()> weaponMaterialIds_{};
        int weaponVisualIndex_ = -1;
        std::array<int, kWeaponSlotCount> holsteredWeaponVisualIndices_{{-1, -1, -1}};
        bool weaponAssetsSet_ = false;
        bool weaponVisible_ = false;
        std::array<bool, kWeaponSlotCount> holsteredWeaponVisible_{};

        // graph state
        bool yawInitialized_ = false;
        float bodyYaw_ = 0.0f;
        float previousAimYaw_ = 0.0f;
        bool turningInPlace_ = false;
        float crouchWeight_ = 0.0f;
        glm::vec2 moveDirection_{0.0f, 1.0f};   // body-relative: x=right, y=forward
        float aimWeight_ = 0.0f;
        float sprintHoldWeight_ = 0.0f;
        float holdWeight_ = 0.0f;
        float weaponWeight_ = 0.0f;              // weapon in hand at all
        float freeHandWeight_ = 0.0f;            // support hand off the weapon (loot, traversal)
        float traverseWeight_ = 0.0f;            // smoothed "in a traversal": torso stops tracking the camera
        float weaponActionWeight_ = 0.0f;
        float actionWeight_ = 0.0f;
        float movingWeight_ = 0.0f;
        glm::vec3 previousVelocity_{0.0f};
        glm::vec2 lean_{0.0f};                   // degrees: x=roll (top to the right), y=pitch (top forward)
        glm::vec2 leanVelocity_{0.0f};
        float turnSway_ = 0.0f;                  // radians the weapon lags behind a turn
        bool recoilActive_ = false;
        float gripError_ = 0.0f;
        float plantError_ = 0.0f;
    };
}
