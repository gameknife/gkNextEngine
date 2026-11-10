#pragma once

#include <cstdint>
#include <string_view>
#include <imgui.h>
#include "Application/Game/NextDayz/Data/ItemDefs.hpp"

namespace NextDayz::UITheme
{
    // ---- Color Palette (Modern Tact-Minimalist) ----
    namespace Colors
    {
        // Surfaces & Panels
        inline constexpr ImU32 SurfaceBg          = IM_COL32(11, 14, 20, 246);    // Deep tactical charcoal/black
        inline constexpr ImU32 SurfacePanel       = IM_COL32(15, 20, 28, 235);    // Elevated tactical chassis panel
        inline constexpr ImU32 SurfaceHeader      = IM_COL32(22, 29, 40, 245);    // Tactical sub-header
        inline constexpr ImU32 SurfaceCard        = IM_COL32(18, 24, 34, 220);    // Inner card surface
        inline constexpr ImU32 BorderMuted        = IM_COL32(40, 50, 68, 140);    // Subtle 1px tactical border
        inline constexpr ImU32 BorderTactical     = IM_COL32(52, 66, 88, 200);    // Crisp tactical border
        inline constexpr ImU32 BorderFocused      = IM_COL32(100, 116, 139, 200);  // Focused border
        inline constexpr ImU32 BorderHighlight    = IM_COL32(245, 158, 11, 240);   // Amber glow border
        inline constexpr ImU32 CornerTick         = IM_COL32(245, 158, 11, 220);   // Corner reticle accent

        // Slots & Grid
        inline constexpr ImU32 SlotEmptyBg        = IM_COL32(16, 21, 29, 180);    // Dark slot depression
        inline constexpr ImU32 SlotEmptyBorder    = IM_COL32(36, 46, 62, 130);    // Guide border
        inline constexpr ImU32 SlotOccupiedBg     = IM_COL32(24, 32, 44, 230);    // Occupied item background
        inline constexpr ImU32 SlotOccupiedBorder = IM_COL32(58, 72, 94, 200);    // Occupied border
        inline constexpr ImU32 SlotHoverBorder    = IM_COL32(245, 158, 11, 240);   // Amber hover
        inline constexpr ImU32 SlotValidDropBg    = IM_COL32(16, 185, 129, 65);   // Emerald tint
        inline constexpr ImU32 SlotValidDropBorder= IM_COL32(16, 185, 129, 240);  // Emerald border
        inline constexpr ImU32 SlotInvalidDropBg  = IM_COL32(244, 63, 94, 65);    // Rose tint
        inline constexpr ImU32 SlotInvalidDropBorder = IM_COL32(244, 63, 94, 240);// Rose border
        inline constexpr ImU32 GridCrossDot       = IM_COL32(55, 70, 92, 90);     // Subtle reticle dot at slot centers

        // Accents & Signals
        inline constexpr ImU32 AccentAmber        = IM_COL32(245, 158, 11, 255);  // Amber gold
        inline constexpr ImU32 AccentCyan         = IM_COL32(56, 189, 248, 255);  // Hydration / Water blue
        inline constexpr ImU32 AccentEmerald      = IM_COL32(16, 185, 129, 255);  // Healing / Positive green
        inline constexpr ImU32 AccentRose         = IM_COL32(244, 63, 94, 255);   // Danger / Bleeding red
        inline constexpr ImU32 AccentIceBlue      = IM_COL32(147, 197, 253, 255); // Freezing cold
        inline constexpr ImU32 AccentOrange       = IM_COL32(249, 115, 22, 255);  // Tactical weapon orange
        inline constexpr ImU32 AccentViolet       = IM_COL32(168, 85, 247, 255);  // Tactical gear purple

        // Hazard Stripes (for discard zone)
        inline constexpr ImU32 HazardYellow       = IM_COL32(234, 179, 8, 160);
        inline constexpr ImU32 HazardDark         = IM_COL32(20, 24, 30, 220);

        // Typography
        inline constexpr ImU32 TextPrimary        = IM_COL32(243, 244, 246, 255); // Crisp off-white
        inline constexpr ImU32 TextSecondary      = IM_COL32(161, 161, 170, 230); // Cool silver
        inline constexpr ImU32 TextMuted          = IM_COL32(107, 114, 128, 180); // Subdued charcoal
        inline constexpr ImU32 TextHighlight      = IM_COL32(251, 191, 36, 255);  // Golden highlight

        // Key Badges
        inline constexpr ImU32 KeyCapBg           = IM_COL32(38, 44, 54, 255);    // Tactical keycap
        inline constexpr ImU32 KeyCapBorder       = IM_COL32(80, 90, 105, 230);   // Keycap top/side bevel
        inline constexpr ImU32 KeyCapShadow       = IM_COL32(10, 12, 16, 220);    // Keycap bottom shadow
    }

    // Category accent bar color for items
    inline ImU32 ItemKindColor(EItemKind kind)
    {
        switch (kind)
        {
        case EItemKind::Weapon:     return IM_COL32(249, 115, 22, 240);  // Tactical Orange
        case EItemKind::Melee:      return IM_COL32(234, 88, 12, 240);   // Burnt Orange
        case EItemKind::Ammo:       return IM_COL32(234, 179, 8, 240);   // Brass Yellow
        case EItemKind::Attachment: return IM_COL32(56, 189, 248, 240);
        case EItemKind::Clothing:   return IM_COL32(168, 85, 247, 240);  // Tactical Purple/Blue
        case EItemKind::Consumable: return IM_COL32(34, 197, 94, 240);   // Emerald Green
        case EItemKind::Misc:
        default:                    return IM_COL32(148, 163, 184, 220); // Tactical Slate
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
