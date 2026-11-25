#pragma once

#include "Engine/Common/CoreMinimal.hpp"
#include "Application/Game/NextDayz/Data/EconomyDefs.hpp"
#include "Application/Game/NextDayz/World/Campaign.hpp"

// The safehouse economy (docs/projects/nextdayz/nextdayz-economy-contracts-design.md):
// prices from FEconomyDefs, trader stock that refills once per finished sortie,
// the paid landing point and the sortie record. Engine-free like ItemLedger, so
// it runs as is in gkNextUnitTests.
namespace NextDayz
{
    enum class ETradeResult : uint8_t
    {
        Ok,
        InField,        // the warehouse is only reachable from the safehouse
        NotStocked,     // the trader does not sell it
        Locked,         // needs a higher reputation tier
        SoldOut,        // this cycle's stock is gone
        NotEnoughGold,
        StorageFull,    // the warehouse is (or would go) over capacity
        Invalid,        // unknown landing point, overflow, ...
    };

    uint64_t SellPrice(std::string_view itemId, const FEconomyDefs& defs);
    // What one unit of a stack fetches, mounted weapon parts included.
    uint64_t UnitSellValue(const FItemInstance& item, const FEconomyDefs& defs);
    uint64_t SellValue(std::span<const FItemInstance> items, const FEconomyDefs& defs);

    uint32_t ReputationTier(uint32_t reputation, const FEconomyDefs& defs);

    // Trader cycles count finished sorties (extraction, death or debug return),
    // never wall-clock time: the world is paused while the player is in the cabin.
    uint64_t FinishedSorties(const FCampaign& campaign);
    uint64_t TraderCycle(const FCampaign& campaign);
    // Refills every shelf when the campaign entered a new cycle since the last refill.
    void RefreshTrader(FCampaign& campaign, const FEconomyDefs& defs);
    // Packs left on a shelf this cycle (a stale cycle counts as full).
    int TraderRemaining(const FCampaign& campaign, const FTraderEntry& entry);
    ETradeResult CanBuy(const FCampaign& campaign, const FEconomyDefs& defs, const FTraderEntry& entry);
    // Buys one pack into storage. Bought guns are empty and bare.
    ETradeResult Buy(FCampaign& campaign, const FEconomyDefs& defs, std::string_view item);
    const char* TradeResultMessage(ETradeResult result);

    // Adds plain units to storage, topping up plain stacks of the same item
    // (no rounds loaded, no parts) before opening new ones.
    bool AddToStorage(FCampaign& campaign, std::string_view defId, int count);

    // Warehouse (design §4.3). Capacity counts stacks. Extraction and contract
    // rewards always store, even over capacity; nothing is ever destroyed. Over
    // capacity the survivor cannot deploy, buy, or store by hand until selling,
    // handing in or carrying brings the count back down.
    uint32_t WarehouseLevels(const FEconomyDefs& defs);
    uint32_t WarehouseLevelCapacity(const FEconomyDefs& defs, uint32_t level);
    uint32_t WarehouseCapacity(const FCampaign& campaign, const FEconomyDefs& defs);
    bool WarehouseOver(const FCampaign& campaign, const FEconomyDefs& defs);
    const FWarehouseStep* NextWarehouseStep(const FCampaign& campaign, const FEconomyDefs& defs);
    ETradeResult UpgradeWarehouse(FCampaign& campaign, const FEconomyDefs& defs);
    // Migrated saves: the lowest level holding warehouseFloor stacks.
    void ResolveWarehouse(FCampaign& campaign, const FEconomyDefs& defs);
    ETradeResult CanDeploy(const FCampaign& campaign, const FEconomyDefs& defs);
    // Stores one carried item (id) or everything carried (id 0) by hand,
    // refusing when that would push storage over capacity.
    ETradeResult StoreCarried(FCampaign& campaign, const FEconomyDefs& defs, Inventory& carried, FItemInstanceId id, int count = -1);

    // Paid landing: choosing a point charges the fee once; changing it while still
    // in the safehouse is free and cancelling refunds it.
    ETradeResult PayInsertion(FCampaign& campaign, const FEconomyDefs& defs, std::string mapId, int point);
    void CancelInsertion(FCampaign& campaign);
    // At deployment: the paid point when it belongs to `mapId`; a paid point for
    // another region is refunded. Either way the request is consumed.
    std::optional<int> TakeInsertion(FCampaign& campaign, std::string_view mapId);

    // Closes the sortie: appends the record (keeping the most recent
    // kSortieHistory), clears the per-sortie state and refills the trader.
    inline constexpr size_t kSortieHistory = 50;
    void FinishSortie(FCampaign& campaign, const FEconomyDefs& defs, FSortieRecord record);
    int64_t SortieNet(const FSortieRecord& record);
}
