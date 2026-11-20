#pragma once

#include "Engine/Common/CoreMinimal.hpp"
#include "../Data/WorldProfiles.hpp"

namespace NextDayz
{
    inline std::string LootKey(std::string_view type, const glm::vec3& position)
    {
        return fmt::format("{}:{:.3f}:{:.3f}:{:.3f}", type, position.x, position.y, position.z);
    }

    inline std::string SpotLootKey(ELootProfile profile, std::string_view item, const glm::vec3& position)
    {
        return item.empty() ? LootKey(fmt::format("spot:{}", static_cast<int>(profile)), position)
                            : LootKey(fmt::format("item:{}", item), position);
    }

    inline void NumberLootKey(std::string& key, std::unordered_map<std::string, int>& occurrences)
    {
        const int ordinal = occurrences[key]++;
        key += fmt::format(":{}", ordinal);
    }

    // Map nodes the artist placed as specific loot. Fixed items are always
    // there; rolled ones come from a profile table each time it restocks.
    struct FAuthoredLoot
    {
        std::string_view displayName;
        std::array<std::string_view, 2> fixed{};
        ELootProfile rollProfile = ELootProfile::Military;
        ELootCategory rollCategory = ELootCategory::Count;
        int rolls = 0;
    };

    inline const std::unordered_map<std::string, FAuthoredLoot>& AuthoredTable()
    {
        using P = ELootProfile;
        using C = ELootCategory;
        static const std::unordered_map<std::string, FAuthoredLoot> table = {
            {"cw_wpn_ak",            {"AK-74", {"ak"}}},
            {"cw_wpn_svd",           {"SVD", {"svd"}}},
            {"cw_wpn_mosin",         {"Mosin", {"mosin"}}},
            {"cw_wpn_shotgun",       {"Shotgun", {"shotgun"}}},
            {"cw_wpn_pistol",        {"Makarov", {"pistol"}}},
            {"cw_wpn_crate",         {"Weapon Crate", {"ak"}, P::Military, C::Ammo, 2}},
            {"cw_prop_crate_ammo",   {"Ammo Crate", {}, P::Military, C::Ammo, 2}},
            {"cw_item_ammobox",      {"Ammo Box", {}, P::Military, C::Ammo, 1}},
            {"cw_item_helmet",       {"Helmet", {"helmet"}}},
            {"cw_item_backpack",     {"Backpack", {"backpack"}}},
            {"cw_item_medkit",       {"First Aid Kit", {"medkit"}}},
            {"cw_item_can",          {"Canned Beans", {"food_can"}}},
            {"cw_item_jerrycan",     {"Fuel Canister", {"fuel"}}},
            {"cw_item_radio",        {"Field Radio", {"radio", "battery"}}},
            {"cw_item_crate_supply", {"Supply Crate", {}, P::Commercial, C::Count, 3}},
            {"cw_item_bedroll",      {"Bedroll", {"bedroll"}}},
            {"cw_item_lantern",      {"Kerosene Lantern", {"lantern"}}},
            {"cw_item_water_bottle", {"Water Bottle", {"water_bottle"}}},
            {"cw_item_axe",          {"Fire Axe", {"axe"}}},
            {"cw_item_crowbar",      {"Crowbar", {"crowbar"}}},
            {"cw_item_knife",        {"Hunting Knife", {"knife"}}},
            {"cw_item_vest",         {"Military Vest", {"vest_military"}}},
            {"cw_item_jacket",       {"Field Jacket", {"jacket"}}},
            {"cw_item_medical_bag",  {"Medical Bag", {}, P::Medical, C::Medical, 3}},
            {"cw_item_toolbox",      {"Toolbox", {}, P::Industrial, C::Count, 2}},
        };
        return table;
    }
}
