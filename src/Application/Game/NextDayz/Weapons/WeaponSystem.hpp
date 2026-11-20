#pragma once

#include "Application/Game/NextDayz/Player/FirstPersonArms.hpp"
#include "Application/Game/NextDayz/Weapons/WeaponRuntime.hpp"
#include "Application/Game/NextDayz/Weapons/WeaponAttachmentVisuals.hpp"

class NextEngine;

namespace NextDayz
{
    class WeaponSystem final : public WeaponRuntime
    {
    public:
        // View-model proc assets injected by the GameInstance (BeforeSceneRebuild).
        void SetViewModelAssets(const std::array<uint32_t, kWeapons.size()>& modelIds,
                                const std::array<FWeaponMaterialSlots, kWeapons.size()>& materialIds);

        void OnSceneLoaded(NextEngine& engine);
        void OnSceneUnloaded();
        void SetAttachmentAssets(const FAttachmentVisualAssets& assets) { attachmentAssets_ = assets; }
        int VisibleAttachments() const { return attachmentVisuals_.VisibleCount(); }
        float RenderedAttachmentError() const { return renderedAttachmentError_; }
        // The survivor's arms holding the first-person firearm (BeforeSceneRebuild).
        void SetFirstPersonArmAssets(FFirstPersonArmAssets assets) { arms_.SetAssets(std::move(assets)); }
        void SetArmClothing(const std::vector<std::string>& equipped) { arms_.SetClothing(equipped); }
        int VisibleArmParts() const { return arms_.VisibleCount(); }
        float ArmGripError() const { return arms_.GripError(); }
        float RenderedArmError() const { return renderedArmError_; }
        bool SightReady() const { return aimAlpha_ > 0.98f && viewModelVisible_; }
        void Update(float deltaSeconds, PlayerController& player, Inventory& inventory, NextEngine& engine);
        bool ViewModelRecoilActive() const;
        bool IsViewModelVisible() const { return viewModelVisible_; }
        // The lens image has replaced the scope mesh.
        bool IsScopeView() const { return scopeView_; }
        // 0 -> 1 as the eye closes on a magnified optic's eyepiece: the HUD
        // closes the housing vignette in from the screen edges with it.
        float ScopeBlend() const { return scopeBlend_; }
        // Look lag of the weapon (radians, x yaw +left, y pitch +muzzle down):
        // the eye box of a scope drifts with it, shading one side of the lens.
        glm::vec2 SwayAngle() const { return swayAngle_; }
        // World-space view-model landmarks. The scoped optical view keeps its
        // weapon pose for effects and validation even while the mesh is hidden.
        bool ViewModelMuzzle(glm::vec3& outPosition, glm::vec3& outForward) const;
        bool ViewModelSight(glm::vec3& outPosition, glm::vec3& outForward) const;
        // Snapshot of the proxy submitted alongside this tick's render camera.
        bool ViewModelRenderedSight(glm::vec3& outPosition, glm::vec3& outForward) const;
        // Glass of a mounted red dot / holographic sight: world centre and half
        // extents along the weapon's right (+X) and up axes.
        bool ViewModelReflexWindow(glm::vec3& outCenter, glm::vec3& outHalfX, glm::vec3& outHalfY) const;

    private:
        void RefreshViewModelVisibility() override;
        void ResetEquipPresentation() override;
        void UpdateViewModelMotion(const PlayerController& player, float deltaSeconds, glm::vec3& outOffset,
                                   glm::vec3& outEuler);

        // view model
        std::array<uint32_t, kWeapons.size()> viewModelModelIds_{};
        std::array<FWeaponMaterialSlots, kWeapons.size()> viewModelMaterialIds_{};
        bool viewModelAssetsSet_ = false;
        int viewModelWeaponIndex_ = -1;
        std::shared_ptr<Assets::Node> viewModelNode_;
        FAttachmentVisualAssets attachmentAssets_;
        WeaponAttachmentVisuals attachmentVisuals_;
        FirstPersonArms arms_;
        float renderedAttachmentError_ = 0.0f;
        float renderedArmError_ = 0.0f;
        bool viewModelVisible_ = false;
        bool scopeView_ = false;
        float scopeBlend_ = 0.0f;
        float previousAimProgress_ = 0.0f;
        bool renderedViewModelValid_ = false;
        glm::mat4 renderedViewModelTransform_{1.0f};
        bool viewModelInScene_ = false;
        glm::vec3 viewModelOffset_{0.16f, -0.19f, 0.55f};
        glm::vec3 viewModelMeleeEuler_{0.0f};
        glm::vec2 swayAngle_{0.0f};          // radians: x yaw (+left), y pitch (+muzzle down)
        glm::vec2 swayVelocity_{0.0f};
        glm::vec2 moveVelocity_{0.0f};       // smoothed camera-relative velocity
        float landingOffset_ = 0.0f;
        float landingVelocity_ = 0.0f;
        float breathTime_ = 0.0f;
        float aimAlpha_ = 0.0f;
    };
}
