#pragma once

#include "Engine/Common/CoreMinimal.hpp"

namespace NextDayz
{
    // One shelf of the safehouse trader. `buy` is the resolved price of one unit
    // (round(sell x buyMarkup) unless the table overrides it); one purchase moves
    // `pack` units into storage and uses one of the cycle's `stock` packs.
    struct FTraderEntry
    {
        std::string item;
        uint32_t tier = 0;
        int stock = 1;
        int pack = 1;
        uint64_t buy = 0;
    };

    struct FWarehouseStep
    {
        uint32_t capacity = 0;
        uint64_t price = 0;
    };

    // assets/configs/nextdayz_economy.json: the single price table the offline
    // campaign and the NextDayz Master both read
    // (docs/projects/nextdayz/nextdayz-economy-contracts-design.md §6.1).
    struct FEconomyDefs
    {
        uint32_t version = 1;
        std::unordered_map<std::string, uint64_t> sell;
        double buyMarkup = 3.0;
        std::vector<FTraderEntry> trader;
        uint64_t insertionFee = 40;
        uint32_t warehouseBase = 40;
        std::vector<FWarehouseStep> warehouseSteps;
        // Reputation needed for each tier, ascending and starting at 0.
        std::vector<uint32_t> reputationTiers{0};
        // Reputation gained for a completed contract of tier 1, 2, 3.
        std::vector<uint32_t> reputationGain;
        uint32_t reputationPenalty = 1;
    };

    // Parses and validates the table: every item definition has an explicit sell
    // price, nothing names an unknown item, every shelf sells at least twice what
    // the item fetches (buying and selling back always loses) and a pack fits in
    // one stack.
    bool ParseEconomyDefs(std::string_view json, FEconomyDefs& defs, std::string& error);
    bool LoadEconomyDefs(const std::filesystem::path& path, FEconomyDefs& defs, std::string& error);
    const FTraderEntry* FindTraderEntry(const FEconomyDefs& defs, std::string_view item);
}
