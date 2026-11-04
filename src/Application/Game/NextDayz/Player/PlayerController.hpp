#pragma once

#include <string>

// ============================================================================
// PlayerController.hpp - The "3C" core: a Jolt-backed NextCharacterController
// plus first/third-person camera (yaw/pitch), WASD/sprint/jump movement and
// ADS-driven FOV smoothing. Owns all player-movement state; the GameInstance
// only feeds it input and reads back the camera basis.
// ============================================================================

#include <glm/glm.hpp>

#include "Gameplay/Character/NextCharacterController.h"

#include "Application/Game/NextDayz/NextDayzConfig.hpp"
#include "Application/Game/NextDayz/Player/PlayerState.hpp"

class NextPhysics;
class NextEngine;

namespace NextDayz
{
    class PlayerController
    {
    public:
        void Create(NextPhysics* physics, const glm::vec3& spawnPosition, const FConfig& config);
        void Destroy();
        bool IsValid() const { return controller_.IsValid(); }

        // --- input feed (called from GameInstance input handlers) ---
        void OnLook(float dxPixels, float dyPixels);
        void SetMoveKey(int dir, bool pressed); // 0=fwd 1=back 2=left 3=right
        void SetWalkModifier(bool pressed) { walkModifier_ = pressed; }
        void SetSprintModifier(bool pressed) { sprintModifier_ = pressed; }
        void QueueContextualJump(NextEngine& engine);
        void QueueCrouchToggle() { crouchToggleQueued_ = true; }
        void SetAiming(bool aiming) { aiming_ = aiming; }
        void SetMeleeEquipped(bool melee) { meleeEquipped_ = melee; }
        void SetMovementLocked(bool locked) { movementLocked_ = locked; }
        // Holding the trigger ends a sprint (the weapon has to come up first).
        void SetSprintBlocked(bool blocked) { sprintBlocked_ = blocked; }
        void SetAimFov(float fov) { aimFov_ = fov; } // absolute FOV target while ADS
        void ApplyCameraRecoil(const glm::vec2& impulseRadians);
        void SetYaw(float yawRadians) { yaw_ = yawRadians; }
        void SetPitch(float pitchRadians) { pitch_ = pitchRadians; }
        void SetFirstPerson(bool firstPerson) { firstPerson_ = firstPerson; }
        void ToggleView();
        void AdjustCameraDistance(float scrollY);

        // --- per-frame update ---
        void Update(float deltaSeconds, NextEngine& engine);

        // --- queries ---
        glm::vec3 Position() const { return controller_.GetPosition(); }
        glm::vec3 EyePosition() const;
        glm::vec3 CameraPosition() const;
        float CameraArmLength() const;
        float CameraArmFraction() const;
        glm::vec3 Forward() const;   // full 3D view direction (yaw+pitch)
        glm::vec3 Right() const;     // horizontal right
        glm::vec3 Up() const;
        float Yaw() const { return yaw_; }
        float Pitch() const { return pitch_; }
        float CurrentFov() const { return currentFov_; }
        bool IsFirstPerson() const { return firstPerson_; }
        bool IsAiming() const { return aiming_; }
        bool IsSprinting() const { return locomotion_.gait == EPlayerGait::Sprint; }
        bool IsTraversing() const { return traversalAction_ != EPlayerTraversalAction::None; }
        const std::string& TraversalProbeResult() const { return traversalProbeResult_; }
        const FPlayerLocomotionState& LocomotionState() const { return locomotion_; }
        float HorizontalSpeed() const { return locomotion_.horizontalSpeed; }
        float ControllerHeight() const { return controller_.GetHeight(); }
        bool RecoilActive() const;
        glm::vec2 CameraRecoil() const { return cameraRecoil_; }

        // --- feel signals (view model, camera, rig) ---
        glm::vec2 LookDelta() const { return lookDelta_; }        // radians this frame: x=yaw, y=pitch
        glm::vec2 LocalVelocity() const;                           // measured, camera-relative: x=right, y=forward
        float StridePhase() const { return stridePhase_; }         // [0,1) per two steps, distance driven
        float SprintAlpha() const { return sprintAlpha_; }         // 0 = weapon up, 1 = sprint carry
        bool WeaponReady() const { return sprintAlpha_ < config_.Player.WeaponReadyThreshold; }
        float LandingSpeed() const { return landingSpeed_; }       // m/s on the landing frame, else 0
        float CrouchAlpha() const;

        // Fills a render camera (view matrix + FOV) for FPS or TPS.
        void FillCamera(glm::mat4& outModelView, float& outFov) const;

    private:
        glm::vec3 MoveForward() const; // horizontal (yaw only)
        bool TryBeginTraversal(NextEngine& engine);
        void UpdateTraversal(float deltaSeconds);
        glm::vec3 UpdateMotion(float deltaSeconds, const glm::vec3& previousPosition); // returns raw measured velocity
        void UpdateView(float deltaSeconds);
        void UpdateCameraArm(float deltaSeconds, NextEngine& engine);
        glm::vec3 ThirdPersonFocus() const;
        glm::vec3 ThirdPersonOffset() const;
        float StrideLength(float speed) const;

        NextCharacterController controller_;
        FConfig config_{};

        float yaw_ = 0.0f;
        float pitch_ = 0.0f;
        bool firstPerson_ = true;
        bool walkModifier_ = false;
        bool sprintModifier_ = false;
        bool sprintBlocked_ = false;
        bool aiming_ = false;
        bool meleeEquipped_ = false;
        bool movementLocked_ = false;
        bool jumpQueued_ = false;
        bool crouchToggleQueued_ = false;
        bool keyMove_[4] = {false, false, false, false};

        float currentFov_ = 75.0f;
        float aimFov_ = 55.0f;
        float camDistance_ = 4.5f;
        float cameraArmLength_ = 0.0f; // resolved length, including the shoulder offset
        float currentEyeHeight_ = 1.65f;
        glm::vec2 cameraRecoil_{0.0f};
        glm::vec2 cameraRecoilVelocity_{0.0f};
        FPlayerLocomotionState locomotion_{};
        float jumpPhaseElapsed_ = 0.0f;
        glm::vec3 planarVelocity_{0.0f};   // commanded, integrated with acceleration
        float verticalVelocity_ = 0.0f;
        glm::vec3 smoothedVelocity_{0.0f}; // measured from position deltas

        glm::vec2 lookDeltaAccum_{0.0f};
        glm::vec2 lookDelta_{0.0f};
        float stridePhase_ = 0.0f;
        float sprintAlpha_ = 0.0f;
        float landingSpeed_ = 0.0f;
        float bobWeight_ = 0.0f;
        float strafeRoll_ = 0.0f;
        float eyeDip_ = 0.0f;
        float eyeDipVelocity_ = 0.0f;
        glm::vec3 headBob_{0.0f};          // first-person eye offset (world)
        float viewRoll_ = 0.0f;            // radians, first person only
        EPlayerTraversalAction traversalAction_ = EPlayerTraversalAction::None;
        float traversalElapsed_ = 0.0f;
        float traversalDuration_ = 0.0f;
        float traversalTopY_ = 0.0f;
        glm::vec3 traversalStart_{0.0f};
        glm::vec3 traversalEnd_{0.0f};
        std::string traversalProbeResult_{"not_tested"};
    };
}
