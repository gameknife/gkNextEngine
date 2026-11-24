#pragma once

#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Assets/AssetsFwd.hpp"
#include "Engine/Runtime/RuntimeFwd.hpp"
#include "Engine/Vulkan/VulkanFwd.hpp"

#include <functional>
#include <glm/vec2.hpp>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vulkan/vulkan.h>

union SDL_Event;

namespace Runtime
{
    enum class EUiDeveloperLayer : uint8_t
    {
        None = 0,
        Statistics = 1u << 0u,
        Console = 1u << 1u,
        Memory = 1u << 2u,
        All = (1u << 0u) | (1u << 1u) | (1u << 2u),
    };

    constexpr EUiDeveloperLayer operator&(const EUiDeveloperLayer lhs, const EUiDeveloperLayer rhs)
    {
        return static_cast<EUiDeveloperLayer>(static_cast<uint8_t>(lhs) & static_cast<uint8_t>(rhs));
    }

    constexpr EUiDeveloperLayer operator|(const EUiDeveloperLayer lhs, const EUiDeveloperLayer rhs)
    {
        return static_cast<EUiDeveloperLayer>(static_cast<uint8_t>(lhs) | static_cast<uint8_t>(rhs));
    }

    constexpr bool HasUiLayer(const EUiDeveloperLayer mask, const EUiDeveloperLayer layer)
    {
        return (static_cast<uint8_t>(mask) & static_cast<uint8_t>(layer)) != 0;
    }

    struct FUiFramePolicy
    {
        bool allowApplicationUi = true;
        EUiDeveloperLayer allowedDeveloperLayers = EUiDeveloperLayer::All;
    };

    struct FUiFrameResult
    {
        EUiDeveloperLayer requestedDeveloperLayers = EUiDeveloperLayer::All;

        static constexpr FUiFrameResult ForApplicationHandled(const bool handled = true)
        {
            return {handled ? EUiDeveloperLayer::None : EUiDeveloperLayer::All};
        }
    };

    enum class EUiSurfaceKind : uint8_t
    {
        MainWindow,
        RemoteView,
    };

    struct FUiFrameContext
    {
        EUiSurfaceKind surfaceKind = EUiSurfaceKind::MainWindow;
        std::string_view sessionId{};
        VkExtent2D framebufferExtent{};
        const Assets::Camera* viewCamera = nullptr;
        bool allowWindowCommands = true;
        FUiFramePolicy policy{};
    };

    struct FUiStatistics final
    {
        VkExtent2D FramebufferSize{};
        VkExtent2D RenderSize{};
        float FrameRate = 0.0f;
        float FrameTime = 0.0f;
        float RayRate = 0.0f;
        uint32_t TotalSamples = 0;
        uint32_t TotalFrames = 0;
        double RenderTime = 0.0;
        uint32_t TriCount = 0;
        uint32_t InstanceCount = 0;
        uint32_t NodeCount = 0;
        uint32_t TextureCount = 0;
        uint32_t ComputePassCount = 0;
        bool LoadingStatus = false;
        mutable std::unordered_map<std::string, std::string> Stats;
    };

    enum class EUiTextureLifetime : uint8_t
    {
        Transient,
        Persistent,
    };

    struct FUiTextureHandle
    {
        uint64_t id = 0;
        glm::vec2 pixelSize{0.0f, 0.0f};
        bool valid = false;
    };

    enum class EUiLayerOrder : int32_t
    {
        Background = 0,
        Application = 100,
        Document = 200,
        Developer = 300,
        SystemModal = 400,
    };

    // Pluggable UI layer (e.g. RmlUi retained HTML/CSS document layer) hosted inside IUiHost.
    // Events dispatch in reverse Z-order (topmost first); rendering executes in forward Z-order.
    class IUiLayer
    {
    public:
        virtual ~IUiLayer() = default;

        virtual std::string_view GetLayerId() const = 0;
        virtual int32_t GetOrder() const = 0;

        virtual bool HandleEvent(const SDL_Event& event)
        {
            (void)event;
            return false;
        }

        virtual void BeginFrame(glm::vec2 logicalSize, float dpiScale)
        {
            (void)logicalSize;
            (void)dpiScale;
        }

        virtual void RenderLayer(NextEngine& engine) = 0;

        virtual bool WantsToCaptureMouse() const { return false; }
        virtual bool WantsToCaptureKeyboard() const { return false; }
    };

    using UiLayerFactory = std::function<std::unique_ptr<IUiLayer>(NextEngine&)>;

    struct FUiHostConfig
    {
        bool enableMultiViewport = false;
        std::function<void()> onPreConfig;
        std::function<void()> onInit;
    };

    // Pure engine-side UI host contract (zero Dear ImGui header/symbol leakage).
    // Implemented by Modules/NextUI.
    class IUiHost
    {
    public:
        virtual ~IUiHost() = default;

        virtual void OnCreateSurface(const Vulkan::SwapChain& swapChain) = 0;
        virtual void OnDestroySurface() = 0;

        virtual void BuildWarmupFrame(NextEngine& engine) = 0;
        virtual void BuildFrame(NextEngine& engine,
                                const FUiFrameContext& context,
                                const FUiStatistics& statistics) = 0;
        virtual void RecordRender(VkCommandBuffer commandBuffer,
                                  const Vulkan::SwapChain& swapChain,
                                  uint32_t imageIndex,
                                  bool suppressAllUi = false) = 0;

        virtual void AddLayer(std::unique_ptr<IUiLayer> layer) = 0;
        virtual IUiLayer* FindLayer(std::string_view layerId) const = 0;

        virtual bool HandleEvent(const SDL_Event& event) = 0;
        virtual void ClaimInputCapture(bool captureMouse, bool captureKeyboard = false) = 0;
        virtual bool WantsToCaptureMouse() const = 0;
        virtual bool WantsToCaptureKeyboard() const = 0;
        virtual float GetUiScale() const = 0;

        virtual uint64_t ResolveTextureId(uint32_t globalTextureId) = 0;
        virtual uint64_t ResolveRawSlotId(uint32_t bindlessSampleSlot, bool rawOutput = false) = 0;
        virtual uint64_t ResolveTextureByName(std::string_view name) = 0;
        virtual FUiTextureHandle RequestTexture(
            std::string_view path,
            bool srgb = true,
            EUiTextureLifetime lifetime = EUiTextureLifetime::Transient) = 0;
    };

    using UiHostFactory = std::function<std::unique_ptr<IUiHost>(NextEngine&, FUiHostConfig)>;
}
