#pragma once

#include <algorithm>
#include <cstdint>
#include <string_view>
#include <imgui.h>
#include "Application/Game/NextDayz/Data/ItemDefs.hpp"

namespace NextDayz::UITheme
{
    // ---- Shared Typography Registry (populated in NextDayzGameInstance::OnInitUI) ----
    struct FFontSet
    {
        ImFont* body = nullptr;     // 19px Roboto-Regular + DroidSansFallback (LightHinting)
        ImFont* bold = nullptr;     // 20px Roboto-BoldCondensed + Bold DroidSansFallback
        ImFont* heading = nullptr;  // 30px Roboto-BoldCondensed + Bold DroidSansFallback
        ImFont* numeral = nullptr;  // 58px Roboto-BoldCondensed (hero numerals & wordmark)
    };

    inline FFontSet& Fonts()
    {
        static FFontSet fonts;
        return fonts;
    }

    inline ImFont* BodyFont()    { return Fonts().body    ? Fonts().body    : ImGui::GetFont(); }
    inline ImFont* BoldFont()    { return Fonts().bold    ? Fonts().bold    : BodyFont(); }
    inline ImFont* HeadingFont() { return Fonts().heading ? Fonts().heading : BoldFont(); }
    inline ImFont* NumeralFont() { return Fonts().numeral ? Fonts().numeral : HeadingFont(); }

    // Responsive scale anchored to 1600x900 = 1.00, 1920x1080 = 1.20, 1280x720 = 0.86, 2560x1440 = 1.40.
    // Keeps 1080p crisp and roomy like ScadLibrary while preventing overflow on smaller 720p viewports.
    inline float UiScale()
    {
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        if (!vp || vp->WorkSize.x <= 0.0f || vp->WorkSize.y <= 0.0f) return 1.0f;
        return std::clamp(std::min(vp->WorkSize.x / 1600.0f, vp->WorkSize.y / 900.0f), 0.86f, 1.40f);
    }

    inline float S(float px) { return px * UiScale(); }

    // ---- Unified Palette: Flat Stylized Tactical Field Minimalism ----
    // Shared with assets/configs/nextdayz/ui_theme.json and web/nextdayz/src/style.css.
    namespace Colors
    {
        // Surfaces & Panels (warm olive-charcoal, clean flat matte layering)
        inline constexpr ImU32 BgScrim            = IM_COL32(10, 12, 11, 200);
        inline constexpr ImU32 SurfaceBg          = IM_COL32(20, 24, 22, 242);
        inline constexpr ImU32 SurfacePanel       = IM_COL32(22, 27, 24, 244);
        inline constexpr ImU32 SurfaceHeader      = IM_COL32(28, 34, 30, 248);
        inline constexpr ImU32 SurfaceCard        = IM_COL32(32, 39, 35, 242);
        inline constexpr ImU32 SurfaceCardHover   = IM_COL32(42, 51, 46, 248);
        inline constexpr ImU32 SurfaceInset       = IM_COL32(16, 20, 18, 236);

        inline constexpr ImU32 BorderMuted        = IM_COL32(232, 226, 208, 42);
        inline constexpr ImU32 BorderTactical     = IM_COL32(232, 226, 208, 88);
        inline constexpr ImU32 BorderFocused      = IM_COL32(232, 226, 208, 145);
        inline constexpr ImU32 BorderHighlight    = IM_COL32(224, 162, 57, 248);
        inline constexpr ImU32 CornerTick         = IM_COL32(224, 162, 57, 220);

        // Slots & Grid
        inline constexpr ImU32 SlotEmptyBg        = IM_COL32(17, 21, 19, 218);
        inline constexpr ImU32 SlotEmptyBorder    = IM_COL32(232, 226, 208, 36);
        inline constexpr ImU32 SlotOccupiedBg     = IM_COL32(33, 40, 36, 242);
        inline constexpr ImU32 SlotOccupiedBorder = IM_COL32(232, 226, 208, 76);
        inline constexpr ImU32 SlotHoverBorder    = IM_COL32(224, 162, 57, 248);
        inline constexpr ImU32 SlotValidDropBg    = IM_COL32(116, 179, 110, 74);
        inline constexpr ImU32 SlotValidDropBorder= IM_COL32(116, 179, 110, 245);
        inline constexpr ImU32 SlotInvalidDropBg  = IM_COL32(220, 76, 76, 74);
        inline constexpr ImU32 SlotInvalidDropBorder = IM_COL32(220, 76, 76, 245);
        inline constexpr ImU32 GridCrossDot       = IM_COL32(232, 226, 208, 52);

        // Accents & Signals (flat stylized tactical paint)
        inline constexpr ImU32 AccentAmber        = IM_COL32(224, 162, 57, 255);  // Vivid signal amber gold
        inline constexpr ImU32 AccentAmberHover   = IM_COL32(240, 182, 84, 255);  // Lit amber
        inline constexpr ImU32 AccentCyan         = IM_COL32(98, 180, 214, 255);  // Flat sky-steel water / optic
        inline constexpr ImU32 AccentEmerald      = IM_COL32(116, 179, 110, 255); // Flat sage-olive green
        inline constexpr ImU32 AccentRose         = IM_COL32(220, 76, 76, 255);   // Flat crimson danger
        inline constexpr ImU32 AccentIceBlue      = IM_COL32(166, 214, 234, 255); // Cold / frost
        inline constexpr ImU32 AccentOrange       = IM_COL32(223, 127, 60, 255);  // Weapon terracotta
        inline constexpr ImU32 AccentBrass        = IM_COL32(210, 176, 70, 255);  // Cartridge brass
        inline constexpr ImU32 AccentViolet       = IM_COL32(152, 135, 196, 255); // Soft gear slate-plum

        // World Atlas
        inline constexpr ImU32 AtlasOcean         = IM_COL32(13, 22, 22, 255);
        inline constexpr ImU32 AtlasLand          = IM_COL32(38, 56, 51, 255);
        inline constexpr ImU32 AtlasCoast         = IM_COL32(74, 105, 95, 255);
        inline constexpr ImU32 AtlasGrid          = IM_COL32(120, 152, 140, 42);

        // Hazard Stripes (for discard zone)
        inline constexpr ImU32 HazardYellow       = IM_COL32(220, 76, 76, 140);
        inline constexpr ImU32 HazardDark         = IM_COL32(16, 20, 18, 232);

        // Typography (high-contrast warm paper off-white)
        inline constexpr ImU32 TextPrimary        = IM_COL32(242, 239, 230, 255);
        inline constexpr ImU32 TextSecondary      = IM_COL32(184, 189, 176, 248);
        inline constexpr ImU32 TextMuted          = IM_COL32(136, 143, 130, 235);
        inline constexpr ImU32 TextHighlight      = IM_COL32(240, 182, 84, 255);
        inline constexpr ImU32 TextOnAccent       = IM_COL32(24, 21, 14, 255);

        // Key Badges
        inline constexpr ImU32 KeyCapBg           = IM_COL32(38, 45, 41, 255);
        inline constexpr ImU32 KeyCapBorder       = IM_COL32(232, 226, 208, 108);
        inline constexpr ImU32 KeyCapShadow       = IM_COL32(8, 10, 9, 240);
    }

    inline ImVec4 ColorVec4(ImU32 col)
    {
        return ImGui::ColorConvertU32ToFloat4(col);
    }

    // Scoped style helper so ImGui controls across lobby, world map, trader, inventory and settings
    // share the exact flat-stylized Tactical Field palette and crisp rounded geometry.
    struct FFieldStyleScope
    {
        FFieldStyleScope()
        {
            const float r = S(6.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, S(8.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, r);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, r);
            ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, r);
            ImGui::PushStyleVar(ImGuiStyleVar_TabRounding, r);
            ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarRounding, r);
            ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, S(4.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.5f);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.2f);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(10.0f), S(6.0f)));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(S(8.0f), S(7.0f)));

            ImGui::PushStyleColor(ImGuiCol_WindowBg, ColorVec4(Colors::SurfaceBg));
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
            ImGui::PushStyleColor(ImGuiCol_PopupBg, ColorVec4(Colors::SurfacePanel));
            ImGui::PushStyleColor(ImGuiCol_Border, ColorVec4(Colors::BorderMuted));
            ImGui::PushStyleColor(ImGuiCol_Separator, ColorVec4(Colors::BorderMuted));
            ImGui::PushStyleColor(ImGuiCol_Text, ColorVec4(Colors::TextPrimary));
            ImGui::PushStyleColor(ImGuiCol_TextDisabled, ColorVec4(Colors::TextMuted));
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ColorVec4(Colors::SurfaceInset));
            ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ColorVec4(Colors::SurfaceCard));
            ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ColorVec4(Colors::SurfaceCardHover));
            ImGui::PushStyleColor(ImGuiCol_Button, ColorVec4(Colors::SurfaceCard));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ColorVec4(Colors::SurfaceCardHover));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ColorVec4(Colors::AccentAmber));
            ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32(36, 44, 39, 245));
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ColorVec4(Colors::SurfaceCardHover));
            ImGui::PushStyleColor(ImGuiCol_HeaderActive, IM_COL32(224, 162, 57, 82));
            ImGui::PushStyleColor(ImGuiCol_CheckMark, ColorVec4(Colors::AccentAmber));
            ImGui::PushStyleColor(ImGuiCol_SliderGrab, ColorVec4(Colors::AccentAmber));
            ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ColorVec4(Colors::AccentAmberHover));
            ImGui::PushStyleColor(ImGuiCol_Tab, ColorVec4(Colors::SurfaceInset));
            ImGui::PushStyleColor(ImGuiCol_TabHovered, ColorVec4(Colors::SurfaceCardHover));
            ImGui::PushStyleColor(ImGuiCol_TabSelected, ColorVec4(Colors::SurfaceCard));
            ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, IM_COL32(10, 12, 11, 110));
            ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, IM_COL32(232, 226, 208, 62));
            ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, IM_COL32(224, 162, 57, 170));
            ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive, ColorVec4(Colors::AccentAmber));
        }

        ~FFieldStyleScope()
        {
            ImGui::PopStyleColor(26);
            ImGui::PopStyleVar(11);
        }

        FFieldStyleScope(const FFieldStyleScope&) = delete;
        FFieldStyleScope& operator=(const FFieldStyleScope&) = delete;
    };

    // Category accent bar color for items (matches ui_theme.json)
    inline ImU32 ItemKindColor(EItemKind kind)
    {
        switch (kind)
        {
        case EItemKind::Weapon:     return Colors::AccentOrange;
        case EItemKind::Melee:      return IM_COL32(204, 106, 48, 250);
        case EItemKind::Ammo:       return Colors::AccentBrass;
        case EItemKind::Attachment: return Colors::AccentCyan;
        case EItemKind::Clothing:   return Colors::AccentViolet;
        case EItemKind::Consumable: return Colors::AccentEmerald;
        case EItemKind::Misc:
        default:                    return IM_COL32(158, 166, 152, 235);
        }
    }

    // Category short badge name
    inline const char* ItemKindBadge(EItemKind kind)
    {
        switch (kind)
        {
        case EItemKind::Weapon:     return "WEAPON";
        case EItemKind::Melee:      return "MELEE";
        case EItemKind::Ammo:       return "AMMO";
        case EItemKind::Attachment: return "ATTACHMENT";
        case EItemKind::Clothing:   return "GEAR";
        case EItemKind::Consumable: return "SUPPLY";
        case EItemKind::Misc:
        default:                    return "ITEM";
        }
    }
}
