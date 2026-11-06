#include "Engine/Common/CoreMinimal.hpp"
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "Application/Game/NextDayz/World/Campaign.hpp"

using namespace NextDayz;

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
