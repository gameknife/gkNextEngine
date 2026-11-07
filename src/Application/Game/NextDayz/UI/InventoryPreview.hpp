#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include "Engine/Rendering/Preview/RenderViewServices.hpp"
#include "Engine/Rendering/RenderView.hpp"

namespace Vulkan::PipelineCommon
{
    class ZeroBindCustomPushConstantPipeline;
}

namespace NextDayz
{
    // A thumbnail-style view of the live player's meshes, with its own scene and lighting.
    // The renderer owns the provider and its GPU resources; requests last one frame.
    class InventoryPreview final : public Vulkan::IRenderViewProvider
    {
    public:
        explicit InventoryPreview(Vulkan::VulkanBaseRenderer& renderer);
        ~InventoryPreview() override;

        void Request(const std::shared_ptr<Assets::Node>& root, VkExtent2D extent, float yaw, float distance);
        bool IsReady() const { return ready_; }
        static uint32_t SampleSlot();
        void BeforeNextFrame() override;
        void OnMainSceneChanged() override;
        void OnSwapChainResourcesInvalidated(bool releaseSampledOutputs) override;
        bool HasWork() const override { return requested_ && !root_.expired(); }
        bool ScheduleViews(VkCommandBuffer commandBuffer, uint32_t imageIndex) override;
        void ClearFrameRequests() override { requested_ = false; }

    private:
        struct FMeshKey
        {
            uint32_t model;
            std::array<uint32_t, 16> materials;
            bool operator==(const FMeshKey&) const = default;
        };
        struct FRetiredScene
        {
            std::unique_ptr<Assets::Scene> scene;
            uint64_t releaseAfterSubmitSerial;
        };

        void RebuildScene(const std::vector<Assets::Node*>& sources, const std::vector<FMeshKey>& keys);
        Vulkan::VulkanBaseRenderer& renderer_;
        Vulkan::FRenderViewHandle view_;
        Vulkan::FRenderViewTargetResources target_;
        std::unique_ptr<Vulkan::PipelineCommon::ZeroBindCustomPushConstantPipeline> resolvePipeline_;
        std::weak_ptr<Assets::Node> root_;
        std::unique_ptr<Assets::Scene> scene_;
        std::vector<std::shared_ptr<Assets::Node>> nodes_;
        std::vector<FMeshKey> meshKeys_;
        std::vector<FRetiredScene> retiredScenes_;
        VkExtent2D extent_{384, 640};
        float yaw_ = 0.35f;
        float distance_ = 3.4f;
        bool requested_ = false;
        bool ready_ = false;
        bool outputInitialized_ = false;
    };

    InventoryPreview& GetInventoryPreview(Vulkan::VulkanBaseRenderer& renderer);
}
