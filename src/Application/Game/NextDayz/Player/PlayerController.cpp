#include "Engine/Common/CoreMinimal.hpp"
#include "PlayerController.hpp"

#include <algorithm>
#include <cmath>
#include <chrono>
#include <optional>

#include <glm/gtc/matrix_transform.hpp>

#include "Engine/Assets/GPU/UniformBuffer.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Subsystems/NextPhysics.hpp"

namespace NextDayz
{
    namespace
    {
        double AimInputTime()
        {
            return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        }

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

        glm::vec3 RightOf(const glm::vec3& dir)
        {
            return glm::vec3(-dir.z, 0.0f, dir.x);
        }

        // One downward column of the ledge scan. Low = nothing at or above
        // minTopY; Steep = a face too steep for a hand or a boot; Top = usable.
        enum class EColumn
        {
            Low,
            Steep,
            Top,
        };

        struct FColumn
        {
            EColumn kind = EColumn::Low;
            bool hit = false;
            float y = 0.0f;
            float normalY = 1.0f;
        };

        FColumn ProbeColumn(NextEngine& engine, const glm::vec3& at, float originY, float depth, float minTopY,
                            float minNormalY)
        {
            FColumn column;
            const auto hit = RaycastWithin(engine, glm::vec3(at.x, originY, at.z), glm::vec3(0.0f, -1.0f, 0.0f), depth);
            if (!hit)
            {
                return column;
            }
            column.hit = true;
            column.y = static_cast<float>(hit->HitPoint.y);
            column.normalY = glm::vec3(hit->Normal).y;
            if (column.y >= minTopY)
            {
                column.kind = column.normalY >= minNormalY ? EColumn::Top : EColumn::Steep;
            }
            return column;
        }

        // Hand plant weight over the clip phase: in over [in0, in1], held,
        // out over [out0, out1]. Must match the contacts authored in
        // nextdayz_survivor.scad (traversal section).
        struct FPlantWindow
        {
            float in0 = 0.0f;
            float in1 = 0.0f;
            float out0 = 0.0f;
            float out1 = 0.0f;
        };

        FPlantWindow PlantWindow(EPlayerTraversalAction action, int hand)
        {
            switch (action)
            {
            case EPlayerTraversalAction::Vault:
                return hand == 0 ? FPlantWindow{0.15f, 0.22f, 0.56f, 0.68f} : FPlantWindow{};
            case EPlayerTraversalAction::Mantle:
                return hand == 0 ? FPlantWindow{0.15f, 0.22f, 0.66f, 0.80f} : FPlantWindow{0.17f, 0.25f, 0.60f, 0.74f};
            case EPlayerTraversalAction::Climb:
                return hand == 0 ? FPlantWindow{0.29f, 0.36f, 0.78f, 0.90f} : FPlantWindow{0.30f, 0.37f, 0.74f, 0.86f};
            case EPlayerTraversalAction::ClimbOver:
                return hand == 0 ? FPlantWindow{0.29f, 0.36f, 0.80f, 0.92f} : FPlantWindow{0.30f, 0.37f, 0.62f, 0.72f};
            case EPlayerTraversalAction::None:
            default: return {};
            }
        }

        float PlantWeight(const FPlantWindow& window, float clip)
        {
            if (window.in1 <= 0.0f)
            {
                return 0.0f;
            }
            const float in = Smooth01((clip - window.in0) / std::max(window.in1 - window.in0, 1.0e-3f));
            const float out = 1.0f - Smooth01((clip - window.out0) / std::max(window.out1 - window.out0, 1.0e-3f));
            return std::min(in, out);
        }

        // sin^2 bump over [a, b] of the clip phase.
        float Bump(float clip, float a, float b)
        {
            if (clip <= a || clip >= b)
            {
                return 0.0f;
            }
            const float s = std::sin(glm::pi<float>() * (clip - a) / (b - a));
            return s * s;
        }

        // Monotone cubic (Fritsch-Butland) tangent at an interior key from the
        // slopes either side: no overshoot, so the path never swings into the wall.
        float MonotoneTangent(float d0, float d1, float h0, float h1)
        {
            if (d0 * d1 <= 0.0f)
            {
                return 0.0f;
            }
            const float w0 = 2.0f * h1 + h0;
            const float w1 = h1 + 2.0f * h0;
            return (w0 + w1) / (w0 / d0 + w1 / d1);
        }

        // End tangent: the requested velocity, kept on the segment's side and
        // within the monotone limit.
        float ClampEndTangent(float requested, float slope)
        {
            if (slope == 0.0f || requested * slope <= 0.0f)
            {
                return 0.0f;
            }
            return std::copysign(std::min(std::abs(requested), 3.0f * std::abs(slope)), slope);
        }

        float Hermite(float p0, float p1, float m0, float m1, float h, float u)
        {
            const float u2 = u * u;
            const float u3 = u2 * u;
            return (2.0f * u3 - 3.0f * u2 + 1.0f) * p0 + (u3 - 2.0f * u2 + u) * h * m0 +
                   (-2.0f * u3 + 3.0f * u2) * p1 + (u3 - u2) * h * m1;
        }
    }

    void PlayerController::Create(NextPhysics* physics, const glm::vec3& spawnPosition, const FConfig& config)
    {
        config_ = config;
        CancelAiming();
        currentFov_ = config_.Camera.BaseFov;
        aimFov_ = config_.Camera.BaseFov;
        camDistance_ = config_.Camera.TpsDistance;
        cameraArmLength_ = glm::length(ThirdPersonOffset());
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
        traversal_ = {};
        traversalElapsed_ = 0.0f;
        traversalImpactDone_ = false;
        traversalViewPitch_ = 0.0f;
        traversalViewRoll_ = 0.0f;
        traversalProbeResult_ = "not_tested";
        walkModifier_ = false;
        sprintModifier_ = false;
        sprintBlocked_ = false;
        movementLocked_ = false;
        jumpQueued_ = false;
        jumpHeld_ = false;
        jumpBufferSeconds_ = 0.0f;
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
        CancelAiming();
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
        const bool firstPerson = !IsFirstPerson();
        if (IsAds()) CancelAiming();
        firstPerson_ = firstPerson;
    }

    void PlayerController::SetAimButton(bool pressed, bool allowAds)
    {
        const double now = AimInputTime();
        if (pressed) aimInput_.Press(now, allowAds);
        else aimInput_.Release(now, config_.Camera.AimTapSeconds);
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

    glm::vec3 PlayerController::CameraForward() const
    {
        if (!IsFirstPerson() || (traversalViewPitch_ == 0.0f))
        {
            return Forward();
        }
        const float viewYaw = yaw_ + cameraRecoil_.x;
        const float maxPitch = glm::radians(config_.Camera.MaxPitchDegrees);
        const float viewPitch = glm::clamp(pitch_ + cameraRecoil_.y + traversalViewPitch_, -maxPitch, maxPitch);
        const float cosPitch = std::cos(viewPitch);
        return glm::normalize(
            glm::vec3(std::sin(viewYaw) * cosPitch, -std::sin(viewPitch), std::cos(viewYaw) * cosPitch));
    }

    glm::vec3 PlayerController::Up() const
    {
        return glm::normalize(glm::cross(Right(), Forward()));
    }

    glm::vec3 PlayerController::CameraUp() const
    {
        const glm::vec3 forward = CameraForward();
        const glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));
        const glm::vec3 up = glm::cross(right, forward);
        return IsFirstPerson() ? glm::vec3(glm::rotate(glm::mat4(1.0f), viewRoll_, forward) * glm::vec4(up, 0.0f)) : up;
    }

    glm::vec3 PlayerController::EyePosition() const
    {
        return controller_.GetPosition() + glm::vec3(0.0f, currentEyeHeight_, 0.0f);
    }

    glm::vec3 PlayerController::CameraPosition() const
    {
        if (IsFirstPerson())
        {
            return EyePosition() + headBob_ + glm::vec3(0.0f, eyeDip_, 0.0f);
        }

        return ThirdPersonFocus() + ThirdPersonOffset() * CameraArmFraction();
    }

    glm::vec3 PlayerController::ThirdPersonFocus() const
    {
        return controller_.GetPosition() +
               glm::vec3(0.0f, currentEyeHeight_ * 0.88f + eyeDip_ * 0.5f, 0.0f);
    }

    glm::vec3 PlayerController::ThirdPersonOffset() const
    {
        const glm::vec3 forward = Forward();
        const glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));
        return -forward * camDistance_ + right * config_.Camera.TpsShoulderOffset;
    }

    float PlayerController::CameraArmLength() const
    {
        return IsFirstPerson() ? 0.0f : std::min(cameraArmLength_, glm::length(ThirdPersonOffset()));
    }

    float PlayerController::CameraArmFraction() const
    {
        const float desiredLength = glm::length(ThirdPersonOffset());
        return desiredLength > 0.0f ? CameraArmLength() / desiredLength : 0.0f;
    }

    void PlayerController::UpdateCameraArm(float deltaSeconds, NextEngine& engine)
    {
        const glm::vec3 offset = ThirdPersonOffset();
        const float desiredLength = glm::length(offset);
        if (IsFirstPerson() || desiredLength <= 0.0f)
        {
            cameraArmLength_ = desiredLength;
            return;
        }

        const FCameraConfig& camera = config_.Camera;
        const glm::vec3 direction = offset / desiredLength;
        const glm::vec3 right = glm::normalize(glm::cross(direction, glm::vec3(0.0f, 1.0f, 0.0f)));
        const glm::vec3 up = glm::normalize(glm::cross(right, direction));
        const glm::vec3 focus = ThirdPersonFocus();
        const float clearance = camera.TpsCollisionRadius + camera.TpsCollisionPadding;
        float clearLength = desiredLength;
        // Probe the centre and near-plane edges, including corners. A centre ray
        // alone lets the lens clip a wall while orbiting beside it. Include the
        // shoulder in the arm, so pulling in also brings it out of side walls.
        for (float x : {-1.0f, 0.0f, 1.0f})
        {
            for (float y : {-1.0f, 0.0f, 1.0f})
            {
                const glm::vec3 origin = focus + (right * x + up * y) * camera.TpsCollisionRadius;
                const auto hit = RaycastWithin(engine, origin, direction, desiredLength + clearance);
                if (hit)
                {
                    clearLength = std::min(clearLength, std::max(0.0f, hit->T - clearance));
                }
            }
        }

        // Collision always wins over the user's minimum zoom distance. Never
        // smooth inward: even one interpolated frame could put the lens in a wall.
        const float returnStep = camera.TpsArmReturnSpeed * std::max(0.0f, deltaSeconds);
        cameraArmLength_ = std::min(clearLength, cameraArmLength_ + returnStep);
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
        // The press opens the airborne catch window whatever happens below:
        // pressed a step too far out, the jump still grabs the lip on the way up.
        jumpBufferSeconds_ = config_.Traversal.AirCatchWindowSeconds;
        if (!locomotion_.onGround)
        {
            return;
        }
        if (!TryBeginTraversal(engine, false))
        {
            jumpQueued_ = true;
        }
    }

    bool PlayerController::WantsAirCatch() const
    {
        // A held jump (or a fresh press) plus forward intent. Walking off a
        // roof or strafing past a wall never grabs anything.
        return !locomotion_.onGround && !movementLocked_ &&
               locomotion_.actualStance == EPlayerStance::Standing &&
               (jumpHeld_ || jumpBufferSeconds_ > 0.0f) && locomotion_.localMove.y > 0.2f;
    }

    // Ledge search. Every number is relative to the current feet:
    //   1. horizontal rays from the knees to overhead find the face and square
    //      the move up to its normal;
    //   2. downward columns from in front of the face (eaves overhang it) to
    //      just past it find the first usable top; the lip is refined between
    //      the last low column and that top;
    //   3. behind the lip: a far side within MaxWallThickness plus a landing
    //      makes it thin (vault / over), a flat area with headroom makes it a
    //      place to stand (mantle / climb, crouched if only that fits).
    // The CPU raycast reports back faces, so every downward column starts
    // from a point kept in free space: under the ceiling above the head, and
    // cut off where a horizontal ray at that height enters something taller.
    bool PlayerController::TryBeginTraversal(NextEngine& engine, bool airborne)
    {
        const std::string prefix = airborne ? "air:" : "";
        const auto reject = [&](const char* reason)
        {
            traversalProbeResult_ = prefix + "rejected:" + reason;
            return false;
        };
        if (movementLocked_)
        {
            return reject("movement_locked");
        }
        if (locomotion_.actualStance != EPlayerStance::Standing)
        {
            return reject("not_standing");
        }
        if (!airborne && !locomotion_.onGround)
        {
            return reject("not_grounded");
        }

        const FTraversalConfig& tc = config_.Traversal;
        const float radius = config_.Player.ControllerRadius;
        const glm::vec3 up(0.0f, 1.0f, 0.0f);
        const glm::vec3 feet = controller_.GetPosition();
        const glm::vec3 planar(planarVelocity_.x, 0.0f, planarVelocity_.z);
        const float speed = glm::length(planar);

        // Heading: where the camera faces, or where the body really moves when
        // that is close to it.
        glm::vec3 heading = MoveForward();
        if (speed > 1.0f && glm::dot(planar / speed, heading) > 0.5f)
        {
            heading = planar / speed;
        }

        const float minTop = airborne ? tc.AirCatchMinHeight : tc.MinObstacleHeight;
        const float maxTop = airborne ? tc.AirCatchMaxHeight : tc.ClimbMaxHeight;

        // --- 1. the face -----------------------------------------------------
        const float reach = std::min(tc.ProbeDistance + speed * tc.ProbeSpeedLookahead, tc.ProbeMaxDistance);
        std::optional<Assets::RayCastResult> face;
        for (float h = 0.30f; h <= maxTop - 0.04f; h += 0.28f)
        {
            const auto hit = RaycastWithin(engine, feet + up * h, heading, reach);
            if (!hit || std::abs(glm::vec3(hit->Normal).y) > 0.70f)
            {
                continue; // nothing, or a ramp / underside
            }
            if (!face || hit->T < face->T)
            {
                face = hit;
            }
        }
        if (!face)
        {
            return reject("no_front_obstacle");
        }
        glm::vec3 normal(face->Normal.x, 0.0f, face->Normal.z);
        if (glm::length(normal) < 1.0e-3f)
        {
            return reject("no_front_obstacle");
        }
        normal = glm::normalize(normal);
        const float facing = glm::dot(heading, -normal);
        if (facing < std::cos(glm::radians(tc.MaxApproachDegrees)))
        {
            return reject("approach_angle");
        }
        const glm::vec3 dir = -normal;
        const glm::vec3 right = RightOf(dir);
        const float faceDist = face->T * facing;
        // In the air the body carries itself to the wall; grab once it is there
        // rather than lunging across the gap.
        if (airborne && faceDist > tc.AirCatchReach)
        {
            return reject("face_out_of_reach");
        }

        // --- 2. the lip ------------------------------------------------------
        float originY = feet.y + maxTop + 0.20f;
        bool overhead = false; // an eave, porch roof or balcony above the head
        if (const auto ceiling = RaycastWithin(engine, feet + up * 1.0f, up, originY - feet.y - 0.95f))
        {
            originY = std::min(originY, static_cast<float>(ceiling->HitPoint.y) - 0.05f);
            overhead = true;
        }
        if (originY < feet.y + minTop + 0.15f && !overhead)
        {
            return reject("low_ceiling");
        }
        const float scanEndNominal = faceDist + 0.45f;
        float coveredFrom = scanEndNominal + 1.0f;
        if (const auto cover = RaycastWithin(engine, glm::vec3(feet.x, originY, feet.z), dir, coveredFrom))
        {
            coveredFrom = cover->T - 0.02f;
        }
        const float columnDepth = originY - feet.y + 0.6f;
        const auto column = [&](float s)
        {
            return ProbeColumn(engine, feet + dir * s, originY, columnDepth, feet.y + minTop, tc.SurfaceNormalMinY);
        };

        float edgeS = 0.0f;
        float topY = 0.0f;
        float lowY = feet.y;
        bool found = false;
        const char* missing = "no_ledge";
        if (originY >= feet.y + minTop + 0.15f)
        {
            const float scanStart = std::max(-0.25f, faceDist - tc.EaveMaxOverhang);
            const float scanEnd = std::min(scanEndNominal, coveredFrom);
            float lowS = scanStart - tc.ScanStep;
            bool haveLow = false;
            float topS = 0.0f;
            FColumn top;
            for (float s = scanStart; s <= scanEnd; s += tc.ScanStep)
            {
                const FColumn sample = column(s);
                if (sample.kind == EColumn::Top)
                {
                    topS = s;
                    top = sample;
                    found = true;
                    break;
                }
                if (sample.kind == EColumn::Low)
                {
                    lowS = s;
                    lowY = sample.hit ? sample.y : feet.y - 1.0f;
                    haveLow = true;
                }
                // Steep: an eave strip, a pitched fascia or a car's windscreen.
                // Keep looking further in for the surface behind it.
            }
            if (found && !haveLow)
            {
                found = false;
                missing = "lip_overhead";
            }
            else if (found)
            {
                float lo = lowS;
                float hi = topS;
                for (int i = 0; i < 4; ++i)
                {
                    const float mid = 0.5f * (lo + hi);
                    (column(mid).kind == EColumn::Top ? hi : lo) = mid;
                }
                const FColumn lip = column(hi + 0.03f);
                topY = lip.kind == EColumn::Top ? lip.y : top.y;
                edgeS = hi;
            }
            else if (coveredFrom < scanEndNominal)
            {
                missing = "too_high";
            }
        }
        if (!found && overhead)
        {
            // Standing under an eave: the lip is over or behind the head, out
            // of sight of the columns (they start under it). Walk back under
            // the overhang until the sky opens; the lip is there.
            const float underReach = maxTop - 1.2f + 0.05f;
            const auto underside = [&](float s) -> std::optional<float>
            {
                const auto hit = RaycastWithin(engine, feet + dir * s + up * 1.2f, up, underReach);
                return hit ? std::optional<float>(static_cast<float>(hit->HitPoint.y)) : std::nullopt;
            };
            float covered = 0.0f;
            float open = 1.0f;
            for (float s = 0.0f; s >= -tc.EaveMaxOverhang; s -= tc.ScanStep)
            {
                if (!underside(s))
                {
                    open = s;
                    break;
                }
                covered = s;
            }
            if (open <= 0.0f)
            {
                for (int i = 0; i < 4; ++i)
                {
                    const float mid = 0.5f * (open + covered);
                    (underside(mid) ? covered : open) = mid;
                }
                const auto soffit = underside(covered + 0.02f);
                const glm::vec3 at = feet + dir * (covered + 0.04f);
                const auto hit = soffit ? RaycastWithin(engine, glm::vec3(at.x, *soffit + 0.80f, at.z), -up, 1.0f)
                                        : std::nullopt;
                if (hit && glm::vec3(hit->Normal).y >= tc.SurfaceNormalMinY &&
                    static_cast<float>(hit->HitPoint.y) - *soffit < 0.45f)
                {
                    topY = static_cast<float>(hit->HitPoint.y);
                    edgeS = covered;
                    lowY = feet.y;
                    found = true;
                }
            }
        }
        if (!found)
        {
            return reject(missing);
        }
        if (topY - lowY < tc.MinObstacleHeight * 0.6f)
        {
            return reject("no_lip"); // a rising slope, not a step
        }
        const glm::vec3 edge = glm::vec3(feet.x, topY, feet.z) + dir * edgeS;
        const float height = topY - feet.y;
        if (height < minTop || height > maxTop)
        {
            return reject("height_out_of_range");
        }
        // The body folds over the lip: it needs that much room above the
        // surface just past it (a pitched roof rises from the lip).
        {
            const glm::vec3 crest = edge + dir * 0.10f;
            const auto surface = RaycastWithin(engine, crest + up * 1.0f, -up, 1.3f);
            const float surfaceY = surface ? static_cast<float>(surface->HitPoint.y) : topY;
            if (surfaceY > topY + 0.35f ||
                RaycastWithin(engine, glm::vec3(crest.x, surfaceY + 0.04f, crest.z), up, 0.95f))
            {
                return reject("no_crest_headroom");
            }
        }

        // Free space above a foot position: five rays across the capsule, each
        // from its own surface, so a pitched roof does not start them inside it.
        const auto hasHeadroom = [&](const glm::vec3& at, float headroom)
        {
            const float side = radius * 0.6f;
            for (const glm::vec3& offset : {glm::vec3(0.0f), right * side, -right * side, dir * side, -dir * side})
            {
                const glm::vec3 p = at + offset;
                float baseY = at.y;
                if (const auto ground = RaycastWithin(engine, p + up * 0.45f, -up, 0.90f))
                {
                    baseY = static_cast<float>(ground->HitPoint.y);
                }
                if (RaycastWithin(engine, glm::vec3(p.x, baseY + 0.04f, p.z), up, headroom - (baseY - at.y)))
                {
                    return false;
                }
            }
            return true;
        };

        // --- 3. behind the lip -------------------------------------------------
        // Probes behind the lip start from above the real top, not from the
        // scan origin (which sits under any eave the player stands beneath).
        const float probeY = topY + 0.90f;
        float farS = 0.0f;
        bool haveFar = false; // the top ends within MaxWallThickness (a wall, a fence, a car body)
        {
            const auto onTop = [&](float s)
            {
                return ProbeColumn(engine, feet + dir * s, probeY, probeY - (topY - 0.30f), topY - 0.30f, 0.0f).kind !=
                       EColumn::Low;
            };
            float previous = edgeS + 0.04f;
            for (float s = edgeS + 0.12f; s <= edgeS + tc.MaxWallThickness + 0.12f; s += 0.12f)
            {
                if (!onTop(s))
                {
                    float a = previous;
                    float b = s;
                    for (int i = 0; i < 3; ++i)
                    {
                        const float mid = 0.5f * (a + b);
                        (onTop(mid) ? a : b) = mid;
                    }
                    farS = b;
                    haveFar = true;
                    break;
                }
                previous = s;
            }
        }

        bool landingFound = false;
        glm::vec3 landing(0.0f);
        if (haveFar)
        {
            for (int k = 0; k < 5 && !landingFound; ++k)
            {
                const float s = farS + radius + 0.08f + 0.15f * static_cast<float>(k);
                const glm::vec3 at = feet + dir * s;
                // The way down the far side must be open (no building right behind the wall).
                if (RaycastWithin(engine, glm::vec3(feet.x, topY + 0.25f, feet.z) + dir * (farS - 0.05f), dir,
                                  s - farS + 0.05f + radius))
                {
                    break;
                }
                const auto hit = RaycastWithin(engine, glm::vec3(at.x, topY + 0.50f, at.z), -up, 0.50f + tc.MaxDropBehind);
                if (!hit || glm::vec3(hit->Normal).y < tc.StandNormalMinY)
                {
                    continue;
                }
                const glm::vec3 ground(at.x, hit->HitPoint.y, at.z);
                if (ground.y > topY - 0.25f || !hasHeadroom(ground, tc.LandingHeadroom))
                {
                    continue;
                }
                landing = ground;
                landingFound = true;
            }
        }

        bool standFound = false;
        bool standCrouched = false;
        glm::vec3 stand(0.0f);
        {
            const glm::vec3 at = feet + dir * (edgeS + radius + 0.14f);
            std::array<float, 3> heights{};
            bool ok = true;
            const std::array<float, 3> lateral = {0.0f, -radius * 0.65f, radius * 0.65f};
            for (size_t i = 0; i < lateral.size() && ok; ++i)
            {
                const glm::vec3 p = at + right * lateral[i];
                const auto hit = RaycastWithin(engine, glm::vec3(p.x, probeY, p.z), -up, probeY - (topY - 0.40f));
                ok = hit && glm::vec3(hit->Normal).y >= tc.StandNormalMinY;
                heights[i] = ok ? static_cast<float>(hit->HitPoint.y) : 0.0f;
            }
            if (ok && std::abs(heights[1] - heights[0]) <= tc.StandFlatness &&
                std::abs(heights[2] - heights[0]) <= tc.StandFlatness && heights[0] >= topY - 0.15f &&
                heights[0] <= topY + 0.50f)
            {
                stand = glm::vec3(at.x, heights[0], at.z);
                if (hasHeadroom(stand, config_.Player.StandingHeight + tc.StandingClearance))
                {
                    standFound = true;
                }
                else if (hasHeadroom(stand, config_.Player.CrouchedHeight + tc.StandingClearance))
                {
                    standFound = true;
                    standCrouched = true;
                }
            }
        }

        // --- choose ------------------------------------------------------------
        const bool thin = haveFar && landingFound;
        EPlayerTraversalAction action = EPlayerTraversalAction::None;
        if (airborne)
        {
            const bool low = height <= tc.AirMantleMaxHeight;
            if (thin)
            {
                action = low ? EPlayerTraversalAction::Vault : EPlayerTraversalAction::ClimbOver;
            }
            else if (standFound)
            {
                action = low ? EPlayerTraversalAction::Mantle : EPlayerTraversalAction::Climb;
            }
        }
        else if (thin && height <= tc.VaultMaxHeight)
        {
            action = EPlayerTraversalAction::Vault;
        }
        else if (standFound)
        {
            action = height <= tc.MantleMaxHeight ? EPlayerTraversalAction::Mantle : EPlayerTraversalAction::Climb;
        }
        else if (thin)
        {
            action = EPlayerTraversalAction::ClimbOver;
        }
        if (action == EPlayerTraversalAction::None)
        {
            return reject(haveFar ? "no_landing" : "no_stand_area");
        }

        FTraversalPlan plan;
        plan.action = action;
        plan.airborne = airborne;
        plan.dir = dir;
        plan.edge = edge;
        plan.topY = topY;
        plan.height = height;
        plan.thickness = haveFar ? farS - edgeS : 0.0f;
        plan.landing = landing;
        plan.stand = stand;
        plan.endCrouched = standCrouched &&
                           (action == EPlayerTraversalAction::Mantle || action == EPlayerTraversalAction::Climb);

        // Hand plants on the real top, each hand under its own column. The
        // support palm lies flat (wrist just above the surface); the firing
        // fist sits a little higher with the weapon laid across the top.
        const auto plantAt = [&](float lateral, float inset, float lift)
        {
            const glm::vec3 p = edge + dir * inset + right * lateral;
            const auto hit = RaycastWithin(engine, glm::vec3(p.x, topY + 0.40f, p.z), -up, 0.70f);
            const float y = hit && std::abs(static_cast<float>(hit->HitPoint.y) - topY) < 0.25f
                                ? static_cast<float>(hit->HitPoint.y)
                                : topY;
            return glm::vec3(p.x, y + lift, p.z);
        };
        if (action == EPlayerTraversalAction::Vault)
        {
            plan.hands[0] = plantAt(-0.15f, 0.11f, 0.055f);
            plan.hands[1] = plan.hands[0];
        }
        else
        {
            plan.hands[0] = plantAt(-tc.HandSpread, tc.HandInset, 0.055f);
            plan.hands[1] = plantAt(tc.HandSpread, tc.HandInset + 0.03f, 0.09f);
        }

        if (!BuildTraversalPath(engine, plan, feet, planar + up * (airborne ? verticalVelocity_ : 0.0f)))
        {
            return reject("no_path");
        }
        BeginTraversal(plan);
        traversalProbeResult_ = prefix + "accepted:" + TraversalActionName(action);
        return true;
    }

    // Lays the keys out in the lip's frame: u along the move from the lip
    // (negative = the player's side), v above the top. Every contact pose sits
    // at a fixed (u, v) whatever the obstacle height, so the clip's authored
    // hand and foot placements meet the real geometry; only the free segments
    // (approach, jump) stretch with it. Clip phases match the contact markers
    // in nextdayz_survivor.scad.
    bool PlayerController::BuildTraversalPath(NextEngine& engine, FTraversalPlan& plan, const glm::vec3& feet,
                                              const glm::vec3& velocity) const
    {
        const FTraversalConfig& tc = config_.Traversal;
        const glm::vec3 up(0.0f, 1.0f, 0.0f);
        const float height = plan.height;
        const float u0 = glm::dot(feet - plan.edge, plan.dir);
        const glm::vec3 planar(velocity.x, 0.0f, velocity.z);
        const float speed = glm::length(planar);
        const float approachSpeed = std::max(speed, tc.ApproachMinSpeed);
        const float endEye = plan.endCrouched ? config_.Player.CrouchedEyeHeight : config_.Player.StandingEyeHeight;
        const auto at = [&](float u, float v) { return plan.edge + plan.dir * u + up * v; };

        int count = 0;
        float time = 0.0f;
        const auto key = [&](const glm::vec3& position, float clip, float eye, float seconds)
        {
            if (count >= kMaxTraversalKeys)
            {
                return;
            }
            time += count == 0 ? 0.0f : std::max(seconds * tc.MoveTimeScale, 0.02f);
            plan.keys[count++] = {time, position, clip, eye};
        };
        // Close the gap to the take-off point; never step back more than a little.
        const auto takeoff = [&](float nominal) { return glm::clamp(u0, nominal, -0.28f); };
        const auto approach = [&](float toU, float minSeconds)
        {
            return glm::clamp(std::abs(toU - u0) / approachSpeed, minSeconds, tc.ApproachMaxSeconds);
        };
        const auto reachIn = [&](const glm::vec3& to, float minSeconds, float maxSeconds)
        {
            return glm::clamp(glm::distance(feet, to) / 4.5f, minSeconds, maxSeconds);
        };

        glm::vec3 endTangent(0.0f);
        const float tall = glm::clamp((height - 0.8f) / 0.8f, 0.0f, 1.0f);
        switch (plan.action)
        {
        case EPlayerTraversalAction::Vault:
        {
            // Plant, sweep over, touch down, stride out. The run carries through.
            const float pace = glm::clamp(4.2f / std::max(speed, 3.0f), 0.82f, 1.10f);
            const float rise = glm::clamp((height - 0.6f) / 0.5f, 0.0f, 1.0f);
            const glm::vec3 over = at(plan.thickness * 0.5f, 0.06f);
            if (plan.airborne)
            {
                key(feet, 0.28f, currentEyeHeight_, 0.0f);
                key(over, 0.50f, 0.98f, reachIn(over, 0.12f, 0.26f));
            }
            else
            {
                key(feet, 0.0f, currentEyeHeight_, 0.0f);
                const float u = takeoff(-0.58f);
                key(at(u, -height), 0.20f, 1.50f, approach(u, 0.08f));
                key(over, 0.50f, 0.98f, (0.20f + 0.10f * rise) * pace);
            }
            // Drops of more than a storey's knee-bend fall the rest of the way.
            const bool fall = plan.landing.y < plan.topY - 1.25f;
            glm::vec3 touch = plan.landing;
            if (fall)
            {
                touch.y = plan.topY - 1.25f;
            }
            key(touch, 0.78f, 1.35f, (0.20f + 0.06f * glm::clamp(plan.topY - touch.y - 0.8f, 0.0f, 1.0f)) * pace);
            const float exitSpeed = std::max(speed, 2.2f);
            plan.exitVelocity = plan.dir * exitSpeed;
            if (fall)
            {
                plan.endAirborne = true;
                plan.exitVelocity.y = -2.0f;
            }
            else
            {
                plan.impactKey = count - 1;
                plan.impactSpeed = 2.2f;
                // Stride out on the far side, as far as the ground allows.
                float stride = std::min(exitSpeed * 0.14f, 0.55f);
                if (const auto block = RaycastWithin(engine, touch + up * 0.5f, plan.dir, stride + config_.Player.ControllerRadius))
                {
                    stride = std::max(0.0f, block->T - config_.Player.ControllerRadius - 0.05f);
                }
                glm::vec3 recover = touch + plan.dir * stride;
                const auto ground = RaycastWithin(engine, recover + up * 0.6f, -up, 1.0f);
                if (!ground || std::abs(static_cast<float>(ground->HitPoint.y) - touch.y) > 0.25f)
                {
                    recover = touch;
                }
                else
                {
                    recover.y = ground->HitPoint.y;
                }
                key(recover, 1.0f, 1.58f, 0.14f);
            }
            endTangent = plan.exitVelocity;
            break;
        }
        case EPlayerTraversalAction::Mantle:
        {
            // Hands on the top, press to locked arms, knee over, stand up.
            const glm::vec3 press = at(-0.30f, std::max(-height, -0.92f));
            if (plan.airborne)
            {
                key(feet, 0.30f, currentEyeHeight_, 0.0f);
                key(press, 0.48f, 1.50f, reachIn(press, 0.10f, 0.22f));
            }
            else
            {
                key(feet, 0.0f, currentEyeHeight_, 0.0f);
                const float u = takeoff(-0.46f);
                key(at(u, -height), 0.22f, 1.48f, approach(u, 0.10f));
                key(press, 0.48f, 1.50f, 0.15f + 0.10f * tall);
            }
            key(at(0.12f, 0.0f), 0.72f, 1.02f, 0.21f);
            plan.impactKey = count - 1;
            plan.impactSpeed = 1.1f;
            key(plan.stand, 1.0f, endEye, 0.28f);
            break;
        }
        case EPlayerTraversalAction::Climb:
        case EPlayerTraversalAction::ClimbOver:
        {
            const bool over = plan.action == EPlayerTraversalAction::ClimbOver;
            // The body hangs off the hands, and on a pitched roof the plants sit
            // above the lip: lift the hanging keys with them.
            const float handRise =
                std::max(0.0f, 0.5f * ((plan.hands[0].y - 0.055f) + (plan.hands[1].y - 0.09f)) - plan.topY);
            const float hangV = std::max(-1.75f + handRise, -height);
            bool pressed = false;
            if (plan.airborne)
            {
                // Caught on the way up: straight into the hang, absorb the catch.
                key(feet, 0.28f, currentEyeHeight_, 0.0f);
                const glm::vec3 catchAt = at(-0.32f, hangV);
                key(catchAt, 0.36f, 1.52f, reachIn(catchAt, 0.08f, 0.22f));
                key(at(-0.34f, hangV - 0.06f), 0.46f, 1.50f, 0.12f);
            }
            else if (over && height <= tc.MantleMaxHeight)
            {
                // A wall too high to vault but too low to hang from: hop
                // straight into the press with a boot on the face.
                key(feet, 0.0f, currentEyeHeight_, 0.0f);
                const float u = takeoff(-0.50f);
                key(at(u, -height), 0.12f, 1.42f, approach(u, 0.10f));
                key(at(-0.26f, std::max(-height, -0.70f)), 0.60f, 1.40f, 0.24f + 0.08f * tall);
                pressed = true;
            }
            else
            {
                // Load, step off the wall, catch the lip, settle into the hang.
                key(feet, 0.0f, currentEyeHeight_, 0.0f);
                const float u = takeoff(-0.55f);
                key(at(u, -height), 0.12f, 1.40f, approach(u, 0.10f));
                key(at(std::max(-0.42f, u), -height + std::max(0.12f, (hangV + height) * 0.5f)), 0.24f, 1.55f,
                    0.12f);
                key(at(-0.32f, hangV), 0.36f, 1.52f, 0.12f);
                key(at(-0.34f, hangV - 0.04f), 0.44f, 1.50f, 0.09f);
            }

            if (!over)
            {
                const bool deepHang = hangV < -1.20f + handRise;
                if (deepHang)
                {
                    key(at(-0.30f, -1.15f + handRise), 0.62f, 1.46f, 0.24f);
                }
                key(at(-0.22f, -0.72f + handRise), 0.74f, 1.42f, deepHang ? 0.13f : 0.24f);
                key(at(0.12f, 0.0f), 0.86f, 1.02f, 0.17f);
                plan.impactKey = count - 1;
                plan.impactSpeed = 1.1f;
                key(plan.stand, 1.0f, endEye, 0.27f);
                break;
            }

            if (!pressed)
            {
                key(at(-0.24f, -0.70f + handRise), 0.60f, 1.40f, 0.26f);
            }
            // Hips onto the top, legs swing over, slide down the far side.
            key(at(plan.thickness * 0.5f, 0.04f), 0.76f, 0.98f, 0.22f);
            const float landV = plan.landing.y - plan.topY;
            key(at(plan.thickness + 0.22f, std::max(-0.55f, landV)), 0.90f, 1.25f, 0.16f);
            glm::vec3 drop = plan.landing;
            if (landV < -1.10f)
            {
                drop.y = plan.topY - 1.10f;
                plan.endAirborne = true;
            }
            key(drop, 1.0f, 1.40f, 0.12f);
            plan.exitVelocity = plan.dir * 0.8f;
            if (plan.endAirborne)
            {
                plan.exitVelocity.y = -1.5f;
            }
            else
            {
                plan.impactKey = count - 1;
                plan.impactSpeed = 2.0f;
            }
            endTangent = plan.exitVelocity;
            break;
        }
        case EPlayerTraversalAction::None:
        default: return false;
        }

        plan.keyCount = count;
        if (count < 2)
        {
            return false;
        }

        // Monotone cubic tangents per axis (and for the eye): smooth through
        // every key, never overshooting one into the wall. The ends take the
        // entry and exit velocities so the run flows in and out.
        const auto value = [&](int k, int axis)
        {
            return axis < 3 ? plan.keys[k].position[axis] : plan.keys[k].eye;
        };
        for (int i = 0; i < count; ++i)
        {
            glm::vec4 tangent(0.0f);
            for (int axis = 0; axis < 4; ++axis)
            {
                if (i == 0)
                {
                    const float h = plan.keys[1].time - plan.keys[0].time;
                    const float d = (value(1, axis) - value(0, axis)) / h;
                    tangent[axis] = ClampEndTangent(axis < 3 ? velocity[axis] : 0.0f, d);
                }
                else if (i == count - 1)
                {
                    const float h = plan.keys[i].time - plan.keys[i - 1].time;
                    const float d = (value(i, axis) - value(i - 1, axis)) / h;
                    tangent[axis] = ClampEndTangent(axis < 3 ? endTangent[axis] : 0.0f, d);
                }
                else
                {
                    const float h0 = plan.keys[i].time - plan.keys[i - 1].time;
                    const float h1 = plan.keys[i + 1].time - plan.keys[i].time;
                    tangent[axis] = MonotoneTangent((value(i, axis) - value(i - 1, axis)) / h0,
                                                    (value(i + 1, axis) - value(i, axis)) / h1, h0, h1);
                }
            }
            plan.tangents[i] = tangent;
        }
        return true;
    }

    void PlayerController::BeginTraversal(const FTraversalPlan& plan)
    {
        traversal_ = plan;
        traversalAction_ = plan.action;
        traversalElapsed_ = 0.0f;
        traversalImpactDone_ = false;
        jumpQueued_ = false;
        jumpBufferSeconds_ = 0.0f;
        crouchToggleQueued_ = false;
        CancelAiming();
        jumpPhaseElapsed_ = 0.0f;
        locomotion_.jumpPhase = EPlayerJumpPhase::None;
        locomotion_.jumpPhaseTime01 = 0.0f;
        locomotion_.traversalAction = plan.action;
        locomotion_.traversalTime01 = plan.keys[0].clip;
        locomotion_.traversalHeight = plan.height;
        locomotion_.traversalYaw = std::atan2(plan.dir.x, plan.dir.z);
        locomotion_.traversalHand = plan.hands;
        locomotion_.traversalHandWeight = {0.0f, 0.0f};
    }

    void PlayerController::UpdateTraversal(float deltaSeconds)
    {
        const FTraversalPlan& plan = traversal_;
        const glm::vec3 previousPosition = controller_.GetPosition();
        traversalElapsed_ += std::max(deltaSeconds, 0.0f);
        const float total = plan.keys[plan.keyCount - 1].time;
        const float t = std::min(traversalElapsed_, total);
        int segment = 0;
        while (segment < plan.keyCount - 2 && t > plan.keys[segment + 1].time)
        {
            ++segment;
        }
        const FTraversalKey& a = plan.keys[segment];
        const FTraversalKey& b = plan.keys[segment + 1];
        const glm::vec4& ma = plan.tangents[segment];
        const glm::vec4& mb = plan.tangents[segment + 1];
        const float h = std::max(b.time - a.time, 1.0e-4f);
        const float u = glm::clamp((t - a.time) / h, 0.0f, 1.0f);
        const glm::vec3 position(Hermite(a.position.x, b.position.x, ma.x, mb.x, h, u),
                                 Hermite(a.position.y, b.position.y, ma.y, mb.y, h, u),
                                 Hermite(a.position.z, b.position.z, ma.z, mb.z, h, u));
        const float clip = glm::mix(a.clip, b.clip, u);
        controller_.SetPosition(position);
        currentEyeHeight_ = Hermite(a.eye, b.eye, ma.w, mb.w, h, u);

        if (!traversalImpactDone_ && plan.impactKey > 0 && t >= plan.keys[plan.impactKey].time)
        {
            landingSpeed_ = plan.impactSpeed;
            traversalImpactDone_ = true;
        }

        locomotion_.localMove = glm::vec2(0.0f);
        locomotion_.gait = EPlayerGait::Idle;
        locomotion_.worldVelocity =
            deltaSeconds > 0.0f ? (position - previousPosition) / deltaSeconds : glm::vec3(0.0f);
        locomotion_.horizontalSpeed =
            glm::length(glm::vec2(locomotion_.worldVelocity.x, locomotion_.worldVelocity.z));
        locomotion_.onGround = false;
        locomotion_.jumpPhase = EPlayerJumpPhase::None;
        locomotion_.jumpPhaseTime01 = 0.0f;
        locomotion_.traversalAction = plan.action;
        locomotion_.traversalTime01 = clip;
        for (int hand = 0; hand < 2; ++hand)
        {
            locomotion_.traversalHandWeight[hand] = PlantWeight(PlantWindow(plan.action, hand), clip);
        }

        // First-person head motion: eyes on the lip while reaching for it,
        // down over it on the press, a lean onto the planted hand.
        float pitch = 0.0f;
        float roll = 0.0f;
        switch (plan.action)
        {
        case EPlayerTraversalAction::Vault:
            pitch = 8.0f * Bump(clip, 0.08f, 0.62f);
            roll = -6.0f * Bump(clip, 0.20f, 0.82f);
            break;
        case EPlayerTraversalAction::Mantle:
            pitch = 12.0f * Bump(clip, 0.10f, 0.88f);
            roll = 2.5f * Bump(clip, 0.45f, 0.98f);
            break;
        case EPlayerTraversalAction::Climb:
            pitch = -10.0f * Bump(clip, 0.02f, 0.46f) + 14.0f * Bump(clip, 0.44f, 0.98f);
            roll = 2.5f * Bump(clip, 0.70f, 1.0f);
            break;
        case EPlayerTraversalAction::ClimbOver:
            pitch = -10.0f * Bump(clip, 0.02f, 0.46f) + 11.0f * Bump(clip, 0.44f, 0.86f);
            roll = 7.0f * Bump(clip, 0.62f, 1.0f);
            break;
        case EPlayerTraversalAction::None:
        default: break;
        }
        const float fadeIn = Smooth01(traversalElapsed_ / 0.10f);
        traversalViewPitch_ = glm::radians(pitch) * fadeIn;
        traversalViewRoll_ = glm::radians(roll) * fadeIn;

        if (traversalElapsed_ >= total)
        {
            // Hand the move back to the controller: a vault keeps the run, a
            // climb stands still, a drop falls the rest of the way.
            planarVelocity_ = glm::vec3(plan.exitVelocity.x, 0.0f, plan.exitVelocity.z);
            verticalVelocity_ = plan.exitVelocity.y;
            if (plan.endCrouched && controller_.TrySetHeight(config_.Player.CrouchedHeight))
            {
                locomotion_.actualStance = EPlayerStance::Crouched;
                locomotion_.desiredStance = EPlayerStance::Crouched;
            }
            locomotion_.onGround = !plan.endAirborne;
            if (plan.endAirborne)
            {
                locomotion_.jumpPhase = EPlayerJumpPhase::AirLoop;
                jumpPhaseElapsed_ = 0.0f;
            }
            locomotion_.traversalHandWeight = {0.0f, 0.0f};
            traversalViewPitch_ = 0.0f;
            traversalViewRoll_ = 0.0f;
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
        // A traversal drives the eye along its own path (the body folds over
        // the lip); afterwards it settles back toward the stance height.
        if (!IsTraversing())
        {
            const float eyeT = 1.0f - std::exp(-config_.Player.EyeHeightLerpSpeed * dt);
            currentEyeHeight_ = glm::mix(currentEyeHeight_, eyeTarget, eyeT);
        }

        const bool sprinting = locomotion_.gait == EPlayerGait::Sprint;
        const float sprintRate = 1.0f / std::max(sprinting ? config_.Player.SprintRaiseSeconds
                                                            : config_.Player.SprintRecoverSeconds,
                                                 0.01f);
        sprintAlpha_ = glm::clamp(sprintAlpha_ + (sprinting ? 1.0f : -1.0f) * sprintRate * dt, 0.0f, 1.0f);

        const float targetFov = (IsAiming() && !meleeEquipped_ ? aimFov_ : camera.BaseFov) + camera.SprintFovBoost * sprintAlpha_;
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
                                          (IsAiming() ? camera.HeadBobAimScale : 1.0f)
                                    : 0.0f;
        bobWeight_ = glm::mix(bobWeight_, bobTarget, 1.0f - std::exp(-10.0f * dt));
        const float theta = glm::two_pi<float>() * stridePhase_;
        const float dip = 0.5f - 0.5f * std::cos(2.0f * theta);
        headBob_ = glm::vec3(0.0f, -camera.HeadBobHeight * bobWeight_ * dip, 0.0f) +
                   Right() * (camera.HeadBobHeight * 0.35f * bobWeight_ * std::sin(theta));

        // Lean the view slightly into a strafe.
        const float rollTarget =
            IsTraversing() ? 0.0f
                           : glm::clamp(LocalVelocity().x * camera.StrafeRollDegreesPerMps, -camera.StrafeRollMaxDegrees,
                                        camera.StrafeRollMaxDegrees) *
                                 (IsAiming() ? 0.4f : 1.0f);
        strafeRoll_ = glm::mix(strafeRoll_, rollTarget, 1.0f - std::exp(-8.0f * dt));
        viewRoll_ = glm::radians(strafeRoll_ + camera.HeadBobRollDegrees * bobWeight_ * std::sin(theta)) +
                    traversalViewRoll_;

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

    void PlayerController::Update(float deltaSeconds, NextEngine& engine)
    {
        aimInput_.Update(AimInputTime(), config_.Camera.AimTapSeconds);
        lookDelta_ = lookDeltaAccum_;
        lookDeltaAccum_ = glm::vec2(0.0f);
        landingSpeed_ = 0.0f;
        jumpBufferSeconds_ = std::max(0.0f, jumpBufferSeconds_ - std::max(deltaSeconds, 0.0f));
        // Airborne with jump held (or just pressed) and pushing forward: grab
        // any lip that comes within reach of the hands on the way up or down.
        if (!IsTraversing() && WantsAirCatch())
        {
            TryBeginTraversal(engine, true);
        }
        if (IsTraversing())
        {
            const glm::vec3 previousPosition = controller_.GetPosition();
            UpdateTraversal(deltaSeconds);
            UpdateMotion(deltaSeconds, previousPosition);
            UpdateView(deltaSeconds);
            UpdateCameraArm(deltaSeconds, engine);
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
        const bool wantsSprint = moving && sprintModifier_ && !IsAiming() && !sprintBlocked_ &&
                                 locomotion_.actualStance == EPlayerStance::Standing &&
                                 localMove.y >= config_.Player.SprintMinForwardInput;

        if (!moving)
        {
            locomotion_.gait = EPlayerGait::Idle;
        }
        else if (locomotion_.actualStance == EPlayerStance::Crouched || IsAiming() || walkModifier_)
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
            if (IsAiming())
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
        UpdateCameraArm(deltaSeconds, engine);
    }

    void PlayerController::FillCamera(glm::mat4& outModelView, float& outFov) const
    {
        const glm::vec3 forward = CameraForward();
        const glm::vec3 eye = CameraPosition();
        // First-person strafe lean and step sway roll the view about its axis.
        outModelView = glm::lookAt(eye, eye + forward, CameraUp());
        outFov = currentFov_;
    }
}
