#pragma once

#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Vulkan/VulkanFwd.hpp"

#include <vulkan/vulkan.h>
#include <unordered_map>

namespace Runtime::Config
{
    struct UserSettings;
}

namespace Vulkan
{
    struct FRenderViewHandle;

    namespace PipelineCommon
    {
        class ZeroBindCustomPushConstantPipeline;
    }

    // Shared with Process.AutoExposure.comp.slang, after the 16-byte view header.
    struct FAutoExposurePushConstants
    {
        uint32_t inputIndex;
        uint32_t width;
        uint32_t height;
        uint32_t reset;
        VkDeviceAddress stateAddress;
        float luminanceScale;
        float deltaSeconds;
        float targetLuminance;
        float minEV;
        float maxEV;
        float speedToDark;
        float speedToLight;
        float compensationEV;
    };
    static_assert(sizeof(FAutoExposurePushConstants) == 56);

    // Meter linear scene color on the GPU. Each view keeps its own EV history;
    // exposure is applied only at display encoding, outside temporal reconstruction.
    class AutoExposurePass final
    {
    public:
        explicit AutoExposurePass(const Device& device);
        ~AutoExposurePass();
        void CreatePipeline(const SwapChain& swapChain);
        void DestroyPipeline();
        VkDeviceAddress Update(VkCommandBuffer commandBuffer, FRenderViewHandle view,
                               uint64_t sceneGeneration, uint32_t inputIndex, VkExtent2D extent,
                               float paperWhiteNit, bool hdr, float deltaSeconds,
                               const Runtime::Config::UserSettings& settings, bool enabled);

    private:
        struct FViewExposure;
        const Device& device_;
        std::unique_ptr<PipelineCommon::ZeroBindCustomPushConstantPipeline> pipeline_;
        std::unordered_map<uint32_t, std::unique_ptr<FViewExposure>> views_;
    };
}
