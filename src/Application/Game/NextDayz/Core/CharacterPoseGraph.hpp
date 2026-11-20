#pragma once

#include "Application/Game/NextDayz/NextDayzConfig.hpp"
#include "Application/Game/NextDayz/Player/PlayerState.hpp"
#include "Gameplay/Rig/RigLayeredAnimator.h"

namespace NextDayz
{
    inline constexpr std::array<std::string_view, 18> survivorLocomotionNames{
        "stand_idle", "stand_walk_f", "stand_walk_b", "stand_walk_l", "stand_walk_r",
        "stand_run_f", "stand_run_b", "stand_run_l", "stand_run_r",
        "stand_sprint_f", "stand_sprint_b", "stand_sprint_l", "stand_sprint_r",
        "crouch_idle", "crouch_walk_f", "crouch_walk_b", "crouch_walk_l", "crouch_walk_r",
    };
    using FSurvivorLocomotionClips = std::array<const Assets::FRigClip*, survivorLocomotionNames.size()>;

    FSurvivorLocomotionClips ResolveSurvivorLocomotionClips(const Assets::FRigDefinition& asset);
    void UpdateSurvivorLocomotion(const Assets::FRigDefinition& asset, NextGameplay::FRigLayeredAnimator& animator,
                                 NextGameplay::FRigLayerHandle layer, const FSurvivorLocomotionClips& clips,
                                 const FPlayerLocomotionState& locomotion, const FAnimationConfig& anim,
                                 float yaw, float crouchWeight, float dt, glm::vec2& moveDirection,
                                 float& movingWeight, std::string& baseClipName);

    const char* UpdateInfectedLocomotion(NextGameplay::FRigLayeredAnimator& animator, NextGameplay::FRigLayerHandle layer,
        const Assets::FRigClip* idle, const Assets::FRigClip* walkClip, const Assets::FRigClip* walkLimpClip,
        const Assets::FRigClip* runClip, const Assets::FRigClip* hobbleClip, float speed, bool lame,
        const FZombieConfig& config);
}
