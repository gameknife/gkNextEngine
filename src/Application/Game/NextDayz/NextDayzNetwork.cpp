// ============================================================================
// NextDayzNetwork.cpp - NextDayzGameInstance's multiplayer half: a map server
// (the authoritative WorldSim with every client's survivor in it, no local
// player) and a client (plays its own survivor; the infected, the other
// survivors and the world clock come from the server). Direct connection, no
// Master yet. Movement is the client's; infected, damage and friendly fire are
// the server's. See docs/projects/nextdayz/nextdayz-multiplayer-design.md.
// ============================================================================

#include "Engine/Common/CoreMinimal.hpp"
#include "Application/Game/NextDayz/NextDayzGameInstance.hpp"

#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Interface/AgentQueries.hpp"

#include <imgui.h>

#if GK_MODULE_NEXTNET
#include "Application/Game/NextDayz/Net/NetProtocol.hpp"
#include "Modules/NextNet/NetHost.hpp"
#endif

NextDayzGameInstance::~NextDayzGameInstance() = default;

#if !GK_MODULE_NEXTNET
struct NextDayzGameInstance::FNetSession
{
};
#endif


#if GK_MODULE_NEXTNET

namespace
{
    using namespace NextDayz;
    using NextNet::EDelivery;
    using NextNet::FNetReader;
    using NextNet::FNetWriter;
    using NextNet::FPeerId;

    constexpr float kSnapshotSeconds = 0.05f;      // 20 Hz world state
    constexpr float kStateSeconds = 1.0f / 30.0f;  // 30 Hz own survivor
    constexpr float kReconnectSeconds = 3.0f;
    constexpr float kRemoteEaseRate = 14.0f;       // 1/s toward the newest snapshot
    // Friendly fire is softer than the infected's flesh: survivors carry 100 HP.
    constexpr float kPvpDamageScale = 0.6f;
    constexpr float kSurvivorRadius = 0.32f;

    float Ease(float rate, float dt) { return 1.0f - std::exp(-rate * std::max(dt, 0.0f)); }

    // Closest distance between segments p0-p1 and q0-q1; s is the parameter on p.
    float SegmentDistance(const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& q0, const glm::vec3& q1, float& s)
    {
        const glm::vec3 d1 = p1 - p0;
        const glm::vec3 d2 = q1 - q0;
        const glm::vec3 r = p0 - q0;
        const float a = glm::dot(d1, d1);
        const float e = glm::dot(d2, d2);
        const float f = glm::dot(d2, r);
        float t = 0.0f;
        s = 0.0f;
        if (a <= 1.0e-8f && e <= 1.0e-8f) return glm::length(r);
        if (a <= 1.0e-8f)
        {
            t = std::clamp(f / e, 0.0f, 1.0f);
        }
        else
        {
            const float c = glm::dot(d1, r);
            if (e <= 1.0e-8f)
            {
                s = std::clamp(-c / a, 0.0f, 1.0f);
            }
            else
            {
                const float b = glm::dot(d1, d2);
                const float denominator = a * e - b * b;
                s = denominator != 0.0f ? std::clamp((b * f - c * e) / denominator, 0.0f, 1.0f) : 0.0f;
                t = (b * s + f) / e;
                if (t < 0.0f) { t = 0.0f; s = std::clamp(-c / a, 0.0f, 1.0f); }
                else if (t > 1.0f) { t = 1.0f; s = std::clamp((b - c) / a, 0.0f, 1.0f); }
            }
        }
        return glm::length((p0 + d1 * s) - (q0 + d2 * t));
    }
}

struct NextDayzGameInstance::FNetSession
{
    NextNet::FNetHost host;
    bool pvp = false;
    std::string status;

    // --- server ---
    struct FClient
    {
        FPeerId peer = NextNet::kInvalidPeer;
        uint32_t survivorId = 0;
        std::string name;
        bool hasState = false;
        Net::FPlayerState state;
    };
    std::unordered_map<FPeerId, FClient> clients;
    uint32_t nextSurvivorId = Net::kFirstSurvivorId;
    float snapshotTimer = 0.0f;

    FClient* ClientBySurvivor(uint32_t survivorId)
    {
        for (auto& [peer, client] : clients)
            if (client.survivorId == survivorId) return &client;
        return nullptr;
    }

    // --- client ---
    bool connected = false;
    uint32_t survivorId = 0;
    std::string rejected;
    float sendTimer = 0.0f;
    float reconnectTimer = 0.0f;
    uint64_t lastNoiseSequence = 0;
    uint32_t snapshots = 0;
    std::optional<std::vector<FZombieRuntime>> pendingZombies;
    struct FEase
    {
        FZombieHandle handle;
        glm::vec3 position{};
        glm::vec3 forward{0.0f, 0.0f, 1.0f};
    };
    std::vector<FEase> zombieTargets;
    struct FRemote
    {
        int rig = -1;
        glm::vec3 feet{};
        glm::vec3 targetFeet{};
        FPlayerPresentationState presentation;
        bool alive = true;
        bool seen = false;
    };
    std::unordered_map<uint32_t, FRemote> remotes;
};

void NextDayzGameInstance::ConfigureNetCVars(NextCVar::FCVarSystem& cvars)
{
    using NextCVar::ECVarFlags;
    cvars.RegisterString("nextdayz.net.mode", "off", &netModeCvar_, ECVarFlags::None,
        "Multiplayer role at startup: off | server | client (pass with --cvar)");
    cvars.RegisterString("nextdayz.net.host", "127.0.0.1", &netHostCvar_, ECVarFlags::None,
        "Client: server address");
    cvars.RegisterInt("nextdayz.net.port", NextDayz::Net::kDefaultPort, &netPortCvar_, ECVarFlags::None,
        "Server: UDP port to listen on / client: port to connect to", nullptr, 1, 65535);
    cvars.RegisterString("nextdayz.net.name", "", &netNameCvar_, ECVarFlags::None, "Client: survivor name");
    cvars.RegisterInt("nextdayz.net.maxPlayers", 8, &netMaxPlayersCvar_, ECVarFlags::None,
        "Server: survivors allowed at once / client: remote bodies to prepare", nullptr, 1, 32);
    cvars.RegisterBool("nextdayz.net.kit", true, &netKitCvar_, ECVarFlags::None,
        "Client: spawn with an AK and a backpack (testing)");
    cvars.RegisterInt("nextdayz.validation.netPvpHit", 0, &validationNetPvpHit_, ECVarFlags::None,
        "Agent-only (client): send a 30-damage friendly-fire hit at the nearest survivor", [this]()
        {
            if (!GOption->AgentValidation || !net_ || validationNetPvpHit_ == 0 || net_->survivorId == 0) return;
            uint32_t victim = 0;
            float nearest = std::numeric_limits<float>::max();
            for (const auto& [id, remote] : net_->remotes)
            {
                const float distance = glm::distance(remote.feet, player_.Position());
                if (distance < nearest) { nearest = distance; victim = id; }
            }
            if (victim == 0) return;
            Net::FPvpHit hit{victim, 30.0f, player_.EyePosition(), net_->remotes[victim].feet + glm::vec3(0.0f, 1.2f, 0.0f)};
            FNetWriter writer;
            Net::Write(writer, hit);
            net_->host.Send(net_->host.ServerPeer(), writer.Bytes(), EDelivery::Reliable);
        });
    cvars.RegisterBool("nextdayz.net.pvp", false, &netPvpCvar_, ECVarFlags::None,
        "Server: survivors can hurt each other (can change while running)", [this]()
        {
            if (!net_ || netMode_ != ENetMode::Server) return;
            net_->pvp = netPvpCvar_;
            FNetWriter writer;
            Net::WritePvp(writer, net_->pvp);
            net_->host.Broadcast(writer.Bytes(), EDelivery::Reliable);
            SPDLOG_INFO("[NextDayz][net] friendly fire {}", net_->pvp ? "ON" : "OFF");
        });
}

uint32_t NextDayzGameInstance::LocalSurvivorId() const
{
    if (netMode_ == ENetMode::Client && net_ && net_->survivorId != 0) return net_->survivorId;
    return NextDayz::kLocalPlayerTargetId;
}

void NextDayzGameInstance::StartNetwork()
{
    if (netMode_ == ENetMode::Offline)
    {
        return;
    }
    net_.reset(new FNetSession());
    std::string error;
    const auto port = static_cast<uint16_t>(netPortCvar_);
    if (netMode_ == ENetMode::Server)
    {
        net_->pvp = netPvpCvar_;
        if (!net_->host.Listen(port, static_cast<size_t>(netMaxPlayersCvar_), error))
        {
            SPDLOG_ERROR("[NextDayz][net] server: {}", error);
            net_->status = error;
            return;
        }
        SPDLOG_INFO("[NextDayz][net] server listening on UDP {} for up to {} survivors, friendly fire {}", port,
                    netMaxPlayersCvar_, net_->pvp ? "ON" : "OFF");
        net_->status = fmt::format("server :{}", port);
    }
    else
    {
        if (!net_->host.Connect(netHostCvar_, port, error))
        {
            SPDLOG_ERROR("[NextDayz][net] client: {}", error);
            net_->status = error;
            return;
        }
        SPDLOG_INFO("[NextDayz][net] connecting to {}:{}", netHostCvar_, port);
        net_->status = fmt::format("connecting to {}:{}", netHostCvar_, port);
    }
}

void NextDayzGameInstance::ShutdownNetwork()
{
    if (net_)
    {
        net_->host.Shutdown();
    }
}

void NextDayzGameInstance::NetPoll()
{
    if (!net_)
    {
        return;
    }
    std::vector<NextNet::FNetEvent> events;
    net_->host.Poll(events);
    FNetSession& net = *net_;

    if (netMode_ == ENetMode::Server)
    {
        for (NextNet::FNetEvent& event : events)
        {
            if (event.type == NextNet::FNetEvent::EType::Connected)
            {
                net.clients[event.peer].peer = event.peer;
                continue;
            }
            if (event.type == NextNet::FNetEvent::EType::Disconnected)
            {
                const auto it = net.clients.find(event.peer);
                if (it == net.clients.end()) continue;
                if (it->second.survivorId != 0)
                {
                    world_.RemoveSurvivor(it->second.survivorId);
                    FNetWriter writer;
                    Net::WritePlayerLeft(writer, it->second.survivorId);
                    net.host.Broadcast(writer.Bytes(), EDelivery::Reliable);
                    SPDLOG_INFO("[NextDayz][net] survivor {} '{}' left ({} online)", it->second.survivorId,
                                it->second.name, net.clients.size() - 1);
                }
                net.clients.erase(it);
                continue;
            }
            auto it = net.clients.find(event.peer);
            if (it == net.clients.end()) continue;
            FNetSession::FClient& client = it->second;
            FNetReader reader(event.payload);
            Net::EMessage message{};
            if (!Net::ReadHeader(reader, message)) continue;
            if (message == Net::EMessage::Hello)
            {
                Net::FHello hello;
                if (!Net::Read(reader, hello) || client.survivorId != 0) continue;
                FNetWriter writer;
                if (hello.protocol != Net::kProtocolVersion)
                {
                    Net::WriteReject(writer, fmt::format("protocol {} expected, client speaks {}", Net::kProtocolVersion,
                                                         hello.protocol));
                    net.host.Send(event.peer, writer.Bytes(), EDelivery::Reliable);
                    net.host.Disconnect(event.peer);
                    continue;
                }
                client.survivorId = net.nextSurvivorId++;
                client.name = hello.name.empty() ? fmt::format("survivor{}", client.survivorId) : hello.name;
                Net::FWelcome welcome;
                welcome.survivorId = client.survivorId;
                welcome.scene = campaignMap_;
                welcome.pvp = net.pvp;
                Net::Write(writer, welcome);
                net.host.Send(event.peer, writer.Bytes(), EDelivery::Reliable);
                SPDLOG_INFO("[NextDayz][net] survivor {} '{}' joined ({} online)", client.survivorId, client.name,
                            net.clients.size());
                continue;
            }
            if (client.survivorId == 0) continue;   // nothing counts before Hello
            switch (message)
            {
            case Net::EMessage::PlayerState:
            {
                Net::FPlayerState state;
                if (!Net::Read(reader, state)) break;
                state.presence.id = client.survivorId;
                state.presence.sightScale = std::clamp(state.presence.sightScale, 0.1f, 2.0f);
                client.state = state;
                client.hasState = true;
                world_.UpsertSurvivor(state.presence);
                break;
            }
            case Net::EMessage::WeaponHit:
            {
                FWeaponHitEvent hit;
                if (!Net::Read(reader, hit) || !client.hasState) break;
                const FWeaponDef* definition = FindWeaponDef(hit.weaponId);
                // A round leaves from where its shooter stands, not across the map.
                if (!definition || glm::distance(hit.origin, client.state.presence.eye) > 4.0f) break;
                hit.shooterId = client.survivorId;
                hit.baseDamage = definition->baseDamage;
                world_.combat.ProcessHit(hit, definition->damageFalloffDistance);
                for (const FDamageEvent& damage : world_.combat.ConsumeDamageEvents())
                {
                    FNetWriter writer;
                    Net::Write(writer, Net::FDamageNotice{client.survivorId, damage});
                    net.host.Broadcast(writer.Bytes(), EDelivery::Reliable);
                }
                break;
            }
            case Net::EMessage::PvpHit:
            {
                Net::FPvpHit hit;
                if (!Net::Read(reader, hit) || !net.pvp || hit.survivorId == client.survivorId) break;
                FNetSession::FClient* victim = net.ClientBySurvivor(hit.survivorId);
                if (!victim || !victim->hasState || !victim->state.presence.alive) break;
                if (glm::distance(hit.source, client.state.presence.eye) > 4.0f) break;
                hit.amount = std::clamp(hit.amount, 0.0f, 150.0f);
                hit.survivorId = client.survivorId;   // the victim learns who shot
                FNetWriter writer;
                Net::Write(writer, hit);
                net.host.Send(victim->peer, writer.Bytes(), EDelivery::Reliable);
                break;
            }
            case Net::EMessage::Noise:
            {
                Net::FNoise noise;
                if (!Net::Read(reader, noise)) break;
                world_.noise.Emit(noise.position, std::clamp(noise.radius, 0.0f, 400.0f),
                                  std::clamp(noise.strength, 0.0f, 1.0f), noise.type);
                break;
            }
            default:
                break;
            }
        }
        return;
    }

    // --- client ---
    for (NextNet::FNetEvent& event : events)
    {
        if (event.type == NextNet::FNetEvent::EType::Connected)
        {
            net.connected = true;
            net.status = "connected, joining";
            Net::FHello hello;
            hello.name = netNameCvar_;
            hello.scene = campaignMap_;
            FNetWriter writer;
            Net::Write(writer, hello);
            net.host.Send(event.peer, writer.Bytes(), EDelivery::Reliable);
            continue;
        }
        if (event.type == NextNet::FNetEvent::EType::Disconnected)
        {
            net.connected = false;
            net.survivorId = 0;
            net.status = net.rejected.empty() ? "disconnected" : "rejected: " + net.rejected;
            for (auto& [id, remote] : net.remotes)
                if (remote.rig >= 0) remoteRigs_[remote.rig]->SetVisible(false);
            net.remotes.clear();
            world_.zombies.ApplyReplicas({});
            SPDLOG_WARN("[NextDayz][net] {}", net.status);
            continue;
        }
        FNetReader reader(event.payload);
        Net::EMessage message{};
        if (!Net::ReadHeader(reader, message)) continue;
        switch (message)
        {
        case Net::EMessage::Welcome:
        {
            Net::FWelcome welcome;
            if (!Net::Read(reader, welcome)) break;
            net.survivorId = welcome.survivorId;
            net.pvp = welcome.pvp;
            net.status = "online";
            SPDLOG_INFO("[NextDayz][net] joined as survivor {} on '{}', friendly fire {}", welcome.survivorId,
                        welcome.scene, welcome.pvp ? "ON" : "OFF");
            if (welcome.scene != campaignMap_)
            {
                // Same map as the server, whatever this client started with.
                loadingMapPath_ = welcome.scene;
                GetEngine().RequestLoadScene({.filename = welcome.scene});
            }
            else if (sceneReady_)
            {
                TeleportPlayer(ResolveSpawnPosition() - glm::vec3(0.0f, 1.0f, 0.0f));
            }
            break;
        }
        case Net::EMessage::Reject:
            net.rejected = reader.ReadString();
            SPDLOG_ERROR("[NextDayz][net] server refused: {}", net.rejected);
            break;
        case Net::EMessage::Pvp:
            net.pvp = reader.Read<uint8_t>() != 0;
            Notify(net.pvp ? "Friendly fire is ON" : "Friendly fire is OFF", 1);
            break;
        case Net::EMessage::Snapshot:
        {
            Net::FSnapshot snapshot;
            if (!Net::Read(reader, snapshot)) break;
            ++net.snapshots;
            // The server keeps the clock and weather; follow it when we drift.
            const TimeSystem::FState local = time_.Capture();
            if (std::abs(local.minutes - snapshot.time.minutes) > 1.0 || local.weather != snapshot.time.weather)
            {
                time_.Restore(snapshot.time);
            }
            net.pendingZombies = std::move(snapshot.zombies);
            for (auto& [id, remote] : net.remotes) remote.seen = false;
            for (const Net::FPlayerState& survivor : snapshot.survivors)
            {
                if (survivor.presence.id == net.survivorId) continue;
                auto [it, fresh] = net.remotes.try_emplace(survivor.presence.id);
                FNetSession::FRemote& remote = it->second;
                if (fresh) remote.feet = survivor.presence.feet;
                remote.targetFeet = survivor.presence.feet;
                remote.presentation = survivor.presentation;
                remote.alive = survivor.presence.alive;
                remote.seen = true;
            }
            for (auto it = net.remotes.begin(); it != net.remotes.end();)
            {
                if (it->second.seen) { ++it; continue; }
                if (it->second.rig >= 0) remoteRigs_[it->second.rig]->SetVisible(false);
                it = net.remotes.erase(it);
            }
            break;
        }
        case Net::EMessage::PlayerLeft:
        {
            const auto id = reader.Read<uint32_t>();
            if (const auto it = net.remotes.find(id); it != net.remotes.end())
            {
                if (it->second.rig >= 0) remoteRigs_[it->second.rig]->SetVisible(false);
                net.remotes.erase(it);
            }
            break;
        }
        case Net::EMessage::DamageNotice:
        {
            Net::FDamageNotice notice;
            if (Net::Read(reader, notice)) netDamage_.push_back({notice.damage, notice.shooterId == net.survivorId});
            break;
        }
        case Net::EMessage::SurvivorStrike:
        {
            Net::FSurvivorStrike strike;
            if (!Net::Read(reader, strike)) break;
            FPlayerDamageRequest request;
            request.targetId = LocalSurvivorId();
            request.amount = strike.amount;
            request.sourcePosition = strike.source;
            netStrikes_.push_back(request);
            break;
        }
        case Net::EMessage::PvpHit:
        {
            Net::FPvpHit hit;
            if (!Net::Read(reader, hit)) break;
            FPlayerDamageRequest request;
            request.targetId = LocalSurvivorId();
            request.amount = hit.amount;
            request.sourcePosition = hit.source;
            netStrikes_.push_back(request);
            debugger_.Log(NextDayz::Debug::EChannel::Player, 1,
                          NextDayz::L10n::Message("Shot by survivor {} for {:.0f}", hit.survivorId, hit.amount));
            break;
        }
        default:
            break;
        }
    }
}

void NextDayzGameInstance::TickServer(float dt)
{
    FNetSession& net = *net_;
    // The world runs with every connected survivor in it, nobody else.
    world_.SetWorldSightScale(WorldSightScale());
    world_.spawns.SetGlobalCap(1.0f - time_.Daylight() > 0.6f ? 48 : 40);
    world_.TickSpawns(dt);
    world_.TickInfected(dt);
    for (const FPlayerDamageRequest& strike : world_.ConsumeSurvivorHits())
    {
        if (FNetSession::FClient* client = net.ClientBySurvivor(strike.targetId))
        {
            FNetWriter writer;
            Net::Write(writer, Net::FSurvivorStrike{strike.amount, strike.sourcePosition});
            net.host.Send(client->peer, writer.Bytes(), EDelivery::Reliable);
        }
    }
    world_.noise.Update(dt);
    time_.Tick(static_cast<double>(dt), GetEngine().GetScene());

    net.snapshotTimer += dt;
    if (net.snapshotTimer < kSnapshotSeconds || net.clients.empty())
    {
        return;
    }
    net.snapshotTimer = std::fmod(net.snapshotTimer, kSnapshotSeconds);
    Net::FSnapshot snapshot;
    snapshot.time = time_.Capture();
    for (const FZombieRuntime& zombie : world_.zombies.Slots())
    {
        if (zombie.active) snapshot.zombies.push_back(zombie);
    }
    std::vector<Net::FPlayerState> everyone;
    for (const auto& [peer, client] : net.clients)
    {
        if (client.hasState) everyone.push_back(client.state);
    }
    for (const auto& [peer, client] : net.clients)
    {
        if (client.survivorId == 0) continue;
        snapshot.survivors.clear();
        for (const Net::FPlayerState& state : everyone)
        {
            if (state.presence.id != client.survivorId) snapshot.survivors.push_back(state);
        }
        FNetWriter writer;
        Net::Write(writer, snapshot);
        net.host.Send(peer, writer.Bytes(), EDelivery::Unreliable);
    }
}

void NextDayzGameInstance::NetClientSend(float dt)
{
    if (!net_)
    {
        return;
    }
    FNetSession& net = *net_;
    if (!net.host.IsOpen() || (!net.connected && net.rejected.empty() && net.status == "disconnected"))
    {
        net.reconnectTimer += dt;
        if (net.reconnectTimer >= kReconnectSeconds)
        {
            net.reconnectTimer = 0.0f;
            std::string error;
            if (net.host.Connect(netHostCvar_, static_cast<uint16_t>(netPortCvar_), error))
                net.status = fmt::format("connecting to {}:{}", netHostCvar_, netPortCvar_);
        }
        return;
    }
    if (net.survivorId == 0)
    {
        return;
    }
    const FPeerId server = net.host.ServerPeer();
    net.sendTimer += dt;
    if (net.sendTimer >= kStateSeconds)
    {
        net.sendTimer = std::fmod(net.sendTimer, kStateSeconds);
        Net::FPlayerState state;
        state.presence = LocalSurvivor();
        state.presence.id = net.survivorId;
        state.presentation = LocalPresentation();
        FNetWriter writer;
        Net::Write(writer, state);
        net.host.Send(server, writer.Bytes(), EDelivery::Unreliable);
    }
    // Our shots and footfalls are what the server's infected hear.
    for (const FNoiseEvent& noise : world_.noise.Events())
    {
        if (noise.sequence <= net.lastNoiseSequence) continue;
        net.lastNoiseSequence = noise.sequence;
        FNetWriter writer;
        Net::Write(writer, Net::FNoise{noise.position, noise.radius, noise.strength, noise.type});
        net.host.Send(server, writer.Bytes(), EDelivery::Reliable);
    }
}

void NextDayzGameInstance::NetSendWeaponHit(const FWeaponHitEvent& hit)
{
    if (!net_ || net_->survivorId == 0)
    {
        return;
    }
    FNetWriter writer;
    Net::Write(writer, hit);
    net_->host.Send(net_->host.ServerPeer(), writer.Bytes(), EDelivery::Reliable);
}

void NextDayzGameInstance::NetCheckPvp(const FWeaponTraceEvent& trace)
{
    if (!net_ || !net_->pvp || net_->survivorId == 0)
    {
        return;
    }
    const FWeaponDef* definition = FindWeaponDef(weapons_.ActiveWeaponId());
    if (!definition)
    {
        return;
    }
    // The round stops at whatever it hit first: the world or an infected.
    const float reach = glm::distance(trace.origin, trace.endPoint);
    const glm::vec3 end = trace.origin + trace.direction * reach;
    uint32_t victim = 0;
    float bestAlong = std::numeric_limits<float>::max();
    glm::vec3 point{};
    for (const auto& [id, remote] : net_->remotes)
    {
        if (!remote.alive) continue;
        float along = 0.0f;
        const glm::vec3 low = remote.feet + glm::vec3(0.0f, kSurvivorRadius, 0.0f);
        const glm::vec3 high = remote.feet + glm::vec3(0.0f, 1.75f - kSurvivorRadius, 0.0f);
        if (SegmentDistance(trace.origin, end, low, high, along) <= kSurvivorRadius && along < bestAlong)
        {
            bestAlong = along;
            victim = id;
            point = trace.origin + (end - trace.origin) * along;
        }
    }
    if (victim == 0)
    {
        return;
    }
    const float height = point.y - net_->remotes[victim].feet.y;
    const float zone = height > 1.45f ? 2.0f : height < 0.9f ? 0.6f : 1.0f;
    Net::FPvpHit hit{victim, definition->baseDamage * zone * kPvpDamageScale, trace.origin, point};
    FNetWriter writer;
    Net::Write(writer, hit);
    net_->host.Send(net_->host.ServerPeer(), writer.Bytes(), EDelivery::Reliable);
    effects_.BloodHit(point, trace.direction, -trace.direction, height > 1.45f ? EHitZone::Head : EHitZone::Torso, false);
    hitMarkerKill_ = false;
    hitMarkerHead_ = height > 1.45f;
    hitMarkerSeconds_ = 0.12f;
}

void NextDayzGameInstance::ApplyZombieReplication(float dt)
{
    if (!net_)
    {
        return;
    }
    FNetSession& net = *net_;
    if (net.pendingZombies)
    {
        world_.zombies.ApplyReplicas(*net.pendingZombies);
        net.zombieTargets.clear();
        for (const FZombieRuntime& zombie : *net.pendingZombies)
        {
            net.zombieTargets.push_back({zombie.handle, zombie.position, zombie.forward});
        }
        net.pendingZombies.reset();
    }
    const float blend = Ease(kRemoteEaseRate, dt);
    for (const FNetSession::FEase& target : net.zombieTargets)
    {
        FZombieRuntime* zombie = world_.zombies.Resolve(target.handle);
        if (!zombie) continue;
        // Far behind (a respawned slot, a long hitch): jump instead of sliding across the map.
        if (glm::distance(zombie->position, target.position) > 4.0f)
        {
            zombie->position = target.position;
        }
        zombie->position = glm::mix(zombie->position, target.position, blend);
        const glm::vec3 forward = glm::mix(zombie->forward, target.forward, blend);
        if (glm::length(forward) > 1.0e-3f) zombie->forward = glm::normalize(forward);
    }
}

void NextDayzGameInstance::UpdateRemoteSurvivors(float dt)
{
    if (!net_)
    {
        return;
    }
    std::vector<bool> used(remoteRigs_.size(), false);
    for (const auto& [id, remote] : net_->remotes)
        if (remote.rig >= 0) used[remote.rig] = true;
    const float blend = Ease(kRemoteEaseRate, dt);
    for (auto& [id, remote] : net_->remotes)
    {
        if (remote.rig < 0)
        {
            const auto free = std::find(used.begin(), used.end(), false);
            if (free == used.end()) continue;   // more survivors than prepared bodies
            remote.rig = static_cast<int>(free - used.begin());
            used[remote.rig] = true;
            remote.feet = remote.targetFeet;
        }
        if (glm::distance(remote.feet, remote.targetFeet) > 6.0f) remote.feet = remote.targetFeet;
        remote.feet = glm::mix(remote.feet, remote.targetFeet, blend);
        PlayerRigVisual& rig = *remoteRigs_[remote.rig];
        rig.SetVisible(remote.alive);
        rig.Update(remote.feet, remote.presentation, dt);
    }
}

void NextDayzGameInstance::RegisterNetQueries(Runtime::Agent::FAgentQueryRegistry& reg)
{
    using Runtime::Agent::FAgentQueryValue;
    reg.Add("netSurvivorId", [this]() -> FAgentQueryValue { return int64_t(net_ ? net_->survivorId : 0); });
    reg.Add("netOnline", [this]() -> FAgentQueryValue { return net_ && net_->survivorId != 0; });
    reg.Add("netRemotes", [this]() -> FAgentQueryValue { return int64_t(net_ ? net_->remotes.size() : 0); });
    reg.Add("netVisibleRemotes", [this]() -> FAgentQueryValue
    {
        int64_t count = 0;
        if (net_)
            for (const auto& [id, remote] : net_->remotes)
                count += remote.rig >= 0 && remoteRigs_[remote.rig]->IsVisible() ? 1 : 0;
        return count;
    });
    reg.Add("netSnapshots", [this]() -> FAgentQueryValue { return int64_t(net_ ? net_->snapshots : 0); });
    reg.Add("netPvp", [this]() -> FAgentQueryValue { return net_ && net_->pvp; });
    reg.Add("netInfected", [this]() -> FAgentQueryValue { return int64_t(world_.zombies.ActiveCount()); });
    reg.Add("netClients", [this]() -> FAgentQueryValue { return int64_t(net_ ? net_->clients.size() : 0); });
}

void NextDayzGameInstance::DrawNetStatus()
{
    if (!net_)
    {
        return;
    }
    std::string text;
    if (netMode_ == ENetMode::Server)
    {
        text = fmt::format("SERVER {}  survivors {}  infected {}  friendly fire {}", net_->status,
                           net_->clients.size(), world_.zombies.ActiveCount(), net_->pvp ? "ON" : "OFF");
    }
    else
    {
        const NextNet::FPeerStats stats = net_->host.Stats(net_->host.ServerPeer());
        text = net_->survivorId != 0
            ? fmt::format("ONLINE survivor {}  rtt {} ms  others {}  friendly fire {}", net_->survivorId,
                          stats.roundTripMs, net_->remotes.size(), net_->pvp ? "ON" : "OFF")
            : fmt::format("NET {}", net_->status);
    }
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    const ImVec2 at(12.0f, ImGui::GetIO().DisplaySize.y - 28.0f);
    draw->AddText(ImVec2(at.x + 1.0f, at.y + 1.0f), IM_COL32(0, 0, 0, 200), text.c_str());
    draw->AddText(at, IM_COL32(210, 230, 200, 255), text.c_str());
}

#else

// Built without the NextNet module (mobile): single player only.
void NextDayzGameInstance::ConfigureNetCVars(NextCVar::FCVarSystem&) {}
uint32_t NextDayzGameInstance::LocalSurvivorId() const { return NextDayz::kLocalPlayerTargetId; }
void NextDayzGameInstance::StartNetwork()
{
    if (netMode_ != ENetMode::Offline) SPDLOG_ERROR("[NextDayz] this build has no networking; playing offline");
    netMode_ = ENetMode::Offline;
}
void NextDayzGameInstance::ShutdownNetwork() {}
void NextDayzGameInstance::NetPoll() {}
void NextDayzGameInstance::TickServer(float) {}
void NextDayzGameInstance::NetClientSend(float) {}
void NextDayzGameInstance::NetSendWeaponHit(const NextDayz::FWeaponHitEvent&) {}
void NextDayzGameInstance::NetCheckPvp(const NextDayz::FWeaponTraceEvent&) {}
void NextDayzGameInstance::ApplyZombieReplication(float) {}
void NextDayzGameInstance::UpdateRemoteSurvivors(float) {}
void NextDayzGameInstance::DrawNetStatus() {}
void NextDayzGameInstance::RegisterNetQueries(Runtime::Agent::FAgentQueryRegistry&) {}

#endif

void NextDayzGameInstance::FNetSessionDeleter::operator()(FNetSession* session) const
{
    delete session;
}
