#include "Engine/Common/CoreMinimal.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <thread>

#include <glm/vec3.hpp>

#include "Modules/NextNet/NetBuffer.hpp"
#include "Modules/NextNet/NetHost.hpp"
#include "Application/Game/NextDayz/Net/NetProtocol.hpp"

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

    // A truncated snapshot is refused rather than half-applied.
    NextNet::FNetReader truncated(writer.Bytes().first(4));
    Net::ReadHeader(truncated, message);
    Net::FWelcome welcome;
    CHECK_FALSE(Net::Read(truncated, welcome));
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
