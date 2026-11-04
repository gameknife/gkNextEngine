#include "Engine/Runtime/Scene/SceneBuilder.hpp"

#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Assets/Data/Material.hpp"
#include "Engine/Assets/Data/RigAsset.hpp"
#include "Engine/Runtime/Components/RenderComponent.hpp"

namespace
{
    Assets::FMaterial CreateLambertianMaterial(const glm::vec3& color)
    {
        return {Assets::Material::Lambertian(glm::clamp(color, glm::vec3(0.0f), glm::vec3(1.0f)))};
    }

    Assets::FMaterial CreateDiffuseLightMaterial(const glm::vec3& color, float intensity)
    {
        const glm::vec3 clampedColor = glm::clamp(color, glm::vec3(0.0f), glm::vec3(1.0f));
        return {Assets::Material::DiffuseLight(clampedColor * std::max(0.0f, intensity))};
    }
}

namespace Assets::SceneBuilder
{
    uint32_t AddLambertianMaterial(std::vector<Assets::FMaterial>& materials, const glm::vec3& color)
    {
        materials.push_back(CreateLambertianMaterial(color));
        return static_cast<uint32_t>(materials.size() - 1);
    }

    uint32_t AddSurfaceMaterial(std::vector<Assets::FMaterial>& materials, const glm::vec3& color, float roughness,
                                float metalness)
    {
        const glm::vec3 rgb = glm::clamp(color, glm::vec3(0.0f), glm::vec3(1.0f));
        roughness = glm::clamp(roughness, 0.0f, 1.0f);
        metalness = glm::clamp(metalness, 0.0f, 1.0f);
        if (roughness >= 0.999f && metalness <= 0.001f)
        {
            materials.push_back({Assets::Material::Lambertian(rgb)});
        }
        else
        {
            Assets::Material material = Assets::Material::Mixture(rgb, roughness);
            material.Metalness = metalness;
            materials.push_back({material});
        }
        return static_cast<uint32_t>(materials.size() - 1);
    }

    uint32_t AddRigSectionMaterial(std::vector<Assets::FMaterial>& materials, const Assets::FRigPart& part,
                                   size_t section)
    {
        const glm::vec3 color = section < part.sectionColors.size() ? glm::vec3(part.sectionColors[section])
                                                                    : glm::vec3(0.78f);
        const glm::vec2 surface = part.SectionSurface(section);
        return AddSurfaceMaterial(materials, color, surface.x, surface.y);
    }

    uint32_t AddDiffuseLightMaterial(std::vector<Assets::FMaterial>& materials, const glm::vec3& color, float intensity)
    {
        materials.push_back(CreateDiffuseLightMaterial(color, intensity));
        return static_cast<uint32_t>(materials.size() - 1);
    }

    uint32_t AddLambertianMaterialToScene(Assets::Scene& scene, const glm::vec3& color)
    {
        return scene.AddMaterial(CreateLambertianMaterial(color));
    }

    uint32_t AddDiffuseLightMaterialToScene(Assets::Scene& scene, const glm::vec3& color, float intensity)
    {
        return scene.AddMaterial(CreateDiffuseLightMaterial(color, intensity));
    }

    std::shared_ptr<Assets::Node> CreateRenderNode(
        std::string_view name,
        const glm::vec3& translation,
        const glm::vec3& scale,
        uint32_t instanceId,
        uint32_t modelId,
        uint32_t materialId,
        bool visible,
        const glm::quat& rotation,
        bool rayCastVisible)
    {
        std::array<uint32_t, 16> materialIds{};
        materialIds[0] = materialId;
        return CreateRenderNode(name, translation, scale, instanceId, modelId, materialIds, visible, rotation, rayCastVisible);
    }

    std::shared_ptr<Assets::Node> CreateRenderNode(
        std::string_view name,
        const glm::vec3& translation,
        const glm::vec3& scale,
        uint32_t instanceId,
        uint32_t modelId,
        const std::array<uint32_t, 16>& materialIds,
        bool visible,
        const glm::quat& rotation,
        bool rayCastVisible)
    {
        auto node = Assets::Node::CreateNode(std::string(name), translation, rotation, scale, instanceId);
        auto renderComponent = std::make_shared<Runtime::RenderComponent>();
        renderComponent->SetModelId(modelId);
        renderComponent->SetMaterials(materialIds);
        renderComponent->SetVisible(visible);
        renderComponent->SetRayCastVisible(rayCastVisible);
        node->AddComponent(renderComponent);
        return node;
    }
}
