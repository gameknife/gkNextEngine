#include "Engine/Common/CoreMinimal.hpp"

#include "Inventory.hpp"

namespace NextDayz
{
    namespace
    {
        bool Accepts(const FInventoryContainer& container, const FItemDef& definition)
        {
            if (!container.active)
            {
                return false;
            }
            return !container.weaponOnly || definition.kind == EItemKind::Weapon || definition.kind == EItemKind::Melee;
        }
    }

    Inventory::Inventory()
    {
        Clear();
    }

    void Inventory::Clear()
    {
        instances_.clear();
        containers_.clear();
        legacyItems_.clear();
        equippedClothing_.clear();
        nextInstanceId_ = 1;
        nextContainerId_ = 1;
        containers_.push_back({nextContainerId_++, 0, "Basic shirt pockets", 6, true, false});
        containers_.push_back({nextContainerId_++, 0, "Basic trouser pockets", 4, true, false});
        containers_.push_back({nextContainerId_++, 0, "Primary weapon sling", 10, true, true});
        containers_.push_back({nextContainerId_++, 0, "Secondary weapon sling", 10, true, true});
    }

    FInventoryState Inventory::Capture() const
    {
        return {instances_, containers_, nextInstanceId_, nextContainerId_};
    }

    bool Inventory::Restore(const FInventoryState& state)
    {
        if (state.instances.size() > kMaxSlots || state.containers.size() < 4 ||
            state.containers.size() > 16 || state.nextInstanceId == 0 || state.nextContainerId == kWornContainer)
        {
            return false;
        }
        Inventory restored;
        const auto basic = restored.containers_;
        restored.instances_ = state.instances;
        restored.containers_ = state.containers;
        restored.nextInstanceId_ = state.nextInstanceId;
        restored.nextContainerId_ = state.nextContainerId;
        std::unordered_set<FItemInstanceId> itemIds;
        std::unordered_set<FContainerId> containerIds;
        std::unordered_set<int> wornSlots;
        std::unordered_set<FItemInstanceId> containerOwners;
        for (const auto& container : state.containers)
        {
            if (container.containerId == 0 || container.containerId >= state.nextContainerId ||
                !containerIds.insert(container.containerId).second || !container.active)
            {
                return false;
            }
            if (container.ownerItemInstanceId == 0)
            {
                const auto it = std::find_if(basic.begin(), basic.end(), [&](const auto& expected)
                { return expected.containerId == container.containerId; });
                if (it == basic.end() || it->capacity != container.capacity || it->weaponOnly != container.weaponOnly)
                    return false;
            }
            else
            {
                const auto* owner = restored.FindInstance(container.ownerItemInstanceId);
                const auto* def = owner ? FindItemDef(owner->defId) : nullptr;
                if (!def || owner->containerId != kWornContainer || def->containerCapacity != container.capacity ||
                    container.capacity <= 0 || container.weaponOnly ||
                    !containerOwners.insert(owner->instanceId).second)
                    return false;
            }
        }
        for (const auto& expected : basic)
            if (!containerIds.contains(expected.containerId)) return false;
        for (const auto& item : state.instances)
        {
            const auto* def = FindItemDef(item.defId);
            if (!def || item.instanceId == 0 || item.instanceId >= state.nextInstanceId ||
                !itemIds.insert(item.instanceId).second || item.count <= 0 || item.count > def->maxStack ||
                item.loadedAmmo < 0 || (item.loadedAmmo != 0 && def->kind != EItemKind::Weapon))
                return false;
            if (item.containerId == kWornContainer)
            {
                if (def->kind != EItemKind::Clothing || def->equipSlot == EEquipSlot::None || item.count != 1 ||
                    !wornSlots.insert(static_cast<int>(def->equipSlot)).second ||
                    (def->containerCapacity > 0 && !containerOwners.contains(item.instanceId)))
                    return false;
                restored.equippedClothing_.push_back(item.defId);
            }
            else
            {
                const auto* container = restored.FindContainer(item.containerId);
                if (!container || !Accepts(*container, *def)) return false;
            }
        }
        for (const auto& container : state.containers)
            if (restored.ContainerUsed(container.containerId) > container.capacity) return false;
        restored.RebuildLegacyItems();
        *this = std::move(restored);
        return true;
    }

    int Inventory::InstanceVolume(const FItemInstance& instance)
    {
        const FItemDef* definition = FindItemDef(instance.defId);
        if (!definition || instance.count <= 0)
        {
            return 0;
        }
        const int stackCount = (instance.count + definition->maxStack - 1) / definition->maxStack;
        return stackCount * definition->volumePerStack;
    }

    const FItemInstance* Inventory::FindInstance(FItemInstanceId instanceId) const
    {
        const auto it = std::find_if(instances_.begin(), instances_.end(),
            [instanceId](const FItemInstance& item) { return item.instanceId == instanceId; });
        return it == instances_.end() ? nullptr : &*it;
    }

    FItemInstance* Inventory::FindInstance(FItemInstanceId instanceId)
    {
        return const_cast<FItemInstance*>(std::as_const(*this).FindInstance(instanceId));
    }

    bool Inventory::SetLoadedAmmo(FItemInstanceId instanceId, int loadedAmmo)
    {
        FItemInstance* instance = FindInstance(instanceId);
        const FItemDef* definition = instance ? FindItemDef(instance->defId) : nullptr;
        if (!instance || !definition || definition->kind != EItemKind::Weapon || loadedAmmo < 0)
        {
            return false;
        }
        instance->loadedAmmo = loadedAmmo;
        return true;
    }

    int Inventory::LoadedAmmo(FItemInstanceId instanceId) const
    {
        const FItemInstance* instance = FindInstance(instanceId);
        return instance ? instance->loadedAmmo : -1;
    }

    const FInventoryContainer* Inventory::FindContainer(FContainerId containerId) const
    {
        const auto it = std::find_if(containers_.begin(), containers_.end(),
            [containerId](const FInventoryContainer& container) { return container.containerId == containerId; });
        return it == containers_.end() ? nullptr : &*it;
    }

    const FItemInstance* Inventory::FindFirst(std::string_view defId) const
    {
        const auto it = std::find_if(instances_.begin(), instances_.end(),
            [defId](const FItemInstance& item) { return item.defId == defId; });
        return it == instances_.end() ? nullptr : &*it;
    }

    const FInventoryContainer* Inventory::ContainerOwnedBy(FItemInstanceId instanceId) const
    {
        const auto it = std::find_if(containers_.begin(), containers_.end(),
            [instanceId](const FInventoryContainer& container) { return container.ownerItemInstanceId == instanceId; });
        return it == containers_.end() ? nullptr : &*it;
    }

    int Inventory::ContainerUsed(FContainerId containerId) const
    {
        int used = 0;
        for (const FItemInstance& instance : instances_)
        {
            if (instance.containerId == containerId)
            {
                used += InstanceVolume(instance);
            }
        }
        return used;
    }

    int Inventory::UsedCapacity() const
    {
        int used = 0;
        for (const FInventoryContainer& container : containers_)
        {
            if (container.active && !container.weaponOnly)
            {
                used += ContainerUsed(container.containerId);
            }
        }
        return used;
    }

    int Inventory::TotalCapacity() const
    {
        int capacity = 0;
        for (const FInventoryContainer& container : containers_)
        {
            if (container.active && !container.weaponOnly)
            {
                capacity += container.capacity;
            }
        }
        return capacity;
    }

    FContainerId Inventory::FindContainerWithSpace(const FItemDef& definition, int count, FContainerId excluded) const
    {
        FItemInstance candidate{0, std::string(definition.id), count};
        const int volume = InstanceVolume(candidate);
        for (const FInventoryContainer& container : containers_)
        {
            if (container.containerId == excluded || !Accepts(container, definition))
            {
                continue;
            }
            if (ContainerUsed(container.containerId) + volume <= container.capacity)
            {
                return container.containerId;
            }
        }
        return 0;
    }

    bool Inventory::CanAdd(const std::string& id, int count, FContainerId destination) const
    {
        const FItemDef* definition = FindItemDef(id);
        if (!definition || count <= 0)
        {
            return false;
        }
        Inventory copy = *this;
        return copy.TryAdd(id, std::string(definition->displayName), definition->kind, count, nullptr, destination);
    }

    int Inventory::FitCount(const std::string& id, int count, FContainerId destination) const
    {
        for (int fit = count; fit > 0; --fit)
        {
            if (CanAdd(id, fit, destination))
            {
                return fit;
            }
        }
        return 0;
    }

    bool Inventory::TryAdd(const std::string& id, const std::string& displayName, EItemKind kind, int count,
                           FItemInstanceId* outInstanceId, FContainerId destination)
    {
        if (count <= 0)
        {
            return false;
        }
        Inventory rollback = *this;
        const FItemDef& definition = ResolveItemDef(id, displayName, kind);
        if (destination == kWornContainer)
        {
            if (count != 1 || definition.kind != EItemKind::Clothing || definition.equipSlot == EEquipSlot::None ||
                instances_.size() >= kMaxSlots)
            {
                return false;
            }
            const FItemInstanceId instanceId = nextInstanceId_++;
            instances_.push_back({instanceId, id, 1, 0, 0});
            if (!TryEquip(instanceId))
            {
                *this = std::move(rollback);
                return false;
            }
            if (outInstanceId)
            {
                *outInstanceId = instanceId;
            }
            return true;
        }
        const FInventoryContainer* target = destination ? FindContainer(destination) : nullptr;
        if (destination && (!target || !Accepts(*target, definition)))
        {
            return false;
        }
        int remaining = count;
        FItemInstanceId firstInstanceId = 0;

        if (definition.maxStack > 1)
        {
            for (FItemInstance& instance : instances_)
            {
                if (instance.defId != id || instance.count >= definition.maxStack ||
                    (destination && instance.containerId != destination))
                {
                    continue;
                }
                const int oldVolume = InstanceVolume(instance);
                const int add = std::min(remaining, definition.maxStack - instance.count);
                instance.count += add;
                const int volumeDelta = InstanceVolume(instance) - oldVolume;
                const FInventoryContainer* container = FindContainer(instance.containerId);
                if (!container || ContainerUsed(instance.containerId) > container->capacity)
                {
                    instance.count -= add;
                    continue;
                }
                remaining -= add;
                firstInstanceId = firstInstanceId == 0 ? instance.instanceId : firstInstanceId;
                if (remaining == 0)
                {
                    break;
                }
                (void)volumeDelta;
            }
        }

        while (remaining > 0)
        {
            const int stackCount = std::min(remaining, definition.maxStack);
            const FContainerId containerId = destination ? destination : FindContainerWithSpace(definition, stackCount);
            if (containerId == 0 || instances_.size() >= kMaxSlots)
            {
                *this = std::move(rollback);
                return false;
            }
            if (destination && ContainerUsed(destination) + InstanceVolume({0, id, stackCount}) > target->capacity)
            {
                *this = std::move(rollback);
                return false;
            }
            const FItemInstanceId instanceId = nextInstanceId_++;
            instances_.push_back({instanceId, id, stackCount, containerId, 0});
            firstInstanceId = firstInstanceId == 0 ? instanceId : firstInstanceId;
            remaining -= stackCount;
        }

        if (outInstanceId)
        {
            *outInstanceId = firstInstanceId;
        }
        RebuildLegacyItems();
        return true;
    }

    bool Inventory::TryAddBatch(std::span<const FInventoryAddRequest> requests)
    {
        Inventory transaction = *this;
        for (const FInventoryAddRequest& request : requests)
        {
            if (!transaction.TryAdd(request.id, request.displayName, request.kind, request.count))
            {
                return false;
            }
        }
        *this = std::move(transaction);
        return true;
    }

    void Inventory::Add(const std::string& id, const std::string& displayName, EItemKind kind, int count)
    {
        TryAdd(id, displayName, kind, count);
    }

    bool Inventory::TryMove(FItemInstanceId instanceId, FContainerId destination, int count)
    {
        FItemInstance* source = FindInstance(instanceId);
        const FInventoryContainer* target = FindContainer(destination);
        if (!source || !target || !target->active || target->ownerItemInstanceId == instanceId)
        {
            return false;
        }
        const FItemDef* definition = FindItemDef(source->defId);
        if (!definition || !Accepts(*target, *definition))
        {
            return false;
        }
        const int moveCount = count < 0 ? source->count : count;
        if (moveCount <= 0 || moveCount > source->count)
        {
            return false;
        }
        if (source->containerId == destination)
        {
            return true;
        }
        if (IsWorn(instanceId))
        {
            return TakeOff(instanceId, true, destination);
        }
        if (moveCount < source->count && instances_.size() >= kMaxSlots)
        {
            return false;
        }
        FItemInstance moving = *source;
        moving.count = moveCount;
        moving.containerId = destination;
        if (ContainerUsed(destination) + InstanceVolume(moving) > target->capacity)
        {
            return false;
        }
        if (moveCount == source->count)
        {
            source->containerId = destination;
        }
        else
        {
            source->count -= moveCount;
            moving.instanceId = nextInstanceId_++;
            instances_.push_back(std::move(moving));
        }
        RebuildLegacyItems();
        return true;
    }

    bool Inventory::TrySplit(FItemInstanceId instanceId, int count, FItemInstanceId& outSplitId)
    {
        FItemInstance* source = FindInstance(instanceId);
        if (!source || count <= 0 || count >= source->count || instances_.size() >= kMaxSlots)
        {
            return false;
        }
        const FItemDef* definition = FindItemDef(source->defId);
        if (!definition || definition->maxStack <= 1)
        {
            return false;
        }
        FItemInstance split = *source;
        split.instanceId = nextInstanceId_++;
        split.count = count;
        source->count -= count;
        const FInventoryContainer* container = FindContainer(source->containerId);
        if (!container || ContainerUsed(source->containerId) + InstanceVolume(split) > container->capacity)
        {
            source->count += count;
            --nextInstanceId_;
            return false;
        }
        outSplitId = split.instanceId;
        instances_.push_back(std::move(split));
        RebuildLegacyItems();
        return true;
    }

    bool Inventory::TryEquip(FItemInstanceId instanceId)
    {
        const FItemInstance* instance = FindInstance(instanceId);
        const FItemDef* definition = instance ? FindItemDef(instance->defId) : nullptr;
        if (!definition || definition->kind != EItemKind::Clothing || definition->equipSlot == EEquipSlot::None ||
            IsWorn(instanceId))
        {
            return false;
        }
        Inventory transaction = *this;
        const FItemInstanceId current = transaction.WornInSlot(definition->equipSlot);
        FItemInstance* item = transaction.FindInstance(instanceId);
        if (!item || item->count != 1)
        {
            return false;
        }
        item->containerId = kWornContainer;
        transaction.equippedClothing_.push_back(item->defId);
        if (definition->containerCapacity > 0)
        {
            transaction.containers_.push_back({transaction.nextContainerId_++, instanceId,
                                               std::string(definition->displayName), definition->containerCapacity,
                                               true, false});
        }
        // Free the incoming item's cargo space and provide its new container
        // before evacuating the old gear, so a full inventory can still swap.
        if (current != 0 && !transaction.TryUnequip(current))
        {
            return false;
        }
        transaction.RebuildLegacyItems();
        *this = std::move(transaction);
        return true;
    }

    namespace
    {
        // Moves everything out of `removedId` into the other containers.
        bool EvacuateContainer(Inventory& inventory, FContainerId removedId,
                               const std::function<FContainerId(const FItemDef&, int)>& findSpace)
        {
            std::vector<FItemInstanceId> contents;
            for (const FItemInstance& item : inventory.Instances())
            {
                if (item.containerId == removedId)
                {
                    contents.push_back(item.instanceId);
                }
            }
            for (FItemInstanceId contentId : contents)
            {
                const FItemInstance* content = inventory.FindInstance(contentId);
                const FItemDef* contentDef = content ? FindItemDef(content->defId) : nullptr;
                const FContainerId destination = contentDef ? findSpace(*contentDef, content->count) : 0;
                if (destination == 0 || !inventory.TryMove(contentId, destination))
                {
                    return false;
                }
            }
            return true;
        }
    }

    bool Inventory::TakeOff(FItemInstanceId instanceId, bool intoCargo, FContainerId destination)
    {
        if (!IsWorn(instanceId))
        {
            return false;
        }
        Inventory transaction = *this;
        const FItemDef* definition = FindItemDef(transaction.FindInstance(instanceId)->defId);
        if (!definition)
        {
            return false;
        }
        if (destination)
        {
            const FInventoryContainer* target = transaction.FindContainer(destination);
            if (!intoCargo || !target || target->ownerItemInstanceId == instanceId || !Accepts(*target, *definition) ||
                transaction.ContainerUsed(destination) + InstanceVolume(*transaction.FindInstance(instanceId)) >
                    target->capacity)
            {
                return false;
            }
            // Reserve the selected cargo space before evacuating the item's contents.
            transaction.FindInstance(instanceId)->containerId = destination;
        }
        FContainerId removedId = 0;
        if (const FInventoryContainer* owned = transaction.ContainerOwnedBy(instanceId))
        {
            removedId = owned->containerId;
            const auto findSpace = [&transaction, removedId](const FItemDef& itemDef, int count)
            {
                return transaction.FindContainerWithSpace(itemDef, count, removedId);
            };
            if (!EvacuateContainer(transaction, removedId, findSpace))
            {
                return false;
            }
            std::erase_if(transaction.containers_,
                [removedId](const FInventoryContainer& container) { return container.containerId == removedId; });
        }
        if (intoCargo && destination == 0)
        {
            const FContainerId destination = transaction.FindContainerWithSpace(*definition, 1, removedId);
            if (destination == 0)
            {
                return false;
            }
            transaction.FindInstance(instanceId)->containerId = destination;
        }
        const auto worn = std::find(transaction.equippedClothing_.begin(), transaction.equippedClothing_.end(),
                                    std::string(definition->id));
        if (worn != transaction.equippedClothing_.end())
        {
            transaction.equippedClothing_.erase(worn);
        }
        transaction.RebuildLegacyItems();
        *this = std::move(transaction);
        return true;
    }

    bool Inventory::TryUnequip(FItemInstanceId instanceId)
    {
        return TakeOff(instanceId, true);
    }

    std::optional<FItemInstance> Inventory::TryRemove(FItemInstanceId instanceId, int count)
    {
        const FItemInstance* instance = FindInstance(instanceId);
        if (!instance)
        {
            return std::nullopt;
        }
        const int removeCount = count < 0 ? instance->count : count;
        if (removeCount <= 0 || removeCount > instance->count)
        {
            return std::nullopt;
        }
        Inventory transaction = *this;
        // Worn: off the body and out of the inventory; its contents stay behind.
        if (transaction.IsWorn(instanceId) && !transaction.TakeOff(instanceId, false))
        {
            return std::nullopt;
        }
        FItemInstance* item = transaction.FindInstance(instanceId);
        FItemInstance removed = *item;
        removed.count = removeCount;
        item->count -= removeCount;
        transaction.RemoveEmptyInstances();
        transaction.RebuildLegacyItems();
        *this = std::move(transaction);
        return removed;
    }

    bool Inventory::TryDrop(FItemInstanceId instanceId, int count)
    {
        return TryRemove(instanceId, count).has_value();
    }

    bool Inventory::TryConsumeInstance(FItemInstanceId instanceId, int count)
    {
        FItemInstance* instance = FindInstance(instanceId);
        if (!instance || count <= 0 || instance->count < count || IsWorn(instanceId))
        {
            return false;
        }
        instance->count -= count;
        RemoveEmptyInstances();
        RebuildLegacyItems();
        return true;
    }

    int Inventory::CountOf(const std::string& id) const
    {
        int total = 0;
        for (const FItemInstance& instance : instances_)
        {
            if (instance.defId == id)
            {
                total += instance.count;
            }
        }
        return total;
    }

    int Inventory::Consume(const std::string& id, int count)
    {
        int remaining = count;
        for (FItemInstance& instance : instances_)
        {
            if (instance.defId == id && remaining > 0)
            {
                const int take = std::min(remaining, instance.count);
                instance.count -= take;
                remaining -= take;
            }
        }
        RemoveEmptyInstances();
        RebuildLegacyItems();
        return count - remaining;
    }

    bool Inventory::IsWorn(FItemInstanceId instanceId) const
    {
        const FItemInstance* instance = FindInstance(instanceId);
        return instance && instance->containerId == kWornContainer;
    }

    FItemInstanceId Inventory::WornInSlot(EEquipSlot slot) const
    {
        for (const FItemInstance& instance : instances_)
        {
            const FItemDef* definition = instance.containerId == kWornContainer ? FindItemDef(instance.defId) : nullptr;
            if (definition && definition->equipSlot == slot)
            {
                return instance.instanceId;
            }
        }
        return 0;
    }

    float Inventory::Armor() const
    {
        float armor = 0.0f;
        for (const FItemInstance& instance : instances_)
        {
            const FItemDef* definition = instance.containerId == kWornContainer ? FindItemDef(instance.defId) : nullptr;
            armor += definition ? definition->armor : 0.0f;
        }
        return std::min(armor, 0.6f);
    }

    float Inventory::Warmth() const
    {
        float warmth = 0.0f;
        for (const FItemInstance& instance : instances_)
        {
            const FItemDef* definition = instance.containerId == kWornContainer ? FindItemDef(instance.defId) : nullptr;
            warmth += definition ? definition->warmth : 0.0f;
        }
        return std::min(warmth, 1.0f);
    }

    bool Inventory::HasTool(EItemTool tool) const
    {
        return std::any_of(instances_.begin(), instances_.end(), [tool](const FItemInstance& instance)
        {
            const FItemDef* definition = FindItemDef(instance.defId);
            return definition && definition->tool == tool;
        });
    }

    bool Inventory::CanOpenCans() const
    {
        return HasTool(EItemTool::CanOpener) ||
               std::any_of(instances_.begin(), instances_.end(), [](const FItemInstance& instance)
               {
                   const FItemDef* definition = FindItemDef(instance.defId);
                   return definition && definition->toolCanOpener;
               });
    }

    bool Inventory::IsClothingEquipped(const std::string& id) const
    {
        return std::find(equippedClothing_.begin(), equippedClothing_.end(), id) != equippedClothing_.end();
    }

    void Inventory::SetClothingEquipped(const std::string& id, bool equipped)
    {
        const auto it = std::find_if(instances_.begin(), instances_.end(),
            [&id, equipped](const FItemInstance& instance)
            {
                return instance.defId == id && (instance.containerId == kWornContainer) != equipped;
            });
        if (it == instances_.end())
        {
            return;
        }
        if (equipped)
        {
            TryEquip(it->instanceId);
        }
        else
        {
            TryUnequip(it->instanceId);
        }
    }

    void Inventory::RemoveEmptyInstances()
    {
        instances_.erase(std::remove_if(instances_.begin(), instances_.end(),
            [](const FItemInstance& instance) { return instance.count <= 0; }), instances_.end());
    }

    void Inventory::RebuildLegacyItems()
    {
        legacyItems_.clear();
        for (const FItemInstance& instance : instances_)
        {
            const FItemDef* definition = FindItemDef(instance.defId);
            const std::string displayName = definition ? std::string(definition->displayName) : instance.defId;
            const EItemKind kind = definition ? definition->kind : EItemKind::Misc;
            auto stack = std::find_if(legacyItems_.begin(), legacyItems_.end(),
                [&instance, kind](const FItemStack& item)
                {
                    return item.id == instance.defId && kind != EItemKind::Weapon && kind != EItemKind::Clothing;
                });
            if (stack != legacyItems_.end())
            {
                stack->count += instance.count;
            }
            else
            {
                legacyItems_.push_back({instance.defId, displayName, kind, instance.count});
            }
        }
    }
}
