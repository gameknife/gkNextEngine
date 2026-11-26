import Shader.Bindless;
import Shader.Types;
import Shader.GpuScene;
import Shader.Tonemap;
import Shader.ShadingScheduler;
import Shader.SurfaceBuffer;

// Direct sample compose. Realtime rendering reads the current diffuse/specular sample;
// explicit offline progressive rendering reads the running-average linear scene color instead.
// The resulting scene color is pre-exposed linear HDR with display white near 1.0. Display
// encoding is performed only by the final output pass after temporal upscaling.
//
// GPUScene.CustomData1: 1 = progressive (read RT_PROGRESSIVE_SCENE_COLOR).
// GPUScene.CustomData2: firefly sigma as float bits, 0 = no firefly suppression. The host sends 0
// for progressive / offline reference rendering, Ray Reconstruction (which denoises the raw planes
// itself) and sparse checkerboard lighting (half the neighbours are unwritten).

static const uint OBJECT_ID_FLAG_VALID = 0x80000000u;
static const uint OBJECT_ID_FLAG_SELECTED = 0x40000000u;
static const uint OBJECT_ID_FLAG_HOVERED = 0x20000000u;
static const uint OBJECT_ID_FLAG_LOCKED = 0x10000000u;
static const uint OBJECT_ID_FLAG_DANGER = 0x08000000u;
static const float DIAGONAL_EDGE_WEIGHT = 0.70710678;

float Luminance(float3 color)
{
    return dot(max(color, float3(0.0)), float3(0.2126, 0.7152, 0.0722));
}

bool HasObjectFlag(uint objectId, uint flag)
{
    return ((objectId & OBJECT_ID_FLAG_VALID) != 0) && ((objectId & flag) != 0);
}

float3 LoadComposedRadiance(int2 pixel, bool progressiveRender)
{
    const UniformBufferObject camera = Bindless.GetGpuscene().Camera[0];
    if (progressiveRender)
    {
        return Bindless.GetViewStorageTexture<float4>(
            Bindless.RT_PROGRESSIVE_SCENE_COLOR)[pixel].rgb;
    }

    const float3 diffuse = Bindless.GetViewStorageTexture<float4>(
        Bindless.RT_SINGLE_DIFFUSE)[pixel].rgb;
    const float3 specular = Bindless.GetViewStorageTexture<float4>(
        Bindless.RT_SINGLE_SPECULAR)[pixel].rgb;
    const float3 albedo = Bindless.GetViewStorageTexture<float4>(
        Bindless.RT_ALBEDO)[pixel].rgb;
    return camera.DebugDraw_Lighting
        ? diffuse * float3(0.5, 0.5, 0.5) + specular
        : diffuse * albedo + specular;
}

// Monte Carlo firefly suppression, in the demodulated domain and before the temporal upscaler can
// lock the outlier into history or spread it with a negative-lobe kernel. Diffuse has albedo
// divided out, so texture contrast never looks like an outlier; only neighbours on the same instance
// are compared, so geometric edges never do either. The ceiling is deliberately soft - the larger of
// mean + k sigma and twice the second-brightest neighbour - so it only catches the 10x-100x single
// pixels a 1-2 spp estimator produces, never a genuinely bright region. Nothing is written back:
// RT_SINGLE_DIFFUSE / RT_SINGLE_SPECULAR stay raw for every other reader.
float FireflyScale(RWTexture2D<float4> signal, RWTexture2D<uint> objectIds, int2 pixel,
                   int2 viewportMin, int2 viewportMax, uint identity, float centerLuma,
                   float sigma, float lumaFloor)
{
    float sum = 0.0f;
    float sumSquares = 0.0f;
    float maxLuma = 0.0f;
    float secondMaxLuma = 0.0f;
    float count = 0.0f;
    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            const int2 tap = pixel + int2(x, y);
            if ((x == 0 && y == 0) || any(tap < viewportMin) || any(tap > viewportMax) ||
                Surface.ObjectIdentity(objectIds[tap]) != identity)
            {
                continue;
            }
            const float luma = Luminance(signal[tap].rgb);
            sum += luma;
            sumSquares += luma * luma;
            count += 1.0f;
            secondMaxLuma = max(secondMaxLuma, min(luma, maxLuma));
            maxLuma = max(maxLuma, luma);
        }
    }
    // A thin feature has too few same-instance neighbours to tell an outlier from detail.
    if (count < 3.0f)
    {
        return 1.0f;
    }
    const float mean = sum / count;
    const float deviation = sqrt(max(sumSquares / count - mean * mean, 0.0f));
    const float ceiling = max(mean + sigma * deviation, secondMaxLuma * 2.0f + lumaFloor);
    return centerLuma > ceiling ? ceiling / max(centerLuma, 1.0e-8f) : 1.0f;
}

float3 LoadClampedRadiance(int2 pixel, int2 viewportMin, int2 viewportMax, float fireflySigma,
                           float exposure)
{
    const UniformBufferObject camera = Bindless.GetGpuscene().Camera[0];
    const uint viewBase = Bindless.GetGpuscene().CustomData0;
    let diffusePlane = Bindless.GetViewStorageTexture<float4>(Bindless.RT_SINGLE_DIFFUSE);
    let specularPlane = Bindless.GetViewStorageTexture<float4>(Bindless.RT_SINGLE_SPECULAR);
    let objectIds = Bindless.GetViewStorageTexture<uint>(Bindless.RT_OBJECTID_0);

    float3 diffuse = diffusePlane[pixel].rgb;
    float3 specular = specularPlane[pixel].rgb;
    const float3 albedo = Bindless.GetViewStorageTexture<float4>(Bindless.RT_ALBEDO)[pixel].rgb;

    // Background, degenerate samples and emitters carry no noisy lighting.
    const uint objectId = objectIds[pixel];
    const uint2 bsdf = Surface.LoadBsdf(viewBase, pixel);
    if (Surface.IsValid(Surface.LoadDepth(viewBase, pixel)) && bsdf.x != Surface.kInvalidMaterialId &&
        !Surface.HasFlag(Surface.UnpackFlags(bsdf.y), Surface.kFlagEmissive))
    {
        const uint identity = Surface.ObjectIdentity(objectId);
        // 5% of display white, in raw radiance units.
        const float lumaFloor = 0.05f / exposure;
        diffuse *= FireflyScale(diffusePlane, objectIds, pixel, viewportMin, viewportMax, identity,
                                Luminance(diffuse), fireflySigma, lumaFloor);
        // A sharp mirror legitimately shows a pinpoint sun or light; only glossy lobes are clamped.
        if (Surface.LoadGeometry(viewBase, pixel).Roughness > 0.15f)
        {
            specular *= FireflyScale(specularPlane, objectIds, pixel, viewportMin, viewportMax,
                                     identity, Luminance(specular), fireflySigma, lumaFloor);
        }
    }
    return camera.DebugDraw_Lighting
        ? diffuse * float3(0.5, 0.5, 0.5) + specular
        : diffuse * albedo + specular;
}

float ComputeEdgeStrengthTwoSided(RWTexture2D<uint> objectImage, int2 pixel, uint flag)
{
    const bool centerFlagged = HasObjectFlag(objectImage[pixel], flag);

    float edgeSum = 0.0;
    float weightSum = 0.0;
    edgeSum += (HasObjectFlag(objectImage[pixel + int2(1, 0)], flag) != centerFlagged) ? 1.0 : 0.0;
    edgeSum += (HasObjectFlag(objectImage[pixel + int2(-1, 0)], flag) != centerFlagged) ? 1.0 : 0.0;
    edgeSum += (HasObjectFlag(objectImage[pixel + int2(0, 1)], flag) != centerFlagged) ? 1.0 : 0.0;
    edgeSum += (HasObjectFlag(objectImage[pixel + int2(0, -1)], flag) != centerFlagged) ? 1.0 : 0.0;
    weightSum += 4.0;

    edgeSum += ((HasObjectFlag(objectImage[pixel + int2(1, 1)], flag) != centerFlagged) ? 1.0 : 0.0) * DIAGONAL_EDGE_WEIGHT;
    edgeSum += ((HasObjectFlag(objectImage[pixel + int2(-1, -1)], flag) != centerFlagged) ? 1.0 : 0.0) * DIAGONAL_EDGE_WEIGHT;
    edgeSum += ((HasObjectFlag(objectImage[pixel + int2(-1, 1)], flag) != centerFlagged) ? 1.0 : 0.0) * DIAGONAL_EDGE_WEIGHT;
    edgeSum += ((HasObjectFlag(objectImage[pixel + int2(1, -1)], flag) != centerFlagged) ? 1.0 : 0.0) * DIAGONAL_EDGE_WEIGHT;
    weightSum += DIAGONAL_EDGE_WEIGHT * 4.0;

    return smoothstep(0.1, 0.4, edgeSum / max(1e-5, weightSum));
}

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const UniformBufferObject camera = Bindless.GetGpuscene().Camera[0];
    const uint2 outputSize = Bindless.GetViewStorageTextureDimensions<float4>(Bindless.RT_SCENE_COLOR);
    if (any(dispatchThreadId.xy >= outputSize))
    {
        return;
    }

    // Sparse checkerboard lighting: the parity Core Shading did not write is left untouched all the
    // way to Native TAAU, which reconstructs it from history instead of from a neighbour copy.
    if (Scheduler.ShouldSkipPixel(dispatchThreadId.xy, camera.CheckerboardSparseLighting, true,
                                  camera.TotalFrames))
    {
        return;
    }

    const bool progressiveRender = Bindless.GetGpuscene().CustomData1 != 0u;
    let objectId0 = Bindless.GetViewStorageTexture<uint>(Bindless.RT_OBJECTID_0);
    let objectId1 = Bindless.GetViewStorageTexture<uint>(Bindless.RT_OBJECTID_1);
    let sceneColor = Bindless.GetViewStorageTexture<float4>(Bindless.RT_SCENE_COLOR);

    const int2 pixel = int2(dispatchThreadId.xy) + int2(camera.ViewportRect.xy);
    const float exposure = max(camera.PaperWhiteNit / 40000.0, 1.0e-6);
    const float fireflySigma = asfloat(Bindless.GetGpuscene().CustomData2);
    float3 radiance;
    if (!progressiveRender && fireflySigma > 0.0)
    {
        const int2 viewportMin = int2(camera.ViewportRect.xy);
        const int2 viewportMax = min(viewportMin + int2(camera.ViewportRect.zw) - int2(1, 1),
                                     int2(outputSize) - int2(1, 1));
        radiance = LoadClampedRadiance(pixel, viewportMin, viewportMax, fireflySigma, exposure);
    }
    else
    {
        radiance = LoadComposedRadiance(pixel, progressiveRender);
    }
    float3 total = max(radiance, 0.0) * exposure;

    const bool noSkyBackground =
        camera.BackgroundMode != BACKGROUND_MODE_STUDIO &&
        !camera.HasSky && !HasObjectFlag(objectId0[pixel], OBJECT_ID_FLAG_VALID);
    if (noSkyBackground)
    {
        total = NoSkyClearSceneColor(camera.HDR, camera.PaperWhiteNit);
    }

    const float selectedEdge = saturate(
        ComputeEdgeStrengthTwoSided(objectId0, pixel, OBJECT_ID_FLAG_SELECTED) * 0.80 +
        ComputeEdgeStrengthTwoSided(objectId1, pixel, OBJECT_ID_FLAG_SELECTED) * 0.20);
    const float hoveredEdge = saturate(
        ComputeEdgeStrengthTwoSided(objectId0, pixel, OBJECT_ID_FLAG_HOVERED) * 0.75 +
        ComputeEdgeStrengthTwoSided(objectId1, pixel, OBJECT_ID_FLAG_HOVERED) * 0.25);
    const float lockedEdge = saturate(
        ComputeEdgeStrengthTwoSided(objectId0, pixel, OBJECT_ID_FLAG_LOCKED) * 0.75 +
        ComputeEdgeStrengthTwoSided(objectId1, pixel, OBJECT_ID_FLAG_LOCKED) * 0.25);
    const float dangerEdge = saturate(
        ComputeEdgeStrengthTwoSided(objectId0, pixel, OBJECT_ID_FLAG_DANGER) * 0.90 +
        ComputeEdgeStrengthTwoSided(objectId1, pixel, OBJECT_ID_FLAG_DANGER) * 0.25);

    if (dangerEdge > 0.0)
    {
        total = lerp(total, float3(255, 45, 35) * exposure, dangerEdge);
    }
    else if (lockedEdge > 0.0)
    {
        total = lerp(total, float3(35, 145, 255) * exposure, lockedEdge);
    }
    else if (selectedEdge > 0.0)
    {
        total = lerp(total, float3(150, 100, 0) * exposure, selectedEdge);
    }
    else if (hoveredEdge > 0.0)
    {
        total = lerp(total, float3(80, 220, 120) * exposure, hoveredEdge);
    }

    sceneColor[pixel] = float4(total, 1.0);
}
