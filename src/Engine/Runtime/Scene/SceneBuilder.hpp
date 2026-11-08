#pragma once

#include "Engine/Assets/AssetsFwd.hpp"
#include "Engine/Common/CoreMinimal.hpp"

#include <glm/ext/quaternion_float.hpp>
#include <glm/vec3.hpp>
#include <string_view>

namespace Assets
{
    struct FRigPart;
}

namespace Assets::SceneBuilder
{
    uint32_t AddLambertianMaterial(std::vector<Assets::FMaterial>& materials, const glm::vec3& color);
    // Lambertian when fully rough and dielectric, otherwise a metal/rough Mixture:
    // the same mapping the SCAD scene loader gives gk_material(). A diffuse
    // texture (gk_texture() print) multiplies the color.
    uint32_t AddSurfaceMaterial(std::vector<Assets::FMaterial>& materials, const glm::vec3& color, float roughness,
                                float metalness, int32_t diffuseTextureId = -1);
    // One section of a rig part: its baked color, its gk_material surface and its gk_texture print.
    uint32_t AddRigSectionMaterial(std::vector<Assets::FMaterial>& materials, const Assets::FRigPart& part,
                                   size_t section);
    uint32_t AddDiffuseLightMaterial(std::vector<Assets::FMaterial>& materials, const glm::vec3& color, float intensity = 1.0f);
    uint32_t AddLambertianMaterialToScene(Assets::Scene& scene, const glm::vec3& color);
    uint32_t AddDiffuseLightMaterialToScene(Assets::Scene& scene, const glm::vec3& color, float intensity = 1.0f);

    std::shared_ptr<Assets::Node> CreateRenderNode(
        std::string_view name,
        const glm::vec3& translation,
        const glm::vec3& scale,
        uint32_t instanceId,
        uint32_t modelId,
        uint32_t materialId,
        bool visible = true,
        const glm::quat& rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
        bool rayCastVisible = true);

    std::shared_ptr<Assets::Node> CreateRenderNode(
        std::string_view name,
        const glm::vec3& translation,
        const glm::vec3& scale,
        uint32_t instanceId,
        uint32_t modelId,
        const std::array<uint32_t, 16>& materialIds,
        bool visible = true,
        const glm::quat& rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
        bool rayCastVisible = true);
}
