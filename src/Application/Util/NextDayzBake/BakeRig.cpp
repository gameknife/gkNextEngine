#include "Engine/Common/CoreMinimal.hpp"
#include "BakeGlb.hpp"

#include "Application/Game/NextDayz/Net/ProtocolVersion.hpp"
#include "Modules/ScadLoader/FScadRig.h"
#include "Modules/ScadLoader/FScadShared.h"

namespace NextDayzBake
{
    namespace
    {
        std::array<float, 4> Quaternion(const glm::quat& q) { return {q.x, q.y, q.z, q.w}; }

        template<class T, class Convert>
        void Keys(Binary& out, const Assets::AnimationChannel<T>& channel, Convert convert)
        {
            out.Count(channel.Keys.size());
            for (const auto& key : channel.Keys)
            {
                out.Scalar(key.Time);
                out.Values(convert(key.Value));
            }
        }

        void WriteRig(const fs::path& path, const Assets::FRigAsset& rig)
        {
            Binary out(path, "NDR1");
            out.Count(rig.bones.size());
            out.Count(rig.clips.size());
            for (const auto& bone : rig.bones)
            {
                out.String(bone.name);
                out.Scalar(bone.parent);
                out.Values(Array(bone.bindT));
                out.Values(Quaternion(bone.bindR));
                out.Values(Array(bone.bindS));
            }

            for (const auto& clip : rig.clips)
            {
                out.String(clip.name);
                out.Scalar(clip.duration);
                out.Scalar(static_cast<uint8_t>(clip.loop));
                out.Count(clip.channels.size());
                for (const auto& channel : clip.channels)
                {
                    out.Scalar(channel.bone);
                    Keys(out, channel.position, [](glm::vec3 v) { return Array(v); });
                    Keys(out, channel.rotation, Quaternion);
                    Keys(out, channel.scale, [](glm::vec3 v) { return Array(v); });
                }
            }
            out.Close();
        }
    }

    void BakeRig(const std::string& source, const fs::path& destination)
    {
        fs::create_directories(destination);
        Assets::FRigAsset rig;
        std::vector<std::string> warnings;
        std::string error;
        if (!Assets::FScadRigLoader::LoadRig(source, {}, rig, error, &warnings))
        {
            throw std::runtime_error(error);
        }

        std::vector<std::string> dependencies;
        Assets::Scad::ScadProgram program;
        if (!Assets::Scad::LoadScadProgram(source, program, error, &dependencies))
        {
            throw std::runtime_error(error);
        }

        const auto sidecars = Assets::Scad::LooseSourceSidecars();
        dependencies.insert(dependencies.end(), sidecars.begin(), sidecars.end());

        const auto name = fs::path(source).stem().string();
        const auto equipment = fs::path(source).parent_path() / (name + ".equipment.json");
        if (fs::exists(equipment))
        {
            dependencies.push_back(equipment.generic_string());
            fs::copy_file(equipment, destination / equipment.filename());
        }
        WriteRig(destination / (name + ".rig.bin"), rig);

        Glb glb;
        PrintCache printCache(destination);
        for (size_t index = 0; index < rig.parts.size(); ++index)
        {
            const auto& part = rig.parts[index];
            const auto& model = rig.partModels.at(part.modelIndex);
            const auto& vertices = model.CPUVertices();
            const auto& indices = model.CPUIndices();
            std::map<uint32_t, Primitive> sections;
            std::vector<uint32_t> remap(vertices.size(), UINT32_MAX);
            for (size_t offset = 0; offset < indices.size(); offset += 3)
            {
                const auto section = vertices[indices[offset]].MaterialIndex;
                auto& batch = sections[section];
                const bool textured = section < part.sectionTextures.size() && !!part.sectionTextures[section];
                for (size_t corner = 0; corner < 3; ++corner)
                {
                    const auto vertex = indices[offset + corner];
                    if (remap[vertex] == UINT32_MAX)
                    {
                        remap[vertex] = batch.Vertex(vertices[vertex], glm::mat4(1), glm::mat3(1), textured, false);
                    }
                    batch.indices.push_back(remap[vertex]);
                }
            }
            Json group = Json::array();
            for (const auto& [section, mesh] : sections)
            {
                const auto surface = part.SectionSurface(section);
                Assets::FMaterial material{};
                material.gpuMaterial_.Diffuse = part.sectionColors.at(section);
                material.gpuMaterial_.MaterialModel = Assets::Material::Enum::Metallic;
                material.gpuMaterial_.Fuzziness = surface.x;
                material.gpuMaterial_.Metalness = surface.y;
                const auto print = section < part.sectionTextures.size() ? part.sectionTextures[section] : nullptr;
                const bool tintable = section < part.sectionTintable.size() && part.sectionTintable[section];
                const auto materialId = glb.Material(material, printCache.Bake(print), tintable);
                group.push_back(glb.WritePrimitive(mesh, materialId));
            }
            if (group.empty()) throw std::runtime_error("Rig part has no triangles");
            const auto& bone = rig.bones.at(part.bone).name;
            glb.Mesh(fmt::format("{}/{}", bone, index), std::move(group), {{"bone", part.bone}, {"part", index}});
        }
        if (rig.parts.empty()) throw std::runtime_error("Rig has no render parts");
        glb.Save(destination / (name + ".glb"));

        Json clips = Json::array();
        for (const auto& clip : rig.clips)
        {
            clips.push_back({{"name", clip.name}, {"duration", clip.duration}, {"channels", clip.channels.size()}});
        }
        WriteJson(destination / "bake.json", {{"protocol", NextDayz::Net::kProtocolVersion}, {"dependencies", dependencies},
                  {"name", name}, {"bones", rig.bones.size()}, {"parts", rig.parts.size()}, {"clips", clips}, {"warnings", warnings}});
    }
}
