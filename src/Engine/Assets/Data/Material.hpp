#pragma once

#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Utilities/Glm.hpp"

namespace Assets
{
    // Opt-in surface character on top of colour / roughness / metalness, authored in SCAD
    // as gk_material(..., ior, subsurface, sheen) and gk_foliage(c, transmission, thickness).
    // The default value changes nothing. See Material::Surface for the GPU mapping.
    struct FSurfaceStyle
    {
        float ior = -1.0f;              // < 0: automatic (1.45, or 1.0 = no specular on a roughness-1 surface)
        float subsurface = 0.0f;        // [0,1] wrapped, warm-tinted diffuse terminator (skin, wax, wood)
        float sheen = 0.0f;             // [0,1] grazing fabric fuzz on the silhouette
        float foliageTransmission = 0.0f; // > 0: Lambertian leaf with back-lit diffuse transmission
        float foliageThickness = 0.10f;

        bool IsDefault() const
        {
            return ior < 0.0f && subsurface <= 0.0f && sheen <= 0.0f && foliageTransmission <= 0.0f;
        }
        bool operator==(const FSurfaceStyle& other) const
        {
            return ior == other.ior && subsurface == other.subsurface && sheen == other.sheen &&
                   foliageTransmission == other.foliageTransmission && foliageThickness == other.foliageThickness;
        }
    };

    struct alignas(16) Material final
    {
        static Material Lambertian(const glm::vec3& diffuse, const int32_t textureId = -1)
        {
            return Material{glm::vec4(diffuse, 1), textureId, -1, -1, -1, 1.0f, 1.f, Enum::Lambertian, 0};
        }

        // Opt into primary-surface foliage lighting without changing the BSDF or GPU layout.
        static Material Foliage(const glm::vec3& diffuse, float transmission = 0.7f,
                                float thickness = 0.08f, int32_t textureId = -1)
        {
            auto material = Lambertian(diffuse, textureId);
            material.Reserved2 = std::clamp(transmission, 0.0f, 1.0f);
            material.Reserved3 = std::clamp(thickness, 0.0f, 0.3f);
            return material;
        }

        bool IsFoliage() const { return MaterialModel == Enum::Lambertian && Reserved2 > 0.0f; }

        static Material Metallic(const glm::vec3& diffuse, const float fuzziness, const int32_t textureId = -1)
        {
            return Material{glm::vec4(diffuse, 1), textureId, -1, -1, -1, fuzziness, 1.45f, Enum::Metallic, 1};
        }

        static Material Mixture(const glm::vec3& diffuse, const float fuzziness, const int32_t textureId = -1)
        {
            return Material{glm::vec4(diffuse, 1), textureId, -1, -1, -1, fuzziness, 1.45f, Enum::Mixture, 0, 1.45f};
        }

        // The opaque surface every SCAD-authored colour becomes. Without a style it is the
        // historical mapping: roughness 1 and metalness 0 is a Lambertian, anything else a
        // Mixture at IOR 1.45. A style can turn it into a foliage Lambertian, or into a
        // Mixture whose diffuse lobe carries subsurface wrap (Reserved2) and sheen
        // (Reserved3); a roughness-1 surface then gets IOR 1.0 (no specular film) unless
        // the style names one. The GPU layout is unchanged.
        static Material Surface(const glm::vec3& diffuse, float roughness, float metalness,
                                const FSurfaceStyle& style = {}, int32_t textureId = -1)
        {
            roughness = std::clamp(roughness, 0.0f, 1.0f);
            metalness = std::clamp(metalness, 0.0f, 1.0f);
            const bool matte = roughness >= 0.999f && metalness <= 0.001f;
            if (style.foliageTransmission > 0.0f)
            {
                return Foliage(diffuse, style.foliageTransmission, style.foliageThickness, textureId);
            }
            const bool styled = style.subsurface > 0.0f || style.sheen > 0.0f || style.ior >= 0.0f;
            if (matte && !styled)
            {
                return Lambertian(diffuse, textureId);
            }
            Material material = Mixture(diffuse, roughness, textureId);
            material.Metalness = metalness;
            const float ior = style.ior >= 0.0f ? std::clamp(style.ior, 1.0f, 2.0f) : (matte ? 1.0f : 1.45f);
            material.RefractionIndex = ior;
            material.RefractionIndex2 = ior;
            material.Reserved2 = std::clamp(style.subsurface, 0.0f, 1.0f);
            material.Reserved3 = std::clamp(style.sheen, 0.0f, 1.0f);
            return material;
        }

        static Material Dielectric(const float refractionIndex, const float fuzziness, const int32_t textureId = -1)
        {
            return Material{glm::vec4(1.0f, 1.0f, 1.0f, 1), textureId, -1, -1, -1, fuzziness, refractionIndex, Enum::Dielectric, 0, refractionIndex};
        }

        static Material Isotropic(const glm::vec3& diffuse, const float refractionIndex, const float fuzziness, const int32_t textureId = -1)
        {
            return Material{glm::vec4(diffuse, 1), textureId, -1, -1, -1, fuzziness, refractionIndex, Enum::Isotropic};
        }

        static Material DiffuseLight(const glm::vec3& diffuse, const int32_t textureId = -1)
        {
            return Material{glm::vec4(diffuse, 1), textureId, -1, -1, -1, 0.0f, 0.0f, Enum::DiffuseLight};
        }

        enum class Enum : uint32_t
        {
            Lambertian = 0,
            Metallic = 1,
            Dielectric = 2,
            Isotropic = 3,
            DiffuseLight = 4,
            Mixture = 5,
        };

        // Note: vec3 and vec4 gets aligned on 16 bytes in Vulkan shaders. 

        // Base material
        glm::vec4 Diffuse;
        
        int32_t DiffuseTextureId;
        int32_t MRATextureId;
        int32_t NormalTextureId;
        int32_t EmissiveTextureId;

        // Metal fuzziness
        float Fuzziness;

        // Dielectric refraction index
        float RefractionIndex;

        // Which material are we dealing with
        Enum MaterialModel;

        // metalness
        float Metalness;

        // Second IOR for calculating refraction
        float RefractionIndex2;
        float NormalTextureScale;
        float Reserved2; // Lambertian: foliage transmission [0,1]; Mixture: subsurface wrap [0,1]
        float Reserved3; // Lambertian: foliage thickness in metres [0,0.3]; Mixture: sheen [0,1]
    };

    // For upper usage
    struct FMaterial final
    {
        Material gpuMaterial_;
        std::string name_;
    };
}
