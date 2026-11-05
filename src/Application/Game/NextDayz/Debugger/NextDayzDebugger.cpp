#include "Engine/Common/CoreMinimal.hpp"
#include "Application/Game/NextDayz/UI/Localization.hpp"

#include "NextDayzDebugger.hpp"

#include <cctype>
#include <fstream>

#include <fmt/format.h>
#include <imgui.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "Engine/Utilities/FileHelper.hpp"
#include "Gameplay/AI/NavGrid.h"

#include "Application/Game/NextDayz/Audio/NextDayzAudio.hpp"
#include "Application/Game/NextDayz/Combat/CombatEffects.hpp"
#include "Application/Game/NextDayz/Combat/CombatSystem.hpp"
#include "Application/Game/NextDayz/Combat/NoiseSystem.hpp"
#include "Application/Game/NextDayz/Data/ItemDefs.hpp"
#include "Application/Game/NextDayz/Inventory/LootSystem.hpp"
#include "Application/Game/NextDayz/NextDayzConfig.hpp"
#include "Application/Game/NextDayz/Player/PlayerActionController.hpp"
#include "Application/Game/NextDayz/Player/PlayerController.hpp"
#include "Application/Game/NextDayz/Player/PlayerRigVisual.hpp"
#include "Application/Game/NextDayz/Player/SurvivalSystem.hpp"
#include "Application/Game/NextDayz/Weapons/WeaponSystem.hpp"
#include "Application/Game/NextDayz/World/WorldAnchorRegistry.hpp"
#include "Application/Game/NextDayz/World/ZombieSpawnDirector.hpp"
#include "Application/Game/NextDayz/Zombies/ZombieRigVisual.hpp"

namespace NextDayz::Debug
{
    namespace
    {
        constexpr float kSampleSeconds = 0.25f;
        constexpr float kWarningSeconds = 0.5f;
        constexpr float kTraceSeconds = 4.0f;
        constexpr size_t kMaxTraces = 64;
        constexpr const char* kSnapshotPath = "debug/nextdayz_snapshot.json";

        constexpr std::array<FLayerInfo, 11> kLayers = {{
            {LayerInfected, "infected", "Infected AI"},
            {LayerPerception, "perception", "Perception"},
            {LayerHitboxes, "hitboxes", "Hitboxes"},
            {LayerTraces, "traces", "Weapon traces"},
            {LayerNoise, "noise", "Noise"},
            {LayerSpawns, "spawns", "Spawn points"},
            {LayerLoot, "loot", "Loot"},
            {LayerLootZones, "zones", "Loot zones"},
            {LayerNav, "nav", "Nav grid"},
            {LayerAnchors, "anchors", "Anchors/POIs"},
            {LayerPlayer, "player", "Player"},
        }};

        nlohmann::ordered_json Vec(const glm::vec3& v)
        {
            return nlohmann::ordered_json::array({std::round(v.x * 100.0f) / 100.0f, std::round(v.y * 100.0f) / 100.0f,
                                                  std::round(v.z * 100.0f) / 100.0f});
        }

        const char* LootStateName(FLootEntry::EState state)
        {
            switch (state)
            {
            case FLootEntry::EState::Available: return "Available";
            case FLootEntry::EState::Reserved: return "Reserved";
            case FLootEntry::EState::Cooldown: return "Cooldown";
            case FLootEntry::EState::Empty: return "Empty";
            }
            return "?";
        }

        const char* LootSourceName(ELootSource source)
        {
            switch (source)
            {
            case ELootSource::Authored: return "authored";
            case ELootSource::Spot: return "spot";
            case ELootSource::Pile: return "pile";
            }
            return "?";
        }

        float Horizontal(const glm::vec3& a, const glm::vec3& b)
        {
            return glm::length(glm::vec2(a.x - b.x, a.z - b.z));
        }
    }

    std::span<const FLayerInfo> AllLayers()
    {
        return kLayers;
    }

    std::optional<EExpeditionAction> ParseExpeditionAction(std::string_view name)
    {
        if (name == "return") return EExpeditionAction::ReturnToSafehouse;
        if (name == "save") return EExpeditionAction::Save;
        if (name == "reload") return EExpeditionAction::ReloadMap;
        if (name == "spawn") return EExpeditionAction::SafeSpawn;
        if (name == "alpha") return EExpeditionAction::ExtractionAlpha;
        if (name == "bravo") return EExpeditionAction::ExtractionBravo;
        if (name == "rescue-kit") return EExpeditionAction::RescueSupplies;
        return std::nullopt;
    }

    const char* TabName(ETab tab)
    {
        switch (tab)
        {
        case ETab::Overview: return "Overview";
        case ETab::Player: return "Player";
        case ETab::Survival: return "Survival";
        case ETab::Inventory: return "Inventory";
        case ETab::Weapons: return "Weapons";
        case ETab::Infected: return "Infected";
        case ETab::Spawns: return "Spawns";
        case ETab::Loot: return "Loot";
        case ETab::World: return "World";
        case ETab::Combat: return "Combat";
        case ETab::Log: return "Log";
        case ETab::Count: break;
        }
        return "?";
    }

    std::optional<ETab> ParseTab(std::string_view name)
    {
        for (size_t index = 0; index < static_cast<size_t>(ETab::Count); ++index)
        {
            const ETab tab = static_cast<ETab>(index);
            const std::string_view candidate = TabName(tab);
            if (candidate.size() == name.size() &&
                std::equal(candidate.begin(), candidate.end(), name.begin(),
                           [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) ==
                                                       std::tolower(static_cast<unsigned char>(b)); }))
            {
                return tab;
            }
        }
        return std::nullopt;
    }

    // ------------------------------------------------------------------ setup

    void NextDayzDebugger::Bind(const FSystems& systems, FCommands commands)
    {
        systems_ = systems;
        commands_ = std::move(commands);
        channelFilter_.fill(true);
    }

    void NextDayzDebugger::OnSessionStart(const char* reason)
    {
        zombieWatch_.clear();
        lootWatch_.clear();
        traces_.clear();
        warnings_.clear();
        selection_ = {};
        navProbePath_.reset();
        lastNoiseSequence_ = 0;
        lastHour_ = -1;
        lastWounds_ = 0;
        lastLife_ = EPlayerLifeState::Alive;
        lastWeak_ = false;
        lastTraversal_ = EPlayerTraversalAction::None;
        lastWeaponAction_ = EWeaponPresentationAction::None;
        lastWeaponId_.clear();
        pathSaturatedUpdates_ = 0;
        watchPrimed_ = false;
        history_ = {};
        Log(EChannel::System, 1, NextDayz::L10n::Message("---- session start: {} ----", reason));
    }

    void NextDayzDebugger::SetVisible(bool visible)
    {
        visible_ = visible;
        if (!visible_ && cursorMode_)
        {
            SetCursorMode(false);
        }
    }

    void NextDayzDebugger::SetCursorMode(bool enabled)
    {
        if (enabled && !visible_)
        {
            visible_ = true;
        }
        if (cursorMode_ == enabled)
        {
            return;
        }
        cursorMode_ = enabled;
        if (commands_.setCursorFree)
        {
            commands_.setCursorFree(enabled);
        }
    }

    std::string NextDayzDebugger::LayerString() const
    {
        std::string result;
        for (const FLayerInfo& info : kLayers)
        {
            if (LayerOn(info.layer))
            {
                result += result.empty() ? "" : ",";
                result += info.id;
            }
        }
        return result.empty() ? "none" : result;
    }

    void NextDayzDebugger::ApplyLayerString(std::string_view text)
    {
        // Tokens separated by commas/spaces; a leading +/- edits, otherwise the
        // first plain token starts the set over.
        bool replaced = false;
        size_t start = 0;
        while (start < text.size())
        {
            size_t end = text.find_first_of(", ", start);
            end = end == std::string_view::npos ? text.size() : end;
            std::string_view token = text.substr(start, end - start);
            start = end + 1;
            if (token.empty())
            {
                continue;
            }
            char op = '=';
            if (token.front() == '+' || token.front() == '-')
            {
                op = token.front();
                token.remove_prefix(1);
            }
            uint32_t bits = 0;
            if (token == "all")
            {
                for (const FLayerInfo& info : kLayers) bits |= info.layer;
            }
            else if (token == "none")
            {
                bits = 0;
            }
            else
            {
                const auto found = std::find_if(kLayers.begin(), kLayers.end(),
                                                [token](const FLayerInfo& info) { return token == info.id; });
                if (found == kLayers.end())
                {
                    SPDLOG_WARN("[NextDayz] unknown debug layer '{}'", token);
                    continue;
                }
                bits = found->layer;
            }
            if (op == '+')
            {
                layers_ |= bits;
            }
            else if (op == '-')
            {
                layers_ &= ~bits;
            }
            else
            {
                layers_ = replaced ? (layers_ | bits) : bits;
                replaced = true;
            }
        }
    }

    // ------------------------------------------------------------------ frame

    bool NextDayzDebugger::BeginTick(float& deltaSeconds)
    {
        if (knobs_.frozen)
        {
            if (knobs_.stepFrames <= 0)
            {
                return false;
            }
            --knobs_.stepFrames;
            deltaSeconds = 1.0f / 60.0f;
            return true;
        }
        knobs_.stepFrames = 0;
        deltaSeconds *= std::max(knobs_.timeScale, 0.0f);
        return true;
    }

    void NextDayzDebugger::EndTick(float deltaSeconds)
    {
        ScopedTimer timer(profiler_, ETimer::Debugger);
        ++frame_;
        clock_ += deltaSeconds;
        log_.SetClock(frame_, clock_);
        for (FTraceRecord& trace : traces_)
        {
            trace.remainingSeconds -= deltaSeconds;
        }
        std::erase_if(traces_, [](const FTraceRecord& trace) { return trace.remainingSeconds <= 0.0f; });

        if (systems_.zombies)
        {
            pathSolvesSinceSample_ += systems_.zombies->PathSolvesLastUpdate();
            pathSaturatedUpdates_ = systems_.zombies->PathDeferredLastUpdate() > 0 ? pathSaturatedUpdates_ + 1 : 0;
        }
        DiffSystems();
        SampleHistory(deltaSeconds);
        warningAccumulator_ += deltaSeconds;
        if (warningAccumulator_ >= kWarningSeconds || warnings_.empty())
        {
            warningAccumulator_ = 0.0f;
            RefreshWarnings();
        }
    }

    // ------------------------------------------------------------------ hooks

    void NextDayzDebugger::Log(EChannel channel, int severity, L10n::FText text, const glm::vec3* where)
    {
        log_.Push(channel, severity, std::move(text), where);
    }

    void NextDayzDebugger::RecordTrace(const FWeaponTraceEvent& trace)
    {
        traces_.push_back({trace, kTraceSeconds});
        if (traces_.size() > kMaxTraces)
        {
            traces_.erase(traces_.begin(), traces_.begin() + static_cast<std::ptrdiff_t>(traces_.size() - kMaxTraces));
        }
    }

    void NextDayzDebugger::RecordDamage(const FDamageEvent& damage)
    {
        std::string who = fmt::format("Z{}:{}", damage.target.index, damage.target.generation);
        float healthLeft = 0.0f;
        if (systems_.zombies)
        {
            if (const FZombieRuntime* zombie = systems_.zombies->Resolve(damage.target))
            {
                who = ZombieTag(*zombie);
                healthLeft = zombie->health;
            }
        }
        L10n::FText flags;
        if (damage.killed) flags += " KILLED";
        if (damage.staggered) flags += " staggered";
        if (damage.crippled) flags += " crippled";
        Log(EChannel::Combat, damage.killed ? 1 : 0,
            NextDayz::L10n::Message("{} hit {} for {:.0f} (impact {:.0f}) hp {:.0f}{}", who, HitZoneName(damage.hitZone),
                        damage.amount, damage.impact, healthLeft, flags),
            &damage.worldPoint);
    }

    void NextDayzDebugger::RecordPlayerHit(const glm::vec3& source, float amount, bool landed, bool woundOpened)
    {
        const float distance = systems_.player ? Horizontal(source, PlayerFeet()) : 0.0f;
        if (!landed)
        {
            Log(EChannel::Survival, 1, NextDayz::L10n::Message("Infected blow for {:.0f} absorbed ({:.1f} m)", amount, distance),
                &source);
            return;
        }
        Log(EChannel::Survival, 2,
            NextDayz::L10n::Message("Player struck for {:.0f} from {:.1f} m{}", amount, distance, woundOpened ? " - wound opened" : ""),
            &source);
    }

    // ------------------------------------------------------------------ helpers

    glm::vec3 NextDayzDebugger::PlayerFeet() const
    {
        return systems_.player && systems_.player->IsValid() ? systems_.player->Position() : glm::vec3(0.0f);
    }

    glm::vec3 NextDayzDebugger::PlayerEye() const
    {
        return systems_.player && systems_.player->IsValid() ? systems_.player->EyePosition() : glm::vec3(0.0f);
    }

    glm::vec3 NextDayzDebugger::PlayerForward() const
    {
        return systems_.player ? systems_.player->Forward() : glm::vec3(0.0f, 0.0f, 1.0f);
    }

    std::optional<FRayHit> NextDayzDebugger::CrosshairHit(float maxDistance) const
    {
        if (!commands_.raycast || !systems_.player)
        {
            return std::nullopt;
        }
        return commands_.raycast(PlayerEye(), PlayerForward(), maxDistance);
    }

    bool NextDayzDebugger::Project(const glm::vec3& world, glm::vec2& screen) const
    {
        return commands_.project && commands_.project(world, screen);
    }

    std::string NextDayzDebugger::ZombieTag(const FZombieRuntime& zombie) const
    {
        return fmt::format("Z{}:{} {}", zombie.handle.index, zombie.handle.generation, ZombieDef(zombie.profile).id);
    }

    int NextDayzDebugger::SpawnPointOf(FZombieHandle handle) const
    {
        if (!systems_.spawns)
        {
            return -1;
        }
        const auto& points = systems_.spawns->Points();
        for (size_t index = 0; index < points.size(); ++index)
        {
            for (const FZombieHandle& member : points[index].active)
            {
                if (member == handle)
                {
                    return static_cast<int>(index);
                }
            }
        }
        return -1;
    }

    float NextDayzDebugger::RatePerMinute(const FSeries& series) const
    {
        const size_t samples = std::min<size_t>(series.Count() > 0 ? series.Count() - 1 : 0, 20);
        if (samples == 0)
        {
            return 0.0f;
        }
        return (series.Latest() - series.Back(samples)) / (static_cast<float>(samples) * kSampleSeconds) * 60.0f;
    }

    void NextDayzDebugger::SelectZombie(FZombieHandle zombie)
    {
        selection_ = {ESelection::Infected, zombie, static_cast<int>(zombie.index)};
    }

    void NextDayzDebugger::SelectLoot(int index)
    {
        selection_ = {ESelection::Loot, {}, index};
    }

    void NextDayzDebugger::SelectSpawnPoint(int index)
    {
        selection_ = {ESelection::SpawnPoint, {}, index};
    }

    const FZombieRuntime* NextDayzDebugger::SelectedZombie() const
    {
        if (selection_.kind != ESelection::Infected || !systems_.zombies)
        {
            return nullptr;
        }
        return systems_.zombies->Resolve(selection_.zombie);
    }

    std::string NextDayzDebugger::SelectionLabel() const
    {
        switch (selection_.kind)
        {
        case ESelection::Infected:
            if (const FZombieRuntime* zombie = SelectedZombie())
            {
                return ZombieTag(*zombie);
            }
            return "infected (gone)";
        case ESelection::Loot: return fmt::format("loot L{}", selection_.index);
        case ESelection::SpawnPoint: return fmt::format("spawn S{}", selection_.index);
        case ESelection::None: break;
        }
        return "none";
    }

    // ------------------------------------------------------------------ input

    bool NextDayzDebugger::HandleKey(SDL_Keycode key, bool pressed, bool shift)
    {
        switch (key)
        {
        case SDLK_F5:
            if (pressed) SetVisible(!visible_);
            return true;
        case SDLK_F6:
            if (pressed) SetLayer(LayerInfected, !LayerOn(LayerInfected));
            return true;
        case SDLK_F7:
            if (pressed) SetLayer(LayerLoot, !LayerOn(LayerLoot));
            return true;
        case SDLK_F8:
        {
            if (pressed)
            {
                const bool on = !(LayerOn(LayerHitboxes) || LayerOn(LayerTraces));
                SetLayer(LayerHitboxes, on);
                SetLayer(LayerTraces, on);
            }
            return true;
        }
        case SDLK_F9:
            if (pressed) SetCursorMode(!cursorMode_);
            return true;
        case SDLK_F10:
            if (pressed)
            {
                if (shift)
                {
                    knobs_.frozen = true;
                    ++knobs_.stepFrames;
                }
                else
                {
                    knobs_.frozen = !knobs_.frozen;
                    knobs_.stepFrames = 0;
                    Log(EChannel::System, 1, knobs_.frozen ? "Simulation frozen (F10)" : "Simulation resumed (F10)");
                }
            }
            return true;
        case SDLK_PAGEUP:
        case SDLK_PAGEDOWN:
            if (!visible_)
            {
                return false;
            }
            if (pressed)
            {
                const int count = static_cast<int>(ETab::Count);
                const int step = key == SDLK_PAGEUP ? count - 1 : 1;
                RequestTab(static_cast<ETab>((static_cast<int>(tab_) + step) % count));
            }
            return true;
        default:
            return false;
        }
    }

    void NextDayzDebugger::PickAtCursor()
    {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        const glm::vec2 cursor(mouse.x, mouse.y);
        float best = 34.0f;
        FSelection picked{};
        const glm::vec3 feet = PlayerFeet();
        const auto consider = [&](const glm::vec3& world, const FSelection& candidate)
        {
            glm::vec2 screen;
            if (glm::distance(world, feet) > knobs_.overlayDistance || !Project(world, screen))
            {
                return;
            }
            const float distance = glm::distance(screen, cursor);
            if (distance < best)
            {
                best = distance;
                picked = candidate;
            }
        };
        if (systems_.zombies)
        {
            for (const FZombieRuntime& zombie : systems_.zombies->Slots())
            {
                if (zombie.active)
                {
                    consider(zombie.position + glm::vec3(0.0f, 1.1f, 0.0f),
                             {ESelection::Infected, zombie.handle, static_cast<int>(zombie.handle.index)});
                }
            }
        }
        if (systems_.loot && LayerOn(LayerLoot))
        {
            const auto& entries = systems_.loot->Entries();
            for (size_t index = 0; index < entries.size(); ++index)
            {
                if (entries[index].state != FLootEntry::EState::Empty)
                {
                    consider(entries[index].worldPos + glm::vec3(0.0f, 0.3f, 0.0f),
                             {ESelection::Loot, {}, static_cast<int>(index)});
                }
            }
        }
        if (systems_.spawns && LayerOn(LayerSpawns))
        {
            const auto& points = systems_.spawns->Points();
            for (size_t index = 0; index < points.size(); ++index)
            {
                consider(points[index].position + glm::vec3(0.0f, 0.5f, 0.0f),
                         {ESelection::SpawnPoint, {}, static_cast<int>(index)});
            }
        }
        selection_ = picked;
        switch (picked.kind)
        {
        case ESelection::Infected: RequestTab(ETab::Infected); break;
        case ESelection::Loot: RequestTab(ETab::Loot); break;
        case ESelection::SpawnPoint: RequestTab(ETab::Spawns); break;
        case ESelection::None: break;
        }
    }

    // ------------------------------------------------------------------ diffing

    void NextDayzDebugger::DiffSystems()
    {
        const bool prime = !watchPrimed_;
        watchPrimed_ = true;
        const glm::vec3 feet = PlayerFeet();

        // Infected: spawns, state transitions, recycles.
        if (systems_.zombies)
        {
            const auto& slots = systems_.zombies->Slots();
            zombieWatch_.resize(slots.size());
            for (size_t index = 0; index < slots.size(); ++index)
            {
                const FZombieRuntime& zombie = slots[index];
                FZombieWatch& watch = zombieWatch_[index];
                const bool replaced = zombie.active && watch.active && watch.handle != zombie.handle;
                if (!prime && watch.active && (!zombie.active || replaced))
                {
                    Log(EChannel::Spawn, 0,
                        NextDayz::L10n::Message("Z{}:{} {}", watch.handle.index, watch.handle.generation,
                                    watch.state == EZombieState::Dead ? "corpse cleared" : "recycled"),
                        &watch.position);
                }
                if (!prime && zombie.active && (!watch.active || replaced))
                {
                    const int point = SpawnPointOf(zombie.handle);
                    Log(EChannel::Spawn, 0,
                        point >= 0 ? NextDayz::L10n::Message("{} spawned at S{} ({:.0f} m away)", ZombieTag(zombie), point,
                                                 Horizontal(zombie.position, feet))
                                   : NextDayz::L10n::Message("{} spawned by hand ({:.0f} m away)", ZombieTag(zombie),
                                                 Horizontal(zombie.position, feet)),
                        &zombie.position);
                }
                else if (!prime && zombie.active && zombie.state != watch.state && zombie.state != EZombieState::Dead)
                {
                    const char* why = "";
                    if (zombie.state == EZombieState::Stagger) why = " (poise broken)";
                    else if (zombie.seesPlayer) why = " (sees you)";
                    else if (zombie.heardNoise > 0.0f) why = " (heard a noise)";
                    else if (zombie.pathFailed) why = " (no route)";
                    const bool hostile = zombie.state == EZombieState::Alert || zombie.state == EZombieState::Chase ||
                                         zombie.state == EZombieState::Attack;
                    Log(EChannel::Infected, hostile ? 1 : 0,
                        NextDayz::L10n::Message("{} {} -> {}{} at {:.1f} m", ZombieTag(zombie), ZombieStateName(watch.state),
                                    ZombieStateName(zombie.state), why, Horizontal(zombie.position, feet)),
                        &zombie.position);
                }
                watch = {zombie.handle, zombie.state, zombie.active, zombie.crippled, zombie.position};
            }
        }

        // Loot: reservations, pickups, restocks, piles.
        if (systems_.loot)
        {
            const auto& entries = systems_.loot->Entries();
            if (lootWatch_.size() > entries.size())
            {
                lootWatch_.clear();
            }
            lootWatch_.resize(entries.size());
            for (size_t index = 0; index < entries.size(); ++index)
            {
                const FLootEntry& entry = entries[index];
                FLootWatch& watch = lootWatch_[index];
                const int state = static_cast<int>(entry.state);
                if (!prime && watch.state >= 0 && state != watch.state)
                {
                    using EState = FLootEntry::EState;
                    const EState from = static_cast<EState>(watch.state);
                    const glm::vec3 where = entry.worldPos;
                    if (from == EState::Available && entry.state == EState::Reserved)
                    {
                        Log(EChannel::Loot, 0, NextDayz::L10n::Message("L{} reserved: {}", index, entry.displayName), &where);
                    }
                    else if (from == EState::Reserved && entry.state == EState::Available)
                    {
                        Log(EChannel::Loot, 0, NextDayz::L10n::Message("L{} pickup cancelled", index), &where);
                    }
                    else if (entry.state == EState::Cooldown)
                    {
                        const double readyAt = systems_.loot->Director().ReadyAt(entry.directorHandle);
                        Log(EChannel::Loot, 1,
                            NextDayz::L10n::Message("L{} taken: {} ({} {}, restock in {:.0f} s)", index, watch.name,
                                        LootProfileName(entry.profile), LootCategoryName(entry.category),
                                        std::max(0.0, readyAt - systems_.loot->WorldSeconds())),
                            &where);
                    }
                    else if (from == EState::Cooldown && entry.state == EState::Available)
                    {
                        Log(EChannel::Loot, 0,
                            NextDayz::L10n::Message("L{} restocked: {} ({})", index, entry.displayName, LootCategoryName(entry.category)),
                            &where);
                    }
                    else if (entry.state == EState::Empty)
                    {
                        Log(EChannel::Loot, 0, NextDayz::L10n::Message("L{} pile emptied: {}", index, watch.name), &where);
                    }
                    else if (from == EState::Empty && entry.state == EState::Available)
                    {
                        Log(EChannel::Loot, 0, NextDayz::L10n::Message("L{} pile: {}", index, entry.displayName), &where);
                    }
                }
                else if (!prime && watch.state < 0 && entry.source == ELootSource::Pile &&
                         entry.state == FLootEntry::EState::Available)
                {
                    Log(EChannel::Loot, 0, NextDayz::L10n::Message("L{} pile: {}", index, entry.displayName), &entry.worldPos);
                }
                watch.state = state;
                watch.name = entry.displayName;
            }
        }

        // Noise: every new event, with how many infected are inside it.
        if (systems_.noise)
        {
            for (const FNoiseEvent& noise : systems_.noise->Events())
            {
                if (noise.sequence <= lastNoiseSequence_)
                {
                    continue;
                }
                lastNoiseSequence_ = std::max(lastNoiseSequence_, noise.sequence);
                if (prime)
                {
                    continue;
                }
                int listeners = 0;
                if (systems_.zombies)
                {
                    for (const FZombieRuntime& zombie : systems_.zombies->Slots())
                    {
                        listeners += zombie.active && zombie.state != EZombieState::Dead &&
                                     glm::distance(zombie.position, noise.position) <= noise.radius ? 1 : 0;
                    }
                }
                Log(EChannel::Noise, 0,
                    NextDayz::L10n::Message("{} noise r={:.0f} m x{:.1f}: {} infected in range", NoiseTypeName(noise.type),
                                noise.radius, noise.strength, listeners),
                    &noise.position);
            }
        }

        // Clock and weather.
        if (systems_.time)
        {
            const int hour = systems_.time->HourInt();
            if (!prime && lastHour_ >= 0 && hour != lastHour_)
            {
                Log(EChannel::World, 0,
                    NextDayz::L10n::Message("Clock {:02d}:00  daylight {:.2f}  chill {:.2f}", hour, systems_.time->Daylight(),
                                systems_.time->Chill()));
            }
            if (!prime && lastHour_ >= 0 && systems_.time->Weather() != lastWeather_)
            {
                Log(EChannel::World, 1,
                    NextDayz::L10n::Message("Weather {} -> {}", WeatherName(lastWeather_), WeatherName(systems_.time->Weather())));
            }
            lastHour_ = hour;
            lastWeather_ = systems_.time->Weather();
        }

        // The body.
        if (systems_.survival)
        {
            const FSurvivalSnapshot& state = systems_.survival->Snapshot();
            if (!prime && state.bleedingWounds != lastWounds_)
            {
                Log(EChannel::Survival, state.bleedingWounds > lastWounds_ ? 2 : 0,
                    state.bleedingWounds > lastWounds_
                        ? NextDayz::L10n::Message("Bleeding: {} wound(s)", state.bleedingWounds)
                        : NextDayz::L10n::Message("Wounds dressed: {} left", state.bleedingWounds));
            }
            if (!prime && state.lifeState != lastLife_)
            {
                Log(EChannel::Survival, 3,
                    state.lifeState == EPlayerLifeState::Alive
                        ? NextDayz::L10n::FText("Player alive again")
                        : NextDayz::L10n::Message("Player died: {}", state.lastDamageSource.empty() ? "?" : state.lastDamageSource));
            }
            const bool weak = systems_.survival->IsWeak();
            if (!prime && weak != lastWeak_)
            {
                Log(EChannel::Survival, weak ? 2 : 0,
                    weak ? NextDayz::L10n::Message("Weak: hunger {:.0f} thirst {:.0f}", state.hunger, state.hydration)
                         : NextDayz::L10n::FText("No longer weak"));
            }
            lastWounds_ = state.bleedingWounds;
            lastLife_ = state.lifeState;
            lastWeak_ = weak;
        }

        // Player traversal and weapon presentation.
        if (systems_.player && systems_.player->IsValid())
        {
            const FPlayerLocomotionState& locomotion = systems_.player->LocomotionState();
            if (!prime && locomotion.traversalAction != lastTraversal_ &&
                locomotion.traversalAction != EPlayerTraversalAction::None)
            {
                Log(EChannel::Player, 0,
                    NextDayz::L10n::Message("{} {:.2f} m", TraversalActionName(locomotion.traversalAction), locomotion.traversalHeight),
                    &feet);
            }
            lastTraversal_ = locomotion.traversalAction;
        }
        if (systems_.weapons)
        {
            const EWeaponPresentationAction action = systems_.weapons->PresentationAction();
            if (!prime && action != lastWeaponAction_ && action != EWeaponPresentationAction::None)
            {
                Log(EChannel::Weapon, 0,
                    NextDayz::L10n::Message("{} {}", WeaponActionName(action), systems_.weapons->ActiveWeaponId()));
            }
            lastWeaponAction_ = action;
            const std::string weaponId = systems_.weapons->ActiveWeaponId();
            if (!prime && weaponId != lastWeaponId_)
            {
                Log(EChannel::Weapon, 0, weaponId.empty() ? NextDayz::L10n::FText("Hands empty")
                                                          : NextDayz::L10n::Message("Holding {} (slot {})", weaponId,
                                                                        systems_.weapons->ActiveSlot() + 1));
            }
            lastWeaponId_ = weaponId;
        }
    }

    // ------------------------------------------------------------------ history

    void NextDayzDebugger::SampleHistory(float deltaSeconds)
    {
        sampleAccumulator_ += deltaSeconds;
        if (sampleAccumulator_ < kSampleSeconds)
        {
            return;
        }
        sampleAccumulator_ = std::fmod(sampleAccumulator_, kSampleSeconds);
        if (systems_.survival)
        {
            const FSurvivalSnapshot& state = systems_.survival->Snapshot();
            history_.health.Push(state.health);
            history_.hunger.Push(state.hunger);
            history_.hydration.Push(state.hydration);
            history_.cold.Push(state.cold * 100.0f);
        }
        if (systems_.zombies)
        {
            history_.infectedActive.Push(static_cast<float>(systems_.zombies->ActiveCount()));
            history_.infectedAlerted.Push(static_cast<float>(systems_.zombies->AlertedCount()));
        }
        if (systems_.loot)
        {
            history_.lootAvailable.Push(static_cast<float>(systems_.loot->RemainingCount()));
        }
        history_.tickMs.Push(profiler_.Stat(ETimer::Tick).lastMs);
        history_.frameMs.Push(ImGui::GetCurrentContext() ? ImGui::GetIO().DeltaTime * 1000.0f : 0.0f);
        history_.pathSolves.Push(static_cast<float>(pathSolvesSinceSample_));
        pathSolvesSinceSample_ = 0;
    }

    // ------------------------------------------------------------------ warnings

    void NextDayzDebugger::RefreshWarnings()
    {
        warnings_.clear();
        const auto warn = [this](int severity, L10n::FText text) { warnings_.push_back({severity, std::move(text)}); };

        if (systems_.navGrid && !systems_.navGrid->IsBuilt())
        {
            warn(2, "Infected nav grid is not built: they walk straight lines");
        }
        if (systems_.spawns && systems_.spawns->Points().empty())
        {
            warn(2, "No infected spawn anchors (nd_spawn_zombie_*) in the scene");
        }
        if (systems_.zombies && systems_.combat)
        {
            int noHitboxes = 0;
            int stuck = 0;
            int unroutable = 0;
            int offGrid = 0;
            for (const FZombieRuntime& zombie : systems_.zombies->Slots())
            {
                if (!zombie.active || zombie.state == EZombieState::Dead)
                {
                    continue;
                }
                noHitboxes += systems_.combat->Hitboxes(zombie.handle).empty() ? 1 : 0;
                stuck += zombie.stuckSeconds > 1.0f ? 1 : 0;
                const bool hunting = zombie.state == EZombieState::Chase || zombie.state == EZombieState::Investigate;
                unroutable += hunting && zombie.pathFailed ? 1 : 0;
                if (systems_.navGrid && systems_.navGrid->IsBuilt() && !systems_.navGrid->IsWalkable(zombie.position))
                {
                    ++offGrid;
                }
            }
            if (noHitboxes > 0)
                warn(2, L10n::Message("{} active infected have no hitboxes and cannot be shot", noHitboxes));
            if (stuck > 0)
                warn(1, L10n::Message("{} infected making no progress (stuck > 1 s)", stuck));
            if (unroutable > 0)
                warn(1, L10n::Message("{} hunting infected found no route", unroutable));
            if (offGrid > 0)
                warn(1, L10n::Message("{} infected stand on unwalkable nav cells", offGrid));
        }
        if (pathSaturatedUpdates_ >= 30)
        {
            warn(1, L10n::Message("Path budget saturated for {} updates (MaxPathSolvesPerUpdate {})", pathSaturatedUpdates_,
                                systems_.zombies ? systems_.zombies->Config().MaxPathSolvesPerUpdate : 0));
        }
        if (systems_.loot)
        {
            if (systems_.loot->SpotCount() == 0)
            {
                warn(1, "No loot spots (nd_spawn_loot_*) in the scene");
            }
            std::array<int, static_cast<size_t>(ELootCategory::Count)> available{};
            for (const FLootEntry& entry : systems_.loot->Entries())
            {
                if (entry.state == FLootEntry::EState::Available)
                {
                    ++available[static_cast<size_t>(entry.category)];
                }
            }
            std::string missing;
            for (size_t category = 0; category < available.size(); ++category)
            {
                if (available[category] == 0)
                {
                    missing += missing.empty() ? "" : ", ";
                    missing += LootCategoryName(static_cast<ELootCategory>(category));
                }
            }
            if (!missing.empty())
            {
                warn(1, L10n::Message("No loot available for: {}", missing));
            }
        }
        if (systems_.anchors && systems_.anchors->Count(EWorldAnchorType::Well) == 0)
        {
            warn(1, "No wells on the map: water only comes from loot");
        }
        if (profiler_.Stat(ETimer::Tick).averageMs > 8.0f)
        {
            warn(1, L10n::Message("Game tick averages {:.1f} ms", profiler_.Stat(ETimer::Tick).averageMs));
        }
        if (systems_.player && systems_.player->IsValid() && systems_.player->Position().y < -50.0f)
        {
            warn(2, "Player fell below the world");
        }
    }

    // ------------------------------------------------------------------ snapshot

    std::string NextDayzDebugger::WriteSnapshot()
    {
        using Json = nlohmann::ordered_json;
        Json root;
        root["frame"] = frame_;
        root["clock"] = clock_;
        root["layers"] = LayerString();
        root["selection"] = SelectionLabel();
        root["frozen"] = knobs_.frozen;
        root["timeScale"] = knobs_.timeScale;

        Json warnings = Json::array();
        for (const FWarning& warning : warnings_)
        {
            warnings.push_back({{"severity", warning.severity}, {"text", warning.text.english}});
        }
        root["warnings"] = std::move(warnings);

        Json timers = Json::object();
        for (size_t index = 0; index < kTimerCount; ++index)
        {
            const FTimerStat& stat = profiler_.Stat(static_cast<ETimer>(index));
            timers[TimerName(static_cast<ETimer>(index))] = {{"last", stat.lastMs}, {"avg", stat.averageMs},
                                                              {"peak", stat.peakMs}};
        }
        root["timersMs"] = std::move(timers);

        if (systems_.player && systems_.player->IsValid())
        {
            const FPlayerLocomotionState& locomotion = systems_.player->LocomotionState();
            root["player"] = {
                {"position", Vec(systems_.player->Position())},
                {"yawDeg", glm::degrees(systems_.player->Yaw())},
                {"pitchDeg", glm::degrees(systems_.player->Pitch())},
                {"firstPerson", systems_.player->IsFirstPerson()},
                {"stance", StanceName(locomotion.actualStance)},
                {"gait", GaitName(locomotion.gait)},
                {"speed", locomotion.horizontalSpeed},
                {"onGround", locomotion.onGround},
                {"traversal", TraversalActionName(locomotion.traversalAction)},
                {"action", systems_.actions ? ActionName(systems_.actions->Action()) : "?"},
                {"baseClip", systems_.rig ? systems_.rig->CurrentBaseClipName() : std::string()},
            };
        }
        if (systems_.survival)
        {
            const FSurvivalSnapshot& state = systems_.survival->Snapshot();
            root["survival"] = {
                {"health", state.health},
                {"hunger", state.hunger},
                {"hydration", state.hydration},
                {"cold", state.cold},
                {"bleeding", state.bleedingWounds},
                {"painkillerSeconds", state.painkillerSeconds},
                {"healing", state.healing},
                {"weak", systems_.survival->IsWeak()},
                {"alive", systems_.survival->IsAlive()},
                {"lastDamageSource", state.lastDamageSource},
                {"healthPerMin", RatePerMinute(history_.health)},
                {"hungerPerMin", RatePerMinute(history_.hunger)},
                {"hydrationPerMin", RatePerMinute(history_.hydration)},
            };
        }
        if (systems_.inventory)
        {
            Json items = Json::array();
            for (const FItemInstance& instance : systems_.inventory->Instances())
            {
                items.push_back({{"id", instance.instanceId},
                                 {"def", instance.defId},
                                 {"count", instance.count},
                                 {"container", instance.containerId},
                                 {"worn", systems_.inventory->IsWorn(instance.instanceId)},
                                 {"loadedAmmo", instance.loadedAmmo}});
            }
            root["inventory"] = {{"used", systems_.inventory->UsedCapacity()},
                                 {"capacity", systems_.inventory->TotalCapacity()},
                                 {"armor", systems_.inventory->Armor()},
                                 {"warmth", systems_.inventory->Warmth()},
                                 {"items", std::move(items)}};
        }
        if (systems_.weapons && systems_.inventory)
        {
            root["weapons"] = {{"active", systems_.weapons->ActiveWeaponId()},
                               {"slot", systems_.weapons->ActiveSlot()},
                               {"slots", Json::array({systems_.weapons->SlotWeaponId(0), systems_.weapons->SlotWeaponId(1)})},
                               {"mag", systems_.weapons->AmmoInMag()},
                               {"reserve", systems_.weapons->AmmoReserve(*systems_.inventory)},
                               {"action", WeaponActionName(systems_.weapons->PresentationAction())},
                               {"shots", systems_.weapons->LastShotSequence()}};
        }
        if (systems_.zombies)
        {
            Json infected = Json::array();
            const glm::vec3 feet = PlayerFeet();
            for (const FZombieRuntime& zombie : systems_.zombies->Slots())
            {
                if (!zombie.active)
                {
                    continue;
                }
                infected.push_back({
                    {"tag", ZombieTag(zombie)},
                    {"state", ZombieStateName(zombie.state)},
                    {"health", zombie.health},
                    {"position", Vec(zombie.position)},
                    {"distance", Horizontal(zombie.position, feet)},
                    {"speed", zombie.Speed()},
                    {"aware", zombie.aware},
                    {"seesPlayer", zombie.seesPlayer},
                    {"heardNoise", zombie.heardNoise},
                    {"pathLeft", zombie.pathWaypoint < zombie.path.size() ? zombie.path.size() - zombie.pathWaypoint : 0},
                    {"pathSolves", zombie.pathSolves},
                    {"pathFailed", zombie.pathFailed},
                    {"stuckSeconds", zombie.stuckSeconds},
                    {"poise", zombie.poise},
                    {"lame", zombie.Lame()},
                    {"spawnPoint", SpawnPointOf(zombie.handle)},
                    {"hitboxes", systems_.combat ? systems_.combat->Hitboxes(zombie.handle).size() : 0},
                });
            }
            root["infected"] = {{"active", systems_.zombies->ActiveCount()},
                                {"alerted", systems_.zombies->AlertedCount()},
                                {"kills", systems_.zombies->KillCount()},
                                {"sightScale", systems_.zombies->SightScale()},
                                {"pathSolvesLastUpdate", systems_.zombies->PathSolvesLastUpdate()},
                                {"pathDeferredLastUpdate", systems_.zombies->PathDeferredLastUpdate()},
                                {"list", std::move(infected)}};
        }
        if (systems_.spawns)
        {
            Json points = Json::array();
            std::array<int, 10> verdicts{};
            for (const FZombieSpawnPoint& point : systems_.spawns->Points())
            {
                ++verdicts[static_cast<size_t>(point.verdict)];
                int out = 0;
                for (const FZombieHandle& handle : point.active) out += handle.IsValid() ? 1 : 0;
                points.push_back({{"profile", ZombieDef(point.profile).id},
                                  {"group", point.groupSize},
                                  {"out", out},
                                  {"verdict", SpawnVerdictName(point.verdict)},
                                  {"distance", point.playerDistance},
                                  {"cooldownLeft", std::max(0.0, point.cooldownUntil - systems_.spawns->WorldSeconds())},
                                  {"position", Vec(point.position)}});
            }
            Json verdictCounts = Json::object();
            for (size_t index = 0; index < verdicts.size(); ++index)
            {
                if (verdicts[index] > 0)
                {
                    verdictCounts[SpawnVerdictName(static_cast<ESpawnVerdict>(index))] = verdicts[index];
                }
            }
            root["spawns"] = {{"cap", systems_.spawns->GlobalCap()},
                              {"spawned", systems_.spawns->SpawnCount()},
                              {"recycled", systems_.spawns->RecycledCount()},
                              {"rejectedVisible", systems_.spawns->RejectedVisible()},
                              {"verdicts", std::move(verdictCounts)},
                              {"points", std::move(points)}};
        }
        if (systems_.loot)
        {
            const LootSystem& loot = *systems_.loot;
            Json categories = Json::object();
            for (size_t category = 0; category < static_cast<size_t>(ELootCategory::Count); ++category)
            {
                int available = 0;
                int total = 0;
                int cooling = 0;
                for (const FLootEntry& entry : loot.Entries())
                {
                    if (entry.state == FLootEntry::EState::Empty || static_cast<size_t>(entry.category) != category)
                    {
                        continue;
                    }
                    ++total;
                    available += entry.state == FLootEntry::EState::Available ? 1 : 0;
                    cooling += entry.state == FLootEntry::EState::Cooldown ? 1 : 0;
                }
                categories[LootCategoryName(static_cast<ELootCategory>(category))] = {
                    {"available", available}, {"total", total}, {"cooldown", cooling},
                    {"cap", loot.CategoryCap(static_cast<ELootCategory>(category))}};
            }
            Json cooling = Json::array();
            const glm::vec3 feet = PlayerFeet();
            for (size_t index = 0; index < loot.Entries().size(); ++index)
            {
                const FLootEntry& entry = loot.Entries()[index];
                if (entry.state != FLootEntry::EState::Cooldown)
                {
                    continue;
                }
                const FLootSlot* slot = loot.ResolveSlot(entry.directorHandle);
                cooling.push_back({{"index", index},
                                   {"category", LootCategoryName(entry.category)},
                                   {"block", slot ? LootRespawnBlockName(slot->block) : "-"},
                                   {"readyIn", std::max(0.0, loot.Director().ReadyAt(entry.directorHandle) - loot.WorldSeconds())},
                                   {"distance", glm::distance(entry.worldPos, feet)}});
            }
            root["loot"] = {{"entries", loot.Entries().size()},
                            {"spots", loot.SpotCount()},
                            {"piles", loot.PileCount()},
                            {"zones", loot.Zones().size()},
                            {"available", loot.RemainingCount()},
                            {"cooldown", loot.CooldownCount()},
                            {"foodScarce", loot.FoodScarce()},
                            {"medicalScarce", loot.MedicalScarce()},
                            {"categories", std::move(categories)},
                            {"cooling", std::move(cooling)}};
        }
        if (systems_.time)
        {
            root["world"] = {{"hour", systems_.time->Hour()},
                             {"weather", WeatherName(systems_.time->Weather())},
                             {"daylight", systems_.time->Daylight()},
                             {"storm", systems_.time->Storm()},
                             {"chill", systems_.time->Chill()},
                             {"timeScale", systems_.time->TimeScale()}};
        }
        if (systems_.noise)
        {
            Json noises = Json::array();
            for (const FNoiseEvent& noise : systems_.noise->Events())
            {
                noises.push_back({{"type", NoiseTypeName(noise.type)},
                                  {"radius", noise.radius},
                                  {"strength", noise.strength},
                                  {"remaining", noise.remainingSeconds},
                                  {"position", Vec(noise.position)}});
            }
            root["noise"] = std::move(noises);
        }

        Json events = Json::array();
        const size_t first = log_.Size() > 200 ? log_.Size() - 200 : 0;
        for (size_t index = first; index < log_.Size(); ++index)
        {
            const FEvent& event = log_.At(index);
            Json line = {{"t", std::round(event.time * 100.0) / 100.0},
                         {"channel", ChannelName(event.channel)},
                         {"severity", event.severity},
                         {"text", event.text.english}};
            if (event.repeats > 1) line["repeats"] = event.repeats;
            events.push_back(std::move(line));
        }
        root["events"] = std::move(events);

        const std::string path = Utilities::FileHelper::GetWritableFilePath(kSnapshotPath);
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        if (!file)
        {
            SPDLOG_WARN("[NextDayz] debug snapshot: cannot write {}", path);
            return {};
        }
        file << root.dump(2);
        snapshotPath_ = path;
        Log(EChannel::System, 0, NextDayz::L10n::Message("Snapshot written: {}", path));
        SPDLOG_INFO("[NextDayz] debug snapshot written: {}", path);
        return path;
    }
}
