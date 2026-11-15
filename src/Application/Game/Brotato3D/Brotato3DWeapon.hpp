#pragma once

#include "Engine/Common/CoreMinimal.hpp"
#include "Brotato3DDataLoader.hpp"

namespace Brotato3D
{
    struct FWeaponRuntime
    {
        std::string weaponId;
        const FWeaponDef* def = nullptr;
        FWeaponDef tieredDef{};
        int tier = 1;
        float cooldownMs = 0.0f;
        uint32_t focusTag = 0;   // runtimeTag of the zombie this weapon is working on (0 = none)
    };
}
