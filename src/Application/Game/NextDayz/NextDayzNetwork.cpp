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
#include "Engine/Runtime/Config/CVarSystem.hpp"

#include <imgui.h>

#if GK_MODULE_NEXTNET
#include "Application/Game/NextDayz/Net/NetProtocol.hpp"
#include "Application/Game/NextDayz/Sim/ItemLedger.hpp"
#include "Application/Game/NextDayz/Sim/LagCompensation.hpp"
#include "Application/Game/NextDayz/Net/MasterApi.hpp"
#include "Modules/NextNet/NetHttp.hpp"
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
    // Master: how often a server commits its world, and how extraction works.
    constexpr float kCheckpointSeconds = 15.0f;
    constexpr float kExtractRadius = 12.0f;
    constexpr float kExtractHoldSeconds = 10.0f;

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
        // A Master survivor: who they are there, and their sortie.
        uint64_t playerId = 0;
        uint64_t sortie = 0;
        bool redeeming = false;
        glm::vec3 spawnFeet{};
        // Holding an extraction point.
        float extractSeconds = -1.0f;
        float extractNotify = 0.0f;
        glm::vec3 extractPoint{};
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
    // --- Master (both roles) ---
    std::unique_ptr<NextNet::FHttpWorker> master;
    // server
    bool worldRequested = false;
    bool worldLoaded = false;
    float heartbeatTimer = 0.0f;
    float checkpointTimer = 0.0f;
    uint64_t checkpointSeq = 0;
    bool stopping = false;
    float stopSeconds = 0.0f;
    std::vector<Master::FCheckpointCharacter> outcomes;   // sorties that ended since the last checkpoint
    // client lobby
    enum class ELobby : uint8_t
    {
        Off,          // not a Master client
        Login,
        Waiting,      // a request to the Master is out
        Profile,
        Deploying,    // the Master is starting a server; asking again shortly
        Joining,      // connecting to the server, waiting for our survivor
        Playing,
    };
    ELobby lobby = ELobby::Off;
    std::string token;
    Master::FProfile profile;
    Master::FDeploy deploy;
    std::vector<int> bring;
    int mapChoice = 0;            // 0 = random
    std::string lobbyMessage;
    float pollSeconds = 0.0f;
    bool autoTried = false;      // automation: logged in once
    bool autoDeployed = false;   // automation: deployed once (not again after the sortie)
    bool wantLogin = false;
    bool wantDeploy = false;
    std::array<char, 64> nameBuffer{};
    std::optional<Net::FCharacterSpawn> pendingSpawn;
    Net::FExtractState extract{};
    bool extracting = false;

    // A Master survivor's sortie changed: remember it for the next checkpoint.
    void RecordOutcome(FClient& client, Master::EOutcome outcome)
    {
        if (!master || client.playerId == 0) return;
        Master::FCheckpointCharacter c;
        c.playerId = client.playerId;
        c.sortie = client.sortie;
        c.outcome = outcome;
        c.position = client.hasState ? client.state.presence.feet : client.spawnFeet;
        c.yaw = client.state.presentation.aimYawRadians;
        c.survivalJson = SurvivalToJson(client.survival.Snapshot());
        if (outcome != Master::EOutcome::Dead) c.inventory = InventoryAsStacks(client.inventory);
        outcomes.push_back(std::move(c));
    }

    // Inventory reports: sent when changed, at most this often.
    float inventoryTimer = 0.0f;
    std::vector<std::byte> lastInventorySync;
    uint32_t fixes = 0;
    uint32_t myHits = 0;          // damage notices for our own rounds

    // --- statistics (the network panel and the server's periodic log line) ---
    struct FStats
    {
        NextNet::FTrafficStats last;
        float window = 0.0f;
        float inKBps = 0.0f;
        float outKBps = 0.0f;
        double tickMsSum = 0.0;
        double tickMsMax = 0.0;
        uint32_t ticks = 0;
        double tickMsAvgShown = 0.0;
        double tickMsMaxShown = 0.0;
        uint64_t snapshotBytes = 0;
        uint32_t snapshotCount = 0;
        uint32_t snapshotAvgShown = 0;
        uint32_t snapshotsPerSecond = 0;
        uint32_t snapshotsThisWindow = 0;
        float logSeconds = 0.0f;
    } stats;

    // Rolls the counters over once a second.
    void UpdateStats(float dt)
    {
        stats.window += dt;
        if (stats.window < 1.0f) return;
        const NextNet::FTrafficStats now = host.Traffic();
        stats.inKBps = static_cast<float>(now.bytesReceived - stats.last.bytesReceived) / 1024.0f / stats.window;
        stats.outKBps = static_cast<float>(now.bytesSent - stats.last.bytesSent) / 1024.0f / stats.window;
        stats.last = now;
        stats.tickMsAvgShown = stats.ticks ? stats.tickMsSum / stats.ticks : 0.0;
        stats.tickMsMaxShown = stats.tickMsMax;
        stats.snapshotAvgShown = stats.snapshotCount ? static_cast<uint32_t>(stats.snapshotBytes / stats.snapshotCount) : 0;
        stats.snapshotsPerSecond = static_cast<uint32_t>(stats.snapshotsThisWindow / stats.window);
        stats.tickMsSum = stats.tickMsMax = 0.0;
        stats.ticks = 0;
        stats.snapshotBytes = 0;
        stats.snapshotCount = 0;
        stats.snapshotsThisWindow = 0;
        stats.window = 0.0f;
    }
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
    cvars.RegisterString("nextdayz.net.master", "", &netMasterCvar_, ECVarFlags::None,
        "URL of the NextDayz Master (e.g. http://127.0.0.1:7780): a server joins its fleet, a client starts in its lobby");
    cvars.RegisterString("nextdayz.net.dsId", "", &netDsIdCvar_, ECVarFlags::None, "Server: id the Master gave it");
    cvars.RegisterString("nextdayz.net.masterKey", "", &netMasterKeyCvar_, ECVarFlags::None,
        "Server: key the Master gave it");
    cvars.RegisterBool("nextdayz.net.autoDeploy", false, &netAutoDeployCvar_, ECVarFlags::None,
        "Client: log in as nextdayz.net.name and deploy at once, bringing all storage (automation)");
    cvars.RegisterString("nextdayz.net.autoMap", "", &netAutoMapCvar_, ECVarFlags::None,
        "Client: map id for autoDeploy (empty: the Master picks)");
    cvars.RegisterInt("nextdayz.validation.netExtract", 0, &validationNetExtract_, ECVarFlags::None,
        "Agent-only (Master client): go to the nearest extraction point and start holding it", [this]()
        {
            if (!GOption->AgentValidation || validationNetExtract_ == 0 || !net_ || net_->survivorId == 0) return;
            const FWorldAnchor* best = nullptr;
            float nearest = std::numeric_limits<float>::max();
            for (const FWorldAnchorHandle handle : world_.anchors.Find(EWorldAnchorType::Extraction))
            {
                const FWorldAnchor* anchor = world_.anchors.Resolve(handle);
                const float distance = anchor ? glm::distance(anchor->worldPos, player_.Position()) : nearest;
                if (distance < nearest) { nearest = distance; best = anchor; }
            }
            if (!best) return;
            TeleportPlayer(best->worldPos);
            Net::FPlayerState stance;
            stance.presence = LocalSurvivor();
            stance.presence.id = net_->survivorId;
            stance.presence.feet = best->worldPos;
            stance.presence.eye = best->worldPos + glm::vec3(0.0f, 1.6f, 0.0f);
            stance.presentation = LocalPresentation();
            FNetWriter state;
            Net::Write(state, stance);
            net_->host.Send(net_->host.ServerPeer(), state.Bytes(), EDelivery::Reliable);
            NetRequestExtract();
        });
    cvars.RegisterInt("nextdayz.net.simLatency", 0, &netSimLatencyCvar_, ECVarFlags::None,
        "Delay (ms) added to everything this process sends (link simulation)", [this]() { ApplyLinkSimulation(); }, 0, 2000);
    cvars.RegisterInt("nextdayz.net.simJitter", 0, &netSimJitterCvar_, ECVarFlags::None,
        "Random +/- delay (ms) on top of simLatency", [this]() { ApplyLinkSimulation(); }, 0, 1000);
    cvars.RegisterFloat("nextdayz.net.simLoss", 0.0f, &netSimLossCvar_, ECVarFlags::None,
        "Percent of unreliable messages this process drops (reliable ones are only delayed)",
        [this]() { ApplyLinkSimulation(); }, 0.0, 100.0);
    cvars.RegisterBool("nextdayz.net.stats", false, &netStatsCvar_, ECVarFlags::None,
        "Show the network panel (RTT, loss, bandwidth, snapshots, server tick)");
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
    ApplyLinkSimulation();
    std::string error;
    const auto port = static_cast<uint16_t>(netPortCvar_);
    if (NetUsesMaster())
    {
        net_->master = std::make_unique<NextNet::FHttpWorker>();
        net_->master->SetBaseUrl(netMasterCvar_);
        if (!NextNet::HttpAvailable()) SPDLOG_ERROR("[NextDayz][net] this build cannot reach a Master (no HTTP client)");
    }
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
        if (net_->master)
        {
            net_->master->Post("/ds/register", Master::RegisterRequest(netDsIdCvar_, netMasterKeyCvar_, port, campaignMap_),
                [](const NextNet::FHttpResult& result)
                {
                    if (result.Ok()) SPDLOG_INFO("[NextDayz][net] registered with the Master");
                    else SPDLOG_ERROR("[NextDayz][net] Master refused registration: {}", result.error);
                });
        }
    }
    else if (net_->master)
    {
        // A Master client starts in the lobby; it connects once a server is ready for it.
        net_->lobby = FNetSession::ELobby::Login;
        std::snprintf(net_->nameBuffer.data(), net_->nameBuffer.size(), "%s", netNameCvar_.c_str());
        net_->status = "lobby";
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
                    if (it->second.life == FNetSession::FClient::ELife::Alive)
                    {
                        // With a Master a survivor who logs out is kept (gear and all) for later;
                        // on a direct server they leave a body.
                        if (net.master) net.RecordOutcome(it->second, Master::EOutcome::Left);
                        else dropBody(it->second);
                    }
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
                if (net.master)
                {
                    // A Master server admits only survivors the Master sent, by ticket.
                    if (hello.ticket.empty() || client.redeeming) continue;
                    client.redeeming = true;
                    const FPeerId peer = event.peer;
                    net.master->Post("/ds/redeem", Master::RedeemRequest(netDsIdCvar_, netMasterKeyCvar_, hello.ticket),
                        [this, peer](const NextNet::FHttpResult& result) { NetAdmitSurvivor(peer, result); });
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
                // A drop sent just before the server decided this survivor died arrives after the
                // death: those items are already in the body. Ignore it rather than correct anyone.
                if (client.life != FNetSession::FClient::ELife::Alive) break;
                if (!SubtractTotals(client.carried, CountItems(drop.contents)))
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
                    if (client.life == ELife::Alive)
                    {
                        net.RecordOutcome(client, Master::EOutcome::Dead);
                        dropBody(client);
                    }
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
            case Net::EMessage::Extract:
            {
                const bool begin = reader.Read<uint8_t>() != 0;
                if (!net.master || client.life != FNetSession::FClient::ELife::Alive) break;
                if (!begin) { client.extractSeconds = -1.0f; break; }
                const FWorldAnchor* point = nullptr;
                for (const FWorldAnchorHandle handle : world_.anchors.Find(EWorldAnchorType::Extraction))
                {
                    const FWorldAnchor* anchor = world_.anchors.Resolve(handle);
                    if (anchor && glm::distance(anchor->worldPos, client.state.presence.feet) <= kExtractRadius) point = anchor;
                }
                FNetWriter writer;
                if (!point)
                {
                    Net::Write(writer, Net::FExtractState{0.0f, false, true});
                    net.host.Send(client.peer, writer.Bytes(), EDelivery::Reliable);
                    break;
                }
                client.extractSeconds = kExtractHoldSeconds;
                client.extractPoint = point->worldPos;
                client.extractNotify = 0.0f;
                Net::Write(writer, Net::FExtractState{kExtractHoldSeconds, false, false});
                net.host.Send(client.peer, writer.Bytes(), EDelivery::Reliable);
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
            hello.name = net.master ? std::string(net.nameBuffer.data()) : netNameCvar_;
            hello.scene = campaignMap_;
            hello.ticket = net.deploy.ticket;
            if (net.master) net.status = "joining " + net.deploy.server;
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
            if (net.master && (net.lobby == FNetSession::ELobby::Playing || net.lobby == FNetSession::ELobby::Joining))
            {
                NetReturnToLobby(net.lobbyMessage.empty() ? "Lost the connection to the server." : net.lobbyMessage);
                return;
            }
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
        case Net::EMessage::CharacterSpawn:
        {
            Net::FCharacterSpawn spawn;
            if (!Net::Read(reader, spawn)) break;
            // Mid map change the survivor is placed once the new map is in.
            net.pendingSpawn = std::move(spawn);
            if (sceneReady_ && campaignMap_ == net.deploy.scene) NetAfterSceneLoaded();
            break;
        }
        case Net::EMessage::ExtractState:
        {
            Net::FExtractState state;
            if (!Net::Read(reader, state)) break;
            net.extract = state;
            net.extracting = !state.done && !state.broken;
            if (state.broken) Notify("Extraction broken off - stay at the point", 1);
            if (state.done)
            {
                net.lobbyMessage = "Extracted. What you carried is in your storage.";
                SPDLOG_INFO("[NextDayz][net] extracted");
            }
            break;
        }
        case Net::EMessage::Farewell:
        {
            Net::FFarewell farewell;
            if (!Net::Read(reader, farewell)) break;
            SPDLOG_INFO("[NextDayz][net] the server says goodbye: {}", farewell.message);
            if (farewell.reason != Net::EFarewell::Extracted) net.lobbyMessage = farewell.message;
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
            ++net.stats.snapshotsThisWindow;
            net.stats.snapshotBytes += event.payload.size();
            ++net.stats.snapshotCount;
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
    const auto tickStart = std::chrono::steady_clock::now();
    struct FTickTimer
    {
        FNetSession& net;
        std::chrono::steady_clock::time_point start;
        float dt;
        ~FTickTimer()
        {
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            net.stats.tickMsSum += ms;
            net.stats.tickMsMax = std::max(net.stats.tickMsMax, ms);
            ++net.stats.ticks;
            net.UpdateStats(dt);
            net.stats.logSeconds += dt;
            if (net.stats.logSeconds >= 10.0f)
            {
                net.stats.logSeconds = 0.0f;
                SPDLOG_INFO("[NextDayz][net] stats: {} survivors, tick avg {:.2f} ms max {:.2f} ms, out {:.1f} KB/s in {:.1f} KB/s, "
                            "snapshot {} B, refused hits {}", net.clients.size(), net.stats.tickMsAvgShown,
                            net.stats.tickMsMaxShown, net.stats.outKBps, net.stats.inKBps, net.stats.snapshotAvgShown,
                            net.rejectedHits);
            }
        }
    } tickTimer{net, tickStart, dt};
    // The world runs with every connected survivor in it, nobody else.
    world_.SetWorldSightScale(WorldSightScale());
    world_.spawns.SetGlobalCap(1.0f - time_.Daylight() > 0.6f ? 48 : 40);
    TickServerMaster(dt);
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
            net.RecordOutcome(client, Master::EOutcome::Dead);
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
        net.stats.snapshotBytes += writer.Size();
        ++net.stats.snapshotCount;
        ++net.stats.snapshotsThisWindow;
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
    net.UpdateStats(dt);
    if (net.master && !net.connected)
    {
        return;   // a Master client reconnects through the lobby
    }
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
    reg.Add("netLobby", [this]() -> FAgentQueryValue
    {
        if (!net_ || !net_->master) return std::string("off");
        switch (net_->lobby)
        {
        case FNetSession::ELobby::Login: return std::string("login");
        case FNetSession::ELobby::Waiting: return std::string("waiting");
        case FNetSession::ELobby::Profile: return std::string("profile");
        case FNetSession::ELobby::Deploying: return std::string("deploying");
        case FNetSession::ELobby::Joining: return std::string("joining");
        case FNetSession::ELobby::Playing: return std::string("playing");
        case FNetSession::ELobby::Off: break;
        }
        return std::string("off");
    });
    reg.Add("netStorageStacks", [this]() -> FAgentQueryValue { return int64_t(net_ ? net_->profile.storage.size() : 0); });
    reg.Add("netExtractions", [this]() -> FAgentQueryValue { return int64_t(net_ ? net_->profile.extractions : 0); });
    reg.Add("netInventoryItems", [this]() -> FAgentQueryValue { return int64_t(inventory_.Instances().size()); });
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
    DrawNetPanel();
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

// ============================================================================ Master: dedicated server

namespace
{
    // A survivor's gear, as the Master keeps it, back in a pack: clothing worn
    // first (so its pockets exist), then everything else; what does not fit is returned.
    FInventoryState BuildInventory(const std::vector<FLootStack>& stacks, bool pocketKit, std::vector<FLootStack>& leftover)
    {
        Inventory inventory;
        if (pocketKit)
        {
            inventory.TryAdd("bandage", "Bandage", EItemKind::Consumable, 1);
            inventory.TryAdd("water_bottle_empty", "Empty Bottle", EItemKind::Misc, 1);
        }
        std::vector<FLootStack> rest;
        for (const FLootStack& stack : stacks)
        {
            const FItemDef* definition = FindItemDef(stack.itemId);
            if (!definition)
            {
                continue;   // an item this build no longer knows
            }
            if (definition->kind != EItemKind::Clothing)
            {
                rest.push_back(stack);
                continue;
            }
            for (int piece = 0; piece < stack.count; ++piece)
            {
                FItemInstanceId id = 0;
                if (!inventory.TryAdd(stack.itemId, std::string(definition->displayName), definition->kind, 1, &id,
                                      kWornContainer))
                {
                    rest.push_back({stack.itemId, 1, 0, {}});
                }
            }
        }
        LootSystem::AddToInventory(rest, inventory, 0, EEquipSlot::None, std::nullopt, leftover);
        return inventory.Capture();
    }
}

void NextDayzGameInstance::NetAdmitSurvivor(uint32_t peer, const NextNet::FHttpResult& result)
{
    FNetSession& net = *net_;
    const auto it = net.clients.find(peer);
    if (it == net.clients.end())
    {
        return;   // gone while the Master answered
    }
    FNetSession::FClient& client = it->second;
    client.redeeming = false;
    Master::FRedeem redeem;
    if (!result.Ok() || !Master::ParseRedeem(result.body, redeem))
    {
        FNetWriter writer;
        Net::WriteReject(writer, result.error.empty() ? "the Master did not admit you" : result.error);
        net.host.Send(peer, writer.Bytes(), EDelivery::Reliable);
        net.host.Disconnect(peer);
        SPDLOG_WARN("[NextDayz][net] refused a survivor: {}", result.error);
        return;
    }
    for (auto& [otherPeer, other] : net.clients)
    {
        if (other.playerId == redeem.playerId && otherPeer != peer)
        {
            net.host.Disconnect(otherPeer);   // the same survivor cannot be here twice
        }
    }
    client.survivorId = net.nextSurvivorId++;
    client.playerId = redeem.playerId;
    client.sortie = redeem.sortie;
    client.name = redeem.name;

    // Where: a fresh survivor at a random safe point, a returning one where they left.
    glm::vec3 feet = redeem.character.position;
    float yaw = redeem.character.yaw;
    if (redeem.character.fresh)
    {
        const auto safe = world_.anchors.Find(EWorldAnchorType::PlayerSafe);
        if (!safe.empty())
        {
            const FWorldAnchor* anchor =
                world_.anchors.Resolve(safe[survivalRng_.Range(static_cast<uint32_t>(safe.size()))]);
            const float angle = static_cast<float>(client.survivorId) * 2.4f;
            feet = anchor->worldPos + glm::vec3(std::cos(angle), 0.0f, std::sin(angle)) * 2.5f;
            GetEngine().RayCast(feet + glm::vec3(0.0f, 3.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f),
                                [&](Assets::RayCastResult hit)
                                {
                                    if (hit.Hit && hit.T < 6.0f) feet.y = static_cast<float>(hit.HitPoint.y);
                                    return true;
                                });
            feet.y += 0.1f;
        }
        yaw = 0.0f;
    }
    client.spawnFeet = feet;

    // What they carry and how their body is.
    std::vector<FLootStack> leftover;
    const FInventoryState inventory = BuildInventory(redeem.character.inventory, redeem.character.fresh, leftover);
    if (!leftover.empty())
    {
        loot_.SpawnPile(std::move(leftover), feet, 0.0f, GetEngine());
    }
    client.survival.Reset();
    FSurvivalSnapshot body;
    if (!redeem.character.fresh && SurvivalFromJson(redeem.character.survivalJson, body) &&
        body.lifeState == EPlayerLifeState::Alive)
    {
        client.survival.Restore(body);
    }
    FNetSession::Accept(client, inventory);
    client.lifeNumber = 1000000u + static_cast<uint32_t>(client.sortie % 1000000u);
    client.life = FNetSession::FClient::ELife::Alive;
    client.hasState = false;

    FNetWriter welcome;
    Net::Write(welcome, Net::FWelcome{Net::kProtocolVersion, client.survivorId, campaignMap_, net.pvp});
    net.host.Send(peer, welcome.Bytes(), EDelivery::Reliable);
    FNetWriter spawn;
    Net::Write(spawn, Net::FCharacterSpawn{client.lifeNumber, client.inventory, client.survival.Snapshot(), feet, yaw});
    net.host.Send(peer, spawn.Bytes(), EDelivery::Reliable);
    SPDLOG_INFO("[NextDayz][net] survivor {} '{}' (player {}, sortie {}) {} ({} online)", client.survivorId, client.name,
                client.playerId, client.sortie, redeem.character.fresh ? "deployed" : "returned", net.clients.size());
}

void NextDayzGameInstance::NetSendCheckpoint()
{
    FNetSession& net = *net_;
    if (!net.master || !net.worldLoaded)
    {
        return;
    }
    net.checkpointTimer = 0.0f;
    std::vector<Master::FCheckpointCharacter> characters = std::move(net.outcomes);
    net.outcomes.clear();
    const size_t ended = characters.size();
    for (auto& [peer, client] : net.clients)
    {
        if (client.playerId == 0 || client.life != FNetSession::FClient::ELife::Alive) continue;
        Master::FCheckpointCharacter c;
        c.playerId = client.playerId;
        c.sortie = client.sortie;
        c.outcome = Master::EOutcome::Field;
        c.position = client.hasState ? client.state.presence.feet : client.spawnFeet;
        c.yaw = client.state.presentation.aimYawRadians;
        c.survivalJson = SurvivalToJson(client.survival.Snapshot());
        c.inventory = InventoryAsStacks(client.inventory);
        characters.push_back(std::move(c));
    }
    // The world and every survivor on it, in one commit.
    std::vector<Master::FCheckpointCharacter> retry(characters.begin(), characters.begin() + ended);
    const std::string body = Master::CheckpointRequest(netDsIdCvar_, netMasterKeyCvar_, ++net.checkpointSeq,
                                                       WorldToJson(loot_.Capture(), time_.Capture()), characters);
    net.master->Post("/ds/checkpoint", body, [this, retry = std::move(retry)](const NextNet::FHttpResult& result) mutable
    {
        if (result.Ok() || !net_) return;
        SPDLOG_WARN("[NextDayz][net] checkpoint failed ({}); ended sorties go with the next one", result.error);
        net_->outcomes.insert(net_->outcomes.begin(), retry.begin(), retry.end());
    }, {}, 20);
}

void NextDayzGameInstance::TickServerMaster(float dt)
{
    FNetSession& net = *net_;
    if (!net.master)
    {
        return;
    }
    net.master->Pump();

    // The map's world as the Master last committed it (once, when the map is in).
    if (sceneReady_ && !net.worldRequested)
    {
        net.worldRequested = true;
        net.master->Post("/ds/world", Master::DsRequest(netDsIdCvar_, netMasterKeyCvar_),
            [this](const NextNet::FHttpResult& result)
            {
                std::string world;
                if (result.Ok() && Master::ParseWorld(result.body, world) && !world.empty())
                {
                    FLootWorldState loot;
                    TimeSystem::FState time;
                    std::string error;
                    if (WorldFromJson(world, loot, time, error))
                    {
                        loot_.Restore(loot, GetEngine());
                        time_.Restore(time);
                        SPDLOG_INFO("[NextDayz][net] world restored from the Master ({} loot entries)", loot.entries.size());
                    }
                    else SPDLOG_ERROR("[NextDayz][net] the Master's world is unreadable: {}", error);
                }
                else if (!result.Ok())
                {
                    SPDLOG_ERROR("[NextDayz][net] could not fetch the world: {}", result.error);
                }
                net_->worldLoaded = true;
            });
    }

    net.heartbeatTimer += dt;
    if (net.heartbeatTimer >= 2.0f)
    {
        net.heartbeatTimer = 0.0f;
        std::vector<uint64_t> players;
        for (const auto& [peer, client] : net.clients)
        {
            if (client.playerId != 0) players.push_back(client.playerId);
        }
        net.master->Post("/ds/heartbeat", Master::HeartbeatRequest(netDsIdCvar_, netMasterKeyCvar_, players, net.worldLoaded),
            [this](const NextNet::FHttpResult& result)
            {
                Master::FHeartbeatReply reply;
                if (!net_ || !result.Ok() || !Master::ParseHeartbeat(result.body, reply)) return;
                FNetSession& session = *net_;
                if (reply.pvp != session.pvp)
                {
                    session.pvp = reply.pvp;
                    FNetWriter writer;
                    Net::WritePvp(writer, session.pvp);
                    session.host.Broadcast(writer.Bytes(), EDelivery::Reliable);
                    SPDLOG_INFO("[NextDayz][net] friendly fire {} (Master)", session.pvp ? "ON" : "OFF");
                }
                for (const uint64_t playerId : reply.kick)
                {
                    for (auto& [peer, client] : session.clients)
                    {
                        if (client.playerId != playerId) continue;
                        FNetWriter writer;
                        Net::Write(writer, Net::FFarewell{Net::EFarewell::Kicked, "Removed from the server by an admin"});
                        session.host.Send(peer, writer.Bytes(), EDelivery::Reliable);
                        session.host.Disconnect(peer);
                    }
                }
                if (reply.stop && !session.stopping)
                {
                    session.stopping = true;
                    SPDLOG_INFO("[NextDayz][net] the Master asked this server to stop");
                    for (auto& [peer, client] : session.clients)
                    {
                        FNetWriter writer;
                        Net::Write(writer, Net::FFarewell{Net::EFarewell::ServerStopping, "The server is shutting down"});
                        session.host.Send(peer, writer.Bytes(), EDelivery::Reliable);
                        if (client.life == FNetSession::FClient::ELife::Alive)
                        {
                            session.RecordOutcome(client, Master::EOutcome::Left);
                            client.life = FNetSession::FClient::ELife::Dead;   // nothing more counts
                        }
                        session.host.Disconnect(peer);
                    }
                    NetSendCheckpoint();
                }
            });
    }

    // Survivors holding an extraction point.
    for (auto& [peer, client] : net.clients)
    {
        if (client.extractSeconds < 0.0f) continue;
        FNetWriter writer;
        const bool alive = client.life == FNetSession::FClient::ELife::Alive && client.survival.IsAlive();
        if (!alive || glm::distance(client.state.presence.feet, client.extractPoint) > kExtractRadius)
        {
            client.extractSeconds = -1.0f;
            Net::Write(writer, Net::FExtractState{0.0f, false, true});
            net.host.Send(peer, writer.Bytes(), EDelivery::Reliable);
            continue;
        }
        client.extractSeconds -= dt;
        client.extractNotify -= dt;
        if (client.extractSeconds > 0.0f)
        {
            if (client.extractNotify <= 0.0f)
            {
                client.extractNotify = 0.5f;
                Net::Write(writer, Net::FExtractState{client.extractSeconds, false, false});
                net.host.Send(peer, writer.Bytes(), EDelivery::Reliable);
            }
            continue;
        }
        // Out: everything carried goes home; nothing is left in the world.
        client.extractSeconds = -1.0f;
        net.RecordOutcome(client, Master::EOutcome::Extracted);
        client.life = FNetSession::FClient::ELife::Dead;
        client.inventory = {};
        client.carried.clear();
        world_.RemoveSurvivor(client.survivorId);
        Net::Write(writer, Net::FExtractState{0.0f, true, false});
        net.host.Send(peer, writer.Bytes(), EDelivery::Reliable);
        FNetWriter farewell;
        Net::Write(farewell, Net::FFarewell{Net::EFarewell::Extracted, "Extracted"});
        net.host.Send(peer, farewell.Bytes(), EDelivery::Reliable);
        net.host.Disconnect(peer);
        SPDLOG_INFO("[NextDayz][net] survivor {} '{}' extracted", client.survivorId, client.name);
    }

    net.checkpointTimer += dt;
    if (!net.stopping && (net.checkpointTimer >= kCheckpointSeconds || !net.outcomes.empty()))
    {
        NetSendCheckpoint();
    }
    if (net.stopping)
    {
        net.stopSeconds += dt;
        if ((net.master->Pending() == 0 && net.outcomes.empty()) || net.stopSeconds > 15.0f)
        {
            SPDLOG_INFO("[NextDayz][net] stopped by the Master");
            GetEngine().RequestExit(0);
        }
    }
}

void NextDayzGameInstance::ApplyLinkSimulation()
{
    if (!net_)
    {
        return;
    }
    const NextNet::FLinkSimulation simulation{static_cast<uint32_t>(std::max(netSimLatencyCvar_, 0)),
                                              static_cast<uint32_t>(std::max(netSimJitterCvar_, 0)),
                                              std::clamp(netSimLossCvar_, 0.0f, 100.0f)};
    net_->host.SetLinkSimulation(simulation);
    if (simulation.Active())
    {
        SPDLOG_INFO("[NextDayz][net] link simulation: {} ms +/- {} ms, {:.1f}% unreliable loss", simulation.latencyMs,
                    simulation.jitterMs, simulation.lossPercent);
    }
}

void NextDayzGameInstance::DrawNetPanel()
{
    if (!net_ || (!netStatsCvar_ && !debugger_.Visible()))
    {
        return;
    }
    FNetSession& net = *net_;
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x - 340.0f, 60.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(320.0f, 0.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Network", nullptr, ImGuiWindowFlags_NoCollapse))
    {
        ImGui::End();
        return;
    }
    const NextNet::FLinkSimulation& simulation = net.host.LinkSimulation();
    if (netMode_ == ENetMode::Server)
    {
        ImGui::Text("Server  %s", net.status.c_str());
        ImGui::Text("Survivors %zu   infected %d", net.clients.size(), world_.zombies.ActiveCount());
        ImGui::Text("Tick avg %.2f ms   max %.2f ms", net.stats.tickMsAvgShown, net.stats.tickMsMaxShown);
        ImGui::Text("Snapshots %u/s   avg %u B", net.stats.snapshotsPerSecond, net.stats.snapshotAvgShown);
        ImGui::Text("Refused hits %u", net.rejectedHits);
        if (ImGui::BeginTable("clients", 4, ImGuiTableFlags_RowBg))
        {
            ImGui::TableSetupColumn("Survivor");
            ImGui::TableSetupColumn("RTT");
            ImGui::TableSetupColumn("Loss");
            ImGui::TableSetupColumn("Refusals");
            ImGui::TableHeadersRow();
            for (const auto& [peer, client] : net.clients)
            {
                const NextNet::FPeerStats peerStats = net.host.Stats(peer);
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("%u %s", client.survivorId, client.name.c_str());
                ImGui::TableNextColumn();
                ImGui::Text("%u ms", peerStats.roundTripMs);
                ImGui::TableNextColumn();
                ImGui::Text("%.1f%%", peerStats.packetLoss * 100.0f);
                ImGui::TableNextColumn();
                ImGui::Text("%u", client.refusals);
            }
            ImGui::EndTable();
        }
    }
    else
    {
        const NextNet::FPeerStats peerStats = net.host.Stats(net.host.ServerPeer());
        ImGui::Text("Survivor %u   %s", net.survivorId, net.status.c_str());
        ImGui::Text("RTT %u ms   loss %.1f%%", peerStats.roundTripMs, peerStats.packetLoss * 100.0f);
        ImGui::Text("Snapshots %u/s   avg %u B", net.stats.snapshotsPerSecond, net.stats.snapshotAvgShown);
        ImGui::Text("Others %zu   infected %d", net.remotes.size(), world_.zombies.ActiveCount());
        ImGui::Text("My hits %u   inventory fixes %u", net.myHits, net.fixes);
    }
    ImGui::Separator();
    ImGui::Text("In %.1f KB/s   out %.1f KB/s", net.stats.inKBps, net.stats.outKBps);
    const NextNet::FTrafficStats traffic = net.host.Traffic();
    ImGui::Text("Messages in %llu  out %llu", static_cast<unsigned long long>(traffic.messagesReceived),
                static_cast<unsigned long long>(traffic.messagesSent));
    if (simulation.Active())
    {
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "Simulating %u ms +/- %u, %.1f%% loss (%llu dropped)",
                           simulation.latencyMs, simulation.jitterMs, simulation.lossPercent,
                           static_cast<unsigned long long>(traffic.simulatedDrops));
    }
    ImGui::End();
}

// ============================================================================ Master: client lobby

bool NextDayzGameInstance::NetInLobby() const
{
    return net_ && net_->master && netMode_ == ENetMode::Client && net_->lobby != FNetSession::ELobby::Playing;
}

void NextDayzGameInstance::NetReturnToLobby(std::string message)
{
    if (!net_ || !net_->master)
    {
        return;
    }
    FNetSession& net = *net_;
    net.host.Shutdown();
    net.connected = false;
    net.survivorId = 0;
    for (auto& [id, remote] : net.remotes)
        if (remote.rig >= 0) remoteRigs_[remote.rig]->SetVisible(false);
    net.remotes.clear();
    net.pendingTakes.clear();
    net.pendingSpawn.reset();
    net.extracting = false;
    net.lootRequestedScene = 0;
    net.lootSyncedScene = 0;
    net.lastInventorySync.clear();
    world_.zombies.ApplyReplicas({});
    inventory_.Clear();
    weapons_.ResetRuntime();
    survival_.Reset();
    SyncClothingVisuals();
    net.lobbyMessage = std::move(message);
    net.lobby = FNetSession::ELobby::Waiting;
    net.master->Post("/api/profile", "{}", [this](const NextNet::FHttpResult& result)
    {
        if (!net_) return;
        if (result.Ok() && Master::ParseProfile(result.body, net_->profile))
        {
            net_->bring.assign(net_->profile.storage.size(), 0);
            net_->lobby = FNetSession::ELobby::Profile;
        }
        else
        {
            net_->lobbyMessage = result.error;
            net_->lobby = FNetSession::ELobby::Login;
        }
    }, net.token);
    SetMouseCaptured(false);
    SPDLOG_INFO("[NextDayz][net] back in the lobby: {}", net.lobbyMessage);
}

bool NextDayzGameInstance::NetRequestExtract()
{
    if (!net_ || net_->survivorId == 0)
    {
        return false;
    }
    bool atPoint = false;
    for (const FWorldAnchorHandle handle : world_.anchors.Find(EWorldAnchorType::Extraction))
    {
        const FWorldAnchor* anchor = world_.anchors.Resolve(handle);
        atPoint = atPoint || (anchor && glm::distance(anchor->worldPos, player_.Position()) <= kExtractRadius);
    }
    if (!atPoint)
    {
        Notify("No extraction point here - find a marked [H] helipad", 1);
        return false;
    }
    FNetWriter writer;
    Net::WriteExtract(writer, !net_->extracting);
    net_->host.Send(net_->host.ServerPeer(), writer.Bytes(), EDelivery::Reliable);
    net_->extracting = !net_->extracting;
    Notify(net_->extracting ? "Hold the extraction point..." : "Extraction cancelled", 0);
    return true;
}

void NextDayzGameInstance::NetAfterSceneLoaded()
{
    if (!net_ || !net_->pendingSpawn || netMode_ != ENetMode::Client)
    {
        return;
    }
    // Our survivor as the Master kept them: gear, body, place.
    Net::FCharacterSpawn spawn = std::move(*net_->pendingSpawn);
    net_->pendingSpawn.reset();
    netLife_ = spawn.life;
    netDeathDropped_ = false;
    if (inventory_.Restore(spawn.inventory))
    {
        weapons_.ResetRuntime();
        for (const FItemInstance& item : inventory_.Instances())
        {
            if (FindWeaponDef(item.defId))
            {
                weapons_.EquipInstance(0, inventory_, item.instanceId);
                break;
            }
        }
    }
    survival_.Restore(spawn.body);
    TeleportPlayer(spawn.feet - glm::vec3(0.0f, 1.0f, 0.0f));
    SyncClothingVisuals();
    net_->lastInventorySync.clear();
    net_->lobby = FNetSession::ELobby::Playing;
    net_->lobbyMessage.clear();
    paused_ = false;
    showInventory_ = false;
    SetMouseCaptured(true);
    SPDLOG_INFO("[NextDayz][net] in the field as survivor {}", net_->survivorId);
}

void NextDayzGameInstance::TickLobby(float dt)
{
    FNetSession& net = *net_;
    net.master->Pump();
    using ELobby = FNetSession::ELobby;
    const auto login = [this, &net]()
    {
        net.lobby = ELobby::Waiting;
        net.master->Post("/api/login", Master::LoginRequest(net.nameBuffer.data()), [this](const NextNet::FHttpResult& result)
        {
            if (!net_) return;
            uint64_t playerId = 0;
            if (!result.Ok() || !Master::ParseLogin(result.body, playerId, net_->token))
            {
                net_->lobbyMessage = result.error.empty() ? "The Master is unreachable" : result.error;
                net_->lobby = ELobby::Login;
                return;
            }
            NetReturnToLobby(net_->lobbyMessage);
        });
    };
    if (net.lobby == ELobby::Login && !net.autoTried && netAutoDeployCvar_ && net.nameBuffer[0] != '\0')
    {
        net.autoTried = true;
        login();
    }
    if (net.lobby == ELobby::Login && std::exchange(net.wantLogin, false))
    {
        login();
    }
    // Automation: deploy once, with everything in storage, as soon as the profile is in.
    if (net.lobby == ELobby::Profile && netAutoDeployCvar_ && sceneReady_ && !net.autoDeployed)
    {
        net.autoDeployed = true;
        for (size_t index = 0; index < net.bring.size(); ++index) net.bring[index] = net.profile.storage[index].count;
        net.wantDeploy = true;
    }
    if (net.lobby == ELobby::Deploying)
    {
        net.pollSeconds -= dt;
    }
    const bool askDeploy = (net.lobby == ELobby::Profile && std::exchange(net.wantDeploy, false)) ||
                           (net.lobby == ELobby::Deploying && net.pollSeconds <= 0.0f);
    if (askDeploy)
    {
        std::vector<Master::FBring> bring;
        for (size_t index = 0; index < net.bring.size(); ++index)
            if (net.bring[index] > 0) bring.push_back({static_cast<int>(index), net.bring[index]});
        std::string mapId = netAutoMapCvar_;
        if (!netAutoDeployCvar_)
            mapId = net.mapChoice > 0 && net.mapChoice <= static_cast<int>(net.profile.maps.size())
                ? net.profile.maps[net.mapChoice - 1].id : std::string();
        net.lobbyMessage.clear();
        net.lobby = ELobby::Waiting;
        net.master->Post("/api/deploy", Master::DeployRequest(mapId, bring), [this](const NextNet::FHttpResult& result)
        {
            if (!net_) return;
            FNetSession& session = *net_;
            if (!result.Ok() || !Master::ParseDeploy(result.body, session.deploy))
            {
                session.lobbyMessage = result.error;
                NetReturnToLobby(session.lobbyMessage);
                return;
            }
            if (session.deploy.state != "ready")
            {
                session.lobby = ELobby::Deploying;   // the server is still loading its map
                session.pollSeconds = 1.0f;
                return;
            }
            session.lobby = ELobby::Joining;
            std::string error;
            if (!session.host.Connect(session.deploy.host, session.deploy.port, error))
            {
                NetReturnToLobby(error);
                return;
            }
            session.status = fmt::format("connecting to {} ({}:{})", session.deploy.server, session.deploy.host,
                                         session.deploy.port);
            SPDLOG_INFO("[NextDayz][net] {}", session.status);
        }, net.token);
    }
}

bool NextDayzGameInstance::DrawLobby()
{
    if (!NetInLobby())
    {
        return false;
    }
    FNetSession& net = *net_;
    using ELobby = FNetSession::ELobby;
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(display.x * 0.5f, display.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(std::min(720.0f, display.x - 40.0f), std::min(600.0f, display.y - 40.0f)), ImGuiCond_Always);
    ImGui::Begin("NextDayz Online", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
    ImGui::TextDisabled("Master %s", netMasterCvar_.c_str());
    const std::string& message = net.lobbyMessage;
    if (!message.empty())
    {
        ImGui::TextWrapped("%s", message.c_str());
    }
    ImGui::Separator();
    switch (net.lobby)
    {
    case ELobby::Login:
        ImGui::InputText("Survivor name", net.nameBuffer.data(), net.nameBuffer.size());
        if (ImGui::Button("Log in", ImVec2(160, 0)) && net.nameBuffer[0] != '\0') net.wantLogin = true;
        break;
    case ELobby::Waiting:
        ImGui::TextUnformatted("Talking to the Master...");
        break;
    case ELobby::Deploying:
        ImGui::Text("Server %s is loading %s...", net.deploy.server.c_str(), net.deploy.map.c_str());
        break;
    case ELobby::Joining:
        ImGui::Text("%s", net.status.c_str());
        break;
    case ELobby::Profile:
    {
        const Master::FProfile& profile = net.profile;
        ImGui::Text("%s    sorties %llu   extracted %llu   died %llu   gold %lld", profile.name.c_str(),
                    static_cast<unsigned long long>(profile.sorties), static_cast<unsigned long long>(profile.extractions),
                    static_cast<unsigned long long>(profile.deaths), static_cast<long long>(profile.gold));
        if (!profile.lastOutcome.empty()) ImGui::TextDisabled("Last sortie: %s", profile.lastOutcome.c_str());
        ImGui::Spacing();
        if (!profile.playing.empty())
        {
            ImGui::TextWrapped("Your survivor is still on %s. Wait for it to save, then return.", profile.playing.c_str());
            if (ImGui::Button("Refresh", ImVec2(160, 0))) NetReturnToLobby({});
            break;
        }
        if (profile.character)
        {
            ImGui::TextWrapped("Your survivor is out on %s with %zu stacks of gear.", profile.character->map.c_str(),
                               profile.character->inventory.size());
            if (ImGui::Button("Return to the field", ImVec2(220, 0))) net.wantDeploy = true;
            break;
        }
        ImGui::TextUnformatted("Storage - choose what to take:");
        if (ImGui::BeginTable("storage", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerH,
                              ImVec2(0, 300)))
        {
            ImGui::TableSetupColumn("Item");
            ImGui::TableSetupColumn("Held", ImGuiTableColumnFlags_WidthFixed, 60);
            ImGui::TableSetupColumn("Take", ImGuiTableColumnFlags_WidthFixed, 140);
            ImGui::TableHeadersRow();
            for (size_t index = 0; index < profile.storage.size() && index < net.bring.size(); ++index)
            {
                const FLootStack& stack = profile.storage[index];
                const FItemDef* definition = FindItemDef(stack.itemId);
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(definition ? std::string(definition->displayName).c_str() : stack.itemId.c_str());
                ImGui::TableNextColumn();
                ImGui::Text("%d", stack.count);
                ImGui::TableNextColumn();
                ImGui::PushID(static_cast<int>(index));
                ImGui::SetNextItemWidth(130);
                ImGui::SliderInt("##take", &net.bring[index], 0, stack.count);
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        std::string maps = "Any map";
        maps.push_back('\0');
        for (const Master::FMap& map : profile.maps)
        {
            maps += map.name.empty() ? map.id : map.name;
            maps.push_back('\0');
        }
        ImGui::SetNextItemWidth(260);
        ImGui::Combo("Map", &net.mapChoice, maps.c_str());
        if (ImGui::Button("Deploy", ImVec2(160, 0))) net.wantDeploy = true;
        ImGui::SameLine();
        if (ImGui::Button("Refresh", ImVec2(120, 0))) NetReturnToLobby({});
        break;
    }
    default:
        break;
    }
    ImGui::End();
    return true;
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
bool NextDayzGameInstance::NetInLobby() const { return false; }
bool NextDayzGameInstance::DrawLobby() { return false; }
void NextDayzGameInstance::NetReturnToLobby(std::string) {}
bool NextDayzGameInstance::NetRequestExtract() { return false; }
void NextDayzGameInstance::NetAfterSceneLoaded() {}
void NextDayzGameInstance::TickLobby(float) {}
void NextDayzGameInstance::TickServerMaster(float) {}
void NextDayzGameInstance::NetSendCheckpoint() {}
void NextDayzGameInstance::NetAdmitSurvivor(uint32_t, const NextNet::FHttpResult&) {}
void NextDayzGameInstance::ApplyLinkSimulation() {}
void NextDayzGameInstance::DrawNetPanel() {}

#endif

void NextDayzGameInstance::FNetSessionDeleter::operator()(FNetSession* session) const
{
    delete session;
}
