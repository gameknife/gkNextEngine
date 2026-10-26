#pragma once

// ============================================================================
// NextRaidenConfig.hpp - Every tunable number NextRaiden has, loaded from
// assets/configs/nextraiden/{gameplay,stages}.json. Nothing gameplay-shaped is
// allowed to be a literal in the code: tuning a vertical shooter is the bulk of
// the work, and it has to be a file edit, not a rebuild.
// ============================================================================

#include <array>
#include <string>
#include <vector>

#include "Application/Game/NextRaiden/Combat/BulletPattern.hpp"
#include "Application/Game/NextRaiden/Combat/BulletPool.hpp"
#include "Application/Game/NextRaiden/Combat/DebrisSystem.hpp"
#include "Application/Game/NextRaiden/Level/StageFlow.hpp"
#include "Application/Game/NextRaiden/World/BossSystem.hpp"
#include "Application/Game/NextRaiden/World/GroundTargetSystem.hpp"
#include "Application/Game/NextRaiden/World/ItemSystem.hpp"
#include "Application/Game/NextRaiden/World/EnemyPool.hpp"

namespace NextRaiden
{
    /// Framing. The engine's perspective takes a *vertical* FOV, so the portrait
    /// playfield is framed by height and the extra width a 16:9 window brings is
    /// covered by the HUD columns (design section 5.2).
    struct FScreenConfig
    {
        float CameraHeight = 47.0f;        // metres above the ground plane
        float CameraPitchDegrees = 15.0f;  // 0 = straight down; tilts the lens back along +Z
        float FieldOfView = 50.0f;         // vertical
        float NearPlane = 1.0f;
        float FarPlane = 600.0f;
        float HudColumnFraction = 0.30f;   // per side, as a fraction of window width
    };

    /// Where the player may fly, in metres relative to the camera centre.
    /// +Z is towards the bottom of the screen, so AheadLimit is a -Z offset.
    struct FPlayfieldConfig
    {
        float FlightAltitude = 7.0f;
        float HalfWidth = 13.0f;
        /// Measured, not guessed: at the configured height/pitch/FOV the flight plane's
        /// bottom edge sits at centre + 19.6 m, so 15 keeps the whole 5 m airframe on screen.
        float AheadLimit = 10.0f;
        float BehindLimit = 15.0f;
    };

    struct FPlayerConfig
    {
        float Speed = 18.0f;
        float SlowScale = 0.5f;
        float HitRadius = 0.6f;      // deliberately far smaller than the 5.5 m airframe
        float BankDegrees = 18.0f;
        float BankRateDegrees = 140.0f;
        float SpawnBehind = 12.0f;   // +Z offset from the camera centre at spawn
    };

    /// The player's vulcan at one power level. Levels are a table rather than a formula so a
    /// designer can make level 4 feel like a reward instead of like level 3 times 1.25.
    struct FWeaponLevel
    {
        int Shots = 1;
        float ArcDegrees = 0.0f;
        float Damage = 4.0f;
    };

    struct FWeaponConfig
    {
        float IntervalSeconds = 0.11f;
        float BulletSpeed = 62.0f;
        float BulletRadius = 0.3f;
        float MuzzleForward = 2.4f;   // ahead of the ship's origin, so shots leave the nose
        float SpreadOffset = 0.75f;   // lateral spacing between barrels
        // Wingmen ("options"). Their laser is narrow and steady: the reward for collecting
        // them is coverage the vulcan's fan does not give, not simply more of the same gun.
        float OptionIntervalSeconds = 0.16f;
        float OptionBulletSpeed = 78.0f;
        float OptionDamage = 3.0f;
        float OptionOffsetX = 3.4f;
        float OptionOffsetZ = 1.8f;
        float OptionFollowRate = 7.0f;
        /// Auto-fire on by default. Holding a button for two and a half minutes is not a
        /// skill the genre ever asked for - the arcade cabinets that did it had an autofire
        /// board bolted on by the operator. C toggles it back off for anyone who wants the
        /// trigger discipline.
        bool AutoFire = true;
        std::array<FWeaponLevel, 4> Levels{{
            FWeaponLevel{1, 0.0f, 4.0f},
            FWeaponLevel{2, 0.0f, 4.0f},
            FWeaponLevel{3, 18.0f, 4.0f},
            FWeaponLevel{5, 34.0f, 4.0f},
        }};
    };

    struct FSurvivalConfig
    {
        int Lives = 3;
        int Shield = 3;
        int Bombs = 3;
        float InvulnerableSeconds = 1.5f;
        float RespawnSeconds = 0.9f;
        float BombDamage = 400.0f;
        float BombInvulnerableSeconds = 1.2f;
        /// Dying costs a power level. Losing everything would make a death a run-ender;
        /// losing nothing would make it free.
        int PowerLossOnDeath = 1;
    };

    struct FScoreConfig
    {
        int ExtendEvery = 100000;
    };

    struct FStageDesc
    {
        std::string Id;
        std::string DisplayName;
        std::string Scene;
        std::string WaveFile;
        uint32_t Seed = 0x5eed1234u;
        /// Midboss first, stage boss last. The one flagged EndsStage finishes the stage.
        std::vector<FBossDesc> Bosses;
        float ScrollSpeed = 7.5f;
        /// Camera centre at stage start, and the Z it stops at. The stage runs towards -Z,
        /// so EndZ is the more negative of the two.
        float StartZ = -22.0f;
        float EndZ = -420.0f;
    };

    struct FConfig
    {
        FScreenConfig Screen;
        FPlayfieldConfig Playfield;
        FPlayerConfig Player;
        FWeaponConfig Weapon;
        FSurvivalConfig Survival;
        FScoreConfig Score;
        FDebrisConfig Debris;
        FBulletPoolSizes Bullets;
        FItemConfig Items;
        FStageFlowConfig Flow;
        std::array<FGroundStats, static_cast<size_t>(EGroundKind::Count)> Grounds{};
        std::array<FEnemyStats, static_cast<size_t>(EEnemyKind::Count)> Enemies{{
            FEnemyStats{12.0f, 1.7f, 14.0f, 100, 2},
            FEnemyStats{28.0f, 2.8f, 11.0f, 250, 3},
            FEnemyStats{90.0f, 4.5f, 8.0f, 800, 5},
        }};
        std::vector<FStageDesc> Stages;

        /// Missing files and malformed fields fall back to the defaults above and warn;
        /// the game always starts. Returns false when it had to invent a stage list.
        bool Load(const std::string& gameplayPath, const std::string& stagesPath);
    };
}
