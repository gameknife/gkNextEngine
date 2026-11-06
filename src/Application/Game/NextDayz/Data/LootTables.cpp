#include "Engine/Common/CoreMinimal.hpp"

#include "LootTables.hpp"

#include "Application/Game/NextDayz/Weapons/WeaponDefs.hpp"

namespace NextDayz
{
    namespace
    {
        // Houses: food, cloth, small tools, the odd hunting gun.
        constexpr std::array<FLootTableEntry, 35> residential = {{
            {"food_can", 10}, {"food_tuna", 6}, {"food_crackers", 7}, {"food_apple", 8}, {"food_chocolate", 5},
            {"drink_soda", 7}, {"water_bottle", 8}, {"water_bottle_empty", 4},
            {"rag", 9, 1, 3}, {"bandage", 4}, {"painkillers", 3},
            {"can_opener", 4}, {"knife", 3}, {"hoodie", 5}, {"work_pants", 4}, {"jacket", 2},
            {"backpack_small", 3}, {"bedroll", 2}, {"lantern", 2}, {"fuel", 2}, {"battery", 3, 1, 2},
            {"firecrackers", 3, 1, 2}, {"map", 2}, {"compass", 1},
            {"ammo_9mm", 2, 6, 16}, {"ammo_12g", 3, 4, 8}, {"shotgun", 1}, {"pistol", 1},
            {"hat_beanie", 2}, {"cap_baseball", 2}, {"hat_ushanka", 1}, {"jacket_puffer", 2}, {"jacket_denim", 3}, {"jeans", 4}, {"backpack_messenger", 2},
        }};
        // Shops, the supermarket, the gas station: the best food, no guns.
        constexpr std::array<FLootTableEntry, 28> commercial = {{
            {"food_can", 12}, {"food_tuna", 8}, {"food_crackers", 9}, {"food_chocolate", 8},
            {"drink_soda", 12}, {"water_bottle", 10}, {"can_opener", 4}, {"battery", 5, 1, 2},
            {"firecrackers", 4, 1, 3}, {"map", 4}, {"compass", 2}, {"rag", 3, 1, 3}, {"bandage", 3},
            {"painkillers", 3}, {"backpack_small", 3}, {"hoodie", 3}, {"fuel", 4}, {"lantern", 2}, {"knife", 2},
            {"hat_beanie", 2}, {"cap_baseball", 2}, {"jacket_puffer", 2}, {"jacket_rain", 2}, {"jeans", 3}, {"vest_utility", 2}, {"vest_rescue", 1}, {"backpack_messenger", 2}, {"backpack_rolltop", 1},
        }};
        constexpr std::array<FLootTableEntry, 10> medical = {{
            {"bandage", 14, 1, 2}, {"medkit", 6}, {"painkillers", 10}, {"rag", 6, 2, 4},
            {"water_bottle", 5}, {"drink_soda", 2}, {"backpack_small", 1},
            {"jacket_rescue", 1}, {"vest_rescue", 1}, {"backpack_medical", 3},
        }};
        // Bases and checkpoints: rifles, ammo, armour and the bigger packs.
        constexpr std::array<FLootTableEntry, 38> military = {{
            {"optic_red_dot", 4}, {"optic_holographic", 3}, {"optic_4x", 2}, {"grip_vertical", 4},
            {"mag_quick", 4}, {"mag_extended", 4}, {"muzzle_suppressor", 2}, {"tactical_flashlight", 3},
            {"signal_flare", 1},
            {"ammo_545", 12, 10, 30}, {"ammo_762", 6, 5, 15}, {"ammo_9mm", 6, 8, 20}, {"ammo_12g", 3, 4, 10},
            {"ak", 3}, {"svd", 1}, {"mosin", 2}, {"pistol", 2},
            {"helmet", 6}, {"vest_military", 3}, {"cargo_pants", 5}, {"jacket", 4},
            {"backpack_military", 2}, {"backpack", 3},
            {"bandage", 5, 1, 2}, {"medkit", 2}, {"food_can", 4}, {"water_bottle", 3},
            {"compass", 4}, {"map", 3}, {"radio", 2}, {"battery", 3, 1, 2}, {"knife", 3},
            {"hat_boonie", 2}, {"helmet_tactical", 3}, {"vest_chest_rig", 3}, {"pants_tactical", 4}, {"pants_snow", 1}, {"backpack_rolltop", 1},
        }};
        constexpr std::array<FLootTableEntry, 23> police = {{
            {"optic_red_dot", 3}, {"optic_holographic", 2}, {"mag_quick", 3}, {"mag_extended", 2}, {"tactical_flashlight", 3},
            {"signal_flare", 1},
            {"pistol", 4}, {"ammo_9mm", 10, 8, 24}, {"ammo_12g", 5, 4, 10}, {"shotgun", 2},
            {"bandage", 4, 1, 2}, {"painkillers", 3}, {"helmet", 2}, {"vest_military", 1},
            {"firecrackers", 2, 1, 2}, {"radio", 1}, {"battery", 3, 1, 2}, {"compass", 1}, {"map", 2},
            {"helmet_tactical", 2}, {"vest_police", 4}, {"pants_tactical", 2}, {"backpack_medical", 1},
        }};
        // Factories, garages, the sawmill: melee weapons, fuel and work clothes.
        constexpr std::array<FLootTableEntry, 19> industrial = {{
            {"crowbar", 8}, {"axe", 4}, {"knife", 3}, {"fuel", 8}, {"work_pants", 6}, {"jacket", 3},
            {"rag", 6, 1, 3}, {"battery", 4, 1, 2}, {"lantern", 4}, {"can_opener", 3}, {"water_bottle", 3},
            {"food_can", 3}, {"firecrackers", 3, 1, 2}, {"backpack_small", 2},
            {"jacket_rain", 2}, {"jacket_rescue", 3}, {"vest_utility", 4}, {"vest_rescue", 3}, {"pants_rain", 3},
        }};
        constexpr std::array<FLootTableEntry, 20> farm = {{
            {"food_apple", 14, 1, 3}, {"food_can", 6}, {"water_bottle", 5}, {"axe", 5}, {"knife", 3},
            {"mosin", 2}, {"shotgun", 3}, {"ammo_12g", 5, 4, 8}, {"ammo_762", 3, 5, 10},
            {"jacket", 4}, {"work_pants", 5}, {"rag", 6, 1, 3}, {"fuel", 4}, {"lantern", 3}, {"bedroll", 3},
            {"cap_baseball", 2}, {"hat_boonie", 3}, {"jacket_denim", 2}, {"vest_hunting", 4}, {"jeans", 3},
        }};
        // Camps, hunting stands and wrecks out in the woods.
        constexpr std::array<FLootTableEntry, 26> wilderness = {{
            {"signal_flare", 1},
            {"mosin", 3}, {"shotgun", 3}, {"ammo_762", 5, 5, 12}, {"ammo_12g", 5, 4, 10},
            {"knife", 6}, {"axe", 3}, {"jacket", 5}, {"backpack", 4}, {"bedroll", 5}, {"lantern", 4},
            {"fuel", 3}, {"food_tuna", 6}, {"food_can", 6}, {"water_bottle", 6}, {"compass", 3}, {"map", 3},
            {"hat_beanie", 2}, {"hat_boonie", 3}, {"hat_ushanka", 2}, {"jacket_puffer", 3}, {"jacket_rain", 3}, {"vest_hunting", 3}, {"pants_rain", 2}, {"pants_snow", 3}, {"backpack_rolltop", 3},
        }};

        uint32_t TotalWeight(std::span<const FLootTableEntry> table, ELootCategory onlyCategory)
        {
            uint32_t total = 0;
            for (const FLootTableEntry& entry : table)
            {
                if (onlyCategory == ELootCategory::Count || CategoryOfItem(entry.itemId) == onlyCategory)
                {
                    total += entry.weight;
                }
            }
            return total;
        }
    }

    std::span<const FLootTableEntry> LootTable(ELootProfile profile)
    {
        switch (profile)
        {
        case ELootProfile::Residential: return residential;
        case ELootProfile::Commercial: return commercial;
        case ELootProfile::Medical: return medical;
        case ELootProfile::Military: return military;
        case ELootProfile::Police: return police;
        case ELootProfile::Industrial: return industrial;
        case ELootProfile::Farm: return farm;
        case ELootProfile::Wilderness: return wilderness;
        default: return residential;
        }
    }

    ELootCategory CategoryOfItem(std::string_view itemId)
    {
        const FItemDef* definition = FindItemDef(itemId);
        if (!definition)
        {
            return ELootCategory::Misc;
        }
        switch (definition->use)
        {
        case EItemUse::Eat:
        case EItemUse::Drink:
            return ELootCategory::FoodWater;
        case EItemUse::Bandage:
        case EItemUse::FirstAid:
        case EItemUse::Painkillers:
            return ELootCategory::Medical;
        default:
            break;
        }
        if (itemId == "water_bottle_empty")
        {
            return ELootCategory::FoodWater;
        }
        switch (definition->kind)
        {
        case EItemKind::Weapon:
        case EItemKind::Melee:
            return ELootCategory::Weapon;
        case EItemKind::Ammo:
            return ELootCategory::Ammo;
        case EItemKind::Clothing:
            return ELootCategory::Clothing;
        default:
            return ELootCategory::Misc;
        }
    }

    FLootStack RollLoot(ELootProfile profile, uint32_t roll, ELootCategory onlyCategory)
    {
        const std::span<const FLootTableEntry> table = LootTable(profile);
        uint32_t total = TotalWeight(table, onlyCategory);
        ELootCategory filter = onlyCategory;
        if (total == 0)
        {
            filter = ELootCategory::Count;   // the profile has none of that category: anything goes
            total = TotalWeight(table, filter);
        }
        uint32_t pick = MixRoll(roll, 1) % std::max(total, 1u);
        const FLootTableEntry* chosen = &table.front();
        for (const FLootTableEntry& entry : table)
        {
            if (filter != ELootCategory::Count && CategoryOfItem(entry.itemId) != filter)
            {
                continue;
            }
            if (pick < entry.weight)
            {
                chosen = &entry;
                break;
            }
            pick -= entry.weight;
        }

        FLootStack stack;
        stack.itemId = std::string(chosen->itemId);
        const uint32_t span = static_cast<uint32_t>(std::max(0, chosen->maxCount - chosen->minCount)) + 1u;
        stack.count = chosen->minCount + static_cast<int>(MixRoll(roll, 2) % span);
        // Guns are found as they were dropped: usually part-loaded, sometimes empty.
        if (const FItemDef* definition = FindItemDef(stack.itemId); definition && definition->kind == EItemKind::Weapon)
        {
            if (const FWeaponDef* weapon = FindWeaponDef(definition->weaponDefId); weapon && weapon->magSize > 0)
            {
                const uint32_t fill = MixRoll(roll, 3) % 100u;
                stack.loadedAmmo = fill < 25u ? 0 : static_cast<int>((fill - 25u) * static_cast<uint32_t>(weapon->magSize) / 75u);
                stack.loadedAmmo = std::clamp(stack.loadedAmmo, 0, weapon->magSize);
            }
        }
        return stack;
    }
}
