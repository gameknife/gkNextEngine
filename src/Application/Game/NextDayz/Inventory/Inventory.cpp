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
        containers_.push_back({nextContainerId_++, 0, "Basic shirt pockets", {6, 1}, true, false});
        containers_.push_back({nextContainerId_++, 0, "Basic trouser pockets", {2, 2}, true, false});
        containers_.push_back({nextContainerId_++, 0, "Primary weapon sling", {4, 2}, true, true});
        containers_.push_back({nextContainerId_++, 0, "Secondary weapon sling", {4, 2}, true, true});
    }

    FInventoryState Inventory::Capture() const
    {
        return {instances_, containers_, nextInstanceId_, nextContainerId_};
    }

    bool Inventory::Restore(const FInventoryState& state)
    {
        return RestoreInternal(state, false, nullptr);
    }

    bool Inventory::RestoreLegacy(const FInventoryState& state, std::vector<FItemInstance>& overflow)
    {
        return RestoreInternal(state, true, &overflow);
    }

    bool Inventory::RestoreInternal(const FInventoryState& state, bool legacy, std::vector<FItemInstance>* overflow)
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
                !containerIds.insert(container.containerId).second || !container.active || !container.size.IsValid())
            {
                return false;
            }
            if (container.ownerItemInstanceId == 0)
            {
                const auto it = std::find_if(basic.begin(), basic.end(), [&](const auto& expected)
                { return expected.containerId == container.containerId; });
                if (it == basic.end() || it->size != container.size || it->weaponOnly != container.weaponOnly)
                    return false;
            }
            else
            {
                const auto* owner = restored.FindInstance(container.ownerItemInstanceId);
                const auto* def = owner ? FindItemDef(owner->defId) : nullptr;
                if (!def || owner->containerId != kWornContainer || def->containerSize != container.size ||
                    container.Capacity() <= 0 || container.weaponOnly ||
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
            if (!ValidAttachments(item.defId, item.attachments)) return false;
            if (def->kind == EItemKind::Weapon && item.loadedAmmo > WeaponMagazineCapacity(item.defId, item.attachments)) return false;
            if (item.containerId == kWornContainer)
            {
                if (def->kind != EItemKind::Clothing || def->equipSlot == EEquipSlot::None || item.count != 1 ||
                    !wornSlots.insert(static_cast<int>(def->equipSlot)).second ||
                    (def->containerSize.Area() > 0 && !containerOwners.contains(item.instanceId)))
                    return false;
                restored.equippedClothing_.push_back(item.defId);
            }
            else
            {
                const auto* container = restored.FindContainer(item.containerId);
                if (!container || !Accepts(*container, *def)) return false;
            }
        }
        std::vector<FItemInstance> surplus;
        if (legacy)
        {
            std::vector<FItemInstance> cargo;
            std::erase_if(restored.instances_, [&](const FItemInstance& item)
            {
                if (item.containerId == kWornContainer) return false;
                cargo.push_back(item);
                return true;
            });
            std::stable_sort(cargo.begin(), cargo.end(), [](const auto& a, const auto& b)
            { return InstanceArea(a) > InstanceArea(b); });
            for (auto item : cargo)
            {
                auto placement = restored.FindPlacement(item.defId, item.containerId);
                if (!placement)
                {
                    item.containerId = restored.FindContainerWithSpace(*FindItemDef(item.defId), item.count);
                    placement = restored.FindPlacement(item.defId, item.containerId);
                }
                if (!placement) { surplus.push_back(item); continue; }
                item.placement = *placement;
                restored.instances_.push_back(std::move(item));
            }
        }
        else
        {
            for (const auto& item : restored.instances_)
                if (item.containerId != kWornContainer &&
                    !restored.CanPlaceItem(item.defId, item.containerId, item.placement, item.instanceId)) return false;
        }
        if (overflow) *overflow = std::move(surplus);
        restored.RebuildLegacyItems();
        *this = std::move(restored);
        return true;
    }

    bool Inventory::SetWeaponState(FItemInstanceId instanceId, int loadedAmmo, const FWeaponAttachments& attachments)
    {
        auto* item = FindInstance(instanceId);
        const auto* def = item ? FindItemDef(item->defId) : nullptr;
        if (!def || def->kind != EItemKind::Weapon || loadedAmmo < 0 ||
            !ValidAttachments(item->defId, attachments) || loadedAmmo > WeaponMagazineCapacity(item->defId, attachments)) return false;
        item->loadedAmmo = loadedAmmo;
        item->attachments = attachments;
        return true;
    }

    bool Inventory::TryAttach(FItemInstanceId weaponId, FItemInstanceId attachmentId, std::string* error)
    {
        const auto fail = [&](const char* message) { if (error) *error = message; return false; };
        const auto* weapon = FindInstance(weaponId);
        const auto* part = FindInstance(attachmentId);
        const auto* def = part ? FindAttachmentDef(part->defId) : nullptr;
        if (!weapon || !def || !SupportsAttachment(weapon->defId, def->type)) return fail("This attachment does not fit that weapon.");
        Inventory transaction = *this;
        const size_t slot = static_cast<size_t>(def->slot);
        const auto old = weapon->attachments[slot];
        if (!transaction.TryConsumeInstance(attachmentId)) return fail("Attachment is no longer available.");
        transaction.FindInstance(weaponId)->attachments[slot] = def->type;
        if (const auto* previous = FindAttachmentDef(old);
            previous && !transaction.TryAdd(std::string(previous->id), std::string(previous->displayName), EItemKind::Attachment, 1))
            return fail("Not enough space for the removed attachment.");
        auto* gun = transaction.FindInstance(weaponId);
        const int capacity = WeaponMagazineCapacity(gun->defId, gun->attachments);
        const int excess = std::max(0, gun->loadedAmmo - capacity);
        const auto* gunDef = FindWeaponDef(gun->defId);
        if (excess && !transaction.TryAdd(std::string(AmmoItemId(gunDef->ammo)), std::string(AmmoDisplayName(gunDef->ammo)), EItemKind::Ammo, excess))
            return fail("Not enough space for the excess ammunition.");
        transaction.FindInstance(weaponId)->loadedAmmo -= excess;
        *this = std::move(transaction);
        if (error) error->clear();
        return true;
    }

    bool Inventory::TryDetach(FItemInstanceId weaponId, EAttachmentSlot slot, std::string* error)
    {
        const auto fail = [&](const char* message) { if (error) *error = message; return false; };
        const auto* weapon = FindInstance(weaponId);
        if (!weapon || static_cast<size_t>(slot) >= kAttachmentSlotCount) return fail("No attachment in that slot.");
        const auto* part = FindAttachmentDef(AttachmentAt(weapon->attachments, slot));
        if (!part) return fail("No attachment in that slot.");
        Inventory transaction = *this;
        transaction.FindInstance(weaponId)->attachments[static_cast<size_t>(slot)] = EWeaponAttachment::None;
        if (!transaction.TryAdd(std::string(part->id), std::string(part->displayName), EItemKind::Attachment, 1))
            return fail("Not enough space for the removed attachment.");
        auto* gun = transaction.FindInstance(weaponId);
        const int excess = std::max(0, gun->loadedAmmo - WeaponMagazineCapacity(gun->defId, gun->attachments));
        const auto* gunDef = FindWeaponDef(gun->defId);
        if (excess && !transaction.TryAdd(std::string(AmmoItemId(gunDef->ammo)), std::string(AmmoDisplayName(gunDef->ammo)), EItemKind::Ammo, excess))
            return fail("Not enough space for the excess ammunition.");
        transaction.FindInstance(weaponId)->loadedAmmo -= excess;
        *this = std::move(transaction);
        if (error) error->clear();
        return true;
    }

    int Inventory::InstanceArea(const FItemInstance& instance)
    {
        const auto* definition = FindItemDef(instance.defId);
        return definition ? definition->size.Area() : 1;
    }

    FGridSize Inventory::InstanceSize(const FItemInstance& instance)
    {
        const auto* definition = FindItemDef(instance.defId);
        const FGridSize size = definition ? definition->size : FGridSize{};
        return instance.placement.rotated ? size.Rotated() : size;
    }

    bool Inventory::CanPlaceItem(std::string_view id, FContainerId destination, FGridPlacement placement,
                                 FItemInstanceId ignored) const
    {
        const auto* definition = FindItemDef(id);
        const auto* container = FindContainer(destination);
        if (!definition || !container || !Accepts(*container, *definition) ||
            (ignored && container->ownerItemInstanceId == ignored)) return false;
        const FGridSize size = placement.rotated ? definition->size.Rotated() : definition->size;
        if (!size.IsValid() || placement.x < 0 || placement.y < 0 ||
            placement.x > container->size.width - size.width || placement.y > container->size.height - size.height)
            return false;
        for (const auto& item : instances_)
        {
            if (item.instanceId == ignored || item.containerId != destination) continue;
            const auto other = InstanceSize(item);
            if (placement.x < item.placement.x + other.width && placement.x + size.width > item.placement.x &&
                placement.y < item.placement.y + other.height && placement.y + size.height > item.placement.y)
                return false;
        }
        return true;
    }

    std::optional<FGridPlacement> Inventory::FindPlacement(std::string_view id, FContainerId destination,
                                                          FItemInstanceId ignored) const
    {
        const auto* container = FindContainer(destination);
        if (!container) return std::nullopt;
        for (bool rotated : {false, true})
            for (int y = 0; y < container->size.height; ++y)
                for (int x = 0; x < container->size.width; ++x)
                    if (CanPlaceItem(id, destination, {x, y, rotated}, ignored)) return FGridPlacement{x, y, rotated};
        return std::nullopt;
    }

    bool Inventory::CanPlace(FItemInstanceId instanceId, FContainerId destination, FGridPlacement placement) const
    {
        const auto* item = FindInstance(instanceId);
        if (!item || !CanPlaceItem(item->defId, destination, placement, instanceId)) return false;
        if (!IsWorn(instanceId)) return true;
        Inventory transaction = *this;
        return transaction.TakeOff(instanceId, true, destination, placement);
    }

    bool Inventory::TryPlace(FItemInstanceId instanceId, FContainerId destination, FGridPlacement placement)
    {
        const auto* item = FindInstance(instanceId);
        if (!item || !CanPlaceItem(item->defId, destination, placement, instanceId)) return false;
        if (IsWorn(instanceId)) return TakeOff(instanceId, true, destination, placement);
        auto* moving = FindInstance(instanceId);
        moving->containerId = destination;
        moving->placement = placement;
        return true;
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
                used += InstanceArea(instance);
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
                capacity += container.Capacity();
            }
        }
        return capacity;
    }

    FContainerId Inventory::FindContainerWithSpace(const FItemDef& definition, int count, FContainerId excluded) const
    {
        if (count <= 0) return 0;
        // Carry guns and melee on the slings first, keeping pockets available for supplies.
        for (bool weapons : {true, false})
            for (const auto& container : containers_)
                if (container.weaponOnly == weapons && container.containerId != excluded &&
                    Accepts(container, definition) && FindPlacement(definition.id, container.containerId))
                    return container.containerId;
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
        const auto* definition = FindItemDef(id);
        if (!definition || count <= 0) return 0;
        int low = 0;
        int high = std::min(count, static_cast<int>(kMaxSlots) * definition->maxStack);
        while (low < high)
        {
            const int mid = low + (high - low + 1) / 2;
            if (CanAdd(id, mid, destination)) low = mid;
            else high = mid - 1;
        }
        return low;
    }

    bool Inventory::TryAdd(const std::string& id, const std::string& displayName, EItemKind kind, int count,
                           FItemInstanceId* outInstanceId, FContainerId destination)
    {
        const auto* known = FindItemDef(id);
        if (!known || count <= 0 || count > static_cast<int>(kMaxSlots) * known->maxStack) return false;
        const FItemDef& definition = *known;
        Inventory transaction = *this;
        FItemInstanceId first = 0;
        if (destination == kWornContainer)
        {
            if (count != 1 || definition.kind != EItemKind::Clothing || definition.equipSlot == EEquipSlot::None ||
                transaction.instances_.size() >= kMaxSlots) return false;
            first = transaction.nextInstanceId_++;
            transaction.instances_.push_back({first, id, 1});
            if (!transaction.TryEquip(first)) return false;
        }
        else
        {
            if (destination)
            {
                const auto* target = transaction.FindContainer(destination);
                if (!target || !Accepts(*target, definition)) return false;
            }
            int remaining = count;
            for (auto& item : transaction.instances_)
            {
                if (item.defId != id || item.containerId == kWornContainer ||
                    (destination && item.containerId != destination)) continue;
                const int add = std::min(remaining, definition.maxStack - item.count);
                if (add <= 0) continue;
                item.count += add;
                remaining -= add;
                if (!first) first = item.instanceId;
                if (!remaining) break;
            }
            while (remaining > 0)
            {
                const int stack = std::min(remaining, definition.maxStack);
                const auto target = destination ? destination : transaction.FindContainerWithSpace(definition, stack);
                const auto placement = transaction.FindPlacement(id, target);
                if (!placement || transaction.instances_.size() >= kMaxSlots) return false;
                FItemInstance item{transaction.nextInstanceId_++, id, stack, target};
                item.placement = *placement;
                if (!first) first = item.instanceId;
                transaction.instances_.push_back(std::move(item));
                remaining -= stack;
            }
        }
        transaction.RebuildLegacyItems();
        *this = std::move(transaction);
        if (outInstanceId) *outInstanceId = first;
        (void)displayName;
        (void)kind;
        return true;
    }

    const FItemInstance* Inventory::FindStackAt(std::string_view id, FContainerId destination, int x, int y,
                                               FItemInstanceId ignored) const
    {
        const auto* def = FindItemDef(id);
        if (!def || def->maxStack <= 1 || x < 0 || y < 0) return nullptr;
        for (const auto& item : instances_)
        {
            if (item.instanceId == ignored || item.containerId != destination || item.defId != id) continue;
            const auto size = InstanceSize(item);
            if (x >= item.placement.x && x < item.placement.x + size.width &&
                y >= item.placement.y && y < item.placement.y + size.height) return &item;
        }
        return nullptr;
    }

    int Inventory::FitCountAt(std::string_view id, int count, FContainerId destination, FGridPlacement placement) const
    {
        const auto* def = FindItemDef(id);
        const auto* container = FindContainer(destination);
        if (!def || count <= 0 || !container || !Accepts(*container, *def)) return 0;
        if (const auto* stack = FindStackAt(id, destination, placement.x, placement.y))
            return std::max(0, std::min(count, def->maxStack - stack->count));
        return instances_.size() < kMaxSlots && CanPlaceItem(id, destination, placement)
            ? std::min(count, def->maxStack) : 0;
    }

    bool Inventory::TryAddAt(const std::string& id, int count, FContainerId destination, FGridPlacement placement,
                             FItemInstanceId* outInstanceId)
    {
        if (count <= 0 || FitCountAt(id, count, destination, placement) < count) return false;
        if (const auto* stack = FindStackAt(id, destination, placement.x, placement.y))
        {
            const auto instanceId = stack->instanceId;
            FindInstance(instanceId)->count += count;
            RebuildLegacyItems();
            if (outInstanceId) *outInstanceId = instanceId;
            return true;
        }
        FItemInstance item{nextInstanceId_++, id, count, destination};
        item.placement = placement;
        instances_.push_back(item);
        RebuildLegacyItems();
        if (outInstanceId) *outInstanceId = item.instanceId;
        return true;
    }

    bool Inventory::TryMerge(FItemInstanceId sourceId, FItemInstanceId targetId)
    {
        auto* source = FindInstance(sourceId);
        auto* target = FindInstance(targetId);
        if (!source || !target || sourceId == targetId || source->defId != target->defId ||
            IsWorn(sourceId) || IsWorn(targetId)) return false;
        const auto* def = FindItemDef(source->defId);
        if (!def || def->maxStack <= 1) return false;
        const int moved = std::min(source->count, def->maxStack - target->count);
        if (moved <= 0) return false;
        target->count += moved;
        source->count -= moved;
        RemoveEmptyInstances();
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
        const auto* source = FindInstance(instanceId);
        if (!source) return false;
        const int movingCount = count < 0 ? source->count : count;
        if (movingCount <= 0 || movingCount > source->count) return false;
        const auto* def = FindItemDef(source->defId);
        if (movingCount == source->count && source->containerId != destination && def && def->maxStack > 1)
        {
            Inventory transaction = *this;
            std::vector<FItemInstanceId> targets;
            for (const auto& item : instances_)
                if (item.containerId == destination && item.defId == source->defId && item.count < def->maxStack)
                    targets.push_back(item.instanceId);
            bool merged = false;
            for (const auto target : targets)
                merged |= transaction.TryMerge(instanceId, target);
            if (merged)
            {
                if (transaction.FindInstance(instanceId) && !transaction.TryMove(instanceId, destination)) return false;
                *this = std::move(transaction);
                return true;
            }
        }
        if (movingCount == source->count)
        {
            const auto placement = FindPlacement(source->defId, destination, instanceId);
            if (!placement) return false;
            if (source->containerId == destination) return true;
            return TryPlace(instanceId, destination, *placement);
        }
        if (IsWorn(instanceId) || instances_.size() >= kMaxSlots) return false;
        const auto placement = FindPlacement(source->defId, destination);
        if (!placement) return false;
        FItemInstance moving = *source;
        moving.instanceId = nextInstanceId_++;
        moving.count = movingCount;
        moving.containerId = destination;
        moving.placement = *placement;
        FindInstance(instanceId)->count -= movingCount;
        instances_.push_back(std::move(moving));
        RebuildLegacyItems();
        return true;
    }

    bool Inventory::TrySplit(FItemInstanceId instanceId, int count, FItemInstanceId& outSplitId)
    {
        const auto* source = FindInstance(instanceId);
        if (!source || IsWorn(instanceId) || count <= 0 || count >= source->count || instances_.size() >= kMaxSlots)
            return false;
        FItemInstance split = *source;
        auto placement = FindPlacement(split.defId, split.containerId);
        if (!placement)
        {
            split.containerId = FindContainerWithSpace(*FindItemDef(split.defId), count);
            placement = FindPlacement(split.defId, split.containerId);
        }
        if (!placement) return false;
        split.placement = *placement;
        split.instanceId = nextInstanceId_++;
        split.count = count;
        FindInstance(instanceId)->count -= count;
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
        item->placement = {};
        transaction.equippedClothing_.push_back(item->defId);
        if (definition->containerSize.Area() > 0)
        {
            transaction.containers_.push_back({transaction.nextContainerId_++, instanceId,
                                               std::string(definition->displayName), definition->containerSize,
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

    bool Inventory::TakeOff(FItemInstanceId instanceId, bool intoCargo, FContainerId destination,
                             std::optional<FGridPlacement> placement)
    {
        if (!IsWorn(instanceId)) return false;
        Inventory transaction = *this;
        const auto* definition = FindItemDef(transaction.FindInstance(instanceId)->defId);
        if (!definition) return false;
        if (destination)
        {
            if (!intoCargo) return false;
            if (!placement) placement = transaction.FindPlacement(definition->id, destination, instanceId);
            if (!placement || !transaction.CanPlaceItem(definition->id, destination, *placement, instanceId)) return false;
            // Reserve the exact rectangle before evacuating the item's container.
            auto* item = transaction.FindInstance(instanceId);
            item->containerId = destination;
            item->placement = *placement;
        }
        FContainerId removedId = 0;
        if (const auto* owned = transaction.ContainerOwnedBy(instanceId))
        {
            removedId = owned->containerId;
            const auto findSpace = [&transaction, removedId](const FItemDef& itemDef, int count)
            { return transaction.FindContainerWithSpace(itemDef, count, removedId); };
            if (!EvacuateContainer(transaction, removedId, findSpace)) return false;
            std::erase_if(transaction.containers_, [removedId](const auto& container)
            { return container.containerId == removedId; });
        }
        if (intoCargo && !destination)
        {
            destination = transaction.FindContainerWithSpace(*definition, 1, removedId);
            const auto chosen = transaction.FindPlacement(definition->id, destination, instanceId);
            if (!chosen) return false;
            auto* item = transaction.FindInstance(instanceId);
            item->containerId = destination;
            item->placement = *chosen;
        }
        std::erase(transaction.equippedClothing_, std::string(definition->id));
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
