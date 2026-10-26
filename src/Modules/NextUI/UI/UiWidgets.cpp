#include "Engine/Common/CoreMinimal.hpp"

#include "Modules/NextUI/UI/UiWidgets.hpp"

#include "Modules/NextUI/UI/UiScopes.hpp"
#include "Modules/NextUI/UI/UiTheme.hpp"

#include <cmath>
#include <imgui_internal.h>

namespace NextUI::Foundation
{
    void Tooltip(const char* text)
    {
        if (text == nullptr || text[0] == '\0' || !ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
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
        const bool transparent = options.variant == EButtonVariant::Ghost || options.variant == EButtonVariant::Toolbar;
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

    void CompactStat(const char* label, const std::string_view value)
    {
        ImGui::TextColored(Color(EColor::TextMuted), "%s", label ? label : "");
        const float valueWidth = ImGui::CalcTextSize(value.data(), value.data() + value.size()).x;
        const float available = ImGui::GetContentRegionAvail().x;
        if (available > valueWidth + 16.0f)
        {
            ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - valueWidth - ImGui::GetStyle().WindowPadding.x);
        }
        else
        {
            ImGui::SameLine();
        }
        ImGui::TextUnformatted(value.data(), value.data() + value.size());
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
}
