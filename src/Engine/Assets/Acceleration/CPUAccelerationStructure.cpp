#include "Engine/Assets/Acceleration/CPUAccelerationStructure.hpp"
#include "Engine/Assets/Acceleration/CPUAccelerationStructure.Internal.hpp"
#include "Engine/Assets/Acceleration/AmbientBakeCache.hpp"
#include "Engine/Options.hpp"
#include "Engine/Runtime/Subsystems/TaskCoordinator.hpp"
#include "Engine/Vulkan/MemoryAndShader.hpp"
#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Runtime/Components/RenderComponent.hpp"
#include "Engine/Runtime/Components/PhysicsComponent.hpp"
#include "Engine/Assets/GPU/TextureImage.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Assets/Core/Scene.hpp"

#include <atomic>
#include <chrono>
#include <climits>
#include <numeric>
#include <unordered_map>
#include <xxhash.h>

namespace Assets::CPU
{

namespace
{
    struct FWorldBounds
    {
        glm::vec3 min{FLT_MAX, FLT_MAX, FLT_MAX};
        glm::vec3 max{-FLT_MAX, -FLT_MAX, -FLT_MAX};
    };

    FWorldBounds ComputeWorldBounds(const Assets::Model& model, const glm::mat4& worldTransform)
    {
        const glm::vec3 localMin = model.GetLocalAABBMin();
        const glm::vec3 localMax = model.GetLocalAABBMax();

        glm::vec3 corners[8];
        corners[0] = glm::vec3(worldTransform * glm::vec4(localMin.x, localMin.y, localMin.z, 1.0f));
        corners[1] = glm::vec3(worldTransform * glm::vec4(localMax.x, localMin.y, localMin.z, 1.0f));
        corners[2] = glm::vec3(worldTransform * glm::vec4(localMin.x, localMax.y, localMin.z, 1.0f));
        corners[3] = glm::vec3(worldTransform * glm::vec4(localMax.x, localMax.y, localMin.z, 1.0f));
        corners[4] = glm::vec3(worldTransform * glm::vec4(localMin.x, localMin.y, localMax.z, 1.0f));
        corners[5] = glm::vec3(worldTransform * glm::vec4(localMax.x, localMin.y, localMax.z, 1.0f));
        corners[6] = glm::vec3(worldTransform * glm::vec4(localMin.x, localMax.y, localMax.z, 1.0f));
        corners[7] = glm::vec3(worldTransform * glm::vec4(localMax.x, localMax.y, localMax.z, 1.0f));

        FWorldBounds bounds;
        bounds.min = corners[0];
        bounds.max = corners[0];
        for (int i = 1; i < 8; ++i)
        {
            bounds.min = glm::min(bounds.min, corners[i]);
            bounds.max = glm::max(bounds.max, corners[i]);
        }
        return bounds;
    }

    bool BoundsNearlyEqual(const FWorldBounds& lhs, const FWorldBounds& rhs, float epsilon = 0.01f)
    {
        return glm::all(glm::lessThanEqual(glm::abs(lhs.min - rhs.min), glm::vec3(epsilon))) &&
               glm::all(glm::lessThanEqual(glm::abs(lhs.max - rhs.max), glm::vec3(epsilon)));
    }

    void AccumulateBounds(bool& hasBounds, glm::vec3& minBounds, glm::vec3& maxBounds, const FWorldBounds& bounds)
    {
        if (!hasBounds)
        {
            minBounds = bounds.min;
            maxBounds = bounds.max;
            hasBounds = true;
            return;
        }

        minBounds = glm::min(minBounds, bounds.min);
        maxBounds = glm::max(maxBounds, bounds.max);
    }

}

using namespace Assets;

FAmbientGridConfig FCPUAccelerationStructure::ResolveAmbientGridConfig(
    const Runtime::Config::UserSettings& settings, const Scene& scene) const
{
    FAmbientGridConfig config;
    config.baseUnit = SanitizeAmbientCubeUnit(settings.AmbientCubeUnit);
    config.offsetBias =
        vec3(settings.AmbientCubeOffsetX, settings.AmbientCubeOffsetY, settings.AmbientCubeOffsetZ);

    FAmbientCascadeLayoutInputs inputs;
    inputs.baseUnit = config.baseUnit;
    inputs.offsetBias = config.offsetBias;
    // Clamp to the GPU arena's allocated capacity so the per-cascade upload never writes out of bounds.
    inputs.cascadeCount = std::min(SanitizeAmbientCubeCascadeCount(settings.AmbientCubeCascadeCount),
                                   std::max(1u, scene.AmbientCubeCascadeCapacity()));
    inputs.cascadeRatio = SanitizeAmbientCubeCascadeRatio(settings.AmbientCubeCascadeRatio);
    inputs.followScene = settings.AmbientCubeFollow;

    // Box and focus come from the CPU snapshot rather than Scene's render AABB: it holds exactly the
    // geometry the voxelizer will see, and it is already current when the bakers are initialized
    // (the render AABB is computed later in the same rebuild).
    const SnapshotPtr snapshot = inputs.followScene ? AcquireSnapshot() : nullptr;
    if (snapshot)
    {
        for (const FCPUTLASInstanceInfo& info : snapshot->contexts)
        {
            inputs.sceneMin = inputs.hasSceneBounds ? glm::min(inputs.sceneMin, info.worldBoundsMin) : info.worldBoundsMin;
            inputs.sceneMax = inputs.hasSceneBounds ? glm::max(inputs.sceneMax, info.worldBoundsMax) : info.worldBoundsMax;
            inputs.hasSceneBounds = true;
        }
    }
    if (inputs.hasSceneBounds)
    {
        // Until the near cascades follow the camera they hold the ground at the middle of the map:
        // a block terrain's skirt can reach far below the surface, so the box floor is no anchor.
        const glm::vec3 center = 0.5f * (inputs.sceneMin + inputs.sceneMax);
        const float drop = inputs.sceneMax.y - inputs.sceneMin.y + 2.0f;
        const RayCastResult ground = RayCastInSnapshot(*snapshot, glm::vec3(center.x, inputs.sceneMax.y + 1.0f, center.z),
                                                       glm::vec3(0.0f, -1.0f, 0.0f), true, drop);
        if (ground.Hit)
        {
            inputs.hasFocus = true;
            inputs.focus = glm::vec3(center.x, ground.HitPoint.y, center.z);
        }
    }
    config.layout = ResolveAmbientCascadeLayout(inputs);
    return config;
}

bool FCPUAccelerationStructure::TryGetAmbientGridConfig(FAmbientGridConfig& outConfig) const
{
    if (!hasCommittedAmbientGrid_)
    {
        return false;
    }
    outConfig = committedAmbientGrid_;
    return true;
}

bool FCPUAccelerationStructure::InitCascadeBakers(const Runtime::Config::UserSettings& settings, const Scene& scene)
{
    const FAmbientGridConfig requested = ResolveAmbientGridConfig(settings, scene);
    const uint32_t cascadeCount = requested.layout.cascadeCount;

    bool needRebuild = cascadeBakers.size() != cascadeCount || !hasCommittedAmbientGrid_;
    for (uint32_t i = 0; !needRebuild && i < cascadeCount; ++i)
    {
        const FCPUProbeBaker& baker = cascadeBakers[i];
        needRebuild = glm::abs(requested.layout.units[i] - baker.UNIT_SIZE) > 1e-6f ||
                      glm::length(requested.layout.origins[i] - baker.CUBE_OFFSET) > 1e-6f ||
                      requested.layout.wraps[i] != baker.BRICK_WRAP;
    }

    if (!needRebuild)
    {
        return false;
    }

    cascadeBakers.clear();
    cascadeBakers.resize(cascadeCount);
    for (uint32_t i = 0; i < cascadeCount; ++i)
    {
        cascadeBakers[i].Init(i, requested.layout.units[i], requested.layout.origins[i], requested.layout.wraps[i]);
    }

    // Publish only after the bakers exist: shading reads this to stay in step with the data.
    committedAmbientGrid_ = requested;
    hasCommittedAmbientGrid_ = true;
    if (requested.layout.followScene)
    {
        SPDLOG_INFO("[AmbientGrid] fit to scene: {} cascades, ratio {:.3f}, cell {:.3f} .. {:.3f} m, "
                    "coarsest covers {:.0f} m",
                    cascadeCount, requested.layout.cascadeRatio, requested.layout.units[0],
                    requested.layout.units[cascadeCount - 1],
                    requested.layout.units[cascadeCount - 1] * static_cast<float>(CUBE_SIZE_XY));
        for (uint32_t i = 0; i < cascadeCount; ++i)
        {
            const glm::vec3 lo = requested.layout.origins[i];
            const glm::vec3 hi = lo + glm::vec3(CUBE_SIZE_XY, CUBE_SIZE_Z, CUBE_SIZE_XY) * requested.layout.units[i];
            SPDLOG_INFO("[AmbientGrid]   cascade {}: ({:.1f}, {:.1f}, {:.1f}) .. ({:.1f}, {:.1f}, {:.1f})",
                        i, lo.x, lo.y, lo.z, hi.x, hi.y, hi.z);
        }
    }

    return true;
}

void FCPUAccelerationStructure::InitBVH(Scene& scene)
{
    const auto timer = std::chrono::high_resolution_clock::now();

    CancelRuntimeBuilds();

    auto blasSet = std::make_shared<FCPUBLASSet>();
    blasSet->generation = ++blasGeneration_;
    blasSet->contexts.resize(scene.Models().size());
    blasSet->list.reserve(scene.Models().size());
    for ( size_t m = 0; m < scene.Models().size(); ++m )
    {
        const Model& model = scene.Models()[m];
        // A million-triangle model reallocates this array ~40 times without the hint, and the
        // copies dominate the whole BLAS pass.
        blasSet->contexts[m].triangles.reserve(model.CPUIndices().size());
        blasSet->contexts[m].extinfos.reserve(model.CPUIndices().size() / 3);
        for (size_t i = 0; i < model.CPUIndices().size(); i += 3)
        {
            // Get the three vertices of the triangle
            const Vertex& v0 = model.CPUVertices()[model.CPUIndices()[i]];
            const Vertex& v1 = model.CPUVertices()[model.CPUIndices()[i + 1]];
            const Vertex& v2 = model.CPUVertices()[model.CPUIndices()[i + 2]];
            
            // Calculate face normal
            vec3 edge1 = vec3(v1.Position) - vec3(v0.Position);
            vec3 edge2 = vec3(v2.Position) - vec3(v1.Position);
            vec3 normal = normalize(cross(edge1, edge2));
            
            // Add triangle vertices to BVH
            blasSet->contexts[m].triangles.push_back(tinybvh::bvhvec4(v0.Position.x, v0.Position.y, v0.Position.z, 0));
            blasSet->contexts[m].triangles.push_back(tinybvh::bvhvec4(v1.Position.x, v1.Position.y, v1.Position.z, 0));
            blasSet->contexts[m].triangles.push_back(tinybvh::bvhvec4(v2.Position.x, v2.Position.y, v2.Position.z, 0));

            // Store additional triangle information
            blasSet->contexts[m].extinfos.push_back({normal, v0.MaterialIndex});
        }

        // here we can cache the blas to disk if its big enough
        if (blasSet->contexts[m].triangles.size() > 16384 * 3)
        {
            XXH64_hash_t vhash = XXH64(blasSet->contexts[m].triangles.data(), blasSet->contexts[m].triangles.size() * sizeof(tinybvh::bvhvec4), 0);
            std::string cacheFileName = Utilities::CookHelper::GetCookedFileName(fmt::format("{:016x}", vhash), "cpubvh");

            if (!std::filesystem::exists(cacheFileName))
            {
                blasSet->contexts[m].bvh.Build(blasSet->contexts[m].triangles.data(), static_cast<int>(blasSet->contexts[m].triangles.size()) / 3);
                blasSet->contexts[m].bvh.Save(cacheFileName.c_str());
            }
            else
            {
                blasSet->contexts[m].bvh.Load(cacheFileName.c_str(), blasSet->contexts[m].triangles.data(), static_cast<int>(blasSet->contexts[m].triangles.size()) / 3);
            }
        }
        else
        {
            blasSet->contexts[m].bvh.Build(blasSet->contexts[m].triangles.data(), static_cast<int>(blasSet->contexts[m].triangles.size()) / 3);
        }

        blasSet->list.push_back(&blasSet->contexts[m].bvh);
    }

    blasSet_ = std::move(blasSet);
    
    UpdateBVH(scene);

    // After UpdateBVH: a fit-scene grid is resolved from the snapshot it publishes.
    const Runtime::Config::UserSettings& settings = NextEngine::GetInstance()->GetUserSettings();
    InitCascadeBakers(settings, scene);
}

void FCPUAccelerationStructure::UpdateBVH(Scene& scene)
{
    std::shared_ptr<FCPUTLASBuildInput> input = CaptureBuildInput(scene);
    std::shared_ptr<FCPUTLASBuildResult> result = BuildSnapshot(*input);
    PublishSnapshot(result->snapshot);
    MergeNavDirtyBounds(*result);
    publishedRevision_.store(result->snapshot->sceneRevision, std::memory_order_release);
    ++completedBuildCount_;
}

std::shared_ptr<FCPUTLASBuildInput> FCPUAccelerationStructure::CaptureBuildInput(Scene& scene)
{
    auto input = std::make_shared<FCPUTLASBuildInput>();
    const auto captureStartTime = std::chrono::steady_clock::now();
    input->epoch = buildEpoch_.load(std::memory_order_acquire);
    input->sceneRevision = ++sceneRevision_;
    input->requestTime = std::chrono::steady_clock::now();
    input->blasSet = blasSet_;
    input->previousSnapshot = AcquireSnapshot();

    auto materialTable = std::make_shared<FCPUMaterialTable>();
    materialTable->generation = ++materialGeneration_;
    materialTable->entries.reserve(scene.Materials().size());
    for (const FMaterial& material : scene.Materials())
    {
        materialTable->entries.push_back({material.gpuMaterial_.MaterialModel});
    }
    input->materialTable = std::move(materialTable);

    const auto renderComponents = scene.Components<Runtime::RenderComponent>();
    input->instances.reserve(renderComponents.size());
    input->contexts.reserve(renderComponents.size());
    for (auto* render : renderComponents)
    {
        Node* node = render->GetOwner();
        if (!node) continue;
        const uint32_t modelId = render->GetModelId();
        if (modelId == -1) continue;
        if (!render->GetVisible()) continue;
        const uint32_t participation = render->GetRenderParticipationMask();
        if ((participation & (Runtime::RenderParticipation::giBake | Runtime::RenderParticipation::gpuAs)) == 0u)
        {
            continue;
        }
        // GPU-only decorations (wind grass, camera presentation) have no CPU ray/nav/bake role.
        if (!render->GetRayCastVisible() && !render->GetReceiveGI()) continue;

        //node->RecalcTransform(true);
        const glm::mat4 nodeWorldTransform = node->WorldTransform();
        const FWorldBounds worldBounds = ComputeWorldBounds(scene.Models()[modelId], nodeWorldTransform);

        const mat4 worldTS = transpose(nodeWorldTransform);
        tinybvh::BLASInstance instance;
        instance.blasIdx = modelId;
        std::memcpy(instance.transform, &worldTS[0], sizeof(instance.transform));
        input->instances.emplace_back(std::move(instance));

        FCPUTLASInstanceInfo info;
        info.matIdxs.fill(0);
        info.nodeId = node->GetInstanceId();
        info.rayCastVisible = render->GetRayCastVisible();
        info.worldBoundsMin = worldBounds.min;
        info.worldBoundsMax = worldBounds.max;
        info.navRelevant = true;

        const auto& mats = render->GetMaterials();
        for (int i = 0; i < mats.size() && i < static_cast<int>(info.matIdxs.size()); ++i)
        {
            info.matIdxs[i] = mats[i];
        }
        input->contexts.emplace_back(std::move(info));
    }

    input->captureMilliseconds =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - captureStartTime).count();
    latestRequestedRevision_.store(input->sceneRevision, std::memory_order_release);
    return input;
}

std::shared_ptr<FCPUTLASBuildResult> FCPUAccelerationStructure::BuildSnapshot(FCPUTLASBuildInput& input)
{
    auto result = std::make_shared<FCPUTLASBuildResult>();
    result->epoch = input.epoch;
    result->requestTime = input.requestTime;
    result->buildStartTime = std::chrono::steady_clock::now();

    auto snapshot = std::make_shared<FCPUTLASSnapshot>();
    snapshot->sceneRevision = input.sceneRevision;
    snapshot->blasSet = input.blasSet;
    snapshot->materialTable = input.materialTable;
    snapshot->instances = std::move(input.instances);
    snapshot->contexts = std::move(input.contexts);

    std::unordered_map<uint32_t, FWorldBounds> previousNavBounds;
    std::unordered_map<uint32_t, FWorldBounds> currentNavBounds;
    if (input.previousSnapshot)
    {
        previousNavBounds.reserve(input.previousSnapshot->contexts.size());
        for (const FCPUTLASInstanceInfo& previousInfo : input.previousSnapshot->contexts)
        {
            if (previousInfo.navRelevant)
            {
                previousNavBounds[previousInfo.nodeId] = {previousInfo.worldBoundsMin, previousInfo.worldBoundsMax};
            }
        }
    }
    currentNavBounds.reserve(snapshot->contexts.size());
    for (const FCPUTLASInstanceInfo& info : snapshot->contexts)
    {
        if (info.navRelevant)
        {
            currentNavBounds[info.nodeId] = {info.worldBoundsMin, info.worldBoundsMax};
        }
    }

    for (const auto& [nodeId, currentBounds] : currentNavBounds)
    {
        const auto previousIt = previousNavBounds.find(nodeId);
        if (previousIt == previousNavBounds.end())
        {
            AccumulateBounds(result->hasNavDirtyBounds, result->navDirtyWorldMin, result->navDirtyWorldMax, currentBounds);
            continue;
        }

        if (!BoundsNearlyEqual(currentBounds, previousIt->second))
        {
            AccumulateBounds(result->hasNavDirtyBounds, result->navDirtyWorldMin, result->navDirtyWorldMax, currentBounds);
            AccumulateBounds(result->hasNavDirtyBounds, result->navDirtyWorldMin, result->navDirtyWorldMax, previousIt->second);
        }
    }

    for (const auto& [nodeId, previousBounds] : previousNavBounds)
    {
        if (currentNavBounds.find(nodeId) == currentNavBounds.end())
        {
            AccumulateBounds(result->hasNavDirtyBounds, result->navDirtyWorldMin, result->navDirtyWorldMax, previousBounds);
        }
    }

    if (!snapshot->instances.empty() && snapshot->blasSet && !snapshot->blasSet->list.empty())
    {
        // tinybvh 1.3.8 does not const-qualify this non-owning BLAS pointer array,
        // although TLAS Build only reads the array and the BLAS objects.
        snapshot->tlas.Build(snapshot->instances.data(), static_cast<uint32_t>(snapshot->instances.size()),
                             const_cast<tinybvh::BVHBase**>(snapshot->blasSet->list.data()),
                             static_cast<uint32_t>(snapshot->blasSet->list.size()));
    }

    result->snapshot = std::move(snapshot);
    result->buildEndTime = std::chrono::steady_clock::now();
    return result;
}

void FCPUAccelerationStructure::MergeNavDirtyBounds(const FCPUTLASBuildResult& result)
{
    if (result.hasNavDirtyBounds)
    {
        if (!hasNavRelevantDirtyBounds_)
        {
            navRelevantDirtyWorldMin_ = result.navDirtyWorldMin;
            navRelevantDirtyWorldMax_ = result.navDirtyWorldMax;
            hasNavRelevantDirtyBounds_ = true;
        }
        else
        {
            navRelevantDirtyWorldMin_ = glm::min(navRelevantDirtyWorldMin_, result.navDirtyWorldMin);
            navRelevantDirtyWorldMax_ = glm::max(navRelevantDirtyWorldMax_, result.navDirtyWorldMax);
        }
    }
}

void FCPUAccelerationStructure::RebuildBVHOnly(Scene& scene)
{
    RequestRuntimeBuild(scene);
}

RayCastResult FCPUAccelerationStructure::RayCastInCPU(vec3 rayOrigin, vec3 rayDir)
{
    const SnapshotPtr snapshot = AcquireSnapshot();
    return snapshot ? RayCastInSnapshot(*snapshot, rayOrigin, rayDir) : RayCastResult{};
}

RayCastResult FCPUAccelerationStructure::RayCastInSnapshot(const FCPUTLASSnapshot& snapshot, vec3 rayOrigin, vec3 rayDir,
                                                          bool faceNormalTowardRay, float maxDistance)
{
    RayCastResult result {};
    if (!snapshot.instances.empty())
    {
        constexpr float skipEpsilon = 1e-3f;
        float accumulatedT = 0.0f;
        glm::vec3 currentOrigin = rayOrigin;
        for (uint32_t iteration = 0; iteration < 16 && accumulatedT < maxDistance; ++iteration)
        {
            const float remainingDistance = maxDistance - accumulatedT;
            tinybvh::Ray ray(tinybvh::bvhvec3(currentOrigin.x, currentOrigin.y, currentOrigin.z),
                             tinybvh::bvhvec3(rayDir.x, rayDir.y, rayDir.z), remainingDistance);
            snapshot.tlas.Intersect(ray);
            if (ray.hit.t >= remainingDistance)
            {
                break;
            }

            uint32_t primIdx = ray.hit.prim;
            if (!snapshot.blasSet || ray.hit.inst >= snapshot.instances.size() || ray.hit.inst >= snapshot.contexts.size())
            {
                break;
            }
            const tinybvh::BLASInstance& instance = snapshot.instances[ray.hit.inst];
            const FCPUTLASInstanceInfo& instContext = snapshot.contexts[ray.hit.inst];
            const float globalT = accumulatedT + ray.hit.t;
            vec3 hitPos = rayOrigin + rayDir * globalT;
            if (!instContext.rayCastVisible)
            {
                accumulatedT = globalT + skipEpsilon;
                currentOrigin = rayOrigin + rayDir * accumulatedT;
                continue;
            }

            if (instance.blasIdx >= snapshot.blasSet->contexts.size())
            {
                break;
            }
            const FCPUBLASContext& context = snapshot.blasSet->contexts[instance.blasIdx];
            if (primIdx >= context.extinfos.size())
            {
                break;
            }
            mat4* worldTS = (mat4*)instance.transform;
            vec4 normalWS = vec4( context.extinfos[primIdx].normal, 0.0f) * *worldTS;
            // Ensure normal faces toward the ray origin (flip if we hit a back face)
            if (faceNormalTowardRay && glm::dot(glm::vec3(normalWS), rayDir) > 0.0f)
                normalWS = -normalWS;
            result.HitPoint = vec4(hitPos, 0);
            result.Normal = normalWS;
            result.Hit = true;
            result.T = globalT;
            result.InstanceId = instContext.nodeId;
            result.MaterialId = FetchMaterialId(snapshot, context.extinfos[primIdx].matIdx, ray.hit.inst);
            break;
        }
    }

    return result;
}

FCPUAccelerationStructure::SnapshotPtr FCPUAccelerationStructure::AcquireSnapshot() const
{
#if defined(__cpp_lib_atomic_shared_ptr) && __cpp_lib_atomic_shared_ptr >= 201711L
    return activeSnapshot_.load(std::memory_order_acquire);
#else
    std::lock_guard<std::mutex> lock(snapshotMutex_);
    return activeSnapshot_;
#endif
}

void FCPUAccelerationStructure::PublishSnapshot(SnapshotPtr snapshot)
{
#if defined(__cpp_lib_atomic_shared_ptr) && __cpp_lib_atomic_shared_ptr >= 201711L
    activeSnapshot_.store(std::move(snapshot), std::memory_order_release);
#else
    std::lock_guard<std::mutex> lock(snapshotMutex_);
    activeSnapshot_ = std::move(snapshot);
#endif
}

uint64_t FCPUAccelerationStructure::RequestRuntimeBuild(Scene& scene)
{
    return QueueBuildInput(CaptureBuildInput(scene));
}

uint64_t FCPUAccelerationStructure::QueueBuildInput(std::shared_ptr<FCPUTLASBuildInput> input)
{
    const uint64_t requestedRevision = input->sceneRevision;
    latestRequestedRevision_.store(requestedRevision, std::memory_order_release);
    bool shouldStartBuild = false;
    {
        std::lock_guard<std::mutex> lock(buildMutex_);
        lastCaptureMilliseconds_ = input->captureMilliseconds;
        if (buildInFlight_)
        {
            latestBuildInput_ = std::move(input);
            ++coalescedRequestCount_;
        }
        else
        {
            buildInFlight_ = true;
            shouldStartBuild = true;
        }
    }

    if (shouldStartBuild)
    {
        StartBuild(std::move(input));
    }
    return requestedRevision;
}

void FCPUAccelerationStructure::StartBuild(std::shared_ptr<FCPUTLASBuildInput> input)
{
    Tasks::TaskCoordinator::GetInstance()->AddNamedTask(
        Tasks::ENamedTaskThread::CPU_AS_BUILD,
        [this, input = std::move(input)](Tasks::ResTask& task) mutable
        {
            std::shared_ptr<FCPUTLASBuildResult> result = BuildSnapshot(*input);
            std::lock_guard<std::mutex> lock(buildMutex_);
            completedBuildResult_ = std::move(result);
        },
        {},
        "CPU TLAS build");
}

void FCPUAccelerationStructure::PollBVHBuild()
{
    std::shared_ptr<FCPUTLASBuildResult> result;
    std::shared_ptr<FCPUTLASBuildInput> nextInput;
    {
        std::lock_guard<std::mutex> lock(buildMutex_);
        if (!completedBuildResult_)
        {
            return;
        }

        result = std::move(completedBuildResult_);
        nextInput = std::move(latestBuildInput_);
        buildInFlight_ = nextInput != nullptr;
    }

    const uint64_t activeEpoch = buildEpoch_.load(std::memory_order_acquire);
    const bool generationMatches = result->snapshot && result->snapshot->blasSet && blasSet_ &&
                                   result->snapshot->blasSet->generation == blasSet_->generation;
    if (result->epoch == activeEpoch && generationMatches)
    {
        const auto publishTime = std::chrono::steady_clock::now();
        PublishSnapshot(result->snapshot);
        MergeNavDirtyBounds(*result);
        publishedRevision_.store(result->snapshot->sceneRevision, std::memory_order_release);
        {
            std::lock_guard<std::mutex> lock(buildMutex_);
            ++completedBuildCount_;
            lastBuildMilliseconds_ =
                std::chrono::duration<double, std::milli>(result->buildEndTime - result->buildStartTime).count();
            lastBuildToPublishMilliseconds_ =
                std::chrono::duration<double, std::milli>(publishTime - result->requestTime).count();
            buildToPublishSamples_.push_back(lastBuildToPublishMilliseconds_);
            constexpr size_t maxBuildTimingSamples = 256;
            if (buildToPublishSamples_.size() > maxBuildTimingSamples)
            {
                buildToPublishSamples_.erase(buildToPublishSamples_.begin());
            }
        }
    }

    if (nextInput)
    {
        StartBuild(std::move(nextInput));
    }
}

void FCPUAccelerationStructure::CancelRuntimeBuilds()
{
    buildEpoch_.fetch_add(1, std::memory_order_acq_rel);
    Tasks::TaskCoordinator::GetInstance()->WaitForNamedTask(Tasks::ENamedTaskThread::CPU_AS_BUILD);
    std::lock_guard<std::mutex> lock(buildMutex_);
    buildInFlight_ = false;
    latestBuildInput_.reset();
    completedBuildResult_.reset();
}

FCPUTLASBuildStats FCPUAccelerationStructure::GetBuildStats() const
{
    std::lock_guard<std::mutex> lock(buildMutex_);
    FCPUTLASBuildStats stats;
    stats.publishedRevision = publishedRevision_.load(std::memory_order_acquire);
    stats.latestRequestedRevision = latestRequestedRevision_.load(std::memory_order_acquire);
    stats.coalescedRequestCount = coalescedRequestCount_;
    stats.completedBuildCount = completedBuildCount_;
    stats.snapshotStaleness = stats.latestRequestedRevision > stats.publishedRevision
                                  ? stats.latestRequestedRevision - stats.publishedRevision
                                  : 0;
    stats.lastBuildMilliseconds = lastBuildMilliseconds_;
    stats.lastCaptureMilliseconds = lastCaptureMilliseconds_;
    stats.lastBuildToPublishMilliseconds = lastBuildToPublishMilliseconds_;
    if (!buildToPublishSamples_.empty())
    {
        std::vector<double> sortedSamples = buildToPublishSamples_;
        std::sort(sortedSamples.begin(), sortedSamples.end());
        const size_t p95Index = static_cast<size_t>(std::ceil(sortedSamples.size() * 0.95)) - 1;
        stats.buildToPublishP95Milliseconds = sortedSamples[p95Index];
    }
    return stats;
}

bool FCPUAccelerationStructure::AsyncProcessFull(Assets::Scene& scene, Vulkan::DeviceMemory* voxelGpuMemory)
{
    if ( !Tasks::TaskCoordinator::GetInstance()->IsAllParralledTaskComplete() )
    {
        return false;
    }
    // Drop whatever job the previous bake left behind: its bricks belong to the old grid.
    voxelJobStage_ = EVoxelJobStage::Idle;
    voxelJobTasks_.clear();
    voxelJobBricks_.clear();
    voxelJobOutputBoxes_.clear();

    const Runtime::Config::UserSettings& settings = NextEngine::GetInstance()->GetUserSettings();
    InitCascadeBakers(settings, scene);

    ambientCacheKey_ = 0;
    ambientCacheWriteArmed_ = false;
    ambientCachePendingSave_ = false;
    ambientCacheSaveFrame_ = 0;
    ambientCacheSaveInFlight_.store(false, std::memory_order_release);
    if (voxelGpuMemory != nullptr && IsAmbientBakeDiskCacheEnabled())
    {
        FAmbientBakeKeyInputs keyInputs;
        keyInputs.baseUnit = committedAmbientGrid_.baseUnit;
        keyInputs.offsetBias = committedAmbientGrid_.offsetBias;
        keyInputs.cascadeCount = GetActiveCascadeCount();
        keyInputs.cascadeRatio = committedAmbientGrid_.layout.cascadeRatio;
        keyInputs.cascadeUnits = committedAmbientGrid_.layout.units;
        keyInputs.cascadeOrigins = committedAmbientGrid_.layout.origins;
        keyInputs.poolBricksPerCascade = scene.AmbientPoolBricksPerCascade();
        // Must match VulkanBaseRenderer::PostRender: the two bake pipelines do not produce
        // identical cubes, so a cache written by one must never be read back by the other.
        keyInputs.hardwareBake =
            NextEngine::GetInstance()->GetRenderer().SupportsRayTracing() && !GOption->ForceSoftGen;
        ambientCacheKey_ = ComputeAmbientBakeKey(scene, keyInputs);

        if (ambientCacheKey_ != 0 && TryRestoreAmbientBakeCache(scene, voxelGpuMemory, ambientCacheKey_))
        {
            return true;
        }
        ambientCacheWriteArmed_ = ambientCacheKey_ != 0;
    }

    // A full bake is every brick of every cascade entering the voxel pipeline at once.
    voxelJobBricks_.assign(GetActiveCascadeCount(), {});
    for (uint32_t cascadeIndex = 0; cascadeIndex < GetActiveCascadeCount(); ++cascadeIndex)
    {
        FCPUProbeBaker& baker = cascadeBakers[cascadeIndex];
        baker.ClearVoxels();
        baker.UploadGPU(*voxelGpuMemory, scene.AmbientVoxelsByteOffset(), cascadeIndex * kCascadeVoxelCount);
        std::vector<uint32_t>& bricks = voxelJobBricks_[cascadeIndex];
        bricks.resize(GPU_SCENE_AMBIENT_BRICKS_PER_CASCADE);
        std::iota(bricks.begin(), bricks.end(), 0u);
    }
    totalVoxelTasks_ = 0;
    completedVoxelTasks_.store(0, std::memory_order_release);
    fullProbeBakePending_ = true;
    ambientBakeIdle_ = false;
    ambientBakeActiveLogged_ = false;

    return true;
}

bool FCPUAccelerationStructure::TryRestoreAmbientBakeCache(Scene& scene, Vulkan::DeviceMemory* arenaMemory,
                                                           uint64_t key)
{
    const uint32_t cascadeCount = GetActiveCascadeCount();
    const uint32_t cascadeCapacity = scene.AmbientCubeCascadeCapacity();
    const uint32_t poolBricksPerCascade = scene.AmbientPoolBricksPerCascade();
    if (cascadeCount == 0u || cascadeCapacity < cascadeCount)
    {
        return false;
    }

    const auto restoreStart = std::chrono::high_resolution_clock::now();
    FAmbientBakeCachePayload payload;
    if (!LoadAmbientBakeCache(key, payload))
    {
        return false;
    }
    bool windowsMatch = payload.cascadeGrids.size() == cascadeCount && payload.cascadeWraps.size() == cascadeCount;
    for (uint32_t cascadeIndex = 0; windowsMatch && cascadeIndex < cascadeCount; ++cascadeIndex)
    {
        const glm::vec4 grid(committedAmbientGrid_.layout.origins[cascadeIndex],
                             committedAmbientGrid_.layout.units[cascadeIndex]);
        windowsMatch = glm::all(glm::lessThanEqual(glm::abs(payload.cascadeGrids[cascadeIndex] - grid), glm::vec4(1e-4f))) &&
                       glm::ivec3(payload.cascadeWraps[cascadeIndex]) == committedAmbientGrid_.layout.wraps[cascadeIndex];
    }
    if (!windowsMatch || payload.cascadeCount != cascadeCount || payload.cascadeCapacity != cascadeCapacity ||
        payload.voxelCountPerCascade != kCascadeVoxelCount ||
        payload.poolBricksPerCascade != poolBricksPerCascade)
    {
        SPDLOG_INFO("Ambient bake cache {:016x} ignored: grid does not match the current scene", key);
        return false;
    }

    // CPU voxels. The distance-field seed is a pure function of occupancy, so it is rebuilt here
    // rather than stored.
    for (uint32_t cascadeIndex = 0; cascadeIndex < cascadeCount; ++cascadeIndex)
    {
        FCPUProbeBaker& baker = cascadeBakers[cascadeIndex];
        const size_t voxelBase = static_cast<size_t>(cascadeIndex) * kCascadeVoxelCount;
        std::memcpy(baker.voxels.data(), payload.voxels.data() + voxelBase,
                    static_cast<size_t>(kCascadeVoxelCount) * sizeof(VoxelData));
        for (size_t voxelIndex = 0; voxelIndex < baker.voxels.size(); ++voxelIndex)
        {
            baker.distanceToSolidSeeds[voxelIndex] =
                baker.voxels[voxelIndex].matId > 0 ? uint8_t{0} : kMaxDistanceFieldSeed;
        }
        baker.UploadGPU(*arenaMemory, scene.AmbientVoxelsByteOffset(), cascadeIndex * kCascadeVoxelCount);
    }

    // The brick table has to come back verbatim: the cube pool below is indexed through it, so a
    // recomputed slot assignment would scatter the restored lighting.
    cpuBrickTable = {};
    cpuBrickTable.brickTable = std::move(payload.brickTable);
    cpuBrickTable.activeBricksPerCascade = std::move(payload.activeBricksPerCascade);
    cpuBrickTable.candidateBricksPerCascade.assign(cascadeCapacity, 0u);
    cpuBrickTable.overflowBricksPerCascade.assign(cascadeCapacity, 0u);
    cpuBrickTable.activeBricksLastBuild = static_cast<uint32_t>(std::count_if(
        cpuBrickTable.brickTable.begin(), cpuBrickTable.brickTable.end(),
        [](uint32_t slot) { return slot != Assets::GPU_SCENE_AMBIENT_BRICK_INVALID; }));
    // Nothing left to bake.
    cpuBrickTable.MarkConverged();
    ++brickTableGeneration_;
    cpuBrickTable.UploadGPU(*arenaMemory, scene.AmbientBrickTableByteOffset());
    scene.SetAmbientActiveBrickCounts(cpuBrickTable.activeBricksPerCascade);

    const size_t cubeBytes = payload.cubes.size() * sizeof(AmbientCube);
    void* mapped = arenaMemory->Map(static_cast<VkDeviceSize>(scene.AmbientCubesByteOffset()),
                                    static_cast<VkDeviceSize>(cubeBytes));
    std::memcpy(mapped, payload.cubes.data(), cubeBytes);
    arenaMemory->Unmap();

    // Land in exactly the state a cold bake ends in once it converges.
    fullProbeBakePending_ = false;
    ambientBakeIdle_ = true;
    voxelJobStage_ = EVoxelJobStage::Idle;
    voxelJobBricks_.clear();
    voxelJobTasks_.clear();
    totalVoxelTasks_ = 0;
    completedVoxelTasks_.store(0, std::memory_order_release);
    scene.MarkAmbientCacheRestored();

    const double elapsedMs =
        std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - restoreStart).count();
    SPDLOG_INFO("Ambient bake cache hit {:016x}: restored {} cascades, {} active bricks in {:.2f}ms",
                key, cascadeCount, cpuBrickTable.activeBricksLastBuild, elapsedMs);
    return true;
}

void FCPUAccelerationStructure::FlushPendingAmbientBakeCacheSave(Scene& scene, Vulkan::DeviceMemory* arenaMemory)
{
    if (!ambientCachePendingSave_ ||
        NextEngine::GetInstance()->GetTotalFrames() < ambientCacheSaveFrame_)
    {
        return;
    }
    ambientCachePendingSave_ = false;
    ambientCacheWriteArmed_ = false;

    if (arenaMemory == nullptr || ambientCacheKey_ == 0 || cascadeBakers.empty() ||
        cpuBrickTable.brickTable.empty() || !IsAmbientBakeDiskCacheEnabled())
    {
        return;
    }

    const uint32_t cascadeCount = GetActiveCascadeCount();
    const uint32_t cascadeCapacity = scene.AmbientCubeCascadeCapacity();
    const uint32_t poolBricksPerCascade = scene.AmbientPoolBricksPerCascade();
    if (cpuBrickTable.brickTable.size() !=
        static_cast<size_t>(cascadeCapacity) * Assets::GPU_SCENE_AMBIENT_BRICKS_PER_CASCADE)
    {
        return;
    }

    const size_t cubeCount = static_cast<size_t>(cascadeCapacity) * poolBricksPerCascade *
                             Assets::GPU_SCENE_AMBIENT_BRICK_VOLUME;
    const size_t cubesByteOffset = scene.AmbientCubesByteOffset();
    const uint64_t key = ambientCacheKey_;

    // Everything below runs on a worker: the cube pool lives in device-local host-visible memory,
    // where a host read runs at BAR speed (~1 s for one cascade), and doing it here would stall the
    // game thread for a whole second right after the bake finished.
    //
    // Reading the bake state off-thread is safe because the ambient pipeline is idle: convergence
    // set ambientBakeIdle_, the voxel job is empty and the bake queue has nothing left, so neither
    // cascadeBakers nor cpuBrickTable is written again. The only writer that could return is a new
    // full bake, and both paths into one (Scene::CleanUp and ~Scene) call ClearAllTasks(), which
    // waits for this task before any of that state -- or the arena itself -- goes away.
    ambientCacheSaveInFlight_.store(true, std::memory_order_release);
    Tasks::TaskCoordinator::GetInstance()->AddParralledTask(
        [this, key, arenaMemory, cubesByteOffset, cubeCount, cascadeCount, cascadeCapacity,
         poolBricksPerCascade](Tasks::ResTask& task)
        {
            const auto readStart = std::chrono::high_resolution_clock::now();

            FAmbientBakeCachePayload payload;
            payload.cascadeCount = cascadeCount;
            payload.cascadeCapacity = cascadeCapacity;
            payload.voxelCountPerCascade = kCascadeVoxelCount;
            payload.poolBricksPerCascade = poolBricksPerCascade;
            payload.voxels.resize(static_cast<size_t>(cascadeCount) * kCascadeVoxelCount);
            for (uint32_t cascadeIndex = 0; cascadeIndex < cascadeCount; ++cascadeIndex)
            {
                std::memcpy(payload.voxels.data() + static_cast<size_t>(cascadeIndex) * kCascadeVoxelCount,
                            cascadeBakers[cascadeIndex].voxels.data(),
                            static_cast<size_t>(kCascadeVoxelCount) * sizeof(VoxelData));
                payload.cascadeGrids.emplace_back(committedAmbientGrid_.layout.origins[cascadeIndex],
                                                  committedAmbientGrid_.layout.units[cascadeIndex]);
                payload.cascadeWraps.emplace_back(committedAmbientGrid_.layout.wraps[cascadeIndex], 0);
            }
            payload.brickTable = cpuBrickTable.brickTable;
            payload.activeBricksPerCascade = cpuBrickTable.activeBricksPerCascade;

            payload.cubes.resize(cubeCount);
            const size_t cubeBytes = cubeCount * sizeof(AmbientCube);
            const void* mapped = arenaMemory->Map(static_cast<VkDeviceSize>(cubesByteOffset),
                                                  static_cast<VkDeviceSize>(cubeBytes));
            std::memcpy(payload.cubes.data(), mapped, cubeBytes);
            arenaMemory->Unmap();

            const auto writeStart = std::chrono::high_resolution_clock::now();
            const double readbackMs =
                std::chrono::duration<double, std::milli>(writeStart - readStart).count();
            const bool saved = SaveAmbientBakeCache(key, payload);
            const double writeMs =
                std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - writeStart)
                    .count();
            if (saved)
            {
                SPDLOG_INFO("Ambient bake cache written {:016x} (readback {:.0f}ms, compress+write {:.0f}ms, "
                            "both off the game thread)",
                            key, readbackMs, writeMs);
            }
            else
            {
                SPDLOG_WARN("Ambient bake cache write failed for {:016x}", key);
            }
            ambientCacheSaveInFlight_.store(false, std::memory_order_release);
        },
        nullptr,
        "Ambient bake cache write");
}

void FCPUAccelerationStructure::StartVoxelJob()
{
    // Voxelization reads the CPU snapshot; without one there is no geometry, and the job still runs
    // to the end so the cleared voxels get their distance field and the table is rebuilt.
    const SnapshotPtr snapshot = AcquireSnapshot();
    const bool hasGeometry = snapshot && !snapshot->instances.empty();

    // A handful of bricks per task: each is 512 cells of six short rays, small enough to spread a
    // cascade over every worker and large enough that task overhead stays out of the profile.
    constexpr size_t kBricksPerTask = 6;
    voxelJobTasks_.clear();
    totalVoxelTasks_ = 0;
    completedVoxelTasks_.store(0, std::memory_order_release);
    for (uint32_t cascadeIndex = 0; cascadeIndex < voxelJobBricks_.size() && cascadeIndex < GetActiveCascadeCount();
         ++cascadeIndex)
    {
        const std::vector<uint32_t>& bricks = voxelJobBricks_[cascadeIndex];
        for (size_t first = 0; first < bricks.size(); first += kBricksPerTask)
        {
            std::vector<uint32_t> taskBricks(bricks.begin() + static_cast<std::ptrdiff_t>(first),
                                             bricks.begin() + static_cast<std::ptrdiff_t>(
                                                 std::min(bricks.size(), first + kBricksPerTask)));
            ++totalVoxelTasks_;
            const uint32_t taskId = Tasks::TaskCoordinator::GetInstance()->AddParralledTask(
                [this, snapshot, hasGeometry, cascadeIndex, taskBricks = std::move(taskBricks)](Tasks::ResTask& task)
                {
                    if (!hasGeometry)
                    {
                        return;
                    }
                    FCPUProbeBaker& baker = cascadeBakers[cascadeIndex];
                    for (uint32_t brick : taskBricks)
                    {
                        baker.VoxelizeBrick(brick, *snapshot);
                    }
                },
                [this](Tasks::ResTask& task)
                {
                    completedVoxelTasks_.fetch_add(1, std::memory_order_release);
                },
                "Ambient voxelize bricks");
            voxelJobTasks_.push_back(taskId);
        }
    }
    voxelJobStage_ = EVoxelJobStage::Voxelizing;
}

void FCPUAccelerationStructure::FinishVoxelJob(Scene& scene, Vulkan::DeviceMemory& arenaMemory)
{
    // Upload every brick whose voxels or distance field the job rewrote.
    for (uint32_t cascadeIndex = 0; cascadeIndex < voxelJobOutputBoxes_.size() && cascadeIndex < GetActiveCascadeCount();
         ++cascadeIndex)
    {
        if (voxelJobBricks_[cascadeIndex].empty())
        {
            continue;
        }
        FCPUProbeBaker& baker = cascadeBakers[cascadeIndex];
        const auto& [outMin, outMax] = voxelJobOutputBoxes_[cascadeIndex];
        baker.UploadBricks(arenaMemory, scene.AmbientVoxelsByteOffset(), cascadeIndex * kCascadeVoxelCount,
                           baker.StorageBricksInBox(outMin, outMax));
    }

    // Reclassify and allocate. Slots that changed owner are zeroed so the new owner never shades
    // with the old one's radiance; their zero sample count keeps them unpublished until rebaked.
    cpuBrickTable.UpdateData(cascadeBakers, scene.AmbientCubeCascadeCapacity(), scene.AmbientPoolBricksPerCascade(),
                             ambientFocus_);
    if (!cpuBrickTable.slotsToClear.empty())
    {
        void* mapped = arenaMemory.Map(scene.AmbientCubesByteOffset(), scene.AmbientVoxelsByteOffset());
        auto* cubeBytes = static_cast<std::byte*>(mapped);
        constexpr size_t brickBytes =
            static_cast<size_t>(Assets::GPU_SCENE_AMBIENT_BRICK_VOLUME) * sizeof(Assets::AmbientCube);
        for (uint32_t globalSlot : cpuBrickTable.slotsToClear)
        {
            std::memset(cubeBytes + static_cast<size_t>(globalSlot) * brickBytes, 0, brickBytes);
        }
        arenaMemory.Unmap();
    }
    if (fullProbeBakePending_)
    {
        // The whole grid was rebuilt: every allocated brick bakes from scratch.
        cpuBrickTable.ResetBakePasses();
        fullProbeBakePending_ = false;
    }
    cpuBrickTable.UploadGPU(arenaMemory, scene.AmbientBrickTableByteOffset());
    scene.SetAmbientActiveBrickCounts(cpuBrickTable.activeBricksPerCascade);
    ++brickTableGeneration_;

    voxelJobBricks_.clear();
    voxelJobOutputBoxes_.clear();
    voxelJobTasks_.clear();
    voxelJobStage_ = EVoxelJobStage::Idle;
}

void FCPUAccelerationStructure::ClearAllTasks()
{
    CancelRuntimeBuilds();

    // Discard queued voxelization work before waiting. A full ambient-cube bake
    // contains many independent tasks; dispatching its entire queue here makes
    // LoadScene wait for obsolete work from the outgoing scene.
    Tasks::TaskCoordinator::GetInstance()->CancelAllParralledTasks();

    // Wait for running tasks and their main-thread completion callbacks before
    // clearing the state those callbacks capture.
    Tasks::TaskCoordinator::GetInstance()->WaitForAllParralledTask();

    voxelJobStage_ = EVoxelJobStage::Idle;
    voxelJobBricks_.clear();
    voxelJobOutputBoxes_.clear();
    voxelJobTasks_.clear();
    totalVoxelTasks_ = 0;
    completedVoxelTasks_.store(0, std::memory_order_release);

    fullProbeBakePending_ = true;
    ambientBakeIdle_ = false;
    ambientBakeActiveLogged_ = false;
    ambientCacheKey_ = 0;
    ambientCacheWriteArmed_ = false;
    ambientCachePendingSave_ = false;
    ambientCacheSaveFrame_ = 0;
    ambientCacheSaveInFlight_.store(false, std::memory_order_release);
    cpuBrickTable = {};
    ++brickTableGeneration_;
    ClearNavRelevantDirtyBounds();
    PublishSnapshot(std::make_shared<FCPUTLASSnapshot>());
    blasSet_.reset();
}

glm::vec3 FCPUAccelerationStructure::ResolveAmbientFocus(const Scene& scene) const
{
    return glm::vec3(glm::inverse(scene.GetRenderCamera().ModelView)[3]);
}

bool FCPUAccelerationStructure::Tick(Scene& scene, Vulkan::DeviceMemory* arenaMemory)
{
    if (arenaMemory == nullptr)
    {
        return false;
    }
    ambientFocus_ = ResolveAmbientFocus(scene);
    bool voxelUploadCompleted = false;
    Tasks::TaskCoordinator* tasks = Tasks::TaskCoordinator::GetInstance();

    switch (voxelJobStage_)
    {
    case EVoxelJobStage::Idle:
        if (std::any_of(voxelJobBricks_.begin(), voxelJobBricks_.end(),
                        [](const std::vector<uint32_t>& bricks) { return !bricks.empty(); }))
        {
            StartVoxelJob();
        }
        break;

    case EVoxelJobStage::Voxelizing:
        if (tasks->IsAllTaskComplete(voxelJobTasks_))
        {
            // One distance-field task per touched cascade, over the box around its changed bricks.
            voxelJobTasks_.clear();
            voxelJobOutputBoxes_.assign(voxelJobBricks_.size(), {glm::ivec3(0), glm::ivec3(-1)});
            for (uint32_t cascadeIndex = 0; cascadeIndex < voxelJobBricks_.size(); ++cascadeIndex)
            {
                const std::vector<uint32_t>& bricks = voxelJobBricks_[cascadeIndex];
                if (bricks.empty())
                {
                    continue;
                }
                const FCPUProbeBaker& baker = cascadeBakers[cascadeIndex];
                glm::ivec3 brickMin(INT_MAX);
                glm::ivec3 brickMax(INT_MIN);
                for (uint32_t brick : bricks)
                {
                    const glm::ivec3 window = baker.WindowBrick(brick);
                    brickMin = glm::min(brickMin, window);
                    brickMax = glm::max(brickMax, window);
                }
                const int edge = static_cast<int>(kVoxelBrickEdge);
                const glm::ivec3 changedMin = brickMin * edge;
                const glm::ivec3 changedMax = (brickMax + 1) * edge - 1;
                voxelJobTasks_.push_back(tasks->AddParralledTask(
                    [this, cascadeIndex, changedMin, changedMax](Tasks::ResTask& task)
                    {
                        voxelJobOutputBoxes_[cascadeIndex] =
                            cascadeBakers[cascadeIndex].RebuildDistanceField(changedMin, changedMax);
                    },
                    nullptr,
                    "Distance field rebuild"));
            }
            voxelJobStage_ = EVoxelJobStage::DistanceField;
        }
        break;

    case EVoxelJobStage::DistanceField:
        if (tasks->IsAllTaskComplete(voxelJobTasks_))
        {
            FinishVoxelJob(scene, *arenaMemory);
            voxelUploadCompleted = true;
        }
        break;
    }

    FlushPendingAmbientBakeCacheSave(scene, arenaMemory);
    return voxelUploadCompleted;
}

bool FCPUAccelerationStructure::HasPendingWork() const
{
    std::lock_guard<std::mutex> lock(buildMutex_);
    return buildInFlight_ || HasProbeVoxelizationWork();
}

FProbeBakeProgress FCPUAccelerationStructure::GetProbeBakeProgress() const
{
    FProbeBakeProgress progress;
    progress.completedVoxelGroups = completedVoxelTasks_.load(std::memory_order_acquire);
    progress.totalVoxelGroups = totalVoxelTasks_;

    if (voxelJobStage_ == EVoxelJobStage::Voxelizing ||
        (voxelJobStage_ == EVoxelJobStage::Idle && HasProbeVoxelizationWork()))
    {
        progress.stage = EProbeBakeStage::VoxelData;
    }
    else if (voxelJobStage_ == EVoxelJobStage::DistanceField)
    {
        progress.stage = EProbeBakeStage::DistanceField;
    }
    else if (ambientCachePendingSave_ || ambientCacheSaveInFlight_.load(std::memory_order_acquire))
    {
        progress.stage = EProbeBakeStage::CacheSave;
    }

    return progress;
}

bool FCPUAccelerationStructure::HasProbeVoxelizationWork() const
{
    return voxelJobStage_ != EVoxelJobStage::Idle ||
           std::any_of(voxelJobBricks_.begin(), voxelJobBricks_.end(),
                       [](const std::vector<uint32_t>& bricks) { return !bricks.empty(); });
}

bool FCPUAccelerationStructure::AcquireAmbientBakeBatch(Scene& scene, uint32_t maxBricks, FAmbientBakeBatch& outBatch)
{
    // Bricks whose voxels are still being rebuilt would bake against a half-written grid.
    if (HasProbeVoxelizationWork() || cascadeBakers.empty() || cpuBrickTable.brickTable.empty())
    {
        return false;
    }
    const uint32_t publishPasses = NextEngine::GetInstance()->GetUserSettings().AmbientCubePublishPasses;
    const uint32_t poolBricks = scene.AmbientPoolBricksPerCascade();
    if (!cpuBrickTable.SelectBakeBatch(cascadeBakers, std::min(maxBricks, poolBricks), publishPasses, ambientFocus_,
                                       outBatch))
    {
        return false;
    }
    outBatch.generation = brickTableGeneration_;
    if (!ambientBakeActiveLogged_)
    {
        const FAmbientBakeQueueStats stats = cpuBrickTable.QueueStats(publishPasses);
        SPDLOG_INFO("Ambient bake started: {} bricks queued, {} unpublished", stats.pendingBricks,
                    stats.unpublishedBricks);
        ambientBakeActiveLogged_ = true;
        ambientBakeIdle_ = false;
    }

    // The cascade's active-list region is this frame's scratch list: the bake shader reads
    // AmbientActiveBrickList[cascade * pool + i] for i < batch size.
    const size_t listOffset = scene.AmbientActiveBrickListByteOffset() +
                              static_cast<size_t>(outBatch.cascade) * poolBricks * sizeof(uint32_t);
    const size_t bytes = outBatch.bricks.size() * sizeof(uint32_t);
    void* mapped = scene.AmbientArenaMemory().Map(static_cast<VkDeviceSize>(listOffset), static_cast<VkDeviceSize>(bytes));
    std::memcpy(mapped, outBatch.bricks.data(), bytes);
    scene.AmbientArenaMemory().Unmap();
    return true;
}

void FCPUAccelerationStructure::CompleteAmbientBakeBatch(const FAmbientBakeBatch& batch)
{
    cpuBrickTable.CompleteBakeBatch(batch);
    if (cpuBrickTable.QueueStats(0u).pendingBricks != 0u)
    {
        return;
    }
    SPDLOG_INFO("Ambient bake converged after {} passes; queue empty", kAmbientBakeConvergencePasses);
    ambientBakeActiveLogged_ = false;
    if (!ambientBakeIdle_)
    {
        ambientBakeIdle_ = true;
        // Converged. The GPU may still have the last dispatches in flight, so the readback waits a
        // few frames rather than happening here on the render path. Every frame waits on the
        // previous submit's fence before recording, so two frames on, this frame's bake dispatches
        // have certainly retired -- which is why the save needs no vkDeviceWaitIdle and can run
        // entirely off the game thread.
        ambientCachePendingSave_ = ambientCacheWriteArmed_;
        ambientCacheSaveFrame_ = NextEngine::GetInstance()->GetTotalFrames() + 3u;
    }
}

void FCPUAccelerationStructure::ResetAmbientBakePasses()
{
    cpuBrickTable.ResetBakePasses();
}

FAmbientBakeQueueStats FCPUAccelerationStructure::GetAmbientBakeQueueStats() const
{
    const uint32_t publishPasses = NextEngine::GetInstance()->GetUserSettings().AmbientCubePublishPasses;
    return cpuBrickTable.QueueStats(publishPasses);
}

bool FCPUAccelerationStructure::ConsumeNavRelevantDirtyBounds(glm::vec3& outWorldMin, glm::vec3& outWorldMax)
{
    if (!hasNavRelevantDirtyBounds_)
    {
        return false;
    }

    outWorldMin = navRelevantDirtyWorldMin_;
    outWorldMax = navRelevantDirtyWorldMax_;
    hasNavRelevantDirtyBounds_ = false;
    navRelevantDirtyWorldMin_ = glm::vec3(0.0f);
    navRelevantDirtyWorldMax_ = glm::vec3(0.0f);
    return true;
}

void FCPUAccelerationStructure::ClearNavRelevantDirtyBounds()
{
    hasNavRelevantDirtyBounds_ = false;
    navRelevantDirtyWorldMin_ = glm::vec3(0.0f);
    navRelevantDirtyWorldMax_ = glm::vec3(0.0f);
}

}
