#pragma once

#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Runtime/RuntimeFwd.hpp"
#include "Modules/NextUI/Scopes.hpp"
#include "Modules/NextUI/Theme.hpp"

#include <functional>
#include <imgui.h>
#include <optional>

namespace NextUI
{
    enum class EAppChromeAction : uint8_t
    {
        None,
        Minimize,
        ToggleMaximize,
        Close,
    };

    struct FAppChromeResult
    {
        EAppChromeAction action = EAppChromeAction::None;
        float dragHeight = 0.0f;
        float dragLeftReservedWidth = 0.0f;
        float dragRightReservedWidth = 0.0f;
    };

    struct FAppTitleBarConfig
    {
        const char* BrandWindowId = "AppTitleBarBrand";
        const char* MenuWindowId = "AppTitleBarMenu";
        const char* RightWindowId = "AppTitleBarRight";
        const char* AppName = "";
        float Height = 48.0f;
        float RightContentWidth = 0.0f;
        float BrandHorizontalPadding = 14.0f;
        float BrandIconSize = 48.0f;
        float BrandTextSpacing = 10.0f;
        float MenuHitPadding = 28.0f;
        float MenuTrailingPadding = 8.0f;
        ImFont* TitleFont = nullptr;
        bool IsMaximized = false;
        bool AllowWindowCommands = false;
        std::function<void(ImDrawList*, ImVec2, float)> DrawBrandIcon;
        std::function<float()> DrawMenuBar;
        std::function<void()> DrawRightContent;
        std::function<void()> OnMinimize;
        std::function<void()> OnToggleMaximize;
        std::function<void()> OnClose;
    };

    struct FBottomBarConfig
    {
        const char* WindowId = "AppBottomBar";
        float Height = 30.0f;
        float HorizontalPadding = 10.0f;
        float VerticalPadding = 4.0f;
        float BackgroundAlpha = 0.76f;
        float CenterWidth = 0.0f;
        float RightWidth = 0.0f;
        std::function<void()> DrawLeftContent;
        std::function<void()> DrawCenterContent;
        std::function<void()> DrawRightContent;
    };

    struct FOverlayPanelConfig
    {
        const char* WindowId = "OverlayPanel";
        ImVec2 Position = ImVec2(0.0f, 0.0f);
        ImVec2 Size = ImVec2(0.0f, 0.0f);
        ImVec2 Padding = ImVec2(10.0f, 5.0f);
        ImVec2 ItemSpacing = ImVec2(6.0f, 0.0f);
        float Rounding = 8.0f;
        float BorderSize = 0.0f;
        float BorderAlpha = 0.74f;
        float BackgroundAlpha = 0.82f;
        EColor BackgroundColor = EColor::Background;
        ImGuiWindowFlags ExtraFlags = 0;
    };

    struct FDetailPanelConfig
    {
        const char* WindowId = "DetailPanel";
        const char* ContentWindowId = "##DetailPanelContent";
        const char* Icon = nullptr;
        const char* Title = nullptr;
        bool* Open = nullptr;
        ImVec2 Position = ImVec2(0.0f, 0.0f);
        ImVec2 Size = ImVec2(0.0f, 0.0f);
        ImVec2 Pivot = ImVec2(0.0f, 0.0f);
        bool DetachedViewport = false;
        ImVec2 ContentPadding = ImVec2(10.0f, 10.0f);
        bool ContentBorder = true;
        float ContentBackgroundAlpha = 0.30f;
        ImGuiWindowFlags ContentFlags = 0;
    };

    struct FCollapsibleSidePanelConfig
    {
        const char* WindowId = "##SidePanel";
        ImVec2 Position = ImVec2(0.0f, 0.0f);
        ImVec2 Size = ImVec2(0.0f, 0.0f);
        bool Collapsed = false;
        float BackgroundAlpha = 0.96f;
        ImVec2 CollapsedPadding = ImVec2(7.0f, 8.0f);
        ImVec2 ExpandedPadding = ImVec2(12.0f, 12.0f);
        ImGuiWindowFlags ExtraFlags = 0;
    };

    struct FModalConfig
    {
        const char* PopupId = "##Modal";
        ImVec2 TargetSize = ImVec2(0.0f, 0.0f);
        ImGuiCond SizeCond = ImGuiCond_Appearing;
        ImGuiCond PosCond = ImGuiCond_Appearing;
        ImVec2 Padding = ImVec2(0.0f, 0.0f);
        bool OverrideStyle = true;
        bool CenterInAnchorViewport = true;
        bool ClampToAnchorViewport = true;
        float Rounding = 8.0f;
        float ViewportMargin = 24.0f;
        float BackdropDimAlpha = 0.0f;
        bool AutoOpen = false;
        bool* Open = nullptr;
        ImGuiWindowFlags ExtraFlags = 0;
    };

    struct FInteractiveCardConfig
    {
        const char* Id = "##InteractiveCard";
        ImVec2 Size = ImVec2(0.0f, 54.0f);
        bool Selected = false;
        float Rounding = 6.0f;
        bool ShowSelectionStrip = true;
    };

    struct FInteractiveCardState
    {
        bool Clicked = false;
        bool Hovered = false;
        ImVec2 Min{};
        ImVec2 Max{};
    };

    class FMainMenuGhostScope final
    {
    public:
        FMainMenuGhostScope();
        ~FMainMenuGhostScope();

        GK_NON_COPIABLE(FMainMenuGhostScope)

        bool BeginMenu(const char* label, bool enabled = true);

    private:
        ImDrawList* drawList_ = nullptr;
        bool channelsSplit_ = false;
    };

    void DrawBrandMark(ImDrawList* drawList, ImVec2 min, float size);
    FAppChromeResult DrawAppTitleBarChrome(const FAppTitleBarConfig& config);
    void DrawAppTitleBar(NextEngine& engine, const FAppTitleBarConfig& config);
    void DrawBottomBar(const FBottomBarConfig& config);

    bool BeginOverlayPanel(const FOverlayPanelConfig& config);
    void EndOverlayPanel();

    bool BeginInsetPanel(const char* id,
                         ImVec2 size = {},
                         bool border = false,
                         ImGuiWindowFlags flags = 0,
                         ImVec2 padding = ImVec2(10.0f, 10.0f),
                         float backgroundAlpha = 0.30f);
    void EndInsetPanel();

    void BeginCard(const char* id, float height = 0.0f, ImGuiWindowFlags flags = 0, float horizontalInset = 1.0f);
    void EndCard();

    bool BeginSection(const char* icon, const char* label, bool defaultOpen = true);
    void EndSection();

    bool BeginPanelSection(const char* label, bool defaultOpen = true);
    void EndPanelSection();

    bool BeginFloatingPanel(const char* id, const char* icon, const char* title, bool* pOpen,
                            ImVec2 position, ImVec2 size, ImVec2 pivot = ImVec2(0.0f, 0.0f),
                            bool detachedViewport = false);
    void EndFloatingPanel();

    bool BeginDetailPanel(const FDetailPanelConfig& config);
    void EndDetailPanel();

    bool BeginCollapsibleSidePanel(const FCollapsibleSidePanelConfig& config);
    void EndCollapsibleSidePanel();

    void DrawPanelHeader(const char* icon, const char* title, const char* subtitle = nullptr,
                         const char* badgeText = nullptr, ImVec4 badgeColor = ImVec4(0.0f, 0.0f, 0.0f, 0.0f));

    bool BeginModal(const FModalConfig& config);
    void EndModal();
    bool BeginAnchoredModal(const char* name,
                            bool* pOpen = nullptr,
                            ImGuiWindowFlags flags = 0,
                            ImVec2 requestedSize = ImVec2(0.0f, 0.0f),
                            float viewportMargin = 16.0f);
    void EndAnchoredModal();
    bool DrawModalHeader(const char* icon, const char* title, const char* subtitle = nullptr,
                         ImFont* titleFont = nullptr, bool allowClose = true);

    FInteractiveCardState DrawInteractiveCard(const FInteractiveCardConfig& config);

    // Zero-heap-allocation RAII container scopes (backed by std::optional instead of std::unique_ptr).
    class FScopedOverlayPanel final
    {
    public:
        explicit FScopedOverlayPanel(const FOverlayPanelConfig& config);
        ~FScopedOverlayPanel() = default;
        GK_NON_COPIABLE(FScopedOverlayPanel)
        explicit operator bool() const { return window_.has_value() && static_cast<bool>(*window_); }

    private:
        FScopedStyle style_;
        std::optional<FScopedWindow> window_;
    };

    class FScopedInsetPanel final
    {
    public:
        FScopedInsetPanel(const char* id,
                          ImVec2 size = {},
                          bool border = true,
                          ImGuiWindowFlags flags = 0,
                          ImVec2 padding = ImVec2(10.0f, 10.0f),
                          float backgroundAlpha = 0.30f);
        ~FScopedInsetPanel() = default;
        GK_NON_COPIABLE(FScopedInsetPanel)
        explicit operator bool() const { return child_.has_value() && static_cast<bool>(*child_); }

    private:
        FScopedStyle style_;
        std::optional<FScopedChild> child_;
    };

    class FScopedCard final
    {
    public:
        explicit FScopedCard(const char* id, float height = 0.0f, ImGuiWindowFlags flags = 0);
        ~FScopedCard();
        GK_NON_COPIABLE(FScopedCard)
    };

    class FScopedSection final
    {
    public:
        FScopedSection(const char* icon, const char* label, bool defaultOpen = true);
        ~FScopedSection();
        GK_NON_COPIABLE(FScopedSection)
        explicit operator bool() const { return open_; }

    private:
        bool open_ = false;
    };

    class FScopedToolbar final
    {
    public:
        FScopedToolbar(const char* id,
                       ImVec2 size = {},
                       ImVec2 padding = ImVec2(4.0f, 4.0f),
                       ImVec2 spacing = ImVec2(4.0f, 0.0f));
        ~FScopedToolbar() = default;
        GK_NON_COPIABLE(FScopedToolbar)
        explicit operator bool() const { return child_.has_value() && static_cast<bool>(*child_); }

    private:
        FScopedStyle style_;
        std::optional<FScopedChild> child_;
    };

    // Shared About dialog & project links (migrated out of Engine/Utilities).
    inline constexpr const char* ProjectHomeUrl = "https://github.com/gameknife/gkNextRenderer";
    inline constexpr const char* ProjectIssuesUrl = "https://github.com/gameknife/gkNextRenderer/issues";
    inline constexpr const char* ProjectDocsUrl = "https://github.com/gameknife/gkNextRenderer/tree/main/docs";
    void ShowAboutDialog(bool& open);
}
