import Shader.Bindless;
import Shader.Types;
import Shader.GpuScene;

import Shader.SoftMeshShaderCull; // ComputeRayTracingScreenExtent / SelectLod

// Picks the LOD level every TLAS instance traces this frame, right before the TLAS is rebuilt from
// what this pass writes. One thread per instance; instance i is NodeProxy i.
//
// The level is the one the main view's cull pass resolves for the same proxy -- same camera
// matrix, bounds, viewport, threshold and SelectLod -- so without a bias a primary hit
// reconstructed from the visibility buffer lies exactly on a triangle the TLAS holds. The bias
// (r.lod.rayTracingBias) then coarsens the traced proxy on top of that choice, and
// FHardwareRayTracer.ResolvePrimaryOrigin reconciles primary origins with the coarser surface.

// Mirrors VkAccelerationStructureInstanceKHR (64 bytes).
struct FTlasInstance
{
    float4 transform[3];
    uint customIndexAndMask;
    uint sbtOffsetAndFlags;
    uint64_t blasAddress;
};

// CustomData2 layout, written by VulkanBaseRenderer::UpdateAccelerationStructuresTop.
static const uint kLodBiasMask = 0xFFu;    // bits 0..7: extra levels for the traced proxy
static const uint kLodEnableBit = 1u << 8; // bit 8: follow the main view's level (else LOD0)

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    let scene = Bindless.GetGpuscene();
    const uint instanceIndex = DTid.x;
    if (instanceIndex >= scene.CustomData1 || instanceIndex >= uint(MAX_RENDER_PROXIES))
    {
        return;
    }

    const FRayTracingLodResources resources = ((FRayTracingLodResources*)scene.ReservedAddress0)[0];
    FTlasInstance instance = ((FTlasInstance*)resources.TemplateInstances)[instanceIndex];
    const NodeProxy node = scene.Nodes[instanceIndex];
    const uint modelIndex = node.modelId / uint(MODEL_SECTION_STRIDE);

    uint lod = 0u;
    // The CPU leaves a slot whose model went stale inactive; it has no BLAS table entry either.
    if (modelIndex < resources.ModelCount)
    {
        const ModelData model = scene.Offsets[node.modelId];
        const uint lodCount = max(model.lodCount, 1u);
        if ((scene.CustomData2 & kLodEnableBit) != 0u)
        {
            const UniformBufferObject camera = scene.Camera[0];
            const float4x4 mvp = mul(camera.PrevViewProjectionUnJit, node.worldTS);
            lod = SelectLod(
                ComputeRayTracingScreenExtent(model.localAabbMin.xyz, model.localAabbMax.xyz, mvp,
                                              float2(camera.ViewportRect.zw)),
                lodCount, camera.LodBaseThreshold);
        }
        lod = min(lod + (scene.CustomData2 & kLodBiasMask), lodCount - 1u);

        // Slots past a model's last BLAS repeat that BLAS's address (a skinned model, say, only
        // ever owns LOD0). Settle on the level the BLAS really is, so the per-proxy level always
        // names the index range the instance's triangles were built from.
        uint64_t* levels = (uint64_t*)resources.BlasAddresses + modelIndex * uint(MAX_MODEL_LOD_COUNT);
        while (lod > 0u && levels[lod - 1u] == levels[lod])
        {
            --lod;
        }
        instance.blasAddress = levels[lod];
    }

    ((FTlasInstance*)resources.Instances)[instanceIndex] = instance;
    scene.RayTracingLods[instanceIndex] = lod;
}
