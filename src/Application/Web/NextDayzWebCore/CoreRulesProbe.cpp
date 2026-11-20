#include "Engine/Common/CoreMinimal.hpp"

#include "Application/Game/NextDayz/Inventory/LootTransfer.hpp"
#include "Application/Game/NextDayz/Net/ClientSession.hpp"
#include "Application/Game/NextDayz/Sim/ItemLedger.hpp"

#include <cstdio>

int main()
{
    using namespace NextDayz;

    Inventory inventory;
    const std::vector<FLootStack> loot{{"water_bottle", 1}, {"bandage", 1}};
    std::vector<FLootStack> left;
    if (AddLootToInventory(loot, inventory, 0, EEquipSlot::None, {}, left).size() != 2 || !left.empty())
        return 1;

    const auto allowed = CountItems(inventory.Capture());
    auto extra = allowed;
    ++extra["bandage"];
    if (CheckConservation(allowed, extra).accepted) return 2;

    SurvivalSystem body;
    body.Reset();
    body.Update(120.0f, false);
    const auto previous = body.Snapshot();
    const auto water = std::ranges::find(inventory.Instances(), "water_bottle", &FItemInstance::defId);
    if (water == inventory.Instances().end() || !body.TryUseItem(inventory, water->instanceId)) return 3;
    if (body.Snapshot().hydration <= previous.hydration || inventory.CountOf("water_bottle_empty") != 1 ||
        !CheckConservation(allowed, CountItems(inventory.Capture())).accepted)
        return 4;

    TimeSystem time;
    time.Reset(FTimeConfig{}, 1);
    auto authority = time.Capture();
    authority.minutes = 23.0 * 60.0;
    authority.weather = EWeather::Fog;
    authority.blend = .8f;
    Net::FClientSession::ReconcileClock(time, authority);
    const auto night = time.SampleEnvironment();
    if (time.Weather() != EWeather::Fog || night.hasSun || night.daylight > 0.0f) return 5;

    Net::FClientSession session;
    session.survivorId = 2;
    std::array<Net::FPlayerState, 2> players{};
    players[0].presence.id = 2;
    players[1].presence.id = 3;
    if (!session.SynchronizeRemotes(players).empty() || session.remotes.size() != 1) return 6;
    players[1].presence.feet.x = 1.0f;
    session.SynchronizeRemotes(players);
    session.StepRemotes(1.0f / 60.0f);
    const float eased = session.remotes.at(3).feet.x;
    if (eased <= 0.f || eased >= 1.f || session.SynchronizeRemotes({}) != std::vector<uint32_t>{3}) return 7;

    std::puts("NextDayzCore rules OK");
}
