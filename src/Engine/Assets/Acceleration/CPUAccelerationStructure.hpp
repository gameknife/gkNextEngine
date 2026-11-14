#pragma once
#include "Engine/Assets/AssetsFwd.hpp"
#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Assets/GPU/UniformBuffer.hpp"
#include "Engine/Assets/Acceleration/AmbientCascadeLayout.hpp"
#include "Engine/Assets/Acceleration/AmbientBakeBatch.hpp"
#include "Engine/Runtime/RuntimeFwd.hpp"
#include "Engine/Vulkan/VulkanFwd.hpp"
#include <glm/glm.hpp>

// Keep tinybvh's scalar implementation for targets without its verified SIMD
// path. Linux x86-64-v2 has no AVX, and the bundled tinybvh NEON path does not
// compile with GCC on AArch64.
#if ((defined(__x86_64__) || defined(_M_X64)) && \
     !defined(__AVX__) && !defined(__AVX2__) && \
     !defined(_M_AVX) && !defined(_M_AVX2)) || \
    defined(__aarch64__) || defined(_M_ARM64)
#define TINYBVH_NO_SIMD
#endif
#include "ThirdParty/tinybvh/tiny_bvh.h"
#include <queue>

#include "Engine/Assets/Data/Material.hpp"

namespace std {
    template <>
    struct hash<glm::ivec3> {
        std::size_t operator()(const glm::ivec3& v) const noexcept {
            return std::hash<int>()(v.x) ^ (std::hash<int>()(v.y) << 1) ^ (std::hash<int>()(v.z) << 2);
        }
    };

    template <>
    struct equal_to<glm::ivec3> {
        bool operator()(const glm::ivec3& lhs, const glm::ivec3& rhs) const noexcept {
            return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z;
        }
    };
}

namespace Assets::CPU
{

struct FCPUBLASVertInfo
{
    glm::vec3 normal;
    uint32_t matIdx;
};

struct FCPUTLASInstanceInfo
{
    std::array<uint32_t, 16> matIdxs;
    uint32_t nodeId;
    glm::vec3 worldBoundsMin{0.0f};
    glm::vec3 worldBoundsMax{0.0f};
    bool navRelevant = false;
    bool rayCastVisible = true;
};

struct FCPUBLASContext
{
    tinybvh::BVH bvh;
    std::vector<tinybvh::bvhvec4> triangles;
    std::vector<FCPUBLASVertInfo> extinfos;
};

struct FCPUBLASSet
{
    FCPUBLASSet() = default;
    FCPUBLASSet(const FCPUBLASSet&) = delete;
    FCPUBLASSet& operator=(const FCPUBLASSet&) = delete;
    FCPUBLASSet(FCPUBLASSet&&) = delete;
    FCPUBLASSet& operator=(FCPUBLASSet&&) = delete;

    uint64_t generation = 0;
    std::vector<FCPUBLASContext> contexts;
    std::vector<tinybvh::BVHBase*> list;
};

struct FCPUMaterialView
{
    Material::Enum materialModel = Material::Enum::Lambertian;
};

struct FCPUMaterialTable
{
    uint64_t generation = 0;
    std::vector<FCPUMaterialView> entries;
};

// A TLAS is built directly in its final heap allocation. tinybvh keeps pointers
// into instances and the BLAS list, so snapshots must never be copied or moved.
struct FCPUTLASSnapshot
{
    FCPUTLASSnapshot() = default;
    FCPUTLASSnapshot(const FCPUTLASSnapshot&) = delete;
    FCPUTLASSnapshot& operator=(const FCPUTLASSnapshot&) = delete;
    FCPUTLASSnapshot(FCPUTLASSnapshot&&) = delete;
    FCPUTLASSnapshot& operator=(FCPUTLASSnapshot&&) = delete;

    uint64_t sceneRevision = 0;
    std::shared_ptr<const FCPUBLASSet> blasSet;
    std::shared_ptr<const FCPUMaterialTable> materialTable;
    std::vector<tinybvh::BLASInstance> instances;
    std::vector<FCPUTLASInstanceInfo> contexts;
    tinybvh::BVH tlas;
};

struct FCPUTLASBuildInput
{
    uint64_t epoch = 0;
    uint64_t sceneRevision = 0;
    std::chrono::steady_clock::time_point requestTime;
    double captureMilliseconds = 0.0;
    std::shared_ptr<const FCPUBLASSet> blasSet;
    std::shared_ptr<const FCPUMaterialTable> materialTable;
    std::shared_ptr<const FCPUTLASSnapshot> previousSnapshot;
    std::vector<tinybvh::BLASInstance> instances;
    std::vector<FCPUTLASInstanceInfo> contexts;
};

struct FCPUTLASBuildResult
{
    uint64_t epoch = 0;
    std::shared_ptr<FCPUTLASSnapshot> snapshot;
    std::chrono::steady_clock::time_point requestTime;
    std::chrono::steady_clock::time_point buildStartTime;
    std::chrono::steady_clock::time_point buildEndTime;
    bool hasNavDirtyBounds = false;
    glm::vec3 navDirtyWorldMin{0.0f};
    glm::vec3 navDirtyWorldMax{0.0f};
};

struct FCPUTLASBuildStats
{
    uint64_t publishedRevision = 0;
    uint64_t latestRequestedRevision = 0;
    uint64_t coalescedRequestCount = 0;
    uint64_t completedBuildCount = 0;
    uint64_t snapshotStaleness = 0;
    double lastBuildMilliseconds = 0.0;
    double lastCaptureMilliseconds = 0.0;
    double lastBuildToPublishMilliseconds = 0.0;
    double buildToPublishP95Milliseconds = 0.0;
};

// CPU half of one cascade: its voxel array (in storage order, see AmbientStorageBrick) and the
// distance-field seeds. Everything here works on whole bricks or on boxes of window cells, so a
// caller can rebuild part of a cascade without touching the rest.
struct FCPUProbeBaker
{
    uint32_t cascadeIndex = 0;
    float UNIT_SIZE = Assets::CUBE_UNIT;
    // World position of window cell (0, 0, 0).
    glm::vec3 CUBE_OFFSET{0.0f};
    // Ring offset of the brick storage.
    glm::ivec3 BRICK_WRAP{0};

    std::vector<Assets::VoxelData> voxels;
    std::vector<uint8_t> distanceToSolidSeeds;

    void Init(uint32_t cascadeIdx, float unitSize, glm::vec3 offset, glm::ivec3 wrap = glm::ivec3(0));
    uint32_t VoxelAddress(int x, int y, int z) const;
    glm::ivec3 WindowBrick(uint32_t storageBrick) const;

    // Voxelizes all 512 cells of one storage brick: material, per-face clearance and the seed the
    // distance field is built from.
    void VoxelizeBrick(uint32_t storageBrick, const FCPUTLASSnapshot& snapshot);
    void ClearVoxels();

    // The distance field is L-infinity and capped at kMaxDistanceFieldSeed, so a cell only depends
    // on occupancy within that many cells. Given the window cells whose occupancy changed, this
    // recomputes every cell that can depend on them (the box grown by two bricks) on a domain grown
    // by two more, which reproduces a whole-cascade rebuild exactly inside the output box. Returns
    // the output box (inclusive, window cells). The no-argument form rebuilds the whole cascade.
    std::pair<glm::ivec3, glm::ivec3> RebuildDistanceField(const glm::ivec3& changedMin, const glm::ivec3& changedMax);
    void RebuildDistanceField();

    // Storage bricks overlapping a box of window cells (inclusive).
    std::vector<uint32_t> StorageBricksInBox(const glm::ivec3& cellMin, const glm::ivec3& cellMax) const;

    void UploadGPU(Vulkan::DeviceMemory& voxelDeviceMemory, size_t byteBaseOffset, uint32_t elementOffset);
    // Uploads only the given storage bricks; each is one contiguous 4 KB run.
    void UploadBricks(Vulkan::DeviceMemory& voxelDeviceMemory, size_t byteBaseOffset, uint32_t elementOffset,
                      const std::vector<uint32_t>& storageBricks);
};

// Sparse cube storage: per-cascade storage brick -> cube-pool slot (GPU_SCENE_AMBIENT_BRICK_INVALID
// when unallocated). Rebuilt from the CPU distance field after each voxel job: a brick is wanted
// when one of its empty voxels lies within the cascade's surface shell (ResolveAmbientBrickShellCells),
// wanted bricks keep their old slot, and new ones take free slots nearest the focus first. Bricks left
// without a slot stay INVALID, and the gather falls through to a coarser cascade there.
//
// The table is also the bake queue: every allocated brick counts the bake passes its slot has had.
// A brick whose slot is new starts at zero; the scheduler serves unpublished bricks first, finer
// cascades first and bricks near the focus first, and a brick leaves the queue at
// kAmbientBakeConvergencePasses.
struct FCPUBrickTable
{
    std::vector<uint32_t> brickTable;
    std::vector<uint8_t> bakePasses;
    std::vector<uint32_t> activeBricksPerCascade;
    // Bricks the activation rule wanted, and how many of them found no free slot.
    std::vector<uint32_t> candidateBricksPerCascade;
    std::vector<uint32_t> overflowBricksPerCascade;
    std::vector<uint32_t> slotsToClear;
    uint32_t activeBricksLastBuild = 0;

    // focus: world position whose nearby bricks get first claim on free slots when the pool is short.
    bool UpdateData(const std::vector<FCPUProbeBaker>& bakers, uint32_t cascadeCapacity,
                    uint32_t poolBricksPerCascade, const glm::vec3& focus);
    void UploadGPU(Vulkan::DeviceMemory& deviceMemory, size_t tableByteOffset) const;

    FAmbientBakeQueueStats QueueStats(uint32_t publishPasses) const;
    bool SelectBakeBatch(const std::vector<FCPUProbeBaker>& bakers, uint32_t maxBricks, uint32_t publishPasses,
                         const glm::vec3& focus, FAmbientBakeBatch& outBatch) const;
    void CompleteBakeBatch(const FAmbientBakeBatch& batch);
    // The GPU cube pool was cleared: every allocated brick starts over.
    void ResetBakePasses();
    // The cube pool came back converged from the disk cache.
    void MarkConverged();
};

// The ambient/voxel grid the cascade bakers were actually built with. User settings can change at
// any time, but the CPU voxel array and the GPU cube pool only adopt a new grid when the bakers are
// re-initialized, so consumers that must agree with the baked data (the shading UBO above all) read
// this instead of the pending settings.
struct FAmbientGridConfig
{
    float baseUnit = Assets::CUBE_UNIT;
    glm::vec3 offsetBias{0.0f};
    // Cascade count, effective ratio and where every cascade's grid sits.
    Assets::FAmbientCascadeLayout layout;
};

// UI-facing state for the CPU half of the ambient GI bake pipeline. Voxel work is counted in
// voxelization tasks (a few bricks each); the distance field runs once every task of the job is done.
enum class EProbeBakeStage : uint8_t
{
    Idle,
    VoxelData,
    DistanceField,
    // The GPU lighting has converged and the result is on its way to the disk cache. Off the game
    // thread, but the bake is not finished until it lands, so the UI keeps showing progress.
    CacheSave,
};

struct FProbeBakeProgress
{
    EProbeBakeStage stage = EProbeBakeStage::Idle;
    uint32_t completedVoxelGroups = 0;
    uint32_t totalVoxelGroups = 0;
};

class FCPUAccelerationStructure
{
public:
    using SnapshotPtr = std::shared_ptr<const FCPUTLASSnapshot>;

    void InitBVH(Assets::Scene& scene);
    void RebuildBVHOnly(Assets::Scene& scene);
    void PollBVHBuild();

    SnapshotPtr AcquireSnapshot() const;
    FCPUTLASBuildStats GetBuildStats() const;

    
    Assets::RayCastResult RayCastInCPU(glm::vec3 rayOrigin, glm::vec3 rayDir);
    // Batch readers pin a snapshot once rather than contending on the shared
    // snapshot owner for every ray. The same geometry is used for the batch.
    // Floor probes preserve the normal to reject the inside of solid meshes.
    static Assets::RayCastResult RayCastInSnapshot(const FCPUTLASSnapshot& snapshot,
                                                   glm::vec3 rayOrigin, glm::vec3 rayDir,
                                                   bool faceNormalTowardRay = true, float maxDistance = 2000.0f);
    
    // Full bake: every brick of every cascade enters the voxel pipeline (or the disk cache restores
    // the whole result).
    bool AsyncProcessFull(Assets::Scene& scene, Vulkan::DeviceMemory* VoxelGPUMemory);

    // Advances the voxel pipeline. Cheap when idle; called every frame.
    bool Tick(Assets::Scene& scene, Vulkan::DeviceMemory* arenaMemory);
    bool HasPendingWork() const;

    FProbeBakeProgress GetProbeBakeProgress() const;

    // GPU bake queue. Acquire picks this frame's bricks and writes them into the cascade's active
    // brick list; Complete records the pass once the dispatch has been recorded.
    bool AcquireAmbientBakeBatch(Assets::Scene& scene, uint32_t maxBricks, FAmbientBakeBatch& outBatch);
    void CompleteAmbientBakeBatch(const FAmbientBakeBatch& batch);
    bool IsAmbientBakeBatchCurrent(const FAmbientBakeBatch& batch) const { return batch.generation == brickTableGeneration_; }
    void ResetAmbientBakePasses();
    FAmbientBakeQueueStats GetAmbientBakeQueueStats() const;

    // The grid the current voxel/cube data was produced with. False until the bakers exist, which is
    // the case for scenes created without CPU acceleration.
    bool TryGetAmbientGridConfig(FAmbientGridConfig& outConfig) const;
    bool ConsumeNavRelevantDirtyBounds(glm::vec3& outWorldMin, glm::vec3& outWorldMax);
    void ClearNavRelevantDirtyBounds();

    
    void ClearAllTasks();

private:
    friend struct FCPUAccelerationStructureTestAccess;

    bool InitCascadeBakers(const Runtime::Config::UserSettings& settings, const Assets::Scene& scene);
    FAmbientGridConfig ResolveAmbientGridConfig(const Runtime::Config::UserSettings& settings,
                                                const Assets::Scene& scene) const;
    uint32_t GetActiveCascadeCount() const { return static_cast<uint32_t>(cascadeBakers.size()); }

    void UpdateBVH(Assets::Scene& scene);
    void PublishSnapshot(SnapshotPtr snapshot);
    std::shared_ptr<FCPUTLASBuildInput> CaptureBuildInput(Assets::Scene& scene);
    static std::shared_ptr<FCPUTLASBuildResult> BuildSnapshot(FCPUTLASBuildInput& input);
    uint64_t RequestRuntimeBuild(Assets::Scene& scene);
    uint64_t QueueBuildInput(std::shared_ptr<FCPUTLASBuildInput> input);
    void StartBuild(std::shared_ptr<FCPUTLASBuildInput> input);
    void CancelRuntimeBuilds();
    void MergeNavDirtyBounds(const FCPUTLASBuildResult& result);
    // Voxel pipeline: the bricks in voxelJobBricks_ are voxelized, the distance field is rebuilt
    // around them, the touched bricks are uploaded and the brick table is reclassified.
    void StartVoxelJob();
    void FinishVoxelJob(Assets::Scene& scene, Vulkan::DeviceMemory& arenaMemory);
    bool HasProbeVoxelizationWork() const;
    glm::vec3 ResolveAmbientFocus(const Assets::Scene& scene) const;

    // Ambient bake disk cache. The key is computed once per full bake from the scene's initial
    // structure; a hit replaces the whole bake, a miss arms the write that happens after the GPU
    // bake converges. See AmbientBakeCache.hpp.
    bool TryRestoreAmbientBakeCache(Assets::Scene& scene, Vulkan::DeviceMemory* arenaMemory, uint64_t key);
    void FlushPendingAmbientBakeCacheSave(Assets::Scene& scene, Vulkan::DeviceMemory* arenaMemory);

#if defined(__cpp_lib_atomic_shared_ptr) && __cpp_lib_atomic_shared_ptr >= 201711L
    std::atomic<SnapshotPtr> activeSnapshot_;
#else
    // Keep a mutex fallback for C++20 standard libraries that do not yet
    // provide the std::atomic<std::shared_ptr<T>> specialization.
    SnapshotPtr activeSnapshot_;
    mutable std::mutex snapshotMutex_;
#endif
    std::shared_ptr<const FCPUBLASSet> blasSet_;
    uint64_t blasGeneration_ = 0;
    uint64_t materialGeneration_ = 0;
    uint64_t sceneRevision_ = 0;
    std::atomic<uint64_t> buildEpoch_{1};
    std::atomic<uint64_t> publishedRevision_{0};
    std::atomic<uint64_t> latestRequestedRevision_{0};
    mutable std::mutex buildMutex_;
    bool buildInFlight_ = false;
    std::shared_ptr<FCPUTLASBuildInput> latestBuildInput_;
    std::shared_ptr<FCPUTLASBuildResult> completedBuildResult_;
    uint64_t coalescedRequestCount_ = 0;
    uint64_t completedBuildCount_ = 0;
    double lastBuildMilliseconds_ = 0.0;
    double lastCaptureMilliseconds_ = 0.0;
    double lastBuildToPublishMilliseconds_ = 0.0;
    std::vector<double> buildToPublishSamples_;
    bool fullProbeBakePending_ = true;
    bool ambientBakeIdle_ = false;
    uint64_t ambientCacheKey_ = 0;
    // Earliest frame the save may run: the converging frame's dispatches must have retired first,
    // which is what lets the whole readback happen on a worker with no device wait.
    uint32_t ambientCacheSaveFrame_ = 0;
    bool ambientCacheWriteArmed_ = false;
    bool ambientCachePendingSave_ = false;
    // Set on the game thread when the write task is dispatched, cleared by that task. Atomic only
    // because GetProbeBakeProgress() reads it while the worker is running.
    std::atomic<bool> ambientCacheSaveInFlight_{false};
    enum class EVoxelJobStage : uint8_t
    {
        Idle,
        Voxelizing,
        DistanceField,
    };
    EVoxelJobStage voxelJobStage_ = EVoxelJobStage::Idle;
    // Per cascade: storage bricks waiting for / in the current voxel job.
    std::vector<std::vector<uint32_t>> voxelJobBricks_;
    // Per cascade: window-cell box the current job's distance field rebuild reported as rewritten.
    std::vector<std::pair<glm::ivec3, glm::ivec3>> voxelJobOutputBoxes_;
    std::vector<uint32_t> voxelJobTasks_;
    uint32_t totalVoxelTasks_ = 0;
    std::atomic<uint32_t> completedVoxelTasks_{0};
    glm::vec3 ambientFocus_{0.0f};
    uint64_t brickTableGeneration_ = 1;
    bool ambientBakeActiveLogged_ = false;

    bool hasNavRelevantDirtyBounds_ = false;
    glm::vec3 navRelevantDirtyWorldMin_{0.0f};
    glm::vec3 navRelevantDirtyWorldMax_{0.0f};

    std::vector<FCPUProbeBaker> cascadeBakers;
    FAmbientGridConfig committedAmbientGrid_;
    bool hasCommittedAmbientGrid_ = false;
    FCPUBrickTable cpuBrickTable;
};

}
