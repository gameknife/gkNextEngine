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
    config.sceneMin = inputs.sceneMin;
    config.sceneMax = inputs.sceneMax;
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

bool FCPUAccelerationStructure::InitCascadeBakers(const Runtime::Config::UserSettings& settings, const Scene& scene,
                                                  bool keepWindows)
{
    const FAmbientGridConfig requested = ResolveAmbientGridConfig(settings, scene);
    const uint32_t cascadeCount = requested.layout.cascadeCount;

    bool needRebuild = cascadeBakers.size() != cascadeCount || !hasCommittedAmbientGrid_ ||
                       requested.layout.followScene != committedAmbientGrid_.layout.followScene;
    for (uint32_t i = 0; !needRebuild && i < cascadeCount; ++i)
    {
        const FCPUProbeBaker& baker = cascadeBakers[i];
        needRebuild = glm::abs(requested.layout.units[i] - baker.UNIT_SIZE) > 1e-6f ||
                      (!keepWindows && (glm::length(requested.layout.origins[i] - baker.CUBE_OFFSET) > 1e-6f ||
                                        requested.layout.wraps[i] != baker.BRICK_WRAP));
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

    // After UpdateBVH: a fit-scene grid is resolved from the snapshot it publishes. A rebuild that
    // is not a level load must not pull scrolled windows back: the voxels on the GPU are theirs.
    const Runtime::Config::UserSettings& settings = NextEngine::GetInstance()->GetUserSettings();
    InitCascadeBakers(settings, scene, true);
}

void FCPUAccelerationStructure::UpdateBVH(Scene& scene)
{
    std::shared_ptr<FCPUTLASBuildInput> input = CaptureBuildInput(scene);
    std::shared_ptr<FCPUTLASBuildResult> result = BuildSnapshot(*input);
    PublishSnapshot(result->snapshot);
    MergeNavDirtyBounds(*result);
    MergeAmbientDirtyBounds(*result);
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
            result->ambientDirtyBounds.emplace_back(currentBounds.min, currentBounds.max);
            continue;
        }

        if (!BoundsNearlyEqual(currentBounds, previousIt->second))
        {
            AccumulateBounds(result->hasNavDirtyBounds, result->navDirtyWorldMin, result->navDirtyWorldMax, currentBounds);
            AccumulateBounds(result->hasNavDirtyBounds, result->navDirtyWorldMin, result->navDirtyWorldMax, previousIt->second);
            result->ambientDirtyBounds.emplace_back(currentBounds.min, currentBounds.max);
            result->ambientDirtyBounds.emplace_back(previousIt->second.min, previousIt->second.max);
        }
    }

    for (const auto& [nodeId, previousBounds] : previousNavBounds)
    {
        if (currentNavBounds.find(nodeId) == currentNavBounds.end())
        {
            AccumulateBounds(result->hasNavDirtyBounds, result->navDirtyWorldMin, result->navDirtyWorldMax, previousBounds);
            result->ambientDirtyBounds.emplace_back(previousBounds.min, previousBounds.max);
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

void FCPUAccelerationStructure::MergeAmbientDirtyBounds(const FCPUTLASBuildResult& result)
{
    if (result.ambientDirtyBounds.empty())
    {
        return;
    }
    for (const auto& [boxMin, boxMax] : result.ambientDirtyBounds)
    {
        EvictAmbientBrickCache(boxMin, boxMax);
    }
    pendingAmbientDirtyBounds_.insert(pendingAmbientDirtyBounds_.end(), result.ambientDirtyBounds.begin(),
                                      result.ambientDirtyBounds.end());
    // Many scattered changes (a level streamed in) collapse into their union: one larger job costs
    // less than walking hundreds of small boxes per cascade.
    constexpr size_t kMaxAmbientDirtyBoxes = 64;
    if (pendingAmbientDirtyBounds_.size() > kMaxAmbientDirtyBoxes)
    {
        std::pair<glm::vec3, glm::vec3> merged = pendingAmbientDirtyBounds_.front();
        for (const auto& [boxMin, boxMax] : pendingAmbientDirtyBounds_)
        {
            merged.first = glm::min(merged.first, boxMin);
            merged.second = glm::max(merged.second, boxMax);
        }
        pendingAmbientDirtyBounds_.assign(1, merged);
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
        MergeAmbientDirtyBounds(*result);
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
    // Drop whatever job the previous bake left behind: its bricks belong to the old grid. A full bake
    // (or the cache restore standing in for one) covers every geometry change made so far.
    pendingAmbientDirtyBounds_.clear();
    ambientBrickCache_.Clear();
    ambientCaptureList_.clear();
    ambientCaptureData_.reset();
    voxelJobStage_ = EVoxelJobStage::Idle;
    voxelJobTasks_.clear();
    voxelJob_.clear();

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
    voxelJob_.assign(GetActiveCascadeCount(), {});
    for (uint32_t cascadeIndex = 0; cascadeIndex < GetActiveCascadeCount(); ++cascadeIndex)
    {
        FCPUProbeBaker& baker = cascadeBakers[cascadeIndex];
        baker.ClearVoxels();
        baker.UploadGPU(*voxelGpuMemory, scene.AmbientVoxelsByteOffset(), cascadeIndex * kCascadeVoxelCount);
        FVoxelJobCascade& job = voxelJob_[cascadeIndex];
        job.bricks.resize(GPU_SCENE_AMBIENT_BRICKS_PER_CASCADE);
        std::iota(job.bricks.begin(), job.bricks.end(), 0u);
        job.changedBoxes.push_back({glm::ivec3(0), glm::ivec3(CUBE_SIZE_XY - 1, CUBE_SIZE_Z - 1, CUBE_SIZE_XY - 1)});
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
    // The windows may have scrolled before the bake was saved. Same cells sizes are required; the
    // positions are taken from the snapshot and the scroll logic moves on from there (plan 3.8).
    bool windowsMatch = payload.cascadeGrids.size() == cascadeCount && payload.cascadeWraps.size() == cascadeCount;
    for (uint32_t cascadeIndex = 0; windowsMatch && cascadeIndex < cascadeCount; ++cascadeIndex)
    {
        windowsMatch = glm::abs(payload.cascadeGrids[cascadeIndex].w - committedAmbientGrid_.layout.units[cascadeIndex]) <= 1e-5f &&
                       glm::all(glm::greaterThanEqual(glm::ivec3(payload.cascadeWraps[cascadeIndex]), glm::ivec3(0))) &&
                       glm::all(glm::lessThan(glm::ivec3(payload.cascadeWraps[cascadeIndex]), kAmbientCascadeBricks));
    }
    if (!windowsMatch || payload.cascadeCount != cascadeCount || payload.cascadeCapacity != cascadeCapacity ||
        payload.voxelCountPerCascade != kCascadeVoxelCount ||
        payload.poolBricksPerCascade != poolBricksPerCascade)
    {
        SPDLOG_INFO("Ambient bake cache {:016x} ignored: grid does not match the current scene", key);
        return false;
    }
    for (uint32_t cascadeIndex = 0; cascadeIndex < cascadeCount; ++cascadeIndex)
    {
        FCPUProbeBaker& baker = cascadeBakers[cascadeIndex];
        baker.CUBE_OFFSET = glm::vec3(payload.cascadeGrids[cascadeIndex]);
        baker.BRICK_WRAP = glm::ivec3(payload.cascadeWraps[cascadeIndex]);
        committedAmbientGrid_.layout.origins[cascadeIndex] = baker.CUBE_OFFSET;
        committedAmbientGrid_.layout.wraps[cascadeIndex] = baker.BRICK_WRAP;
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
    voxelJob_.clear();
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

void FCPUAccelerationStructure::StartVoxelJob(Scene& scene, Vulkan::DeviceMemory& arenaMemory)
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
    voxelJobStart_ = std::chrono::steady_clock::now();
    StartAmbientBrickCapture(scene, arenaMemory);
    for (uint32_t cascadeIndex = 0; cascadeIndex < voxelJob_.size() && cascadeIndex < GetActiveCascadeCount();
         ++cascadeIndex)
    {
        const std::vector<uint32_t>& bricks = voxelJob_[cascadeIndex].bricks;
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
    // Commit in one game-thread step: the touched bricks, the new windows and the brick table all
    // reach the GPU before the next frame's UBO, so shading never sees a window ahead of its data.
    std::vector<std::vector<uint32_t>> enteredBricks(GetActiveCascadeCount());
    std::vector<std::vector<uint32_t>> requeueBricks(GetActiveCascadeCount());
    bool scrolled = false;
    bool requeued = false;
    for (uint32_t cascadeIndex = 0; cascadeIndex < voxelJob_.size() && cascadeIndex < GetActiveCascadeCount();
         ++cascadeIndex)
    {
        FVoxelJobCascade& job = voxelJob_[cascadeIndex];
        if (job.bricks.empty())
        {
            continue;
        }
        FCPUProbeBaker& baker = cascadeBakers[cascadeIndex];
        std::vector<uint32_t> upload;
        for (const auto& [outMin, outMax] : job.outputBoxes)
        {
            const std::vector<uint32_t> bricks = baker.StorageBricksInBox(outMin, outMax);
            upload.insert(upload.end(), bricks.begin(), bricks.end());
        }
        std::sort(upload.begin(), upload.end());
        upload.erase(std::unique(upload.begin(), upload.end()), upload.end());
        baker.UploadBricks(arenaMemory, scene.AmbientVoxelsByteOffset(), cascadeIndex * kCascadeVoxelCount, upload);
        if (job.requeue)
        {
            // Every probe the rebuilt distance field reaches sees different geometry now.
            requeueBricks[cascadeIndex] = upload;
            requeued = true;
        }
        if (job.scrolls)
        {
            committedAmbientGrid_.layout.origins[cascadeIndex] = baker.CUBE_OFFSET;
            committedAmbientGrid_.layout.wraps[cascadeIndex] = baker.BRICK_WRAP;
            enteredBricks[cascadeIndex] = job.bricks;
            scrolled = true;
        }
    }
    if (scrolled)
    {
        ++scrollCount_;
        lastScrollMilliseconds_ =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - voxelJobStart_).count();
    }

    // Reclassify and allocate. Slots that changed owner are zeroed so the new owner never shades
    // with the old one's radiance; their zero sample count keeps them unpublished until rebaked.
    cpuBrickTable.UpdateData(cascadeBakers, scene.AmbientCubeCascadeCapacity(), scene.AmbientPoolBricksPerCascade(),
                             ambientFocus_, scrolled ? &enteredBricks : nullptr);
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
    if (scrolled)
    {
        const size_t captured = ambientCaptureList_.size();
        const auto captureCommitStart = std::chrono::steady_clock::now();
        CommitAmbientBrickCapture();
        lastScrollRestoredBricks_ = RestoreEnteringAmbientBricks(scene, arenaMemory, enteredBricks);
        const FAmbientBrickCacheStats cacheStats = ambientBrickCache_.Stats();
        SPDLOG_INFO("[AmbientBricks] scroll: {} bricks kept, {} restored from the brick cache ({:.1f} ms); "
                    "cache {} bricks, {} hits, {} evictions",
                    captured, lastScrollRestoredBricks_,
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - captureCommitStart).count(),
                    cacheStats.entries, cacheStats.hits, cacheStats.evictions);
    }
    if (fullProbeBakePending_)
    {
        // The whole grid was rebuilt: every allocated brick bakes from scratch.
        cpuBrickTable.ResetBakePasses();
        fullProbeBakePending_ = false;
    }
    else if (requeued)
    {
        // Rebake around a geometry change without dropping what is there: the bricks stay published
        // and their probes' sample count drops to the publish threshold, so the running average the
        // bake keeps (weight 1 / (count + 1)) moves to the new lighting within a few passes.
        ++dirtyRevoxelCount_;
        const uint32_t publishPasses = NextEngine::GetInstance()->GetUserSettings().AmbientCubePublishPasses;
        const uint8_t keptSamples = static_cast<uint8_t>(std::clamp(publishPasses, 1u, 255u));
        void* mapped = arenaMemory.Map(scene.AmbientCubesByteOffset(), scene.AmbientVoxelsByteOffset());
        auto* cubes = static_cast<AmbientCube*>(mapped);
        for (uint32_t cascadeIndex = 0; cascadeIndex < requeueBricks.size(); ++cascadeIndex)
        {
            if (requeueBricks[cascadeIndex].empty())
            {
                continue;
            }
            const std::vector<uint32_t> slots = cpuBrickTable.RequeueBricks(
                cascadeIndex, requeueBricks[cascadeIndex], scene.AmbientPoolBricksPerCascade(), publishPasses);
            for (uint32_t globalSlot : slots)
            {
                AmbientCube* brick = cubes + static_cast<size_t>(globalSlot) * Assets::GPU_SCENE_AMBIENT_BRICK_VOLUME;
                for (uint32_t probe = 0; probe < static_cast<uint32_t>(Assets::GPU_SCENE_AMBIENT_BRICK_VOLUME); ++probe)
                {
                    // Byte 2 of SkyVisibilityPXNXS0S1 is the sample count (AmbientCubeSampleCount).
                    const uint32_t packed = brick[probe].SkyVisibilityPXNXS0S1;
                    const uint32_t samples = (packed >> 16u) & 0xFFu;
                    if (samples > keptSamples)
                    {
                        brick[probe].SkyVisibilityPXNXS0S1 =
                            (packed & ~0x00FF0000u) | (static_cast<uint32_t>(keptSamples) << 16u);
                    }
                }
            }
        }
        arenaMemory.Unmap();
    }
    cpuBrickTable.UploadGPU(arenaMemory, scene.AmbientBrickTableByteOffset());
    scene.SetAmbientActiveBrickCounts(cpuBrickTable.activeBricksPerCascade);
    ++brickTableGeneration_;

    voxelJob_.clear();
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
    voxelJob_.clear();
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
    pendingAmbientDirtyBounds_.clear();
    ambientBrickCache_.Clear();
    ambientCaptureList_.clear();
    ambientCaptureData_.reset();
    ClearNavRelevantDirtyBounds();
    PublishSnapshot(std::make_shared<FCPUTLASSnapshot>());
    blasSet_.reset();
}

glm::vec3 FCPUAccelerationStructure::ResolveAmbientFocus(const Scene& scene) const
{
    // The camera actually rendering this frame: a game instance may drive it independently of the
    // scene's own (file) camera, which is only the fallback.
    const NextEngine* engine = NextEngine::GetInstance();
    const Assets::Camera camera = engine != nullptr ? engine->GetActiveRenderCamera() : scene.GetRenderCamera();
    glm::vec3 focus = glm::vec3(glm::inverse(camera.ModelView)[3]);

    // In Y the windows hold the ground under the camera rather than the camera itself: a camera a
    // few tens of metres up looks at the ground, and a finest cascade centred on it would sit in the
    // air. Indoors the ray stops at the floor below, which is what the player stands on.
    const SnapshotPtr snapshot = AcquireSnapshot();
    if (committedAmbientGrid_.layout.followScene && snapshot && !snapshot->instances.empty())
    {
        const float drop = std::max(focus.y - committedAmbientGrid_.sceneMin.y, 0.0f) + 1.0f;
        const RayCastResult ground =
            RayCastInSnapshot(*snapshot, focus, glm::vec3(0.0f, -1.0f, 0.0f), true, drop);
        if (ground.Hit)
        {
            focus.y = ground.HitPoint.y;
        }
    }
    return focus;
}

bool FCPUAccelerationStructure::Tick(Scene& scene, Vulkan::DeviceMemory* arenaMemory)
{
    if (arenaMemory == nullptr)
    {
        return false;
    }
    ambientFocus_ = ResolveAmbientFocus(scene);
    ambientBrickCache_.SetBudgetBytes(
        static_cast<uint64_t>(NextEngine::GetInstance()->GetUserSettings().AmbientCubeBrickCacheMB) * 1024u * 1024u);
    bool voxelUploadCompleted = false;
    Tasks::TaskCoordinator* tasks = Tasks::TaskCoordinator::GetInstance();

    switch (voxelJobStage_)
    {
    case EVoxelJobStage::Idle:
        if (HasProbeVoxelizationWork() || PlanAmbientScroll() || PlanAmbientDirtyRegions())
        {
            StartVoxelJob(scene, *arenaMemory);
        }
        break;

    case EVoxelJobStage::Voxelizing:
        if (tasks->IsAllTaskComplete(voxelJobTasks_))
        {
            // One distance-field task per touched cascade, over the boxes whose occupancy changed.
            voxelJobTasks_.clear();
            for (uint32_t cascadeIndex = 0; cascadeIndex < voxelJob_.size(); ++cascadeIndex)
            {
                if (voxelJob_[cascadeIndex].changedBoxes.empty())
                {
                    continue;
                }
                voxelJobTasks_.push_back(tasks->AddParralledTask(
                    [this, cascadeIndex](Tasks::ResTask& task)
                    {
                        FVoxelJobCascade& job = voxelJob_[cascadeIndex];
                        job.outputBoxes.clear();
                        for (const auto& [changedMin, changedMax] : job.changedBoxes)
                        {
                            job.outputBoxes.push_back(
                                cascadeBakers[cascadeIndex].RebuildDistanceField(changedMin, changedMax));
                        }
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
           std::any_of(voxelJob_.begin(), voxelJob_.end(),
                       [](const FVoxelJobCascade& job) { return !job.bricks.empty(); });
}

bool FCPUAccelerationStructure::AcquireAmbientBakeBatch(Scene& scene, uint32_t maxBricks, FAmbientBakeBatch& outBatch)
{
    // A full bake rebuilds every brick, so baking before it lands is wasted. A scroll does not block
    // the bake: the GPU keeps the old voxels, table and windows until the job commits all of them at
    // once, and a batch that straddles the commit is dropped by its generation.
    if (fullProbeBakePending_ || cascadeBakers.empty() || cpuBrickTable.brickTable.empty())
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
    outBatch.slots.resize(outBatch.bricks.size());
    const size_t tableBase = static_cast<size_t>(outBatch.cascade) * Assets::GPU_SCENE_AMBIENT_BRICKS_PER_CASCADE;
    for (size_t i = 0; i < outBatch.bricks.size(); ++i)
    {
        outBatch.slots[i] = cpuBrickTable.brickTable[tableBase + outBatch.bricks[i]];
    }
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
    // The cube pool was cleared because the lighting changed (environment, sun): what the cache holds
    // was lit the old way.
    ambientBrickCache_.Clear();
}

bool FCPUAccelerationStructure::PlanAmbientScroll()
{
    // Never while a cache save is reading the voxel arrays on a worker, nor before the first bake.
    const FAmbientCascadeLayout& layout = committedAmbientGrid_.layout;
    if (!hasCommittedAmbientGrid_ || !layout.followScene || cascadeBakers.empty() || fullProbeBakePending_ ||
        ambientCacheSaveInFlight_.load(std::memory_order_acquire) || ambientCachePendingSave_)
    {
        return false;
    }

    const glm::ivec3 dims = kAmbientCascadeBricks;
    const int edge = static_cast<int>(kVoxelBrickEdge);
    const glm::ivec3 lastCell(CUBE_SIZE_XY - 1, CUBE_SIZE_Z - 1, CUBE_SIZE_XY - 1);
    std::vector<FVoxelJobCascade> job(GetActiveCascadeCount());
    bool any = false;
    for (uint32_t cascadeIndex = 0; cascadeIndex < GetActiveCascadeCount(); ++cascadeIndex)
    {
        FCPUProbeBaker& baker = cascadeBakers[cascadeIndex];
        const float unit = baker.UNIT_SIZE;
        const glm::vec3 lattice = CalculateAmbientCubeOffset(unit, committedAmbientGrid_.offsetBias);
        const glm::vec3 desired = ResolveAmbientFitWindowOrigin(unit, lattice, committedAmbientGrid_.sceneMin,
                                                                committedAmbientGrid_.sceneMax, ambientFocus_);
        const glm::ivec3 delta =
            glm::ivec3(glm::round((desired - baker.CUBE_OFFSET) / (unit * static_cast<float>(edge))));
        // Hysteresis: a window recentres only once the focus is about a brick and a half off centre,
        // so walking back and forth across a brick boundary does not rebuild the same slab twice.
        const glm::ivec3 distance = glm::abs(delta);
        if (std::max({distance.x, distance.y, distance.z}) < kAmbientScrollHysteresisBricks)
        {
            continue;
        }

        // The CPU side moves now; the GPU voxels, brick table and shading windows follow at commit.
        glm::vec3 origin = baker.CUBE_OFFSET;
        glm::ivec3 wrap = baker.BRICK_WRAP;
        ShiftAmbientWindow(origin, wrap, delta, unit);
        baker.CUBE_OFFSET = origin;
        baker.BRICK_WRAP = wrap;

        FVoxelJobCascade& cascadeJob = job[cascadeIndex];
        cascadeJob.scrolls = true;
        // New window brick w was old window brick w + delta; outside the old window it entered.
        for (uint32_t linear = 0; linear < static_cast<uint32_t>(GPU_SCENE_AMBIENT_BRICKS_PER_CASCADE); ++linear)
        {
            const glm::ivec3 windowBrick = AmbientBrickFromLinearIndex(linear);
            const glm::ivec3 previous = windowBrick + delta;
            if (glm::any(glm::lessThan(previous, glm::ivec3(0))) || glm::any(glm::greaterThanEqual(previous, dims)))
            {
                cascadeJob.bricks.push_back(AmbientBrickLinearIndex(AmbientStorageBrick(windowBrick, wrap)));
            }
        }
        // One slab per moved axis; a jump of a whole window or more replaces everything.
        if (glm::any(glm::greaterThanEqual(glm::abs(delta), dims)))
        {
            cascadeJob.changedBoxes.push_back({glm::ivec3(0), lastCell});
        }
        else
        {
            for (int axis = 0; axis < 3; ++axis)
            {
                if (delta[axis] == 0)
                {
                    continue;
                }
                glm::ivec3 boxMin(0);
                glm::ivec3 boxMax = lastCell;
                if (delta[axis] > 0)
                {
                    boxMin[axis] = (dims[axis] - delta[axis]) * edge;
                }
                else
                {
                    boxMax[axis] = -delta[axis] * edge - 1;
                }
                cascadeJob.changedBoxes.push_back({boxMin, boxMax});
            }
        }
        any = true;
    }
    if (any)
    {
        voxelJob_ = std::move(job);
    }
    return any;
}

glm::ivec3 FCPUAccelerationStructure::AmbientOriginBrick(uint32_t cascade, const glm::vec3& origin) const
{
    // Windows are snapped to whole bricks of their lattice, so this is an exact integer.
    const float unit = committedAmbientGrid_.layout.units[cascade];
    const glm::vec3 lattice = CalculateAmbientCubeOffset(unit, committedAmbientGrid_.offsetBias);
    return glm::ivec3(glm::round((origin - lattice) / (unit * static_cast<float>(kVoxelBrickEdge))));
}

void FCPUAccelerationStructure::StartAmbientBrickCapture(Scene& scene, Vulkan::DeviceMemory& arenaMemory)
{
    ambientCaptureList_.clear();
    ambientCaptureData_.reset();
    if (!ambientBrickCache_.Enabled() || cpuBrickTable.brickTable.empty())
    {
        return;
    }
    // Only bricks worth more than a fresh start: published ones. Their passes come along, so one
    // still topping up carries on from there when it returns. The committed layout still holds the
    // old windows -- the job has moved only the bakers -- which is where these bricks were.
    const uint32_t publishPasses = std::max(1u, NextEngine::GetInstance()->GetUserSettings().AmbientCubePublishPasses);
    const uint32_t poolBricks = scene.AmbientPoolBricksPerCascade();
    for (uint32_t cascade = 0; cascade < voxelJob_.size() && cascade < GetActiveCascadeCount(); ++cascade)
    {
        const FVoxelJobCascade& job = voxelJob_[cascade];
        if (!job.scrolls)
        {
            continue;
        }
        const glm::ivec3 originBrick = AmbientOriginBrick(cascade, committedAmbientGrid_.layout.origins[cascade]);
        const glm::ivec3 wrap = committedAmbientGrid_.layout.wraps[cascade];
        const size_t tableBase = static_cast<size_t>(cascade) * GPU_SCENE_AMBIENT_BRICKS_PER_CASCADE;
        for (uint32_t storageBrick : job.bricks)
        {
            const size_t entry = tableBase + storageBrick;
            const uint32_t slot = cpuBrickTable.brickTable[entry];
            if (slot == GPU_SCENE_AMBIENT_BRICK_INVALID || cpuBrickTable.bakePasses[entry] < publishPasses)
            {
                continue;
            }
            ambientCaptureList_.push_back(
                {cascade, originBrick + AmbientWindowBrick(AmbientBrickFromLinearIndex(storageBrick), wrap),
                 cascade * poolBricks + slot, cpuBrickTable.bakePasses[entry]});
        }
    }
    if (ambientCaptureList_.empty())
    {
        return;
    }

    std::vector<uint32_t> slots;
    slots.reserve(ambientCaptureList_.size());
    for (const FAmbientBrickCapture& capture : ambientCaptureList_)
    {
        slots.push_back(capture.globalSlot);
    }
    ambientCaptureData_ = std::make_shared<std::vector<std::vector<AmbientCube>>>(slots.size());
    // The slots keep their owners until the commit, and the bake may still be topping some of them
    // up meanwhile; a copy a pass behind is as good as the pass count recorded with it. Queued ahead
    // of the voxelization and split up, so the slow device-memory reads overlap it rather than
    // trailing it, and each brick's buffer is allocated here rather than on the game thread.
    const size_t cubesOffset = scene.AmbientCubesByteOffset();
    const size_t cubesBytes = scene.AmbientVoxelsByteOffset();
    const auto sharedSlots = std::make_shared<const std::vector<uint32_t>>(std::move(slots));
    constexpr size_t kBricksPerCaptureTask = 128;
    for (size_t first = 0; first < sharedSlots->size(); first += kBricksPerCaptureTask)
    {
        const size_t last = std::min(sharedSlots->size(), first + kBricksPerCaptureTask);
        voxelJobTasks_.push_back(Tasks::TaskCoordinator::GetInstance()->AddParralledTask(
            [&arenaMemory, cubesOffset, cubesBytes, first, last, slots = sharedSlots,
             data = ambientCaptureData_](Tasks::ResTask&)
            {
                const auto* cubes = static_cast<const AmbientCube*>(arenaMemory.Map(cubesOffset, cubesBytes));
                for (size_t i = first; i < last; ++i)
                {
                    std::vector<AmbientCube>& brick = (*data)[i];
                    brick.resize(FAmbientBrickCache::kProbesPerBrick);
                    CopyFromDeviceMemory(brick.data(),
                                         cubes + static_cast<size_t>((*slots)[i]) * FAmbientBrickCache::kProbesPerBrick,
                                         FAmbientBrickCache::kBytesPerEntry);
                }
                arenaMemory.Unmap();
            },
            nullptr,
            "Ambient brick capture"));
    }
}

void FCPUAccelerationStructure::CommitAmbientBrickCapture()
{
    if (ambientCaptureData_ && ambientCaptureData_->size() == ambientCaptureList_.size())
    {
        for (size_t i = 0; i < ambientCaptureList_.size(); ++i)
        {
            const FAmbientBrickCapture& capture = ambientCaptureList_[i];
            // A task cancelled with the job leaves its bricks empty; Store ignores those.
            ambientBrickCache_.Store(capture.cascade, capture.worldBrick, std::move((*ambientCaptureData_)[i]),
                                     capture.passes);
        }
    }
    ambientCaptureList_.clear();
    ambientCaptureData_.reset();
}

uint32_t FCPUAccelerationStructure::RestoreEnteringAmbientBricks(
    Scene& scene, Vulkan::DeviceMemory& arenaMemory, const std::vector<std::vector<uint32_t>>& enteredBricks)
{
    if (ambientBrickCache_.Stats().entries == 0u)
    {
        return 0u;
    }
    uint32_t restored = 0;
    AmbientCube* cubes = nullptr;
    for (uint32_t cascade = 0; cascade < enteredBricks.size(); ++cascade)
    {
        if (enteredBricks[cascade].empty())
        {
            continue;
        }
        const glm::ivec3 originBrick = AmbientOriginBrick(cascade, committedAmbientGrid_.layout.origins[cascade]);
        const glm::ivec3 wrap = committedAmbientGrid_.layout.wraps[cascade];
        const size_t tableBase = static_cast<size_t>(cascade) * GPU_SCENE_AMBIENT_BRICKS_PER_CASCADE;
        const size_t poolBase = static_cast<size_t>(cascade) * scene.AmbientPoolBricksPerCascade();
        for (uint32_t storageBrick : enteredBricks[cascade])
        {
            const size_t entry = tableBase + storageBrick;
            const uint32_t slot = cpuBrickTable.brickTable[entry];
            if (slot == GPU_SCENE_AMBIENT_BRICK_INVALID)
            {
                continue;
            }
            const glm::ivec3 worldBrick =
                originBrick + AmbientWindowBrick(AmbientBrickFromLinearIndex(storageBrick), wrap);
            if (!ambientBrickCache_.Contains(cascade, worldBrick))
            {
                continue;
            }
            if (cubes == nullptr)
            {
                cubes = static_cast<AmbientCube*>(
                    arenaMemory.Map(scene.AmbientCubesByteOffset(), scene.AmbientVoxelsByteOffset()));
            }
            uint8_t passes = 0;
            ambientBrickCache_.Fetch(cascade, worldBrick, cubes + (poolBase + slot) * FAmbientBrickCache::kProbesPerBrick,
                                     passes);
            cpuBrickTable.bakePasses[entry] = passes;
            ++restored;
        }
    }
    if (cubes != nullptr)
    {
        arenaMemory.Unmap();
    }
    return restored;
}

void FCPUAccelerationStructure::EvictAmbientBrickCache(const glm::vec3& worldMin, const glm::vec3& worldMax)
{
    if (!hasCommittedAmbientGrid_ || ambientBrickCache_.Stats().entries == 0u)
    {
        return;
    }
    // A change reaches as far as the distance field it rewrites (16 cells, two bricks) on every
    // cascade; light carried further than that is left to the next rebake of those bricks.
    constexpr int kReachBricks = 2;
    for (uint32_t cascade = 0; cascade < committedAmbientGrid_.layout.cascadeCount; ++cascade)
    {
        const float unit = committedAmbientGrid_.layout.units[cascade];
        const float brick = unit * static_cast<float>(kVoxelBrickEdge);
        const glm::vec3 lattice = CalculateAmbientCubeOffset(unit, committedAmbientGrid_.offsetBias);
        const glm::ivec3 brickMin = glm::ivec3(glm::floor((worldMin - lattice) / brick)) - kReachBricks;
        const glm::ivec3 brickMax = glm::ivec3(glm::floor((worldMax - lattice) / brick)) + kReachBricks;
        ambientBrickCache_.EvictBox(cascade, brickMin, brickMax);
    }
}

bool FCPUAccelerationStructure::PlanAmbientDirtyRegions()
{
    if (pendingAmbientDirtyBounds_.empty())
    {
        return false;
    }
    if (!NextEngine::GetInstance()->GetUserSettings().AmbientCubeDynamicUpdate)
    {
        pendingAmbientDirtyBounds_.clear();
        return false;
    }
    // Same gates as scrolling: never during a full bake (which covers the change anyway) or while a
    // cache save reads the voxel arrays.
    if (!hasCommittedAmbientGrid_ || cascadeBakers.empty() || fullProbeBakePending_ ||
        ambientCacheSaveInFlight_.load(std::memory_order_acquire) || ambientCachePendingSave_)
    {
        return false;
    }

    const int edge = static_cast<int>(kVoxelBrickEdge);
    const glm::ivec3 lastCell(CUBE_SIZE_XY - 1, CUBE_SIZE_Z - 1, CUBE_SIZE_XY - 1);
    std::vector<FVoxelJobCascade> job(GetActiveCascadeCount());
    bool any = false;
    for (uint32_t cascadeIndex = 0; cascadeIndex < GetActiveCascadeCount(); ++cascadeIndex)
    {
        const FCPUProbeBaker& baker = cascadeBakers[cascadeIndex];
        FVoxelJobCascade& cascadeJob = job[cascadeIndex];
        for (const auto& [worldMin, worldMax] : pendingAmbientDirtyBounds_)
        {
            // A voxel samples its whole cell, so a change touches every cell it overlaps plus one.
            const glm::ivec3 cellMin = glm::ivec3(glm::floor((worldMin - baker.CUBE_OFFSET) / baker.UNIT_SIZE)) - 1;
            const glm::ivec3 cellMax = glm::ivec3(glm::floor((worldMax - baker.CUBE_OFFSET) / baker.UNIT_SIZE)) + 1;
            if (glm::any(glm::greaterThan(cellMin, lastCell)) || glm::any(glm::lessThan(cellMax, glm::ivec3(0))))
            {
                continue;
            }
            // Whole bricks are re-voxelized, so the occupancy change spans whole bricks too.
            const glm::ivec3 brickMin = glm::clamp(cellMin, glm::ivec3(0), lastCell) / edge;
            const glm::ivec3 brickMax = glm::clamp(cellMax, glm::ivec3(0), lastCell) / edge;
            const std::vector<uint32_t> bricks = baker.StorageBricksInBox(brickMin * edge, brickMax * edge);
            cascadeJob.bricks.insert(cascadeJob.bricks.end(), bricks.begin(), bricks.end());
            cascadeJob.changedBoxes.push_back({brickMin * edge, glm::min(brickMax * edge + (edge - 1), lastCell)});
        }
        if (cascadeJob.bricks.empty())
        {
            continue;
        }
        std::sort(cascadeJob.bricks.begin(), cascadeJob.bricks.end());
        cascadeJob.bricks.erase(std::unique(cascadeJob.bricks.begin(), cascadeJob.bricks.end()),
                                cascadeJob.bricks.end());
        cascadeJob.requeue = true;
        any = true;
    }
    // The boxes are consumed whether or not a window held them: outside every window there is
    // nothing to update, and a window that scrolls there later voxelizes from the current snapshot.
    pendingAmbientDirtyBounds_.clear();
    if (any)
    {
        voxelJob_ = std::move(job);
    }
    return any;
}

FAmbientScrollStats FCPUAccelerationStructure::GetAmbientScrollStats() const
{
    FAmbientScrollStats stats;
    stats.scrollCount = scrollCount_;
    stats.lastScrollMilliseconds = lastScrollMilliseconds_;
    stats.voxelJobActive = HasProbeVoxelizationWork();
    stats.dirtyRevoxelCount = dirtyRevoxelCount_;
    stats.brickCache = ambientBrickCache_.Stats();
    stats.lastScrollRestoredBricks = lastScrollRestoredBricks_;
    return stats;
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
