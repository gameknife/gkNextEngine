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

    enum class EContractType : uint8_t
    {
        Deliver,   // hand items in from storage, in the safehouse
        Recon,     // visit 1-2 sites on a map and extract
        Fetch,     // bring a parcel planted at a site home on the helicopter
    };

    struct FContractItemRange
    {
        std::string item;
        int min = 1;
        int max = 1;
    };

    // A contract the board can offer (economy design §5, §6.1). Values are
    // rolled with the board's random numbers when the contract is offered.
    struct FContractTemplate
    {
        std::string id;
        std::string issuer;          // medical | quartermaster | collector | radio
        std::string title;           // English; the translation key
        EContractType type = EContractType::Deliver;
        uint32_t tier = 1;           // 1..3: needs reputation tier >= this (tier 0 offers tier 1)
        uint32_t weight = 1;
        std::vector<FContractItemRange> items;   // deliver
        uint64_t bonus = 0;                      // deliver: added to the items' value
        std::vector<std::string> poiKinds;       // recon / fetch; empty = any kind
        uint32_t poiCount = 1;
        uint32_t riskMin = 1;
        uint32_t riskMax = 3;
        std::string parcel;                      // fetch: a contract_* item
        uint64_t rewardMin = 0;                  // recon / fetch
        uint64_t rewardMax = 0;
        std::vector<std::string> itemRewards;    // one is rolled, if any
    };

    struct FBoardRules
    {
        std::vector<uint32_t> slots{3};   // offered contracts per reputation tier
        uint32_t replaceAfter = 2;        // unaccepted offers leave after this many finished sorties
        uint32_t expireAfter = 3;         // accepted field contracts expire after this many
        uint32_t maxActive = 2;           // accepted field contracts at once
        double deliverMultiplier = 1.6;   // deliver reward = items' sell value x this + bonus
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
        FBoardRules board;
        std::vector<FContractTemplate> contracts;
    };

    // Parses and validates the table: every item definition has an explicit sell
    // price, nothing names an unknown item, every shelf sells at least twice what
    // the item fetches (buying and selling back always loses) and a pack fits in
    // one stack.
    bool ParseEconomyDefs(std::string_view json, FEconomyDefs& defs, std::string& error);
    bool LoadEconomyDefs(const std::filesystem::path& path, FEconomyDefs& defs, std::string& error);
    const FTraderEntry* FindTraderEntry(const FEconomyDefs& defs, std::string_view item);
    const FContractTemplate* FindContractTemplate(const FEconomyDefs& defs, std::string_view id);
    const char* ContractTypeName(EContractType type);
}
