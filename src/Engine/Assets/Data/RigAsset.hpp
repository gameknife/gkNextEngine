#pragma once

// ============================================================================
// RigAsset.hpp - Format-agnostic rigid-body rig asset (no skinning).
//
// A rig is a bone hierarchy where each bone carries zero or more rigid parts
// (whole meshes that follow the bone node transform, Minecraft-entity style),
// plus a set of animation clips that offset bones from their bind pose:
//   L_final = L_bind * T(pos) * R(rot) * S(scale)
//
// Producers live in loader modules (e.g. ScadLoader's FScadRigLoader);
// consumers live in NextGameplay (FRigInstance / FRigAnimator). All data is
// already in engine space (Y-up) by the time it lands here.
// ============================================================================

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "Engine/Assets/Core/Model.hpp"
#include "Engine/Assets/Data/RigDefinition.hpp"
#include "Engine/Assets/Loaders/ProceduralTexture.hpp"
#include "Engine/Utilities/Glm.hpp"
#include <glm/gtc/quaternion.hpp>

namespace Assets
{
    // One renderable chunk of geometry rigidly bound to a bone.
    struct FRigPart
    {
        int32_t bone = -1;
        int32_t modelIndex = -1;                // index into FRigAsset::partModels
        std::vector<glm::vec4> sectionColors;   // baked color per mesh section
        std::vector<glm::vec2> sectionSurfaces; // (roughness, metalness) per section; (1, 0) = Lambertian
        std::vector<bool> sectionTintable;      // section hit the tint placeholder color
        // gk_texture() print per section (null = untextured); the part's vertices
        // carry box-projected UVs for it. Materials multiply color by the bake.
        std::vector<std::shared_ptr<const FProceduralTexture>> sectionTextures;

        // (roughness, metalness) of a section; parts built without surfaces stay Lambertian.
        glm::vec2 SectionSurface(size_t section) const
        {
            return section < sectionSurfaces.size() ? sectionSurfaces[section] : glm::vec2(1.0f, 0.0f);
        }
    };

    struct FRigAsset : FRigDefinition
    {
        std::vector<Model> partModels;    // staging storage until injected into a scene
        std::vector<FRigPart> parts;
    };
} // namespace Assets
