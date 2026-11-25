#include "Engine/Common/CoreMinimal.hpp"
#include <catch2/catch_test_macros.hpp>
#include "Application/Game/NextDayz/World/Contracts.hpp"
#include "Application/Game/NextDayz/World/Economy.hpp"
#include "Application/Game/NextDayz/World/ExpeditionMaps.hpp"
#include <nlohmann/json.hpp>

using namespace NextDayz;

namespace
{
    const std::filesystem::path sourceRoot = GK_NEXT_SOURCE_DIR;

    const FEconomyDefs& Defs()
    {
        static const FEconomyDefs defs = []
        {
            FEconomyDefs result;
            std::string error;
            if (!LoadEconomyDefs(sourceRoot / "assets/configs/nextdayz_economy.json", result, error))
                throw std::runtime_error(error);
            return result;
        }();
        return defs;
    }

    // The published sites of every dispatchable map, as the game loads them.
    const FPoiIndex& Sites()
    {
        static const FPoiIndex index = []
        {
            std::vector<FExpeditionMap> maps;
            std::string error;
            if (!LoadExpeditionMaps(sourceRoot / "assets/configs/nextdayz_maps.json", maps, error)) throw std::runtime_error(error);
            FPoiIndex result;
            for (const auto& map : maps)
            {
                if (!map.published || map.training) continue;
                std::vector<FPoi> sites;
                if (LoadPoiIndex(sourceRoot / GenerationRecordPath(map), sites, error)) result[map.id] = std::move(sites);
            }
            return result;
        }();
        return index;
    }

    FCampaign Fresh(uint64_t seed, uint32_t reputation = 0)
    {
        FCampaign campaign;
        campaign.boardRng = seed;
        campaign.reputation = reputation;
        RefreshBoard(campaign, Defs(), Sites());
        return campaign;
    }

    void Deploy(FCampaign& campaign)
    {
        campaign.inField = true;
        ++campaign.sorties;
        ++campaign.sortieId;
    }

    // One sortie end in the game's order: settle (extraction only), leave the
    // field, expire, record, refresh the board.
    FContractReport End(FCampaign& campaign, Inventory& carried, bool extracted, std::string_view mapId)
    {
        FContractReport report;
        if (extracted) report = SettleContracts(campaign, Defs(), carried, mapId);
        campaign.inField = false;
        ExpireContracts(campaign, Defs(), report);
        FSortieRecord record;
        record.sortieId = campaign.sortieId;
        FinishSortie(campaign, Defs(), record);
        RefreshBoard(campaign, Defs(), Sites());
        return report;
    }

    const FContract* FirstOfType(const std::vector<FContract>& list, EContractType type)
    {
        const auto found = std::find_if(list.begin(), list.end(), [type](const auto& contract) { return contract.type == type; });
        return found == list.end() ? nullptr : &*found;
    }

    // A campaign whose board offers `type` (tries seeds in order).
    FCampaign WithOffer(EContractType type, uint32_t reputation = 0)
    {
        for (uint64_t seed = 1; seed < 400; ++seed)
        {
            auto campaign = Fresh(seed, reputation);
            if (FirstOfType(campaign.board, type)) return campaign;
        }
        FAIL("no seed offers the contract type");
        return {};
    }

    std::string Describe(const FCampaign& campaign)
    {
        std::string text;
        for (const auto& contract : campaign.board)
        {
            text += contract.templateId + "@" + contract.mapId + ":" + std::to_string(contract.reward);
            for (const auto& target : contract.targets) text += "/" + target;
            for (const auto& item : contract.items) text += "/" + item.item + "x" + std::to_string(item.count);
            text += ";";
        }
        return text;
    }
}

TEST_CASE("NextDayz contract templates and sites load from the shipped data", "[NextDayz][Contracts]")
{
    REQUIRE(Defs().contracts.size() >= 12);
    REQUIRE(Sites().contains("cz_loket_riverbend"));
    const auto& loket = Sites().at("cz_loket_riverbend");
    CHECK(loket.size() == 15);
    const auto* castle = FindPoi(loket, "Castle Loket");
    REQUIRE(castle);
    CHECK(castle->kind == "fortress");
    CHECK(castle->risk == 3);
    CHECK(castle->center == glm::vec2(20.0f, -140.0f));
    CHECK(castle->radius > 40.0f);
    CHECK(PoisAt(loket, castle->center).size() >= 1);
    CHECK(PoiKind("medical") == "hospital");
    CHECK(PoiKind("jzd") == "industrial");
    CHECK(PoiKind("naves") == "village");
    // Every template can place itself on a published map at some tier.
    for (const auto& entry : Defs().contracts)
    {
        if (entry.type == EContractType::Deliver) continue;
        CAPTURE(entry.id);
        bool placeable = false;
        for (const auto& [map, sites] : Sites())
        {
            const auto count = std::count_if(sites.begin(), sites.end(), [&](const auto& site)
            {
                const bool kind = entry.poiKinds.empty() ||
                    std::find(entry.poiKinds.begin(), entry.poiKinds.end(), site.kind) != entry.poiKinds.end();
                return kind && site.risk >= entry.riskMin && site.risk <= entry.riskMax;
            });
            placeable |= count >= static_cast<long long>(entry.poiCount);
        }
        CHECK(placeable);
    }
}

TEST_CASE("NextDayz contract board is reproducible and follows its rules", "[NextDayz][Contracts]")
{
    CHECK(Describe(Fresh(42)) == Describe(Fresh(42)));
    std::set<std::string> boards;
    for (uint64_t seed = 1; seed <= 60; ++seed)
    {
        CAPTURE(seed);
        const auto campaign = Fresh(seed);
        boards.insert(Describe(campaign));
        REQUIRE(campaign.board.size() == Defs().board.slots[0]);
        CHECK(FirstOfType(campaign.board, EContractType::Recon) != nullptr);   // tier 1 field work is recon
        std::set<std::string> templates, items;
        for (const auto& contract : campaign.board)
        {
            CHECK(templates.insert(contract.templateId).second);
            CHECK(contract.tier == 1);
            CHECK(contract.state == EContractState::Offered);
            CHECK(contract.reward > 0);
            for (const auto& item : contract.items) CHECK(items.insert(item.item).second);
            for (const auto& target : contract.targets) CHECK(FindPoi(Sites().at(contract.mapId), target));
        }
        // A refresh within the same cycle changes nothing.
        auto again = campaign;
        RefreshBoard(again, Defs(), Sites());
        CHECK(Describe(again) == Describe(campaign));
    }
    CHECK(boards.size() > 10);

    // Reputation tier 2: a bigger board with tier 2 work, including parcels.
    bool fetch = false;
    for (uint64_t seed = 1; seed <= 60; ++seed)
    {
        const auto campaign = Fresh(seed, Defs().reputationTiers[2]);
        CHECK(campaign.board.size() == Defs().board.slots[2]);
        fetch |= FirstOfType(campaign.board, EContractType::Fetch) != nullptr;
        for (const auto& contract : campaign.board) CHECK(contract.tier <= 2);
    }
    CHECK(fetch);
}

TEST_CASE("NextDayz deliveries are handed in from storage for gold and reputation", "[NextDayz][Contracts]")
{
    auto campaign = WithOffer(EContractType::Deliver);
    const FContract contract = *FirstOfType(campaign.board, EContractType::Deliver);
    FContractReport report;
    CHECK(AcceptContract(campaign, Defs(), contract.id) == EContractResult::WrongState);
    CHECK(HandInContract(campaign, Defs(), contract.id, report) == EContractResult::MissingItems);
    uint64_t value = 0;
    for (const auto& item : contract.items)
    {
        REQUIRE(AddToStorage(campaign, item.item, item.count + 1));
        value += SellPrice(item.item, Defs()) * static_cast<uint64_t>(item.count);
    }
    CHECK(contract.reward > value);   // worth more than selling the same items
    campaign.inField = true;
    CHECK(HandInContract(campaign, Defs(), contract.id, report) == EContractResult::InField);
    campaign.inField = false;
    REQUIRE(HandInContract(campaign, Defs(), contract.id, report) == EContractResult::Ok);
    CHECK(campaign.gold == contract.reward);
    CHECK(campaign.reputation == Defs().reputationGain[0]);
    CHECK(campaign.contractsCompleted == 1);
    CHECK(campaign.sortie.contractGold == contract.reward);
    for (const auto& item : contract.items) CHECK(StoredCount(campaign, item.item) == 1);
    CHECK_FALSE(FindContract(campaign, contract.id));
    // The emptied slot fills at the next cycle, not now.
    CHECK(campaign.board.size() == Defs().board.slots[0] - 1);
}

TEST_CASE("NextDayz recon settles before it expires and dying does not fail it", "[NextDayz][Contracts]")
{
    auto campaign = WithOffer(EContractType::Recon);
    const FContract offer = *FirstOfType(campaign.board, EContractType::Recon);
    REQUIRE(AcceptContract(campaign, Defs(), offer.id) == EContractResult::Ok);
    Inventory carried;
    // Sortie 1: visited the sites but died. Sortie 2: extracted elsewhere.
    Deploy(campaign);
    campaign.sortie.visitedPois = offer.targets;
    auto report = End(campaign, carried, false, offer.mapId);
    CHECK(report.completed.empty());
    Deploy(campaign);
    report = End(campaign, carried, true, offer.mapId);
    CHECK(report.completed.empty());
    REQUIRE(campaign.contracts.size() == 1);
    CHECK(SortiesLeft(campaign, Defs(), campaign.contracts.front()) == 1);
    // Sortie 3, the last one: visiting and extracting still pays.
    Deploy(campaign);
    for (const auto& target : offer.targets) RecordVisit(campaign.sortie, target);
    RecordVisit(campaign.sortie, offer.targets.front());
    CHECK(campaign.sortie.visitedPois.size() == offer.targets.size());
    const uint64_t goldBefore = campaign.gold;
    report = End(campaign, carried, true, offer.mapId);
    REQUIRE(report.completed.size() == 1);
    CHECK(report.expired.empty());
    CHECK(campaign.gold == goldBefore + offer.reward);
    CHECK(campaign.contracts.empty());
    REQUIRE(campaign.history.size() == 3);
    CHECK(campaign.history.back().contractGold == offer.reward);
    CHECK(campaign.history.back().contractsCompleted == 1);
    CHECK(campaign.history.back().visitedPois == offer.targets);
}

TEST_CASE("NextDayz field contracts expire after three sorties and cost reputation within the tier", "[NextDayz][Contracts]")
{
    auto campaign = WithOffer(EContractType::Recon, 4);
    REQUIRE(ReputationTier(campaign.reputation, Defs()) == 1);
    const FContract offer = *FirstOfType(campaign.board, EContractType::Recon);
    REQUIRE(AcceptContract(campaign, Defs(), offer.id) == EContractResult::Ok);
    Inventory carried;
    FContractReport report;
    for (int sortie = 0; sortie < 3; ++sortie)
    {
        Deploy(campaign);
        report = End(campaign, carried, true, offer.mapId);
    }
    REQUIRE(report.expired.size() == 1);
    CHECK(campaign.contracts.empty());
    CHECK(campaign.reputation == 3);
    // At the bottom of tier 1, another failure keeps the tier.
    FCampaign floor = WithOffer(EContractType::Recon, 3);
    const auto id = FirstOfType(floor.board, EContractType::Recon)->id;
    REQUIRE(AcceptContract(floor, Defs(), id) == EContractResult::Ok);
    CHECK(AbandonContract(floor, Defs(), id, report) == EContractResult::Ok);
    CHECK(floor.reputation == 3);
}

TEST_CASE("NextDayz field contracts are limited and unaccepted offers rotate", "[NextDayz][Contracts]")
{
    auto campaign = Fresh(7, Defs().reputationTiers[3]);
    std::vector<uint64_t> field;
    for (const auto& contract : campaign.board)
        if (IsFieldContract(contract.type)) field.push_back(contract.id);
    // Make sure three field offers exist to test the limit.
    for (uint64_t seed = 8; field.size() < 3 && seed < 400; ++seed)
    {
        campaign = Fresh(seed, Defs().reputationTiers[3]);
        field.clear();
        for (const auto& contract : campaign.board)
            if (IsFieldContract(contract.type)) field.push_back(contract.id);
    }
    REQUIRE(field.size() >= 3);
    CHECK(AcceptContract(campaign, Defs(), field[0]) == EContractResult::Ok);
    CHECK(AcceptContract(campaign, Defs(), field[1]) == EContractResult::Ok);
    CHECK(AcceptContract(campaign, Defs(), field[2]) == EContractResult::TooMany);
    const auto remaining = field[2];
    Inventory carried;
    Deploy(campaign);
    End(campaign, carried, true, "");
    CHECK(FindContract(campaign, remaining));
    Deploy(campaign);
    End(campaign, carried, true, "");
    CHECK_FALSE(FindContract(campaign, remaining));     // replaced after two finished sorties
    CHECK(campaign.board.size() == Defs().board.slots[3]);
}

TEST_CASE("NextDayz fetch contracts pay for the carried parcel and purge it when they end", "[NextDayz][Contracts]")
{
    auto campaign = WithOffer(EContractType::Fetch, Defs().reputationTiers[2]);
    const FContract offer = *FirstOfType(campaign.board, EContractType::Fetch);
    REQUIRE(AcceptContract(campaign, Defs(), offer.id) == EContractResult::Ok);
    REQUIRE(ActiveParcels(campaign).size() == 1);
    CHECK(ActiveParcels(campaign).front().site == offer.targets.front());
    CHECK(ParcelIsActive(campaign, offer.parcel));

    // Died with it (the parcel lies in a pile), then picked it up again and extracted.
    Inventory carried;
    Deploy(campaign);
    auto report = End(campaign, carried, false, offer.mapId);
    CHECK(report.completed.empty());
    Deploy(campaign);
    REQUIRE(carried.TryAdd(offer.parcel, "Parcel", EItemKind::Misc, 1));
    report = End(campaign, carried, true, offer.mapId);
    REQUIRE(report.completed.size() == 1);
    CHECK(carried.CountOf(offer.parcel) == 0);
    CHECK(campaign.contracts.empty());
    if (!offer.itemReward.empty()) CHECK(StoredCount(campaign, offer.itemReward) == 1);
    for (const auto& stack : campaign.storage) CHECK_FALSE(IsContractItem(stack.defId));

    // Abandoning reports the parcel for purging; the world loses every copy.
    auto other = WithOffer(EContractType::Fetch, Defs().reputationTiers[2]);
    const auto* fetch = FirstOfType(other.board, EContractType::Fetch);
    const std::string parcel = fetch->parcel;
    REQUIRE(AcceptContract(other, Defs(), fetch->id) == EContractResult::Ok);
    report = {};
    REQUIRE(AbandonContract(other, Defs(), other.contracts.front().id, report) == EContractResult::Ok);
    REQUIRE(report.purgeParcels == std::vector<std::string>{parcel});
    FLootWorldState world;
    world.seconds = 50.0;
    FSavedLootEntry spot;
    spot.id = "spot:1";
    spot.source = ELootSource::Spot;
    spot.contents = {{parcel, 1, 0}};
    FSavedLootEntry pile;
    pile.id = "pile:1";
    pile.source = ELootSource::Pile;
    pile.contents = {{parcel, 1, 0}, {"bandage", 2, 0}};
    FSavedLootEntry lone = pile;
    lone.id = "pile:2";
    lone.contents = {{parcel, 1, 0}};
    world.entries = {spot, pile, lone};
    CHECK(PurgeItemFromWorld(world, parcel) == 3);
    CHECK(world.entries[0].contents.empty());
    CHECK(world.entries[0].state == FLootEntry::EState::Cooldown);
    CHECK(world.entries[0].slot.cooldownUntil == 50.0);
    REQUIRE(world.entries[1].contents.size() == 1);
    CHECK(world.entries[1].contents.front().itemId == "bandage");
    CHECK(world.entries[2].state == FLootEntry::EState::Empty);
}

TEST_CASE("NextDayz parcels can be neither stored nor sold", "[NextDayz][Contracts]")
{
    FCampaign campaign;
    Inventory carried;
    FItemInstanceId parcel = 0;
    REQUIRE(carried.TryAdd("contract_parcel", "Parcel", EItemKind::Misc, 1, &parcel));
    REQUIRE(carried.TryAdd("bandage", "Bandage", EItemKind::Consumable, 2));
    CHECK_FALSE(campaign.Store(carried, parcel));
    campaign.StoreAll(carried);
    REQUIRE(campaign.storage.size() == 1);
    CHECK(campaign.storage.front().defId == "bandage");
    CHECK(SellPrice("contract_parcel", Defs()) == 0);
}

TEST_CASE("NextDayz save v7 keeps the contract board and migrates v6 saves", "[NextDayz][Contracts][Campaign]")
{
    const auto path = std::filesystem::temp_directory_path() /
        fmt::format("nextdayz-contracts-{}.json", std::chrono::steady_clock::now().time_since_epoch().count());
    struct Cleanup
    {
        std::filesystem::path path;
        ~Cleanup() { std::error_code ec; for (const auto* suffix : {"", ".bak", ".tmp"}) std::filesystem::remove(path.string() + suffix, ec); }
    } cleanup{path};
    FCampaignSave saved;
    saved.map = "contracts-test";
    saved.inventory = Inventory{}.Capture();
    saved.campaign = WithOffer(EContractType::Fetch, Defs().reputationTiers[2]);
    const auto id = FirstOfType(saved.campaign.board, EContractType::Fetch)->id;
    REQUIRE(AcceptContract(saved.campaign, Defs(), id) == EContractResult::Ok);
    saved.campaign.sortie.visitedPois = {"Castle Loket"};
    std::string error;
    REQUIRE(WriteCampaignSave(path, saved, error));
    FCampaignSave read;
    REQUIRE(ReadCampaignSave(path, read, error));
    CHECK(Describe(read.campaign) == Describe(saved.campaign));
    REQUIRE(read.campaign.contracts.size() == 1);
    CHECK(read.campaign.contracts.front().state == EContractState::Accepted);
    CHECK(read.campaign.contracts.front().parcel == saved.campaign.contracts.front().parcel);
    CHECK(read.campaign.boardRng == saved.campaign.boardRng);
    CHECK(read.campaign.sortie.visitedPois == saved.campaign.sortie.visitedPois);

    auto invalid = saved;
    invalid.campaign.contracts.push_back(invalid.campaign.contracts.front());
    CHECK_FALSE(WriteCampaignSave(path, invalid, error));   // duplicate id and parcel
    invalid = saved;
    invalid.campaign.contracts.front().state = EContractState::Offered;
    CHECK_FALSE(WriteCampaignSave(path, invalid, error));

    nlohmann::json document;
    { std::ifstream stream(path); stream >> document; }
    REQUIRE(document.at("version") == 8);
    document["version"] = 6;
    auto& campaign = document["state"]["campaign"];
    for (const auto* key : {"boardRng", "boardCycle", "contractSerial", "board", "contracts", "contractsCompleted"}) campaign.erase(key);
    for (const auto* key : {"visitedPois", "contractGold", "contractsCompleted"}) campaign["sortie"].erase(key);
    { std::ofstream stream(path, std::ios::trunc); stream << document; }
    REQUIRE(ReadCampaignSave(path, read, error));
    CHECK(read.campaign.board.empty());
    CHECK(read.campaign.contracts.empty());
    CHECK(read.campaign.boardCycle == 0);
    CHECK(read.campaign.sortie.visitedPois.empty());
    // The first refresh after the upgrade fills the board.
    RefreshBoard(read.campaign, Defs(), Sites());
    CHECK(read.campaign.board.size() == BoardSlots(read.campaign, Defs()));
}

TEST_CASE("NextDayz reputation tiers open contract tiers and board slots", "[NextDayz][Contracts][Reputation]")
{
    FCampaign campaign;
    for (const auto& [reputation, contractTier, slots] : {std::tuple{0u, 1u, 3u}, std::tuple{7u, 1u, 3u},
                                                          std::tuple{8u, 2u, 4u}, std::tuple{15u, 3u, 4u}})
    {
        CAPTURE(reputation);
        campaign.reputation = reputation;
        CHECK(AllowedContractTier(campaign, Defs()) == contractTier);
        CHECK(BoardSlots(campaign, Defs()) == slots);
    }
}
