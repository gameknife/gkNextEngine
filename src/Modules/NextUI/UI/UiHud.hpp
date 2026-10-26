#pragma once

#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Runtime/RuntimeFwd.hpp"
#include "Modules/NextUI/UI/UiTheme.hpp"

#include <glm/glm.hpp>
#include <imgui.h>
#include <string_view>

namespace NextUI::Hud
{
    // ── Color & Status Helpers ──────────────────────────────────────────────────

    inline ImVec4 ToImVec4(const glm::vec3& c, float alpha = 1.0f) noexcept
    {
        return ImVec4(c.x, c.y, c.z, alpha);
    }

    inline ImVec4 ToImVec4(const glm::vec4& c) noexcept
    {
        return ImVec4(c.x, c.y, c.z, c.w);
    }

    inline ImU32 ToImU32(const glm::vec3& c, float alpha = 1.0f) noexcept
    {
        return ImGui::ColorConvertFloat4ToU32(ImVec4(c.x, c.y, c.z, alpha));
    }

    inline ImU32 ToImU32(const glm::vec4& c) noexcept
    {
        return ImGui::ColorConvertFloat4ToU32(ImVec4(c.x, c.y, c.z, c.w));
    }

    inline ImU32 ToImU32(const ImVec4& c) noexcept
    {
        return ImGui::ColorConvertFloat4ToU32(c);
    }

    /// Returns semantic health color based on normalized health fraction [0, 1]:
    /// > 0.5 -> StatusSuccess, > 0.25 -> StatusWarning, else -> StatusError
    ImVec4 GetHealthColor(float fraction, float alpha = 1.0f) noexcept;
    ImU32 GetHealthColorU32(float fraction, float alpha = 1.0f) noexcept;

    // ── Text Drawing Primitives ─────────────────────────────────────────────────

    void DrawTextShadowed(
        ImDrawList* drawList,
        ImVec2 pos,
        ImU32 col,
        std::string_view text,
        float fontSize = 0.0f,
        ImU32 shadowCol = IM_COL32(0, 0, 0, 180),
        ImVec2 shadowOffset = ImVec2(1.0f, 1.0f));

    void DrawTextCentered(
        ImDrawList* drawList,
        ImVec2 center,
        ImU32 col,
        std::string_view text,
        float fontSize = 0.0f,
        bool shadowed = true);

    void DrawTextOutlined(
        ImDrawList* drawList,
        ImVec2 pos,
        ImU32 col,
        std::string_view text,
        float fontSize = 0.0f,
        ImU32 outlineCol = IM_COL32(0, 0, 0, 220));

    // ── Status Bar Primitives ───────────────────────────────────────────────────

    struct FStatusBarOptions
    {
        ImU32 bgCol = 0;      // 0 uses Foundation::EColor::BgInput with alpha 0.85
        ImU32 borderCol = 0;  // 0 uses Foundation::EColor::BorderSubtle
        float rounding = -1.0f; // < 0 uses Foundation::ERounding::Control
        std::string_view centerText{};
        ImU32 textCol = 0;    // 0 uses Foundation::EColor::TextBright
    };

    void DrawStatusBar(
        ImDrawList* drawList,
        ImVec2 pos,
        ImVec2 size,
        float fraction,
        ImU32 fillCol,
        const FStatusBarOptions& opts = {});

    // ── 3D World-Anchored HUD Primitives ────────────────────────────────────────

    bool TryProjectWorldToScreen(
        const glm::vec3& worldPos,
        ImVec2& outScreenPos,
        const NextGameInstanceBase* gameInstance = nullptr);

    bool TryProjectWorldToScreen(
        const glm::mat4& viewProjection,
        ImVec2 viewportPos,
        ImVec2 viewportSize,
        const glm::vec3& worldPos,
        ImVec2& outScreenPos,
        float ndcLimit = 1.2f) noexcept;

    bool DrawWorldHealthBar(
        ImDrawList* drawList,
        const glm::vec3& worldPos,
        float fraction,
        ImVec2 size = ImVec2(48.0f, 6.0f),
        ImVec2 screenOffset = ImVec2(0.0f, -12.0f),
        ImU32 fillOverride = 0,
        const NextGameInstanceBase* gameInstance = nullptr);

    bool DrawWorldBillboardTag(
        ImDrawList* drawList,
        const glm::vec3& worldPos,
        std::string_view text,
        ImU32 accentCol = 0,
        ImVec2 screenOffset = ImVec2(0.0f, -20.0f),
        const NextGameInstanceBase* gameInstance = nullptr);

    bool DrawWorldFloatingText(
        ImDrawList* drawList,
        const glm::vec3& worldPos,
        std::string_view text,
        float normalizedAge,
        ImU32 baseCol,
        float risePixels = 28.0f,
        float fontSize = 0.0f,
        const NextGameInstanceBase* gameInstance = nullptr);

    // ── Tactical & Action HUD Primitives ────────────────────────────────────────

    void DrawCrosshair(
        ImDrawList* drawList,
        ImVec2 center,
        float gap = 5.0f,
        float length = 9.0f,
        float thickness = 1.5f,
        ImU32 col = IM_COL32(240, 244, 248, 210),
        bool centerDot = true);

    void DrawSelectionBox(
        ImDrawList* drawList,
        ImVec2 pMin,
        ImVec2 pMax,
        ImU32 accentCol = 0);

    /// Draws an inline "[Key] Action" pill hint and advances the ImGui cursor.
    ImVec2 DrawKeyHintPill(
        std::string_view keyText,
        std::string_view actionText,
        ImU32 keyAccentCol = 0);
} // namespace NextUI::Hud
