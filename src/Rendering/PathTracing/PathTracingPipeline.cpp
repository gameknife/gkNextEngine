#include "PathTracingPipeline.hpp"

#include "Vulkan/RayTracing/TopLevelAccelerationStructure.hpp"
#include "Assets/Scene.hpp"
#include "Assets/UniformBuffer.hpp"
#include "Vulkan/Buffer.hpp"
#include "Vulkan/Device.hpp"
#include "Vulkan/DescriptorBinding.hpp"
#include "Vulkan/DescriptorSetManager.hpp"
#include "Vulkan/DescriptorSets.hpp"
#include "Vulkan/PipelineLayout.hpp"
#include "Vulkan/ShaderModule.hpp"
#include "Vulkan/SwapChain.hpp"
#include "Rendering/VulkanBaseRenderer.hpp"

namespace Vulkan::RayTracing
{
    PathTracingPipeline::PathTracingPipeline(const SwapChain& swapChain,
        const TopLevelAccelerationStructure& accelerationStructure,
        const VulkanBaseRenderer& baseRenderer,
        const std::vector<Assets::UniformBuffer>& uniformBuffers, const Assets::Scene& scene):PipelineBase(swapChain)
    {
         // Create descriptor pool/sets.
        const auto& device = swapChain.Device();
        const std::vector<DescriptorBinding> descriptorBindings =
        {
            {0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},
            {1, 1, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, VK_SHADER_STAGE_COMPUTE_BIT},
        };
        descriptorSetManager_.reset(new DescriptorSetManager(device, descriptorBindings, uniformBuffers.size()));

        auto& descriptorSets = descriptorSetManager_->DescriptorSets();
        for (uint32_t i = 0; i != swapChain.Images().size(); ++i)
        {
             // Top level acceleration structure.
            const auto accelerationStructureHandle = accelerationStructure.Handle();
            VkWriteDescriptorSetAccelerationStructureKHR structureInfo = {};
            structureInfo.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
            structureInfo.pNext = nullptr;
            structureInfo.accelerationStructureCount = 1;
            structureInfo.pAccelerationStructures = &accelerationStructureHandle;

            // Uniform buffer
            VkDescriptorBufferInfo uniformBufferInfo = {};
            uniformBufferInfo.buffer = uniformBuffers[i].Buffer().Handle();
            uniformBufferInfo.range = VK_WHOLE_SIZE;

            std::vector<VkWriteDescriptorSet> descriptorWrites =
            {
                descriptorSets.Bind(i, 0, uniformBufferInfo),
                descriptorSets.Bind(i, 1, structureInfo),
            };

            descriptorSets.UpdateDescriptors(i, descriptorWrites);
        }

        VkPushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        pushConstantRange.offset = 0;
        pushConstantRange.size = sizeof(Assets::GPUScene);

        std::vector<DescriptorSetManager*> managers = {
            &Assets::GlobalTexturePool::GetInstance()->GetStorageDescriptorManager(),
            descriptorSetManager_.get(),
            &Assets::GlobalTexturePool::GetInstance()->GetDescriptorManager(),
            &scene.GetSceneBufferDescriptorSetManager()
        };

        pipelineLayout_.reset(new class PipelineLayout(device, managers, static_cast<uint32_t>(uniformBuffers.size()),
                                                       &pushConstantRange, 1));
        const ShaderModule denoiseShader(device, "assets/shaders/Core.PathTracing.comp.slang.spv");
        
        VkComputePipelineCreateInfo pipelineCreateInfo = {};
        pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineCreateInfo.stage = denoiseShader.CreateShaderStage(VK_SHADER_STAGE_COMPUTE_BIT);
        pipelineCreateInfo.layout = pipelineLayout_->Handle();


        Check(vkCreateComputePipelines(device.Handle(), VK_NULL_HANDLE,
                                       1, &pipelineCreateInfo,
                                       NULL, &pipeline_),
              "create compose pipeline");
    }
}
