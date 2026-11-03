#include "PlayerController.hpp"

#include <algorithm>
#include <cmath>
#include <optional>

#include <glm/gtc/matrix_transform.hpp>

#include "Engine/Assets/GPU/UniformBuffer.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Subsystems/NextPhysics.hpp"

namespace NextDayz
{
    namespace
    {
        std::optional<Assets::RayCastResult> RaycastWithin(NextEngine& engine,
                                                           const glm::vec3& origin,
                                                           const glm::vec3& direction,
                                                           float maxDistance)
        {
            std::optional<Assets::RayCastResult> hit;
            engine.RayCast(origin, glm::normalize(direction), [&](Assets::RayCastResult result)
            {
                if (result.Hit && result.T >= 0.0f && result.T <= maxDistance)
                {
                    hit = result;
                }
                return true;
            });
            return hit;
        }

        float Smooth01(float value)
        {
            value = glm::clamp(value, 0.0f, 1.0f);
            return value * value * (3.0f - 2.0f * value);
        }
    }

    void PlayerController::Create(NextPhysics* physics, const glm::vec3& spawnPosition, const FConfig& config)
    {
        config_ = config;
        currentFov_ = config_.Camera.BaseFov;
        aimFov_ = config_.Camera.BaseFov;
        camDistance_ = config_.Camera.TpsDistance;
        currentEyeHeight_ = config_.Player.StandingEyeHeight;
        cameraRecoil_ = glm::vec2(0.0f);
        cameraRecoilVelocity_ = glm::vec2(0.0f);
        locomotion_ = {};
        jumpPhaseElapsed_ = 0.0f;
        planarVelocity_ = glm::vec3(0.0f);
        verticalVelocity_ = 0.0f;
        smoothedVelocity_ = glm::vec3(0.0f);
        lookDeltaAccum_ = glm::vec2(0.0f);
        lookDelta_ = glm::vec2(0.0f);
        stridePhase_ = 0.0f;
        sprintAlpha_ = 0.0f;
        landingSpeed_ = 0.0f;
        bobWeight_ = 0.0f;
        strafeRoll_ = 0.0f;
        eyeDip_ = 0.0f;
        eyeDipVelocity_ = 0.0f;
        traversalAction_ = EPlayerTraversalAction::None;
        traversalElapsed_ = 0.0f;
        traversalDuration_ = 0.0f;
        traversalTopY_ = 0.0f;
        traversalProbeResult_ = "not_tested";
        walkModifier_ = false;
        sprintModifier_ = false;
        sprintBlocked_ = false;
        movementLocked_ = false;
        jumpQueued_ = false;
        crouchToggleQueued_ = false;
        std::fill(std::begin(keyMove_), std::end(keyMove_), false);

        FCharacterControllerSettings settings;
        settings.height = config_.Player.StandingHeight;
        settings.radius = config_.Player.ControllerRadius;
        settings.mass = config_.Player.ControllerMass;
        settings.maxStrength = config_.Player.ControllerStrength;
        settings.maxStepHeight = config_.Player.MaxStepHeight;
        settings.maxSlopeAngle = config_.Player.MaxSlopeAngle;
        settings.initialPosition = spawnPosition;
        controller_.Create(physics, settings);
    }

    void PlayerController::Destroy()
    {
        controller_.Destroy();
    }

    void PlayerController::OnLook(float dxPixels, float dyPixels)
    {
        const float previousPitch = pitch_;
        yaw_ -= dxPixels * config_.Camera.MouseSensitivity;
        pitch_ += dyPixels * config_.Camera.MouseSensitivity;
        const float maxPitch = glm::radians(config_.Camera.MaxPitchDegrees);
        pitch_ = glm::clamp(pitch_, -maxPitch, maxPitch);
        lookDeltaAccum_ += glm::vec2(-dxPixels * config_.Camera.MouseSensitivity, pitch_ - previousPitch);
    }

    void PlayerController::SetMoveKey(int dir, bool pressed)
    {
        if (dir >= 0 && dir < 4)
        {
            keyMove_[dir] = pressed;
        }
    }

    void PlayerController::ToggleView()
    {
        firstPerson_ = !firstPerson_;
    }

    void PlayerController::AdjustCameraDistance(float scrollY)
    {
        camDistance_ = glm::clamp(camDistance_ - scrollY * config_.Camera.ScrollStep,
                                  config_.Camera.TpsMinDistance, config_.Camera.TpsMaxDistance);
    }

    glm::vec3 PlayerController::MoveForward() const
    {
        return glm::vec3(std::sin(yaw_), 0.0f, std::cos(yaw_));
    }

    glm::vec3 PlayerController::Right() const
    {
        return glm::vec3(-std::cos(yaw_), 0.0f, std::sin(yaw_));
    }

    glm::vec3 PlayerController::Forward() const
    {
        const float viewYaw = yaw_ + cameraRecoil_.x;
        const float maxPitch = glm::radians(config_.Camera.MaxPitchDegrees);
        const float viewPitch = glm::clamp(pitch_ + cameraRecoil_.y, -maxPitch, maxPitch);
        const float cosPitch = std::cos(viewPitch);
        return glm::normalize(
            glm::vec3(std::sin(viewYaw) * cosPitch, -std::sin(viewPitch), std::cos(viewYaw) * cosPitch));
    }

    glm::vec3 PlayerController::Up() const
    {
        return glm::normalize(glm::cross(Right(), Forward()));
    }

    glm::vec3 PlayerController::EyePosition() const
    {
        return controller_.GetPosition() + glm::vec3(0.0f, currentEyeHeight_, 0.0f);
    }

    glm::vec3 PlayerController::CameraPosition() const
    {
        if (firstPerson_)
        {
            return EyePosition() + headBob_ + glm::vec3(0.0f, eyeDip_, 0.0f);
        }

        const glm::vec3 forward = Forward();
        constexpr glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
        const glm::vec3 cameraRight = glm::normalize(glm::cross(forward, worldUp));
        const glm::vec3 characterFocus =
            controller_.GetPosition() + glm::vec3(0.0f, currentEyeHeight_ * 0.88f + eyeDip_ * 0.5f, 0.0f);
        return characterFocus - forward * camDistance_ +
               cameraRight * config_.Camera.TpsShoulderOffset;
    }

    glm::vec2 PlayerController::LocalVelocity() const
    {
        const glm::vec3& v = locomotion_.worldVelocity;
        return glm::vec2(glm::dot(v, Right()), glm::dot(v, MoveForward()));
    }

    float PlayerController::CrouchAlpha() const
    {
        const float range = config_.Player.StandingEyeHeight - config_.Player.CrouchedEyeHeight;
        return range > 0.0f
                   ? glm::clamp((config_.Player.StandingEyeHeight - currentEyeHeight_) / range, 0.0f, 1.0f)
                   : 0.0f;
    }

    float PlayerController::StrideLength(float speed) const
    {
        const FCameraConfig& camera = config_.Camera;
        if (locomotion_.actualStance == EPlayerStance::Crouched)
        {
            return camera.StrideCrouch;
        }
        const FAnimationConfig& anim = config_.Animation;
        if (speed <= anim.StandRunAuthoredSpeed)
        {
            const float t = glm::clamp((speed - anim.StandWalkAuthoredSpeed) /
                                           (anim.StandRunAuthoredSpeed - anim.StandWalkAuthoredSpeed),
                                       0.0f, 1.0f);
            return glm::mix(camera.StrideWalk, camera.StrideRun, t);
        }
        const float t = glm::clamp((speed - anim.StandRunAuthoredSpeed) /
                                       (anim.StandSprintAuthoredSpeed - anim.StandRunAuthoredSpeed),
                                   0.0f, 1.0f);
        return glm::mix(camera.StrideRun, camera.StrideSprint, t);
    }

    void PlayerController::ApplyCameraRecoil(const glm::vec2& impulseRadians)
    {
        cameraRecoilVelocity_ += impulseRadians;
    }

    bool PlayerController::RecoilActive() const
    {
        return glm::dot(cameraRecoil_, cameraRecoil_) > 0.000001f ||
               glm::dot(cameraRecoilVelocity_, cameraRecoilVelocity_) > 0.0001f;
    }

    void PlayerController::QueueContextualJump(NextEngine& engine)
    {
        if (IsTraversing())
        {
            return;
        }
        if (!TryBeginTraversal(engine))
        {
            jumpQueued_ = true;
        }
    }

    bool PlayerController::TryBeginTraversal(NextEngine& engine)
    {
        traversalProbeResult_ = "checking";
        if (!locomotion_.onGround || movementLocked_ ||
            locomotion_.actualStance != EPlayerStance::Standing)
        {
            traversalProbeResult_ = !locomotion_.onGround
                                        ? "rejected:not_grounded"
                                    : movementLocked_
                                        ? "rejected:movement_locked"
                                        : "rejected:not_standing";
            return false;
        }

        const FTraversalConfig& traversal = config_.Traversal;
        const glm::vec3 feet = controller_.GetPosition();
        const glm::vec3 forward = MoveForward();
        const glm::vec3 right = Right();
        const auto frontHit = RaycastWithin(
            engine, feet + glm::vec3(0.0f, traversal.ProbeHeight, 0.0f),
            forward, traversal.ProbeDistance);
        if (!frontHit)
        {
            traversalProbeResult_ = "rejected:no_front_obstacle";
            return false;
        }

        const glm::vec3 frontNormal = glm::vec3(frontHit->Normal);
        if (frontNormal.y > 0.45f)
        {
            traversalProbeResult_ = "rejected:front_is_walkable";
            return false;
        }

        const float frontDistance = frontHit->T;
        const float downwardOriginY = feet.y + traversal.ClimbMaxHeight + 0.45f;
        const float downwardDistance = traversal.ClimbMaxHeight + 0.65f;
        const auto surfaceAt = [&](float distance, float lateral,
                                   std::optional<uint32_t> expectedInstance = std::nullopt)
            -> std::optional<Assets::RayCastResult>
        {
            const glm::vec3 origin =
                feet + forward * distance + right * lateral;
            auto hit = RaycastWithin(
                engine, glm::vec3(origin.x, downwardOriginY, origin.z),
                glm::vec3(0.0f, -1.0f, 0.0f), downwardDistance);
            if (!hit || glm::vec3(hit->Normal).y < traversal.SurfaceNormalMinY ||
                (expectedInstance && hit->InstanceId != *expectedInstance))
            {
                return std::nullopt;
            }
            return hit;
        };

        const auto topHit = surfaceAt(
            frontDistance + traversal.TopProbeInset, 0.0f, frontHit->InstanceId);
        if (!topHit)
        {
            traversalProbeResult_ = "rejected:no_matching_top_surface";
            return false;
        }

        const float obstacleTopY = static_cast<float>(topHit->HitPoint.y);
        const float obstacleHeight = obstacleTopY - feet.y;
        if (obstacleHeight < traversal.MinObstacleHeight ||
            obstacleHeight > traversal.ClimbMaxHeight)
        {
            traversalProbeResult_ = "rejected:height_out_of_range";
            return false;
        }

        const auto hasStandingClearance = [&](const glm::vec3& targetFeet)
        {
            const float clearanceDistance =
                config_.Player.StandingHeight + traversal.StandingClearance;
            for (float lateral : {0.0f, -config_.Player.ControllerRadius * 0.65f,
                                 config_.Player.ControllerRadius * 0.65f})
            {
                const glm::vec3 origin =
                    targetFeet + right * lateral + glm::vec3(0.0f, 0.04f, 0.0f);
                if (RaycastWithin(engine, origin, glm::vec3(0.0f, 1.0f, 0.0f),
                                  clearanceDistance))
                {
                    return false;
                }
            }
            return true;
        };

        EPlayerTraversalAction selectedAction = EPlayerTraversalAction::None;
        glm::vec3 targetFeet(0.0f);

        if (obstacleHeight <= traversal.VaultMaxHeight)
        {
            const float firstBehindDistance =
                frontDistance + config_.Player.ControllerRadius * 2.0f + 0.10f;
            const float lastBehindDistance =
                frontDistance + traversal.MaxVaultDepth +
                config_.Player.ControllerRadius + 0.15f;
            for (float distance = firstBehindDistance;
                 distance <= lastBehindDistance; distance += 0.15f)
            {
                const auto groundHit = surfaceAt(distance, 0.0f);
                if (!groundHit)
                {
                    continue;
                }
                const float groundY = static_cast<float>(groundHit->HitPoint.y);
                if (groundY <= feet.y + config_.Player.MaxStepHeight)
                {
                    targetFeet = glm::vec3(groundHit->HitPoint);
                    if (hasStandingClearance(targetFeet))
                    {
                        selectedAction = EPlayerTraversalAction::Vault;
                        break;
                    }
                }
            }
        }

        if (selectedAction == EPlayerTraversalAction::None)
        {
            const float standDistance =
                frontDistance + config_.Player.ControllerRadius +
                traversal.PlatformStandDepth;
            const auto center = surfaceAt(standDistance, 0.0f, frontHit->InstanceId);
            const auto left = surfaceAt(
                standDistance, -config_.Player.ControllerRadius * 0.65f,
                frontHit->InstanceId);
            const auto rightSurface = surfaceAt(
                standDistance, config_.Player.ControllerRadius * 0.65f,
                frontHit->InstanceId);
            if (center && left && rightSurface)
            {
                const float centerY = static_cast<float>(center->HitPoint.y);
                const float leftY = static_cast<float>(left->HitPoint.y);
                const float rightY = static_cast<float>(rightSurface->HitPoint.y);
                if (std::abs(centerY - obstacleTopY) <= 0.12f &&
                    std::abs(leftY - obstacleTopY) <= 0.12f &&
                    std::abs(rightY - obstacleTopY) <= 0.12f)
                {
                    targetFeet = glm::vec3(center->HitPoint);
                    if (hasStandingClearance(targetFeet))
                    {
                        selectedAction = EPlayerTraversalAction::ClimbUp;
                    }
                }
            }
        }

        if (selectedAction == EPlayerTraversalAction::None)
        {
            traversalProbeResult_ =
                obstacleHeight <= traversal.VaultMaxHeight
                    ? "rejected:no_vault_landing_or_clearance"
                    : "rejected:no_climb_stand_area_or_clearance";
            return false;
        }

        traversalAction_ = selectedAction;
        traversalElapsed_ = 0.0f;
        traversalDuration_ =
            selectedAction == EPlayerTraversalAction::Vault
                ? traversal.VaultDurationSeconds
                : traversal.ClimbDurationSeconds;
        traversalTopY_ = obstacleTopY;
        traversalStart_ = feet;
        traversalEnd_ = targetFeet;
        traversalProbeResult_ =
            selectedAction == EPlayerTraversalAction::Vault
                ? "accepted:vault"
                : "accepted:climb_up";
        jumpQueued_ = false;
        crouchToggleQueued_ = false;
        aiming_ = false;
        jumpPhaseElapsed_ = 0.0f;
        locomotion_.jumpPhase = EPlayerJumpPhase::None;
        locomotion_.jumpPhaseTime01 = 0.0f;
        locomotion_.traversalAction = traversalAction_;
        locomotion_.traversalTime01 = 0.0f;
        locomotion_.traversalHeight = obstacleHeight;
        return true;
    }

    void PlayerController::UpdateTraversal(float deltaSeconds)
    {
        const glm::vec3 previousPosition = controller_.GetPosition();
        traversalElapsed_ += std::max(deltaSeconds, 0.0f);
        const float time01 =
            glm::clamp(traversalElapsed_ / std::max(traversalDuration_, 0.01f),
                       0.0f, 1.0f);
        const float riseEnd =
            traversalAction_ == EPlayerTraversalAction::Vault ? 0.42f : 0.58f;
        const float riseForwardFraction =
            traversalAction_ == EPlayerTraversalAction::Vault ? 0.48f : 0.18f;
        const float apexY =
            std::max({traversalStart_.y, traversalEnd_.y, traversalTopY_}) +
            config_.Traversal.ArcClearance;
        const glm::vec3 riseEndPosition =
            glm::mix(traversalStart_, traversalEnd_, riseForwardFraction);

        glm::vec3 position;
        if (time01 < riseEnd)
        {
            const float phase = Smooth01(time01 / riseEnd);
            position = glm::mix(traversalStart_, riseEndPosition, phase);
            position.y = glm::mix(traversalStart_.y, apexY, phase);
        }
        else
        {
            const float phase = Smooth01((time01 - riseEnd) / (1.0f - riseEnd));
            position = glm::mix(
                glm::vec3(riseEndPosition.x, apexY, riseEndPosition.z),
                traversalEnd_, phase);
        }
        controller_.SetPosition(position);

        locomotion_.localMove = glm::vec2(0.0f);
        locomotion_.gait = EPlayerGait::Idle;
        locomotion_.worldVelocity =
            deltaSeconds > 0.0f ? (position - previousPosition) / deltaSeconds
                                : glm::vec3(0.0f);
        locomotion_.horizontalSpeed =
            glm::length(glm::vec2(locomotion_.worldVelocity.x,
                                  locomotion_.worldVelocity.z));
        locomotion_.onGround = time01 >= 1.0f;
        locomotion_.jumpPhase = EPlayerJumpPhase::None;
        locomotion_.jumpPhaseTime01 = 0.0f;
        locomotion_.traversalAction = traversalAction_;
        locomotion_.traversalTime01 = time01;

        if (time01 >= 1.0f)
        {
            // planarVelocity_ was left untouched through the vault, so a running
            // vault comes out still running.
            traversalAction_ = EPlayerTraversalAction::None;
            traversalElapsed_ = 0.0f;
        }
    }

    void PlayerController::UpdateView(float deltaSeconds)
    {
        const float dt = std::max(0.0f, deltaSeconds);
        const FCameraConfig& camera = config_.Camera;
        const float eyeTarget = locomotion_.actualStance == EPlayerStance::Crouched
                                    ? config_.Player.CrouchedEyeHeight
                                    : config_.Player.StandingEyeHeight;
        const float eyeT = 1.0f - std::exp(-config_.Player.EyeHeightLerpSpeed * dt);
        currentEyeHeight_ = glm::mix(currentEyeHeight_, eyeTarget, eyeT);

        const bool sprinting = locomotion_.gait == EPlayerGait::Sprint;
        const float sprintRate = 1.0f / std::max(sprinting ? config_.Player.SprintRaiseSeconds
                                                            : config_.Player.SprintRecoverSeconds,
                                                 0.01f);
        sprintAlpha_ = glm::clamp(sprintAlpha_ + (sprinting ? 1.0f : -1.0f) * sprintRate * dt, 0.0f, 1.0f);

        const float targetFov = (aiming_ ? aimFov_ : camera.BaseFov) + camera.SprintFovBoost * sprintAlpha_;
        const float t = 1.0f - std::exp(-camera.FovLerpSpeed * dt);
        currentFov_ = glm::mix(currentFov_, targetFov, t);

        // Landing dip: an impulse into a damped spring so the eye sinks and
        // recovers instead of stepping.
        if (landingSpeed_ > 0.0f)
        {
            eyeDipVelocity_ -= landingSpeed_ * camera.LandingDipPerMps;
        }
        eyeDipVelocity_ += (-camera.LandingSpring * eyeDip_ - camera.LandingDamping * eyeDipVelocity_) * dt;
        eyeDip_ = glm::clamp(eyeDip_ + eyeDipVelocity_ * dt, -0.18f, 0.06f);

        // Head bob in step with the stride: two dips and one roll sway per cycle.
        const float speed = locomotion_.horizontalSpeed;
        const float bobTarget = locomotion_.onGround && !IsTraversing()
                                    ? glm::clamp(speed / config_.Animation.StandRunAuthoredSpeed, 0.0f, 1.0f) *
                                          (sprinting ? camera.HeadBobSprintScale : 1.0f) *
                                          (aiming_ ? camera.HeadBobAimScale : 1.0f)
                                    : 0.0f;
        bobWeight_ = glm::mix(bobWeight_, bobTarget, 1.0f - std::exp(-10.0f * dt));
        const float theta = glm::two_pi<float>() * stridePhase_;
        const float dip = 0.5f - 0.5f * std::cos(2.0f * theta);
        headBob_ = glm::vec3(0.0f, -camera.HeadBobHeight * bobWeight_ * dip, 0.0f) +
                   Right() * (camera.HeadBobHeight * 0.35f * bobWeight_ * std::sin(theta));

        // Lean the view slightly into a strafe.
        const float rollTarget =
            glm::clamp(LocalVelocity().x * camera.StrafeRollDegreesPerMps, -camera.StrafeRollMaxDegrees,
                       camera.StrafeRollMaxDegrees) *
            (aiming_ ? 0.4f : 1.0f);
        strafeRoll_ = glm::mix(strafeRoll_, rollTarget, 1.0f - std::exp(-8.0f * dt));
        viewRoll_ = glm::radians(strafeRoll_ + camera.HeadBobRollDegrees * bobWeight_ * std::sin(theta));

        const float recoilDt = std::max(0.0f, deltaSeconds);
        cameraRecoilVelocity_ +=
            (-config_.Weapon.CameraRecoilSpring * cameraRecoil_ -
             config_.Weapon.CameraRecoilDamping * cameraRecoilVelocity_) *
            recoilDt;
        cameraRecoil_ += cameraRecoilVelocity_ * recoilDt;
    }

    glm::vec3 PlayerController::UpdateMotion(float deltaSeconds, const glm::vec3& previousPosition)
    {
        const float dt = std::max(deltaSeconds, 0.0f);
        const glm::vec3 measured =
            dt > 0.0f ? (controller_.GetPosition() - previousPosition) / dt : glm::vec3(0.0f);
        const float smoothing = config_.Player.VelocitySmoothingSeconds;
        smoothedVelocity_ = smoothing > 0.0f
                                ? glm::mix(smoothedVelocity_, measured, 1.0f - std::exp(-dt / smoothing))
                                : measured;
        locomotion_.worldVelocity = smoothedVelocity_;
        locomotion_.horizontalSpeed = glm::length(glm::vec2(smoothedVelocity_.x, smoothedVelocity_.z));

        // The bob phase advances with distance travelled, so cadence follows speed.
        if (locomotion_.onGround)
        {
            stridePhase_ += locomotion_.horizontalSpeed * dt /
                            std::max(StrideLength(locomotion_.horizontalSpeed), 0.1f);
            stridePhase_ -= std::floor(stridePhase_);
        }
        return measured;
    }

    void PlayerController::Update(float deltaSeconds)
    {
        lookDelta_ = lookDeltaAccum_;
        lookDeltaAccum_ = glm::vec2(0.0f);
        landingSpeed_ = 0.0f;
        if (IsTraversing())
        {
            const glm::vec3 previousPosition = controller_.GetPosition();
            UpdateTraversal(deltaSeconds);
            UpdateMotion(deltaSeconds, previousPosition);
            UpdateView(deltaSeconds);
            return;
        }
        locomotion_.traversalAction = EPlayerTraversalAction::None;
        locomotion_.traversalTime01 = 0.0f;
        locomotion_.traversalHeight = 0.0f;

        if (crouchToggleQueued_)
        {
            locomotion_.desiredStance = locomotion_.desiredStance == EPlayerStance::Standing
                                             ? EPlayerStance::Crouched
                                             : EPlayerStance::Standing;
            crouchToggleQueued_ = false;
        }
        if (jumpQueued_ && locomotion_.actualStance == EPlayerStance::Crouched)
        {
            locomotion_.desiredStance = EPlayerStance::Standing;
        }

        locomotion_.standBlocked = false;
        if (locomotion_.desiredStance != locomotion_.actualStance)
        {
            const bool wantsStanding = locomotion_.desiredStance == EPlayerStance::Standing;
            const float targetHeight =
                wantsStanding ? config_.Player.StandingHeight : config_.Player.CrouchedHeight;
            if (controller_.TrySetHeight(targetHeight))
            {
                locomotion_.actualStance = locomotion_.desiredStance;
            }
            else if (wantsStanding)
            {
                locomotion_.standBlocked = true;
            }
        }

        glm::vec2 localMove(0.0f);
        if (keyMove_[0]) localMove.y += 1.0f;
        if (keyMove_[1]) localMove.y -= 1.0f;
        if (keyMove_[3]) localMove.x += 1.0f;
        if (keyMove_[2]) localMove.x -= 1.0f;
        if (glm::length(localMove) > 1.0f)
        {
            localMove = glm::normalize(localMove);
        }
        if (movementLocked_)
        {
            localMove = glm::vec2(0.0f);
        }
        locomotion_.localMove = localMove;

        const bool moving = glm::length(localMove) > 0.001f;
        // CoD rules: sprinting is forward-led, and aiming or holding the
        // trigger takes priority over it.
        const bool wantsSprint = moving && sprintModifier_ && !aiming_ && !sprintBlocked_ &&
                                 locomotion_.actualStance == EPlayerStance::Standing &&
                                 localMove.y >= config_.Player.SprintMinForwardInput;

        if (!moving)
        {
            locomotion_.gait = EPlayerGait::Idle;
        }
        else if (locomotion_.actualStance == EPlayerStance::Crouched || aiming_ || walkModifier_)
        {
            locomotion_.gait = EPlayerGait::Walk;
        }
        else if (wantsSprint)
        {
            locomotion_.gait = EPlayerGait::Sprint;
        }
        else
        {
            locomotion_.gait = EPlayerGait::Run;
        }

        float speed = 0.0f;
        if (moving)
        {
            if (locomotion_.actualStance == EPlayerStance::Crouched)
            {
                speed = config_.Player.CrouchWalkSpeed;
            }
            else
            {
                switch (locomotion_.gait)
                {
                case EPlayerGait::Walk: speed = config_.Player.StandWalkSpeed; break;
                case EPlayerGait::Sprint: speed = config_.Player.StandSprintSpeed; break;
                case EPlayerGait::Run: speed = config_.Player.StandRunSpeed; break;
                case EPlayerGait::Idle:
                default: break;
                }
            }

            const float absX = std::abs(localMove.x);
            const float absY = std::abs(localMove.y);
            const float forwardScale = localMove.y < 0.0f ? config_.Player.BackwardScale : 1.0f;
            const float axisWeight = absX + absY;
            if (axisWeight > 0.0f)
            {
                speed *= (absX * config_.Player.StrafeScale + absY * forwardScale) / axisWeight;
            }
            if (aiming_)
            {
                speed *= config_.Player.AimMoveScale;
            }
        }

        const glm::vec3 fwd = MoveForward();
        const glm::vec3 right = Right();
        glm::vec3 moveDir = fwd * localMove.y + right * localMove.x;
        if (glm::length(moveDir) > 0.001f)
        {
            moveDir = glm::normalize(moveDir);
        }

        const bool wasOnGround = locomotion_.onGround;
        const bool jump = jumpQueued_ && !movementLocked_ && wasOnGround &&
                          locomotion_.actualStance == EPlayerStance::Standing;

        // Integrate velocity toward the input target. Accelerating, braking and
        // turning all take time; in the air only a little steering is allowed
        // and releasing the keys keeps the momentum.
        const float dt = std::max(deltaSeconds, 0.0f);
        const glm::vec3 targetVelocity = moveDir * speed;
        const auto moveTowards = [](const glm::vec3& from, const glm::vec3& to, float maxStep)
        {
            const glm::vec3 delta = to - from;
            const float distance = glm::length(delta);
            return distance <= maxStep || distance < 1.0e-5f ? to : from + delta * (maxStep / distance);
        };
        if (wasOnGround)
        {
            const float currentSpeed = glm::length(planarVelocity_);
            const bool braking = speed < currentSpeed - 0.01f || glm::dot(targetVelocity, planarVelocity_) < 0.0f;
            const float rate = braking ? config_.Player.GroundDeceleration
                               : currentSpeed > config_.Player.StandRunSpeed
                                   ? config_.Player.SprintAcceleration
                                   : config_.Player.GroundAcceleration;
            planarVelocity_ = moveTowards(planarVelocity_, targetVelocity, rate * dt);
        }
        else if (speed > 0.0f)
        {
            planarVelocity_ = moveTowards(planarVelocity_, targetVelocity, config_.Player.AirAcceleration * dt);
        }

        const float fallSpeed = -verticalVelocity_;
        if (jump)
        {
            verticalVelocity_ = config_.Player.JumpSpeed;
        }
        else if (wasOnGround)
        {
            verticalVelocity_ = 0.0f;
        }
        else
        {
            verticalVelocity_ += config_.Player.Gravity * dt;
        }

        const glm::vec3 previousPosition = controller_.GetPosition();
        controller_.UpdateWithVelocity(planarVelocity_ + glm::vec3(0.0f, verticalVelocity_, 0.0f), true, dt);
        jumpQueued_ = false;

        locomotion_.onGround = controller_.IsOnGround();
        if (locomotion_.onGround && !wasOnGround && !jump)
        {
            landingSpeed_ = std::max(fallSpeed, 0.0f);
            verticalVelocity_ = 0.0f;
        }
        const glm::vec3 achieved = UpdateMotion(deltaSeconds, previousPosition);

        // Running into a wall eats the speed: keep only what the body actually
        // achieved, or it bursts off the end of the wall at full speed. Glancing
        // contacts slide on, they keep well over the threshold.
        const float achievedSpeed = glm::length(glm::vec2(achieved.x, achieved.z));
        const float commandedSpeed = glm::length(planarVelocity_);
        if (locomotion_.onGround && commandedSpeed > 0.5f && achievedSpeed < commandedSpeed * 0.35f)
        {
            planarVelocity_ = glm::vec3(achieved.x, 0.0f, achieved.z);
        }

        const float jumpDt = std::max(deltaSeconds, 0.0f);
        const auto beginJumpPhase = [this](EPlayerJumpPhase phase)
        {
            locomotion_.jumpPhase = phase;
            locomotion_.jumpPhaseTime01 = 0.0f;
            jumpPhaseElapsed_ = 0.0f;
        };
        if (jump)
        {
            beginJumpPhase(EPlayerJumpPhase::Up);
        }
        else
        {
            switch (locomotion_.jumpPhase)
            {
            case EPlayerJumpPhase::None:
                if (!locomotion_.onGround)
                {
                    beginJumpPhase(verticalVelocity_ > 0.15f
                                       ? EPlayerJumpPhase::Up
                                       : EPlayerJumpPhase::AirLoop);
                }
                break;
            case EPlayerJumpPhase::Up:
                jumpPhaseElapsed_ += jumpDt;
                locomotion_.jumpPhaseTime01 =
                    glm::clamp(jumpPhaseElapsed_ / std::max(config_.Animation.JumpUpSeconds, 0.01f),
                               0.0f, 1.0f);
                if (locomotion_.onGround && !wasOnGround)
                {
                    beginJumpPhase(EPlayerJumpPhase::Down);
                }
                else if (jumpPhaseElapsed_ >= config_.Animation.JumpUpSeconds ||
                         verticalVelocity_ <= 0.0f)
                {
                    beginJumpPhase(EPlayerJumpPhase::AirLoop);
                }
                break;
            case EPlayerJumpPhase::AirLoop:
                if (locomotion_.onGround)
                {
                    beginJumpPhase(EPlayerJumpPhase::Down);
                }
                break;
            case EPlayerJumpPhase::Down:
                if (!locomotion_.onGround)
                {
                    beginJumpPhase(EPlayerJumpPhase::AirLoop);
                }
                else
                {
                    jumpPhaseElapsed_ += jumpDt;
                    locomotion_.jumpPhaseTime01 =
                        glm::clamp(jumpPhaseElapsed_ / std::max(config_.Animation.JumpDownSeconds, 0.01f),
                                   0.0f, 1.0f);
                    if (jumpPhaseElapsed_ >= config_.Animation.JumpDownSeconds)
                    {
                        beginJumpPhase(EPlayerJumpPhase::None);
                    }
                }
                break;
            }
        }

        UpdateView(deltaSeconds);
    }

    void PlayerController::FillCamera(glm::mat4& outModelView, float& outFov) const
    {
        const glm::vec3 forward = Forward();
        constexpr glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
        const glm::vec3 eye = CameraPosition();
        // First-person strafe lean and step sway roll the view about its axis.
        const glm::vec3 up = firstPerson_ ? glm::vec3(glm::rotate(glm::mat4(1.0f), viewRoll_, forward) *
                                                      glm::vec4(worldUp, 0.0f))
                                          : worldUp;
        outModelView = glm::lookAt(eye, eye + forward, up);
        outFov = currentFov_;
    }
}
