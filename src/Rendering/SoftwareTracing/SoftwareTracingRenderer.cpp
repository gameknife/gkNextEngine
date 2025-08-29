#include "SoftwareTracingRenderer.hpp"
#include "Rendering/PipelineCommon/CommonComputePipeline.hpp"

#include "Vulkan/SwapChain.hpp"
#include "Vulkan/RenderImage.hpp"

#include "Utilities/Math.hpp"

namespace Vulkan::ModernDeferred {

SoftwareTracingRenderer::SoftwareTracingRenderer(Vulkan::VulkanBaseRenderer& baseRender):LogicRendererBase(baseRender)
{
	
}

SoftwareTracingRenderer::~SoftwareTracingRenderer()
{
	SoftwareTracingRenderer::DeleteSwapChain();
}

void SoftwareTracingRenderer::CreateSwapChain(const VkExtent2D& extent)
{
	deferredShadingPipeline_.reset(new PipelineCommon::ZeroBindPipeline(SwapChain(), "assets/shaders/Core.SwTracing.comp.slang.spv"));
	accumulatePipeline_.reset(new PipelineCommon::ZeroBindPipeline(SwapChain(), "assets/shaders/Process.ReProject.comp.slang.spv"));
	composePipeline_.reset(new PipelineCommon::ZeroBindPipeline(SwapChain(), "assets/shaders/Process.DenoiseJBF.comp.slang.spv"));
}

void SoftwareTracingRenderer::DeleteSwapChain()
{
	deferredShadingPipeline_.reset();
	accumulatePipeline_.reset();
	composePipeline_.reset();
}

void SoftwareTracingRenderer::Render(VkCommandBuffer commandBuffer, uint32_t imageIndex)
{
	baseRender_.InitializeBarriers(commandBuffer);
	{
		SCOPED_GPU_TIMER("shadingpass");
		// cs shading pass
		deferredShadingPipeline_->BindPipeline(commandBuffer, GetScene(), imageIndex);
		vkCmdDispatch(commandBuffer, SwapChain().RenderExtent().width / 8, SwapChain().RenderExtent().height / 8, 1);

		baseRender_.GetStorageImage(Assets::Bindless::RT_SINGLE_DIFFUSE)->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
	}

	{
		SCOPED_GPU_TIMER("reproject pass");
		accumulatePipeline_->BindPipeline(commandBuffer, GetScene(), imageIndex);
		vkCmdDispatch(commandBuffer, Utilities::Math::GetSafeDispatchCount(SwapChain().RenderExtent().width, 8), Utilities::Math::GetSafeDispatchCount(SwapChain().RenderExtent().height, 8), 1);

		baseRender_.GetStorageImage(Assets::Bindless::RT_ACCUMLATE_DIFFUSE)->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
		baseRender_.GetStorageImage(Assets::Bindless::RT_ACCUMLATE_SPECULAR)->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
		baseRender_.GetStorageImage(Assets::Bindless::RT_ACCUMLATE_ALBEDO)->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);	}
	{
		SCOPED_GPU_TIMER("compose pass");
		
		SwapChain().InsertBarrierToWrite(commandBuffer, imageIndex);
		
		composePipeline_->BindPipeline(commandBuffer, GetScene(), imageIndex);
		vkCmdDispatch(commandBuffer, SwapChain().RenderExtent().width / 8, SwapChain().RenderExtent().height / 8, 1);

		SwapChain().InsertBarrierToPresent(commandBuffer, imageIndex);
	}
	
	{
        SCOPED_GPU_TIMER("copy pass");
        baseRender_.GetStorageImage(Assets::Bindless::RT_ACCUMLATE_DIFFUSE)->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        baseRender_.GetStorageImage(Assets::Bindless::RT_SINGLE_PREV_DIFFUSE)->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    
        VkImageCopy copyRegion;
        copyRegion.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copyRegion.srcOffset = {0, 0, 0};
        copyRegion.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copyRegion.dstOffset = {0, 0, 0};
        copyRegion.extent = {baseRender_.GetStorageImage(Assets::Bindless::RT_SINGLE_PREV_DIFFUSE)->GetImage().Extent().width, baseRender_.GetStorageImage(Assets::Bindless::RT_SINGLE_PREV_DIFFUSE)->GetImage().Extent().height, 1};
    
        vkCmdCopyImage(commandBuffer, baseRender_.GetStorageImage(Assets::Bindless::RT_ACCUMLATE_DIFFUSE)->GetImage().Handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, baseRender_.GetStorageImage(Assets::Bindless::RT_SINGLE_PREV_DIFFUSE)->GetImage().Handle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

        baseRender_.GetStorageImage(Assets::Bindless::RT_ACCUMLATE_SPECULAR)->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        baseRender_.GetStorageImage(Assets::Bindless::RT_SINGLE_PREV_SPECULAR)->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
                
        vkCmdCopyImage(commandBuffer, baseRender_.GetStorageImage(Assets::Bindless::RT_ACCUMLATE_SPECULAR)->GetImage().Handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, baseRender_.GetStorageImage(Assets::Bindless::RT_SINGLE_PREV_SPECULAR)->GetImage().Handle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

        baseRender_.GetStorageImage(Assets::Bindless::RT_ACCUMLATE_ALBEDO)->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        baseRender_.GetStorageImage(Assets::Bindless::RT_SINGLE_PREV_ALBEDO)->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        vkCmdCopyImage(commandBuffer, baseRender_.GetStorageImage(Assets::Bindless::RT_ACCUMLATE_ALBEDO)->GetImage().Handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, baseRender_.GetStorageImage(Assets::Bindless::RT_SINGLE_PREV_ALBEDO)->GetImage().Handle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);
    }
}
}
