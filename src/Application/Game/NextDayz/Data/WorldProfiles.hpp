#pragma once

#include "Engine/Common/CoreMinimal.hpp"

namespace NextDayz
{
    // What kind of place a loot spot is; picks its weighted item table.
    enum class ELootProfile : uint8_t
    {
        Residential,
        Medical,
        Military,
        Industrial,
        Wilderness,
        Commercial,   // shops, supermarket, gas station
        Farm,
        Police,
        Count,
    };

    enum class ELootCategory : uint8_t
    {
        FoodWater,
        Medical,
        Ammo,
        Weapon,
        Clothing,
        Misc,
        Count,
    };

    struct FLootRespawnTuning
    {
        double foodWaterSeconds = 6.0 * 60.0;
        double medicalSeconds = 9.0 * 60.0;
        double ammoSeconds = 12.0 * 60.0;
        double weaponSeconds = 24.0 * 60.0;
        double clothingSeconds = 14.0 * 60.0;
        double miscSeconds = 10.0 * 60.0;
        // A spot restocks only once the player has left its area (POI zone)
        // by this much and has not been looking at the spot for a while.
        float minimumPlayerDistance = 60.0f;
        double minimumOffscreenSeconds = 20.0;
        // Share of a category's spots that may be stocked at once.
        float categoryStockShare = 0.8f;
    };

    inline double RespawnCooldown(ELootCategory category, const FLootRespawnTuning& tuning)
    {
        switch (category)
        {
        case ELootCategory::FoodWater: return tuning.foodWaterSeconds;
        case ELootCategory::Medical: return tuning.medicalSeconds;
        case ELootCategory::Ammo: return tuning.ammoSeconds;
        case ELootCategory::Weapon: return tuning.weaponSeconds;
        case ELootCategory::Clothing: return tuning.clothingSeconds;
        case ELootCategory::Misc:
        default: return tuning.miscSeconds;
        }
    }

    inline const char* LootProfileName(ELootProfile profile)
    {
        switch (profile)
        {
        case ELootProfile::Residential: return "residential";
        case ELootProfile::Medical: return "medical";
        case ELootProfile::Military: return "military";
        case ELootProfile::Industrial: return "industrial";
        case ELootProfile::Wilderness: return "wilderness";
        case ELootProfile::Commercial: return "commercial";
        case ELootProfile::Farm: return "farm";
        case ELootProfile::Police: return "police";
        default: return "unknown";
        }
    }

    inline std::optional<ELootProfile> ParseLootProfile(std::string_view name)
    {
        for (uint8_t index = 0; index < static_cast<uint8_t>(ELootProfile::Count); ++index)
        {
            const ELootProfile profile = static_cast<ELootProfile>(index);
            if (name == LootProfileName(profile))
            {
                return profile;
            }
        }
        return std::nullopt;
    }
}
