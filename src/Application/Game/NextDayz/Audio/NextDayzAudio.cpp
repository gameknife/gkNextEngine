#include "NextDayzAudio.hpp"

#include <algorithm>
#include <span>

#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Subsystems/NextAudio.hpp"
#include "Application/Game/NextDayz/World/TimeSystem.hpp"
#include "Application/Game/NextDayz/Weapons/WeaponDefs.hpp"

namespace NextDayz
{
    namespace
    {
        constexpr const char* kRifle[] = {"assets/sounds/nextdayz/shot_rifle_1.wav",
                                          "assets/sounds/nextdayz/shot_rifle_2.wav",
                                          "assets/sounds/nextdayz/shot_rifle_3.wav"};
        constexpr const char* kSniper[] = {"assets/sounds/nextdayz/shot_sniper_1.wav",
                                           "assets/sounds/nextdayz/shot_sniper_2.wav"};
        constexpr const char* kShotgun[] = {"assets/sounds/nextdayz/shot_shotgun_1.wav",
                                            "assets/sounds/nextdayz/shot_shotgun_2.wav"};
        constexpr const char* kPistol[] = {"assets/sounds/nextdayz/shot_pistol_1.wav",
                                           "assets/sounds/nextdayz/shot_pistol_2.wav"};
        constexpr const char* kFlesh[] = {"assets/sounds/nextdayz/hit_flesh_1.wav",
                                          "assets/sounds/nextdayz/hit_flesh_2.wav",
                                          "assets/sounds/nextdayz/hit_flesh_3.wav"};
        constexpr const char* kHead[] = {"assets/sounds/nextdayz/hit_head_1.wav",
                                         "assets/sounds/nextdayz/hit_head_2.wav"};
        constexpr const char* kDirt[] = {"assets/sounds/nextdayz/impact_dirt_1.wav",
                                         "assets/sounds/nextdayz/impact_dirt_2.wav"};
        constexpr const char* kHurt[] = {"assets/sounds/nextdayz/player_hurt_1.wav",
                                         "assets/sounds/nextdayz/player_hurt_2.wav"};
        constexpr const char* kGroan[] = {"assets/sounds/nextdayz/zombie_groan_1.wav",
                                          "assets/sounds/nextdayz/zombie_groan_2.wav",
                                          "assets/sounds/nextdayz/zombie_groan_3.wav",
                                          "assets/sounds/nextdayz/zombie_groan_4.wav"};
        constexpr const char* kAlert[] = {"assets/sounds/nextdayz/zombie_alert_1.wav",
                                          "assets/sounds/nextdayz/zombie_alert_2.wav"};
        constexpr const char* kAttack[] = {"assets/sounds/nextdayz/zombie_attack_1.wav",
                                           "assets/sounds/nextdayz/zombie_attack_2.wav"};
        constexpr const char* kDeath[] = {"assets/sounds/nextdayz/zombie_death_1.wav",
                                          "assets/sounds/nextdayz/zombie_death_2.wav"};
        constexpr const char* kDryFire = "assets/sounds/nextdayz/dry_fire.wav";
        constexpr const char* kReload = "assets/sounds/nextdayz/reload_rifle.wav";
        constexpr const char* kSwitch = "assets/sounds/nextdayz/switch_weapon.wav";
        constexpr const char* kKill = "assets/sounds/nextdayz/kill_confirm.wav";
        constexpr const char* kBodyFall = "assets/sounds/nextdayz/body_fall.wav";
        constexpr const char* kSwing[] = {"assets/sounds/nextdayz/melee_swing_1.wav",
                                          "assets/sounds/nextdayz/melee_swing_2.wav"};
        constexpr const char* kWindCalm[] = {"assets/sounds/nextdayz/ambient_wind_1.wav",
                                             "assets/sounds/nextdayz/ambient_wind_2.wav"};
        constexpr const char* kWindGust[] = {"assets/sounds/nextdayz/ambient_wind_3.wav",
                                             "assets/sounds/nextdayz/ambient_wind_4.wav"};
        constexpr const char* kCrow[] = {"assets/sounds/nextdayz/ambient_crow_1.wav",
                                         "assets/sounds/nextdayz/ambient_crow_2.wav"};
        constexpr const char* kDistantShot[] = {"assets/sounds/nextdayz/distant_shot_1.wav",
                                                "assets/sounds/nextdayz/distant_shot_2.wav"};
        constexpr const char* kDayBed[] = {"assets/sounds/nextdayz/ambient_day_1.wav",
                                           "assets/sounds/nextdayz/ambient_day_2.wav",
                                           "assets/sounds/nextdayz/ambient_day_3.wav"};
        constexpr const char* kEveningBed[] = {"assets/sounds/nextdayz/ambient_evening_1.wav",
                                               "assets/sounds/nextdayz/ambient_evening_2.wav"};
        constexpr const char* kNightBed[] = {"assets/sounds/nextdayz/ambient_night_1.wav",
                                             "assets/sounds/nextdayz/ambient_night_2.wav",
                                             "assets/sounds/nextdayz/ambient_night_3.wav"};
        constexpr const char* kBird[] = {"assets/sounds/nextdayz/ambient_bird_1.wav",
                                         "assets/sounds/nextdayz/ambient_bird_2.wav",
                                         "assets/sounds/nextdayz/ambient_bird_3.wav",
                                         "assets/sounds/nextdayz/ambient_bird_4.wav",
                                         "assets/sounds/nextdayz/ambient_bird_5.wav",
                                         "assets/sounds/nextdayz/ambient_bird_6.wav"};
        constexpr const char* kDayInsect[] = {"assets/sounds/nextdayz/ambient_insect_day_1.wav",
                                              "assets/sounds/nextdayz/ambient_insect_day_2.wav",
                                              "assets/sounds/nextdayz/ambient_insect_day_3.wav"};
        constexpr const char* kNightInsect[] = {"assets/sounds/nextdayz/ambient_insect_night_1.wav",
                                                "assets/sounds/nextdayz/ambient_insect_night_2.wav",
                                                "assets/sounds/nextdayz/ambient_insect_night_3.wav"};
        constexpr const char* kOwl[] = {"assets/sounds/nextdayz/ambient_owl_1.wav",
                                        "assets/sounds/nextdayz/ambient_owl_2.wav",
                                        "assets/sounds/nextdayz/ambient_owl_3.wav"};
        constexpr const char* kFoliage[] = {"assets/sounds/nextdayz/ambient_foliage_1.wav",
                                            "assets/sounds/nextdayz/ambient_foliage_2.wav",
                                            "assets/sounds/nextdayz/ambient_foliage_3.wav"};
        constexpr const char* kMeleeImpact = "assets/sounds/nextdayz/melee_impact.wav";
        constexpr const char* kFirecracker = "assets/sounds/nextdayz/firecracker.wav";
        constexpr const char* kDistantScream = "assets/sounds/nextdayz/distant_scream.wav";
        constexpr const char* kPickup = "assets/sounds/nextdayz/pickup.wav";
        constexpr const char* kDrop = "assets/sounds/nextdayz/item_drop.wav";
        constexpr const char* kEat = "assets/sounds/nextdayz/eat.wav";
        constexpr const char* kDrink = "assets/sounds/nextdayz/drink.wav";
        constexpr const char* kBandage = "assets/sounds/nextdayz/bandage.wav";
        constexpr const char* kRadio = "assets/sounds/nextdayz/radio_static.wav";
        constexpr const char* kLantern = "assets/sounds/nextdayz/lantern.wav";

        std::span<const char* const> Variants(size_t cue)
        {
            static constexpr std::span<const char* const> table[] = {
                kRifle, kSniper, kShotgun, kPistol, kFlesh, kHead, kDirt, kHurt, kGroan, kAlert, kAttack, kDeath,
                kSwing, kWindCalm, kWindGust, kCrow, kDistantShot,
                kDayBed, kEveningBed, kNightBed, kBird, kDayInsect, kNightInsect, kOwl, kFoliage,
            };
            return table[cue];
        }

        float SmoothStep(float lo, float hi, float value)
        {
            const float t = std::clamp((value - lo) / (hi - lo), 0.0f, 1.0f);
            return t * t * (3.0f - 2.0f * t);
        }
    }

    float NextDayzAudio::Falloff(const glm::vec3& at, float range) const
    {
        const float t = std::clamp(1.0f - glm::distance(at, listener_) / std::max(range, 1.0f), 0.0f, 1.0f);
        return t * t;
    }

    void NextDayzAudio::PlayOne(const char* path, float volume, uint64_t minIntervalMs)
    {
        if (volume <= 0.01f || !engine_)
        {
            return;
        }
        ++played_;
        if (NextAudio* audio = engine_->GetAudio())
        {
            audio->PlaySfx(path, std::clamp(volume, 0.0f, 1.0f), minIntervalMs);
        }
    }

    void NextDayzAudio::Play(ECue cue, float volume, uint64_t minIntervalMs)
    {
        const std::span<const char* const> variants = Variants(cue);
        uint32_t& cursor = cursors_[cue];
        PlayOne(variants[cursor % variants.size()], volume, minIntervalMs);
        ++cursor;
    }

    void NextDayzAudio::Shot(std::string_view weaponId, bool suppressed)
    {
        const float volume = suppressed ? 0.25f : 1.0f;
        const FWeaponDef* weapon = FindWeaponDef(weaponId);
        const EWeaponFamily family = weapon ? weapon->family : EWeaponFamily::Rifle;
        if (family == EWeaponFamily::Marksman)
        {
            Play(Sniper, volume);
        }
        else if (family == EWeaponFamily::Shotgun)
        {
            Play(Shotgun, volume);
        }
        else if (family == EWeaponFamily::Sidearm || family == EWeaponFamily::Smg)
        {
            Play(Pistol, 0.85f * volume);
        }
        else
        {
            Play(Rifle, 0.9f * volume);
        }
    }

    void NextDayzAudio::DryFire() { PlayOne(kDryFire, 0.6f, 120); }
    void NextDayzAudio::Reload() { PlayOne(kReload, 0.7f, 500); }
    void NextDayzAudio::Switch() { PlayOne(kSwitch, 0.55f, 300); }

    void NextDayzAudio::Hit(EHitZone zone, bool killed)
    {
        // Hit sounds are feedback, not world audio: no falloff. A shotgun's
        // pellets share one hit sound per burst through the interval.
        Play(zone == EHitZone::Head ? Head : Flesh, zone == EHitZone::Head ? 0.8f : 0.6f, 45);
        if (killed)
        {
            PlayOne(kKill, 0.55f, 80);
        }
    }

    void NextDayzAudio::Impact(const glm::vec3& at) { Play(Dirt, 0.5f * Falloff(at, 40.0f), 40); }
    void NextDayzAudio::PlayerHurt() { Play(Hurt, 0.8f, 200); }
    void NextDayzAudio::ZombieGroan(const glm::vec3& at) { Play(Groan, 0.75f * Falloff(at, 30.0f), 200); }
    void NextDayzAudio::ZombieAlert(const glm::vec3& at) { Play(Alert, Falloff(at, 50.0f), 150); }
    void NextDayzAudio::ZombieAttack(const glm::vec3& at) { Play(Attack, 0.85f * Falloff(at, 20.0f), 100); }
    void NextDayzAudio::ZombieDeath(const glm::vec3& at) { Play(Death, 0.8f * Falloff(at, 35.0f), 100); }
    void NextDayzAudio::BodyFall(const glm::vec3& at) { PlayOne(kBodyFall, 0.7f * Falloff(at, 25.0f), 60); }

    void NextDayzAudio::MeleeSwing() { Play(Swing, 0.6f, 120); }
    void NextDayzAudio::MeleeImpact() { PlayOne(kMeleeImpact, 0.75f, 80); }
    void NextDayzAudio::Firecracker(const glm::vec3& at) { PlayOne(kFirecracker, 0.9f * Falloff(at, 90.0f), 200); }
    void NextDayzAudio::Pickup() { PlayOne(kPickup, 0.5f, 150); }
    void NextDayzAudio::Drop() { PlayOne(kDrop, 0.5f, 150); }
    void NextDayzAudio::Eat() { PlayOne(kEat, 0.6f, 500); }
    void NextDayzAudio::Drink() { PlayOne(kDrink, 0.6f, 500); }
    void NextDayzAudio::Bandage() { PlayOne(kBandage, 0.6f, 500); }
    void NextDayzAudio::RadioStatic() { PlayOne(kRadio, 0.6f, 1000); }
    void NextDayzAudio::Lantern() { PlayOne(kLantern, 0.5f, 300); }

    float NextDayzAudio::AmbienceRandom(float lo, float hi)
    {
        ambienceState_ = ambienceState_ * 1664525u + 1013904223u;
        return lo + (hi - lo) * static_cast<float>(ambienceState_ >> 8) / 16777216.0f;
    }

    void NextDayzAudio::UpdateAmbience(float deltaSeconds, float hour, float daylight, EWeather weather, float storm)
    {
        const float dt = std::max(deltaSeconds, 0.0f);
        const float day = SmoothStep(0.1f, 0.85f, daylight);
        const float night = 1.0f - SmoothStep(0.05f, 0.65f, daylight);
        const float dawn = SmoothStep(4.5f, 5.5f, hour) * (1.0f - SmoothStep(8.0f, 9.0f, hour));
        const float evening = SmoothStep(16.0f, 18.0f, hour) * (1.0f - SmoothStep(19.5f, 21.0f, hour));
        const bool fog = weather == EWeather::Fog;
        const float overcast = std::clamp(storm / 0.6f, 0.0f, 1.0f);
        const float wildlife = fog ? 0.35f : 1.0f - 0.45f * overcast;
        ambienceMix_ = {
            0.20f * day * (1.0f - 0.6f * evening) * wildlife,
            0.18f * evening * wildlife,
            0.18f * night * (1.0f - 0.5f * evening) * wildlife,
            0.22f * std::min(day + 0.65f * dawn, 1.6f) * wildlife,
            0.10f * day * wildlife,
            0.13f * std::max(night, evening) * wildlife,
            0.18f * night * wildlife,
            fog ? 0.018f : 0.045f + 0.07f * overcast,
            fog ? 0.025f : 0.045f + 0.08f * overcast,
        };

        // Adjacent clips overlap only over their baked fades. Inactive layers
        // stop being scheduled, letting the previous clip fade out naturally.
        if ((bedSeconds_ -= dt) <= 0.0f)
        {
            Play(DayBed, ambienceMix_.day, 1000);
            Play(EveningBed, ambienceMix_.evening, 1000);
            Play(NightBed, ambienceMix_.night, 1000);
            bedSeconds_ = 6.0f;
        }
        if ((windSeconds_ -= dt) <= 0.0f)
        {
            Play(!fog && overcast > 0.5f ? WindGust : WindCalm, ambienceMix_.wind, 1000);
            windSeconds_ = 6.0f;
        }
        if ((birdSeconds_ -= dt) <= 0.0f)
        {
            Play(Bird, ambienceMix_.birds * AmbienceRandom(0.65f, 1.0f), 1000);
            birdSeconds_ = AmbienceRandom(5.0f, 14.0f) / (1.0f + dawn) / std::max(wildlife, 0.3f);
        }
        if ((insectSeconds_ -= dt) <= 0.0f)
        {
            Play(DayInsect, ambienceMix_.dayInsects * AmbienceRandom(0.6f, 1.0f), 1000);
            Play(NightInsect, ambienceMix_.nightInsects * AmbienceRandom(0.6f, 1.0f), 1000);
            insectSeconds_ = AmbienceRandom(8.0f, 18.0f);
        }
        if ((owlSeconds_ -= dt) <= 0.0f)
        {
            Play(Owl, ambienceMix_.owls * AmbienceRandom(0.6f, 1.0f), 2000);
            owlSeconds_ = AmbienceRandom(20.0f, 45.0f);
        }
        if ((foliageSeconds_ -= dt) <= 0.0f)
        {
            Play(Foliage, ambienceMix_.foliage * AmbienceRandom(0.6f, 1.0f), 1000);
            foliageSeconds_ = AmbienceRandom(12.0f, 30.0f);
        }
        if ((crowSeconds_ -= dt) <= 0.0f)
        {
            Play(Crow, day * wildlife * AmbienceRandom(0.08f, 0.16f), 2000);
            crowSeconds_ = AmbienceRandom(18.0f, 55.0f);
        }
        if ((shotSeconds_ -= dt) <= 0.0f)
        {
            Play(DistantShot, AmbienceRandom(0.10f, 0.24f), 3000);
            shotSeconds_ = AmbienceRandom(45.0f, 140.0f);
        }
        if ((screamSeconds_ -= dt) <= 0.0f)
        {
            if (night > 0.5f)
            {
                PlayOne(kDistantScream, night * AmbienceRandom(0.08f, 0.16f), 3000);
            }
            screamSeconds_ = AmbienceRandom(30.0f, 90.0f);
        }
    }
}
