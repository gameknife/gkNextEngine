#pragma once

#include "Vulkan/VulkanBaseRenderer.hpp"
#include "Vulkan/RayTracing/TopLevelAccelerationStructure.hpp"
#include "Vulkan/RayTracing/BottomLevelAccelerationStructure.hpp"
#include "RayTracingProperties.hpp"
#include "Vulkan/PipelineCommon/CommonComputePipeline.hpp"

namespace Vulkan
{
	namespace PipelineCommon
	{
		class AccumulatePipeline;
	}

	class CommandBuffers;
	class Buffer;
	class DeviceMemory;
	class Image;
	class ImageView;
}

namespace Vulkan::RayTracing
{
	class RayQueryPipeline;

	class RayTraceBaseRenderer : public Vulkan::VulkanBaseRenderer
	{
	public:

		VULKAN_NON_COPIABLE(RayTraceBaseRenderer);
	
	protected:

		RayTraceBaseRenderer(const char* rendererType, const WindowConfig& windowConfig, VkPresentModeKHR presentMode, bool enableValidationLayers);
		virtual ~RayTraceBaseRenderer();

		void SetPhysicalDeviceImpl(VkPhysicalDevice physicalDevice,
			std::vector<const char*>& requiredExtensions,
			VkPhysicalDeviceFeatures& deviceFeatures,
			void* nextDeviceFeatures) override;
		
		void OnDeviceSet() override;
		void CreateAccelerationStructures();
		void DeleteAccelerationStructures();
		void CreateSwapChain() override;
		void DeleteSwapChain() override;

		virtual void AfterRenderCmd() override;

		virtual void OnPreLoadScene() override;
		virtual void OnPostLoadScene() override;

		virtual bool GetFocusDistance(float& distance) const override;
		virtual bool GetLastRaycastResult(Assets::RayCastResult& result) const override;
		virtual void SetRaycastRay(glm::vec3 org, glm::vec3 dir) const override;

	protected:
		void CreateBottomLevelStructures(VkCommandBuffer commandBuffer);
		void CreateTopLevelStructures(VkCommandBuffer commandBuffer);
		
		std::unique_ptr<class RayTracingProperties> rayTracingProperties_;
	
		std::vector<BottomLevelAccelerationStructure> bottomAs_;
		std::unique_ptr<Buffer> bottomBuffer_;
		std::unique_ptr<DeviceMemory> bottomBufferMemory_;
		std::unique_ptr<Buffer> bottomScratchBuffer_;
		std::unique_ptr<DeviceMemory> bottomScratchBufferMemory_;
		std::vector<TopLevelAccelerationStructure> topAs_;
		std::unique_ptr<Buffer> topBuffer_;
		std::unique_ptr<DeviceMemory> topBufferMemory_;
		std::unique_ptr<Buffer> topScratchBuffer_;
		std::unique_ptr<DeviceMemory> topScratchBufferMemory_;
		std::unique_ptr<Buffer> instancesBuffer_;
		std::unique_ptr<DeviceMemory> instancesBufferMemory_;
		
		Assets::RayCastResult cameraCenterCastResult_;
		mutable Assets::RayCastContext cameraCenterCastContext_;
		std::unique_ptr<Assets::RayCastBuffer> rayCastBuffer_;
		std::unique_ptr<PipelineCommon::RayCastPipeline> raycastPipeline_;
	};

}
