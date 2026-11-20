#pragma once

#include "Application/Game/NextDayz/Weapons/AttachmentDefs.hpp"
#include "Application/Game/NextDayz/Weapons/WeaponDefs.hpp"

namespace NextDayz
{
    struct FWeaponMount
    {
        glm::vec3 position{};
        float scale = 1.f;
    };

    inline FWeaponMount WeaponMount(std::string_view weaponId, EAttachmentSlot slot, bool factoryScope = false)
    {
        const auto points = WeaponModelPoints(weaponId);
        const FWeaponDef* weapon = FindWeaponDef(weaponId);
        const bool pistol = weapon && weapon->IsSidearm();
        glm::vec3 position(0.0f);
        switch (slot)
        {
        case EAttachmentSlot::Optic:
            if (!factoryScope) position = {0.0f, WeaponOpticRailHeight(weaponId), WeaponOpticRailForward(weaponId)};
            break;
        case EAttachmentSlot::Grip:
            position = {0.0f, points.boreHeight - 0.040f, weaponId == "svd" ? 0.36f : 0.28f};
            break;
        case EAttachmentSlot::Magazine:
            if (weaponId == "aps") position = {0.0f, -0.135f, -0.05f};
            else if (weaponId == "svt40") position = {0.0f, -0.14f, 0.09f};
            else position = {0.0f, pistol ? -0.115f : weaponId == "svd" ? -0.15f : -0.19f, pistol ? -0.04f : 0.15f};
            break;
        case EAttachmentSlot::Muzzle: position = {0.0f, points.boreHeight, points.muzzleForward}; break;
        case EAttachmentSlot::Tactical:
            position = {pistol ? 0.0f : -0.045f, points.boreHeight - (pistol ? 0.05f : 0.015f), pistol ? 0.11f : 0.30f};
            break;
        default: break;
        }
        return {position, pistol && slot == EAttachmentSlot::Muzzle ? .7f : 1.f};
    }
}
