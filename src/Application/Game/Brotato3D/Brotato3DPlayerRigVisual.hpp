#pragma once

// ============================================================================
// Brotato3DPlayerRigVisual.hpp - The hero's ScadRig body (brotato_survivor.scad,
// the NextDayz articulated skeleton) and the weapon in its hands.
//
// Presentation only; Brotato3DPlayerSystem.cpp owns movement and aim. The graph
// is the NextDayz one cut down to what a top-down shooter shows:
//   locomotion  idle + run F/B/L/R blended by the velocity in the body frame
//               (one sync group, cadence-matched): the body faces the aim and
//               the legs strafe, backpedal or run under it
//   hold        both arms on the weapon (ready <-> aim), torso / head additive
//   recoil      additive kick retriggered per shot
//   action      the dash lunge
// plus a procedural lean into acceleration.
// ============================================================================

#include <array>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include "Engine/Assets/Data/RigAsset.hpp"
#include "Gameplay/Rig/RigInstance.h"
#include "Gameplay/Rig/RigLayeredAnimator.h"

namespace Assets
{
    class Node;
    class Scene;
    struct FMaterial;
    class Model;
}

namespace Brotato3D
{
    struct FPlayerRigInput
    {
        glm::vec3 feetPosition{0.0f};
        glm::vec3 velocity{0.0f};
        glm::vec3 facingDirection{0.0f, 0.0f, -1.0f};   // where the body and the muzzle point
        bool aiming = false;                            // a target is in range
        bool dashing = false;
    };

    class FPlayerRigVisual
    {
    public:
        bool LoadRig(const std::string& rigPath, const std::string& weaponCatalogPath);
        void InjectAssets(std::vector<::Assets::Model>& models,
                          std::vector<::Assets::FMaterial>& materials,
                          uint32_t defaultTintMaterialId);
        void OnSceneLoaded(::Assets::Scene& scene);
        void OnSceneUnloaded();

        void SetVisible(bool visible);
        void SetTintMaterial(uint32_t materialId);
        void ResetFacing(const glm::vec3& direction);
        // Empty id (or one with no model) leaves the hands empty.
        void SetWeapon(const std::string& weaponId);
        void NotifyFired();
        void Update(const FPlayerRigInput& input, float deltaSeconds);

        bool IsBound() const { return bound_; }
        float BodyYaw() const { return bodyYaw_; }
        // World position of the held weapon's muzzle; false with no rig or no weapon.
        bool MuzzleWorldPosition(glm::vec3& outPosition) const;

    private:
        void ApplyTintMaterials();
        void ApplyWeaponVisibility();

        ::Assets::FRigAsset asset_{};
        ::Assets::FRigAsset weaponAsset_{};
        bool hasRig_ = false;
        bool hasWeapons_ = false;
        bool injected_ = false;
        bool bound_ = false;
        bool visible_ = false;

        std::vector<uint32_t> partModelIds_;
        std::vector<std::array<uint32_t, 16>> partMaterialIds_;
        std::vector<::Assets::Node*> partNodes_;
        uint32_t tintMaterialId_ = 0;

        struct FWeaponModel
        {
            uint32_t modelId = 0;
            std::array<uint32_t, 16> materials{};
            float muzzleDistance = 0.5f;
            std::shared_ptr<::Assets::Node> node;
        };
        std::map<std::string, FWeaponModel> weapons_;
        std::string weaponId_;

        ::Assets::Scene* scene_ = nullptr;
        std::shared_ptr<::Assets::Node> worldNode_;
        std::vector<::Assets::Node*> boneNodes_;
        ::Assets::Node* socketNode_ = nullptr;

        NextGameplay::FRigLayeredAnimator animator_;
        NextGameplay::FRigLayerHandle locomotionLayer_ = NextGameplay::invalidRigLayerHandle;
        NextGameplay::FRigLayerHandle holdLayer_ = NextGameplay::invalidRigLayerHandle;
        NextGameplay::FRigLayerHandle holdBodyLayer_ = NextGameplay::invalidRigLayerHandle;
        NextGameplay::FRigLayerHandle recoilLayer_ = NextGameplay::invalidRigLayerHandle;
        NextGameplay::FRigLayerHandle actionLayer_ = NextGameplay::invalidRigLayerHandle;
        const ::Assets::FRigClip* idleClip_ = nullptr;
        std::array<const ::Assets::FRigClip*, 4> runClips_{};   // F, B, L, R
        const ::Assets::FRigClip* readyClip_ = nullptr;
        const ::Assets::FRigClip* aimClip_ = nullptr;
        const ::Assets::FRigClip* recoilClip_ = nullptr;
        const ::Assets::FRigClip* dashClip_ = nullptr;

        float bodyYaw_ = glm::pi<float>();
        glm::vec2 localMove_{0.0f};     // smoothed, x = left, y = forward, in run-speed units
        float aimWeight_ = 0.0f;
        bool dashing_ = false;
        glm::vec3 smoothedVelocity_{0.0f};
        glm::vec2 lean_{0.0f};          // world-space tilt target (x, z), radians
    };
}
