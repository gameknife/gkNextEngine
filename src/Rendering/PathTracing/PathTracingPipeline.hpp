#pragma once

#include "Vulkan/Vulkan.hpp"
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
	class DescriptorSetManager;
	class ImageView;
	class PipelineLayout;
	class SwapChain;
	class DeviceProcedures;
	class VulkanBaseRenderer;
}

namespace Vulkan::RayTracing
{
	class TopLevelAccelerationStructure;
	
	class PathTracingPipeline : public PipelineBase
	{
	public:
		VULKAN_NON_COPIABLE(PathTracingPipeline)
		PathTracingPipeline(
			const SwapChain& swapChain);
	};
	
}
