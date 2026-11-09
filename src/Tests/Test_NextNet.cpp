#include "Engine/Common/CoreMinimal.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <thread>

#include <glm/vec3.hpp>

#include "Modules/NextNet/NetBuffer.hpp"
#include "Modules/NextNet/NetHost.hpp"
#include "Application/Game/NextDayz/Net/NetProtocol.hpp"
#include "Application/Game/NextDayz/Sim/ItemLedger.hpp"
#include "Application/Game/NextDayz/Net/MasterApi.hpp"
#include <nlohmann/json.hpp>

TEST_CASE("NextDayz net protocol round-trips a snapshot, a hit and a welcome", "[Unit][NextNet][NextDayz][Multiplayer]")
{
    using namespace NextDayz;
    Net::FSnapshot snapshot;
    snapshot.time.minutes = 615.5;
    snapshot.time.weather = EWeather::Clear;
    Net::FPlayerState survivor;
    survivor.presence = {7, {1.0f, 2.0f, 3.0f}, {1.0f, 3.6f, 3.0f}, {0.0f, 0.0f, 1.0f}, true, 0.7f};
    survivor.presentation.weaponId = "ak";
    survivor.presentation.slotWeaponIds = {"ak", "", "makarov"};
    survivor.presentation.locomotion.gait = EPlayerGait::Sprint;
    survivor.presentation.aimPitchRadians = 0.25f;
    survivor.clothing = {"jacket", "helmet"};
    snapshot.survivors.push_back(survivor);
    FZombieRuntime zombie;
    zombie.handle = {3, 9};
    zombie.state = EZombieState::Attack;
    zombie.position = {10.0f, 0.0f, -4.0f};
    zombie.targetId = 7;
    zombie.crippled = true;
    snapshot.zombies.push_back(zombie);

    NextNet::FNetWriter writer;
    Net::Write(writer, snapshot);
    NextNet::FNetReader reader(writer.Bytes());
    Net::EMessage message{};
    REQUIRE(Net::ReadHeader(reader, message));
    REQUIRE(message == Net::EMessage::Snapshot);
    Net::FSnapshot copy;
    REQUIRE(Net::Read(reader, copy));
    CHECK(reader.AtEnd());
    CHECK(copy.time.minutes == 615.5);
    REQUIRE(copy.survivors.size() == 1);
    CHECK(copy.survivors[0].presence.id == 7);
    CHECK(copy.survivors[0].presence.sightScale == 0.7f);
    CHECK(copy.survivors[0].presentation.weaponId == "ak");
    CHECK(copy.survivors[0].presentation.slotWeaponIds[2] == "makarov");
    CHECK(copy.survivors[0].presentation.locomotion.gait == EPlayerGait::Sprint);
    CHECK(copy.survivors[0].clothing == std::vector<std::string>{"jacket", "helmet"});
    REQUIRE(copy.zombies.size() == 1);
    CHECK(copy.zombies[0].handle == zombie.handle);
    CHECK(copy.zombies[0].state == EZombieState::Attack);
    CHECK(copy.zombies[0].position == zombie.position);
    CHECK(copy.zombies[0].targetId == 7);
    CHECK(copy.zombies[0].crippled);

    FWeaponHitEvent hit;
    hit.weaponId = "ak";
    hit.zombie = {3, 9};
    hit.zone = EHitZone::Head;
    hit.hitPoint = {1.0f, 1.7f, 0.0f};
    writer.Clear();
    Net::Write(writer, hit);
    NextNet::FNetReader hitReader(writer.Bytes());
    REQUIRE(Net::ReadHeader(hitReader, message));
    REQUIRE(message == Net::EMessage::WeaponHit);
    FWeaponHitEvent hitCopy;
    REQUIRE(Net::Read(hitReader, hitCopy));
    CHECK(hitCopy.weaponId == "ak");
    CHECK(hitCopy.zombie == hit.zombie);
    CHECK(hitCopy.zone == EHitZone::Head);

    // The body the server keeps, as its owner receives it.
    Net::FSurvivalState body;
    body.life = 4;
    body.body.health = 37.5f;
    body.body.bleedingWounds = 2;
    body.body.lifeState = EPlayerLifeState::Dead;
    body.body.lastDamageSource = "infected";
    writer.Clear();
    Net::Write(writer, body);
    NextNet::FNetReader bodyReader(writer.Bytes());
    REQUIRE(Net::ReadHeader(bodyReader, message));
    REQUIRE(message == Net::EMessage::SurvivalState);
    Net::FSurvivalState bodyCopy;
    REQUIRE(Net::Read(bodyReader, bodyCopy));
    CHECK(bodyCopy.life == 4);
    CHECK(bodyCopy.body.health == 37.5f);
    CHECK(bodyCopy.body.bleedingWounds == 2);
    CHECK(bodyCopy.body.lifeState == EPlayerLifeState::Dead);
    CHECK(bodyCopy.body.lastDamageSource == "infected");

    // A truncated snapshot is refused rather than half-applied.
    NextNet::FNetReader truncated(writer.Bytes().first(4));
    Net::ReadHeader(truncated, message);
    Net::FWelcome welcome;
    CHECK_FALSE(Net::Read(truncated, welcome));
}

TEST_CASE("NextDayz net protocol round-trips loot state, takes, grants and drops", "[Unit][NextNet][NextDayz][Multiplayer]")
{
    using namespace NextDayz;
    Net::FLootState state;
    state.full = true;
    FLootReplica rifle;
    rifle.id = "spot:military:12";
    rifle.source = ELootSource::Spot;
    rifle.position = {3.0f, 1.0f, -2.0f};
    rifle.available = true;
    rifle.contents.push_back({"ak", 1, 30, {}});
    rifle.contents.push_back({"ammo_545", 45, 0, {}});
    FLootReplica pile;
    pile.id = "pile:7";
    pile.source = ELootSource::Pile;
    state.entries = {rifle, pile};

    NextNet::FNetWriter writer;
    Net::Write(writer, state);
    NextNet::FNetReader reader(writer.Bytes());
    Net::EMessage message{};
    REQUIRE(Net::ReadHeader(reader, message));
    REQUIRE(message == Net::EMessage::LootState);
    Net::FLootState copy;
    REQUIRE(Net::Read(reader, copy));
    CHECK(reader.AtEnd());
    CHECK(copy.full);
    REQUIRE(copy.entries.size() == 2);
    CHECK(copy.entries[0].id == "spot:military:12");
    CHECK(copy.entries[0].available);
    REQUIRE(copy.entries[0].contents.size() == 2);
    CHECK(copy.entries[0].contents[0].loadedAmmo == 30);
    CHECK(copy.entries[0].contents[1].count == 45);
    CHECK(copy.entries[1].source == ELootSource::Pile);
    CHECK_FALSE(copy.entries[1].available);

    writer.Clear();
    Net::Write(writer, Net::FLootGrant{9, true, rifle.contents});
    NextNet::FNetReader grantReader(writer.Bytes());
    REQUIRE(Net::ReadHeader(grantReader, message));
    REQUIRE(message == Net::EMessage::LootGrant);
    Net::FLootGrant grant;
    REQUIRE(Net::Read(grantReader, grant));
    CHECK(grant.request == 9);
    CHECK(grant.granted);
    CHECK(grant.contents.size() == 2);

    writer.Clear();
    Net::Write(writer, Net::FLootDrop{{1.0f, 0.0f, 1.0f}, 0.5f, {{"bandage", 2, 0, {}}}});
    NextNet::FNetReader dropReader(writer.Bytes());
    REQUIRE(Net::ReadHeader(dropReader, message));
    Net::FLootDrop drop;
    REQUIRE(Net::Read(dropReader, drop));
    CHECK(drop.contents.front().itemId == "bandage");
    CHECK(drop.yaw == 0.5f);

    // A packet claiming more stacks than any entry holds is refused.
    NextNet::FNetWriter hostile;
    hostile.Write(Net::EMessage::LootDrop).Write(glm::vec3(0.0f)).Write(0.0f).Write<uint16_t>(60000);
    NextNet::FNetReader hostileReader(hostile.Bytes());
    Net::ReadHeader(hostileReader, message);
    Net::FLootDrop refused;
    CHECK_FALSE(Net::Read(hostileReader, refused));
}

TEST_CASE("NextDayz net protocol round-trips an inventory report and a correction", "[Unit][NextNet][NextDayz][Multiplayer]")
{
    using namespace NextDayz;
    Inventory inventory;
    FItemInstanceId rifle = 0;
    REQUIRE(inventory.TryAdd("ak", "AK-74", EItemKind::Weapon, 1, &rifle));
    REQUIRE(inventory.SetLoadedAmmo(rifle, 17));
    REQUIRE(inventory.TryAdd("bandage", "Bandage", EItemKind::Consumable, 2));
    const FInventoryState state = inventory.Capture();

    NextNet::FNetWriter writer;
    Net::Write(writer, Net::FInventorySync{3, true, state});
    NextNet::FNetReader reader(writer.Bytes());
    Net::EMessage message{};
    REQUIRE(Net::ReadHeader(reader, message));
    REQUIRE(message == Net::EMessage::InventorySync);
    Net::FInventorySync sync;
    REQUIRE(Net::Read(reader, sync));
    CHECK(reader.AtEnd());
    CHECK(sync.life == 3);
    CHECK(sync.alive);
    CHECK(CountItems(sync.inventory) == CountItems(state));
    Inventory copy;
    REQUIRE(copy.Restore(sync.inventory));
    CHECK(copy.CountOf("bandage") == 2);
    REQUIRE(copy.FindInstance(rifle) != nullptr);
    CHECK(copy.FindInstance(rifle)->loadedAmmo == 17);

    writer.Clear();
    Net::Write(writer, Net::FInventoryFix{3, "bandage x5 appeared from nothing", state});
    NextNet::FNetReader fixReader(writer.Bytes());
    REQUIRE(Net::ReadHeader(fixReader, message));
    REQUIRE(message == Net::EMessage::InventoryFix);
    Net::FInventoryFix fix;
    REQUIRE(Net::Read(fixReader, fix));
    CHECK(fix.reason == "bandage x5 appeared from nothing");
    CHECK(fix.inventory.instances.size() == state.instances.size());
}

TEST_CASE("NextDayz zombie replicas take the server's slots and drop the rest", "[Unit][NextDayz][Multiplayer]")
{
    using namespace NextDayz;
    ZombieSystem client(4);
    const FZombieHandle local = client.Spawn(EZombieProfile::Civilian, {});
    REQUIRE(local.IsValid());
    FZombieRuntime replica;
    replica.handle = {2, 17};
    replica.profile = EZombieProfile::Military;
    replica.state = EZombieState::Chase;
    replica.position = {5.0f, 0.0f, 5.0f};
    client.ApplyReplicas(std::span<const FZombieRuntime>(&replica, 1));
    CHECK(client.Resolve(local) == nullptr);           // not on the server: gone
    const FZombieRuntime* mirrored = client.Resolve(replica.handle);
    REQUIRE(mirrored != nullptr);
    CHECK(mirrored->state == EZombieState::Chase);
    CHECK(mirrored->position == replica.position);
    CHECK(client.ActiveCount() == 1);

    // The same infected again keeps its eased position; a new one jumps in place.
    client.Resolve(replica.handle)->position = {4.0f, 0.0f, 4.0f};
    replica.position = {6.0f, 0.0f, 6.0f};
    replica.state = EZombieState::Attack;
    client.ApplyReplicas(std::span<const FZombieRuntime>(&replica, 1));
    CHECK(client.Resolve(replica.handle)->position == glm::vec3(4.0f, 0.0f, 4.0f));
    CHECK(client.Resolve(replica.handle)->state == EZombieState::Attack);
    client.ApplyReplicas({});
    CHECK(client.ActiveCount() == 0);
}

TEST_CASE("NextNet buffers round-trip values and refuse to read past the end", "[Unit][NextNet]")
{
    NextNet::FNetWriter writer;
    writer.Write<uint8_t>(7).Write<float>(1.5f).WriteString("survivor").Write<glm::vec3>(glm::vec3(1.0f, 2.0f, 3.0f));
    NextNet::FNetReader reader(writer.Bytes());
    CHECK(reader.Read<uint8_t>() == 7);
    CHECK(reader.Read<float>() == 1.5f);
    CHECK(reader.ReadString() == "survivor");
    CHECK(reader.Read<glm::vec3>() == glm::vec3(1.0f, 2.0f, 3.0f));
    CHECK(reader.AtEnd());
    CHECK_FALSE(reader.Failed());

    // A truncated packet fails instead of reading garbage.
    NextNet::FNetReader truncated(writer.Bytes().first(3));
    truncated.Read<uint8_t>();
    CHECK(truncated.Read<float>() == 0.0f);
    CHECK(truncated.Failed());
    CHECK(truncated.ReadString().empty());
}

TEST_CASE("NextNet hosts connect over loopback and deliver reliable and unreliable messages",
          "[Unit][NextNet]")
{
    using namespace NextNet;
    constexpr uint16_t port = 47911;
    FNetHost server;
    FNetHost client;
    std::string error;
    REQUIRE(server.Listen(port, 4, error));
    REQUIRE(client.Connect("127.0.0.1", port, error));

    std::vector<FNetEvent> serverEvents;
    std::vector<FNetEvent> clientEvents;
    FPeerId clientOnServer = kInvalidPeer;
    const auto pump = [&](const std::function<bool()>& done)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!done() && std::chrono::steady_clock::now() < deadline)
        {
            server.Poll(serverEvents);
            client.Poll(clientEvents);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return done();
    };
    REQUIRE(pump([&]
    {
        for (const FNetEvent& event : serverEvents)
            if (event.type == FNetEvent::EType::Connected) clientOnServer = event.peer;
        return clientOnServer != kInvalidPeer && client.ServerPeer() != kInvalidPeer;
    }));
    CHECK(server.PeerCount() == 1);

    // Ordered reliable delivery client -> server.
    for (uint32_t index = 0; index < 50; ++index)
    {
        FNetWriter writer;
        writer.Write(index);
        REQUIRE(client.Send(client.ServerPeer(), writer.Bytes(), EDelivery::Reliable));
    }
    serverEvents.clear();
    std::vector<uint32_t> received;
    REQUIRE(pump([&]
    {
        for (const FNetEvent& event : serverEvents)
            if (event.type == FNetEvent::EType::Message)
                received.push_back(FNetReader(event.payload).Read<uint32_t>());
        serverEvents.clear();
        return received.size() == 50;
    }));
    for (uint32_t index = 0; index < 50; ++index) CHECK(received[index] == index);

    // Server -> client broadcast on the unreliable channel arrives on loopback.
    FNetWriter state;
    state.WriteString("snapshot");
    server.Broadcast(state.Bytes(), EDelivery::Unreliable);
    std::string text;
    REQUIRE(pump([&]
    {
        for (const FNetEvent& event : clientEvents)
            if (event.type == FNetEvent::EType::Message) text = FNetReader(event.payload).ReadString();
        return !text.empty();
    }));
    CHECK(text == "snapshot");

    // The server sees the client leave.
    client.Shutdown();
    bool left = false;
    REQUIRE(pump([&]
    {
        for (const FNetEvent& event : serverEvents)
            if (event.type == FNetEvent::EType::Disconnected && event.peer == clientOnServer) left = true;
        return left;
    }));
    CHECK(server.PeerCount() == 0);
}

TEST_CASE("NextDayz Master JSON matches what tools/gnb/internal/dzmaster sends and expects",
          "[Unit][NextNet][NextDayz][Multiplayer]")
{
    using namespace NextDayz;
    // A profile as the Go Master encodes it (Stack: id/count/loaded/attachments).
    const std::string profileJson = R"({"playerId":7,"name":"pilot","gold":12,
        "storage":[{"id":"ak","count":1,"loaded":30,"attachments":[1,0,0,0,0]},{"id":"bandage","count":4}],
        "character":{"map":"loket","position":[1.5,2,3],"yaw":0.25,"survival":{"health":80},
                     "inventory":[{"id":"food_can","count":2}],"sortie":9,"fresh":false},
        "sorties":3,"extractions":1,"deaths":1,"lastOutcome":"died on loket","maps":[{"id":"loket","name":"LOKET","scene":"a.scad","training":false}],
        "playing":""})";
    Master::FProfile profile;
    REQUIRE(Master::ParseProfile(profileJson, profile));
    CHECK(profile.playerId == 7);
    REQUIRE(profile.storage.size() == 2);
    CHECK(profile.storage[0].loadedAmmo == 30);
    CHECK(profile.storage[0].attachments[0] == static_cast<EWeaponAttachment>(1));
    CHECK(profile.storage[1].count == 4);
    REQUIRE(profile.character);
    CHECK(profile.character->position == glm::vec3(1.5f, 2.0f, 3.0f));
    CHECK(profile.character->sortie == 9);
    CHECK_FALSE(profile.character->fresh);
    CHECK(profile.character->survivalJson.find("80") != std::string::npos);
    REQUIRE(profile.maps.size() == 1);
    CHECK(profile.maps[0].scene == "a.scad");

    Master::FDeploy deploy;
    REQUIRE(Master::ParseDeploy(R"({"state":"ready","server":"ds1","host":"127.0.0.1","port":27100,"ticket":"ab","map":"loket","scene":"a.scad","resume":true})", deploy));
    CHECK(deploy.port == 27100);
    CHECK(deploy.resume);
    Master::FHeartbeatReply beat;
    REQUIRE(Master::ParseHeartbeat(R"({"stop":true,"pvp":false,"kick":[3,4]})", beat));
    CHECK(beat.stop);
    CHECK(beat.kick == std::vector<uint64_t>{3, 4});
    std::string world;
    REQUIRE(Master::ParseWorld(R"({"seq":2,"blob":{"version":1}})", world));
    CHECK(world.find("version") != std::string::npos);
    REQUIRE(Master::ParseWorld(R"({"seq":0,"blob":null})", world));
    CHECK(world.empty());
    CHECK_FALSE(Master::ParseDeploy("not json", deploy));

    // A checkpoint as the Go Master decodes it.
    Master::FCheckpointCharacter extracted;
    extracted.playerId = 7;
    extracted.sortie = 9;
    extracted.outcome = Master::EOutcome::Extracted;
    extracted.position = {1.0f, 2.0f, 3.0f};
    extracted.survivalJson = R"({"health":50})";
    extracted.inventory = {{"ak", 1, 12, {}}, {"bandage", 2, 0, {}}};
    const auto body = nlohmann::json::parse(Master::CheckpointRequest("ds1", "key", 4, R"({"loot":1})", {extracted}));
    CHECK(body["seq"] == 4);
    CHECK(body["world"]["loot"] == 1);
    const auto& character = body["characters"][0];
    CHECK(character["outcome"] == "extracted");
    CHECK(character["inventory"][0]["loaded"] == 12);
    CHECK_FALSE(character["inventory"][1].contains("attachments"));
    CHECK(character["survival"]["health"] == 50);
}
