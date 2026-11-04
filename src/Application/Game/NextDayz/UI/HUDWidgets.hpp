#pragma once

#include <imgui.h>
#include <string_view>
#include "HUDTheme.hpp"

namespace NextDayz::UIWidgets
{
    // Renders a modern 3D tactical keyboard badge (e.g. "[ E ]", "[ F ]", "[ TAB ]")
    // Returns the total width consumed.
    float DrawKeyBadge(ImDrawList* draw, const ImVec2& pos, const char* keyText, float height = 24.0f);

    // Renders a smooth radial action progress circle around center (e.g. for bandaging / eating)
    void DrawRadialProgress(ImDrawList* draw, const ImVec2& center, float radius, float thickness,
                            float progress01, ImU32 arcColor = UITheme::Colors::AccentAmber,
                            ImU32 trackColor = IM_COL32(30, 36, 45, 180));

    // Renders a sleek modern tactical progress bar with segmented or smooth fill
    void DrawTacticalBar(ImDrawList* draw, const ImVec2& pMin, const ImVec2& pMax, float fraction,
                         ImU32 fillColor, ImU32 emptyColor = IM_COL32(25, 30, 40, 160),
                         bool pulse = false);

    // Renders a slot depression frame with explicit hover / drop feedback
    void DrawSlotFrame(ImDrawList* draw, const ImVec2& min, const ImVec2& max,
                       bool occupied, bool hovered, bool validDropTarget, bool invalidDropTarget,
                       float rounding = 3.0f);

    // Renders an affliction / status pill (e.g. "BLEEDING x2", "FREEZING")
    ImVec2 DrawStatusBadge(ImDrawList* draw, const ImVec2& pos, const char* icon, const char* text,
                           ImU32 bgColor, ImU32 borderColor, ImU32 textColor);
}
