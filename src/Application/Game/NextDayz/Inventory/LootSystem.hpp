#pragma once

// ============================================================================
// LootSystem.hpp - Everything lying in the world that can be picked up.
//
//  * Authored loot: map nodes whose module name is in the authored table
//    (cw_wpn_ak, cw_item_medkit, ...). The artist's placement is the visual.
//  * Spots: nd_spawn_loot_<profile> anchors. Each rolls an item from its
//    profile's table (LootTables) and shows it with a pooled node whose model
//    is swapped to the rolled item's catalog visual.
//  * Piles: what the player drops and what the infected leave behind; pooled
//    nodes, gone once picked up.
//
// Restocking (LootDirector): a taken spot cools down per category, and only
// restocks when the player has left its area (spots are clustered into POI
// zones) and has not been looking at it. Restocked spots re-roll, under
// per-category stock caps so guns stay rare; food restocks faster while the
// player is starving. Picking up takes whatever fits; the rest stays.
// ============================================================================

#include <string>
#include <optional>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "Application/Game/NextDayz/Data/LootTables.hpp"
#include "Application/Game/NextDayz/Inventory/Inventory.hpp"
#include "Application/Game/NextDayz/NextDayzConfig.hpp"
#include "Application/Game/NextDayz/World/LootDirector.hpp"

class NextEngine;

namespace Assets
{
    class Scene;
    class Node;
}

namespace NextDayz
{
    // A catalog item model (nextdayz_loot_items.scad) injected into the scene.
    struct FLootVisual
    {
        uint32_t modelId = 0;
        std::array<uint32_t, 16> materials{};
        bool valid = false;
    };

    // Map anchor that becomes a loot spot.
    struct FLootSpotSeed
    {
        glm::vec3 position{};
        ELootProfile profile = ELootProfile::Residential;
        // Display spot (nd_item): always this item. Never rolls, always stocked
        // at start, and restocks shortly after it is taken (training ground).
        std::string itemId;
        int count = 1;
        float yaw = 0.0f;
    };

    enum class ELootSource : uint8_t
    {
        Authored,
        Spot,
        Pile,
    };

    struct FLootEntry
    {
        enum class EState
        {
            Available,
            Reserved,
            Cooldown,
            Empty,      // a pile that was picked up; never restocks
        };

        ELootSource source = ELootSource::Authored;
        uint32_t nodeInstanceId = 0;   // the map node (authored) or the pooled visual
        glm::vec3 worldPos{0.0f};
        float yaw = 0.0f;
        std::string displayName;
        std::vector<FLootStack> contents;
        ELootProfile profile = ELootProfile::Residential;
        ELootCategory category = ELootCategory::Misc;
        EState state = EState::Available;
        FLootSlotHandle directorHandle{};
        int zone = -1;
        int authoredIndex = -1;
        double pileTime = 0.0;
        std::string persistentId;
        std::string fixedItem;     // display spot: what it always holds
        int fixedCount = 1;
    };

    struct FSavedLootEntry
    {
        std::string id;
        ELootSource source = ELootSource::Spot;
        glm::vec3 position{};
        float yaw = 0.0f;
        std::vector<FLootStack> contents;
        FLootEntry::EState state = FLootEntry::EState::Available;
        FLootSlot slot{};
    };

    struct FLootWorldState
    {
        double seconds = 0.0;
        uint64_t directorRng = 1;
        uint64_t rollRng = 1;
        uint32_t pileSerial = 0;
        std::vector<FSavedLootEntry> entries;
    };

    struct FLootZone
    {
        glm::vec3 center{0.0f};
        float radius = 0.0f;
        int entries = 0;
    };

    struct FLootHandle
    {
        uint32_t index = 0;
        uint32_t nodeInstanceId = 0;
        uint32_t generation = 0;

        bool IsValid() const { return generation != 0; }
    };

    struct FLootCommitResult
    {
        bool taken = false;
        bool partial = false;        // something did not fit and was left behind
        std::vector<FLootStack> stacks;
    };

    class LootSystem
    {
    public:
        void Configure(const FLootConfig& config) { config_ = config; }
        void ConfigureRespawn(const FLootRespawnTuning& tuning) { director_.Configure(tuning); }
        void SetSeed(uint64_t seed) { seed_ = seed; }
        void SetVisualCatalog(std::unordered_map<std::string, FLootVisual> catalog, FLootVisual fallback);
        const FLootVisual& VisualForItem(std::string_view itemId) const;

        void OnSceneLoaded(Assets::Scene& scene, std::span<const FLootSpotSeed> spots = {});
        void OnSceneUnloaded();
        void ResetSession(NextEngine& engine);
        FLootWorldState Capture() const;
        void Restore(const FLootWorldState& state, NextEngine& engine);

        // Recomputes the hovered entry from player position + view direction and
        // runs restocking.
        void Update(float deltaSeconds, const glm::vec3& eyePosition, const glm::vec3& viewForward,
                    NextEngine& engine);
        // Starving or out of bandages: food / medical restock sooner and spots
        // lean toward them.
        void SetScarcity(bool foodScarce, bool medicalScarce);

        // Nearest authored loot to an approximate XZ (used to derive a safe
        // spawn on real ground next to supplies). Returns false if no loot.
        bool NearestLootPos(const glm::vec2& approxXZ, glm::vec3& outWorldPos) const;

        bool HasHovered() const { return hoveredIndex_ >= 0; }
        const FLootEntry* Hovered() const;
        int HoveredIndex() const { return hoveredIndex_; }
        std::string HoveredPrompt() const; // "AK-74" (empty if none)
        // Available entries within `radius` of the eye, nearest first.
        std::vector<int> Nearby(const glm::vec3& eyePosition, float radius) const;

        std::optional<FLootHandle> ReserveHovered();
        std::optional<FLootHandle> ReserveEntry(int index);
        FLootCommitResult Commit(const FLootHandle& handle, Inventory& inventory, NextEngine& engine,
                                 FContainerId destination = 0, EEquipSlot equipSlot = EEquipSlot::None,
                                 std::optional<FGridPlacement> placement = std::nullopt);
        bool Cancel(const FLootHandle& handle);
        bool IsReservedHandleValid(const FLootHandle& handle) const;

        // Puts a pile on the ground (player drop, infected remains). Expands the
        // visual pool as needed; occupied piles are never overwritten.
        bool SpawnPile(std::vector<FLootStack> contents, const glm::vec3& position, float yaw, NextEngine& engine);
        // Restocks one military spot far from `from` right now with a rifle and
        // ammo (radio supply cache). Returns its position.
        std::optional<glm::vec3> PlantCache(const glm::vec3& from, float minDistance, NextEngine& engine);

        int RemainingCount() const;
        int CooldownCount() const;
        int SpotCount() const;
        int PileCount() const;

        // Read-only diagnostics used by the F5 world overlay. Gameplay code
        // still mutates slots exclusively through LootSystem transactions.
        const std::vector<FLootEntry>& Entries() const { return entries_; }
        const std::vector<FLootZone>& Zones() const { return zones_; }
        const FLootSlot* ResolveSlot(FLootSlotHandle handle) const { return director_.Resolve(handle); }
        const LootDirector& Director() const { return director_; }
        double WorldSeconds() const { return worldSeconds_; }
        bool FoodScarce() const { return foodScarce_; }
        bool MedicalScarce() const { return medicalScarce_; }
        // How many entries of a category restocking may keep stocked at once.
        int CategoryCap(ELootCategory category) const;

        // Debugger only: restock a cooling spot (or every one) right now with a
        // fresh roll, ignoring cooldown, distance and visibility.
        bool DebugRestock(int index, NextEngine& engine);
        int DebugRestockAll(NextEngine& engine);

    private:
        void Rebuild(Assets::Scene& scene, std::span<const FLootSpotSeed> spots, std::span<const uint32_t> reuseNodes);
        void BuildZones();
        void RollContents(FLootEntry& entry, uint32_t roll, bool restock);
        void ShowEntry(FLootEntry& entry, Assets::Scene& scene, NextEngine* engine);
        void HideEntry(FLootEntry& entry, Assets::Scene& scene, NextEngine* engine);
        void UpdateRestock(const glm::vec3& eyePosition, const glm::vec3& viewForward, NextEngine& engine);
        const FLootVisual& VisualFor(const FLootEntry& entry) const;
        void CreatePoolNodes(Assets::Scene& scene, std::span<const uint32_t> reuse);
        static std::string DescribeContents(const std::vector<FLootStack>& contents);

        FLootConfig config_{};
        std::vector<FLootEntry> entries_;
        std::vector<FLootZone> zones_;
        std::vector<uint32_t> pileNodeIds_;   // pooled pile visuals
        int hoveredIndex_ = -1;
        uint32_t generation_ = 0;
        uint64_t seed_ = 1337;
        double worldSeconds_ = 0.0;
        float restockAccumulator_ = 0.0f;
        bool foodScarce_ = false;
        bool medicalScarce_ = false;
        uint32_t pileSerial_ = 0;
        std::unordered_map<std::string, FLootVisual> catalog_;
        FLootVisual fallbackVisual_{};
        LootDirector director_;
        FDeterministicRng rollRng_;
    };
}
