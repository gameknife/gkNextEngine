import Shader.Bindless;
import Shader.Types;
import Shader.GpuScene;
import Shader.ShadingScheduler;

float3 ReconstructWorldPosition(
    UniformBufferObject camera, int2 pixel, uint2 size, float depth)
{
    const float2 ndc = ((float2(pixel) + 0.5f) / float2(size)) * 2.0f - 1.0f;
    const float4 viewPositionH = mul(camera.ProjectionInverse, float4(ndc, depth, 1.0f));
    const float safeW = abs(viewPositionH.w) > 1.0e-6f
        ? viewPositionH.w
        : (viewPositionH.w >= 0.0f ? 1.0e-6f : -1.0e-6f);
    const float3 viewPosition = viewPositionH.xyz / safeW;
    return mul(camera.ModelViewInverse, float4(viewPosition, 1.0f)).xyz;
}

float3 WorldRayDirection(UniformBufferObject camera, int2 pixel, uint2 size)
{
    const float2 ndc = ((float2(pixel) + 0.5f) / float2(size)) * 2.0f - 1.0f;
    const float4 viewTarget = mul(camera.ProjectionInverse, float4(ndc, 1.0f, 1.0f));
    // Transform a direction directly: subtracting two world positions at the near
    // plane loses precision and produces visible bands in the horizon fog.
    return normalize(mul(camera.ModelViewInverse, float4(normalize(viewTarget.xyz), 0.0f)).xyz);
}

float HeightFogTransmittance(
    FAtmosphereParams params, float3 cameraPosition, float3 direction, float distance, bool sky)
{
    const float startDistance = sky ? params.FogStartDistance : min(params.FogStartDistance, distance);
    const float activeDistance = distance - startDistance;
    if ((!sky && activeDistance <= 0.0f) || params.FogDensity <= 0.0f)
    {
        return 1.0f;
    }
    const float3 fogStartPosition = cameraPosition + direction * startDistance;
    const float startDensity =
        params.FogDensity * exp(clamp(
            -params.FogHeightFalloff * (fogStartPosition.y - params.FogBaseHeight), -80.0f, 80.0f));
    const float heightSlope = params.FogHeightFalloff * direction.y;
    float opticalDepth;
    if (sky)
    {
        // The same medium continues to infinity for a sky ray. Upward rays leave
        // the layer; horizontal/downward rays reach the configured opacity cap.
        opticalDepth = heightSlope > 0.0f ? startDensity / heightSlope : 80.0f;
    }
    else
    {
        const float exponent = heightSlope * activeDistance;
        const float integralFactor = abs(exponent) < 1.0e-4f
            ? 1.0f
            : (1.0f - exp(clamp(-exponent, -80.0f, 80.0f))) / exponent;
        opticalDepth = max(startDensity * activeDistance * integralFactor, 0.0f);
    }
    const float rawTransmittance = exp(-opticalDepth);
    return max(rawTransmittance, 1.0f - params.FogMaxOpacity);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const UniformBufferObject camera = Bindless.GetGpuscene().Camera[0];
    if (camera.AtmosphereParams == 0)
    {
        return;
    }
    const FAtmosphereParams params = ((FAtmosphereParams*)camera.AtmosphereParams)[0];
    const uint2 size = Bindless.GetViewStorageTextureDimensions<float4>(Bindless.RT_SCENE_COLOR);
    if (any(dispatchThreadId.xy >= size))
    {
        return;
    }

    const int2 pixel = int2(dispatchThreadId.xy);

    // Sparse checkerboard lighting: the parity Core Shading did not write is left untouched all the
    // way to Native TAAU, which reconstructs it from history instead of from a neighbour copy.
    if (Scheduler.ShouldSkipPixel(uint2(pixel), camera.CheckerboardSparseLighting, true,
                                  camera.TotalFrames))
    {
        return;
    }

    const float depth = Bindless.GetViewStorageTexture<float>(Bindless.RT_PREV_DEPTHBUFFER)[pixel];
    const bool surface = depth >= 0.0f && depth <= 1.0f;
    if (!surface && (camera.ForceBlackBackground || camera.BackgroundMode == BACKGROUND_MODE_STUDIO ||
        (!camera.HasSky && (params.Flags & ATMOSPHERE_FLAG_SKY) == 0)))
    {
        return;
    }

    const float4 originalColor =
        Bindless.GetViewStorageTexture<float4>(Bindless.RT_SCENE_COLOR)[pixel];
    const float3 cameraPosition =
        mul(camera.ModelViewInverse, float4(0.0f, 0.0f, 0.0f, 1.0f)).xyz;
    const float3 worldPosition = surface
        ? ReconstructWorldPosition(camera, pixel, size, depth) : cameraPosition;
    const float distance = length(worldPosition - cameraPosition);
    const float3 direction = surface
        ? (worldPosition - cameraPosition) / max(distance, 1.0e-5f)
        : WorldRayDirection(camera, pixel, size);

    // Compose/GTAOCompose already pre-exposed RT_SCENE_COLOR. LUT radiance and
    // artist-authored fog radiance must enter that same space before blending.
    const float exposure = max(camera.PaperWhiteNit / 40000.0f, 1.0e-6f);

    float3 color = originalColor.rgb;
    float3 aerialInScatter = float3(0.0f);
    float aerialTransmittance = 1.0f;
    const bool hasAerialVolume = Bindless.GetGpuscene().CustomData1 != 0;
    // Sky radiance already includes its atmospheric scattering in the sky LUT.
    if (surface && hasAerialVolume && (params.Flags & ATMOSPHERE_FLAG_AERIAL_PERSPECTIVE) != 0)
    {
        const float3 uvw = float3(
            (float2(pixel) + 0.5f) / float2(size),
            saturate(distance / max(params.AerialPerspectiveMaxDistance, 1.0f)));
        const float4 aerial =
            Bindless.GetSampleTexture3D(params.AerialPerspectiveLutId).SampleLevel(uvw, 0.0f);
        aerialInScatter = aerial.rgb * camera.SunColor.rgb *
            camera.SkyColor.rgb * params.SkyLuminanceScale * exposure;
        aerialTransmittance = saturate(aerial.a);
        color = aerialInScatter + color * aerialTransmittance;
    }

    float fogTransmittance = 1.0f;
    if ((params.Flags & ATMOSPHERE_FLAG_HEIGHT_FOG) != 0)
    {
        fogTransmittance =
            HeightFogTransmittance(params, cameraPosition, direction, distance, !surface);
        color = params.FogInscatteringColor * exposure * (1.0f - fogTransmittance) +
                color * fogTransmittance;
    }

    const uint debugMode = Bindless.GetGpuscene().CustomData2;
    if (debugMode == 1u)
    {
        color = aerialInScatter;
    }
    else if (debugMode == 2u)
    {
        color = float3(aerialTransmittance * fogTransmittance);
    }
    else if (debugMode == 3u)
    {
        const float3 worldDirection = WorldRayDirection(camera, pixel, size);
        const float3 directionHorizontal = Atmosphere.HorizontalDirection(worldDirection);
        const float3 sunHorizontal =
            Atmosphere.HorizontalDirection(normalize(camera.SunDirection.xyz));
        const float2 skyUv = float2(
            dot(directionHorizontal, sunHorizontal) * 0.5f + 0.5f,
            sign(worldDirection.y) * sqrt(abs(worldDirection.y)) * 0.5f + 0.5f);
        color = Bindless.GetSampleTexture(params.SkyViewLutId).SampleLevel(skyUv, 0.0f).rgb;
    }

    // Preserve scene coverage for downstream consumers; background color is already real data.
    Bindless.GetViewStorageTexture<float4>(Bindless.RT_SCENE_COLOR)[pixel] =
        float4(max(color, float3(0.0f)), originalColor.a);
}
