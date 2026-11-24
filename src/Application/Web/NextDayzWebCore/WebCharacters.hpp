#pragma once

#include "Application/Game/NextDayz/Core/SurvivorPoseGraph.hpp"
#include "Application/Game/NextDayz/Zombies/ZombieState.hpp"
#include "Gameplay/Rig/RigPoseOps.h"

#include <span>

namespace NextDayz
{
    struct FWebCharacterPose
    {
        size_t offset = 0; // In float32 elements, in the current frame.
        float yaw = 0.f;
        // Survivors only: four more matrices after the bones, the held weapon
        // then one per holster slot, and which of them the desktop rig shows.
        size_t gearOffset = 0;
        bool heldVisible = false;
        std::array<bool, kWeaponSlotCount> holsterVisible{};
    };

    // The desktop's third-person graph (Core/SurvivorPoseGraph), streamed as matrices.
    class FWebSurvivorPose
    {
    public:
        void Bind(const Assets::FRigDefinition& asset, const FAnimationConfig& animation, float weaponScale);
        FWebCharacterPose Advance(const glm::vec3& feet, const FPlayerPresentationState& state, float dt,
                                  std::vector<float>& output);
        void TriggerRecoil(float scale) { graph_.TriggerRecoil(scale); }

    private:
        FSurvivorPoseGraph graph_;
        const Assets::FRigDefinition* asset_ = nullptr;
        float weaponScale_ = 1.f;
        int32_t socket_ = -1;
        std::array<int32_t, kWeaponSlotCount> holsterParents_{};
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
