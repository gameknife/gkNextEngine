#pragma once

#include "Vulkan/PipelineCommon/CommonComputePipeline.hpp"
#include "Vulkan/RayTracing/RayTraceBaseRenderer.hpp"

#if WITH_OIDN
#include <oidn.hpp>
#endif

namespace Vulkan
{
	namespace PipelineCommon
	{
		class AccumulatePipeline;
		class FinalComposePipeline;
		class RayCastPipeline;
	}

	class CommandBuffers;
	class Buffer;
	class DeviceMemory;
	class Image;
	class ImageView;
	class RenderImage;
}

namespace Vulkan::RayTracing
{
	class RayQueryPipeline;

	class RayQueryRenderer final : public Vulkan::LogicRendererBase
	{
	public:

		VULKAN_NON_COPIABLE(RayQueryRenderer);
	
	public:

		RayQueryRenderer(Vulkan::VulkanBaseRenderer& baseRender);
		virtual ~RayQueryRenderer();

		void SetPhysicalDeviceImpl(VkPhysicalDevice physicalDevice,
			std::vector<const char*>& requiredExtensions,
			VkPhysicalDeviceFeatures& deviceFeatures,
			void* nextDeviceFeatures);// override;
		
		void OnDeviceSet() override;
		void CreateSwapChain(const VkExtent2D& extent) override;
		void DeleteSwapChain() override;
		void Render(VkCommandBuffer commandBuffer, uint32_t imageIndex) override;
		void BeforeNextFrame() override;
	
	private:
		void CreateOutputImage(const VkExtent2D& extent);

		// individual textures
		std::unique_ptr<RenderImage> rtPingPong0;
		
		std::unique_ptr<RayQueryPipeline> rayTracingPipeline_;
		std::unique_ptr<PipelineCommon::AccumulatePipeline> accumulatePipeline_;
		std::unique_ptr<PipelineCommon::FinalComposePipeline> composePipelineNonDenoiser_;

#if WITH_OIDN
		std::unique_ptr<RenderImage> rtDenoise0_;
		std::unique_ptr<RenderImage> rtDenoise1_;
		std::unique_ptr<PipelineCommon::FinalComposePipeline> composePipelineDenoiser_;
		oidn::DeviceRef device;
		oidn::FilterRef filter;
#endif
	};

}
