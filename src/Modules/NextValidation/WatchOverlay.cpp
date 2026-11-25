#include "Modules/NextValidation/WatchOverlay.hpp"

#include <SDL3/SDL_timer.h>

#if GK_VALIDATION_WATCH_OVERLAY
#include "Engine/Runtime/Engine.hpp"

#include <imgui.h>

#include <algorithm>
#include <cfloat>
#endif

namespace Modules::NextValidation
{
    double WatchNow()
    {
        return static_cast<double>(SDL_GetTicksNS()) * 1.0e-9;
    }

#if GK_VALIDATION_WATCH_OVERLAY
    namespace
    {
        constexpr double rippleSeconds = 0.55;
        constexpr double keySeconds = 1.2;
        constexpr double flashSeconds = 0.7;

        ImU32 StatusColor(const std::string& status, int alpha)
        {
            if (status == "pass" || status == "done")
            {
                return IM_COL32(34, 197, 94, alpha);
            }
            if (status == "fail" || status == "failed")
            {
                return IM_COL32(239, 68, 68, alpha);
            }
            return IM_COL32(59, 130, 246, alpha);
        }

        int FadeAlpha(double age, double duration, int maxAlpha)
        {
            if (age < 0.0 || age >= duration)
            {
                return 0;
            }
            return static_cast<int>(static_cast<double>(maxAlpha) * (1.0 - age / duration));
        }

        class FWatchOverlayLayer final : public Runtime::IUiLayer
        {
        public:
            explicit FWatchOverlayLayer(std::shared_ptr<FWatchState> state) : state_(std::move(state)) {}

            std::string_view GetLayerId() const override { return "agent-watch"; }
            // Above every application, document and developer layer.
            int32_t GetOrder() const override { return static_cast<int32_t>(Runtime::EUiLayerOrder::SystemModal) + 50; }

            void RenderLayer(NextEngine& engine) override
            {
                FWatchState& state = *state_;
                if (state.hidden)
                {
                    return;
                }
                ImDrawList* drawList = ImGui::GetForegroundDrawList();
                ImFont* font = ImGui::GetFont();
                if (drawList == nullptr || font == nullptr)
                {
                    return;
                }
                const ImVec2 display = ImGui::GetIO().DisplaySize;
                const float uiScale = engine.GetUiHost() ? std::max(0.01f, engine.GetUiHost()->GetUiScale()) : 1.0f;
                const float baseSize = ImGui::GetFontSize();
                const double now = WatchNow();

                DrawStatusBorder(drawList, display, state, now);
                // Subtitle-style at the bottom: title bars, menus and toolbars live at the top.
                const float captionTop = DrawCaption(drawList, font, baseSize, display, state);
                DrawPointer(drawList, state, now, uiScale);
                DrawKeyBadge(drawList, font, baseSize, display, captionTop, state, now);
            }

        private:
            static void DrawStatusBorder(ImDrawList* drawList, ImVec2 display, const FWatchState& state, double now)
            {
                int alpha = 0;
                float thickness = 6.0f;
                if (state.status == "done" || state.status == "failed")
                {
                    alpha = 200;
                    thickness = 4.0f;
                }
                else if (state.status == "pass" || state.status == "fail")
                {
                    alpha = FadeAlpha(now - state.statusTime, flashSeconds, 230);
                }
                if (alpha > 0)
                {
                    drawList->AddRect(ImVec2(thickness * 0.5f, thickness * 0.5f),
                                      ImVec2(display.x - thickness * 0.5f, display.y - thickness * 0.5f),
                                      StatusColor(state.status, alpha), 0.0f, 0, thickness);
                }
            }

            // Returns the top edge of the caption box so the key badge can sit above it.
            static float DrawCaption(ImDrawList* drawList, ImFont* font, float baseSize, ImVec2 display,
                                     const FWatchState& state)
            {
                const char* badge = "AGENT WATCH  -  keyboard and mouse input is ignored";
                const float badgeSize = baseSize * 0.8f;
                const float captionSize = baseSize * 1.35f;
                const float detailSize = baseSize * 1.05f;
                const float wrap = std::max(200.0f, display.x * 0.7f);
                const ImVec2 badgeExtent = font->CalcTextSizeA(badgeSize, FLT_MAX, 0.0f, badge);
                const ImVec2 captionExtent = state.caption.empty()
                    ? ImVec2(0.0f, 0.0f)
                    : font->CalcTextSizeA(captionSize, FLT_MAX, wrap, state.caption.c_str());
                const ImVec2 detailExtent = state.detail.empty()
                    ? ImVec2(0.0f, 0.0f)
                    : font->CalcTextSizeA(detailSize, FLT_MAX, wrap, state.detail.c_str());
                const float padding = 14.0f;
                const float gap = 6.0f;
                const float width = std::max({badgeExtent.x, captionExtent.x, detailExtent.x}) + padding * 2.0f;
                float height = badgeExtent.y + padding * 2.0f;
                if (!state.caption.empty()) height += gap + captionExtent.y;
                if (!state.detail.empty()) height += gap + detailExtent.y;
                // Clear of the status bars most tools draw along the bottom edge.
                const float bottomMargin = std::max(56.0f, display.y * 0.08f);
                const ImVec2 min((display.x - width) * 0.5f, display.y - bottomMargin - height);
                const ImVec2 max(min.x + width, min.y + height);
                drawList->AddRectFilled(min, max, IM_COL32(15, 18, 24, 215), 10.0f);
                drawList->AddRectFilled(min, ImVec2(min.x + 6.0f, max.y), StatusColor(state.status, 255), 10.0f,
                                        ImDrawFlags_RoundCornersLeft);
                ImVec2 cursor(min.x + padding, min.y + padding);
                drawList->AddText(font, badgeSize, cursor, IM_COL32(250, 204, 21, 255), badge);
                cursor.y += badgeExtent.y + gap;
                if (!state.caption.empty())
                {
                    drawList->AddText(font, captionSize, cursor, IM_COL32(245, 247, 250, 255), state.caption.c_str(),
                                      nullptr, wrap);
                    cursor.y += captionExtent.y + gap;
                }
                if (!state.detail.empty())
                {
                    drawList->AddText(font, detailSize, cursor, IM_COL32(190, 198, 210, 255), state.detail.c_str(),
                                      nullptr, wrap);
                }
                return min.y;
            }

            static void DrawPointer(ImDrawList* drawList, FWatchState& state, double now, float uiScale)
            {
                std::erase_if(state.ripples, [now](const FWatchState::FRipple& ripple)
                {
                    return now - ripple.time > rippleSeconds;
                });
                for (const FWatchState::FRipple& ripple : state.ripples)
                {
                    const double t = (now - ripple.time) / rippleSeconds;
                    const float radius = 12.0f + 34.0f * static_cast<float>(t);
                    const int alpha = FadeAlpha(now - ripple.time, rippleSeconds, 255);
                    const ImU32 color = ripple.secondary ? IM_COL32(217, 70, 239, alpha) : IM_COL32(250, 204, 21, alpha);
                    drawList->AddCircle(ImVec2(ripple.x / uiScale, ripple.y / uiScale), radius, color, 32, 3.0f);
                }
                if (state.hasPointer)
                {
                    const ImVec2 center(state.pointerX / uiScale, state.pointerY / uiScale);
                    drawList->AddCircle(center, 13.0f, IM_COL32(0, 0, 0, 200), 32, 5.0f);
                    drawList->AddCircle(center, 13.0f, IM_COL32(255, 255, 255, 255), 32, 2.5f);
                    drawList->AddCircleFilled(center, 3.0f, IM_COL32(250, 204, 21, 255));
                }
                const int lookAlpha = FadeAlpha(now - state.lookTime, keySeconds, 230);
                if (lookAlpha > 0)
                {
                    const ImVec2 center(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f);
                    drawList->AddCircle(center, 28.0f, IM_COL32(255, 255, 255, lookAlpha), 48, 2.0f);
                    drawList->AddLine(ImVec2(center.x - 40.0f, center.y), ImVec2(center.x + 40.0f, center.y),
                                      IM_COL32(255, 255, 255, lookAlpha), 2.0f);
                }
            }

            static void DrawKeyBadge(ImDrawList* drawList, ImFont* font, float baseSize, ImVec2 display,
                                     float captionTop, const FWatchState& state, double now)
            {
                const int alpha = FadeAlpha(now - state.keyTime, keySeconds, 255);
                if (alpha <= 0 || state.keyLabel.empty())
                {
                    return;
                }
                const float size = baseSize * 1.6f;
                const ImVec2 textSize = font->CalcTextSizeA(size, FLT_MAX, 0.0f, state.keyLabel.c_str());
                const ImVec2 min((display.x - textSize.x) * 0.5f - 16.0f, captionTop - textSize.y - 16.0f - 12.0f);
                const ImVec2 max(min.x + textSize.x + 32.0f, min.y + textSize.y + 16.0f);
                drawList->AddRectFilled(min, max, IM_COL32(15, 18, 24, alpha * 220 / 255), 8.0f);
                drawList->AddRect(min, max, IM_COL32(250, 204, 21, alpha), 8.0f, 0, 2.0f);
                drawList->AddText(font, size, ImVec2(min.x + 16.0f, min.y + 8.0f), IM_COL32(255, 255, 255, alpha),
                                  state.keyLabel.c_str());
            }

            std::shared_ptr<FWatchState> state_;
        };
    }

    std::unique_ptr<Runtime::IUiLayer> CreateWatchOverlayLayer(std::shared_ptr<FWatchState> state)
    {
        return std::make_unique<FWatchOverlayLayer>(std::move(state));
    }
#endif
}
