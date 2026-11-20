#include "Engine/Common/CoreMinimal.hpp"
#include "BakeGlb.hpp"

#include <meshoptimizer.h>
#include <webp/encode.h>
#include <xxhash.h>

namespace NextDayzBake
{
    std::vector<uint8_t> EncodeVertices(const void* data, size_t count, size_t stride)
    {
        std::vector<uint8_t> encoded(meshopt_encodeVertexBufferBound(count, stride));
        const size_t size = meshopt_encodeVertexBufferLevel(encoded.data(), encoded.size(), data, count, stride, 2, 0);
        if (size == 0) throw std::runtime_error("Could not encode vertex stream");
        encoded.resize(size);
        return encoded;
    }

    std::vector<uint8_t> EncodeTriangles(const std::vector<uint32_t>& indices, size_t vertexCount)
    {
        std::vector<uint8_t> encoded(meshopt_encodeIndexBufferBound(indices.size(), vertexCount));
        const size_t size = meshopt_encodeIndexBuffer(encoded.data(), encoded.size(), indices.data(), indices.size());
        if (size == 0) throw std::runtime_error("Could not encode triangle stream");
        encoded.resize(size);
        return encoded;
    }

    void SaveWebp(const fs::path& path, const uint8_t* rgba, int width, int height)
    {
        uint8_t* bytes = nullptr;
        const size_t length = WebPEncodeLosslessRGBA(rgba, width, height, width * 4, &bytes);
        const std::unique_ptr<uint8_t, decltype(&WebPFree)> encoded(bytes, &WebPFree);
        if (!length) throw std::runtime_error("Could not encode " + path.string());
        auto out = Output(path);
        out.write(reinterpret_cast<const char*>(bytes), length);
        out.close();
    }

    PrintFiles PrintCache::Bake(const Assets::FProceduralTextureRef& print)
    {
        if (!print || !print->graph) return {};
        const auto [found, inserted] = files_.try_emplace(print->Hash());
        if (inserted)
        {
            using namespace Assets::ProceduralTexture;
            const auto maps = BakeMaps(*print);
            const auto save = [&](const std::vector<uint8_t>& rgba)
            {
                const auto filename = fmt::format("texture-{:016x}.webp", XXH64(rgba.data(), rgba.size(), 0));
                const auto path = directory_ / filename;
                if (!fs::exists(path)) SaveWebp(path, rgba.data(), kDefaultSize, kDefaultSize);
                return filename;
            };
            found->second.color = save(maps.albedo);
            if (maps.hasMra) found->second.surface = save(maps.mra);
            if (maps.hasNormal) found->second.normal = save(maps.normal);
        }
        return found->second;
    }

    uint32_t Primitive::Vertex(const Assets::Vertex& vertex, const glm::mat4& transform, const glm::mat3& normalMatrix,
                               bool textured, bool mirrored)
    {
        const auto index = static_cast<uint32_t>(positions.size());
        const glm::vec3 point(transform * glm::vec4(vertex.Position, 1));
        const glm::vec3 normal = glm::normalize(normalMatrix * vertex.Normal);
        const auto pack = [](float value) { return static_cast<int8_t>(meshopt_quantizeSnorm(value, 8)); };
        positions.push_back(Array(point));
        bounds.Include(point);
        normals.push_back({pack(normal.x), pack(normal.y), pack(normal.z), 0});
        if (textured)
        {
            glm::vec3 tangent = glm::mat3(transform) * glm::vec3(vertex.Tangent);
            tangent = glm::normalize(tangent - glm::dot(tangent, normal) * normal);
            const float sign = (vertex.Tangent.w < 0) != mirrored ? -1.f : 1.f;
            tangents.push_back({pack(tangent.x), pack(tangent.y), pack(tangent.z), pack(sign)});
            uv.push_back(Array(vertex.TexCoord));
        }
        return index;
    }

    size_t Glb::Texture(const std::string& uri)
    {
        const auto [found, inserted] = textures_.try_emplace(uri, textures_.size());
        if (inserted)
        {
            if (textures_.size() == 1)
            {
                for (const auto* field : {"extensionsRequired", "extensionsUsed"})
                {
                    data_[field].push_back("EXT_texture_webp");
                }
            }
            data_["images"].push_back({{"uri", uri}, {"mimeType", "image/webp"}});
            data_["textures"].push_back({{"extensions", {{"EXT_texture_webp", {{"source", found->second}}}}}});
        }
        return found->second;
    }

    size_t Glb::Material(const Assets::FMaterial& material, const PrintFiles& print, bool tintable, bool vertexColors)
    {
        const auto& value = material.gpuMaterial_;
        const auto roughness = value.MaterialModel == Assets::Material::Enum::Lambertian
            ? 1.f : std::clamp(value.Fuzziness, 0.f, 1.f);
        Json pbr = {
            {"baseColorFactor", Array(vertexColors ? glm::vec4(1) : glm::clamp(value.Diffuse, glm::vec4(0), glm::vec4(1)))},
            {"roughnessFactor", roughness}, {"metallicFactor", std::clamp(value.Metalness, 0.f, 1.f)},
        };
        if (!print.color.empty()) pbr["baseColorTexture"] = {{"index", Texture(print.color)}};
        if (!print.surface.empty()) pbr["metallicRoughnessTexture"] = {{"index", Texture(print.surface)}};
        Json out = {{"name", material.name_}, {"pbrMetallicRoughness", std::move(pbr)}};
        if (!print.normal.empty()) out["normalTexture"] = {{"index", Texture(print.normal)}};
        if (value.Diffuse.a < 0.99f)
        {
            out["alphaMode"] = "BLEND";
            out["doubleSided"] = true;
        }
        if (tintable) out["extras"] = {{"tintable", true}};
        const auto index = data_["materials"].size();
        data_["materials"].push_back(std::move(out));
        return index;
    }

    size_t Glb::Accessor(std::vector<uint8_t> encoded, size_t count, size_t stride, bool indices, int component,
                         std::string_view type, bool normalized)
    {
        buffer_.resize((buffer_.size() + 3) & ~size_t{3}, 0);
        fallbackSize_ = (fallbackSize_ + 3) & ~size_t{3};
        const size_t viewIndex = data_["bufferViews"].size();
        Json compression = {
            {"buffer", 0}, {"byteOffset", buffer_.size()}, {"byteLength", encoded.size()},
            {"byteStride", stride}, {"count", count}, {"mode", indices ? "TRIANGLES" : "ATTRIBUTES"},
        };
        Json view = {
            {"buffer", 1}, {"byteOffset", fallbackSize_}, {"byteLength", count * stride},
            {"target", indices ? 34963 : 34962}, {"extensions", {{"EXT_meshopt_compression", std::move(compression)}}},
        };
        if (!indices) view["byteStride"] = stride;
        data_["bufferViews"].push_back(std::move(view));
        fallbackSize_ += count * stride;
        buffer_.insert(buffer_.end(), encoded.begin(), encoded.end());

        Json accessor = {{"bufferView", viewIndex}, {"componentType", component}, {"count", count}, {"type", type}};
        if (normalized) accessor["normalized"] = true;
        const size_t index = data_["accessors"].size();
        data_["accessors"].push_back(std::move(accessor));
        return index;
    }

    Json Glb::WritePrimitive(const Primitive& mesh, size_t material)
    {
        const auto add = [&](const auto& stream, int component, std::string_view type, bool normalized = false)
        {
            const size_t stride = sizeof(stream.front());
            return Accessor(EncodeVertices(stream.data(), stream.size(), stride), stream.size(), stride,
                            false, component, type, normalized);
        };
        Json attributes;
        const auto position = add(mesh.positions, 5126, "VEC3");
        data_["accessors"][position]["min"] = Array(mesh.bounds.min);
        data_["accessors"][position]["max"] = Array(mesh.bounds.max);
        attributes["POSITION"] = position;
        attributes["NORMAL"] = add(mesh.normals, 5120, "VEC3", true);
        if (!mesh.uv.empty())
        {
            attributes["TEXCOORD_0"] = add(mesh.uv, 5126, "VEC2");
            attributes["TANGENT"] = add(mesh.tangents, 5120, "VEC4", true);
        }
        if (!mesh.colors.empty()) attributes["COLOR_0"] = add(mesh.colors, 5123, "VEC4", true);
        const auto indices = Accessor(EncodeTriangles(mesh.indices, mesh.positions.size()), mesh.indices.size(), 4,
                                      true, 5125, "SCALAR");
        return {{"attributes", std::move(attributes)}, {"indices", indices}, {"material", material}};
    }

    void Glb::Mesh(std::string_view name, Json primitives, Json extras)
    {
        const size_t meshIndex = data_["meshes"].size();
        const size_t nodeIndex = data_["nodes"].size();
        data_["meshes"].push_back({{"primitives", std::move(primitives)}});
        Json node = {{"mesh", meshIndex}, {"name", name}};
        if (!extras.is_null()) node["extras"] = std::move(extras);
        data_["nodes"].push_back(std::move(node));
        data_["scenes"][0]["nodes"].push_back(nodeIndex);
    }

    void Glb::Instance(std::span<const Transform> transforms)
    {
        for (const auto* field : {"extensionsRequired", "extensionsUsed"})
        {
            if (std::ranges::find(data_[field], Json("EXT_mesh_gpu_instancing")) == data_[field].end())
            {
                data_[field].push_back("EXT_mesh_gpu_instancing");
            }
        }
        std::vector<std::array<float, 3>> positions, scales;
        std::vector<std::array<float, 4>> rotations;
        for (const auto& transform : transforms)
        {
            positions.push_back(Array(transform.position));
            const auto& q = transform.rotation;
            rotations.push_back({q.x, q.y, q.z, q.w});
            scales.push_back(Array(transform.scale));
        }
        const auto add = [&](const auto& stream, std::string_view type)
        {
            const size_t stride = sizeof(stream.front());
            return Accessor(EncodeVertices(stream.data(), stream.size(), stride),
                            stream.size(), stride, false, 5126, type);
        };
        data_["nodes"].back()["extensions"]["EXT_mesh_gpu_instancing"]["attributes"] = {
            {"TRANSLATION", add(positions, "VEC3")}, {"ROTATION", add(rotations, "VEC4")}, {"SCALE", add(scales, "VEC3")},
        };
    }

    void Glb::Save(const fs::path& path)
    {
        buffer_.resize((buffer_.size() + 3) & ~size_t{3}, 0);
        data_["buffers"] = Json::array({
            {{"byteLength", buffer_.size()}},
            {{"byteLength", fallbackSize_}, {"extensions", {{"EXT_meshopt_compression", {{"fallback", true}}}}}},
        });
        std::string encoded = data_.dump();
        encoded.resize((encoded.size() + 3) & ~size_t{3}, ' ');
        Binary out(path, "glTF");
        out.Scalar(uint32_t{2});
        out.Count(28 + encoded.size() + buffer_.size());
        out.Count(encoded.size());
        out.Scalar(uint32_t{0x4e4f534a});
        out.Bytes(encoded.data(), encoded.size());
        out.Count(buffer_.size());
        out.Scalar(uint32_t{0x004e4942});
        out.Bytes(buffer_.data(), buffer_.size());
        out.Close();
    }
}
