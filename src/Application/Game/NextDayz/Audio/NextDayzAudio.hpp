#pragma once

// ============================================================================
// NextDayzAudio.hpp - The one place NextDayz talks to NextAudio. Sounds are
// generated per checkout, not committed: `python tools/audio/nextdayz_sfx.py`
// writes assets/sounds/nextdayz. For a reversible local sample trial, use
// tools/audio/nextdayz_dayz_trial.py --dayz-root <installation> (or --restore).
// Restart the game after switching the WAVs: NextAudio caches decoded files.
// A missing file is skipped with one warning. This file only names them.
// NextAudio plays 2D one-shots with one voice per file, so variants are cycled
// round-robin (automatic fire never cuts off its own tail) and distance is a
// squared volume falloff.
// ============================================================================

#include <array>
#include <cstdint>
#include <string_view>

#include <glm/glm.hpp>

#include "Application/Game/NextDayz/Combat/CombatEvents.hpp"

class NextEngine;

namespace NextDayz
{
    enum class EWeather : uint8_t;

    struct FAmbienceMix
    {
        float day = 0.0f;
        float evening = 0.0f;
        float night = 0.0f;
        float birds = 0.0f;
        float dayInsects = 0.0f;
        float nightInsects = 0.0f;
        float owls = 0.0f;
        float wind = 0.0f;
        float foliage = 0.0f;
    };

    class NextDayzAudio
    {
    public:
        void SetEngine(NextEngine* engine) { engine_ = engine; }
        void SetListener(const glm::vec3& position) { listener_ = position; }
        uint64_t PlayedCount() const { return played_; }

        // Player weapon.
        void Shot(std::string_view weaponId, bool suppressed = false);
        void DryFire();
        void Reload();
        void Switch();
        // Rounds landing.
        void Hit(EHitZone zone, bool killed);
        void Impact(const glm::vec3& at);
        void PlayerHurt();
        // Infected.
        void ZombieGroan(const glm::vec3& at);
        void ZombieAlert(const glm::vec3& at);
        void ZombieAttack(const glm::vec3& at);
        void ZombieDeath(const glm::vec3& at);
        void BodyFall(const glm::vec3& at);
        // Melee and survival actions.
        void MeleeSwing();
        void MeleeImpact();
        void Firecracker(const glm::vec3& at);
        void Pickup();
        void Drop();
        void Eat();
        void Drink();
        void Bandage();
        void RadioStatic();
        void Lantern();
        // Layered rural beds, wildlife and quiet wind, selected by clock and
        // actual weather. Fog is sheltered/muffled, not a stronger wind storm.
        // Eight-second beds have two-second fades and start six seconds apart.
        void UpdateAmbience(float deltaSeconds, float hour, float daylight, EWeather weather, float storm);
        const FAmbienceMix& AmbienceMix() const { return ambienceMix_; }

    private:
        enum ECue : size_t
        {
            Rifle,
            Sniper,
            Shotgun,
            Pistol,
            Flesh,
            Head,
            Dirt,
            Hurt,
            Groan,
            Alert,
            Attack,
            Death,
            Swing,
            WindCalm,
            WindGust,
            Crow,
            DistantShot,
            DayBed,
            EveningBed,
            NightBed,
            Bird,
            DayInsect,
            NightInsect,
            Owl,
            Foliage,
            CueCount,
        };

        void Play(ECue cue, float volume, uint64_t minIntervalMs = 30);
        void PlayOne(const char* path, float volume, uint64_t minIntervalMs = 30);
        float Falloff(const glm::vec3& at, float range) const;

        NextEngine* engine_ = nullptr;
        glm::vec3 listener_{0.0f};
        std::array<uint32_t, CueCount> cursors_{};
        uint64_t played_ = 0;
        // Ambience timers (seconds until the next one) and a tiny LCG so the
        // soundscape does not depend on any gameplay random stream.
        FAmbienceMix ambienceMix_{};
        float bedSeconds_ = 0.1f;
        float windSeconds_ = 1.5f;
        float birdSeconds_ = 2.0f;
        float insectSeconds_ = 4.0f;
        float owlSeconds_ = 5.0f;
        float foliageSeconds_ = 9.0f;
        float crowSeconds_ = 12.0f;
        float shotSeconds_ = 40.0f;
        float screamSeconds_ = 25.0f;
        uint32_t ambienceState_ = 0x2545F491u;
        float AmbienceRandom(float lo, float hi);
    };
}
