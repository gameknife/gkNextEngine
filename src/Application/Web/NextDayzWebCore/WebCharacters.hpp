#pragma once

#include "Application/Game/NextDayz/Core/CharacterPoseGraph.hpp"
#include "Application/Game/NextDayz/Zombies/ZombieState.hpp"
#include "Gameplay/Rig/RigPoseOps.h"

#include <span>

namespace NextDayz
{
    struct FWebCharacterPose
    {
        size_t offset = 0; // In float32 elements, in the current frame.
        float yaw = 0.f;
    };

    class FWebSurvivorPose
    {
    public:
        void Bind(const Assets::FRigDefinition& asset);
        FWebCharacterPose Advance(const FPlayerPresentationState& state, const FAnimationConfig& config,
                                  float dt, std::vector<float>& output);

    private:
        NextGameplay::FRigLayeredAnimator animator_;
        FSurvivorLocomotionClips clips_{};
        NextGameplay::FRigLayerHandle locomotion_, hold_, holdBody_, weaponAction_, weaponActionBody_, lootAction_;
        glm::vec2 direction_{0.f, 1.f};
        float crouch_ = 0.f, moving_ = 0.f, holdWeight_ = 0.f, aim_ = 0.f, yaw_ = 0.f;
        bool yawInitialized_ = false, turning_ = false;
        std::string baseClip_;
        std::vector<glm::mat4> transforms_;
    };

    class FWebInfectedPose
    {
    public:
        void Bind(const Assets::FRigDefinition& asset);
        const std::vector<glm::mat4>& Transforms() const { return transforms_; }
        FWebCharacterPose Advance(const FZombieRuntime& zombie, const FZombieConfig& config, float dt,
                                  std::vector<float>& output);

    private:
        NextGameplay::FRigLayeredAnimator animator_;
        NextGameplay::FRigLayerHandle locomotion_, action_;
        const Assets::FRigClip* idle_ = nullptr;
        const Assets::FRigClip* walk_ = nullptr;
        const Assets::FRigClip* limp_ = nullptr;
        const Assets::FRigClip* run_ = nullptr;
        const Assets::FRigClip* hobble_ = nullptr;
        std::vector<glm::mat4> transforms_;
    };
}
