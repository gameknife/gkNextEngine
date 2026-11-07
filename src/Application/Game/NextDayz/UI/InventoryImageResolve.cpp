#include "Engine/Common/CoreMinimal.hpp"
#include "InventoryImageResolve.hpp"

#include "Engine/Rendering/PipelineCommon/CommonComputePipeline.hpp"
#include "Engine/Rendering/VulkanBaseRenderer.hpp"
#include "Engine/Vulkan/GpuResources.hpp"

namespace NextDayz
{
    bool ResolveInventoryImage(Vulkan::VulkanBaseRenderer& renderer, Vulkan::RenderView& view,
        VkCommandBuffer commandBuffer,
        std::unique_ptr<Vulkan::PipelineCommon::ZeroBindCustomPushConstantPipeline>& pipeline,
        Vulkan::RenderImage& output, bool& initialized, VkOffset2D sourceOffset)
    {
        using namespace Vulkan::PipelineCommon;
        // Compose has finished reading diffuse; reuse it as a separate resolve target.
        const auto* resolved = renderer.GetStorageImage(view.RtBankBase() + Assets::Bindless::RT_SINGLE_DIFFUSE);
        if (!resolved) return false;
        struct FResolveParams
        {
            VkOffset2D sourceOffset;
            VkExtent2D outputExtent;
            uint32_t sampleScale;
        };
        const VkExtent2D extent = output.GetImage().Extent();
        const FResolveParams params{sourceOffset, extent, inventoryImageSampleScale};
        if (!pipeline)
        {
            pipeline = std::make_unique<ZeroBindCustomPushConstantPipeline>(
                renderer.SwapChain(), "assets/shaders/NextDayz.InventoryPreview.comp.slang.spv", static_cast<uint32_t>(sizeof(params)));
        }
        renderer.TransitionViewImages(commandBuffer, view, {
            {Assets::Bindless::RT_OBJECTID_0, ERenderStage::Compute, EResourceAccess::ShaderRead},
            {Assets::Bindless::RT_SCENE_COLOR, ERenderStage::Compute, EResourceAccess::ShaderRead},
            {Assets::Bindless::RT_SINGLE_DIFFUSE, ERenderStage::Compute, EResourceAccess::ShaderWrite},
        }, "inventory SSAA resolve");
        pipeline->BindPipeline(commandBuffer, &params, view.RtBankBase());
        vkCmdDispatch(commandBuffer, (extent.width + 7u) / 8u, (extent.height + 7u) / 8u, 1u);

        renderer.TransitionViewImages(commandBuffer, view, {
            {Assets::Bindless::RT_SINGLE_DIFFUSE, ERenderStage::Transfer, EResourceAccess::TransferRead,
             VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL},
        }, "copy inventory SSAA output");
        output.InsertBarrier(commandBuffer, initialized ? VK_ACCESS_SHADER_READ_BIT : 0, VK_ACCESS_TRANSFER_WRITE_BIT,
            initialized ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        VkImageCopy copy{};
        copy.srcSubresource = copy.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.extent = {extent.width, extent.height, 1};
        vkCmdCopyImage(commandBuffer, resolved->GetImage().Handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            output.GetImage().Handle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
        output.InsertBarrier(commandBuffer, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        initialized = true;
        return true;
    }
}
