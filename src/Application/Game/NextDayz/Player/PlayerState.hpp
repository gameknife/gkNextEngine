#pragma once

#include <array>
#include <string>

#include <glm/glm.hpp>

namespace NextDayz
{
    enum class EPlayerStance
    {
        Standing,
        Crouched,
    };

    enum class EPlayerGait
    {
        Idle,
        Walk,
        Run,
        Sprint,
    };

    enum class EPlayerJumpPhase
    {
        None,
        Up,
        AirLoop,
        Down,
    };

    // Contextual traversal (jump key, or a held jump while airborne). Clip names
    // match TraversalActionName one to one.
    enum class EPlayerTraversalAction
    {
        None,
        Vault,     // thin top up to the waist: support hand plants, legs sweep over, the run continues
        Mantle,    // top up to the chest: both hands on top, press up, knee over, stand
        Climb,     // top above the chest: jump, catch the lip, haul up, stand on top
        ClimbOver, // thin wall above the waist: catch or press, swing the legs over, drop behind
    };

    enum class EPlayerAction
    {
        None,
        LootGround,
        Eat,
        Drink,
        Heal,
        DrinkFromWell,
        FillBottle,
        Rest,
    };

    enum class EPlayerLifeState
    {
        Alive,
        Dead,
    };

    enum class EWeaponPresentationAction
    {
        None,
        Reload,
        Switch,
        Melee,
    };

    struct FPlayerLocomotionState
    {
        EPlayerStance desiredStance = EPlayerStance::Standing;
        EPlayerStance actualStance = EPlayerStance::Standing;
        EPlayerGait gait = EPlayerGait::Idle;
        glm::vec2 localMove{0.0f}; // x=right, y=forward
        glm::vec3 worldVelocity{0.0f};
        float horizontalSpeed = 0.0f;
        EPlayerJumpPhase jumpPhase = EPlayerJumpPhase::None;
        float jumpPhaseTime01 = 0.0f;
        EPlayerTraversalAction traversalAction = EPlayerTraversalAction::None;
        float traversalTime01 = 0.0f;   // clip phase, warped onto the geometry by the controller
        float traversalHeight = 0.0f;   // obstacle top above the take-off feet
        float traversalYaw = 0.0f;      // the body squares up to the obstacle for the whole move
        // World-space hand plants on the obstacle top: [0] support (left) hand,
        // [1] firing (right) hand with the weapon laid on the top. Weight 0 = free.
        std::array<glm::vec3, 2> traversalHand{glm::vec3(0.0f), glm::vec3(0.0f)};
        std::array<float, 2> traversalHandWeight{0.0f, 0.0f};
        bool onGround = false;
        bool standBlocked = false;
    };

    struct FPlayerPresentationState
    {
        FPlayerLocomotionState locomotion;
        EPlayerAction action = EPlayerAction::None;
        float actionTime01 = 0.0f;
        float aimWeight = 0.0f;
        float aimPitchRadians = 0.0f;
        float aimYawRadians = 0.0f;   // camera yaw; the rig's lower body follows it with lag
        float sprintAlpha = 0.0f;     // 0 = weapon up, 1 = sprint carry
        bool hasWeapon = false;
        std::string weaponId;
        EWeaponPresentationAction weaponAction = EWeaponPresentationAction::None;
        float weaponActionTime01 = 0.0f;
        int activeWeaponSlot = 0;
        std::array<std::string, 2> slotWeaponIds;
    };

    inline const char* StanceName(EPlayerStance stance)
    {
        return stance == EPlayerStance::Crouched ? "crouched" : "standing";
    }

    inline const char* GaitName(EPlayerGait gait)
    {
        switch (gait)
        {
        case EPlayerGait::Walk: return "walk";
        case EPlayerGait::Run: return "run";
        case EPlayerGait::Sprint: return "sprint";
        case EPlayerGait::Idle:
        default: return "idle";
        }
    }

    inline const char* JumpPhaseName(EPlayerJumpPhase phase)
    {
        switch (phase)
        {
        case EPlayerJumpPhase::Up: return "up";
        case EPlayerJumpPhase::AirLoop: return "air_loop";
        case EPlayerJumpPhase::Down: return "down";
        case EPlayerJumpPhase::None:
        default: return "none";
        }
    }

    inline const char* TraversalActionName(EPlayerTraversalAction action)
    {
        switch (action)
        {
        case EPlayerTraversalAction::Vault: return "vault";
        case EPlayerTraversalAction::Mantle: return "mantle";
        case EPlayerTraversalAction::Climb: return "climb";
        case EPlayerTraversalAction::ClimbOver: return "climb_over";
        case EPlayerTraversalAction::None:
        default: return "none";
        }
    }

    inline const char* ActionName(EPlayerAction action)
    {
        switch (action)
        {
        case EPlayerAction::LootGround: return "loot_ground";
        case EPlayerAction::Eat: return "eat";
        case EPlayerAction::Drink: return "drink";
        case EPlayerAction::Heal: return "heal";
        case EPlayerAction::DrinkFromWell: return "drink_from_well";
        case EPlayerAction::FillBottle: return "fill_bottle";
        case EPlayerAction::Rest: return "rest";
        case EPlayerAction::None:
        default: return "none";
        }
    }

    // What the player is doing, for the HUD.
    inline const char* ActionLabel(EPlayerAction action)
    {
        switch (action)
        {
        case EPlayerAction::LootGround: return "Taking";
        case EPlayerAction::Eat: return "Eating";
        case EPlayerAction::Drink: return "Drinking";
        case EPlayerAction::Heal: return "Treating wounds";
        case EPlayerAction::DrinkFromWell: return "Drinking from the well";
        case EPlayerAction::FillBottle: return "Filling the bottle";
        case EPlayerAction::Rest: return "Resting";
        case EPlayerAction::None:
        default: return "";
        }
    }

    inline const char* WeaponActionName(EWeaponPresentationAction action)
    {
        switch (action)
        {
        case EWeaponPresentationAction::Reload: return "reload";
        case EWeaponPresentationAction::Switch: return "switch";
        case EWeaponPresentationAction::Melee: return "melee";
        case EWeaponPresentationAction::None:
        default: return "none";
        }
    }
}
