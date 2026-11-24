#include "Engine/Common/CoreMinimal.hpp"

#include "Modules/NextUI/Theme.hpp"

#include "Engine/Rendering/VulkanBaseRenderer.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Utilities/Math.hpp"
#include "Engine/Vulkan/SwapChain.hpp"
#include "Modules/NextUI/NextUIModule.hpp"
#include "Modules/NextUI/UiSystem.hpp"

#include <algorithm>
#include <cmath>

namespace NextUI
{
    namespace
    {
        ImVec4 WithAlpha(ImVec4 color, const float alpha)
        {
            color.w *= alpha;
            return color;
        }

        ImVec2 GetFramebufferScale()
        {
            if (ImGui::GetCurrentContext() == nullptr)
            {
                return ImVec2(1.0f, 1.0f);
            }
            const ImVec2 scale = ImGui::GetIO().DisplayFramebufferScale;
            return ImVec2(scale.x > 0.0f ? scale.x : 1.0f,
                          scale.y > 0.0f ? scale.y : 1.0f);
        }
    }

    FUiMetrics FUiMetrics::FromScale(const float scale)
    {
        const float s = std::max(0.5f, scale);
        return FUiMetrics{
            .scale = s,
            .spacing = 6.0f * s,
            .radius = 5.0f * s,
            .controlHeight = 27.0f * s,
            .toolbarHeight = 38.0f * s,
            .titleBarHeight = 48.0f * s,
            .footerHeight = 30.0f * s,
        };
    }

    ImVec4 Color(const EColor color, const float alpha)
    {
        ImVec4 result;
        switch (color)
        {
        case EColor::Text: result = ImVec4(0.90f, 0.90f, 0.90f, 1.0f); break;
        case EColor::TextMuted: result = ImVec4(0.67f, 0.67f, 0.67f, 1.0f); break;
        case EColor::TextDim: result = ImVec4(0.47f, 0.47f, 0.47f, 1.0f); break;
        case EColor::Background: result = ImVec4(0.040f, 0.040f, 0.040f, 1.0f); break;
        case EColor::Surface: result = ImVec4(0.098f, 0.098f, 0.098f, 1.0f); break;
        case EColor::SurfaceElevated: result = ImVec4(0.148f, 0.148f, 0.148f, 1.0f); break;
        case EColor::SurfaceHover: result = ImVec4(0.205f, 0.205f, 0.205f, 1.0f); break;
        case EColor::Border: result = ImVec4(0.260f, 0.260f, 0.260f, 1.0f); break;
        case EColor::BorderStrong: result = ImVec4(0.360f, 0.360f, 0.360f, 1.0f); break;
        case EColor::Accent: result = ImVec4(0.18f, 0.43f, 0.78f, 1.0f); break;
        case EColor::AccentHover: result = ImVec4(0.26f, 0.53f, 0.90f, 1.0f); break;
        case EColor::Brand: result = ImVec4(0.95f, 0.58f, 0.14f, 1.0f); break;
        case EColor::Success: result = ImVec4(0.22f, 0.78f, 0.38f, 1.0f); break;
        case EColor::Warning: result = ImVec4(0.95f, 0.70f, 0.24f, 1.0f); break;
        case EColor::Danger: result = ImVec4(0.92f, 0.25f, 0.28f, 1.0f); break;
        case EColor::Blue:
        case EColor::Count:
        default: result = ImVec4(0.38f, 0.62f, 0.94f, 1.0f); break;
        }
        return WithAlpha(result, alpha);
    }

    ImU32 ColorU32(const EColor color, const float alpha)
    {
        return ImGui::ColorConvertFloat4ToU32(Color(color, alpha));
    }

    uint32_t PackRgba8(const EColor color, const float alpha)
    {
        const ImVec4 c = Color(color, alpha);
        const auto toByte = [](const float v) -> uint32_t
        {
            return static_cast<uint32_t>(std::clamp(std::lround(v * 255.0f), 0l, 255l));
        };
        return toByte(c.x) | (toByte(c.y) << 8u) | (toByte(c.z) << 16u) | (toByte(c.w) << 24u);
    }

    void ApplyTheme()
    {
        ImGuiStyle& style = ImGui::GetStyle();
        ImGui::StyleColorsDark(&style);
        ImVec4* colors = style.Colors;
        colors[ImGuiCol_Text] = Color(EColor::Text);
        colors[ImGuiCol_TextDisabled] = Color(EColor::TextDim);
        colors[ImGuiCol_WindowBg] = Color(EColor::Surface, 0.98f);
        colors[ImGuiCol_ChildBg] = Color(EColor::Background, 0.62f);
        colors[ImGuiCol_PopupBg] = Color(EColor::SurfaceElevated, 0.98f);
        colors[ImGuiCol_Border] = Color(EColor::Border, 0.28f);
        colors[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
        colors[ImGuiCol_FrameBg] = Color(EColor::Background, 0.96f);
        colors[ImGuiCol_FrameBgHovered] = Color(EColor::SurfaceHover, 0.96f);
        colors[ImGuiCol_FrameBgActive] = Color(EColor::SurfaceHover);
        colors[ImGuiCol_TitleBg] = Color(EColor::Background);
        colors[ImGuiCol_TitleBgActive] = Color(EColor::Background);
        colors[ImGuiCol_TitleBgCollapsed] = Color(EColor::Background);
        colors[ImGuiCol_MenuBarBg] = Color(EColor::Background);
        colors[ImGuiCol_ScrollbarBg] = Color(EColor::Background, 0.26f);
        colors[ImGuiCol_ScrollbarGrab] = Color(EColor::BorderStrong, 0.92f);
        colors[ImGuiCol_ScrollbarGrabHovered] = Color(EColor::TextDim);
        colors[ImGuiCol_ScrollbarGrabActive] = Color(EColor::TextMuted);
        colors[ImGuiCol_CheckMark] = Color(EColor::AccentHover);
        colors[ImGuiCol_SliderGrab] = Color(EColor::Blue);
        colors[ImGuiCol_SliderGrabActive] = Color(EColor::AccentHover);
        colors[ImGuiCol_Button] = Color(EColor::SurfaceElevated, 0.94f);
        colors[ImGuiCol_ButtonHovered] = Color(EColor::SurfaceHover);
        colors[ImGuiCol_ButtonActive] = Color(EColor::Accent);
        colors[ImGuiCol_Header] = Color(EColor::SurfaceElevated, 0.90f);
        colors[ImGuiCol_HeaderHovered] = Color(EColor::SurfaceHover, 0.96f);
        colors[ImGuiCol_HeaderActive] = Color(EColor::Accent, 0.82f);
        colors[ImGuiCol_Separator] = Color(EColor::Border, 0.20f);
        colors[ImGuiCol_SeparatorHovered] = Color(EColor::AccentHover, 0.50f);
        colors[ImGuiCol_SeparatorActive] = Color(EColor::AccentHover, 0.80f);
        colors[ImGuiCol_ResizeGrip] = Color(EColor::BorderStrong, 0.55f);
        colors[ImGuiCol_ResizeGripHovered] = Color(EColor::AccentHover);
        colors[ImGuiCol_ResizeGripActive] = Color(EColor::AccentHover);
        colors[ImGuiCol_Tab] = Color(EColor::Surface, 0.96f);
        colors[ImGuiCol_TabHovered] = Color(EColor::SurfaceHover);
        colors[ImGuiCol_TabActive] = Color(EColor::SurfaceElevated, 0.98f);
        colors[ImGuiCol_TabUnfocused] = Color(EColor::Surface, 0.82f);
        colors[ImGuiCol_TabUnfocusedActive] = Color(EColor::SurfaceElevated, 0.88f);
        colors[ImGuiCol_DockingPreview] = Color(EColor::Accent, 0.55f);
        colors[ImGuiCol_DockingEmptyBg] = Color(EColor::Background);
        colors[ImGuiCol_TableHeaderBg] = Color(EColor::SurfaceElevated, 0.92f);
        colors[ImGuiCol_TableBorderStrong] = Color(EColor::BorderStrong, 0.40f);
        colors[ImGuiCol_TableBorderLight] = Color(EColor::Border, 0.20f);
        colors[ImGuiCol_TableRowBg] = Color(EColor::Background, 0.10f);
        colors[ImGuiCol_TableRowBgAlt] = Color(EColor::Surface, 0.18f);
        colors[ImGuiCol_PlotHistogram] = Color(EColor::Success);
        colors[ImGuiCol_PlotHistogramHovered] = Color(EColor::AccentHover);
        colors[ImGuiCol_PlotLines] = Color(EColor::Blue);
        colors[ImGuiCol_PlotLinesHovered] = Color(EColor::AccentHover);
        colors[ImGuiCol_TextSelectedBg] = Color(EColor::Accent, 0.45f);
        colors[ImGuiCol_NavHighlight] = Color(EColor::AccentHover);
        colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.62f);

        style.WindowPadding = ImVec2(8.0f, 7.0f);
        style.FramePadding = ImVec2(7.0f, 4.0f);
        style.DisabledAlpha = 0.42f;
        style.CellPadding = ImVec2(7.0f, 4.0f);
        style.ItemSpacing = ImVec2(6.0f, 5.0f);
        style.ItemInnerSpacing = ImVec2(5.0f, 4.0f);
        style.TouchExtraPadding = ImVec2(0.0f, 0.0f);
        style.IndentSpacing = 12.0f;
        style.ScrollbarSize = 10.0f;
        style.GrabMinSize = 10.0f;
        style.WindowBorderSize = 1.0f;
        style.ChildBorderSize = 0.0f;
        style.PopupBorderSize = 1.0f;
        style.FrameBorderSize = 0.0f;
        style.TabBorderSize = 0.0f;
        style.WindowRounding = 7.0f;
        style.ChildRounding = 6.0f;
        style.FrameRounding = 5.0f;
        style.PopupRounding = 7.0f;
        style.ScrollbarRounding = 8.0f;
        style.GrabRounding = 7.0f;
        style.TabRounding = 5.0f;
        style.WindowMenuButtonPosition = ImGuiDir_None;
        style.SeparatorTextBorderSize = 1.0f;
    }

    ImFont* GetDefaultFont(NextEngine& engine)
    {
        if (FUiSystem* ui = Modules::NextUI::Get(engine))
        {
            return ui->GetDefaultFont();
        }
        return nullptr;
    }

    ImFont* GetTitleFont(NextEngine& engine)
    {
        if (FUiSystem* ui = Modules::NextUI::Get(engine))
        {
            return ui->GetTitleBarFont();
        }
        return nullptr;
    }

    float GetUiScale(const float baseFontSize)
    {
        if (ImGui::GetCurrentContext() == nullptr || baseFontSize <= 0.0f)
        {
            return 1.0f;
        }
        const float fontSize = ImGui::GetFontSize();
        return fontSize > 0.0f ? fontSize / baseFontSize : 1.0f;
    }

    float GetDpiScale()
    {
        if (NextEngine* engine = NextEngine::GetInstance())
        {
            if (const Runtime::IUiHost* ui = engine->GetUiHost())
            {
                return std::max(0.1f, ui->GetUiScale());
            }
        }
        return GetFramebufferScale().x;
    }

    float GetViewportUiScale(const ImGuiViewport* viewport, const float baseWidth, const float baseHeight)
    {
        if (!viewport || viewport->Size.x <= 0.0f || viewport->Size.y <= 0.0f || baseWidth <= 0.0f || baseHeight <= 0.0f)
        {
            return 1.0f;
        }
        return std::max(0.75f, std::min(viewport->Size.x / baseWidth, viewport->Size.y / baseHeight));
    }

    ImVec2 MainFramebufferToImGuiPoint(const ImVec2& position)
    {
        const ImVec2 scale = GetFramebufferScale();
        const ImVec2 origin = ImGui::GetMainViewport()->Pos;
        return ImVec2(origin.x + position.x / scale.x, origin.y + position.y / scale.y);
    }

    ImVec2 ImGuiToMainFramebufferPoint(const ImVec2& position)
    {
        const ImVec2 scale = GetFramebufferScale();
        const ImVec2 origin = ImGui::GetMainViewport()->Pos;
        return ImVec2((position.x - origin.x) * scale.x, (position.y - origin.y) * scale.y);
    }

    FViewportRect MainFramebufferToImGuiViewport(const ImVec2& position, const ImVec2& size)
    {
        const ImVec2 scale = GetFramebufferScale();
        return {
            .Position = MainFramebufferToImGuiPoint(position),
            .Size = ImVec2(size.x / scale.x, size.y / scale.y),
        };
    }

    FViewportRect ImGuiToMainFramebufferViewport(const ImVec2& position, const ImVec2& size)
    {
        const ImVec2 scale = GetFramebufferScale();
        return {
            .Position = ImGuiToMainFramebufferPoint(position),
            .Size = ImVec2(size.x * scale.x, size.y * scale.y),
        };
    }

    void SyncImGuiViewportToSwapChain(NextEngine& engine, const ImVec2& position, const ImVec2& size)
    {
        const FViewportRect framebufferViewport = ImGuiToMainFramebufferViewport(position, size);
        engine.GetRenderer().SwapChain().UpdateOutputViewport(
            Utilities::Math::floorToInt(framebufferViewport.Position.x),
            Utilities::Math::floorToInt(framebufferViewport.Position.y),
            Utilities::Math::ceilToInt(std::max(1.0f, framebufferViewport.Size.x)),
            Utilities::Math::ceilToInt(std::max(1.0f, framebufferViewport.Size.y)));
    }
}
