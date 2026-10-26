#pragma once

// ============================================================================
// StageIndex.hpp - What the stage's .scad actually put in the world.
//
// The scene is the level: ground targets, buildings and scenery rows are kit
// module calls, and the loader turns each call into a named node. This walks the
// committed scene once and buckets those nodes, so the systems that drive them
// never have to search again.
//
// Chunked targets keep their nr_chunk children, each carrying the asset author's
// own releaseStage / hitPoints metadata. That metadata is the damage model - the
// game does not keep a second table of what comes off when.
// ============================================================================

#include <memory>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "Application/Game/NextRaiden/NextRaidenTypes.hpp"
#include "Engine/Assets/AssetsFwd.hpp"

namespace NextRaiden
{
    enum class EGroundKind : uint8_t
    {
        Tank,
        Turret,
        Launcher,
        PatrolBoat,
        Bunker,
        FuelDepot,
        Count
    };

    bool ParseGroundKind(std::string_view moduleName, EGroundKind& out);
    std::string_view GroundKindName(EGroundKind kind);

    /// One detachable piece of a ground target, as the .scad authored it.
    struct FStageChunk
    {
        std::shared_ptr<Assets::Node> Node;
        int ReleaseStage = 1;   // 1 outer armour, 2 systems, 3 last core
        float HitPoints = 20.0f;
        bool Detached = false;
    };

    struct FStageTarget
    {
        EGroundKind Kind = EGroundKind::Tank;
        std::shared_ptr<Assets::Node> Node;
        glm::vec3 Position{0.0f};
        std::vector<FStageChunk> Chunks;   // empty for a flattened module
    };

    struct FStageIndex
    {
        /// Ground and sea targets, sorted by depth so the activation walk is a moving window
        /// rather than a scan of the whole stage every step. Sorted descending in Z, i.e.
        /// nearest the player's start first.
        std::vector<FStageTarget> Targets;
        /// Scenery rows, kept only for reporting and tests: nothing drives them.
        int SceneryRows = 0;
        int RackPrefabs = 0;

        static FStageIndex Build(const Assets::Scene& scene, std::vector<std::string>* outWarnings = nullptr);
        size_t TargetCount(EGroundKind kind) const;
    };
}
