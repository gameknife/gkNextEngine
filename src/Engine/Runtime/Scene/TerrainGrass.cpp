#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Runtime/Scene/TerrainGrass.hpp"
#include "Engine/Assets/Core/Model.hpp"
#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Runtime/Components/RenderComponent.hpp"
#include "Engine/Runtime/Components/TerrainBiome.hpp"
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>

namespace Runtime
{
    namespace
    {
        constexpr std::array<int, 3> grassSamples = {14, 4, 2};
        constexpr std::array<uint32_t, 3> grassBlades = {12, 36, 64};
        constexpr std::array<float, 3> grassPatchRadius = {0.45f, 1.35f, 2.5f};
        constexpr std::array<float, 3> grassClearance = {0.85f, 1.95f, 3.5f};

        float GrassTransmission(float strength, uint32_t biome, uint32_t slot)
        {
            return std::clamp(strength, 0.0f, 1.0f) * (slot % 2 == 0 ? 0.4f : 1.0f) *
                   (biome == 2 ? 0.65f : 1.0f);
        }

        uint32_t GrassHash(uint32_t value)
        {
            value ^= value >> 16;
            value *= 0x7feb352du;
            value ^= value >> 15;
            value *= 0x846ca68bu;
            return value ^ (value >> 16);
        }

        glm::vec2 GrassLodRange(float radius, uint32_t lod)
        {
            if (lod == 0) return {0.0f, std::min(radius, 48.0f)};
            if (lod == 1) return {36.0f, std::min(radius, 96.0f)};
            return {80.0f, radius};
        }

        float GrassRandom(uint32_t seed, uint32_t channel)
        {
            return float(GrassHash(seed ^ (channel * 0x9e3779b9u)) >> 8) / 16777216.0f;
        }

        bool GrassSurface(const TerrainComponent& terrain, float x, float z)
        {
            const auto& data = terrain.GetGridData();
            if (!terrain.HasData()) return false;
            const glm::dvec4 local = data->worldToLocal * glm::dvec4(x, 0.0, z, 1.0);
            // ContainsWorld intentionally allows border slack for gameplay; growth must not.
            if (std::abs(local.x) >= data->sizeX * 0.5 || std::abs(local.y) >= data->sizeY * 0.5)
                return false;
            return terrain.BiomeId(x, z) <= static_cast<uint8_t>(ETerrainBiome::DryGrass) &&
                   terrain.SurfaceFlags(x, z) == 0;
        }
    }

    float FTerrainGrassSystem::EdgeDistance(const TerrainComponent& terrain, float worldX, float worldZ,
                                           float maxDistance)
    {
        if (maxDistance <= 0.0f || !GrassSurface(terrain, worldX, worldZ)) return 0.0f;
        const auto& grid = *terrain.GetGridData();
        const glm::dvec4 local = grid.worldToLocal * glm::dvec4(worldX, 0.0, worldZ, 1.0);
        const double scaleX = glm::length(glm::dvec2(grid.localToWorld[0].x, grid.localToWorld[0].z));
        const double scaleY = glm::length(glm::dvec2(grid.localToWorld[1].x, grid.localToWorld[1].z));
        if (grid.cellsX <= 0 || grid.cellsY <= 0 || scaleX < 1.0e-9 || scaleY < 1.0e-9) return 0.0f;
        const double cellX = grid.sizeX / grid.cellsX;
        const double cellY = grid.sizeY / grid.cellsY;
        if (cellX <= 0.0 || cellY <= 0.0) return 0.0f;
        const double hx = grid.sizeX * 0.5;
        const double hy = grid.sizeY * 0.5;
        const auto cellIndex = [](double coordinate, double halfSize, double cellSize, int count)
        {
            return static_cast<int>(std::clamp(std::floor((coordinate + halfSize) / cellSize), 0.0, double(count - 1)));
        };
        const int x0 = cellIndex(local.x - maxDistance / scaleX, hx, cellX, grid.cellsX);
        const int x1 = cellIndex(local.x + maxDistance / scaleX, hx, cellX, grid.cellsX);
        const int y0 = cellIndex(local.y - maxDistance / scaleY, hy, cellY, grid.cellsY);
        const int y1 = cellIndex(local.y + maxDistance / scaleY, hy, cellY, grid.cellsY);
        double nearestSquared = double(maxDistance) * maxDistance;
        for (int y = y0; y <= y1; ++y)
        {
            for (int x = x0; x <= x1; ++x)
            {
                const size_t cell = size_t(y) * grid.cellsX + x;
                if (grid.biome[cell] <= uint8_t(ETerrainBiome::DryGrass) && grid.flags[cell] == 0) continue;
                // Distance to the semantic cell's rectangle, not its centre: this stays
                // continuous across cells and avoids a stepped, grid-shaped transition.
                const double left = -hx + x * cellX;
                const double bottom = -hy + y * cellY;
                const double dx = std::max({left - local.x, 0.0, local.x - left - cellX}) * scaleX;
                const double dy = std::max({bottom - local.y, 0.0, local.y - bottom - cellY}) * scaleY;
                nearestSquared = std::min(nearestSquared, dx * dx + dy * dy);
            }
        }
        return static_cast<float>(std::sqrt(nearestSquared));
    }

    std::vector<FGrassPlacement> FTerrainGrassSystem::GenerateTile(
        std::span<const TerrainComponent* const> terrains, int tileX, int tileZ, float density, uint32_t lod,
        float edgeWidth)
    {
        std::vector<FGrassPlacement> result;
        if (lod >= lodCount) return result;
        const int samples = grassSamples[lod];
        const float spacing = tileSize / samples;
        for (int z = 0; z < samples; ++z)
        {
            for (int x = 0; x < samples; ++x)
            {
                const int gx = tileX * samples + x;
                const int gz = tileZ * samples + z;
                const uint32_t cellSeed = GrassHash(uint32_t(gx) * 73856093u ^ uint32_t(gz) * 19349663u);
                const float px = (float(gx) + 0.15f + 0.7f * GrassRandom(cellSeed, 1)) * spacing;
                const float pz = (float(gz) + 0.15f + 0.7f * GrassRandom(cellSeed, 2)) * spacing;
                for (const TerrainComponent* terrain : terrains)
                {
                    if (!GrassSurface(*terrain, px, pz)) continue;
                    const uint64_t terrainSeed = terrain->GetGridData()->seed;
                    const uint32_t seed = GrassHash(cellSeed ^ uint32_t(terrainSeed) ^ uint32_t(terrainSeed >> 32));
                    const uint8_t biome = terrain->BiomeId(px, pz);
                    const float biomeDensity = biome == uint8_t(ETerrainBiome::DryGrass) ? 0.55f : 1.0f;
                    // Broad meadow variation, plus independently thinned clumps (no grid rows).
                    const float patch = 0.78f + 0.22f * std::sin(px * 0.13f + std::sin(pz * 0.17f));
                    const float probability = std::clamp(density, 0.0f, 1.0f) * biomeDensity * patch;
                    const float random = GrassRandom(seed, 3);
                    if (random >= probability) break;
                    const glm::vec3 normal = terrain->SampleNormal(px, pz);
                    if (normal.y < 0.866f) break; // >30 degrees
                    // A large rigid patch must fit both the biome footprint and the actual
                    // terrain plane. Reject folds instead of floating grass over roads/ridges.
                    const float ground = terrain->SampleHeight(px, pz);
                    bool fits = true;
                    for (uint32_t probe = 0; probe < 8; ++probe)
                    {
                        const float angle = float(probe) * glm::two_pi<float>() / 8.0f;
                        const glm::vec2 offset(std::cos(angle) * grassClearance[lod],
                                               std::sin(angle) * grassClearance[lod]);
                        if (!GrassSurface(*terrain, px + offset.x, pz + offset.y))
                        {
                            fits = false;
                            break;
                        }
                        if (lod > 0)
                        {
                            const float planeHeight = ground - (normal.x * offset.x + normal.z * offset.y) / normal.y;
                            const float sampled = terrain->SampleHeight(px + offset.x, pz + offset.y);
                            if (std::abs(sampled - planeHeight) > (lod == 1 ? 0.22f : 0.35f))
                            {
                                fits = false;
                                break;
                            }
                        }
                    }
                    if (!fits) break;
                    float edgeWeight = 1.0f;
                    if (edgeWidth > 0.0f)
                    {
                        const float limit = grassClearance[lod] + edgeWidth;
                        const float distance = EdgeDistance(*terrain, px, pz, limit);
                        edgeWeight = glm::smoothstep(grassClearance[lod], limit, distance);
                        // Reuse the same random threshold so feathered grass is a stable
                        // subset of the original distribution, never a newly shuffled field.
                        if (random >= probability * edgeWeight) break;
                    }
                    FGrassPlacement placement;
                    placement.position = {px, ground - 0.025f, pz};
                    placement.normal = normal;
                    const float width = lod == 0 ? 0.8f + GrassRandom(seed, 4) * 0.65f
                                                 : 0.88f + GrassRandom(seed, 4) * 0.28f;
                    const float height = (0.7f + GrassRandom(seed, 5) * 0.85f) * (biome == 2 ? 0.85f : 1.0f);
                    placement.scale = {width, height * glm::mix(0.35f, 1.0f, edgeWeight), width};
                    placement.edgeWeight = edgeWeight;
                    placement.heading = (GrassRandom(seed, 6) - 0.5f) * 1.4f;
                    placement.phase = GrassRandom(seed, 7) * glm::two_pi<float>();
                    placement.biome = biome;
                    placement.lod = static_cast<uint8_t>(lod);
                    placement.seed = seed;
                    result.push_back(placement);
                    break; // one owner at tile seams / overlapping terrains
                }
            }
        }
        return result;
    }

    float FTerrainGrassSystem::Coverage(float distance, float radius, uint32_t lod)
    {
        const float nearToMid = glm::smoothstep(36.0f, 48.0f, distance);
        const float midToFar = glm::smoothstep(80.0f, 96.0f, distance);
        const float cutoff = 1.0f - glm::smoothstep(std::max(0.0f, radius - 16.0f), radius, distance);
        if (lod == 0) return (1.0f - nearToMid) * cutoff;
        if (lod == 1) return nearToMid * (1.0f - midToFar) * cutoff;
        if (lod == 2) return midToFar * cutoff;
        return 0.0f;
    }

    uint32_t FTerrainGrassSystem::SelectPose(float bend, uint32_t previous)
    {
        const float sample = std::clamp(bend, 0.0f, 1.0f) * float(poseCount - 1);
        if (previous < poseCount && std::abs(sample - float(previous)) <= 0.65f) return previous;
        return static_cast<uint32_t>(std::round(sample));
    }

    Assets::Model FTerrainGrassSystem::CreatePose(uint32_t pose, uint32_t lod)
    {
        std::vector<Assets::Vertex> vertices;
        std::vector<uint32_t> indices;
        lod = std::min(lod, lodCount - 1);
        const float bend = float(std::min(pose, poseCount - 1)) / float(poseCount - 1) * 0.42f;
        const auto triangle = [&](glm::vec3 a, glm::vec3 b, glm::vec3 c, uint32_t material)
        {
            const glm::vec3 normal = glm::normalize(glm::cross(b - a, c - a));
            const glm::vec3 tangent = glm::normalize(b - a);
            for (const glm::vec3 point : {a, b, c})
            {
                indices.push_back(static_cast<uint32_t>(vertices.size()));
                vertices.push_back({point, normal, glm::vec4(tangent, 1.0f), {0.0f, point.y}, material});
            }
        };
        for (uint32_t blade = 0; blade < grassBlades[lod]; ++blade)
        {
            const float angle = GrassRandom(blade + 19, 1) * glm::two_pi<float>();
            const glm::vec3 root = glm::vec3(std::cos(angle), 0.0f, std::sin(angle)) *
                                   (lod == 0 ? 0.08f + 0.37f * GrassRandom(blade, 2)
                                             : grassPatchRadius[lod] * std::sqrt(GrassRandom(blade, 2)));
            const float height = 0.26f + 0.31f * GrassRandom(blade, 3);
            const float heading = GrassRandom(blade, 4) * glm::two_pi<float>();
            const glm::vec3 widthAxis(std::cos(heading), 0.0f, std::sin(heading));
            const glm::vec3 depthAxis(-std::sin(heading), 0.0f, std::cos(heading));
            const glm::vec3 naturalLean = glm::vec3(std::sin(angle), 0.0f, std::cos(angle)) * 0.13f;
            const glm::vec3 lean = naturalLean + glm::vec3(bend, 0, 0);
            const float width = (0.045f + GrassRandom(blade, 5) * 0.035f) * (1.0f + float(lod) * 0.55f);
            const float depth = (0.03f + GrassRandom(blade, 6) * 0.025f) * (1.0f + float(lod) * 0.3f);
            const auto center = [&](float t)
            {
                return root + glm::vec3(0.0f, height * t * (1.0f - bend * t * 0.18f), 0.0f) +
                       lean * height * t * t;
            };
            std::array<glm::vec3, 3> base;
            std::array<glm::vec3, 3> middle;
            for (uint32_t corner = 0; corner < 3; ++corner)
            {
                const float angleAround = float(corner) * glm::two_pi<float>() / 3.0f;
                const glm::vec3 section = widthAxis * (std::cos(angleAround) * width) +
                                          depthAxis * (std::sin(angleAround) * depth);
                base[corner] = root + section;
                middle[corner] = center(0.55f) + section * 0.65f;
            }
            const uint32_t material = (blade % 2) * 2;
            // Closed, flat-shaded triangular stalk: 6 lower faces + 3 tip faces + root cap.
            // It has real volume instead of coincident front/back ribbons, at the same 10 tris.
            triangle(base[0], base[1], base[2], material);
            for (uint32_t corner = 0; corner < 3; ++corner)
            {
                const uint32_t next = (corner + 1) % 3;
                if (lod == 0)
                {
                    triangle(base[corner], middle[corner], base[next], material);
                    triangle(base[next], middle[corner], middle[next], material);
                    triangle(middle[corner], center(1.0f), middle[next], material + 1);
                }
                else
                {
                    // Large distant patches keep the closed low-poly silhouette at 4 tris/leaf.
                    triangle(base[corner], center(1.0f), base[next], material + (corner % 2));
                }
            }
        }
        auto model = Assets::Model::CreateFromGeometry(fmt::format("terrain_grass_lod{}_pose_{}", lod, pose),
                                                       std::move(vertices), std::move(indices), false);
        model.SetGenerateLods(false);
        return model;
    }

    void FTerrainGrassSystem::Prepare(std::span<const std::shared_ptr<Assets::Node>> nodes,
                                      std::vector<Assets::Model>& models, std::vector<Assets::FMaterial>& materials)
    {
        Clear();
        prepared_ = false;
        if (std::none_of(nodes.begin(), nodes.end(), [](const auto& node)
            { return node && node->template GetComponent<TerrainComponent>(); })) return;
        for (uint32_t lod = 0; lod < lodCount; ++lod)
        {
            for (uint32_t pose = 0; pose < poseCount; ++pose)
            {
                models_[lod][pose] = static_cast<uint32_t>(models.size());
                models.push_back(CreatePose(pose, lod));
            }
        }
        // Muted olive / grey-green / dry khaki, with similar luminance to the old palette.
        const std::array<glm::vec3, 3> colors = {
            glm::vec3(0.23f, 0.275f, 0.185f), {0.155f, 0.205f, 0.145f}, {0.34f, 0.31f, 0.235f}};
        for (uint32_t biome = 0; biome < colors.size(); ++biome)
        {
            for (uint32_t slot = 0; slot < 4; ++slot)
            {
                materials_[biome][slot] = static_cast<uint32_t>(materials.size());
                const float shade = (slot % 2 == 0 ? 0.82f : 1.12f) * (slot < 2 ? 1.0f : 0.90f);
                materials.push_back({Assets::Material::Foliage(colors[biome] * shade,
                                         GrassTransmission(settings.transmission, biome, slot),
                                         slot % 2 == 0 ? 0.14f : 0.08f),
                                     fmt::format("terrain_grass_{}_{}", biome, slot)});
            }
        }
        prepared_ = true;
    }

    void FTerrainGrassSystem::Clear()
    {
        terrains_.clear();
        tiles_.clear();
        pool_.clear();
        free_.clear();
        activeCount_ = 0;
        activeByLod_.fill(0);
        maxActiveDistance_ = 0.0f;
        edgeCount_ = 0;
        poseChanges_ = 0;
        tileBuilds_ = 0;
        time_ = 0;
        updateClock_ = 0;
        lastFocus_ = glm::vec3(1.0e20f);
        lastDensity_ = lastRadius_ = -1.0f;
        lastEdgeWidth_ = -1.0f;
        lastTransmission_ = -1.0f;
        lastEnabled_ = settings.enabled;
    }

    void FTerrainGrassSystem::Bind(Assets::Scene& scene)
    {
        terrains_.clear();
        if (!prepared_) return;
        for (const auto* terrain : scene.Components<TerrainComponent>())
            if (terrain->HasData()) terrains_.push_back(terrain);
        windYaw_ = settings.windYaw;
    }

    void FTerrainGrassSystem::Stream(Assets::Scene& scene, const glm::vec3& focus, float radius)
    {
        const int cx = static_cast<int>(std::floor(focus.x / tileSize));
        const int cz = static_cast<int>(std::floor(focus.z / tileSize));
        const int extent = static_cast<int>(std::ceil(radius / tileSize));
        const bool densityChanged = settings.density != lastDensity_ || settings.edgeWidth != lastEdgeWidth_;
        for (auto it = tiles_.begin(); it != tiles_.end();)
        {
            const auto [lod, x, z] = it->first;
            const glm::vec2 range = GrassLodRange(radius, lod);
            const glm::vec2 delta = glm::vec2((x + 0.5f) * tileSize - focus.x, (z + 0.5f) * tileSize - focus.z);
            const float distance = glm::length(delta);
            if (densityChanged || !settings.enabled || range.y <= range.x ||
                distance > range.y + tileSize * 1.5f || distance < range.x - tileSize * 1.5f)
            {
                for (uint32_t slot : it->second)
                {
                    pool_[slot].node->GetComponent<RenderComponent>()->SetVisible(false);
                    free_.push_back(slot);
                }
                it = tiles_.erase(it);
            }
            else ++it;
        }
        std::vector<std::array<int, 3>> requested;
        if (settings.enabled)
        {
            for (uint32_t lod = 0; lod < lodCount; ++lod)
            {
                const glm::vec2 range = GrassLodRange(radius, lod);
                if (range.y <= range.x) continue;
                for (int z = cz - extent; z <= cz + extent; ++z)
                    for (int x = cx - extent; x <= cx + extent; ++x)
                    {
                        const std::array<int, 3> tile{static_cast<int>(lod), x, z};
                        const float distance = glm::length(glm::vec2((x + 0.5f) * tileSize - focus.x,
                                                                       (z + 0.5f) * tileSize - focus.z));
                        if (!tiles_.contains(tile) && distance <= range.y + tileSize * 0.71f &&
                            distance >= range.x - tileSize * 0.71f)
                            requested.push_back(tile);
                    }
            }
        }
        std::sort(requested.begin(), requested.end(), [&](auto a, auto b)
        {
            if (a[0] != b[0]) return a[0] < b[0];
            const auto distance = [&](auto tile)
            {
                return glm::length2(glm::vec2((tile[1] + 0.5f) * tileSize - focus.x,
                                               (tile[2] + 0.5f) * tileSize - focus.z));
            };
            return distance(a) < distance(b);
        });
        for (auto tile : requested)
        {
            const auto placements = GenerateTile(terrains_, tile[1], tile[2], settings.density, tile[0], settings.edgeWidth);
            auto& slots = tiles_[tile];
            ++tileBuilds_;
            for (const auto& placement : placements)
            {
                uint32_t slot;
                if (!free_.empty())
                {
                    slot = free_.back();
                    free_.pop_back();
                }
                else
                {
                    if (pool_.size() >= maxInstances) break;
                    slot = static_cast<uint32_t>(pool_.size());
                    auto node = Assets::Node::CreateNode(fmt::format("RuntimeGrass_{}", slot), {},
                                                         glm::quat(1, 0, 0, 0), glm::vec3(1), scene.GenerateInstanceId());
                    auto render = std::make_shared<RenderComponent>();
                    render->SetModelId(models_[placement.lod][0]);
                    render->SetRayCastVisible(false);
                    render->SetReceiveGI(false); // do not bake a moving meadow into the GI cache
                    node->AddComponent(render);
                    node->SetTag("RuntimeGrass");
                    scene.AddNode(node);
                    pool_.push_back({std::move(node), {}, 0});
                }
                pool_[slot].placement = placement;
                pool_[slot].node->GetComponent<RenderComponent>()->SetMaterials(materials_[placement.biome]);
                slots.push_back(slot);
            }
        }
        lastFocus_ = focus;
        lastDensity_ = settings.density;
        lastEdgeWidth_ = settings.edgeWidth;
        lastRadius_ = radius;
        lastEnabled_ = settings.enabled;
    }

    void FTerrainGrassSystem::Tick(Assets::Scene& scene, float dt, const glm::vec3& focus, float weather)
    {
        if (!prepared_ || terrains_.empty()) return;
        const float transmission = std::clamp(settings.transmission, 0.0f, 1.0f);
        if (transmission != lastTransmission_)
        {
            for (uint32_t biome = 0; biome < materials_.size(); ++biome)
                for (uint32_t slot = 0; slot < 4; ++slot)
                    scene.Materials()[materials_[biome][slot]].gpuMaterial_.Reserved2 =
                        GrassTransmission(transmission, biome, slot);
            scene.MarkMaterialsDirty();
            lastTransmission_ = transmission;
        }
        const float radius = std::clamp(settings.radius, 8.0f, maxRadius);
        time_ += std::max(0.0f, dt) * std::clamp(settings.windSpeed, 0.0f, 4.0f);
        updateClock_ += std::max(0.0f, dt);
        const bool stream = glm::length2(glm::vec2(focus.x - lastFocus_.x, focus.z - lastFocus_.z)) > 1.0f ||
                            lastDensity_ != settings.density || lastRadius_ != radius ||
                            lastEdgeWidth_ != settings.edgeWidth ||
                            lastEnabled_ != settings.enabled ||
                            (!settings.enabled && !tiles_.empty());
        if (!stream && updateClock_ < 0.05f) return;
        const float elapsed = updateClock_;
        updateClock_ = 0;
        if (stream) Stream(scene, focus, radius);
        const float targetYaw = settings.windYaw + 0.16f * std::sin(float(time_) * 0.12f);
        windYaw_ += std::atan2(std::sin(targetYaw - windYaw_), std::cos(targetYaw - windYaw_)) *
                    std::min(1.0f, elapsed * 0.6f);
        activeCount_ = 0;
        activeByLod_.fill(0);
        maxActiveDistance_ = 0.0f;
        edgeCount_ = 0;
        for (const auto& [tile, slots] : tiles_)
        {
            for (uint32_t slot : slots)
            {
                auto& instance = pool_[slot];
                const auto& p = instance.placement;
                auto* render = instance.node->GetComponent<RenderComponent>();
                const float distance = glm::length(glm::vec2(p.position.x - focus.x, p.position.z - focus.z));
                const float fade = Coverage(distance, radius, p.lod);
                const bool visible = settings.enabled && fade > 0.015f;
                render->SetVisible(visible);
                if (!visible) continue;
                ++activeCount_;
                ++activeByLod_[p.lod];
                if (p.edgeWeight < 0.999f) ++edgeCount_;
                maxActiveDistance_ = std::max(maxActiveDistance_, distance);
                const float alongWind = p.position.x * std::cos(windYaw_) - p.position.z * std::sin(windYaw_);
                const float phase = float(time_) * 1.8f - alongWind * 0.12f;
                const float gust = 0.55f + 0.24f * std::sin(phase) + 0.09f * std::sin(float(time_) * 2.9f + p.phase);
                const float bend = std::clamp((settings.windStrength + weather * 0.3f) * gust, 0.0f, 1.0f);
                const uint32_t pose = SelectPose(bend, instance.pose);
                if (pose != instance.pose) ++poseChanges_;
                instance.pose = pose;
                render->SetModelId(models_[p.lod][pose]);
                const glm::quat align = glm::rotation(glm::vec3(0, 1, 0), p.normal);
                const glm::quat yaw = glm::angleAxis(windYaw_ + p.heading, glm::vec3(0, 1, 0));
                // Sub-pose residual rotation softens quantisation without modifying mesh/BLAS.
                const float residual = bend - float(pose) / float(poseCount - 1);
                const glm::quat sway = glm::angleAxis(-residual * 0.35f, glm::vec3(0, 0, 1));
                instance.node->SetTransform(p.position, align * yaw * sway,
                                             p.scale * glm::vec3(1.0f, fade, 1.0f));
            }
        }
        scene.MarkTransformDirty(); // GPU/TLAS only; CPU collision/navigation stays unchanged
    }
}
