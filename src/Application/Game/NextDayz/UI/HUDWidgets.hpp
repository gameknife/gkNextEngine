#pragma once

#include <imgui.h>
#include <string_view>
#include "HUDTheme.hpp"

namespace NextDayz::UIWidgets
{
    enum class EVitalIcon : uint8_t
    {
        Health,
        Hunger,
        Hydration,
        Bleeding,
        Cold,
        Healing,
        Weak,
        Lantern,
        Armor,
        Compass,
        Map,
        Opener,
        Gear,
        Coin,
    };

    // Renders a crisp physical keyboard badge (e.g. "[E]", "[TAB]") and returns consumed width.
    float DrawKeyBadge(ImDrawList* draw, const ImVec2& pos, const char* keyText, float height = 24.0f);

    // Renders a smooth radial action progress circle around center (e.g. bandaging / eating / extracting).
    void DrawRadialProgress(ImDrawList* draw, const ImVec2& center, float radius, float thickness,
                            float progress01, ImU32 arcColor = UITheme::Colors::AccentAmber,
                            ImU32 trackColor = IM_COL32(14, 17, 15, 210));

    // Renders a crisp flat-stylized rounded progress bar with subtle top highlight.
    void DrawTacticalBar(ImDrawList* draw, const ImVec2& pMin, const ImVec2& pMax, float fraction,
                         ImU32 fillColor, ImU32 emptyColor = IM_COL32(15, 19, 17, 215),
                         bool pulse = false, float rounding = 4.0f);

    // Renders a chunky flat-stylized inline vital bar (icon + label + % overlaid inside the bar).
    void DrawInlineVitalBar(ImDrawList* draw, const ImVec2& pMin, const ImVec2& pMax,
                            EVitalIcon icon, const char* label, float value01,
                            ImU32 fillColor, const char* valStr = nullptr, bool pulse = false);

    // Renders a recessed or occupied item slot frame.
    void DrawSlotFrame(ImDrawList* draw, const ImVec2& min, const ImVec2& max,
                       bool occupied, bool hovered, bool validDropTarget, bool invalidDropTarget,
                       float rounding = 6.0f);

    // Renders a plump flat-stylized vector icon (avoids missing emoji glyphs in ImGui fonts).
    void DrawVitalIcon(ImDrawList* draw, const ImVec2& center, float size, EVitalIcon icon, ImU32 color);

    // Renders an affliction / status pill with an optional vector icon.
    ImVec2 DrawStatusBadge(ImDrawList* draw, const ImVec2& pos, const char* icon, const char* text,
                           ImU32 bgColor, ImU32 borderColor, ImU32 textColor);
    ImVec2 DrawStatusPill(ImDrawList* draw, const ImVec2& pos, EVitalIcon icon, const char* text,
                          ImU32 bgColor, ImU32 borderColor, ImU32 textColor, bool pulse = false);

    // Renders subtle 4-corner reticle ticks around a box.
    void DrawCornerTicks(ImDrawList* draw, const ImVec2& min, const ImVec2& max,
                         float tickLen = 6.0f, ImU32 color = UITheme::Colors::BorderTactical);

    // Renders a unified Tactical Field panel with optional title header.
    void DrawTacticalPanel(ImDrawList* draw, const ImVec2& min, const ImVec2& max,
                           const char* title = nullptr, const char* badge = nullptr,
                           bool cornerTicks = false);

    // Renders a flat-stylized rounded card surface with soft shadow and optional left accent stripe.
    void DrawFieldCard(ImDrawList* draw, const ImVec2& min, const ImVec2& max,
                       ImU32 leftAccent = 0, bool selected = false, bool hovered = false);

    // Renders a bold section header bar with rounded vertical accent tick and optional right badge.
    void DrawSectionHeader(const char* title, const char* rightBadge = nullptr,
                           ImU32 accentColor = UITheme::Colors::AccentAmber);

    // Renders crisp drop-shadowed text for frameless HUD elements.
    void DrawTextShadow(ImDrawList* draw, ImFont* font, float fontSize, const ImVec2& pos,
                        ImU32 color, const char* text, float shadowOffset = 1.6f);
    void DrawTextShadow(ImDrawList* draw, const ImVec2& pos, ImU32 color, const char* text);

    // Interactive Tactical Field buttons (primary amber CTA, ghost tactical button, top nav tab, segmented tab).
    bool PrimaryButton(const char* label, const ImVec2& size = ImVec2(0.0f, 44.0f), bool enabled = true, bool hero = false);
    bool GhostButton(const char* label, const ImVec2& size = ImVec2(0.0f, 38.0f), bool active = false, bool danger = false);
    bool NavTabButton(const char* label, const char* keyHint, bool active, float height = 64.0f);
    bool SegmentedButton(const char* label, bool active, const ImVec2& size);

    // Renders a hazard discard / drop zone with angled red warning stripes.
    void DrawHazardDropZone(ImDrawList* draw, const ImVec2& min, const ImVec2& max,
                            const char* label, bool hovered, bool activeDrop);

    // Renders a compact vitals gauge meter (label, bar, value).
    void DrawVitalsGauge(ImDrawList* draw, const ImVec2& pos, float width,
                         const char* label, float value01, ImU32 barColor,
                         const char* valStr = nullptr, bool pulse = false);

    // Kept for backward compatibility; no longer rendered in the unified minimalist inventory.
    void DrawECGLine(ImDrawList* draw, const ImVec2& pos, const ImVec2& size,
                     float bpm, ImU32 color = UITheme::Colors::AccentRose);

    // Renders a plump flat-stylized silhouette for an empty equipment or weapon slot.
    void DrawSlotSilhouette(ImDrawList* draw, const ImVec2& min, const ImVec2& max, const char* slotType);
}
