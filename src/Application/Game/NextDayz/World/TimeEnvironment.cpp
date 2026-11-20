#include "Engine/Common/CoreMinimal.hpp"
#include "TimeSystem.hpp"

#include "Engine/Assets/Core/Scene.hpp"

namespace NextDayz
{
    void TimeSystem::Tick(double deltaRealSeconds, Assets::Scene& scene)
    {
        Advance(deltaRealSeconds);
        const auto frame = SampleEnvironment();
        auto& env = scene.GetEnvSettings();
        env.AtmosphereEnabled = true;
        env.AerialPerspectiveEnabled = true;
        env.SunRotation = frame.sunRotation;
        env.HasSun = frame.hasSun;
        env.SunIntensity = frame.sunIntensity;
        env.SkyIntensity = frame.skyIntensity;
        env.SunColor = frame.sunColor;
        env.SkyColor = frame.skyColor;
        env.Atmosphere.SkyLuminanceScale = frame.skyLuminanceScale;
        env.HeightFogEnabled = true;
        env.Atmosphere.FogDensity = frame.fogDensity;
        env.Atmosphere.FogHeightFalloff = frame.fogHeightFalloff;
        env.Atmosphere.FogBaseHeight = frame.fogBaseHeight;
        env.Atmosphere.FogStartDistance = frame.fogStartDistance;
        env.Atmosphere.FogMaxOpacity = frame.fogMaxOpacity;
        env.Atmosphere.FogInscatteringColor = frame.fogColor;
    }
}
