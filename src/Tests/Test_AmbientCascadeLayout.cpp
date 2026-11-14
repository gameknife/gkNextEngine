#include "TestCommon.hpp"

#include "Engine/Assets/Acceleration/AmbientCascadeLayout.hpp"

#include <cmath>

namespace
{
    // How far `value` sits from the lattice anchored at `lattice` with spacing `step`, in steps.
    float LatticeResidual(float value, float lattice, float step)
    {
        const float steps = (value - lattice) / step;
        return std::abs(steps - std::round(steps));
    }
}

TEST_CASE("Fixed-anchor ambient cascades reproduce the concentric layout", "[Unit][AmbientCascadeLayout]")
{
    Assets::FAmbientCascadeLayoutInputs inputs;
    inputs.baseUnit = 0.25f;
    inputs.offsetBias = glm::vec3(0.02f, 0.0f, 0.02f);
    inputs.cascadeCount = 3;
    inputs.cascadeRatio = 2.0f;
    // Scene bounds are ignored unless the cascades follow the scene.
    inputs.hasSceneBounds = true;
    inputs.sceneMin = glm::vec3(-400.0f, 0.0f, -400.0f);
    inputs.sceneMax = glm::vec3(400.0f, 30.0f, 400.0f);

    const Assets::FAmbientCascadeLayout layout = Assets::ResolveAmbientCascadeLayout(inputs);
    CHECK_FALSE(layout.followScene);
    CHECK(layout.cascadeCount == 3u);
    CHECK(layout.cascadeRatio == 2.0f);
    for (uint32_t i = 0; i < 3u; ++i)
    {
        const float unit = Assets::CalculateAmbientCubeCascadeUnit(0.25f, 2.0f, i);
        CHECK(layout.units[i] == unit);
        const glm::vec3 expected = Assets::CalculateAmbientCubeOffset(unit, inputs.offsetBias);
        CHECK(glm::length(layout.origins[i] - expected) < 1e-5f);
    }
}

TEST_CASE("Fit-scene ambient cascades cover the map with the coarsest cascade", "[Unit][AmbientCascadeLayout]")
{
    Assets::FAmbientCascadeLayoutInputs inputs;
    inputs.baseUnit = 0.25f;
    inputs.cascadeCount = 5;
    inputs.followScene = true;
    inputs.hasSceneBounds = true;
    inputs.sceneMin = glm::vec3(-410.0f, 6.5f, -380.0f);
    inputs.sceneMax = glm::vec3(390.0f, 28.7f, 420.0f);

    const Assets::FAmbientCascadeLayout layout = Assets::ResolveAmbientCascadeLayout(inputs);
    REQUIRE(layout.followScene);
    REQUIRE(layout.cascadeCount == 5u);
    CHECK(layout.cascadeRatio >= Assets::kAmbientFitRatioMin);
    CHECK(layout.cascadeRatio <= Assets::kAmbientFitRatioMax);
    // Cascade 0 keeps the configured cell size; the ratio stretches the rest.
    CHECK(layout.units[0] == 0.25f);
    CHECK(layout.units[4] == Catch::Approx(800.0f / Assets::kAmbientFitCoverageCells).epsilon(1e-4));

    for (uint32_t i = 0; i < layout.cascadeCount; ++i)
    {
        const float unit = layout.units[i];
        const glm::vec3 lattice = Assets::CalculateAmbientCubeOffset(unit, inputs.offsetBias);
        const float brick = unit * static_cast<float>(Assets::GPU_SCENE_AMBIENT_BRICK_EDGE);
        INFO("cascade " << i);
        // Windows are whole bricks of their own lattice, so cells stay on the world lattice.
        CHECK(LatticeResidual(layout.origins[i].x, lattice.x, brick) < 1e-3f);
        CHECK(LatticeResidual(layout.origins[i].y, lattice.y, brick) < 1e-3f);
        CHECK(LatticeResidual(layout.origins[i].z, lattice.z, brick) < 1e-3f);
        // The floor sits under the scene's lowest point.
        CHECK(layout.origins[i].y <= inputs.sceneMin.y - unit + 1e-3f);
        CHECK(layout.origins[i].y > inputs.sceneMin.y - unit - brick - 1e-3f);
    }

    // The coarsest cascade holds the whole map in XZ with two cells of margin for the gather.
    const float unit = layout.units[4];
    const glm::vec3 lo = layout.origins[4];
    const glm::vec3 hi = lo + glm::vec3(Assets::CUBE_SIZE_XY, Assets::CUBE_SIZE_Z, Assets::CUBE_SIZE_XY) * unit;
    CHECK(lo.x <= inputs.sceneMin.x - 2.0f * unit + 1e-3f);
    CHECK(lo.z <= inputs.sceneMin.z - 2.0f * unit + 1e-3f);
    CHECK(hi.x >= inputs.sceneMax.x + 2.0f * unit - 1e-3f);
    CHECK(hi.z >= inputs.sceneMax.z + 2.0f * unit - 1e-3f);
    CHECK(hi.y >= inputs.sceneMax.y);
}

TEST_CASE("Fit-scene ambient cascades clamp the ratio on huge and tiny maps", "[Unit][AmbientCascadeLayout]")
{
    Assets::FAmbientCascadeLayoutInputs inputs;
    inputs.baseUnit = 0.25f;
    inputs.cascadeCount = 5;
    inputs.followScene = true;
    inputs.hasSceneBounds = true;

    inputs.sceneMin = glm::vec3(-20000.0f, 0.0f, -20000.0f);
    inputs.sceneMax = glm::vec3(20000.0f, 10.0f, 20000.0f);
    CHECK(Assets::ResolveAmbientCascadeLayout(inputs).cascadeRatio == Assets::kAmbientFitRatioMax);

    inputs.sceneMin = glm::vec3(-5.0f, 0.0f, -5.0f);
    inputs.sceneMax = glm::vec3(5.0f, 3.0f, 5.0f);
    CHECK(Assets::ResolveAmbientCascadeLayout(inputs).cascadeRatio == Assets::kAmbientFitRatioMin);

    // No bounds: fall back to the fixed anchor rather than fitting a degenerate box.
    inputs.hasSceneBounds = false;
    CHECK_FALSE(Assets::ResolveAmbientCascadeLayout(inputs).followScene);
}

TEST_CASE("Fit-scene ambient cascades smaller than the scene hold the ground at the focus",
          "[Unit][AmbientCascadeLayout]")
{
    // A block terrain: the skirt reaches 40 m under a surface at about y = 10.
    Assets::FAmbientCascadeLayoutInputs inputs;
    inputs.baseUnit = 0.25f;
    inputs.cascadeCount = 5;
    inputs.followScene = true;
    inputs.hasSceneBounds = true;
    inputs.sceneMin = glm::vec3(-400.0f, -33.5f, -400.0f);
    inputs.sceneMax = glm::vec3(400.5f, 51.5f, 400.0f);
    inputs.hasFocus = true;
    inputs.focus = glm::vec3(0.0f, 10.0f, 0.0f);

    const Assets::FAmbientCascadeLayout layout = Assets::ResolveAmbientCascadeLayout(inputs);
    REQUIRE(layout.followScene);
    for (uint32_t i = 0; i < layout.cascadeCount; ++i)
    {
        INFO("cascade " << i);
        const float unit = layout.units[i];
        const glm::vec3 lo = layout.origins[i];
        const glm::vec3 hi = lo + glm::vec3(Assets::CUBE_SIZE_XY, Assets::CUBE_SIZE_Z, Assets::CUBE_SIZE_XY) * unit;
        // Every cascade holds the focus with room above it, and never starts below the scene.
        CHECK(lo.y <= inputs.focus.y);
        CHECK(hi.y >= inputs.focus.y + 0.25f * (hi.y - lo.y));
        CHECK(lo.y >= inputs.sceneMin.y - unit - unit * Assets::GPU_SCENE_AMBIENT_BRICK_EDGE);
        CHECK(lo.x <= inputs.focus.x);
        CHECK(hi.x >= inputs.focus.x);
    }
    // The finest cascade does not waste itself on the skirt: its floor is near the ground.
    CHECK(layout.origins[0].y > inputs.focus.y - 0.25f * Assets::CUBE_SIZE_Z * layout.units[0] - 2.0f);
    // A cascade taller than the scene keeps holding all of it.
    const float coarseUnit = layout.units[4];
    CHECK(layout.origins[4].y <= inputs.sceneMin.y);
    CHECK(layout.origins[4].y + Assets::CUBE_SIZE_Z * coarseUnit >= inputs.sceneMax.y);
}
