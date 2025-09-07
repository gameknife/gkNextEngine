#include "SoftwareModernRenderer.hpp"
#include "Runtime/Engine.hpp"
#include "Utilities/Math.hpp"
#include "Vulkan/SwapChain.hpp"
#include "Vulkan/Window.hpp"
#include "Vulkan/RenderImage.hpp"

namespace Vulkan::LegacyDeferred {

SoftwareModernRenderer::SoftwareModernRenderer(Vulkan::VulkanBaseRenderer& baseRender):LogicRendererBase(baseRender)
{
	
}

SoftwareModernRenderer::~SoftwareModernRenderer()
{
	DeleteSwapChain();
}
	
void SoftwareModernRenderer::CreateSwapChain(const VkExtent2D& extent)
{
	deferredShadingPipeline_.reset(new PipelineCommon::ZeroBindPipeline(SwapChain(), "assets/shaders/Core.SwModern.comp.slang.spv"));
	accumulatePipeline_.reset(new PipelineCommon::ZeroBindCustomPushConstantPipeline(SwapChain(), "assets/shaders/Process.ReProject.comp.slang.spv", 20));
	composePipeline_.reset(new PipelineCommon::ZeroBindPipeline(SwapChain(), "assets/shaders/Process.DenoiseJBF.comp.slang.spv"));

	if (GOption->ReferenceMode)
	{
		prevSingleDiffuseId_ = baseRender_.GetTemporalStorageImage(VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, "prevDiffuseTmp");
		prevSingleSpecularId_ = baseRender_.GetTemporalStorageImage(VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, "prevSpecularTmp");
		prevSingleAlbedoId_ = baseRender_.GetTemporalStorageImage(VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, "prevAlbedoTmp");
	}
	else
	{
		prevSingleDiffuseId_ = Assets::Bindless::RT_SINGLE_PREV_DIFFUSE;
		prevSingleSpecularId_ = Assets::Bindless::RT_SINGLE_PREV_SPECULAR;
		prevSingleAlbedoId_ = Assets::Bindless::RT_SINGLE_PREV_ALBEDO;
	}
}
	
void SoftwareModernRenderer::DeleteSwapChain()
{
	deferredShadingPipeline_.reset();
	accumulatePipeline_.reset();
	composePipeline_.reset();
}

void SoftwareModernRenderer::Render(VkCommandBuffer commandBuffer, uint32_t imageIndex)
{
	baseRender_.InitializeBarriers(commandBuffer);
	
	{
		SCOPED_GPU_TIMER("shadingpass");
		
		// cs shading pass
		deferredShadingPipeline_->BindPipeline(commandBuffer, GetScene(), imageIndex);
		vkCmdDispatch(commandBuffer, SwapChain().RenderExtent().width / 8, SwapChain().RenderExtent().height / 8, 1);	

		// copy to swap-buffer
		baseRender_.GetStorageImage(Assets::Bindless::RT_DENOISED)->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL,
			VK_IMAGE_LAYOUT_GENERAL);
	}

		{
		SCOPED_GPU_TIMER("reproject pass");
		std::array<uint32_t, 5> pushConst { NextEngine::GetInstance()->IsProgressiveRendering(), uint32_t(NextEngine::GetInstance()->GetUserSettings().TemporalFrames),
			   prevSingleDiffuseId_, prevSingleSpecularId_, prevSingleAlbedoId_ };
		accumulatePipeline_->BindPipeline(commandBuffer, pushConst.data());
		vkCmdDispatch(commandBuffer, Utilities::Math::GetSafeDispatchCount(SwapChain().RenderExtent().width, 8), Utilities::Math::GetSafeDispatchCount(SwapChain().RenderExtent().height, 8), 1);

		baseRender_.GetStorageImage(Assets::Bindless::RT_ACCUMLATE_DIFFUSE)->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
		baseRender_.GetStorageImage(Assets::Bindless::RT_ACCUMLATE_SPECULAR)->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
		baseRender_.GetStorageImage(Assets::Bindless::RT_ACCUMLATE_ALBEDO)->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
	}
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
        baseRender_.GetStorageImage(prevSingleDiffuseId_)->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    
        VkImageCopy copyRegion;
        copyRegion.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copyRegion.srcOffset = {0, 0, 0};
        copyRegion.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copyRegion.dstOffset = {0, 0, 0};
        copyRegion.extent = {baseRender_.GetStorageImage(prevSingleDiffuseId_)->GetImage().Extent().width, baseRender_.GetStorageImage(prevSingleDiffuseId_)->GetImage().Extent().height, 1};
    
        vkCmdCopyImage(commandBuffer, baseRender_.GetStorageImage(Assets::Bindless::RT_ACCUMLATE_DIFFUSE)->GetImage().Handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, baseRender_.GetStorageImage(prevSingleDiffuseId_)->GetImage().Handle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

        baseRender_.GetStorageImage(Assets::Bindless::RT_ACCUMLATE_SPECULAR)->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        baseRender_.GetStorageImage(prevSingleSpecularId_)->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
                
        vkCmdCopyImage(commandBuffer, baseRender_.GetStorageImage(Assets::Bindless::RT_ACCUMLATE_SPECULAR)->GetImage().Handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, baseRender_.GetStorageImage(prevSingleSpecularId_)->GetImage().Handle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

        baseRender_.GetStorageImage(Assets::Bindless::RT_ACCUMLATE_ALBEDO)->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        baseRender_.GetStorageImage(prevSingleAlbedoId_)->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        vkCmdCopyImage(commandBuffer, baseRender_.GetStorageImage(Assets::Bindless::RT_ACCUMLATE_ALBEDO)->GetImage().Handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, baseRender_.GetStorageImage(prevSingleAlbedoId_)->GetImage().Handle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);
    }
}
}

Vulkan::VoxelTracing::VoxelTracingRenderer::VoxelTracingRenderer(Vulkan::VulkanBaseRenderer& baseRender):LogicRendererBase(baseRender)
{
}

Vulkan::VoxelTracing::VoxelTracingRenderer::~VoxelTracingRenderer()
{
	DeleteSwapChain();
}

void Vulkan::VoxelTracing::VoxelTracingRenderer::CreateSwapChain(const VkExtent2D& extent)
{
	deferredShadingPipeline_.reset(new PipelineCommon::ZeroBindPipeline(SwapChain(), "assets/shaders/Core.VoxelTracing.comp.slang.spv"));
}

void Vulkan::VoxelTracing::VoxelTracingRenderer::DeleteSwapChain()
{
	deferredShadingPipeline_.reset();
}

void Vulkan::VoxelTracing::VoxelTracingRenderer::Render(VkCommandBuffer commandBuffer, uint32_t imageIndex)
{
	baseRender_.InitializeBarriers(commandBuffer);
	
	{
		SCOPED_GPU_TIMER("shadingpass");

		deferredShadingPipeline_->BindPipeline(commandBuffer, GetScene(), imageIndex);
		vkCmdDispatch(commandBuffer, SwapChain().RenderExtent().width / 8, SwapChain().RenderExtent().height / 8, 1);

		// copy to swap-buffer
		baseRender_.GetStorageImage(Assets::Bindless::RT_DENOISED)->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL,
			VK_IMAGE_LAYOUT_GENERAL);
	}
}
