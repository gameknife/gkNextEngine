#include "Engine/Common/CoreMinimal.hpp"

#include "Modules/NextUI/Widgets.hpp"
#include "Modules/NextUI/Scopes.hpp"
#include "ThirdParty/fontawesome/IconsFontAwesome6.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fmt/format.h>
#include <imgui_internal.h>

namespace NextUI
{
    void Tooltip(const char* text)
    {
        if (text == nullptr || text[0] == '\0' || !ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        {
            return;
        }
        ShowHoveredTooltip(text);
    }

    void ShowHoveredTooltip(const char* text)
    {
        if (text == nullptr || text[0] == '\0')
        {
            return;
        }
        ImGui::SetNextWindowSizeConstraints(ImVec2(), ImVec2(420.0f, FLT_MAX));
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }

    bool Button(const char* label, const FButtonOptions& options)
    {
        FScopedStyle style;
        const bool transparent =
            options.variant == EButtonVariant::Ghost || options.variant == EButtonVariant::Toolbar;
        const EColor activeColor = options.variant == EButtonVariant::Danger ? EColor::Danger
            : options.tone == EButtonTone::Success ? EColor::Success
            : options.tone == EButtonTone::Warning ? EColor::Warning
            : EColor::Accent;
        style.Add(ImGuiStyleVar_FrameRounding, options.variant == EButtonVariant::Toolbar ? 4.0f : 5.0f)
             .Add(ImGuiStyleVar_FrameBorderSize, options.variant == EButtonVariant::Ghost ? 0.0f
                                                                                          : ImGui::GetStyle().FrameBorderSize)
             .Add(ImGuiCol_Button, options.active ? Color(activeColor, 0.72f)
                                                  : transparent ? ImVec4(0, 0, 0, 0)
                                                                : Color(EColor::SurfaceElevated, 0.94f))
             .Add(ImGuiCol_ButtonHovered, Color(options.variant == EButtonVariant::Danger ? EColor::Danger
                                                                                          : EColor::SurfaceHover))
             .Add(ImGuiCol_ButtonActive, Color(activeColor, 0.86f));
        FScopedDisabled disabled(options.disabled);
        const bool pressed = ImGui::Button(label != nullptr ? label : "", options.size);
        Tooltip(options.tooltip);
        return pressed;
    }

    bool GhostButton(const char* label, const char* tooltip, const bool active, const ImVec2 size)
    {
        return Button(label, FButtonOptions{
            .variant = EButtonVariant::Ghost,
            .size = size,
            .tooltip = tooltip,
            .active = active,
        });
    }

    bool ToolbarButton(const char* label, const char* tooltip, const bool active, const ImVec2 size)
    {
        return Button(label, FButtonOptions{
            .variant = EButtonVariant::Toolbar,
            .size = size,
            .tooltip = tooltip,
            .active = active,
        });
    }

    bool IconButton(const char* icon, const char* tooltip, const bool active, ImVec2 size,
                    const bool activeUnderline)
    {
        if (size.x <= 0.0f) size.x = ImGui::GetFrameHeight();
        if (size.y <= 0.0f) size.y = ImGui::GetFrameHeight();

        const char* label = icon != nullptr ? icon : "";
        const char* visibleEnd = strstr(label, "##");
        if (visibleEnd == nullptr)
        {
            visibleEnd = label + strlen(label);
        }

        const ImVec4 textColor = ImGui::GetStyleColorVec4(ImGuiCol_Text);
        bool pressed = false;
        {
            FScopedStyle style;
            const bool showActiveFill = active && !activeUnderline;
            style.Add(ImGuiStyleVar_FrameRounding, 4.0f)
                .Add(ImGuiStyleVar_FrameBorderSize, 0.0f)
                .Add(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.5f, 0.5f))
                .Add(ImGuiCol_Text, ImVec4(textColor.x, textColor.y, textColor.z, 0.0f))
                .Add(ImGuiCol_Border, ImVec4(0, 0, 0, 0))
                .Add(ImGuiCol_Button, showActiveFill ? Color(EColor::Accent, 0.72f) : ImVec4(0, 0, 0, 0))
                .Add(ImGuiCol_ButtonHovered, Color(EColor::SurfaceHover))
                .Add(ImGuiCol_ButtonActive, activeUnderline ? Color(EColor::SurfaceHover)
                                                             : Color(EColor::Accent, 0.86f));

            pressed = ImGui::Button(label, size);
            const ImVec2 itemMin = ImGui::GetItemRectMin();
            const ImVec2 itemMax = ImGui::GetItemRectMax();
            const ImVec2 itemCenter = (itemMin + itemMax) * 0.5f;
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            const ImU32 renderedTextColor = ImGui::GetColorU32(textColor);

            unsigned int codepoint = 0;
            const int codepointBytes = visibleEnd > label ? ImTextCharFromUtf8(&codepoint, label, visibleEnd) : 0;
            const bool singleGlyph = codepointBytes > 0 && label + codepointBytes == visibleEnd;
            const ImFontGlyph* glyph = singleGlyph
                ? ImGui::GetFontBaked()->FindGlyphNoFallback(static_cast<ImWchar>(codepoint))
                : nullptr;

            drawList->PushClipRect(itemMin, itemMax, true);
            if (glyph != nullptr && glyph->Visible)
            {
                const ImVec2 drawPosition(
                    itemCenter.x - (glyph->X0 + glyph->X1) * 0.5f,
                    itemCenter.y - (glyph->Y0 + glyph->Y1) * 0.5f);
                drawList->AddText(ImGui::GetFont(), ImGui::GetFontSize(), drawPosition,
                                  renderedTextColor, label, visibleEnd);
            }
            else if (visibleEnd > label)
            {
                const ImVec2 textSize = ImGui::CalcTextSize(label, visibleEnd, false);
                const ImVec2 drawPosition = itemCenter - textSize * 0.5f;
                drawList->AddText(ImGui::GetFont(), ImGui::GetFontSize(), drawPosition,
                                  renderedTextColor, label, visibleEnd);
            }
            if (active && activeUnderline)
            {
                drawList->AddRectFilled(
                    ImVec2(itemMin.x + 3.0f, itemMax.y - 2.0f),
                    ImVec2(itemMax.x - 3.0f, itemMax.y),
                    ColorU32(EColor::Accent, 0.95f), 1.0f);
            }
            drawList->PopClipRect();
        }

        Tooltip(tooltip);
        return pressed;
    }

    bool ModeRailButton(const char* icon, const char* tooltip, const bool active, const float buttonSize)
    {
        {
            FScopedStyle style;
            style.Add(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f))
                 .Add(ImGuiStyleVar_FrameRounding, 6.0f);
            if (active)
            {
                style.Add(ImGuiCol_Button, Color(EColor::Accent, 0.18f))
                     .Add(ImGuiCol_ButtonHovered, Color(EColor::Accent, 0.30f))
                     .Add(ImGuiCol_ButtonActive, Color(EColor::Accent, 0.45f))
                     .Add(ImGuiCol_Text, Color(EColor::AccentHover));
            }
            else
            {
                style.Add(ImGuiCol_Button, ImVec4(0, 0, 0, 0))
                     .Add(ImGuiCol_ButtonHovered, Color(EColor::SurfaceHover, 0.65f))
                     .Add(ImGuiCol_ButtonActive, Color(EColor::SurfaceHover, 0.85f))
                     .Add(ImGuiCol_Text, Color(EColor::TextMuted));
            }
            const bool pressed = ImGui::Button(icon ? icon : "?", ImVec2(buttonSize, buttonSize));
            if (active)
            {
                DrawSelectionAccentStrip(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), 6.0f, -4.0f);
            }
            Tooltip(tooltip);
            return pressed;
        }
    }

    bool ComboOption(const char* label, const bool selected)
    {
        FScopedStyle style;
        style.Add(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.0f, 0.5f));
        return ImGui::Selectable(label, selected, ImGuiSelectableFlags_None, ImVec2(0.0f, ImGui::GetFrameHeight()));
    }

    void DrawPillBadgeU32(const char* icon, const char* label, const ImU32 textCol, const ImU32 bgCol,
                          const ImU32 borderCol, const float rounding, const ImVec2 padding)
    {
        const std::string text =
            icon != nullptr && icon[0] != '\0' ? fmt::format("{} {}", icon, label ? label : "")
                                               : std::string(label ? label : "");
        const ImVec2 textSize = ImGui::CalcTextSize(text.c_str());
        const ImVec2 size(textSize.x + padding.x * 2.0f, textSize.y + padding.y * 2.0f);
        const float actualRounding = rounding < 0.0f ? size.y * 0.5f : rounding;

        const ImVec2 pos = ImGui::GetCursorScreenPos();
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), bgCol, actualRounding);
        if ((borderCol >> IM_COL32_A_SHIFT) > 0)
        {
            drawList->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), borderCol, actualRounding, 0, 1.0f);
        }
        drawList->AddText(ImVec2(pos.x + padding.x, pos.y + padding.y), textCol, text.c_str());

        ImGui::Dummy(size);
    }

    void DrawPillBadge(const char* icon, const char* label, const ImVec4 bgCol, const ImVec4 textCol,
                       const ImVec4 borderCol, const float rounding, const ImVec2 padding)
    {
        DrawPillBadgeU32(icon, label, ImGui::GetColorU32(textCol), ImGui::GetColorU32(bgCol),
                         ImGui::GetColorU32(borderCol), rounding, padding);
    }

    void DrawPillBadge(const char* label, const FPillBadgeOptions& options)
    {
        DrawPillBadge(options.icon, label, options.backgroundColor, options.textColor,
                      options.borderColor, options.rounding, options.padding);
        Tooltip(options.tooltip);
    }

    void DrawCodeBadge(const char* code)
    {
        DrawPillBadge(nullptr, code, Color(EColor::Background, 0.70f), Color(EColor::AccentHover),
                      Color(EColor::Border, 0.60f), 5.0f, ImVec2(8.0f, 4.0f));
    }

    void TagChip(const char* label, const ImVec4& color, const char* tooltip)
    {
        FScopedStyle style;
        style.Add(ImGuiStyleVar_FrameRounding, 4.0f)
             .Add(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 2.0f))
             .Add(ImGuiCol_Button, ImVec4(color.x, color.y, color.z, 0.18f))
             .Add(ImGuiCol_ButtonHovered, ImVec4(color.x, color.y, color.z, 0.28f))
             .Add(ImGuiCol_ButtonActive, ImVec4(color.x, color.y, color.z, 0.35f))
             .Add(ImGuiCol_Text, color);
        ImGui::SmallButton(label ? label : "");
        Tooltip(tooltip);
    }

    void DrawSelectionAccentStrip(const ImVec2 itemMin, const ImVec2 itemMax,
                                  const float verticalPadding, const float horizontalOffset)
    {
        constexpr float stripWidth = 3.0f;
        const float x = itemMin.x + horizontalOffset;
        ImGui::GetWindowDrawList()->AddRectFilled(
            ImVec2(x, itemMin.y + verticalPadding), ImVec2(x + stripWidth, itemMax.y - verticalPadding),
            ColorU32(EColor::AccentHover), stripWidth * 0.5f);
    }

    void CompactStat(const char* label, const std::string_view value)
    {
        ImGui::TextColored(Color(EColor::TextMuted), "%s", label ? label : "");
        const float valueWidth = ImGui::CalcTextSize(value.data(), value.data() + value.size()).x;
        SameLineRightAligned(valueWidth, 16.0f);
        ImGui::TextUnformatted(value.data(), value.data() + value.size());
    }

    void SameLineRightAligned(const float groupWidth, const float minLeadingGap)
    {
        const float available = ImGui::GetContentRegionAvail().x;
        if (available > groupWidth + minLeadingGap)
        {
            ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - groupWidth - ImGui::GetStyle().WindowPadding.x);
        }
        else
        {
            ImGui::SameLine();
        }
    }

    std::string TruncateWithEllipsis(const std::string_view text, const float maxWidth)
    {
        if (text.empty())
        {
            return {};
        }
        const char* begin = text.data();
        const char* end = begin + text.size();
        if (ImGui::CalcTextSize(begin, end).x <= maxWidth)
        {
            return std::string(text);
        }

        static constexpr const char* kEllipsis = "...";
        const float ellipsisWidth = ImGui::CalcTextSize(kEllipsis).x;
        if (ellipsisWidth >= maxWidth)
        {
            return kEllipsis;
        }

        const char* remaining = begin;
        ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize(), maxWidth - ellipsisWidth, 0.0f, begin, end, &remaining);
        return std::string(begin, remaining) + kEllipsis;
    }

    void TextCentered(const std::string_view text)
    {
        const char* begin = text.data();
        const char* end = begin + text.size();
        const float windowWidth = ImGui::GetContentRegionAvail().x;
        const float textWidth = ImGui::CalcTextSize(begin, end).x;
        ImGui::SetCursorPosX((ImGui::GetCursorPosX() + windowWidth - textWidth) * 0.5f);
        ImGui::TextUnformatted(begin, end);
    }

    ImVec2 TextCentered(const std::string_view text, const float fixedWidth)
    {
        const char* begin = text.data();
        const char* end = begin + text.size();
        const float textWidth = ImGui::CalcTextSize(begin, end).x;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (fixedWidth - textWidth) * 0.5f);
        const ImVec2 drawPos =
            ImGui::GetCursorScreenPos() + ImVec2(textWidth * 0.5f, ImGui::GetTextLineHeight() * 0.5f);
        ImGui::TextUnformatted(begin, end);
        return drawPos;
    }

    bool SegmentedPillsIndexed(const char* id, std::span<const char* const> labels, int& selectedIndex,
                               const float totalWidth, const float itemHeight)
    {
        if (labels.empty())
        {
            return false;
        }

        FScopedId scopeId(id);
        const float available = totalWidth > 0.0f ? totalWidth : ImGui::GetContentRegionAvail().x;
        constexpr float spacing = 3.0f;
        const float count = static_cast<float>(labels.size());
        const float itemWidth = std::max(20.0f, (available - spacing * (count - 1.0f)) / count);

        bool changed = false;
        for (size_t i = 0; i < labels.size(); ++i)
        {
            if (i > 0)
            {
                ImGui::SameLine(0.0f, spacing);
            }
            const bool selected = selectedIndex == static_cast<int>(i);
            FScopedStyle style;
            style.Add(ImGuiStyleVar_FrameRounding, 4.0f);
            if (selected)
            {
                style.Add(ImGuiCol_Button, Color(EColor::Accent, 0.85f))
                     .Add(ImGuiCol_ButtonHovered, Color(EColor::AccentHover))
                     .Add(ImGuiCol_ButtonActive, Color(EColor::Accent))
                     .Add(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
            }
            else
            {
                style.Add(ImGuiCol_Button, Color(EColor::SurfaceElevated, 0.45f))
                     .Add(ImGuiCol_ButtonHovered, Color(EColor::SurfaceHover, 0.75f))
                     .Add(ImGuiCol_ButtonActive, Color(EColor::SurfaceHover))
                     .Add(ImGuiCol_Text, Color(EColor::TextMuted));
            }

            const char* label = labels[i] != nullptr ? labels[i] : "";
            if (ImGui::Button(label, ImVec2(itemWidth, itemHeight)) && !selected)
            {
                selectedIndex = static_cast<int>(i);
                changed = true;
            }
        }
        return changed;
    }

    bool SegmentedPills(const char* id, std::span<const FPillOption> options, int& value,
                        const float totalWidth, const float itemHeight)
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
            labels.push_back(options[i].Label);
            if (options[i].Value == value)
            {
                selectedIndex = static_cast<int>(i);
            }
        }
        if (SegmentedPillsIndexed(id, labels, selectedIndex, totalWidth, itemHeight) &&
            selectedIndex >= 0 && static_cast<size_t>(selectedIndex) < options.size())
        {
            value = options[static_cast<size_t>(selectedIndex)].Value;
            return true;
        }
        return false;
    }

    int PresetPills(const char* id, std::span<const FPresetPill> pills, const float totalWidth, const float itemHeight)
    {
        if (pills.empty())
        {
            return -1;
        }

        FScopedId scopeId(id);
        const float available = totalWidth > 0.0f ? totalWidth : ImGui::GetContentRegionAvail().x;
        constexpr float spacing = 4.0f;
        const float count = static_cast<float>(pills.size());
        const float itemWidth = std::max(20.0f, (available - spacing * (count - 1.0f)) / count);

        int clickedIndex = -1;
        FScopedStyle roundingStyle;
        roundingStyle.Add(ImGuiStyleVar_FrameRounding, 4.0f);
        for (size_t i = 0; i < pills.size(); ++i)
        {
            if (i > 0)
            {
                ImGui::SameLine(0.0f, spacing);
            }
            FScopedStyle selectedStyle;
            if (pills[i].Selected)
            {
                selectedStyle.Add(ImGuiCol_Button, Color(EColor::Accent, 0.78f))
                             .Add(ImGuiCol_ButtonHovered, Color(EColor::AccentHover))
                             .Add(ImGuiCol_ButtonActive, Color(EColor::Accent))
                             .Add(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
            }
            if (ImGui::Button(pills[i].Label ? pills[i].Label : "", ImVec2(itemWidth, itemHeight)))
            {
                clickedIndex = static_cast<int>(i);
            }
        }
        return clickedIndex;
    }

    void LabeledRow(const char* label, const float ratio, const float minLabelWidth, const float maxLabelWidth)
    {
        ImGui::AlignTextToFramePadding();
        {
            FScopedStyle textStyle;
            textStyle.Add(ImGuiCol_Text, Color(EColor::TextMuted));
            ImGui::TextUnformatted(label ? label : "");
        }
        const float availableWidth = ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX();
        const float labelWidth = std::clamp(availableWidth * ratio, minLabelWidth, maxLabelWidth);
        ImGui::SameLine(labelWidth);
    }

    bool DrawSettingSliderRow(const char* label, const ImGuiDataType dataType, void* value,
                              const void* minValue, const void* maxValue, const char* format,
                              const float dragSpeed, const float valueWidth)
    {
        FScopedId scopeId(label);
        LabeledRow(label);

        const float sliderWidth = std::max(40.0f, ImGui::GetContentRegionAvail().x - valueWidth - 6.0f);
        bool changed = false;

        ImGui::SetNextItemWidth(sliderWidth);
        changed |= ImGui::SliderScalar("##Slider", dataType, value, minValue, maxValue, format);

        ImGui::SameLine(0.0f, 6.0f);
        ImGui::SetNextItemWidth(valueWidth);
        changed |= ImGui::DragScalar("##Value", dataType, value, dragSpeed, minValue, maxValue, format);

        return changed;
    }

    bool DrawFloatSettingRow(const char* label, float* value, const float minValue, const float maxValue,
                             const char* format, const float dragSpeed, const float valueWidth)
    {
        return DrawSettingSliderRow(label, ImGuiDataType_Float, value, &minValue, &maxValue,
                                    format, dragSpeed, valueWidth);
    }

    bool DrawIntSettingRow(const char* label, int* value, const int minValue, const int maxValue,
                           const char* format, const float valueWidth)
    {
        return DrawSettingSliderRow(label, ImGuiDataType_S32, value, &minValue, &maxValue,
                                    format, 1.0f, valueWidth);
    }

    bool DrawCheckboxSettingRow(const char* label, bool* value)
    {
        return DrawSettingRow(label, [value]() { return ImGui::Checkbox("##Value", value); });
    }

    bool Draw3AxisFloatDrag(const char* label, float value[3], const float speed,
                            const float min, const float max, const float resetValue,
                            float labelWidth, const char* format)
    {
        if (value == nullptr)
        {
            return false;
        }
        const char* resolvedFormat = (format != nullptr && format[0] != '\0') ? format : "%.3f";

        FScopedId scopeId(label);
        const float availWidth = ImGui::GetContentRegionAvail().x;
        constexpr float resetBtnWidth = 22.0f;
        const float frameHeight = ImGui::GetFrameHeight();
        ImFont* font = ImGui::GetFont();
        const float textSize = ImGui::GetFontSize();
        ImDrawList* drawList = ImGui::GetWindowDrawList();

        if (label != nullptr && label[0] != '\0' && labelWidth > 0.0f)
        {
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(Color(EColor::TextMuted), "%s", label);
            ImGui::SameLine(labelWidth);
        }
        else
        {
            labelWidth = 0.0f;
        }

        constexpr float axisGap = 6.0f;
        const float inputAreaWidth = std::max(120.0f, availWidth - labelWidth - resetBtnWidth - 6.0f);
        const float axisWidth = std::floor((inputAreaWidth - axisGap * 2.0f) / 3.0f);
        constexpr float tagWidth = 19.0f;
        const float dragWidth = std::max(26.0f, axisWidth - tagWidth);

        bool changed = false;
        struct FAxisDef
        {
            const char* name;
            const char* tagId;
            const char* inputId;
            ImU32 tagCol;
            ImU32 tagHoverCol;
            ImU32 tagActiveCol;
        };
        static constexpr FAxisDef axes[3] = {
            {"X", "##tag_X", "##axis_X",
             IM_COL32(208, 52, 58, 235), IM_COL32(232, 68, 74, 255), IM_COL32(180, 38, 44, 255)},
            {"Y", "##tag_Y", "##axis_Y",
             IM_COL32(46, 160, 67, 235), IM_COL32(56, 185, 78, 255), IM_COL32(35, 135, 52, 255)},
            {"Z", "##tag_Z", "##axis_Z",
             IM_COL32(38, 115, 222, 235), IM_COL32(52, 138, 245, 255), IM_COL32(28, 92, 185, 255)},
        };

        for (int i = 0; i < 3; ++i)
        {
            if (i > 0)
            {
                ImGui::SameLine(0.0f, axisGap);
            }

            const ImVec2 tagMin = ImGui::GetCursorScreenPos();
            const ImVec2 tagMax(tagMin.x + tagWidth, tagMin.y + frameHeight);
            const ImVec2 dragMin(tagMax.x, tagMin.y);
            const ImVec2 dragMax(dragMin.x + dragWidth, tagMin.y + frameHeight);
            const ImVec2 axisTotalMin = tagMin;
            const ImVec2 axisTotalMax = dragMax;

            ImGui::InvisibleButton(axes[i].tagId, ImVec2(tagWidth, frameHeight));
            const bool tagHovered = ImGui::IsItemHovered();
            const bool tagActive = ImGui::IsItemActive();

            if (tagHovered)
            {
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
                ImGui::SetTooltip("%s Axis (Drag to scrub, double-click to reset)", axes[i].name);
            }

            if (tagActive && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
            {
                value[i] += ImGui::GetIO().MouseDelta.x * speed;
                if (min < max)
                {
                    value[i] = std::clamp(value[i], min, max);
                }
                changed = true;
            }
            if (tagHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
                value[i] = resetValue;
                changed = true;
            }

            const ImU32 tagColor =
                tagActive ? axes[i].tagActiveCol : (tagHovered ? axes[i].tagHoverCol : axes[i].tagCol);
            drawList->AddRectFilled(tagMin, tagMax, tagColor, 4.0f, ImDrawFlags_RoundCornersLeft);

            const ImVec2 charSize = font->CalcTextSizeA(textSize, FLT_MAX, 0.0f, axes[i].name);
            const ImVec2 charPos(tagMin.x + (tagWidth - charSize.x) * 0.5f,
                                 tagMin.y + (frameHeight - charSize.y) * 0.5f);
            drawList->AddText(font, textSize, charPos, IM_COL32(255, 255, 255, 245), axes[i].name);

            ImGui::SameLine(0.0f, 0.0f);

            const bool isDragAreaHovered = ImGui::IsMouseHoveringRect(dragMin, dragMax);
            const ImU32 dragBgCol = ColorU32(
                isDragAreaHovered ? EColor::SurfaceHover : EColor::SurfaceElevated,
                isDragAreaHovered ? 0.90f : 0.65f);
            drawList->AddRectFilled(dragMin, dragMax, dragBgCol, 4.0f, ImDrawFlags_RoundCornersRight);

            ImGui::SetNextItemWidth(dragWidth);
            bool dragActive = false;
            {
                FScopedStyle dragStyle;
                dragStyle.Add(ImGuiStyleVar_FrameRounding, 0.0f)
                         .Add(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0))
                         .Add(ImGuiCol_FrameBgHovered, ImVec4(0, 0, 0, 0))
                         .Add(ImGuiCol_FrameBgActive, ImVec4(0, 0, 0, 0));
                if (ImGui::DragFloat(axes[i].inputId, &value[i], speed, min, max, resolvedFormat))
                {
                    changed = true;
                }
                dragActive = ImGui::IsItemActive();
            }

            if (dragActive)
            {
                drawList->AddRect(axisTotalMin, axisTotalMax, ColorU32(EColor::Accent, 0.70f), 4.0f, 0, 1.0f);
            }
        }

        ImGui::SameLine(0.0f, 6.0f);
        const bool isModified = (std::abs(value[0] - resetValue) > 0.0001f ||
                                 std::abs(value[1] - resetValue) > 0.0001f ||
                                 std::abs(value[2] - resetValue) > 0.0001f);

        {
            FScopedStyle resetStyle;
            resetStyle.Add(ImGuiStyleVar_FrameRounding, 4.0f)
                      .Add(ImGuiCol_Button, ImVec4(0, 0, 0, 0))
                      .Add(ImGuiCol_ButtonHovered, Color(EColor::SurfaceHover, 0.75f))
                      .Add(ImGuiCol_ButtonActive, Color(EColor::SurfaceHover, 0.95f))
                      .Add(ImGuiCol_Text, Color(isModified ? EColor::Accent : EColor::TextDim,
                                                isModified ? 0.95f : 0.40f));

            if (ImGui::Button(ICON_FA_ROTATE_LEFT, ImVec2(resetBtnWidth, frameHeight)))
            {
                value[0] = resetValue;
                value[1] = resetValue;
                value[2] = resetValue;
                changed = true;
            }
            if (ImGui::IsItemHovered())
            {
                const std::string tooltip = isModified
                    ? fmt::format("Reset {} to default ({:.1f})", label ? label : "value", resetValue)
                    : fmt::format("{} is at default value ({:.1f})", label ? label : "Value", resetValue);
                ImGui::SetTooltip("%s", tooltip.c_str());
            }
        }

        return changed;
    }

    void DrawThinSeparator(const float alpha)
    {
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddLine(pos, ImVec2(pos.x + ImGui::GetContentRegionAvail().x, pos.y),
                          ColorU32(EColor::Border, alpha), 1.0f);
        ImGui::Dummy(ImVec2(0.0f, 4.0f));
    }

    void DrawVerticalSeparator(const float height, const float spacing, const float alpha)
    {
        ImGui::SameLine(0.0f, spacing);
        const ImVec2 separatorMin = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddLine(
            ImVec2(separatorMin.x, separatorMin.y + 1.0f),
            ImVec2(separatorMin.x, separatorMin.y + height - 1.0f),
            ColorU32(EColor::Border, alpha));
        ImGui::Dummy(ImVec2(1.0f, height));
        ImGui::SameLine(0.0f, spacing);
    }

    void DrawProgressBar(float fraction, const ImVec4 color, const ImVec2 size)
    {
        fraction = std::clamp(fraction, 0.0f, 1.0f);
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(pos, pos + size, ColorU32(EColor::Background, 0.86f), 4.0f);
        drawList->AddRect(pos, pos + size, ColorU32(EColor::BorderStrong, 0.88f), 4.0f);
        if (fraction > 0.0f)
        {
            const float fillWidth = std::max(2.0f, size.x * fraction);
            drawList->AddRectFilled(pos + ImVec2(1.0f, 1.0f), pos + ImVec2(fillWidth - 1.0f, size.y - 1.0f),
                                    ImGui::GetColorU32(color), 3.0f);
        }
        ImGui::Dummy(size);
    }

    void DrawSmoothSpinner(ImDrawList* drawList, const ImVec2 center, const float radius, const float thickness,
                           const ImU32 color)
    {
        if (drawList == nullptr)
        {
            return;
        }
        constexpr int numSegments = 36;
        const float startAngle = static_cast<float>(ImGui::GetTime() * 6.0);
        constexpr float sweepAngle = 1.6f * 3.14159265f;

        drawList->AddCircle(center, radius, ColorU32(EColor::Border, 0.35f), numSegments, thickness);

        const int arcSegments = static_cast<int>(numSegments * (sweepAngle / (2.0f * 3.14159265f)));
        const float angleStep = sweepAngle / static_cast<float>(arcSegments);

        const ImVec4 baseCol = ImGui::ColorConvertU32ToFloat4(color);
        for (int i = 0; i < arcSegments; ++i)
        {
            const float a0 = startAngle + static_cast<float>(i) * angleStep;
            const float a1 = startAngle + static_cast<float>(i + 1) * angleStep;
            const float alpha = static_cast<float>(i + 1) / static_cast<float>(arcSegments);
            const ImU32 segCol = ImGui::ColorConvertFloat4ToU32(
                ImVec4(baseCol.x, baseCol.y, baseCol.z, baseCol.w * (0.2f + 0.8f * alpha)));

            drawList->AddLine(
                ImVec2(center.x + std::cos(a0) * radius, center.y + std::sin(a0) * radius),
                ImVec2(center.x + std::cos(a1) * radius, center.y + std::sin(a1) * radius),
                segCol, thickness);
        }
    }

    void DrawAnimatedProgressBar(ImDrawList* drawList, const ImVec2 barMin, const ImVec2 barMax, const ImU32 fillCol)
    {
        if (drawList == nullptr)
        {
            return;
        }
        const float width = barMax.x - barMin.x;
        const float height = barMax.y - barMin.y;
        const float rounding = height * 0.5f;

        drawList->AddRectFilled(barMin, barMax, ColorU32(EColor::Background, 0.75f), rounding);
        drawList->AddRect(barMin, barMax, ColorU32(EColor::Border, 0.40f), rounding, 0, 1.0f);

        constexpr float pulseWidthRatio = 0.38f;
        const float pulseWidth = width * pulseWidthRatio;
        const float travelDist = width - pulseWidth;
        const float t = 0.5f + 0.5f * std::sin(static_cast<float>(ImGui::GetTime() * 3.2));
        const float pulseLeft = barMin.x + travelDist * t;
        const float pulseRight = pulseLeft + pulseWidth;

        drawList->PushClipRect(barMin, barMax, true);
        drawList->AddRectFilled(ImVec2(pulseLeft, barMin.y), ImVec2(pulseRight, barMax.y), fillCol, rounding);
        drawList->PopClipRect();
    }

    void Sparkline(const float* values, const int count, ImVec2 size, const ImVec4 color,
                   float scaleMin, float scaleMax, const bool baselineAtZero)
    {
        if (values == nullptr || count <= 1)
        {
            ImGui::Dummy(size);
            return;
        }

        if (size.x <= 0.0f) size.x = ImGui::GetContentRegionAvail().x;
        if (size.y <= 0.0f) size.y = ImGui::GetTextLineHeight() * 1.6f;

        if (scaleMin == FLT_MAX || scaleMax == FLT_MAX)
        {
            float lo = baselineAtZero ? 0.0f : values[0];
            float hi = values[0];
            for (int i = 1; i < count; ++i)
            {
                hi = std::max(hi, values[i]);
                if (!baselineAtZero)
                {
                    lo = std::min(lo, values[i]);
                }
            }
            scaleMin = lo;
            scaleMax = hi;
        }
        const float range = std::max(0.0001f, scaleMax - scaleMin);

        const ImVec2 origin = ImGui::GetCursorScreenPos();
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(origin, origin + size, ColorU32(EColor::Background, 0.55f), 4.0f);

        const float stepX = size.x / static_cast<float>(count - 1);
        const ImU32 lineCol = ImGui::GetColorU32(color);
        const ImU32 fillCol = ImGui::GetColorU32(ImVec4(color.x, color.y, color.z, color.w * 0.18f));

        std::vector<ImVec2> pts;
        pts.reserve(static_cast<size_t>(count));
        for (int i = 0; i < count; ++i)
        {
            const float t = (values[i] - scaleMin) / range;
            const float x = origin.x + stepX * static_cast<float>(i);
            const float y = origin.y + size.y - 2.0f - t * (size.y - 4.0f);
            pts.emplace_back(x, y);
        }

        for (int i = 0; i + 1 < count; ++i)
        {
            const ImVec2 quad[4] = {pts[static_cast<size_t>(i)],
                                    pts[static_cast<size_t>(i + 1)],
                                    ImVec2(pts[static_cast<size_t>(i + 1)].x, origin.y + size.y),
                                    ImVec2(pts[static_cast<size_t>(i)].x, origin.y + size.y)};
            drawList->AddConvexPolyFilled(quad, 4, fillCol);
        }

        drawList->AddPolyline(pts.data(), count, lineCol, ImDrawFlags_None, 1.5f);
        ImGui::Dummy(size);
    }

    void DrawShowFlagItem(const char* name, bool& flag)
    {
        const std::string itemLabel = fmt::format("{} {}", flag ? ICON_FA_SQUARE_CHECK : ICON_FA_SQUARE, name);
        if (ImGui::MenuItem(itemLabel.c_str(), nullptr, false))
        {
            flag = !flag;
        }
    }

    void DrawWireframeOptions(Runtime::Config::UserSettings& settings, const bool enabled)
    {
        if (!enabled)
        {
            return;
        }
        ImGui::Indent();
        DrawShowFlagItem("  X-Ray (ignore depth)", settings.WireframeXRay);
        bool tintByLod = settings.WireframeColorMode == 1;
        DrawShowFlagItem("  Tint by LOD", tintByLod);
        settings.WireframeColorMode = tintByLod ? 1u : 0u;
        ImGui::Unindent();
    }

    void DrawShowFlagsCommon(Runtime::Config::ShowFlags& showFlags, Runtime::Config::UserSettings* settings)
    {
        DrawShowFlagItem("Grid", showFlags.ShowGrid);
        ImGui::Separator();
        DrawShowFlagItem("Lighting", showFlags.DebugDraw_Lighting);
        DrawShowFlagItem("Area Lights", showFlags.DebugDraw_AreaLights);
        DrawShowFlagItem("Bounding Box", showFlags.DebugDraw_BoundingBox);
        DrawShowFlagItem("Physics Bodies", showFlags.DebugDraw_PhysicsBodies);
        DrawShowFlagItem("Visual Debug", showFlags.ShowVisualDebug);
        DrawShowFlagItem("Edge", showFlags.ShowEdge);
        DrawShowFlagItem("Skeleton", showFlags.ShowDebugSkeleton);
        ImGui::Separator();
        DrawShowFlagItem("Wireframe", showFlags.ShowWireframe);
        if (settings != nullptr)
        {
            DrawWireframeOptions(*settings, showFlags.ShowWireframe);
        }
    }

    void PushViewportToolbarStyle()
    {
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(7.0f, 3.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, Color(EColor::Surface, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, Color(EColor::SurfaceHover, 0.72f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, Color(EColor::Accent, 0.24f));
        ImGui::PushStyleColor(ImGuiCol_Button, Color(EColor::Surface, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Color(EColor::SurfaceHover, 0.72f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, Color(EColor::Accent, 0.28f));
        ImGui::PushStyleColor(ImGuiCol_Border, Color(EColor::Border, 0.0f));
    }

    void PopViewportToolbarStyle()
    {
        ImGui::PopStyleColor(7);
        ImGui::PopStyleVar(3);
    }

    void PushViewportPopupStyle()
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 8.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 5.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.0f, 0.5f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 4.0f));
        ImGui::PushStyleColor(ImGuiCol_PopupBg, Color(EColor::Background, 0.96f));
        ImGui::PushStyleColor(ImGuiCol_Header, Color(EColor::SurfaceHover, 0.46f));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, Color(EColor::SurfaceHover, 0.78f));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, Color(EColor::Accent, 0.26f));
        ImGui::PushStyleColor(ImGuiCol_Border, Color(EColor::Border, 0.0f));
    }

    void PopViewportPopupStyle()
    {
        ImGui::PopStyleColor(5);
        ImGui::PopStyleVar(5);
    }

    void PushToolWindowStyle()
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18.0f, 16.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 8.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 7.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(8.0f, 5.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, Color(EColor::SurfaceElevated));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, Color(EColor::SurfaceHover));
        ImGui::PushStyleColor(ImGuiCol_Header, Color(EColor::SurfaceElevated, 0.80f));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, Color(EColor::SurfaceHover, 0.92f));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, Color(EColor::Accent, 0.32f));
        ImGui::PushStyleColor(ImGuiCol_TableHeaderBg, Color(EColor::Surface, 0.56f));
        ImGui::PushStyleColor(ImGuiCol_TableRowBg, Color(EColor::Background, 0.08f));
        ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt, Color(EColor::SurfaceElevated, 0.32f));
    }

    void PopToolWindowStyle()
    {
        ImGui::PopStyleColor(8);
        ImGui::PopStyleVar(7);
    }

    void PushToolWindowContentStyle()
    {
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 5.0f));
    }

    void PopToolWindowContentStyle()
    {
        ImGui::PopStyleVar();
    }
}
