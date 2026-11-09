// ============================================================================
// NextDayzBots - load for a NextDayz map server. Each bot is a full protocol
// client without a renderer or a map: it joins, asks for the loot, reports a
// starting kit, walks between loot spots taking things and putting them back
// (the item ledger's busiest path), shoots infected it is near (lag-
// compensated hit checks), and starts a new life when it dies. Every few
// seconds it prints what the swarm sees; the last line is a RESULT summary.
//
//   NextDayzBots --host 127.0.0.1 --port 27015 --bots 16 --seconds 1800
//
// See docs/projects/nextdayz/nextdayz-multiplayer-design.md.
// ============================================================================

#include "Engine/Common/CoreMinimal.hpp"

#include "Application/Game/NextDayz/Inventory/Inventory.hpp"
#include "Application/Game/NextDayz/Net/NetProtocol.hpp"
#include "Modules/NextNet/NetBuffer.hpp"
#include "Modules/NextNet/NetHost.hpp"

#include <glm/geometric.hpp>

#include <chrono>
#include <cstdio>
#include <random>
#include <thread>
#include <unordered_map>

namespace
{
    using namespace NextDayz;
    using NextNet::EDelivery;
    using NextNet::FNetReader;
    using NextNet::FNetWriter;

    struct FOptions
    {
        std::string host = "127.0.0.1";
        uint16_t port = Net::kDefaultPort;
        int bots = 8;
        double seconds = 120.0;
        double reportSeconds = 10.0;
        uint32_t latencyMs = 0;
        float lossPercent = 0.0f;
    };

    FInventoryState StartingKit()
    {
        Inventory inventory;
        inventory.TryAdd("bandage", "Bandage", EItemKind::Consumable, 1);
        inventory.TryAdd("water_bottle_empty", "Empty Bottle", EItemKind::Misc, 1);
        return inventory.Capture();
    }

    struct FBot
    {
        int index = 0;
        NextNet::FNetHost host;
        std::mt19937 rng;
        uint32_t survivorId = 0;
        bool connected = false;
        bool lootSynced = false;
        bool placed = false;
        uint32_t life = 1;
        bool alive = true;
        glm::vec3 feet{};
        glm::vec3 target{};
        std::string targetId;
        std::unordered_map<std::string, FLootReplica> loot;
        std::vector<FZombieRuntime> zombies;
        float stateTimer = 0.0f;
        float shootTimer = 2.0f;
        float inventoryTimer = 0.0f;
        uint32_t request = 1;
        uint64_t hitSequence = 1;
        // what it has seen
        uint64_t snapshots = 0;
        uint64_t granted = 0;
        uint64_t denied = 0;
        uint64_t hits = 0;
        uint64_t shots = 0;
        uint64_t refusals = 0;
        uint64_t deaths = 0;
        uint64_t disconnects = 0;

        void Send(const FNetWriter& writer, EDelivery delivery)
        {
            host.Send(host.ServerPeer(), writer.Bytes(), delivery);
        }

        void SendInventory()
        {
            FNetWriter writer;
            Net::Write(writer, Net::FInventorySync{life, true, StartingKit()});
            Send(writer, EDelivery::Reliable);
        }

        void PickTarget()
        {
            // The nearest few available spots, one of them at random: a survivor working a town.
            std::vector<std::pair<float, const FLootReplica*>> near;
            for (const auto& [id, entry] : loot)
            {
                if (entry.available && entry.source != ELootSource::Pile)
                    near.emplace_back(glm::distance(entry.position, feet), &entry);
            }
            if (near.empty()) return;
            std::sort(near.begin(), near.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
            const size_t pick = std::uniform_int_distribution<size_t>(0, std::min<size_t>(near.size(), 6) - 1)(rng);
            target = near[pick].second->position;
            targetId = near[pick].second->id;
        }

        void OnMessage(std::span<const std::byte> payload)
        {
            FNetReader reader(payload);
            Net::EMessage message{};
            if (!Net::ReadHeader(reader, message)) return;
            switch (message)
            {
            case Net::EMessage::Welcome:
            {
                Net::FWelcome welcome;
                if (!Net::Read(reader, welcome)) return;
                survivorId = welcome.survivorId;
                FNetWriter sync;
                Net::WriteLootSync(sync);
                Send(sync, EDelivery::Reliable);
                SendInventory();
                break;
            }
            case Net::EMessage::LootState:
            {
                Net::FLootState state;
                if (!Net::Read(reader, state)) return;
                for (FLootReplica& entry : state.entries) loot[entry.id] = std::move(entry);
                if (state.full)
                {
                    lootSynced = true;
                    if (!placed && !loot.empty())
                    {
                        // Start at a random spot of the map.
                        auto it = loot.begin();
                        std::advance(it, std::uniform_int_distribution<size_t>(0, loot.size() - 1)(rng));
                        feet = it->second.position;
                        placed = true;
                    }
                }
                break;
            }
            case Net::EMessage::Snapshot:
            {
                Net::FSnapshot snapshot;
                if (!Net::Read(reader, snapshot)) return;
                ++snapshots;
                zombies = std::move(snapshot.zombies);
                break;
            }
            case Net::EMessage::LootGrant:
            {
                Net::FLootGrant grant;
                if (!Net::Read(reader, grant)) return;
                if (!grant.granted) { ++denied; break; }
                ++granted;
                // Put it straight back: the ledger must subtract exactly what it added.
                FNetWriter drop;
                Net::Write(drop, Net::FLootDrop{feet, 0.0f, grant.contents});
                Send(drop, EDelivery::Reliable);
                break;
            }
            case Net::EMessage::DamageNotice:
            {
                Net::FDamageNotice notice;
                if (Net::Read(reader, notice) && notice.shooterId == survivorId) ++hits;
                break;
            }
            case Net::EMessage::InventoryFix:
                ++refusals;
                break;
            case Net::EMessage::SurvivalState:
            {
                Net::FSurvivalState state;
                if (!Net::Read(reader, state) || state.life != life) return;
                if (alive && state.body.lifeState == EPlayerLifeState::Dead)
                {
                    // Dead: report it, then start the next life with a fresh kit.
                    alive = false;
                    ++deaths;
                    FNetWriter died;
                    Net::Write(died, Net::FInventorySync{life, false, {}});
                    Send(died, EDelivery::Reliable);
                    ++life;
                    alive = true;
                    SendInventory();
                }
                break;
            }
            default:
                break;
            }
        }

        void Tick(float dt)
        {
            if (!connected || survivorId == 0 || !placed) return;
            // Walk toward the target spot at a run; take what is there on arrival.
            if (targetId.empty()) PickTarget();
            glm::vec3 to = target - feet;
            const float distance = glm::length(to);
            glm::vec3 forward(0.0f, 0.0f, 1.0f);
            if (distance > 0.05f) forward = to / distance;
            const float step = 4.5f * dt;
            if (distance <= 1.5f && !targetId.empty())
            {
                if (const auto it = loot.find(targetId); it != loot.end() && it->second.available)
                {
                    FNetWriter take;
                    Net::Write(take, Net::FLootTake{request++, targetId});
                    Send(take, EDelivery::Reliable);
                }
                targetId.clear();
            }
            else
            {
                feet += forward * std::min(step, distance);
            }

            stateTimer += dt;
            if (stateTimer >= 1.0f / 30.0f)
            {
                stateTimer = 0.0f;
                Net::FPlayerState state;
                state.presence = {survivorId, feet, feet + glm::vec3(0.0f, 1.6f, 0.0f), forward, alive, 1.0f};
                state.presentation.locomotion.gait = EPlayerGait::Run;
                state.presentation.locomotion.worldVelocity = forward * 4.5f;
                state.presentation.locomotion.horizontalSpeed = 4.5f;
                state.presentation.locomotion.onGround = true;
                state.presentation.aimYawRadians = std::atan2(forward.x, forward.z);
                state.presentation.aimDirection = forward;
                FNetWriter writer;
                Net::Write(writer, state);
                Send(writer, EDelivery::Unreliable);
            }

            // Shoot an infected that is close, as a real survivor would see it.
            shootTimer -= dt;
            if (shootTimer <= 0.0f)
            {
                shootTimer = 1.5f;
                const FZombieRuntime* nearest = nullptr;
                float best = 25.0f;
                for (const FZombieRuntime& zombie : zombies)
                {
                    const float d = glm::distance(zombie.position, feet);
                    if (zombie.state != EZombieState::Dead && d < best) { best = d; nearest = &zombie; }
                }
                if (nearest)
                {
                    ++shots;
                    FWeaponHitEvent hit;
                    hit.sequence = hitSequence++;
                    hit.weaponId = "ak";
                    hit.origin = feet + glm::vec3(0.0f, 1.6f, 0.0f);
                    hit.hitPoint = nearest->position + glm::vec3(0.0f, 1.3f, 0.0f);
                    hit.direction = glm::normalize(hit.hitPoint - hit.origin);
                    hit.zombie = nearest->handle;
                    hit.zone = EHitZone::Torso;
                    FNetWriter writer;
                    Net::Write(writer, hit);
                    Send(writer, EDelivery::Reliable);
                    FNetWriter noise;
                    Net::Write(noise, Net::FNoise{feet, 110.0f, 1.0f, ENoiseType::Rifle});
                    Send(noise, EDelivery::Reliable);
                }
            }
        }
    };

    bool ParseArgs(int argc, char** argv, FOptions& options)
    {
        for (int i = 1; i < argc; ++i)
        {
            const std::string_view arg = argv[i];
            const auto value = [&]() -> const char* { return i + 1 < argc ? argv[++i] : ""; };
            if (arg == "--host") options.host = value();
            else if (arg == "--port") options.port = static_cast<uint16_t>(std::atoi(value()));
            else if (arg == "--bots") options.bots = std::max(1, std::atoi(value()));
            else if (arg == "--seconds") options.seconds = std::atof(value());
            else if (arg == "--report") options.reportSeconds = std::max(1.0, std::atof(value()));
            else if (arg == "--latency") options.latencyMs = static_cast<uint32_t>(std::atoi(value()));
            else if (arg == "--loss") options.lossPercent = static_cast<float>(std::atof(value()));
            else
            {
                std::printf("NextDayzBots --host H --port P --bots N --seconds S [--report S] [--latency MS] [--loss PCT]\n");
                return false;
            }
        }
        return true;
    }
}

int main(int argc, char** argv)
{
    FOptions options;
    if (!ParseArgs(argc, argv, options)) return 2;
    std::vector<std::unique_ptr<FBot>> bots;
    for (int index = 0; index < options.bots; ++index)
    {
        auto bot = std::make_unique<FBot>();
        bot->index = index;
        bot->rng.seed(0x9e3779b9u + static_cast<uint32_t>(index) * 7919u);
        bot->host.SetLinkSimulation({options.latencyMs, options.latencyMs / 5, options.lossPercent});
        std::string error;
        if (!bot->host.Connect(options.host, options.port, error))
        {
            std::printf("bot %d: %s\n", index, error.c_str());
            return 1;
        }
        bots.push_back(std::move(bot));
    }
    std::printf("NextDayzBots: %d bots -> %s:%u for %.0f s\n", options.bots, options.host.c_str(), options.port, options.seconds);
    std::fflush(stdout);

    using Clock = std::chrono::steady_clock;
    const auto start = Clock::now();
    auto last = start;
    auto nextReport = start + std::chrono::duration<double>(options.reportSeconds);
    NextNet::FTrafficStats lastTraffic{};
    std::vector<NextNet::FNetEvent> events;
    uint32_t maxRtt = 0;
    const auto totals = [&](auto field)
    {
        uint64_t sum = 0;
        for (const auto& bot : bots) sum += field(*bot);
        return sum;
    };
    while (Clock::now() - start < std::chrono::duration<double>(options.seconds))
    {
        const auto now = Clock::now();
        const float dt = std::chrono::duration<float>(now - last).count();
        last = now;
        for (auto& bot : bots)
        {
            events.clear();
            bot->host.Poll(events);
            for (NextNet::FNetEvent& event : events)
            {
                if (event.type == NextNet::FNetEvent::EType::Connected)
                {
                    bot->connected = true;
                    Net::FHello hello;
                    hello.name = fmt::format("bot{}", bot->index);
                    FNetWriter writer;
                    Net::Write(writer, hello);
                    bot->Send(writer, EDelivery::Reliable);
                }
                else if (event.type == NextNet::FNetEvent::EType::Disconnected)
                {
                    bot->connected = false;
                    bot->survivorId = 0;
                    ++bot->disconnects;
                }
                else
                {
                    bot->OnMessage(event.payload);
                }
            }
            bot->Tick(dt);
        }
        if (now >= nextReport)
        {
            nextReport = now + std::chrono::duration<double>(options.reportSeconds);
            NextNet::FTrafficStats traffic{};
            uint32_t rttSum = 0;
            int online = 0;
            for (const auto& bot : bots)
            {
                const NextNet::FTrafficStats t = bot->host.Traffic();
                traffic.bytesReceived += t.bytesReceived;
                traffic.bytesSent += t.bytesSent;
                if (bot->survivorId != 0)
                {
                    ++online;
                    const uint32_t rtt = bot->host.Stats(bot->host.ServerPeer()).roundTripMs;
                    rttSum += rtt;
                    maxRtt = std::max(maxRtt, rtt);
                }
            }
            const double window = options.reportSeconds;
            std::printf("[%5.0fs] online %d/%d  in %.1f KB/s  out %.1f KB/s  rtt avg %u ms  snapshots %llu  "
                        "takes %llu/%llu  hits %llu/%llu  deaths %llu  refusals %llu\n",
                        std::chrono::duration<double>(now - start).count(), online, options.bots,
                        (traffic.bytesReceived - lastTraffic.bytesReceived) / 1024.0 / window,
                        (traffic.bytesSent - lastTraffic.bytesSent) / 1024.0 / window, online ? rttSum / online : 0,
                        static_cast<unsigned long long>(totals([](const FBot& b) { return b.snapshots; })),
                        static_cast<unsigned long long>(totals([](const FBot& b) { return b.granted; })),
                        static_cast<unsigned long long>(totals([](const FBot& b) { return b.granted + b.denied; })),
                        static_cast<unsigned long long>(totals([](const FBot& b) { return b.hits; })),
                        static_cast<unsigned long long>(totals([](const FBot& b) { return b.shots; })),
                        static_cast<unsigned long long>(totals([](const FBot& b) { return b.deaths; })),
                        static_cast<unsigned long long>(totals([](const FBot& b) { return b.refusals; })));
            std::fflush(stdout);
            lastTraffic = traffic;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    int online = 0;
    for (const auto& bot : bots) online += bot->survivorId != 0 ? 1 : 0;
    const uint64_t refusals = totals([](const FBot& b) { return b.refusals; });
    const uint64_t disconnects = totals([](const FBot& b) { return b.disconnects; });
    const bool ok = online == options.bots && refusals == 0 && disconnects == 0;
    std::printf("RESULT %s online=%d/%d refusals=%llu disconnects=%llu deaths=%llu takes=%llu hits=%llu/%llu maxRtt=%u\n",
                ok ? "ok" : "FAIL", online, options.bots, static_cast<unsigned long long>(refusals),
                static_cast<unsigned long long>(disconnects),
                static_cast<unsigned long long>(totals([](const FBot& b) { return b.deaths; })),
                static_cast<unsigned long long>(totals([](const FBot& b) { return b.granted; })),
                static_cast<unsigned long long>(totals([](const FBot& b) { return b.hits; })),
                static_cast<unsigned long long>(totals([](const FBot& b) { return b.shots; })), maxRtt);
    for (auto& bot : bots) bot->host.Shutdown();
    return ok ? 0 : 1;
}
