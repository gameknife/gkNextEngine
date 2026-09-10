#include <catch2/catch_all.hpp>
#include "TestCommon.hpp"

#include "Engine/Assets/Acceleration/AmbientBakeCache.hpp"
#include "Engine/Assets/Acceleration/CPUAccelerationStructure.hpp"
#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Rendering/VulkanBaseRenderer.hpp"
#include "Engine/Runtime/Components/RenderComponent.hpp"

#include <filesystem>
#include <fstream>
#include <algorithm>
#include <functional>

using namespace Assets::CPU;

namespace
{
    // A structurally valid but tiny payload: one cascade, one pool brick. Load() validates the
    // section sizes against the header, so the shapes have to be consistent even at this size.
    FAmbientBakeCachePayload MakeTinyPayload()
    {
        FAmbientBakeCachePayload payload;
        payload.cascadeCount = 1;
        payload.cascadeCapacity = 1;
        payload.voxelCountPerCascade = 64;
        payload.poolBricksPerCascade = 1;

        payload.voxels.resize(payload.voxelCountPerCascade);
        for (uint32_t i = 0; i < payload.voxelCountPerCascade; ++i)
        {
            payload.voxels[i].matId = i % 3u;
            payload.voxels[i].distanceToSolid = 0x12345678u ^ i;
        }

        payload.brickTable.assign(Assets::GPU_SCENE_AMBIENT_BRICKS_PER_CASCADE,
                                  Assets::GPU_SCENE_AMBIENT_BRICK_INVALID);
        payload.brickTable[7] = 0u;
        payload.activeBrickList.assign(1, 7u);
        payload.activeBricksPerCascade.assign(1, 1u);
        payload.pages.resize(static_cast<size_t>(Assets::ACGI_PAGE_COUNT) * Assets::ACGI_PAGE_COUNT);
        payload.pages[11].voxelCount = 1;

        payload.cubes.resize(static_cast<size_t>(Assets::GPU_SCENE_AMBIENT_BRICK_VOLUME));
        for (size_t i = 0; i < payload.cubes.size(); ++i)
        {
            payload.cubes[i].PosZ = static_cast<uint32_t>(i);
            payload.cubes[i].NegX = static_cast<uint32_t>(i * 7u + 1u);
        }
        return payload;
    }

    std::filesystem::path AmbientCacheDirectory()
    {
        return std::filesystem::path(GetAmbientBakeCachePath(0)).parent_path();
    }

    // A finished entry only: the writer renames from "<name>.tmp", and a test that counted the
    // temporary would read a file that is still being written.
    bool IsFinishedAmbientCacheFile(const std::filesystem::path& path)
    {
        const std::string name = path.filename().string();
        return name.rfind("ambient", 0) == 0 && path.extension() == ".gncook";
    }

    // The cache file name is derived from a key the test does not recompute, so the end-to-end test
    // watches the whole directory instead.
    size_t CountAmbientCacheFiles()
    {
        size_t count = 0;
        std::error_code errorCode;
        for (const auto& entry : std::filesystem::directory_iterator(AmbientCacheDirectory(), errorCode))
        {
            if (IsFinishedAmbientCacheFile(entry.path()))
            {
                ++count;
            }
        }
        return count;
    }

    // The end-to-end test does not recompute the engine's key, so it recovers it from the file name
    // (ambient<16 hex>.gncook) to read the payload back.
    bool FindAmbientCacheKey(uint64_t& outKey)
    {
        std::error_code errorCode;
        for (const auto& entry : std::filesystem::directory_iterator(AmbientCacheDirectory(), errorCode))
        {
            const std::string name = entry.path().filename().string();
            if (!IsFinishedAmbientCacheFile(entry.path()) || name.size() < 7 + 16)
            {
                continue;
            }
            outKey = std::stoull(name.substr(7, 16), nullptr, 16);
            return true;
        }
        return false;
    }

    void RemoveAmbientCacheFiles()
    {
        std::error_code errorCode;
        for (const auto& entry : std::filesystem::directory_iterator(AmbientCacheDirectory(), errorCode))
        {
            if (entry.path().filename().string().rfind("ambient", 0) == 0)
            {
                std::filesystem::remove(entry.path(), errorCode);
            }
        }
    }

    // Archived cvars are written back when the fixture's engine shuts down, so a test that changes
    // them would silently rewrite the developer's own renderer and GI settings.
    struct FScopedSettings
    {
        explicit FScopedSettings(NextEngine& engine)
            : engine_(engine), settings_(engine.GetUserSettings()),
              rendererType_(engine.GetUserSettings().RendererType),
              cascadeCount_(engine.GetUserSettings().AmbientCubeCascadeCount),
              bakeTargetFps_(engine.GetUserSettings().AmbientCubeBakeTargetFps),
              diskCache_(engine.GetUserSettings().AmbientCubeDiskCache)
        {
        }

        ~FScopedSettings()
        {
            settings_.AmbientCubeCascadeCount = cascadeCount_;
            settings_.AmbientCubeBakeTargetFps = bakeTargetFps_;
            settings_.AmbientCubeDiskCache = diskCache_;
            engine_.RequestRendererType(static_cast<Vulkan::ERendererType>(rendererType_));
            settings_.RendererType = rendererType_;
        }

        NextEngine& engine_;
        Runtime::Config::UserSettings& settings_;
        int32_t rendererType_;
        int cascadeCount_;
        uint32_t bakeTargetFps_;
        bool diskCache_;
    };

    // The ambient cache is a cache: clearing it costs a re-bake, not data. The end-to-end test
    // needs a known-empty directory to detect the entry it produces, and clears again afterwards so
    // its one-cascade entry (which no normal run would ever hit) does not linger.
    struct FScopedEmptyAmbientCache
    {
        FScopedEmptyAmbientCache() { RemoveAmbientCacheFiles(); }
        ~FScopedEmptyAmbientCache() { RemoveAmbientCacheFiles(); }
    };

    struct FScopedCacheFile
    {
        explicit FScopedCacheFile(uint64_t key) : path(GetAmbientBakeCachePath(key)) { Remove(); }
        ~FScopedCacheFile() { Remove(); }

        void Remove() const
        {
            std::error_code ignored;
            std::filesystem::remove(path, ignored);
            std::filesystem::remove(path + ".tmp", ignored);
        }

        std::string path;
    };
}

TEST_CASE("Ambient bake cache round-trips a payload and rejects mismatches", "[Unit][AmbientBake]")
{
    constexpr uint64_t kKey = 0xA11CEB0BDEADBEEFull;
    const FScopedCacheFile cacheFile(kKey);
    const FAmbientBakeCachePayload written = MakeTinyPayload();

    REQUIRE(SaveAmbientBakeCache(kKey, written));
    REQUIRE(std::filesystem::exists(cacheFile.path));
    // The write goes through a .tmp rename, so a partial file is never left under the real key.
    CHECK_FALSE(std::filesystem::exists(cacheFile.path + ".tmp"));

    SECTION("A hit restores every section byte for byte")
    {
        FAmbientBakeCachePayload read;
        REQUIRE(LoadAmbientBakeCache(kKey, read));

        CHECK(read.cascadeCount == written.cascadeCount);
        CHECK(read.cascadeCapacity == written.cascadeCapacity);
        CHECK(read.voxelCountPerCascade == written.voxelCountPerCascade);
        CHECK(read.poolBricksPerCascade == written.poolBricksPerCascade);
        REQUIRE(read.voxels.size() == written.voxels.size());
        CHECK(std::memcmp(read.voxels.data(), written.voxels.data(),
                          written.voxels.size() * sizeof(Assets::VoxelData)) == 0);
        CHECK(read.brickTable == written.brickTable);
        CHECK(read.activeBrickList == written.activeBrickList);
        CHECK(read.activeBricksPerCascade == written.activeBricksPerCascade);
        REQUIRE(read.pages.size() == written.pages.size());
        CHECK(std::memcmp(read.pages.data(), written.pages.data(),
                          written.pages.size() * sizeof(Assets::PageIndex)) == 0);
        REQUIRE(read.cubes.size() == written.cubes.size());
        CHECK(std::memcmp(read.cubes.data(), written.cubes.data(),
                          written.cubes.size() * sizeof(Assets::AmbientCube)) == 0);
    }

    SECTION("A different key is a miss, not a wrong restore")
    {
        FAmbientBakeCachePayload read;
        CHECK_FALSE(LoadAmbientBakeCache(kKey ^ 1ull, read));
    }

    SECTION("A truncated file is a miss")
    {
        const auto size = std::filesystem::file_size(cacheFile.path);
        REQUIRE(size > 32);
        std::filesystem::resize_file(cacheFile.path, size / 2);

        FAmbientBakeCachePayload read;
        CHECK_FALSE(LoadAmbientBakeCache(kKey, read));
    }

    SECTION("A corrupt header is a miss")
    {
        std::fstream file(cacheFile.path, std::ios::binary | std::ios::in | std::ios::out);
        REQUIRE(file.is_open());
        const uint32_t garbage = 0xDEADC0DEu;
        file.write(reinterpret_cast<const char*>(&garbage), sizeof(garbage));
        file.close();

        FAmbientBakeCachePayload read;
        CHECK_FALSE(LoadAmbientBakeCache(kKey, read));
    }
}

TEST_CASE_METHOD(EngineTestFixture, "Ambient bake key tracks the scene's initial structure",
                 "[GPU][Integration][AmbientBake]")
{
    // Only GI-participating geometry is hashed, so the scene is not ready for this test until its
    // render components exist -- nodes alone can be the previous scene's.
    const auto findBakedNode = [](Assets::Scene& scene) -> Assets::Node*
    {
        for (auto* render : scene.Components<Runtime::RenderComponent>())
        {
            if (render->GetOwner() != nullptr && render->GetVisible() &&
                render->GetModelId() != static_cast<uint32_t>(-1) &&
                (render->GetRenderParticipationMask() &
                 (Runtime::RenderParticipation::giBake | Runtime::RenderParticipation::gpuAs)) != 0u)
            {
                return render->GetOwner();
            }
        }
        return nullptr;
    };

    engine_->RequestLoadScene({.filename = "assets/models/playground.glb"});
    Assets::Node* bakedNode = nullptr;
    for (int i = 0; i < 1200 && bakedNode == nullptr; ++i)
    {
        Simulate(1);
        bakedNode = findBakedNode(engine_->GetScene());
    }
    Simulate(5);

    Assets::Scene& scene = engine_->GetScene();
    REQUIRE(bakedNode != nullptr);

    FAmbientBakeKeyInputs inputs;
    inputs.baseUnit = Assets::CUBE_UNIT;
    inputs.cascadeCount = 3;
    inputs.cascadeRatio = 2.0f;
    inputs.poolBricksPerCascade = scene.AmbientPoolBricksPerCascade();

    const uint64_t baseline = ComputeAmbientBakeKey(scene, inputs);
    CHECK(baseline != 0u);
    // Same scene, same key: the key may not depend on anything transient.
    CHECK(ComputeAmbientBakeKey(scene, inputs) == baseline);

    SECTION("Grid parameters change the key")
    {
        FAmbientBakeKeyInputs other = inputs;
        other.cascadeCount = 2;
        CHECK(ComputeAmbientBakeKey(scene, other) != baseline);

        other = inputs;
        other.baseUnit = Assets::CUBE_UNIT * 2.0f;
        CHECK(ComputeAmbientBakeKey(scene, other) != baseline);

        other = inputs;
        other.hardwareBake = !inputs.hardwareBake;
        CHECK(ComputeAmbientBakeKey(scene, other) != baseline);
    }

    SECTION("Moving a node changes the key")
    {
        Assets::Node* node = bakedNode;
        const glm::vec3 original = node->Translation();
        node->SetTranslation(original + glm::vec3(0.0f, 3.0f, 0.0f));
        node->RecalcTransform(true);
        CHECK(ComputeAmbientBakeKey(scene, inputs) != baseline);

        node->SetTranslation(original);
        node->RecalcTransform(true);
        CHECK(ComputeAmbientBakeKey(scene, inputs) == baseline);
    }

    SECTION("Moving the sun changes the key")
    {
        Assets::EnvironmentSetting& env = scene.GetEnvSettings();
        const float originalRotation = env.SunRotation;
        env.SunRotation = originalRotation + 0.25f;
        CHECK(ComputeAmbientBakeKey(scene, inputs) != baseline);

        env.SunRotation = originalRotation;
        CHECK(ComputeAmbientBakeKey(scene, inputs) == baseline);
    }
}

// End to end: a cold load bakes and writes the cache, a second load of the same scene restores it
// and never starts a bake. One cascade keeps the runtime sane while still exercising every step.
TEST_CASE_METHOD(EngineTestFixture, "Ambient bake cache replaces the bake on the second load",
                 "[GPU][Integration][AmbientBake][Slow]")
{
    const FScopedEmptyAmbientCache cacheGuard;
    REQUIRE(CountAmbientCacheFiles() == 0);

    const FScopedSettings settingsGuard(*engine_);
    engine_->GetUserSettings().AmbientCubeCascadeCount = 1;
    engine_->GetUserSettings().AmbientCubeDiskCache = true;
    // The fixture ticks with a forced 1/30 s delta, so the bake's frame-time controller reads every
    // frame as "already too slow" for the default 60 FPS target and never grows past one dispatch
    // group per frame -- 442k frames to converge. A target below the forced delta lets it ramp.
    engine_->GetUserSettings().AmbientCubeBakeTargetFps = 1;
    engine_->RequestRendererType(Vulkan::ERT_SoftwareTracing);
    Simulate(5);
    REQUIRE(engine_->GetRenderer().ActiveRendererRequirements().requestAmbientCube);

    const auto waitFor = [this](const std::function<bool()>& predicate, int maxFrames)
    {
        for (int i = 0; i < maxFrames; ++i)
        {
            Simulate(1);
            if (predicate())
            {
                return true;
            }
            if (i % 500 == 499)
            {
                const Vulkan::FAmbientBakeProgress progress = engine_->GetRenderer().GetAmbientBakeProgress();
                SPDLOG_INFO("[test] frame {}: bake {} of {} groups", static_cast<uint32_t>(i + 1),
                            progress.completedDispatchGroups, progress.totalDispatchGroups);
            }
        }
        return false;
    };

    // ---- Cold: the full bake runs and its result is written out ----
    engine_->RequestLoadScene({.filename = "assets/models/playground.glb"});
    bool sawCacheSaveStage = false;
    REQUIRE(waitFor(
        [this, &sawCacheSaveStage]
        {
            sawCacheSaveStage =
                sawCacheSaveStage ||
                engine_->GetScene().GetCPUAccelerationStructure().GetProbeBakeProgress().stage ==
                    EProbeBakeStage::CacheSave;
            return CountAmbientCacheFiles() == 1;
        },
        6000));
    // The footer's bake indicator is driven by this stage: without it the seconds spent writing the
    // cache would read as "Complete" while a scene switch could still block on the write.
    CHECK(sawCacheSaveStage);

    Assets::CPU::FCPUAccelerationStructure& coldAcceleration =
        engine_->GetScene().GetCPUAccelerationStructure();
    CHECK(coldAcceleration.AmbientBakeDirtyBrickCount(0) == 0);

    // What landed on disk has to be the bake, not a zeroed arena: a readback at the wrong offset or
    // before the GPU finished would still produce a structurally valid file.
    uint64_t writtenKey = 0;
    REQUIRE(FindAmbientCacheKey(writtenKey));
    FAmbientBakeCachePayload storedPayload;
    REQUIRE(LoadAmbientBakeCache(writtenKey, storedPayload));
    REQUIRE(storedPayload.cascadeCount == 1);
    const size_t litCubes = static_cast<size_t>(std::count_if(
        storedPayload.cubes.begin(), storedPayload.cubes.end(),
        [](const Assets::AmbientCube& cube)
        { return (cube.PosY | cube.NegY | cube.PosX | cube.NegX | cube.PosZ | cube.NegZ |
                  cube.SunDirect | cube.SkyEmissiveDirect) != 0u; }));
    const size_t solidVoxels = static_cast<size_t>(std::count_if(
        storedPayload.voxels.begin(), storedPayload.voxels.end(),
        [](const Assets::VoxelData& voxel) { return voxel.matId != 0u; }));
    CHECK(solidVoxels > 0);
    CHECK(litCubes > storedPayload.cubes.size() / 100);

    // ---- Warm: the same scene comes back without a bake ----
    // The restore happens inside the scene commit, so no bake work may ever become visible: the
    // cold path above queued its voxel groups and reached a non-zero dirty revision well within
    // this window (the bake started ~1 s after the load).
    engine_->RequestLoadScene({.filename = "assets/models/playground.glb"});
    for (int i = 0; i < 600; ++i)
    {
        Simulate(1);
        const Assets::CPU::FCPUAccelerationStructure& warmAcceleration =
            engine_->GetScene().GetCPUAccelerationStructure();
        const FProbeBakeProgress probeProgress = warmAcceleration.GetProbeBakeProgress();
        REQUIRE(probeProgress.stage == EProbeBakeStage::Idle);
        REQUIRE(probeProgress.totalVoxelGroups == 0);
        REQUIRE(warmAcceleration.AmbientBakeDirtyRevision() == 0);
        REQUIRE_FALSE(engine_->GetRenderer().GetAmbientBakeProgress().active);
    }
    // A hit must not write a second entry.
    CHECK(CountAmbientCacheFiles() == 1);
}
