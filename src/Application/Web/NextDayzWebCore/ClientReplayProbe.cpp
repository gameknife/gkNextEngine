// Replays a scripted map-server message stream and client frames through the
// shared FClientSession and prints the survivor after every step: inventory,
// weapon slots, body, locks and what the client sends back. The native build
// and the WASM build must print client-replay-golden.txt byte for byte, so the
// desktop and Web clients cannot drift apart without this file changing.

#include "Engine/Common/CoreMinimal.hpp"

#include "Application/Game/NextDayz/Inventory/LootTransfer.hpp"
#include "Application/Game/NextDayz/Net/ClientSession.hpp"
#include "Application/Game/NextDayz/Player/PlayerActionController.hpp"
#include "Application/Game/NextDayz/Player/PlayerController.hpp"
#include "Application/Game/NextDayz/Player/SurvivalSystem.hpp"
#include "Application/Game/NextDayz/Weapons/WeaponRuntime.hpp"

#include <algorithm>
#include <cstdio>

namespace
{
    using namespace NextDayz;
    using namespace NextDayz::Net;

    struct FReplay
    {
        FReplay()
        {
            actions.Configure(config.Action);
            weapons.Configure(config.Weapon);
            body.Reset();
            clock.Reset(config.Time, 1);
            session.survivorId = 0;
        }

        FLocalSurvivor Survivor() { return {inventory, weapons, body, actions}; }

        template <class Message>
        FClientNews Server(const Message& message)
        {
            NextNet::FNetWriter writer;
            if constexpr (std::is_same_v<Message, FServerPvp>) WritePvp(writer, message.enabled);
            else if constexpr (std::is_same_v<Message, FPlayerLeft>) WritePlayerLeft(writer, message.id);
            else Write(writer, message);
            return session.Receive(writer.Bytes(), Survivor(), clock);
        }

        FClientNews ServerBytes(std::initializer_list<uint8_t> raw)
        {
            std::vector<std::byte> bytes;
            for (const uint8_t value : raw) bytes.push_back(static_cast<std::byte>(value));
            return session.Receive(bytes, Survivor(), clock);
        }

        FItemInstanceId Find(std::string_view id) const
        {
            for (const auto& item : inventory.Instances())
                if (item.defId == id) return item.instanceId;
            return 0;
        }

        void Print(const char* step, std::string detail = {})
        {
            std::string line = step;
            if (!detail.empty()) line += " " + detail;

            line += " | life=" + std::to_string(session.life);
            const auto& state = body.Snapshot();
            line += fmt::format(" body={:.1f}/{:.1f}/{:.1f}/{}{}", state.health, state.hunger, state.hydration,
                                state.bleedingWounds, body.IsAlive() ? "" : " dead");

            line += " inv=";
            bool first = true;
            for (const auto& item : inventory.Instances())
            {
                line += fmt::format("{}{}#{}x{}@{}", first ? "" : ",", item.defId, item.instanceId, item.count, item.containerId);
                if (FindItemDef(item.defId) && IsWieldable(*FindItemDef(item.defId))) line += fmt::format("[{}]", item.loadedAmmo);
                first = false;
            }
            if (first) line += "-";

            line += " slots=";
            for (int slot = 0; slot < kWeaponSlotCount; ++slot)
            {
                const auto instance = weapons.SlotInstance(slot);
                line += fmt::format("{}{}", slot ? "," : "", instance ? std::to_string(instance) : "-");
            }
            line += fmt::format(" active={} mag={} trigger={} ads={} busy={}", weapons.ActiveSlot(), weapons.AmmoInMag(),
                                weapons.TriggerDown() ? 1 : 0, player.IsAds() ? 1 : 0, actions.IsActive() ? 1 : 0);

            std::vector<uint32_t> remoteIds;
            for (const auto& [id, remote] : session.remotes) remoteIds.push_back(id);
            std::ranges::sort(remoteIds);
            line += " remotes=";
            for (size_t index = 0; index < remoteIds.size(); ++index)
                line += fmt::format("{}{}", index ? "," : "", remoteIds[index]);
            if (remoteIds.empty()) line += "-";

            line += fmt::format(" loot={}/{} extract={:.1f}{}{} pvp={} counts={}/{}/{}/{}/{}/{} clock={:.1f}/{}",
                                session.lootSynced ? 1 : 0, session.pendingTakes.size(), session.extract.secondsLeft,
                                session.extracting ? "+" : "", session.extracted ? "!" : "", session.pvp ? 1 : 0,
                                session.snapshots, session.fixes, session.takes, session.denials, session.hits, session.kills,
                                clock.GameMinutes(), WeatherName(clock.Weather()));

            line += " out=";
            for (size_t index = 0; index < session.outbox.size(); ++index)
            {
                const auto& packet = session.outbox[index];
                line += fmt::format("{}{}:{}{}", index ? "," : "", std::to_integer<int>(packet.bytes.front()),
                                    packet.bytes.size(), packet.reliable ? "" : "u");
            }
            if (session.outbox.empty()) line += "-";
            session.outbox.clear();
            std::puts(line.c_str());
        }

        static std::string News(const FClientNews& news)
        {
            std::string detail = fmt::format("applied={}", news.applied ? 1 : 0);
            if (news.replaced) detail += " replaced";
            if (news.take) detail += fmt::format(" take={}:{}", news.request, news.take->spot.id);
            for (const auto& stack : news.taken) detail += fmt::format(" +{}x{}", stack.itemId, stack.count);
            for (const auto& stack : news.left) detail += fmt::format(" ~{}x{}", stack.itemId, stack.count);
            for (const auto id : news.removed) detail += fmt::format(" -{}", id);
            return detail;
        }

        static std::string Locks(const FFrameLocks& locks)
        {
            return fmt::format("locks={}{}{}{}", locks.action ? "A" : "", locks.traversal ? "T" : "", locks.menu ? "M" : "",
                               locks.dead ? "D" : "") + (locks.Any() ? "" : "-");
        }

        FConfig config;
        FClientSession session;
        Inventory inventory;
        WeaponRuntime weapons;
        SurvivalSystem body;
        PlayerActionController actions;
        PlayerController player;
        TimeSystem clock;
    };

    FLootReplica Spot(std::string id, glm::vec3 position, std::vector<FLootStack> contents)
    {
        return {std::move(id), ELootSource::Spot, position, 0.5f, true, std::move(contents)};
    }

    FInventoryState Kit(std::vector<FLootStack> stacks)
    {
        Inventory inventory;
        std::vector<FLootStack> left;
        AddLootToInventory(stacks, inventory, 0, EEquipSlot::None, {}, left);
        return inventory.Capture();
    }
}

int main()
{
    FReplay r;

    // Connect and spawn: every weapon goes to its usual slot, the first slot is drawn.
    r.Print("start");
    r.Print("malformed", FReplay::News(r.ServerBytes({0xff})));
    r.Print("welcome", FReplay::News(r.Server(FWelcome{kProtocolVersion, 2, "assets/scad/nextdayz/training.scad", false})));
    FSurvivalSnapshot wounded;
    wounded.health = 80.0f;
    wounded.hunger = 70.0f;
    wounded.hydration = 60.0f;
    wounded.bleedingWounds = 1;
    r.Print("spawn", FReplay::News(r.Server(FCharacterSpawn{3,
        Kit({{"backpack", 1}, {"pistol", 1, 6}, {"bandage", 2}, {"ak", 1, 20}, {"knife", 1}}), wounded,
        {12.0f, 0.0f, -4.0f}, 1.25f})));
    r.Print("stale-state", FReplay::News(r.Server(FSurvivalState{2, {}})));

    FSnapshot snapshot;
    snapshot.time = r.clock.Capture();
    snapshot.time.minutes = 600.0;
    snapshot.time.weather = EWeather::Fog;
    snapshot.time.blend = 0.75f;
    FPlayerState self, near, far;
    self.presence.id = 2;
    near.presence.id = 5;
    near.presence.feet = {3.0f, 0.0f, 4.0f};
    far.presence.id = 7;
    far.presence.feet = {40.0f, 0.0f, 0.0f};
    snapshot.survivors = {self, far, near};
    r.Print("snapshot", FReplay::News(r.Server(snapshot)));

    // Loot: changes before the full picture are already in it; a grant answers one take.
    r.session.RequestLoot();
    r.Print("ask-loot");
    r.Print("loot-early", FReplay::News(r.Server(FLootState{false, {Spot("spot.a", {1, 0, 1}, {{"bandage", 1}})}})));
    const auto bandages = Spot("spot.a", {1, 0, 1}, {{"bandage", 1}});
    const auto rifle = Spot("spot.b", {2, 0, 1}, {{"mosin", 1, 3}});
    const auto crate = Spot("spot.c", {3, 0, 1}, {{"water_bottle", 1}});
    r.Print("loot-full", FReplay::News(r.Server(FLootState{true, {bandages, rifle, crate}})));
    const uint32_t first = r.session.RequestTake(bandages);
    r.Print("take-a");
    r.Print("grant-a", FReplay::News(r.Server(FLootGrant{first, true, {{"bandage", 1}}})));
    const uint32_t second = r.session.RequestTake(rifle);
    r.Print("take-b");
    // A rifle picked up goes into the bag; the drawn weapon stays as it was.
    r.Print("grant-b", FReplay::News(r.Server(FLootGrant{second, true, {{"mosin", 1, 3}}})));
    r.Print("grant-unknown", FReplay::News(r.Server(FLootGrant{99, true, {{"ak", 1}}})));
    const uint32_t third = r.session.RequestTake(crate);
    r.Print("take-c");
    r.Print("deny-c", FReplay::News(r.Server(FLootGrant{third, false, {}})));

    // The server corrects the bag: what is still carried stays in its slot.
    Inventory corrected = r.inventory;
    corrected.TryRemove(r.Find("pistol"));
    corrected.SetLoadedAmmo(r.Find("ak"), 7);
    r.Print("fix-stale", FReplay::News(r.Server(FInventoryFix{2, "stale", corrected.Capture()})));
    r.Print("fix", FReplay::News(r.Server(FInventoryFix{3, "ammo", corrected.Capture()})));

    // Friendly fire: the first survivor along the round.
    r.Print("pvp-on", FReplay::News(r.Server(FServerPvp{true})));
    FWeaponTraceEvent trace;
    trace.origin = {3.0f, 1.2f, -2.0f};
    trace.direction = {0.0f, 0.0f, 1.0f};
    trace.endPoint = {3.0f, 1.2f, 30.0f};
    if (const auto hit = r.session.FindPvpHit(trace, *FindWeaponDef("ak")))
    {
        r.session.Queue(*hit);
        r.Print("pvp-hit", fmt::format("victim={} amount={:.2f} at={:.2f}", hit->survivorId, hit->amount, hit->point.z));
    }
    FShotEvent shot;
    shot.weaponId = "ak";
    shot.suppressed = true;
    const auto loud = FClientSession::ShotNoise(shot, {1.0f, 0.0f, 2.0f});
    shot.weaponId = "knife";
    shot.melee = true;
    const auto swing = FClientSession::ShotNoise(shot, {1.0f, 0.0f, 2.0f});
    r.Print("noise", fmt::format("shot={}:{:.1f}:{:.1f} swing={}:{:.1f}:{:.1f}", static_cast<int>(loud->type), loud->radius,
                                 loud->strength, static_cast<int>(swing->type), swing->radius, swing->strength));

    // The frame gate: aim and trigger survive a free frame and drop in a menu.
    FClientSession::PressAim(true, r.player, r.Survivor());
    FClientSession::PressAim(false, r.player, r.Survivor());
    FClientSession::PressTrigger(true, r.player, r.Survivor());
    r.Print("aim-fire");
    r.Print("frame", FReplay::Locks(FClientSession::LockFrame(r.player, r.Survivor(), false)));
    r.Print("frame-menu", FReplay::Locks(FClientSession::LockFrame(r.player, r.Survivor(), true)));
    FClientSession::PressTrigger(false, r.player, r.Survivor());

    // Busy hands lock the frame and ignore the buttons; a blow breaks the action off.
    r.actions.BeginUse(EPlayerAction::Heal, r.Find("bandage"));
    FClientSession::PressTrigger(true, r.player, r.Survivor());
    r.Print("heal", FReplay::Locks(FClientSession::LockFrame(r.player, r.Survivor(), false)));
    r.Print("struck", FReplay::News(r.Server(FSurvivorStrike{12.0f, {4.0f, 0.0f, 4.0f}})));
    r.actions.Update(1.0f / 60.0f);
    r.Print("after-blow", FReplay::Locks(FClientSession::LockFrame(r.player, r.Survivor(), false)));

    // Sync and extraction.
    r.session.SyncInventory(0.25f, true, r.Survivor());
    r.Print("sync");
    r.session.SyncInventory(0.25f, true, r.Survivor());
    r.Print("sync-same");
    r.session.RequestExtract(true);
    r.Print("extract");
    r.Print("extract-hold", FReplay::News(r.Server(FExtractState{6.5f, false, false})));
    r.Print("extract-broken", FReplay::News(r.Server(FExtractState{0.0f, false, true})));
    r.Print("left", FReplay::News(r.Server(FPlayerLeft{5})));
    FDamageNotice notice;
    notice.shooterId = 2;
    notice.damage.hitZone = EHitZone::Head;
    notice.damage.amount = 30.0f;
    notice.damage.killed = true;
    r.Print("hit-notice", FReplay::News(r.Server(notice)));

    // Death: the server says so, the frame locks, the client lets go once.
    FSurvivalSnapshot dead = r.body.Snapshot();
    dead.health = 0.0f;
    dead.lifeState = EPlayerLifeState::Dead;
    r.Print("dies", FReplay::News(r.Server(FSurvivalState{3, dead})));
    r.Print("frame-dead", FReplay::Locks(FClientSession::LockFrame(r.player, r.Survivor(), false)));
    r.Print("fix-dead", FReplay::News(r.Server(FInventoryFix{3, "late", corrected.Capture()})));
    r.Print("report", fmt::format("reported={}", r.session.ReportDeath(r.Survivor()) ? 1 : 0));
    r.Print("report-again", fmt::format("reported={}", r.session.ReportDeath(r.Survivor()) ? 1 : 0));
    r.session.SyncInventory(0.25f, true, r.Survivor());
    r.Print("sync-dead");

    // A new life from the Master: two rifles, one primary slot.
    r.Print("respawn", FReplay::News(r.Server(FCharacterSpawn{4,
        Kit({{"backpack", 1}, {"mosin", 1, 5}, {"ak", 1, 30}, {"pistol", 1, 8}}), {}, {}, 0.0f})));
    r.session.SyncInventory(0.25f, true, r.Survivor());
    r.Print("sync-respawn");
    r.Print("respawn-unarmed", FReplay::News(r.Server(FCharacterSpawn{5, Kit({{"bandage", 1}}), {}, {}, 0.0f})));
    r.Print("farewell", FReplay::News(r.Server(FFarewell{EFarewell::Extracted, "bye"})));
    r.session.ResetConnection();
    r.Print("closed");
}
