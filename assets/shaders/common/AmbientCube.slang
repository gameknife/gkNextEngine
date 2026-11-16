implementing PathTracing;

#include "PreProcessor.slang"

public static const int SHADOWMAP_SIZE = 4096;

public static const int CUBE_SIZE_XY = 192; // 256;
public static const int CUBE_SIZE_Z = 48;
public static const uint CUBE_CASCADE_MAX = 5;

float GetAmbientCubeUnit()
{
    return max(Bindless.GetGpuscene().Camera[0].AmbientCubeUnit, 0.0001f);
}

public uint GetAmbientCubeCascadeCount()
{
    uint count = uint(max(Bindless.GetGpuscene().Camera[0].AmbientCubeCascadeParams.x, 1.0f));
    return min(count, CUBE_CASCADE_MAX);
}

// Each cascade's grid comes straight from the UBO (FAmbientCascadeLayout on the CPU): cell (0,0,0)
// sits at AmbientCubeCascadeGrid[i].xyz and a cell is AmbientCubeCascadeGrid[i].w wide. Nothing
// here derives one cascade from another, so a cascade can sit anywhere on its lattice.
public float GetAmbientCubeUnitAtCascade(uint cascadeIndex)
{
    return max(Bindless.GetGpuscene().Camera[0].AmbientCubeCascadeGrid[cascadeIndex].w, 0.0001f);
}

public float3 GetAmbientCubeOffsetAtCascade(uint cascadeIndex)
{
    return Bindless.GetGpuscene().Camera[0].AmbientCubeCascadeGrid[cascadeIndex].xyz;
}

// Cascade + grid parameters for a world position in one pass: the finest cascade whose grid holds
// the point, or the coarsest one (returning false) when none does.
public bool GetAmbientCubeGridAtPosition(float3 worldPos, out uint cascadeIndex, out float unit, out float3 offset)
{
    uint cascadeCount = GetAmbientCubeCascadeCount();
    for (cascadeIndex = 0; cascadeIndex < cascadeCount; ++cascadeIndex)
    {
        float4 grid = Bindless.GetGpuscene().Camera[0].AmbientCubeCascadeGrid[cascadeIndex];
        unit = max(grid.w, 0.0001f);
        offset = grid.xyz;
        float3 voxelPos = (worldPos - offset) / unit;
        if (voxelPos.x >= 0 && voxelPos.y >= 0 && voxelPos.z >= 0 && voxelPos.x <= CUBE_SIZE_XY - 1 &&
            voxelPos.y <= CUBE_SIZE_Z - 1 && voxelPos.z <= CUBE_SIZE_XY - 1)
        {
            return true;
        }
    }

    cascadeIndex = cascadeCount - 1;
    unit = GetAmbientCubeUnitAtCascade(cascadeIndex);
    offset = GetAmbientCubeOffsetAtCascade(cascadeIndex);
    return false;
}

public uint GetAmbientCubeCascadeAtPosition(float3 worldPos)
{
    uint cascadeIndex;
    float unit;
    float3 offset;
    GetAmbientCubeGridAtPosition(worldPos, cascadeIndex, unit, offset);
    return cascadeIndex;
}

public float GetAmbientCubeUnitAtPosition(float3 worldPos)
{
    uint cascadeIndex;
    float unit;
    float3 offset;
    GetAmbientCubeGridAtPosition(worldPos, cascadeIndex, unit, offset);
    return unit;
}

public uint GetCascadeBaseOffset(uint cascadeIndex)
{
    return cascadeIndex * uint(CUBE_SIZE_XY * CUBE_SIZE_XY * CUBE_SIZE_Z);
}

public uint PackNibbles(uint4 low, uint4 high)
{
    return (low.x & 0xFu) |
           ((low.y & 0xFu) << 4) |
           ((low.z & 0xFu) << 8) |
           ((low.w & 0xFu) << 12) |
           ((high.x & 0xFu) << 16) |
           ((high.y & 0xFu) << 20) |
           ((high.z & 0xFu) << 24) |
           ((high.w & 0xFu) << 28);
}

public uint4 UnpackNibblesLow(uint packed)
{
    return uint4(
        packed & 0xFu,
        (packed >> 4) & 0xFu,
        (packed >> 8) & 0xFu,
        (packed >> 12) & 0xFu);
}

public uint4 UnpackNibblesHigh(uint packed)
{
    return uint4(
        (packed >> 16) & 0xFu,
        (packed >> 20) & 0xFu,
        (packed >> 24) & 0xFu,
        (packed >> 28) & 0xFu);
}

public static const float3 cubeVectors[6] = {
    float3(0, 1, 0),
    float3(0, -1, 0),
    float3(0, 0, 1),
    float3(0, 0, -1),
    float3(1, 0, 0),
    float3(-1, 0, 0),
};

public static const uint FACE_TRACING = 16;
public static const uint FACE_TRACING_FAST = 9;
public static const uint FACE_TRACING_SUPER = 4;

const static float2 grid2x2[4] = {
    float2(-0.5, -0.5), float2(0.5, -0.5),
    float2(-0.5, 0.5), float2(0.5, 0.5)
};

const static float2 grid3x3[9] = {
    float2(-0.667, -0.667), float2(0.0, -0.667), float2(0.667, -0.667),
    float2(-0.667, 0.0), float2(0.0, 0.0), float2(0.667, 0.0),
    float2(-0.667, 0.667), float2(0.0, 0.667), float2(0.667, 0.667)
};

const static float2 grid4x4[16] = {
    float2(-0.75, -0.75), float2(-0.25, -0.75), float2(0.25, -0.75), float2(0.75, -0.75),
    float2(-0.75, -0.25), float2(-0.25, -0.25), float2(0.25, -0.25), float2(0.75, -0.25),
    float2(-0.75, 0.25), float2(-0.25, 0.25), float2(0.25, 0.25), float2(0.75, 0.25),
    float2(-0.75, 0.75), float2(-0.25, 0.75), float2(0.25, 0.75), float2(0.75, 0.75)
};

const static float2 grid5x5[25] = {
    float2(-0.8, -0.8), float2(-0.4, -0.8), float2(0.0, -0.8), float2(0.4, -0.8), float2(0.8, -0.8),
    float2(-0.8, -0.4), float2(-0.4, -0.4), float2(0.0, -0.4), float2(0.4, -0.4), float2(0.8, -0.4),
    float2(-0.8, 0.0), float2(-0.4, 0.0), float2(0.0, 0.0), float2(0.4, 0.0), float2(0.8, 0.0),
    float2(-0.8, 0.4), float2(-0.4, 0.4), float2(0.0, 0.4), float2(0.4, 0.4), float2(0.8, 0.4),
    float2(-0.8, 0.8), float2(-0.4, 0.8), float2(0.0, 0.8), float2(0.4, 0.8), float2(0.8, 0.8)
};

uint PackColor(float4 source)
{
    // Cube color faces carry no alpha; RGB9E5 gives wider HDR range + better
    // dark-tone precision at the same 32 bits (see ConstFunc.slang packRGB9E5).
    return packRGB9E5(source.rgb);
}

float4 UnpackColor(uint packed)
{
    return float4(unpackRGB9E5(packed), 0.0f);
}

uint LerpPackedColor(uint c0, uint c1, float t)
{
    // Extract RGBA components
    float4 color0 = UnpackColor(c0);
    float4 color1 = UnpackColor(c1);
    // Pack back to uint32
    return PackColor(lerp(color0, color1, t));
}

uint LerpPackedColorAlt(uint c0, float4 c1, float t)
{
    // Extract RGBA components
    float4 color0 = UnpackColor(c0);
    float4 color1 = c1;
    // Pack back to uint32
    return PackColor(lerp(color0, color1, t));
}

float4 sampleAmbientCubeHL2_ID(AmbientCube cube, float3 normal)
{
    float4 color = float4(0.0, 0.0, 0.0, 0.0);
    float sum = 0.0;

    float wx = max(normal.x, 0.0f);
    float wnx = max(-normal.x, 0.0f);
    float wy = max(normal.y, 0.0f);
    float wny = max(-normal.y, 0.0f);
    float wz = max(normal.z, 0.0f);
    float wnz = max(-normal.z, 0.0f);

    sum = wx + wnx + wy + wny + wz + wnz;

    color += wx * UnpackColor(cube.PosX);
    color += wnx * UnpackColor(cube.NegX);
    color += wy * UnpackColor(cube.PosY);
    color += wny * UnpackColor(cube.NegY);
    color += wz * UnpackColor(cube.PosZ);
    color += wnz * UnpackColor(cube.NegZ);

    color *= (sum > 0.0) ? (1.0 / sum) : 1.0;
    return color;
}

float AmbientCubeSkyVisibility(AmbientCube cube, float3 normal)
{
    float wx = max(normal.x, 0.0);
    float wnx = max(-normal.x, 0.0);
    float wy = max(normal.y, 0.0);
    float wny = max(-normal.y, 0.0);
    float wz = max(normal.z, 0.0);
    float wnz = max(-normal.z, 0.0);
    float sum = wx + wnx + wy + wny + wz + wnz;

    uint4 unpacked0 = UnpackBytes(cube.SkyVisibilityPZNZPYNY);
    uint4 unpacked1 = UnpackBytes(cube.SkyVisibilityPXNXS0S1);

    float skyvis = 0.0;
    skyvis += float(unpacked0.x) * wz;
    skyvis += float(unpacked0.y) * wnz;
    skyvis += float(unpacked0.z) * wy;
    skyvis += float(unpacked0.w) * wny;
    skyvis += float(unpacked1.x) * wx;
    skyvis += float(unpacked1.y) * wnx;
    skyvis *= (sum > 0.0) ? (1.0 / sum) : 1.0;
    return skyvis / 255.0f;
}

float4 sampleAmbientCubeDirectBounce(AmbientCube cube, float3 normal)
{
    float4 color = UnpackColor(cube.SunDirect) + UnpackColor(cube.SkyEmissiveDirect);
    color.a = AmbientCubeSkyVisibility(cube, normal);
    return color;
}

float4 sampleAmbientCubeDirectSurface(AmbientCube cube, float3 normal)
{
    float4 color = UnpackColor(cube.SkyEmissiveDirect);
    color.a = AmbientCubeSkyVisibility(cube, normal);
    return color;
}


float4 sampleAmbientCubeHL2_Full(AmbientCube cube, float3 normal)
{
    float4 color = sampleAmbientCubeHL2_ID(cube, normal) + sampleAmbientCubeDirectSurface(cube, normal);
    color.a = AmbientCubeSkyVisibility(cube, normal);
    return color;
}

interface IAmbientCubeSampler
{
    static float4 Sample(AmbientCube cube, float3 normal);
}




public struct FullAmbientCubeSampler : IAmbientCubeSampler
{
    static float4 Sample(AmbientCube cube, float3 normal)
    {
        return sampleAmbientCubeHL2_Full(cube, normal);
    }
};

public struct FullAmbientCubeWNSunSampler : IAmbientCubeSampler
{
    static float4 Sample(AmbientCube cube, float3 normal)
    {
        return sampleAmbientCubeHL2_Full(cube, normal);
    }
};

public struct DIAmbientCubeSampler : IAmbientCubeSampler
{
    static float4 Sample(AmbientCube cube, float3 normal)
    {
        return sampleAmbientCubeDirectBounce(cube, normal);
    }
};

public struct LightCacheAmbientCubeSampler : IAmbientCubeSampler
{
    static float4 Sample(AmbientCube cube, float3 normal)
    {
        return sampleAmbientCubeDirectSurface(cube, normal);
    }
};

public struct IDAmbientCubeSampler : IAmbientCubeSampler
{
    static float4 Sample(AmbientCube cube, float3 normal)
    {
        return sampleAmbientCubeHL2_ID(cube, normal);
    }
};

static const float kTraceHistoryLength = 32.0f;

// ---------- Phase 3 sparse brick indirection ----------
// The cube grid is tiled into 8^3 bricks; the per-cascade brick table maps a brick to a pool slot
// (or INVALID when unallocated). probePos below is a cascade-local grid coordinate in [0,CUBE_SIZE).

public AmbientCube ZeroAmbientCube()
{
    AmbientCube c;
    c.PosZ = 0; c.NegZ = 0; c.PosY = 0; c.NegY = 0; c.PosX = 0; c.NegX = 0;
    c.SunDirect = 0; c.SkyEmissiveDirect = 0;
    c.SkyVisibilityPZNZPYNY = 0; c.SkyVisibilityPXNXS0S1 = 0;
    return c;
}

// Brick coordinates are only ever formed from in-range probe positions, so the divide and modulo
// are done in unsigned: signed / 8 and % 8 make the compiler emit the sign-correction dance
// instead of a shift and a mask, and this runs eight times per probe interpolation.
static const uint kAmbientBrickEdgeShift = 3; // GPU_SCENE_AMBIENT_BRICK_EDGE == 8
static const uint kAmbientBrickEdgeMask = uint(GPU_SCENE_AMBIENT_BRICK_EDGE) - 1u;

// Everything needed to address one cascade's storage, resolved once per cascade so the DDA loop
// and the gather corners do not reload it from the camera UBO and the resource table every time.
public struct FAmbientCascadeAddress
{
    public uint cascadeIndex;
    public uint voxelBase;      // first VoxelData element of the cascade
    public uint brickTableBase; // first brick-table entry of the cascade
    public uint poolBrickBase;  // first cube-pool brick of the cascade
    public uint3 wrap;          // ring offset of the brick storage
};

public FAmbientCascadeAddress GetAmbientCascadeAddress(uint cascadeIndex)
{
    FAmbientCascadeAddress address;
    address.cascadeIndex = cascadeIndex;
    address.voxelBase = GetCascadeBaseOffset(cascadeIndex);
    address.brickTableBase = cascadeIndex * uint(GPU_SCENE_AMBIENT_BRICKS_PER_CASCADE);
    address.poolBrickBase = cascadeIndex * Bindless.GetGpuscene().AmbientPoolBricksPerCascade;
    address.wrap = uint3(Bindless.GetGpuscene().Camera[0].AmbientCubeCascadeWrap[cascadeIndex].xyz);
    return address;
}

// Ring addressing: window brick b of a cascade is stored at (b + wrap) mod BRICKS_*. This and
// GetVoxelAddress() in CPUAccelerationStructure.Internal.hpp are the only places the wrap applies;
// the brick table, voxel storage and cube-pool lookups all go through here.
uint AmbientBrickLinear(FAmbientCascadeAddress address, int3 probePos)
{
    uint3 brick = (uint3(probePos) >> kAmbientBrickEdgeShift) + address.wrap;
    const uint3 dims = uint3(GPU_SCENE_AMBIENT_BRICKS_X, GPU_SCENE_AMBIENT_BRICKS_Y, GPU_SCENE_AMBIENT_BRICKS_Z);
    brick.x -= brick.x >= dims.x ? dims.x : 0u;
    brick.y -= brick.y >= dims.y ? dims.y : 0u;
    brick.z -= brick.z >= dims.z ? dims.z : 0u;
    return brick.y * uint(GPU_SCENE_AMBIENT_BRICKS_X * GPU_SCENE_AMBIENT_BRICKS_Z)
         + brick.z * uint(GPU_SCENE_AMBIENT_BRICKS_X) + brick.x;
}

uint AmbientBrickSlot(FAmbientCascadeAddress address, int3 probePos)
{
    return Bindless.GetGpuscene().AmbientBrickTable[address.brickTableBase + AmbientBrickLinear(address, probePos)];
}

uint AmbientBrickLocalOffset(int3 probePos)
{
    uint lx = uint(probePos.x) & kAmbientBrickEdgeMask;
    uint ly = uint(probePos.y) & kAmbientBrickEdgeMask;
    uint lz = uint(probePos.z) & kAmbientBrickEdgeMask;
    return ly * uint(GPU_SCENE_AMBIENT_BRICK_EDGE * GPU_SCENE_AMBIENT_BRICK_EDGE)
         + lz * uint(GPU_SCENE_AMBIENT_BRICK_EDGE) + lx;
}

// Element index of a probe inside one cascade's dense VoxelData array. The array is brick-swizzled
// rather than laid out as y * W * W + z * W + x: the DDA reads it one cell at a time, and linearly
// a single Y step landed ~2 MB away, so nearly every step missed cache. Tiled into 8^3 blocks an
// entire neighbourhood is one 4 KB run. Must match GetVoxelAddress() in
// Engine/Assets/Acceleration/CPUAccelerationStructure.Internal.hpp, which writes the array.
public uint VoxelStorageIndex(FAmbientCascadeAddress address, int3 probePos)
{
    return AmbientBrickLinear(address, probePos) * uint(GPU_SCENE_AMBIENT_BRICK_VOLUME)
         + AmbientBrickLocalOffset(probePos);
}

// Bake passes a probe has accumulated (AdvanceAmbientCubeSampleCount, byte 2 of
// SkyVisibilityPXNXS0S1). A slot that changed owner is zeroed, so a fresh probe reads 0.
public uint AmbientCubeSampleCount(AmbientCube cube)
{
    return (cube.SkyVisibilityPXNXS0S1 >> 16u) & 0xFFu;
}

// A probe takes part in interpolation only once it has this many passes behind it. Until then the
// gather skips it, and a cascade left with no weight falls through to the next coarser one, so a
// bake that has not caught up shows coarser lighting rather than black or noisy probes.
public uint GetAmbientCubePublishPasses()
{
    return uint(max(Bindless.GetGpuscene().Camera[0].AmbientCubeCascadeParams.z, 0.0f));
}

public bool IsAmbientCubePublished(AmbientCube cube)
{
    return AmbientCubeSampleCount(cube) >= GetAmbientCubePublishPasses();
}

// r.ambientCube.debugView. 1 colours the surface by the cascade that lights it; 2 by what the
// gather finds there: outside every cascade (magenta), no cube brick (red), probes not yet published
// (yellow), published (blue). Both modes paint magenta outside every cascade window.
public bool TryAmbientCubeDebugView(float3 worldPos, out float4 color)
{
    uint debugMode = uint(Bindless.GetGpuscene().Camera[0].AmbientCubeCascadeParams.w);
    if (debugMode == 0u)
    {
        color = float4(0.0f);
        return false;
    }
    // The colours below are 0..255 display values; Compose multiplies scene radiance by
    // PaperWhiteNit / 40000, so undo that here or every colour lands far past white.
    float displayToRadiance =
        0.6f / (255.0f * max(Bindless.GetGpuscene().Camera[0].PaperWhiteNit / 40000.0f, 1.0e-6f));

    // Report what the gather actually uses: the finest cascade with a published probe around the
    // point. A cascade whose pool overflowed falls through to a coarser one there, and the views
    // show that coarser cascade (mode 1) rather than a hole (mode 2).
    uint cascadeCount = GetAmbientCubeCascadeCount();
    bool inAnyWindow = false;
    bool anyBrick = false;
    uint finestContaining = cascadeCount;
    uint servingCascade = cascadeCount;
    for (uint cascadeIndex = 0; cascadeIndex < cascadeCount && servingCascade == cascadeCount; ++cascadeIndex)
    {
        float4 grid = Bindless.GetGpuscene().Camera[0].AmbientCubeCascadeGrid[cascadeIndex];
        float cubeUnit = max(grid.w, 0.0001f);
        float3 nearpos = (worldPos - grid.xyz) / cubeUnit;
        if (any(nearpos < 0.0f) || nearpos.x > CUBE_SIZE_XY - 2 || nearpos.y > CUBE_SIZE_Z - 2 ||
            nearpos.z > CUBE_SIZE_XY - 2)
        {
            continue;
        }
        inAnyWindow = true;
        finestContaining = min(finestContaining, cascadeIndex);
        FAmbientCascadeAddress address = GetAmbientCascadeAddress(cascadeIndex);
        int3 baseIdx = int3(floor(nearpos));
        for (int i = 0; i < 8; i++)
        {
            AmbientCube cube;
            if (TryFetchCubePool(baseIdx + int3(i & 1, (i >> 1) & 1, (i >> 2) & 1), address, cube))
            {
                anyBrick = true;
                if (IsAmbientCubePublished(cube))
                {
                    servingCascade = cascadeIndex;
                }
            }
        }
    }

    if (!inAnyWindow)
    {
        color = float4(float3(180.0f, 0.0f, 180.0f) * displayToRadiance, 1.0f);
        return true;
    }

    if (debugMode == 1u)
    {
        const float3 cascadeColors[5] = {
            float3(230.0f, 60.0f, 50.0f),
            float3(240.0f, 170.0f, 30.0f),
            float3(60.0f, 200.0f, 70.0f),
            float3(40.0f, 140.0f, 240.0f),
            float3(150.0f, 80.0f, 230.0f),
        };
        uint shown = servingCascade < cascadeCount ? servingCascade : finestContaining;
        float3 shownColor = cascadeColors[min(shown, 4u)];
        if (shown + 1u < cascadeCount)
        {
            // Show the transition band the gather uses, fading toward the next coarser cascade.
            float4 grid = Bindless.GetGpuscene().Camera[0].AmbientCubeCascadeGrid[shown];
            float3 nearpos = (worldPos - grid.xyz) / max(grid.w, 0.0001f);
            shownColor = lerp(cascadeColors[min(shown + 1u, 4u)], shownColor, AmbientCascadeBlendWeight(shown, nearpos));
        }
        color = float4(shownColor * displayToRadiance, 1.0f);
        return true;
    }

    float3 stateColor = !anyBrick ? float3(255.0f, 18.0f, 18.0f)
                      : servingCascade == cascadeCount ? float3(240.0f, 220.0f, 20.0f)
                      : float3(18.0f, 90.0f, 255.0f);
    color = float4(stateColor * displayToRadiance, 1.0f);
    return true;
}

// Global cube-pool element index for a cascade-local probe, or -1 if its brick is unallocated.
public int AmbientCubePoolIndex(FAmbientCascadeAddress address, int3 probePos)
{
    uint slot = AmbientBrickSlot(address, probePos);
    if (slot == GPU_SCENE_AMBIENT_BRICK_INVALID) return -1;
    return int((address.poolBrickBase + slot) * uint(GPU_SCENE_AMBIENT_BRICK_VOLUME)
               + AmbientBrickLocalOffset(probePos));
}

// Pong pool index (single baking cascade, no cascade base) for a probe, or -1 if unallocated.
public int AmbientCubePongPoolIndex(FAmbientCascadeAddress address, int3 probePos)
{
    uint slot = AmbientBrickSlot(address, probePos);
    if (slot == GPU_SCENE_AMBIENT_BRICK_INVALID) return -1;
    return int(slot * uint(GPU_SCENE_AMBIENT_BRICK_VOLUME) + AmbientBrickLocalOffset(probePos));
}

public bool DecodeAmbientActiveBrickProbe(uint cascadeIndex,
                                          uint activeBrickIndex,
                                          uint localProbeOffset,
                                          out int3 probePos,
                                          out uint voxelIdx,
                                          out int cubeIdx,
                                          out uint bakePasses)
{
    uint poolBricks = Bindless.GetGpuscene().AmbientPoolBricksPerCascade;
    uint packedBrick = Bindless.GetGpuscene().AmbientActiveBrickList[cascadeIndex * poolBricks + activeBrickIndex];
    bakePasses = packedBrick >> GPU_SCENE_AMBIENT_ACTIVE_PASSES_SHIFT;
    if (packedBrick == GPU_SCENE_AMBIENT_BRICK_INVALID)
    {
        probePos = int3(0, 0, 0);
        voxelIdx = 0;
        cubeIdx = -1;
        return false;
    }

    packedBrick &= GPU_SCENE_AMBIENT_ACTIVE_BRICK_MASK;
    // The list holds storage bricks and CPU bake passes; undo the ring offset to get the window brick.
    uint bricksPerY = uint(GPU_SCENE_AMBIENT_BRICKS_X * GPU_SCENE_AMBIENT_BRICKS_Z);
    uint sy = packedBrick / bricksPerY;
    uint sz = (packedBrick - sy * bricksPerY) / uint(GPU_SCENE_AMBIENT_BRICKS_X);
    uint sx = packedBrick - sy * bricksPerY - sz * uint(GPU_SCENE_AMBIENT_BRICKS_X);
    uint3 wrap = uint3(Bindless.GetGpuscene().Camera[0].AmbientCubeCascadeWrap[cascadeIndex].xyz);
    uint bx = (sx + uint(GPU_SCENE_AMBIENT_BRICKS_X) - wrap.x) % uint(GPU_SCENE_AMBIENT_BRICKS_X);
    uint by = (sy + uint(GPU_SCENE_AMBIENT_BRICKS_Y) - wrap.y) % uint(GPU_SCENE_AMBIENT_BRICKS_Y);
    uint bz = (sz + uint(GPU_SCENE_AMBIENT_BRICKS_Z) - wrap.z) % uint(GPU_SCENE_AMBIENT_BRICKS_Z);

    uint ly = localProbeOffset / uint(GPU_SCENE_AMBIENT_BRICK_EDGE * GPU_SCENE_AMBIENT_BRICK_EDGE);
    uint lz = (localProbeOffset - ly * uint(GPU_SCENE_AMBIENT_BRICK_EDGE * GPU_SCENE_AMBIENT_BRICK_EDGE)) /
              uint(GPU_SCENE_AMBIENT_BRICK_EDGE);
    uint lx = localProbeOffset - ly * uint(GPU_SCENE_AMBIENT_BRICK_EDGE * GPU_SCENE_AMBIENT_BRICK_EDGE) -
              lz * uint(GPU_SCENE_AMBIENT_BRICK_EDGE);

    uint x = bx * uint(GPU_SCENE_AMBIENT_BRICK_EDGE) + lx;
    uint y = by * uint(GPU_SCENE_AMBIENT_BRICK_EDGE) + ly;
    uint z = bz * uint(GPU_SCENE_AMBIENT_BRICK_EDGE) + lz;
    probePos = int3(int(x), int(y), int(z));

    voxelIdx = GetCascadeBaseOffset(cascadeIndex) + packedBrick * uint(GPU_SCENE_AMBIENT_BRICK_VOLUME) +
               localProbeOffset;
    uint slot = Bindless.GetGpuscene().AmbientBrickTable[cascadeIndex * uint(GPU_SCENE_AMBIENT_BRICKS_PER_CASCADE) +
                                                         packedBrick];
    cubeIdx = int((cascadeIndex * poolBricks + slot) * uint(GPU_SCENE_AMBIENT_BRICK_VOLUME) +
                  localProbeOffset);
    return true;
}

bool TryFetchCubePool(int3 probePos, FAmbientCascadeAddress address, out AmbientCube cube)
{
    int idx = AmbientCubePoolIndex(address, probePos);
    if (idx < 0)
    {
        cube = ZeroAmbientCube();
        return false;
    }
    cube = Bindless.GetGpuscene().Cubes[idx];
    return true;
}

bool TryFetchCubePongPool(int3 probePos, FAmbientCascadeAddress address, out AmbientCube cube)
{
    int idx = AmbientCubePongPoolIndex(address, probePos);
    if (idx < 0)
    {
        cube = ZeroAmbientCube();
        return false;
    }
    cube = Bindless.GetGpuscene().CubesPong[idx];
    return true;
}

VoxelData FetchVoxel(int3 probePos, in VoxelData* Voxels, FAmbientCascadeAddress address)
{
    return Voxels[address.voxelBase + VoxelStorageIndex(address, probePos)];
}

// Weight of a cascade at a point inside its window: 1 in the core, easing to 0 across the transition
// band before each window face that a coarser cascade continues past (AmbientCascadeOpenFaces on the
// CPU packs the faces and the band width into AmbientCubeCascadeWrap[i].w). Y bands are half width.
public float AmbientCascadeBlendWeight(uint cascadeIndex, float3 nearpos)
{
    uint packed = uint(Bindless.GetGpuscene().Camera[0].AmbientCubeCascadeWrap[cascadeIndex].w);
    uint openFaces = packed & 0x3Fu;
    float band = float(packed >> 6u);
    if (openFaces == 0u || band <= 0.0f)
    {
        return 1.0f;
    }
    float3 invBand = 1.0f / float3(band, max(band * 0.5f, 1.0f), band);
    float3 low = nearpos * invBand;
    float3 high = (float3(CUBE_SIZE_XY - 2, CUBE_SIZE_Z - 2, CUBE_SIZE_XY - 2) - nearpos) * invBand;
    float edge = 1.0f;
    if ((openFaces & 0x01u) != 0u) edge = min(edge, low.x);
    if ((openFaces & 0x02u) != 0u) edge = min(edge, high.x);
    if ((openFaces & 0x04u) != 0u) edge = min(edge, low.y);
    if ((openFaces & 0x08u) != 0u) edge = min(edge, high.y);
    if ((openFaces & 0x10u) != 0u) edge = min(edge, low.z);
    if ((openFaces & 0x20u) != 0u) edge = min(edge, high.z);
    return smoothstep(0.0f, 1.0f, saturate(edge));
}

// Per-sample uniform number for the cascade transition: a hash of the sample position and the frame,
// so neighbouring pixels and successive frames draw differently and temporal accumulation (or, in the
// bake, the running average) turns the selection into a gradient.
float AmbientCascadeTransitionRandom(float3 inPos)
{
    uint value = asuint(inPos.x) ^ (asuint(inPos.y) * 0x9e3779b9u) ^ (asuint(inPos.z) * 0x85ebca6bu) ^
                 (Bindless.GetGpuscene().Camera[0].TotalFrames * 0xc2b2ae35u);
    value ^= value >> 16u;
    value *= 0x7feb352du;
    value ^= value >> 15u;
    value *= 0x846ca68bu;
    value ^= value >> 16u;
    return float(value >> 8u) * (1.0f / 16777216.0f);
}

// One trilinear probe gather, shared by the plain and the "stable" (pong pool) entry points: the finest
// cascade with a contributing probe lights the point. Inside a cascade's transition band
// (AmbientCascadeBlendWeight) the sample goes to the next coarser cascade with probability 1 - weight
// instead, so the switch is a gradient that costs one gather per sample, not two; a brick that just
// scrolled into the outer edge of a window also only takes over gradually. Only one cascade is ever
// passed over, and one extra iteration gathers it after all if no coarser cascade contributes.
//
// The corner gather is written out once, inside the loop. The bake calls this for every bounce hit,
// and a second inlined copy (or a helper with an out parameter) measurably slowed the bake kernel.
// The corner loop is ordered cheapest-first: the trilinear weight needs no memory at all, the
// 8-byte VoxelData decides visibility, and only a corner that survives both pays for the 40-byte
// AmbientCube fetch behind the sparse brick indirection.
float4 gatherAmbientCubes<T : IAmbientCubeSampler>(float3 inPos, float3 normal, bool useStablePool,
                                                   uint stableCascadeIndex)
{
    VoxelData* Voxels = Bindless.GetGpuscene().Voxels;
    uint cascadeCount = GetAmbientCubeCascadeCount();
    uint publishPasses = GetAmbientCubePublishPasses();

    bool passedOver = false;
    uint passedCascade = 0u;
    float transitionRandom = -1.0f;
    for (uint iteration = 0; iteration <= cascadeCount; ++iteration)
    {
        // The extra last iteration revisits the cascade that was passed over, if any.
        if (iteration == cascadeCount && !passedOver)
        {
            break;
        }
        uint cascadeIndex = iteration < cascadeCount ? iteration : passedCascade;
        float4 grid = Bindless.GetGpuscene().Camera[0].AmbientCubeCascadeGrid[cascadeIndex];
        float cubeUnit = max(grid.w, 0.0001f);
        float3 nearpos = (inPos - grid.xyz) / cubeUnit;

        if (nearpos.x < 0 || nearpos.y < 0 || nearpos.z < 0 || nearpos.x > CUBE_SIZE_XY - 2 || nearpos.y > CUBE_SIZE_Z - 2 ||
            nearpos.z > CUBE_SIZE_XY - 2)
        {
            continue;
        }

        if (!passedOver && iteration < cascadeCount)
        {
            float blendWeight = AmbientCascadeBlendWeight(cascadeIndex, nearpos);
            if (blendWeight < 1.0f)
            {
                if (transitionRandom < 0.0f)
                {
                    transitionRandom = AmbientCascadeTransitionRandom(inPos);
                }
                if (transitionRandom >= blendWeight)
                {
                    passedOver = true;
                    passedCascade = cascadeIndex;
                    continue;
                }
            }
        }

        FAmbientCascadeAddress address = GetAmbientCascadeAddress(cascadeIndex);
        int3 baseIdx = int3(floor(nearpos));
        float3 frac = frac(nearpos);

        float totalWeight = 0.0;
        float4 result = float4(0.0, 0.0, 0.0, 0.0);

        for (int i = 0; i < 8; i++)
        {
            int3 offset = int3(i & 1, (i >> 1) & 1, (i >> 2) & 1);

            float wx = offset.x == 0 ? (1.0 - frac.x) : frac.x;
            float wy = offset.y == 0 ? (1.0 - frac.y) : frac.y;
            float wz = offset.z == 0 ? (1.0 - frac.z) : frac.z;
            float weight = wx * wy * wz;
            if (weight <= 0.0f)
            {
                continue;
            }

            int3 probePos = baseIdx + offset;
            uint packedDistance = FetchVoxel(probePos, Voxels, address).distanceToSolid;

            // Nibble y is the "inside geometry" marker; a probe buried in a solid contributes nothing.
            if (((packedDistance >> 4) & 0xFu) == 0u)
            {
                continue;
            }

            // Leak test: is the probe visible from the sample point, given the probe's per-face
            // clearance? Only the face the offset direction points at can be the blocker on each
            // axis, so take |dir| once and select that nibble -- the six sqrt(max(+-dir, 0)) pairs
            // this replaces evaluated three terms that were always zero.
            float3 probeToLocal = frac - float3(offset);
            float lengthSquared = dot(probeToLocal, probeToLocal);
            float invLength = rsqrt(max(lengthSquared, 1e-12f));
            float distanceToProbe = lengthSquared * invLength;
            float3 axisWeight = sqrt(abs(probeToLocal) * invLength);
            float3 faceDistance = float3(
                float((packedDistance >> (probeToLocal.x >= 0.0f ? 16u : 20u)) & 0xFu),
                float((packedDistance >> (probeToLocal.y >= 0.0f ? 24u : 28u)) & 0xFu),
                float((packedDistance >> (probeToLocal.z >= 0.0f ? 8u : 12u)) & 0xFu)) * (1.0f / 15.0f);

            if (distanceToProbe > dot(axisWeight, faceDistance) + 0.05f)
            {
                continue;
            }

            AmbientCube cube;
            bool hasCube = (useStablePool && cascadeIndex == stableCascadeIndex)
                ? TryFetchCubePongPool(probePos, address, cube)
                : TryFetchCubePool(probePos, address, cube);
            // Unpublished probes (see GetAmbientCubePublishPasses) are skipped like missing ones.
            if (!hasCube || AmbientCubeSampleCount(cube) < publishPasses)
            {
                continue;
            }

            result += T.Sample(cube, normal) * weight;
            totalWeight += weight;
        }

        if (totalWeight > 0.0)
        {
            return result / totalWeight;
        }
    }

    return float4(0.0, 0.0, 0.0, 1.0);
}

public float4 interpolateAmbientCubes<T : IAmbientCubeSampler>(float3 inPos, float3 normal)
{
    float4 debugColor;
    if (TryAmbientCubeDebugView(inPos, debugColor))
    {
        return debugColor;
    }
    return gatherAmbientCubes<T>(inPos, normal, false, 0u);
}

public float4 interpolateAmbientCubesStable<T : IAmbientCubeSampler>(float3 inPos, float3 normal, uint stableCascadeIndex)
{
    return gatherAmbientCubes<T>(inPos, normal, true, stableCascadeIndex);
}

// Solid test for the voxel containing `pos` (cascade-local grid coordinates). `clearanceVoxels`
// comes back as the L-infinity distance transform in cells, always >= 1: every cell whose index
// differs from this one by less than that on all three axes is guaranteed empty, which is what
// lets the DDA jump ahead. See FCPUProbeBaker::RebuildDistanceField for how the field is built.
public bool inSolid(float3 pos, float3 rayDir, inout float offsetDist, inout uint matId, inout float clearanceVoxels,
                      in VoxelData* Cubes, FAmbientCascadeAddress address, float cubeUnit)
{
    clearanceVoxels = 1;
    // Early out if position is outside the probe grid
    if (pos.x <= 0 || pos.y <= 0 || pos.z <= 0 ||
        pos.x > CUBE_SIZE_XY - 2 || pos.y > CUBE_SIZE_Z - 2 || pos.z > CUBE_SIZE_XY - 2)
    {
        return false;
    }

    int3 baseIdx = int3(floor(pos));

    VoxelData cube = FetchVoxel(baseIdx, Cubes, address);
    matId = cube.matId;

    uint4 unpack0 = UnpackNibblesLow(cube.distanceToSolid);
    if (cube.matId > 0)
    {
        if (unpack0.y == 0)
        {
            offsetDist = 0;
            return true;
        }

        // The per-face nibbles only matter once the voxel is known to be occupied, and rayDir is
        // already unit length, so the axis weights are its components straight off.
        uint4 unpack1 = UnpackNibblesHigh(cube.distanceToSolid);
        float3 dir = -rayDir;

        float distPZ = unpack0.z / 15.0f;
        float distNZ = unpack0.w / 15.0f;
        float distPX = unpack1.x / 15.0f;
        float distNX = unpack1.y / 15.0f;
        float distPY = unpack1.z / 15.0f;
        float distNY = unpack1.w / 15.0f;

        float wx_pos = (max(dir.x, 0.0));
        float wx_neg = (max(-dir.x, 0.0));
        float wy_pos = (max(dir.y, 0.0));
        float wy_neg = (max(-dir.y, 0.0));
        float wz_pos = (max(dir.z, 0.0));
        float wz_neg = (max(-dir.z, 0.0));
        float hitLength = wx_pos * distPX + wx_neg * distNX +
                          wy_pos * distPY + wy_neg * distNY +
                          wz_pos * distPZ + wz_neg * distNZ;

        float hitLengthRef = abs(dir.x) + abs(dir.y) + abs(dir.z);

        if (hitLength < hitLengthRef)
        {
            offsetDist = hitLength * cubeUnit;
            return true;
        }
    }

    clearanceVoxels = max(1.0f, float(unpack0.x));
    return false;
}

public interface IProbeGenerator
{
    void Render(IRayTracer tracer, float3 origin, int cubeIdx, uint voxelIdx, inout uint4 RandomSeed);
    void RenderAmbientCubeOnly(IRayTracer tracer, float3 origin, int cubeIdx, uint voxelIdx, uint bakePasses,
                              inout uint4 RandomSeed);
}

// Bake-side logic (FGpuProbeGenerator) lives in AmbientCubeBaker.slang.
