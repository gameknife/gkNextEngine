#pragma once

// ============================================================================
// TimeSystem.hpp - Day/night cycle and weather for NextDayz. Sweeps the scene
// sun rotation/intensity by game hour, and runs a deterministic weather cycle
// (clear / overcast / fog) that dims and greys the light and thickens a
// height fog: morning mist in the valleys, a grey murk on bad days, near
// darkness at night. No rain particles.
// ============================================================================

#include <cmath>

#include "Application/Game/NextDayz/NextDayzConfig.hpp"

namespace Assets
{
    class Scene;
}

namespace NextDayz
{
    enum class EWeather : uint8_t
    {
        Clear,
        Overcast,
        Fog,
    };

    inline const char* WeatherName(EWeather weather)
    {
        switch (weather)
        {
        case EWeather::Overcast: return "Overcast";
        case EWeather::Fog: return "Fog";
        case EWeather::Clear:
        default: return "Clear";
        }
    }

    class TimeSystem
    {
    public:
        void Reset(const FTimeConfig& config, uint64_t seed = 1);
        struct FState
        {
            double minutes = 480.0;
            double nextWeatherChange = 630.0;
            uint64_t weatherRng = 1;
            EWeather weather = EWeather::Clear;
            float blend = 0.0f;
        };
        FState Capture() const { return {gameMinutes_, nextWeatherChange_, weatherState_, weather_, weatherBlend_}; }
        void Restore(const FState& state)
        {
            gameMinutes_ = state.minutes;
            nextWeatherChange_ = state.nextWeatherChange;
            weatherState_ = state.weatherRng;
            weather_ = state.weather;
            weatherBlend_ = state.blend;
        }

        void Tick(double deltaRealSeconds, Assets::Scene& scene);
        void Skip(double gameMinutes) { gameMinutes_ += gameMinutes; }
        void SetHour(double hour) { gameMinutes_ = std::floor(gameMinutes_ / 1440.0) * 1440.0 + hour * 60.0; }
        // Debug tuning: fog colour radiance by day, and a density multiplier.
        void SetFogTuning(float dayRadiance, float densityScale)
        {
            config_.FogDayRadiance = dayRadiance;
            fogDensityScale_ = densityScale;
        }

        double GameMinutes() const { return gameMinutes_; }
        double Hour() const;               // 0..24
        int HourInt() const;
        int MinuteInt() const;
        double& TimeScaleRef() { return timeScale_; }
        double TimeScale() const { return timeScale_; }

        // Weather. The cycle changes it every few game hours; G steps it by hand.
        EWeather Weather() const { return weather_; }
        bool Overcast() const { return weather_ != EWeather::Clear; }
        void SetOvercast(bool overcast) { SetWeather(overcast ? EWeather::Overcast : EWeather::Clear); }
        void SetWeather(EWeather weather);
        void ToggleOvercast();

        // 0 night .. 1 full day.
        float Daylight() const { return daylight_; }
        // 0 clear .. 1 the worst of it (for wind, cold and the infected).
        float Storm() const;
        // Current fog coverage, including the gradual arrival/departure.
        float Fog() const;
        // 0 day .. 1 deep night, deepened by bad weather: how cold it is outside.
        float Chill() const;
        // Extra night light from a lit lantern (0 off .. 1 lit).
        void SetLanternLight(float light) { lanternLight_ = light; }

    private:
        void ApplyEnvironment(Assets::Scene& scene);
        void AdvanceWeather();

        FTimeConfig config_{};
        double gameMinutes_ = 8.0 * 60.0;
        double timeScale_ = 60.0;
        EWeather weather_ = EWeather::Clear;
        double nextWeatherChange_ = 0.0;   // game minutes
        uint64_t weatherState_ = 1;
        float daylight_ = 1.0f;
        float weatherBlend_ = 0.0f;        // smoothed Storm()
        float lanternLight_ = 0.0f;
        float fogDensityScale_ = 1.0f;
    };
}
