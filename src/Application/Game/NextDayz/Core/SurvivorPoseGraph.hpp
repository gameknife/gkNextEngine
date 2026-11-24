#pragma once

// ============================================================================
// SurvivorPoseGraph.hpp - The survivor's third-person animation graph, shared
// by the desktop rig (PlayerRigVisual writes it onto scene nodes) and the Web
// client (which streams the bone matrices to three.js). Evaluated per frame:
//   clip layers (FRigLayeredAnimator::Evaluate)
//     locomotion   full body   18-clip blend space: stance x speed x direction
//     hold         arms + IK   ready / aim / sprint carry weapon frames
//     hold_body    torso+head  the same clips, additive: blade and head turn
//     weapon_*     reload / switch / melee, same split as hold
//     recoil       upper body  additive per-shot kick
//     action       full body   loot
//   procedural pass (RigPoseOps on the blended pose)
//     whole-body lean into acceleration, aim pitch / twist split across
//     torso, head and weapon, weapon bob and turn sway, traversal hand plants,
//     the weapon switch between hands and holsters, then both arms IK-solved
//     onto bone_ik_hand_r/l so the hands never leave the weapon.
// No engine dependency: the animator is bound by the caller.
// ============================================================================

#include "Application/Game/NextDayz/Core/CharacterPoseGraph.hpp"
#include "Application/Game/NextDayz/Weapons/WeaponDefs.hpp"

#include <array>
#include <functional>
#include <string>
#include <vector>

#include <glm/gtc/quaternion.hpp>

namespace NextDayz
{
    class FSurvivorPoseGraph
    {
    public:
        // Where a holstered weapon hangs: in the pelvis's space for the sidearm,
        // the torso's for the back slots. Moves while a weapon is being switched.
        struct FHolster
        {
            glm::vec3 position{0.0f};
            glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
        };

        static int32_t HolsterParentBone(const Assets::FRigDefinition& asset, int slot);
        // Where each holster sits on a bare body.
        static FHolster RestingHolster(int slot);
        // Clothing pushes the holsters out (a coat, a pack); the switch reaches for them there.
        void SetRestingHolster(int slot, const glm::vec3& position) { rest_[slot].position = position; }
        const FHolster& Resting(int slot) const { return rest_[slot]; }

        // `bind` attaches the animator to the rig: to scene nodes on desktop, to
        // a bare pose on the Web. Layers are created afterwards.
        void Bind(const Assets::FRigDefinition& asset, const FAnimationConfig& config,
                  const std::function<void(NextGameplay::FRigLayeredAnimator&)>& bind);
        void Unbind();
        bool IsBound() const { return bound_; }

        // One frame: body yaw, layer weights, clip blend, procedural pass. The
        // rig's component space is the feet, turned to BodyYaw().
        void Advance(const glm::vec3& feet, const FPlayerPresentationState& state, float dt);
        // The cabin's seated pose: every layer but the base off, weapons stowed.
        void Seated(float dt);
        void TriggerRecoil(float scale);

        NextGameplay::FRigLayeredAnimator& Animator() { return animator_; }
        const NextGameplay::FRigLayeredAnimator& Animator() const { return animator_; }
        const std::array<FHolster, kWeaponSlotCount>& Holsters() const { return holsters_; }

        float BodyYaw() const { return bodyYaw_; }
        float AimWeight() const { return aimWeight_; }
        float WeaponActionWeight() const { return weaponActionWeight_; }
        float LeanRollDegrees() const { return lean_.x; }
        bool TurningInPlace() const { return turningInPlace_; }
        bool RecoilActive() const { return recoilActive_; }
        const std::string& CurrentBaseClipName() const { return baseClipName_; }
        const std::string& CurrentWeaponActionClipName() const { return weaponActionClipName_; }
        // Distance from the right hand to its grip target (m); 0 when no weapon.
        float GripError() const { return gripError_; }
        // Worst distance (m) of a fully planted traversal hand from its lip target; 0 when none is planted.
        float PlantError() const { return plantError_; }
        // Actual pose deviation from a forward-only elbow hinge (degrees).
        float ElbowConstraintErrorDegrees() const;
        // Height of a hand in the torso's frame: slot 0 the left, else the right.
        float HandHeightInTorso(int slot) const;

    private:
        struct FBones
        {
            int32_t root = -1, pelvis = -1, torso = -1, head = -1;
            int32_t upperArmL = -1, foreArmL = -1, handL = -1;
            int32_t upperArmR = -1, foreArmR = -1, handR = -1;
            int32_t ikWeapon = -1, ikHandL = -1, ikHandR = -1;
        };
        struct FClips
        {
            FSurvivorLocomotionClips locomotion{};
            const Assets::FRigClip* ready = nullptr;
            const Assets::FRigClip* aim = nullptr;
            const Assets::FRigClip* sprint = nullptr;
            const Assets::FRigClip* recoil = nullptr;
        };

        void UpdateProcedural(const FPlayerPresentationState& state, const glm::mat4& componentToWorld);
        void UpdateSwitchPose(const FPlayerPresentationState& state);

        const Assets::FRigDefinition* asset_ = nullptr;
        FAnimationConfig config_{};
        FBones bones_{};
        FClips clips_{};
        NextGameplay::FRigLayeredAnimator animator_;
        NextGameplay::FRigLayerHandle locomotionLayer_ = NextGameplay::invalidRigLayerHandle;
        NextGameplay::FRigLayerHandle holdLayer_ = NextGameplay::invalidRigLayerHandle;
        NextGameplay::FRigLayerHandle holdBodyLayer_ = NextGameplay::invalidRigLayerHandle;
        NextGameplay::FRigLayerHandle weaponActionLayer_ = NextGameplay::invalidRigLayerHandle;
        NextGameplay::FRigLayerHandle weaponActionBodyLayer_ = NextGameplay::invalidRigLayerHandle;
        NextGameplay::FRigLayerHandle recoilLayer_ = NextGameplay::invalidRigLayerHandle;
        NextGameplay::FRigLayerHandle actionLayer_ = NextGameplay::invalidRigLayerHandle;
        bool bound_ = false;
        std::vector<glm::mat4> transforms_;
        std::array<FHolster, kWeaponSlotCount> holsters_{};
        std::array<FHolster, kWeaponSlotCount> rest_{RestingHolster(0), RestingHolster(1), RestingHolster(2)};
        std::string baseClipName_;
        std::string weaponActionClipName_;

        bool yawInitialized_ = false;
        float bodyYaw_ = 0.0f;
        float previousAimYaw_ = 0.0f;
        bool turningInPlace_ = false;
        float crouchWeight_ = 0.0f;
        glm::vec2 moveDirection_{0.0f, 1.0f};   // body-relative: x=right, y=forward
        float aimWeight_ = 0.0f;
        float sprintHoldWeight_ = 0.0f;
        float holdWeight_ = 0.0f;
        float weaponWeight_ = 0.0f;              // weapon in hand at all
        float freeHandWeight_ = 0.0f;            // support hand off the weapon (loot, traversal)
        float traverseWeight_ = 0.0f;            // smoothed "in a traversal": torso stops tracking the camera
        float weaponActionWeight_ = 0.0f;
        float actionWeight_ = 0.0f;
        float movingWeight_ = 0.0f;
        glm::vec3 previousVelocity_{0.0f};
        glm::vec2 lean_{0.0f};                   // degrees: x=roll (top to the right), y=pitch (top forward)
        glm::vec2 leanVelocity_{0.0f};
        float turnSway_ = 0.0f;                  // radians the weapon lags behind a turn
        bool recoilActive_ = false;
        float gripError_ = 0.0f;
        float plantError_ = 0.0f;
    };
}
