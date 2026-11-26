#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "TestCommon.hpp"

#include "Engine/Assets/Core/Model.hpp"
#include "Engine/Assets/GPU/UniformBuffer.hpp"
#include "Engine/Utilities/Math.hpp"

#include <cstddef>

namespace
{
    Assets::CascadeShadowSetup ComputeThirdPersonCascades(float splitLambda)
    {
        // NextDayz third-person camera: 75 degree vertical FOV, 0.1m near plane, global 10km far.
        Assets::EnvironmentSetting env;
        const glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 2.0f, -4.0f), glm::vec3(0.0f, 1.5f, 0.0f),
                                           glm::vec3(0.0f, 1.0f, 0.0f));
        glm::mat4 projection = Utilities::Math::ReverseZPerspective(
            glm::radians(75.0f), 16.0f / 9.0f, 0.1f, 10000.0f);
        projection[1][1] *= -1.0f;
        return env.ComputeSunCascades(projection * view, 0.1f, 10000.0f,
                                      Assets::defaultSunShadowDistance, splitLambda);
    }
}

TEST_CASE("Sun cascade parameters reach the camera UBO", "[Unit][Shadow]")
{
    STATIC_REQUIRE(offsetof(Assets::UniformBufferObject, CascadeDepthRange) ==
                   offsetof(Assets::UniformBufferObject, CascadeTexelWorldSize) + sizeof(glm::vec4));
    STATIC_REQUIRE(sizeof(Assets::UniformBufferObject) % 16 == 0);
}

TEST_CASE("Sun cascade 0 stays close to the camera", "[Unit][Shadow]")
{
    const Assets::CascadeShadowSetup cascades = ComputeThirdPersonCascades(Assets::defaultSunShadowSplitLambda);

    // The old 0.75 split ended cascade 0 at ~25.6m with ~8cm texels, which blurred a character's
    // self-shadow away. Near-camera detail needs cascade 0 within a few meters and ~2cm texels.
    REQUIRE(cascades.splits[0] < 8.0f);
    REQUIRE(cascades.texelWorldSize[0] < 0.03f);
    REQUIRE(cascades.splits[3] == Catch::Approx(Assets::defaultSunShadowDistance));

    for (int c = 0; c < 4; ++c)
    {
        REQUIRE(cascades.texelWorldSize[c] > 0.0f);
        REQUIRE(cascades.depthRange[c] > cascades.texelWorldSize[c] * Assets::SUN_SHADOW_RESOLUTION);
        if (c > 0)
        {
            REQUIRE(cascades.splits[c] > cascades.splits[c - 1]);
            REQUIRE(cascades.texelWorldSize[c] > cascades.texelWorldSize[c - 1]);
        }
    }

    // Lambda is the knob: a more uniform split pushes cascade 0 back out.
    const Assets::CascadeShadowSetup uniform = ComputeThirdPersonCascades(0.0f);
    REQUIRE(uniform.splits[0] > cascades.splits[0]);
}
