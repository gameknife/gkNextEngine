#include "HUDWidgets.hpp"

#include <algorithm>
#include <cmath>

namespace NextDayz::UIWidgets
{
    float DrawKeyBadge(ImDrawList* draw, const ImVec2& pos, const char* keyText, float height)
    {
        if (!draw || !keyText)
        {
            return 0.0f;
        }

        const ImVec2 textSize = ImGui::CalcTextSize(keyText);
        const float paddingX = 8.0f;
        const float width = std::max(height, textSize.x + paddingX * 2.0f);
        const float rounding = 4.0f;

        const ImVec2 min = pos;
        const ImVec2 max(pos.x + width, pos.y + height);

        // Bottom drop shadow / 3D extrusion
        draw->AddRectFilled(ImVec2(min.x, min.y + 2.0f), ImVec2(max.x, max.y + 2.0f),
                            UITheme::Colors::KeyCapShadow, rounding);

        // Keycap body
        draw->AddRectFilled(min, max, UITheme::Colors::KeyCapBg, rounding);

        // Keycap top edge highlight
        draw->AddLine(ImVec2(min.x + rounding, min.y + 1.0f), ImVec2(max.x - rounding, min.y + 1.0f),
                      IM_COL32(140, 150, 170, 160), 1.0f);

        // Keycap border
        draw->AddRect(min, max, UITheme::Colors::KeyCapBorder, rounding, 0, 1.0f);

        // Centered text
        const ImVec2 textPos(min.x + (width - textSize.x) * 0.5f, min.y + (height - textSize.y) * 0.5f);
        draw->AddText(textPos, UITheme::Colors::TextPrimary, keyText);

        return width;
    }

    void DrawRadialProgress(ImDrawList* draw, const ImVec2& center, float radius, float thickness,
                            float progress01, ImU32 arcColor, ImU32 trackColor)
    {
        if (!draw || radius <= 0.0f)
        {
            return;
        }

        const float clamped = std::clamp(progress01, 0.0f, 1.0f);
        constexpr int kSegments = 64;
        constexpr float kPi = 3.14159265358979323846f;
        constexpr float kStartAngle = -kPi * 0.5f; // 12 o'clock

        // Full track circle
        draw->AddCircle(center, radius, trackColor, kSegments, thickness);

        // Active progress arc
        if (clamped > 0.005f)
        {
            const float endAngle = kStartAngle + clamped * 2.0f * kPi;
            draw->PathClear();
            draw->PathArcTo(center, radius, kStartAngle, endAngle, kSegments);
            draw->PathStroke(arcColor, 0, thickness);

            // Glowing tip dot
            const ImVec2 tip(center.x + std::cos(endAngle) * radius, center.y + std::sin(endAngle) * radius);
            draw->AddCircleFilled(tip, thickness * 0.75f, IM_COL32(255, 255, 255, 240));
        }
    }

    void DrawTacticalBar(ImDrawList* draw, const ImVec2& pMin, const ImVec2& pMax, float fraction,
                         ImU32 fillColor, ImU32 emptyColor, bool pulse)
    {
        if (!draw)
        {
            return;
        }

        const float width = pMax.x - pMin.x;
        const float height = pMax.y - pMin.y;
        if (width <= 0.0f || height <= 0.0f)
        {
            return;
        }

        const float clamped = std::clamp(fraction, 0.0f, 1.0f);
        const float rounding = 2.0f;

        // Background track
        draw->AddRectFilled(pMin, pMax, emptyColor, rounding);

        // Fill segment
        if (clamped > 0.002f)
        {
            const float fillWidth = width * clamped;
            ImU32 col = fillColor;
            if (pulse)
            {
                const float t = static_cast<float>(ImGui::GetTime());
                const float wave = 0.65f + 0.35f * std::sin(t * 7.0f);
                const int alpha = static_cast<int>(255.0f * wave);
                col = (col & 0x00FFFFFF) | (static_cast<ImU32>(alpha) << 24);
            }
            draw->AddRectFilled(pMin, ImVec2(pMin.x + fillWidth, pMax.y), col, rounding);
        }

        // 1px Subtle tactical border
        draw->AddRect(pMin, pMax, UITheme::Colors::BorderMuted, rounding, 0, 1.0f);
    }

    void DrawSlotFrame(ImDrawList* draw, const ImVec2& min, const ImVec2& max,
                       bool occupied, bool hovered, bool validDropTarget, bool invalidDropTarget,
                       float rounding)
    {
        if (!draw)
        {
            return;
        }

        ImU32 bg = occupied ? UITheme::Colors::SlotOccupiedBg : UITheme::Colors::SlotEmptyBg;
        ImU32 border = occupied ? UITheme::Colors::SlotOccupiedBorder : UITheme::Colors::SlotEmptyBorder;
        float borderWidth = 1.0f;

        if (invalidDropTarget)
        {
            bg = UITheme::Colors::SlotInvalidDropBg;
            border = UITheme::Colors::SlotInvalidDropBorder;
            borderWidth = 2.0f;
        }
        else if (validDropTarget)
        {
            bg = UITheme::Colors::SlotValidDropBg;
            border = UITheme::Colors::SlotValidDropBorder;
            borderWidth = 2.0f;
        }
        else if (hovered)
        {
            border = UITheme::Colors::SlotHoverBorder;
            borderWidth = 1.5f;
        }

        draw->AddRectFilled(min, max, bg, rounding);
        draw->AddRect(min, max, border, rounding, 0, borderWidth);
    }

    ImVec2 DrawStatusBadge(ImDrawList* draw, const ImVec2& pos, const char* icon, const char* text,
                           ImU32 bgColor, ImU32 borderColor, ImU32 textColor)
    {
        if (!draw || !text)
        {
            return ImVec2(0.0f, 0.0f);
        }

        char fullLabel[128];
        if (icon && icon[0] != '\0')
        {
            std::snprintf(fullLabel, sizeof(fullLabel), "%s  %s", icon, text);
        }
        else
        {
            std::snprintf(fullLabel, sizeof(fullLabel), "%s", text);
        }

        const ImVec2 textSize = ImGui::CalcTextSize(fullLabel);
        const float padX = 10.0f;
        const float padY = 4.0f;
        const ImVec2 size(textSize.x + padX * 2.0f, textSize.y + padY * 2.0f);
        const ImVec2 max(pos.x + size.x, pos.y + size.y);
        const float rounding = 4.0f;

        draw->AddRectFilled(pos, max, bgColor, rounding);
        draw->AddRect(pos, max, borderColor, rounding, 0, 1.2f);
        draw->AddText(ImVec2(pos.x + padX, pos.y + padY), textColor, fullLabel);

        return size;
    }
}
