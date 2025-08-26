#include "SoftwareModernRenderer.hpp"
#include "SoftwareModernPipeline.hpp"
#include "Vulkan/PipelineLayout.hpp"
#include "Vulkan/RenderPass.hpp"
#include "Vulkan/SwapChain.hpp"
#include "Vulkan/Window.hpp"
#include "Vulkan/RenderImage.hpp"
#include "Assets/Model.hpp"
#include "Assets/Scene.hpp"

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
	composePipeline_.reset(new Vulkan::PipelineCommon::SimpleComposePipeline(SwapChain(), baseRender_.rtAccumlatedDiffuse->GetImageView(), UniformBuffers()));
}
	
void SoftwareModernRenderer::DeleteSwapChain()
{
	composePipeline_.reset();
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
		baseRender_.rtDenoised->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL,
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
	deferredShadingPipeline_.reset(new ShadingPipeline(SwapChain(), baseRender_, UniformBuffers(), GetScene()));
	composePipeline_.reset(new Vulkan::PipelineCommon::SimpleComposePipeline(SwapChain(), baseRender_.rtAccumlatedDiffuse->GetImageView(), UniformBuffers()));
}

void Vulkan::VoxelTracing::VoxelTracingRenderer::DeleteSwapChain()
{
	deferredShadingPipeline_.reset();
	composePipeline_.reset();
}

void Vulkan::VoxelTracing::VoxelTracingRenderer::Render(VkCommandBuffer commandBuffer, uint32_t imageIndex)
{
	baseRender_.InitializeBarriers(commandBuffer);
	
	{
		SCOPED_GPU_TIMER("shadingpass");

		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, deferredShadingPipeline_->Handle());
		deferredShadingPipeline_->PipelineLayout().BindDescriptorSets(commandBuffer, imageIndex);
		vkCmdDispatch(commandBuffer, SwapChain().RenderExtent().width / 8, SwapChain().RenderExtent().height / 8, 1);	

		// copy to swap-buffer
		baseRender_.rtDenoised->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL,
			VK_IMAGE_LAYOUT_GENERAL);
	}
}
