#pragma once

// ============================================================================
// NextDayzDebugger.hpp - The in-game debugger: one place to watch and poke
// every NextDayz system while the game runs.
//
//  * Window (F5): tabs for Overview / Player / Survival / Inventory / Weapons /
//    Infected / Spawns / Loot / World / Combat / Log. Each tab reads the live
//    system and offers the knobs that system has (spawn, kill, restock,
//    teleport, set needs, clock, weather...).
//  * World overlay: layers projected through the game camera and drawn with
//    ImGui (clipped at the near plane, no depth test, so they read through
//    walls and in every renderer/upscaler and in UI screenshots) -
//    infected state/path/facing, perception cones, hitboxes, weapon traces,
//    noise rings, spawn points with their last verdict, loot with its restock
//    block, loot zones, the nav grid and map anchors.
//  * Event log: always recording, built mostly by diffing system state each
//    frame (spawn/state/death/recycle, loot state, noise, weather, wounds) plus
//    a few hooks from the game (damage, messages, traces).
//  * Timers + history: per-system CPU cost and 60 s plots of the key values.
//  * Freeze / single step / time scale, a free debug cursor (F9) that can
//    click-select things in the world, and a JSON snapshot of everything for
//    agents (nextdayz.debug.dump).
//
// The game instance owns it, binds pointers to its systems once, and calls
// BeginTick / EndTick / Draw. Rules never read the debugger, except for the
// knobs the game asks it for (time scales, god mode, orbit camera).
// ============================================================================

#include "Engine/Common/CoreMinimal.hpp"

#include <functional>
#include <optional>
#include <span>

#include <SDL3/SDL_keycode.h>
#include <glm/glm.hpp>

#include "Application/Game/NextDayz/Combat/CombatEvents.hpp"
#include "Application/Game/NextDayz/Data/ZombieDefs.hpp"
#include "Application/Game/NextDayz/Debugger/DebugEventLog.hpp"
#include "Application/Game/NextDayz/Debugger/DebugHistory.hpp"
#include "Application/Game/NextDayz/Debugger/DebugProfiler.hpp"
#include "Application/Game/NextDayz/Inventory/Inventory.hpp"
#include "Application/Game/NextDayz/Player/PlayerState.hpp"
#include "Application/Game/NextDayz/Weapons/WeaponDefs.hpp"
#include "Application/Game/NextDayz/World/TimeSystem.hpp"
#include "Application/Game/NextDayz/Zombies/ZombieSystem.hpp"

class NextEngine;

namespace NextGameplay
{
    class FNavGrid;
}

namespace NextDayz
{
    struct FConfig;
    class PlayerController;
    class PlayerActionController;
    class SurvivalSystem;
    class PlayerRigVisual;
    class WeaponSystem;
    class LootSystem;
    class WorldAnchorRegistry;
    class CombatSystem;
    class CombatEffects;
    class NextDayzAudio;
    class NoiseSystem;
    class ZombieSpawnDirector;
    class ZombieRigVisual;
}

namespace NextDayz::Debug
{
    // World overlay layers (bit mask; also addressable by id from the
    // nextdayz.debug.layers cvar, e.g. "infected,noise,spawns").
    enum ELayer : uint32_t
    {
        LayerInfected = 1u << 0,    // state, facing, path, last known position
        LayerPerception = 1u << 1,  // sight cones and what each infected noticed
        LayerHitboxes = 1u << 2,    // skeleton hit capsules
        LayerTraces = 1u << 3,      // recent weapon traces
        LayerNoise = 1u << 4,       // live noise events
        LayerSpawns = 1u << 5,      // infected spawn points + activation rings
        LayerLoot = 1u << 6,        // loot entries, state, restock block
        LayerLootZones = 1u << 7,   // POI zones loot restocks by
        LayerNav = 1u << 8,         // infected nav grid around the player
        LayerAnchors = 1u << 9,     // POIs, wells, cache, firecrackers
        LayerPlayer = 1u << 10,     // velocity, aim ray, reach
    };

    struct FLayerInfo
    {
        ELayer layer;
        const char* id;
        const char* label;
    };
    std::span<const FLayerInfo> AllLayers();

    enum class ETab : uint8_t
    {
        Overview,
        Player,
        Survival,
        Inventory,
        Weapons,
        Infected,
        Spawns,
        Loot,
        World,
        Combat,
        Log,
        Count,
    };
    const char* TabName(ETab tab);
    std::optional<ETab> ParseTab(std::string_view name);

    enum class EExpeditionAction : uint8_t
    {
        ReturnToSafehouse,
        Save,
        ReloadMap,
        SafeSpawn,
        ExtractionAlpha,
        ExtractionBravo,
        RescueSupplies,
    };
    std::optional<EExpeditionAction> ParseExpeditionAction(std::string_view name);

    // Everything the debugger may read or poke: the game instance's members.
    struct FSystems
    {
        NextEngine* engine = nullptr;
        FConfig* config = nullptr;
        PlayerController* player = nullptr;
        PlayerActionController* actions = nullptr;
        SurvivalSystem* survival = nullptr;
        PlayerRigVisual* rig = nullptr;
        WeaponSystem* weapons = nullptr;
        Inventory* inventory = nullptr;
        LootSystem* loot = nullptr;
        WorldAnchorRegistry* anchors = nullptr;
        ZombieSystem* zombies = nullptr;
        CombatSystem* combat = nullptr;
        CombatEffects* effects = nullptr;
        NextDayzAudio* audio = nullptr;
        NoiseSystem* noise = nullptr;
        ZombieSpawnDirector* spawns = nullptr;
        const NextGameplay::FNavGrid* navGrid = nullptr;
        TimeSystem* time = nullptr;
        const std::vector<ZombieRigVisual>* zombieVisuals = nullptr;
    };

    // The camera the frame is rendered with (OverrideRenderCamera).
    struct FViewCamera
    {
        glm::mat4 view{1.0f};
        float fovDegrees = 60.0f;
        float nearPlane = 0.1f;
        float farPlane = 1500.0f;
    };

    struct FPainter;   // NextDayzDebugOverlay.cpp

    struct FRayHit
    {
        glm::vec3 point{0.0f};
        glm::vec3 normal{0.0f, 1.0f, 0.0f};
        float distance = 0.0f;
        uint32_t instanceId = 0;
    };

    // Requests that span several systems or the scene; the game carries them out.
    struct FCommands
    {
        std::function<bool(const glm::vec3& world, glm::vec2& screen)> project;
        std::function<std::optional<FViewCamera>()> viewCamera;
        std::function<std::optional<FRayHit>(const glm::vec3& origin, const glm::vec3& direction, float maxDistance)>
            raycast;
        std::function<std::string(uint32_t instanceId)> nodeName;
        std::function<void(const glm::vec3& feet)> teleport;
        std::function<FZombieHandle(EZombieProfile profile, const glm::vec3& feet)> spawnZombie;
        std::function<void(FZombieHandle zombie)> killZombie;
        std::function<void(const std::string& itemId, int count)> giveItem;
        std::function<void(FItemInstanceId instanceId)> useItem;
        std::function<void(FItemInstanceId instanceId)> dropItem;
        std::function<void()> resetSession;
        std::function<void(EExpeditionAction action)> expeditionAction;
        std::function<void()> syncZombieVisuals;
        std::function<void(bool free)> setCursorFree;
    };

    // Per-frame facts only the game instance holds.
    struct FGameFacts
    {
        double survivalSeconds = 0.0;
        bool paused = false;
        bool mouseFree = false;           // inventory, map, pause or death freed the cursor
        bool lanternLit = false;
        float lanternSeconds = 0.0f;
        std::optional<glm::vec3> cacheMarker;
        std::vector<glm::vec3> firecrackers;
        bool soldierRigLoaded = false;
        bool zombieRigLoaded = false;
        bool lootCatalogLoaded = false;
        bool hoveringWell = false;
        std::string objective;
        bool campaignEnabled = false;
        bool inField = false;
        bool expeditionBusy = false;
        bool saveBlocked = false;
        std::string regionName;
        std::string flightPhase;
    };

    // Debug knobs the game reads back (bound to nextdayz.debug.* cvars too).
    struct FKnobs
    {
        float timeScale = 1.0f;          // whole simulation
        float zombieTimeScale = 1.0f;    // infected only (0 freezes them)
        float fxTimeScale = 1.0f;        // combat effects only
        bool frozen = false;             // simulation stopped; Step advances it
        int stepFrames = 0;
        bool god = false;                // infected blows land but do no damage
        bool freezeNeeds = false;        // survival metabolism stopped
        bool orbit = false;              // orbit camera instead of the gameplay camera
        float orbitYaw = 90.0f;
        float orbitDistance = 3.0f;
        float orbitHeight = 1.05f;
        bool orbitZombie = false;        // orbit the focus infected (selection first)
        float overlayDistance = 220.0f;  // markers beyond this are not drawn
        float labelDistance = 45.0f;     // text labels beyond this are not drawn
        float navRadius = 26.0f;
    };

    enum class ESelection : uint8_t
    {
        None,
        Infected,
        Loot,
        SpawnPoint,
    };

    struct FSelection
    {
        ESelection kind = ESelection::None;
        FZombieHandle zombie{};
        int index = -1;                  // loot entry / spawn point index
    };

    struct FWarning
    {
        int severity = 1;                // 1 warning, 2 error
        std::string text;
    };

    struct FTraceRecord
    {
        FWeaponTraceEvent trace;
        float remainingSeconds = 0.0f;
    };

    class NextDayzDebugger
    {
    public:
        static constexpr size_t kHistory = 240;   // 60 s at 4 Hz
        using FSeries = TSeries<kHistory>;

        void Bind(const FSystems& systems, FCommands commands);
        // Scene load or in-process restart: forget per-entity caches.
        void OnSessionStart(const char* reason);

        // --- frame ---
        // First thing in the game tick. False = frozen, skip the simulation this
        // frame. Otherwise `deltaSeconds` is scaled (a single step uses 1/60 s).
        bool BeginTick(float& deltaSeconds);
        // Last thing in the game tick: diff systems into the log, sample history,
        // refresh warnings, age traces.
        void EndTick(float deltaSeconds);
        // Very first thing each frame, before any early return: closes the
        // previous frame's timers.
        void BeginFrame() { profiler_.EndFrame(); }
        // From OnRenderUI: overlay layers + window (when visible).
        void Draw(const FGameFacts& facts);

        // --- game hooks ---
        void Log(EChannel channel, int severity, std::string text, const glm::vec3* where = nullptr);
        void RecordTrace(const FWeaponTraceEvent& trace);
        void RecordDamage(const FDamageEvent& damage);
        void RecordPlayerHit(const glm::vec3& source, float amount, bool landed, bool woundOpened);

        // --- input ---
        bool HandleKey(SDL_Keycode key, bool pressed, bool shift);
        // Left click into the world while the debug cursor is free.
        void PickAtCursor();

        // --- state ---
        bool Visible() const { return visible_; }
        void SetVisible(bool visible);
        bool CursorMode() const { return cursorMode_; }
        void SetCursorMode(bool enabled);
        bool LayerOn(ELayer layer) const { return (layers_ & layer) != 0; }
        void SetLayer(ELayer layer, bool on) { layers_ = on ? (layers_ | layer) : (layers_ & ~static_cast<uint32_t>(layer)); }
        uint32_t LayerMask() const { return layers_; }
        std::string LayerString() const;
        // "infected,noise" (replace), "+nav" / "-loot" (edit), "all", "none".
        void ApplyLayerString(std::string_view text);
        ETab Tab() const { return tab_; }
        void RequestTab(ETab tab) { requestedTab_ = tab; tab_ = tab; }
        FKnobs& Knobs() { return knobs_; }
        const FKnobs& Knobs() const { return knobs_; }
        Profiler& Timers() { return profiler_; }
        const EventLog& Events() const { return log_; }
        const std::vector<FWarning>& Warnings() const { return warnings_; }
        const FSelection& Selection() const { return selection_; }
        void SelectZombie(FZombieHandle zombie);
        void SelectLoot(int index);
        void SelectSpawnPoint(int index);
        void ClearSelection() { selection_ = {}; }
        const FZombieRuntime* SelectedZombie() const;
        std::string SelectionLabel() const;
        const std::vector<FTraceRecord>& Traces() const { return traces_; }
        uint64_t Frame() const { return frame_; }
        double Clock() const { return clock_; }
        // Writes every system's state as JSON; returns the absolute path (empty on failure).
        std::string WriteSnapshot();
        const std::string& LastSnapshotPath() const { return snapshotPath_; }

    private:
        struct FHistory
        {
            FSeries health;
            FSeries hunger;
            FSeries hydration;
            FSeries cold;
            FSeries infectedActive;
            FSeries infectedAlerted;
            FSeries lootAvailable;
            FSeries tickMs;
            FSeries frameMs;
            FSeries pathSolves;
        };
        struct FZombieWatch
        {
            FZombieHandle handle{};
            EZombieState state = EZombieState::Dormant;
            bool active = false;
            bool crippled = false;
            glm::vec3 position{0.0f};
        };
        struct FLootWatch
        {
            int state = -1;
            std::string name;
        };

        // NextDayzDebugger.cpp
        void DiffSystems();
        void SampleHistory(float deltaSeconds);
        void RefreshWarnings();
        std::string ZombieTag(const FZombieRuntime& zombie) const;
        int SpawnPointOf(FZombieHandle handle) const;
        glm::vec3 PlayerFeet() const;
        glm::vec3 PlayerEye() const;
        glm::vec3 PlayerForward() const;
        std::optional<FRayHit> CrosshairHit(float maxDistance = 250.0f) const;
        float RatePerMinute(const FSeries& series) const;

        // NextDayzDebugPanels.cpp
        void DrawWindow(const FGameFacts& facts);
        void DrawToolbar(const FGameFacts& facts);
        void DrawWarnings();
        void TabOverview(const FGameFacts& facts);
        void TabPlayer();
        void TabSurvival();
        void TabInventory();
        void TabWeapons();
        void TabInfected();
        void TabSpawns();
        void TabLoot();
        void TabWorld(const FGameFacts& facts);
        void TabCombat();
        void TabLog();
        void InfectedDetail(const FZombieRuntime& zombie);
        void LayerToggles();

        // NextDayzDebugOverlay.cpp
        void DrawOverlay(const FGameFacts& facts);
        void OverlayInfected(FPainter& paint);
        void OverlayPerception(FPainter& paint);
        void OverlayHitboxes(FPainter& paint);
        void OverlayTraces(FPainter& paint);
        void OverlayNoise(FPainter& paint);
        void OverlaySpawns(FPainter& paint);
        void OverlayLoot(FPainter& paint);
        void OverlayLootZones(FPainter& paint);
        void OverlayNav(FPainter& paint);
        void OverlayAnchors(FPainter& paint, const FGameFacts& facts);
        void OverlayPlayer(FPainter& paint);
        void OverlaySelection(FPainter& paint);
        bool Project(const glm::vec3& world, glm::vec2& screen) const;

        FSystems systems_{};
        FCommands commands_{};
        FKnobs knobs_{};
        Profiler profiler_{};
        EventLog log_{2048};
        FHistory history_{};
        std::vector<FWarning> warnings_;
        std::vector<FTraceRecord> traces_;
        FSelection selection_{};

        bool visible_ = false;
        bool cursorMode_ = false;
        uint32_t layers_ = LayerInfected | LayerLoot | LayerHitboxes | LayerTraces | LayerNoise | LayerPerception;
        ETab tab_ = ETab::Overview;
        std::optional<ETab> requestedTab_;

        uint64_t frame_ = 0;
        double clock_ = 0.0;
        float sampleAccumulator_ = 0.0f;
        float warningAccumulator_ = 0.0f;
        int pathSolvesSinceSample_ = 0;
        int pathSaturatedUpdates_ = 0;
        std::string snapshotPath_;

        // Diff caches for the event log.
        std::vector<FZombieWatch> zombieWatch_;
        std::vector<FLootWatch> lootWatch_;
        uint64_t lastNoiseSequence_ = 0;
        int lastHour_ = -1;
        EWeather lastWeather_ = EWeather::Clear;
        int lastWounds_ = 0;
        EPlayerLifeState lastLife_ = EPlayerLifeState::Alive;
        bool lastWeak_ = false;
        EPlayerTraversalAction lastTraversal_ = EPlayerTraversalAction::None;
        EWeaponPresentationAction lastWeaponAction_ = EWeaponPresentationAction::None;
        std::string lastWeaponId_;
        bool watchPrimed_ = false;

        // UI state.
        std::array<bool, kChannelCount> channelFilter_{};
        std::string logFilter_;
        bool logFollow_ = true;
        std::optional<glm::vec3> hoveredWorldPoint_;   // log/table row under the cursor
        int spawnProfile_ = 0;
        int spawnCount_ = 1;
        float spawnDistance_ = 8.0f;
        int giveItemIndex_ = 0;
        int giveCount_ = 1;
        std::string itemFilter_;
        int lootCategoryFilter_ = -1;
        int lootStateFilter_ = -1;
        bool infectedShowDead_ = true;
        std::optional<std::vector<glm::vec3>> navProbePath_;
    };
}
