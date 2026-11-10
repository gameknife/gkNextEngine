#include "Engine/Common/CoreMinimal.hpp"
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "Application/Game/NextDayz/World/Campaign.hpp"
#include <nlohmann/json.hpp>

using namespace NextDayz;

TEST_CASE("NextDayz mounted parts survive storage world piles and legacy save migration", "[NextDayz][Campaign][Attachments]")
{
    const auto path = std::filesystem::temp_directory_path() /
        fmt::format("nextdayz-attachments-{}.json", std::chrono::steady_clock::now().time_since_epoch().count());
    struct Cleanup
    {
        std::filesystem::path path;
        ~Cleanup() { std::error_code ec; for (const auto* suffix : {"", ".bak", ".tmp"}) std::filesystem::remove(path.string() + suffix, ec); }
    } cleanup{path};
    Inventory inventory;
    FItemInstanceId gun = 0, part = 0;
    REQUIRE(inventory.TryAdd("ak", "AK", EItemKind::Weapon, 1, &gun));
    REQUIRE(inventory.TryAdd("optic_4x", "Scope", EItemKind::Attachment, 1, &part));
    REQUIRE(inventory.TryAttach(gun, part));
    REQUIRE(inventory.SetLoadedAmmo(gun, 21));
    const auto loadout = inventory.FindInstance(gun)->attachments;
    FCampaign campaign;
    REQUIRE(campaign.Store(inventory, gun));
    const auto storageId = campaign.storage.front().instanceId;
    REQUIRE(campaign.storage.front().attachments == loadout);
    REQUIRE(campaign.Take(inventory, storageId, 1, false));
    REQUIRE(inventory.FindFirst("ak")->attachments == loadout);
    REQUIRE(inventory.FindFirst("ak")->loadedAmmo == 21);
    FCampaignSave saved;
    saved.map = "attachments-test";
    saved.inventory = inventory.Capture();
    saved.campaign = campaign;
    FSavedLootEntry pile;
    pile.id = "pile:1";
    pile.source = ELootSource::Pile;
    pile.contents.push_back({"ak", 1, 21, loadout});
    saved.world.entries.push_back(pile);
    // Also cover stored guns and inactive-map piles.
    campaign.StoreAll(inventory);
    saved.campaign = campaign;
    saved.maps["inactive"] = {saved.world, saved.time, 1};
    std::string error;
    REQUIRE(WriteCampaignSave(path, saved, error));
    FCampaignSave read;
    REQUIRE(ReadCampaignSave(path, read, error));
    REQUIRE(read.inventory.instances.front().attachments == loadout);
    REQUIRE(read.campaign.storage.front().attachments == loadout);
    REQUIRE(read.world.entries.front().contents.front().attachments == loadout);
    REQUIRE(read.maps.at("inactive").world.entries.front().contents.front().attachments == loadout);
    auto invalid = saved;
    invalid.world.entries.front().contents.front().attachments[0] = EWeaponAttachment::VerticalGrip;
    REQUIRE_FALSE(WriteCampaignSave(path, invalid, error));

    nlohmann::json document;
    { std::ifstream stream(path); stream >> document; }
    REQUIRE(document.at("version") == 5);
    document["version"] = 3;
    auto& state = document["state"];
    for (auto& container : state["inventory"]["containers"])
    {
        container["capacity"] = container["weaponOnly"].get<bool>() ? 10 :
            container["width"].get<int>() * container["height"].get<int>();
        container.erase("width");
        container.erase("height");
    }
    for (auto& item : state["inventory"]["instances"]) item.erase("attachments");
    for (auto& item : state["campaign"]["storage"]) item.erase("attachments");
    const auto stripWorld = [](auto& world)
    { for (auto& entry : world["entries"]) for (auto& item : entry["contents"]) item.erase("attachments"); };
    stripWorld(state["world"]);
    for (auto& map : state["maps"].items()) stripWorld(map.value()["world"]);
    { std::ofstream stream(path, std::ios::trunc); stream << document; }
    REQUIRE(ReadCampaignSave(path, read, error));
    REQUIRE(read.inventory.instances.front().attachments == FWeaponAttachments{});
    REQUIRE(read.campaign.storage.front().attachments == FWeaponAttachments{});
    REQUIRE(read.world.entries.front().contents.front().attachments == FWeaponAttachments{});
    state["inventory"]["instances"][0]["attachments"] = {0, 0};
    { std::ofstream stream(path, std::ios::trunc); stream << document; }
    REQUIRE_FALSE(ReadCampaignSave(path, read, error));
    state["inventory"]["instances"][0]["attachments"] = {257, 0, 0, 0, 0};
    { std::ofstream stream(path, std::ios::trunc); stream << document; }
    REQUIRE_FALSE(ReadCampaignSave(path, read, error));
}

TEST_CASE("NextDayz inventory snapshot restores worn containers and rejects invalid ownership", "[NextDayz][Campaign]")
{
    Inventory original;
    FItemInstanceId pack = 0, gun = 0;
    REQUIRE(original.TryAdd("backpack", "Backpack", EItemKind::Clothing, 1, &pack, kWornContainer));
    REQUIRE(original.TryAdd("ak", "AK", EItemKind::Weapon, 1, &gun));
    REQUIRE(original.SetLoadedAmmo(gun, 17));
    REQUIRE(original.TryAdd("food_can", "Food", EItemKind::Consumable, 3));
    const auto state = original.Capture();
    Inventory restored;
    REQUIRE(restored.Restore(state));
    REQUIRE(restored.LoadedAmmo(gun) == 17);
    REQUIRE(restored.IsWorn(pack));
    REQUIRE(restored.CountOf("food_can") == 3);
    REQUIRE(restored.TotalCapacity() == original.TotalCapacity());
    auto bad = state;
    bad.instances.front().containerId = 123456;
    REQUIRE_FALSE(restored.Restore(bad));
    REQUIRE(restored.IsWorn(pack));
    bad = state;
    bad.instances.push_back(bad.instances.front());
    REQUIRE_FALSE(restored.Restore(bad));
}

TEST_CASE("NextDayz safehouse transfers preserve magazines and selling is one-way", "[NextDayz][Campaign]")
{
    FCampaign campaign;
    FExtractionConfig config;
    Inventory carried;
    FItemInstanceId gun = 0;
    REQUIRE(carried.TryAdd("ak", "AK", EItemKind::Weapon, 1, &gun));
    REQUIRE(carried.SetLoadedAmmo(gun, 13));
    REQUIRE(campaign.Store(carried, gun));
    REQUIRE(carried.CountOf("ak") == 0);
    const auto storedId = campaign.storage.front().instanceId;
    REQUIRE(campaign.storage.front().loadedAmmo == 13);
    campaign.inField = true;
    REQUIRE_FALSE(campaign.Take(carried, storedId, 1, false));
    REQUIRE_FALSE(campaign.Sell(storedId, 1, config));
    campaign.inField = false;
    REQUIRE(campaign.Take(carried, storedId, 1, false));
    REQUIRE(carried.FindFirst("ak")->loadedAmmo == 13);
    REQUIRE(campaign.storage.empty());
    campaign.StoreAll(carried);
    const auto id = campaign.storage.front().instanceId;
    config.sellPrices["ak"] = 120;
    REQUIRE_FALSE(campaign.Sell(id, 2, config));
    REQUIRE(campaign.Sell(id, 1, config));
    REQUIRE(campaign.gold == 120);
    REQUIRE_FALSE(campaign.Sell(id, 1, config));
    REQUIRE(campaign.gold == 120);
}

TEST_CASE("NextDayz helicopter requires valid flare call and physical boarding", "[NextDayz][Campaign]")
{
    FCampaign campaign;
    campaign.inField = true;
    Inventory inventory;
    FItemInstanceId flare = 0;
    REQUIRE(inventory.TryAdd("signal_flare", "Flare", EItemKind::Consumable, 1, &flare));
    const glm::vec3 pad(10, 5, 20);
    FExtractionConfig config;
    REQUIRE_FALSE(campaign.Call(inventory, flare, "hospital", pad, pad + glm::vec3(0,20,0), config));
    REQUIRE(inventory.CountOf("signal_flare") == 1);
    REQUIRE(campaign.Call(inventory, flare, "hospital", pad, pad, config));
    REQUIRE(inventory.CountOf("signal_flare") == 0);
    REQUIRE_FALSE(campaign.Board(pad, config));
    REQUIRE_FALSE(campaign.TickFlight(config.arrivalSeconds + config.landingSeconds, config));
    REQUIRE(campaign.flight.phase == EFlightPhase::Waiting);
    REQUIRE_FALSE(campaign.Board(pad + glm::vec3(20,0,0), config));
    REQUIRE(campaign.Board(pad + glm::vec3(2,0.3f,0), config));
    REQUIRE_FALSE(campaign.TickFlight(config.departureSeconds - 1, config));
    REQUIRE(campaign.inField);
    REQUIRE(campaign.TickFlight(1, config));
    REQUIRE_FALSE(campaign.inField);
    REQUIRE(campaign.extractions == 1);
    REQUIRE_FALSE(campaign.TickFlight(1000, config));
    REQUIRE(campaign.extractions == 1);
}

TEST_CASE("NextDayz missed helicopter does not extract its caller", "[NextDayz][Campaign]")
{
    FCampaign campaign;
    campaign.inField = true;
    campaign.flight = {EFlightPhase::Waiting, "base", {0,0,0}, 1, false};
    REQUIRE_FALSE(campaign.TickFlight(1000, FExtractionConfig{}));
    REQUIRE(campaign.inField);
    REQUIRE(campaign.extractions == 0);
    REQUIRE(campaign.flight.phase == EFlightPhase::Idle);
}

TEST_CASE("NextDayz save round-trip retains world depletion and recovers a backup", "[NextDayz][Campaign]")
{
    const auto path = std::filesystem::temp_directory_path() /
        fmt::format("nextdayz-campaign-{}.json", std::chrono::steady_clock::now().time_since_epoch().count());
    struct Cleanup
    {
        std::filesystem::path path;
        ~Cleanup()
        {
            std::error_code ec;
            for (const auto* suffix : {"", ".bak", ".tmp"}) std::filesystem::remove(path.string() + suffix, ec);
        }
    } cleanup{path};
    FCampaignSave saved;
    Inventory inventory;
    REQUIRE(inventory.TryAdd("bandage", "Bandage", EItemKind::Consumable, 2));
    FItemInstanceId pistol = 0;
    REQUIRE(inventory.TryAdd("pistol", "Makarov", EItemKind::Weapon, 1, &pistol));
    inventory.SetLoadedAmmo(pistol, 7);
    saved.weaponSlots[2] = pistol;
    saved.activeSlot = 2;
    saved.previousSlot = 1;
    saved.inventory = inventory.Capture();
    saved.map = "test-map";
    saved.campaign.gold = 42;
    saved.campaign.inField = true;
    saved.campaign.flight = {EFlightPhase::Landing, "roof", {10,20,30}, 4, false};
    saved.position = {1,2,3};
    saved.world.seconds = 153;
    FSavedLootEntry taken;
    taken.id = "spot:medical:1:2:3";
    taken.state = FLootEntry::EState::Cooldown;
    taken.slot.state = ELootSlotState::Cooldown;
    taken.slot.committedAt = 150;
    taken.slot.cooldownUntil = 200;
    saved.world.entries.push_back(taken);
    std::string error;
    REQUIRE(WriteCampaignSave(path, saved, error));
    FCampaignSave read;
    REQUIRE(ReadCampaignSave(path, read, error));
    REQUIRE(read.world.entries.front().slot.cooldownUntil == 200);
    REQUIRE(read.campaign.flight.remaining == 4);
    REQUIRE(read.position.x == 1);
    REQUIRE(read.weaponSlots[2] == pistol);
    REQUIRE(read.activeSlot == 2);
    REQUIRE(read.previousSlot == 1);
    REQUIRE(read.inventory.instances.back().loadedAmmo == 7);
    saved.campaign.gold = 99;
    REQUIRE(WriteCampaignSave(path, saved, error));
    std::ofstream(path, std::ios::trunc) << "broken save";
    REQUIRE(ReadCampaignSave(path, read, error));
    REQUIRE(read.campaign.gold == 42);
    REQUIRE(error.find("Recovered backup") != std::string::npos);
    saved.inventory.instances.front().containerId = 9999;
    REQUIRE_FALSE(WriteCampaignSave(path, saved, error));
}


TEST_CASE("NextDayz grid save roundtrip retains coordinates and rejects overlapping cargo", "[NextDayz][Campaign][InventoryGrid]")
{
    const auto path = std::filesystem::temp_directory_path() /
        fmt::format("nextdayz-grid-{}.json", std::chrono::steady_clock::now().time_since_epoch().count());
    struct Cleanup
    {
        std::filesystem::path path;
        ~Cleanup() { std::error_code ec; for (const auto* suffix : {"", ".bak", ".tmp"}) std::filesystem::remove(path.string() + suffix, ec); }
    } cleanup{path};
    Inventory inventory;
    FItemInstanceId pistol = 0;
    REQUIRE(inventory.TryAddAt("pistol", 1, 2, {0, 0, true}, &pistol));
    REQUIRE(inventory.TryAddAt("bandage", 2, 2, {1, 1, false}));
    REQUIRE(inventory.SetLoadedAmmo(pistol, 6));
    FCampaignSave saved;
    saved.map = "grid-test";
    saved.inventory = inventory.Capture();
    std::string error;
    REQUIRE(WriteCampaignSave(path, saved, error));
    FCampaignSave read;
    REQUIRE(ReadCampaignSave(path, read, error));
    Inventory restored;
    REQUIRE(restored.Restore(read.inventory));
    REQUIRE((restored.FindInstance(pistol)->placement == FGridPlacement{0, 0, true}));
    REQUIRE(restored.LoadedAmmo(pistol) == 6);
    auto invalid = saved;
    invalid.inventory.instances.back().placement = {0, 0, false};
    REQUIRE_FALSE(WriteCampaignSave(path, invalid, error));
}

TEST_CASE("NextDayz legacy grid migration keeps items that no longer fit in safehouse storage", "[NextDayz][Campaign][InventoryGrid]")
{
    const auto path = std::filesystem::temp_directory_path() /
        fmt::format("nextdayz-grid-legacy-{}.json", std::chrono::steady_clock::now().time_since_epoch().count());
    struct Cleanup
    {
        std::filesystem::path path;
        ~Cleanup() { std::error_code ec; for (const auto* suffix : {"", ".bak", ".tmp"}) std::filesystem::remove(path.string() + suffix, ec); }
    } cleanup{path};
    FCampaignSave saved;
    saved.map = "legacy-grid-test";
    saved.inventory = Inventory{}.Capture();
    std::string error;
    REQUIRE(WriteCampaignSave(path, saved, error));
    nlohmann::json document;
    { std::ifstream stream(path); stream >> document; }
    document["version"] = 4;
    auto& inventory = document["state"]["inventory"];
    for (auto& container : inventory["containers"])
    {
        container["capacity"] = container["weaponOnly"].get<bool>() ? 10 :
            container["width"].get<int>() * container["height"].get<int>();
        container.erase("width");
        container.erase("height");
    }
    // Under the old scalar rules two helmets fit in the shirt (6), one in the trousers (4).
    // Their 2x2 footprints can now use only the trousers; the other two must not disappear.
    for (int id = 1; id <= 3; ++id)
        inventory["instances"].push_back({{"instanceId", id}, {"defId", "helmet"}, {"count", 1},
            {"containerId", id <= 2 ? 1 : 2}, {"loadedAmmo", 0}});
    inventory["nextInstanceId"] = 4;
    { std::ofstream stream(path, std::ios::trunc); stream << document; }
    FCampaignSave read;
    REQUIRE(ReadCampaignSave(path, read, error));
    Inventory restored;
    REQUIRE(restored.Restore(read.inventory));
    REQUIRE(restored.CountOf("helmet") == 1);
    REQUIRE(read.campaign.storage.size() == 2);
    for (const auto& item : read.campaign.storage) REQUIRE(item.defId == "helmet");
    REQUIRE(WriteCampaignSave(path, read, error));
    REQUIRE(ReadCampaignSave(path, read, error));
    REQUIRE(read.campaign.storage.size() == 2);
}
