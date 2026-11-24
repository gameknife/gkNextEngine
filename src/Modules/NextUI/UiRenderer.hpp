#pragma once

#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Vulkan/VulkanFwd.hpp"

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>
#include <span>
#include <vector>
#include <vulkan/vulkan.h>

struct ImDrawData;

namespace NextUI
{
    enum EUiBatchFlags : uint32_t
    {
        UI_BATCH_FLAG_NONE = 0u,
        UI_BATCH_FLAG_STRAIGHT_ALPHA = 1u << 0u,
        UI_BATCH_FLAG_RAW_OUTPUT = 1u << 1u,
    };

    // Binary-identical to ImDrawVert (20 bytes) so Dear ImGui command lists upload via a single memcpy.
    struct FUiVertex
    {
        glm::vec2 position{0.0f, 0.0f};
        glm::vec2 uv{0.0f, 0.0f};
        uint32_t color = 0;
    };
    static_assert(sizeof(FUiVertex) == 20);

    // Per-draw-batch instance payload bound at VK_VERTEX_INPUT_RATE_INSTANCE (8 bytes).
    struct FUiBatchInstance
    {
        uint32_t textureIndex = 0;
        uint32_t flags = UI_BATCH_FLAG_STRAIGHT_ALPHA;
    };
    static_assert(sizeof(FUiBatchInstance) == 8);

    // Retained/custom UI draw command (used by RmlUi and other non-ImGui layers).
    struct FUiCustomDrawCmd
    {
        glm::vec4 clipRect{0.0f, 0.0f, 0.0f, 0.0f}; // minX, minY, maxX, maxY in logical UI space
        bool hasClipRect = false;
        uint64_t textureHandle = 0;
        uint32_t firstIndex = 0;
        uint32_t indexCount = 0;
        int32_t vertexOffset = 0;
    };

    struct FUiCustomDrawStream
    {
        glm::vec2 displayPos{0.0f, 0.0f};
        glm::vec2 displaySize{0.0f, 0.0f};
        std::vector<FUiVertex> vertices;
        std::vector<uint16_t> indices;
        std::vector<FUiCustomDrawCmd> commands;

        void Clear()
        {
            vertices.clear();
            indices.clear();
            commands.clear();
        }

        bool Empty() const { return commands.empty() || indices.empty(); }
    };

    // Per-in-flight-image GPU geometry/instance arena.
    class FUiRenderBuffer final
    {
    public:
        FUiRenderBuffer();
        ~FUiRenderBuffer();
        FUiRenderBuffer(const FUiRenderBuffer&) = delete;
        FUiRenderBuffer& operator=(const FUiRenderBuffer&) = delete;
        FUiRenderBuffer(FUiRenderBuffer&&) noexcept;
        FUiRenderBuffer& operator=(FUiRenderBuffer&&) noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;

        friend class FUiRenderer;
    };

    // Unified modern Vulkan 2D/UI renderer shared by main swapchain UI, pluggable layers (RmlUi),
    // detached editor platform viewports, and headless/remote streaming surfaces.
    class FUiRenderer final
    {
    public:
        FUiRenderer() = default;
        ~FUiRenderer();

        GK_NON_COPIABLE(FUiRenderer)

        void CreateSwapChainResources(const Vulkan::SwapChain& swapChain);
        void DestroySwapChainResources(const Vulkan::Device& device);

        VkPipeline CreatePipelineForRenderPass(const Vulkan::Device& device, VkRenderPass renderPass) const;
        void DestroyPipeline(const Vulkan::Device& device, VkPipeline pipeline) const;

        void RecordSwapChainPass(VkCommandBuffer commandBuffer,
                                 const Vulkan::SwapChain& swapChain,
                                 uint32_t imageIndex,
                                 const ImDrawData* imguiDrawData,
                                 std::span<const FUiCustomDrawStream* const> overlayStreams = {});

        void RecordOffscreenDrawData(VkCommandBuffer commandBuffer,
                                     const Vulkan::Device& device,
                                     FUiRenderBuffer& renderBuffer,
                                     VkExtent2D framebufferExtent,
                                     uint32_t hdrOutputMode,
                                     VkPipeline pipeline,
                                     const ImDrawData* imguiDrawData,
                                     std::span<const FUiCustomDrawStream* const> overlayStreams = {});

        void SetDefaultFontTextureIndex(uint32_t fontTextureIndex) { defaultFontTextureIndex_ = fontTextureIndex; }
        uint32_t DefaultFontTextureIndex() const { return defaultFontTextureIndex_; }

        static uint64_t EncodeTextureHandle(uint32_t textureIndex,
                                            uint32_t batchFlags = UI_BATCH_FLAG_STRAIGHT_ALPHA);
        static bool DecodeTextureHandle(uint64_t handle,
                                        uint32_t& outTextureIndex,
                                        uint32_t& outBatchFlags);

    private:
        void EnsurePipelineLayout(const Vulkan::Device& device);

        std::unique_ptr<Vulkan::RenderPass> renderPass_;
        std::vector<Vulkan::FrameBuffer> frameBuffers_;
        std::vector<FUiRenderBuffer> renderBuffers_;
        VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
        VkPipeline swapChainPipeline_ = VK_NULL_HANDLE;
        uint32_t defaultFontTextureIndex_ = UINT32_MAX;
    };
}
