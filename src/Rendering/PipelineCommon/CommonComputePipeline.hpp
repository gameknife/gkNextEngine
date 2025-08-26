#pragma once

#include "Vulkan/Vulkan.hpp"
#include "Vulkan/ImageView.hpp"
#include "Vulkan/PipelineBase.hpp"

#include <memory>
#include <vector>

namespace Assets
{
	class Scene;
	class UniformBuffer;
}

namespace Vulkan
{
	class DepthBuffer;
	class PipelineLayout;
	class RenderPass;
	class SwapChain;
	class DescriptorSetManager;
	class DeviceProcedures;
	class Buffer;
	class VulkanBaseRenderer;
}

namespace Vulkan::RayTracing
{
	class TopLevelAccelerationStructure;
}

namespace Vulkan::PipelineCommon
{
	class ZeroBindPipeline : public PipelineBase
	{
	public:
		VULKAN_NON_COPIABLE(ZeroBindPipeline)
		ZeroBindPipeline(const SwapChain& swapChain, const char* shaderfile);

		void BindPipeline(VkCommandBuffer commandBuffer, const Assets::Scene& scene, uint32_t imageIndex);
	};
	
	class AccumulatePipeline : public PipelineBase
	{
	public:
		VULKAN_NON_COPIABLE(AccumulatePipeline)
		
		AccumulatePipeline(
			const SwapChain& swapChain, const VulkanBaseRenderer& baseRender,
			const ImageView& sourceImageView, const ImageView& prevImageView, const ImageView& targetImageView,
			const std::vector<Assets::UniformBuffer>& uniformBuffers,
			const Assets::Scene& scene);
	};
	
	class FinalComposePipeline : public PipelineBase
	{
	public:
		VULKAN_NON_COPIABLE(FinalComposePipeline)
	
		FinalComposePipeline(
			const SwapChain& swapChain, 
			const VulkanBaseRenderer& baseRender,
			const std::vector<Assets::UniformBuffer>& uniformBuffers);
	};

	class SimpleComposePipeline : public PipelineBase
	{
	public:
		VULKAN_NON_COPIABLE(SimpleComposePipeline)
	
		SimpleComposePipeline(
			const SwapChain& swapChain, 
			const ImageView& sourceImageView,
			const std::vector<Assets::UniformBuffer>& uniformBuffers);
	};

	class BufferClearPipeline  : public PipelineBase
	{
	public:
		VULKAN_NON_COPIABLE(BufferClearPipeline)
	
		BufferClearPipeline(
			const SwapChain& swapChain, const VulkanBaseRenderer& baseRender);
	};

	class VisualDebuggerPipeline : public PipelineBase
	{
	public:
		VULKAN_NON_COPIABLE(VisualDebuggerPipeline)
	
		VisualDebuggerPipeline(
			const SwapChain& swapChain,
			const VulkanBaseRenderer& baseRender,
			const std::vector<Assets::UniformBuffer>& uniformBuffers);
	};

	class HardwareGPULightBakePipeline  : public PipelineBase
	{
	public:
		VULKAN_NON_COPIABLE(HardwareGPULightBakePipeline)
	
		HardwareGPULightBakePipeline(
			const SwapChain& swapChain,
			const DeviceProcedures& deviceProcedures,
			const RayTracing::TopLevelAccelerationStructure& accelerationStructure,
			const std::vector<Assets::UniformBuffer>& uniformBuffers,
			const Assets::Scene& scene);
	};

	class SoftwareGPULightBakePipeline  : public PipelineBase
	{
	public:
		VULKAN_NON_COPIABLE(SoftwareGPULightBakePipeline)
		SoftwareGPULightBakePipeline(
			const SwapChain& swapChain,
			const std::vector<Assets::UniformBuffer>& uniformBuffers,
			const Assets::Scene& scene);
	};

	class GPUCullPipeline  : public PipelineBase
	{
	public:
		VULKAN_NON_COPIABLE(GPUCullPipeline)
		GPUCullPipeline(const SwapChain& swapChain, const VulkanBaseRenderer& baseRender, const std::vector<Assets::UniformBuffer>& uniformBuffers, const Assets::Scene& scene);
	};

	class VisibilityPipeline : public PipelineBase
	{
	public:
		VULKAN_NON_COPIABLE(VisibilityPipeline)

		VisibilityPipeline(
			const SwapChain& swapChain, 
			const DepthBuffer& depthBuffer,
			const std::vector<Assets::UniformBuffer>& uniformBuffers,
			const Assets::Scene& scene);
		~VisibilityPipeline();
		
		const Vulkan::RenderPass& RenderPass() const { return *renderPass_; }

	private:
		std::unique_ptr<Vulkan::RenderPass> renderPass_;
		std::unique_ptr<Vulkan::RenderPass> swapRenderPass_;
	};

	class GraphicsPipeline : public PipelineBase
	{
	public:

		VULKAN_NON_COPIABLE(GraphicsPipeline)

		GraphicsPipeline(
			const SwapChain& swapChain, 
			const DepthBuffer& depthBuffer,
			const std::vector<Assets::UniformBuffer>& uniformBuffers,
			const Assets::Scene& scene,
			bool isWireFrame);
		~GraphicsPipeline();
		
		const class RenderPass& RenderPass() const { return *renderPass_; }

	private:
		std::unique_ptr<class RenderPass> renderPass_;
	};
}
