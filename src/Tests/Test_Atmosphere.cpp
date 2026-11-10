#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "TestCommon.hpp"

#include "Engine/Assets/GPU/UniformBuffer.hpp"
#include "Engine/Assets/Core/Model.hpp"
#include "Engine/Rendering/Atmosphere/SkyIrradianceProjector.hpp"

#include <cstddef>

TEST_CASE("Atmosphere parameter layout matches the Slang contract", "[Unit][Atmosphere]")
{
    STATIC_REQUIRE(alignof(Assets::FAtmosphereParams) == 16);
    STATIC_REQUIRE(sizeof(Assets::FAtmosphereParams) == 160);
    STATIC_REQUIRE(offsetof(Assets::FAtmosphereParams, RayleighScattering) == 0);
    STATIC_REQUIRE(offsetof(Assets::FAtmosphereParams, MieScattering) == 16);
    STATIC_REQUIRE(offsetof(Assets::FAtmosphereParams, OzoneAbsorption) == 48);
    STATIC_REQUIRE(offsetof(Assets::FAtmosphereParams, BottomRadius) == 80);
    STATIC_REQUIRE(offsetof(Assets::FAtmosphereParams, TransmittanceLutId) == 96);
    STATIC_REQUIRE(offsetof(Assets::FAtmosphereParams, AerialPerspectiveMaxDistance) == 112);
    STATIC_REQUIRE(offsetof(Assets::FAtmosphereParams, FogInscatteringColor) == 128);
    STATIC_REQUIRE(offsetof(Assets::FAtmosphereParams, FogHeightFalloff) == 144);

    STATIC_REQUIRE(offsetof(Assets::UniformBufferObject, AtmosphereReserved0) ==
                   offsetof(Assets::UniformBufferObject, AtmosphereParams) + sizeof(uint64_t));
    STATIC_REQUIRE(sizeof(Assets::UniformBufferObject) % 16 == 0);
}

TEST_CASE("Atmosphere SH projection has positive finite L0", "[Unit][Atmosphere]")
{
    Assets::AtmosphereSetting source;
    Assets::FAtmosphereParams params{};
    params.RayleighScattering = source.RayleighScattering;
    params.RayleighDensityH = source.RayleighDensityH;
    params.MieScattering = source.MieScattering;
    params.MieDensityH = source.MieDensityH;
    params.MieAbsorption = source.MieAbsorption;
    params.MiePhaseG = source.MiePhaseG;
    params.OzoneAbsorption = source.OzoneAbsorption;
    params.OzoneWidth = source.OzoneWidth;
    params.BottomRadius = source.BottomRadius;
    params.TopRadius = source.TopRadius;
    params.GroundAlbedo = source.GroundAlbedo;
    params.SkyLuminanceScale = 1.0f;

    const auto sh = Rendering::Atmosphere::SkyIrradianceProjector::Project(
        params, glm::normalize(glm::vec3(0.2f, 0.8f, 0.4f)), glm::vec3(500.0f), 0.0f);
    for (uint32_t channel = 0; channel < 3; ++channel)
    {
        REQUIRE(std::isfinite(sh.coefficients[channel][0]));
        REQUIRE(sh.coefficients[channel][0] > 0.0f);
    }
}

TEST_CASE("Environment atmosphere features are disabled by default", "[Unit][Atmosphere]")
{
    const Assets::EnvironmentSetting environment;

    REQUIRE_FALSE(environment.AtmosphereEnabled);
    REQUIRE_FALSE(environment.AerialPerspectiveEnabled);
    REQUIRE_FALSE(environment.HeightFogEnabled);
}

TEST_CASE_METHOD(EngineTestFixture, "Atmosphere GI refreshes when irradiance changes at a stationary sun",
                 "[GPU][Integration][Atmosphere]")
{
    // The fixture starts without a loaded scene, so supply a real camera before
    // the atmosphere reads the previous frame's camera altitude.
    auto& camera = engine_->GetScene().GetRenderCamera();
    camera.ModelView = glm::lookAt(glm::vec3(0.0f, 2.0f, 5.0f), glm::vec3(0.0f, 2.0f, 0.0f),
                                   glm::vec3(0.0f, 1.0f, 0.0f));
    camera.FieldOfView = 60.0f;
    camera.Aperture = 0.0f;
    camera.FocalDistance = 5.0f;
    Simulate(3);

    auto& environment = engine_->GetScene().GetEnvSettings();
    environment.AtmosphereEnabled = true;
    environment.SunRotation = 0.3f;
    environment.SunElevation = glm::radians(40.0f);
    environment.SunIntensity = 200.0f;
    environment.SunColor = glm::vec3(1.0f);
    environment.Atmosphere.SkyLuminanceScale = 1.0f;
    Simulate(3);

    const auto& projectedSky = Assets::GlobalTexturePool::GetInstance()->GetHDRSphericalHarmonics()[
        Assets::MAX_HDR_SH - 1];
    const float initialRed = projectedSky.coefficients[0][0];
    const float initialGreen = projectedSky.coefficients[1][0];
    REQUIRE(initialRed > 0.0f);
    REQUIRE(initialGreen > 0.0f);

    environment.SunIntensity *= 2.0f;
    Simulate(3);
    CHECK(projectedSky.coefficients[0][0] == Catch::Approx(initialRed * 2.0f).epsilon(0.01f));
    CHECK(projectedSky.coefficients[1][0] == Catch::Approx(initialGreen * 2.0f).epsilon(0.01f));

    environment.SunColor.r = 0.5f;
    Simulate(3);
    CHECK(projectedSky.coefficients[0][0] == Catch::Approx(initialRed).epsilon(0.01f));
    CHECK(projectedSky.coefficients[1][0] == Catch::Approx(initialGreen * 2.0f).epsilon(0.01f));
}
