#pragma once

#include "Application/Game/NextDayz/Data/LootTables.hpp"
#include "Inventory.hpp"

namespace NextDayz
{
    std::vector<FLootStack> AddLootToInventory(const std::vector<FLootStack>& contents, Inventory& inventory,
                                               FContainerId destination, EEquipSlot equipSlot,
                                               std::optional<FGridPlacement> placement,
                                               std::vector<FLootStack>& left);
}
