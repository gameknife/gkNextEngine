#pragma once

#include "Engine/Common/CoreMinimal.hpp"

#include <span>

namespace NextDayz
{
    enum class EItemKind : uint8_t
    {
        Weapon,
        Ammo,
        Clothing,
        Consumable,
        Melee,
        Misc,
    };

    enum class EEquipSlot : uint8_t
    {
        None,
        Head,
        Torso,
        Vest,
        Legs,
        Back,
        PrimaryWeapon,
        SecondaryWeapon,
        Hands,
    };

    // What "Use" does with an item. Passive tools (compass, map, can opener)
    // work just by being carried and have no use action.
    enum class EItemUse : uint8_t
    {
        None,
        Eat,
        Drink,
        Bandage,      // stops bleeding
        FirstAid,     // stops bleeding and heals
        Painkillers,  // heals over time
        Rest,         // bedroll: a long rest that trades food and water for health
        Radio,        // tune in: needs a battery, marks a supply cache
        Lantern,      // light or put out; burns fuel
        Firecrackers, // thrown ahead, the bangs draw the infected
        SignalFlare,  // single-use rescue call, only at an extraction helipad
    };

    // Carried tools that change how other items work.
    enum class EItemTool : uint8_t
    {
        None,
        CanOpener,   // opens cans without spilling them
        Compass,     // heading strip on the HUD
        Map,         // the map screen (M)
    };

    struct FItemDef
    {
        std::string_view id;
        std::string_view displayName;
        EItemKind kind = EItemKind::Misc;
        int volumePerStack = 1;
        int maxStack = 1;
        EEquipSlot equipSlot = EEquipSlot::None;
        int containerCapacity = 0;
        float hungerDelta = 0.0f;
        float hydrationDelta = 0.0f;
        float healthDelta = 0.0f;
        std::string_view weaponDefId;
        EItemUse use = EItemUse::None;
        EItemTool tool = EItemTool::None;
        bool toolCanOpener = false;  // a blade that also opens cans (knife, axe)
        bool needsOpener = false;    // sealed can: half of it spills without a tool
        float armor = 0.0f;          // share of infected damage absorbed while worn
        float warmth = 0.0f;         // insulation while worn (0..1, summed)
        std::string_view leftover;   // what remains after use (water bottle -> empty bottle)
        std::string_view description;
    };

    const FItemDef* FindItemDef(std::string_view id);
    const FItemDef& ResolveItemDef(std::string_view id, std::string_view displayName, EItemKind fallbackKind);
    std::span<const FItemDef> AllItemDefs();

    // True for anything that goes in a weapon slot (firearms and melee).
    inline bool IsWieldable(const FItemDef& definition)
    {
        return (definition.kind == EItemKind::Weapon || definition.kind == EItemKind::Melee) &&
               !definition.weaponDefId.empty();
    }
}
