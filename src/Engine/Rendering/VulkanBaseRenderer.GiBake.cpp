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
        const uint32_t groupCount = (cubePoolTotal + cubesPerGroup - 1) / cubesPerGroup;

        ambient_.clearCache->BindPipeline(commandBuffer, GetScene(), imageIndex);

        Assets::GPUScene gpuScene = GetScene().FetchGPUScene(imageIndex, 0);
        gpuScene.CustomData0 = cubePoolTotal;
        gpuScene.CustomData1 = 0;

        vkCmdPushConstants(commandBuffer, ambient_.clearCache->PipelineLayout().Handle(),
                           VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Assets::GPUScene), &gpuScene);
        vkCmdDispatch(commandBuffer, groupCount, 1, 1);
        // The pool changed under the pong copy; the next bake copies its cascade in full.
        ambient_.pong.valid = false;
        // Every cube is back to the sky prior with zero samples: every allocated brick bakes again.
        GetScene().GetCPUAccelerationStructure().ResetAmbientBakePasses();

        BufferMemoryBarrier::Insert(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, {
            BufferMemoryBarrier::Make(GetScene().AmbientArenaBuffer().Handle(), VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                                      GetScene().AmbientCubesByteOffset(), static_cast<VkDeviceSize>(cubePoolTotal) * sizeof(Assets::AmbientCube)),
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

        if (ambient_.smoothedMillisecondsPerGroup > 0.0 && ambient_.smoothedNonBakeMilliseconds >= 0.0)
        {
            ambient_.groupsPerFrame = AmbientBake::PlanNextDispatchGroups(
                ambient_.groupsPerFrame,
                ambient_.smoothedMillisecondsPerGroup,
                ambient_.smoothedNonBakeMilliseconds,
                NextEngine::GetInstance()->GetUserSettings().AmbientCubeBakeTargetFps);
        }

        // The CPU queue picks the bricks (unpublished first, finer cascades first, nearest the camera
        // first) and writes them into the cascade's active brick list. A brick is 512 probes, eight
        // 64-thread groups; when the frame budget is below one brick the batch is finished over the
        // next frames, so an over-budget frame still pays for a single group, as it always has.
        constexpr uint32_t cubesPerGroup = 64u;
        constexpr uint32_t groupsPerBrick =
            static_cast<uint32_t>(Assets::GPU_SCENE_AMBIENT_BRICK_VOLUME) / cubesPerGroup;
        auto& cpuAcceleration = GetScene().GetCPUAccelerationStructure();
        // Unpublished bricks always make progress, even on a frame that is already over budget.
        uint32_t groupsThisFrame = ambient_.groupsPerFrame;
        if (cpuAcceleration.GetAmbientBakeQueueStats().unpublishedBricks > 0u)
        {
            groupsThisFrame = std::max(groupsThisFrame,
                                       AmbientBake::PublishFloorGroups(ambient_.smoothedMillisecondsPerGroup,
                                                                       ambient_.smoothedNonBakeMilliseconds));
        }
        if (ambient_.batchActive && !cpuAcceleration.IsAmbientBakeBatchCurrent(ambient_.batch))
        {
            ambient_.batchActive = false;
        }
        if (!ambient_.batchActive)
        {
            const uint32_t maxBricks = std::max(1u, groupsThisFrame / groupsPerBrick);
            if (!cpuAcceleration.AcquireAmbientBakeBatch(GetScene(), maxBricks, ambient_.batch))
            {
                return;
            }
            ambient_.batchActive = true;
            ambient_.batchNextGroup = 0u;
        }
        const uint32_t cascadeIndex = ambient_.batch.cascade;
        const uint32_t batchGroups = static_cast<uint32_t>(ambient_.batch.bricks.size()) * groupsPerBrick;
        const uint32_t firstGroup = ambient_.batchNextGroup;
        const uint32_t dispatchGroupCount = std::min(std::max(groupsThisFrame, 1u), batchGroups - firstGroup);

        const char* timerName = useHardware ? "hw-lightbake" : "sw-lightbake";
        SCOPED_GPU_TIMER(timerName);

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
        constexpr VkDeviceSize brickBytes =
            static_cast<VkDeviceSize>(Assets::GPU_SCENE_AMBIENT_BRICK_VOLUME) * sizeof(Assets::AmbientCube);

        // ping (cube) -> pong copy with surrounding barriers. Back-to-back batches of the same cascade
        // against the same brick table only re-sync the slots the previous dispatch wrote; anything
        // else (another cascade, a table rebuild, a cleared or reloaded pool) copies the cascade whole.
        BufferMemoryBarrier::Insert(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                    cubeBuffer, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, cascadeByteOffset, cascadeByteSize);

        std::vector<std::pair<uint32_t, uint32_t>> pongRuns;
        const uint64_t arenaId = reinterpret_cast<const uint64_t&>(cubeBuffer);
        const bool fullPongCopy =
            AmbientBake::PlanPongSync(ambient_.pong, cascadeIndex, ambient_.batch.generation, arenaId, pongRuns);
        std::vector<VkBufferCopy> copyRegions;
        if (fullPongCopy)
        {
            copyRegions.push_back({cascadeByteOffset, pongByteOffset, cascadeByteSize});
        }
        else
        {
            copyRegions.reserve(pongRuns.size());
            for (const auto& [firstSlot, slotCount] : pongRuns)
            {
                copyRegions.push_back({cascadeByteOffset + firstSlot * brickBytes, pongByteOffset + firstSlot * brickBytes,
                                       slotCount * brickBytes});
            }
        }
        ambient_.lastPongCopyBytes = 0u;
        for (const VkBufferCopy& region : copyRegions)
        {
            ambient_.lastPongCopyBytes += region.size;
        }
        if (!copyRegions.empty())
        {
            vkCmdCopyBuffer(commandBuffer, cubeBuffer, pongBuffer, static_cast<uint32_t>(copyRegions.size()),
                            copyRegions.data());
        }
        // This dispatch writes the batch's slots, so they are what the next sync has to carry over.
        ambient_.pong.valid = true;
        ambient_.pong.cascade = cascadeIndex;
        ambient_.pong.generation = ambient_.batch.generation;
        ambient_.pong.arena = arenaId;
        ambient_.pong.dirtySlots = ambient_.batch.slots;

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
        // The batch sits at the start of the cascade's active list; resume where the last frame stopped.
        gpuScene.CustomData0 = firstGroup * cubesPerGroup;
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

        ambient_.batchNextGroup = firstGroup + dispatchGroupCount;
        if (ambient_.batchNextGroup >= batchGroups)
        {
            cpuAcceleration.CompleteAmbientBakeBatch(ambient_.batch);
            ambient_.batchActive = false;
        }
    }

    FAmbientBakeProgress VulkanBaseRenderer::GetAmbientBakeProgress()
    {
        FAmbientBakeProgress progress;
        if (!ActiveRendererRequirements().requestAmbientCube || ShouldSkipAmbientCubeUpdates())
        {
            return progress;
        }

        // Counted in brick passes: every allocated brick owes kAmbientBakeConvergencePasses.
        const Assets::CPU::FAmbientBakeQueueStats stats =
            GetScene().GetCPUAccelerationStructure().GetAmbientBakeQueueStats();
        progress.totalDispatchGroups = static_cast<uint32_t>(std::min<uint64_t>(stats.totalPasses, UINT32_MAX));
        progress.completedDispatchGroups = static_cast<uint32_t>(std::min<uint64_t>(stats.completedPasses, UINT32_MAX));
        progress.active = stats.pendingBricks > 0u;
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
