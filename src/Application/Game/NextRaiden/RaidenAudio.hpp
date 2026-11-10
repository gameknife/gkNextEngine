#pragma once

// ============================================================================
// RaidenAudio.hpp - The one place NextRaiden talks to NextAudio. The sounds are
// placeholders (copies of the shared set, kept under assets/sounds/nextraiden so
// this game does not reach into another game's project) until it gets its own;
// every call site goes through here so swapping them is a one-file change.
//
// The player's gun fires nine times a second, so it goes through PlaySfxEx with
// a minimum interval: without it a held trigger queues a voice per shot and the
// mix turns into a buzz.
// ============================================================================

#include <string>

#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Subsystems/NextAudio.hpp"

namespace NextRaiden::Audio
{
    inline constexpr const char* kShotSfx = "assets/sounds/nextraiden/shot.wav";
    inline constexpr const char* kHitSfx = "assets/sounds/nextraiden/hit.wav";
    inline constexpr const char* kExplodeSfx = "assets/sounds/nextraiden/explode.wav";
    inline constexpr const char* kPickupSfx = "assets/sounds/nextraiden/pickup.wav";

    inline void Play(NextEngine& engine, const char* path, float volume = 1.0f, uint64_t minIntervalMs = 50)
    {
        if (NextAudio* audio = engine.GetAudio())
        {
            audio->PlaySfx(path, volume, minIntervalMs);
        }
    }

    inline void PlayShot(NextEngine& engine) { Play(engine, kShotSfx, 0.22f, 70); }
    inline void PlayEnemyHit(NextEngine& engine) { Play(engine, kHitSfx, 0.25f, 40); }
    inline void PlayExplosion(NextEngine& engine) { Play(engine, kExplodeSfx, 0.6f, 30); }
    inline void PlayPlayerHit(NextEngine& engine) { Play(engine, kExplodeSfx, 1.0f); }
    inline void PlayPickup(NextEngine& engine) { Play(engine, kPickupSfx, 0.7f); }
}
