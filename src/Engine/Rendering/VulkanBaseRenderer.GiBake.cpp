// VulkanBaseRenderer software GI baking: ambient cube cascade bake and
// distance field cascade rebuild.
// Split from VulkanBaseRenderer.cpp; same class, separate TU.
#include "Engine/Rendering/VulkanBaseRenderer.hpp"

// Ambient GI bake orchestration follows the scene-acceleration implementation.
#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Assets/GPU/Texture.hpp"
#include "Engine/Assets/GPU/UniformBuffer.hpp"
#include "Engine/Options.hpp"
#include "Engine/Rendering/PipelineCommon/CommonComputePipeline.hpp"
#include "Engine/Rendering/AmbientBakeScheduler.hpp"
#include "Engine/Rendering/Shadow/ShadowMapPass.hpp"
#include "Engine/Runtime/Components/RenderComponent.hpp"
#include "Engine/Runtime/Components/SkinnedMeshComponent.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Vulkan/BufferUtil.hpp"
#include "Engine/Vulkan/CommandExecution.hpp"
#include "Engine/Vulkan/DebugUtilities.hpp"
#include "Engine/Vulkan/Device.hpp"
#include "Engine/Vulkan/GpuResources.hpp"
#include "Engine/Vulkan/RayTracing/DeviceProcedures.hpp"
#include "Engine/Vulkan/RayTracing/RayTracingProperties.hpp"
#include "Engine/Vulkan/RenderingPipeline.hpp"
#include "Engine/Vulkan/SwapChain.hpp"
#include "Engine/Vulkan/SyncAndTiming.hpp"
#include "Engine/Runtime/Profiling/ProfilerMacros.hpp"

namespace Vulkan
{
    namespace
    {
        constexpr uint32_t kAmbientBakeFrameStartQuery = 0u;
        constexpr uint32_t kAmbientBakeStartQuery = 1u;
        constexpr uint32_t kAmbientBakeEndQuery = 2u;
        constexpr uint32_t kAmbientBakeFrameEndQuery = 3u;
        constexpr uint32_t kAmbientBakeTimestampQueryCount = 4u;
        constexpr uint32_t kGpuFrameStartQuery = 0u;
        constexpr uint32_t kGpuFrameEndQuery = 1u;
        constexpr uint32_t kGpuFrameTimestampQueryCount = 2u;

        bool TryGetElapsedMilliseconds(uint64_t startTimestamp, uint64_t endTimestamp,
                                       const double timestampPeriodNanoseconds, const uint32_t validBits,
                                       double& outMilliseconds)
        {
            outMilliseconds = 0.0;
            if (timestampPeriodNanoseconds <= 0.0 || validBits == 0u)
            {
                return false;
            }

            const uint32_t bits = std::min(validBits, 64u);
            const uint64_t mask = bits == 64u ? std::numeric_limits<uint64_t>::max() : (uint64_t{1} << bits) - 1u;
            startTimestamp &= mask;
            endTimestamp &= mask;
            const uint64_t elapsedTicks = endTimestamp >= startTimestamp
                ? endTimestamp - startTimestamp
                : (bits < 64u ? mask - startTimestamp + endTimestamp + 1u : 0u);
            if (elapsedTicks == 0u && endTimestamp < startTimestamp && bits == 64u)
            {
                return false;
            }

            outMilliseconds = static_cast<double>(elapsedTicks) * timestampPeriodNanoseconds * 1.0e-6;
            return std::isfinite(outMilliseconds);
        }
    }

    void VulkanBaseRenderer::BeginGpuFrameTiming(const VkCommandBuffer commandBuffer)
    {
        gpuFrameTiming_.frameActive = false;
        if (gpuFrameTiming_.queryPool == VK_NULL_HANDLE)
        {
            const auto queueFamilies = GetEnumerateVector(
                ctx_.device->PhysicalDevice(), vkGetPhysicalDeviceQueueFamilyProperties);
            const uint32_t queueFamily = ctx_.device->GraphicsFamilyIndex();
            const double timestampPeriod = ctx_.device->DeviceProperties().limits.timestampPeriod;
            if (queueFamily >= queueFamilies.size() || queueFamilies[queueFamily].timestampValidBits == 0u ||
                timestampPeriod <= 0.0)
            {
                return;
            }

            VkQueryPoolCreateInfo createInfo{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
            createInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
            createInfo.queryCount = kGpuFrameTimestampQueryCount;
            if (vkCreateQueryPool(ctx_.device->Handle(), &createInfo, nullptr, &gpuFrameTiming_.queryPool) != VK_SUCCESS)
            {
                SPDLOG_WARN("GPU frame timing disabled: failed to create timestamp query pool");
                gpuFrameTiming_.queryPool = VK_NULL_HANDLE;
                return;
            }
            gpuFrameTiming_.timestampValidBits = std::min(queueFamilies[queueFamily].timestampValidBits, 64u);
            gpuFrameTiming_.timestampPeriodNanoseconds = timestampPeriod;
        }

        if (gpuFrameTiming_.pending)
        {
            std::array<uint64_t, kGpuFrameTimestampQueryCount * 2u> results{};
            const VkResult result = vkGetQueryPoolResults(
                ctx_.device->Handle(), gpuFrameTiming_.queryPool, 0u, kGpuFrameTimestampQueryCount,
                sizeof(results), results.data(), sizeof(uint64_t) * 2u,
                VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
            if (result == VK_SUCCESS && results[kGpuFrameStartQuery * 2u + 1u] != 0u &&
                results[kGpuFrameEndQuery * 2u + 1u] != 0u)
            {
                double milliseconds = 0.0;
                if (TryGetElapsedMilliseconds(results[kGpuFrameStartQuery * 2u], results[kGpuFrameEndQuery * 2u],
                                               gpuFrameTiming_.timestampPeriodNanoseconds,
                                               gpuFrameTiming_.timestampValidBits, milliseconds))
                {
                    gpuFrameTiming_.lastMilliseconds = milliseconds;
                }
                gpuFrameTiming_.pending = false;
            }
            else if (result != VK_NOT_READY)
            {
                SPDLOG_WARN("GPU frame timing disabled: timestamp query failed ({})", static_cast<int>(result));
                DeleteGpuFrameTiming();
                return;
            }
            else
            {
                return;
            }
        }

        vkCmdResetQueryPool(commandBuffer, gpuFrameTiming_.queryPool, 0u, kGpuFrameTimestampQueryCount);
        vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, gpuFrameTiming_.queryPool,
                            kGpuFrameStartQuery);
        gpuFrameTiming_.frameActive = true;
    }

    void VulkanBaseRenderer::EndGpuFrameTiming(const VkCommandBuffer commandBuffer)
    {
        if (!gpuFrameTiming_.frameActive)
        {
            return;
        }

        vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, gpuFrameTiming_.queryPool,
                            kGpuFrameEndQuery);
        gpuFrameTiming_.pending = true;
        gpuFrameTiming_.frameActive = false;
    }

    void VulkanBaseRenderer::DeleteGpuFrameTiming()
    {
        if (gpuFrameTiming_.queryPool != VK_NULL_HANDLE && ctx_.device)
        {
            vkDestroyQueryPool(ctx_.device->Handle(), gpuFrameTiming_.queryPool, nullptr);
        }
        gpuFrameTiming_ = {};
    }

    void VulkanBaseRenderer::BeginAmbientBakeFrameTiming(const VkCommandBuffer commandBuffer)
    {
        ambient_.timingFrameActive = false;
        ambient_.timingBakeDispatched = false;
        if (!ActiveRendererRequirements().requestAmbientCube || ShouldSkipAmbientCubeUpdates())
        {
            return;
        }

        if (ambient_.timingQueryPool == VK_NULL_HANDLE)
        {
            const auto queueFamilies = GetEnumerateVector(
                ctx_.device->PhysicalDevice(), vkGetPhysicalDeviceQueueFamilyProperties);
            const uint32_t queueFamily = ctx_.device->GraphicsFamilyIndex();
            const double timestampPeriod = ctx_.device->DeviceProperties().limits.timestampPeriod;
            if (queueFamily >= queueFamilies.size() || queueFamilies[queueFamily].timestampValidBits == 0u ||
                timestampPeriod <= 0.0)
            {
                return;
            }

            VkQueryPoolCreateInfo createInfo{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
            createInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
            createInfo.queryCount = kAmbientBakeTimestampQueryCount;
            if (vkCreateQueryPool(ctx_.device->Handle(), &createInfo, nullptr, &ambient_.timingQueryPool) != VK_SUCCESS)
            {
                SPDLOG_WARN("Ambient bake timing disabled: failed to create timestamp query pool");
                ambient_.timingQueryPool = VK_NULL_HANDLE;
                return;
            }
            ambient_.timestampValidBits = std::min(queueFamilies[queueFamily].timestampValidBits, 64u);
            ambient_.timestampPeriodNanoseconds = timestampPeriod;
        }

        if (ambient_.timingPending)
        {
            std::array<uint64_t, kAmbientBakeTimestampQueryCount * 2u> results{};
            const VkResult result = vkGetQueryPoolResults(
                ctx_.device->Handle(), ambient_.timingQueryPool, 0u, kAmbientBakeTimestampQueryCount,
                sizeof(results), results.data(), sizeof(uint64_t) * 2u,
                VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
            if (result == VK_SUCCESS && results[kAmbientBakeFrameStartQuery * 2u + 1u] != 0u &&
                results[kAmbientBakeStartQuery * 2u + 1u] != 0u &&
                results[kAmbientBakeEndQuery * 2u + 1u] != 0u &&
                results[kAmbientBakeFrameEndQuery * 2u + 1u] != 0u)
            {
                double totalMilliseconds = 0.0;
                double bakeMilliseconds = 0.0;
                if (TryGetElapsedMilliseconds(results[kAmbientBakeFrameStartQuery * 2u],
                                               results[kAmbientBakeFrameEndQuery * 2u],
                                               ambient_.timestampPeriodNanoseconds, ambient_.timestampValidBits,
                                               totalMilliseconds) &&
                    TryGetElapsedMilliseconds(results[kAmbientBakeStartQuery * 2u],
                                               results[kAmbientBakeEndQuery * 2u],
                                               ambient_.timestampPeriodNanoseconds, ambient_.timestampValidBits,
                                               bakeMilliseconds) &&
                    bakeMilliseconds > 0.0 && ambient_.lastDispatchedGroups > 0u)
                {
                    constexpr double smoothing = 0.2;
                    const double millisecondsPerGroup = bakeMilliseconds / ambient_.lastDispatchedGroups;
                    const double nonBakeMilliseconds = std::max(0.0, totalMilliseconds - bakeMilliseconds);
                    ambient_.lastBakeMilliseconds = bakeMilliseconds;
                    ambient_.smoothedMillisecondsPerGroup = ambient_.smoothedMillisecondsPerGroup > 0.0
                        ? ambient_.smoothedMillisecondsPerGroup * (1.0 - smoothing) + millisecondsPerGroup * smoothing
                        : millisecondsPerGroup;
                    ambient_.smoothedNonBakeMilliseconds = ambient_.smoothedNonBakeMilliseconds > 0.0
                        ? ambient_.smoothedNonBakeMilliseconds * (1.0 - smoothing) + nonBakeMilliseconds * smoothing
                        : nonBakeMilliseconds;
                }
                ambient_.timingPending = false;
            }
            else if (result != VK_NOT_READY)
            {
                SPDLOG_WARN("Ambient bake timing disabled: timestamp query failed ({})", static_cast<int>(result));
                DeleteAmbientBakeFrameTiming();
                return;
            }
            else
            {
                // The frame fence normally guarantees readiness. Do not reset a query pool whose
                // results are unexpectedly still in flight.
                return;
            }
        }

        vkCmdResetQueryPool(commandBuffer, ambient_.timingQueryPool, 0u, kAmbientBakeTimestampQueryCount);
        vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, ambient_.timingQueryPool,
                            kAmbientBakeFrameStartQuery);
        ambient_.timingFrameActive = true;
    }

    void VulkanBaseRenderer::EndAmbientBakeFrameTiming(const VkCommandBuffer commandBuffer)
    {
        if (!ambient_.timingFrameActive)
        {
            return;
        }

        vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, ambient_.timingQueryPool,
                            kAmbientBakeFrameEndQuery);
        ambient_.timingPending = ambient_.timingBakeDispatched;
        ambient_.timingFrameActive = false;
    }

    void VulkanBaseRenderer::DeleteAmbientBakeFrameTiming()
    {
        if (ambient_.timingQueryPool != VK_NULL_HANDLE && ctx_.device)
        {
            vkDestroyQueryPool(ctx_.device->Handle(), ambient_.timingQueryPool, nullptr);
        }
        ambient_.timingQueryPool = VK_NULL_HANDLE;
        ambient_.timestampValidBits = 0u;
        ambient_.timestampPeriodNanoseconds = 0.0;
        ambient_.lastBakeMilliseconds = 0.0;
        ambient_.timingFrameActive = false;
        ambient_.timingBakeDispatched = false;
        ambient_.timingPending = false;
    }

    void VulkanBaseRenderer::HandleAmbientCubeCacheInvalidation(VkCommandBuffer commandBuffer, uint32_t imageIndex)
    {
        if (!ActiveRendererRequirements().requestAmbientCube || ShouldSkipAmbientCubeUpdates())
        {
            return;
        }

        if (ambient_.requestClearCache)
        {
            ClearAmbientCubeCache(commandBuffer, imageIndex);
            ambient_.requestClearCache = false;
        }
    }

    bool VulkanBaseRenderer::ShouldSkipAmbientCubeUpdates() const
    {
        if (GOption->ReferenceMode)
        {
            return false;
        }

        return CurrentLogicRendererType() == ERendererType::ERT_PathTracing;
    }

    void VulkanBaseRenderer::ClearAmbientCubeCache(VkCommandBuffer commandBuffer, uint32_t imageIndex)
    {
        SCOPED_GPU_TIMER("clear-ambient-cube-cache");

        constexpr uint32_t cubesPerGroup = 64;
        // Clear only the allocated cascades (Phase 2 right-sizing). The sparse cube pool (Phase 3b) is
        // smaller than the dense voxel array, but VoxelData no longer has transient cache fields.
        const uint32_t clearCascadeCount = std::min(
            Assets::SanitizeAmbientCubeCascadeCount(NextEngine::GetInstance()->GetUserSettings().AmbientCubeCascadeCount),
            GetScene().AmbientCubeCascadeCapacity());
        const uint32_t poolCubesPerCascade =
            GetScene().AmbientPoolBricksPerCascade() * static_cast<uint32_t>(Assets::GPU_SCENE_AMBIENT_BRICK_VOLUME);
        const uint32_t cubePoolTotal = clearCascadeCount * poolCubesPerCascade;
        const uint32_t residencyTotal =
            clearCascadeCount * static_cast<uint32_t>(Assets::GPU_SCENE_AMBIENT_BRICKS_PER_CASCADE);
        const uint32_t groupCount =
            (std::max(cubePoolTotal, residencyTotal) + cubesPerGroup - 1) / cubesPerGroup;

        ambient_.clearCache->BindPipeline(commandBuffer, GetScene(), imageIndex);

        Assets::GPUScene gpuScene = GetScene().FetchGPUScene(imageIndex, 0);
        gpuScene.CustomData0 = cubePoolTotal;
        gpuScene.CustomData1 = 0;
        gpuScene.CustomData2 = residencyTotal;

        vkCmdPushConstants(commandBuffer, ambient_.clearCache->PipelineLayout().Handle(),
                           VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Assets::GPUScene), &gpuScene);
        vkCmdDispatch(commandBuffer, groupCount, 1, 1);

        BufferMemoryBarrier::Insert(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, {
            BufferMemoryBarrier::Make(GetScene().AmbientArenaBuffer().Handle(), VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                                      GetScene().AmbientCubesByteOffset(), static_cast<VkDeviceSize>(cubePoolTotal) * sizeof(Assets::AmbientCube)),
            BufferMemoryBarrier::Make(GetScene().AmbientArenaBuffer().Handle(), VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                                      GetScene().AmbientResidencyByteOffset(), static_cast<VkDeviceSize>(residencyTotal) * sizeof(Assets::AmbientBrickResidency)),
        });
    }

    void VulkanBaseRenderer::BakeAmbientCubeCascade(VkCommandBuffer commandBuffer, uint32_t imageIndex, bool useHardware)
    {
        Vulkan::PipelineBase* pipeline = useHardware ? static_cast<Vulkan::PipelineBase*>(rt_->directLightGenPipeline.get())
                                                     : static_cast<Vulkan::PipelineBase*>(ambient_.softBake.get());
        if (!pipeline)
        {
            return;
        }

        constexpr uint32_t cubesPerGroup = 64u;
        constexpr uint32_t convergencePasses = AmbientCubePipelines::convergencePasses;
        const uint32_t cascadeCount = std::min(
            Assets::SanitizeAmbientCubeCascadeCount(NextEngine::GetInstance()->GetUserSettings().AmbientCubeCascadeCount),
            GetScene().AmbientCubeCascadeCapacity());
        if (cascadeCount == 0u)
        {
            return;
        }

        auto& cpuAcceleration = GetScene().GetCPUAccelerationStructure();
        const uint64_t dirtyRevision = cpuAcceleration.AmbientBakeDirtyRevision();
        if (dirtyRevision == 0u)
        {
            return;
        }

        uint32_t dirtyBrickTotal = 0u;
        for (uint32_t cascade = 0; cascade < cascadeCount; ++cascade)
        {
            dirtyBrickTotal += cpuAcceleration.AmbientBakeDirtyBrickCount(cascade);
        }
        if (dirtyBrickTotal == 0u)
        {
            ambient_.dirtyRevision = 0u;
            return;
        }

        if (ambient_.dirtyRevision != dirtyRevision)
        {
            ambient_.dirtyRevision = dirtyRevision;
            ambient_.nextGroup.fill(0u);
            ambient_.completedPasses.fill(0u);
            ambient_.nextCascade = 0u;
            SPDLOG_INFO("Ambient bake revision {} started: {} dirty bricks, {} tracing",
                        dirtyRevision, dirtyBrickTotal, useHardware ? "hardware" : "software");
        }

        if (ambient_.smoothedMillisecondsPerGroup > 0.0 && ambient_.smoothedNonBakeMilliseconds >= 0.0)
        {
            ambient_.groupsPerFrame = AmbientBake::PlanNextDispatchGroups(
                ambient_.groupsPerFrame,
                ambient_.smoothedMillisecondsPerGroup,
                ambient_.smoothedNonBakeMilliseconds,
                NextEngine::GetInstance()->GetUserSettings().AmbientCubeBakeTargetFps);
        }

        const char* timerName = useHardware ? "hw-lightbake" : "sw-lightbake";
        uint32_t cascadeIndex = cascadeCount;
        for (uint32_t attempt = 0; attempt < cascadeCount; ++attempt)
        {
            const uint32_t candidate = (ambient_.nextCascade + attempt) % cascadeCount;
            if (cpuAcceleration.AmbientBakeDirtyBrickCount(candidate) > 0u &&
                ambient_.completedPasses[candidate] < convergencePasses)
            {
                cascadeIndex = candidate;
                ambient_.nextCascade = (candidate + 1u) % cascadeCount;
                break;
            }
        }

        if (cascadeIndex == cascadeCount)
        {
            cpuAcceleration.AcknowledgeAmbientBake(dirtyRevision);
            SPDLOG_INFO("Ambient bake revision {} converged after {} passes; scheduler idle",
                        dirtyRevision, convergencePasses);
            ambient_.dirtyRevision = 0u;
            return;
        }

        const uint32_t dirtyBrickCount = cpuAcceleration.AmbientBakeDirtyBrickCount(cascadeIndex);
        const uint32_t dirtyProbeCount =
            dirtyBrickCount * static_cast<uint32_t>(Assets::GPU_SCENE_AMBIENT_BRICK_VOLUME);
        const uint32_t totalGroups = (dirtyProbeCount + cubesPerGroup - 1u) / cubesPerGroup;
        if (totalGroups == 0u)
        {
            return;
        }

        const uint32_t offset = std::min(ambient_.nextGroup[cascadeIndex], totalGroups);
        const uint32_t dispatchGroupCount = std::min(ambient_.groupsPerFrame, totalGroups - offset);
        const uint32_t offsetInActiveProbes = offset * cubesPerGroup;

        SCOPED_GPU_TIMER(timerName);

        if (dispatchGroupCount == 0u)
        {
            return;
        }
        const VkBuffer cubeBuffer = GetScene().AmbientArenaBuffer().Handle();
        const VkBuffer pongBuffer = cubeBuffer;
        // The cube pool is laid out per cascade with poolCubesPerCascade cubes; the ping-pong copy and
        // its barriers operate on that pool stride, not the dense per-cascade probe count.
        const VkDeviceSize poolCubesPerCascade =
            static_cast<VkDeviceSize>(GetScene().AmbientPoolBricksPerCascade()) * Assets::GPU_SCENE_AMBIENT_BRICK_VOLUME;
        const VkDeviceSize cascadeByteOffset =
            GetScene().AmbientCubesByteOffset() +
            static_cast<VkDeviceSize>(cascadeIndex) * poolCubesPerCascade * sizeof(Assets::AmbientCube);
        const VkDeviceSize pongByteOffset = GetScene().AmbientCubesPongByteOffset();
        const VkDeviceSize cascadeByteSize = poolCubesPerCascade * sizeof(Assets::AmbientCube);

        // ping (cube) -> pong copy with surrounding barriers
        BufferMemoryBarrier::Insert(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                    cubeBuffer, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, cascadeByteOffset, cascadeByteSize);

        VkBufferCopy copyRegion{};
        copyRegion.srcOffset = cascadeByteOffset;
        copyRegion.dstOffset = pongByteOffset;
        copyRegion.size = cascadeByteSize;
        vkCmdCopyBuffer(commandBuffer, cubeBuffer, pongBuffer, 1, &copyRegion);

        BufferMemoryBarrier::Insert(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, {
            BufferMemoryBarrier::Make(pongBuffer, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, pongByteOffset, cascadeByteSize),
            BufferMemoryBarrier::Make(cubeBuffer, VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT, cascadeByteOffset, cascadeByteSize),
        });

        // Dispatch the chosen bake pipeline. Both share the same GPUScene push constant layout.
        if (useHardware)
        {
            rt_->directLightGenPipeline->BindPipeline(commandBuffer, GetScene(), imageIndex);
        }
        else
        {
            ambient_.softBake->BindPipeline(commandBuffer, GetScene(), imageIndex);
        }

        Assets::GPUScene gpuScene = GetScene().FetchGPUScene(imageIndex, 0);
        gpuScene.CustomData0 = static_cast<uint32_t>(offsetInActiveProbes);
        gpuScene.CustomData1 = cascadeIndex;

        vkCmdPushConstants(commandBuffer, pipeline->PipelineLayout().Handle(),
                           VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Assets::GPUScene), &gpuScene);
        if (ambient_.timingFrameActive)
        {
            vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, ambient_.timingQueryPool,
                                kAmbientBakeStartQuery);
        }
        vkCmdDispatch(commandBuffer, dispatchGroupCount, 1, 1);
        if (ambient_.timingFrameActive)
        {
            vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, ambient_.timingQueryPool,
                                kAmbientBakeEndQuery);
            ambient_.timingBakeDispatched = true;
            ambient_.lastDispatchedGroups = dispatchGroupCount;
        }

        ambient_.nextGroup[cascadeIndex] = offset + dispatchGroupCount;
        if (ambient_.nextGroup[cascadeIndex] >= totalGroups)
        {
            ambient_.nextGroup[cascadeIndex] = 0u;
            ++ambient_.completedPasses[cascadeIndex];
        }
    }

    FAmbientBakeProgress VulkanBaseRenderer::GetAmbientBakeProgress()
    {
        FAmbientBakeProgress progress;
        if (!ActiveRendererRequirements().requestAmbientCube || ShouldSkipAmbientCubeUpdates())
        {
            return progress;
        }

        const uint32_t cascadeCount = std::min(
            Assets::SanitizeAmbientCubeCascadeCount(NextEngine::GetInstance()->GetUserSettings().AmbientCubeCascadeCount),
            GetScene().AmbientCubeCascadeCapacity());
        auto& cpuAcceleration = GetScene().GetCPUAccelerationStructure();
        const uint64_t dirtyRevision = cpuAcceleration.AmbientBakeDirtyRevision();
        if (dirtyRevision == 0u || cascadeCount == 0u)
        {
            return progress;
        }

        constexpr uint32_t cubesPerGroup = 64u;
        for (uint32_t cascadeIndex = 0; cascadeIndex < cascadeCount; ++cascadeIndex)
        {
            const uint32_t dirtyBrickCount = cpuAcceleration.AmbientBakeDirtyBrickCount(cascadeIndex);
            const uint32_t probeCount =
                dirtyBrickCount * static_cast<uint32_t>(Assets::GPU_SCENE_AMBIENT_BRICK_VOLUME);
            const uint32_t groupsPerPass = (probeCount + cubesPerGroup - 1u) / cubesPerGroup;
            progress.totalDispatchGroups += groupsPerPass * AmbientCubePipelines::convergencePasses;

            if (ambient_.dirtyRevision == dirtyRevision)
            {
                const uint32_t completedPasses = std::min(
                    ambient_.completedPasses[cascadeIndex], AmbientCubePipelines::convergencePasses);
                const uint32_t currentGroup = std::min(ambient_.nextGroup[cascadeIndex], groupsPerPass);
                progress.completedDispatchGroups += completedPasses * groupsPerPass + currentGroup;
            }
        }

        progress.completedDispatchGroups = std::min(progress.completedDispatchGroups, progress.totalDispatchGroups);
        progress.active = progress.totalDispatchGroups > 0u;
        return progress;
    }

    FGpuFrameTiming VulkanBaseRenderer::GetGpuFrameTiming() const
    {
        return {
            .milliseconds = gpuFrameTiming_.lastMilliseconds,
            .valid = gpuFrameTiming_.lastMilliseconds > 0.0,
        };
    }

}
