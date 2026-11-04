#pragma once

// ============================================================================
// NextDayzConfig.hpp - All tunable numbers for the NextDayz MVP live here
// (mirrors the CharacterDemoConfig.hpp convention). No magic numbers scattered
// through the systems; balance and feel changes happen in one place.
// ============================================================================

#include <glm/vec3.hpp>

namespace NextDayz
{
    struct FPlayerConfig
    {
        float StandingEyeHeight = 1.65f;
        float CrouchedEyeHeight = 1.02f;
        float EyeHeightLerpSpeed = 12.0f;
        float StandWalkSpeed = 2.0f;
        float StandRunSpeed = 4.2f;
        float StandSprintSpeed = 7.6f;
        float CrouchWalkSpeed = 1.55f;
        float BackwardScale = 0.72f;
        float StrafeScale = 0.82f;
        float AimMoveScale = 0.65f;
        float StandingHeight = 1.8f;
        float CrouchedHeight = 1.18f;
        float ControllerRadius = 0.35f;
        float ControllerMass = 80.0f;
        float ControllerStrength = 4000.0f;
        // Maximum obstacle height the character controller can step over.
        // Bound to nextdayz.player.maxStepHeight in ConfigureCVars().
        float MaxStepHeight = 0.25f;
        float MaxSlopeAngle = 52.0f;
        // Velocity is integrated, never set: weight comes from how fast the
        // body gets up to speed and how hard it brakes (m/s^2).
        float GroundAcceleration = 26.0f;
        float SprintAcceleration = 14.0f;   // run -> sprint takes ~0.25 s
        float GroundDeceleration = 30.0f;
        float AirAcceleration = 4.0f;
        float Gravity = -15.0f;
        float JumpSpeed = 6.0f;
        float VelocitySmoothingSeconds = 0.05f; // measured velocity filter for animation
        // Sprint needs a forward-led input (CoD rule); strafing degrades it to a run.
        float SprintMinForwardInput = 0.5f;
        float SprintRaiseSeconds = 0.22f;    // weapon dropping into the sprint carry
        float SprintRecoverSeconds = 0.16f;  // weapon coming back up; no fire until ready
        float WeaponReadyThreshold = 0.3f;
    };

    struct FCameraConfig
    {
        float MouseSensitivity = 0.0022f;
        float BaseFov = 75.0f;
        float FovLerpSpeed = 12.0f;     // exponential smoothing rate for ADS FOV
        float MaxPitchDegrees = 85.0f;
        // Third person
        float TpsDistance = 3.8f;
        float TpsHeight = 1.7f;
        float TpsShoulderOffset = 0.90f;
        float TpsMinDistance = 1.5f;
        float TpsMaxDistance = 9.0f;
        float ScrollStep = 0.6f;
        float TpsCollisionRadius = 0.20f; // near-plane clearance around the camera arm
        float TpsCollisionPadding = 0.05f;
        float TpsArmReturnSpeed = 5.0f;   // m/s; obstruction retracts the arm immediately
        float TpsHideRigDistance = 1.5f;  // avoid the player's body filling a retracted view
        // First-person head motion, driven by the stride phase.
        float HeadBobHeight = 0.016f;        // m, at run speed
        float HeadBobRollDegrees = 0.45f;
        float HeadBobSprintScale = 1.5f;
        float HeadBobAimScale = 0.2f;
        float StrafeRollDegreesPerMps = 0.35f;
        float StrafeRollMaxDegrees = 1.6f;
        float LandingDipPerMps = 0.12f;      // eye dip velocity per m/s of landing speed
        float LandingSpring = 140.0f;
        float LandingDamping = 15.0f;
        float SprintFovBoost = 7.0f;
        // Stride length (m) per cycle at the authored gait speeds; the bob phase
        // advances by distance so cadence follows speed.
        float StrideWalk = 1.51f;
        float StrideRun = 2.86f;
        float StrideSprint = 4.41f;
        float StrideCrouch = 1.24f;
    };

    struct FWeaponFeelConfig
    {
        // FPS view-model placement, in camera space (right, up, forward).
        float ViewModelScale = 0.65f;
        glm::vec3 ViewModelHipOffset{0.22f, -0.30f, 0.75f};
        glm::vec3 ViewModelAdsOffset{0.0f, -0.060f, 0.75f};
        float ViewModelLerpSpeed = 14.0f;
        float CameraRecoilSpring = 90.0f;
        float CameraRecoilDamping = 18.0f;
        float ViewModelRecoilSpring = 120.0f;
        float ViewModelRecoilDamping = 20.0f;
        float TracerLifetimeSeconds = 0.04f;
        float FireAnimSeconds = 0.22f;  // TPS fire clip hold
        float ReloadSeconds = 2.2f;
        float SwitchSeconds = 0.9f;
        // View-model motion (camera space: x right, y up, z forward).
        glm::vec3 ViewModelBob{0.011f, 0.009f, 0.0f};   // m, at run speed
        float ViewModelBobRollDegrees = 1.4f;
        float ViewModelBobSprintScale = 2.2f;
        float ViewModelBobAimScale = 0.1f;
        float ViewModelSwayLagSeconds = 0.03f;          // angular velocity -> lag angle
        float ViewModelSwayMaxDegrees = 6.0f;
        float ViewModelSwaySpring = 170.0f;
        float ViewModelSwayDamping = 19.0f;
        float ViewModelMoveRollDegreesPerMps = 1.1f;
        float ViewModelMoveOffsetPerMps = 0.005f;
        float ViewModelMoveSmoothingSeconds = 0.12f;
        glm::vec3 ViewModelSprintOffset{0.02f, -0.075f, -0.07f};
        glm::vec3 ViewModelSprintEulerDegrees{10.0f, 35.0f, -25.0f}; // pitch (+muzzle down), yaw (+left), roll (+clockwise)
        float ViewModelBreathHeight = 0.0022f;
        float ViewModelBreathSeconds = 3.4f;
        float ViewModelLandingKick = 0.018f;
        float ViewModelRecoilPitchDegrees = 4.0f;       // muzzle climb per cm of view-model kick
        float TpsWeaponScale = 0.9f;
    };

    struct FAnimationConfig
    {
        float LocomotionFadeSeconds = 0.14f;
        float MoveThreshold = 0.15f;
        // Speeds the generated gait clips were authored at (nextdayz_survivor.scad).
        float StandWalkAuthoredSpeed = 1.8f;
        float StandRunAuthoredSpeed = 4.2f;
        float StandSprintAuthoredSpeed = 7.6f;
        float CrouchWalkAuthoredSpeed = 1.55f;
        float MinPlayRate = 0.7f;
        float MaxPlayRate = 1.3f;
        float WeaponReadyPoseWeight = 1.0f;
        float AimFadeSeconds = 0.12f;
        float AimPitchLimitDegrees = 80.0f;
        float RecoilFadeOutSeconds = 0.04f;
        float JumpUpSeconds = 0.24f;
        float JumpDownSeconds = 0.32f;
        float JumpFadeSeconds = 0.08f;
        // Blend space smoothing.
        float StanceBlendSeconds = 0.18f;
        float DirectionSmoothingSeconds = 0.08f;
        float HoldFadeSeconds = 0.15f;
        float SprintHoldFadeSeconds = 0.18f;
        // Aim offsets: the camera pitch/yaw left over after the body is split
        // across torso and head; the rest rotates the weapon about the shoulder
        // so the muzzle stays exactly on the crosshair.
        float AimPitchTorsoShare = 0.45f;
        float AimPitchHeadShare = 0.4f;
        float TwistTorsoShare = 0.55f;
        float TwistHeadShare = 0.3f;
        // Lower body follows the camera: quickly while moving, in a turn-in-place
        // step once the upper body has twisted past the threshold.
        float BodyYawFollowSeconds = 0.08f;
        float TurnInPlaceThresholdDegrees = 55.0f;
        float TurnInPlaceRateDegrees = 380.0f;
        // Whole-body lean into acceleration (banking into turns, rocking back
        // when braking), on a damped spring so it overshoots and settles.
        float LeanRollPerMps2 = 0.9f;
        float LeanPitchPerMps2 = 0.55f;
        float LeanMaxRollDegrees = 9.0f;
        float LeanMaxPitchDegrees = 7.0f;
        float LeanSpring = 70.0f;
        float LeanDamping = 12.0f;
        // Weapon frame motion on top of the torso.
        float WeaponBobHeight = 0.010f;
        float WeaponBobSide = 0.007f;
        float SprintPumpHeight = 0.045f;
        float SprintPumpRollDegrees = 9.0f;
        float TurnSwayDegreesPerRadPerSecond = 2.5f;
        float TurnSwayMaxDegrees = 8.0f;
    };

    struct FTraversalConfig
    {
        float ProbeDistance = 1.15f;
        float ProbeHeight = 0.52f;
        float MinObstacleHeight = 0.32f;
        float VaultMaxHeight = 0.88f;
        float ClimbMaxHeight = 1.48f;
        float TopProbeInset = 0.12f;
        float MaxVaultDepth = 0.95f;
        float PlatformStandDepth = 0.32f;
        float SurfaceNormalMinY = 0.70f;
        float StandingClearance = 0.10f;
        float ArcClearance = 0.14f;
        float VaultDurationSeconds = 0.52f;
        float ClimbDurationSeconds = 0.86f;
    };

    struct FLootConfig
    {
        float ReachMeters = 3.0f;       // must be closer than this to interact
        float AimDotMin = 0.4f;         // crosshair-to-item alignment gate (cos angle)
        float SpawnRayHeight = 220.0f;  // downward raycast start height for spawn
    };

    struct FActionConfig
    {
        float LootDurationSeconds = 0.9f;
        float LootCommitNormalizedTime = 0.55f;
        float UseDurationSeconds = 1.35f;
        float WellDurationSeconds = 2.0f;
        float UseCommitNormalizedTime = 0.65f;
        float ActionFadeSeconds = 0.10f;
        float RestDurationSeconds = 6.0f;
    };

    struct FSurvivalConfig
    {
        float StartingHealth = 100.0f;
        float StartingHunger = 82.0f;
        float StartingHydration = 76.0f;
        float HungerDrainPerSecond = 100.0f / (45.0f * 60.0f);
        float HydrationDrainPerSecond = 100.0f / (30.0f * 60.0f);
        float SprintHungerMultiplier = 1.25f;
        float SprintHydrationMultiplier = 1.60f;
        float StarvationDamagePerSecond = 1.25f;
        float DehydrationDamagePerSecond = 2.0f;
        float DamageProtectionSeconds = 0.35f;
        // Bleeding: infected hits can open a wound; each one drains health
        // until a bandage, rags or a first aid kit closes them all.
        float BleedChance = 0.45f;
        int MaxWounds = 3;
        float BleedDamagePerSecondPerWound = 0.45f;
        // Well fed and watered (and warm, and not bleeding) the body heals.
        float WellFedThreshold = 70.0f;
        float WellFedRegenPerSecond = 0.06f;
        // Below this hunger or hydration the body is too weak to sprint.
        float WeakThreshold = 12.0f;
        // Cold (night, rain, too few clothes) burns food faster.
        float ColdHungerMultiplier = 1.0f;
        float PainkillerHeal = 25.0f;
        float PainkillerSeconds = 30.0f;
        float RestHeal = 20.0f;
        float RestHungerCost = 6.0f;
        float RestHydrationCost = 6.0f;
        float UnopenedCanShare = 0.5f;    // eaten without a can opener or blade
    };

    // Infected behaviour and body language. Clip timings must match
    // assets/scad/characters/nextdayz_infected.scad.
    struct FZombieConfig
    {
        // Steering: they turn and get up to speed like bodies, not cursors.
        float WanderTurnDegreesPerSecond = 100.0f;
        float InvestigateTurnDegreesPerSecond = 140.0f;
        float ChaseTurnDegreesPerSecond = 220.0f;    // a charging infected overshoots a sidestep
        float AttackTurnDegreesPerSecond = 260.0f;
        float AlertTurnDegreesPerSecond = 420.0f;
        float Acceleration = 7.0f;
        float Deceleration = 10.0f;
        float SeparationRadius = 0.70f;
        float PlayerPersonalSpace = 1.0f;            // arms' length: grabbing hands, not overlapping bodies
        float RoamRadius = 7.0f;
        int MaxPathSolvesPerUpdate = 4;
        int MaxPathExpansions = 6000;   // A* nodes per route; beyond that, get as close as it got
        float RoamMinSeconds = 4.0f;
        float RoamMaxSeconds = 9.0f;
        float LameChance = 0.18f;
        // Noticing the player: a scream that tells you it is coming. Skipped
        // when it is already close enough to bite.
        float AlertSeconds = 0.80f;
        float AlertSkipDistance = 3.0f;
        float LostSightSeconds = 0.6f;
        // Attacks. Damage lands on the strike frame; the infected stops
        // tracking just before it, so a late sidestep makes it miss.
        float LungeSeconds = 1.0f;
        float LungeStrikeSeconds = 0.45f;
        float SwipeSeconds = 0.9f;
        float SwipeStrikeSeconds = 0.42f;
        float TrackingLockLeadSeconds = 0.12f;
        float StrikeDistance = 1.2f;                 // closes in to this during the wind-up
        float StrikeReachBonus = 0.15f;
        float StrikeFacingDot = 0.45f;
        float AttackCooldownSeconds = 0.55f;
        // Hit reactions. Every hit flinches (procedural, the legs keep going)
        // and slows it briefly; impact builds poise damage, and breaking poise
        // staggers it back. A stagger grants a short immunity so automatic fire
        // cannot stun-lock.
        float PoiseMax = 60.0f;
        float PoiseRecoverPerSecond = 40.0f;
        float StaggerSeconds = 0.90f;
        float StaggerPushback = 0.45f;
        float StaggerImmunitySeconds = 1.6f;
        float FlinchSeconds = 0.25f;
        float FlinchSpeedScale = 0.45f;
        // Legs: enough leg damage lames it for good (hobble instead of run).
        float CrippleLegDamage = 45.0f;
        float CrippledSpeedScale = 0.55f;
        float LameWanderScale = 0.85f;
        float HeadMultiplier = 3.0f;
        float TorsoMultiplier = 1.0f;
        float LimbMultiplier = 0.6f;
        float LegMultiplier = 0.7f;
        float HeadImpactScale = 1.5f;
        float LegImpactScale = 1.3f;
        // Death: momentum carries the body a little as it falls.
        float DeathMomentumKeep = 0.7f;
        float DeathImpulsePerImpact = 0.025f;
        float DeathMaxImpulse = 2.5f;
        float DeathSlideSeconds = 0.5f;
        float CorpseSeconds = 25.0f;
        // Presentation (ZombieRigVisual).
        float WalkAuthoredSpeed = 0.95f;             // nextdayz_infected.scad gaits
        float RunAuthoredSpeed = 3.2f;
        float LimpAuthoredSpeed = 0.8f;
        float HobbleAuthoredSpeed = 1.7f;
        float MinPlayRate = 0.7f;
        float MaxPlayRate = 1.4f;
        float ActionFadeSeconds = 0.08f;
        float ReachDistance = 6.0f;                  // arms come up inside this
        float LookYawLimitDegrees = 75.0f;
        float LookPitchLimitDegrees = 50.0f;
        float LookTorsoShare = 0.35f;
        float FlinchSpring = 160.0f;
        float FlinchDamping = 14.0f;
        float FlinchPerImpact = 0.2f;                // rad/s of torso kick per point of impact
        float FlinchMaxSpeed = 7.0f;
        float HeadFlinchScale = 2.5f;
        float LegBuckleSpeed = 0.9f;                 // m/s pelvis drop on a leg hit
        float TurnLeanPerRadPerMps = 0.06f;
        float TurnLeanMaxDegrees = 10.0f;
    };

    struct FTimeConfig
    {
        double StartHour = 8.0;         // 08:00 first frame -> daytime
        double TimeScale = 1.0;         // 1 real second = 1 game minute (24 real-min day)
        float DaySunIntensity = 480.0f;
        float DaySkyIntensity = 120.0f;
        float DaySkyLuminanceScale = 4.0f; // procedural sky and GI, independent of HDR intensity
        float NightSkyFraction = 0.08f; // sky never goes fully black
        float OvercastFactor = 0.55f;   // sun multiplier while overcast
        float OvercastSkyFactor = 0.85f; // diffuse daylight remains when the direct sun is hidden
        // Weather cycle (game hours between changes).
        double WeatherMinHours = 2.5;
        double WeatherMaxHours = 5.5;
        // Height fog: density per weather, a morning mist on top, and the fog
        // colour's radiance (same scale as the sun: ~50 reads as daylight haze).
        float ClearFogDensity = 0.0035f;
        float OvercastFogDensity = 0.0075f;
        float FogWeatherDensity = 0.028f;
        float DawnMistDensity = 0.008f;
        float NightFogDensity = 0.004f;
        float FogHeightFalloff = 0.05f;
        glm::vec3 FogColor{0.60f, 0.63f, 0.66f};
        float FogDayRadiance = 50.0f;
        float FogNightRadiance = 0.15f;
        float LanternNightLift = 0.10f; // extra night sky fraction while a lantern burns
    };

    struct FConfig
    {
        uint32_t Seed = 1337;
        FPlayerConfig Player{};
        FCameraConfig Camera{};
        FWeaponFeelConfig Weapon{};
        FAnimationConfig Animation{};
        FTraversalConfig Traversal{};
        FLootConfig Loot{};
        FActionConfig Action{};
        FSurvivalConfig Survival{};
        FZombieConfig Zombie{};
        FTimeConfig Time{};

        // Default spawn: bridge-west gas station (map centre, flat, stocked).
        // scad pad (-30,-177) -> engine XZ (x, -y) = (-30, 177). Ground Y is
        // resolved at runtime with a downward raycast (see PlayerController).
        glm::vec3 SpawnXZ{-30.0f, 0.0f, 177.0f};
    };
}
