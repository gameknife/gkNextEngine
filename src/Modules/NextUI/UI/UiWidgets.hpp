#pragma once

#include "Engine/Common/CoreMinimal.hpp"

#include <imgui.h>
#include <string_view>

namespace NextUI::Foundation
{
    enum class EButtonVariant : uint8_t
    {
        Primary,
        Secondary,
        Ghost,
        Toolbar,
        Danger,
    };

    enum class EButtonTone : uint8_t
    {
        Default,
        Accent,
        Success,
        Warning,
    };

    struct FButtonOptions
    {
        EButtonVariant variant = EButtonVariant::Secondary;
        EButtonTone tone = EButtonTone::Default;
        ImVec2 size{};
        const char* tooltip = nullptr;
        bool active = false;
        bool disabled = false;
    };

    struct FPillBadgeOptions
    {
        const char* icon = nullptr;
        ImVec4 backgroundColor = ImVec4(0.148f, 0.148f, 0.148f, 0.85f);
        ImVec4 textColor = ImVec4(0.90f, 0.90f, 0.90f, 1.0f);
        ImVec4 borderColor = ImVec4(0.26f, 0.26f, 0.26f, 0.60f);
        ImVec2 padding = ImVec2(8.0f, 3.5f);
        float rounding = -1.0f; // < 0 uses capsule rounding (height * 0.5f)
        const char* tooltip = nullptr;
    };

    void Tooltip(const char* text);
    bool Button(const char* label, const FButtonOptions& options = {});
    bool IconButton(const char* icon, const char* tooltip, bool active = false,
                    ImVec2 size = ImVec2(), bool activeUnderline = false);
    bool ComboOption(const char* label, bool selected);

    void DrawPillBadge(const char* label, const FPillBadgeOptions& options = {});
    void DrawPillBadge(const char* icon, const char* label, ImVec4 bgCol, ImVec4 textCol, ImVec4 borderCol,
                       float rounding = -1.0f, ImVec2 padding = ImVec2(8.0f, 3.5f));
    void DrawPillBadgeU32(const char* icon, const char* label, ImU32 textCol, ImU32 bgCol, ImU32 borderCol,
                          float rounding = -1.0f, ImVec2 padding = ImVec2(8.0f, 3.5f));
    void DrawCodeBadge(const char* code);

    void CompactStat(const char* label, std::string_view value);

    void DrawSmoothSpinner(ImDrawList* drawList, ImVec2 center, float radius, float thickness, ImU32 color);
    void DrawAnimatedProgressBar(ImDrawList* drawList, ImVec2 barMin, ImVec2 barMax, ImU32 fillCol);
}
