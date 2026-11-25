#include "Engine/Common/CoreMinimal.hpp"
#include <catch2/catch_test_macros.hpp>
#include "Application/Game/NextDayz/World/Economy.hpp"
#include "Application/Game/NextDayz/World/Contracts.hpp"
#include "Application/Game/NextDayz/World/ExpeditionMaps.hpp"
#include <nlohmann/json.hpp>

using namespace NextDayz;

namespace
{
    const std::filesystem::path sourceRoot = GK_NEXT_SOURCE_DIR;

    nlohmann::json ShippedTable()
    {
        std::ifstream stream(sourceRoot / "assets/configs/nextdayz_economy.json");
        return nlohmann::json::parse(stream);
    }

    FEconomyDefs ShippedDefs()
    {
        FEconomyDefs defs;
        std::string error;
        REQUIRE(ParseEconomyDefs(ShippedTable().dump(), defs, error));
        return defs;
    }

    void Deploy(FCampaign& campaign)
    {
        campaign.inField = true;
        ++campaign.sorties;
        ++campaign.sortieId;
    }

    void Return(FCampaign& campaign, const FEconomyDefs& defs, std::string outcome = "extracted")
    {
        campaign.inField = false;
        FSortieRecord record;
        record.sortieId = campaign.sortieId;
        record.outcome = std::move(outcome);
        FinishSortie(campaign, defs, std::move(record));
    }
}

TEST_CASE("NextDayz economy table prices every item and never lets a trade loop pay", "[NextDayz][Economy]")
{
    const auto defs = ShippedDefs();
    for (const auto& item : AllItemDefs())
    {
        CAPTURE(item.id);
        REQUIRE(defs.sell.contains(std::string(item.id)));
        CHECK((defs.sell.at(std::string(item.id)) > 0) != IsContractItem(item.id));
    }
    CHECK(defs.sell.size() == AllItemDefs().size());
    // Prices players already know carry over from the old extraction table.
    CHECK(SellPrice("ak", defs) == 120);
    CHECK(SellPrice("svd", defs) == 200);
    CHECK(SellPrice("food_can", defs) == 5);
    CHECK(SellPrice("signal_flare", defs) == 75);
    REQUIRE_FALSE(defs.trader.empty());
    for (const auto& entry : defs.trader)
    {
        CAPTURE(entry.item);
        CHECK(entry.buy >= 2 * SellPrice(entry.item, defs));
        CHECK(entry.pack <= FindItemDef(entry.item)->maxStack);
        CHECK(entry.tier < defs.reputationTiers.size());
    }
    // The confirmed spending decisions (design §3.2).
    const auto* flare = FindTraderEntry(defs, "signal_flare");
    REQUIRE(flare);
    CHECK(flare->buy == 300);
    CHECK(flare->tier == 2);
    CHECK(flare->stock == 1);
    CHECK(defs.insertionFee == 40);
    CHECK(defs.warehouseBase == 40);
    REQUIRE(defs.warehouseSteps.size() == 3);
    CHECK(defs.warehouseSteps.back().capacity == 130);
    CHECK(FindTraderEntry(defs, "ak")->buy == 360);
    CHECK(FindTraderEntry(defs, "ammo_9mm")->buy == 4);
}

TEST_CASE("NextDayz economy table rejects incomplete or exploitable prices", "[NextDayz][Economy]")
{
    const auto reject = [](const nlohmann::json& document)
    {
        FEconomyDefs defs;
        std::string error;
        const bool parsed = ParseEconomyDefs(document.dump(), defs, error);
        CAPTURE(error);
        return !parsed && !error.empty();
    };
    auto table = ShippedTable();
    table["sell"].erase("bandage");
    CHECK(reject(table));
    table = ShippedTable();
    table["sell"]["not_an_item"] = 5;
    CHECK(reject(table));
    table = ShippedTable();
    table["trader"].push_back({{"item", "svd"}, {"tier", 0}, {"stock", 1}, {"pack", 1}, {"buy", 399}});
    CHECK(reject(table));   // 399 < 2 x 200
    table = ShippedTable();
    table["trader"][0]["pack"] = 99;
    CHECK(reject(table));   // a pack must fit in one stack
    table = ShippedTable();
    table["trader"][0]["tier"] = 9;
    CHECK(reject(table));
    table = ShippedTable();
    table["trader"].push_back(table["trader"][0]);
    CHECK(reject(table));
    table = ShippedTable();
    table["surprise"] = true;
    CHECK(reject(table));
    table = ShippedTable();
    table["buyMarkup"] = 1.5;
    CHECK(reject(table));
}

TEST_CASE("NextDayz trader purchases land in storage and selling them back always loses", "[NextDayz][Economy]")
{
    const auto defs = ShippedDefs();
    FCampaign campaign;
    CHECK(Buy(campaign, defs, "bandage") == ETradeResult::NotEnoughGold);
    campaign.gold = 1000;
    REQUIRE(Buy(campaign, defs, "bandage") == ETradeResult::Ok);
    const auto* bandage = FindTraderEntry(defs, "bandage");
    CHECK(campaign.gold == 1000 - bandage->buy * bandage->pack);
    CHECK(campaign.sortie.spent == bandage->buy * bandage->pack);
    REQUIRE(campaign.storage.size() == 1);
    CHECK(campaign.storage.front().count == bandage->pack);
    // A second pack tops up the plain stack up to its maximum, then opens a new one.
    REQUIRE(Buy(campaign, defs, "bandage") == ETradeResult::Ok);
    REQUIRE(Buy(campaign, defs, "bandage") == ETradeResult::Ok);
    REQUIRE(campaign.storage.size() == 2);
    CHECK(campaign.storage[0].count == FindItemDef("bandage")->maxStack);
    CHECK(campaign.storage[1].count == 2);

    REQUIRE(Buy(campaign, defs, "pistol") == ETradeResult::Ok);
    const auto& pistol = campaign.storage.back();
    CHECK(pistol.defId == "pistol");
    CHECK(pistol.loadedAmmo == 0);
    CHECK(pistol.attachments == FWeaponAttachments{});
    const uint64_t beforeSale = campaign.gold;
    REQUIRE(campaign.Sell(pistol.instanceId, 1, defs));
    CHECK(campaign.gold - beforeSale < FindTraderEntry(defs, "pistol")->buy);

    CHECK(Buy(campaign, defs, "svd") == ETradeResult::NotStocked);
    campaign.inField = true;
    CHECK(Buy(campaign, defs, "bandage") == ETradeResult::InField);
}

TEST_CASE("NextDayz trader restocks once per finished sortie, never while deployed", "[NextDayz][Economy]")
{
    const auto defs = ShippedDefs();
    FCampaign campaign;
    campaign.gold = 100000;
    const auto* medkit = FindTraderEntry(defs, "medkit");
    REQUIRE(medkit);
    for (int bought = 0; bought < medkit->stock; ++bought)
        REQUIRE(Buy(campaign, defs, "medkit") == ETradeResult::Ok);
    CHECK(TraderRemaining(campaign, *medkit) == 0);
    CHECK(Buy(campaign, defs, "medkit") == ETradeResult::SoldOut);
    // Deploying does not restock; neither does reading a fresh copy of the save.
    Deploy(campaign);
    CHECK(TraderRemaining(campaign, *medkit) == 0);
    Return(campaign, defs, "died");
    CHECK(TraderRemaining(campaign, *medkit) == medkit->stock);
    REQUIRE(Buy(campaign, defs, "medkit") == ETradeResult::Ok);
    CHECK(TraderRemaining(campaign, *medkit) == medkit->stock - 1);
    // A migrated campaign (no cycle recorded yet) starts with full shelves.
    FCampaign migrated;
    migrated.sorties = 7;
    CHECK(TraderRemaining(migrated, *medkit) == medkit->stock);
}

TEST_CASE("NextDayz trader shelves unlock with the reputation tier", "[NextDayz][Economy]")
{
    const auto defs = ShippedDefs();
    FCampaign campaign;
    campaign.gold = 100000;
    CHECK(Buy(campaign, defs, "signal_flare") == ETradeResult::Locked);
    campaign.reputation = defs.reputationTiers[2] - 1;
    CHECK(ReputationTier(campaign.reputation, defs) == 1);
    CHECK(Buy(campaign, defs, "signal_flare") == ETradeResult::Locked);
    campaign.reputation = defs.reputationTiers[2];
    REQUIRE(Buy(campaign, defs, "signal_flare") == ETradeResult::Ok);
    // One flare per cycle.
    CHECK(Buy(campaign, defs, "signal_flare") == ETradeResult::SoldOut);
}

TEST_CASE("NextDayz paid landing charges once, refunds on cancel and is used up by deploying", "[NextDayz][Economy]")
{
    const auto defs = ShippedDefs();
    FCampaign campaign;
    CHECK(PayInsertion(campaign, defs, "cz_loket_riverbend", 1) == ETradeResult::NotEnoughGold);
    campaign.gold = 100;
    REQUIRE(PayInsertion(campaign, defs, "cz_loket_riverbend", 1) == ETradeResult::Ok);
    CHECK(campaign.gold == 60);
    REQUIRE(PayInsertion(campaign, defs, "cz_loket_riverbend", 3) == ETradeResult::Ok);
    CHECK(campaign.gold == 60);
    CHECK(campaign.sortie.insertionPoint == 3);
    CancelInsertion(campaign);
    CHECK(campaign.gold == 100);
    CHECK(campaign.sortie.spent == 0);
    CHECK(campaign.sortie.insertionMap.empty());

    // Deployed somewhere else: refunded.
    REQUIRE(PayInsertion(campaign, defs, "cz_loket_riverbend", 2) == ETradeResult::Ok);
    CHECK_FALSE(TakeInsertion(campaign, "ke_talek_mara").has_value());
    CHECK(campaign.gold == 100);

    REQUIRE(PayInsertion(campaign, defs, "cz_loket_riverbend", 2) == ETradeResult::Ok);
    const auto point = TakeInsertion(campaign, "cz_loket_riverbend");
    REQUIRE(point.has_value());
    CHECK(*point == 2);
    CHECK(campaign.gold == 60);
    CHECK(campaign.sortie.insertionMap.empty());
    Deploy(campaign);
    CHECK(PayInsertion(campaign, defs, "cz_loket_riverbend", 0) == ETradeResult::InField);
    Return(campaign, defs);
    REQUIRE(campaign.history.size() == 1);
    CHECK(campaign.history.back().spentBefore == 40);
    CHECK(SortieNet(campaign.history.back()) == -40);
    CHECK(campaign.sortie.spent == 0);
}

TEST_CASE("NextDayz sortie history keeps the most recent records", "[NextDayz][Economy]")
{
    const auto defs = ShippedDefs();
    FCampaign campaign;
    for (int index = 0; index < 60; ++index)
    {
        Deploy(campaign);
        Return(campaign, defs);
    }
    REQUIRE(campaign.history.size() == kSortieHistory);
    CHECK(campaign.history.front().sortieId == 11);
    CHECK(campaign.history.back().sortieId == 60);
}

TEST_CASE("NextDayz save v6 keeps the economy and migrates v5 saves with defaults", "[NextDayz][Economy][Campaign]")
{
    const auto path = std::filesystem::temp_directory_path() /
        fmt::format("nextdayz-economy-{}.json", std::chrono::steady_clock::now().time_since_epoch().count());
    struct Cleanup
    {
        std::filesystem::path path;
        ~Cleanup() { std::error_code ec; for (const auto* suffix : {"", ".bak", ".tmp"}) std::filesystem::remove(path.string() + suffix, ec); }
    } cleanup{path};
    const auto defs = ShippedDefs();
    FCampaignSave saved;
    saved.map = "economy-test";
    saved.inventory = Inventory{}.Capture();
    saved.campaign.gold = 500;
    REQUIRE(Buy(saved.campaign, defs, "medkit") == ETradeResult::Ok);
    REQUIRE(PayInsertion(saved.campaign, defs, "cz_loket_riverbend", 4) == ETradeResult::Ok);
    Deploy(saved.campaign);
    Return(saved.campaign, defs);
    REQUIRE(PayInsertion(saved.campaign, defs, "cz_loket_riverbend", 1) == ETradeResult::Ok);
    saved.campaign.reputation = 4;
    std::string error;
    REQUIRE(WriteCampaignSave(path, saved, error));
    FCampaignSave read;
    REQUIRE(ReadCampaignSave(path, read, error));
    CHECK(read.campaign.reputation == 4);
    CHECK(read.campaign.traderStock == saved.campaign.traderStock);
    CHECK(read.campaign.traderCycle == saved.campaign.traderCycle);
    CHECK(read.campaign.sortie.insertionMap == "cz_loket_riverbend");
    CHECK(read.campaign.sortie.insertionPoint == 1);
    CHECK(read.campaign.sortie.spent == 40);
    REQUIRE(read.campaign.history.size() == 1);
    CHECK(read.campaign.history.front().spentBefore == 115);

    auto invalid = saved;
    invalid.campaign.traderStock["not_an_item"] = 1;
    CHECK_FALSE(WriteCampaignSave(path, invalid, error));
    invalid = saved;
    invalid.campaign.sortie.insertionPaid = 1000;
    CHECK_FALSE(WriteCampaignSave(path, invalid, error));

    nlohmann::json document;
    { std::ifstream stream(path); stream >> document; }
    REQUIRE(document.at("version") == 8);
    document["version"] = 5;
    auto& campaign = document["state"]["campaign"];
    for (const auto* key : {"reputation", "traderStock", "traderCycle", "sortie", "history"}) campaign.erase(key);
    { std::ofstream stream(path, std::ios::trunc); stream << document; }
    REQUIRE(ReadCampaignSave(path, read, error));
    CHECK(read.campaign.gold == saved.campaign.gold);
    CHECK(read.campaign.reputation == 0);
    CHECK(read.campaign.traderStock.empty());
    CHECK(read.campaign.history.empty());
    CHECK(read.campaign.sortie.insertionMap.empty());
    // Full shelves on the first visit after the upgrade.
    CHECK(TraderRemaining(read.campaign, *FindTraderEntry(defs, "medkit")) == FindTraderEntry(defs, "medkit")->stock);
}

TEST_CASE("NextDayz landing points come from the published generation record", "[NextDayz][Economy][WorldMaps]")
{
    std::vector<FExpeditionMap> maps;
    std::string error;
    REQUIRE(LoadExpeditionMaps(sourceRoot / "assets/configs/nextdayz_maps.json", maps, error));
    const auto* loket = FindExpeditionMap(maps, "cz_loket_riverbend");
    REQUIRE(loket);
    std::vector<FLandingPoint> points;
    REQUIRE(LoadLandingPoints(sourceRoot / GenerationRecordPath(*loket), points, error));
    REQUIRE(points.size() == 5);
    // SCAD (x, y) is game (x, -z): the first spawn is [-460, -300].
    CHECK(points[0].position == glm::vec2(-460.0f, 300.0f));
    for (const auto& point : points)
    {
        CHECK_FALSE(point.nearSite.empty());
        CHECK(point.siteDistance > 0.0f);
    }
    CHECK_FALSE(LoadLandingPoints(sourceRoot / "assets/configs/does-not-exist.json", points, error));
}

namespace
{
    // `stacks` single garments that cannot merge with each other.
    void FillStorage(FCampaign& campaign, int stacks)
    {
        for (int index = 0; stacks > 0; ++index)
        {
            const auto& item = AllItemDefs()[static_cast<size_t>(index) % AllItemDefs().size()];
            if (item.kind != EItemKind::Clothing || item.maxStack != 1) continue;
            FItemInstance stack;
            stack.defId = std::string(item.id);
            REQUIRE(campaign.Put(std::move(stack)));
            --stacks;
        }
    }
}

TEST_CASE("NextDayz storage merges plain stacks and keeps loaded weapons apart", "[NextDayz][Economy][Warehouse]")
{
    FCampaign campaign;
    Inventory carried;
    REQUIRE(carried.TryAdd("food_can", "Food", EItemKind::Consumable, 2));
    REQUIRE(carried.TryAdd("ammo_9mm", "Ammo", EItemKind::Ammo, 20));
    FItemInstanceId gun = 0;
    REQUIRE(carried.TryAdd("pistol", "Makarov", EItemKind::Weapon, 1, &gun));
    REQUIRE(carried.SetLoadedAmmo(gun, 8));
    campaign.StoreAll(carried);
    REQUIRE(carried.TryAdd("food_can", "Food", EItemKind::Consumable, 1));
    REQUIRE(carried.TryAdd("ammo_9mm", "Ammo", EItemKind::Ammo, 10));
    REQUIRE(carried.TryAdd("pistol", "Makarov", EItemKind::Weapon, 1));
    campaign.StoreAll(carried);
    CHECK(StoredCount(campaign, "food_can") == 3);
    CHECK(StoredCount(campaign, "ammo_9mm") == 30);
    // Food 3 in one stack, 9x18 as 24 + 6, the loaded pistol and the empty pistol.
    CHECK(campaign.storage.size() == 5);
    CHECK(std::count_if(campaign.storage.begin(), campaign.storage.end(),
                        [](const auto& stack) { return stack.defId == "pistol" && stack.loadedAmmo == 8; }) == 1);
}

TEST_CASE("NextDayz warehouse capacity blocks deploying buying and storing but never loses gear", "[NextDayz][Economy][Warehouse]")
{
    const auto defs = ShippedDefs();
    FCampaign campaign;
    campaign.gold = 10000;
    CHECK(WarehouseCapacity(campaign, defs) == 40);
    FillStorage(campaign, 39);
    REQUIRE(Buy(campaign, defs, "medkit") == ETradeResult::Ok);
    CHECK(campaign.storage.size() == 40);
    CHECK(CanDeploy(campaign, defs) == ETradeResult::Ok);
    CHECK(Buy(campaign, defs, "pistol") == ETradeResult::StorageFull);   // would open stack 41
    CHECK(campaign.storage.size() == 40);

    Inventory carried;
    FItemInstanceId helmet = 0;
    REQUIRE(carried.TryAdd("helmet", "Helmet", EItemKind::Clothing, 1, &helmet));
    CHECK(StoreCarried(campaign, defs, carried, helmet) == ETradeResult::StorageFull);
    CHECK(carried.CountOf("helmet") == 1);
    CHECK(StoreCarried(campaign, defs, carried, 0) == ETradeResult::StorageFull);
    CHECK(carried.CountOf("helmet") == 1);

    // Extraction stores everything regardless of capacity.
    REQUIRE(carried.TryAdd("svd", "SVD", EItemKind::Weapon, 1));
    campaign.StoreAll(carried);
    CHECK(campaign.storage.size() == 42);
    CHECK(WarehouseOver(campaign, defs));
    CHECK(CanDeploy(campaign, defs) == ETradeResult::StorageFull);
    CHECK(Buy(campaign, defs, "water_bottle") == ETradeResult::StorageFull);
    // Selling brings it back within capacity.
    REQUIRE(campaign.Sell(campaign.storage.back().instanceId, 1, defs));
    REQUIRE(campaign.Sell(campaign.storage.back().instanceId, 1, defs));
    CHECK(campaign.storage.size() == 40);
    CHECK(CanDeploy(campaign, defs) == ETradeResult::Ok);
}

TEST_CASE("NextDayz warehouse expands through its paid levels", "[NextDayz][Economy][Warehouse]")
{
    const auto defs = ShippedDefs();
    FCampaign campaign;
    campaign.gold = 299;
    CHECK(UpgradeWarehouse(campaign, defs) == ETradeResult::NotEnoughGold);
    campaign.gold = 300 + 800 + 2000;
    REQUIRE(UpgradeWarehouse(campaign, defs) == ETradeResult::Ok);
    CHECK(WarehouseCapacity(campaign, defs) == 60);
    CHECK(campaign.sortie.spent == 300);
    REQUIRE(NextWarehouseStep(campaign, defs));
    CHECK(NextWarehouseStep(campaign, defs)->capacity == 90);
    REQUIRE(UpgradeWarehouse(campaign, defs) == ETradeResult::Ok);
    REQUIRE(UpgradeWarehouse(campaign, defs) == ETradeResult::Ok);
    CHECK(WarehouseCapacity(campaign, defs) == 130);
    CHECK(campaign.gold == 0);
    CHECK(NextWarehouseStep(campaign, defs) == nullptr);
    CHECK(UpgradeWarehouse(campaign, defs) == ETradeResult::Invalid);
    campaign.inField = true;
    campaign.warehouseLevel = 0;
    CHECK(UpgradeWarehouse(campaign, defs) == ETradeResult::InField);
}

TEST_CASE("NextDayz save v8 gives migrated warehouses enough room", "[NextDayz][Economy][Warehouse][Campaign]")
{
    const auto path = std::filesystem::temp_directory_path() /
        fmt::format("nextdayz-warehouse-{}.json", std::chrono::steady_clock::now().time_since_epoch().count());
    struct Cleanup
    {
        std::filesystem::path path;
        ~Cleanup() { std::error_code ec; for (const auto* suffix : {"", ".bak", ".tmp"}) std::filesystem::remove(path.string() + suffix, ec); }
    } cleanup{path};
    const auto defs = ShippedDefs();
    const auto migrate = [&](int stacks)
    {
        FCampaignSave saved;
        saved.map = "warehouse-test";
        saved.inventory = Inventory{}.Capture();
        FillStorage(saved.campaign, stacks);
        std::string error;
        REQUIRE(WriteCampaignSave(path, saved, error));
        nlohmann::json document;
        { std::ifstream stream(path); stream >> document; }
        REQUIRE(document.at("version") == 8);
        document["version"] = 7;
        document["state"]["campaign"].erase("warehouseLevel");
        document["state"]["campaign"].erase("warehouseFloor");
        { std::ofstream stream(path, std::ios::trunc); stream << document; }
        FCampaignSave read;
        REQUIRE(ReadCampaignSave(path, read, error));
        CHECK(read.campaign.warehouseFloor == static_cast<uint32_t>(stacks));
        ResolveWarehouse(read.campaign, defs);
        CHECK_FALSE(WarehouseOver(read.campaign, defs));
        return read.campaign;
    };
    const auto small = migrate(12);
    CHECK(small.warehouseLevel == 0);
    CHECK(small.warehouseFloor == 0);
    const auto mid = migrate(70);
    CHECK(mid.warehouseLevel == 2);
    CHECK(mid.warehouseFloor == 0);
    CHECK(WarehouseCapacity(mid, defs) == 90);
    const auto huge = migrate(150);
    CHECK(huge.warehouseLevel == 3);
    CHECK(WarehouseCapacity(huge, defs) == 150);
    // Resolving again changes nothing.
    auto again = huge;
    ResolveWarehouse(again, defs);
    CHECK(again.warehouseLevel == huge.warehouseLevel);
    CHECK(again.warehouseFloor == huge.warehouseFloor);
}

TEST_CASE("NextDayz reputation tiers unlock shelves at their thresholds", "[NextDayz][Economy][Reputation]")
{
    const auto defs = ShippedDefs();
    REQUIRE(defs.reputationTiers == std::vector<uint32_t>{0, 3, 8, 15});
    FCampaign campaign;
    campaign.gold = 10000;
    const auto* rounds = FindTraderEntry(defs, "ammo_545");
    REQUIRE(rounds);
    REQUIRE(rounds->tier == 1);
    campaign.reputation = 2;
    CHECK(ReputationTier(campaign.reputation, defs) == 0);
    CHECK(CanBuy(campaign, defs, *rounds) == ETradeResult::Locked);
    campaign.reputation = 3;
    CHECK(ReputationTier(campaign.reputation, defs) == 1);
    CHECK(CanBuy(campaign, defs, *rounds) == ETradeResult::Ok);
    CHECK(ReputationTier(7, defs) == 1);
    CHECK(ReputationTier(8, defs) == 2);
    CHECK(ReputationTier(14, defs) == 2);
    CHECK(ReputationTier(15, defs) == 3);
    CHECK(ReputationTier(1000, defs) == 3);
}
