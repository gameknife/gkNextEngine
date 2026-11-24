#pragma once

#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Runtime/Config/ShowFlags.hpp"
#include "Engine/Runtime/Config/UserSettings.hpp"
#include "Modules/NextUI/Theme.hpp"

#include <cfloat>
#include <glm/vec3.hpp>
#include <imgui.h>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace NextUI
{
    enum class EButtonVariant : uint8_t
    {
        Primary,
        Secondary,
        Ghost,
        Toolbar,
        Danger,
    };

    enum class EButtonTone : uint8_t
    {
        Default,
        Accent,
        Success,
        Warning,
    };

    struct FButtonOptions
    {
        EButtonVariant variant = EButtonVariant::Secondary;
        EButtonTone tone = EButtonTone::Default;
        ImVec2 size{};
        const char* tooltip = nullptr;
        bool active = false;
        bool disabled = false;
    };

    struct FPillBadgeOptions
    {
        const char* icon = nullptr;
        ImVec4 backgroundColor = ImVec4(0.148f, 0.148f, 0.148f, 0.85f);
        ImVec4 textColor = ImVec4(0.90f, 0.90f, 0.90f, 1.0f);
        ImVec4 borderColor = ImVec4(0.26f, 0.26f, 0.26f, 0.60f);
        ImVec2 padding = ImVec2(8.0f, 3.5f);
        float rounding = -1.0f; // < 0 uses capsule rounding (height * 0.5f)
        const char* tooltip = nullptr;
    };

    struct FPillOption
    {
        int Value = 0;
        const char* Label = "";
    };

    struct FPresetPill
    {
        const char* Label = "";
        bool Selected = false;
    };

    void Tooltip(const char* text);
    void ShowHoveredTooltip(const char* text);

    bool Button(const char* label, const FButtonOptions& options = {});
    bool GhostButton(const char* label, const char* tooltip = nullptr, bool active = false, ImVec2 size = ImVec2(0.0f, 0.0f));
    inline bool GhostButton(const char* label, const char* tooltip, ImVec2 size)
    {
        return GhostButton(label, tooltip, false, size);
    }
    bool ToolbarButton(const char* label, const char* tooltip, bool active = false, ImVec2 size = ImVec2(34.0f, 30.0f));
    bool IconButton(const char* icon, const char* tooltip, bool active = false,
                    ImVec2 size = ImVec2(), bool activeUnderline = false);
    bool ModeRailButton(const char* icon, const char* tooltip, bool active, float buttonSize);
    bool ComboOption(const char* label, bool selected);

    void DrawPillBadge(const char* label, const FPillBadgeOptions& options = {});
    void DrawPillBadge(const char* icon, const char* label, ImVec4 bgCol, ImVec4 textCol, ImVec4 borderCol,
                       float rounding = -1.0f, ImVec2 padding = ImVec2(8.0f, 3.5f));
    void DrawPillBadgeU32(const char* icon, const char* label, ImU32 textCol, ImU32 bgCol, ImU32 borderCol,
                          float rounding = -1.0f, ImVec2 padding = ImVec2(8.0f, 3.5f));
    void DrawCodeBadge(const char* code);
    void TagChip(const char* label, const ImVec4& color, const char* tooltip = nullptr);
    void DrawSelectionAccentStrip(ImVec2 itemMin, ImVec2 itemMax, float verticalPadding = 1.0f,
                                  float horizontalOffset = 0.0f);

    void CompactStat(const char* label, std::string_view value);
    void SameLineRightAligned(float groupWidth, float minLeadingGap = 30.0f);
    std::string TruncateWithEllipsis(std::string_view text, float maxWidth);
    void TextCentered(std::string_view text);
    ImVec2 TextCentered(std::string_view text, float fixedWidth);

    bool SegmentedPillsIndexed(const char* id, std::span<const char* const> labels, int& selectedIndex,
                               float totalWidth = -1.0f, float itemHeight = 22.0f);
    bool SegmentedPills(const char* id, std::span<const FPillOption> options, int& value,
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
            (void)val;
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

    void LabeledRow(const char* label,
                    float ratio = 0.40f,
                    float minLabelWidth = 96.0f,
                    float maxLabelWidth = 140.0f);

    template <typename DrawControl>
    bool DrawSettingRow(const char* label, DrawControl&& drawControl)
    {
        ImGui::PushID(label);
        LabeledRow(label);
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

    bool DrawSettingSliderRow(const char* label, ImGuiDataType dataType, void* value,
                              const void* minValue, const void* maxValue, const char* format,
                              float dragSpeed, float valueWidth = 84.0f);
    bool DrawFloatSettingRow(const char* label, float* value, float minValue, float maxValue,
                             const char* format = "%.2f", float dragSpeed = 0.01f, float valueWidth = 84.0f);
    bool DrawIntSettingRow(const char* label, int* value, int minValue, int maxValue,
                           const char* format = "%d", float valueWidth = 84.0f);
    bool DrawCheckboxSettingRow(const char* label, bool* value);

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

    void DrawThinSeparator(float alpha = 1.0f);
    void DrawVerticalSeparator(float height = 18.0f, float spacing = 12.0f, float alpha = 0.9f);
    void DrawProgressBar(float fraction, ImVec4 color, ImVec2 size);
    void DrawSmoothSpinner(ImDrawList* drawList, ImVec2 center, float radius, float thickness, ImU32 color);
    void DrawAnimatedProgressBar(ImDrawList* drawList, ImVec2 barMin, ImVec2 barMax, ImU32 fillCol);
    void Sparkline(const float* values, int count, ImVec2 size, ImVec4 color,
                   float scaleMin = FLT_MAX, float scaleMax = FLT_MAX,
                   bool baselineAtZero = false);

    void DrawShowFlagItem(const char* name, bool& flag);
    void DrawWireframeOptions(Runtime::Config::UserSettings& settings, bool enabled);
    void DrawShowFlagsCommon(Runtime::Config::ShowFlags& showFlags,
                             Runtime::Config::UserSettings* settings = nullptr);

    void PushViewportToolbarStyle();
    void PopViewportToolbarStyle();
    void PushViewportPopupStyle();
    void PopViewportPopupStyle();
    void PushToolWindowStyle();
    void PopToolWindowStyle();
    void PushToolWindowContentStyle();
    void PopToolWindowContentStyle();
}
