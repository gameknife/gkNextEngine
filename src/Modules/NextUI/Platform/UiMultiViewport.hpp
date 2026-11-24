#pragma once

#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Vulkan/DebugUtilities.hpp"
#include "Modules/NextUI/UiRenderer.hpp"

#include <imgui.h>
#include <unordered_map>

class NextEngine;

namespace NextUI
{
    class FUiSystem;

    // Detached platform-window backend for Dear ImGui multi-viewport mode (desktop editors).
    class FUiMultiViewport final
    {
    public:
        explicit FUiMultiViewport(NextEngine& engine);
        ~FUiMultiViewport();

        GK_NON_COPIABLE(FUiMultiViewport)

        void Initialize(FUiSystem& uiSystem);
        void Shutdown();
        void OnUiPipelineDestroyed();
        void RenderPlatformWindows();

    private:
        void CreatePlatformViewportWindow(ImGuiViewport* viewport);
        void DestroyPlatformViewportWindow(ImGuiViewport* viewport);
        void ResizePlatformViewportWindow(ImGuiViewport* viewport, ImVec2 size);
        void RenderPlatformViewportWindow(ImGuiViewport* viewport);
        void SwapPlatformViewportBuffers(ImGuiViewport* viewport);
        VkPipeline GetOrCreatePlatformViewportPipeline(VkRenderPass renderPass);
        void PrunePlatformViewportRenderBuffers();
        VkExtent2D GetPlatformFramebufferExtent(ImGuiViewport* viewport, ImVec2 logicalSize) const;

        static FUiMultiViewport* GetRendererBackendOwner();
        static void CreateDpiScaledPlatformWindowCallback(ImGuiViewport* viewport);
        static void SetDpiScaledPlatformWindowPosCallback(ImGuiViewport* viewport, ImVec2 pos);
        static ImVec2 GetDpiScaledPlatformWindowPosCallback(ImGuiViewport* viewport);
        static void SetDpiScaledPlatformWindowSizeCallback(ImGuiViewport* viewport, ImVec2 size);
        static ImVec2 GetDpiScaledPlatformWindowSizeCallback(ImGuiViewport* viewport);
        static void CreatePlatformViewportWindowCallback(ImGuiViewport* viewport);
        static void DestroyPlatformViewportWindowCallback(ImGuiViewport* viewport);
        static void ResizePlatformViewportWindowCallback(ImGuiViewport* viewport, ImVec2 size);
        static void RenderPlatformViewportWindowCallback(ImGuiViewport* viewport, void* renderArg);
        static void SwapPlatformViewportBuffersCallback(ImGuiViewport* viewport, void* renderArg);

        NextEngine& engine_;
        FUiSystem* uiSystem_ = nullptr;
        VkPipeline uiPlatformViewportPipeline_ = VK_NULL_HANDLE;
        VkRenderPass uiPlatformViewportRenderPass_ = VK_NULL_HANDLE;
        void (*platformCreateWindow_)(ImGuiViewport*) = nullptr;
        void (*platformSetWindowPos_)(ImGuiViewport*, ImVec2) = nullptr;
        ImVec2 (*platformGetWindowPos_)(ImGuiViewport*) = nullptr;
        void (*platformSetWindowSize_)(ImGuiViewport*, ImVec2) = nullptr;
        ImVec2 (*platformGetWindowSize_)(ImGuiViewport*) = nullptr;
        bool platformDpiCallbacksOverridden_ = false;
        std::unordered_map<ImGuiID, std::vector<FUiRenderBuffer>> platformUiRenderBuffers_;
    };
}
