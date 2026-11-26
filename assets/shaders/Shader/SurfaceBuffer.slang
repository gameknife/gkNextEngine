import Shader.Bindless;
module SurfaceBuffer;

// The Primary Surface is the single dense, already-resolved description of what the camera sees at
// a pixel: geometry, material parameters and motion, produced once by Core.SurfaceBuild and read by
// every downstream consumer. It deliberately reuses the existing RT bank rather than adding a
// parallel resource set, so this module is a *codec*, not a storage owner: it is the only place
// that knows which RT slot carries which semantic, what the background sentinel is, and how the
// BSDF word is packed. Business shaders must go through it and never hand-assemble slot
// combinations, exactly as they must go through Visibility.Load for the visibility buffer.
//
// The C++ mirror of this contract lives in Rendering/PipelineCommon/SurfaceBufferLayout.hpp.
namespace Surface
{
    // --- Contract constants -------------------------------------------------------------------

    // Depth written where the primary ray hit nothing. Deliberately outside [0,1] so any consumer
    // that only asks "is this a surface?" gets the right answer without a second buffer.
    public static const float kBackgroundDepth = -1.0f;

    // RT_BSDF_DATA.x for background / degenerate samples. A resolved material id is a real index
    // into GPUScene materials, so it can never collide with this.
    public static const uint kInvalidMaterialId = 0xFFFFFFFFu;

    // Feature flags, packed into the high 16 bits of RT_BSDF_DATA.y. Material *identity* stays in
    // the material id; these describe the shading *strategy*, which is what a scheduler buckets on.
    // Keep this set minimal - it is not a place to mirror the material system.
    public static const uint kFlagEmissive = 1u << 0u;
    public static const uint kFlagMetallic = 1u << 1u;
    public static const uint kFlagDielectric = 1u << 2u;
    public static const uint kFlagHasMra = 1u << 3u;
    public static const uint kFlagHasNormalMap = 1u << 4u;

    // GPUScene.CustomData2 carries a single pass-local bit, the checkerboard flag (bit 0). Bits 1-3
    // are retired: they selected the surface path, the tile scheduler and GTAO-in-Core back when a
    // renderer could run without them; all three are now unconditional on every surface renderer.

    // GPUScene.CustomData1 bits for Core.SurfaceBuild: which optional planes this build writes.
    // Specular albedo is material data that only the tracing renderers consume, so a light
    // consumer (SoftwareModernNoAmbient) does not pay for it.
    public static const uint kBuildWriteSpecularAlbedo = 1u << 0u;
    // RT_HISTORY_AGE saturates here. Background pixels are written at the cap: the sky has no
    // stochastic lighting to reconverge.
    public static const uint kHistoryAgeMax = 255u;

    // RT_HISTORY_AGE_PREV value meaning "no history". The host clears the previous-age plane to it
    // whenever the view's temporal history is invalidated (camera cut, resize, renderer or scene
    // change, reset), so every surface restarts at age 0 exactly when the upscaler and ReSTIR drop
    // their history. Must equal kSurfaceHistoryAgeInvalid in SurfaceBufferLayout.hpp.
    public static const uint kHistoryAgeInvalid = 0xFFFFu;

    // Instance identity bits of a Common.EncodeObjectId payload (valid flag + instance index; the
    // selection/hover flags are presentation state and must not reset history). Must equal
    // OBJECT_ID_FLAG_VALID | OBJECT_ID_INSTANCE_MASK in common/Interfaces.slang.
    static const uint kObjectIdIdentityMask = 0x80000000u | 0x07FFFFFFu;

    // --- Payloads -----------------------------------------------------------------------------

    // Everything Build resolves. Consumers rarely want all of it; the narrower loads below exist so
    // a screen-space march does not read eight planes to answer a depth question.
    public struct FPrimarySurface
    {
        public float Depth;           // NDC depth (jittered ViewProjection), kBackgroundDepth = miss
        public float3 Normal;         // world shading normal, normal-mapped
        public float Roughness;       // perceptual roughness, MRA already applied
        public float4 Albedo;         // base color, diffuse texture already multiplied in
        public float4 SpecularAlbedo; // specular albedo (base color for metals)
        public uint ObjectId;         // Common.EncodeObjectId payload
        public float2 Motion;         // render-pixel units
        public uint MaterialId;       // resolved GPUScene material index, kInvalidMaterialId = none
        public float Metalness;       // effective metalness, MRA and material-model override applied
        public uint Flags;            // kFlag* bitset
    };

    // Geometry-only view, for screen-space marching (occlusion, contact shadows, SS tracing).
    public struct FSurfaceGeometry
    {
        public float Depth;
        public float3 Normal;
        public float Roughness;
    };

    // Everything a shading kernel needs; skips object id, motion and specular albedo.
    public struct FSurfaceShading
    {
        public float Depth;
        public float3 Normal;
        public float Roughness;
        public float4 Albedo;
        public uint MaterialId;
        public float Metalness;
        public uint Flags;
    };

    // --- BSDF word packing --------------------------------------------------------------------

    public uint PackBsdfWord(float metalness, uint flags)
    {
        return (uint(f32tof16(saturate(metalness))) & 0xFFFFu) | ((flags & 0xFFFFu) << 16u);
    }

    public float UnpackMetalness(uint bsdfWord)
    {
        return f16tof32(bsdfWord & 0xFFFFu);
    }

    public uint UnpackFlags(uint bsdfWord)
    {
        return (bsdfWord >> 16u) & 0xFFFFu;
    }

    public bool HasFlag(uint flags, uint flag)
    {
        return (flags & flag) != 0u;
    }

    // --- Validity and reconstruction ----------------------------------------------------------

    public bool IsValid(float depth)
    {
        return depth >= 0.0f && depth <= 1.0f;
    }

    // Camera-space position of the sample. Build stores depth through the *jittered* view
    // projection, so reconstruction must invert the jittered projection: pass Camera.ProjectionInverse
    // (not the UnJit variant) or the reconstructed point drifts by the jitter offset.
    public float3 ReconstructViewPosition(
        float4x4 projectionInverse, int2 pixel, uint2 size, float depth)
    {
        const float2 ndc = ((float2(pixel) + 0.5f) / float2(size)) * 2.0f - 1.0f;
        const float4 view = mul(projectionInverse, float4(ndc, depth, 1.0f));
        const float safeW = abs(view.w) > 1e-6f ? view.w : (view.w >= 0.0f ? 1e-6f : -1e-6f);
        return view.xyz / safeW;
    }

    public float3 ReconstructPosition(
        float4x4 projectionInverse, float4x4 modelViewInverse, int2 pixel, uint2 size, float depth)
    {
        const float3 view = ReconstructViewPosition(projectionInverse, pixel, size, depth);
        return mul(modelViewInverse, float4(view, 1.0f)).xyz;
    }

    // --- Physical access ----------------------------------------------------------------------
    // The only code in the engine allowed to name a surface RT slot.

    RWTexture2D<float> DepthPlane(uint viewBase)
    {
        return Bindless.GetStorageTexture<float>(
            Bindless.ViewRT(viewBase, Bindless.RT_PREV_DEPTHBUFFER));
    }

    RWTexture2D<float4> NormalPlane(uint viewBase)
    {
        return Bindless.GetStorageTexture<float4>(Bindless.ViewRT(viewBase, Bindless.RT_NORMAL));
    }

    RWTexture2D<float4> AlbedoPlane(uint viewBase)
    {
        return Bindless.GetStorageTexture<float4>(Bindless.ViewRT(viewBase, Bindless.RT_ALBEDO));
    }

    RWTexture2D<float4> SpecularAlbedoPlane(uint viewBase)
    {
        return Bindless.GetStorageTexture<float4>(
            Bindless.ViewRT(viewBase, Bindless.RT_SPECULAR_ALBEDO));
    }

    RWTexture2D<uint> ObjectIdPlane(uint viewBase)
    {
        return Bindless.GetStorageTexture<uint>(Bindless.ViewRT(viewBase, Bindless.RT_OBJECTID_0));
    }

    RWTexture2D<float4> MotionPlane(uint viewBase)
    {
        return Bindless.GetStorageTexture<float4>(
            Bindless.ViewRT(viewBase, Bindless.RT_MOTIONVECTOR));
    }

    RWTexture2D<uint2> BsdfPlane(uint viewBase)
    {
        return Bindless.GetStorageTexture<uint2>(Bindless.ViewRT(viewBase, Bindless.RT_BSDF_DATA));
    }

    RWTexture2D<uint> HistoryAgePlane(uint viewBase)
    {
        return Bindless.GetStorageTexture<uint>(Bindless.ViewRT(viewBase, Bindless.RT_HISTORY_AGE));
    }

    RWTexture2D<uint> HistoryAgePrevPlane(uint viewBase)
    {
        return Bindless.GetStorageTexture<uint>(
            Bindless.ViewRT(viewBase, Bindless.RT_HISTORY_AGE_PREV));
    }

    RWTexture2D<uint> ObjectIdPrevPlane(uint viewBase)
    {
        return Bindless.GetStorageTexture<uint>(Bindless.ViewRT(viewBase, Bindless.RT_OBJECTID_1));
    }

    RWTexture2D<float> DepthHistoryPrevPlane(uint viewBase)
    {
        return Bindless.GetStorageTexture<float>(
            Bindless.ViewRT(viewBase, Bindless.RT_DEPTH_HISTORY_PREV));
    }

    public uint2 Dimensions(uint viewBase)
    {
        return Bindless.GetStorageTextureDimensions<float>(
            Bindless.ViewRT(viewBase, Bindless.RT_PREV_DEPTHBUFFER));
    }

    // --- Store / Load -------------------------------------------------------------------------

    public void Store(uint viewBase, int2 pixel, FPrimarySurface s, bool writeSpecularAlbedo)
    {
        DepthPlane(viewBase)[pixel] = s.Depth;
        NormalPlane(viewBase)[pixel] = float4(s.Normal, s.Roughness);
        AlbedoPlane(viewBase)[pixel] = s.Albedo;
        ObjectIdPlane(viewBase)[pixel] = s.ObjectId;
        MotionPlane(viewBase)[pixel] = float4(s.Motion, 0.0f, 0.0f);
        BsdfPlane(viewBase)[pixel] = uint2(s.MaterialId, PackBsdfWord(s.Metalness, s.Flags));
        if (writeSpecularAlbedo)
        {
            SpecularAlbedoPlane(viewBase)[pixel] = s.SpecularAlbedo;
        }
    }

    public FPrimarySurface Load(uint viewBase, int2 pixel)
    {
        const float4 normal = NormalPlane(viewBase)[pixel];
        const uint2 bsdf = BsdfPlane(viewBase)[pixel];
        const float4 motion = MotionPlane(viewBase)[pixel];

        FPrimarySurface s;
        s.Depth = DepthPlane(viewBase)[pixel];
        s.Normal = normal.xyz;
        s.Roughness = normal.w;
        s.Albedo = AlbedoPlane(viewBase)[pixel];
        s.SpecularAlbedo = SpecularAlbedoPlane(viewBase)[pixel];
        s.ObjectId = ObjectIdPlane(viewBase)[pixel];
        s.Motion = motion.xy;
        s.MaterialId = bsdf.x;
        s.Metalness = UnpackMetalness(bsdf.y);
        s.Flags = UnpackFlags(bsdf.y);
        return s;
    }

    public FSurfaceShading LoadShading(uint viewBase, int2 pixel)
    {
        const float4 normal = NormalPlane(viewBase)[pixel];
        const uint2 bsdf = BsdfPlane(viewBase)[pixel];

        FSurfaceShading s;
        s.Depth = DepthPlane(viewBase)[pixel];
        s.Normal = normal.xyz;
        s.Roughness = normal.w;
        s.Albedo = AlbedoPlane(viewBase)[pixel];
        s.MaterialId = bsdf.x;
        s.Metalness = UnpackMetalness(bsdf.y);
        s.Flags = UnpackFlags(bsdf.y);
        return s;
    }

    public FSurfaceGeometry LoadGeometry(uint viewBase, int2 pixel)
    {
        const float4 normal = NormalPlane(viewBase)[pixel];

        FSurfaceGeometry s;
        s.Depth = DepthPlane(viewBase)[pixel];
        s.Normal = normal.xyz;
        s.Roughness = normal.w;
        return s;
    }

    public float LoadDepth(uint viewBase, int2 pixel)
    {
        return DepthPlane(viewBase)[pixel];
    }

    public uint LoadObjectId(uint viewBase, int2 pixel)
    {
        return ObjectIdPlane(viewBase)[pixel];
    }

    // The part of an object id that names the surface (valid flag + instance); selection and
    // hover flags are presentation state.
    public uint ObjectIdentity(uint objectId)
    {
        return objectId & kObjectIdIdentityMask;
    }

    public uint LoadMaterialId(uint viewBase, int2 pixel)
    {
        return BsdfPlane(viewBase)[pixel].x;
    }

    // Raw material word, for consumers that only need identity and strategy - tile classification
    // buckets on exactly this and must not pay for the geometry planes.
    public uint2 LoadBsdf(uint viewBase, int2 pixel)
    {
        return BsdfPlane(viewBase)[pixel];
    }

    public float4 LoadSpecularAlbedo(uint viewBase, int2 pixel)
    {
        return SpecularAlbedoPlane(viewBase)[pixel];
    }

    public float4 LoadAlbedo(uint viewBase, int2 pixel)
    {
        return AlbedoPlane(viewBase)[pixel];
    }

    // --- History age --------------------------------------------------------------------------
    // RT_HISTORY_AGE counts the frames a pixel's reprojected history has stayed valid: 0 right
    // after a disocclusion, +1 per frame while the surface under it keeps reprojecting onto
    // itself, saturating at kHistoryAgeMax. It is geometry-only bookkeeping (like RT_OBJECTID_1),
    // never color. Whoever writes the surface (Core.SurfaceBuild, or the PathTracing primary hit,
    // which resolves its own surface) reads the previous frame's age, object id and depth and writes
    // the current age to a separate plane; the end-of-view history copy then moves age, object id
    // and depth into RT_HISTORY_AGE_PREV / RT_OBJECTID_1 / RT_DEPTH_HISTORY_PREV.

    // Linear view distance of an NDC depth. Only the depth row of the projection matters, and the
    // jitter lives in the x/y rows, so the current frame's inverse also linearizes last frame's
    // depth for any camera whose near/far did not change between the two frames.
    float LinearViewDepth(float4x4 projectionInverse, float ndcDepth)
    {
        const float4 view = mul(projectionInverse, float4(0.0f, 0.0f, ndcDepth, 1.0f));
        return abs(view.z / (abs(view.w) > 1e-8f ? view.w : 1e-8f));
    }

    // Reprojects the surface point under this pixel into last frame's raster and inherits its age.
    //
    // worldPosition is the primary hit (traced through the jittered pixel centre) and prevTransform
    // the instance's NodeProxy.combinedPrevTS. Projecting through Camera.PrevViewProjection - last
    // frame's *jittered* matrix - lands on exactly the sub-pixel position last frame's planes were
    // rasterized at, so a static view no longer reads across a silhouette every time the jitter
    // phase changes (the unjittered motion vector is off by the jitter delta, up to a pixel).
    //
    // The object id test alone cannot see an instance occluding itself (terrain, a building, a
    // single-mesh character), so every tap's previous depth must also match the depth this point
    // had last frame. The tolerance grows with the depth slope across a pixel at grazing angles;
    // cosView is |dot(normal, viewDir)|.
    public uint ReprojectHistoryAge(uint viewBase, int2 pixel, uint2 size, uint objectId,
                                    float4x4 prevViewProjection, float4x4 projectionInverse,
                                    float4x4 prevTransform, float3 worldPosition, float cosView)
    {
        const float4 prevWorld = mul(prevTransform, float4(worldPosition, 1.0f));
        const float4 prevClip = mul(prevViewProjection, prevWorld);
        if (prevClip.w <= 1e-6f)
        {
            return 0u;
        }
        const float3 prevNdc = prevClip.xyz / prevClip.w;
        const float expectedLinearDepth = LinearViewDepth(projectionInverse, prevNdc.z);

        // Depth slope: one pixel spans pixelAngle * depth at normal incidence, divided by cosView
        // at an angle. The taps sit within ~1.5 px of the reprojected point.
        const float4 v0 = mul(projectionInverse, float4(0.0f, 0.0f, prevNdc.z, 1.0f));
        const float4 v1 = mul(projectionInverse, float4(0.0f, 2.0f / float(size.y), prevNdc.z, 1.0f));
        const float pixelAngle = abs(v1.y / v1.z - v0.y / v0.z);
        const float clampedCos = max(cosView, 0.03f);
        const float tanView = sqrt(saturate(1.0f - clampedCos * clampedCos)) / clampedCos;
        const float depthTolerance =
            expectedLinearDepth * (0.03f + 1.5f * pixelAngle * tanView) + 0.02f;

        // Bilinear footprint of the previous-frame position, in texel-centre space. Only taps that
        // actually carry weight are tested, so a static camera does not read across silhouettes.
        const float2 prevPosition = (prevNdc.xy * 0.5f + 0.5f) * float2(size) - 0.5f;
        const int2 base = int2(floor(prevPosition));
        const float2 f = prevPosition - float2(base);
        const float4 weights = float4(
            (1.0f - f.x) * (1.0f - f.y), f.x * (1.0f - f.y), (1.0f - f.x) * f.y, f.x * f.y);

        const uint identity = ObjectIdentity(objectId);
        float matchedWeight = 0.0f;
        float nearestWeight = 0.0f;
        uint nearestAge = kHistoryAgeInvalid;
        [unroll]
        for (uint i = 0u; i < 4u; ++i)
        {
            if (weights[i] <= 1.0e-3f)
            {
                continue;
            }
            const int2 tap = base + int2(int(i & 1u), int(i >> 1u));
            if (any(tap < int2(0, 0)) || any(tap >= int2(size)))
            {
                continue;
            }
            if (ObjectIdentity(ObjectIdPrevPlane(viewBase)[tap]) != identity)
            {
                continue;
            }
            const float tapDepth = DepthHistoryPrevPlane(viewBase)[tap];
            if (!IsValid(tapDepth) ||
                abs(LinearViewDepth(projectionInverse, tapDepth) - expectedLinearDepth) > depthTolerance)
            {
                continue;
            }
            matchedWeight += weights[i];
            // The age is inherited from the nearest matching tap, not the footprint minimum. A
            // minimum is a min-filter applied once per frame: under any sub-pixel motion the zero
            // ages at a silhouette erode inward by a pixel a frame, until a moving object or a
            // panning view reads as freshly disoccluded everywhere. Validity stays conservative
            // through the coverage test below.
            if (weights[i] > nearestWeight)
            {
                nearestWeight = weights[i];
                nearestAge = HistoryAgePrevPlane(viewBase)[tap];
            }
        }
        // Most of the footprint must have been this same surface last frame; anything less is a
        // disocclusion (or the surface came from off-screen). A cleared history matches nothing.
        return matchedWeight >= 0.5f && nearestAge < kHistoryAgeInvalid
            ? min(nearestAge + 1u, kHistoryAgeMax)
            : 0u;
    }

    public void StoreHistoryAge(uint viewBase, int2 pixel, uint age)
    {
        HistoryAgePlane(viewBase)[pixel] = age;
    }

    public uint LoadHistoryAge(uint viewBase, int2 pixel)
    {
        return HistoryAgePlane(viewBase)[pixel];
    }
}
