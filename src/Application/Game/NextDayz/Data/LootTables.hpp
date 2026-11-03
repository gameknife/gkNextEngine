#pragma once

// ============================================================================
// LootTables.hpp - What each kind of place holds. A loot spot rolls one item
// from its profile's weighted table; the roll is a pure function of a 32-bit
// value, so the LootDirector's deterministic rolls replay the same world.
// Pure data + rules (unit tested); visuals and inventory live elsewhere.
// ============================================================================

#include "Engine/Common/CoreMinimal.hpp"

#include <span>

#include "Application/Game/NextDayz/Data/ItemDefs.hpp"
#include "Application/Game/NextDayz/Data/WorldProfiles.hpp"

namespace NextDayz
{
    struct FLootTableEntry
    {
        std::string_view itemId;
        uint16_t weight = 1;
        uint8_t minCount = 1;
        uint8_t maxCount = 1;
    };

    // One stack lying in the world (or in a pile of several).
    struct FLootStack
    {
        std::string itemId;
        int count = 1;
        int loadedAmmo = 0;     // firearms: rounds already in the magazine
    };

    std::span<const FLootTableEntry> LootTable(ELootProfile profile);
    ELootCategory CategoryOfItem(std::string_view itemId);

    // Rolls one stack. `onlyCategory` restricts the table (Count = any).
    FLootStack RollLoot(ELootProfile profile, uint32_t roll, ELootCategory onlyCategory = ELootCategory::Count);

    // Hash step so one roll value can seed several independent draws.
    constexpr uint32_t MixRoll(uint32_t value, uint32_t salt)
    {
        uint32_t x = value ^ (salt * 0x9E3779B9u);
        x ^= x >> 16;
        x *= 0x7FEB352Du;
        x ^= x >> 15;
        x *= 0x846CA68Bu;
        x ^= x >> 16;
        return x;
    }
}
