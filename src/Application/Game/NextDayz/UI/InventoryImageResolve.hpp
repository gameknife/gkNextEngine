#pragma once

#include <memory>
#include <vulkan/vulkan.h>

namespace Vulkan
{
    class VulkanBaseRenderer;
    class RenderView;
    class RenderImage;
    namespace PipelineCommon
    {
        class ZeroBindCustomPushConstantPipeline;
    }
}

namespace NextDayz
{
    inline constexpr uint32_t inventoryImageSampleScale = 2;

    // Resolve geometry coverage and color together, producing straight alpha for ImGui.
    bool ResolveInventoryImage(Vulkan::VulkanBaseRenderer& renderer, Vulkan::RenderView& view,
        VkCommandBuffer commandBuffer,
        std::unique_ptr<Vulkan::PipelineCommon::ZeroBindCustomPushConstantPipeline>& pipeline,
        Vulkan::RenderImage& output, bool& initialized, VkOffset2D sourceOffset = {});
}
