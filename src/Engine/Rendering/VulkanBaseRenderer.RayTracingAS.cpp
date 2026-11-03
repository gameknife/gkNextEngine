// VulkanBaseRenderer ray tracing acceleration structure lifecycle:
// BLAS/TLAS creation, per-frame updates and teardown.
// Split from VulkanBaseRenderer.cpp; same class, separate TU.
#include "Engine/Rendering/VulkanBaseRenderer.hpp"

// Hardware acceleration-structure lifecycle owned by gkNextEngine.
#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Assets/GPU/Texture.hpp"
#include "Engine/Assets/GPU/UniformBuffer.hpp"
#include "Engine/Options.hpp"
#include "Engine/Rendering/PipelineCommon/CommonComputePipeline.hpp"
#include "Engine/Rendering/Shadow/ShadowMapPass.hpp"
#include "Engine/Runtime/Components/RenderComponent.hpp"
#include "Engine/Runtime/Components/SkinnedMeshComponent.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Utilities/Math.hpp"
#include "Engine/Vulkan/BufferUtil.hpp"
#include "Engine/Vulkan/CommandExecution.hpp"
#include "Engine/Vulkan/DebugUtilities.hpp"
#include "Engine/Vulkan/Device.hpp"
#include "Engine/Vulkan/GpuResources.hpp"
#include "Engine/Vulkan/RayTracing/DeviceProcedures.hpp"
#include "Engine/Vulkan/RayTracing/RayTracingProperties.hpp"
#include "Engine/Vulkan/RenderingPipeline.hpp"
#include "Engine/Vulkan/SwapChain.hpp"
#include "Engine/Runtime/Profiling/ProfilerMacros.hpp"
#include "Engine/Utilities/Exception.hpp"

namespace Vulkan
{
    namespace
    {
        // Layout of the LOD select pass's CustomData2 word; Task.RayTracingLodSelect decodes it.
        constexpr uint32_t kLodSelectBiasMask = 0xFFu;
        constexpr uint32_t kLodSelectEnableBit = 1u << 8;

        void InsertGlobalBarrier(VkCommandBuffer commandBuffer,
                                 VkPipelineStageFlags srcStages, VkAccessFlags srcAccess,
                                 VkPipelineStageFlags dstStages, VkAccessFlags dstAccess)
        {
            VkMemoryBarrier barrier = {};
            barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
            barrier.srcAccessMask = srcAccess;
            barrier.dstAccessMask = dstAccess;
            vkCmdPipelineBarrier(commandBuffer, srcStages, dstStages, 0, 1, &barrier, 0, nullptr, 0, nullptr);
        }
    }

    void VulkanBaseRenderer::UpdateAccelerationStructuresTop(VkCommandBuffer commandBuffer, const uint32_t imageIndex)
    {
        if (!caps_.supportRayTracing)
        {
            return;
        }
        if (!(GOption->ReferenceMode || CurrentRendererRequirements().requestRayTracing ||
              (CurrentRendererRequirements().requestAmbientCube && !GOption->ForceSoftGen)) ||
            !rt_->lodSelectPipeline)
        {
            return;
        }

        bool rebuild = false;
        if (rt_->tlasUpdateRequest > 0)
        {
            rt_->tlasInstanceCount = static_cast<uint32_t>(rt_->tlasUpdateRequest);
            rt_->tlasUpdateRequest = 0;
            rebuild = true;
        }
        if (rt_->tlasInstanceCount == 0)
        {
            return;
        }

        // Levels are resolved from the primary camera with the exact inputs the main view's cull
        // pass uses, so a camera that moves proxies across levels must rebuild the TLAS even when
        // no node changed. A scene without simplified levels has nothing to re-select.
        const Runtime::Config::UserSettings& settings = FrameSettings().userSettings;
        uint32_t lodSettings = 0;
        RayTracingResources::FLodSelectionKey key;
        if (rt_->hasSimplifiedBlas)
        {
            lodSettings = std::min(settings.RayTracingLodBias, kLodSelectBiasMask) |
                          (settings.RayTracingLod ? kLodSelectEnableBit : 0u);
            key.settings = lodSettings;
            // A zero threshold resolves every proxy to LOD0 whatever the camera does.
            if (settings.RayTracingLod && frame_.lastUBO.LodBaseThreshold > 0.0f)
            {
                key.viewProjection = frame_.lastUBO.PrevViewProjectionUnJit;
                key.viewportSize = glm::vec2(frame_.lastUBO.ViewportRect.z, frame_.lastUBO.ViewportRect.w);
                key.baseThreshold = frame_.lastUBO.LodBaseThreshold;
            }
        }
        if (!(key == rt_->lodSelectionKey))
        {
            rt_->lodSelectionKey = key;
            rebuild = true;
        }
        if (!rebuild)
        {
            return;
        }

        SCOPED_GPU_TIMER("TLAS Update");
        DispatchRayTracingLodSelect(commandBuffer, imageIndex, lodSettings);
        rt_->tlas[0].Update(commandBuffer, rt_->tlasInstanceCount);
        // Every ray query later in the frame (all views, ambient bake) reads the rebuilt TLAS.
        InsertGlobalBarrier(commandBuffer,
                            VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                            VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
                            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR);
    }

    void VulkanBaseRenderer::DispatchRayTracingLodSelect(
        VkCommandBuffer commandBuffer, const uint32_t imageIndex, const uint32_t lodSettings)
    {
        SCOPED_GPU_TIMER("RT LOD select");
        auto& scene = GetScene();

        // The previous frame's TLAS build read the instances and its ray queries read the
        // per-proxy levels; this pass rewrites both, and the build below rewrites the TLAS those
        // queries traversed.
        InsertGlobalBarrier(commandBuffer,
                            VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR |
                                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            0,
                            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            VK_ACCESS_SHADER_WRITE_BIT);

        // No active view here, so the camera is the primary view's: the one whose visibility
        // buffer the traced levels have to agree with.
        Assets::GPUScene gpuScene = scene.FetchGPUScene(imageIndex, 0);
        gpuScene.ReservedAddress0 = rt_->lodResourcesBuffer->GetDeviceAddress();
        rt_->lodSelectPipeline->BindPipeline(commandBuffer, gpuScene, 0, rt_->tlasInstanceCount, lodSettings);
        vkCmdDispatch(commandBuffer, Utilities::Math::GetSafeDispatchCount(rt_->tlasInstanceCount, 64), 1, 1);

        InsertGlobalBarrier(commandBuffer,
                            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            VK_ACCESS_SHADER_WRITE_BIT,
                            VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR |
                                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            VK_ACCESS_SHADER_READ_BIT);
    }

    void VulkanBaseRenderer::UpdateAccelerationStructuresBottom(VkCommandBuffer commandBuffer)
    {
        if (!caps_.supportRayTracing || !rt_->blasScratch)
        {
            return;
        }
        SCOPED_GPU_TIMER("BLAS Update");
        VkDeviceSize scratchOffset = 0;
        for (const uint32_t modelId : GetScene().SkinUpdateRequests())
        {
            // Skinned models only ever own a LOD0 BLAS, refit in place from the skinned stream.
            if (modelId < rt_->blasLodBase.size())
            {
                auto& blas = rt_->blas[rt_->blasLodBase[modelId]];
                blas.Update(commandBuffer, *rt_->blasScratch, scratchOffset);
                scratchOffset += blas.BuildSizes().updateScratchSize;
            }
        }
        RayTracing::AccelerationStructure::InsertMemoryBarrier(commandBuffer);
    }

    std::vector<RayTracing::TopLevelAccelerationStructure>& VulkanBaseRenderer::TLAS()
    {
        static std::vector<RayTracing::TopLevelAccelerationStructure> empty;
        return rt_ ? rt_->tlas : empty;
    }

    VkAccelerationStructureKHR VulkanBaseRenderer::ActiveTLASHandle() const
    {
        if (!rt_ || rt_->tlas.empty())
        {
            return VK_NULL_HANDLE;
        }
        return rt_->tlas.front().Handle();
    }

    namespace
    {
        template <class TAccelerationStructure>
        VkAccelerationStructureBuildSizesInfoKHR GetTotalRequirements(
            const std::vector<TAccelerationStructure>& accelerationStructures)
        {
            VkAccelerationStructureBuildSizesInfoKHR total{};
            for (const auto& accelerationStructure : accelerationStructures)
            {
                total.accelerationStructureSize += accelerationStructure.BuildSizes().accelerationStructureSize;
                total.buildScratchSize += accelerationStructure.BuildSizes().buildScratchSize;
                total.updateScratchSize += accelerationStructure.BuildSizes().updateScratchSize;
            }
            return total;
        }
    }

    void VulkanBaseRenderer::CreateAccelerationStructures()
    {
        const auto timer = std::chrono::high_resolution_clock::now();

        SingleTimeCommands::Submit(CommandPool(), [this](VkCommandBuffer commandBuffer)
        {
            CreateBottomLevelStructures(commandBuffer);
            CreateTopLevelStructures(commandBuffer);
        });
        CreateRayTracingLodResources();

        // The new TLAS starts from zeroed instances and a template nobody has written yet; the next
        // AfterUpdateScene fills it (and only then requests a rebuild), and the cleared key makes
        // sure that rebuild re-selects every level.
        rt_->tlasUpdateRequest = 0;
        rt_->tlasInstanceCount = 0;
        rt_->lodSelectionKey = {};

        const auto elapsed = std::chrono::duration<float, std::chrono::seconds::period>(
            std::chrono::high_resolution_clock::now() - timer).count();
        SPDLOG_INFO("- built acceleration structures in {:.2f}ms", elapsed * 1000.f);
    }

    void VulkanBaseRenderer::DeleteAccelerationStructures()
    {
        if (rt_)
        {
            rt_->tlas.clear();
            rt_->instancesBuffer.reset();
            rt_->instancesMemory.reset();
            rt_->lodInstancesBuffer.reset();
            rt_->lodInstancesMemory.reset();
            rt_->lodResourcesBuffer.reset();
            rt_->lodResourcesMemory.reset();
            rt_->tlasScratch.reset();
            rt_->tlasScratchMemory.reset();
            rt_->tlasBuffer.reset();
            rt_->tlasMemory.reset();

            rt_->blas.clear();
            rt_->blasLodBase.clear();
            rt_->blasLodCount.clear();
            rt_->blasAddressBuffer.reset();
            rt_->blasAddressMemory.reset();
            rt_->blasScratch.reset();
            rt_->blasScratchMemory.reset();
            rt_->blasBuffer.reset();
            rt_->blasMemory.reset();
        }
    }

    void VulkanBaseRenderer::CreateBottomLevelStructures(VkCommandBuffer commandBuffer)
    {
        const auto& scene = GetScene();
        const auto& debugUtils = Device().DebugUtils();

        uint32_t vertexOffset = 0;
        uint32_t indexOffset = 0;
        uint32_t aabbOffset = 0;

        rt_->blasLodBase.clear();
        rt_->blasLodCount.clear();
        rt_->hasSimplifiedBlas = false;

        if (scene.Models().empty())
        {
            RayTracing::BottomLevelGeometry geometries;
            rt_->blas.emplace_back(Device().GetDeviceProcedures(), *rt_->properties, geometries);
        }

        for (size_t modelIdx = 0; modelIdx < scene.Models().size(); ++modelIdx)
        {
            auto& model = scene.Models()[modelIdx];
            bool hasSkin = false;
            for (const auto* render : scene.Components<Runtime::RenderComponent>())
            {
                if (render->GetModelId() == modelIdx && render->GetSkinIndex() != -1)
                {
                    hasSkin = true;
                    break;
                }
            }

            const auto vertexCount = static_cast<uint32_t>(model.NumberOfVertices());
            const auto indexCount = static_cast<uint32_t>(model.NumberOfIndices());
            RayTracing::BottomLevelGeometry geometries;

            VkDeviceAddress vertexAddr = 0;
            if (hasSkin && scene.SkinnedVertexBuffer())
            {
                vertexAddr = scene.SkinnedVertexBuffer()->GetDeviceAddress();
            }

            rt_->blasLodBase.push_back(static_cast<uint32_t>(rt_->blas.size()));
            geometries.AddGeometryTriangles(scene, vertexOffset, vertexCount, indexOffset, indexCount, true, vertexAddr);
            rt_->blas.emplace_back(Device().GetDeviceProcedures(), *rt_->properties, geometries);

            // Simplified levels take the raster LOD table's own index ranges, so the proxy traced at
            // level l is triangle for triangle the surface the visibility pass draws at level l.
            // Only section 0 of a single-section model carries levels (see Scene::RebuildMeshBuffer);
            // a skinned model is refit from the skinned stream every frame and stays at LOD0.
            uint32_t levelCount = 1;
            uint32_t encodedModelSection = 0;
            if (!hasSkin &&
                Assets::Scene::TryEncodeModelSection(static_cast<uint32_t>(modelIdx), 0, encodedModelSection) &&
                encodedModelSection < scene.Offsets().size())
            {
                const Assets::ModelData& section = scene.Offsets()[encodedModelSection];
                const uint32_t lodCount = std::min<uint32_t>(section.lodCount, Assets::MAX_MODEL_LOD_COUNT);
                for (; levelCount < lodCount; ++levelCount)
                {
                    RayTracing::BottomLevelGeometry levelGeometries;
                    levelGeometries.AddGeometryTriangles(
                        scene, vertexOffset, vertexCount,
                        section.lodIndexOffset[levelCount] * static_cast<uint32_t>(sizeof(uint32_t)),
                        section.lodIndexCount[levelCount], true);
                    rt_->blas.emplace_back(Device().GetDeviceProcedures(), *rt_->properties, levelGeometries);
                }
            }
            rt_->blasLodCount.push_back(levelCount);
            rt_->hasSimplifiedBlas = rt_->hasSimplifiedBlas || levelCount > 1;

            vertexOffset += vertexCount * sizeof(Assets::GPUVertex);
            indexOffset += indexCount * sizeof(uint32_t);
            aabbOffset += sizeof(VkAabbPositionsKHR);
        }

        const auto total = GetTotalRequirements(rt_->blas);

        rt_->blasBuffer.reset(new Buffer(Device(), total.accelerationStructureSize,
                                       VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
                                       VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT));
        rt_->blasMemory.reset(new DeviceMemory(
            rt_->blasBuffer->AllocateMemory(
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                {.AllocateFlags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT})));
        rt_->blasScratch.reset(new Buffer(Device(), std::max(total.buildScratchSize, total.updateScratchSize),
                                              VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
                                              VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                                              VK_BUFFER_USAGE_STORAGE_BUFFER_BIT));
        rt_->blasScratchMemory.reset(new DeviceMemory(
            rt_->blasScratch->AllocateMemory(
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                {.AllocateFlags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT})));

        debugUtils.SetObjectName(rt_->blasBuffer->Handle(), "BLAS Buffer");
        rt_->blasMemory->SetName("BLAS Memory");
        debugUtils.SetObjectName(rt_->blasScratch->Handle(), "BLAS Scratch Buffer");
        rt_->blasScratchMemory->SetName("BLAS Scratch Memory");

        VkDeviceSize resultOffset = 0;
        VkDeviceSize scratchOffset = 0;

        for (size_t i = 0; i != rt_->blas.size(); ++i)
        {
            rt_->blas[i].Generate(commandBuffer, *rt_->blasScratch, scratchOffset, *rt_->blasBuffer, resultOffset);
            resultOffset += rt_->blas[i].BuildSizes().accelerationStructureSize;
            scratchOffset += rt_->blas[i].BuildSizes().buildScratchSize;
            debugUtils.SetObjectName(rt_->blas[i].Handle(), ("BLAS #" + std::to_string(i)).c_str());
        }
    }

    void VulkanBaseRenderer::CreateTopLevelStructures(VkCommandBuffer commandBuffer)
    {
        const auto& debugUtils = Device().DebugUtils();
        const uint64_t requestedCapacity = std::max<size_t>(1, Assets::Scene::kRenderProxyCapacity);
        const uint64_t deviceLimit = rt_->properties->MaxInstanceCount();
        if (requestedCapacity > deviceLimit)
        {
            Throw(std::runtime_error(fmt::format(
                "TLAS instance requirement {} exceeds device limit {}", requestedCapacity, deviceLimit)));
        }
        const uint32_t instanceCapacity = static_cast<uint32_t>(std::min<uint64_t>(
            std::bit_ceil(requestedCapacity), deviceLimit));
        rt_->tlasInstanceCapacity = instanceCapacity;

        rt_->instancesBuffer.reset(new Buffer(Device(), instanceCapacity * sizeof(VkAccelerationStructureInstanceKHR),
                                          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                          VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT));
        rt_->instancesMemory.reset(new DeviceMemory(
            rt_->instancesBuffer->AllocateMemory(
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                {.AllocateFlags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT, .Passthrough = true})));
        rt_->lodInstancesBuffer.reset(new Buffer(Device(), instanceCapacity * sizeof(VkAccelerationStructureInstanceKHR),
                                             VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                                             VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                             VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                                             VK_BUFFER_USAGE_TRANSFER_DST_BIT));
        rt_->lodInstancesMemory.reset(new DeviceMemory(
            rt_->lodInstancesBuffer->AllocateMemory(
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                {.AllocateFlags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT})));

        // The initial build below covers the whole capacity. Zeroed instances reference no BLAS,
        // which Vulkan defines as inactive, so the TLAS is valid before any instance is written.
        vkCmdFillBuffer(commandBuffer, rt_->lodInstancesBuffer->Handle(), 0, VK_WHOLE_SIZE, 0);
        InsertGlobalBarrier(commandBuffer,
                            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                            VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_ACCESS_SHADER_READ_BIT);
        RayTracing::AccelerationStructure::InsertMemoryBarrier(commandBuffer);

        rt_->tlas.emplace_back(Device().GetDeviceProcedures(), *rt_->properties,
                            rt_->lodInstancesBuffer->GetDeviceAddress(), instanceCapacity);

        const auto total = GetTotalRequirements(rt_->tlas);

        rt_->tlasBuffer.reset(new Buffer(Device(), total.accelerationStructureSize,
                                    VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
                                    VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT));
        rt_->tlasMemory.reset(new DeviceMemory(
            rt_->tlasBuffer->AllocateMemory(
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                {.AllocateFlags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT, .Passthrough = true})));

        rt_->tlasScratch.reset(new Buffer(Device(), total.buildScratchSize,
                                           VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
                                           VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                                           VK_BUFFER_USAGE_STORAGE_BUFFER_BIT));
        rt_->tlasScratchMemory.reset(new DeviceMemory(
            rt_->tlasScratch->AllocateMemory(
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                {.AllocateFlags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT, .Passthrough = true})));

        debugUtils.SetObjectName(rt_->tlasBuffer->Handle(), "TLAS Buffer");
        rt_->tlasMemory->SetName("TLAS Memory");
        debugUtils.SetObjectName(rt_->tlasScratch->Handle(), "TLAS Scratch Buffer");
        rt_->tlasScratchMemory->SetName("TLAS Scratch Memory");
        debugUtils.SetObjectName(rt_->instancesBuffer->Handle(), "TLAS Instance Template Buffer");
        rt_->instancesMemory->SetName("TLAS Instance Template Memory");
        debugUtils.SetObjectName(rt_->lodInstancesBuffer->Handle(), "TLAS Instances Buffer");
        rt_->lodInstancesMemory->SetName("TLAS Instances Memory");

        rt_->tlas[0].Generate(commandBuffer, *rt_->tlasScratch, 0, *rt_->tlasBuffer, 0);
        debugUtils.SetObjectName(rt_->tlas[0].Handle(), "TLAS");
    }

    void VulkanBaseRenderer::CreateRayTracingLodResources()
    {
        const auto& procedures = Device().GetDeviceProcedures();

        // MAX_MODEL_LOD_COUNT slots per model, every slot resolved: a level a model does not have
        // clamps to its coarsest one, which is what SelectLod would have clamped to anyway.
        const auto modelCount = static_cast<uint32_t>(rt_->blasLodBase.size());
        std::vector<VkDeviceAddress> blasAddresses(
            std::max<size_t>(1, static_cast<size_t>(modelCount) * Assets::MAX_MODEL_LOD_COUNT), 0);
        for (uint32_t modelIndex = 0; modelIndex < modelCount; ++modelIndex)
        {
            const uint32_t levelCount = rt_->blasLodCount[modelIndex];
            for (uint32_t level = 0; level < static_cast<uint32_t>(Assets::MAX_MODEL_LOD_COUNT); ++level)
            {
                const auto& blas = rt_->blas[rt_->blasLodBase[modelIndex] + std::min(level, levelCount - 1)];
                VkAccelerationStructureDeviceAddressInfoKHR addressInfo = {};
                addressInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
                addressInfo.accelerationStructure = blas.Handle();
                blasAddresses[modelIndex * Assets::MAX_MODEL_LOD_COUNT + level] =
                    procedures.vkGetAccelerationStructureDeviceAddressKHR(Device().Handle(), &addressInfo);
            }
        }

        const VkBufferUsageFlags flags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        BufferUtil::CreateDeviceBuffer(CommandPool(), "BLAS LOD Addresses", flags, blasAddresses,
                                       rt_->blasAddressBuffer, rt_->blasAddressMemory);

        const std::vector<Assets::FRayTracingLodResources> resources = {
            {
                rt_->instancesBuffer->GetDeviceAddress(),
                rt_->lodInstancesBuffer->GetDeviceAddress(),
                rt_->blasAddressBuffer->GetDeviceAddress(),
                modelCount,
                0,
            },
        };
        BufferUtil::CreateDeviceBuffer(CommandPool(), "RayTracing LOD Resources", flags, resources,
                                       rt_->lodResourcesBuffer, rt_->lodResourcesMemory);
    }
}
