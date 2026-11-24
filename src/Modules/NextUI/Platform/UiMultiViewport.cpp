#include "Engine/Common/CoreMinimal.hpp"

#include "Modules/NextUI/Platform/UiMultiViewport.hpp"

#include "Engine/Rendering/VulkanBaseRenderer.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Utilities/Exception.hpp"
#include "Engine/Vulkan/Device.hpp"
#include "Engine/Vulkan/Instance.hpp"
#include "Engine/Vulkan/SwapChain.hpp"
#include "Engine/Vulkan/WindowSurface.hpp"
#include "Modules/NextUI/UiSystem.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <unordered_set>

namespace NextUI
{
    namespace
    {
        struct FUiPlatformFrame
        {
            VkCommandPool commandPool = VK_NULL_HANDLE;
            VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
            VkFence fence = VK_NULL_HANDLE;
            VkImage backbuffer = VK_NULL_HANDLE;
            VkImageView backbufferView = VK_NULL_HANDLE;
            VkFramebuffer framebuffer = VK_NULL_HANDLE;
        };

        struct FUiPlatformFrameSemaphores
        {
            VkSemaphore imageAcquiredSemaphore = VK_NULL_HANDLE;
            VkSemaphore renderCompleteSemaphore = VK_NULL_HANDLE;
        };

        struct FUiPlatformWindow
        {
            int width = 0;
            int height = 0;
            VkSwapchainKHR swapchain = VK_NULL_HANDLE;
            VkSurfaceKHR surface = VK_NULL_HANDLE;
            VkSurfaceFormatKHR surfaceFormat{};
            VkPresentModeKHR presentMode = static_cast<VkPresentModeKHR>(~0);
            VkRenderPass renderPass = VK_NULL_HANDLE;
            bool useDynamicRendering = false;
            bool clearEnable = true;
            VkClearValue clearValue{};
            uint32_t frameIndex = 0;
            uint32_t imageCount = 0;
            uint32_t semaphoreCount = 0;
            uint32_t semaphoreIndex = 0;
            ImVector<FUiPlatformFrame> frames;
            ImVector<FUiPlatformFrameSemaphores> frameSemaphores;
        };

        struct FUiPlatformViewportData
        {
            FUiPlatformWindow window;
            bool windowOwned = false;
            bool swapChainNeedRebuild = false;
            bool swapChainSuboptimal = false;
        };

        VkSurfaceFormatKHR SelectPlatformSurfaceFormat(VkPhysicalDevice physicalDevice,
                                                       VkSurfaceKHR surface,
                                                       const VkFormat* requestFormats,
                                                       int requestFormatsCount,
                                                       VkColorSpaceKHR requestColorSpace)
        {
            uint32_t availableCount = 0;
            Vulkan::Check(vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &availableCount, nullptr),
                          "query ui platform surface format count");

            ImVector<VkSurfaceFormatKHR> availableFormats;
            availableFormats.resize(static_cast<int>(availableCount));
            Vulkan::Check(vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &availableCount,
                                                               availableFormats.Data),
                          "query ui platform surface formats");

            if (availableCount == 1)
            {
                if (availableFormats[0].format == VK_FORMAT_UNDEFINED)
                {
                    VkSurfaceFormatKHR format{};
                    format.format = requestFormats[0];
                    format.colorSpace = requestColorSpace;
                    return format;
                }
                return availableFormats[0];
            }

            for (int requestedIndex = 0; requestedIndex < requestFormatsCount; ++requestedIndex)
            {
                for (uint32_t availableIndex = 0; availableIndex < availableCount; ++availableIndex)
                {
                    if (availableFormats[availableIndex].format == requestFormats[requestedIndex] &&
                        availableFormats[availableIndex].colorSpace == requestColorSpace)
                    {
                        return availableFormats[availableIndex];
                    }
                }
            }

            return availableFormats[0];
        }

        VkPresentModeKHR SelectPlatformPresentMode(VkPhysicalDevice physicalDevice,
                                                   VkSurfaceKHR surface,
                                                   const VkPresentModeKHR* requestModes,
                                                   int requestModesCount)
        {
            uint32_t availableCount = 0;
            Vulkan::Check(vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &availableCount, nullptr),
                          "query ui platform present mode count");

            ImVector<VkPresentModeKHR> availableModes;
            availableModes.resize(static_cast<int>(availableCount));
            Vulkan::Check(vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &availableCount,
                                                                    availableModes.Data),
                          "query ui platform present modes");

            for (int requestedIndex = 0; requestedIndex < requestModesCount; ++requestedIndex)
            {
                for (uint32_t availableIndex = 0; availableIndex < availableCount; ++availableIndex)
                {
                    if (requestModes[requestedIndex] == availableModes[availableIndex])
                    {
                        return requestModes[requestedIndex];
                    }
                }
            }

            return VK_PRESENT_MODE_FIFO_KHR;
        }

        void DestroyPlatformFrame(VkDevice device, FUiPlatformFrame& frame)
        {
            if (frame.fence != VK_NULL_HANDLE)
            {
                vkDestroyFence(device, frame.fence, nullptr);
                frame.fence = VK_NULL_HANDLE;
            }
            if (frame.commandBuffer != VK_NULL_HANDLE && frame.commandPool != VK_NULL_HANDLE)
            {
                vkFreeCommandBuffers(device, frame.commandPool, 1, &frame.commandBuffer);
                frame.commandBuffer = VK_NULL_HANDLE;
            }
            if (frame.commandPool != VK_NULL_HANDLE)
            {
                vkDestroyCommandPool(device, frame.commandPool, nullptr);
                frame.commandPool = VK_NULL_HANDLE;
            }
            if (frame.backbufferView != VK_NULL_HANDLE)
            {
                vkDestroyImageView(device, frame.backbufferView, nullptr);
                frame.backbufferView = VK_NULL_HANDLE;
            }
            if (frame.framebuffer != VK_NULL_HANDLE)
            {
                vkDestroyFramebuffer(device, frame.framebuffer, nullptr);
                frame.framebuffer = VK_NULL_HANDLE;
            }
        }

        void DestroyPlatformFrameSemaphores(VkDevice device, FUiPlatformFrameSemaphores& semaphores)
        {
            if (semaphores.imageAcquiredSemaphore != VK_NULL_HANDLE)
            {
                vkDestroySemaphore(device, semaphores.imageAcquiredSemaphore, nullptr);
                semaphores.imageAcquiredSemaphore = VK_NULL_HANDLE;
            }
            if (semaphores.renderCompleteSemaphore != VK_NULL_HANDLE)
            {
                vkDestroySemaphore(device, semaphores.renderCompleteSemaphore, nullptr);
                semaphores.renderCompleteSemaphore = VK_NULL_HANDLE;
            }
        }

        void CreatePlatformWindowCommandBuffers(VkDevice device, FUiPlatformWindow& window, uint32_t queueFamily)
        {
            for (uint32_t imageIndex = 0; imageIndex < window.imageCount; ++imageIndex)
            {
                FUiPlatformFrame& frame = window.frames[imageIndex];

                VkCommandPoolCreateInfo commandPoolInfo{};
                commandPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
                commandPoolInfo.queueFamilyIndex = queueFamily;
                Vulkan::Check(vkCreateCommandPool(device, &commandPoolInfo, nullptr, &frame.commandPool),
                              "create ui platform viewport command pool");

                VkCommandBufferAllocateInfo commandBufferInfo{};
                commandBufferInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
                commandBufferInfo.commandPool = frame.commandPool;
                commandBufferInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
                commandBufferInfo.commandBufferCount = 1;
                Vulkan::Check(vkAllocateCommandBuffers(device, &commandBufferInfo, &frame.commandBuffer),
                              "allocate ui platform viewport command buffer");

                VkFenceCreateInfo fenceInfo{};
                fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
                fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
                Vulkan::Check(vkCreateFence(device, &fenceInfo, nullptr, &frame.fence),
                              "create ui platform viewport fence");
            }

            for (uint32_t semaphoreIndex = 0; semaphoreIndex < window.semaphoreCount; ++semaphoreIndex)
            {
                FUiPlatformFrameSemaphores& semaphores = window.frameSemaphores[semaphoreIndex];
                VkSemaphoreCreateInfo semaphoreInfo{};
                semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
                Vulkan::Check(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &semaphores.imageAcquiredSemaphore),
                              "create ui platform viewport acquire semaphore");
                Vulkan::Check(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &semaphores.renderCompleteSemaphore),
                              "create ui platform viewport render semaphore");
            }
        }

        void CreatePlatformWindowSwapChain(VkPhysicalDevice physicalDevice,
                                           VkDevice device,
                                           FUiPlatformWindow& window,
                                           int width,
                                           int height,
                                           uint32_t minImageCount)
        {
            VkSwapchainKHR oldSwapChain = window.swapchain;
            window.swapchain = VK_NULL_HANDLE;

            Vulkan::Check(vkDeviceWaitIdle(device), "wait device idle for ui platform viewport resize");

            for (int imageIndex = 0; imageIndex < window.frames.Size; ++imageIndex)
            {
                DestroyPlatformFrame(device, window.frames[imageIndex]);
            }
            for (int semaphoreIndex = 0; semaphoreIndex < window.frameSemaphores.Size; ++semaphoreIndex)
            {
                DestroyPlatformFrameSemaphores(device, window.frameSemaphores[semaphoreIndex]);
            }
            window.frames.clear();
            window.frameSemaphores.clear();
            window.imageCount = 0;
            if (window.renderPass != VK_NULL_HANDLE)
            {
                vkDestroyRenderPass(device, window.renderPass, nullptr);
                window.renderPass = VK_NULL_HANDLE;
            }

            VkSurfaceCapabilitiesKHR surfaceCapabilities{};
            Vulkan::Check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, window.surface, &surfaceCapabilities),
                          "query ui platform surface capabilities");

            VkSwapchainCreateInfoKHR swapChainInfo{};
            swapChainInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
            swapChainInfo.surface = window.surface;
            swapChainInfo.minImageCount = minImageCount;
            swapChainInfo.imageFormat = window.surfaceFormat.format;
            swapChainInfo.imageColorSpace = window.surfaceFormat.colorSpace;
            swapChainInfo.imageArrayLayers = 1;
            swapChainInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
            swapChainInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
            swapChainInfo.preTransform =
                (surfaceCapabilities.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)
                    ? VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR
                    : surfaceCapabilities.currentTransform;
            swapChainInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
            swapChainInfo.presentMode = window.presentMode;
            swapChainInfo.clipped = VK_TRUE;
            swapChainInfo.oldSwapchain = oldSwapChain;
            if (swapChainInfo.minImageCount < surfaceCapabilities.minImageCount)
            {
                swapChainInfo.minImageCount = surfaceCapabilities.minImageCount;
            }
            else if (surfaceCapabilities.maxImageCount != 0 &&
                     swapChainInfo.minImageCount > surfaceCapabilities.maxImageCount)
            {
                swapChainInfo.minImageCount = surfaceCapabilities.maxImageCount;
            }

            if (surfaceCapabilities.currentExtent.width == 0xffffffff)
            {
                swapChainInfo.imageExtent.width = window.width = width;
                swapChainInfo.imageExtent.height = window.height = height;
            }
            else
            {
                swapChainInfo.imageExtent.width = window.width = static_cast<int>(surfaceCapabilities.currentExtent.width);
                swapChainInfo.imageExtent.height = window.height = static_cast<int>(surfaceCapabilities.currentExtent.height);
            }

            Vulkan::Check(vkCreateSwapchainKHR(device, &swapChainInfo, nullptr, &window.swapchain),
                          "create ui platform viewport swapchain");
            Vulkan::Check(vkGetSwapchainImagesKHR(device, window.swapchain, &window.imageCount, nullptr),
                          "query ui platform viewport swapchain image count");

            std::array<VkImage, 16> backbuffers{};
            if (window.imageCount > backbuffers.size())
            {
                Throw(std::runtime_error("ui platform viewport swapchain exceeds supported image count"));
            }
            Vulkan::Check(vkGetSwapchainImagesKHR(device, window.swapchain, &window.imageCount, backbuffers.data()),
                          "query ui platform viewport swapchain images");

            window.semaphoreCount = window.imageCount + 1;
            window.frames.resize(static_cast<int>(window.imageCount));
            window.frameSemaphores.resize(static_cast<int>(window.semaphoreCount));
            std::fill_n(window.frames.Data, window.frames.Size, FUiPlatformFrame{});
            std::fill_n(window.frameSemaphores.Data, window.frameSemaphores.Size, FUiPlatformFrameSemaphores{});
            for (uint32_t imageIndex = 0; imageIndex < window.imageCount; ++imageIndex)
            {
                window.frames[imageIndex].backbuffer = backbuffers[imageIndex];
            }

            if (oldSwapChain != VK_NULL_HANDLE)
            {
                vkDestroySwapchainKHR(device, oldSwapChain, nullptr);
            }

            VkAttachmentDescription attachment{};
            attachment.format = window.surfaceFormat.format;
            attachment.samples = VK_SAMPLE_COUNT_1_BIT;
            attachment.loadOp = window.clearEnable ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

            VkAttachmentReference colorAttachment{};
            colorAttachment.attachment = 0;
            colorAttachment.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

            VkSubpassDescription subpass{};
            subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
            subpass.colorAttachmentCount = 1;
            subpass.pColorAttachments = &colorAttachment;

            VkSubpassDependency dependency{};
            dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
            dependency.dstSubpass = 0;
            dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

            VkRenderPassCreateInfo renderPassInfo{};
            renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
            renderPassInfo.attachmentCount = 1;
            renderPassInfo.pAttachments = &attachment;
            renderPassInfo.subpassCount = 1;
            renderPassInfo.pSubpasses = &subpass;
            renderPassInfo.dependencyCount = 1;
            renderPassInfo.pDependencies = &dependency;
            Vulkan::Check(vkCreateRenderPass(device, &renderPassInfo, nullptr, &window.renderPass),
                          "create ui platform viewport render pass");

            VkImageViewCreateInfo imageViewInfo{};
            imageViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            imageViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            imageViewInfo.format = window.surfaceFormat.format;
            imageViewInfo.components.r = VK_COMPONENT_SWIZZLE_R;
            imageViewInfo.components.g = VK_COMPONENT_SWIZZLE_G;
            imageViewInfo.components.b = VK_COMPONENT_SWIZZLE_B;
            imageViewInfo.components.a = VK_COMPONENT_SWIZZLE_A;
            imageViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            imageViewInfo.subresourceRange.levelCount = 1;
            imageViewInfo.subresourceRange.layerCount = 1;

            for (uint32_t imageIndex = 0; imageIndex < window.imageCount; ++imageIndex)
            {
                FUiPlatformFrame& frame = window.frames[imageIndex];
                imageViewInfo.image = frame.backbuffer;
                Vulkan::Check(vkCreateImageView(device, &imageViewInfo, nullptr, &frame.backbufferView),
                              "create ui platform viewport image view");
            }

            VkFramebufferCreateInfo framebufferInfo{};
            framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebufferInfo.renderPass = window.renderPass;
            framebufferInfo.attachmentCount = 1;
            framebufferInfo.width = static_cast<uint32_t>(window.width);
            framebufferInfo.height = static_cast<uint32_t>(window.height);
            framebufferInfo.layers = 1;

            for (uint32_t imageIndex = 0; imageIndex < window.imageCount; ++imageIndex)
            {
                FUiPlatformFrame& frame = window.frames[imageIndex];
                VkImageView attachmentView = frame.backbufferView;
                framebufferInfo.pAttachments = &attachmentView;
                Vulkan::Check(vkCreateFramebuffer(device, &framebufferInfo, nullptr, &frame.framebuffer),
                              "create ui platform viewport framebuffer");
            }
        }

        void CreateOrResizePlatformWindow(VkPhysicalDevice physicalDevice,
                                          VkDevice device,
                                          FUiPlatformWindow& window,
                                          uint32_t queueFamily,
                                          int width,
                                          int height,
                                          uint32_t minImageCount)
        {
            CreatePlatformWindowSwapChain(physicalDevice, device, window, width, height, minImageCount);
            CreatePlatformWindowCommandBuffers(device, window, queueFamily);
        }

        void DestroyPlatformWindow(VkInstance instance, VkDevice device, FUiPlatformWindow& window)
        {
            Vulkan::Check(vkDeviceWaitIdle(device), "wait device idle for ui platform viewport destroy");

            for (int imageIndex = 0; imageIndex < window.frames.Size; ++imageIndex)
            {
                DestroyPlatformFrame(device, window.frames[imageIndex]);
            }
            for (int semaphoreIndex = 0; semaphoreIndex < window.frameSemaphores.Size; ++semaphoreIndex)
            {
                DestroyPlatformFrameSemaphores(device, window.frameSemaphores[semaphoreIndex]);
            }
            window.frames.clear();
            window.frameSemaphores.clear();

            if (window.renderPass != VK_NULL_HANDLE)
            {
                vkDestroyRenderPass(device, window.renderPass, nullptr);
            }
            if (window.swapchain != VK_NULL_HANDLE)
            {
                vkDestroySwapchainKHR(device, window.swapchain, nullptr);
            }
            if (window.surface != VK_NULL_HANDLE)
            {
                vkDestroySurfaceKHR(instance, window.surface, nullptr);
            }

            window = FUiPlatformWindow{};
        }
    }

    FUiMultiViewport::FUiMultiViewport(NextEngine& engine) : engine_(engine) {}

    FUiMultiViewport::~FUiMultiViewport() = default;

    void FUiMultiViewport::Initialize(FUiSystem& uiSystem)
    {
        uiSystem_ = &uiSystem;
        auto& io = ImGui::GetIO();
        io.BackendRendererUserData = this;
        io.BackendFlags |= ImGuiBackendFlags_RendererHasViewports;

        ImGuiPlatformIO& platformIo = ImGui::GetPlatformIO();
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
            if (platformIo.Platform_CreateVkSurface == nullptr)
            {
                Throw(std::runtime_error("imgui platform backend does not provide Platform_CreateVkSurface"));
            }

#if WIN32
            platformCreateWindow_ = platformIo.Platform_CreateWindow;
            platformSetWindowPos_ = platformIo.Platform_SetWindowPos;
            platformGetWindowPos_ = platformIo.Platform_GetWindowPos;
            platformSetWindowSize_ = platformIo.Platform_SetWindowSize;
            platformGetWindowSize_ = platformIo.Platform_GetWindowSize;
            platformIo.Platform_CreateWindow = &FUiMultiViewport::CreateDpiScaledPlatformWindowCallback;
            platformIo.Platform_SetWindowPos = &FUiMultiViewport::SetDpiScaledPlatformWindowPosCallback;
            platformIo.Platform_GetWindowPos = &FUiMultiViewport::GetDpiScaledPlatformWindowPosCallback;
            platformIo.Platform_SetWindowSize = &FUiMultiViewport::SetDpiScaledPlatformWindowSizeCallback;
            platformIo.Platform_GetWindowSize = &FUiMultiViewport::GetDpiScaledPlatformWindowSizeCallback;
            platformDpiCallbacksOverridden_ = true;
#endif
        }

        platformIo.Renderer_CreateWindow = &FUiMultiViewport::CreatePlatformViewportWindowCallback;
        platformIo.Renderer_DestroyWindow = &FUiMultiViewport::DestroyPlatformViewportWindowCallback;
        platformIo.Renderer_SetWindowSize = &FUiMultiViewport::ResizePlatformViewportWindowCallback;
        platformIo.Renderer_RenderWindow = &FUiMultiViewport::RenderPlatformViewportWindowCallback;
        platformIo.Renderer_SwapBuffers = &FUiMultiViewport::SwapPlatformViewportBuffersCallback;

        ImGuiViewport* mainViewport = ImGui::GetMainViewport();
        if (mainViewport->RendererUserData == nullptr)
        {
            mainViewport->RendererUserData = IM_NEW(FUiPlatformViewportData)();
        }
    }

    void FUiMultiViewport::Shutdown()
    {
        if (ImGui::GetCurrentContext() == nullptr)
        {
            return;
        }

        auto& io = ImGui::GetIO();
        ImGuiPlatformIO& platformIo = ImGui::GetPlatformIO();
        ImGui::DestroyPlatformWindows();

        if (ImGuiViewport* mainViewport = ImGui::GetMainViewport(); mainViewport != nullptr)
        {
            if (mainViewport->RendererUserData != nullptr)
            {
                IM_DELETE(static_cast<FUiPlatformViewportData*>(mainViewport->RendererUserData));
                mainViewport->RendererUserData = nullptr;
            }
        }

        platformIo.Renderer_CreateWindow = nullptr;
        platformIo.Renderer_DestroyWindow = nullptr;
        platformIo.Renderer_SetWindowSize = nullptr;
        platformIo.Renderer_RenderWindow = nullptr;
        platformIo.Renderer_SwapBuffers = nullptr;
        platformIo.Renderer_RenderState = nullptr;
        if (platformDpiCallbacksOverridden_)
        {
            platformIo.Platform_CreateWindow = platformCreateWindow_;
            platformIo.Platform_SetWindowPos = platformSetWindowPos_;
            platformIo.Platform_GetWindowPos = platformGetWindowPos_;
            platformIo.Platform_SetWindowSize = platformSetWindowSize_;
            platformIo.Platform_GetWindowSize = platformGetWindowSize_;
            platformCreateWindow_ = nullptr;
            platformSetWindowPos_ = nullptr;
            platformGetWindowPos_ = nullptr;
            platformSetWindowSize_ = nullptr;
            platformGetWindowSize_ = nullptr;
            platformDpiCallbacksOverridden_ = false;
        }

        io.BackendRendererUserData = uiSystem_;
        io.BackendFlags &= ~ImGuiBackendFlags_RendererHasViewports;
        platformUiRenderBuffers_.clear();
        OnUiPipelineDestroyed();
        uiSystem_ = nullptr;
    }

    void FUiMultiViewport::OnUiPipelineDestroyed()
    {
        if (uiPlatformViewportPipeline_ != VK_NULL_HANDLE && uiSystem_ != nullptr)
        {
            uiSystem_->GetRenderer().DestroyPipeline(engine_.GetRenderer().Device(), uiPlatformViewportPipeline_);
        }
        uiPlatformViewportPipeline_ = VK_NULL_HANDLE;
        uiPlatformViewportRenderPass_ = VK_NULL_HANDLE;
    }

    void FUiMultiViewport::RenderPlatformWindows()
    {
        ImGui::UpdatePlatformWindows();
        PrunePlatformViewportRenderBuffers();
        ImGui::RenderPlatformWindowsDefault(nullptr, this);
    }

    VkPipeline FUiMultiViewport::GetOrCreatePlatformViewportPipeline(VkRenderPass renderPass)
    {
        if (renderPass == VK_NULL_HANDLE || uiSystem_ == nullptr)
        {
            return VK_NULL_HANDLE;
        }

        if (uiPlatformViewportPipeline_ != VK_NULL_HANDLE && uiPlatformViewportRenderPass_ == renderPass)
        {
            return uiPlatformViewportPipeline_;
        }

        const auto& device = engine_.GetRenderer().Device();
        if (uiPlatformViewportPipeline_ != VK_NULL_HANDLE)
        {
            uiSystem_->GetRenderer().DestroyPipeline(device, uiPlatformViewportPipeline_);
        }

        uiPlatformViewportPipeline_ = uiSystem_->GetRenderer().CreatePipelineForRenderPass(device, renderPass);
        uiPlatformViewportRenderPass_ = renderPass;
        return uiPlatformViewportPipeline_;
    }

    FUiMultiViewport* FUiMultiViewport::GetRendererBackendOwner()
    {
        if (ImGui::GetCurrentContext() == nullptr)
        {
            return nullptr;
        }

        return static_cast<FUiMultiViewport*>(ImGui::GetIO().BackendRendererUserData);
    }

    void FUiMultiViewport::CreateDpiScaledPlatformWindowCallback(ImGuiViewport* viewport)
    {
        FUiMultiViewport* owner = GetRendererBackendOwner();
        if (owner == nullptr || owner->platformCreateWindow_ == nullptr || owner->uiSystem_ == nullptr)
        {
            return;
        }

        owner->platformCreateWindow_(viewport);
        const float scale = owner->uiSystem_->GetUiScale();
        if (owner->platformSetWindowPos_ != nullptr)
        {
            owner->platformSetWindowPos_(viewport, ImVec2(viewport->Pos.x * scale, viewport->Pos.y * scale));
        }
        if (owner->platformSetWindowSize_ != nullptr)
        {
            owner->platformSetWindowSize_(viewport, ImVec2(viewport->Size.x * scale, viewport->Size.y * scale));
        }
    }

    void FUiMultiViewport::SetDpiScaledPlatformWindowPosCallback(ImGuiViewport* viewport, ImVec2 pos)
    {
        FUiMultiViewport* owner = GetRendererBackendOwner();
        if (owner == nullptr || owner->platformSetWindowPos_ == nullptr || owner->uiSystem_ == nullptr)
        {
            return;
        }

        const float scale = owner->uiSystem_->GetUiScale();
        owner->platformSetWindowPos_(viewport, ImVec2(pos.x * scale, pos.y * scale));
        if (viewport->PlatformHandle != nullptr)
        {
            const SDL_WindowID windowId = static_cast<SDL_WindowID>(reinterpret_cast<intptr_t>(viewport->PlatformHandle));
            if (SDL_Window* window = SDL_GetWindowFromID(windowId))
            {
                SDL_SyncWindow(window);
            }
        }
    }

    ImVec2 FUiMultiViewport::GetDpiScaledPlatformWindowPosCallback(ImGuiViewport* viewport)
    {
        FUiMultiViewport* owner = GetRendererBackendOwner();
        if (owner == nullptr || owner->platformGetWindowPos_ == nullptr || owner->uiSystem_ == nullptr)
        {
            return viewport != nullptr ? viewport->Pos : ImVec2(0.0f, 0.0f);
        }

        const float scale = owner->uiSystem_->GetUiScale();
        const ImVec2 physicalPos = owner->platformGetWindowPos_(viewport);
        return ImVec2(physicalPos.x / scale, physicalPos.y / scale);
    }

    void FUiMultiViewport::SetDpiScaledPlatformWindowSizeCallback(ImGuiViewport* viewport, ImVec2 size)
    {
        FUiMultiViewport* owner = GetRendererBackendOwner();
        if (owner == nullptr || owner->platformSetWindowSize_ == nullptr || owner->uiSystem_ == nullptr)
        {
            return;
        }

        const float scale = owner->uiSystem_->GetUiScale();
        owner->platformSetWindowSize_(viewport, ImVec2(size.x * scale, size.y * scale));
    }

    ImVec2 FUiMultiViewport::GetDpiScaledPlatformWindowSizeCallback(ImGuiViewport* viewport)
    {
        FUiMultiViewport* owner = GetRendererBackendOwner();
        if (owner == nullptr || owner->platformGetWindowSize_ == nullptr || owner->uiSystem_ == nullptr)
        {
            return viewport != nullptr ? viewport->Size : ImVec2(0.0f, 0.0f);
        }

        const float scale = owner->uiSystem_->GetUiScale();
        const ImVec2 physicalSize = owner->platformGetWindowSize_(viewport);
        return ImVec2(physicalSize.x / scale, physicalSize.y / scale);
    }

    void FUiMultiViewport::CreatePlatformViewportWindowCallback(ImGuiViewport* viewport)
    {
        if (FUiMultiViewport* owner = GetRendererBackendOwner(); owner != nullptr)
        {
            owner->CreatePlatformViewportWindow(viewport);
        }
    }

    void FUiMultiViewport::DestroyPlatformViewportWindowCallback(ImGuiViewport* viewport)
    {
        if (FUiMultiViewport* owner = GetRendererBackendOwner(); owner != nullptr)
        {
            owner->DestroyPlatformViewportWindow(viewport);
        }
    }

    void FUiMultiViewport::ResizePlatformViewportWindowCallback(ImGuiViewport* viewport, ImVec2 size)
    {
        if (FUiMultiViewport* owner = GetRendererBackendOwner(); owner != nullptr)
        {
            owner->ResizePlatformViewportWindow(viewport, size);
        }
    }

    void FUiMultiViewport::RenderPlatformViewportWindowCallback(ImGuiViewport* viewport, void* renderArg)
    {
        FUiMultiViewport* owner =
            renderArg != nullptr ? static_cast<FUiMultiViewport*>(renderArg) : GetRendererBackendOwner();
        if (owner != nullptr)
        {
            owner->RenderPlatformViewportWindow(viewport);
        }
    }

    void FUiMultiViewport::SwapPlatformViewportBuffersCallback(ImGuiViewport* viewport, void* renderArg)
    {
        FUiMultiViewport* owner =
            renderArg != nullptr ? static_cast<FUiMultiViewport*>(renderArg) : GetRendererBackendOwner();
        if (owner != nullptr)
        {
            owner->SwapPlatformViewportBuffers(viewport);
        }
    }

    void FUiMultiViewport::CreatePlatformViewportWindow(ImGuiViewport* viewport)
    {
        if (viewport == nullptr)
        {
            return;
        }

        auto* viewportData = IM_NEW(FUiPlatformViewportData)();
        viewport->RendererUserData = viewportData;

        FUiPlatformWindow& window = viewportData->window;
        const auto& renderer = engine_.GetRenderer();
        const auto& device = renderer.Device();

        ImGuiPlatformIO& platformIo = ImGui::GetPlatformIO();
        VkResult result = static_cast<VkResult>(platformIo.Platform_CreateVkSurface(
            viewport, reinterpret_cast<ImU64>(device.Surface().Instance().Handle()), nullptr,
            reinterpret_cast<ImU64*>(&window.surface)));
        Vulkan::Check(result, "create ui platform viewport surface");

        VkBool32 presentSupported = VK_FALSE;
        Vulkan::Check(vkGetPhysicalDeviceSurfaceSupportKHR(device.PhysicalDevice(), device.GraphicsFamilyIndex(),
                                                           window.surface, &presentSupported),
                      "query ui platform viewport present support");
        if (presentSupported != VK_TRUE)
        {
            Throw(std::runtime_error("ui platform viewport surface has no present support"));
        }

        const VkFormat requestedFormats[] = {
            VK_FORMAT_B8G8R8A8_UNORM,
            VK_FORMAT_R8G8B8A8_UNORM,
            VK_FORMAT_B8G8R8_UNORM,
            VK_FORMAT_R8G8B8_UNORM};
        window.surfaceFormat = SelectPlatformSurfaceFormat(device.PhysicalDevice(), window.surface, requestedFormats,
                                                           static_cast<int>(std::size(requestedFormats)),
                                                           VK_COLORSPACE_SRGB_NONLINEAR_KHR);

        const VkPresentModeKHR requestedPresentModes[] = {
            VK_PRESENT_MODE_MAILBOX_KHR,
            VK_PRESENT_MODE_IMMEDIATE_KHR,
            VK_PRESENT_MODE_FIFO_KHR};
        window.presentMode = SelectPlatformPresentMode(device.PhysicalDevice(), window.surface, requestedPresentModes,
                                                       static_cast<int>(std::size(requestedPresentModes)));
        window.clearEnable = (viewport->Flags & ImGuiViewportFlags_NoRendererClear) == 0;
        window.useDynamicRendering = false;

        const VkExtent2D framebufferExtent = GetPlatformFramebufferExtent(viewport, viewport->Size);
        CreateOrResizePlatformWindow(device.PhysicalDevice(), device.Handle(), window, device.GraphicsFamilyIndex(),
                                     static_cast<int>(framebufferExtent.width),
                                     static_cast<int>(framebufferExtent.height), renderer.SwapChain().MinImageCount());
        viewportData->windowOwned = true;
    }

    void FUiMultiViewport::DestroyPlatformViewportWindow(ImGuiViewport* viewport)
    {
        if (viewport == nullptr || viewport->RendererUserData == nullptr)
        {
            return;
        }

        auto* viewportData = static_cast<FUiPlatformViewportData*>(viewport->RendererUserData);
        if (viewportData->windowOwned)
        {
            const auto& device = engine_.GetRenderer().Device();
            DestroyPlatformWindow(device.Surface().Instance().Handle(), device.Handle(), viewportData->window);
        }

        platformUiRenderBuffers_.erase(viewport->ID);
        IM_DELETE(viewportData);
        viewport->RendererUserData = nullptr;
    }

    void FUiMultiViewport::ResizePlatformViewportWindow(ImGuiViewport* viewport, ImVec2 size)
    {
        if (viewport == nullptr || viewport->RendererUserData == nullptr)
        {
            return;
        }

        auto* viewportData = static_cast<FUiPlatformViewportData*>(viewport->RendererUserData);
        FUiPlatformWindow& window = viewportData->window;
        const auto& renderer = engine_.GetRenderer();
        const auto& device = renderer.Device();

        window.clearEnable = (viewport->Flags & ImGuiViewportFlags_NoRendererClear) == 0;
        const VkExtent2D framebufferExtent = GetPlatformFramebufferExtent(viewport, size);
        CreateOrResizePlatformWindow(device.PhysicalDevice(), device.Handle(), window, device.GraphicsFamilyIndex(),
                                     static_cast<int>(framebufferExtent.width),
                                     static_cast<int>(framebufferExtent.height),
                                     renderer.SwapChain().MinImageCount());
        viewportData->swapChainNeedRebuild = false;
        viewportData->swapChainSuboptimal = false;
    }

    VkExtent2D FUiMultiViewport::GetPlatformFramebufferExtent(ImGuiViewport* viewport, ImVec2 logicalSize) const
    {
        if (viewport != nullptr && viewport->PlatformHandle != nullptr)
        {
            const SDL_WindowID windowId = static_cast<SDL_WindowID>(reinterpret_cast<intptr_t>(viewport->PlatformHandle));
            if (SDL_Window* window = SDL_GetWindowFromID(windowId))
            {
                int width = 0;
                int height = 0;
                if (SDL_GetWindowSizeInPixels(window, &width, &height) && width > 0 && height > 0)
                {
                    return VkExtent2D{static_cast<uint32_t>(width), static_cast<uint32_t>(height)};
                }
            }
        }

        const float scale = uiSystem_ != nullptr ? uiSystem_->GetUiScale() : 1.0f;
        return VkExtent2D{
            static_cast<uint32_t>(std::max(1.0f, std::round(logicalSize.x * scale))),
            static_cast<uint32_t>(std::max(1.0f, std::round(logicalSize.y * scale)))};
    }

    void FUiMultiViewport::RenderPlatformViewportWindow(ImGuiViewport* viewport)
    {
        if (viewport == nullptr || viewport->RendererUserData == nullptr || viewport->DrawData == nullptr ||
            uiSystem_ == nullptr)
        {
            return;
        }

        auto* viewportData = static_cast<FUiPlatformViewportData*>(viewport->RendererUserData);
        FUiPlatformWindow& window = viewportData->window;
        const auto& renderer = engine_.GetRenderer();
        const auto& device = renderer.Device();
        VkResult result = VK_SUCCESS;

        if (viewportData->swapChainNeedRebuild || viewportData->swapChainSuboptimal)
        {
            ResizePlatformViewportWindow(viewport, viewport->Size);
        }

        FUiPlatformFrameSemaphores& frameSemaphores = window.frameSemaphores[window.semaphoreIndex];
        result = vkAcquireNextImageKHR(device.Handle(), window.swapchain, UINT64_MAX,
                                       frameSemaphores.imageAcquiredSemaphore, VK_NULL_HANDLE, &window.frameIndex);
        if (result == VK_ERROR_OUT_OF_DATE_KHR)
        {
            viewportData->swapChainNeedRebuild = true;
            return;
        }
        if (result == VK_SUBOPTIMAL_KHR)
        {
            viewportData->swapChainSuboptimal = true;
        }
        else
        {
            Vulkan::Check(result, "acquire ui platform viewport image");
        }

        FUiPlatformFrame& frame = window.frames[window.frameIndex];
        for (;;)
        {
            result = vkWaitForFences(device.Handle(), 1, &frame.fence, VK_TRUE, 100);
            if (result == VK_SUCCESS)
            {
                break;
            }
            if (result != VK_TIMEOUT)
            {
                Vulkan::Check(result, "wait ui platform viewport fence");
            }
        }

        Vulkan::Check(vkResetCommandPool(device.Handle(), frame.commandPool, 0),
                      "reset ui platform viewport command pool");

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        Vulkan::Check(vkBeginCommandBuffer(frame.commandBuffer, &beginInfo),
                      "begin ui platform viewport command buffer");

        VkRenderPassBeginInfo renderPassBeginInfo{};
        renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassBeginInfo.renderPass = window.renderPass;
        renderPassBeginInfo.framebuffer = frame.framebuffer;
        renderPassBeginInfo.renderArea.extent.width = static_cast<uint32_t>(window.width);
        renderPassBeginInfo.renderArea.extent.height = static_cast<uint32_t>(window.height);
        renderPassBeginInfo.clearValueCount = (viewport->Flags & ImGuiViewportFlags_NoRendererClear) ? 0u : 1u;
        renderPassBeginInfo.pClearValues =
            (viewport->Flags & ImGuiViewportFlags_NoRendererClear) ? nullptr : &window.clearValue;
        vkCmdBeginRenderPass(frame.commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

        VkPipeline viewportPipeline = GetOrCreatePlatformViewportPipeline(window.renderPass);
        auto& viewportRenderBuffers = platformUiRenderBuffers_[viewport->ID];
        if (viewportRenderBuffers.size() < window.imageCount)
        {
            viewportRenderBuffers.resize(window.imageCount);
        }

        uiSystem_->GetRenderer().RecordOffscreenDrawData(
            frame.commandBuffer,
            device,
            viewportRenderBuffers[window.frameIndex],
            VkExtent2D{static_cast<uint32_t>(window.width), static_cast<uint32_t>(window.height)},
            0u,
            viewportPipeline,
            viewport->DrawData);

        vkCmdEndRenderPass(frame.commandBuffer);

        VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = &frameSemaphores.imageAcquiredSemaphore;
        submitInfo.pWaitDstStageMask = &waitStage;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &frame.commandBuffer;
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = &frameSemaphores.renderCompleteSemaphore;

        Vulkan::Check(vkEndCommandBuffer(frame.commandBuffer), "end ui platform viewport command buffer");
        Vulkan::Check(vkResetFences(device.Handle(), 1, &frame.fence), "reset ui platform viewport fence");
        Vulkan::Check(vkQueueSubmit(device.GraphicsQueue(), 1, &submitInfo, frame.fence),
                      "submit ui platform viewport command buffer");
    }

    void FUiMultiViewport::SwapPlatformViewportBuffers(ImGuiViewport* viewport)
    {
        if (viewport == nullptr || viewport->RendererUserData == nullptr)
        {
            return;
        }

        auto* viewportData = static_cast<FUiPlatformViewportData*>(viewport->RendererUserData);
        if (viewportData->swapChainNeedRebuild)
        {
            return;
        }

        FUiPlatformWindow& window = viewportData->window;
        FUiPlatformFrameSemaphores& frameSemaphores = window.frameSemaphores[window.semaphoreIndex];
        uint32_t presentIndex = window.frameIndex;

        VkPresentInfoKHR presentInfo{};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = &frameSemaphores.renderCompleteSemaphore;
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &window.swapchain;
        presentInfo.pImageIndices = &presentIndex;

        VkResult result = vkQueuePresentKHR(engine_.GetRenderer().Device().GraphicsQueue(), &presentInfo);
        if (result == VK_ERROR_OUT_OF_DATE_KHR)
        {
            viewportData->swapChainNeedRebuild = true;
            return;
        }
        if (result == VK_SUBOPTIMAL_KHR)
        {
            viewportData->swapChainSuboptimal = true;
        }
        else
        {
            Vulkan::Check(result, "present ui platform viewport");
        }

        window.semaphoreIndex = (window.semaphoreIndex + 1) % window.semaphoreCount;
    }

    void FUiMultiViewport::PrunePlatformViewportRenderBuffers()
    {
        std::unordered_set<ImGuiID> activeViewportIds;
        ImGuiPlatformIO& platformIo = ImGui::GetPlatformIO();
        ImGuiViewport* mainViewport = ImGui::GetMainViewport();
        for (int viewportIndex = 0; viewportIndex < platformIo.Viewports.Size; ++viewportIndex)
        {
            ImGuiViewport* viewport = platformIo.Viewports[viewportIndex];
            if (viewport == nullptr || viewport == mainViewport)
            {
                continue;
            }
            activeViewportIds.insert(viewport->ID);
        }

        for (auto it = platformUiRenderBuffers_.begin(); it != platformUiRenderBuffers_.end();)
        {
            if (!activeViewportIds.contains(it->first))
            {
                it = platformUiRenderBuffers_.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }
}
