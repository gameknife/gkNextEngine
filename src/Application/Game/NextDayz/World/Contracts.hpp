#pragma once

#include "Engine/Common/CoreMinimal.hpp"
#include "Application/Game/NextDayz/World/Campaign.hpp"
#include "Application/Game/NextDayz/World/PoiIndex.hpp"

// The contract board (docs/projects/nextdayz/nextdayz-economy-contracts-design.md §5).
// Engine-free like World/Economy: the game supplies the carried inventory and
// the site index, and moves parcels in the loaded world itself.
//
// Order at the end of a sortie is fixed: SettleContracts (extraction only),
// then the survivor is out of the field, then ExpireContracts, FinishSortie
// and RefreshBoard. Settling before expiring means a parcel carried home on
// the contract's last sortie always counts.
namespace NextDayz
{
    enum class EContractResult : uint8_t
    {
        Ok,
        InField,        // the board is only reachable from the safehouse
        NotFound,
        WrongState,     // e.g. accepting a delivery, handing in a recon contract
        TooMany,        // already working on the maximum number of field contracts
        MissingItems,   // storage does not hold what the delivery asks for
    };

    struct FContractReport
    {
        std::vector<std::string> completed;      // template ids
        std::vector<std::string> expired;        // template ids
        uint64_t gold = 0;
        int64_t reputation = 0;
        std::vector<std::string> itemRewards;
        // Parcels whose contract ended: remove them from every world, pile and pack.
        std::vector<std::string> purgeParcels;
    };

    const char* ContractResultMessage(EContractResult result);
    bool IsFieldContract(EContractType type);
    // Contract tier the board offers at this reputation (tier 0 still gets tier 1).
    uint32_t AllowedContractTier(const FCampaign& campaign, const FEconomyDefs& defs);
    uint32_t BoardSlots(const FCampaign& campaign, const FEconomyDefs& defs);

    // Once per cycle: drops offers left untaken for replaceAfter sorties, then
    // fills the board with reproducible rolls from campaign.boardRng. A board
    // without a field contract gets one whenever any can be rolled.
    void RefreshBoard(FCampaign& campaign, const FEconomyDefs& defs, const FPoiIndex& pois);
    // Validation and debugging: makes sure `templateId` is on offer now (rolls it
    // if needed, ignoring tier and slots). False when it is already accepted or
    // cannot be placed on any map.
    bool OfferContract(FCampaign& campaign, const FEconomyDefs& defs, const FPoiIndex& pois, std::string_view templateId);

    const FContract* FindContract(const FCampaign& campaign, uint64_t id);
    int StoredCount(const FCampaign& campaign, std::string_view item);
    bool CanHandIn(const FCampaign& campaign, const FContract& contract);
    // Sorties left before an offer is replaced or an accepted contract expires.
    int SortiesLeft(const FCampaign& campaign, const FEconomyDefs& defs, const FContract& contract);

    EContractResult AcceptContract(FCampaign& campaign, const FEconomyDefs& defs, uint64_t id);
    EContractResult AbandonContract(FCampaign& campaign, const FEconomyDefs& defs, uint64_t id, FContractReport& report);
    EContractResult HandInContract(FCampaign& campaign, const FEconomyDefs& defs, uint64_t id, FContractReport& report);

    // Records a site visit for this sortie (once per site).
    void RecordVisit(FSortieState& sortie, std::string_view site);
    bool ReconComplete(const FContract& contract, const FSortieState& sortie, std::string_view mapId);
    // At extraction: completes recon contracts whose sites were all visited on
    // `mapId`, and fetch contracts whose parcel is carried (the parcel is removed).
    FContractReport SettleContracts(FCampaign& campaign, const FEconomyDefs& defs, Inventory& carried, std::string_view mapId);
    // After the sortie: expires accepted contracts that ran out of sorties.
    void ExpireContracts(FCampaign& campaign, const FEconomyDefs& defs, FContractReport& report);

    // Parcels that belong to an accepted contract, with the site they are planted at.
    struct FParcelTask
    {
        std::string parcel;
        std::string mapId;
        std::string site;
    };
    std::vector<FParcelTask> ActiveParcels(const FCampaign& campaign);
    bool ParcelIsActive(const FCampaign& campaign, std::string_view parcel);
    // Strips `item` from every entry of a saved world; returns how many stacks went.
    int PurgeItemFromWorld(FLootWorldState& world, std::string_view item);
}
