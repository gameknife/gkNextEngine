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
    };

    struct FTimeConfig
    {
        double StartHour = 8.0;         // 08:00 first frame -> daytime
        double TimeScale = 1.0;         // 1 real second = 1 game minute (24 real-min day)
        float DaySunIntensity = 480.0f;
        float DaySkyIntensity = 120.0f;
        float NightSkyFraction = 0.12f; // sky never goes fully black
        float OvercastFactor = 0.55f;   // sun/sky multiplier while overcast
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
        FTimeConfig Time{};

        // Default spawn: bridge-west gas station (map centre, flat, stocked).
        // scad pad (-30,-177) -> engine XZ (x, -y) = (-30, 177). Ground Y is
        // resolved at runtime with a downward raycast (see PlayerController).
        glm::vec3 SpawnXZ{-30.0f, 0.0f, 177.0f};
    };
}
