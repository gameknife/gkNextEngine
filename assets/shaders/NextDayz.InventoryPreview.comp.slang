import Shader.Bindless;
import Shader.GpuScene;

struct ResolveParams
{
    uint _ViewHdr_SwapChainIndex;
    uint _ViewHdr_CustomData0;
    uint _ViewHdr_CustomData1;
    uint _ViewHdr_CustomData2;
    int2 SourceOffset;
    uint2 OutputExtent;
    uint SampleScale;
};
[[vk::push_constant]] ConstantBuffer<ResolveParams> params;

// Geometry supplies coverage, preserving black clothing and gaps between limbs.
// Keep RGB unassociated with alpha: ImGui applies coverage when compositing.
static const uint objectIdFlagValid = 0x80000000u;

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (any(dispatchThreadId.xy >= params.OutputExtent)) return;

    const int2 pixel = int2(dispatchThreadId.xy);
    let color = Bindless.GetViewStorageTexture<float4>(Bindless.RT_SCENE_COLOR);
    let objectIds = Bindless.GetViewStorageTexture<uint>(Bindless.RT_OBJECTID_0);
    const int2 sourceBase = params.SourceOffset + pixel * int(params.SampleScale);
    float3 colorSum = float3(0.0f);
    uint covered = 0;
    for (uint y = 0; y < params.SampleScale; ++y)
    {
        for (uint x = 0; x < params.SampleScale; ++x)
        {
            const int2 source = sourceBase + int2(x, y);
            if ((objectIds[source] & objectIdFlagValid) != 0u)
            {
                colorSum += color[source].rgb;
                ++covered;
            }
        }
    }
    Bindless.GetViewStorageTexture<float4>(Bindless.RT_SINGLE_DIFFUSE)[pixel] = covered > 0u
        ? float4(colorSum / float(covered), float(covered) / float(params.SampleScale * params.SampleScale))
        : float4(0.0f);
}
