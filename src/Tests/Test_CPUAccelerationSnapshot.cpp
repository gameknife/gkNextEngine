#include "TestCommon.hpp"
#include "Test_CPUAccelerationAccess.hpp"

#include "Engine/Assets/Acceleration/CPUAccelerationStructure.hpp"
#include "Engine/Assets/Acceleration/CPUAccelerationStructure.Internal.hpp"
#include "Engine/Runtime/Subsystems/TaskCoordinator.hpp"

#include <atomic>
#include <thread>

namespace
{
    using namespace Assets::CPU;

    std::shared_ptr<const FCPUTLASSnapshot> MakeSnapshot(uint32_t nodeId, uint32_t materialId)
    {
        auto blasSet = std::make_shared<FCPUBLASSet>();
        blasSet->generation = 1;
        blasSet->contexts.resize(1);

        FCPUBLASContext& blas = blasSet->contexts[0];
        blas.triangles = {
            tinybvh::bvhvec4(-1.0f, -1.0f, 0.0f, 0.0f),
            tinybvh::bvhvec4(1.0f, -1.0f, 0.0f, 0.0f),
            tinybvh::bvhvec4(0.0f, 1.0f, 0.0f, 0.0f),
        };
        blas.extinfos.push_back({glm::vec3(0.0f, 0.0f, 1.0f), 0});
        blas.bvh.Build(blas.triangles.data(), 1);
        blasSet->list.push_back(&blas.bvh);

        auto materialTable = std::make_shared<FCPUMaterialTable>();
        materialTable->generation = 1;
        materialTable->entries.resize(materialId + 1);

        auto snapshot = std::make_shared<FCPUTLASSnapshot>();
        snapshot->sceneRevision = nodeId;
        snapshot->blasSet = blasSet;
        snapshot->materialTable = materialTable;

        tinybvh::BLASInstance instance;
        instance.blasIdx = 0;
        const glm::mat4 identity(1.0f);
        const glm::mat4 transposed = glm::transpose(identity);
        std::memcpy(instance.transform, &transposed[0], sizeof(instance.transform));
        snapshot->instances.push_back(instance);

        FCPUTLASInstanceInfo context;
        context.matIdxs.fill(0);
        context.matIdxs[0] = materialId;
        context.nodeId = nodeId;
        snapshot->contexts.push_back(context);

        snapshot->tlas.Build(snapshot->instances.data(), 1, blasSet->list.data(), 1);
        return snapshot;
    }

    std::shared_ptr<FCPUTLASBuildInput> MakeBuildInput(
        const std::shared_ptr<const FCPUTLASSnapshot>& baseline, uint64_t revision,
        uint32_t nodeId, uint32_t materialId)
    {
        auto input = std::make_shared<FCPUTLASBuildInput>();
        input->sceneRevision = revision;
        input->requestTime = std::chrono::steady_clock::now();
        input->blasSet = baseline->blasSet;
        input->previousSnapshot = baseline;
        input->instances = baseline->instances;
        input->contexts = baseline->contexts;
        input->contexts[0].nodeId = nodeId;
        input->contexts[0].matIdxs[0] = materialId;

        auto materialTable = std::make_shared<FCPUMaterialTable>();
        materialTable->generation = revision;
        materialTable->entries.resize(materialId + 1);
        input->materialTable = std::move(materialTable);
        return input;
    }
}

TEST_CASE("CPU acceleration snapshots isolate owners", "[Unit][CPUAccelerationSnapshot]")
{
    FCPUAccelerationStructure first;
    FCPUAccelerationStructure second;
    FCPUAccelerationStructureTestAccess::Publish(first, MakeSnapshot(11, 3));
    FCPUAccelerationStructureTestAccess::Publish(second, MakeSnapshot(22, 7));

    const Assets::RayCastResult firstHit = first.RayCastInCPU({0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, -1.0f});
    const Assets::RayCastResult secondHit = second.RayCastInCPU({0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, -1.0f});

    REQUIRE(firstHit.Hit);
    REQUIRE(secondHit.Hit);
    CHECK(firstHit.InstanceId == 11);
    CHECK(firstHit.MaterialId == 3);
    CHECK(secondHit.InstanceId == 22);
    CHECK(secondHit.MaterialId == 7);
    CHECK(first.AcquireSnapshot()->materialTable->entries.size() == 4);
    CHECK(second.AcquireSnapshot()->materialTable->entries.size() == 8);
}

TEST_CASE("CPU acceleration snapshot publish is safe with concurrent readers",
          "[Unit][CPUAccelerationSnapshot][Concurrency]")
{
    FCPUAccelerationStructure accelerationStructure;
    const auto first = MakeSnapshot(31, 1);
    const auto second = MakeSnapshot(32, 2);
    FCPUAccelerationStructureTestAccess::Publish(accelerationStructure, first);

    std::atomic_bool start{false};
    std::atomic_bool failed{false};
    std::vector<std::thread> readers;
    readers.reserve(8);
    for (int readerIndex = 0; readerIndex < 8; ++readerIndex)
    {
        readers.emplace_back([&]()
        {
            while (!start.load(std::memory_order_acquire))
            {
                std::this_thread::yield();
            }
            for (int queryIndex = 0; queryIndex < 2000; ++queryIndex)
            {
                const Assets::RayCastResult hit =
                    accelerationStructure.RayCastInCPU({0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, -1.0f});
                const bool isFirst = hit.InstanceId == 31 && hit.MaterialId == 1;
                const bool isSecond = hit.InstanceId == 32 && hit.MaterialId == 2;
                if (!hit.Hit || (!isFirst && !isSecond))
                {
                    failed.store(true, std::memory_order_relaxed);
                    break;
                }
            }
        });
    }

    start.store(true, std::memory_order_release);
    for (int publishIndex = 0; publishIndex < 1000; ++publishIndex)
    {
        FCPUAccelerationStructureTestAccess::Publish(
            accelerationStructure, (publishIndex & 1) == 0 ? first : second);
    }

    for (std::thread& reader : readers)
    {
        reader.join();
    }
    CHECK_FALSE(failed.load(std::memory_order_relaxed));

    FCPUAccelerationStructureTestAccess::Publish(
        accelerationStructure, std::make_shared<FCPUTLASSnapshot>());
    CHECK_FALSE(accelerationStructure.RayCastInCPU({0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, -1.0f}).Hit);
}

TEST_CASE("CPU acceleration pinned batch keeps its geometry after publication", "[Unit][CPUAccelerationSnapshot]")
{
    FCPUAccelerationStructure accelerationStructure;
    const auto first = MakeSnapshot(31, 1);
    FCPUAccelerationStructureTestAccess::Publish(accelerationStructure, first);
    const auto pinned = accelerationStructure.AcquireSnapshot();
    FCPUAccelerationStructureTestAccess::Publish(accelerationStructure, MakeSnapshot(32, 2));
    const auto batchHit = FCPUAccelerationStructure::RayCastInSnapshot(*pinned, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, -1.0f});
    const auto liveHit = accelerationStructure.RayCastInCPU({0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, -1.0f});
    REQUIRE(batchHit.Hit);
    REQUIRE(liveHit.Hit);
    CHECK(batchHit.InstanceId == 31);
    CHECK(batchHit.MaterialId == 1);
    CHECK(liveHit.InstanceId == 32);
    CHECK(liveHit.MaterialId == 2);
    CHECK(batchHit.T == Catch::Approx(liveHit.T));
    const auto back = FCPUAccelerationStructure::RayCastInSnapshot(*pinned, {0.0f, 0.0f, -1.0f}, {0.0f, 0.0f, 1.0f}, false);
    const auto faced = FCPUAccelerationStructure::RayCastInSnapshot(*pinned, {0.0f, 0.0f, -1.0f}, {0.0f, 0.0f, 1.0f});
    REQUIRE(back.Hit);
    REQUIRE(faced.Hit);
    CHECK(back.Normal.z > 0.0f);
    CHECK(faced.Normal.z < 0.0f);
    const auto shortMiss = FCPUAccelerationStructure::RayCastInSnapshot(*pinned,
        {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, -1.0f}, true, 0.9f);
    const auto shortHit = FCPUAccelerationStructure::RayCastInSnapshot(*pinned,
        {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, -1.0f}, true, 1.1f);
    CHECK_FALSE(shortMiss.Hit);
    REQUIRE(shortHit.Hit);
    CHECK(shortHit.T == Catch::Approx(1.0f));
}

TEST_CASE("CPU acceleration background build coalesces to the latest request",
          "[Unit][CPUAccelerationSnapshot][Concurrency]")
{
    FCPUAccelerationStructure accelerationStructure;
    const auto baseline = MakeSnapshot(40, 0);
    FCPUAccelerationStructureTestAccess::Publish(accelerationStructure, baseline);

    FCPUAccelerationStructureTestAccess::Queue(
        accelerationStructure, MakeBuildInput(baseline, 101, 41, 1));
    FCPUAccelerationStructureTestAccess::Queue(
        accelerationStructure, MakeBuildInput(baseline, 102, 42, 2));
    FCPUAccelerationStructureTestAccess::Queue(
        accelerationStructure, MakeBuildInput(baseline, 103, 43, 3));

    Tasks::TaskCoordinator::GetInstance()->WaitForNamedTask(Tasks::ENamedTaskThread::CPU_AS_BUILD);
    accelerationStructure.PollBVHBuild();
    Tasks::TaskCoordinator::GetInstance()->WaitForNamedTask(Tasks::ENamedTaskThread::CPU_AS_BUILD);
    accelerationStructure.PollBVHBuild();

    const Assets::RayCastResult hit =
        accelerationStructure.RayCastInCPU({0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, -1.0f});
    REQUIRE(hit.Hit);
    CHECK(hit.InstanceId == 43);
    CHECK(hit.MaterialId == 3);

    const FCPUTLASBuildStats stats = accelerationStructure.GetBuildStats();
    CHECK(stats.publishedRevision == 103);
    CHECK(stats.latestRequestedRevision == 103);
    CHECK(stats.coalescedRequestCount == 2);
    CHECK(stats.completedBuildCount == 2);
    CHECK(stats.snapshotStaleness == 0);
    CHECK(stats.lastBuildMilliseconds >= 0.0);
    CHECK(stats.lastBuildToPublishMilliseconds >= stats.lastBuildMilliseconds);
}

namespace
{
    // Marks a voxel solid the way voxelization does (matId + distance-field seed); the caller runs
    // RebuildDistanceField() once all solids are in.
    void SetSolid(FCPUProbeBaker& baker, int x, int y, int z)
    {
        const uint32_t address = GetVoxelAddress(x, y, z);
        baker.voxels[address].matId = 1u;
        baker.distanceToSolidSeeds[address] = 0u;
    }

    uint32_t BrickIndex(uint32_t bx, uint32_t by, uint32_t bz)
    {
        return by * static_cast<uint32_t>(Assets::GPU_SCENE_AMBIENT_BRICKS_X * Assets::GPU_SCENE_AMBIENT_BRICKS_Z) +
               bz * static_cast<uint32_t>(Assets::GPU_SCENE_AMBIENT_BRICKS_X) + bx;
    }
}

TEST_CASE("Ambient brick table keeps newly dirty bricks at the front until acknowledged",
          "[Unit][CPUAccelerationSnapshot][AmbientBake]")
{
    FCPUProbeBaker baker;
    baker.Init(0, 1.0f, glm::vec3(0.0f));
    SetSolid(baker, 4, 4, 4);
    baker.RebuildDistanceField();

    FCPUBrickTable table;
    const uint32_t poolCapacity = static_cast<uint32_t>(Assets::GPU_SCENE_AMBIENT_BRICKS_PER_CASCADE);
    REQUIRE(table.UpdateData({baker}, 1u, poolCapacity, glm::vec3(0.0f)));
    REQUIRE(table.dirtyBricksPerCascade[0] > 0u);
    CHECK(table.dirtyBricksPerCascade[0] == table.activeBricksPerCascade[0]);

    const uint64_t firstRevision = table.dirtyRevision;
    table.AcknowledgeDirty(firstRevision);
    CHECK(table.dirtyBricksPerCascade[0] == 0u);

    SetSolid(baker, Assets::CUBE_SIZE_XY - 5, 4, Assets::CUBE_SIZE_XY - 5);
    baker.RebuildDistanceField();
    REQUIRE(table.UpdateData({baker}, 1u, poolCapacity, glm::vec3(0.0f)));
    const uint32_t dirtyCount = table.dirtyBricksPerCascade[0];
    REQUIRE(dirtyCount > 0u);
    CHECK(dirtyCount < table.activeBricksPerCascade[0]);
    for (uint32_t listIndex = 0; listIndex < dirtyCount; ++listIndex)
    {
        const uint32_t brick = table.activeBrickList[listIndex];
        REQUIRE(brick < table.dirtyBricks.size());
        CHECK(table.dirtyBricks[brick] != 0u);
    }
}

TEST_CASE("Ambient brick activation keeps the air shell over surfaces only",
          "[Unit][CPUAccelerationSnapshot][AmbientBake]")
{
    CHECK(ResolveAmbientBrickShellCells(0.25f) == 8u);
    CHECK(ResolveAmbientBrickShellCells(0.5f) == 4u);
    CHECK(ResolveAmbientBrickShellCells(1.0f) == 2u);
    CHECK(ResolveAmbientBrickShellCells(4.0f) == 2u);

    // A solid block filling bricks x,z in [0,2), y in [0,2) of a 0.25 m cascade (8-cell shell).
    FCPUProbeBaker baker;
    baker.Init(0, 0.25f, glm::vec3(0.0f));
    for (int y = 0; y < 16; ++y)
        for (int z = 0; z < 16; ++z)
            for (int x = 0; x < 16; ++x)
                SetSolid(baker, x, y, z);
    baker.RebuildDistanceField();

    FCPUBrickTable table;
    const uint32_t poolCapacity = static_cast<uint32_t>(Assets::GPU_SCENE_AMBIENT_BRICKS_PER_CASCADE);
    table.UpdateData({baker}, 1u, poolCapacity, glm::vec3(0.0f));
    const auto allocated = [&](uint32_t bx, uint32_t by, uint32_t bz)
    {
        return table.brickTable[BrickIndex(bx, by, bz)] != Assets::GPU_SCENE_AMBIENT_BRICK_INVALID;
    };

    // Entirely solid: no probe in it can be used.
    CHECK_FALSE(allocated(0, 0, 0));
    CHECK_FALSE(allocated(1, 1, 1));
    // The air right above and beside the block is within the shell.
    CHECK(allocated(0, 2, 0));
    CHECK(allocated(2, 0, 0));
    CHECK(allocated(2, 1, 2));
    // One more brick out the nearest air is 9 cells away, past the 8-cell shell.
    CHECK_FALSE(allocated(0, 3, 0));
    CHECK_FALSE(allocated(3, 0, 0));
    CHECK(table.candidateBricksPerCascade[0] == table.activeBricksPerCascade[0]);
    CHECK(table.overflowBricksPerCascade[0] == 0u);
}

TEST_CASE("Ambient brick pool goes to the bricks nearest the focus and keeps existing owners",
          "[Unit][CPUAccelerationSnapshot][AmbientBake]")
{
    // A floor at y = 0 across the whole 1 m cascade: every brick of the lowest layer is wanted.
    FCPUProbeBaker baker;
    baker.Init(0, 1.0f, glm::vec3(0.0f));
    for (int z = 0; z < Assets::CUBE_SIZE_XY; ++z)
        for (int x = 0; x < Assets::CUBE_SIZE_XY; ++x)
            SetSolid(baker, x, 0, z);
    baker.RebuildDistanceField();

    constexpr uint32_t poolCapacity = 8u;
    FCPUBrickTable table;
    table.UpdateData({baker}, 1u, poolCapacity, glm::vec3(4.0f, 0.0f, 4.0f));
    const uint32_t wanted = table.candidateBricksPerCascade[0];
    REQUIRE(wanted == static_cast<uint32_t>(Assets::GPU_SCENE_AMBIENT_BRICKS_X * Assets::GPU_SCENE_AMBIENT_BRICKS_Z));
    CHECK(table.activeBricksPerCascade[0] == poolCapacity);
    CHECK(table.overflowBricksPerCascade[0] == wanted - poolCapacity);
    // The focus sits in brick (0, 0, 0): it and its nearest neighbours win.
    CHECK(table.brickTable[BrickIndex(0, 0, 0)] != Assets::GPU_SCENE_AMBIENT_BRICK_INVALID);
    CHECK(table.brickTable[BrickIndex(1, 0, 0)] != Assets::GPU_SCENE_AMBIENT_BRICK_INVALID);
    CHECK(table.brickTable[BrickIndex(0, 0, 1)] != Assets::GPU_SCENE_AMBIENT_BRICK_INVALID);
    CHECK(table.brickTable[BrickIndex(23, 0, 23)] == Assets::GPU_SCENE_AMBIENT_BRICK_INVALID);

    // Moving the focus does not evict bricks that are still wanted: their probes are baked.
    const std::vector<uint32_t> before = table.brickTable;
    table.UpdateData({baker}, 1u, poolCapacity, glm::vec3(188.0f, 0.0f, 188.0f));
    CHECK(table.brickTable == before);
    CHECK(table.slotsToClear.empty());
}

TEST_CASE("Parallel task cancellation discards queued work and drains completions",
          "[Unit][TaskCoordinator][Concurrency]")
{
    Tasks::TaskCoordinator* coordinator = Tasks::TaskCoordinator::GetInstance();
    std::atomic_bool runningTaskStarted{false};
    std::atomic_bool allowRunningTaskToFinish{false};
    std::atomic_uint32_t executedTaskCount{0};
    std::atomic_uint32_t completedTaskCount{0};

    coordinator->AddParralledTask(
        [&](Tasks::ResTask&)
        {
            runningTaskStarted.store(true, std::memory_order_release);
            while (!allowRunningTaskToFinish.load(std::memory_order_acquire))
            {
                std::this_thread::yield();
            }
            executedTaskCount.fetch_add(1, std::memory_order_relaxed);
        },
        [&](Tasks::ResTask&)
        {
            completedTaskCount.fetch_add(1, std::memory_order_relaxed);
        });
    coordinator->Tick();

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!runningTaskStarted.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::yield();
    }
    REQUIRE(runningTaskStarted.load(std::memory_order_acquire));

    for (uint32_t taskIndex = 0; taskIndex < 8; ++taskIndex)
    {
        coordinator->AddParralledTask(
            [&](Tasks::ResTask&)
            {
                executedTaskCount.fetch_add(1, std::memory_order_relaxed);
            },
            [&](Tasks::ResTask&)
            {
                completedTaskCount.fetch_add(1, std::memory_order_relaxed);
            });
    }

    coordinator->CancelAllParralledTasks();
    allowRunningTaskToFinish.store(true, std::memory_order_release);
    coordinator->WaitForAllParralledTask();

    CHECK(executedTaskCount.load(std::memory_order_relaxed) == 1u);
    CHECK(completedTaskCount.load(std::memory_order_relaxed) == 1u);
}
