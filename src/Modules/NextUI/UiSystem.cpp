#include "Engine/Common/CoreMinimal.hpp"

#include "Modules/NextUI/UiSystem.hpp"

#include "Engine/Assets/GPU/Texture.hpp"
#include "Engine/Assets/GPU/TextureImage.hpp"
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Rendering/VulkanBaseRenderer.hpp"
#include "Engine/Runtime/Config/ShowFlags.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/GameInstance.hpp"
#include "Engine/Runtime/Interface/DebugUiProvider.hpp"
#include "Engine/Utilities/Exception.hpp"
#include "Engine/Utilities/FileHelper.hpp"
#include "Engine/Vulkan/Device.hpp"
#include "Engine/Vulkan/Instance.hpp"
#include "Engine/Vulkan/SwapChain.hpp"
#include "Engine/Vulkan/WindowSurface.hpp"
#include "Modules/NextUI/Containers.hpp"
#include "Modules/NextUI/FontAtlas.hpp"
#include "Modules/NextUI/Theme.hpp"

#include "ThirdParty/fontawesome/IconsFontAwesome6.h"
#include "ThirdParty/imgui-custom/imgui_impl_sdl3_custom.h"

#include <SDL3/SDL.h>
#include <fmt/format.h>
#include <imgui_freetype.h>

#include <algorithm>
#include <cmath>

namespace NextUI
{
    namespace
    {
        constexpr const char* kUiFontAtlasTextureName = "__imgui_font_atlas__";
    }

    FUiSystem::FUiSystem(NextEngine& engine, Runtime::FUiHostConfig config)
        : engine_(engine)
    {
        auto& renderer = engine_.GetRenderer();
        const auto& swapChain = renderer.SwapChain();
        const auto& window = swapChain.Device().Surface().Instance().Window();

        IMGUI_CHECKVERSION();
        context_ = ImGui::CreateContext();
        MakeContextCurrent();

        auto& io = ImGui::GetIO();
        fontAtlas_ = io.Fonts;
        imguiIniPath_ = Utilities::FileHelper::GetWritableFilePath("imgui.ini");
        io.IniFilename = imguiIniPath_.c_str();
        io.WantCaptureMouse = false;
        io.WantCaptureKeyboard = false;

        if (config.onPreConfig)
        {
            config.onPreConfig();
        }

        if (config.enableMultiViewport && !window.IsHeadless())
        {
            multiViewport_ = std::make_unique<FUiMultiViewport>(engine_);
        }

        sdlPlatformBackendInitialized_ = !window.IsHeadless();
        if (!sdlPlatformBackendInitialized_)
        {
            io.ConfigFlags &= ~ImGuiConfigFlags_ViewportsEnable;
        }
        if (sdlPlatformBackendInitialized_ && !ImGui_ImplSDL3_InitForVulkan(window.Handle()))
        {
            Throw(std::runtime_error("failed to initialise ImGui SDL adapter"));
        }
        if (!sdlPlatformBackendInitialized_)
        {
            const auto extent = swapChain.Extent();
            io.DisplaySize = ImVec2(static_cast<float>(extent.width), static_cast<float>(extent.height));
            io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
            io.DeltaTime = 1.0f / 60.0f;
        }

        InitializeRendererBackend();

#if ANDROID
        constexpr float androidUiScale = 0.70f;
        const float scaleFactor = std::max(1.0f, window.ContentScale()) * androidUiScale;
#elif WIN32
        const float scaleFactor = std::max(1.0f, window.ContentScale());
#elif IOS
        const float density = window.PixelDensity();
        const VkExtent2D pointSize = window.WindowSize();
        const bool isTablet = (std::min(pointSize.width, pointSize.height) >= 600);
        const float targetMagnification = isTablet ? density : 1.5f;
        const float iosUiScale = density > 0.0f ? (targetMagnification / density) : 1.0f;
        const float scaleFactor = iosUiScale;
#else
        const float scaleFactor = 1.0f;
#endif
        uiScale_ = scaleFactor;

#if IOS
        fontRasterizerDensity_ = 1.0f;
#else
        fontRasterizerDensity_ = scaleFactor;
#endif
        constexpr float fontSize = 16.0f;

        ApplyTheme();
#if !WIN32 && !ANDROID && !IOS
        ImGui::GetStyle().ScaleAllSizes(scaleFactor);
#endif

        io.Fonts->SetFontLoader(ImGuiFreeType::GetFontLoader());
        io.Fonts->FontLoaderFlags = ImGuiFreeTypeLoaderFlags_NoHinting;

        defaultFont_ = LoadDefaultUiFont("assets/fonts/Roboto-Regular.ttf", fontSize, fontRasterizerDensity_, true);
        if (defaultFont_ == nullptr)
        {
            Throw(std::runtime_error("failed to load default ImGui text font"));
        }

        constexpr float titleFontSize = 18.0f;
        titleBarFont_ = LoadFont(FFontRequest{
            .filePath = "assets/fonts/Roboto-BoldCondensed.ttf",
            .pixelSize = titleFontSize,
            .includeChineseFull = false,
            .rasterizerDensity = fontRasterizerDensity_,
        });
        if (titleBarFont_ != nullptr)
        {
            MergeFontAwesomeIcons(titleFontSize, fontRasterizerDensity_);
        }

        if (config.onInit)
        {
            config.onInit();
        }
        InitializeFontTexture(renderer.CommandPool());
    }

    FUiSystem::~FUiSystem()
    {
        layers_.clear();
        frameOverlayStreams_.clear();

        ShutdownRendererBackend();
        OnDestroySurface();

        if (sdlPlatformBackendInitialized_)
        {
            MakeContextCurrent();
            ImGui_ImplSDL3_Shutdown();
        }
        if (context_ != nullptr)
        {
            ImGui::DestroyContext(context_);
            context_ = nullptr;
        }
    }

    void FUiSystem::MakeContextCurrent() const
    {
        if (context_ != nullptr && ImGui::GetCurrentContext() != context_)
        {
            ImGui::SetCurrentContext(context_);
        }
    }

    void FUiSystem::InitializeRendererBackend()
    {
        auto& io = ImGui::GetIO();
        if (io.BackendRendererUserData != nullptr)
        {
            Throw(std::runtime_error("imgui renderer backend already initialized"));
        }

        AttachRendererBackendToCurrentContext();

        if (multiViewport_)
        {
            multiViewport_->Initialize(*this);
        }
    }

    void FUiSystem::ShutdownRendererBackend()
    {
        MakeContextCurrent();
        if (ImGui::GetCurrentContext() == nullptr)
        {
            return;
        }

        if (multiViewport_)
        {
            multiViewport_->Shutdown();
        }

        auto& io = ImGui::GetIO();
        ImGui::GetPlatformIO().Renderer_RenderState = nullptr;
        io.BackendRendererName = nullptr;
        io.BackendRendererUserData = nullptr;
        io.BackendFlags &= ~ImGuiBackendFlags_RendererHasVtxOffset;
    }

    void FUiSystem::InitializeFontTexture(Vulkan::CommandPool& commandPool)
    {
        MakeContextCurrent();
        auto& io = ImGui::GetIO();
        unsigned char* pixels = nullptr;
        int width = 0;
        int height = 0;
        io.Fonts->GetTexDataAsAlpha8(&pixels, &width, &height);

        if (pixels == nullptr || width <= 0 || height <= 0)
        {
            Throw(std::runtime_error("failed to build imgui font atlas"));
        }

        const uint32_t fontTextureSize = static_cast<uint32_t>(width * height);
        const VkComponentMapping fontComponentMapping{
            VK_COMPONENT_SWIZZLE_ONE,
            VK_COMPONENT_SWIZZLE_ONE,
            VK_COMPONENT_SWIZZLE_ONE,
            VK_COMPONENT_SWIZZLE_R,
        };
        auto fontTexture = std::make_unique<Assets::TextureImage>(
            commandPool, static_cast<size_t>(width), static_cast<size_t>(height), 1, VK_FORMAT_R8_UNORM, pixels,
            fontTextureSize, fontComponentMapping);
        fontTexture->MainThreadPostLoading(commandPool);
        fontTexture->SetDebugName(kUiFontAtlasTextureName);
        io.Fonts->ClearTexData();

        auto* texturePool = Assets::GlobalTexturePool::GetInstance();
        if (texturePool == nullptr)
        {
            Throw(std::runtime_error("global texture pool is unavailable for imgui font atlas"));
        }

        fontTextureIndex_ = texturePool->RegisterTexture(
            kUiFontAtlasTextureName, std::move(fontTexture), Assets::ETextureLifetime::ETL_Persistent);
        renderer_.SetDefaultFontTextureIndex(fontTextureIndex_);
        io.Fonts->TexID = ToImTextureId(FUiRenderer::EncodeTextureHandle(fontTextureIndex_, UI_BATCH_FLAG_STRAIGHT_ALPHA));
    }

    void FUiSystem::AttachRendererBackendToCurrentContext() const
    {
        auto& io = ImGui::GetIO();
        io.BackendRendererUserData = const_cast<FUiSystem*>(this);
        io.BackendRendererName = "gk_ui_renderer";
        io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
        if (io.FontDefault == nullptr)
        {
            io.FontDefault = defaultFont_;
        }
        if (fontTextureIndex_ != UINT32_MAX)
        {
            io.Fonts->TexID = ToImTextureId(
                FUiRenderer::EncodeTextureHandle(fontTextureIndex_, UI_BATCH_FLAG_STRAIGHT_ALPHA));
        }
    }

    void FUiSystem::SubmitOverlayStream(const FUiCustomDrawStream* stream)
    {
        if (stream != nullptr && !stream->Empty())
        {
            frameOverlayStreams_.push_back(stream);
        }
    }

    void FUiSystem::OnCreateSurface(const Vulkan::SwapChain& swapChain)
    {
        if (multiViewport_)
        {
            multiViewport_->OnUiPipelineDestroyed();
        }
        renderer_.CreateSwapChainResources(swapChain);
    }

    void FUiSystem::OnDestroySurface()
    {
        hasPreparedDrawData_ = false;
        frameOverlayStreams_.clear();
        if (multiViewport_)
        {
            multiViewport_->OnUiPipelineDestroyed();
        }
        renderer_.DestroySwapChainResources(engine_.GetRenderer().Device());
    }

    void FUiSystem::AddLayer(std::unique_ptr<Runtime::IUiLayer> layer)
    {
        if (!layer)
        {
            return;
        }
        layers_.push_back(std::move(layer));
        std::stable_sort(layers_.begin(), layers_.end(),
                         [](const std::unique_ptr<Runtime::IUiLayer>& lhs,
                            const std::unique_ptr<Runtime::IUiLayer>& rhs)
                         {
                             return lhs->GetOrder() < rhs->GetOrder();
                         });
    }

    Runtime::IUiLayer* FUiSystem::FindLayer(const std::string_view layerId) const
    {
        for (const auto& layer : layers_)
        {
            if (layer && layer->GetLayerId() == layerId)
            {
                return layer.get();
            }
        }
        return nullptr;
    }

    void FUiSystem::PreRender()
    {
        MakeContextCurrent();
        if (sdlPlatformBackendInitialized_)
        {
            ImGui_ImplSDL3_NewFrame();
        }
        else
        {
            auto& io = ImGui::GetIO();
            const auto extent = engine_.GetRenderer().SwapChain().Extent();
            io.DisplaySize = ImVec2(static_cast<float>(extent.width), static_cast<float>(extent.height));
            io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
            io.DeltaTime = 1.0f / 60.0f;
        }

#if WIN32 || ANDROID || IOS
        auto& io = ImGui::GetIO();
#if ANDROID || IOS
        if (uiScale_ != 1.0f)
#else
        if (uiScale_ > 1.0f)
#endif
        {
            io.DisplaySize.x /= uiScale_;
            io.DisplaySize.y /= uiScale_;
            io.DisplayFramebufferScale.x *= uiScale_;
            io.DisplayFramebufferScale.y *= uiScale_;

#if WIN32
            auto& monitors = ImGui::GetPlatformIO().Monitors;
            for (int monitorIndex = 0; monitorIndex < monitors.Size; ++monitorIndex)
            {
                ImGuiPlatformMonitor& monitor = monitors[monitorIndex];
                monitor.MainPos.x /= uiScale_;
                monitor.MainPos.y /= uiScale_;
                monitor.MainSize.x /= uiScale_;
                monitor.MainSize.y /= uiScale_;
                monitor.WorkPos.x /= uiScale_;
                monitor.WorkPos.y /= uiScale_;
                monitor.WorkSize.x /= uiScale_;
                monitor.WorkSize.y /= uiScale_;
            }

            SDL_Window* mouseWindow = SDL_GetMouseFocus();
            if (mouseWindow != nullptr && !SDL_GetWindowRelativeMouseMode(mouseWindow))
            {
                float globalX = 0.0f;
                float globalY = 0.0f;
                int windowX = 0;
                int windowY = 0;
                SDL_GetGlobalMouseState(&globalX, &globalY);

                ImVec2 logicalPosition;
                if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
                {
                    logicalPosition = ImVec2(globalX / uiScale_, globalY / uiScale_);
                }
                else
                {
                    SDL_GetWindowPosition(mouseWindow, &windowX, &windowY);
                    logicalPosition = ImVec2(
                        (globalX - static_cast<float>(windowX)) / uiScale_,
                        (globalY - static_cast<float>(windowY)) / uiScale_);
                }
                io.AddMousePosEvent(logicalPosition.x, logicalPosition.y);
            }
#endif
        }
#endif
        ImGui::NewFrame();
    }

    void FUiSystem::BuildWarmupFrame(NextEngine& engine)
    {
        claimedMouseCapture_ = false;
        claimedKeyboardCapture_ = false;
        frameOverlayStreams_.clear();

        PreRender();
        DrawPipelineWarmupOverlay(engine.GetRenderer().PipelineWarmupProgress());
        ImGui::Render();
        hasPreparedDrawData_ = ImGui::GetDrawData() != nullptr;
    }

    void FUiSystem::BuildFrame(NextEngine& engine,
                               const Runtime::FUiFrameContext& context,
                               const Runtime::FUiStatistics& statistics)
    {
        claimedMouseCapture_ = false;
        claimedKeyboardCapture_ = false;
        frameOverlayStreams_.clear();

        PreRender();

        const ImGuiIO& io = ImGui::GetIO();
        const glm::vec2 logicalSize{
            std::max(1.0f, io.DisplaySize.x),
            std::max(1.0f, io.DisplaySize.y),
        };
        for (const auto& layer : layers_)
        {
            if (layer)
            {
                layer->BeginFrame(logicalSize, uiScale_);
            }
        }

        Runtime::FUiFrameResult uiResult{Runtime::EUiDeveloperLayer::None};
        if (context.policy.allowApplicationUi)
        {
            if (NextGameInstanceBase* gameInstance = engine.GetGameInstance())
            {
                uiResult = gameInstance->RenderUiFrame(context);
            }

            for (const auto& layer : layers_)
            {
                if (layer)
                {
                    layer->RenderLayer(engine);
                }
            }
        }

        Runtime::IDebugUiProvider* debugProvider = engine.GetDebugUiProvider();
        auto& showFlags = engine.GetShowFlags();
        if (showFlags.DebugPhysicsOverlay)
        {
            Assets::Camera debugCamera = engine.GetScene().GetRenderCamera();
            if (NextGameInstanceBase* gameInstance = engine.GetGameInstance())
            {
                gameInstance->OverrideRenderCamera(debugCamera);
            }
            if (debugProvider)
            {
                debugProvider->DrawPhysicsOverlay(engine.GetScene(), debugCamera);
            }
            if (NextGameInstanceBase* gameInstance = engine.GetGameInstance())
            {
                gameInstance->DrawAdditionalPhysicsDebugOverlay(debugCamera);
            }
        }

        if (debugProvider && showFlags.DebugCVarPanel)
        {
            debugProvider->DrawCVarEditor(engine, showFlags.DebugCVarPanel);
        }

        const Runtime::EUiDeveloperLayer developerLayers =
            uiResult.requestedDeveloperLayers & context.policy.allowedDeveloperLayers;
        if (debugProvider && developerLayers != Runtime::EUiDeveloperLayer::None)
        {
            debugProvider->DrawUiPanels(engine, statistics, developerLayers, false);
        }

        if (debugProvider)
        {
            const float topOffset = engine.GetGameInstance()
                ? engine.GetGameInstance()->GetGraphicsDebugPanelTopOffset()
                : 0.0f;
            debugProvider->DrawGraphicsPanel(engine, showFlags.DebugGraphicsPanel, topOffset);
        }

        DrawLoadingOrWarmupOverlay(engine);

        ImGui::Render();
        hasPreparedDrawData_ = ImGui::GetDrawData() != nullptr || !frameOverlayStreams_.empty();
    }

    void FUiSystem::RecordRender(VkCommandBuffer commandBuffer,
                                 const Vulkan::SwapChain& swapChain,
                                 const uint32_t imageIndex,
                                 const bool suppressAllUi)
    {
        if (suppressAllUi || !hasPreparedDrawData_)
        {
            return;
        }

        MakeContextCurrent();
        renderer_.RecordSwapChainPass(
            commandBuffer, swapChain, imageIndex, ImGui::GetDrawData(), frameOverlayStreams_);

        if ((ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) && multiViewport_)
        {
            multiViewport_->RenderPlatformWindows();
        }
    }

    bool FUiSystem::HandleEvent(const SDL_Event& event)
    {
        MakeContextCurrent();

        if (Runtime::IDebugUiProvider* provider = engine_.GetDebugUiProvider())
        {
            if (provider->HandleUiEvent(event))
            {
                return true;
            }
        }

        // Dispatch to pluggable UI layers in reverse Z-order (topmost first).
        for (auto it = layers_.rbegin(); it != layers_.rend(); ++it)
        {
            if (*it && (*it)->HandleEvent(event))
            {
                return true;
            }
        }

#if WIN32
        if (uiScale_ > 1.0f && event.type == SDL_EVENT_MOUSE_MOTION)
        {
            SDL_Window* window = SDL_GetWindowFromID(event.motion.windowID);
            ImGuiViewport* viewport = ImGui::FindViewportByPlatformHandle(
                reinterpret_cast<void*>(static_cast<intptr_t>(event.motion.windowID)));
            if (window != nullptr && viewport != nullptr && !SDL_GetWindowRelativeMouseMode(window))
            {
                ImVec2 logicalPosition(event.motion.x / uiScale_, event.motion.y / uiScale_);
                if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
                {
                    int windowX = 0;
                    int windowY = 0;
                    SDL_GetWindowPosition(window, &windowX, &windowY);
                    logicalPosition.x += static_cast<float>(windowX) / uiScale_;
                    logicalPosition.y += static_cast<float>(windowY) / uiScale_;
                }

                auto& io = ImGui::GetIO();
                io.AddMouseSourceEvent(event.motion.which == SDL_TOUCH_MOUSEID
                                           ? ImGuiMouseSource_TouchScreen
                                           : ImGuiMouseSource_Mouse);
                io.AddMousePosEvent(logicalPosition.x, logicalPosition.y);
                return false;
            }
        }
#elif ANDROID || IOS
        if (uiScale_ != 1.0f && event.type == SDL_EVENT_MOUSE_MOTION)
        {
            SDL_Window* window = SDL_GetWindowFromID(event.motion.windowID);
            if (window != nullptr && !SDL_GetWindowRelativeMouseMode(window))
            {
                auto& io = ImGui::GetIO();
                io.AddMouseSourceEvent(event.motion.which == SDL_TOUCH_MOUSEID
                                           ? ImGuiMouseSource_TouchScreen
                                           : ImGuiMouseSource_Mouse);
                io.AddMousePosEvent(event.motion.x / uiScale_, event.motion.y / uiScale_);
                return false;
            }
        }
#endif
        if (sdlPlatformBackendInitialized_)
        {
            ImGui_ImplSDL3_ProcessEvent(&event);
        }
        return false;
    }

    void FUiSystem::ClaimInputCapture(const bool captureMouse, const bool captureKeyboard)
    {
        claimedMouseCapture_ = claimedMouseCapture_ || captureMouse;
        claimedKeyboardCapture_ = claimedKeyboardCapture_ || captureKeyboard;
    }

    bool FUiSystem::WantsToCaptureMouse() const
    {
        MakeContextCurrent();
        if (claimedMouseCapture_ || (context_ != nullptr && ImGui::GetIO().WantCaptureMouse))
        {
            return true;
        }
        for (const auto& layer : layers_)
        {
            if (layer && layer->WantsToCaptureMouse())
            {
                return true;
            }
        }
        return false;
    }

    bool FUiSystem::WantsToCaptureKeyboard() const
    {
        MakeContextCurrent();
        if (claimedKeyboardCapture_ || (context_ != nullptr && ImGui::GetIO().WantCaptureKeyboard))
        {
            return true;
        }
        for (const auto& layer : layers_)
        {
            if (layer && layer->WantsToCaptureKeyboard())
            {
                return true;
            }
        }
        return false;
    }

    uint64_t FUiSystem::ResolveTextureId(const uint32_t globalTextureId)
    {
        if (Assets::GlobalTexturePool::GetTextureImage(globalTextureId) == nullptr)
        {
            return 0;
        }
        return FUiRenderer::EncodeTextureHandle(globalTextureId, UI_BATCH_FLAG_STRAIGHT_ALPHA);
    }

    uint64_t FUiSystem::ResolveRawSlotId(const uint32_t bindlessSampleSlot, const bool rawOutput)
    {
        const uint32_t flags = UI_BATCH_FLAG_STRAIGHT_ALPHA | (rawOutput ? UI_BATCH_FLAG_RAW_OUTPUT : 0u);
        return FUiRenderer::EncodeTextureHandle(bindlessSampleSlot, flags);
    }

    uint64_t FUiSystem::ResolveTextureByName(const std::string_view name)
    {
        const uint32_t id = Assets::GlobalTexturePool::GetTextureIndexByName(std::string(name));
        if (id == static_cast<uint32_t>(-1))
        {
            return 0;
        }
        return ResolveTextureId(id);
    }

    Runtime::FUiTextureHandle FUiSystem::RequestTexture(const std::string_view path,
                                                        const bool srgb,
                                                        const Runtime::EUiTextureLifetime lifetime)
    {
        return textureCache_.Request(path, srgb, lifetime,
                                     [this](const std::string_view name) { return ResolveTextureByName(name); });
    }

    void FUiSystem::DrawLoadingOrWarmupOverlay(NextEngine& engine)
    {
        constexpr double loadingIndicatorDelaySeconds = 0.5;
        const bool isLoading = engine.GetEngineStatus() == NextRenderer::EApplicationStatus::Loading;
        const Vulkan::FPipelineWarmupProgress& pipelineWarmup =
            engine.GetRenderer().PipelineWarmupProgress();
        if (isLoading)
        {
            if (pipelineWarmup.active)
            {
                loadingStartedAt_ = -1.0;
                DrawLoadingPopup(engine.GetTotalFrames(), true);
            }
            else
            {
                if (loadingStartedAt_ < 0.0)
                {
                    loadingStartedAt_ = ImGui::GetTime();
                }
                if (ImGui::GetTime() - loadingStartedAt_ >= loadingIndicatorDelaySeconds)
                {
                    DrawLoadingPopup(engine.GetTotalFrames(), true);
                }
            }
        }
        else
        {
            loadingStartedAt_ = -1.0;
            if (loadingIndicatorOpen_)
            {
                DrawLoadingPopup(engine.GetTotalFrames(), false);
            }
        }
    }

    void FUiSystem::DrawLoadingPopup(uint32_t frameCount, const bool show)
    {
        frameCount /= 60;
        const Vulkan::FPipelineWarmupProgress& pipelineWarmup =
            engine_.GetRenderer().PipelineWarmupProgress();
        if (pipelineWarmup.active)
        {
            DrawPipelineWarmupOverlay(pipelineWarmup);
            return;
        }

        if (show)
        {
            ImGui::OpenPopup("Loading");
            loadingIndicatorOpen_ = true;
        }
        if (BeginAnchoredModal(
                "Loading",
                nullptr,
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoResize,
                ImVec2(200.0f, 40.0f)))
        {
            if (show)
            {
                ImGui::Text("Loading%s",
                            frameCount % 4 == 0       ? ""
                                : frameCount % 4 == 1 ? "."
                                : frameCount % 4 == 2 ? ".."
                                                      : "...");
            }
            else
            {
                ImGui::CloseCurrentPopup();
                loadingIndicatorOpen_ = false;
            }
            EndAnchoredModal();
        }
    }

    void FUiSystem::DrawPipelineWarmupOverlay(const Vulkan::FPipelineWarmupProgress& pipelineWarmup)
    {
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowBgAlpha(0.0f);
        constexpr ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoBackground;

        // ImGui DisplaySize is already normalized into logical dp coordinates; do NOT multiply
        // layout metrics by uiScale_ here (doing so double-scales on High-DPI displays).
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 6.0f));
        if (!ImGui::Begin("##PipelineWarmup", nullptr, flags))
        {
            ImGui::End();
            ImGui::PopStyleVar();
            return;
        }

        ImFont* titleFont = titleBarFont_ != nullptr ? titleBarFont_ : defaultFont_;
        if (titleFont == nullptr)
        {
            titleFont = ImGui::GetFont();
        }

        ImGui::PushFont(titleFont);
        const std::string titleText = ICON_FA_MICROCHIP "  OPTIMIZING PIPELINE CACHE";
        const std::string groupProgressText = fmt::format("{}/{}", pipelineWarmup.completedRenderers, pipelineWarmup.totalRenderers);
        const float titleTextWidth = ImGui::CalcTextSize(titleText.c_str()).x;
        const float groupTextWidth = ImGui::CalcTextSize(groupProgressText.c_str()).x;
        const float minRequiredWidth = titleTextWidth + groupTextWidth + 24.0f;
        const float panelWidth = std::max(360.0f, minRequiredWidth);
        const float startPosX = ImGui::GetCursorPosX();

        ImGui::PushStyleColor(ImGuiCol_Text, Color(EColor::Text));
        ImGui::TextUnformatted(titleText.c_str());
        ImGui::PopStyleColor();

        ImGui::SameLine(startPosX + panelWidth - groupTextWidth);
        ImGui::PushStyleColor(ImGuiCol_Text, Color(EColor::TextMuted));
        ImGui::TextUnformatted(groupProgressText.c_str());
        ImGui::PopStyleColor();
        ImGui::PopFont();

        const float totalRenderers = static_cast<float>(std::max(pipelineWarmup.totalRenderers, 1u));
        const float progressFraction = std::clamp(
            static_cast<float>(pipelineWarmup.completedRenderers) / totalRenderers, 0.0f, 1.0f);
        constexpr float barHeight = 3.0f;
        const ImVec2 barPos = ImGui::GetCursorScreenPos();
        ImDrawList* drawList = ImGui::GetWindowDrawList();

        drawList->AddRectFilled(
            barPos,
            ImVec2(barPos.x + panelWidth, barPos.y + barHeight),
            ColorU32(EColor::BorderStrong, 0.45f),
            1.5f);

        if (progressFraction > 0.0f)
        {
            const float fillWidth = std::max(2.0f, panelWidth * progressFraction);
            drawList->AddRectFilled(
                barPos,
                ImVec2(barPos.x + fillWidth, barPos.y + barHeight),
                ColorU32(EColor::AccentHover),
                1.5f);
        }
        ImGui::Dummy(ImVec2(panelWidth, barHeight));

        const std::string activeRenderer = pipelineWarmup.currentRenderer.empty()
            ? "Preparing pipeline cache..."
            : pipelineWarmup.currentRenderer;

        ImGui::PushStyleColor(ImGuiCol_Text, Color(EColor::AccentHover));
        ImGui::TextUnformatted(ICON_FA_SLIDERS);
        ImGui::PopStyleColor();
        ImGui::SameLine(0.0f, 8.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, Color(EColor::Text));
        ImGui::TextUnformatted(activeRenderer.c_str());
        ImGui::PopStyleColor();

        ImGui::PushStyleColor(ImGuiCol_Text, Color(EColor::TextDim));
        ImGui::TextUnformatted(ICON_FA_BOLT);
        ImGui::PopStyleColor();
        ImGui::SameLine(0.0f, 8.0f);

        std::string statsText = fmt::format("{} pipelines warmed", pipelineWarmup.completedPipelines);
        if (pipelineWarmup.pipelinesCreatedBeforeWarmup > 0)
        {
            statsText += fmt::format(" ({} baseline)", pipelineWarmup.pipelinesCreatedBeforeWarmup);
        }
        ImGui::PushStyleColor(ImGuiCol_Text, Color(EColor::TextMuted));
        ImGui::TextUnformatted(statsText.c_str());
        ImGui::PopStyleColor();

        ImGui::End();
        ImGui::PopStyleVar();
    }
}
