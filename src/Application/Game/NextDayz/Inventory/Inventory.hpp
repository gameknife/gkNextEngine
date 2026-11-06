#pragma once

// ============================================================================
// Inventory.hpp - Capacity-bound inventory. Every item is an instance that
// lives either in a cargo container (pockets, the weapon slings, and whatever
// the worn clothing provides) or is worn in an equipment slot. Worn items do
// not take cargo space. Every Try* is a transaction: it fully succeeds or
// leaves the inventory untouched.
// ============================================================================

#include "Engine/Common/CoreMinimal.hpp"

#include "Application/Game/NextDayz/Data/ItemDefs.hpp"
#include "Application/Game/NextDayz/Weapons/AttachmentDefs.hpp"

#include <functional>
#include <optional>
#include <span>

namespace NextDayz
{
    using FItemInstanceId = uint64_t;
    using FContainerId = uint32_t;

    // Container id of items worn in an equipment slot.
    inline constexpr FContainerId kWornContainer = 0xFFFFFFFFu;

    struct FItemStack
    {
        std::string id;
        std::string displayName;
        EItemKind kind = EItemKind::Misc;
        int count = 1;
    };

    struct FItemInstance
    {
        FItemInstanceId instanceId = 0;
        std::string defId;
        int count = 1;
        FContainerId containerId = 0;
        int loadedAmmo = 0;
        FWeaponAttachments attachments{};
    };

    struct FInventoryContainer
    {
        FContainerId containerId = 0;
        FItemInstanceId ownerItemInstanceId = 0;
        std::string displayName;
        int capacity = 0;
        bool active = true;
        bool weaponOnly = false;
    };

    struct FInventoryAddRequest
    {
        std::string id;
        std::string displayName;
        EItemKind kind = EItemKind::Misc;
        int count = 1;
    };

    struct FInventoryState
    {
        std::vector<FItemInstance> instances;
        std::vector<FInventoryContainer> containers;
        FItemInstanceId nextInstanceId = 1;
        FContainerId nextContainerId = 1;
    };

    class Inventory
    {
    public:
        static constexpr size_t kMaxSlots = 256;

        Inventory();
        void Clear();
        FInventoryState Capture() const;
        bool Restore(const FInventoryState& state);

        // 0 auto-stows; kWornContainer equips directly without temporary cargo space.
        bool CanAdd(const std::string& id, int count = 1, FContainerId destination = 0) const;
        bool TryAdd(const std::string& id, const std::string& displayName, EItemKind kind, int count,
                    FItemInstanceId* outInstanceId = nullptr, FContainerId destination = 0);
        bool TryAddBatch(std::span<const FInventoryAddRequest> requests);
        // How many of `count` would fit right now (0..count).
        int FitCount(const std::string& id, int count, FContainerId destination = 0) const;

        // Compatibility entry point for the pre-productization callers. New code
        // should use TryAdd/TryAddBatch so capacity rejection is observable.
        void Add(const std::string& id, const std::string& displayName, EItemKind kind, int count);

        bool TryMove(FItemInstanceId instanceId, FContainerId destination, int count = -1);
        bool TrySplit(FItemInstanceId instanceId, int count, FItemInstanceId& outSplitId);
        // Wears the item; whatever is worn in that slot is taken off first (and
        // needs somewhere to go), so this is also a swap.
        bool TryEquip(FItemInstanceId instanceId);
        // Takes the item off into cargo; its container's contents move to the
        // other containers. Fails if either does not fit.
        bool TryUnequip(FItemInstanceId instanceId);
        bool TryDrop(FItemInstanceId instanceId, int count = -1);
        // Like TryDrop, but hands back what left the inventory (for the world).
        std::optional<FItemInstance> TryRemove(FItemInstanceId instanceId, int count = -1);
        bool TryConsumeInstance(FItemInstanceId instanceId, int count = 1);

        const std::vector<FItemStack>& Items() const { return legacyItems_; }
        const std::vector<FItemInstance>& Instances() const { return instances_; }
        const std::vector<FInventoryContainer>& Containers() const { return containers_; }

        const FItemInstance* FindInstance(FItemInstanceId instanceId) const;
        FItemInstance* FindInstance(FItemInstanceId instanceId);
        const FItemInstance* FindFirst(std::string_view defId) const;
        bool SetLoadedAmmo(FItemInstanceId instanceId, int loadedAmmo);
        int LoadedAmmo(FItemInstanceId instanceId) const;
        bool SetWeaponState(FItemInstanceId instanceId, int loadedAmmo, const FWeaponAttachments& attachments);
        bool TryAttach(FItemInstanceId weaponId, FItemInstanceId attachmentId, std::string* error = nullptr);
        bool TryDetach(FItemInstanceId weaponId, EAttachmentSlot slot, std::string* error = nullptr);
        const FInventoryContainer* FindContainer(FContainerId containerId) const;
        // The container an equipped item provides (nullptr if none).
        const FInventoryContainer* ContainerOwnedBy(FItemInstanceId instanceId) const;

        int ContainerUsed(FContainerId containerId) const;
        int UsedCapacity() const;
        int TotalCapacity() const;
        int FreeCapacity() const { return TotalCapacity() - UsedCapacity(); }
        static int InstanceVolume(const FItemInstance& instance);

        int CountOf(const std::string& id) const;
        int Consume(const std::string& id, int count);

        bool IsWorn(FItemInstanceId instanceId) const;
        FItemInstanceId WornInSlot(EEquipSlot slot) const;
        bool IsClothingEquipped(const std::string& id) const;
        void SetClothingEquipped(const std::string& id, bool equipped);
        const std::vector<std::string>& EquippedClothing() const { return equippedClothing_; }

        // Worn protection and insulation.
        float Armor() const;
        float Warmth() const;
        bool HasTool(EItemTool tool) const;
        // A can opener, or a blade that does the job.
        bool CanOpenCans() const;

    private:
        // Takes a worn item off: its container's contents move to the other
        // containers; the item itself goes to cargo (or leaves, for a drop).
        bool TakeOff(FItemInstanceId instanceId, bool intoCargo, FContainerId destination = 0);
        FContainerId FindContainerWithSpace(const FItemDef& definition, int count,
                                            FContainerId excluded = 0) const;
        void RebuildLegacyItems();
        void RemoveEmptyInstances();

        std::vector<FItemInstance> instances_;
        std::vector<FInventoryContainer> containers_;
        std::vector<FItemStack> legacyItems_;
        std::vector<std::string> equippedClothing_;
        FItemInstanceId nextInstanceId_ = 1;
        FContainerId nextContainerId_ = 1;
    };
}
