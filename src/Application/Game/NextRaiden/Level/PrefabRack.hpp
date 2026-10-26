#pragma once

// ============================================================================
// PrefabRack.hpp - Turns the stage .scad's "rack" into runtime object pools.
//
// The rack (nr_rack() in nr_stage_common.scad) holds exactly one instance of
// every nr_pf_* prefab. The engine parses the .scad *before* it calls
// BeforeSceneRebuild, so by the time the game sees the node array the prefabs
// are there with their models already loaded. Clone() deep-copies a prefab's
// subtree as many times as a pool needs, sharing the source's model ids: the
// geometry is never duplicated, only the nodes are.
//
// Everything a pool owns is created here, at scene-build time. Nothing in this
// game adds or removes a node while the scene is live - that is what keeps the
// GPU buffers from being rebuilt mid-frame (design section 5.3).
// ============================================================================

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "Engine/Assets/AssetsFwd.hpp"

namespace NextRaiden
{
    /// Prefab module names live in one place so a typo is a link error, not a silently
    /// empty pool. These must match the module names in nr_stage_common.scad.
    namespace Prefabs
    {
        inline constexpr const char* kPlayer = "nr_pf_player";
        inline constexpr const char* kWingman = "nr_pf_wingman";
        inline constexpr const char* kScout = "nr_pf_scout";
        inline constexpr const char* kInterceptor = "nr_pf_interceptor";
        inline constexpr const char* kBomber = "nr_pf_bomber";
        inline constexpr const char* kBolt = "nr_pf_bolt";
        inline constexpr const char* kBoltHostile = "nr_pf_bolt_hostile";
        inline constexpr const char* kOrb = "nr_pf_orb";
        inline constexpr const char* kNeedle = "nr_pf_needle";
        inline constexpr const char* kMissile = "nr_pf_missile";
        inline constexpr const char* kBomb = "nr_pf_bomb";
        inline constexpr const char* kLaser = "nr_pf_laser";
        inline constexpr const char* kItemPower = "nr_pf_item_power";
        inline constexpr const char* kItemMedal = "nr_pf_item_medal";
        inline constexpr const char* kItemRepair = "nr_pf_item_repair";
        inline constexpr const char* kItemBomb = "nr_pf_item_bomb";
        inline constexpr const char* kItemOption = "nr_pf_item_option";
        inline constexpr const char* kFxBlast = "nr_pf_fx_blast";
        inline constexpr const char* kFxSmoke = "nr_pf_fx_smoke";
        inline constexpr const char* kFxFlash = "nr_pf_fx_flash";
        inline constexpr const char* kFxCrater = "nr_pf_fx_crater";
        inline constexpr const char* kPartArmour = "nr_pf_part_armour";
        inline constexpr const char* kPartWing = "nr_pf_part_wing";
        inline constexpr const char* kPartEngine = "nr_pf_part_engine";
        inline constexpr const char* kPartCanopy = "nr_pf_part_canopy";
        inline constexpr const char* kPartFin = "nr_pf_part_fin";
        inline constexpr const char* kPartPod = "nr_pf_part_pod";
        inline constexpr const char* kPartGun = "nr_pf_part_gun";
    }

    class FPrefabRack
    {
    public:
        /// Scans the freshly parsed node array for nr_pf_* roots and hides them. Call this
        /// first thing in BeforeSceneRebuild; the nodes have no world transform and no scene
        /// yet, so indexing and visibility flags are the only safe work here.
        void Index(const std::vector<std::shared_ptr<Assets::Node>>& nodes);

        bool Has(const std::string& prefab) const { return prefabs_.count(prefab) != 0; }
        size_t PrefabCount() const { return prefabs_.size(); }

        /// Appends `count` hidden clones of `prefab` to `nodes`, parked at `park`.
        /// Returns the clone roots - the game keeps these and drives their transforms.
        /// An unknown prefab warns once and returns an empty vector, which shows up as a
        /// pool that never spawns rather than as a crash.
        std::vector<std::shared_ptr<Assets::Node>> Clone(const std::string& prefab, int count,
                                                         std::vector<std::shared_ptr<Assets::Node>>& nodes,
                                                         const glm::vec3& park);

        /// Where pooled objects wait: far behind the player's start, on the ground plane, so
        /// the scene's bounding box grows along the axis the stage already spans.
        static glm::vec3 ParkPosition(int slot);

    private:
        std::shared_ptr<Assets::Node> CloneSubtree(const Assets::Node& source,
                                                   const std::shared_ptr<Assets::Node>& parent, bool isRoot,
                                                   const glm::vec3& park,
                                                   std::vector<std::shared_ptr<Assets::Node>>& nodes);

        std::unordered_map<std::string, std::shared_ptr<Assets::Node>> prefabs_;
        /// Instance ids must stay unique across the whole scene - Scene indexes nodes by them.
        uint32_t nextInstanceId_ = 1;
    };
}
