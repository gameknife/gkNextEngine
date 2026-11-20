#pragma once

#include "BakeIO.hpp"
#include "Engine/Assets/Core/Model.hpp"
#include "Engine/Assets/Data/Material.hpp"
#include "Engine/Assets/Loaders/ProceduralTexture.hpp"

namespace NextDayzBake
{
    struct PrintFiles
    {
        std::string color;
        std::string surface;
        std::string normal;

        auto operator<=>(const PrintFiles&) const = default;
    };

    class PrintCache
    {
    public:
        explicit PrintCache(fs::path directory) : directory_(std::move(directory)) {}
        PrintFiles Bake(const Assets::FProceduralTextureRef& print);

    private:
        fs::path directory_;
        std::map<uint64_t, PrintFiles> files_;
    };

    struct Primitive
    {
        std::vector<std::array<float, 3>> positions;
        std::vector<std::array<int8_t, 4>> normals;
        std::vector<std::array<int8_t, 4>> tangents;
        std::vector<std::array<float, 2>> uv;
        std::vector<uint32_t> indices;
        std::vector<std::array<uint16_t, 4>> colors;
        Aabb bounds;

        uint32_t Vertex(const Assets::Vertex& vertex, const glm::mat4& transform, const glm::mat3& normalMatrix,
                        bool textured, bool mirrored);
    };

    class Glb
    {
    public:
        size_t Material(const Assets::FMaterial& material, const PrintFiles& print, bool tintable = false, bool vertexColors = false);
        Json WritePrimitive(const Primitive& mesh, size_t material);
        void Mesh(std::string_view name, Json primitives, Json extras = nullptr);
        struct Transform
        {
            glm::vec3 position;
            glm::quat rotation;
            glm::vec3 scale;
        };

        void Instance(std::span<const Transform> transforms);
        void Save(const fs::path& path);

    private:
        size_t Texture(const std::string& uri);
        size_t Accessor(std::vector<uint8_t> encoded, size_t count, size_t stride, bool indices, int component,
                        std::string_view type, bool normalized = false);

        Json data_ = {
            {"asset", {{"version", "2.0"}, {"generator", "NextDayzBake"}}},
            {"scene", 0}, {"scenes", Json::array({{{"nodes", Json::array()}}})},
            {"nodes", Json::array()}, {"meshes", Json::array()}, {"materials", Json::array()},
            {"accessors", Json::array()}, {"bufferViews", Json::array()},
            {"extensionsRequired", {"EXT_meshopt_compression", "KHR_mesh_quantization"}},
            {"extensionsUsed", {"EXT_meshopt_compression", "KHR_mesh_quantization"}},
        };
        std::map<std::string, size_t> textures_;
        std::vector<uint8_t> buffer_;
        size_t fallbackSize_ = 0;
    };

    void SaveWebp(const fs::path& path, const uint8_t* rgba, int width, int height);
}
