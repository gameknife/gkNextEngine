import Shader.Bindless;
import Shader.Types;
import Shader.GpuScene;
import Shader.SurfaceBuffer;

// Disocclusion filter: an edge-stopping a-trous filter over RT_SINGLE_DIFFUSE that runs only where
// the temporal upscaler has no history yet.
//
// Realtime rendering hands the upscaler a raw 1-spp-class sample and lets its history do the
// converging, so a pixel the upscaler just lost history for (an object moved off it, the view
// panned onto it) shows raw noise for as many frames as the upscaler needs to reconverge. This pass
// trades that noise for spatial blur on exactly those pixels and nowhere else:
//
//   strength w = saturate(1 - age / FullFrames), age = RT_HISTORY_AGE
//
// w is 1 right after a disocclusion and reaches 0 FullFrames later. A pixel runs ceil(w * passes)
// of the passes (the kernel narrows as its history grows) and the result is blended in by w, so a
// pixel with w == 0 is never written and is bit-for-bit the sample Core Shading produced. A static
// view therefore costs only the age read per pixel.
//
// No history of any kind is read or kept here: the filter only ever mixes current-frame samples.
// The signal is diffuse lighting with albedo divided out (compose multiplies albedo back in), so
// texture detail is never blurred; specular is left alone.
//
// Edge stops: same instance, the tap lies on the centre's tangent plane, normals agree, and
// luminance differs by less than a few local noise deviations (SVGF-style: the variance is
// estimated spatially on pass 0, carried in the ping-pong alpha and shrunk pass by pass). The last
// one is what keeps a hard shadow edge in a freshly disoccluded region from smearing.
//
// Buffers. A = RT_SINGLE_DIFFUSE, T0/T1 = RT_DISOCCLUSION_PING/PONG. Pass i reads its active taps
// from the previous pass's output (A for pass 0) and writes T[i % 2]; the last pass writes A. A tap
// that is not active (w == 0, or not a filterable surface) is read from A, which holds its final
// value already. The last pass writes A only at active pixels and reads A only at inactive ones,
// so it never races with itself. That is also why the host runs at least two passes.
//
// GPUScene.CustomData1: bits 0-7 pass index, 8-15 pass count, 16-31 FullFrames.

static const float kKernel[3] = { 0.25f, 0.5f, 0.25f };

// Sharpness of the normal stop; normal-mapped detail deliberately stops the filter.
static const float kNormalPower = 32.0f;
// Tolerated slope of the tap out of the centre's tangent plane (planeDistance / lateral distance).
static const float kPlaneSlope = 0.1f;
// Luminance stop in units of the local noise standard deviation (SVGF uses 4).
static const float kLuminanceSigma = 4.0f;

float Luminance(float3 color)
{
    return dot(max(color, float3(0, 0, 0)), float3(0.2126f, 0.7152f, 0.0722f));
}

float Square(float value)
{
    return value * value;
}

struct FFilterPixel
{
    bool Active;
    float Strength;
    uint Identity;
    float3 Position;
    float3 Normal;
};

// Whether a pixel takes part in the filter, and how strongly. Must give the same answer for a pixel
// whether it is asked as a centre or as a tap: it decides which plane holds that pixel's value.
bool IsFilterTarget(uint viewBase, int2 pixel, uint fullFrames, out float strength)
{
    const uint age = Surface.LoadHistoryAge(viewBase, pixel);
    strength = saturate(1.0f - float(age) / float(max(fullFrames, 1u)));
    if (strength <= 0.0f || !Surface.IsValid(Surface.LoadDepth(viewBase, pixel)))
    {
        return false;
    }
    // Emitters store absolute radiance in the diffuse plane (with a white demodulation guide), and
    // degenerate / debug samples carry no material: neither is noisy lighting.
    const uint2 bsdf = Surface.LoadBsdf(viewBase, pixel);
    return bsdf.x != Surface.kInvalidMaterialId &&
        !Surface.HasFlag(Surface.UnpackFlags(bsdf.y), Surface.kFlagEmissive);
}

FFilterPixel LoadFilterPixel(uint viewBase, int2 pixel, uint2 size, uint fullFrames,
                             in UniformBufferObject camera)
{
    FFilterPixel result;
    result.Identity = 0u;
    result.Position = float3(0, 0, 0);
    result.Normal = float3(0, 0, 1);
    result.Active = IsFilterTarget(viewBase, pixel, fullFrames, result.Strength);
    if (!result.Active)
    {
        return result;
    }

    const Surface.FSurfaceGeometry geometry = Surface.LoadGeometry(viewBase, pixel);
    result.Identity = Surface.ObjectIdentity(Surface.LoadObjectId(viewBase, pixel));
    result.Position = Surface.ReconstructPosition(
        camera.ProjectionInverse, camera.ModelViewInverse, pixel, size, geometry.Depth);
    const float normalLength = length(geometry.Normal);
    result.Normal = normalLength > 1e-4f ? geometry.Normal / normalLength : float3(0, 0, 1);
    return result;
}

// Luminance variance of the raw sample over the 3x3 neighbours on the same instance: the noise
// level the luminance stop is scaled by. Only pass 0 calls it, while A still holds raw samples.
float EstimateLuminanceVariance(RWTexture2D<float4> samples, uint viewBase, int2 pixel, uint2 size,
                                uint identity)
{
    float sum = 0.0f;
    float sumSquares = 0.0f;
    float count = 0.0f;
    [unroll]
    for (int dy = -1; dy <= 1; ++dy)
    {
        [unroll]
        for (int dx = -1; dx <= 1; ++dx)
        {
            const int2 tap = pixel + int2(dx, dy);
            if (any(tap < int2(0, 0)) || any(tap >= int2(size)) ||
                Surface.ObjectIdentity(Surface.LoadObjectId(viewBase, tap)) != identity)
            {
                continue;
            }
            const float luminance = Luminance(samples[tap].rgb);
            sum += luminance;
            sumSquares += luminance * luminance;
            count += 1.0f;
        }
    }
    const float mean = sum / max(count, 1.0f);
    return max(sumSquares / max(count, 1.0f) - mean * mean, 0.0f);
}

uint PassesFor(float strength, uint passCount)
{
    return strength > 0.0f ? clamp(uint(ceil(strength * float(passCount))), 1u, passCount) : 0u;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const UniformBufferObject camera = Bindless.GetGpuscene().Camera[0];
    const uint viewBase = Bindless.GetGpuscene().CustomData0;
    const uint params = Bindless.GetGpuscene().CustomData1;
    const uint pass = params & 0xFFu;
    const uint passCount = (params >> 8u) & 0xFFu;
    const uint fullFrames = params >> 16u;

    const uint2 size = Surface.Dimensions(viewBase);
    const int2 pixel = int2(dispatchThreadId.xy);
    if (any(dispatchThreadId.xy >= size))
    {
        return;
    }

    const FFilterPixel center = LoadFilterPixel(viewBase, pixel, size, fullFrames, camera);
    if (!center.Active)
    {
        return;
    }

    let original = Bindless.GetViewStorageTexture<float4>(Bindless.RT_SINGLE_DIFFUSE);
    let ping = Bindless.GetViewStorageTexture<float4>(Bindless.RT_DISOCCLUSION_PING);
    let pong = Bindless.GetViewStorageTexture<float4>(Bindless.RT_DISOCCLUSION_PONG);
    const bool lastPass = pass + 1u >= passCount;

    // Previous pass output at an active pixel (rgb = lighting, a = luminance variance); pass 0
    // starts from the sample itself and estimates the variance from its 3x3 neighbourhood.
    float4 previous;
    if (pass == 0u)
    {
        previous = float4(original[pixel].rgb, EstimateLuminanceVariance(
            original, viewBase, pixel, size, center.Identity));
    }
    else
    {
        previous = (pass & 1u) == 1u ? ping[pixel] : pong[pixel];
    }

    float4 filtered = previous;
    if (pass < PassesFor(center.Strength, passCount))
    {
        const int step = 1 << int(pass);
        const float centerLuminance = Luminance(previous.rgb);
        // Luminance stop scaled by the local noise level (SVGF): noise-sized differences pass,
        // real lighting edges such as shadow boundaries stop the filter.
        const float luminanceScale = kLuminanceSigma * sqrt(max(previous.a, 0.0f)) + 1.0e-3f;
        float3 sum = previous.rgb * kKernel[1] * kKernel[1];
        float varianceSum = previous.a * Square(kKernel[1] * kKernel[1]);
        float weightSum = kKernel[1] * kKernel[1];
        [unroll]
        for (int dy = -1; dy <= 1; ++dy)
        {
            [unroll]
            for (int dx = -1; dx <= 1; ++dx)
            {
                if (dx == 0 && dy == 0)
                {
                    continue;
                }
                const float kernel = kKernel[dx + 1] * kKernel[dy + 1];
                const int2 tap = pixel + int2(dx, dy) * step;
                if (any(tap < int2(0, 0)) || any(tap >= int2(size)))
                {
                    continue;
                }
                const float tapDepth = Surface.LoadDepth(viewBase, tap);
                if (!Surface.IsValid(tapDepth) ||
                    Surface.ObjectIdentity(Surface.LoadObjectId(viewBase, tap)) != center.Identity)
                {
                    continue;
                }

                const float3 tapPosition = Surface.ReconstructPosition(
                    camera.ProjectionInverse, camera.ModelViewInverse, tap, size, tapDepth);
                const float3 offset = tapPosition - center.Position;
                const float planeDistance = abs(dot(center.Normal, offset));
                const float planeWeight =
                    exp(-planeDistance / (kPlaneSlope * length(offset) + 1.0e-4f));

                const float3 tapNormal = Surface.LoadGeometry(viewBase, tap).Normal;
                const float normalWeight = pow(
                    saturate(dot(center.Normal, tapNormal * rsqrt(max(dot(tapNormal, tapNormal), 1e-8f)))),
                    kNormalPower);

                const float geometryWeight = kernel * planeWeight * normalWeight;
                if (geometryWeight <= 1.0e-5f)
                {
                    continue;
                }

                // Only an active tap has a value in the ping-pong planes; anything else is final
                // in A, and its variance is taken to be the centre's.
                float tapStrength;
                float4 tapValue;
                if (pass == 0u || !IsFilterTarget(viewBase, tap, fullFrames, tapStrength))
                {
                    tapValue = float4(original[tap].rgb, previous.a);
                }
                else
                {
                    tapValue = (pass & 1u) == 1u ? ping[tap] : pong[tap];
                }
                const float luminanceWeight =
                    exp(-abs(Luminance(tapValue.rgb) - centerLuminance) / luminanceScale);
                const float weight = geometryWeight * luminanceWeight;
                sum += tapValue.rgb * weight;
                varianceSum += tapValue.a * weight * weight;
                weightSum += weight;
            }
        }
        // Variance of a weighted mean shrinks by the squared weights, which is what lets the
        // luminance stop tighten as the passes remove noise.
        filtered = float4(sum / weightSum, varianceSum / Square(weightSum));
    }

    if (lastPass)
    {
        const float4 sample = original[pixel];
        original[pixel] = float4(lerp(sample.rgb, filtered.rgb, center.Strength), sample.a);
    }
    else if ((pass & 1u) == 0u)
    {
        ping[pixel] = filtered;
    }
    else
    {
        pong[pixel] = filtered;
    }
}
