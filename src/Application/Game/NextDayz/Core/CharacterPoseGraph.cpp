#include "Engine/Common/CoreMinimal.hpp"
#include "CharacterPoseGraph.hpp"

#include <algorithm>
#include <cmath>

namespace NextDayz
{
    namespace
    {
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

    FSurvivorLocomotionClips ResolveSurvivorLocomotionClips(const Assets::FRigDefinition& asset)
    {
        FSurvivorLocomotionClips clips;
        std::ranges::transform(survivorLocomotionNames, clips.begin(), [&asset](auto name) { return asset.FindClip(name); });
        return clips;
    }

    void UpdateSurvivorLocomotion(const Assets::FRigDefinition& asset, NextGameplay::FRigLayeredAnimator& animator,
                                 NextGameplay::FRigLayerHandle layer, const FSurvivorLocomotionClips& clips,
                                 const FPlayerLocomotionState& locomotion, const FAnimationConfig& anim,
                                 float yaw, float crouchWeight, float dt, glm::vec2& moveDirection,
                                 float& movingWeight, std::string& baseClipName)
    {
        if (locomotion.traversalAction != EPlayerTraversalAction::None)
        {
            // TraversalActionName matches the clip names one to one.
            const char* name = TraversalActionName(locomotion.traversalAction);
            animator.SetManualBlend(layer, {{asset.FindClip(name), 1.0f}}, locomotion.traversalTime01,
                                     anim.JumpFadeSeconds);
            baseClipName = name;
            return;
        }
        if (locomotion.jumpPhase == EPlayerJumpPhase::Up || locomotion.jumpPhase == EPlayerJumpPhase::Down)
        {
            const char* name = locomotion.jumpPhase == EPlayerJumpPhase::Up ? "jump_up" : "jump_down";
            animator.SetManualBlend(layer, {{asset.FindClip(name), 1.0f}}, locomotion.jumpPhaseTime01,
                                     anim.JumpFadeSeconds);
            baseClipName = name;
            return;
        }
        if (locomotion.jumpPhase == EPlayerJumpPhase::AirLoop)
        {
            animator.SetLoopBlend(layer, {{asset.FindClip("jump_air_loop"), 1.0f}}, "jump_air", 1.0f,
                                   anim.JumpFadeSeconds);
            baseClipName = "jump_air_loop";
            return;
        }

        // Blend space. Velocity is measured, so walls, acceleration and stops
        // all show up in the legs; it is read in the body frame because the
        // lower body is what the clips move.
        const glm::vec3 bodyForward(std::sin(yaw), 0.0f, std::cos(yaw));
        const glm::vec3 bodyRight(-std::cos(yaw), 0.0f, std::sin(yaw));
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
            const glm::vec2 blended = glm::mix(moveDirection, target,
                                               1.0f - std::exp(-dt / std::max(anim.DirectionSmoothingSeconds, 1e-3f)));
            moveDirection = glm::length(blended) > 1.0e-3f ? glm::normalize(blended) : target;
        }

        const float right = std::max(moveDirection.x, 0.0f);
        const float left = std::max(-moveDirection.x, 0.0f);
        const float forward = std::max(moveDirection.y, 0.0f);
        const float backward = std::max(-moveDirection.y, 0.0f);
        const float directionSum = std::max(right + left + forward + backward, 1.0e-3f);
        const std::array<float, 4> direction = {forward / directionSum, backward / directionSum, left / directionSum,
                                                right / directionSum};

        const std::array<float, 4> stand = SpeedWeights(speed, anim.StandWalkAuthoredSpeed,
                                                        anim.StandRunAuthoredSpeed, anim.StandSprintAuthoredSpeed);
        const std::array<float, 4> crouch = SpeedWeights(speed, anim.CrouchWalkAuthoredSpeed,
                                                         anim.CrouchWalkAuthoredSpeed * 2.0f,
                                                         anim.CrouchWalkAuthoredSpeed * 3.0f);
        const float standing = 1.0f - crouchWeight;

        std::vector<NextGameplay::FRigClipBlendSample> samples;
        samples.reserve(clips.size());
        const auto clipAt = [&clips](size_t index)
        {
            return index < clips.size() ? clips[index] : nullptr;
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
        samples.push_back({clipAt(13), crouchWeight * crouch[0]});
        const float crouchWalk = crouchWeight * (1.0f - crouch[0]);
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
        if (crouchWeight < 0.5f && speed > anim.StandSprintAuthoredSpeed)
        {
            overspeed = speed / anim.StandSprintAuthoredSpeed;
        }
        else if (crouchWeight >= 0.5f && speed > anim.CrouchWalkAuthoredSpeed)
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
        movingWeight = glm::mix(movingWeight, glm::clamp(speed / anim.StandRunAuthoredSpeed, 0.0f, 1.0f),
                                1.0f - std::exp(-dt / 0.1f));

        animator.SetLoopBlend(layer, std::move(samples), "locomotion", playRate,
                               anim.LocomotionFadeSeconds);
        if (const Assets::FRigClip* clip = animator.DominantClip(layer))
        {
            baseClipName = clip->name;
        }
    }

    const char* UpdateInfectedLocomotion(NextGameplay::FRigLayeredAnimator& animator, NextGameplay::FRigLayerHandle layer,
        const Assets::FRigClip* idle, const Assets::FRigClip* walkClip, const Assets::FRigClip* walkLimpClip,
        const Assets::FRigClip* runClip, const Assets::FRigClip* hobbleClip, float speed, bool lame,
        const FZombieConfig& config)
    {
        // Base: idle -> walk -> run by the simulated speed.
        const Assets::FRigClip* walk = lame ? walkLimpClip : walkClip;
        const Assets::FRigClip* run = lame ? hobbleClip : runClip;
        const float walkSpeed = lame ? config.LimpAuthoredSpeed : config.WalkAuthoredSpeed;
        const float runSpeed = lame ? config.HobbleAuthoredSpeed : config.RunAuthoredSpeed;
        float idleWeight = 0.0f;
        float walkWeight = 0.0f;
        float runWeight = 0.0f;
        if (speed < walkSpeed)
        {
            walkWeight = speed / walkSpeed;
            idleWeight = 1.0f - walkWeight;
        }
        else if (speed < runSpeed)
        {
            runWeight = (speed - walkSpeed) / (runSpeed - walkSpeed);
            walkWeight = 1.0f - runWeight;
        }
        else
        {
            runWeight = 1.0f;
        }
        // Cadence-match the gait clips to the ground speed so the feet plant.
        const float gaitWeight = walkWeight + runWeight;
        float playRate = 1.0f;
        if (gaitWeight > 1.0e-3f && walk && run)
        {
            const float cadence = (walkWeight / walk->duration + runWeight / run->duration) / gaitWeight;
            const float authored = (walkWeight * walkSpeed + runWeight * runSpeed) / gaitWeight;
            const float overspeed = std::clamp(speed / std::max(authored, 0.01f), config.MinPlayRate, config.MaxPlayRate);
            const Assets::FRigClip* dominant =
                idleWeight > std::max(walkWeight, runWeight) ? idle : (runWeight > walkWeight ? run : walk);
            playRate = cadence * overspeed * (dominant ? dominant->duration : 1.0f);
        }
        animator.SetLoopBlend(layer, {{idle, idleWeight}, {walk, walkWeight}, {run, runWeight}}, "gait",
                               playRate, 0.2f);
        return runWeight > std::max(walkWeight, idleWeight)
            ? (lame ? "hobble" : "run")
            : (walkWeight > idleWeight ? (lame ? "walk_limp" : "walk") : "idle");
    }
}
