#include "Brotato3DGameInstance.hpp"
#include "Brotato3DCommon.hpp"
#include "Brotato3DAudio.hpp"

#include <chrono>
#include <string_view>

#include "Engine/Assets/Core/Model.hpp"
#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Assets/Data/Vertex.hpp"
#include "Engine/Runtime/Components/RenderComponent.hpp"
#include "Engine/Runtime/Engine.hpp"
#include <spdlog/spdlog.h>

// ============================================================================
// The arena's solid ground and its sight lines, taken from the SCAD scene.
//
// The SCAD loader names every node after the module call that made it, so the
// arena knows a building by its name (dd_bldg_*). Its footprint is the part of
// its geometry between knee and head height: walls, posts, a parked car's
// body. Awnings, roofs and canopies are above the band and stay walkable
// underneath; kerbs, corpses and blood are below it. FArenaNav then seals the
// interiors the walls enclose.
//
// Anything tall between the camera and the player (a building, a water
// tower, a tree crown) is dithered out while it is in the way.
// ============================================================================

using namespace Brotato3DUtil;

namespace
{
    constexpr float FootprintBandLow = 0.45f;
    constexpr float FootprintBandHigh = 1.6f;
    constexpr float OccluderMinHeight = 2.6f;
    constexpr float OccluderOpacity = 0.28f;
    constexpr float OccluderFadeRate = 10.0f;     // per second
    constexpr float OccluderSightPadding = 0.9f;  // metres around the sight line
    constexpr float FlowWindowHalf = 56.0f;
    constexpr float FlowRefreshMs = 250.0f;

    // Module names (prefixes) that stand in the way. Everything else - trees, fences,
    // hedges, lamps, signs, ground - can be walked through: the horde needs open space.
    constexpr std::array<std::string_view, 15> blockingModules = {
        "dd_bldg_",          "dd_veh_",           "dw_veh_",          "dw_prop_roadblock", "dw_ground_road_clutter",
        "dd_prop_container", "dd_prop_tank",      "dd_prop_dumpster", "dd_prop_jersey",    "dd_prop_sandbags",
        "dd_prop_windturbine_fallen", "dd_prop_billboard", "desert_mesa", "desert_rock", "oh_terrain_",
    };
    // Tall things that are not obstacles but still hide the player.
    constexpr std::array<std::string_view, 3> occludingModules = {"dd_nature_pine", "dd_nature_tree", "oh_nature_"};

    template <size_t N>
    bool MatchesAny(const std::string& name, const std::array<std::string_view, N>& prefixes)
    {
        for (const std::string_view prefix : prefixes)
        {
            if (name.compare(0, prefix.size(), prefix) == 0)
            {
                return true;
            }
        }
        return false;
    }

    // Slab test of the segment a->b against an axis-aligned box.
    bool SegmentHitsBox(const glm::vec3& a, const glm::vec3& b, const glm::vec3& boxMin, const glm::vec3& boxMax)
    {
        const glm::vec3 d = b - a;
        float t0 = 0.0f;
        float t1 = 1.0f;
        for (int axis = 0; axis < 3; ++axis)
        {
            if (std::abs(d[axis]) < 1.0e-6f)
            {
                if (a[axis] < boxMin[axis] || a[axis] > boxMax[axis])
                {
                    return false;
                }
                continue;
            }
            float enter = (boxMin[axis] - a[axis]) / d[axis];
            float exit = (boxMax[axis] - a[axis]) / d[axis];
            if (enter > exit)
            {
                std::swap(enter, exit);
            }
            t0 = std::max(t0, enter);
            t1 = std::min(t1, exit);
            if (t0 > t1)
            {
                return false;
            }
        }
        return true;
    }
}

void Brotato3DGameInstance::BuildArenaObstacles()
{
    const auto started = std::chrono::steady_clock::now();
    arenaNav_.Reset(arenaHalfExtent_, 0.5f);
    occluders_.clear();
    flowRefreshMs_ = 0.0f;

    Assets::Scene& scene = GetEngine().GetScene();
    size_t obstacleCount = 0;
    size_t triangleCount = 0;

    // Gathers the render components and world bounds of a subtree; with `rasterise`,
    // also stamps its knee-to-head triangles into the block grid.
    const auto collect = [&](auto&& self, Assets::Node& node, bool rasterise, FOccluder& occluder) -> void
    {
        if (auto* render = node.GetComponent<Runtime::RenderComponent>())
        {
            if (const Assets::Model* model = scene.GetModel(render->GetModelId()))
            {
                occluder.renders.push_back(render);
                const glm::mat4& world = node.WorldTransform();
                const std::vector<Assets::Vertex>& vertices = model->CPUVertices();
                const std::vector<uint32_t>& indices = model->CPUIndices();
                if (!vertices.empty())
                {
                    const size_t count = indices.empty() ? vertices.size() : indices.size();
                    for (size_t i = 0; i + 2 < count; i += 3)
                    {
                        glm::vec3 p[3];
                        for (int k = 0; k < 3; ++k)
                        {
                            const uint32_t index = indices.empty() ? static_cast<uint32_t>(i + k) : indices[i + k];
                            p[k] = glm::vec3(world * glm::vec4(vertices[index].Position, 1.0f));
                            occluder.boundsMin = glm::min(occluder.boundsMin, p[k]);
                            occluder.boundsMax = glm::max(occluder.boundsMax, p[k]);
                        }
                        if (!rasterise)
                        {
                            continue;
                        }
                        const float low = std::min(p[0].y, std::min(p[1].y, p[2].y));
                        const float high = std::max(p[0].y, std::max(p[1].y, p[2].y));
                        if (high < FootprintBandLow || low > FootprintBandHigh)
                        {
                            continue;
                        }
                        arenaNav_.BlockTriangle(glm::vec2(p[0].x, p[0].z), glm::vec2(p[1].x, p[1].z),
                                                glm::vec2(p[2].x, p[2].z));
                        ++triangleCount;
                    }
                }
            }
        }
        for (const auto& child : node.Children())
        {
            if (child)
            {
                self(self, *child, rasterise, occluder);
            }
        }
    };

    const auto visit = [&](auto&& self, Assets::Node& node) -> void
    {
        const std::string& name = node.GetName();
        const bool blocking = MatchesAny(name, blockingModules);
        if (blocking || MatchesAny(name, occludingModules))
        {
            FOccluder occluder;
            occluder.boundsMin = glm::vec3(std::numeric_limits<float>::max());
            occluder.boundsMax = glm::vec3(std::numeric_limits<float>::lowest());
            collect(collect, node, blocking, occluder);
            obstacleCount += blocking ? 1 : 0;
            // Flattened road clutter is one node hundreds of metres long: never fade it.
            const glm::vec3 size = occluder.boundsMax - occluder.boundsMin;
            if (!occluder.renders.empty() && size.y >= OccluderMinHeight && std::max(size.x, size.z) < 80.0f)
            {
                occluders_.push_back(std::move(occluder));
            }
            return;
        }
        for (const auto& child : node.Children())
        {
            if (child)
            {
                self(self, *child);
            }
        }
    };

    for (const std::shared_ptr<Assets::Node>& node : scene.Nodes())
    {
        // Our own nodes (player, zombies, pools) are named Brotato3D_* and never match.
        if (node && !node->GetParent())
        {
            visit(visit, *node);
        }
    }
    arenaNav_.FinishBuild(glm::vec2(0.0f));

    const float elapsedMs = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - started).count();
    spdlog::info("[Brotato3D] arena obstacles: {} modules, {} footprint triangles, {:.1f}% of the arena solid, "
                 "{} occluders, built in {:.0f} ms",
                 obstacleCount, triangleCount, arenaNav_.BlockedFraction() * 100.0f, occluders_.size(), elapsedMs);
}

void Brotato3DGameInstance::ClearArenaObstacles()
{
    // The render components belong to the scene that is going away.
    occluders_.clear();
    arenaNav_.Reset(arenaHalfExtent_, 0.5f);
}

void Brotato3DGameInstance::UpdateArenaNavigation(double deltaSeconds)
{
    if (!arenaNav_.IsBuilt())
    {
        return;
    }
    flowRefreshMs_ -= static_cast<float>(deltaSeconds * 1000.0);
    if (flowRefreshMs_ <= 0.0f)
    {
        arenaNav_.UpdateFlowField(player_.worldPos, FlowWindowHalf);
        flowRefreshMs_ = FlowRefreshMs;
    }
}

glm::vec3 Brotato3DGameInstance::ChaseDirection(Brotato3D::FEnemyRuntime& enemy, const glm::vec3& toPlayerDir,
                                                double deltaSeconds)
{
    glm::vec3 desired = toPlayerDir;
    enemy.seesPlayer = !arenaNav_.IsBuilt() || arenaNav_.HasLineOfSight(enemy.worldPos, player_.worldPos);
    if (!enemy.seesPlayer)
    {
        glm::vec3 flow(0.0f);
        if (arenaNav_.FlowDirection(enemy.worldPos, flow))
        {
            desired = flow;
        }
    }
    // Steer, do not snap: eight-way flow cells would otherwise read as a robot's path.
    const float blend = 1.0f - std::exp(-9.0f * static_cast<float>(deltaSeconds));
    glm::vec3 steered = glm::length(enemy.navDir) > 0.01f ? glm::mix(enemy.navDir, desired, blend) : desired;
    steered.y = 0.0f;
    enemy.navDir = glm::length(steered) > 1.0e-4f ? glm::normalize(steered) : desired;
    return enemy.navDir;
}

void Brotato3DGameInstance::UpdateOccluderFade(double deltaSeconds)
{
    if (occluders_.empty())
    {
        return;
    }
    glm::vec3 cameraPosition = cameraSmoothedTarget_ + glm::vec3(0.0f, CameraHeight, CameraBack);
    if (validation_.cameraHeight > 0.0f)
    {
        cameraPosition = glm::vec3(cameraSmoothedTarget_.x, 0.9f, cameraSmoothedTarget_.z) +
                         glm::vec3(0.0f, validation_.cameraHeight, validation_.cameraBack);
    }
    const bool playing = appState_ != Brotato3D::EAppState::MainMenu && appState_ != Brotato3D::EAppState::CharacterSelect;
    const glm::vec3 eye = player_.worldPos + glm::vec3(0.0f, 0.5f, 0.0f);
    const glm::vec3 lineMin = glm::min(cameraPosition, eye) - glm::vec3(OccluderSightPadding);
    const glm::vec3 lineMax = glm::max(cameraPosition, eye) + glm::vec3(OccluderSightPadding);
    const float step = std::min(1.0f, OccluderFadeRate * static_cast<float>(deltaSeconds));
    for (FOccluder& occluder : occluders_)
    {
        bool hiding = false;
        if (playing && Brotato3D::FadeOccluders &&
            glm::all(glm::lessThanEqual(occluder.boundsMin, lineMax)) &&
            glm::all(glm::greaterThanEqual(occluder.boundsMax, lineMin)))
        {
            const glm::vec3 pad(OccluderSightPadding, 0.0f, OccluderSightPadding);
            hiding = SegmentHitsBox(eye, cameraPosition, occluder.boundsMin - pad, occluder.boundsMax + pad);
        }
        const float target = hiding ? OccluderOpacity : 1.0f;
        if (std::abs(occluder.opacity - target) < 1.0e-3f)
        {
            continue;
        }
        occluder.opacity = glm::mix(occluder.opacity, target, step);
        if (std::abs(occluder.opacity - target) < 0.01f)
        {
            occluder.opacity = target;
        }
        for (Runtime::RenderComponent* render : occluder.renders)
        {
            render->SetMainOpacity(occluder.opacity);
        }
    }
}
