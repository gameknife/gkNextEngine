#include "HUDWidgets.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace NextDayz::UIWidgets
{
    namespace
    {
        ImU32 PulseColor(ImU32 color, float speed = 7.0f, float minAlpha = 0.55f)
        {
            const float t = static_cast<float>(ImGui::GetTime());
            const float wave = minAlpha + (1.0f - minAlpha) * (0.5f + 0.5f * std::sin(t * speed));
            const int baseA = static_cast<int>((color >> IM_COL32_A_SHIFT) & 0xFF);
            const int a = static_cast<int>(baseA * wave);
            return (color & ~IM_COL32_A_MASK) | (static_cast<ImU32>(a) << IM_COL32_A_SHIFT);
        }
    }

    float DrawKeyBadge(ImDrawList* draw, const ImVec2& pos, const char* keyText, float height)
    {
        if (!draw || !keyText) return 0.0f;

        const ImVec2 textSize = ImGui::CalcTextSize(keyText);
        const float paddingX = 6.0f;
        const float width = std::max(height, textSize.x + paddingX * 2.0f);
        const float rounding = 2.0f;

        const ImVec2 min = pos;
        const ImVec2 max(pos.x + width, pos.y + height);

        draw->AddRectFilled(ImVec2(min.x, min.y + 2.0f), ImVec2(max.x, max.y + 2.0f),
                            UITheme::Colors::KeyCapShadow, rounding);
        draw->AddRectFilled(min, max, UITheme::Colors::KeyCapBg, rounding);
        draw->AddLine(ImVec2(min.x + 1.0f, min.y + 1.0f), ImVec2(max.x - 1.0f, min.y + 1.0f),
                      IM_COL32(228, 224, 208, 45), 1.0f);
        draw->AddRect(min, max, UITheme::Colors::KeyCapBorder, rounding, 0, 1.0f);

        const ImVec2 textPos(min.x + (width - textSize.x) * 0.5f, min.y + (height - textSize.y) * 0.5f);
        draw->AddText(textPos, UITheme::Colors::TextPrimary, keyText);
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

        draw->AddCircleFilled(center, radius - thickness * 0.5f, IM_COL32(12, 15, 13, 175), kSegments);
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
                         ImU32 fillColor, ImU32 emptyColor, bool pulse)
    {
        if (!draw) return;
        const float width = pMax.x - pMin.x;
        const float height = pMax.y - pMin.y;
        if (width <= 0.0f || height <= 0.0f) return;

        const float clamped = std::clamp(fraction, 0.0f, 1.0f);
        draw->AddRectFilled(pMin, pMax, emptyColor, 1.0f);
        if (clamped > 0.002f)
        {
            const ImU32 col = pulse ? PulseColor(fillColor, 7.0f, 0.55f) : fillColor;
            draw->AddRectFilled(pMin, ImVec2(pMin.x + width * clamped, pMax.y), col, 1.0f);
        }
        draw->AddRect(pMin, pMax, IM_COL32(228, 224, 208, 34), 1.0f, 0, 1.0f);
    }

    void DrawSlotFrame(ImDrawList* draw, const ImVec2& min, const ImVec2& max,
                       bool occupied, bool hovered, bool validDropTarget, bool invalidDropTarget,
                       float rounding)
    {
        if (!draw) return;
        ImU32 bg = occupied ? UITheme::Colors::SlotOccupiedBg : UITheme::Colors::SlotEmptyBg;
        ImU32 border = occupied ? UITheme::Colors::SlotOccupiedBorder : UITheme::Colors::SlotEmptyBorder;
        float borderWidth = 1.0f;

        if (invalidDropTarget)
        {
            bg = UITheme::Colors::SlotInvalidDropBg;
            border = UITheme::Colors::SlotInvalidDropBorder;
            borderWidth = 1.8f;
        }
        else if (validDropTarget)
        {
            bg = UITheme::Colors::SlotValidDropBg;
            border = UITheme::Colors::SlotValidDropBorder;
            borderWidth = 1.8f;
        }
        else if (hovered)
        {
            border = UITheme::Colors::SlotHoverBorder;
            borderWidth = 1.4f;
        }

        draw->AddRectFilled(min, max, bg, rounding);
        draw->AddRect(min, max, border, rounding, 0, borderWidth);
    }

    void DrawVitalIcon(ImDrawList* draw, const ImVec2& c, float s, EVitalIcon icon, ImU32 col)
    {
        if (!draw) return;
        const float r = s * 0.45f;
        switch (icon)
        {
        case EVitalIcon::Health:
        case EVitalIcon::Healing:
        {
            const float arm = r * 0.36f;
            draw->AddRectFilled(ImVec2(c.x - arm, c.y - r), ImVec2(c.x + arm, c.y + r), col, 1.0f);
            draw->AddRectFilled(ImVec2(c.x - r, c.y - arm), ImVec2(c.x + r, c.y + arm), col, 1.0f);
            break;
        }
        case EVitalIcon::Hunger:
        {
            // Minimalist ration / grain diamond-bowl icon
            draw->AddTriangleFilled(ImVec2(c.x, c.y - r), ImVec2(c.x + r * 0.85f, c.y),
                                    ImVec2(c.x - r * 0.85f, c.y), col);
            draw->AddRectFilled(ImVec2(c.x - r * 0.72f, c.y + r * 0.15f),
                                ImVec2(c.x + r * 0.72f, c.y + r * 0.82f), col, 1.0f);
            break;
        }
        case EVitalIcon::Hydration:
        case EVitalIcon::Bleeding:
        {
            // Crisp teardrop
            draw->AddTriangleFilled(ImVec2(c.x, c.y - r * 1.05f), ImVec2(c.x + r * 0.78f, c.y + r * 0.18f),
                                    ImVec2(c.x - r * 0.78f, c.y + r * 0.18f), col);
            draw->AddCircleFilled(ImVec2(c.x, c.y + r * 0.28f), r * 0.72f, col, 14);
            break;
        }
        case EVitalIcon::Cold:
        {
            // Crisp 6-spoke frost asterisk
            draw->AddLine(ImVec2(c.x - r, c.y), ImVec2(c.x + r, c.y), col, 1.4f);
            draw->AddLine(ImVec2(c.x, c.y - r), ImVec2(c.x, c.y + r), col, 1.4f);
            const float d = r * 0.7f;
            draw->AddLine(ImVec2(c.x - d, c.y - d), ImVec2(c.x + d, c.y + d), col, 1.2f);
            draw->AddLine(ImVec2(c.x + d, c.y - d), ImVec2(c.x - d, c.y + d), col, 1.2f);
            break;
        }
        case EVitalIcon::Weak:
        {
            // Warning triangle
            draw->AddTriangle(ImVec2(c.x, c.y - r), ImVec2(c.x + r, c.y + r * 0.85f),
                              ImVec2(c.x - r, c.y + r * 0.85f), col, 1.6f);
            draw->AddLine(ImVec2(c.x, c.y - r * 0.35f), ImVec2(c.x, c.y + r * 0.25f), col, 1.6f);
            draw->AddCircleFilled(ImVec2(c.x, c.y + r * 0.52f), 1.2f, col, 8);
            break;
        }
        case EVitalIcon::Lantern:
        {
            draw->AddCircle(c, r * 0.6f, col, 12, 1.5f);
            draw->AddCircleFilled(c, r * 0.28f, col, 10);
            break;
        }
        case EVitalIcon::Armor:
        {
            const ImVec2 shield[5] = {
                ImVec2(c.x, c.y - r),
                ImVec2(c.x + r * 0.85f, c.y - r * 0.55f),
                ImVec2(c.x + r * 0.68f, c.y + r * 0.45f),
                ImVec2(c.x, c.y + r),
                ImVec2(c.x - r * 0.68f, c.y + r * 0.45f),
            };
            draw->AddConvexPolyFilled(shield, 5, col);
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

        const ImVec2 textSize = ImGui::CalcTextSize(fullLabel);
        const float padX = 9.0f;
        const float padY = 4.0f;
        const ImVec2 size(textSize.x + padX * 2.0f, textSize.y + padY * 2.0f);
        const ImVec2 max(pos.x + size.x, pos.y + size.y);

        draw->AddRectFilled(pos, max, bgColor, 2.0f);
        draw->AddRect(pos, max, borderColor, 2.0f, 0, 1.0f);
        draw->AddText(ImVec2(pos.x + padX, pos.y + padY), textColor, fullLabel);
        return size;
    }

    ImVec2 DrawStatusPill(ImDrawList* draw, const ImVec2& pos, EVitalIcon icon, const char* text,
                          ImU32 bgColor, ImU32 borderColor, ImU32 textColor, bool pulse)
    {
        if (!draw || !text) return ImVec2(0.0f, 0.0f);
        const ImVec2 textSize = ImGui::CalcTextSize(text);
        const float iconBox = 14.0f;
        const float padX = 8.0f;
        const float padY = 4.0f;
        const ImVec2 size(padX + iconBox + 5.0f + textSize.x + padX, textSize.y + padY * 2.0f);
        const ImVec2 max(pos.x + size.x, pos.y + size.y);

        const ImU32 bg = pulse ? PulseColor(bgColor, 6.5f, 0.65f) : bgColor;
        const ImU32 border = pulse ? PulseColor(borderColor, 6.5f, 0.65f) : borderColor;
        draw->AddRectFilled(pos, max, bg, 2.0f);
        draw->AddRect(pos, max, border, 2.0f, 0, 1.0f);
        DrawVitalIcon(draw, ImVec2(pos.x + padX + iconBox * 0.45f, pos.y + size.y * 0.5f), 11.0f, icon, textColor);
        draw->AddText(ImVec2(pos.x + padX + iconBox + 4.0f, pos.y + padY), textColor, text);
        return size;
    }

    void DrawCornerTicks(ImDrawList* draw, const ImVec2& min, const ImVec2& max, float tickLen, ImU32 color)
    {
        if (!draw) return;
        const float len = std::min(tickLen, std::min((max.x - min.x) * 0.35f, (max.y - min.y) * 0.35f));
        if (len <= 0.0f) return;

        draw->AddLine(min, ImVec2(min.x + len, min.y), color, 1.2f);
        draw->AddLine(min, ImVec2(min.x, min.y + len), color, 1.2f);
        draw->AddLine(ImVec2(max.x, min.y), ImVec2(max.x - len, min.y), color, 1.2f);
        draw->AddLine(ImVec2(max.x, min.y), ImVec2(max.x, min.y + len), color, 1.2f);
        draw->AddLine(ImVec2(min.x, max.y), ImVec2(min.x + len, max.y), color, 1.2f);
        draw->AddLine(ImVec2(min.x, max.y), ImVec2(min.x, max.y - len), color, 1.2f);
        draw->AddLine(max, ImVec2(max.x - len, max.y), color, 1.2f);
        draw->AddLine(max, ImVec2(max.x, max.y - len), color, 1.2f);
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
            draw->AddRectFilled(ImVec2(min.x, min.y), ImVec2(min.x + 3.0f, hMax.y), UITheme::Colors::AccentAmber);
            draw->AddText(ImVec2(min.x + 11.0f, min.y + 5.0f), UITheme::Colors::TextPrimary, title);

            if (badge && badge[0] != '\0')
            {
                const ImVec2 bSize = ImGui::CalcTextSize(badge);
                draw->AddText(ImVec2(max.x - bSize.x - 10.0f, min.y + 5.0f), UITheme::Colors::AccentAmber, badge);
            }
        }
        if (cornerTicks) DrawCornerTicks(draw, min, max, 6.0f, UITheme::Colors::BorderTactical);
    }

    void DrawFieldCard(ImDrawList* draw, const ImVec2& min, const ImVec2& max,
                       ImU32 leftAccent, bool selected, bool hovered)
    {
        if (!draw) return;
        const ImU32 bg = selected ? IM_COL32(33, 39, 34, 245)
                       : hovered  ? UITheme::Colors::SurfaceCardHover
                                  : UITheme::Colors::SurfaceCard;
        const ImU32 border = selected ? UITheme::Colors::BorderHighlight
                           : hovered  ? UITheme::Colors::BorderTactical
                                      : UITheme::Colors::BorderMuted;
        draw->AddRectFilled(min, max, bg, 2.0f);
        draw->AddRect(min, max, border, 2.0f, 0, 1.0f);
        if (leftAccent != 0)
        {
            draw->AddRectFilled(min, ImVec2(min.x + 3.0f, max.y), leftAccent, 1.0f);
        }
    }

    void DrawSectionHeader(const char* title, const char* rightBadge, ImU32 accentColor)
    {
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const float w = ImGui::GetContentRegionAvail().x;
        const float h = 24.0f;
        const ImVec2 max(pos.x + w, pos.y + h);
        ImDrawList* draw = ImGui::GetWindowDrawList();

        draw->AddRectFilled(pos, max, UITheme::Colors::SurfaceHeader, 2.0f);
        draw->AddRectFilled(pos, ImVec2(pos.x + 3.0f, max.y), accentColor, 1.0f);
        draw->AddLine(ImVec2(pos.x, max.y), max, UITheme::Colors::BorderMuted, 1.0f);
        if (title)
        {
            draw->AddText(ImVec2(pos.x + 10.0f, pos.y + (h - ImGui::GetTextLineHeight()) * 0.5f),
                          UITheme::Colors::TextPrimary, title);
        }
        if (rightBadge && rightBadge[0] != '\0')
        {
            const ImVec2 bSize = ImGui::CalcTextSize(rightBadge);
            draw->AddText(ImVec2(max.x - bSize.x - 8.0f, pos.y + (h - bSize.y) * 0.5f),
                          UITheme::Colors::AccentAmber, rightBadge);
        }
        ImGui::Dummy(ImVec2(w, h + 4.0f));
    }

    void DrawTextShadow(ImDrawList* draw, ImFont* font, float fontSize, const ImVec2& pos,
                        ImU32 color, const char* text, float shadowOffset)
    {
        if (!draw || !text) return;
        const int alpha = static_cast<int>((color >> IM_COL32_A_SHIFT) & 0xFF);
        const ImU32 shadow = IM_COL32(6, 8, 7, static_cast<int>(alpha * 0.78f));
        draw->AddText(font, fontSize, ImVec2(pos.x + shadowOffset, pos.y + shadowOffset), shadow, text);
        draw->AddText(font, fontSize, pos, color, text);
    }

    void DrawTextShadow(ImDrawList* draw, const ImVec2& pos, ImU32 color, const char* text)
    {
        DrawTextShadow(draw, ImGui::GetFont(), ImGui::GetFontSize(), pos, color, text, 1.2f);
    }

    bool PrimaryButton(const char* label, const ImVec2& size, bool enabled)
    {
        ImGui::BeginDisabled(!enabled);
        ImGui::PushStyleColor(ImGuiCol_Button, UITheme::ColorVec4(UITheme::Colors::AccentAmber));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, UITheme::ColorVec4(UITheme::Colors::AccentAmberHover));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(194, 134, 42, 255));
        ImGui::PushStyleColor(ImGuiCol_Text, UITheme::ColorVec4(UITheme::Colors::TextOnAccent));
        ImGui::PushStyleColor(ImGuiCol_Border, UITheme::ColorVec4(UITheme::Colors::AccentAmberHover));
        const bool pressed = ImGui::Button(label, size);
        ImGui::PopStyleColor(5);
        ImGui::EndDisabled();
        return pressed;
    }

    bool GhostButton(const char* label, const ImVec2& size, bool active, bool danger)
    {
        const ImU32 bg = active ? IM_COL32(36, 43, 38, 245) : IM_COL32(22, 27, 24, 215);
        const ImU32 hover = danger ? IM_COL32(72, 28, 28, 240) : IM_COL32(37, 44, 40, 245);
        const ImU32 border = active ? UITheme::Colors::BorderHighlight
                           : danger ? IM_COL32(214, 69, 69, 130)
                                    : UITheme::Colors::BorderTactical;
        const ImU32 text = danger ? IM_COL32(236, 152, 152, 255) : UITheme::Colors::TextPrimary;

        ImGui::PushStyleColor(ImGuiCol_Button, UITheme::ColorVec4(bg));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, UITheme::ColorVec4(hover));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, UITheme::ColorVec4(UITheme::Colors::AccentAmber));
        ImGui::PushStyleColor(ImGuiCol_Border, UITheme::ColorVec4(border));
        ImGui::PushStyleColor(ImGuiCol_Text, UITheme::ColorVec4(text));
        const bool pressed = ImGui::Button(label, size);
        ImGui::PopStyleColor(5);
        return pressed;
    }

    bool NavTabButton(const char* label, const char* keyHint, bool active, float height)
    {
        const ImVec2 labelSize = ImGui::CalcTextSize(label);
        const bool hasKey = keyHint && keyHint[0] != '\0';
        const ImVec2 keySize = hasKey ? ImGui::CalcTextSize(keyHint) : ImVec2(0.0f, 0.0f);
        const float padX = 16.0f;
        const float width = padX * 2.0f + labelSize.x + (hasKey ? 8.0f + keySize.x + 10.0f : 0.0f);

        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const ImVec2 max(pos.x + width, pos.y + height);
        ImGui::PushID(label);
        const bool clicked = ImGui::InvisibleButton("##nav_tab", ImVec2(width, height));
        const bool hovered = ImGui::IsItemHovered();
        ImGui::PopID();

        ImDrawList* draw = ImGui::GetWindowDrawList();
        if (active)
        {
            draw->AddRectFilled(pos, max, IM_COL32(236, 234, 224, 14));
            draw->AddRectFilled(ImVec2(pos.x, max.y - 2.5f), max, UITheme::Colors::AccentAmber);
        }
        else if (hovered)
        {
            draw->AddRectFilled(pos, max, IM_COL32(236, 234, 224, 10));
        }

        const ImU32 textCol = active ? UITheme::Colors::TextPrimary
                            : hovered ? UITheme::Colors::TextPrimary
                                      : UITheme::Colors::TextSecondary;
        const float textY = pos.y + (height - labelSize.y) * 0.5f;
        draw->AddText(ImVec2(pos.x + padX, textY), textCol, label);

        if (hasKey)
        {
            const float kx = pos.x + padX + labelSize.x + 8.0f;
            const float kh = labelSize.y + 2.0f;
            const float ky = pos.y + (height - kh) * 0.5f;
            draw->AddRectFilled(ImVec2(kx, ky), ImVec2(kx + keySize.x + 10.0f, ky + kh),
                                IM_COL32(236, 234, 224, 16), 2.0f);
            draw->AddText(ImVec2(kx + 5.0f, ky + 1.0f),
                          active ? UITheme::Colors::AccentAmber : UITheme::Colors::TextMuted, keyHint);
        }
        return clicked;
    }

    bool SegmentedButton(const char* label, bool active, const ImVec2& size)
    {
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
            : UITheme::ColorVec4(UITheme::Colors::BorderMuted));
        const bool clicked = ImGui::Button(label, size);
        ImGui::PopStyleColor(5);
        return clicked;
    }

    void DrawHazardDropZone(ImDrawList* draw, const ImVec2& min, const ImVec2& max,
                            const char* label, bool hovered, bool activeDrop)
    {
        if (!draw) return;
        const float w = max.x - min.x;
        const float h = max.y - min.y;
        if (w <= 0.0f || h <= 0.0f) return;

        draw->AddRectFilled(min, max, activeDrop ? UITheme::Colors::SlotInvalidDropBg : UITheme::Colors::SurfaceInset, 2.0f);

        draw->PushClipRect(min, max, true);
        const float stripeW = 12.0f;
        const float totalSpan = w + h + stripeW * 2.0f;
        const ImU32 stripeCol = activeDrop ? IM_COL32(214, 69, 69, 68)
                              : hovered    ? IM_COL32(217, 155, 56, 38)
                                           : IM_COL32(214, 69, 69, 24);
        for (float x = -h; x < totalSpan; x += stripeW * 2.0f)
        {
            draw->AddQuadFilled(ImVec2(min.x + x, max.y), ImVec2(min.x + x + stripeW, max.y),
                                ImVec2(min.x + x + stripeW + h, min.y), ImVec2(min.x + x + h, min.y), stripeCol);
        }
        draw->PopClipRect();

        const ImU32 borderCol = activeDrop ? UITheme::Colors::AccentRose
                              : hovered    ? UITheme::Colors::AccentAmber
                                           : IM_COL32(214, 69, 69, 110);
        draw->AddRect(min, max, borderCol, 2.0f, 0, activeDrop ? 1.8f : 1.0f);

        if (label && label[0] != '\0')
        {
            const ImVec2 tSize = ImGui::CalcTextSize(label);
            const ImVec2 center((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
            const ImVec2 bMin(center.x - tSize.x * 0.5f - 10.0f, center.y - tSize.y * 0.5f - 4.0f);
            const ImVec2 bMax(center.x + tSize.x * 0.5f + 10.0f, center.y + tSize.y * 0.5f + 4.0f);
            draw->AddRectFilled(bMin, bMax, IM_COL32(14, 17, 15, 228), 2.0f);
            draw->AddRect(bMin, bMax, borderCol, 2.0f, 0, 1.0f);
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
        const float barH = 6.0f;

        char defVal[32];
        if (!valStr)
        {
            std::snprintf(defVal, sizeof(defVal), "%.0f%%", clamped * 100.0f);
            valStr = defVal;
        }
        const ImVec2 vSize = ImGui::CalcTextSize(valStr);
        draw->AddText(pos, UITheme::Colors::TextSecondary, label);
        draw->AddText(ImVec2(pos.x + width - vSize.x, pos.y), UITheme::Colors::TextPrimary, valStr);

        const ImVec2 bMin(pos.x, pos.y + ImGui::GetTextLineHeight() + 3.0f);
        const ImVec2 bMax(pos.x + width, bMin.y + barH);
        draw->AddRectFilled(bMin, bMax, UITheme::Colors::SurfaceInset, 1.0f);
        draw->AddRect(bMin, bMax, UITheme::Colors::BorderMuted, 1.0f, 0, 1.0f);

        if (clamped > 0.005f)
        {
            const ImU32 fillCol = pulse ? PulseColor(barColor, 8.0f, 0.55f) : barColor;
            const float fillW = (width - 2.0f) * clamped;
            draw->AddRectFilled(ImVec2(bMin.x + 1.0f, bMin.y + 1.0f),
                                ImVec2(bMin.x + 1.0f + fillW, bMax.y - 1.0f), fillCol, 1.0f);
        }
    }

    void DrawECGLine(ImDrawList*, const ImVec2&, const ImVec2&, float, ImU32)
    {
        // Intentionally quiet under Tactical Field Minimalism.
    }

    void DrawSlotSilhouette(ImDrawList* draw, const ImVec2& min, const ImVec2& max, const char* slotType)
    {
        if (!draw || !slotType) return;
        const ImVec2 center((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
        const ImU32 col = IM_COL32(159, 164, 152, 72);
        constexpr float stroke = 1.2f;

        if (std::strcmp(slotType, "HEAD") == 0)
        {
            draw->AddCircle(ImVec2(center.x, center.y - 2.0f), 10.0f, col, 16, stroke);
            draw->AddLine(ImVec2(center.x - 11.0f, center.y + 4.0f), ImVec2(center.x + 11.0f, center.y + 4.0f), col, stroke);
            draw->AddLine(ImVec2(center.x - 7.0f, center.y + 8.0f), ImVec2(center.x + 7.0f, center.y + 8.0f), col, stroke);
        }
        else if (std::strcmp(slotType, "TORSO") == 0 || std::strcmp(slotType, "VEST") == 0)
        {
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
            const float w = 18.0f, h = 20.0f;
            draw->AddRect(ImVec2(center.x - w * 0.5f, center.y - h * 0.5f),
                          ImVec2(center.x + w * 0.5f, center.y + h * 0.5f), col, 2.0f, 0, stroke);
            draw->AddRect(ImVec2(center.x - w * 0.35f, center.y - 2.0f),
                          ImVec2(center.x + w * 0.35f, center.y + h * 0.45f), col, 1.0f, 0, stroke);
        }
        else if (std::strcmp(slotType, "WEAPON") == 0 || std::strcmp(slotType, "SIDEARM") == 0)
        {
            draw->AddLine(ImVec2(center.x - 16.0f, center.y), ImVec2(center.x + 14.0f, center.y), col, stroke);
            draw->AddLine(ImVec2(center.x - 8.0f, center.y), ImVec2(center.x - 11.0f, center.y + 9.0f), col, stroke);
            draw->AddLine(ImVec2(center.x - 1.0f, center.y), ImVec2(center.x - 3.0f, center.y + 7.0f), col, stroke);
        }
    }
}
