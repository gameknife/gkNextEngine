#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Runtime/GameInstance.hpp"
#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Assets/Data/Material.hpp"
#include "Engine/Runtime/Components/RenderComponent.hpp"
#include "Engine/Runtime/Components/TerrainComponent.hpp"
#include "Modules/ScadLoader/FScadLoader.h"
#include "Modules/ScadLoader/FScadShared.h"
#include "BakeIO.hpp"

#include <bit>
#include <limits>
#include <tuple>
#include <nlohmann/json.hpp>
#define TINYGLTF_NO_STB_IMAGE
#include <tiny_gltf.h>

std::unique_ptr<NextGameInstanceBase> CreateGameInstance(Vulkan::WindowConfig& config,
    Runtime::Config::Options& options, NextEngine* engine)
{
    return std::make_unique<NextGameInstanceVoid>(config, options, engine);
}

namespace
{
    using Clock = std::chrono::steady_clock;

    struct Bounds
    {
        glm::vec3 min{std::numeric_limits<float>::max()};
        glm::vec3 max{-std::numeric_limits<float>::max()};

        void Include(glm::vec3 point)
        {
            min = glm::min(min, point);
            max = glm::max(max, point);
        }
    };

    std::array<float, 3> Components(glm::vec3 value)
    {
        return {value.x, value.y, value.z};
    }

    struct Batch
    {
        std::vector<std::array<float, 3>> positions;
        std::vector<std::array<float, 3>> normals;
        std::vector<uint32_t> indices;
        Bounds bounds;
    };

    struct Collision
    {
        std::vector<std::array<float, 3>> positions;
        std::vector<uint32_t> indices;

        void Save(const std::filesystem::path& path) const
        {
            static_assert(std::endian::native == std::endian::little);
            constexpr uint32_t magic = 0x3043444e; // NDC0
            const std::array header{magic, static_cast<uint32_t>(positions.size()),
                                    static_cast<uint32_t>(indices.size() / 3)};
            std::ofstream file(path, std::ios::binary);
            file.exceptions(std::ios::badbit | std::ios::failbit);
            file.write(reinterpret_cast<const char*>(header.data()), sizeof(header));
            file.write(reinterpret_cast<const char*>(positions.data()), positions.size() * sizeof(positions[0]));
            file.write(reinterpret_cast<const char*>(indices.data()), indices.size() * sizeof(indices[0]));
            file.close();
        }
    };

    template<class T>
    int AppendAccessor(tinygltf::Model& gltf, const std::vector<T>& values, int componentType, int type, int target)
    {
        auto& data = gltf.buffers.front().data;
        auto& view = gltf.bufferViews.emplace_back();
        view.buffer = 0;
        view.byteOffset = data.size();
        view.byteLength = values.size() * sizeof(T);
        view.target = target;
        const auto* bytes = reinterpret_cast<const unsigned char*>(values.data());
        data.insert(data.end(), bytes, bytes + view.byteLength);
        auto& value = gltf.accessors.emplace_back();
        value.bufferView = static_cast<int>(gltf.bufferViews.size() - 1);
        value.componentType = componentType;
        value.type = type;
        value.count = values.size();
        return static_cast<int>(gltf.accessors.size() - 1);
    }

    void AddBatch(tinygltf::Model& gltf, const Batch& batch, uint32_t material)
    {
        tinygltf::Primitive primitive;
        primitive.material = static_cast<int>(material);
        primitive.mode = TINYGLTF_MODE_TRIANGLES;
        primitive.attributes["POSITION"] = AppendAccessor(gltf, batch.positions, TINYGLTF_COMPONENT_TYPE_FLOAT,
                                                          TINYGLTF_TYPE_VEC3, TINYGLTF_TARGET_ARRAY_BUFFER);
        gltf.accessors.back().minValues = {batch.bounds.min.x, batch.bounds.min.y, batch.bounds.min.z};
        gltf.accessors.back().maxValues = {batch.bounds.max.x, batch.bounds.max.y, batch.bounds.max.z};
        primitive.attributes["NORMAL"] = AppendAccessor(gltf, batch.normals, TINYGLTF_COMPONENT_TYPE_FLOAT,
                                                        TINYGLTF_TYPE_VEC3, TINYGLTF_TARGET_ARRAY_BUFFER);
        primitive.indices = AppendAccessor(gltf, batch.indices, TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT,
                                           TINYGLTF_TYPE_SCALAR, TINYGLTF_TARGET_ELEMENT_ARRAY_BUFFER);
        gltf.meshes.emplace_back().primitives.push_back(std::move(primitive));
        gltf.nodes.emplace_back().mesh = static_cast<int>(gltf.meshes.size() - 1);
        gltf.scenes.front().nodes.push_back(static_cast<int>(gltf.nodes.size() - 1));
    }

    void Bake(const std::string& source, const std::filesystem::path& destination)
    {
        Assets::EnvironmentSetting environment;
        std::vector<std::shared_ptr<Assets::Node>> nodes;
        std::vector<Assets::Model> models;
        std::vector<Assets::FMaterial> materials;
        std::vector<Assets::LightObject> lights;
        std::vector<Assets::AnimationTrack> tracks;
        std::vector<Assets::Skeleton> skeletons;
        const auto started = Clock::now();
        if (!Assets::FScadLoader::LoadScadScene(source, environment, nodes, models, materials, lights, tracks, skeletons))
        {
            throw std::runtime_error("SCAD load failed");
        }
        const auto loaded = Clock::now();

        using BatchKey = std::tuple<int, int, uint32_t>;
        std::map<BatchKey, Batch> batches;
        const auto batchFor = [&batches](glm::vec3 center, uint32_t material) -> Batch&
        {
            constexpr float chunkSize = 128.0f;
            return batches[{static_cast<int>(std::floor(center.x / chunkSize)),
                            static_cast<int>(std::floor(center.z / chunkSize)), material}];
        };
        Collision collision;
        Bounds bounds;
        std::vector<std::array<float, 3>> spawns;
        size_t desktopTriangles = 0;
        for (const auto& node : nodes)
        {
            const std::string& name = node->GetName();
            if (name == "nd_spawn_player_safe")
            {
                spawns.push_back(Components(node->WorldTranslation()));
            }

            const auto* render = node->GetComponent<Runtime::RenderComponent>();
            if (!render || !render->IsDrawable())
            {
                continue;
            }
            const auto& model = models.at(render->GetModelId());
            const auto& vertices = model.CPUVertices();
            const auto& indices = model.CPUIndices();
            if (indices.empty())
            {
                continue;
            }

            const glm::mat4 transform = node->WorldTransform();
            const glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(transform)));
            const bool mirrored = glm::determinant(glm::mat3(transform)) < 0;
            const glm::vec3 center = node->WorldTranslation();
            std::vector<glm::vec3> worldVertices;
            worldVertices.reserve(vertices.size());
            for (const auto& vertex : vertices)
            {
                worldVertices.emplace_back(transform * glm::vec4(vertex.Position, 1.0f));
            }

            // Scene::RebuildMeshBuffer uses raycast participation and makes
            // an exception to its size limit for terrain models.
            if (render->GetRayCastVisible() &&
                (indices.size() < 65535 * 3 || node->GetComponent<Runtime::TerrainComponent>()))
            {
                const auto offset = static_cast<uint32_t>(collision.positions.size());
                for (const auto& vertex : worldVertices)
                {
                    collision.positions.push_back(Components(vertex));
                }
                for (size_t i = 0; i < indices.size(); i += 3)
                {
                    collision.indices.push_back(offset + indices[i]);
                    collision.indices.push_back(offset + indices[i + (mirrored ? 2 : 1)]);
                    collision.indices.push_back(offset + indices[i + (mirrored ? 1 : 2)]);
                }
            }

            if (!render->GetVisible() || !render->GetMainVisible())
            {
                continue;
            }
            desktopTriangles += indices.size() / 3;
            std::vector<uint32_t> remap(vertices.size(), UINT32_MAX);
            for (size_t i = 0; i < indices.size(); i += 3)
            {
                const uint32_t material = render->GetMaterials().at(vertices[indices[i]].MaterialIndex);
                Batch& batch = batchFor(center, material);
                const std::array corners{indices[i], indices[i + (mirrored ? 2 : 1)], indices[i + (mirrored ? 1 : 2)]};
                for (const uint32_t index : corners)
                {
                    if (remap[index] == UINT32_MAX)
                    {
                        const auto point = worldVertices[index];
                        remap[index] = static_cast<uint32_t>(batch.positions.size());
                        batch.positions.push_back(Components(point));
                        batch.normals.push_back(Components(glm::normalize(normalMatrix * vertices[index].Normal)));
                        bounds.Include(point);
                        batch.bounds.Include(point);
                    }
                    batch.indices.push_back(remap[index]);
                }
            }
        }

        tinygltf::Model gltf;
        gltf.asset.version = "2.0";
        gltf.asset.generator = "NextDayz W0 bake probe";
        gltf.defaultScene = 0;
        gltf.scenes.emplace_back();
        gltf.buffers.emplace_back();
        for (const auto& material : materials)
        {
            auto& out = gltf.materials.emplace_back();
            const auto& value = material.gpuMaterial_;
            out.name = material.name_;
            out.pbrMetallicRoughness.baseColorFactor = {value.Diffuse.r, value.Diffuse.g, value.Diffuse.b, value.Diffuse.a};
            out.pbrMetallicRoughness.roughnessFactor = value.MaterialModel == Assets::Material::Enum::Lambertian
                ? 1.0 : std::clamp(static_cast<double>(value.Fuzziness), 0.05, 1.0);
            out.pbrMetallicRoughness.metallicFactor = value.Metalness;
            if (value.Diffuse.a < 0.99f)
            {
                out.alphaMode = "BLEND";
                out.doubleSided = true;
            }
        }
        size_t triangles = 0;
        for (const auto& [key, batch] : batches)
        {
            AddBatch(gltf, batch, std::get<2>(key));
            triangles += batch.indices.size() / 3;
        }
        if (triangles != desktopTriangles || batches.empty() || collision.indices.empty() || spawns.empty())
        {
            throw std::runtime_error("unexpected scene or mismatched triangle count");
        }

        std::filesystem::create_directories(destination);
        const auto glbPath = destination / "scene.glb";
        tinygltf::TinyGLTF writer;
        if (!writer.WriteGltfSceneToFile(&gltf, glbPath.string(), true, true, false, true))
        {
            throw std::runtime_error("GLB write failed");
        }
        const auto collisionPath = destination / "collision.bin";
        collision.Save(collisionPath);
        const auto finished = Clock::now();

        const nlohmann::json stats{
            {"source", source}, {"triangles", triangles}, {"desktopTriangles", desktopTriangles},
            {"collisionTriangles", collision.indices.size() / 3}, {"batches", batches.size()},
            {"glbBytes", std::filesystem::file_size(glbPath)}, {"collisionBytes", std::filesystem::file_size(collisionPath)},
            {"scadMs", std::chrono::duration_cast<std::chrono::milliseconds>(loaded - started).count()},
            {"bakeMs", std::chrono::duration_cast<std::chrono::milliseconds>(finished - loaded).count()},
            {"bounds", {Components(bounds.min), Components(bounds.max)}}, {"spawns", spawns},
        };
        std::ofstream summary(destination / "bake.json");
        summary.exceptions(std::ios::badbit | std::ios::failbit);
        summary << stats.dump(2) << '\n';
        summary.close();
        std::cout << stats.dump() << '\n';
    }
}

int main(int argc, const char* argv[])
{
    try
    {
        if (argc == 4 && std::string_view(argv[1]) == "map")
        {
            Assets::Scad::SetLooseSourceRoot(std::filesystem::current_path());
            NextDayzBake::BakeMap(argv[2], argv[3]);
            return 0;
        }
        if (argc == 4 && std::string_view(argv[1]) == "rig")
        {
            Assets::Scad::SetLooseSourceRoot(std::filesystem::current_path());
            NextDayzBake::BakeRig(argv[2], argv[3]);
            return 0;
        }
        if (argc == 3 && std::string_view(argv[1]) == "shared")
        {
            NextDayzBake::BakeShared(argv[2]);
            return 0;
        }
        if (argc == 3)
        {
            Bake(argv[1], argv[2]);
            return 0;
        }
        std::cerr << "Usage: NextDayzBake [map|rig] <source.scad> <output directory> | shared <output directory>\n";
        return 2;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
