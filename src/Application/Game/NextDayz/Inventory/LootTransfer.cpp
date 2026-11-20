#include "Engine/Common/CoreMinimal.hpp"
#include "LootTransfer.hpp"

namespace NextDayz
{
    std::vector<FLootStack> AddLootToInventory(const std::vector<FLootStack>& contents, Inventory& inventory,
                                               FContainerId destination, EEquipSlot equipSlot,
                                               std::optional<FGridPlacement> placement,
                                               std::vector<FLootStack>& left)
    {
        std::vector<FLootStack> taken;
        bool firstStack = true;
        for (const FLootStack& stack : contents)
        {
            const FItemDef* definition = FindItemDef(stack.itemId);
            const bool matchesSlot = destination != kWornContainer ||
                (definition && definition->kind == EItemKind::Clothing && definition->equipSlot == equipSlot);
            int fit = definition && matchesSlot ? inventory.FitCount(stack.itemId, stack.count, destination) : 0;
            if (placement)
                fit = firstStack ? inventory.FitCountAt(stack.itemId, stack.count, destination, *placement) : 0;
            if (fit > 0)
            {
                FItemInstanceId instanceId = 0;
                Inventory transaction = inventory;
                const bool added = placement
                    ? transaction.TryAddAt(stack.itemId, fit, destination, *placement, &instanceId)
                    : transaction.TryAdd(stack.itemId, std::string(definition->displayName), definition->kind, fit,
                                         &instanceId, destination);
                if (added &&
                    (definition->kind != EItemKind::Weapon || transaction.SetWeaponState(instanceId, stack.loadedAmmo, stack.attachments)))
                {
                    inventory = std::move(transaction);
                    taken.push_back({stack.itemId, fit, stack.loadedAmmo, stack.attachments});
                }
                else
                {
                    fit = 0;
                }
            }
            if (fit < stack.count)
            {
                left.push_back({stack.itemId, stack.count - fit, stack.loadedAmmo, stack.attachments});
            }
            firstStack = false;
        }
        return taken;
    }
}
