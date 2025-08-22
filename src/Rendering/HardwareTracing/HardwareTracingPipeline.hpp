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
	class Buffer;
	class DescriptorSetManager;
	class VulkanBaseRenderer;

	namespace RayTracing
	{
		class TopLevelAccelerationStructure;
	}
}

namespace Vulkan::HybridDeferred
{
	class HardwareTracingPipeline : public PipelineBase
	{
	public:
		VULKAN_NON_COPIABLE(HardwareTracingPipeline)
	
		HardwareTracingPipeline(
			const SwapChain& swapChain, const RayTracing::TopLevelAccelerationStructure& accelerationStructure,
			const VulkanBaseRenderer& baseRenderer,
			const std::vector<Assets::UniformBuffer>& uniformBuffers,
			const Assets::Scene& scene);
	};

}
