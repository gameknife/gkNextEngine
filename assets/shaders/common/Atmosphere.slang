#ifndef GK_ATMOSPHERE_SLANG
#define GK_ATMOSPHERE_SLANG

#ifdef __cplusplus
#define ATMOS_ALIGN_16 alignas(16)
#else
#define ATMOS_ALIGN_16
#endif

public static const uint ATMOSPHERE_FLAG_SKY = 1u << 0u;
public static const uint ATMOSPHERE_FLAG_AERIAL_PERSPECTIVE = 1u << 1u;
public static const uint ATMOSPHERE_FLAG_HEIGHT_FOG = 1u << 2u;

// Shared CPU/GPU contract. Distances in the physical atmosphere model are kilometres;
// AerialPerspectiveMaxDistance and fog distances/heights remain in engine world units.
public struct ATMOS_ALIGN_16 FAtmosphereParams
{
    public float3 RayleighScattering;   public float RayleighDensityH;
    public float3 MieScattering;        public float MieDensityH;
    public float3 MieAbsorption;        public float MiePhaseG;
    public float3 OzoneAbsorption;      public float OzoneCenterAltitude;
    public float3 GroundAlbedo;         public float OzoneWidth;

    public float BottomRadius;
    public float TopRadius;
    public float WorldUnitsPerKm;
    public float WorldOriginAltitude;

    public uint TransmittanceLutId;
    public uint MultiScatterLutId;
    public uint SkyViewLutId;
    public uint AerialPerspectiveLutId;

    public float AerialPerspectiveMaxDistance;
    public float SkyLuminanceScale;
    public uint Flags;
    public float Pad0;

    public float3 FogInscatteringColor; public float FogDensity;
    public float FogHeightFalloff;      public float FogBaseHeight;
    public float FogStartDistance;      public float FogMaxOpacity;
};

#ifndef __cplusplus
namespace Atmosphere
{
    // Raymarch budgets. Mobile runs every count at exactly half the desktop value; the LUT
    // resolutions in AtmosphereSubsystem.cpp are halved by the same `IOS || ANDROID` condition.
    // Keep them here rather than as literals in each pass: the sky-view and aerial-perspective
    // passes must stay in the same ballpark or their shared horizon stops matching, and a
    // per-file literal is how that drifts.
#ifdef PLATFORM_MOBILE
    public static const uint STEPS_TRANSMITTANCE = 20;
    public static const uint STEPS_SKY_VIEW = 16;
    public static const uint STEPS_SKY_VIEW_SUN = 6;
    public static const uint STEPS_AERIAL = 8;
    public static const uint STEPS_AERIAL_SUN = 4;
#else
    public static const uint STEPS_TRANSMITTANCE = 40;
    public static const uint STEPS_SKY_VIEW = 32;
    public static const uint STEPS_SKY_VIEW_SUN = 12;
    public static const uint STEPS_AERIAL = 16;
    public static const uint STEPS_AERIAL_SUN = 8;
#endif

    public float SafeSqrt(float value)
    {
        return sqrt(max(value, 0.0f));
    }

    public float RaySphereNearest(float3 origin, float3 direction, float radius)
    {
        const float b = dot(origin, direction);
        const float c = dot(origin, origin) - radius * radius;
        const float discriminant = b * b - c;
        if (discriminant < 0.0f)
        {
            return -1.0f;
        }
        const float root = SafeSqrt(discriminant);
        const float nearT = -b - root;
        const float farT = -b + root;
        return nearT >= 0.0f ? nearT : farT;
    }

    public float3 MediumExtinction(FAtmosphereParams params, float altitude)
    {
        const float rayleighDensity = exp(-max(altitude, 0.0f) / max(params.RayleighDensityH, 1.0e-3f));
        const float mieDensity = exp(-max(altitude, 0.0f) / max(params.MieDensityH, 1.0e-3f));
        const float ozoneDensity =
            saturate(1.0f - abs(altitude - params.OzoneCenterAltitude) / max(params.OzoneWidth, 1.0e-3f));
        return params.RayleighScattering * rayleighDensity +
               (params.MieScattering + params.MieAbsorption) * mieDensity +
               params.OzoneAbsorption * ozoneDensity;
    }

    public float3 IntegrateTransmittance(
        FAtmosphereParams params, float3 origin, float3 direction, float distance, uint stepCount)
    {
        const float stepLength = max(distance, 0.0f) / float(max(stepCount, 1u));
        float3 opticalDepth = float3(0.0f);
        for (uint i = 0; i < stepCount; ++i)
        {
            const float t = (float(i) + 0.5f) * stepLength;
            const float altitude = length(origin + direction * t) - params.BottomRadius;
            opticalDepth += MediumExtinction(params, altitude) * stepLength;
        }
        return exp(-opticalDepth);
    }

    // Azimuth basis for the sky-view LUT parameterisation. A direction pointing straight up or
    // down has no horizontal component at all, and the obvious `normalize(xz + tiny_epsilon)`
    // returns NaN on any driver that flushes the *squared* epsilon to zero -- which is what mobile
    // relaxed-precision arithmetic does. Bake.ClearAmbientCubeCache samples exactly (0,+-1,0), so
    // that is not a corner case here, it is a guaranteed one. Pick the fallback axis explicitly.
    // Every producer and consumer of the LUT must call this so the forward and inverse azimuth
    // mappings agree on the fallback.
    public float3 HorizontalDirection(float3 direction)
    {
        const float2 horizontal = float2(direction.x, direction.z);
        const float lengthSquared = dot(horizontal, horizontal);
        if (!(lengthSquared > 1.0e-6f))
        {
            return float3(1.0f, 0.0f, 0.0f);
        }
        return float3(horizontal.x, 0.0f, horizontal.y) * rsqrt(lengthSquared);
    }

    public float RayleighPhase(float cosTheta)
    {
        return 3.0f * (1.0f + cosTheta * cosTheta) / (16.0f * 3.14159265359f);
    }

    public float MiePhase(float cosTheta, float g)
    {
        const float g2 = g * g;
        return (1.0f - g2) /
               (4.0f * 3.14159265359f *
                max(pow(1.0f + g2 - 2.0f * g * cosTheta, 1.5f), 1.0e-4f));
    }
}
#endif

#ifdef __cplusplus
#undef ATMOS_ALIGN_16
#endif

#endif
