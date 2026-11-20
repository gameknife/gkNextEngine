#pragma once

#include <cstdint>

namespace NextDayz
{
    enum class ENoiseType : uint8_t
    {
        Footstep,
        Melee,
        Pistol,
        Rifle,
        Shotgun,
        Impact,
    };

    inline const char* NoiseTypeName(ENoiseType type)
    {
        switch (type)
        {
        case ENoiseType::Footstep: return "footstep";
        case ENoiseType::Melee: return "melee";
        case ENoiseType::Pistol: return "pistol";
        case ENoiseType::Rifle: return "rifle";
        case ENoiseType::Shotgun: return "shotgun";
        case ENoiseType::Impact: return "impact";
        }
        return "noise";
    }
}
