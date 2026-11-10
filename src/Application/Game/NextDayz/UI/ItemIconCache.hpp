#pragma once

#include "Engine/Rendering/Preview/RenderViewServices.hpp"

#include <memory>
#include <cstdint>
#include <string_view>

namespace NextDayz
{
    struct FLootVisual;

    // Runtime thumbnails share the loot catalog's geometry and materials. Generated
    // images live in GPU memory and the application's writable cache, never assets/.
    class ItemIconCache final : public Vulkan::IRenderViewProvider
    {
    public:
        explicit ItemIconCache(Vulkan::VulkanBaseRenderer& renderer);
        ~ItemIconCache() override;

        // Returns a bindless sample slot, or UINT32_MAX while queued/unavailable.
        uint32_t Request(std::string_view itemId, const FLootVisual& visual);
        uint32_t ReadyCount() const;
        uint32_t RenderedCount() const;
        uint32_t CacheHitCount() const;
        bool IsReady(std::string_view itemId) const;

        void BeforeNextFrame() override;
        void OnMainSceneChanged() override;
        void OnSwapChainResourcesInvalidated(bool releaseSampledOutputs) override;
        bool HasWork() const override;
        bool ScheduleViews(VkCommandBuffer commandBuffer, uint32_t imageIndex) override;

    private:
        struct FState;
        std::unique_ptr<FState> state_;
    };

    ItemIconCache& GetItemIconCache(Vulkan::VulkanBaseRenderer& renderer);
}
