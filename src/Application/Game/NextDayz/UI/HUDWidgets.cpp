#include "HUDWidgets.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace NextDayz::UIWidgets
{
    namespace
    {
        ImU32 PulseColor(ImU32 color, float speed = 7.0f, float minAlpha = 0.58f)
        {
            const float t = static_cast<float>(ImGui::GetTime());
            const float wave = minAlpha + (1.0f - minAlpha) * (0.5f + 0.5f * std::sin(t * speed));
            const int baseA = static_cast<int>((color >> IM_COL32_A_SHIFT) & 0xFF);
            const int a = static_cast<int>(baseA * wave);
            return (color & ~IM_COL32_A_MASK) | (static_cast<ImU32>(a) << IM_COL32_A_SHIFT);
        }

        ImU32 ScaleAlpha(ImU32 color, float mul)
        {
            const int baseA = static_cast<int>((color >> IM_COL32_A_SHIFT) & 0xFF);
            const int a = std::clamp(static_cast<int>(baseA * mul), 0, 255);
            return (color & ~IM_COL32_A_MASK) | (static_cast<ImU32>(a) << IM_COL32_A_SHIFT);
        }
    }

    float DrawKeyBadge(ImDrawList* draw, const ImVec2& pos, const char* keyText, float height)
    {
        if (!draw || !keyText) return 0.0f;

        ImFont* font = UITheme::BoldFont();
        const float fontSize = std::max(12.0f, height * 0.62f);
        const ImVec2 textSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, keyText);
        const float paddingX = UITheme::S(7.0f);
        const float width = std::max(height, textSize.x + paddingX * 2.0f);
        const float rounding = UITheme::S(5.0f);

        const ImVec2 min = pos;
        const ImVec2 max(pos.x + width, pos.y + height);

        draw->AddRectFilled(ImVec2(min.x, min.y + 2.0f), ImVec2(max.x, max.y + 2.5f),
                            UITheme::Colors::KeyCapShadow, rounding);
        draw->AddRectFilled(min, max, UITheme::Colors::KeyCapBg, rounding);
        draw->AddLine(ImVec2(min.x + rounding * 0.5f, min.y + 1.2f),
                      ImVec2(max.x - rounding * 0.5f, min.y + 1.2f),
                      IM_COL32(242, 239, 230, 58), 1.0f);
        draw->AddRect(min, max, UITheme::Colors::KeyCapBorder, rounding, 0, 1.3f);

        const ImVec2 textPos(min.x + (width - textSize.x) * 0.5f, min.y + (height - textSize.y) * 0.5f);
        draw->AddText(font, fontSize, textPos, UITheme::Colors::TextPrimary, keyText);
        return width;
    }

    void DrawRadialProgress(ImDrawList* draw, const ImVec2& center, float radius, float thickness,
                            float progress01, ImU32 arcColor, ImU32 trackColor)
    {
        if (!draw || radius <= 0.0f) return;

        const float clamped = std::clamp(progress01, 0.0f, 1.0f);
        constexpr int kSegments = 64;
        constexpr float kPi = 3.14159265358979323846f;
        constexpr float kStartAngle = -kPi * 0.5f;

        draw->AddCircleFilled(center, radius - thickness * 0.5f, IM_COL32(16, 20, 18, 210), kSegments);
        draw->AddCircle(center, radius, trackColor, kSegments, thickness);

        if (clamped > 0.005f)
        {
            const float endAngle = kStartAngle + clamped * 2.0f * kPi;
            draw->PathClear();
            draw->PathArcTo(center, radius, kStartAngle, endAngle, kSegments);
            draw->PathStroke(arcColor, 0, thickness);

            const ImVec2 tip(center.x + std::cos(endAngle) * radius, center.y + std::sin(endAngle) * radius);
            draw->AddCircleFilled(tip, thickness * 0.65f, UITheme::Colors::TextPrimary);
        }
    }

    void DrawTacticalBar(ImDrawList* draw, const ImVec2& pMin, const ImVec2& pMax, float fraction,
                         ImU32 fillColor, ImU32 emptyColor, bool pulse, float rounding)
    {
        if (!draw) return;
        const float width = pMax.x - pMin.x;
        const float height = pMax.y - pMin.y;
        if (width <= 0.0f || height <= 0.0f) return;

        const float r = std::min(rounding, height * 0.45f);
        const float clamped = std::clamp(fraction, 0.0f, 1.0f);
        draw->AddRectFilled(ImVec2(pMin.x, pMin.y + 1.5f), ImVec2(pMax.x, pMax.y + 1.5f),
                            IM_COL32(6, 8, 7, 130), r);
        draw->AddRectFilled(pMin, pMax, emptyColor, r);
        if (clamped > 0.004f)
        {
            const ImU32 col = pulse ? PulseColor(fillColor, 7.0f, 0.58f) : fillColor;
            const float fillW = std::max(r * 1.4f, width * clamped);
            const ImVec2 fMax(std::min(pMax.x, pMin.x + fillW), pMax.y);
            draw->AddRectFilled(pMin, fMax, col, r);
            // Subtle flat-cartoon top specular band for tactile crispness
            if (height >= 8.0f)
            {
                draw->AddRectFilled(ImVec2(pMin.x + 1.5f, pMin.y + 1.0f),
                                    ImVec2(fMax.x - 1.5f, pMin.y + height * 0.36f),
                                    IM_COL32(255, 255, 255, 38), r * 0.6f);
            }
        }
        draw->AddRect(pMin, pMax, IM_COL32(232, 226, 208, 58), r, 0, 1.25f);
    }

    void DrawInlineVitalBar(ImDrawList* draw, const ImVec2& pMin, const ImVec2& pMax,
                            EVitalIcon icon, const char* label, float value01,
                            ImU32 fillColor, const char* valStr, bool pulse)
    {
        if (!draw) return;
        const float width = pMax.x - pMin.x;
        const float height = pMax.y - pMin.y;
        if (width <= 0.0f || height <= 0.0f) return;

        const float clamped = std::clamp(value01, 0.0f, 1.0f);
        const float r = UITheme::S(6.0f);
        draw->AddRectFilled(pMin, pMax, IM_COL32(20, 25, 22, 232), r);
        if (clamped > 0.005f)
        {
            const ImU32 col = pulse ? PulseColor(fillColor, 7.5f, 0.60f) : fillColor;
            const float fillW = std::max(r * 1.5f, width * clamped);
            const ImVec2 fMax(std::min(pMax.x, pMin.x + fillW), pMax.y);
            draw->AddRectFilled(pMin, fMax, ScaleAlpha(col, 0.88f), r);
            draw->AddRectFilled(ImVec2(pMin.x + 1.5f, pMin.y + 1.0f),
                                ImVec2(fMax.x - 1.5f, pMin.y + height * 0.34f),
                                IM_COL32(255, 255, 255, 28), r * 0.6f);
        }
        draw->AddRect(pMin, pMax, IM_COL32(232, 226, 208, 56), r, 0, 1.25f);

        const float midY = (pMin.y + pMax.y) * 0.5f;
        const float iconSz = std::min(height * 0.58f, UITheme::S(16.0f));
        DrawVitalIcon(draw, ImVec2(pMin.x + UITheme::S(14.0f), midY), iconSz, icon, UITheme::Colors::TextPrimary);

        ImFont* boldFont = UITheme::BoldFont();
        const float labelSz = std::min(height * 0.62f, UITheme::S(16.5f));
        if (label && label[0] != '\0')
        {
            const ImVec2 lSz = boldFont->CalcTextSizeA(labelSz, FLT_MAX, 0.0f, label);
            DrawTextShadow(draw, boldFont, labelSz, ImVec2(pMin.x + UITheme::S(27.0f), midY - lSz.y * 0.5f),
                           UITheme::Colors::TextPrimary, label, 1.2f);
        }

        char defVal[32];
        if (!valStr)
        {
            std::snprintf(defVal, sizeof(defVal), "%.0f%%", clamped * 100.0f);
            valStr = defVal;
        }
        ImFont* numFont = UITheme::BoldFont();
        const float numSz = std::min(height * 0.68f, UITheme::S(18.0f));
        const ImVec2 vSz = numFont->CalcTextSizeA(numSz, FLT_MAX, 0.0f, valStr);
        DrawTextShadow(draw, numFont, numSz, ImVec2(pMax.x - vSz.x - UITheme::S(10.0f), midY - vSz.y * 0.5f),
                       UITheme::Colors::TextPrimary, valStr, 1.2f);
    }

    void DrawSlotFrame(ImDrawList* draw, const ImVec2& min, const ImVec2& max,
                       bool occupied, bool hovered, bool validDropTarget, bool invalidDropTarget,
                       float rounding)
    {
        if (!draw) return;
        ImU32 bg = occupied ? UITheme::Colors::SlotOccupiedBg : UITheme::Colors::SlotEmptyBg;
        ImU32 border = occupied ? UITheme::Colors::SlotOccupiedBorder : UITheme::Colors::SlotEmptyBorder;
        float borderWidth = 1.25f;

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
            borderWidth = 1.8f;
        }

        draw->AddRectFilled(min, max, bg, rounding);
        draw->AddRect(min, max, border, rounding, 0, borderWidth);
    }

    void DrawVitalIcon(ImDrawList* draw, const ImVec2& c, float s, EVitalIcon icon, ImU32 col)
    {
        if (!draw) return;
        const float r = s * 0.48f;
        const ImU32 hi = IM_COL32(255, 255, 255, 115);
        switch (icon)
        {
        case EVitalIcon::Health:
        {
            // Plump flat-stylized heart matching the mockups
            const float lobeR = r * 0.54f;
            const float lobeY = c.y - r * 0.24f;
            draw->AddCircleFilled(ImVec2(c.x - r * 0.42f, lobeY), lobeR, col, 16);
            draw->AddCircleFilled(ImVec2(c.x + r * 0.42f, lobeY), lobeR, col, 16);
            draw->AddTriangleFilled(ImVec2(c.x - r * 0.92f, lobeY + lobeR * 0.28f),
                                    ImVec2(c.x + r * 0.92f, lobeY + lobeR * 0.28f),
                                    ImVec2(c.x, c.y + r * 0.98f), col);
            draw->AddCircleFilled(ImVec2(c.x - r * 0.48f, lobeY - lobeR * 0.22f), lobeR * 0.24f, hi, 10);
            break;
        }
        case EVitalIcon::Healing:
        {
            // Plump rounded medical cross
            const float arm = r * 0.38f;
            draw->AddRectFilled(ImVec2(c.x - arm, c.y - r), ImVec2(c.x + arm, c.y + r), col, arm * 0.5f);
            draw->AddRectFilled(ImVec2(c.x - r, c.y - arm), ImVec2(c.x + r, c.y + arm), col, arm * 0.5f);
            break;
        }
        case EVitalIcon::Hunger:
        {
            // Stylized flat meal bowl + steam wisps (warm cartoon silhouette)
            draw->AddCircleFilled(ImVec2(c.x, c.y + r * 0.16f), r * 0.78f, col, 18);
            // Cut top flat using a horizontal rim band
            draw->AddRectFilled(ImVec2(c.x - r * 0.92f, c.y - r * 0.18f),
                                ImVec2(c.x + r * 0.92f, c.y + r * 0.22f), col, r * 0.18f);
            draw->AddRectFilled(ImVec2(c.x - r * 0.48f, c.y + r * 0.72f),
                                ImVec2(c.x + r * 0.48f, c.y + r * 0.96f), col, r * 0.12f);
            // Two small warm steam puffs above the bowl
            draw->AddLine(ImVec2(c.x - r * 0.30f, c.y - r * 0.42f), ImVec2(c.x - r * 0.18f, c.y - r * 0.88f), col, std::max(1.5f, r * 0.22f));
            draw->AddLine(ImVec2(c.x + r * 0.26f, c.y - r * 0.42f), ImVec2(c.x + r * 0.38f, c.y - r * 0.88f), col, std::max(1.5f, r * 0.22f));
            break;
        }
        case EVitalIcon::Hydration:
        case EVitalIcon::Bleeding:
        {
            // Plump glossy teardrop with crisp white specular dot
            draw->AddTriangleFilled(ImVec2(c.x, c.y - r * 1.08f),
                                    ImVec2(c.x + r * 0.78f, c.y + r * 0.16f),
                                    ImVec2(c.x - r * 0.78f, c.y + r * 0.16f), col);
            draw->AddCircleFilled(ImVec2(c.x, c.y + r * 0.28f), r * 0.76f, col, 18);
            draw->AddCircleFilled(ImVec2(c.x - r * 0.26f, c.y + r * 0.18f), r * 0.20f, hi, 10);
            break;
        }
        case EVitalIcon::Cold:
        {
            const float th = std::max(1.6f, r * 0.24f);
            draw->AddLine(ImVec2(c.x - r, c.y), ImVec2(c.x + r, c.y), col, th);
            draw->AddLine(ImVec2(c.x, c.y - r), ImVec2(c.x, c.y + r), col, th);
            const float d = r * 0.70f;
            draw->AddLine(ImVec2(c.x - d, c.y - d), ImVec2(c.x + d, c.y + d), col, th * 0.85f);
            draw->AddLine(ImVec2(c.x + d, c.y - d), ImVec2(c.x - d, c.y + d), col, th * 0.85f);
            draw->AddCircleFilled(c, r * 0.26f, col, 10);
            break;
        }
        case EVitalIcon::Weak:
        {
            draw->AddTriangleFilled(ImVec2(c.x, c.y - r), ImVec2(c.x + r, c.y + r * 0.88f),
                                    ImVec2(c.x - r, c.y + r * 0.88f), col);
            draw->AddLine(ImVec2(c.x, c.y - r * 0.35f), ImVec2(c.x, c.y + r * 0.28f), UITheme::Colors::TextOnAccent, std::max(1.8f, r * 0.26f));
            draw->AddCircleFilled(ImVec2(c.x, c.y + r * 0.56f), std::max(1.3f, r * 0.16f), UITheme::Colors::TextOnAccent, 8);
            break;
        }
        case EVitalIcon::Lantern:
        {
            // Flat camp lantern silhouette (handle, cap, glowing body, base)
            draw->PathClear();
            draw->PathArcTo(ImVec2(c.x, c.y - r * 0.56f), r * 0.38f, 3.14159f, 6.28318f, 12);
            draw->PathStroke(col, 0, std::max(1.5f, r * 0.18f));
            draw->AddRectFilled(ImVec2(c.x - r * 0.58f, c.y - r * 0.58f), ImVec2(c.x + r * 0.58f, c.y - r * 0.32f), col, 2.0f);
            draw->AddRectFilled(ImVec2(c.x - r * 0.46f, c.y - r * 0.30f), ImVec2(c.x + r * 0.46f, c.y + r * 0.54f), ScaleAlpha(col, 0.55f), 2.0f);
            draw->AddRect(ImVec2(c.x - r * 0.46f, c.y - r * 0.30f), ImVec2(c.x + r * 0.46f, c.y + r * 0.54f), col, 2.0f, 0, std::max(1.4f, r * 0.16f));
            draw->AddCircleFilled(ImVec2(c.x, c.y + r * 0.12f), r * 0.22f, UITheme::Colors::AccentAmber, 10);
            draw->AddRectFilled(ImVec2(c.x - r * 0.64f, c.y + r * 0.56f), ImVec2(c.x + r * 0.64f, c.y + r * 0.88f), col, 2.0f);
            break;
        }
        case EVitalIcon::Armor:
        {
            const ImVec2 shield[5] = {
                ImVec2(c.x, c.y - r),
                ImVec2(c.x + r * 0.88f, c.y - r * 0.56f),
                ImVec2(c.x + r * 0.72f, c.y + r * 0.44f),
                ImVec2(c.x, c.y + r),
                ImVec2(c.x - r * 0.72f, c.y + r * 0.44f),
            };
            const ImVec2 shieldFull[6] = {
                ImVec2(c.x, c.y - r),
                ImVec2(c.x + r * 0.88f, c.y - r * 0.56f),
                ImVec2(c.x + r * 0.72f, c.y + r * 0.44f),
                ImVec2(c.x, c.y + r),
                ImVec2(c.x - r * 0.72f, c.y + r * 0.44f),
                ImVec2(c.x - r * 0.88f, c.y - r * 0.56f),
            };
            (void)shield;
            draw->AddConvexPolyFilled(shieldFull, 6, col);
            const ImVec2 halfHi[4] = {
                ImVec2(c.x, c.y - r * 0.86f),
                ImVec2(c.x, c.y + r * 0.84f),
                ImVec2(c.x - r * 0.58f, c.y + r * 0.36f),
                ImVec2(c.x - r * 0.72f, c.y - r * 0.48f),
            };
            draw->AddConvexPolyFilled(halfHi, 4, hi);
            break;
        }
        case EVitalIcon::Compass:
        {
            draw->AddCircleFilled(c, r * 0.92f, ScaleAlpha(col, 0.22f), 24);
            draw->AddCircle(c, r * 0.92f, col, 24, std::max(1.6f, r * 0.18f));
            // Two-tone diamond needle angled NE-SW
            const ImVec2 nTip(c.x + r * 0.46f, c.y - r * 0.46f);
            const ImVec2 sTip(c.x - r * 0.46f, c.y + r * 0.46f);
            const ImVec2 lMid(c.x - r * 0.22f, c.y - r * 0.22f);
            const ImVec2 rMid(c.x + r * 0.22f, c.y + r * 0.22f);
            draw->AddTriangleFilled(nTip, lMid, rMid, UITheme::Colors::AccentAmber);
            draw->AddTriangleFilled(sTip, lMid, rMid, col);
            draw->AddCircleFilled(c, r * 0.14f, UITheme::Colors::SurfacePanel, 8);
            break;
        }
        case EVitalIcon::Map:
        {
            // 3-panel folded map silhouette
            const float w3 = r * 0.58f;
            const float h = r * 0.78f;
            const ImVec2 p0[4] = {ImVec2(c.x - w3 * 1.5f, c.y - h * 0.85f), ImVec2(c.x - w3 * 0.5f, c.y - h),
                                  ImVec2(c.x - w3 * 0.5f, c.y + h * 0.85f), ImVec2(c.x - w3 * 1.5f, c.y + h)};
            const ImVec2 p1[4] = {ImVec2(c.x - w3 * 0.5f, c.y - h), ImVec2(c.x + w3 * 0.5f, c.y - h * 0.82f),
                                  ImVec2(c.x + w3 * 0.5f, c.y + h), ImVec2(c.x - w3 * 0.5f, c.y + h * 0.85f)};
            const ImVec2 p2[4] = {ImVec2(c.x + w3 * 0.5f, c.y - h * 0.82f), ImVec2(c.x + w3 * 1.5f, c.y - h),
                                  ImVec2(c.x + w3 * 1.5f, c.y + h * 0.85f), ImVec2(c.x + w3 * 0.5f, c.y + h)};
            draw->AddConvexPolyFilled(p0, 4, ScaleAlpha(col, 0.72f));
            draw->AddConvexPolyFilled(p1, 4, ScaleAlpha(col, 0.48f));
            draw->AddConvexPolyFilled(p2, 4, ScaleAlpha(col, 0.82f));
            draw->AddPolyline(p0, 4, col, ImDrawFlags_Closed, 1.2f);
            draw->AddPolyline(p1, 4, col, ImDrawFlags_Closed, 1.2f);
            draw->AddPolyline(p2, 4, col, ImDrawFlags_Closed, 1.2f);
            draw->AddCircleFilled(ImVec2(c.x + w3 * 0.15f, c.y - h * 0.1f), r * 0.16f, UITheme::Colors::AccentAmber, 8);
            break;
        }
        case EVitalIcon::Opener:
        {
            // Flat utility tool / knife-opener silhouette
            draw->AddRectFilled(ImVec2(c.x - r * 0.72f, c.y - r * 0.24f),
                                ImVec2(c.x + r * 0.20f, c.y + r * 0.30f), col, r * 0.20f);
            draw->AddTriangleFilled(ImVec2(c.x + r * 0.16f, c.y - r * 0.20f),
                                    ImVec2(c.x + r * 0.90f, c.y - r * 0.04f),
                                    ImVec2(c.x + r * 0.16f, c.y + r * 0.26f), ScaleAlpha(col, 0.82f));
            draw->AddCircleFilled(ImVec2(c.x - r * 0.44f, c.y + r * 0.03f), r * 0.12f, UITheme::Colors::SurfacePanel, 8);
            break;
        }
        case EVitalIcon::Gear:
        {
            constexpr int kTeeth = 8;
            constexpr float kTwoPi = 6.2831853f;
            for (int i = 0; i < kTeeth; ++i)
            {
                const float a = kTwoPi * i / kTeeth;
                const ImVec2 tip(c.x + std::cos(a) * r * 0.86f, c.y + std::sin(a) * r * 0.86f);
                draw->AddCircleFilled(tip, r * 0.22f, col, 8);
            }
            draw->AddCircleFilled(c, r * 0.66f, col, 18);
            draw->AddCircleFilled(c, r * 0.30f, UITheme::Colors::SurfaceHeader, 12);
            break;
        }
        case EVitalIcon::Coin:
        {
            // Flat 4-point tactical gold emblem
            const ImVec2 pts[4] = {
                ImVec2(c.x, c.y - r * 0.96f),
                ImVec2(c.x + r * 0.96f, c.y),
                ImVec2(c.x, c.y + r * 0.96f),
                ImVec2(c.x - r * 0.96f, c.y),
            };
            draw->AddConvexPolyFilled(pts, 4, col);
            draw->AddCircleFilled(c, r * 0.34f, hi, 10);
            break;
        }
        }
    }

    ImVec2 DrawStatusBadge(ImDrawList* draw, const ImVec2& pos, const char* icon, const char* text,
                           ImU32 bgColor, ImU32 borderColor, ImU32 textColor)
    {
        if (!draw || !text) return ImVec2(0.0f, 0.0f);
        char fullLabel[128];
        if (icon && icon[0] != '\0') std::snprintf(fullLabel, sizeof(fullLabel), "%s %s", icon, text);
        else std::snprintf(fullLabel, sizeof(fullLabel), "%s", text);

        ImFont* font = UITheme::BoldFont();
        const float fontSize = UITheme::S(15.0f);
        const ImVec2 textSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, fullLabel);
        const float padX = UITheme::S(10.0f);
        const float padY = UITheme::S(5.0f);
        const float r = UITheme::S(6.0f);
        const ImVec2 size(textSize.x + padX * 2.0f, textSize.y + padY * 2.0f);
        const ImVec2 max(pos.x + size.x, pos.y + size.y);

        draw->AddRectFilled(pos, max, bgColor, r);
        draw->AddRect(pos, max, borderColor, r, 0, 1.3f);
        draw->AddText(font, fontSize, ImVec2(pos.x + padX, pos.y + padY), textColor, fullLabel);
        return size;
    }

    ImVec2 DrawStatusPill(ImDrawList* draw, const ImVec2& pos, EVitalIcon icon, const char* text,
                          ImU32 bgColor, ImU32 borderColor, ImU32 textColor, bool pulse)
    {
        if (!draw || !text) return ImVec2(0.0f, 0.0f);
        ImFont* font = UITheme::BoldFont();
        const float fontSize = UITheme::S(16.0f);
        const ImVec2 textSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, text);
        const float iconBox = UITheme::S(18.0f);
        const float padX = UITheme::S(11.0f);
        const float padY = UITheme::S(6.0f);
        const float r = UITheme::S(8.0f);
        const ImVec2 size(padX + iconBox + UITheme::S(6.0f) + textSize.x + padX, textSize.y + padY * 2.0f);
        const ImVec2 max(pos.x + size.x, pos.y + size.y);

        const ImU32 bg = pulse ? PulseColor(bgColor, 6.5f, 0.72f) : bgColor;
        const ImU32 border = pulse ? PulseColor(borderColor, 6.5f, 0.72f) : borderColor;
        draw->AddRectFilled(ImVec2(pos.x, pos.y + 2.0f), ImVec2(max.x, max.y + 2.0f), IM_COL32(6, 8, 7, 130), r);
        draw->AddRectFilled(pos, max, bg, r);
        draw->AddRect(pos, max, border, r, 0, 1.5f);
        DrawVitalIcon(draw, ImVec2(pos.x + padX + iconBox * 0.45f, pos.y + size.y * 0.5f), UITheme::S(14.0f), icon, textColor);
        draw->AddText(font, fontSize, ImVec2(pos.x + padX + iconBox + UITheme::S(5.0f), pos.y + padY), textColor, text);
        return size;
    }

    void DrawCornerTicks(ImDrawList* draw, const ImVec2& min, const ImVec2& max, float tickLen, ImU32 color)
    {
        if (!draw) return;
        const float len = std::min(tickLen, std::min((max.x - min.x) * 0.35f, (max.y - min.y) * 0.35f));
        if (len <= 0.0f) return;

        draw->AddLine(min, ImVec2(min.x + len, min.y), color, 1.4f);
        draw->AddLine(min, ImVec2(min.x, min.y + len), color, 1.4f);
        draw->AddLine(ImVec2(max.x, min.y), ImVec2(max.x - len, min.y), color, 1.4f);
        draw->AddLine(ImVec2(max.x, min.y), ImVec2(max.x, min.y + len), color, 1.4f);
        draw->AddLine(ImVec2(min.x, max.y), ImVec2(min.x + len, max.y), color, 1.4f);
        draw->AddLine(ImVec2(min.x, max.y), ImVec2(min.x, max.y - len), color, 1.4f);
        draw->AddLine(max, ImVec2(max.x - len, max.y), color, 1.4f);
        draw->AddLine(max, ImVec2(max.x, max.y - len), color, 1.4f);
    }

    void DrawTacticalPanel(ImDrawList* draw, const ImVec2& min, const ImVec2& max,
                           const char* title, const char* badge, bool cornerTicks)
    {
        if (!draw) return;
        const float r = UITheme::S(8.0f);
        draw->AddRectFilled(min, max, UITheme::Colors::SurfacePanel, r);
        draw->AddRect(min, max, UITheme::Colors::BorderTactical, r, 0, 1.3f);

        if (title && title[0] != '\0')
        {
            ImFont* boldFont = UITheme::BoldFont();
            const float fontSize = UITheme::S(17.0f);
            const float headerH = UITheme::S(34.0f);
            const ImVec2 hMax(max.x, min.y + headerH);
            draw->AddRectFilled(min, hMax, UITheme::Colors::SurfaceHeader, r, ImDrawFlags_RoundCornersTop);
            draw->AddLine(ImVec2(min.x, hMax.y), hMax, UITheme::Colors::BorderMuted, 1.0f);
            draw->AddRectFilled(ImVec2(min.x + 2.0f, min.y + 4.0f), ImVec2(min.x + 6.0f, hMax.y - 4.0f), UITheme::Colors::AccentAmber, 2.0f);
            const ImVec2 tSz = boldFont->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, title);
            draw->AddText(boldFont, fontSize, ImVec2(min.x + UITheme::S(14.0f), min.y + (headerH - tSz.y) * 0.5f),
                          UITheme::Colors::TextPrimary, title);

            if (badge && badge[0] != '\0')
            {
                const ImVec2 bSize = boldFont->CalcTextSizeA(fontSize * 0.9f, FLT_MAX, 0.0f, badge);
                draw->AddText(boldFont, fontSize * 0.9f,
                              ImVec2(max.x - bSize.x - UITheme::S(12.0f), min.y + (headerH - bSize.y) * 0.5f),
                              UITheme::Colors::AccentAmber, badge);
            }
        }
        if (cornerTicks) DrawCornerTicks(draw, min, max, 6.0f, UITheme::Colors::BorderTactical);
    }

    void DrawFieldCard(ImDrawList* draw, const ImVec2& min, const ImVec2& max,
                       ImU32 leftAccent, bool selected, bool hovered)
    {
        if (!draw) return;
        const float r = UITheme::S(8.0f);
        const ImU32 bg = selected ? IM_COL32(38, 46, 40, 246)
                       : hovered  ? UITheme::Colors::SurfaceCardHover
                                  : UITheme::Colors::SurfaceCard;
        const ImU32 border = selected ? UITheme::Colors::BorderHighlight
                           : hovered  ? UITheme::Colors::BorderTactical
                                      : UITheme::Colors::BorderMuted;
        draw->AddRectFilled(ImVec2(min.x, min.y + 2.5f), ImVec2(max.x, max.y + 2.5f), IM_COL32(5, 7, 6, 120), r);
        draw->AddRectFilled(min, max, bg, r);
        if (leftAccent != 0)
        {
            draw->AddRectFilled(min, ImVec2(min.x + UITheme::S(4.5f), max.y), leftAccent, r, ImDrawFlags_RoundCornersLeft);
        }
        draw->AddRect(min, max, border, r, 0, selected ? 1.8f : 1.3f);
    }

    void DrawSectionHeader(const char* title, const char* rightBadge, ImU32 accentColor)
    {
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const float w = ImGui::GetContentRegionAvail().x;
        const float h = UITheme::S(30.0f);
        const float r = UITheme::S(6.0f);
        const ImVec2 max(pos.x + w, pos.y + h);
        ImDrawList* draw = ImGui::GetWindowDrawList();

        draw->AddRectFilled(pos, max, UITheme::Colors::SurfaceHeader, r);
        draw->AddRectFilled(ImVec2(pos.x, pos.y), ImVec2(pos.x + UITheme::S(4.0f), max.y), accentColor, r, ImDrawFlags_RoundCornersLeft);
        draw->AddLine(ImVec2(pos.x, max.y), max, UITheme::Colors::BorderMuted, 1.0f);
        ImFont* boldFont = UITheme::BoldFont();
        const float fontSize = UITheme::S(16.5f);
        if (title)
        {
            const ImVec2 tSz = boldFont->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, title);
            draw->AddText(boldFont, fontSize, ImVec2(pos.x + UITheme::S(12.0f), pos.y + (h - tSz.y) * 0.5f),
                          UITheme::Colors::TextPrimary, title);
        }
        if (rightBadge && rightBadge[0] != '\0')
        {
            const ImVec2 bSize = boldFont->CalcTextSizeA(fontSize * 0.92f, FLT_MAX, 0.0f, rightBadge);
            draw->AddText(boldFont, fontSize * 0.92f,
                          ImVec2(max.x - bSize.x - UITheme::S(10.0f), pos.y + (h - bSize.y) * 0.5f),
                          UITheme::Colors::AccentAmber, rightBadge);
        }
        ImGui::Dummy(ImVec2(w, h + UITheme::S(5.0f)));
    }

    void DrawTextShadow(ImDrawList* draw, ImFont* font, float fontSize, const ImVec2& pos,
                        ImU32 color, const char* text, float shadowOffset)
    {
        if (!draw || !text) return;
        const int alpha = static_cast<int>((color >> IM_COL32_A_SHIFT) & 0xFF);
        const ImU32 shadow = IM_COL32(8, 10, 9, static_cast<int>(alpha * 0.78f));
        draw->AddText(font, fontSize, ImVec2(pos.x + shadowOffset * 0.65f, pos.y + shadowOffset), shadow, text);
        draw->AddText(font, fontSize, pos, color, text);
    }

    void DrawTextShadow(ImDrawList* draw, const ImVec2& pos, ImU32 color, const char* text)
    {
        DrawTextShadow(draw, ImGui::GetFont(), ImGui::GetFontSize(), pos, color, text, 1.5f);
    }

    bool PrimaryButton(const char* label, const ImVec2& size, bool enabled, bool hero)
    {
        ImGui::BeginDisabled(!enabled);
        ImFont* font = hero ? UITheme::HeadingFont() : UITheme::BoldFont();
        const float fontSize = hero ? UITheme::S(24.0f) : UITheme::S(17.5f);
        ImGui::PushFont(font, fontSize);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, UITheme::S(7.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.5f);
        ImGui::PushStyleColor(ImGuiCol_Button, UITheme::ColorVec4(UITheme::Colors::AccentAmber));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, UITheme::ColorVec4(UITheme::Colors::AccentAmberHover));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(198, 138, 42, 255));
        ImGui::PushStyleColor(ImGuiCol_Text, UITheme::ColorVec4(UITheme::Colors::TextOnAccent));
        ImGui::PushStyleColor(ImGuiCol_Border, UITheme::ColorVec4(UITheme::Colors::AccentAmberHover));
        const bool pressed = ImGui::Button(label, size);
        // Subtle tactile top specular edge on primary buttons
        if (enabled)
        {
            const ImVec2 bMin = ImGui::GetItemRectMin();
            const ImVec2 bMax = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddLine(
                ImVec2(bMin.x + UITheme::S(5.0f), bMin.y + 1.5f),
                ImVec2(bMax.x - UITheme::S(5.0f), bMin.y + 1.5f),
                IM_COL32(255, 245, 215, 110), 1.5f);
        }
        ImGui::PopStyleColor(5);
        ImGui::PopStyleVar(2);
        ImGui::PopFont();
        ImGui::EndDisabled();
        return pressed;
    }

    bool GhostButton(const char* label, const ImVec2& size, bool active, bool danger)
    {
        const ImU32 bg = active ? IM_COL32(42, 50, 44, 246) : IM_COL32(24, 30, 26, 224);
        const ImU32 hover = danger ? IM_COL32(82, 30, 30, 245) : IM_COL32(42, 51, 46, 248);
        const ImU32 border = active ? UITheme::Colors::BorderHighlight
                           : danger ? IM_COL32(220, 76, 76, 150)
                                    : UITheme::Colors::BorderTactical;
        const ImU32 text = danger ? IM_COL32(244, 162, 162, 255) : UITheme::Colors::TextPrimary;

        ImGui::PushFont(UITheme::BoldFont(), UITheme::S(16.5f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, UITheme::S(6.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.4f);
        ImGui::PushStyleColor(ImGuiCol_Button, UITheme::ColorVec4(bg));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, UITheme::ColorVec4(hover));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, UITheme::ColorVec4(UITheme::Colors::AccentAmber));
        ImGui::PushStyleColor(ImGuiCol_Border, UITheme::ColorVec4(border));
        ImGui::PushStyleColor(ImGuiCol_Text, UITheme::ColorVec4(text));
        const bool pressed = ImGui::Button(label, size);
        ImGui::PopStyleColor(5);
        ImGui::PopStyleVar(2);
        ImGui::PopFont();
        return pressed;
    }

    bool NavTabButton(const char* label, const char* keyHint, bool active, float height)
    {
        ImFont* boldFont = UITheme::BoldFont();
        const float fontSize = UITheme::S(19.0f);
        const float keyFontSize = UITheme::S(15.5f);
        const ImVec2 labelSize = boldFont->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, label);
        const bool hasKey = keyHint && keyHint[0] != '\0';
        char keyBuf[32] = "";
        if (hasKey) std::snprintf(keyBuf, sizeof(keyBuf), "[%s]", keyHint);
        const ImVec2 keySize = hasKey ? boldFont->CalcTextSizeA(keyFontSize, FLT_MAX, 0.0f, keyBuf) : ImVec2(0.0f, 0.0f);
        const float padX = UITheme::S(20.0f);
        const float width = padX * 2.0f + labelSize.x + (hasKey ? UITheme::S(7.0f) + keySize.x : 0.0f);

        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const ImVec2 max(pos.x + width, pos.y + height);
        ImGui::PushID(label);
        const bool clicked = ImGui::InvisibleButton("##nav_tab", ImVec2(width, height));
        const bool hovered = ImGui::IsItemHovered();
        ImGui::PopID();

        ImDrawList* draw = ImGui::GetWindowDrawList();
        if (active)
        {
            draw->AddRectFilled(pos, max, IM_COL32(242, 239, 230, 18));
            draw->AddRectFilled(ImVec2(pos.x, max.y - UITheme::S(3.5f)), max, UITheme::Colors::AccentAmber);
        }
        else if (hovered)
        {
            draw->AddRectFilled(pos, max, IM_COL32(242, 239, 230, 11));
        }

        const ImU32 textCol = active  ? UITheme::Colors::TextPrimary
                            : hovered ? UITheme::Colors::TextPrimary
                                      : UITheme::Colors::TextSecondary;
        const float textY = pos.y + (height - labelSize.y) * 0.5f;
        draw->AddText(boldFont, fontSize, ImVec2(pos.x + padX, textY), textCol, label);

        if (hasKey)
        {
            const float kx = pos.x + padX + labelSize.x + UITheme::S(7.0f);
            const float ky = pos.y + (height - keySize.y) * 0.5f;
            draw->AddText(boldFont, keyFontSize, ImVec2(kx, ky),
                          active ? UITheme::Colors::AccentAmber : UITheme::Colors::TextMuted, keyBuf);
        }
        return clicked;
    }

    bool SegmentedButton(const char* label, bool active, const ImVec2& size)
    {
        ImGui::PushFont(UITheme::BoldFont(), UITheme::S(15.5f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, UITheme::S(6.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.3f);
        ImGui::PushStyleColor(ImGuiCol_Button, active
            ? UITheme::ColorVec4(UITheme::Colors::AccentAmber)
            : UITheme::ColorVec4(UITheme::Colors::SurfaceInset));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, active
            ? UITheme::ColorVec4(UITheme::Colors::AccentAmberHover)
            : UITheme::ColorVec4(UITheme::Colors::SurfaceCardHover));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, UITheme::ColorVec4(UITheme::Colors::AccentAmber));
        ImGui::PushStyleColor(ImGuiCol_Text, active
            ? UITheme::ColorVec4(UITheme::Colors::TextOnAccent)
            : UITheme::ColorVec4(UITheme::Colors::TextSecondary));
        ImGui::PushStyleColor(ImGuiCol_Border, active
            ? UITheme::ColorVec4(UITheme::Colors::AccentAmberHover)
            : UITheme::ColorVec4(UITheme::Colors::BorderTactical));
        const bool clicked = ImGui::Button(label, size);
        ImGui::PopStyleColor(5);
        ImGui::PopStyleVar(2);
        ImGui::PopFont();
        return clicked;
    }

    void DrawHazardDropZone(ImDrawList* draw, const ImVec2& min, const ImVec2& max,
                            const char* label, bool hovered, bool activeDrop)
    {
        if (!draw) return;
        const float w = max.x - min.x;
        const float h = max.y - min.y;
        if (w <= 0.0f || h <= 0.0f) return;

        const float r = UITheme::S(8.0f);
        draw->AddRectFilled(min, max, activeDrop ? UITheme::Colors::SlotInvalidDropBg : IM_COL32(28, 17, 17, 232), r);

        draw->PushClipRect(min, max, true);
        const float stripeW = UITheme::S(14.0f);
        const float totalSpan = w + h + stripeW * 2.0f;
        const ImU32 stripeCol = activeDrop ? IM_COL32(220, 76, 76, 86)
                              : hovered    ? IM_COL32(224, 162, 57, 46)
                                           : IM_COL32(195, 58, 58, 38);
        for (float x = -h; x < totalSpan; x += stripeW * 2.0f)
        {
            draw->AddQuadFilled(ImVec2(min.x + x, max.y), ImVec2(min.x + x + stripeW, max.y),
                                ImVec2(min.x + x + stripeW + h, min.y), ImVec2(min.x + x + h, min.y), stripeCol);
        }
        draw->PopClipRect();

        const ImU32 borderCol = activeDrop ? UITheme::Colors::AccentRose
                              : hovered    ? UITheme::Colors::AccentAmber
                                           : IM_COL32(220, 76, 76, 130);
        draw->AddRect(min, max, borderCol, r, 0, activeDrop ? 2.0f : 1.4f);

        if (label && label[0] != '\0')
        {
            ImFont* boldFont = UITheme::BoldFont();
            const float fontSize = UITheme::S(16.0f);
            const ImVec2 tSize = boldFont->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, label);
            const ImVec2 center((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
            const ImVec2 bMin(center.x - tSize.x * 0.5f - UITheme::S(12.0f), center.y - tSize.y * 0.5f - UITheme::S(5.0f));
            const ImVec2 bMax(center.x + tSize.x * 0.5f + UITheme::S(12.0f), center.y + tSize.y * 0.5f + UITheme::S(5.0f));
            draw->AddRectFilled(bMin, bMax, IM_COL32(18, 16, 15, 232), UITheme::S(6.0f));
            draw->AddRect(bMin, bMax, borderCol, UITheme::S(6.0f), 0, 1.2f);
            draw->AddText(boldFont, fontSize, ImVec2(center.x - tSize.x * 0.5f, center.y - tSize.y * 0.5f),
                          activeDrop ? UITheme::Colors::AccentRose : UITheme::Colors::TextPrimary, label);
        }
    }

    void DrawVitalsGauge(ImDrawList* draw, const ImVec2& pos, float width,
                         const char* label, float value01, ImU32 barColor,
                         const char* valStr, bool pulse)
    {
        if (!draw || width <= 0.0f) return;
        const float clamped = std::clamp(value01, 0.0f, 1.0f);
        const float barH = UITheme::S(8.0f);

        char defVal[32];
        if (!valStr)
        {
            std::snprintf(defVal, sizeof(defVal), "%.0f%%", clamped * 100.0f);
            valStr = defVal;
        }
        ImFont* boldFont = UITheme::BoldFont();
        const float fontSize = UITheme::S(15.0f);
        const ImVec2 vSize = boldFont->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, valStr);
        draw->AddText(boldFont, fontSize, pos, UITheme::Colors::TextSecondary, label);
        draw->AddText(boldFont, fontSize, ImVec2(pos.x + width - vSize.x, pos.y), UITheme::Colors::TextPrimary, valStr);

        const ImVec2 bMin(pos.x, pos.y + vSize.y + UITheme::S(4.0f));
        const ImVec2 bMax(pos.x + width, bMin.y + barH);
        DrawTacticalBar(draw, bMin, bMax, clamped, barColor, UITheme::Colors::SurfaceInset, pulse, UITheme::S(3.0f));
    }

    void DrawECGLine(ImDrawList*, const ImVec2&, const ImVec2&, float, ImU32)
    {
        // Intentionally quiet under Tactical Field Minimalism.
    }

    void DrawSlotSilhouette(ImDrawList* draw, const ImVec2& min, const ImVec2& max, const char* slotType)
    {
        if (!draw || !slotType) return;
        const ImVec2 c((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
        const float dim = std::min(max.x - min.x, max.y - min.y);
        const float u = dim * 0.42f;
        const ImU32 fill = IM_COL32(142, 152, 134, 135);
        const ImU32 outline = IM_COL32(204, 210, 194, 175);
        const ImU32 detail = IM_COL32(24, 30, 26, 175);
        const float stroke = std::max(1.4f, u * 0.08f);

        if (std::strcmp(slotType, "HEAD") == 0)
        {
            // Plump combat helmet with NVG bracket and brim
            draw->PathClear();
            draw->PathArcTo(ImVec2(c.x, c.y + u * 0.14f), u * 0.84f, 3.14159f, 6.28318f, 18);
            draw->PathLineTo(ImVec2(c.x + u * 0.92f, c.y + u * 0.44f));
            draw->PathLineTo(ImVec2(c.x - u * 0.92f, c.y + u * 0.44f));
            draw->PathFillConvex(fill);
            draw->AddRectFilled(ImVec2(c.x - u * 0.22f, c.y - u * 0.52f), ImVec2(c.x + u * 0.22f, c.y - u * 0.12f), outline, 2.0f);
            draw->AddLine(ImVec2(c.x - u * 0.90f, c.y + u * 0.44f), ImVec2(c.x + u * 0.90f, c.y + u * 0.44f), outline, stroke * 1.2f);
            draw->AddLine(ImVec2(c.x - u * 0.56f, c.y + u * 0.44f), ImVec2(c.x - u * 0.36f, c.y + u * 0.78f), outline, stroke);
            draw->AddLine(ImVec2(c.x + u * 0.56f, c.y + u * 0.44f), ImVec2(c.x + u * 0.36f, c.y + u * 0.78f), outline, stroke);
        }
        else if (std::strcmp(slotType, "TORSO") == 0)
        {
            // Tactical field jacket silhouette
            draw->AddRectFilled(ImVec2(c.x - u * 0.56f, c.y - u * 0.76f), ImVec2(c.x + u * 0.56f, c.y + u * 0.84f), fill, u * 0.18f);
            // Sleeves
            const ImVec2 lSleeve[4] = {ImVec2(c.x - u * 0.54f, c.y - u * 0.70f), ImVec2(c.x - u * 0.94f, c.y + u * 0.32f),
                                       ImVec2(c.x - u * 0.62f, c.y + u * 0.42f), ImVec2(c.x - u * 0.48f, c.y - u * 0.10f)};
            const ImVec2 rSleeve[4] = {ImVec2(c.x + u * 0.54f, c.y - u * 0.70f), ImVec2(c.x + u * 0.94f, c.y + u * 0.32f),
                                       ImVec2(c.x + u * 0.62f, c.y + u * 0.42f), ImVec2(c.x + u * 0.48f, c.y - u * 0.10f)};
            draw->AddConvexPolyFilled(lSleeve, 4, fill);
            draw->AddConvexPolyFilled(rSleeve, 4, fill);
            draw->AddLine(ImVec2(c.x, c.y - u * 0.68f), ImVec2(c.x, c.y + u * 0.82f), detail, stroke);
            draw->AddCircleFilled(ImVec2(c.x, c.y - u * 0.78f), u * 0.24f, detail, 12);
        }
        else if (std::strcmp(slotType, "VEST") == 0)
        {
            // Plate carrier vest with shoulder straps + 3 mag pouches
            draw->AddRectFilled(ImVec2(c.x - u * 0.66f, c.y - u * 0.48f), ImVec2(c.x + u * 0.66f, c.y + u * 0.80f), fill, u * 0.16f);
            draw->AddRectFilled(ImVec2(c.x - u * 0.56f, c.y - u * 0.84f), ImVec2(c.x - u * 0.26f, c.y - u * 0.40f), fill, 2.0f);
            draw->AddRectFilled(ImVec2(c.x + u * 0.26f, c.y - u * 0.84f), ImVec2(c.x + u * 0.56f, c.y - u * 0.40f), fill, 2.0f);
            draw->AddRect(ImVec2(c.x - u * 0.66f, c.y - u * 0.48f), ImVec2(c.x + u * 0.66f, c.y + u * 0.80f), outline, u * 0.16f, 0, stroke);
            for (int i = -1; i <= 1; ++i)
            {
                const float px = c.x + i * u * 0.36f;
                draw->AddRectFilled(ImVec2(px - u * 0.13f, c.y + u * 0.08f), ImVec2(px + u * 0.13f, c.y + u * 0.60f), detail, 2.0f);
            }
        }
        else if (std::strcmp(slotType, "LEGS") == 0)
        {
            // Flat cargo pants silhouette
            draw->AddRectFilled(ImVec2(c.x - u * 0.54f, c.y - u * 0.82f), ImVec2(c.x + u * 0.54f, c.y - u * 0.18f), fill, 3.0f);
            draw->AddRectFilled(ImVec2(c.x - u * 0.54f, c.y - u * 0.28f), ImVec2(c.x - u * 0.08f, c.y + u * 0.86f), fill, 3.0f);
            draw->AddRectFilled(ImVec2(c.x + u * 0.08f, c.y - u * 0.28f), ImVec2(c.x + u * 0.54f, c.y + u * 0.86f), fill, 3.0f);
            draw->AddLine(ImVec2(c.x - u * 0.50f, c.y - u * 0.62f), ImVec2(c.x + u * 0.50f, c.y - u * 0.62f), detail, stroke);
            draw->AddRectFilled(ImVec2(c.x - u * 0.60f, c.y + u * 0.02f), ImVec2(c.x - u * 0.44f, c.y + u * 0.40f), outline, 2.0f);
            draw->AddRectFilled(ImVec2(c.x + u * 0.44f, c.y + u * 0.02f), ImVec2(c.x + u * 0.60f, c.y + u * 0.40f), outline, 2.0f);
        }
        else if (std::strcmp(slotType, "BACKPACK") == 0)
        {
            // Flat assault backpack with top flap & front pouch
            draw->AddRectFilled(ImVec2(c.x - u * 0.24f, c.y - u * 0.92f), ImVec2(c.x + u * 0.24f, c.y - u * 0.68f), outline, 3.0f);
            draw->AddRectFilled(ImVec2(c.x - u * 0.64f, c.y - u * 0.72f), ImVec2(c.x + u * 0.64f, c.y + u * 0.84f), fill, u * 0.24f);
            draw->AddRect(ImVec2(c.x - u * 0.64f, c.y - u * 0.72f), ImVec2(c.x + u * 0.64f, c.y + u * 0.84f), outline, u * 0.24f, 0, stroke);
            draw->AddRectFilled(ImVec2(c.x - u * 0.44f, c.y + u * 0.02f), ImVec2(c.x + u * 0.44f, c.y + u * 0.66f), detail, u * 0.14f);
            draw->AddLine(ImVec2(c.x - u * 0.52f, c.y - u * 0.24f), ImVec2(c.x + u * 0.52f, c.y - u * 0.24f), outline, stroke);
        }
        else if (std::strcmp(slotType, "WEAPON") == 0)
        {
            // Clean flat AK-style rifle silhouette (matches Mockup 03)
            const float w = (max.x - min.x) * 0.42f;
            const float h = std::min((max.y - min.y) * 0.34f, w * 0.28f);
            // Stock
            const ImVec2 stock[4] = {ImVec2(c.x - w, c.y + h * 0.55f), ImVec2(c.x - w, c.y - h * 0.22f),
                                     ImVec2(c.x - w * 0.48f, c.y - h * 0.36f), ImVec2(c.x - w * 0.48f, c.y + h * 0.28f)};
            draw->AddConvexPolyFilled(stock, 4, fill);
            // Receiver & handguard
            draw->AddRectFilled(ImVec2(c.x - w * 0.48f, c.y - h * 0.42f), ImVec2(c.x + w * 0.42f, c.y + h * 0.18f), fill, 2.0f);
            // Barrel + front sight
            draw->AddRectFilled(ImVec2(c.x + w * 0.42f, c.y - h * 0.26f), ImVec2(c.x + w * 0.96f, c.y - h * 0.06f), outline, 1.0f);
            draw->AddRectFilled(ImVec2(c.x + w * 0.78f, c.y - h * 0.62f), ImVec2(c.x + w * 0.86f, c.y - h * 0.22f), outline, 1.0f);
            // Pistol grip
            const ImVec2 grip[4] = {ImVec2(c.x - w * 0.36f, c.y + h * 0.14f), ImVec2(c.x - w * 0.18f, c.y + h * 0.14f),
                                    ImVec2(c.x - w * 0.28f, c.y + h * 0.92f), ImVec2(c.x - w * 0.46f, c.y + h * 0.86f)};
            draw->AddConvexPolyFilled(grip, 4, fill);
            // Curved magazine
            const ImVec2 mag[4] = {ImVec2(c.x - w * 0.02f, c.y + h * 0.14f), ImVec2(c.x + w * 0.18f, c.y + h * 0.14f),
                                   ImVec2(c.x + w * 0.30f, c.y + h * 1.05f), ImVec2(c.x + w * 0.08f, c.y + h * 1.05f)};
            draw->AddConvexPolyFilled(mag, 4, outline);
        }
        else if (std::strcmp(slotType, "SIDEARM") == 0)
        {
            const float w = std::min((max.x - min.x) * 0.34f, (max.y - min.y) * 0.56f);
            const float h = w * 0.46f;
            draw->AddRectFilled(ImVec2(c.x - w * 0.72f, c.y - h * 0.55f), ImVec2(c.x + w * 0.82f, c.y + h * 0.08f), fill, 2.0f);
            const ImVec2 grip[4] = {ImVec2(c.x - w * 0.64f, c.y + h * 0.04f), ImVec2(c.x - w * 0.22f, c.y + h * 0.04f),
                                    ImVec2(c.x - w * 0.38f, c.y + h * 1.15f), ImVec2(c.x - w * 0.80f, c.y + h * 1.08f)};
            draw->AddConvexPolyFilled(grip, 4, outline);
        }
    }
}
