#pragma once

#include <cstdint>
#include <string_view>
#include <imgui.h>
#include "Application/Game/NextDayz/Data/ItemDefs.hpp"

namespace NextDayz::UITheme
{
    // ---- Unified Palette: Tactical Field Minimalism ----
    // Shared with assets/configs/nextdayz/ui_theme.json and web/nextdayz/src/style.css.
    namespace Colors
    {
        // Surfaces & Panels (warm olive-charcoal, zero cold sci-fi blue)
        inline constexpr ImU32 BgScrim            = IM_COL32(10, 12, 11, 196);
        inline constexpr ImU32 SurfaceBg          = IM_COL32(17, 20, 18, 238);
        inline constexpr ImU32 SurfacePanel       = IM_COL32(18, 22, 20, 240);
        inline constexpr ImU32 SurfaceHeader      = IM_COL32(22, 26, 23, 246);
        inline constexpr ImU32 SurfaceCard        = IM_COL32(28, 33, 30, 235);
        inline constexpr ImU32 SurfaceCardHover   = IM_COL32(37, 44, 40, 245);
        inline constexpr ImU32 SurfaceInset       = IM_COL32(14, 17, 15, 228);

        inline constexpr ImU32 BorderMuted        = IM_COL32(228, 224, 208, 30);
        inline constexpr ImU32 BorderTactical     = IM_COL32(228, 224, 208, 68);
        inline constexpr ImU32 BorderFocused      = IM_COL32(228, 224, 208, 118);
        inline constexpr ImU32 BorderHighlight    = IM_COL32(217, 155, 56, 242);
        inline constexpr ImU32 CornerTick         = IM_COL32(217, 155, 56, 210);

        // Slots & Grid
        inline constexpr ImU32 SlotEmptyBg        = IM_COL32(14, 17, 15, 205);
        inline constexpr ImU32 SlotEmptyBorder    = IM_COL32(228, 224, 208, 24);
        inline constexpr ImU32 SlotOccupiedBg     = IM_COL32(28, 33, 30, 236);
        inline constexpr ImU32 SlotOccupiedBorder = IM_COL32(228, 224, 208, 60);
        inline constexpr ImU32 SlotHoverBorder    = IM_COL32(217, 155, 56, 242);
        inline constexpr ImU32 SlotValidDropBg    = IM_COL32(111, 166, 107, 68);
        inline constexpr ImU32 SlotValidDropBorder= IM_COL32(111, 166, 107, 240);
        inline constexpr ImU32 SlotInvalidDropBg  = IM_COL32(214, 69, 69, 68);
        inline constexpr ImU32 SlotInvalidDropBorder = IM_COL32(214, 69, 69, 240);
        inline constexpr ImU32 GridCrossDot       = IM_COL32(228, 224, 208, 42);

        // Accents & Signals (muted military industrial paint)
        inline constexpr ImU32 AccentAmber        = IM_COL32(217, 155, 56, 255);  // Signal amber gold
        inline constexpr ImU32 AccentAmberHover   = IM_COL32(235, 177, 82, 255);  // Lit amber
        inline constexpr ImU32 AccentCyan         = IM_COL32(92, 168, 201, 255);  // Steel water / optic
        inline constexpr ImU32 AccentEmerald      = IM_COL32(111, 166, 107, 255); // Field olive green
        inline constexpr ImU32 AccentRose         = IM_COL32(214, 69, 69, 255);   // Signal crimson danger
        inline constexpr ImU32 AccentIceBlue      = IM_COL32(158, 206, 227, 255); // Cold / frost
        inline constexpr ImU32 AccentOrange       = IM_COL32(217, 119, 54, 255);  // Weapon terracotta
        inline constexpr ImU32 AccentBrass        = IM_COL32(200, 168, 66, 255);  // Cartridge brass
        inline constexpr ImU32 AccentViolet       = IM_COL32(139, 123, 181, 255); // Muted gear plum-slate

        // World Atlas
        inline constexpr ImU32 AtlasOcean         = IM_COL32(13, 20, 20, 255);
        inline constexpr ImU32 AtlasLand          = IM_COL32(34, 48, 44, 255);
        inline constexpr ImU32 AtlasCoast         = IM_COL32(63, 89, 81, 255);
        inline constexpr ImU32 AtlasGrid          = IM_COL32(120, 152, 140, 38);

        // Hazard Stripes (for discard zone)
        inline constexpr ImU32 HazardYellow       = IM_COL32(214, 69, 69, 130);
        inline constexpr ImU32 HazardDark         = IM_COL32(14, 17, 15, 225);

        // Typography (warm paper off-white)
        inline constexpr ImU32 TextPrimary        = IM_COL32(236, 234, 224, 255);
        inline constexpr ImU32 TextSecondary      = IM_COL32(159, 164, 152, 242);
        inline constexpr ImU32 TextMuted          = IM_COL32(103, 109, 99, 220);
        inline constexpr ImU32 TextHighlight      = IM_COL32(235, 177, 82, 255);
        inline constexpr ImU32 TextOnAccent       = IM_COL32(20, 18, 12, 255);

        // Key Badges
        inline constexpr ImU32 KeyCapBg           = IM_COL32(35, 40, 37, 255);
        inline constexpr ImU32 KeyCapBorder       = IM_COL32(228, 224, 208, 78);
        inline constexpr ImU32 KeyCapShadow       = IM_COL32(8, 10, 9, 235);
    }

    inline ImVec4 ColorVec4(ImU32 col)
    {
        return ImGui::ColorConvertU32ToFloat4(col);
    }

    // Scoped style helper so ImGui controls inside lobby, world map, trader and settings
    // inherit the exact Tactical Field Minimalism palette instead of default grey chrome.
    struct FFieldStyleScope
    {
        FFieldStyleScope()
        {
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 2.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 2.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 2.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_TabRounding, 2.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarRounding, 2.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, 2.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);

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
            ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32(33, 39, 34, 240));
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ColorVec4(Colors::SurfaceCardHover));
            ImGui::PushStyleColor(ImGuiCol_HeaderActive, IM_COL32(217, 155, 56, 70));
            ImGui::PushStyleColor(ImGuiCol_CheckMark, ColorVec4(Colors::AccentAmber));
            ImGui::PushStyleColor(ImGuiCol_SliderGrab, ColorVec4(Colors::AccentAmber));
            ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ColorVec4(Colors::AccentAmberHover));
            ImGui::PushStyleColor(ImGuiCol_Tab, ColorVec4(Colors::SurfaceInset));
            ImGui::PushStyleColor(ImGuiCol_TabHovered, ColorVec4(Colors::SurfaceCardHover));
            ImGui::PushStyleColor(ImGuiCol_TabSelected, ColorVec4(Colors::SurfaceCard));
            ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, IM_COL32(10, 12, 11, 100));
            ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, IM_COL32(228, 224, 208, 45));
            ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, IM_COL32(217, 155, 56, 150));
            ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive, ColorVec4(Colors::AccentAmber));
        }

        ~FFieldStyleScope()
        {
            ImGui::PopStyleColor(26);
            ImGui::PopStyleVar(9);
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
        case EItemKind::Melee:      return IM_COL32(196, 98, 42, 245);
        case EItemKind::Ammo:       return Colors::AccentBrass;
        case EItemKind::Attachment: return Colors::AccentCyan;
        case EItemKind::Clothing:   return Colors::AccentViolet;
        case EItemKind::Consumable: return Colors::AccentEmerald;
        case EItemKind::Misc:
        default:                    return IM_COL32(143, 150, 138, 225);
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
