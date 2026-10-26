#include "Application/Game/NextRaiden/Combat/BulletPattern.hpp"

#include <algorithm>
#include <cmath>

#include <glm/gtc/constants.hpp>

namespace NextRaiden
{
    namespace
    {
        constexpr float kDegToRad = glm::pi<float>() / 180.0f;

        /// Where to shoot so a bullet of `speed` meets a target that keeps moving. Solved the
        /// cheap way (one iteration off the current distance) rather than the quadratic: at shmup
        /// speeds the difference is millimetres, and the quadratic has a no-solution branch that
        /// would need a fallback anyway.
        glm::vec3 LeadPoint(const FPatternContext& context, float leadSeconds)
        {
            return context.TargetPosition + context.TargetVelocity * leadSeconds;
        }

        float AimAngle(const FPatternSpec& spec, const FPatternContext& context)
        {
            const glm::vec3 to = LeadPoint(context, spec.LeadSeconds) - context.Origin;
            if (std::abs(to.x) < 1e-5f && std::abs(to.z) < 1e-5f)
            {
                return 0.0f; // shooter sits on the target: fire straight down the screen
            }
            return AngleFromDirection(to);
        }

        int Emit(std::span<FBulletSeed> out, int index, const FPatternSpec& spec, const glm::vec3& origin,
                 float angleDegrees)
        {
            if (index >= static_cast<int>(out.size()))
            {
                return index;
            }
            FBulletSeed& seed = out[static_cast<size_t>(index)];
            seed.Position = origin;
            seed.Velocity = DirectionFromAngle(angleDegrees) * spec.Speed;
            seed.Visual = spec.Visual;
            seed.Damage = spec.Damage;
            return index + 1;
        }
    }

    glm::vec3 DirectionFromAngle(float degrees)
    {
        const float radians = degrees * kDegToRad;
        return glm::vec3(std::sin(radians), 0.0f, std::cos(radians));
    }

    float AngleFromDirection(const glm::vec3& direction)
    {
        return std::atan2(direction.x, direction.z) / kDegToRad;
    }

    int PatternBulletCount(const FPatternSpec& spec)
    {
        switch (spec.Kind)
        {
        case EPatternKind::Spread:
        case EPatternKind::Ring:
        case EPatternKind::Spiral:
            return std::max(1, spec.Count);
        case EPatternKind::Aimed:
        case EPatternKind::Stream:
        case EPatternKind::Sweep:
        default:
            return 1;
        }
    }

    int EvaluatePattern(const FPatternSpec& spec, const FPatternContext& context, std::span<FBulletSeed> out)
    {
        if (out.empty())
        {
            return 0;
        }

        const int count = std::max(1, spec.Count);
        int written = 0;

        switch (spec.Kind)
        {
        case EPatternKind::Aimed:
        case EPatternKind::Stream:
            // Stream differs from Aimed only in timing - the shooter fires it Burst times with
            // BurstGapSeconds between - so the shape of one shot is identical.
            written = Emit(out, written, spec, context.Origin, AimAngle(spec, context));
            break;

        case EPatternKind::Spread:
        {
            const float centre = AimAngle(spec, context);
            if (count == 1)
            {
                written = Emit(out, written, spec, context.Origin, centre);
                break;
            }
            // The fan spans ArcDegrees end to end, so the outermost bullets sit on the edges
            // and the spacing is the same whether the count is odd or even.
            const float step = spec.ArcDegrees / static_cast<float>(count - 1);
            const float start = centre - spec.ArcDegrees * 0.5f;
            for (int i = 0; i < count; ++i)
            {
                written = Emit(out, written, spec, context.Origin, start + step * static_cast<float>(i));
            }
            break;
        }

        case EPatternKind::Ring:
        {
            const float step = 360.0f / static_cast<float>(count);
            const float offset = spec.SpinDegrees * static_cast<float>(context.VolleyIndex);
            for (int i = 0; i < count; ++i)
            {
                written = Emit(out, written, spec, context.Origin, offset + step * static_cast<float>(i));
            }
            break;
        }

        case EPatternKind::Spiral:
        {
            // Arms evenly spaced around the circle, the whole thing turning with the phase the
            // shooter advances. One volley is one shot per arm.
            const float step = 360.0f / static_cast<float>(count);
            for (int i = 0; i < count; ++i)
            {
                written = Emit(out, written, spec, context.Origin, context.PhaseDegrees + step * static_cast<float>(i));
            }
            break;
        }

        case EPatternKind::Sweep:
        {
            // A single shot whose angle oscillates: over time the stream paints an arc across
            // the playfield. Centred on the aim direction so it still threatens the player.
            const float centre = AimAngle(spec, context);
            const float swing = spec.SweepDegrees * std::sin(context.PhaseDegrees * kDegToRad);
            written = Emit(out, written, spec, context.Origin, centre + swing);
            break;
        }

        case EPatternKind::Count:
        default:
            break;
        }

        return written;
    }
}
