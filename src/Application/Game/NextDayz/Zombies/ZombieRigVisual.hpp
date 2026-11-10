#pragma once

// ============================================================================
// ZombieRigVisual.hpp - One infected's ScadRig body, driven by its
// FZombieRuntime. The simulation decides; this only presents, and reports the
// hitboxes its skeleton ends up with.
//
// Animation graph, evaluated once per frame:
//   clip layers (FRigLayeredAnimator::Evaluate)
//     base     full body   idle / walk / run (or the lame pair) blended by the
//                          simulated speed, cadence-matched so feet do not skate
//     reach    arms        hands up toward the prey as a chase closes in
//     action   full body   alert / attack / stagger, sampled by the
//                          simulation's own timers: the strike frame is the
//                          damage frame
//     death    full body   back / forward / crumple, held on the last frame
//   procedural pass (RigPoseOps on the blended pose)
//     per-spawn posture (hunch, head tilt), head and torso tracking the
//     player, lean into turns, and hit flinches: damped springs kicked by each
//     hit's direction, point and impact, so rounds visibly land, stack and
//     never stop the legs.
// ============================================================================

#include <array>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "Engine/Assets/Data/RigAsset.hpp"
#include "Gameplay/Rig/RigInstance.h"
#include "Gameplay/Rig/RigLayeredAnimator.h"

#include "Application/Game/NextDayz/NextDayzConfig.hpp"
#include "Application/Game/NextDayz/Combat/CombatEvents.hpp"
#include "Application/Game/NextDayz/Zombies/ZombieSystem.hpp"

namespace Assets
{
    class Scene;
    class Node;
}

namespace NextDayz
{
    class ZombieRigVisual
    {
    public:
        static constexpr size_t kHitCapsuleCount = 12;

        struct FClips
        {
            const Assets::FRigClip* idle = nullptr;
            const Assets::FRigClip* walk = nullptr;
            const Assets::FRigClip* walkLimp = nullptr;
            const Assets::FRigClip* run = nullptr;
            const Assets::FRigClip* hobble = nullptr;
            const Assets::FRigClip* reach = nullptr;
            const Assets::FRigClip* alert = nullptr;
            const Assets::FRigClip* attackLunge = nullptr;
            const Assets::FRigClip* attackSwipe = nullptr;
            const Assets::FRigClip* stagger = nullptr;
            const Assets::FRigClip* deathBack = nullptr;
            const Assets::FRigClip* deathForward = nullptr;
            const Assets::FRigClip* deathCrumple = nullptr;

            static FClips Resolve(const Assets::FRigAsset& asset);
        };

        bool Instantiate(Assets::Scene& scene, const Assets::FRigAsset& asset, const FClips& clips,
                         const NextGameplay::FRigInstanceDesc& desc, const std::string& name);
        void Release();

        // A new infected took this body: snap to it and forget the last one.
        void Reset(const FZombieRuntime& zombie);
        // Kicks the flinch springs. Call for every damage event on this infected.
        void AddHit(const FDamageEvent& hit, const FZombieConfig& config);
        void Update(const FZombieRuntime& zombie, const glm::vec3& lookTarget, const FZombieConfig& config,
                    float deltaSeconds);
        void Hide();

        bool IsBound() const { return worldNode_ != nullptr; }
        bool Visible() const { return visible_; }
        std::span<const FHitCapsule> Hitboxes() const { return {hitboxes_.data(), hitboxCount_}; }
        const char* BaseClipName() const { return baseClipName_; }
        const char* ActionClipName() const { return actionClipName_; }
        float FlinchDegrees() const;

        // Standing capsules for an infected with no rig (or before its first pose).
        static size_t StandingHitboxes(const glm::vec3& position, const glm::vec3& forward,
                                       std::array<FHitCapsule, kHitCapsuleCount>& out);

    private:
        struct FBones
        {
            int32_t root = -1;
            int32_t pelvis = -1;
            int32_t torso = -1;
            int32_t head = -1;
            int32_t upperArmL = -1;
            int32_t foreArmL = -1;
            int32_t upperArmR = -1;
            int32_t foreArmR = -1;
            int32_t thighL = -1;
            int32_t calfL = -1;
            int32_t thighR = -1;
            int32_t calfR = -1;
        };

        struct FSpring3
        {
            glm::vec3 value{0.0f};
            glm::vec3 velocity{0.0f};
        };

        void UpdateLayers(const FZombieRuntime& zombie, const glm::vec3& lookTarget, const FZombieConfig& config,
                          float deltaSeconds);
        void UpdateProcedural(const FZombieRuntime& zombie, const glm::vec3& lookTarget, const FZombieConfig& config,
                              float deltaSeconds);
        void UpdateHitboxes();

        const Assets::FRigAsset* asset_ = nullptr;
        const FClips* clips_ = nullptr;
        FBones bones_{};
        std::shared_ptr<Assets::Node> worldNode_;
        NextGameplay::FRigLayeredAnimator animator_;
        NextGameplay::FRigLayerHandle baseLayer_ = NextGameplay::invalidRigLayerHandle;
        NextGameplay::FRigLayerHandle reachLayer_ = NextGameplay::invalidRigLayerHandle;
        NextGameplay::FRigLayerHandle actionLayer_ = NextGameplay::invalidRigLayerHandle;
        NextGameplay::FRigLayerHandle deathLayer_ = NextGameplay::invalidRigLayerHandle;
        std::vector<glm::mat4> transforms_;
        std::array<FHitCapsule, kHitCapsuleCount> hitboxes_{};
        size_t hitboxCount_ = 0;
        bool visible_ = false;

        // graph state
        float yaw_ = 0.0f;
        float yawRate_ = 0.0f;
        float turnLean_ = 0.0f;
        float lookWeight_ = 0.0f;
        float reachWeight_ = 0.0f;
        bool actionActive_ = false;
        bool deathActive_ = false;
        FSpring3 torsoFlinch_;
        FSpring3 headFlinch_;
        glm::vec2 pelvisDrop_{0.0f};                 // x = metres down, y = velocity
        glm::vec2 armKickL_{0.0f};                   // x = radians, y = velocity
        glm::vec2 armKickR_{0.0f};
        const char* baseClipName_ = "";
        const char* actionClipName_ = "";
    };
}
