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
        inline constexpr ImU32 SurfaceBg          = IM_COL32(13, 17, 23, 232);    // Dark charcoal
        inline constexpr ImU32 SurfaceHeader      = IM_COL32(20, 26, 35, 240);    // Slightly elevated surface
        inline constexpr ImU32 SurfaceCard        = IM_COL32(18, 24, 32, 220);    // Inner card surface
        inline constexpr ImU32 BorderMuted        = IM_COL32(45, 55, 72, 140);    // Subtle 1px tactical border
        inline constexpr ImU32 BorderFocused      = IM_COL32(100, 116, 139, 200);  // Focused border
        inline constexpr ImU32 BorderHighlight    = IM_COL32(245, 158, 11, 220);   // Amber glow border

        // Slots & Grid
        inline constexpr ImU32 SlotEmptyBg        = IM_COL32(20, 25, 33, 160);    // Dark slot depression
        inline constexpr ImU32 SlotEmptyBorder    = IM_COL32(40, 48, 60, 120);    // Guide border
        inline constexpr ImU32 SlotOccupiedBg     = IM_COL32(28, 36, 48, 225);    // Occupied item background
        inline constexpr ImU32 SlotOccupiedBorder = IM_COL32(65, 78, 98, 200);    // Occupied border
        inline constexpr ImU32 SlotHoverBorder    = IM_COL32(245, 158, 11, 240);   // Amber hover
        inline constexpr ImU32 SlotValidDropBg    = IM_COL32(16, 185, 129, 65);   // Emerald tint
        inline constexpr ImU32 SlotValidDropBorder= IM_COL32(16, 185, 129, 240);  // Emerald border
        inline constexpr ImU32 SlotInvalidDropBg  = IM_COL32(244, 63, 94, 65);    // Rose tint
        inline constexpr ImU32 SlotInvalidDropBorder = IM_COL32(244, 63, 94, 240);// Rose border

        // Accents & Signals
        inline constexpr ImU32 AccentAmber        = IM_COL32(245, 158, 11, 255);  // Amber gold
        inline constexpr ImU32 AccentCyan         = IM_COL32(56, 189, 248, 255);  // Hydration / Water blue
        inline constexpr ImU32 AccentEmerald      = IM_COL32(16, 185, 129, 255);  // Healing / Positive green
        inline constexpr ImU32 AccentRose         = IM_COL32(244, 63, 94, 255);   // Danger / Bleeding red
        inline constexpr ImU32 AccentIceBlue      = IM_COL32(147, 197, 253, 255); // Freezing cold

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
        case EItemKind::Clothing:   return "GEAR";
        case EItemKind::Consumable: return "SUPPLY";
        case EItemKind::Misc:
        default:                    return "ITEM";
        }
    }
}
