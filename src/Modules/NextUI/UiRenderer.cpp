#include "Engine/Common/CoreMinimal.hpp"

#include "Modules/NextUI/UiRenderer.hpp"

#include "Engine/Assets/GPU/Texture.hpp"
#include "Engine/Utilities/Exception.hpp"
#include "Engine/Vulkan/Device.hpp"
#include "Engine/Vulkan/GpuResources.hpp"
#include "Engine/Vulkan/GraphicsPipelineBuilder.hpp"
#include "Engine/Vulkan/MemoryAndShader.hpp"
#include "Engine/Vulkan/RenderingPipeline.hpp"
#include "Engine/Vulkan/SwapChain.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>

namespace NextUI
{
    static_assert(sizeof(ImDrawVert) == sizeof(FUiVertex));
    static_assert(offsetof(ImDrawVert, pos) == offsetof(FUiVertex, position));
    static_assert(offsetof(ImDrawVert, uv) == offsetof(FUiVertex, uv));
    static_assert(offsetof(ImDrawVert, col) == offsetof(FUiVertex, color));
    static_assert(sizeof(ImDrawIdx) == sizeof(uint16_t));

    namespace
    {
        constexpr const char* kUiVertexShaderPath = "assets/shaders/UI.Composite.vert.slang.spv";
        constexpr const char* kUiFragmentShaderPath = "assets/shaders/UI.Composite.frag.slang.spv";
        constexpr float kUiHdrReferenceWhiteNit = 203.0f;
        constexpr VkDeviceSize kMinUiBufferCapacity = 16384;

        struct FUiPushConstants
        {
            float scale[2]{};
            float translate[2]{};
            float rotation[4]{};
            uint32_t hdrOutputMode = 0;
            float hdrReferenceWhiteNit = kUiHdrReferenceWhiteNit;
            float padding[2]{};
        };
        static_assert(sizeof(FUiPushConstants) == 48);

        struct FUiRendererRenderState
        {
            VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
            VkPipeline pipeline = VK_NULL_HANDLE;
            VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
        };

        struct FGpuDynamicBuffer
        {
            std::unique_ptr<Vulkan::Buffer> buffer;
            std::unique_ptr<Vulkan::DeviceMemory> memory;
            VkDeviceSize capacity = 0;

            void* EnsureAndMap(const Vulkan::Device& device,
                               const VkDeviceSize requiredBytes,
                               const VkBufferUsageFlags usage,
                               const char* debugName)
            {
                if (requiredBytes == 0)
                {
                    return nullptr;
                }
                if (!buffer || capacity < requiredBytes)
                {
                    const VkDeviceSize newCapacity = std::max({kMinUiBufferCapacity, requiredBytes, capacity * 2});
                    buffer = std::make_unique<Vulkan::Buffer>(device, newCapacity, usage);
                    memory = std::make_unique<Vulkan::DeviceMemory>(buffer->AllocateMemory(
                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT));
                    device.DebugUtils().SetObjectName(buffer->Handle(), debugName);
                    memory->SetName(debugName);
                    capacity = newCapacity;
                }
                return memory->Map(0, requiredBytes);
            }

            void Unmap() const
            {
                if (memory)
                {
                    memory->Unmap();
                }
            }

            VkBuffer Handle() const { return buffer ? buffer->Handle() : VK_NULL_HANDLE; }
        };

        bool ScissorEquals(const VkRect2D& lhs, const VkRect2D& rhs)
        {
            return lhs.offset.x == rhs.offset.x && lhs.offset.y == rhs.offset.y &&
                   lhs.extent.width == rhs.extent.width && lhs.extent.height == rhs.extent.height;
        }

        glm::vec2 TransformUiPointToFramebuffer(const glm::vec2 point,
                                                const FUiPushConstants& pushConsts,
                                                const VkExtent2D& extent)
        {
            const float x = point.x * pushConsts.scale[0] + pushConsts.translate[0];
            const float y = point.y * pushConsts.scale[1] + pushConsts.translate[1];
            const float rx = x * pushConsts.rotation[0] + y * pushConsts.rotation[1];
            const float ry = x * pushConsts.rotation[2] + y * pushConsts.rotation[3];

            return glm::vec2((rx * 0.5f + 0.5f) * static_cast<float>(extent.width),
                             (ry * 0.5f + 0.5f) * static_cast<float>(extent.height));
        }

        bool ComputeFramebufferScissor(const glm::vec4& logicalClip,
                                       const FUiPushConstants& pushConsts,
                                       const VkExtent2D& framebufferExtent,
                                       VkRect2D& outScissor)
        {
            const glm::vec2 corners[4] = {
                TransformUiPointToFramebuffer({logicalClip.x, logicalClip.y}, pushConsts, framebufferExtent),
                TransformUiPointToFramebuffer({logicalClip.z, logicalClip.y}, pushConsts, framebufferExtent),
                TransformUiPointToFramebuffer({logicalClip.z, logicalClip.w}, pushConsts, framebufferExtent),
                TransformUiPointToFramebuffer({logicalClip.x, logicalClip.w}, pushConsts, framebufferExtent),
            };

            float minX = corners[0].x;
            float minY = corners[0].y;
            float maxX = corners[0].x;
            float maxY = corners[0].y;
            for (const glm::vec2& corner : corners)
            {
                minX = std::min(minX, corner.x);
                minY = std::min(minY, corner.y);
                maxX = std::max(maxX, corner.x);
                maxY = std::max(maxY, corner.y);
            }

            const float fbWidth = static_cast<float>(framebufferExtent.width);
            const float fbHeight = static_cast<float>(framebufferExtent.height);
            minX = std::clamp(minX, 0.0f, fbWidth);
            minY = std::clamp(minY, 0.0f, fbHeight);
            maxX = std::clamp(maxX, 0.0f, fbWidth);
            maxY = std::clamp(maxY, 0.0f, fbHeight);
            if (maxX <= minX || maxY <= minY)
            {
                return false;
            }

            const int32_t x0 = std::clamp(static_cast<int32_t>(std::floor(minX)),
                                          0, static_cast<int32_t>(framebufferExtent.width));
            const int32_t y0 = std::clamp(static_cast<int32_t>(std::floor(minY)),
                                          0, static_cast<int32_t>(framebufferExtent.height));
            const int32_t x1 = std::clamp(static_cast<int32_t>(std::ceil(maxX)),
                                          x0, static_cast<int32_t>(framebufferExtent.width));
            const int32_t y1 = std::clamp(static_cast<int32_t>(std::ceil(maxY)),
                                          y0, static_cast<int32_t>(framebufferExtent.height));
            if (x1 <= x0 || y1 <= y0)
            {
                return false;
            }

            outScissor.offset = {x0, y0};
            outScissor.extent = {static_cast<uint32_t>(x1 - x0), static_cast<uint32_t>(y1 - y0)};
            return true;
        }

        void BindUiRenderState(VkCommandBuffer commandBuffer,
                               VkPipeline pipeline,
                               VkPipelineLayout pipelineLayout,
                               VkDescriptorSet bindlessDescriptorSet,
                               VkBuffer vertexBuffer,
                               VkBuffer instanceBuffer,
                               VkBuffer indexBuffer,
                               const VkViewport& viewport,
                               const FUiPushConstants& pushConsts)
        {
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
            if (bindlessDescriptorSet != VK_NULL_HANDLE)
            {
                vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1,
                                        &bindlessDescriptorSet, 0, nullptr);
            }
            if (vertexBuffer != VK_NULL_HANDLE && instanceBuffer != VK_NULL_HANDLE)
            {
                const VkBuffer buffers[2] = {vertexBuffer, instanceBuffer};
                constexpr VkDeviceSize offsets[2] = {0, 0};
                vkCmdBindVertexBuffers(commandBuffer, 0, 2, buffers, offsets);
            }
            if (indexBuffer != VK_NULL_HANDLE)
            {
                vkCmdBindIndexBuffer(commandBuffer, indexBuffer, 0, VK_INDEX_TYPE_UINT16);
            }
            vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
            vkCmdPushConstants(commandBuffer, pipelineLayout,
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                               0, sizeof(FUiPushConstants), &pushConsts);
        }
    }

    struct FUiRenderBuffer::Impl
    {
        struct FDrawOp
        {
            enum class EType : uint8_t
            {
                IndexedBatch,
                Callback,
            };

            EType type = EType::IndexedBatch;
            VkRect2D scissor{};
            uint32_t firstIndex = 0;
            uint32_t indexCount = 0;
            int32_t vertexOffset = 0;
            uint32_t batchInstanceIndex = 0;
            uint32_t textureIndex = 0;
            uint32_t flags = 0;
            const ImDrawList* drawList = nullptr;
            const ImDrawCmd* drawCmd = nullptr;
        };

        FGpuDynamicBuffer vertexBuffer;
        FGpuDynamicBuffer indexBuffer;
        FGpuDynamicBuffer instanceBuffer;
        std::vector<FUiBatchInstance> instances;
        std::vector<FDrawOp> drawOps;
    };

    FUiRenderBuffer::FUiRenderBuffer() : impl_(std::make_unique<Impl>()) {}
    FUiRenderBuffer::~FUiRenderBuffer() = default;
    FUiRenderBuffer::FUiRenderBuffer(FUiRenderBuffer&&) noexcept = default;
    FUiRenderBuffer& FUiRenderBuffer::operator=(FUiRenderBuffer&&) noexcept = default;

    FUiRenderer::~FUiRenderer() = default;

    uint64_t FUiRenderer::EncodeTextureHandle(const uint32_t textureIndex, const uint32_t batchFlags)
    {
        return (static_cast<uint64_t>(batchFlags) << 32u) | static_cast<uint64_t>(textureIndex + 1u);
    }

    bool FUiRenderer::DecodeTextureHandle(const uint64_t handle,
                                          uint32_t& outTextureIndex,
                                          uint32_t& outBatchFlags)
    {
        const uint64_t encodedIndex = handle & 0xFFFFFFFFull;
        if (encodedIndex == 0)
        {
            return false;
        }

        const uint64_t textureIndex = encodedIndex - 1u;
        if (textureIndex >= Assets::GlobalTexturePool::kMaxBindlessSlots)
        {
            return false;
        }

        outTextureIndex = static_cast<uint32_t>(textureIndex);
        outBatchFlags = static_cast<uint32_t>(handle >> 32u);
        return true;
    }

    void FUiRenderer::EnsurePipelineLayout(const Vulkan::Device& device)
    {
        if (pipelineLayout_ != VK_NULL_HANDLE)
        {
            return;
        }

        const auto* texturePool = Assets::GlobalTexturePool::GetInstance();
        if (texturePool == nullptr)
        {
            Throw(std::runtime_error("global texture pool is unavailable for UI pipeline"));
        }

        const VkDescriptorSetLayout bindlessLayout = texturePool->Layout();

        VkPushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        pushConstantRange.offset = 0;
        pushConstantRange.size = sizeof(FUiPushConstants);

        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &bindlessLayout;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushConstantRange;

        Vulkan::Check(vkCreatePipelineLayout(device.Handle(), &layoutInfo, nullptr, &pipelineLayout_),
                      "create ui pipeline layout");
    }

    VkPipeline FUiRenderer::CreatePipelineForRenderPass(const Vulkan::Device& device,
                                                        const VkRenderPass renderPass) const
    {
        if (renderPass == VK_NULL_HANDLE || pipelineLayout_ == VK_NULL_HANDLE)
        {
            return VK_NULL_HANDLE;
        }

        const Vulkan::ShaderModule vertexShader(device, kUiVertexShaderPath);
        const Vulkan::ShaderModule fragmentShader(device, kUiFragmentShaderPath);

        const std::array<VkVertexInputBindingDescription, 2> bindings = {{
            {0, sizeof(FUiVertex), VK_VERTEX_INPUT_RATE_VERTEX},
            {1, sizeof(FUiBatchInstance), VK_VERTEX_INPUT_RATE_INSTANCE},
        }};

        const std::array<VkVertexInputAttributeDescription, 5> attributes = {{
            {0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(FUiVertex, position)},
            {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(FUiVertex, uv)},
            {2, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof(FUiVertex, color)},
            {3, 1, VK_FORMAT_R32_UINT, offsetof(FUiBatchInstance, textureIndex)},
            {4, 1, VK_FORMAT_R32_UINT, offsetof(FUiBatchInstance, flags)},
        }};

        return Vulkan::GraphicsPipelineBuilder(device)
            .SetShaders(vertexShader, fragmentShader)
            .SetVertexInput(bindings, attributes)
            .SetDynamicViewportAndScissor()
            .SetPremultipliedAlphaBlend()
            .Build(pipelineLayout_, renderPass, "create ui composite pipeline");
    }

    void FUiRenderer::DestroyPipeline(const Vulkan::Device& device, const VkPipeline pipeline) const
    {
        if (pipeline != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(device.Handle(), pipeline, nullptr);
        }
    }

    void FUiRenderer::CreateSwapChainResources(const Vulkan::SwapChain& swapChain)
    {
        const auto& device = swapChain.Device();
        DestroySwapChainResources(device);

        renderPass_ = std::make_unique<Vulkan::RenderPass>(swapChain, VK_ATTACHMENT_LOAD_OP_LOAD);
        renderPass_->SetDebugName("UI Composite Render Pass");

        EnsurePipelineLayout(device);
        swapChainPipeline_ = CreatePipelineForRenderPass(device, renderPass_->Handle());

        frameBuffers_.reserve(swapChain.ImageViews().size());
        for (const auto& imageView : swapChain.ImageViews())
        {
            frameBuffers_.emplace_back(swapChain.Extent(), *imageView, *renderPass_, false);
        }
        renderBuffers_.resize(swapChain.Images().size());
    }

    void FUiRenderer::DestroySwapChainResources(const Vulkan::Device& device)
    {
        if (swapChainPipeline_ != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(device.Handle(), swapChainPipeline_, nullptr);
            swapChainPipeline_ = VK_NULL_HANDLE;
        }
        if (pipelineLayout_ != VK_NULL_HANDLE)
        {
            vkDestroyPipelineLayout(device.Handle(), pipelineLayout_, nullptr);
            pipelineLayout_ = VK_NULL_HANDLE;
        }
        frameBuffers_.clear();
        renderPass_.reset();
        renderBuffers_.clear();
    }

    void FUiRenderer::RecordSwapChainPass(VkCommandBuffer commandBuffer,
                                          const Vulkan::SwapChain& swapChain,
                                          const uint32_t imageIndex,
                                          const ImDrawData* imguiDrawData,
                                          std::span<const FUiCustomDrawStream* const> overlayStreams)
    {
        if (!renderPass_ || swapChainPipeline_ == VK_NULL_HANDLE ||
            imageIndex >= frameBuffers_.size() || imageIndex >= renderBuffers_.size())
        {
            return;
        }

        const bool hasImGuiWork = imguiDrawData != nullptr && imguiDrawData->CmdListsCount > 0;
        bool hasOverlayWork = false;
        for (const FUiCustomDrawStream* stream : overlayStreams)
        {
            if (stream != nullptr && !stream->Empty())
            {
                hasOverlayWork = true;
                break;
            }
        }
        if (!hasImGuiWork && !hasOverlayWork)
        {
            return;
        }

        VkRenderPassBeginInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = renderPass_->Handle();
        renderPassInfo.framebuffer = frameBuffers_[imageIndex].Handle();
        renderPassInfo.renderArea.offset = {0, 0};
        renderPassInfo.renderArea.extent = swapChain.Extent();
        renderPassInfo.clearValueCount = 0;
        renderPassInfo.pClearValues = nullptr;

        vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
        RecordOffscreenDrawData(commandBuffer, swapChain.Device(), renderBuffers_[imageIndex], swapChain.Extent(),
                                swapChain.HDROutputMode(), swapChainPipeline_, imguiDrawData, overlayStreams);
        vkCmdEndRenderPass(commandBuffer);
    }

    void FUiRenderer::RecordOffscreenDrawData(VkCommandBuffer commandBuffer,
                                              const Vulkan::Device& device,
                                              FUiRenderBuffer& renderBuffer,
                                              const VkExtent2D framebufferExtent,
                                              const uint32_t hdrOutputMode,
                                              const VkPipeline pipeline,
                                              const ImDrawData* imguiDrawData,
                                              std::span<const FUiCustomDrawStream* const> overlayStreams)
    {
        if (pipeline == VK_NULL_HANDLE || pipelineLayout_ == VK_NULL_HANDLE ||
            framebufferExtent.width == 0 || framebufferExtent.height == 0)
        {
            return;
        }

        glm::vec2 displayPos{0.0f, 0.0f};
        glm::vec2 displaySize{0.0f, 0.0f};
        if (imguiDrawData != nullptr && imguiDrawData->DisplaySize.x > 0.0f && imguiDrawData->DisplaySize.y > 0.0f)
        {
            displayPos = {imguiDrawData->DisplayPos.x, imguiDrawData->DisplayPos.y};
            displaySize = {imguiDrawData->DisplaySize.x, imguiDrawData->DisplaySize.y};
        }
        else
        {
            for (const FUiCustomDrawStream* stream : overlayStreams)
            {
                if (stream != nullptr && stream->displaySize.x > 0.0f && stream->displaySize.y > 0.0f)
                {
                    displayPos = stream->displayPos;
                    displaySize = stream->displaySize;
                    break;
                }
            }
        }
        if (displaySize.x <= 0.0f || displaySize.y <= 0.0f)
        {
            return;
        }

        size_t totalVertices = 0;
        size_t totalIndices = 0;
        size_t estimatedCommands = 0;
        if (imguiDrawData != nullptr)
        {
            for (int listIndex = 0; listIndex < imguiDrawData->CmdListsCount; ++listIndex)
            {
                if (const ImDrawList* drawList = imguiDrawData->CmdLists[listIndex])
                {
                    totalVertices += static_cast<size_t>(std::max(0, drawList->VtxBuffer.Size));
                    totalIndices += static_cast<size_t>(std::max(0, drawList->IdxBuffer.Size));
                    estimatedCommands += static_cast<size_t>(std::max(0, drawList->CmdBuffer.Size));
                }
            }
        }
        for (const FUiCustomDrawStream* stream : overlayStreams)
        {
            if (stream != nullptr && !stream->Empty())
            {
                totalVertices += stream->vertices.size();
                totalIndices += stream->indices.size();
                estimatedCommands += stream->commands.size();
            }
        }
        if (estimatedCommands == 0)
        {
            return;
        }

        FUiPushConstants pushConsts{};
        pushConsts.scale[0] = 2.0f / displaySize.x;
        pushConsts.scale[1] = 2.0f / displaySize.y;
        pushConsts.translate[0] = -1.0f - displayPos.x * pushConsts.scale[0];
        pushConsts.translate[1] = -1.0f - displayPos.y * pushConsts.scale[1];
        pushConsts.rotation[0] = 1.0f;
        pushConsts.rotation[1] = 0.0f;
        pushConsts.rotation[2] = 0.0f;
        pushConsts.rotation[3] = 1.0f;
#if ANDROID
        pushConsts.rotation[0] = 0.0f;
        pushConsts.rotation[1] = -1.0f;
        pushConsts.rotation[2] = 1.0f;
        pushConsts.rotation[3] = 0.0f;
#endif
        pushConsts.hdrOutputMode = hdrOutputMode;
        pushConsts.hdrReferenceWhiteNit = kUiHdrReferenceWhiteNit;

        FUiRenderBuffer::Impl& buffers = *renderBuffer.impl_;
        using FDrawOp = FUiRenderBuffer::Impl::FDrawOp;

        auto* mappedVertices = static_cast<FUiVertex*>(buffers.vertexBuffer.EnsureAndMap(
            device, totalVertices * sizeof(FUiVertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, "UI Vertex Buffer"));
        auto* mappedIndices = static_cast<uint16_t*>(buffers.indexBuffer.EnsureAndMap(
            device, totalIndices * sizeof(uint16_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, "UI Index Buffer"));

        buffers.instances.clear();
        buffers.instances.reserve(estimatedCommands);
        buffers.drawOps.clear();
        buffers.drawOps.reserve(estimatedCommands);

        auto AppendBatch = [&](const VkRect2D& scissor,
                               const uint32_t textureIndex,
                               const uint32_t flags,
                               const uint32_t firstIndex,
                               const uint32_t indexCount,
                               const int32_t vertexOffset)
        {
            if (indexCount == 0)
            {
                return;
            }
            if (!buffers.drawOps.empty())
            {
                FDrawOp& prev = buffers.drawOps.back();
                if (prev.type == FDrawOp::EType::IndexedBatch &&
                    ScissorEquals(prev.scissor, scissor) &&
                    prev.textureIndex == textureIndex &&
                    prev.flags == flags &&
                    prev.vertexOffset == vertexOffset &&
                    prev.firstIndex + prev.indexCount == firstIndex)
                {
                    prev.indexCount += indexCount;
                    return;
                }
            }

            const uint32_t batchInstanceIndex = static_cast<uint32_t>(buffers.instances.size());
            buffers.instances.push_back(FUiBatchInstance{textureIndex, flags});
            buffers.drawOps.push_back(FDrawOp{
                .type = FDrawOp::EType::IndexedBatch,
                .scissor = scissor,
                .firstIndex = firstIndex,
                .indexCount = indexCount,
                .vertexOffset = vertexOffset,
                .batchInstanceIndex = batchInstanceIndex,
                .textureIndex = textureIndex,
                .flags = flags,
            });
        };

        uint32_t globalVertexBase = 0;
        uint32_t globalIndexBase = 0;

        if (imguiDrawData != nullptr)
        {
            for (int listIndex = 0; listIndex < imguiDrawData->CmdListsCount; ++listIndex)
            {
                const ImDrawList* drawList = imguiDrawData->CmdLists[listIndex];
                if (drawList == nullptr)
                {
                    continue;
                }

                const uint32_t listVertexCount = static_cast<uint32_t>(std::max(0, drawList->VtxBuffer.Size));
                const uint32_t listIndexCount = static_cast<uint32_t>(std::max(0, drawList->IdxBuffer.Size));
                if (listVertexCount > 0 && mappedVertices != nullptr)
                {
                    std::memcpy(mappedVertices + globalVertexBase,
                                drawList->VtxBuffer.Data,
                                static_cast<size_t>(listVertexCount) * sizeof(FUiVertex));
                }
                if (listIndexCount > 0 && mappedIndices != nullptr)
                {
                    std::memcpy(mappedIndices + globalIndexBase,
                                drawList->IdxBuffer.Data,
                                static_cast<size_t>(listIndexCount) * sizeof(uint16_t));
                }

                for (int cmdIndex = 0; cmdIndex < drawList->CmdBuffer.Size; ++cmdIndex)
                {
                    const ImDrawCmd* drawCmd = &drawList->CmdBuffer[cmdIndex];
                    if (drawCmd->UserCallback != nullptr)
                    {
                        buffers.drawOps.push_back(FDrawOp{
                            .type = FDrawOp::EType::Callback,
                            .drawList = drawList,
                            .drawCmd = drawCmd,
                        });
                        continue;
                    }

                    uint32_t textureIndex = defaultFontTextureIndex_;
                    uint32_t batchFlags = UI_BATCH_FLAG_STRAIGHT_ALPHA;
                    const uint64_t rawHandle = static_cast<uint64_t>(static_cast<uintptr_t>(drawCmd->GetTexID()));
                    if (!DecodeTextureHandle(rawHandle, textureIndex, batchFlags))
                    {
                        continue;
                    }

                    VkRect2D scissor{};
                    const glm::vec4 clipRect{
                        drawCmd->ClipRect.x,
                        drawCmd->ClipRect.y,
                        drawCmd->ClipRect.z,
                        drawCmd->ClipRect.w,
                    };
                    if (!ComputeFramebufferScissor(clipRect, pushConsts, framebufferExtent, scissor))
                    {
                        continue;
                    }

                    AppendBatch(scissor,
                                textureIndex,
                                batchFlags,
                                globalIndexBase + drawCmd->IdxOffset,
                                drawCmd->ElemCount,
                                static_cast<int32_t>(globalVertexBase + drawCmd->VtxOffset));
                }

                globalVertexBase += listVertexCount;
                globalIndexBase += listIndexCount;
            }
        }

        const VkRect2D fullScissor{{0, 0}, framebufferExtent};
        for (const FUiCustomDrawStream* stream : overlayStreams)
        {
            if (stream == nullptr || stream->Empty())
            {
                continue;
            }

            const uint32_t streamVertexCount = static_cast<uint32_t>(stream->vertices.size());
            const uint32_t streamIndexCount = static_cast<uint32_t>(stream->indices.size());
            if (streamVertexCount > 0 && mappedVertices != nullptr)
            {
                std::memcpy(mappedVertices + globalVertexBase,
                            stream->vertices.data(),
                            static_cast<size_t>(streamVertexCount) * sizeof(FUiVertex));
            }
            if (streamIndexCount > 0 && mappedIndices != nullptr)
            {
                std::memcpy(mappedIndices + globalIndexBase,
                            stream->indices.data(),
                            static_cast<size_t>(streamIndexCount) * sizeof(uint16_t));
            }

            for (const FUiCustomDrawCmd& cmd : stream->commands)
            {
                uint32_t textureIndex = defaultFontTextureIndex_;
                uint32_t batchFlags = UI_BATCH_FLAG_NONE;
                if (!DecodeTextureHandle(cmd.textureHandle, textureIndex, batchFlags))
                {
                    continue;
                }

                VkRect2D scissor = fullScissor;
                if (cmd.hasClipRect &&
                    !ComputeFramebufferScissor(cmd.clipRect, pushConsts, framebufferExtent, scissor))
                {
                    continue;
                }

                AppendBatch(scissor,
                            textureIndex,
                            batchFlags,
                            globalIndexBase + cmd.firstIndex,
                            cmd.indexCount,
                            static_cast<int32_t>(globalVertexBase) + cmd.vertexOffset);
            }

            globalVertexBase += streamVertexCount;
            globalIndexBase += streamIndexCount;
        }

        buffers.vertexBuffer.Unmap();
        buffers.indexBuffer.Unmap();

        if (buffers.drawOps.empty())
        {
            return;
        }

        if (!buffers.instances.empty())
        {
            const VkDeviceSize instanceBytes = buffers.instances.size() * sizeof(FUiBatchInstance);
            if (void* mappedInstances = buffers.instanceBuffer.EnsureAndMap(
                    device, instanceBytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, "UI Batch Instance Buffer"))
            {
                std::memcpy(mappedInstances, buffers.instances.data(), instanceBytes);
                buffers.instanceBuffer.Unmap();
            }
        }

        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(framebufferExtent.width);
        viewport.height = static_cast<float>(framebufferExtent.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;

        const VkDescriptorSet bindlessDescriptorSet = Assets::GlobalTexturePool::GetInstance()->DescriptorSet(0);
        BindUiRenderState(commandBuffer,
                          pipeline,
                          pipelineLayout_,
                          bindlessDescriptorSet,
                          buffers.vertexBuffer.Handle(),
                          buffers.instanceBuffer.Handle(),
                          buffers.indexBuffer.Handle(),
                          viewport,
                          pushConsts);

        FUiRendererRenderState renderState{};
        renderState.commandBuffer = commandBuffer;
        renderState.pipeline = pipeline;
        renderState.pipelineLayout = pipelineLayout_;
        if (ImGui::GetCurrentContext() != nullptr)
        {
            ImGui::GetPlatformIO().Renderer_RenderState = &renderState;
        }

        bool hasCurrentScissor = false;
        VkRect2D currentScissor{};

        for (const FDrawOp& op : buffers.drawOps)
        {
            if (op.type == FDrawOp::EType::IndexedBatch)
            {
                if (!hasCurrentScissor || !ScissorEquals(currentScissor, op.scissor))
                {
                    vkCmdSetScissor(commandBuffer, 0, 1, &op.scissor);
                    currentScissor = op.scissor;
                    hasCurrentScissor = true;
                }
                vkCmdDrawIndexed(commandBuffer,
                                 op.indexCount,
                                 1,
                                 op.firstIndex,
                                 op.vertexOffset,
                                 op.batchInstanceIndex);
                continue;
            }

            if (op.drawCmd == nullptr || op.drawCmd->UserCallback == nullptr)
            {
                continue;
            }

            if (op.drawCmd->UserCallback != ImDrawCallback_ResetRenderState)
            {
                op.drawCmd->UserCallback(op.drawList, op.drawCmd);
            }
            BindUiRenderState(commandBuffer,
                              pipeline,
                              pipelineLayout_,
                              bindlessDescriptorSet,
                              buffers.vertexBuffer.Handle(),
                              buffers.instanceBuffer.Handle(),
                              buffers.indexBuffer.Handle(),
                              viewport,
                              pushConsts);
            hasCurrentScissor = false;
        }

        if (ImGui::GetCurrentContext() != nullptr)
        {
            ImGui::GetPlatformIO().Renderer_RenderState = nullptr;
        }
    }
}
