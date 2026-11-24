#pragma once

#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Runtime/Interface/UiHost.hpp"
#include "Modules/NextUI/Platform/UiMultiViewport.hpp"
#include "Modules/NextUI/TextureCache.hpp"
#include "Modules/NextUI/UiRenderer.hpp"

#include <imgui.h>
#include <memory>
#include <string>
#include <vector>

namespace Vulkan
{
    struct FPipelineWarmupProgress;
}

namespace NextUI
{
    class FUiSystem final : public Runtime::IUiHost
    {
    public:
        FUiSystem(NextEngine& engine, Runtime::FUiHostConfig config);
        ~FUiSystem() override;

        GK_NON_COPIABLE(FUiSystem)

        // Runtime::IUiHost
        void OnCreateSurface(const Vulkan::SwapChain& swapChain) override;
        void OnDestroySurface() override;

        void BuildWarmupFrame(NextEngine& engine) override;
        void BuildFrame(NextEngine& engine,
                        const Runtime::FUiFrameContext& context,
                        const Runtime::FUiStatistics& statistics) override;
        void RecordRender(VkCommandBuffer commandBuffer,
                          const Vulkan::SwapChain& swapChain,
                          uint32_t imageIndex,
                          bool suppressAllUi = false) override;

        void AddLayer(std::unique_ptr<Runtime::IUiLayer> layer) override;
        Runtime::IUiLayer* FindLayer(std::string_view layerId) const override;

        bool HandleEvent(const SDL_Event& event) override;
        void ClaimInputCapture(bool captureMouse, bool captureKeyboard = false) override;
        bool WantsToCaptureMouse() const override;
        bool WantsToCaptureKeyboard() const override;
        float GetUiScale() const override { return uiScale_; }

        uint64_t ResolveTextureId(uint32_t globalTextureId) override;
        uint64_t ResolveRawSlotId(uint32_t bindlessSampleSlot, bool rawOutput = false) override;
        uint64_t ResolveTextureByName(std::string_view name) override;
        Runtime::FUiTextureHandle RequestTexture(
            std::string_view path,
            bool srgb = true,
            Runtime::EUiTextureLifetime lifetime = Runtime::EUiTextureLifetime::Transient) override;

        // Module-internal / NextUI-specific accessors
        FUiRenderer& GetRenderer() { return renderer_; }
        const FUiRenderer& GetRenderer() const { return renderer_; }
        ImFontAtlas* GetFontAtlas() const { return fontAtlas_; }
        ImFont* GetDefaultFont() const { return defaultFont_; }
        ImFont* GetTitleBarFont() const { return titleBarFont_; }
        float GetRasterizerDensity() const { return fontRasterizerDensity_; }
        void AttachRendererBackendToCurrentContext() const;
        void SubmitOverlayStream(const FUiCustomDrawStream* stream);

    private:
        void MakeContextCurrent() const;
        void PreRender();
        void InitializeRendererBackend();
        void ShutdownRendererBackend();
        void InitializeFontTexture(Vulkan::CommandPool& commandPool);
        void DrawLoadingOrWarmupOverlay(NextEngine& engine);
        void DrawLoadingPopup(uint32_t frameCount, bool show);
        void DrawPipelineWarmupOverlay(const Vulkan::FPipelineWarmupProgress& pipelineWarmup);

        NextEngine& engine_;
        ImGuiContext* context_ = nullptr;
        std::string imguiIniPath_;
        FUiRenderer renderer_;
        FTextureCache textureCache_;
        std::unique_ptr<FUiMultiViewport> multiViewport_;
        std::vector<std::unique_ptr<Runtime::IUiLayer>> layers_;
        std::vector<const FUiCustomDrawStream*> frameOverlayStreams_;

        ImFontAtlas* fontAtlas_ = nullptr;
        ImFont* defaultFont_ = nullptr;
        ImFont* titleBarFont_ = nullptr;
        float uiScale_ = 1.0f;
        float fontRasterizerDensity_ = 1.0f;
        uint32_t fontTextureIndex_ = UINT32_MAX;

        bool sdlPlatformBackendInitialized_ = false;
        bool hasPreparedDrawData_ = false;
        bool claimedMouseCapture_ = false;
        bool claimedKeyboardCapture_ = false;
        double loadingStartedAt_ = -1.0;
        bool loadingIndicatorOpen_ = false;
    };
}
