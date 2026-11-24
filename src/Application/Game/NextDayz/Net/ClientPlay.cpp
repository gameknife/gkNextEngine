// ============================================================================
// ClientPlay.cpp - What a NextDayz client does with the map server's messages
// and in what order a client frame gates the player. Desktop and Web both call
// this; they keep only presentation (scene, HUD, audio) and their transport.
// Native and WASM must agree item by item: ClientReplayProbe checks it.
// ============================================================================

#include "Engine/Common/CoreMinimal.hpp"
#include "ClientSession.hpp"

#include "Application/Game/NextDayz/Inventory/LootTransfer.hpp"
#include "Application/Game/NextDayz/Player/PlayerActionController.hpp"
#include "Application/Game/NextDayz/Player/PlayerController.hpp"
#include "Application/Game/NextDayz/Player/SurvivalSystem.hpp"
#include "Application/Game/NextDayz/Weapons/WeaponRuntime.hpp"

#include <limits>

namespace NextDayz::Net
{
    namespace
    {
        // Friendly fire is softer than the infected's flesh: survivors carry 100 HP.
        constexpr float kPvpDamageScale = 0.6f;
        constexpr float kSurvivorRadius = 0.32f;
        constexpr float kSurvivorHeight = 1.75f;

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

    FClientNews FClientSession::Receive(std::span<const std::byte> bytes, FLocalSurvivor survivor, TimeSystem& clock)
    {
        FClientNews news;
        news.packet = Decode(bytes);
        Apply(news, survivor, clock);
        return news;
    }

    void FClientSession::Apply(FClientNews& news, FLocalSurvivor survivor, TimeSystem& clock)
    {
        auto& packet = news.packet;
        if (const auto* welcome = std::get_if<FWelcome>(&packet))
        {
            news.applied = ApplyWelcome(*welcome);
        }
        else if (const auto* spawn = std::get_if<FCharacterSpawn>(&packet))
        {
            // The survivor the Master sent: its gear, body and life number. A kit
            // the client cannot read leaves it empty-handed; the server corrects it.
            if (survivor.inventory.Restore(spawn->inventory)) EquipCarried(survivor.weapons, survivor.inventory);
            else
            {
                survivor.inventory.Clear();
                survivor.weapons.ResetRuntime();
            }
            survivor.body.Restore(spawn->body);
            life = spawn->life;
            deathReported = false;
            lastInventorySync.clear();
            news.applied = news.replaced = true;
        }
        else if (const auto* state = std::get_if<FSurvivalState>(&packet))
        {
            if (state->life != life) return;
            survivor.body.Restore(state->body);
            news.applied = true;
        }
        else if (const auto* fix = std::get_if<FInventoryFix>(&packet))
        {
            // A dead survivor has let go of its gear; the server's view of it no longer matters.
            if (fix->life != life || !survivor.body.IsAlive()) return;
            ++fixes;
            lastFix = fix->reason;
            if (survivor.inventory.Restore(fix->inventory))
            {
                KeepCarried(survivor.weapons, survivor.inventory);
                news.replaced = true;
            }
            news.applied = true;
        }
        else if (const auto* loot = std::get_if<FLootState>(&packet))
        {
            // Changes before the full picture are already part of it.
            if (loot->full) lootSynced = true;
            news.applied = lootSynced;
        }
        else if (const auto* grant = std::get_if<FLootGrant>(&packet))
        {
            const auto pending = pendingTakes.find(grant->request);
            if (pending == pendingTakes.end()) return;
            news.request = grant->request;
            news.take = std::move(pending->second);
            pendingTakes.erase(pending);
            news.applied = true;
            if (!grant->granted)
            {
                ++denials;
                return;
            }
            const auto& [spot, destination] = *news.take;
            news.taken = AddLootToInventory(grant->contents, survivor.inventory, destination.container,
                                            destination.slot, destination.placement, news.left);
            if (!news.taken.empty()) ++takes;
            // It is ours either way; what does not fit goes back on the ground where it lay.
            if (!news.left.empty()) Queue(FLootDrop{spot.position, spot.yaw, news.left});
        }
        else if (auto* snapshot = std::get_if<FSnapshot>(&packet))
        {
            ++snapshots;
            ReconcileClock(clock, snapshot->time, snapshots == 1);
            news.removed = SynchronizeRemotes(snapshot->survivors);
            news.applied = true;
        }
        else if (const auto* left = std::get_if<FPlayerLeft>(&packet))
        {
            if (remotes.erase(left->id)) news.removed.push_back(left->id);
            news.applied = true;
        }
        else if (const auto* notice = std::get_if<FDamageNotice>(&packet))
        {
            if (notice->shooterId == survivorId)
            {
                ++hits;
                if (notice->damage.killed) ++kills;
            }
            news.applied = true;
        }
        else if (std::holds_alternative<FSurvivorStrike>(packet) || std::holds_alternative<FPvpHit>(packet))
        {
            // The server has already hurt the body it keeps; a blow breaks off what the hands were doing.
            survivor.actions.RequestCancel();
            news.applied = true;
        }
        else if (const auto* extraction = std::get_if<FExtractState>(&packet))
        {
            extract = *extraction;
            extracting = !extraction->done && !extraction->broken;
            extracted = extracted || extraction->done;
            news.applied = true;
        }
        else if (const auto* reject = std::get_if<FServerReject>(&packet))
        {
            rejected = reject->reason;
            news.applied = true;
        }
        else if (const auto* update = std::get_if<FServerPvp>(&packet))
        {
            pvp = update->enabled;
            news.applied = true;
        }
        else if (std::holds_alternative<FFarewell>(packet) || std::holds_alternative<FNoise>(packet))
        {
            news.applied = true;
        }
    }

    void FClientSession::BeginLife(uint32_t number)
    {
        life = number;
        deathReported = false;
        lastInventorySync.clear();
    }

    FFrameLocks FClientSession::LockFrame(PlayerController& player, FLocalSurvivor survivor, bool menuOpen)
    {
        const FFrameLocks locks{survivor.actions.IsActive(), player.IsTraversing(), menuOpen, !survivor.body.IsAlive()};
        WeaponRuntime& weapons = survivor.weapons;
        player.SetMovementLocked(locks.Any());
        player.SetMeleeEquipped(weapons.ActiveIsMelee());
        // Traversal keeps the weapon: it is tucked away while the hands are on the obstacle.
        weapons.SetPresentationSuppressed(locks.action || locks.dead);
        if (!weapons.HasActiveWeapon() && player.IsAds()) player.CancelAiming();
        if (locks.Any())
        {
            player.CancelAiming();
            weapons.SetTriggerDown(false);
            weapons.CancelMelee();
        }
        player.SetSprintBlocked((weapons.TriggerDown() && weapons.HasActiveWeapon()) || weapons.IsSwinging() ||
                                weapons.MeleeInputPending() || survivor.body.IsWeak());
        return locks;
    }

    void FClientSession::PressTrigger(bool down, PlayerController& player, FLocalSurvivor survivor)
    {
        if (survivor.actions.IsActive()) return;
        if (down) player.SetFreeLook(false);
        survivor.weapons.SetTriggerDown(down);
    }

    void FClientSession::PressAim(bool down, PlayerController& player, FLocalSurvivor survivor)
    {
        if (survivor.actions.IsActive()) return;
        const WeaponRuntime& weapons = survivor.weapons;
        if (!down) player.SetAimButton(false, false);
        else if (player.IsTraversing() || weapons.IsReloading() || weapons.IsSwitching()) player.CancelAiming();
        else player.SetAimButton(true, weapons.HasActiveWeapon() && !weapons.ActiveIsMelee());
    }

    void FClientSession::EquipCarried(WeaponRuntime& weapons, const Inventory& inventory)
    {
        weapons.ResetRuntime();
        for (const FItemInstance& item : inventory.Instances())
        {
            const FItemDef* definition = FindItemDef(item.defId);
            if (!definition || !IsWieldable(*definition)) continue;
            const int slot = PreferredWeaponSlot(*definition);
            if (!weapons.SlotInstance(slot)) weapons.EquipInstance(slot, inventory, item.instanceId);
        }
        int drawn = 0;
        for (int slot = kWeaponSlotCount - 1; slot >= 0; --slot)
            if (weapons.SlotInstance(slot)) drawn = slot;
        weapons.RestoreSelection(drawn, -1);
    }

    void FClientSession::KeepCarried(WeaponRuntime& weapons, const Inventory& inventory)
    {
        const int active = weapons.ActiveSlot();
        const int previous = weapons.PreviousSlot();
        std::array<FItemInstanceId, kWeaponSlotCount> held{};
        for (int slot = 0; slot < kWeaponSlotCount; ++slot) held[slot] = weapons.SlotInstance(slot);
        weapons.ResetRuntime();
        // Re-equipping reads the corrected magazine and attachments from the item.
        for (int slot = 0; slot < kWeaponSlotCount; ++slot)
            if (held[slot] && inventory.FindInstance(held[slot])) weapons.EquipInstance(slot, inventory, held[slot]);
        weapons.RestoreSelection(active, previous);
    }

    std::optional<FNoise> FClientSession::ShotNoise(const FShotEvent& shot, const glm::vec3& position)
    {
        const FWeaponDef* weapon = FindWeaponDef(shot.weaponId);
        if (!weapon) return std::nullopt;
        if (shot.melee) return FNoise{position, weapon->noiseRadius, 0.5f, ENoiseType::Melee};
        const ENoiseType type = weapon->family == EWeaponFamily::Shotgun ? ENoiseType::Shotgun
            : weapon->family == EWeaponFamily::Sidearm || weapon->family == EWeaponFamily::Smg ? ENoiseType::Pistol
            : ENoiseType::Rifle;
        return FNoise{position, weapon->noiseRadius * (shot.suppressed ? 0.25f : 1.0f), 1.0f, type};
    }

    void FClientSession::SyncInventory(float dt, bool ready, FLocalSurvivor survivor)
    {
        if (!ShouldSyncInventory(dt, ready && survivor.body.IsAlive() && pendingTakes.empty())) return;
        survivor.weapons.CopyMagazinesTo(survivor.inventory);
        NextNet::FNetWriter writer;
        if (WriteInventoryIfChanged(writer, life, survivor.inventory.Capture())) QueueBytes(writer, true);
    }

    void FClientSession::RequestLoot()
    {
        lootSynced = false;
        pendingTakes.clear();
        NextNet::FNetWriter writer;
        WriteLootSync(writer);
        QueueBytes(writer, true);
    }

    uint32_t FClientSession::RequestTake(const FLootReplica& spot, const FLootDestination& destination)
    {
        const uint32_t request = nextTake++;
        pendingTakes[request] = {spot, destination};
        Queue(FLootTake{request, spot.id});
        return request;
    }

    void FClientSession::RequestExtract(bool begin)
    {
        NextNet::FNetWriter writer;
        WriteExtract(writer, begin);
        QueueBytes(writer, true);
        extracting = begin;
    }

    bool FClientSession::ReportDeath(FLocalSurvivor survivor)
    {
        if (deathReported) return false;
        deathReported = true;
        if (survivorId == 0) return false;
        // The server drops the body from its own ledger; here we only let go of it.
        Queue(FInventorySync{life, false, {}});
        lastInventorySync.clear();
        survivor.inventory.Clear();
        survivor.weapons.ResetRuntime();
        return true;
    }

    std::optional<FPvpHit> FClientSession::FindPvpHit(const FWeaponTraceEvent& trace, const FWeaponDef& weapon) const
    {
        if (!pvp || survivorId == 0) return std::nullopt;
        // The round stops at whatever it hit first: the world or an infected.
        const float reach = glm::distance(trace.origin, trace.endPoint);
        const glm::vec3 end = trace.origin + trace.direction * reach;
        uint32_t victim = 0;
        float bestAlong = std::numeric_limits<float>::max();
        glm::vec3 point{};
        for (const auto& [id, remote] : remotes)
        {
            if (!remote.alive) continue;
            float along = 0.0f;
            const glm::vec3 low = remote.feet + glm::vec3(0.0f, kSurvivorRadius, 0.0f);
            const glm::vec3 high = remote.feet + glm::vec3(0.0f, kSurvivorHeight - kSurvivorRadius, 0.0f);
            if (SegmentDistance(trace.origin, end, low, high, along) <= kSurvivorRadius &&
                (along < bestAlong || (along == bestAlong && id < victim)))
            {
                bestAlong = along;
                victim = id;
                point = trace.origin + (end - trace.origin) * along;
            }
        }
        if (victim == 0) return std::nullopt;
        const float height = point.y - remotes.at(victim).feet.y;
        const float zone = height > 1.45f ? 2.0f : height < 0.9f ? 0.6f : 1.0f;
        return FPvpHit{victim, weapon.baseDamage * zone * kPvpDamageScale, trace.origin, point};
    }

    void FClientSession::QueueBytes(const NextNet::FNetWriter& writer, bool reliable)
    {
        const auto bytes = writer.Bytes();
        outbox.push_back({reliable, {bytes.begin(), bytes.end()}});
    }
}
