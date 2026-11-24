#pragma once

#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Runtime/RuntimeFwd.hpp"

#include <glm/vec4.hpp>
#include <imgui.h>

namespace NextUI
{
    enum class EColor : uint8_t
    {
        Text = 0,
        TextMuted,
        TextDim,
        Background,
        Surface,
        SurfaceElevated,
        SurfaceHover,
        Border,
        BorderStrong,
        Accent,
        AccentHover,
        Brand,
        Success,
        Warning,
        Danger,
        Blue,
        Count,
    };

    struct FUiMetrics
    {
        float scale = 1.0f;
        float spacing = 6.0f;
        float radius = 5.0f;
        float controlHeight = 27.0f;
        float toolbarHeight = 38.0f;
        float titleBarHeight = 48.0f;
        float footerHeight = 30.0f;

        static FUiMetrics FromScale(float scale);
    };

    struct FViewportRect
    {
        ImVec2 Position{};
        ImVec2 Size{};
    };

    ImVec4 Color(EColor color, float alpha = 1.0f);
    ImU32 ColorU32(EColor color, float alpha = 1.0f);
    uint32_t PackRgba8(EColor color, float alpha = 1.0f);

    inline ImU32 Vec4ToImU32(const glm::vec4& color)
    {
        return IM_COL32(static_cast<int>(color.r * 255.0f),
                        static_cast<int>(color.g * 255.0f),
                        static_cast<int>(color.b * 255.0f),
                        static_cast<int>(color.a * 255.0f));
    }

    inline ImTextureID ToImTextureId(const uint64_t handle)
    {
        return static_cast<ImTextureID>(static_cast<uintptr_t>(handle));
    }

    // Installs the modern flat dark theme into the current ImGui context.
    void ApplyTheme();

    // Font helpers for the engine's active UI host.
    ImFont* GetDefaultFont(NextEngine& engine);
    ImFont* GetTitleFont(NextEngine& engine);

    // Logical font/layout scale within ImGui's dp coordinate space (1.0f at base 16px).
    float GetUiScale(float baseFontSize = 16.0f);
    // Physical framebuffer density / DPI scale for the main window (e.g. 2.0f on 200% display).
    float GetDpiScale();
    float GetViewportUiScale(const ImGuiViewport* viewport, float baseWidth = 1280.0f, float baseHeight = 720.0f);

    // Logical <-> physical framebuffer coordinate conversions.
    ImVec2 MainFramebufferToImGuiPoint(const ImVec2& position);
    ImVec2 ImGuiToMainFramebufferPoint(const ImVec2& position);
    FViewportRect MainFramebufferToImGuiViewport(const ImVec2& position, const ImVec2& size);
    FViewportRect ImGuiToMainFramebufferViewport(const ImVec2& position, const ImVec2& size);

    void SyncImGuiViewportToSwapChain(NextEngine& engine, const ImVec2& position, const ImVec2& size);
}
