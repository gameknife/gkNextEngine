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
    PathTracingPipeline::PathTracingPipeline(const SwapChain& swapChain):PipelineBase(swapChain)
    {
         // Create descriptor pool/sets.
        const auto& device = swapChain.Device();
        
        VkPushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        pushConstantRange.offset = 0;
        pushConstantRange.size = sizeof(Assets::GPUScene);

        std::vector<DescriptorSetManager*> managers = {
            &Assets::GlobalTexturePool::GetInstance()->GetDescriptorManager()
        };

        pipelineLayout_.reset(new class PipelineLayout(device, managers, 1,
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
