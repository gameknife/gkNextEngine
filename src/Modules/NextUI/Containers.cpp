#include "Engine/Common/CoreMinimal.hpp"

#include "Modules/NextUI/Containers.hpp"
#include "Modules/NextUI/Widgets.hpp"

#include "Engine/Rendering/VulkanBaseRenderer.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Interface/UiHost.hpp"
#include "Engine/Runtime/Platform/PlatformCommon.hpp"
#include "Engine/Utilities/LogFile.hpp"
#include "Engine/Vulkan/Device.hpp"
#include "ThirdParty/fontawesome/IconsFontAwesome6.h"

#include <SDL3/SDL_misc.h>
#include <algorithm>
#include <cmath>
#include <fmt/format.h>
#include <imgui_internal.h>

namespace NextUI
{
    namespace
    {
        constexpr const char* kBrandIconAssetPath = "assets/brand/gknext_logo_icon.png";
        constexpr const char* kBrandIconAssetPath125 = "assets/brand/gknext_logo_icon_125.png";
        constexpr const char* kBrandIconAssetPath150 = "assets/brand/gknext_logo_icon_150.png";
        constexpr const char* kBrandIconAssetPath175 = "assets/brand/gknext_logo_icon_175.png";
        constexpr const char* kBrandIconAssetPath200 = "assets/brand/gknext_logo_icon_200.png";
        constexpr float kDetailPanelBackgroundAlpha = 0.82f;
        constexpr float kDetailPanelHeaderAlpha = 0.56f;
        constexpr float kDetailPanelBorderAlpha = 0.52f;
        constexpr float kTitleBarControlWidth = 46.0f;

        const char* GetBrandIconAssetPath(const float dpiScale)
        {
            if (dpiScale >= 1.875f) return kBrandIconAssetPath200;
            if (dpiScale >= 1.625f) return kBrandIconAssetPath175;
            if (dpiScale >= 1.375f) return kBrandIconAssetPath150;
            if (dpiScale >= 1.125f) return kBrandIconAssetPath125;
            return kBrandIconAssetPath;
        }

        void DrawEngineBrandIcon(NextEngine& engine, ImDrawList* drawList, const ImVec2 min, const float size)
        {
            if (drawList == nullptr)
            {
                return;
            }

            Runtime::IUiHost* uiHost = engine.GetUiHost();
            if (uiHost == nullptr)
            {
                DrawBrandMark(drawList, min, size);
                return;
            }

            const Runtime::FUiTextureHandle texture =
                uiHost->RequestTexture(GetBrandIconAssetPath(uiHost->GetUiScale()), false,
                                       Runtime::EUiTextureLifetime::Persistent);
            if (!texture.valid || texture.pixelSize.x <= 0.0f || texture.pixelSize.y <= 0.0f)
            {
                DrawBrandMark(drawList, min, size);
                return;
            }

            const float aspect = texture.pixelSize.x / texture.pixelSize.y;
            float drawWidth = size;
            float drawHeight = drawWidth / aspect;
            if (drawHeight > size)
            {
                drawHeight = size;
                drawWidth = drawHeight * aspect;
            }

            const ImVec2 drawMin(min.x + std::floor((size - drawWidth) * 0.5f),
                                 min.y + std::floor((size - drawHeight) * 0.5f));
            const ImVec2 drawMax(drawMin.x + drawWidth, drawMin.y + drawHeight);
            drawList->AddImage(ToImTextureId(texture.id), drawMin, drawMax, ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f),
                               ImGui::GetColorU32(ImGuiCol_Text));
        }

        float CalcFontTextWidth(ImFont* font, const char* text)
        {
            if (text == nullptr || text[0] == '\0')
            {
                return 0.0f;
            }
            ImFont* activeFont = font != nullptr ? font : ImGui::GetFont();
            return activeFont->CalcTextSizeA(activeFont->LegacySize, FLT_MAX, 0.0f, text).x;
        }

        bool WindowControlButton(const char* label, const char* tooltip, const ImVec2 size, const bool danger = false)
        {
            FScopedStyle style;
            style.Add(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f))
                .Add(ImGuiStyleVar_FrameRounding, 0.0f)
                .Add(ImGuiStyleVar_FrameBorderSize, 0.0f)
                .Add(ImGuiCol_Border, ImVec4(0.0f, 0.0f, 0.0f, 0.0f))
                .Add(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f))
                .Add(ImGuiCol_ButtonHovered, danger ? Color(EColor::Danger, 0.90f) : Color(EColor::SurfaceHover))
                .Add(ImGuiCol_ButtonActive, danger ? Color(EColor::Danger) : Color(EColor::SurfaceHover, 0.92f));
            const bool pressed = ImGui::Button(label, size);
            Tooltip(tooltip);
            return pressed;
        }
    }

    FMainMenuGhostScope::FMainMenuGhostScope() : drawList_(ImGui::GetWindowDrawList())
    {
        if (drawList_ != nullptr)
        {
            drawList_->ChannelsSplit(2);
            drawList_->ChannelsSetCurrent(1);
            channelsSplit_ = true;
        }
    }

    FMainMenuGhostScope::~FMainMenuGhostScope()
    {
        if (channelsSplit_)
        {
            drawList_->ChannelsMerge();
        }
    }

    bool FMainMenuGhostScope::BeginMenu(const char* label, const bool enabled)
    {
        if (!channelsSplit_)
        {
            return ImGui::BeginMenu(label, enabled);
        }

        drawList_->ChannelsSetCurrent(1);
        bool menuOpen = false;
        {
            FScopedStyle transparentHeader;
            transparentHeader.Add(ImGuiCol_Header, ImVec4(0.0f, 0.0f, 0.0f, 0.0f))
                             .Add(ImGuiCol_HeaderHovered, ImVec4(0.0f, 0.0f, 0.0f, 0.0f))
                             .Add(ImGuiCol_HeaderActive, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
            menuOpen = ImGui::BeginMenu(label, enabled);
        }

        const bool menuHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup);
        if (menuHovered || menuOpen)
        {
            const ImVec2 inset(1.0f, 1.0f);
            const ImVec2 itemMin = ImGui::GetItemRectMin() + inset;
            const ImVec2 itemMax = ImGui::GetItemRectMax() - inset;
            drawList_->ChannelsSetCurrent(0);
            drawList_->AddRectFilled(itemMin, itemMax, ColorU32(EColor::SurfaceHover), 5.0f);
        }
        drawList_->ChannelsSetCurrent(1);
        return menuOpen;
    }

    void DrawBrandMark(ImDrawList* drawList, const ImVec2 min, const float size)
    {
        if (drawList == nullptr)
        {
            return;
        }

        const ImVec2 max(min.x + size, min.y + size);
        const float rounding = std::max(3.0f, size * 0.18f);
        drawList->AddRectFilled(min, max, ColorU32(EColor::Brand, 0.16f), rounding);
        drawList->AddRect(min, max, ColorU32(EColor::Brand, 0.92f), rounding, 0, 1.5f);

        const float pad = size * 0.24f;
        const float stroke = std::max(2.0f, size * 0.10f);
        const ImU32 lineColor = ColorU32(EColor::Brand);
        drawList->AddLine(ImVec2(min.x + pad, min.y + size - pad), ImVec2(min.x + pad, min.y + pad), lineColor, stroke);
        drawList->AddLine(ImVec2(min.x + pad, min.y + pad), ImVec2(min.x + size * 0.52f, min.y + pad), lineColor, stroke);
        drawList->AddLine(ImVec2(min.x + size * 0.52f, min.y + pad), ImVec2(min.x + size * 0.52f, min.y + size - pad), lineColor, stroke);
        drawList->AddLine(ImVec2(min.x + size * 0.52f, min.y + size - pad), ImVec2(min.x + size - pad, min.y + size - pad), lineColor, stroke);
        drawList->AddLine(ImVec2(min.x + size - pad, min.y + size - pad), ImVec2(min.x + size - pad, min.y + pad), lineColor, stroke);
    }

    FAppChromeResult DrawAppTitleBarChrome(const FAppTitleBarConfig& config)
    {
        FAppChromeResult result;
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        if (viewport == nullptr)
        {
            return result;
        }

        const bool allowWindowCommands = config.AllowWindowCommands ||
            static_cast<bool>(config.OnMinimize) ||
            static_cast<bool>(config.OnToggleMaximize) ||
            static_cast<bool>(config.OnClose);
        const float controlsWidth = allowWindowCommands ? kTitleBarControlWidth * 3.0f : 0.0f;
        ImFont* titleFont = config.TitleFont != nullptr ? config.TitleFont : ImGui::GetFont();
        const float brandTextWidth = CalcFontTextWidth(titleFont, config.AppName);
        const float brandWidth = config.BrandHorizontalPadding * 2.0f + config.BrandIconSize +
            config.BrandTextSpacing + brandTextWidth;
        const float rightWidth = config.RightContentWidth + controlsWidth;
        const float menuWidth = std::max(
            0.0f, viewport->Size.x - brandWidth - rightWidth - config.MenuTrailingPadding);
        float menuRight = viewport->Pos.x + brandWidth;
        const ImVec2 menuFramePadding = ImGui::GetStyle().FramePadding;

        ImGui::GetBackgroundDrawList()->AddRectFilled(
            viewport->Pos, viewport->Pos + ImVec2(viewport->Size.x, config.Height),
            ColorU32(EColor::Background), 0.0f);

        constexpr ImGuiWindowFlags baseFlags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
            ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav;
        FScopedStyle chromeStyle;
        chromeStyle.Add(ImGuiStyleVar_WindowRounding, 0.0f)
            .Add(ImGuiStyleVar_WindowBorderSize, 0.0f)
            .Add(ImGuiCol_Border, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));

        ImGui::SetNextWindowPos(viewport->Pos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(brandWidth, config.Height), ImGuiCond_Always);
        ImGui::SetNextWindowViewport(viewport->ID);
        ImGui::SetNextWindowBgAlpha(0.0f);
        {
            FScopedStyle brandStyle;
            brandStyle.Add(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
            FScopedWindow brandWindow(config.BrandWindowId, nullptr, baseFlags);
            if (brandWindow)
            {
                ImGui::SetCursorPos(ImVec2(config.BrandHorizontalPadding,
                                           std::floor((config.Height - config.BrandIconSize) * 0.5f)));
                const ImVec2 iconMin = ImGui::GetCursorScreenPos();
                if (config.DrawBrandIcon)
                {
                    config.DrawBrandIcon(ImGui::GetWindowDrawList(), iconMin, config.BrandIconSize);
                }
                else
                {
                    DrawBrandMark(ImGui::GetWindowDrawList(), iconMin, config.BrandIconSize);
                }
                ImGui::Dummy(ImVec2(config.BrandIconSize, config.BrandIconSize));
                ImGui::SameLine(0.0f, config.BrandTextSpacing);
                ImGui::PushFont(titleFont);
                ImGui::SetCursorPosY((config.Height - ImGui::GetTextLineHeight()) * 0.5f);
                ImGui::TextUnformatted(config.AppName != nullptr ? config.AppName : "");
                ImGui::PopFont();
            }
        }

        ImGui::SetNextWindowPos(
            ImVec2(viewport->Pos.x + brandWidth,
                   viewport->Pos.y +
                       (config.Height - ImGui::GetTextLineHeight() - ImGui::GetStyle().FramePadding.y * 2.0f) * 0.5f),
            ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(menuWidth, config.Height), ImGuiCond_Always);
        ImGui::SetNextWindowViewport(viewport->ID);
        ImGui::SetNextWindowBgAlpha(0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        const bool menuWindowVisible = ImGui::Begin(
            config.MenuWindowId, nullptr, baseFlags | ImGuiWindowFlags_MenuBar);
        ImGui::PopStyleVar();
        if (menuWindowVisible)
        {
            FScopedStyle menuBarStyle;
            menuBarStyle.Add(ImGuiStyleVar_FramePadding, menuFramePadding)
                .Add(ImGuiStyleVar_ItemSpacing, ImVec2(14.0f, 6.0f));
            if (ImGui::BeginMenuBar())
            {
                if (config.DrawMenuBar)
                {
                    menuRight = std::max(menuRight, config.DrawMenuBar());
                }
                ImGui::EndMenuBar();
            }
        }
        ImGui::End();

        if (rightWidth > 0.0f)
        {
            ImGui::SetNextWindowPos(
                viewport->Pos + ImVec2(viewport->Size.x - rightWidth, 0.0f), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(rightWidth, config.Height), ImGuiCond_Always);
            ImGui::SetNextWindowViewport(viewport->ID);
            ImGui::SetNextWindowBgAlpha(0.0f);
            {
                FScopedStyle rightStyle;
                rightStyle.Add(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
                FScopedWindow rightWindow(config.RightWindowId, nullptr, baseFlags);
                if (rightWindow)
                {
                    if (config.DrawRightContent) config.DrawRightContent();
                    ImGui::SetCursorPos(ImVec2(config.RightContentWidth, 0.0f));
                    if (allowWindowCommands)
                    {
                        const ImVec2 controlSize(kTitleBarControlWidth, config.Height);
                        if (WindowControlButton(ICON_FA_WINDOW_MINIMIZE, "Minimize", controlSize))
                            result.action = EAppChromeAction::Minimize;
                        ImGui::SameLine(0.0f, 0.0f);
                        if (WindowControlButton(config.IsMaximized ? ICON_FA_WINDOW_RESTORE : ICON_FA_WINDOW_MAXIMIZE,
                                                config.IsMaximized ? "Restore" : "Maximize", controlSize))
                            result.action = EAppChromeAction::ToggleMaximize;
                        ImGui::SameLine(0.0f, 0.0f);
                        if (WindowControlButton(ICON_FA_XMARK, "Close", controlSize, true))
                            result.action = EAppChromeAction::Close;
                    }
                }
            }
        }

        result.dragHeight = config.Height;
        result.dragLeftReservedWidth = std::max(
            brandWidth + 12.0f, menuRight - viewport->Pos.x + config.MenuHitPadding);
        result.dragRightReservedWidth = rightWidth;
        return result;
    }

    void DrawAppTitleBar(NextEngine& engine, const FAppTitleBarConfig& config)
    {
        FAppTitleBarConfig resolved = config;
        if (resolved.TitleFont == nullptr)
        {
            resolved.TitleFont = GetTitleFont(engine);
            if (resolved.TitleFont == nullptr)
            {
                resolved.TitleFont = GetDefaultFont(engine);
            }
        }
        if (!resolved.DrawBrandIcon)
        {
            resolved.DrawBrandIcon = [&engine](ImDrawList* drawList, const ImVec2 min, const float size)
            {
                DrawEngineBrandIcon(engine, drawList, min, size);
            };
        }
        const bool hasWindowCommands = resolved.AllowWindowCommands ||
            static_cast<bool>(resolved.OnMinimize) ||
            static_cast<bool>(resolved.OnToggleMaximize) ||
            static_cast<bool>(resolved.OnClose);
        resolved.AllowWindowCommands = hasWindowCommands;

        const FAppChromeResult result = DrawAppTitleBarChrome(resolved);
        if (hasWindowCommands)
        {
            engine.ConfigureCustomTitleBarDrag(true, result.dragHeight,
                                               result.dragLeftReservedWidth, result.dragRightReservedWidth);
        }
        switch (result.action)
        {
        case EAppChromeAction::Minimize:
            if (resolved.OnMinimize) resolved.OnMinimize();
            break;
        case EAppChromeAction::ToggleMaximize:
            if (resolved.OnToggleMaximize) resolved.OnToggleMaximize();
            break;
        case EAppChromeAction::Close:
            if (resolved.OnClose) resolved.OnClose();
            break;
        case EAppChromeAction::None:
            break;
        }
    }

    void DrawBottomBar(const FBottomBarConfig& config)
    {
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        if (viewport == nullptr)
        {
            return;
        }
        ImGui::SetNextWindowPos(
            ImVec2(viewport->Pos.x, viewport->Pos.y + viewport->Size.y - config.Height), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, config.Height), ImGuiCond_Always);
        ImGui::SetNextWindowViewport(viewport->ID);
        ImGui::SetNextWindowBgAlpha(config.BackgroundAlpha);
        FScopedStyle style;
        style.Add(ImGuiStyleVar_WindowPadding, ImVec2(config.HorizontalPadding, config.VerticalPadding))
            .Add(ImGuiStyleVar_WindowMinSize, ImVec2(0.0f, 0.0f))
            .Add(ImGuiStyleVar_WindowBorderSize, 0.0f)
            .Add(ImGuiStyleVar_WindowRounding, 0.0f)
            .Add(ImGuiCol_WindowBg, Color(EColor::Background, 0.98f));
        constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoDocking;
        FScopedWindow window(config.WindowId, nullptr, flags);
        if (!window)
        {
            return;
        }
        if (config.DrawLeftContent) config.DrawLeftContent();
        if (config.DrawCenterContent)
        {
            ImGui::SameLine();
            ImGui::SetCursorPosX((viewport->Size.x - config.CenterWidth) * 0.5f);
            config.DrawCenterContent();
        }
        if (config.DrawRightContent)
        {
            ImGui::SameLine();
            ImGui::SetCursorPosX(viewport->Size.x - config.RightWidth - config.HorizontalPadding);
            config.DrawRightContent();
        }
    }

    bool BeginOverlayPanel(const FOverlayPanelConfig& config)
    {
        ImGui::SetNextWindowPos(config.Position, ImGuiCond_Always);
        ImGui::SetNextWindowSize(config.Size, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(config.BackgroundAlpha);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, config.Padding);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, config.ItemSpacing);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, config.Rounding);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, config.BorderSize);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, Color(config.BackgroundColor, config.BackgroundAlpha));
        ImGui::PushStyleColor(ImGuiCol_Border, Color(EColor::BorderStrong, config.BorderAlpha));

        const ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | config.ExtraFlags;
        return ImGui::Begin(config.WindowId, nullptr, flags);
    }

    void EndOverlayPanel()
    {
        ImGui::End();
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(4);
    }

    bool BeginInsetPanel(const char* id,
                         const ImVec2 size,
                         const bool border,
                         const ImGuiWindowFlags flags,
                         const ImVec2 padding,
                         const float backgroundAlpha)
    {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, Color(EColor::Background, backgroundAlpha));
        ImGui::PushStyleColor(ImGuiCol_Border, Color(EColor::Border, 0.82f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, padding);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, border ? 1.0f : 0.0f);
        const ImGuiChildFlags childFlags = ImGuiChildFlags_AlwaysUseWindowPadding |
            (border ? ImGuiChildFlags_Borders : ImGuiChildFlags_None);
        const bool visible = ImGui::BeginChild(id, size, childFlags, flags);
        ImGui::PopStyleVar();
        return visible;
    }

    void EndInsetPanel()
    {
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(2);
    }

    void BeginCard(const char* id, const float height, const ImGuiWindowFlags flags, const float horizontalInset)
    {
        ImGui::Dummy(ImVec2(0.0f, 2.0f));
        const float width = std::max(0.0f, ImGui::GetContentRegionAvail().x - horizontalInset * 2.0f);
        if (horizontalInset > 1.0f)
        {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + horizontalInset);
        }
        FScopedStyle style;
        style.Add(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 8.0f))
             .Add(ImGuiStyleVar_ChildRounding, 6.0f)
             .Add(ImGuiStyleVar_ChildBorderSize, 1.0f)
             .Add(ImGuiCol_ChildBg, Color(EColor::SurfaceElevated, 0.40f))
             .Add(ImGuiCol_Border, Color(EColor::Border, 0.70f));
        const ImGuiChildFlags childFlags = height > 0.0f
            ? ImGuiChildFlags_Borders
            : ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY;
        ImGui::BeginChild(id, ImVec2(width, std::max(0.0f, height)), childFlags, flags);
    }

    void EndCard()
    {
        ImGui::EndChild();
        ImGui::Dummy(ImVec2(0.0f, 3.0f));
    }

    bool BeginSection(const char* icon, const char* label, const bool defaultOpen)
    {
        bool open = false;
        {
            FScopedStyle style;
            style.Add(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 4.0f))
                 .Add(ImGuiStyleVar_FrameBorderSize, 1.0f)
                 .Add(ImGuiCol_Header, Color(EColor::Background, 0.5f))
                 .Add(ImGuiCol_HeaderHovered, Color(EColor::SurfaceHover, 0.5f))
                 .Add(ImGuiCol_HeaderActive, Color(EColor::Accent, 0.5f))
                 .Add(ImGuiCol_Border, Color(EColor::Border, 0.84f));

            const std::string header = fmt::format("{} {}", icon ? icon : "", label ? label : "");
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_Framed;
            if (defaultOpen)
            {
                flags |= ImGuiTreeNodeFlags_DefaultOpen;
            }
            open = ImGui::CollapsingHeader(header.c_str(), flags);
        }

        if (open)
        {
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 5.0f));
            ImGui::Indent(5.0f);
        }
        return open;
    }

    void EndSection()
    {
        ImGui::Unindent(5.0f);
        ImGui::PopStyleVar();
        ImGui::Spacing();
    }

    bool BeginPanelSection(const char* label, const bool defaultOpen)
    {
        bool open = false;
        {
            FScopedStyle style;
            style.Add(ImGuiCol_Header, ImVec4(0, 0, 0, 0))
                 .Add(ImGuiCol_HeaderHovered, Color(EColor::SurfaceHover, 0.45f))
                 .Add(ImGuiCol_HeaderActive, Color(EColor::SurfaceHover, 0.65f))
                 .Add(ImGuiCol_Text, Color(EColor::Text, 0.92f))
                 .Add(ImGuiStyleVar_FramePadding, ImVec2(2.0f, 4.0f));

            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_AllowOverlap;
            if (defaultOpen)
            {
                flags |= ImGuiTreeNodeFlags_DefaultOpen;
            }
            open = ImGui::CollapsingHeader(label ? label : "", flags);
        }

        if (open)
        {
            ImGui::PushID(label);
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 5.0f));
            ImGui::Dummy(ImVec2(0.0f, 1.0f));
        }
        return open;
    }

    void EndPanelSection()
    {
        ImGui::PopID();
        ImGui::PopStyleVar();
        ImGui::Dummy(ImVec2(0.0f, 4.0f));
    }

    bool BeginFloatingPanel(const char* id, const char* icon, const char* title, bool* pOpen,
                            const ImVec2 position, const ImVec2 size, const ImVec2 pivot, const bool detachedViewport)
    {
        if (detachedViewport && (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0)
        {
            ImGuiWindowClass windowClass;
            windowClass.ParentViewportId = 0;
            windowClass.ViewportFlagsOverrideSet = ImGuiViewportFlags_NoAutoMerge;
            ImGui::SetNextWindowClass(&windowClass);
            ImGui::SetNextWindowPos(position, ImGuiCond_Appearing, pivot);
            ImGui::SetNextWindowSize(size, ImGuiCond_Appearing);
        }
        else
        {
            ImGui::SetNextWindowPos(position, ImGuiCond_Always, pivot);
            ImGui::SetNextWindowSize(size, ImGuiCond_Always);
        }
        ImGui::SetNextWindowBgAlpha(kDetailPanelBackgroundAlpha);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Border, Color(EColor::Border, kDetailPanelBorderAlpha));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, Color(EColor::Background, kDetailPanelBackgroundAlpha));

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoNavInputs;
        if (!detachedViewport)
        {
            flags |= ImGuiWindowFlags_NoMove;
        }

        const bool visible = ImGui::Begin(id, nullptr, flags);
        if (!visible)
        {
            ImGui::End();
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(3);
            return false;
        }

        constexpr float headerHeight = 38.0f;
        const ImVec2 winPos = ImGui::GetWindowPos();
        const ImVec2 winSize = ImGui::GetWindowSize();
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(
            winPos, ImVec2(winPos.x + winSize.x, winPos.y + headerHeight),
            ColorU32(EColor::Surface, kDetailPanelHeaderAlpha), 8.0f, ImDrawFlags_RoundCornersTop);
        drawList->AddLine(
            ImVec2(winPos.x, winPos.y + headerHeight),
            ImVec2(winPos.x + winSize.x, winPos.y + headerHeight),
            ColorU32(EColor::Border, kDetailPanelBorderAlpha));

        const float textY = winPos.y + (headerHeight - ImGui::GetTextLineHeight()) * 0.5f;
        const float textX = winPos.x + 14.0f;
        const ImU32 iconCol = ColorU32(EColor::AccentHover);
        const ImU32 titleCol = ColorU32(EColor::Text);
        if (icon != nullptr && icon[0] != '\0')
        {
            drawList->AddText(ImVec2(textX, textY), iconCol, icon);
            const float iconWidth = ImGui::CalcTextSize(icon).x;
            drawList->AddText(ImVec2(textX + iconWidth + 8.0f, textY), titleCol, title ? title : "");
        }
        else
        {
            drawList->AddText(ImVec2(textX, textY), titleCol, title ? title : "");
        }

        if (pOpen != nullptr)
        {
            constexpr float closeSize = 20.0f;
            ImGui::SetCursorScreenPos(ImVec2(winPos.x + winSize.x - closeSize - 10.0f,
                                             winPos.y + (headerHeight - closeSize) * 0.5f));
            FScopedStyle closeStyle;
            closeStyle.Add(ImGuiCol_Button, ImVec4(0, 0, 0, 0))
                      .Add(ImGuiCol_ButtonHovered, Color(EColor::SurfaceHover, 0.7f))
                      .Add(ImGuiCol_ButtonActive, Color(EColor::SurfaceHover))
                      .Add(ImGuiCol_Text, Color(EColor::TextMuted))
                      .Add(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
            if (ImGui::Button(ICON_FA_XMARK, ImVec2(closeSize, closeSize)))
            {
                *pOpen = false;
            }
        }

        ImGui::SetCursorScreenPos(ImVec2(winPos.x, winPos.y + headerHeight));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18.0f, 14.0f));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
        ImGui::BeginChild("##FloatingPanelBody", ImVec2(0, 0), false, ImGuiWindowFlags_NoBackground);
        return true;
    }

    void EndFloatingPanel()
    {
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();

        ImGui::End();
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(3);
    }

    bool BeginDetailPanel(const FDetailPanelConfig& config)
    {
        if (!BeginFloatingPanel(config.WindowId, config.Icon, config.Title, config.Open,
                                config.Position, config.Size, config.Pivot, config.DetachedViewport))
        {
            return false;
        }

        BeginInsetPanel(config.ContentWindowId, ImVec2(0.0f, 0.0f), config.ContentBorder,
                        config.ContentFlags, config.ContentPadding, config.ContentBackgroundAlpha);
        return true;
    }

    void EndDetailPanel()
    {
        EndInsetPanel();
        EndFloatingPanel();
    }

    bool BeginCollapsibleSidePanel(const FCollapsibleSidePanelConfig& config)
    {
        ImGui::SetNextWindowPos(config.Position, ImGuiCond_Always);
        ImGui::SetNextWindowSize(config.Size, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(config.BackgroundAlpha);

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking |
            config.ExtraFlags;
        if (config.Collapsed)
        {
            flags |= ImGuiWindowFlags_NoScrollbar;
        }

        FScopedStyle style;
        style.Add(ImGuiStyleVar_WindowRounding, 0.0f)
             .Add(ImGuiStyleVar_WindowBorderSize, 0.0f)
             .Add(ImGuiStyleVar_WindowPadding, config.Collapsed ? config.CollapsedPadding : config.ExpandedPadding);
        const bool visible = ImGui::Begin(config.WindowId, nullptr, flags);
        if (!visible)
        {
            ImGui::End();
            return false;
        }
        return true;
    }

    void EndCollapsibleSidePanel()
    {
        ImGui::End();
    }

    void DrawPanelHeader(const char* icon, const char* title, const char* subtitle,
                         const char* badgeText, const ImVec4 badgeColor)
    {
        {
            FScopedStyle style;
            style.Add(ImGuiCol_Text, Color(EColor::Text));
            if (icon != nullptr && icon[0] != '\0')
            {
                ImGui::TextColored(Color(EColor::AccentHover), "%s", icon);
                ImGui::SameLine(0.0f, 6.0f);
            }
            ImGui::TextUnformatted(title ? title : "");
        }

        if (badgeText != nullptr && badgeText[0] != '\0')
        {
            const float badgeWidth = ImGui::CalcTextSize(badgeText).x + 14.0f;
            SameLineRightAligned(badgeWidth, 8.0f);
            const ImVec4 resolvedBadgeColor = badgeColor.w > 0.0f ? badgeColor : Color(EColor::AccentHover);
            TagChip(badgeText, resolvedBadgeColor);
        }

        if (subtitle != nullptr && subtitle[0] != '\0')
        {
            ImGui::TextColored(Color(EColor::TextMuted), "%s", subtitle);
        }

        DrawThinSeparator(0.9f);
    }

    namespace
    {
        thread_local bool GActiveModalHasStyleOverride = false;
    }

    bool BeginModal(const FModalConfig& config)
    {
        ImGuiViewport* anchorViewport = ImGui::GetWindowViewport();
        if (anchorViewport == nullptr)
        {
            anchorViewport = ImGui::GetMainViewport();
        }

        if (anchorViewport != nullptr)
        {
            if (config.BackdropDimAlpha > 0.0f)
            {
                ImGui::GetBackgroundDrawList()->AddRectFilled(
                    anchorViewport->Pos,
                    anchorViewport->Pos + anchorViewport->Size,
                    ColorU32(EColor::Background, std::clamp(config.BackdropDimAlpha, 0.0f, 1.0f)));
            }

            const ImVec2 workPos = anchorViewport->WorkPos;
            const ImVec2 workSize = anchorViewport->WorkSize;
            const ImVec2 maxSize(std::max(1.0f, workSize.x - config.ViewportMargin * 2.0f),
                                 std::max(1.0f, workSize.y - config.ViewportMargin * 2.0f));

            ImGui::SetNextWindowViewport(anchorViewport->ID);
            if (config.CenterInAnchorViewport)
            {
                ImGui::SetNextWindowPos(
                    ImVec2(workPos.x + workSize.x * 0.5f, workPos.y + workSize.y * 0.5f),
                    config.PosCond,
                    ImVec2(0.5f, 0.5f));
            }
            if (config.ClampToAnchorViewport)
            {
                ImGui::SetNextWindowSizeConstraints(ImVec2(1.0f, 1.0f), maxSize);
            }
            if (config.TargetSize.x > 0.0f || config.TargetSize.y > 0.0f)
            {
                const ImVec2 requestedSize(
                    config.TargetSize.x > 0.0f ? std::min(config.TargetSize.x, maxSize.x) : 0.0f,
                    config.TargetSize.y > 0.0f ? std::min(config.TargetSize.y, maxSize.y) : 0.0f);
                ImGui::SetNextWindowSize(requestedSize, config.SizeCond);
            }
        }

        if (config.AutoOpen)
        {
            ImGui::OpenPopup(config.PopupId);
        }

        ImGuiWindowFlags windowFlags = config.ExtraFlags;
        if (config.OverrideStyle)
        {
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, config.Rounding);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, config.Padding);
            ImGui::PushStyleColor(ImGuiCol_WindowBg, Color(EColor::Surface, 0.98f));
            ImGui::PushStyleColor(ImGuiCol_Border, Color(EColor::Border, 0.85f));
            ImGui::PushStyleColor(ImGuiCol_PopupBg, Color(EColor::Surface, 0.98f));
            windowFlags |= ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoTitleBar;
        }

        const bool open = ImGui::BeginPopupModal(config.PopupId, config.Open, windowFlags);
        if (!open)
        {
            if (config.OverrideStyle)
            {
                ImGui::PopStyleColor(3);
                ImGui::PopStyleVar(3);
            }
            return false;
        }

        GActiveModalHasStyleOverride = config.OverrideStyle;

        if (config.ClampToAnchorViewport && anchorViewport != nullptr)
        {
            const ImVec2 workPos = anchorViewport->WorkPos;
            const ImVec2 workSize = anchorViewport->WorkSize;
            const ImVec2 minPos(workPos.x + config.ViewportMargin, workPos.y + config.ViewportMargin);
            const ImVec2 windowSize = ImGui::GetWindowSize();
            const ImVec2 maxPos(
                std::max(minPos.x, workPos.x + workSize.x - windowSize.x - config.ViewportMargin),
                std::max(minPos.y, workPos.y + workSize.y - windowSize.y - config.ViewportMargin));
            const ImVec2 currentPos = ImGui::GetWindowPos();
            const ImVec2 clampedPos(std::clamp(currentPos.x, minPos.x, maxPos.x),
                                    std::clamp(currentPos.y, minPos.y, maxPos.y));
            if (std::fabs(clampedPos.x - currentPos.x) > 0.5f || std::fabs(clampedPos.y - currentPos.y) > 0.5f)
            {
                ImGui::SetWindowPos(clampedPos);
            }
        }
        return true;
    }

    void EndModal()
    {
        ImGui::EndPopup();
        if (GActiveModalHasStyleOverride)
        {
            ImGui::PopStyleColor(3);
            ImGui::PopStyleVar(3);
            GActiveModalHasStyleOverride = false;
        }
    }

    bool BeginAnchoredModal(const char* name,
                            bool* pOpen,
                            const ImGuiWindowFlags flags,
                            const ImVec2 requestedSize,
                            const float viewportMargin)
    {
        return BeginModal(FModalConfig{
            .PopupId = name,
            .TargetSize = requestedSize,
            .OverrideStyle = false,
            .ViewportMargin = viewportMargin,
            .Open = pOpen,
            .ExtraFlags = flags,
        });
    }

    void EndAnchoredModal()
    {
        EndModal();
    }

    bool DrawModalHeader(const char* icon, const char* title, const char* subtitle,
                         ImFont* titleFont, const bool allowClose)
    {
        constexpr float kHeaderHeight = 56.0f;
        const ImVec2 dialogPos = ImGui::GetWindowPos();
        const ImVec2 dialogSize = ImGui::GetWindowSize();
        ImDrawList* drawList = ImGui::GetWindowDrawList();

        const ImVec2 headerMin = dialogPos;
        const ImVec2 headerMax(dialogPos.x + dialogSize.x, dialogPos.y + kHeaderHeight);

        drawList->AddRectFilled(headerMin, headerMax, ColorU32(EColor::Background, 0.65f), 8.0f,
                                ImDrawFlags_RoundCornersTop);
        drawList->AddLine(ImVec2(headerMin.x, headerMax.y), headerMax, ColorU32(EColor::Border, 0.50f), 1.0f);

        constexpr float headerPadX = 18.0f;
        float titleX = dialogPos.x + headerPadX;
        if (icon != nullptr && icon[0] != '\0')
        {
            constexpr float iconBoxSize = 34.0f;
            const float iconBoxPosY = dialogPos.y + (kHeaderHeight - iconBoxSize) * 0.5f;
            const ImVec2 iconBoxMin(dialogPos.x + headerPadX, iconBoxPosY);
            const ImVec2 iconBoxMax(iconBoxMin.x + iconBoxSize, iconBoxMin.y + iconBoxSize);

            drawList->AddRectFilled(iconBoxMin, iconBoxMax, ColorU32(EColor::Accent, 0.16f), 6.0f);
            drawList->AddRect(iconBoxMin, iconBoxMax, ColorU32(EColor::AccentHover, 0.45f), 6.0f);

            const ImVec2 headerIconSize = ImGui::CalcTextSize(icon);
            drawList->AddText(
                ImVec2(iconBoxMin.x + (iconBoxSize - headerIconSize.x) * 0.5f,
                       iconBoxMin.y + (iconBoxSize - headerIconSize.y) * 0.5f),
                ColorU32(EColor::AccentHover), icon);
            titleX = iconBoxMax.x + 12.0f;
        }

        if (titleFont != nullptr)
        {
            ImGui::PushFont(titleFont);
        }
        const float titleY = subtitle != nullptr && subtitle[0] != '\0'
            ? dialogPos.y + 9.0f
            : dialogPos.y + (kHeaderHeight - ImGui::GetTextLineHeight()) * 0.5f;
        drawList->AddText(ImVec2(titleX, titleY), ColorU32(EColor::Text), title ? title : "");
        if (titleFont != nullptr)
        {
            ImGui::PopFont();
        }

        if (subtitle != nullptr && subtitle[0] != '\0')
        {
            drawList->AddText(ImVec2(titleX, dialogPos.y + 32.0f), ColorU32(EColor::TextDim), subtitle);
        }

        bool closeClicked = false;
        if (allowClose)
        {
            constexpr float closeBtnSize = 28.0f;
            const ImVec2 closeBtnPos(dialogPos.x + dialogSize.x - closeBtnSize - 16.0f,
                                     dialogPos.y + (kHeaderHeight - closeBtnSize) * 0.5f);
            ImGui::SetCursorScreenPos(closeBtnPos);
            FScopedStyle closeStyle;
            closeStyle.Add(ImGuiCol_Button, ImVec4(0, 0, 0, 0))
                      .Add(ImGuiCol_ButtonHovered, Color(EColor::SurfaceHover, 0.85f))
                      .Add(ImGuiCol_ButtonActive, Color(EColor::SurfaceElevated))
                      .Add(ImGuiCol_Text, Color(EColor::TextMuted))
                      .Add(ImGuiStyleVar_FrameRounding, 5.0f)
                      .Add(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 4.0f));
            if (ImGui::Button(ICON_FA_XMARK "##CloseModal", ImVec2(closeBtnSize, closeBtnSize)))
            {
                closeClicked = true;
            }
        }

        ImGui::SetCursorScreenPos(ImVec2(dialogPos.x, dialogPos.y + kHeaderHeight));
        return closeClicked;
    }

    FInteractiveCardState DrawInteractiveCard(const FInteractiveCardConfig& config)
    {
        FInteractiveCardState state;
        state.Min = ImGui::GetCursorScreenPos();
        const float width = config.Size.x > 0.0f ? config.Size.x : ImGui::GetContentRegionAvail().x;
        const float height = config.Size.y > 0.0f ? config.Size.y : 54.0f;
        state.Max = ImVec2(state.Min.x + width, state.Min.y + height);

        ImGui::InvisibleButton(config.Id ? config.Id : "##InteractiveCard", ImVec2(width, height));
        state.Hovered = ImGui::IsItemHovered();
        state.Clicked = ImGui::IsItemClicked();

        const ImU32 cardBg = config.Selected ? ColorU32(EColor::SurfaceHover, 0.92f)
                                             : (state.Hovered ? ColorU32(EColor::SurfaceElevated, 0.70f)
                                                              : ColorU32(EColor::Surface, 0.35f));
        const ImU32 cardBorder = config.Selected ? ColorU32(EColor::AccentHover, 0.85f)
                                                 : (state.Hovered ? ColorU32(EColor::BorderStrong, 0.60f)
                                                                  : ColorU32(EColor::Border, 0.30f));

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(state.Min, state.Max, cardBg, config.Rounding);
        drawList->AddRect(state.Min, state.Max, cardBorder, config.Rounding, 0, config.Selected ? 1.5f : 1.0f);

        if (config.Selected && config.ShowSelectionStrip)
        {
            drawList->AddRectFilled(ImVec2(state.Min.x + 2.0f, state.Min.y + 8.0f),
                                    ImVec2(state.Min.x + 5.0f, state.Max.y - 8.0f),
                                    ColorU32(EColor::AccentHover), 2.0f);
        }
        return state;
    }

    FScopedOverlayPanel::FScopedOverlayPanel(const FOverlayPanelConfig& config)
    {
        ImGui::SetNextWindowPos(config.Position, ImGuiCond_Always);
        ImGui::SetNextWindowSize(config.Size, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(config.BackgroundAlpha);
        style_.Add(ImGuiStyleVar_WindowPadding, config.Padding)
            .Add(ImGuiStyleVar_ItemSpacing, config.ItemSpacing)
            .Add(ImGuiStyleVar_WindowRounding, config.Rounding)
            .Add(ImGuiStyleVar_WindowBorderSize, config.BorderSize)
            .Add(ImGuiCol_WindowBg, Color(config.BackgroundColor, config.BackgroundAlpha))
            .Add(ImGuiCol_Border, Color(EColor::BorderStrong, config.BorderAlpha));

        const ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | config.ExtraFlags;
        window_.emplace(config.WindowId, nullptr, flags);
    }

    FScopedInsetPanel::FScopedInsetPanel(const char* id,
                                         const ImVec2 size,
                                         const bool border,
                                         const ImGuiWindowFlags flags,
                                         const ImVec2 padding,
                                         const float backgroundAlpha)
    {
        style_.Add(ImGuiCol_ChildBg, Color(EColor::Background, backgroundAlpha))
            .Add(ImGuiCol_Border, Color(EColor::Border, 0.82f))
            .Add(ImGuiStyleVar_WindowPadding, padding)
            .Add(ImGuiStyleVar_ChildBorderSize, border ? 1.0f : 0.0f);
        const ImGuiChildFlags childFlags = ImGuiChildFlags_AlwaysUseWindowPadding |
            (border ? ImGuiChildFlags_Borders : ImGuiChildFlags_None);
        child_.emplace(id, size, childFlags, flags);
    }

    FScopedCard::FScopedCard(const char* id, const float height, const ImGuiWindowFlags flags)
    {
        BeginCard(id, height, flags);
    }

    FScopedCard::~FScopedCard()
    {
        EndCard();
    }

    FScopedSection::FScopedSection(const char* icon, const char* label, const bool defaultOpen)
        : open_(BeginSection(icon, label, defaultOpen))
    {
    }

    FScopedSection::~FScopedSection()
    {
        if (open_)
        {
            EndSection();
        }
    }

    FScopedToolbar::FScopedToolbar(const char* id,
                                   const ImVec2 size,
                                   const ImVec2 padding,
                                   const ImVec2 spacing)
    {
        style_.Add(ImGuiStyleVar_WindowPadding, padding)
            .Add(ImGuiStyleVar_ItemSpacing, spacing)
            .Add(ImGuiStyleVar_ChildRounding, 6.0f)
            .Add(ImGuiStyleVar_ChildBorderSize, 0.0f)
            .Add(ImGuiCol_ChildBg, Color(EColor::Surface, 0.92f));
        child_.emplace(id, size, ImGuiChildFlags_None,
                       ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    }

    namespace
    {
        void DrawAboutRow(const char* label, const std::string& value)
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextDisabled("%s", label);
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(value.c_str());
        }

        void DrawCopyableAboutRow(const char* label, const std::string& value)
        {
            DrawAboutRow(label, value);
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Click to copy");
            }
            if (ImGui::IsItemClicked())
            {
                ImGui::SetClipboardText(value.c_str());
            }
        }

        std::string DescribeRenderer()
        {
            NextEngine* engine = NextEngine::GetInstance();
            if (engine == nullptr)
            {
                return "unavailable";
            }
            return Vulkan::GetRendererName(engine->GetRenderer().CurrentLogicRendererType());
        }

        std::string DescribeDevice(const bool driverOnly)
        {
            NextEngine* engine = NextEngine::GetInstance();
            if (engine == nullptr)
            {
                return "unavailable";
            }
            const VkPhysicalDeviceProperties properties = engine->GetRenderer().Device().DeviceProperties();
            if (!driverOnly)
            {
                return properties.deviceName;
            }
            return fmt::format("{}.{}.{} (Vulkan {}.{}.{})",
                               VK_VERSION_MAJOR(properties.driverVersion), VK_VERSION_MINOR(properties.driverVersion),
                               VK_VERSION_PATCH(properties.driverVersion),
                               VK_VERSION_MAJOR(properties.apiVersion), VK_VERSION_MINOR(properties.apiVersion),
                               VK_VERSION_PATCH(properties.apiVersion));
        }
    }

    void ShowAboutDialog(bool& open)
    {
        if (!open)
        {
            return;
        }

        ImGui::OpenPopup("About");
        if (!BeginAnchoredModal("About", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            return;
        }

        TextCentered(NextRenderer::GetApplicationIdentity());
        TextCentered("A cross-platform Vulkan renderer and engine");
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0.0f, 4.0f));

        if (ImGui::BeginTable("##AboutInfo", 2, ImGuiTableFlags_SizingFixedFit))
        {
            ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthFixed, 120.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

            DrawAboutRow("Version", NextRenderer::GetBuildVersion());
            DrawAboutRow("Built", fmt::format("{} {}", __DATE__, __TIME__));
            DrawAboutRow("Renderer", DescribeRenderer());
            DrawAboutRow("GPU", DescribeDevice(false));
            DrawAboutRow("Driver", DescribeDevice(true));

            const std::string& logPath = Utilities::Logging::GetLogFilePath();
            DrawCopyableAboutRow("Log file", logPath.empty() ? std::string("not available") : logPath);
            ImGui::EndTable();
        }

        ImGui::Dummy(ImVec2(0.0f, 6.0f));
        ImGui::Separator();
        ImGui::TextDisabled("MIT License. See LICENSE and THIRD-PARTY-NOTICES.md.");
        ImGui::Dummy(ImVec2(0.0f, 4.0f));

        if (ImGui::Button("Project Page"))
        {
            SDL_OpenURL(ProjectHomeUrl);
        }
        ImGui::SameLine();
        if (ImGui::Button("Documentation"))
        {
            SDL_OpenURL(ProjectDocsUrl);
        }
        ImGui::SameLine();
        if (ImGui::Button("Report an Issue"))
        {
            SDL_OpenURL(ProjectIssuesUrl);
        }
        ImGui::SameLine();
        if (ImGui::Button("Close"))
        {
            ImGui::CloseCurrentPopup();
            open = false;
        }

        EndAnchoredModal();
    }
}
