import Shader.Bindless;
import Shader.GpuScene;

// Only the inventory preview gets a cutout alpha. Coverage comes from geometry,
// so black clothing, weapon parts and gaps between limbs retain their silhouette.
static const uint objectIdFlagValid = 0x80000000u;

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint2 size = Bindless.GetViewStorageTextureDimensions<float4>(Bindless.RT_SCENE_COLOR);
    if (any(dispatchThreadId.xy >= size)) return;

    const int2 pixel = int2(dispatchThreadId.xy);
    let color = Bindless.GetViewStorageTexture<float4>(Bindless.RT_SCENE_COLOR);
    const uint objectId = Bindless.GetViewStorageTexture<uint>(Bindless.RT_OBJECTID_0)[pixel];
    color[pixel] = (objectId & objectIdFlagValid) != 0u
        ? float4(color[pixel].rgb, 1.0f)
        : float4(0.0f);
}
