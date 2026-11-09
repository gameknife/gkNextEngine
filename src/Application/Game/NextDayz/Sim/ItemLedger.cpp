#include "Engine/Common/CoreMinimal.hpp"
#include "Application/Game/NextDayz/Sim/ItemLedger.hpp"

#include "Application/Game/NextDayz/Data/ItemDefs.hpp"
#include "Application/Game/NextDayz/Weapons/AttachmentDefs.hpp"
#include "Application/Game/NextDayz/Weapons/WeaponDefs.hpp"

namespace NextDayz
{
    namespace
    {
        void CountOne(FItemTotals& totals, std::string_view itemId, int count, int loadedAmmo,
                      const FWeaponAttachments& attachments)
        {
            if (count > 0) totals[std::string(itemId)] += count;
            if (const FWeaponDef* weapon = FindWeaponDef(itemId))
            {
                if (loadedAmmo > 0) totals[std::string(AmmoItemId(weapon->ammo))] += loadedAmmo;
            }
            for (const EWeaponAttachment attachment : attachments)
            {
                if (const FAttachmentDef* definition = FindAttachmentDef(attachment); definition && attachment != EWeaponAttachment{})
                {
                    totals[std::string(definition->id)] += 1;
                }
            }
        }

        // Items whose use (or filling) may turn into `itemId`.
        std::vector<std::string> ConversionSources(std::string_view itemId)
        {
            std::vector<std::string> sources;
            for (const FItemDef& definition : AllItemDefs())
            {
                if (!definition.leftover.empty() && definition.leftover == itemId) sources.emplace_back(definition.id);
            }
            if (itemId == "water_bottle") sources.emplace_back("water_bottle_empty");
            return sources;
        }
    }

    FItemTotals CountItems(const FInventoryState& inventory)
    {
        FItemTotals totals;
        for (const FItemInstance& item : inventory.instances)
        {
            CountOne(totals, item.defId, item.count, item.loadedAmmo, item.attachments);
        }
        return totals;
    }

    FItemTotals CountItems(std::span<const FLootStack> stacks)
    {
        FItemTotals totals;
        for (const FLootStack& stack : stacks)
        {
            CountOne(totals, stack.itemId, stack.count, stack.loadedAmmo, stack.attachments);
        }
        return totals;
    }

    void AddTotals(FItemTotals& into, const FItemTotals& more)
    {
        for (const auto& [id, count] : more) into[id] += count;
    }

    bool SubtractTotals(FItemTotals& from, const FItemTotals& less)
    {
        for (const auto& [id, count] : less)
        {
            const auto it = from.find(id);
            if (count > 0 && (it == from.end() || it->second < count)) return false;
        }
        for (const auto& [id, count] : less)
        {
            if (count <= 0) continue;
            if ((from[id] -= count) == 0) from.erase(id);
        }
        return true;
    }

    FLedgerVerdict CheckConservation(const FItemTotals& allowed, const FItemTotals& reported)
    {
        // What disappeared can pay for what it legitimately becomes.
        std::map<std::string, int64_t> spent;
        for (const auto& [id, count] : allowed)
        {
            const auto it = reported.find(id);
            const int64_t now = it == reported.end() ? 0 : it->second;
            if (count > now) spent[id] = count - now;
        }
        for (const auto& [id, count] : reported)
        {
            const auto it = allowed.find(id);
            int64_t gained = count - (it == allowed.end() ? 0 : it->second);
            if (gained <= 0) continue;
            for (const std::string& source : ConversionSources(id))
            {
                int64_t& available = spent[source];
                const int64_t used = std::min(available, gained);
                available -= used;
                gained -= used;
                if (gained == 0) break;
            }
            if (gained > 0)
            {
                return {false, fmt::format("{} x{} appeared from nothing", id, gained)};
            }
        }
        return {true, {}};
    }

    FItemTotals StartingKit(bool testingKit)
    {
        FItemTotals kit{{"bandage", 1}, {"water_bottle_empty", 1}};
        if (testingKit)
        {
            kit["backpack"] = 1;
            kit["ak"] = 1;
            kit["ammo_545"] = 90;   // a full magazine and 60 spare
        }
        return kit;
    }

    std::vector<FLootStack> InventoryAsStacks(const FInventoryState& inventory)
    {
        std::vector<FLootStack> stacks;
        for (const FItemInstance& item : inventory.instances)
        {
            stacks.push_back({item.defId, item.count, item.loadedAmmo, item.attachments});
        }
        return stacks;
    }
}
