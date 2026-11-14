// AmbientCascadeLayout: where each voxel / ambient-cube cascade sits in the world.
//
// Every cascade is a 192x192x48 grid on its own world lattice. The lattice phase is today's
// CalculateAmbientCubeOffset(unit, offsetBias), so a cascade's cells line up with the same world
// points whatever its window does. The window -- the part of the lattice the grid currently holds --
// is described by its minimum corner and the cell size, and that pair is the only thing shading,
// baking and the CPU voxelizer read.
//
// Two modes share this one description:
//   * fixed anchor (sys.ambientCubeFollow = 0): every window sits on the lattice origin, cell size
//     is baseUnit * ratio^i. This is exactly the concentric layout the engine always had.
//   * fit scene   (sys.ambientCubeFollow = 1): the ratio adapts so the coarsest cascade covers the
//     scene horizontally. Each window is placed around a focus point and kept inside the scene box:
//     on an axis where the window is at least as large as the scene it simply holds the whole scene;
//     otherwise it is centred on the focus (in Y the focus sits a quarter of the way up, since what
//     matters is above the ground), clamped to the box, and snapped to whole bricks of its lattice.
//
// See docs/plans/ambientcube-clipmap-plan.md (3.1, 3.2).
#pragma once
#include "Engine/Assets/GPU/UniformBuffer.hpp"

#include <array>
#include <cmath>

namespace Assets
{
    struct FAmbientCascadeLayout
    {
        uint32_t cascadeCount = 1;
        // Effective ratio between consecutive cascades. In fit-scene mode this is the adapted value.
        float cascadeRatio = 2.0f;
        bool followScene = false;
        std::array<float, CUBE_CASCADE_MAX> units{};
        // World position of grid cell (0, 0, 0) for each cascade.
        std::array<glm::vec3, CUBE_CASCADE_MAX> origins{};
    };

    struct FAmbientCascadeLayoutInputs
    {
        float baseUnit = CUBE_UNIT;
        glm::vec3 offsetBias{0.0f};
        uint32_t cascadeCount = 1;
        float cascadeRatio = 2.0f;
        bool followScene = false;
        bool hasSceneBounds = false;
        glm::vec3 sceneMin{0.0f};
        glm::vec3 sceneMax{0.0f};
        // Where the windows that cannot hold the whole scene are centred. Without one, the scene
        // centre in XZ and the scene floor in Y.
        bool hasFocus = false;
        glm::vec3 focus{0.0f};
    };

    // Ratio limits in fit-scene mode. Below 1.5 consecutive cascades overlap so much that the extra
    // level buys almost nothing; above 4 the jump in resolution at a cascade edge becomes obvious.
    inline constexpr float kAmbientFitRatioMin = 1.5f;
    inline constexpr float kAmbientFitRatioMax = 4.0f;

    // Cells of horizontal extent the coarsest cascade must span to cover the scene. A window snapped
    // to whole bricks can sit up to half a brick (4 cells) off centre, and two cells per side are
    // kept as margin for the trilinear gather, so 192 - 2 * (4 + 2) = 180 cells reach the scene.
    inline constexpr float kAmbientFitCoverageCells = static_cast<float>(CUBE_SIZE_XY - 2 * (4 + 2));

    inline float ResolveAmbientFitRatio(float baseUnit, uint32_t cascadeCount, float sceneHorizontalSize)
    {
        if (cascadeCount <= 1u || !(sceneHorizontalSize > 0.0f))
        {
            return kAmbientFitRatioMin;
        }
        const float coarsestUnit = sceneHorizontalSize / kAmbientFitCoverageCells;
        const float ratio = std::pow(std::max(coarsestUnit / baseUnit, 1.0f),
                                     1.0f / static_cast<float>(cascadeCount - 1u));
        return std::clamp(ratio, kAmbientFitRatioMin, kAmbientFitRatioMax);
    }

    // Fraction of a window kept below the focus on axes where the window is smaller than the scene.
    // Horizontally the focus is centred; vertically most of what needs light is above the ground.
    inline constexpr float kAmbientFocusAnchor[3] = {0.5f, 0.25f, 0.5f};

    // Window origin for a cascade of `unit` on the lattice anchored at `lattice`, snapped to whole
    // bricks of that lattice. Per axis: a window at least as large as the scene (plus a cell of margin
    // each side) holds all of it -- centred in XZ, on the scene floor in Y; a smaller one sits at
    // kAmbientFocusAnchor around the focus and is clamped to the scene box.
    inline glm::vec3 ResolveAmbientFitWindowOrigin(float unit, const glm::vec3& lattice, const glm::vec3& sceneMin,
                                                   const glm::vec3& sceneMax, const glm::vec3& focus)
    {
        const float brick = unit * static_cast<float>(GPU_SCENE_AMBIENT_BRICK_EDGE);
        const glm::vec3 size = glm::vec3(CUBE_SIZE_XY, CUBE_SIZE_Z, CUBE_SIZE_XY) * unit;
        glm::vec3 origin;
        for (int axis = 0; axis < 3; ++axis)
        {
            const float lo = sceneMin[axis] - unit;
            const float hi = sceneMax[axis] + unit;
            float wanted;
            if (size[axis] >= hi - lo)
            {
                wanted = axis == 1 ? lo : 0.5f * (lo + hi) - 0.5f * size[axis];
            }
            else
            {
                wanted = std::clamp(focus[axis] - kAmbientFocusAnchor[axis] * size[axis], lo, hi - size[axis]);
            }
            // Y rounds down so the floor never rises above the point it was placed under.
            const float steps = (wanted - lattice[axis]) / brick;
            origin[axis] = lattice[axis] + (axis == 1 ? std::floor(steps) : std::round(steps)) * brick;
        }
        return origin;
    }

    inline FAmbientCascadeLayout ResolveAmbientCascadeLayout(const FAmbientCascadeLayoutInputs& inputs)
    {
        FAmbientCascadeLayout layout;
        layout.cascadeCount = std::clamp<uint32_t>(inputs.cascadeCount, 1u, static_cast<uint32_t>(CUBE_CASCADE_MAX));
        const float baseUnit = SanitizeAmbientCubeUnit(inputs.baseUnit);
        const bool fitScene = inputs.followScene && inputs.hasSceneBounds &&
                              glm::all(glm::lessThanEqual(inputs.sceneMin, inputs.sceneMax));
        layout.followScene = fitScene;

        const glm::vec3 sceneSize = inputs.sceneMax - inputs.sceneMin;
        layout.cascadeRatio = fitScene
            ? ResolveAmbientFitRatio(baseUnit, layout.cascadeCount, std::max(sceneSize.x, sceneSize.z))
            : SanitizeAmbientCubeCascadeRatio(inputs.cascadeRatio);

        const glm::vec3 center = 0.5f * (inputs.sceneMin + inputs.sceneMax);
        const glm::vec3 focus = inputs.hasFocus ? inputs.focus : glm::vec3(center.x, inputs.sceneMin.y, center.z);
        for (uint32_t i = 0; i < static_cast<uint32_t>(CUBE_CASCADE_MAX); ++i)
        {
            const float unit = CalculateAmbientCubeCascadeUnit(baseUnit, layout.cascadeRatio, i);
            const glm::vec3 lattice = CalculateAmbientCubeOffset(unit, inputs.offsetBias);
            layout.units[i] = unit;
            layout.origins[i] = fitScene
                ? ResolveAmbientFitWindowOrigin(unit, lattice, inputs.sceneMin, inputs.sceneMax, focus)
                : lattice;
        }
        return layout;
    }
}
