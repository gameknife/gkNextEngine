#pragma once

#include "Engine/Common/CoreMinimal.hpp"
#include "Modules/NextUI/UI/AppChrome.hpp"
#include "Modules/NextUI/UI/UiContainers.hpp"
#include "Modules/NextUI/UI/UiTheme.hpp"
#include "Modules/NextUI/UI/UiWidgets.hpp"
#include "Engine/Runtime/RuntimeFwd.hpp"

#include <functional>
#include <glm/vec3.hpp>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <imgui.h>

namespace NextUI::Theme
{
    using EColor = Foundation::EColor;
    using Foundation::Color;
    using Foundation::ColorU32;
    using Foundation::DrawBrandMark;
    using Foundation::FPillBadgeOptions;
    using Foundation::DrawPillBadge;
    using Foundation::DrawPillBadgeU32;
    using Foundation::DrawCodeBadge;
    using Foundation::CompactStat;
    using Foundation::DrawSmoothSpinner;
    using Foundation::DrawAnimatedProgressBar;
    using Foundation::BeginInsetPanel;
    using Foundation::EndInsetPanel;
    using Foundation::BeginCard;
    using Foundation::EndCard;
    using Foundation::BeginSection;
    using Foundation::EndSection;
    using Foundation::FScopedCard;
    using Foundation::FScopedInsetPanel;
    using Foundation::FScopedOverlayPanel;
    using Foundation::FScopedSection;

    ImFont* GetDefaultFont(NextEngine& engine);
    ImFont* GetTitleFont(NextEngine& engine);

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
        // Overlay surfaces are flat by default. Set this explicitly for panels
        // that need an outlined treatment.
        float BorderSize = 0.0f;
        float BorderAlpha = 0.74f;
        float BackgroundAlpha = 0.82f;
        EColor BackgroundColor = EColor::Background;
        ImGuiWindowFlags ExtraFlags = 0;
    };

    // The standard detail surface used by renderer settings, diagnostics, and
    // application explorers. Its weak translucent black shell intentionally
    // matches viewport toolbars; it owns the inset scrollable content surface
    // so detail windows cannot drift in padding or border treatment.
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

    struct FModernModalConfig
    {
        const char* PopupId = "##ModernModal";
        ImVec2 TargetSize = ImVec2(640.0f, 440.0f);
        ImVec2 Padding = ImVec2(0.0f, 0.0f);
        float Rounding = 8.0f;
        float ViewportMargin = 32.0f;
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

    void ApplyProfessionalTheme();

    // Styles the top-level entries of an app menu bar as ghost buttons. The
    // scope only affects menus opened through BeginMenu(), not popup entries.
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

    void DrawAppTitleBar(NextEngine& engine, const FAppTitleBarConfig& config);
    void DrawBottomBar(const FBottomBarConfig& config);
    void DrawTooltip(const char* text);
    // A non-positive size component is fitted to the active font and theme padding.
    bool IconButton(const char* label, const char* tooltip, bool active = false, ImVec2 size = ImVec2(0.0f, 0.0f));
    bool GhostButton(const char* label, const char* tooltip = nullptr, ImVec2 size = ImVec2(0.0f, 0.0f));
    bool ToolbarButton(const char* label, const char* tooltip, bool active = false, ImVec2 size = ImVec2(34.0f, 30.0f));
    void PushViewportToolbarStyle();
    void PopViewportToolbarStyle();
    bool DrawFlatViewportButton(const char* label, const char* tooltip, bool active, ImVec2 size);
    void PushViewportPopupStyle();
    void PopViewportPopupStyle();
    bool DrawViewportComboOption(const char* label, bool selected);
    void PushToolWindowStyle();
    void PopToolWindowStyle();
    void PushToolWindowContentStyle();
    void PopToolWindowContentStyle();
    bool ModeRailButton(const char* icon, const char* tooltip, bool active, float buttonSize);
    void BeginFormRow(const char* label, float ratio = 0.40f, float minLabelWidth = 96.0f, float maxLabelWidth = 140.0f);
    bool BeginOverlayPanel(const FOverlayPanelConfig& config);
    void EndOverlayPanel();

    // Small filled label used to tag a row or a header with a category.
    void TagChip(const char* label, const ImVec4& color, const char* tooltip = nullptr);

    // Rounded accent strip on the left edge of a selected row / rail button. The single motif that
    // marks "this is the active one" across the mode rail, the Outliner and the settings nav.
    void DrawSelectionAccentStrip(ImVec2 itemMin, ImVec2 itemMax, float verticalPadding = 1.0f,
                                  float horizontalOffset = 0.0f);

    // Horizontal group of mutually exclusive pills, sharing the available width evenly.
    // Returns true when the selection changed.
    struct FPillOption
    {
        int Value = 0;
        const char* Label = "";
    };
    bool SegmentedPills(const char* id, std::span<const FPillOption> options, int& value,
                        float totalWidth = -1.0f, float itemHeight = 22.0f);

    bool SegmentedPillsIndexed(const char* id, std::span<const char* const> labels, int& selectedIndex,
                               float totalWidth = -1.0f, float itemHeight = 22.0f);

    template <typename T, typename LabelT>
    bool SegmentedPills(const char* id, const std::vector<std::pair<T, LabelT>>& options, T& value,
                        float totalWidth = -1.0f, float itemHeight = 22.0f)
    {
        if (options.empty())
        {
            return false;
        }
        std::vector<const char*> labels;
        labels.reserve(options.size());
        int selectedIndex = -1;
        for (size_t i = 0; i < options.size(); ++i)
        {
            if constexpr (std::is_same_v<std::decay_t<LabelT>, std::string>)
            {
                labels.push_back(options[i].second.c_str());
            }
            else
            {
                labels.push_back(options[i].second);
            }
            if (options[i].first == value)
            {
                selectedIndex = static_cast<int>(i);
            }
        }
        if (SegmentedPillsIndexed(id, labels, selectedIndex, totalWidth, itemHeight) &&
            selectedIndex >= 0 && static_cast<size_t>(selectedIndex) < options.size())
        {
            value = options[static_cast<size_t>(selectedIndex)].first;
            return true;
        }
        return false;
    }

    template <typename T, typename LabelT>
    bool SegmentedPills(const char* id, const std::vector<std::pair<T, LabelT>>& options, T* value,
                        float totalWidth = -1.0f, float itemHeight = 24.0f)
    {
        return value != nullptr && SegmentedPills(id, options, *value, totalWidth, itemHeight);
    }

    struct FPresetPill
    {
        const char* Label = "";
        bool Selected = false;
    };
    // Renders a row of equal-width rounded preset buttons. Returns the clicked index, or -1 if none.
    int PresetPills(const char* id, std::span<const FPresetPill> pills, float totalWidth = -1.0f,
                    float itemHeight = 0.0f);

    template <typename T>
    bool PresetPills(const char* id, std::span<const std::pair<T, const char*>> presets, T* outValue,
                     float itemHeight = 20.0f, float totalWidth = -1.0f)
    {
        if (presets.empty() || outValue == nullptr)
        {
            return false;
        }
        std::vector<FPresetPill> pills;
        pills.reserve(presets.size());
        for (const auto& [val, label] : presets)
        {
            pills.push_back(FPresetPill{label, false});
        }
        const int clicked = PresetPills(id, pills, totalWidth, itemHeight);
        if (clicked >= 0 && static_cast<size_t>(clicked) < presets.size())
        {
            *outValue = presets[static_cast<size_t>(clicked)].first;
            return true;
        }
        return false;
    }

    // DCC-style X/Y/Z 3-axis float drag with color-coded scrubbing tags, double-click per-axis reset,
    // and right-aligned full reset button.
    bool Draw3AxisFloatDrag(const char* label, float value[3], float speed = 0.1f,
                            float min = 0.0f, float max = 0.0f, float resetValue = 0.0f,
                            float labelWidth = 70.0f, const char* format = "%.3f");
    inline bool Draw3AxisFloatDrag(const char* label, float value[3], float speed,
                                   float min, float max, const char* format)
    {
        return Draw3AxisFloatDrag(label, value, speed, min, max, 0.0f, 70.0f, format);
    }
    inline bool Draw3AxisFloatDrag(const char* label, glm::vec3& value, float speed = 0.1f,
                                   float min = 0.0f, float max = 0.0f, float resetValue = 0.0f,
                                   float labelWidth = 70.0f, const char* format = "%.3f")
    {
        return Draw3AxisFloatDrag(label, &value.x, speed, min, max, resetValue, labelWidth, format);
    }

    // Standard form rows (left muted label + right control).
    bool DrawSettingSliderRow(const char* label, ImGuiDataType dataType, void* value,
                              const void* minValue, const void* maxValue, const char* format,
                              float dragSpeed, float valueWidth = 84.0f);
    bool DrawFloatSettingRow(const char* label, float* value, float minValue, float maxValue,
                             const char* format = "%.2f", float dragSpeed = 0.01f, float valueWidth = 84.0f);
    bool DrawIntSettingRow(const char* label, int* value, int minValue, int maxValue,
                           const char* format = "%d", float valueWidth = 84.0f);
    bool DrawCheckboxSettingRow(const char* label, bool* value);

    template <typename DrawControl>
    bool DrawSettingRow(const char* label, DrawControl&& drawControl)
    {
        ImGui::PushID(label);
        BeginFormRow(label);
        const bool changed = drawControl();
        ImGui::PopID();
        return changed;
    }

    template <typename DrawComboBody>
    bool DrawComboSettingRow(const char* label, const char* preview, DrawComboBody&& drawComboBody)
    {
        bool changed = false;
        DrawSettingRow(label,
                       [&]()
                       {
                           ImGui::SetNextItemWidth(-FLT_MIN);
                           if (ImGui::BeginCombo("##Value", preview))
                           {
                               changed = drawComboBody();
                               ImGui::EndCombo();
                           }
                           return changed;
                       });
        return changed;
    }

    // Places the next item on the same line, right-aligned to the window content edge.
    // Falls back to a plain SameLine() when the row is too narrow to hold the group.
    void SameLineRightAligned(float groupWidth, float minLeadingGap = 30.0f);

    // Shortens text to fit maxWidth, appending an ellipsis. Single-pass, unlike a per-character probe.
    std::string TruncateWithEllipsis(const std::string& text, float maxWidth);

    // Floating panel matching the new design language: rounded surface, single-line title with optional close X.
    // pOpen may be null. Returns true when the panel body is visible (matches ImGui::Begin semantics).
    bool BeginFloatingPanel(const char* id, const char* icon, const char* title, bool* pOpen,
                             ImVec2 position, ImVec2 size, ImVec2 pivot = ImVec2(0.0f, 0.0f),
                             bool detachedViewport = false);
    void EndFloatingPanel();

    bool BeginDetailPanel(const FDetailPanelConfig& config);
    void EndDetailPanel();

    bool BeginCollapsibleSidePanel(const FCollapsibleSidePanelConfig& config);
    void EndCollapsibleSidePanel();

    bool BeginModernModal(const FModernModalConfig& config);
    void EndModernModal();
    // Renders the 56px flat header bar at the top of a borderless modal. Returns true if the close X button was pressed.
    bool DrawModalHeader(const char* icon, const char* title, const char* subtitle = nullptr,
                         ImFont* titleFont = nullptr, bool allowClose = true);

    FInteractiveCardState DrawInteractiveCard(const FInteractiveCardConfig& config);

    // Collapsible section inside a floating panel: chevron + title row, no border background.
    bool BeginPanelSection(const char* label, bool defaultOpen = true);
    void EndPanelSection();

    // Inline sparkline. width<=0 fills available width.
    // When scaleMin/scaleMax are left at FLT_MAX the range auto-fits the dataset.
    // Set baselineAtZero=true (in auto-fit mode) to pin scaleMin=0 so timing-style
    // data (frame time, pass ms) doesn't visually amplify small noise — the line
    // wiggles far less than the default min..max fit.
    void Sparkline(const float* values, int count, ImVec2 size, ImVec4 color,
                   float scaleMin = FLT_MAX, float scaleMax = FLT_MAX,
                   bool baselineAtZero = false);

    void DrawPanelHeader(const char* icon, const char* title, const char* subtitle = nullptr,
                         const char* badgeText = nullptr, ImVec4 badgeColor = ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    void DrawThinSeparator(float alpha = 1.0f);
    void DrawVerticalSeparator(float height = 18.0f, float spacing = 12.0f, float alpha = 0.9f);
    void DrawProgressBar(float fraction, ImVec4 color, ImVec2 size);
} // namespace NextUI::Theme
