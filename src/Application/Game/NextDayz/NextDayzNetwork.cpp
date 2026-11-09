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
#include "Application/Game/NextDayz/Sim/ItemLedger.hpp"
#include "Application/Game/NextDayz/Sim/LagCompensation.hpp"
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
    // What each client is told about: survivors within sight (scopes reach far),
    // infected around where they spawn and hunt.
    constexpr float kSurvivorRelevance = 500.0f;
    constexpr float kInfectedRelevance = 250.0f;
    // Lag compensation: a client sees the world a round trip plus its
    // interpolation behind; a hit must land on a body as it stood then.
    constexpr double kClientInterpolationSeconds = 0.12;
    constexpr double kRewindWindowSeconds = 0.15;
    constexpr float kHitToleranceMeters = 1.05f;   // body radius plus aim and ease slop

    float Ease(float rate, float dt) { return 1.0f - std::exp(-rate * std::max(dt, 0.0f)); }

    std::string ItemDisplayName(std::string_view itemId)
    {
        const FItemDef* definition = FindItemDef(itemId);
        return definition ? std::string(definition->displayName) : std::string(itemId);
    }

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
        // What the server knows this survivor carries (see Sim/ItemLedger).
        enum class ELife : uint8_t
        {
            AwaitingKit,   // a new life: the first inventory must be a starting kit
            Alive,
            Dead,          // until the client starts its next life
        };
        ELife life = ELife::AwaitingKit;
        uint32_t lifeNumber = 0;
        FItemTotals carried;
        FInventoryState inventory;   // the last report the ledger accepted
        uint32_t refusals = 0;
        // The body, kept here: the client only sees it.
        SurvivalSystem survival;
        Inventory body;              // `inventory` as an object: armour, warmth, uses
        float survivalSyncTimer = 0.0f;
        // Movement sanity: where the last report put them, and when.
        glm::vec3 lastFeet{};
        std::chrono::steady_clock::time_point lastStateTime{};
        uint32_t suspiciousMoves = 0;
    };
    std::unordered_map<FPeerId, FClient> clients;
    uint32_t nextSurvivorId = Net::kFirstSurvivorId;
    float snapshotTimer = 0.0f;
    uint64_t lootRevision = 0;   // loot changes up to here have been broadcast
    double clock = 0.0;          // server simulation seconds
    FLagHistory history;
    uint32_t rejectedHits = 0;

    FClient* ClientBySurvivor(uint32_t survivorId)
    {
        for (auto& [peer, client] : clients)
            if (client.survivorId == survivorId) return &client;
        return nullptr;
    }

    // A survivor who dies or leaves leaves everything the server knows they carried.
    static void DropBody(FClient& client, LootSystem& loot, NextEngine& engine)
    {
        std::vector<FLootStack> body = InventoryAsStacks(client.inventory);
        if (!body.empty() && client.hasState)
        {
            loot.SpawnPile(std::move(body), client.state.presence.feet, 0.0f, engine);
        }
        client.carried.clear();
        client.inventory = {};
        client.body.Clear();
    }

    void SendSurvival(FClient& client)
    {
        client.survivalSyncTimer = 0.0f;
        FNetWriter writer;
        Net::Write(writer, Net::FSurvivalState{client.lifeNumber, client.survival.Snapshot()});
        host.Send(client.peer, writer.Bytes(), EDelivery::Reliable);
    }

    // The ledger accepted this inventory: it is now what the body carries.
    static void Accept(FClient& client, FInventoryState inventory)
    {
        client.carried = CountItems(inventory);
        client.inventory = std::move(inventory);
        Inventory body;
        if (body.Restore(client.inventory)) client.body = std::move(body);
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
        std::vector<std::string> clothing;
        bool clothingDirty = true;
        bool alive = true;
        bool seen = false;
    };
    std::unordered_map<uint32_t, FRemote> remotes;
    // Loot: synced once per loaded map; pick-ups wait here for the server's answer.
    uint64_t lootSyncedScene = 0;
    uint64_t lootRequestedScene = 0;
    uint32_t nextTakeRequest = 1;
    struct FPendingTake
    {
        FLootHandle handle;
        FLootReplica entry;
        FContainerId destination = 0;
        EEquipSlot equipSlot = EEquipSlot::None;
        std::optional<FGridPlacement> placement;
    };
    std::unordered_map<uint32_t, FPendingTake> pendingTakes;
    // Inventory reports: sent when changed, at most this often.
    float inventoryTimer = 0.0f;
    std::vector<std::byte> lastInventorySync;
    uint32_t fixes = 0;
    uint32_t myHits = 0;          // damage notices for our own rounds
};

namespace
{
    // A fresh survivor's starting inventory, built the way a client builds it.
    FInventoryState StartingInventory(bool testingKit)
    {
        Inventory inventory;
        inventory.TryAdd("bandage", "Bandage", EItemKind::Consumable, 1);
        inventory.TryAdd("water_bottle_empty", "Empty Bottle", EItemKind::Misc, 1);
        if (testingKit)
        {
            FItemInstanceId backpack = 0;
            if (inventory.TryAdd("backpack", "Backpack", EItemKind::Clothing, 1, &backpack)) inventory.TryEquip(backpack);
            FItemInstanceId rifle = 0;
            if (inventory.TryAdd("ak", "AK-74", EItemKind::Weapon, 1, &rifle)) inventory.SetLoadedAmmo(rifle, 30);
            inventory.TryAdd("ammo_545", "5.45x39", EItemKind::Ammo, 60);
        }
        return inventory.Capture();
    }
}

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
    cvars.RegisterBool("nextdayz.net.cheats", false, &netCheatsCvar_, ECVarFlags::None,
        "Server: honour clients' debug harm (automated tests only)");
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
    cvars.RegisterInt("nextdayz.validation.netTake", 0, &validationNetTake_, ECVarFlags::None,
        "Agent-only (client): 1 = take the available loot nearest the map's first safe point; "
        "2 = ask for the nearest spot even if it looks taken here (the server must refuse a stale request)", [this]()
        {
            if (!GOption->AgentValidation || !net_ || validationNetTake_ == 0 || net_->survivorId == 0) return;
            const auto safe = world_.anchors.Find(EWorldAnchorType::PlayerSafe);
            const FWorldAnchor* anchor = safe.empty() ? nullptr : world_.anchors.Resolve(safe.front());
            if (!anchor) return;
            const bool stale = validationNetTake_ == 2;
            int best = -1;
            float nearest = std::numeric_limits<float>::max();
            const auto& entries = loot_.Entries();
            for (size_t index = 0; index < entries.size(); ++index)
            {
                if (entries[index].source == ELootSource::Pile) continue;
                if (!stale && (entries[index].state != FLootEntry::EState::Available || entries[index].contents.empty())) continue;
                const float distance = glm::distance(entries[index].worldPos, anchor->worldPos);
                if (distance < nearest) { nearest = distance; best = static_cast<int>(index); }
            }
            if (best < 0) return;
            // Stand next to it, as a survivor reaching for it would.
            TeleportPlayer(entries[best].worldPos);
            std::optional<FLootHandle> handle = loot_.ReserveEntry(best);
            if (!handle && !stale) return;
            if (!handle) handle = FLootHandle{static_cast<uint32_t>(best), entries[best].nodeInstanceId, 0};
            validationNetTakeId_ = entries[best].persistentId;
            validationNetTakeResult_ = 0;
            // Our survivor's new position reaches the server before the request.
            FNetWriter state;
            Net::FPlayerState presence;
            presence.presence = LocalSurvivor();
            presence.presence.id = net_->survivorId;
            presence.presence.feet = entries[best].worldPos;
            presence.presence.eye = entries[best].worldPos + glm::vec3(0.0f, 1.6f, 0.0f);
            presence.presentation = LocalPresentation();
            Net::Write(state, presence);
            net_->host.Send(net_->host.ServerPeer(), state.Bytes(), EDelivery::Reliable);
            NetRequestTake(*handle, 0, EEquipSlot::None, std::nullopt);
        });
    cvars.RegisterInt("nextdayz.validation.netShoot", 0, &validationNetShoot_, ECVarFlags::None,
        "Agent-only (client): stand 2 m from the nearest infected and report a hit on its chest; "
        "2 = report the same hit 5 m to its side (the server must refuse it)", [this]()
        {
            if (!GOption->AgentValidation || !net_ || validationNetShoot_ == 0 || net_->survivorId == 0) return;
            const FZombieRuntime* target = nullptr;
            float nearest = std::numeric_limits<float>::max();
            for (const FZombieRuntime& zombie : world_.zombies.Slots())
            {
                if (!zombie.active || zombie.state == EZombieState::Dead) continue;
                const float distance = glm::distance(zombie.position, player_.Position());
                if (distance < nearest) { nearest = distance; target = &zombie; }
            }
            if (!target) return;
            const glm::vec3 chest = target->position + glm::vec3(0.0f, 1.3f, 0.0f);
            const glm::vec3 side = glm::normalize(glm::cross(target->forward, glm::vec3(0.0f, 1.0f, 0.0f)));
            const glm::vec3 feet = target->position + target->forward * 2.0f;
            TeleportPlayer(feet);
            // Our new stance reaches the server before the shot.
            Net::FPlayerState stance;
            stance.presence = LocalSurvivor();
            stance.presence.id = net_->survivorId;
            stance.presence.feet = feet;
            stance.presence.eye = feet + glm::vec3(0.0f, 1.6f, 0.0f);
            stance.presentation = LocalPresentation();
            FNetWriter state;
            Net::Write(state, stance);
            net_->host.Send(net_->host.ServerPeer(), state.Bytes(), EDelivery::Reliable);
            FWeaponHitEvent hit;
            hit.sequence = ++validationAttackSequence_;
            hit.weaponId = "ak";
            hit.origin = stance.presence.eye;
            hit.hitPoint = validationNetShoot_ == 2 ? chest + side * 5.0f : chest;
            hit.direction = glm::normalize(hit.hitPoint - hit.origin);
            hit.zombie = target->handle;
            hit.zone = EHitZone::Torso;
            NetSendWeaponHit(hit);
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
        const auto dropBody = [this](FNetSession::FClient& client)
        {
            FNetSession::DropBody(client, loot_, GetEngine());
        };
        // When the shooter saw the world, and whether a round from `from` reaches `to`.
        const auto shooterTime = [&net](const FNetSession::FClient& shooter)
        {
            const double rtt = static_cast<double>(net.host.Stats(shooter.peer).roundTripMs) / 1000.0;
            return net.clock - (rtt * 0.5 + kClientInterpolationSeconds);
        };
        const auto worldBlocks = [this](const glm::vec3& from, const glm::vec3& to)
        {
            const glm::vec3 delta = to - from;
            const float distance = glm::length(delta);
            if (distance < 0.05f) return false;
            bool blocked = false;
            GetEngine().RayCast(from, delta / distance, [&](Assets::RayCastResult result)
            {
                blocked = result.Hit && result.T < distance - 0.3f;
                return true;
            });
            return blocked;
        };
        const auto rejectHit = [&net](const FNetSession::FClient& shooter, const char* why)
        {
            if (net.rejectedHits++ % 10 == 0)
            {
                SPDLOG_WARN("[NextDayz][net] survivor {} hit refused: {} ({} refused so far)", shooter.survivorId, why,
                            net.rejectedHits);
            }
        };
        const auto fix = [&net](FNetSession::FClient& client, std::string reason)
        {
            ++client.refusals;
            SPDLOG_WARN("[NextDayz][net] survivor {} inventory refused: {}", client.survivorId, reason);
            FNetWriter writer;
            Net::Write(writer, Net::FInventoryFix{client.lifeNumber, std::move(reason), client.inventory});
            net.host.Send(client.peer, writer.Bytes(), EDelivery::Reliable);
        };
        for (NextNet::FNetEvent& event : events)
        {
            if (event.type == NextNet::FNetEvent::EType::Connected)
            {
                FNetSession::FClient& client = net.clients[event.peer];
                client.peer = event.peer;
                client.survival.Configure(config_.Survival);
                continue;
            }
            if (event.type == NextNet::FNetEvent::EType::Disconnected)
            {
                const auto it = net.clients.find(event.peer);
                if (it == net.clients.end()) continue;
                if (it->second.survivorId != 0)
                {
                    if (it->second.life == FNetSession::FClient::ELife::Alive) dropBody(it->second);
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
                // Movement is the client's; the server only notices impossible moves.
                const auto now = std::chrono::steady_clock::now();
                if (client.hasState && client.life == FNetSession::FClient::ELife::Alive)
                {
                    const float seconds = std::max(0.05f, std::chrono::duration<float>(now - client.lastStateTime).count());
                    const glm::vec2 moved(state.presence.feet.x - client.lastFeet.x, state.presence.feet.z - client.lastFeet.z);
                    if (glm::length(moved) > 6.0f && glm::length(moved) / seconds > 25.0f)
                    {
                        if (client.suspiciousMoves++ % 20 == 0)
                        {
                            SPDLOG_WARN("[NextDayz][net] survivor {} moved {:.1f} m in {:.2f} s", client.survivorId,
                                        glm::length(moved), seconds);
                        }
                    }
                }
                client.lastFeet = state.presence.feet;
                client.lastStateTime = now;
                // Alive is the server's to say: a body it keeps dead stays dead.
                state.presence.alive = client.survival.IsAlive() && client.life != FNetSession::FClient::ELife::Dead;
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
                if (hit.zombie.IsValid())
                {
                    const std::optional<float> miss = net.history.ZombieDistance(hit.zombie, hit.hitPoint,
                                                                                 shooterTime(client), kRewindWindowSeconds);
                    if (!miss || *miss > kHitToleranceMeters) { rejectHit(client, "no infected stood there"); break; }
                    if (worldBlocks(hit.origin, hit.hitPoint)) { rejectHit(client, "the shot passes through the world"); break; }
                }
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
                const std::optional<float> miss = net.history.SurvivorDistance(hit.survivorId, hit.point,
                                                                               shooterTime(client), kRewindWindowSeconds);
                if (!miss || *miss > kHitToleranceMeters) { rejectHit(client, "no survivor stood there"); break; }
                if (worldBlocks(hit.source, hit.point)) { rejectHit(client, "the shot passes through the world"); break; }
                hit.amount = std::clamp(hit.amount, 0.0f, 150.0f);
                if (!victim->survival.ApplyInfectedHit(hit.amount, victim->body.Armor(), survivalRng_.UnitFloat())) break;
                hit.survivorId = client.survivorId;   // the victim learns who shot
                FNetWriter writer;
                Net::Write(writer, hit);
                net.host.Send(victim->peer, writer.Bytes(), EDelivery::Reliable);
                net.SendSurvival(*victim);
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
            case Net::EMessage::LootSync:
            {
                if (!sceneReady_) break;
                FNetWriter writer;
                Net::Write(writer, Net::FLootState{true, loot_.Replicas(0)});
                net.host.Send(event.peer, writer.Bytes(), EDelivery::Reliable);
                break;
            }
            case Net::EMessage::LootTake:
            {
                Net::FLootTake take;
                if (!Net::Read(reader, take)) break;
                Net::FLootGrant grant;
                grant.request = take.request;
                const int index = loot_.FindEntry(take.id);
                // Whoever's request arrives first gets it; it has to be within reach.
                if (index >= 0 && client.hasState && client.life == FNetSession::FClient::ELife::Alive &&
                    glm::distance(loot_.Entries()[index].worldPos, client.state.presence.eye) <= config_.Loot.ReachMeters + 2.0f)
                {
                    if (std::optional<std::vector<FLootStack>> contents = loot_.TakeAll(index, GetEngine()))
                    {
                        AddTotals(client.carried, CountItems(*contents));
                        grant.granted = true;
                        grant.contents = std::move(*contents);
                    }
                }
                FNetWriter writer;
                Net::Write(writer, grant);
                net.host.Send(event.peer, writer.Bytes(), EDelivery::Reliable);
                if (grant.granted)
                {
                    SPDLOG_INFO("[NextDayz][net] survivor {} took '{}'", client.survivorId, take.id);
                }
                break;
            }
            case Net::EMessage::LootDrop:
            {
                Net::FLootDrop drop;
                if (!Net::Read(reader, drop) || !client.hasState || drop.contents.empty()) break;
                if (glm::distance(drop.position, client.state.presence.feet) > 6.0f) break;
                if (client.life != FNetSession::FClient::ELife::Alive ||
                    !SubtractTotals(client.carried, CountItems(drop.contents)))
                {
                    fix(client, "dropped something it does not carry");
                    break;
                }
                loot_.SpawnPile(std::move(drop.contents), drop.position, drop.yaw, GetEngine());
                break;
            }
            case Net::EMessage::InventorySync:
            {
                Net::FInventorySync sync;
                if (!Net::Read(reader, sync) || sync.life < client.lifeNumber) break;
                using ELife = FNetSession::FClient::ELife;
                if (sync.life > client.lifeNumber)
                {
                    // A new life; a previous one that never reported its death leaves its body.
                    if (client.life == ELife::Alive) dropBody(client);
                    client.lifeNumber = sync.life;
                    client.life = ELife::AwaitingKit;
                    client.carried.clear();
                    client.inventory = {};
                    client.survival.Reset();
                    net.SendSurvival(client);
                }
                if (!sync.alive)
                {
                    if (client.life == ELife::Alive) dropBody(client);
                    client.life = ELife::Dead;
                    break;
                }
                if (client.life == ELife::Dead) break;
                const FItemTotals reported = CountItems(sync.inventory);
                if (client.life == ELife::AwaitingKit)
                {
                    const FLedgerVerdict verdict = CheckConservation(StartingKit(netKitCvar_), reported);
                    client.life = ELife::Alive;
                    if (!verdict.accepted)
                    {
                        FNetSession::Accept(client, StartingInventory(netKitCvar_));
                        fix(client, "not a starting kit: " + verdict.reason);
                        break;
                    }
                    FNetSession::Accept(client, std::move(sync.inventory));
                    break;
                }
                const FLedgerVerdict verdict = CheckConservation(client.carried, reported);
                if (!verdict.accepted)
                {
                    fix(client, verdict.reason);
                    break;
                }
                FNetSession::Accept(client, std::move(sync.inventory));
                break;
            }
            case Net::EMessage::SurvivalAction:
            {
                Net::FSurvivalAction action;
                if (!Net::Read(reader, action) || client.life != FNetSession::FClient::ELife::Alive ||
                    !client.survival.IsAlive())
                {
                    break;
                }
                switch (action.action)
                {
                case Net::ESurvivalAction::UseItem:
                    // Only what the ledger knows they carry can be eaten, drunk or put on a wound.
                    if (client.body.FindInstance(action.instanceId) &&
                        client.survival.TryUseItem(client.body, action.instanceId))
                    {
                        FNetSession::Accept(client, client.body.Capture());
                    }
                    break;
                case Net::ESurvivalAction::DrinkFromWell:
                {
                    bool nearWell = false;
                    for (const FWorldAnchorHandle handle : world_.anchors.Find(EWorldAnchorType::Well))
                    {
                        const FWorldAnchor* well = world_.anchors.Resolve(handle);
                        nearWell = nearWell || (well && glm::distance(well->worldPos, client.state.presence.eye) <
                                                            config_.Loot.ReachMeters + 2.0f);
                    }
                    if (nearWell) client.survival.DrinkFromWell();
                    break;
                }
                case Net::ESurvivalAction::Rest:
                    client.survival.Rest();
                    break;
                case Net::ESurvivalAction::DebugHarm:
                    if (netCheatsCvar_) client.survival.ApplyDamage(std::clamp(action.amount, 0.0f, 500.0f), "debug", false);
                    break;
                }
                net.SendSurvival(client);
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
        case Net::EMessage::LootState:
        {
            Net::FLootState state;
            if (!Net::Read(reader, state) || !sceneReady_) break;
            // Changes before the full picture are already part of it.
            if (!state.full && net.lootSyncedScene != geographySceneGeneration_) break;
            for (const FLootReplica& entry : state.entries) loot_.ApplyReplica(entry, GetEngine());
            if (state.full)
            {
                net.lootSyncedScene = geographySceneGeneration_;
                SPDLOG_INFO("[NextDayz][net] loot synced: {} entries", state.entries.size());
            }
            break;
        }
        case Net::EMessage::LootGrant:
        {
            Net::FLootGrant grant;
            if (!Net::Read(reader, grant)) break;
            const auto it = net.pendingTakes.find(grant.request);
            if (it == net.pendingTakes.end()) break;
            FNetSession::FPendingTake pending = std::move(it->second);
            net.pendingTakes.erase(it);
            if (pending.entry.id == validationNetTakeId_) validationNetTakeResult_ = grant.granted ? 1 : 2;
            if (!grant.granted)
            {
                loot_.Cancel(pending.handle);
                Notify("Someone else got there first", 1);
                break;
            }
            // It is ours: gone from the ground at once, here and (by the server) everywhere.
            FLootReplica gone = pending.entry;
            gone.available = false;
            loot_.ApplyReplica(gone, GetEngine());
            std::vector<FLootStack> left;
            const std::vector<FLootStack> taken = LootSystem::AddToInventory(
                grant.contents, inventory_, pending.destination, pending.equipSlot, pending.placement, left);
            if (!taken.empty())
            {
                SyncClothingVisuals();
                audio_.Pickup();
                for (const FLootStack& stack : taken)
                {
                    Notify(stack.count > 1 ? L10n::Message("Picked up {} x{}", ItemDisplayName(stack.itemId), stack.count)
                                           : L10n::Message("Picked up {}", ItemDisplayName(stack.itemId)));
                }
            }
            if (!left.empty())
            {
                PlacePile(std::move(left), pending.entry.position, pending.entry.yaw);
                Notify(taken.empty() ? "Not enough space - drop something or find a bigger bag"
                                     : "No room for the rest - it stays here", 1);
            }
            break;
        }
        case Net::EMessage::SurvivalState:
        {
            Net::FSurvivalState state;
            if (!Net::Read(reader, state) || state.life != netLife_) break;
            survival_.Restore(state.body);
            break;
        }
        case Net::EMessage::InventoryFix:
        {
            Net::FInventoryFix fix;
            if (!Net::Read(reader, fix) || fix.life != netLife_ || !survival_.IsAlive()) break;
            ++net.fixes;
            if (inventory_.Restore(fix.inventory))
            {
                weapons_.ResetRuntime();
                SyncClothingVisuals();
            }
            SPDLOG_WARN("[NextDayz][net] the server corrected our inventory: {}", fix.reason);
            Notify("The server corrected your inventory", 2);
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
                if (remote.clothing != survivor.clothing)
                {
                    remote.clothing = survivor.clothing;
                    remote.clothingDirty = true;
                }
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
            if (!Net::Read(reader, notice)) break;
            netDamage_.push_back({notice.damage, notice.shooterId == net.survivorId});
            if (notice.shooterId == net.survivorId) ++net.myHits;
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
    net.clock += dt;
    net.history.Record(net.clock, world_.zombies.Slots(), world_.Survivors());
    for (const FPlayerDamageRequest& strike : world_.ConsumeSurvivorHits())
    {
        FNetSession::FClient* client = net.ClientBySurvivor(strike.targetId);
        if (!client || client->life != FNetSession::FClient::ELife::Alive) continue;
        if (!client->survival.ApplyInfectedHit(strike.amount, client->body.Armor(), survivalRng_.UnitFloat())) continue;
        FNetWriter writer;
        Net::Write(writer, Net::FSurvivorStrike{strike.amount, strike.sourcePosition});
        net.host.Send(client->peer, writer.Bytes(), EDelivery::Reliable);
        net.SendSurvival(*client);
    }
    // Every living body: hunger, thirst, cold, bleeding; the server says who dies.
    for (auto& [peer, client] : net.clients)
    {
        if (client.life != FNetSession::FClient::ELife::Alive) continue;
        client.survival.SetCold(std::clamp(time_.Chill() - client.body.Warmth() * 0.8f, 0.0f, 1.0f));
        client.survival.Update(dt, client.state.presentation.locomotion.gait == EPlayerGait::Sprint);
        if (!client.survival.IsAlive())
        {
            SPDLOG_INFO("[NextDayz][net] survivor {} died ({})", client.survivorId,
                        client.survival.Snapshot().lastDamageSource);
            FNetSession::DropBody(client, loot_, GetEngine());
            client.life = FNetSession::FClient::ELife::Dead;
            world_.RemoveSurvivor(client.survivorId);
            net.SendSurvival(client);
            continue;
        }
        client.survivalSyncTimer += dt;
        if (client.survivalSyncTimer >= 0.25f) net.SendSurvival(client);
    }
    world_.noise.Update(dt);
    time_.Tick(static_cast<double>(dt), GetEngine().GetScene());
    TickServerLoot(dt);

    net.snapshotTimer += dt;
    if (net.snapshotTimer < kSnapshotSeconds || net.clients.empty())
    {
        return;
    }
    net.snapshotTimer = std::fmod(net.snapshotTimer, kSnapshotSeconds);
    Net::FSnapshot snapshot;
    snapshot.time = time_.Capture();
    std::vector<const FZombieRuntime*> infected;
    for (const FZombieRuntime& zombie : world_.zombies.Slots())
    {
        if (zombie.active) infected.push_back(&zombie);
    }
    std::vector<Net::FPlayerState> everyone;
    for (const auto& [peer, client] : net.clients)
    {
        if (client.hasState) everyone.push_back(client.state);
    }
    for (const auto& [peer, client] : net.clients)
    {
        if (client.survivorId == 0) continue;
        // Only what is near this survivor: the rest of the map is not theirs to know.
        const glm::vec3 here = client.state.presence.feet;
        const auto relevant = [&](const glm::vec3& at, float radius)
        {
            return !client.hasState || glm::length(glm::vec2(at.x - here.x, at.z - here.z)) <= radius;
        };
        snapshot.survivors.clear();
        for (const Net::FPlayerState& state : everyone)
        {
            if (state.presence.id != client.survivorId && relevant(state.presence.feet, kSurvivorRelevance))
                snapshot.survivors.push_back(state);
        }
        snapshot.zombies.clear();
        for (const FZombieRuntime* zombie : infected)
        {
            if (relevant(zombie->position, kInfectedRelevance)) snapshot.zombies.push_back(*zombie);
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
    if (sceneReady_ && net.lootRequestedScene != geographySceneGeneration_)
    {
        // A freshly loaded map: the server's loot replaces our own roll of it.
        net.lootRequestedScene = geographySceneGeneration_;
        net.pendingTakes.clear();
        FNetWriter writer;
        Net::WriteLootSync(writer);
        net.host.Send(server, writer.Bytes(), EDelivery::Reliable);
    }
    net.sendTimer += dt;
    if (net.sendTimer >= kStateSeconds)
    {
        net.sendTimer = std::fmod(net.sendTimer, kStateSeconds);
        Net::FPlayerState state;
        state.presence = LocalSurvivor();
        state.presence.id = net.survivorId;
        state.presentation = LocalPresentation();
        state.clothing = inventory_.EquippedClothing();
        FNetWriter writer;
        Net::Write(writer, state);
        net.host.Send(server, writer.Bytes(), EDelivery::Unreliable);
    }
    // What we carry, whenever it changed (the server's ledger checks it).
    net.inventoryTimer += dt;
    if (sceneReady_ && survival_.IsAlive() && net.inventoryTimer >= 0.2f)
    {
        net.inventoryTimer = 0.0f;
        weapons_.CopyMagazinesTo(inventory_);
        FNetWriter writer;
        Net::Write(writer, Net::FInventorySync{netLife_, true, inventory_.Capture()});
        const std::span<const std::byte> bytes = writer.Bytes();
        if (!std::equal(bytes.begin(), bytes.end(), net.lastInventorySync.begin(), net.lastInventorySync.end()))
        {
            net.lastInventorySync.assign(bytes.begin(), bytes.end());
            net.host.Send(server, bytes, EDelivery::Reliable);
        }
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

void NextDayzGameInstance::TickServerLoot(float dt)
{
    FNetSession& net = *net_;
    std::vector<LootSystem::FViewer> viewers;
    for (const FSurvivorPresence& survivor : world_.Survivors())
    {
        if (survivor.alive) viewers.push_back({survivor.eye, survivor.forward});
    }
    loot_.TickWorld(dt, viewers, GetEngine());
    const uint64_t latest = loot_.Revision();
    if (latest == net.lootRevision)
    {
        return;
    }
    Net::FLootState changes{false, loot_.Replicas(net.lootRevision)};
    net.lootRevision = latest;
    if (changes.entries.empty() || net.clients.empty())
    {
        return;
    }
    FNetWriter writer;
    Net::Write(writer, changes);
    net.host.Broadcast(writer.Bytes(), EDelivery::Reliable);
}

void NextDayzGameInstance::NetRequestTake(const FLootHandle& handle, FContainerId destination, EEquipSlot equipSlot,
                                          std::optional<FGridPlacement> placement)
{
    if (!net_ || net_->survivorId == 0 || handle.index >= loot_.Entries().size())
    {
        loot_.Cancel(handle);
        Notify("Not connected to the server", 1);
        return;
    }
    FNetSession& net = *net_;
    const FLootEntry& entry = loot_.Entries()[handle.index];
    const uint32_t request = net.nextTakeRequest++;
    net.pendingTakes[request] = {handle, {entry.persistentId, entry.source, entry.worldPos, entry.yaw, true, entry.contents},
                                 destination, equipSlot, placement};
    FNetWriter writer;
    Net::Write(writer, Net::FLootTake{request, entry.persistentId});
    net.host.Send(net.host.ServerPeer(), writer.Bytes(), EDelivery::Reliable);
}

bool NextDayzGameInstance::PlacePile(std::vector<FLootStack> contents, const glm::vec3& position, float yaw)
{
    if (netMode_ != ENetMode::Client)
    {
        return loot_.SpawnPile(std::move(contents), position, yaw, GetEngine());
    }
    if (!net_ || net_->survivorId == 0 || contents.empty())
    {
        return false;
    }
    FNetWriter writer;
    Net::Write(writer, Net::FLootDrop{position, yaw, std::move(contents)});
    return net_->host.Send(net_->host.ServerPeer(), writer.Bytes(), EDelivery::Reliable);
}

void NextDayzGameInstance::NetDropOnDeath()
{
    if (netDeathDropped_)
    {
        return;
    }
    netDeathDropped_ = true;
    if (!net_ || net_->survivorId == 0)
    {
        return;
    }
    // The server drops the body from its own ledger; we only let go of it here.
    FNetWriter writer;
    Net::Write(writer, Net::FInventorySync{netLife_, false, {}});
    net_->host.Send(net_->host.ServerPeer(), writer.Bytes(), EDelivery::Reliable);
    net_->lastInventorySync.clear();
    inventory_.Clear();
    weapons_.ResetRuntime();
    SyncClothingVisuals();
}

void NextDayzGameInstance::NetSurvivalAction(uint8_t action, FItemInstanceId instanceId, float amount)
{
    if (!net_ || net_->survivorId == 0)
    {
        return;
    }
    FNetWriter writer;
    Net::Write(writer, Net::FSurvivalAction{static_cast<Net::ESurvivalAction>(action), instanceId, amount});
    net_->host.Send(net_->host.ServerPeer(), writer.Bytes(), EDelivery::Reliable);
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
            remote.clothingDirty = true;
            remote.feet = remote.targetFeet;
        }
        if (glm::distance(remote.feet, remote.targetFeet) > 6.0f) remote.feet = remote.targetFeet;
        remote.feet = glm::mix(remote.feet, remote.targetFeet, blend);
        PlayerRigVisual& rig = *remoteRigs_[remote.rig];
        if (remote.clothingDirty)
        {
            rig.SetClothing(remote.clothing);
            remote.clothingDirty = false;
        }
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
    reg.Add("netLootSynced", [this]() -> FAgentQueryValue
    {
        return net_ && net_->lootSyncedScene != 0 && net_->lootSyncedScene == geographySceneGeneration_;
    });
    reg.Add("netPendingTakes", [this]() -> FAgentQueryValue { return int64_t(net_ ? net_->pendingTakes.size() : 0); });
    reg.Add("netTakeResult", [this]() -> FAgentQueryValue { return int64_t(validationNetTakeResult_); });
    reg.Add("netInventoryFixes", [this]() -> FAgentQueryValue { return int64_t(net_ ? net_->fixes : 0); });
    reg.Add("netMyHits", [this]() -> FAgentQueryValue { return int64_t(net_ ? net_->myHits : 0); });
    reg.Add("netBandages", [this]() -> FAgentQueryValue { return int64_t(inventory_.CountOf("bandage")); });
    reg.Add("netLife", [this]() -> FAgentQueryValue { return int64_t(netLife_); });
    // Whether the entry the validation take went for still lies on the ground here.
    reg.Add("netTakeTargetOnGround", [this]() -> FAgentQueryValue
    {
        const int index = loot_.FindEntry(validationNetTakeId_);
        if (index < 0) return false;
        const FLootEntry::EState state = loot_.Entries()[index].state;
        return state == FLootEntry::EState::Available || state == FLootEntry::EState::Reserved;
    });
    reg.Add("netLootAvailable", [this]() -> FAgentQueryValue { return int64_t(loot_.RemainingCount()); });
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
void NextDayzGameInstance::NetRequestTake(const NextDayz::FLootHandle& handle, NextDayz::FContainerId,
                                          NextDayz::EEquipSlot, std::optional<NextDayz::FGridPlacement>)
{
    loot_.Cancel(handle);
}
bool NextDayzGameInstance::PlacePile(std::vector<NextDayz::FLootStack> contents, const glm::vec3& position, float yaw)
{
    return loot_.SpawnPile(std::move(contents), position, yaw, GetEngine());
}
void NextDayzGameInstance::TickServerLoot(float) {}
void NextDayzGameInstance::NetDropOnDeath() {}
void NextDayzGameInstance::NetSurvivalAction(uint8_t, NextDayz::FItemInstanceId, float) {}

#endif

void NextDayzGameInstance::FNetSessionDeleter::operator()(FNetSession* session) const
{
    delete session;
}
