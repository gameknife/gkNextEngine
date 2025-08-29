#pragma once

#include "Rendering/VulkanBaseRenderer.hpp"
#include "Rendering/RayTraceBaseRenderer.hpp"

#include <memory>

namespace Vulkan
{
	class RenderImage;
}

namespace Vulkan::PipelineCommon
{
	class AccumulatePipeline;
}

namespace Assets
{
	class Scene;
	struct UniformBufferObject;
	class UniformBuffer;
}

namespace Vulkan::ModernDeferred
{
	class SoftwareTracingRenderer final : public Vulkan::LogicRendererBase
	{
	public:

		VULKAN_NON_COPIABLE(SoftwareTracingRenderer)
		
		SoftwareTracingRenderer(Vulkan::VulkanBaseRenderer& baseRender);
		~SoftwareTracingRenderer();
		
		void CreateSwapChain(const VkExtent2D& extent) override;
		void DeleteSwapChain() override;
		void Render(VkCommandBuffer commandBuffer, uint32_t imageIndex) override;

	private:
		std::unique_ptr<PipelineCommon::ZeroBindPipeline> deferredShadingPipeline_;
		std::unique_ptr<PipelineCommon::ZeroBindPipeline> accumulatePipeline_;
		std::unique_ptr<PipelineCommon::ZeroBindPipeline> composePipeline_;
	};

}
