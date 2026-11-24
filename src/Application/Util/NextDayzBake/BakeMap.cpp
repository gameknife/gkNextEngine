#include "Engine/Common/CoreMinimal.hpp"
#include "BakeGlb.hpp"

#include "Application/Game/NextDayz/Inventory/MapLootKeys.hpp"
#include "Application/Game/NextDayz/Net/ProtocolVersion.hpp"
#include "Application/Game/NextDayz/World/WorldAnchorRegistry.hpp"
#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Assets/Loaders/LoaderRegistry.hpp"
#include "Engine/Runtime/Components/PhysicsComponent.hpp"
#include "Engine/Runtime/Components/RenderComponent.hpp"
#include "Engine/Runtime/Components/TerrainComponent.hpp"
#include "Modules/ScadLoader/FScadLoader.h"
#include "Modules/ScadLoader/FScadShared.h"

#include <meshoptimizer.h>

namespace NextDayzBake
{
    namespace
    {
        using Nodes = std::vector<std::shared_ptr<Assets::Node>>;
        using Models = std::vector<Assets::Model>;
        using TerrainGrid = Runtime::TerrainComponent::FGridData;

        struct CollisionInstance
        {
            uint32_t mesh;
            uint32_t node;
            uint32_t target;
            uint32_t flags; // ray, static body, kinematic body
            glm::mat4 transform;
            glm::vec3 translation;
            glm::quat rotation;
            glm::vec3 scale;
        };

        Json BakeAnchors(const Nodes& nodes, const fs::path& output)
        {
            NextDayz::WorldAnchorRegistry registry;
            registry.Scan(nodes);

            const std::array types{"playerSafe", "zombie", "loot", "well", "poi", "extraction"};
            Json counts = Json::object();
            for (const auto* type : types) counts[type] = 0;
            Json anchors = Json::array();
            Json loot = Json::array();
            Json gameplay = Json::array();
            std::unordered_map<std::string, int> occurrences;
            const auto addLoot = [&](std::string key, glm::vec3 pos, Json reference)
            {
                NextDayz::NumberLootKey(key, occurrences);
                reference["persistentId"] = key;
                reference["position"] = Array(pos);
                loot.push_back(std::move(reference));
                return key;
            };

            for (size_t index = 0; index < nodes.size(); ++index)
            {
                const auto& node = nodes[index];
                const auto& name = node->GetName();
                if (NextDayz::AuthoredTable().contains(name))
                {
                    addLoot(NextDayz::LootKey(name, node->WorldTranslation()), node->WorldTranslation(),
                            {{"source", "authored"}, {"node", index}, {"name", name}});
                }
                if (name == "nd_target" || (!node->GetMetadata().empty() && name.starts_with("nd_")))
                {
                    Json metadata = Json::object();
                    for (const auto& [key, value] : Assets::Scad::ParseScadMetadata(node->GetMetadata()))
                    {
                        metadata.emplace(key, value);
                    }
                    gameplay.push_back({{"name", name}, {"node", index}, {"metadata", std::move(metadata)},
                                        {"transform", Array(node->WorldTransform())}});
                }
            }

            for (const auto& anchor : registry.All())
            {
                const auto* type = types.at(static_cast<size_t>(anchor.type));
                counts[type] = counts[type].get<size_t>() + 1;
                Json item = {
                    {"type", type}, {"profile", anchor.profile}, {"label", anchor.label}, {"position", Array(anchor.worldPos)},
                    {"group", anchor.group}, {"item", anchor.item}, {"count", anchor.count}, {"yaw", anchor.yaw},
                };
                if (anchor.type == NextDayz::EWorldAnchorType::Loot)
                {
                    const auto profile = NextDayz::ParseLootProfile(anchor.profile).value_or(NextDayz::ELootProfile::Residential);
                    item["persistentId"] = addLoot(NextDayz::SpotLootKey(profile, anchor.item, anchor.worldPos),
                                                  anchor.worldPos, {{"source", "spot"}, {"anchor", anchors.size()}});
                }
                anchors.push_back(std::move(item));
            }

            WriteJson(output, {{"anchors", anchors}, {"loot", loot}, {"gameplay", gameplay}});
            return {{"anchorCounts", counts}, {"lootEntries", loot.size()}, {"gameplayNodes", gameplay.size()}};
        }

        void SaveTerrain(const std::vector<std::shared_ptr<const TerrainGrid>>& grids, const fs::path& path)
        {
            Binary out(path, "NDT1");
            out.Count(grids.size());
            for (const auto& grid : grids)
            {
                out.Count(grid->cellsX);
                out.Count(grid->cellsY);
                out.Scalar(grid->sizeX);
                out.Scalar(grid->sizeY);
                out.Scalar(grid->seed);
                out.Values(Array(grid->localToWorld));
                out.Values(Array(grid->worldToLocal));
                for (const auto& vertex : grid->verts) out.Values(Array(vertex));
                out.Bytes(grid->diagFlip.data(), grid->diagFlip.size());
                out.Bytes(grid->biome.data(), grid->biome.size());
                out.Bytes(grid->flags.data(), grid->flags.size());
                out.Values(std::span<const double>(grid->waterLevel));
            }
            out.Close();
        }

        Json Environment(const Assets::EnvironmentSetting& environment)
        {
            const auto& atm = environment.Atmosphere;
            return {
                {"sun", {{"enabled", environment.HasSun}, {"rotation", environment.SunRotation},
                         {"elevation", environment.SunElevation}, {"color", Array(environment.SunColor)},
                         {"intensity", environment.SunIntensity}, {"direction", Array(environment.SunDirection())}}},
                {"sky", {{"enabled", environment.HasSky}, {"index", environment.SkyIdx},
                         {"rotation", environment.SkyRotation}, {"intensity", environment.SkyIntensity},
                         {"color", Array(environment.SkyColor)}}},
                {"atmosphere", {{"enabled", environment.AtmosphereEnabled}, {"aerialPerspective", environment.AerialPerspectiveEnabled},
                    {"rayleighScattering", Array(atm.RayleighScattering)}, {"rayleighDensityH", atm.RayleighDensityH},
                    {"mieScattering", Array(atm.MieScattering)}, {"mieDensityH", atm.MieDensityH},
                    {"mieAbsorption", Array(atm.MieAbsorption)}, {"miePhaseG", atm.MiePhaseG},
                    {"ozoneAbsorption", Array(atm.OzoneAbsorption)}, {"ozoneCenterAltitude", atm.OzoneCenterAltitude},
                    {"ozoneWidth", atm.OzoneWidth}, {"groundAlbedo", Array(atm.GroundAlbedo)},
                    {"bottomRadius", atm.BottomRadius}, {"topRadius", atm.TopRadius}, {"worldUnitsPerKm", atm.WorldUnitsPerKm},
                    {"worldOriginAltitude", atm.WorldOriginAltitude},
                    {"aerialPerspectiveMaxDistance", atm.AerialPerspectiveMaxDistance}, {"skyLuminanceScale", atm.SkyLuminanceScale}}},
                {"fog", {{"enabled", environment.HeightFogEnabled}, {"color", Array(atm.FogInscatteringColor)},
                         {"density", atm.FogDensity}, {"heightFalloff", atm.FogHeightFalloff}, {"baseHeight", atm.FogBaseHeight},
                         {"startDistance", atm.FogStartDistance}, {"maxOpacity", atm.FogMaxOpacity}}},
            };
        }

        Json SaveCollision(const Nodes& nodes, const Models& models, const fs::path& path)
        {
            std::unordered_set<uint32_t> terrainModels;
            std::unordered_map<const Assets::Node*, uint32_t> targets;
            for (size_t index = 0; index < nodes.size(); ++index)
            {
                const auto& node = nodes[index];
                if (node->GetName() == "nd_target") targets.emplace(node.get(), static_cast<uint32_t>(index));
                const auto* render = node->GetComponent<Runtime::RenderComponent>();
                if (render && node->GetComponent<Runtime::TerrainComponent>())
                {
                    terrainModels.insert(render->GetModelId());
                }
            }
            std::map<uint32_t, uint32_t> meshIndexes;
            std::vector<uint32_t> meshModels;
            std::vector<CollisionInstance> instances;
            size_t rayTriangles = 0;
            size_t bodyTriangles = 0;
            for (size_t index = 0; index < nodes.size(); ++index)
            {
                const auto& node = nodes[index];
                const auto* render = node->GetComponent<Runtime::RenderComponent>();
                if (!render || !render->IsDrawable() || !render->GetRayCastVisible()) continue;
                const auto model = render->GetModelId();
                const auto count = models.at(model).CPUIndices().size();
                if (!count) continue;

                namespace Render = Runtime::RenderParticipation;
                const bool ray = render->GetVisible() &&
                    (render->GetRenderParticipationMask() & (Render::giBake | Render::gpuAs));
                const auto* physics = node->GetComponent<Runtime::PhysicsComponent>();
                const auto mobility = physics ? physics->GetMobility() : Assets::Node::ENodeMobility::Static;
                const glm::vec3 scale = node->WorldScale();
                const bool body = mobility != Assets::Node::ENodeMobility::Dynamic &&
                    (count < 65535 * 3 || terrainModels.contains(model)) &&
                    glm::length(scale) > 0.01f && glm::all(glm::greaterThan(glm::abs(scale), glm::vec3(0.001f)));
                if (!ray && !body) continue;
                const uint32_t bodyFlag = mobility == Assets::Node::ENodeMobility::Static ? 2u : 4u;
                const auto [found, inserted] = meshIndexes.try_emplace(model, static_cast<uint32_t>(meshModels.size()));
                if (inserted) meshModels.push_back(model);

                uint32_t target = UINT32_MAX;
                for (const auto* ancestor = node.get(); ancestor; ancestor = ancestor->GetParent())
                {
                    if (const auto it = targets.find(ancestor); it != targets.end())
                    {
                        target = it->second;
                        break;
                    }
                }
                instances.push_back({found->second, static_cast<uint32_t>(index), target,
                    (ray ? 1u : 0u) | (body ? bodyFlag : 0u),
                    node->WorldTransform(), node->WorldTranslation(), node->WorldRotation(), scale});
                if (ray) rayTriangles += count / 3;
                if (body) bodyTriangles += count / 3;
            }

            Binary out(path, "NDC1");
            out.Count(meshModels.size());
            out.Count(instances.size());
            for (const auto model : meshModels)
            {
                const auto& [vertices, indices] = std::tie(models[model].CPUVertices(), models[model].CPUIndices());
                std::vector<std::array<float, 3>> positions;
                positions.reserve(vertices.size());
                for (const auto& vertex : vertices) positions.push_back(Array(vertex.Position));
                std::vector<uint32_t> remap(positions.size());
                const size_t count = meshopt_generateVertexRemap(remap.data(), indices.data(), indices.size(),
                    positions.data(), positions.size(), sizeof(positions.front()));
                std::vector<uint32_t> weldedIndices(indices.size());
                meshopt_remapIndexBuffer(weldedIndices.data(), indices.data(), indices.size(), remap.data());
                std::vector<std::array<float, 3>> weldedPositions(count);
                meshopt_remapVertexBuffer(weldedPositions.data(), positions.data(), positions.size(),
                    sizeof(positions.front()), remap.data());
                const auto packed = EncodeVertices(weldedPositions.data(), count, sizeof(weldedPositions.front()));
                const auto triangles = EncodeTriangles(weldedIndices, count);
                out.Count(count);
                out.Count(indices.size());
                out.Count(packed.size());
                out.Count(triangles.size());
                out.Bytes(packed.data(), packed.size());
                out.Bytes(triangles.data(), triangles.size());
            }
            for (const auto& instance : instances)
            {
                out.Values(std::array{instance.mesh, instance.node, instance.target, instance.flags});
                out.Values(Array(instance.transform));
                out.Values(Array(instance.translation));
                const auto& q = instance.rotation;
                out.Values(std::array{q.x, q.y, q.z, q.w});
                out.Values(Array(instance.scale));
            }
            out.Close();
            return {{"rayTriangles", rayTriangles}, {"bodyTriangles", bodyTriangles},
                    {"collisionMeshes", meshModels.size()}, {"collisionInstances", instances.size()}};
        }
    }

    void BakeMap(const std::string& source, const fs::path& destination)
    {
        fs::create_directories(destination);
        Assets::EnvironmentSetting environment;
        Nodes nodes;
        Models models;
        std::vector<Assets::FMaterial> materials;
        std::vector<Assets::FProceduralTextureRef> prints;
        std::vector<Assets::LightObject> lights;
        std::vector<Assets::AnimationTrack> tracks;
        std::vector<Assets::Skeleton> skeletons;
        if (!Assets::FScadLoader::LoadScadScene(source, environment, nodes, models, materials, lights, tracks, skeletons, {}, &prints))
        {
            throw std::runtime_error("SCAD load failed");
        }
        Assets::FSceneSourceReport report;
        if (!Assets::FLoaderRegistry::Get().FindLoadReport(source, report))
        {
            throw std::runtime_error("Missing SCAD load report");
        }
        for (const auto& node : nodes) node->RecalcTransform(true);
        auto stats = BakeAnchors(nodes, destination / "anchors.json");
        stats.update(SaveCollision(nodes, models, destination / "collision.bin"));
        WriteJson(destination / "env.json", Environment(environment));

        std::vector<std::shared_ptr<const TerrainGrid>> grids;
        using Tile = std::pair<int, int>;
        using MeshKey = std::pair<uint32_t, std::array<uint32_t, 16>>;
        std::map<Tile, std::map<MeshKey, std::vector<const Assets::Node*>>> tiles;
        size_t desktopTriangles = 0;
        Aabb desktopBounds;
        for (const auto& node : nodes)
        {
            const auto* grid = node->GetComponent<Runtime::TerrainComponent>();
            if (grid && grid->HasData()) grids.push_back(grid->GetGridData());
            const auto* render = node->GetComponent<Runtime::RenderComponent>();
            if (!render || !render->IsDrawable() || !render->GetVisible() || !render->GetMainVisible()) continue;
            const auto& model = models.at(render->GetModelId());
            if (model.CPUIndices().empty()) continue;
            desktopTriangles += model.CPUIndices().size() / 3;
            const auto transform = node->WorldTransform();
            for (const auto index : model.CPUIndices())
            {
                desktopBounds.Include(glm::vec3(transform * glm::vec4(model.CPUVertices()[index].Position, 1)));
            }
            const auto center = node->WorldTranslation();
            constexpr float tileSize = 256.f;
            const Tile tile{static_cast<int>(std::floor(center.x / tileSize)), static_cast<int>(std::floor(center.z / tileSize))};
            tiles[tile][{render->GetModelId(), render->GetMaterials()}].push_back(node.get());
        }

        SaveTerrain(grids, destination / "terrain.bin");
        PrintCache printCache(destination);
        size_t triangles = 0;
        size_t batches = 0;
        size_t instancedNodes = 0;
        Aabb bounds;
        Json chunks = Json::array();
        for (const auto& [tile, meshes] : tiles)
        {
            Glb glb;
            std::map<uint32_t, size_t> glbMaterials;
            const auto materialIndex = [&](uint32_t material)
            {
                const auto [entry, inserted] = glbMaterials.try_emplace(material);
                if (inserted) entry->second = glb.Material(materials.at(material), printCache.Bake(prints.at(material)));
                return entry->second;
            };
            // Batch sections with the same print and PBR response. Their
            // SCAD diffuse colors travel with the vertices.
            using SurfaceKey = std::tuple<PrintFiles, float, float, bool>;
            std::map<SurfaceKey, Primitive> surfaces;
            std::map<uint32_t, Primitive*> surfaceByMaterial;
            Aabb chunkBounds;
            size_t chunkTriangles = 0;
            for (const auto& [key, copies] : meshes)
            {
                const auto& [modelId, sections] = key;
                const auto& model = models.at(modelId);
                const auto& vertices = model.CPUVertices();
                const auto& indices = model.CPUIndices();
                const auto append = [&](const glm::mat4& transform, auto getBatch)
                {
                    const glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(transform)));
                    const bool mirrored = glm::determinant(glm::mat3(transform)) < 0.f;
                    std::vector<uint32_t> remap(vertices.size(), UINT32_MAX);
                    for (size_t offset = 0; offset < indices.size(); offset += 3)
                    {
                        const uint32_t material = sections.at(vertices[indices[offset]].MaterialIndex);
                        auto [batch, colors] = getBatch(material);
                        const std::array corners{indices[offset], indices[offset + (mirrored ? 2 : 1)],
                                                 indices[offset + (mirrored ? 1 : 2)]};
                        for (const auto index : corners)
                        {
                            if (remap[index] == UINT32_MAX)
                            {
                                remap[index] = batch->Vertex(vertices[index], transform, normalMatrix, !!prints.at(material), mirrored);
                                if (colors)
                                {
                                    const auto rgba = materials.at(material).gpuMaterial_.Diffuse;
                                    const auto pack = [](float value) { return static_cast<uint16_t>(meshopt_quantizeUnorm(value, 16)); };
                                    batch->colors.push_back({pack(rgba.r), pack(rgba.g), pack(rgba.b), pack(rgba.a)});
                                }
                            }
                            batch->indices.push_back(remap[index]);
                        }
                    }
                };

                std::vector<Glb::Transform> transforms;
                // Preserve batching for small props. Repeated high-poly models
                // save substantial transfer and decode work as GPU instances.
                if (copies.size() >= 3 && (copies.size() - 1) * indices.size() / 3 >= 4000)
                {
                    for (const auto* node : copies)
                    {
                        const auto matrix = node->WorldTransform();
                        glm::mat3 basis(matrix);
                        const glm::vec3 scale(glm::length(basis[0]), glm::length(basis[1]), glm::length(basis[2]));
                        if (glm::any(glm::lessThanEqual(scale, glm::vec3(1e-6f))) || glm::determinant(basis) <= 0.f)
                        {
                            transforms.clear();
                            break;
                        }
                        for (int axis = 0; axis < 3; ++axis) basis[axis] /= scale[axis];
                        if (std::abs(glm::dot(basis[0], basis[1])) > 1e-5f ||
                            std::abs(glm::dot(basis[1], basis[2])) > 1e-5f ||
                            std::abs(glm::dot(basis[0], basis[2])) > 1e-5f)
                        {
                            transforms.clear();
                            break;
                        }
                        transforms.push_back({glm::vec3(matrix[3]), glm::normalize(glm::quat_cast(basis)), scale});
                    }
                }

                if (!transforms.empty())
                {
                    std::map<uint32_t, Primitive> primitives;
                    append(glm::mat4(1), [&](uint32_t material) { return std::pair{&primitives[material], false}; });
                    Json group = Json::array();
                    for (const auto& [material, primitive] : primitives)
                    {
                        group.push_back(glb.WritePrimitive(primitive, materialIndex(material)));
                        chunkTriangles += (primitive.indices.size() / 3) * transforms.size();
                    }
                    batches += primitives.size();
                    glb.Mesh(fmt::format("instances-{}-{}", modelId, instancedNodes), std::move(group));
                    glb.Instance(transforms);
                    instancedNodes += transforms.size();
                    for (const auto* node : copies)
                    {
                        const auto transform = node->WorldTransform();
                        for (const auto index : indices)
                        {
                            chunkBounds.Include(glm::vec3(transform * glm::vec4(vertices[index].Position, 1)));
                        }
                    }
                    continue;
                }

                for (const auto* node : copies)
                {
                    append(node->WorldTransform(), [&](uint32_t material)
                    {
                        const auto [entry, inserted] = surfaceByMaterial.try_emplace(material);
                        if (inserted)
                        {
                            const auto& value = materials.at(material).gpuMaterial_;
                            const float roughness = value.MaterialModel == Assets::Material::Enum::Lambertian
                                ? 1.f : std::clamp(value.Fuzziness, 0.f, 1.f);
                            auto print = printCache.Bake(prints.at(material));
                            entry->second = &surfaces[{std::move(print), roughness, value.Metalness, value.Diffuse.a < 0.99f}];
                        }
                        return std::pair{entry->second, true};
                    });
                }
            }
            Json group = Json::array();
            const auto addMerged = [&](const Primitive& primitive, size_t material)
            {
                group.push_back(glb.WritePrimitive(primitive, material));
                chunkBounds.Include(primitive.bounds.min);
                chunkBounds.Include(primitive.bounds.max);
                chunkTriangles += primitive.indices.size() / 3;
            };
            for (const auto& [surface, primitive] : surfaces)
            {
                const auto& [print, roughness, metalness, transparent] = surface;
                Assets::FMaterial material{};
                material.gpuMaterial_.Diffuse = glm::vec4(1, 1, 1, transparent ? 0.f : 1.f);
                material.gpuMaterial_.MaterialModel = Assets::Material::Enum::Metallic;
                material.gpuMaterial_.Fuzziness = roughness;
                material.gpuMaterial_.Metalness = metalness;
                addMerged(primitive, glb.Material(material, print, false, true));
            }
            const auto name = fmt::format("scene-{}-{}.glb", tile.first, tile.second);
            batches += group.size();
            if (!group.empty()) glb.Mesh(name, std::move(group));
            glb.Save(destination / name);
            triangles += chunkTriangles;
            bounds.Include(chunkBounds.min);
            bounds.Include(chunkBounds.max);
            chunks.push_back({{"file", name}, {"triangles", chunkTriangles}, {"bounds", chunkBounds.ToJson()}});
        }
        if (triangles == 0 || triangles != desktopTriangles || bounds.min != desktopBounds.min || bounds.max != desktopBounds.max)
        {
            throw std::runtime_error("Map geometry verification failed");
        }

        const auto sidecars = Assets::Scad::LooseSourceSidecars();
        report.dependencies.insert(report.dependencies.end(), sidecars.begin(), sidecars.end());
        stats.update(Json{
            {"protocol", NextDayz::Net::kProtocolVersion}, {"sourceTriangles", report.triangleCount},
            {"desktopTriangles", desktopTriangles}, {"triangles", triangles}, {"batches", batches}, {"bounds", bounds.ToJson()},
            {"instancedNodes", instancedNodes}, {"terrainGrids", grids.size()}, {"chunks", chunks}, {"dependencies", report.dependencies}, {"warnings", report.warnings},
        });
        WriteJson(destination / "bake.json", stats);
        std::cout << fmt::format("{}: {} triangles, {} chunks, {} batches, {} loot entries\n", source, triangles,
                                 chunks.size(), batches, stats["lootEntries"].get<size_t>());
    }
}
