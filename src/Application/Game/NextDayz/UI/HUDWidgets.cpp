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

    void DrawCornerTicks(ImDrawList* draw, const ImVec2& min, const ImVec2& max, float tickLen, ImU32 color)
    {
        if (!draw) return;
        const float len = std::min(tickLen, std::min((max.x - min.x) * 0.4f, (max.y - min.y) * 0.4f));
        if (len <= 0.0f) return;

        // Top-Left
        draw->AddLine(min, ImVec2(min.x + len, min.y), color, 1.5f);
        draw->AddLine(min, ImVec2(min.x, min.y + len), color, 1.5f);

        // Top-Right
        draw->AddLine(ImVec2(max.x, min.y), ImVec2(max.x - len, min.y), color, 1.5f);
        draw->AddLine(ImVec2(max.x, min.y), ImVec2(max.x, min.y + len), color, 1.5f);

        // Bottom-Left
        draw->AddLine(ImVec2(min.x, max.y), ImVec2(min.x + len, max.y), color, 1.5f);
        draw->AddLine(ImVec2(min.x, max.y), ImVec2(min.x, max.y - len), color, 1.5f);

        // Bottom-Right
        draw->AddLine(max, ImVec2(max.x - len, max.y), color, 1.5f);
        draw->AddLine(max, ImVec2(max.x, max.y - len), color, 1.5f);
    }

    void DrawTacticalPanel(ImDrawList* draw, const ImVec2& min, const ImVec2& max,
                           const char* title, const char* badge, bool cornerTicks)
    {
        if (!draw) return;
        draw->AddRectFilled(min, max, UITheme::Colors::SurfacePanel, 2.0f);
        draw->AddRect(min, max, UITheme::Colors::BorderMuted, 2.0f, 0, 1.0f);

        if (title && title[0] != '\0')
        {
            const float headerH = 26.0f;
            const ImVec2 hMax(max.x, min.y + headerH);
            draw->AddRectFilled(min, hMax, UITheme::Colors::SurfaceHeader, 2.0f);
            draw->AddLine(ImVec2(min.x, hMax.y), hMax, UITheme::Colors::BorderMuted, 1.0f);

            // Left amber indicator pip
            draw->AddRectFilled(ImVec2(min.x + 4.0f, min.y + 6.0f), ImVec2(min.x + 7.0f, hMax.y - 6.0f),
                                UITheme::Colors::AccentAmber, 1.0f);

            draw->AddText(ImVec2(min.x + 14.0f, min.y + 5.0f), UITheme::Colors::TextPrimary, title);

            if (badge && badge[0] != '\0')
            {
                const ImVec2 bSize = ImGui::CalcTextSize(badge);
                draw->AddText(ImVec2(max.x - bSize.x - 10.0f, min.y + 5.0f), UITheme::Colors::AccentCyan, badge);
            }
        }

        if (cornerTicks)
        {
            DrawCornerTicks(draw, min, max, 8.0f, UITheme::Colors::BorderTactical);
        }
    }

    void DrawHazardDropZone(ImDrawList* draw, const ImVec2& min, const ImVec2& max,
                            const char* label, bool hovered, bool activeDrop)
    {
        if (!draw) return;
        const float w = max.x - min.x;
        const float h = max.y - min.y;
        if (w <= 0.0f || h <= 0.0f) return;

        // Base background
        draw->AddRectFilled(min, max, activeDrop ? UITheme::Colors::SlotInvalidDropBg : UITheme::Colors::SurfaceCard, 3.0f);

        // Angled hazard warning stripes inside clip
        draw->PushClipRect(min, max, true);
        const float stripeW = 14.0f;
        const float totalSpan = w + h + stripeW * 2.0f;
        const ImU32 stripeCol = activeDrop ? IM_COL32(244, 63, 94, 60) :
                                hovered ? IM_COL32(245, 158, 11, 40) : IM_COL32(45, 55, 75, 30);

        for (float x = -h; x < totalSpan; x += stripeW * 2.0f)
        {
            const ImVec2 p1(min.x + x, max.y);
            const ImVec2 p2(min.x + x + stripeW, max.y);
            const ImVec2 p3(min.x + x + stripeW + h, min.y);
            const ImVec2 p4(min.x + x + h, min.y);
            draw->AddQuadFilled(p1, p2, p3, p4, stripeCol);
        }
        draw->PopClipRect();

        // Outer border
        const ImU32 borderCol = activeDrop ? UITheme::Colors::AccentRose :
                                hovered ? UITheme::Colors::AccentAmber : UITheme::Colors::BorderMuted;
        draw->AddRect(min, max, borderCol, 3.0f, 0, activeDrop ? 2.0f : 1.2f);
        DrawCornerTicks(draw, min, max, 7.0f, borderCol);

        // Label in center with tactical pill
        if (label && label[0] != '\0')
        {
            const ImVec2 tSize = ImGui::CalcTextSize(label);
            const ImVec2 center((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
            const ImVec2 bMin(center.x - tSize.x * 0.5f - 10.0f, center.y - tSize.y * 0.5f - 4.0f);
            const ImVec2 bMax(center.x + tSize.x * 0.5f + 10.0f, center.y + tSize.y * 0.5f + 4.0f);
            draw->AddRectFilled(bMin, bMax, IM_COL32(10, 14, 20, 220), 3.0f);
            draw->AddRect(bMin, bMax, borderCol, 3.0f, 0, 1.0f);
            draw->AddText(ImVec2(center.x - tSize.x * 0.5f, center.y - tSize.y * 0.5f),
                          activeDrop ? UITheme::Colors::AccentRose : UITheme::Colors::TextSecondary, label);
        }
    }

    void DrawVitalsGauge(ImDrawList* draw, const ImVec2& pos, float width,
                         const char* label, float value01, ImU32 barColor,
                         const char* valStr, bool pulse)
    {
        if (!draw || width <= 0.0f) return;
        const float clamped = std::clamp(value01, 0.0f, 1.0f);
        const float barH = 7.0f;

        // Label & value string line
        char defVal[32];
        if (!valStr)
        {
            std::snprintf(defVal, sizeof(defVal), "%.0f%%", clamped * 100.0f);
            valStr = defVal;
        }
        const ImVec2 vSize = ImGui::CalcTextSize(valStr);
        draw->AddText(pos, UITheme::Colors::TextSecondary, label);
        draw->AddText(ImVec2(pos.x + width - vSize.x, pos.y), barColor, valStr);

        // Bar track
        const ImVec2 bMin(pos.x, pos.y + ImGui::GetTextLineHeight() + 3.0f);
        const ImVec2 bMax(pos.x + width, bMin.y + barH);
        draw->AddRectFilled(bMin, bMax, IM_COL32(20, 26, 36, 200), 2.0f);
        draw->AddRect(bMin, bMax, UITheme::Colors::BorderMuted, 2.0f, 0, 1.0f);

        if (clamped > 0.005f)
        {
            ImU32 fillCol = barColor;
            if (pulse)
            {
                const float t = static_cast<float>(ImGui::GetTime());
                const float wave = 0.65f + 0.35f * std::sin(t * 8.0f);
                const int alpha = static_cast<int>(255.0f * wave);
                fillCol = (fillCol & 0x00FFFFFF) | (static_cast<ImU32>(alpha) << 24);
            }
            const float fillW = (width - 2.0f) * clamped;
            draw->AddRectFilled(ImVec2(bMin.x + 1.0f, bMin.y + 1.0f),
                                ImVec2(bMin.x + 1.0f + fillW, bMax.y - 1.0f), fillCol, 1.5f);
        }
    }

    void DrawECGLine(ImDrawList* draw, const ImVec2& pos, const ImVec2& size, float bpm, ImU32 color)
    {
        if (!draw || size.x <= 10.0f || size.y <= 6.0f) return;
        const float midY = pos.y + size.y * 0.5f;
        const float time = static_cast<float>(ImGui::GetTime()) * (bpm / 60.0f);

        // Background subtle grid line
        draw->AddLine(ImVec2(pos.x, midY), ImVec2(pos.x + size.x, midY), IM_COL32(60, 30, 35, 90), 1.0f);

        constexpr int kPoints = 32;
        ImVec2 points[kPoints];
        for (int i = 0; i < kPoints; ++i)
        {
            const float u = static_cast<float>(i) / static_cast<float>(kPoints - 1);
            const float x = pos.x + u * size.x;
            // Phase along waveform
            const float phase = std::fmod(u * 2.0f - time, 1.0f);
            const float normP = phase < 0.0f ? phase + 1.0f : phase;
            float dy = 0.0f;

            if (normP > 0.40f && normP < 0.46f)
            {
                // P wave
                dy = -0.2f * std::sin((normP - 0.40f) / 0.06f * 3.14159f);
            }
            else if (normP >= 0.46f && normP < 0.49f)
            {
                // Q dip
                dy = 0.25f * ((normP - 0.46f) / 0.03f);
            }
            else if (normP >= 0.49f && normP < 0.53f)
            {
                // R spike
                const float t = (normP - 0.49f) / 0.04f;
                dy = t < 0.5f ? (0.25f - 1.25f * (t * 2.0f)) : (-1.0f + 1.35f * ((t - 0.5f) * 2.0f));
            }
            else if (normP >= 0.53f && normP < 0.57f)
            {
                // S recovery
                dy = 0.35f * (1.0f - (normP - 0.53f) / 0.04f);
            }
            else if (normP >= 0.60f && normP < 0.72f)
            {
                // T wave
                dy = -0.3f * std::sin((normP - 0.60f) / 0.12f * 3.14159f);
            }

            points[i] = ImVec2(x, midY + dy * (size.y * 0.45f));
        }

        for (int i = 0; i < kPoints - 1; ++i)
        {
            draw->AddLine(points[i], points[i + 1], color, 1.5f);
        }
    }

    void DrawSlotSilhouette(ImDrawList* draw, const ImVec2& min, const ImVec2& max, const char* slotType)
    {
        if (!draw || !slotType) return;
        const ImVec2 center((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
        const ImU32 col = IM_COL32(70, 85, 110, 85);
        constexpr float stroke = 1.2f;

        if (std::strcmp(slotType, "HEAD") == 0)
        {
            // Helmet dome + visor silhouette
            draw->AddCircle(ImVec2(center.x, center.y - 2.0f), 10.0f, col, 16, stroke);
            draw->AddLine(ImVec2(center.x - 11.0f, center.y + 4.0f), ImVec2(center.x + 11.0f, center.y + 4.0f), col, stroke);
            draw->AddLine(ImVec2(center.x - 7.0f, center.y + 8.0f), ImVec2(center.x + 7.0f, center.y + 8.0f), col, stroke);
        }
        else if (std::strcmp(slotType, "TORSO") == 0 || std::strcmp(slotType, "VEST") == 0)
        {
            // Tactical vest / armor plate silhouette
            const float w = 18.0f, h = 18.0f;
            draw->AddRect(ImVec2(center.x - w * 0.5f, center.y - h * 0.5f),
                          ImVec2(center.x + w * 0.5f, center.y + h * 0.5f), col, 2.0f, 0, stroke);
            draw->AddLine(ImVec2(center.x - w * 0.5f + 4.0f, center.y - h * 0.5f + 5.0f),
                          ImVec2(center.x + w * 0.5f - 4.0f, center.y - h * 0.5f + 5.0f), col, stroke);
            draw->AddLine(ImVec2(center.x - w * 0.5f + 4.0f, center.y + 1.0f),
                          ImVec2(center.x + w * 0.5f - 4.0f, center.y + 1.0f), col, stroke);
        }
        else if (std::strcmp(slotType, "LEGS") == 0)
        {
            // Tactical pants silhouette
            draw->AddLine(ImVec2(center.x - 8.0f, center.y - 10.0f), ImVec2(center.x + 8.0f, center.y - 10.0f), col, stroke);
            draw->AddLine(ImVec2(center.x - 8.0f, center.y - 10.0f), ImVec2(center.x - 8.0f, center.y + 10.0f), col, stroke);
            draw->AddLine(ImVec2(center.x + 8.0f, center.y - 10.0f), ImVec2(center.x + 8.0f, center.y + 10.0f), col, stroke);
            draw->AddLine(ImVec2(center.x - 8.0f, center.y + 10.0f), ImVec2(center.x - 3.0f, center.y + 10.0f), col, stroke);
            draw->AddLine(ImVec2(center.x + 8.0f, center.y + 10.0f), ImVec2(center.x + 3.0f, center.y + 10.0f), col, stroke);
            draw->AddLine(ImVec2(center.x - 3.0f, center.y + 10.0f), ImVec2(center.x, center.y - 1.0f), col, stroke);
            draw->AddLine(ImVec2(center.x + 3.0f, center.y + 10.0f), ImVec2(center.x, center.y - 1.0f), col, stroke);
        }
        else if (std::strcmp(slotType, "BACKPACK") == 0)
        {
            // Tactical backpack with pouch straps
            const float w = 18.0f, h = 20.0f;
            draw->AddRect(ImVec2(center.x - w * 0.5f, center.y - h * 0.5f),
                          ImVec2(center.x + w * 0.5f, center.y + h * 0.5f), col, 3.0f, 0, stroke);
            draw->AddRect(ImVec2(center.x - w * 0.35f, center.y - 2.0f),
                          ImVec2(center.x + w * 0.35f, center.y + h * 0.45f), col, 1.5f, 0, stroke);
        }
        else if (std::strcmp(slotType, "WEAPON") == 0 || std::strcmp(slotType, "SIDEARM") == 0)
        {
            // Stylized firearm receiver / barrel line
            draw->AddLine(ImVec2(center.x - 16.0f, center.y), ImVec2(center.x + 14.0f, center.y), col, stroke);
            draw->AddLine(ImVec2(center.x - 8.0f, center.y), ImVec2(center.x - 11.0f, center.y + 9.0f), col, stroke);
            draw->AddLine(ImVec2(center.x - 1.0f, center.y), ImVec2(center.x - 3.0f, center.y + 7.0f), col, stroke);
        }
    }
}
