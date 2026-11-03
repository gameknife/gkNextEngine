#include "NextDayzAudio.hpp"

#include <algorithm>
#include <span>

#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Subsystems/NextAudio.hpp"

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
        constexpr const char* kWind[] = {"assets/sounds/nextdayz/ambient_wind_1.wav",
                                         "assets/sounds/nextdayz/ambient_wind_2.wav",
                                         "assets/sounds/nextdayz/ambient_wind_3.wav"};
        constexpr const char* kCrow[] = {"assets/sounds/nextdayz/ambient_crow_1.wav",
                                         "assets/sounds/nextdayz/ambient_crow_2.wav"};
        constexpr const char* kDistantShot[] = {"assets/sounds/nextdayz/distant_shot_1.wav",
                                                "assets/sounds/nextdayz/distant_shot_2.wav"};
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
                kSwing, kWind, kCrow, kDistantShot,
            };
            return table[cue];
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

    void NextDayzAudio::Shot(std::string_view weaponId)
    {
        if (weaponId == "svd" || weaponId == "mosin")
        {
            Play(Sniper, 1.0f);
        }
        else if (weaponId == "shotgun")
        {
            Play(Shotgun, 1.0f);
        }
        else if (weaponId == "pistol")
        {
            Play(Pistol, 0.85f);
        }
        else
        {
            Play(Rifle, 0.9f);
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

    void NextDayzAudio::UpdateAmbience(float deltaSeconds, float daylight, float storm)
    {
        const float dt = std::max(deltaSeconds, 0.0f);
        // Wind clips are ~7-9 s long and overlap a little, so it never stops.
        if ((windSeconds_ -= dt) <= 0.0f)
        {
            Play(Wind, 0.16f + 0.22f * storm + 0.06f * (1.0f - daylight), 1000);
            windSeconds_ = AmbienceRandom(5.5f, 6.5f);
        }
        if ((crowSeconds_ -= dt) <= 0.0f)
        {
            if (daylight > 0.4f)
            {
                Play(Crow, AmbienceRandom(0.12f, 0.3f), 2000);
            }
            crowSeconds_ = AmbienceRandom(18.0f, 55.0f);
        }
        if ((shotSeconds_ -= dt) <= 0.0f)
        {
            Play(DistantShot, AmbienceRandom(0.15f, 0.4f), 3000);
            shotSeconds_ = AmbienceRandom(45.0f, 140.0f);
        }
        if ((screamSeconds_ -= dt) <= 0.0f)
        {
            if (daylight < 0.5f)
            {
                PlayOne(kDistantScream, AmbienceRandom(0.15f, 0.35f), 3000);
            }
            screamSeconds_ = AmbienceRandom(30.0f, 90.0f);
        }
    }
}
