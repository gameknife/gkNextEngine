#pragma once

#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Runtime/RuntimeFwd.hpp"
#include "Modules/NextUI/Theme.hpp"

#include <deque>
#include <glm/glm.hpp>
#include <imgui.h>
#include <string>
#include <string_view>

namespace NextUI
{
    enum class ENotificationKind : uint8_t
    {
        Info,
        Success,
        Warning,
        Critical,
    };

    struct FToast
    {
        std::string text;
        ENotificationKind kind = ENotificationKind::Info;
        float lifeMs = 0.0f;
        float durationMs = 0.0f;
    };

    class FNotificationCenter
    {
    public:
        struct FStyle
        {
            ImVec4 infoAccent = Color(EColor::Blue);
            ImVec4 successAccent = Color(EColor::Success);
            ImVec4 warningAccent = Color(EColor::Warning);
            ImVec4 criticalAccent = Color(EColor::Danger);
            ImVec4 surface = Color(EColor::SurfaceElevated, 0.96f);
            ImVec4 text = Color(EColor::Text);
            float uiScale = 1.0f;
        };

        void Push(std::string text, ENotificationKind kind, float durationMs = 2500.0f);
        void Update(float deltaMs);
        void Clear();
        void Render(float bottomInset = 0.0f, float rightInset = 12.0f, ImFont* font = nullptr) const;
        void SetStyle(const FStyle& style);
        size_t ActiveCount() const noexcept { return toasts_.size(); }

    private:
        ImVec4 GetAccentColor(ENotificationKind kind) const;

        std::deque<FToast> toasts_;
        FStyle style_{};
    };

    namespace Hud
    {
        inline ImVec4 ToImVec4(const glm::vec3& c, const float alpha = 1.0f) noexcept
        {
            return ImVec4(c.x, c.y, c.z, alpha);
        }

        inline ImVec4 ToImVec4(const glm::vec4& c) noexcept
        {
            return ImVec4(c.x, c.y, c.z, c.w);
        }

        inline ImU32 ToImU32(const glm::vec3& c, const float alpha = 1.0f) noexcept
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

        ImVec4 GetHealthColor(float fraction, float alpha = 1.0f) noexcept;
        ImU32 GetHealthColorU32(float fraction, float alpha = 1.0f) noexcept;

        // Text drawing primitives
        void DrawTextShadowed(ImDrawList* drawList,
                              ImVec2 pos,
                              ImU32 col,
                              std::string_view text,
                              float fontSize = 0.0f,
                              ImU32 shadowCol = IM_COL32(0, 0, 0, 180),
                              ImVec2 shadowOffset = ImVec2(1.0f, 1.0f));

        void DrawTextCentered(ImDrawList* drawList,
                              ImVec2 center,
                              ImU32 col,
                              std::string_view text,
                              float fontSize = 0.0f,
                              bool shadowed = true);

        void DrawTextOutlined(ImDrawList* drawList,
                              ImVec2 pos,
                              ImU32 col,
                              std::string_view text,
                              float fontSize = 0.0f,
                              ImU32 outlineCol = IM_COL32(0, 0, 0, 220));

        // Status & textured bars
        struct FStatusBarOptions
        {
            ImU32 bgCol = 0;
            ImU32 borderCol = 0;
            float rounding = -1.0f;
            std::string_view centerText{};
            ImU32 textCol = 0;
        };

        void DrawStatusBar(ImDrawList* drawList,
                           ImVec2 pos,
                           ImVec2 size,
                           float fraction,
                           ImU32 fillCol,
                           const FStatusBarOptions& opts = {});

        void DrawBar(const ImVec2& pos,
                     const ImVec2& size,
                     float ratio,
                     ImU32 fillColor,
                     const char* text,
                     float scale);

        void DrawTexturedBar(ImDrawList* drawList,
                             ImTextureID bgTexture,
                             ImTextureID fillTexture,
                             ImTextureID frameTexture,
                             const ImVec2& pos,
                             const ImVec2& size,
                             float ratio,
                             const std::string& text,
                             float scale,
                             ImU32 fillFallback,
                             ImU32 textColor = IM_COL32_WHITE);

        // Image / 9-slice / backdrop painter primitives
        void DrawImageContain(ImDrawList* drawList,
                              ImTextureID texture,
                              const ImVec2& textureSize,
                              const ImVec2& boxMin,
                              const ImVec2& boxMax,
                              float padding = 0.0f,
                              ImU32 tint = IM_COL32_WHITE);

        void DrawImageCover(ImDrawList* drawList,
                            ImTextureID texture,
                            const ImVec2& textureSize,
                            const ImVec2& boxMin,
                            const ImVec2& boxMax,
                            ImU32 tint = IM_COL32_WHITE);

        void DrawNineSlicePanel(ImDrawList* drawList,
                                ImTextureID texture,
                                const ImVec2& textureSize,
                                const ImVec2& min,
                                const ImVec2& max,
                                ImU32 tint = IM_COL32_WHITE);

        void DrawPanel(ImDrawList* drawList,
                       ImTextureID texture,
                       const ImVec2& min,
                       const ImVec2& max,
                       ImU32 fallbackColor,
                       float rounding,
                       ImU32 tint = IM_COL32_WHITE,
                       const ImVec2& textureSize = ImVec2(64.0f, 64.0f));

        void DrawFullscreenDim(const ImGuiViewport* viewport, float alpha);
        bool BeginAppModal(const char* title,
                           float dimAlpha = 0.55f,
                           ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize);
        void EndAppModal();

        // Unified 3D world -> logical screen projection & world-anchored HUD primitives
        bool TryProjectWorldToScreen(const glm::vec3& worldPos,
                                     ImVec2& outScreenPos,
                                     const NextGameInstanceBase* gameInstance = nullptr);
        bool TryProjectWorldToScreen(const NextGameInstanceBase& gameInstance,
                                     const glm::vec3& worldPos,
                                     ImVec2& outScreenPos);
        bool TryProjectWorldToScreen(const glm::mat4& viewProjection,
                                     ImVec2 viewportPos,
                                     ImVec2 viewportSize,
                                     const glm::vec3& worldPos,
                                     ImVec2& outScreenPos,
                                     float ndcLimit = 1.2f) noexcept;

        bool DrawWorldHealthBar(ImDrawList* drawList,
                                const glm::vec3& worldPos,
                                float fraction,
                                ImVec2 size = ImVec2(48.0f, 6.0f),
                                ImVec2 screenOffset = ImVec2(0.0f, -12.0f),
                                ImU32 fillOverride = 0,
                                const NextGameInstanceBase* gameInstance = nullptr);

        bool DrawWorldBillboardTag(ImDrawList* drawList,
                                   const glm::vec3& worldPos,
                                   std::string_view text,
                                   ImU32 accentCol = 0,
                                   ImVec2 screenOffset = ImVec2(0.0f, -20.0f),
                                   const NextGameInstanceBase* gameInstance = nullptr);

        bool DrawWorldFloatingText(ImDrawList* drawList,
                                   const glm::vec3& worldPos,
                                   std::string_view text,
                                   float normalizedAge,
                                   ImU32 baseCol,
                                   float risePixels = 28.0f,
                                   float fontSize = 0.0f,
                                   const NextGameInstanceBase* gameInstance = nullptr);

        // Tactical & action HUD primitives
        void DrawCrosshair(ImDrawList* drawList,
                           ImVec2 center,
                           float gap = 5.0f,
                           float length = 9.0f,
                           float thickness = 1.5f,
                           ImU32 col = IM_COL32(240, 244, 248, 210),
                           bool centerDot = true);

        void DrawSelectionBox(ImDrawList* drawList,
                              ImVec2 pMin,
                              ImVec2 pMax,
                              ImU32 accentCol = 0);

        ImVec2 DrawKeyHintPill(std::string_view keyText,
                               std::string_view actionText,
                               ImU32 keyAccentCol = 0);
    }
}
