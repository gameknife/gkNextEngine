#pragma once

// ============================================================================
// AstroAudio.hpp - The one place NextAstrobot talks to NextAudio. Sounds are
// placeholders (copies of the Flappy set, kept under assets/sounds/astrobot so
// this game does not reach into another game's project) until the game gets
// its own; keep every call site going through here so swapping them is a
// one-file change.
// ============================================================================

#include <string>

#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Subsystems/NextAudio.hpp"

namespace NextAstrobot::Audio
{
    inline constexpr const char* kCoinSfx = "assets/sounds/astrobot/coin.wav";
    inline constexpr const char* kJumpSfx = "assets/sounds/astrobot/jump.wav";
    inline constexpr const char* kHitSfx = "assets/sounds/astrobot/hit.wav";

    inline void Play(NextEngine& engine, const char* path, float volume = 1.0f)
    {
        if (NextAudio* audio = engine.GetAudio())
        {
            audio->PlaySfx(path, volume);
        }
    }

    inline void PlayCoin(NextEngine& engine) { Play(engine, kCoinSfx, 0.55f); }
    inline void PlayJump(NextEngine& engine) { Play(engine, kJumpSfx, 0.35f); }
    inline void PlayLaunch(NextEngine& engine) { Play(engine, kJumpSfx, 0.7f); }
    inline void PlayPunch(NextEngine& engine) { Play(engine, kHitSfx, 0.4f); }
    inline void PlayDeath(NextEngine& engine) { Play(engine, kHitSfx, 0.9f); }
    inline void PlayRescue(NextEngine& engine) { Play(engine, kCoinSfx, 0.9f); }
    inline void PlayGoal(NextEngine& engine) { Play(engine, kCoinSfx, 1.0f); }
}
