#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Rendering/Exposure/AutoExposurePass.hpp"

#include "Engine/Rendering/PipelineCommon/CommonComputePipeline.hpp"
#include "Engine/Rendering/RenderView.hpp"
#include "Engine/Runtime/Config/UserSettings.hpp"
#include "Engine/Vulkan/GpuResources.hpp"
#include "Engine/Vulkan/MemoryAndShader.hpp"
#include "Engine/Vulkan/SyncAndTiming.hpp"

namespace Vulkan
{
    struct AutoExposurePass::FViewExposure
    {
        // Buffer must be destroyed before its backing allocation.
        std::unique_ptr<DeviceMemory> memory;
        std::unique_ptr<Buffer> buffer;
        uint32_t generation = 0;
        uint64_t sceneGeneration = 0;
        bool initialized = false;
    };

    AutoExposurePass::AutoExposurePass(const Device& device) : device_(device)
    {
    }
    AutoExposurePass::~AutoExposurePass() = default;

    void AutoExposurePass::CreatePipeline(const SwapChain& swapChain)
    {
        pipeline_ = std::make_unique<PipelineCommon::ZeroBindCustomPushConstantPipeline>(
            swapChain, "assets/shaders/Process.AutoExposure.comp.slang.spv",
            static_cast<uint32_t>(sizeof(FAutoExposurePushConstants)));
    }

    void AutoExposurePass::DestroyPipeline()
    {
        pipeline_.reset();
    }

    VkDeviceAddress AutoExposurePass::Update(
        VkCommandBuffer commandBuffer, FRenderViewHandle view, uint64_t sceneGeneration,
        uint32_t inputIndex, VkExtent2D extent, float paperWhiteNit, bool hdr, float deltaSeconds,
        const Runtime::Config::UserSettings& settings, bool enabled)
    {
        if (!enabled || !pipeline_ || extent.width == 0 || extent.height == 0)
        {
            if (auto found = views_.find(view.bankBase); found != views_.end())
            {
                found->second->initialized = false;
            }
            return 0;
        }

        auto& history = views_[view.bankBase];
        if (!history)
        {
            history = std::make_unique<FViewExposure>();
            history->buffer = std::make_unique<Buffer>(device_, sizeof(glm::vec4),
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);
            history->memory = std::make_unique<DeviceMemory>(history->buffer->AllocateMemory(
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                {.AllocateFlags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT}));
        }

        const bool reset = !history->initialized || history->generation != view.generation ||
            history->sceneGeneration != sceneGeneration;
        const float minEV = std::min(settings.AutoExposureMinEV, settings.AutoExposureMaxEV);
        const float maxEV = std::max(settings.AutoExposureMinEV, settings.AutoExposureMaxEV);
        // Match the existing display pipeline: HDR white is scene radiance 230,
        // while SDR tone mapping consumes the pre-exposed value directly.
        const float luminanceScale = hdr
            ? 1.0f / (230.0f * std::max(paperWhiteNit / 40000.0f, 1.0e-6f)) : 1.0f;
        const FAutoExposurePushConstants params{
            inputIndex, extent.width, extent.height, reset ? 1u : 0u,
            history->buffer->GetDeviceAddress(), luminanceScale,
            std::clamp(deltaSeconds, 0.0f, 0.25f), settings.AutoExposureTargetLuminance,
            minEV, maxEV, settings.AutoExposureSpeedToDark, settings.AutoExposureSpeedToLight,
            settings.AutoExposureCompensationEV,
        };
        BufferMemoryBarrier::Insert(commandBuffer,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            history->buffer->Handle(), VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
        pipeline_->BindPipeline(commandBuffer, &params);
        vkCmdDispatch(commandBuffer, 1, 1, 1);
        BufferMemoryBarrier::Insert(commandBuffer,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            history->buffer->Handle(), VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);

        history->generation = view.generation;
        history->sceneGeneration = sceneGeneration;
        history->initialized = true;
        return history->buffer->GetDeviceAddress();
    }
}
