#include "SoftwareModernRenderer.hpp"
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
	//composePipeline_.reset(new Vulkan::PipelineCommon::SimpleComposePipeline(SwapChain(), baseRender_.rtAccumlatedDiffuse->GetImageView(), UniformBuffers()));
}
	
void SoftwareModernRenderer::DeleteSwapChain()
{
	deferredShadingPipeline_.reset();
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
