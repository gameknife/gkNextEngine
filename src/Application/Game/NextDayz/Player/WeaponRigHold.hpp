#pragma once

// ============================================================================
// WeaponRigHold.hpp - How a weapon model sits in the survivor's hands, shared
// by the third-person rig (PlayerRigVisual) and the first-person arms
// (FirstPersonArms) so both views put the palms on the same grip points.
// The rig's own grips (bone_ik_hand_r / _l under bone_ik_weapon) were built
// for the rifles; other weapons override the model offset or the support hand.
// ============================================================================

#include <string_view>

#include <glm/glm.hpp>

namespace NextDayz
{
    struct FWeaponRigHold
    {
        glm::vec3 modelOffset{0.0f};   // weapon model under bone_weapon_socket
        bool overrideSupport = false;
        glm::vec3 supportWrist{0.0f};  // left wrist in bone_ik_weapon space
        float extend = 0.0f;           // TPS: push the frame along the muzzle (m)
    };

    inline FWeaponRigHold WeaponRigHoldFor(std::string_view weaponId)
    {
        if (weaponId == "pistol")
        {
            // Grip into the palm (the Makarov's grip hangs below its origin),
            // support hand cupping the firing hand, arms extended.
            return {glm::vec3(0.0f, 0.025f, -0.015f), true, glm::vec3(0.07f, -0.03f, -0.02f), 0.12f};
        }
        return {};
    }
}
