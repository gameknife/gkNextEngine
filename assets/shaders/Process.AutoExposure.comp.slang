import Shader.Bindless;

struct PushConsts
{
    uint _ViewHdr_SwapChainIndex;
    uint _ViewHdr_CustomData0;
    uint _ViewHdr_CustomData1;
    uint _ViewHdr_CustomData2;
    uint InputIndex;
    uint Width;
    uint Height;
    uint Reset;
    uint64_t StateAddress;
    float LuminanceScale;
    float DeltaSeconds;
    float TargetLuminance;
    float MinEV;
    float MaxEV;
    float SpeedToDark;
    float SpeedToLight;
    float CompensationEV;
};
[[vk::push_constant]] ConstantBuffer<PushConsts> params;

static const uint histogramBins = 128u;
static const uint meterSize = 64u;
static const float minLogLuminance = -16.0f;
static const float logLuminanceRange = 32.0f;
groupshared uint histogram[histogramBins];

[shader("compute")]
[numthreads(16, 16, 1)]
void main(uint3 threadId : SV_GroupThreadID, uint lane : SV_GroupIndex)
{
    if (lane < histogramBins)
    {
        histogram[lane] = 0u;
    }
    GroupMemoryBarrierWithGroupSync();

    // A fixed 64x64 meter bounds the cost independently of render resolution.
    // Central weighting keeps a small bright window from dominating an interior.
    for (uint y = threadId.y; y < meterSize; y += 16u)
    {
        for (uint x = threadId.x; x < meterSize; x += 16u)
        {
            const float2 uv = (float2(x, y) + 0.5f) / float(meterSize);
            const uint2 pixel = min(uint2(uv * float2(params.Width, params.Height)),
                                    uint2(params.Width - 1u, params.Height - 1u));
            const float3 color = Bindless.GetStorageTexture<float4>(int(params.InputIndex))[pixel].rgb;
            const float luminance = dot(max(color, float3(0.0f)), float3(0.2126f, 0.7152f, 0.0722f)) *
                params.LuminanceScale;
            // Truly black or invalid samples carry no light to adapt to.
            if (!isfinite(luminance) || luminance <= 1.0e-6f)
            {
                continue;
            }
            const float2 center = uv * 2.0f - 1.0f;
            const float centrality = saturate(1.0f - dot(center, center) * 0.5f);
            const uint weight = 1u + uint(3.0f * centrality * centrality);
            const uint bin = min(uint(saturate(
                (log2(luminance) - minLogLuminance) / logLuminanceRange) * float(histogramBins)),
                histogramBins - 1u);
            InterlockedAdd(histogram[bin], weight);
        }
    }
    GroupMemoryBarrierWithGroupSync();
    if (lane != 0u)
    {
        return;
    }

    float4* state = (float4*)params.StateAddress;
    const float previousEV = params.Reset != 0u ? 0.0f : state[0].x;
    uint total = 0u;
    for (uint bin = 0u; bin < histogramBins; ++bin)
    {
        total += histogram[bin];
    }
    if (total == 0u)
    {
        state[0] = float4(previousEV, previousEV, 0.0f, 0.0f);
        return;
    }

    // Trim both tails before averaging in log space: lamps, sun and dark edge
    // pixels should not make exposure pump as they enter or leave the frame.
    const float lowCount = float(total) * 0.1f;
    const float highCount = float(total) * 0.9f;
    float cumulative = 0.0f;
    float logSum = 0.0f;
    float weightSum = 0.0f;
    for (uint bin = 0u; bin < histogramBins; ++bin)
    {
        const float next = cumulative + float(histogram[bin]);
        const float weight = max(min(next, highCount) - max(cumulative, lowCount), 0.0f);
        const float logLuminance = minLogLuminance +
            (float(bin) + 0.5f) / float(histogramBins) * logLuminanceRange;
        logSum += weight * logLuminance;
        weightSum += weight;
        cumulative = next;
    }
    const float meteredLuminance = exp2(logSum / max(weightSum, 1.0f));
    const float targetEV = clamp(log2(max(params.TargetLuminance, 1.0e-4f) / meteredLuminance) +
        params.CompensationEV, params.MinEV, params.MaxEV);
    const float speed = targetEV > previousEV ? params.SpeedToDark : params.SpeedToLight;
    const float blend = 1.0f - exp(-max(speed, 0.0f) * max(params.DeltaSeconds, 0.0f));
    const float adaptedEV = params.Reset != 0u ? targetEV : lerp(previousEV, targetEV, blend);
    // x = adapted EV, y = target EV, z = measured normalized luminance.
    state[0] = float4(clamp(adaptedEV, params.MinEV, params.MaxEV), targetEV, meteredLuminance, 0.0f);
}
