#pragma once

#include "Vulkan/Vulkan.hpp"
#include "Vulkan/PipelineBase.hpp"

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
	class VulkanBaseRenderer;
}

namespace Vulkan::LegacyDeferred
{
	class ShadingPipeline : public PipelineBase
	{
	public:
		VULKAN_NON_COPIABLE(ShadingPipeline)
		ShadingPipeline(
			const SwapChain& swapChain,
			const VulkanBaseRenderer& baseRenderer,
			const std::vector<Assets::UniformBuffer>& uniformBuffers,
			const Assets::Scene& scene);
	};

}

namespace Vulkan::VoxelTracing
{
	class ShadingPipeline : public PipelineBase
	{
	public:
		VULKAN_NON_COPIABLE(ShadingPipeline)
		ShadingPipeline(
			const SwapChain& swapChain, 
			const VulkanBaseRenderer& baseRenderer,
			const std::vector<Assets::UniformBuffer>& uniformBuffers,
			const Assets::Scene& scene);
	};
}
