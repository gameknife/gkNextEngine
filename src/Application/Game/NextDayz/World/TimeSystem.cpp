#include "TimeSystem.hpp"

#include <algorithm>
#include <cmath>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include "Engine/Assets/Core/Model.hpp"
#include "Engine/Assets/Core/Scene.hpp"

namespace NextDayz
{
    namespace
    {
        float SmoothStep(float a, float b, float x)
        {
            const float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
            return t * t * (3.0f - 2.0f * t);
        }

        float WeatherStorm(EWeather weather)
        {
            switch (weather)
            {
            case EWeather::Overcast: return 0.6f;
            case EWeather::Fog: return 0.8f;
            case EWeather::Clear:
            default: return 0.0f;
            }
        }
    }

    void TimeSystem::Reset(const FTimeConfig& config, uint64_t seed)
    {
        config_ = config;
        gameMinutes_ = config.StartHour * 60.0;
        timeScale_ = config.TimeScale;
        weather_ = EWeather::Clear;
        weatherState_ = seed | 1ULL;
        weatherBlend_ = 0.0f;
        daylight_ = 1.0f;
        lanternLight_ = 0.0f;
        nextWeatherChange_ = gameMinutes_ + config.WeatherMinHours * 60.0;
    }

    double TimeSystem::Hour() const
    {
        return std::fmod(gameMinutes_, 24.0 * 60.0) / 60.0;
    }

    int TimeSystem::HourInt() const
    {
        return static_cast<int>(Hour());
    }

    int TimeSystem::MinuteInt() const
    {
        const double dayMinutes = std::fmod(gameMinutes_, 24.0 * 60.0);
        return static_cast<int>(std::fmod(dayMinutes, 60.0));
    }

    void TimeSystem::SetWeather(EWeather weather)
    {
        weather_ = weather;
        weatherBlend_ = WeatherStorm(weather);
    }

    void TimeSystem::ToggleOvercast()
    {
        SetWeather(weather_ == EWeather::Clear ? EWeather::Overcast
                   : weather_ == EWeather::Overcast ? EWeather::Fog : EWeather::Clear);
    }

    float TimeSystem::Storm() const
    {
        return weatherBlend_;
    }

    float TimeSystem::Chill() const
    {
        return std::clamp((1.0f - daylight_) * 0.8f + weatherBlend_ * 0.35f, 0.0f, 1.0f);
    }

    void TimeSystem::AdvanceWeather()
    {
        weatherState_ = weatherState_ * 6364136223846793005ULL + 1442695040888963407ULL;
        const uint32_t roll = static_cast<uint32_t>(weatherState_ >> 33) % 100u;
        weather_ = roll < 45u ? EWeather::Clear : roll < 80u ? EWeather::Overcast : EWeather::Fog;
        const double span = std::max(config_.WeatherMaxHours - config_.WeatherMinHours, 0.0);
        const double hours = config_.WeatherMinHours + span * static_cast<double>((weatherState_ >> 20) % 1000u) / 999.0;
        nextWeatherChange_ = gameMinutes_ + hours * 60.0;
    }

    void TimeSystem::Tick(double deltaRealSeconds, Assets::Scene& scene)
    {
        gameMinutes_ += deltaRealSeconds * timeScale_;
        if (gameMinutes_ >= nextWeatherChange_)
        {
            AdvanceWeather();
        }
        // Weather rolls in over a minute or so of real time.
        const float target = WeatherStorm(weather_);
        const float step = static_cast<float>(std::max(deltaRealSeconds, 0.0)) * 0.02f;
        weatherBlend_ += std::clamp(target - weatherBlend_, -step, step);
        ApplyEnvironment(scene);
    }

    void TimeSystem::ApplyEnvironment(Assets::Scene& scene)
    {
        const float hour = static_cast<float>(Hour());

        // Dawn 05:00-07:00 rise, dusk 17:00-19:00 fall.
        const float rise = SmoothStep(5.0f, 7.0f, hour);
        const float set = 1.0f - SmoothStep(17.0f, 19.0f, hour);
        daylight_ = std::min(rise, set);

        const float gloom = std::clamp(weatherBlend_ / 0.6f, 0.0f, 1.0f);
        const float dim = glm::mix(1.0f, config_.OvercastFactor, gloom);
        const float skyDim = glm::mix(1.0f, config_.OvercastSkyFactor, gloom);
        const float nightFloor = config_.NightSkyFraction + config_.LanternNightLift * lanternLight_;
        const float skyLight = nightFloor + (1.0f - nightFloor) * daylight_;

        auto& env = scene.GetEnvSettings();
        env.AtmosphereEnabled = true;
        env.AerialPerspectiveEnabled = true;
        // 06:00 -> 18:00 sweeps half a turn (east rise, west set).
        env.SunRotation = std::clamp((hour - 6.0f) / 12.0f, 0.0f, 1.0f);
        env.HasSun = daylight_ > 0.02f && gloom < 0.5f;
        // Procedural sky is driven by sun irradiance, not SkyIntensity. Keep the
        // night/lantern floor in that source; HasSun still gates direct sunlight.
        const float sunLight = std::max(daylight_, nightFloor);
        env.SunIntensity = config_.DaySunIntensity * sunLight * dim;
        env.SkyIntensity = config_.DaySkyIntensity * skyLight * skyDim;
        // The procedural source shares SunIntensity, so cancel its weather dimming
        // here before applying the independently tuned diffuse sky intensity.
        env.Atmosphere.SkyLuminanceScale = config_.DaySkyLuminanceScale * skyLight * skyDim /
            std::max(sunLight * dim, 0.001f);
        // Low sun burns orange; bad weather drains the colour out of everything.
        const float noon = std::sin(glm::pi<float>() * std::clamp((hour - 6.0f) / 12.0f, 0.0f, 1.0f));
        const float warm = 1.0f - std::clamp(noon * 2.2f, 0.0f, 1.0f);
        env.SunColor = glm::mix(glm::vec3(1.0f, 0.97f, 0.92f), glm::vec3(1.0f, 0.62f, 0.38f), warm);
        env.SkyColor = glm::mix(glm::vec3(0.92f, 0.95f, 1.0f), glm::vec3(0.80f, 0.82f, 0.84f), gloom);

        // Height fog. Mist settles at dawn; fog days close in to ~100 m.
        float density = config_.ClearFogDensity;
        if (weather_ == EWeather::Overcast || weatherBlend_ > 0.0f)
        {
            density = glm::mix(config_.ClearFogDensity, config_.OvercastFogDensity, gloom);
        }
        const float fogShare = std::clamp((weatherBlend_ - 0.6f) / 0.2f, 0.0f, 1.0f);
        density = glm::mix(density, config_.FogWeatherDensity, weather_ == EWeather::Fog ? fogShare : 0.0f);
        const float mist = SmoothStep(4.0f, 6.0f, hour) * (1.0f - SmoothStep(7.5f, 9.5f, hour));
        density += config_.DawnMistDensity * mist + config_.NightFogDensity * (1.0f - daylight_);
        env.HeightFogEnabled = true;
        env.Atmosphere.FogDensity = density * fogDensityScale_;
        env.Atmosphere.FogHeightFalloff = config_.FogHeightFalloff;
        env.Atmosphere.FogBaseHeight = 0.0f;
        env.Atmosphere.FogStartDistance = 8.0f;
        env.Atmosphere.FogMaxOpacity = glm::mix(0.85f, 0.98f, fogShare);
        const float radiance = glm::mix(config_.FogNightRadiance, config_.FogDayRadiance, daylight_) * dim;
        const glm::vec3 tint = glm::mix(config_.FogColor, glm::vec3(0.78f, 0.62f, 0.50f), warm * daylight_ * 0.5f);
        env.Atmosphere.FogInscatteringColor = tint * radiance;
    }
}
