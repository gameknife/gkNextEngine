#pragma once

// ============================================================================
// BulletPattern.hpp - What a volley looks like, as a pure function.
//
// Evaluate() takes a spec plus the shooter's situation and writes bullet seeds
// into a caller-provided span. It touches no scene, no pool and no clock, which
// is the point: bullet patterns are the part of a shmup most worth unit-testing
// and least worth debugging through the renderer.
//
// Everything is 2D on the XZ plane at the shooter's altitude. Angles are
// measured the way the rest of the game measures facing: 0 = engine +Z (towards
// the bottom of the screen, i.e. at the player), growing towards +X.
// ============================================================================

#include <span>

#include <glm/glm.hpp>

#include "Application/Game/NextRaiden/NextRaidenTypes.hpp"

namespace NextRaiden
{
    struct FPatternSpec
    {
        EPatternKind Kind = EPatternKind::Aimed;
        EBulletVisual Visual = EBulletVisual::BoltHostile;
        /// Bullets per volley for Spread and Ring, arms for Spiral. Ignored elsewhere.
        int Count = 1;
        float ArcDegrees = 30.0f;          // Spread: total width of the fan
        float SpinDegrees = 0.0f;          // Ring: extra rotation applied per volley
        float RateDegreesPerSec = 120.0f;  // Spiral/Sweep: how fast the phase turns
        float SweepDegrees = 45.0f;        // Sweep: half-width of the oscillation
        int Burst = 1;                     // Stream: shots per burst
        float BurstGapSeconds = 0.08f;
        float IntervalSeconds = 1.2f;      // between volleys
        float Speed = 22.0f;
        /// Aimed/Spread/Stream: how far ahead of the player to lead. 0 shoots where the
        /// player is, which at low bullet speeds means always shooting behind them.
        float LeadSeconds = 0.0f;
        float Damage = 1.0f;
    };

    /// Everything about *this* volley that is not in the spec.
    struct FPatternContext
    {
        glm::vec3 Origin{0.0f};
        glm::vec3 TargetPosition{0.0f};
        glm::vec3 TargetVelocity{0.0f};
        /// Counts up once per volley; Ring uses it to rotate, so successive rings interleave.
        int VolleyIndex = 0;
        /// Continuous phase in degrees, advanced by the shooter at RateDegreesPerSec.
        float PhaseDegrees = 0.0f;
    };

    struct FBulletSeed
    {
        glm::vec3 Position{0.0f};
        glm::vec3 Velocity{0.0f};
        EBulletVisual Visual = EBulletVisual::BoltHostile;
        float Damage = 1.0f;
    };

    /// Direction for an angle in degrees, 0 = +Z, growing towards +X.
    glm::vec3 DirectionFromAngle(float degrees);
    /// Inverse of the above; the value a node's yaw should take to face `direction`.
    float AngleFromDirection(const glm::vec3& direction);

    /// Writes at most `out.size()` seeds and returns how many it wrote. A spec asking for more
    /// bullets than the span holds is truncated rather than dropped: half a ring still reads as
    /// an attack, an empty volley reads as a bug.
    int EvaluatePattern(const FPatternSpec& spec, const FPatternContext& context, std::span<FBulletSeed> out);

    /// Upper bound on what EvaluatePattern can write for this spec. Callers size their
    /// scratch buffer with it.
    int PatternBulletCount(const FPatternSpec& spec);
}
