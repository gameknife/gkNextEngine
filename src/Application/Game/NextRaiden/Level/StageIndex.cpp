#include "Application/Game/NextRaiden/Level/StageIndex.hpp"

#include <algorithm>
#include <array>
#include <utility>

#include <fmt/format.h>

#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Assets/Core/Scene.hpp"
#include "Modules/ScadLoader/FScadShared.h"

namespace NextRaiden
{
    namespace
    {
        constexpr std::array<std::pair<std::string_view, EGroundKind>, 6> kGroundModules = {{
            {"nr_veh_tank", EGroundKind::Tank},
            {"nr_prop_turret", EGroundKind::Turret},
            {"nr_veh_launcher", EGroundKind::Launcher},
            {"nr_veh_patrolboat", EGroundKind::PatrolBoat},
            {"nr_bldg_bunker", EGroundKind::Bunker},
            {"nr_bldg_fueldepot", EGroundKind::FuelDepot},
        }};

        constexpr std::string_view kChunkModule = "nr_chunk";
        constexpr std::string_view kRowPrefix = "nr_row_";
        constexpr std::string_view kRackPrefabPrefix = "nr_pf_";

        void GatherChunks(const std::shared_ptr<Assets::Node>& node, std::vector<FStageChunk>& out)
        {
            for (const std::shared_ptr<Assets::Node>& child : node->Children())
            {
                if (!child)
                {
                    continue;
                }
                if (child->GetName() == kChunkModule)
                {
                    // The chunk's own module parameters are the damage model. Reading them here
                    // rather than keeping a parallel table in code means an asset edit - a part
                    // moved from stage 1 to stage 2 - changes the game without touching the game.
                    const Assets::Scad::FMetadata metadata = Assets::Scad::ParseScadMetadata(child->GetMetadata());
                    FStageChunk chunk;
                    chunk.Node = child;
                    chunk.ReleaseStage =
                        static_cast<int>(Assets::Scad::MetadataNumber(metadata, "releaseStage", 1.0));
                    chunk.HitPoints = static_cast<float>(Assets::Scad::MetadataNumber(metadata, "hitPoints", 20.0));
                    out.push_back(std::move(chunk));
                }
                GatherChunks(child, out);
            }
        }
    }

    bool ParseGroundKind(std::string_view moduleName, EGroundKind& out)
    {
        for (const auto& [name, kind] : kGroundModules)
        {
            if (name == moduleName)
            {
                out = kind;
                return true;
            }
        }
        return false;
    }

    std::string_view GroundKindName(EGroundKind kind)
    {
        for (const auto& [name, value] : kGroundModules)
        {
            if (value == kind)
            {
                return name;
            }
        }
        return "unknown";
    }

    size_t FStageIndex::TargetCount(EGroundKind kind) const
    {
        return static_cast<size_t>(
            std::count_if(Targets.begin(), Targets.end(), [kind](const FStageTarget& t) { return t.Kind == kind; }));
    }

    FStageIndex FStageIndex::Build(const Assets::Scene& scene, std::vector<std::string>* outWarnings)
    {
        FStageIndex index;

        for (const std::shared_ptr<Assets::Node>& node : scene.Nodes())
        {
            if (!node)
            {
                continue;
            }
            const std::string& name = node->GetName();

            if (name.rfind(kRowPrefix, 0) == 0)
            {
                ++index.SceneryRows;
                continue;
            }
            if (name.rfind(kRackPrefabPrefix, 0) == 0)
            {
                ++index.RackPrefabs;
                continue;
            }

            EGroundKind kind = EGroundKind::Tank;
            if (!ParseGroundKind(name, kind))
            {
                continue;
            }

            // A prefab clone carries the prefab's name, and the rack lives behind the start
            // line; a target parked back there would fire at the player from off-stage.
            FStageTarget target;
            target.Kind = kind;
            target.Node = node;
            target.Position = node->WorldTranslation();
            GatherChunks(node, target.Chunks);

            if (target.Chunks.empty() && kind != EGroundKind::FuelDepot && outWarnings)
            {
                // Not fatal - it just means this one dies in a single step instead of shedding
                // armour - but it is nearly always a gk_flatten that should not be there.
                outWarnings->push_back(
                    fmt::format("{} at z={:.0f} has no nr_chunk children; staged damage disabled for it",
                                GroundKindName(kind), target.Position.z));
            }
            index.Targets.push_back(std::move(target));
        }

        // Nearest first: the activation window walks forward through this list and never looks
        // back, which is what keeps it O(active) instead of O(stage).
        std::sort(index.Targets.begin(), index.Targets.end(),
                  [](const FStageTarget& a, const FStageTarget& b) { return a.Position.z > b.Position.z; });
        return index;
    }
}
