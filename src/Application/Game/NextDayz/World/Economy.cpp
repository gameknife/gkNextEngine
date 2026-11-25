#include "Engine/Common/CoreMinimal.hpp"
#include "Economy.hpp"
#include "Application/Game/NextDayz/Weapons/AttachmentDefs.hpp"

#include <limits>

namespace NextDayz
{
    namespace
    {
        bool AddGold(uint64_t& gold, uint64_t amount)
        {
            if (amount > std::numeric_limits<uint64_t>::max() - gold) return false;
            gold += amount;
            return true;
        }
    }

    uint64_t SellPrice(std::string_view itemId, const FEconomyDefs& defs)
    {
        const auto found = defs.sell.find(std::string(itemId));
        return found == defs.sell.end() ? 0 : found->second;
    }

    uint64_t UnitSellValue(const FItemInstance& item, const FEconomyDefs& defs)
    {
        uint64_t price = SellPrice(item.defId, defs);
        for (const auto type : item.attachments)
            if (const auto* part = FindAttachmentDef(type)) price += SellPrice(part->id, defs);
        return price;
    }

    uint64_t SellValue(std::span<const FItemInstance> items, const FEconomyDefs& defs)
    {
        uint64_t total = 0;
        for (const auto& item : items)
            total += UnitSellValue(item, defs) * static_cast<uint64_t>(std::max(item.count, 0));
        return total;
    }

    uint32_t ReputationTier(uint32_t reputation, const FEconomyDefs& defs)
    {
        uint32_t tier = 0;
        for (size_t index = 1; index < defs.reputationTiers.size(); ++index)
            if (reputation >= defs.reputationTiers[index]) tier = static_cast<uint32_t>(index);
        return tier;
    }

    uint64_t FinishedSorties(const FCampaign& campaign)
    {
        return campaign.sorties - (campaign.inField && campaign.sorties > 0 ? 1 : 0);
    }

    uint64_t TraderCycle(const FCampaign& campaign)
    {
        // Cycle numbers start at 1, so a fresh or migrated campaign (traderCycle 0)
        // always starts with full shelves.
        return FinishedSorties(campaign) + 1;
    }

    void RefreshTrader(FCampaign& campaign, const FEconomyDefs& defs)
    {
        const uint64_t cycle = TraderCycle(campaign);
        if (campaign.traderCycle == cycle) return;
        campaign.traderCycle = cycle;
        campaign.traderStock.clear();
        for (const auto& entry : defs.trader) campaign.traderStock[entry.item] = entry.stock;
    }

    int TraderRemaining(const FCampaign& campaign, const FTraderEntry& entry)
    {
        if (campaign.traderCycle != TraderCycle(campaign)) return entry.stock;
        const auto found = campaign.traderStock.find(entry.item);
        return found == campaign.traderStock.end() ? entry.stock : std::clamp(found->second, 0, entry.stock);
    }

    ETradeResult CanBuy(const FCampaign& campaign, const FEconomyDefs& defs, const FTraderEntry& entry)
    {
        if (campaign.inField) return ETradeResult::InField;
        if (entry.tier > ReputationTier(campaign.reputation, defs)) return ETradeResult::Locked;
        if (WarehouseOver(campaign, defs)) return ETradeResult::StorageFull;
        if (TraderRemaining(campaign, entry) <= 0) return ETradeResult::SoldOut;
        const uint64_t price = entry.buy * static_cast<uint64_t>(entry.pack);
        if (campaign.gold < price) return ETradeResult::NotEnoughGold;
        return ETradeResult::Ok;
    }

    ETradeResult Buy(FCampaign& campaign, const FEconomyDefs& defs, std::string_view item)
    {
        const auto* entry = FindTraderEntry(defs, item);
        if (!entry) return ETradeResult::NotStocked;
        const auto result = CanBuy(campaign, defs, *entry);
        if (result != ETradeResult::Ok) return result;
        auto next = campaign;
        RefreshTrader(next, defs);
        const uint64_t price = entry->buy * static_cast<uint64_t>(entry->pack);
        if (!AddToStorage(next, entry->item, entry->pack)) return ETradeResult::Invalid;
        if (next.storage.size() > campaign.storage.size() && next.storage.size() > WarehouseCapacity(next, defs))
            return ETradeResult::StorageFull;
        next.gold -= price;
        next.sortie.spent += price;
        --next.traderStock[entry->item];
        campaign = std::move(next);
        return ETradeResult::Ok;
    }

    const char* TradeResultMessage(ETradeResult result)
    {
        switch (result)
        {
        case ETradeResult::Ok: return "Purchased. It is waiting in your storage.";
        case ETradeResult::InField: return "The trader only deals with the safehouse.";
        case ETradeResult::NotStocked: return "The trader does not sell that.";
        case ETradeResult::Locked: return "Your reputation is too low for this shelf.";
        case ETradeResult::SoldOut: return "Sold out. The trader restocks after your next expedition.";
        case ETradeResult::NotEnoughGold: return "Not enough gold.";
        case ETradeResult::StorageFull: return "Your storage is full. Sell, hand in or carry items, or expand the warehouse.";
        default: return "That deal is not possible.";
        }
    }

    bool AddToStorage(FCampaign& campaign, std::string_view defId, int count)
    {
        const auto* def = FindItemDef(defId);
        if (!def || count <= 0) return false;
        // Only storage changes, so references into the rest of the campaign (its
        // contracts, say) stay valid; a failure puts storage back as it was.
        auto storage = campaign.storage;
        const uint64_t nextId = campaign.nextStorageId;
        while (count > 0)
        {
            FItemInstance units;
            units.defId = std::string(defId);
            units.count = std::min(count, def->maxStack);
            count -= units.count;
            if (!campaign.Put(std::move(units)))
            {
                campaign.storage = std::move(storage);
                campaign.nextStorageId = nextId;
                return false;
            }
        }
        return true;
    }

    uint32_t WarehouseLevels(const FEconomyDefs& defs)
    {
        return static_cast<uint32_t>(defs.warehouseSteps.size());
    }

    uint32_t WarehouseLevelCapacity(const FEconomyDefs& defs, uint32_t level)
    {
        if (level == 0 || defs.warehouseSteps.empty()) return defs.warehouseBase;
        return defs.warehouseSteps[std::min<size_t>(level, defs.warehouseSteps.size()) - 1].capacity;
    }

    uint32_t WarehouseCapacity(const FCampaign& campaign, const FEconomyDefs& defs)
    {
        return std::max(WarehouseLevelCapacity(defs, campaign.warehouseLevel), campaign.warehouseFloor);
    }

    bool WarehouseOver(const FCampaign& campaign, const FEconomyDefs& defs)
    {
        return campaign.storage.size() > WarehouseCapacity(campaign, defs);
    }

    const FWarehouseStep* NextWarehouseStep(const FCampaign& campaign, const FEconomyDefs& defs)
    {
        return campaign.warehouseLevel < defs.warehouseSteps.size() ? &defs.warehouseSteps[campaign.warehouseLevel] : nullptr;
    }

    ETradeResult UpgradeWarehouse(FCampaign& campaign, const FEconomyDefs& defs)
    {
        if (campaign.inField) return ETradeResult::InField;
        const auto* step = NextWarehouseStep(campaign, defs);
        if (!step) return ETradeResult::Invalid;
        if (campaign.gold < step->price) return ETradeResult::NotEnoughGold;
        campaign.gold -= step->price;
        campaign.sortie.spent += step->price;
        ++campaign.warehouseLevel;
        if (campaign.warehouseFloor <= WarehouseLevelCapacity(defs, campaign.warehouseLevel)) campaign.warehouseFloor = 0;
        return ETradeResult::Ok;
    }

    void ResolveWarehouse(FCampaign& campaign, const FEconomyDefs& defs)
    {
        // A migrated save carries its stack count as a floor; turn it into the lowest
        // level that holds it, keeping the floor only beyond the top level.
        if (campaign.warehouseFloor == 0 || defs.warehouseSteps.empty()) return;
        campaign.warehouseLevel = std::min(campaign.warehouseLevel, WarehouseLevels(defs));
        while (campaign.warehouseLevel < WarehouseLevels(defs) &&
               WarehouseLevelCapacity(defs, campaign.warehouseLevel) < campaign.warehouseFloor)
            ++campaign.warehouseLevel;
        if (WarehouseLevelCapacity(defs, campaign.warehouseLevel) >= campaign.warehouseFloor) campaign.warehouseFloor = 0;
    }

    ETradeResult CanDeploy(const FCampaign& campaign, const FEconomyDefs& defs)
    {
        return WarehouseOver(campaign, defs) ? ETradeResult::StorageFull : ETradeResult::Ok;
    }

    ETradeResult StoreCarried(FCampaign& campaign, const FEconomyDefs& defs, Inventory& carried, FItemInstanceId id, int count)
    {
        if (campaign.inField) return ETradeResult::InField;
        auto next = campaign;
        auto inventory = carried;
        const bool all = id == 0;
        if (all) next.StoreAll(inventory);
        else if (!next.Store(inventory, id, count)) return ETradeResult::Invalid;
        // Storing by hand may not push the warehouse over (or further over) capacity.
        if (next.storage.size() > campaign.storage.size() && next.storage.size() > WarehouseCapacity(next, defs))
            return ETradeResult::StorageFull;
        campaign = std::move(next);
        carried = std::move(inventory);
        return ETradeResult::Ok;
    }

    ETradeResult PayInsertion(FCampaign& campaign, const FEconomyDefs& defs, std::string mapId, int point)
    {
        if (campaign.inField) return ETradeResult::InField;
        if (mapId.empty() || point < 0) return ETradeResult::Invalid;
        auto& sortie = campaign.sortie;
        if (sortie.insertionPaid && sortie.insertionMap == mapId)
        {
            sortie.insertionPoint = point;
            return ETradeResult::Ok;
        }
        CancelInsertion(campaign);
        if (campaign.gold < defs.insertionFee) return ETradeResult::NotEnoughGold;
        campaign.gold -= defs.insertionFee;
        sortie.spent += defs.insertionFee;
        sortie.insertionPaid = defs.insertionFee;
        sortie.insertionMap = std::move(mapId);
        sortie.insertionPoint = point;
        return ETradeResult::Ok;
    }

    void CancelInsertion(FCampaign& campaign)
    {
        auto& sortie = campaign.sortie;
        if (campaign.inField || sortie.insertionMap.empty()) return;
        AddGold(campaign.gold, sortie.insertionPaid);
        sortie.spent -= std::min(sortie.spent, sortie.insertionPaid);
        sortie.insertionPaid = 0;
        sortie.insertionMap.clear();
        sortie.insertionPoint = -1;
    }

    std::optional<int> TakeInsertion(FCampaign& campaign, std::string_view mapId)
    {
        auto& sortie = campaign.sortie;
        if (sortie.insertionMap.empty()) return std::nullopt;
        if (sortie.insertionMap != mapId)
        {
            CancelInsertion(campaign);
            return std::nullopt;
        }
        const int point = sortie.insertionPoint;
        // The fee stays spent; the request itself is used up by this deployment.
        sortie.insertionMap.clear();
        sortie.insertionPoint = -1;
        sortie.insertionPaid = 0;
        return point;
    }

    void FinishSortie(FCampaign& campaign, const FEconomyDefs& defs, FSortieRecord record)
    {
        record.spentBefore = campaign.sortie.spent;
        record.contractGold = campaign.sortie.contractGold;
        record.contractsCompleted = campaign.sortie.contractsCompleted;
        record.visitedPois = campaign.sortie.visitedPois;
        record.reputationAfter = campaign.reputation;
        campaign.history.push_back(std::move(record));
        if (campaign.history.size() > kSortieHistory)
            campaign.history.erase(campaign.history.begin(),
                campaign.history.begin() + static_cast<std::ptrdiff_t>(campaign.history.size() - kSortieHistory));
        campaign.sortie = {};
        RefreshTrader(campaign, defs);
    }

    int64_t SortieNet(const FSortieRecord& record)
    {
        return static_cast<int64_t>(record.extractedValue + record.contractGold) - static_cast<int64_t>(record.spentBefore);
    }
}
