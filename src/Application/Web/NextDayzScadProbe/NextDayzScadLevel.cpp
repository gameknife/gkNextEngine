#include "Engine/Common/CoreMinimal.hpp"

#include "Application/Game/NextDayz/World/WorldAnchorRegistry.hpp"
#include "Engine/Assets/Loaders/ProceduralTexture.hpp"
#include "Modules/ScadLoader/FScadCsg.h"
#include "Modules/ScadLoader/FScadEvaluator.h"
#include "Modules/ScadLoader/FScadShared.h"
#include "Modules/ScadLoader/FScadTess.h"
#include "Modules/ScadLoader/FScadText.h"

#include <emscripten/emscripten.h>
#include <meshoptimizer.h>
#include <nlohmann/json.hpp>
#include <xxhash.h>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/quaternion.hpp>

#include <limits>
#include <span>
#include <tuple>

namespace NextDayzScadLevel
{
    using namespace Assets::Scad;
    using Json = nlohmann::json;

    // The packet and JSON index are owned by this Worker for a single evaluation.
    // Offsets are in 32-bit words. All binary arrays share one transferable buffer.
    std::vector<uint32_t> packet;
    std::string levelIndex;

    template <typename T>
    auto Append(std::span<const T> values)
    {
        static_assert(sizeof(T) % sizeof(uint32_t) == 0);
        const size_t start = packet.size();
        packet.resize(start + values.size_bytes() / sizeof(uint32_t));
        if (!values.empty())
        {
            std::memcpy(packet.data() + start, values.data(), values.size_bytes());
        }
        return std::array{start, packet.size() - start};
    }

    template <typename T>
    auto Append(const std::vector<T>& values)
    {
        return Append(std::span<const T>(values));
    }

    auto Point(const glm::vec3& p)
    {
        return std::array{p.x, p.y, p.z};
    }

    template <typename T>
    std::vector<uint32_t> Weld(std::vector<T>& points)
    {
        std::vector<uint32_t> remap(points.size());
        const size_t unique = meshopt_generateVertexRemap(remap.data(), nullptr, points.size(), points.data(),
                                                         points.size(), sizeof(T));
        meshopt_remapVertexBuffer(points.data(), points.data(), points.size(), sizeof(T), remap.data());
        points.resize(unique);
        return remap;
    }

    struct Vertex
    {
        glm::vec3 position;
        glm::vec3 normal;
        glm::vec2 uv;
        glm::vec4 tangent;
    };
    static_assert(sizeof(Vertex) == 12 * sizeof(float));

    struct Geometry
    {
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
    };

    uint32_t PackColor(const glm::vec4& color)
    {
        uint32_t packed = 0;
        for (unsigned i = 0; i < 4; ++i)
        {
            packed |= static_cast<uint32_t>(meshopt_quantizeUnorm(color[i], 8)) << (i * 8);
        }
        return packed;
    }

    Json ExportMesh(const Geometry& mesh)
    {
        std::vector<std::array<float, 5>> points;
        std::vector<std::array<int16_t, 8>> directions;
        points.reserve(mesh.vertices.size());
        directions.reserve(mesh.vertices.size());
        const auto pack = [](float value) {
            return static_cast<int16_t>(meshopt_quantizeSnorm(std::isfinite(value) ? value : 0.f, 16));
        };
        for (const auto& v : mesh.vertices)
        {
            points.push_back({v.position.x, v.position.y, v.position.z, v.uv.x, v.uv.y});
            directions.push_back({pack(v.normal.x), pack(v.normal.y), pack(v.normal.z), 0,
                pack(v.tangent.x), pack(v.tangent.y), pack(v.tangent.z), pack(v.tangent.w)});
        }
        return {{"vertices", Append(points)}, {"directions", Append(directions)}, {"indices", Append(mesh.indices)}};
    }

    struct Placement
    {
        glm::mat4 transform;
        glm::vec4 color;
    };

    struct RenderGroup
    {
        std::map<size_t, std::vector<Placement>> copies;
    };

    struct CollisionMesh
    {
        std::vector<glm::vec3> positions;
        std::vector<uint32_t> indices;
    };

    struct CollisionInstance
    {
        uint32_t mesh;
        uint32_t node;
        uint32_t target;
        uint32_t flags;
        glm::mat4 transform;
        glm::vec3 translation;
        glm::vec4 rotation; // x, y, z, w
        glm::vec3 scale;
    };
    static_assert(sizeof(glm::vec3) == 3 * sizeof(float));
    static_assert(sizeof(CollisionInstance) == 30 * sizeof(uint32_t));

    struct LevelBuilder
    {
        using SurfaceKey = std::tuple<int, float, float, bool, bool>;
        using GroupKey = std::tuple<int, int, size_t>;

        std::vector<Geometry> geometry;
        std::map<std::tuple<uint64_t, size_t, float, bool, uint64_t>, size_t> geometryByShape;
        std::map<SurfaceKey, size_t> surfaceByKey;
        std::map<GroupKey, RenderGroup> groups;
        std::map<uint64_t, int> printByHash;
        std::vector<Assets::FProceduralTextureRef> prints;
        std::vector<CollisionMesh> collisionMeshes;
        std::map<std::pair<uint64_t, size_t>, uint32_t> collisionByShape;
        std::vector<CollisionInstance> collisionInstances;
        std::unordered_set<uint64_t> terrainOwners;
        Json surfaces = Json::array();
        Json anchors = Json::array();
        Json batches = Json::array();
        Json textures = Json::array();
        glm::vec3 min{std::numeric_limits<float>::max()};
        glm::vec3 max{std::numeric_limits<float>::lowest()};
        size_t renderTriangles = 0;
        size_t collisionTriangles = 0;
        uint32_t nextNode = 0;

        size_t Surface(const SceneMeshBucket& mesh)
        {
            int print = -1;
            if (mesh.material.texture && mesh.color.a >= 0.99f)
            {
                const auto found = printByHash.try_emplace(mesh.material.texture->Hash(), static_cast<int>(prints.size()));
                print = found.first->second;
                if (found.second) prints.push_back(mesh.material.texture);
            }
            const bool transparent = mesh.color.a < 0.99f;
            const float roughness = transparent && !mesh.material.explicitParameters ? 0.0f :
                std::clamp(mesh.material.roughness, 0.f, 1.f);
            const float metalness = std::clamp(mesh.material.metalness, 0.f, 1.f);
            const auto [entry, inserted] = surfaceByKey.try_emplace(
                SurfaceKey{print, roughness, metalness, transparent, mesh.terrainDecal}, surfaces.size());
            if (inserted)
            {
                surfaces.push_back({{"print", print}, {"roughness", roughness}, {"metalness", metalness},
                    {"transparent", transparent}, {"decal", mesh.terrainDecal}});
            }
            return entry->second;
        }

        size_t Mesh(const SceneMeshBucket& mesh, const std::vector<glm::vec3>& positions, float smoothAngle)
        {
            const float scale = mesh.material.texture ? mesh.material.texture->scale : 0.0f;
            // gk_crease / gk_puff_normals spans change the normals, not the positions.
            std::vector<double> spanFields;
            for (const ScadNormalSpan& span : mesh.normalSpans)
            {
                const ScadShading& s = span.shading;
                spanFields.insert(spanFields.end(), {double(span.firstTri), double(span.triCount),
                    double(s.smoothAngleDegrees), s.puff ? 1.0 : 0.0, s.puffCenter.x, s.puffCenter.y, s.puffCenter.z,
                    s.puffRadius, double(s.puffStrength), double(s.puffUpBias)});
            }
            for (const glm::dvec3& n : mesh.normals)
            {
                spanFields.insert(spanFields.end(), {n.x, n.y, n.z});
            }
            const uint64_t shading = spanFields.empty() ? 0 : XXH64(spanFields.data(), spanFields.size() * sizeof(double), 0);
            const auto key = std::tuple{XXH64(positions.data(), positions.size() * sizeof(glm::vec3), 0),
                positions.size(), scale, mesh.faceted, shading};
            const auto [entry, inserted] = geometryByShape.try_emplace(key, geometry.size());
            if (inserted)
            {
                auto& asset = geometry.emplace_back();
                const auto normals = ScadComputeShadedNormals(positions, mesh.faceted ? 0.f : smoothAngle, mesh.normalSpans, mesh.normals, 1.0);
                std::vector<glm::vec2> uv(positions.size(), glm::vec2(0.f));
                std::vector<glm::vec4> tangents(positions.size(), glm::vec4(1.f, 0.f, 0.f, 1.f));
                if (mesh.material.texture)
                {
                    Assets::ProceduralTexture::BoxProjectUVs(mesh.tris, scale, uv);
                    Assets::ProceduralTexture::BoxProjectTangents(mesh.tris, glm::mat3(ScadToWorldBasis(1.0)), normals, tangents);
                }
                asset.vertices.reserve(positions.size());
                for (size_t i = 0; i < positions.size(); ++i)
                {
                    asset.vertices.push_back({positions[i], normals[i], uv[i], tangents[i]});
                }
                asset.indices = Weld(asset.vertices);
            }
            return entry->second;
        }

        void AddCollision(std::vector<glm::vec3>& positions, const glm::mat4& world,
                          uint32_t node, uint32_t target, bool terrain)
        {
            if (positions.empty()) return;
            glm::vec3 scale{1.f};
            glm::vec3 translation{world[3]};
            glm::vec3 skew;
            glm::vec4 perspective;
            glm::quat rotation{1.f, 0.f, 0.f, 0.f};
            const size_t count = positions.size();
            const bool body = (count < 65535 * 3 || terrain) &&
                glm::decompose(world, scale, rotation, translation, skew, perspective) &&
                glm::length(scale) > 0.01f && glm::all(glm::greaterThan(glm::abs(scale), glm::vec3(0.001f)));
            const auto key = std::pair{XXH64(positions.data(), positions.size() * sizeof(glm::vec3), 0), count};
            const auto [entry, inserted] = collisionByShape.try_emplace(key, static_cast<uint32_t>(collisionMeshes.size()));
            if (inserted)
            {
                auto indices = Weld(positions);
                collisionMeshes.push_back({std::move(positions), std::move(indices)});
            }
            collisionInstances.push_back({entry->second, node, target, body ? 3u : 1u,
                world, translation, {rotation.x, rotation.y, rotation.z, rotation.w}, scale});
            collisionTriangles += count / 3;
            positions.clear();
        }

        void AddNode(const SceneNode& node, const glm::mat4& parent, bool collision, uint32_t target, float smoothAngle)
        {
            const uint32_t id = nextNode++;
            glm::vec3 translation, scale;
            glm::quat rotation;
            ScadLocalToEngineTRS(node.localTransform, 1.0, translation, rotation, scale);
            const glm::mat4 world = parent * (glm::translate(glm::mat4(1.0f), translation) *
                glm::mat4_cast(rotation) * glm::scale(glm::mat4(1.0f), scale));

            if (NextDayz::IsWorldAnchor(node.name))
            {
                const auto anchor = NextDayz::ReadWorldAnchor(node.name, SerializeScadNodeMetadata(node), world, id);
                if (anchor)
                {
                    using NextDayz::EWorldAnchorType;
                    constexpr std::array types{"playerSafe", "zombie", "loot", "well", "poi", "extraction"};
                    anchors.push_back({{"type", types.at(static_cast<size_t>(anchor->type))},
                        {"profile", anchor->profile}, {"label", anchor->label}, {"item", anchor->item},
                        {"count", anchor->count}, {"group", anchor->group}, {"yaw", anchor->yaw},
                        {"position", Point(anchor->worldPos)}});
                    if (anchor->type != EWorldAnchorType::Well) return;
                }
            }

            if (node.name == "nd_target") target = id;
            collision = collision && !ScadNodeDisablesCollision(node);
            const bool terrain = terrainOwners.contains(node.instanceId);
            constexpr float tileSize = 250.0f;
            const int tileX = static_cast<int>(std::floor(world[3].x / tileSize));
            const int tileZ = static_cast<int>(std::floor(world[3].z / tileSize));

            std::vector<glm::vec3> solid;
            size_t solidSections = 0;
            for (const auto& mesh : node.meshes)
            {
                if (mesh.tris.empty()) continue;
                std::vector<glm::vec3> positions;
                positions.reserve(mesh.tris.size());
                for (const auto& p : mesh.tris) positions.push_back(ScadToWorldPos(p, 1.0));
                const size_t surface = Surface(mesh);
                const size_t asset = Mesh(mesh, positions, smoothAngle);
                groups[{tileX, tileZ, surface}].copies[asset].push_back({world, mesh.color});
                renderTriangles += positions.size() / 3;
                for (const auto& vertex : geometry[asset].vertices)
                {
                    const glm::vec3 p(world * glm::vec4(vertex.position, 1.f));
                    min = glm::min(min, p);
                    max = glm::max(max, p);
                }

                if (collision && !mesh.terrainDecal && !mesh.terrainWater)
                {
                    solid.insert(solid.end(), positions.begin(), positions.end());
                    if (++solidSections == 16)
                    {
                        AddCollision(solid, world, id, target, terrain);
                        solidSections = 0;
                    }
                }
            }
            AddCollision(solid, world, id, target, terrain);
            for (const auto& child : node.children) AddNode(child, world, collision, target, smoothAngle);
        }

        static bool CanInstance(const Placement& placement)
        {
            glm::mat3 basis(placement.transform);
            for (int i = 0; i < 3; ++i)
            {
                const float length = glm::length(basis[i]);
                if (length < 1e-6f) return false;
                basis[i] /= length;
            }
            return glm::determinant(basis) > 0.f && std::abs(glm::dot(basis[0], basis[1])) < 1e-5f &&
                std::abs(glm::dot(basis[0], basis[2])) < 1e-5f && std::abs(glm::dot(basis[1], basis[2])) < 1e-5f;
        }

        bool UseInstances(size_t surface, const Geometry& mesh, const std::vector<Placement>& copies) const
        {
            return !surfaces.at(surface)["transparent"].get<bool>() && copies.size() >= 3 &&
                (copies.size() - 1) * mesh.indices.size() >= 2700 && std::ranges::all_of(copies, CanInstance);
        }

        void ReservePacket()
        {
            constexpr size_t textureSize = Assets::ProceduralTexture::kDefaultSize;
            size_t words = prints.size() * 3 * textureSize * textureSize + collisionInstances.size() * 30;
            for (const auto& mesh : collisionMeshes) words += mesh.positions.size() * 3 + mesh.indices.size();
            std::unordered_set<size_t> instancedAssets;
            for (const auto& [key, group] : groups)
            {
                for (const auto& [id, copies] : group.copies)
                {
                    const auto& mesh = geometry[id];
                    if (UseInstances(std::get<2>(key), mesh, copies))
                    {
                        words += copies.size() * 17;
                        if (instancedAssets.insert(id).second) words += mesh.vertices.size() * 9 + mesh.indices.size();
                    }
                    else
                    {
                        words += copies.size() * (mesh.vertices.size() * 10 + mesh.indices.size());
                    }
                }
            }
            packet.reserve(words);
        }

        void ExportGeometry()
        {
            std::map<size_t, Json> instanceAssets;
            for (const auto& [key, group] : groups)
            {
                const size_t surface = std::get<2>(key);
                Geometry merged;
                std::vector<uint32_t> colors;
                for (const auto& [id, copies] : group.copies)
                {
                    const auto& mesh = geometry[id];
                    if (UseInstances(surface, mesh, copies))
                    {
                        const auto [asset, inserted] = instanceAssets.try_emplace(id);
                        if (inserted)
                        {
                            asset->second = ExportMesh(mesh);
                        }
                        std::vector<glm::mat4> transforms;
                        std::vector<uint32_t> tints;
                        for (const auto& placement : copies)
                        {
                            transforms.push_back(placement.transform);
                            tints.push_back(PackColor(placement.color));
                        }
                        Json batch = asset->second;
                        batch.update(Json{{"surface", surface}, {"matrices", Append(transforms)}, {"colors", Append(tints)}});
                        batches.push_back(std::move(batch));
                        continue;
                    }
                    for (const auto& placement : copies)
                    {
                        const glm::mat3 transform(placement.transform);
                        const float determinant = glm::determinant(transform);
                        const bool mirror = determinant < 0.f;
                        const glm::mat3 normals = std::abs(determinant) > 1e-9f ? glm::transpose(glm::inverse(transform)) : glm::mat3(1.f);
                        const auto base = static_cast<uint32_t>(merged.vertices.size());
                        const uint32_t color = PackColor(placement.color);
                        for (const auto& vertex : mesh.vertices)
                        {
                            const glm::vec3 tangent = transform * glm::vec3(vertex.tangent);
                            merged.vertices.push_back({
                                glm::vec3(placement.transform * glm::vec4(vertex.position, 1.f)), glm::normalize(normals * vertex.normal),
                                vertex.uv, glm::vec4(glm::length(tangent) > 1e-8f ? glm::normalize(tangent) : glm::vec3(1, 0, 0),
                                    mirror ? -vertex.tangent.w : vertex.tangent.w)});
                            colors.push_back(color);
                        }
                        for (size_t i = 0; i < mesh.indices.size(); i += 3)
                        {
                            merged.indices.push_back(base + mesh.indices[i]);
                            merged.indices.push_back(base + mesh.indices[i + (mirror ? 2 : 1)]);
                            merged.indices.push_back(base + mesh.indices[i + (mirror ? 1 : 2)]);
                        }
                    }
                }
                if (!merged.indices.empty())
                {
                    Json batch = ExportMesh(merged);
                    batch.update(Json{{"surface", surface}, {"colors", Append(colors)}});
                    batches.push_back(std::move(batch));
                }
            }
            geometry.clear();
            groups.clear();
        }

        void ExportPrints()
        {
            for (const auto& print : prints)
            {
                constexpr uint32_t size = Assets::ProceduralTexture::kDefaultSize;
                const auto maps = Assets::ProceduralTexture::BakeMaps(*print, size);
                const auto append = [](const std::vector<uint8_t>& data)
                {
                    std::vector<uint32_t> words(data.size() / 4);
                    if (!data.empty()) std::memcpy(words.data(), data.data(), data.size());
                    return Append(words);
                };
                textures.push_back({{"size", size}, {"albedo", append(maps.albedo)}, {"normal", append(maps.normal)},
                    {"mra", append(maps.mra)}});
            }
        }
    };

    bool Build(const char* path, std::string& error)
    {
        if (!ScadCsg::BackendAvailable() || !ScadText::Available() || !ScadTess::Available())
        {
            error = "The SCAD scene compiler requires Manifold, FreeType and Earcut.";
            return false;
        }

        LevelBuilder builder;
        const auto started = std::chrono::steady_clock::now();
        const auto elapsed = [&]() {
            return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
        };
        double evaluateMs = 0.0;
        Json diagnostics;
        {
            ScadProgram program;
            SceneEvalResult result;
            Assets::ScadLoadOptions options;
            options.parallelTopLevel = false;
            if (!LoadScadProgram(path, program, error) ||
                !ScadEvaluator::EvaluateScene(program.mainTopLevel, program.modules, program.functions, options, result, error))
            {
                return false;
            }
            evaluateMs = elapsed();
            diagnostics = {{"evaluatedTriangles", result.triangleCount}, {"warningCount", result.warningCount},
                {"warnings", result.warnings}, {"terrainCount", result.terrains.size()}};
            for (const auto& terrain : result.terrains) builder.terrainOwners.insert(terrain.ownerInstanceId);
            for (const auto& root : result.roots)
            {
                builder.AddNode(root, glm::mat4(1.f), true, UINT32_MAX, options.smoothAngleDegrees);
            }
        }
        if (builder.renderTriangles == 0 || builder.collisionInstances.empty() ||
            !std::ranges::any_of(builder.anchors, [](const Json& anchor) { return anchor["type"] == "playerSafe"; }))
        {
            error = "The generated scene has no geometry, collision or safe spawn.";
            return false;
        }

        builder.ReservePacket();
        Json collision = Json::array();
        for (const auto& mesh : builder.collisionMeshes)
        {
            collision.push_back({{"positions", Append(mesh.positions)}, {"indices", Append(mesh.indices)}});
        }
        const auto collisionRecords = Append(builder.collisionInstances);
        builder.collisionMeshes.clear();
        builder.ExportGeometry();
        builder.ExportPrints();
        levelIndex = Json{
            {"triangles", builder.renderTriangles}, {"collisionTriangles", builder.collisionTriangles},
            {"anchors", builder.anchors}, {"bounds", {Point(builder.min), Point(builder.max)}},
            {"batches", builder.batches}, {"surfaces", builder.surfaces}, {"textures", builder.textures},
            {"collision", {{"meshes", collision}, {"records", collisionRecords}}},
            {"diagnostics", diagnostics}, {"evaluateMs", evaluateMs}, {"generateMs", elapsed()}
        }.dump();
        return true;
    }
}

extern "C"
{
    EMSCRIPTEN_KEEPALIVE int ScadGenerateLevel(const char* path)
    {
        using namespace NextDayzScadLevel;
        packet.clear();
        try
        {
            std::string error;
            if (Build(path, error)) return 1;
            levelIndex = std::move(error);
        }
        catch (const std::exception& exception)
        {
            levelIndex = exception.what();
        }
        packet.clear();
        return 0;
    }

    EMSCRIPTEN_KEEPALIVE const uint32_t* ScadLevelPacket()
    {
        return NextDayzScadLevel::packet.data();
    }

    EMSCRIPTEN_KEEPALIVE size_t ScadLevelSize()
    {
        return NextDayzScadLevel::packet.size() * sizeof(uint32_t);
    }

    EMSCRIPTEN_KEEPALIVE const char* ScadLevelIndex()
    {
        return NextDayzScadLevel::levelIndex.c_str();
    }
}
