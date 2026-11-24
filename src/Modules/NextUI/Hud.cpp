#include "Engine/Common/CoreMinimal.hpp"

#include "Modules/NextUI/Hud.hpp"
#include "Modules/NextUI/Containers.hpp"
#include "Engine/Runtime/Utilities/NextEngineHelper.hpp"

#include <algorithm>
#include <cmath>

namespace NextUI
{
    namespace
    {
        constexpr float kToastSlideDurationMs = 200.0f;
        constexpr float kToastFadeDurationMs = 300.0f;
        constexpr size_t kMaxToastCount = 4;
    }

    void FNotificationCenter::Push(std::string text, const ENotificationKind kind, const float durationMs)
    {
        if (text.empty())
        {
            return;
        }

        toasts_.push_back(FToast{
            .text = std::move(text),
            .kind = kind,
            .lifeMs = durationMs,
            .durationMs = durationMs,
        });
        while (toasts_.size() > kMaxToastCount)
        {
            toasts_.pop_front();
        }
    }

    void FNotificationCenter::Update(const float deltaMs)
    {
        for (FToast& toast : toasts_)
        {
            toast.lifeMs = std::max(0.0f, toast.lifeMs - deltaMs);
        }

        while (!toasts_.empty() && toasts_.front().lifeMs <= 0.0f)
        {
            toasts_.pop_front();
        }
    }

    void FNotificationCenter::Clear()
    {
        toasts_.clear();
    }

    void FNotificationCenter::SetStyle(const FStyle& style)
    {
        style_ = style;
    }

    ImVec4 FNotificationCenter::GetAccentColor(const ENotificationKind kind) const
    {
        switch (kind)
        {
        case ENotificationKind::Success: return style_.successAccent;
        case ENotificationKind::Warning: return style_.warningAccent;
        case ENotificationKind::Critical: return style_.criticalAccent;
        case ENotificationKind::Info:
        default: return style_.infoAccent;
        }
    }

    void FNotificationCenter::Render(const float bottomInset, const float rightInset, ImFont* font) const
    {
        if (toasts_.empty())
        {
            return;
        }

        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImDrawList* drawList = ImGui::GetForegroundDrawList();
        if (!viewport || !drawList)
        {
            return;
        }

        font = font ? font : ImGui::GetFont();
        const float fontSize = font ? font->LegacySize : ImGui::GetFontSize();
        const float scale = std::max(0.01f, style_.uiScale);
        const float toastWidth = 280.0f * scale;
        const float toastHeight = 44.0f * scale;
        const float toastGap = 8.0f * scale;
        const float toastSlideOffset = 60.0f * scale;
        const float rounding = 6.0f * scale;

        size_t stackIndex = 0;
        for (auto iter = toasts_.rbegin(); iter != toasts_.rend(); ++iter, ++stackIndex)
        {
            const FToast& toast = *iter;
            const float ageMs = toast.durationMs - toast.lifeMs;
            const float slideT = std::clamp(ageMs / kToastSlideDurationMs, 0.0f, 1.0f);
            const float fadeT = toast.lifeMs < kToastFadeDurationMs
                ? std::clamp(toast.lifeMs / kToastFadeDurationMs, 0.0f, 1.0f)
                : 1.0f;
            const float alpha = slideT * fadeT;
            const float xOffset = (1.0f - slideT) * toastSlideOffset;

            const float x = viewport->Pos.x + viewport->Size.x - toastWidth - rightInset + xOffset;
            const float y = viewport->Pos.y + viewport->Size.y - bottomInset - toastHeight -
                static_cast<float>(stackIndex) * (toastHeight + toastGap);
            const ImVec2 cardMin(x, y);
            const ImVec2 cardMax(x + toastWidth, y + toastHeight);

            const ImVec4 accent = GetAccentColor(toast.kind);
            const ImU32 bgColor = ImGui::ColorConvertFloat4ToU32(
                ImVec4(style_.surface.x, style_.surface.y, style_.surface.z, style_.surface.w * alpha));
            const ImU32 borderColor = ImGui::ColorConvertFloat4ToU32(ImVec4(accent.x, accent.y, accent.z, 0.35f * alpha));
            const ImU32 accentColor = ImGui::ColorConvertFloat4ToU32(ImVec4(accent.x, accent.y, accent.z, alpha));
            drawList->AddRectFilled(cardMin, cardMax, bgColor, rounding);
            drawList->AddRect(cardMin, cardMax, borderColor, rounding, 0, scale);
            drawList->AddRectFilled(cardMin,
                                    ImVec2(cardMin.x + 5.0f * scale, cardMax.y),
                                    accentColor,
                                    rounding,
                                    ImDrawFlags_RoundCornersLeft);

            const ImVec2 textSize = font->CalcTextSizeA(fontSize, toastWidth - 28.0f * scale, 0.0f, toast.text.c_str());
            const ImVec2 textPos(cardMin.x + 16.0f * scale, cardMin.y + (toastHeight - textSize.y) * 0.5f);
            drawList->AddText(
                font,
                fontSize,
                textPos,
                ImGui::ColorConvertFloat4ToU32(
                    ImVec4(style_.text.x, style_.text.y, style_.text.z, style_.text.w * alpha)),
                toast.text.c_str());
        }
    }

    namespace Hud
    {
        namespace
        {
            float ResolveFontSize(const float requestedFontSize) noexcept
            {
                return requestedFontSize > 0.0f ? requestedFontSize : ImGui::GetFontSize();
            }

            ImVec2 CalcTextSizeWithSize(ImFont* font, const float fontSize, const std::string_view text) noexcept
            {
                if (text.empty())
                {
                    return ImVec2(0.0f, fontSize);
                }
                if (font != nullptr)
                {
                    return font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, text.data(), text.data() + text.size());
                }
                return ImGui::CalcTextSize(text.data(), text.data() + text.size());
            }
        }

        ImVec4 GetHealthColor(const float fraction, const float alpha) noexcept
        {
            const float clamped = std::clamp(fraction, 0.0f, 1.0f);
            if (clamped > 0.5f)
            {
                return Color(EColor::Success, alpha);
            }
            if (clamped > 0.25f)
            {
                return Color(EColor::Warning, alpha);
            }
            return Color(EColor::Danger, alpha);
        }

        ImU32 GetHealthColorU32(const float fraction, const float alpha) noexcept
        {
            return ImGui::ColorConvertFloat4ToU32(GetHealthColor(fraction, alpha));
        }

        void DrawTextShadowed(ImDrawList* drawList,
                              const ImVec2 pos,
                              const ImU32 col,
                              const std::string_view text,
                              const float fontSize,
                              const ImU32 shadowCol,
                              const ImVec2 shadowOffset)
        {
            if (drawList == nullptr || text.empty())
            {
                return;
            }

            ImFont* font = ImGui::GetFont();
            const float sz = ResolveFontSize(fontSize);
            const char* begin = text.data();
            const char* end = begin + text.size();

            if ((shadowCol >> IM_COL32_A_SHIFT) > 0)
            {
                drawList->AddText(font, sz, ImVec2(pos.x + shadowOffset.x, pos.y + shadowOffset.y), shadowCol, begin, end);
            }
            drawList->AddText(font, sz, pos, col, begin, end);
        }

        void DrawTextCentered(ImDrawList* drawList,
                              const ImVec2 center,
                              const ImU32 col,
                              const std::string_view text,
                              const float fontSize,
                              const bool shadowed)
        {
            if (drawList == nullptr || text.empty())
            {
                return;
            }

            ImFont* font = ImGui::GetFont();
            const float sz = ResolveFontSize(fontSize);
            const ImVec2 textSize = CalcTextSizeWithSize(font, sz, text);
            const ImVec2 pos(std::floor(center.x - textSize.x * 0.5f), std::floor(center.y - textSize.y * 0.5f));

            if (shadowed)
            {
                DrawTextShadowed(drawList, pos, col, text, sz);
            }
            else
            {
                drawList->AddText(font, sz, pos, col, text.data(), text.data() + text.size());
            }
        }

        void DrawTextOutlined(ImDrawList* drawList,
                              const ImVec2 pos,
                              const ImU32 col,
                              const std::string_view text,
                              const float fontSize,
                              const ImU32 outlineCol)
        {
            if (drawList == nullptr || text.empty())
            {
                return;
            }

            ImFont* font = ImGui::GetFont();
            const float sz = ResolveFontSize(fontSize);
            const char* begin = text.data();
            const char* end = begin + text.size();

            if ((outlineCol >> IM_COL32_A_SHIFT) > 0)
            {
                drawList->AddText(font, sz, ImVec2(pos.x - 1.0f, pos.y), outlineCol, begin, end);
                drawList->AddText(font, sz, ImVec2(pos.x + 1.0f, pos.y), outlineCol, begin, end);
                drawList->AddText(font, sz, ImVec2(pos.x, pos.y - 1.0f), outlineCol, begin, end);
                drawList->AddText(font, sz, ImVec2(pos.x, pos.y + 1.0f), outlineCol, begin, end);
            }
            drawList->AddText(font, sz, pos, col, begin, end);
        }

        void DrawStatusBar(ImDrawList* drawList,
                           const ImVec2 pos,
                           const ImVec2 size,
                           const float fraction,
                           const ImU32 fillCol,
                           const FStatusBarOptions& opts)
        {
            if (drawList == nullptr || size.x <= 0.0f || size.y <= 0.0f)
            {
                return;
            }

            const float clamped = std::clamp(fraction, 0.0f, 1.0f);
            const ImU32 bgCol = (opts.bgCol != 0) ? opts.bgCol : ColorU32(EColor::Background, 0.85f);
            const ImU32 borderCol = (opts.borderCol != 0) ? opts.borderCol : ColorU32(EColor::Border, 0.90f);
            const float rounding = (opts.rounding >= 0.0f)
                ? opts.rounding
                : std::min(4.0f * GetUiScale(), size.y * 0.5f);

            const ImVec2 pMax(pos.x + size.x, pos.y + size.y);
            drawList->AddRectFilled(pos, pMax, bgCol, rounding);

            if (clamped > 0.0f)
            {
                const float fillWidth = std::max(size.x * clamped, std::min(size.x, rounding * 1.5f));
                drawList->AddRectFilled(pos, ImVec2(pos.x + fillWidth, pMax.y), fillCol, rounding);
            }

            if ((borderCol >> IM_COL32_A_SHIFT) > 0)
            {
                drawList->AddRect(pos, pMax, borderCol, rounding, 0, 1.0f);
            }

            if (!opts.centerText.empty())
            {
                const ImU32 textCol = (opts.textCol != 0) ? opts.textCol : ColorU32(EColor::Text);
                DrawTextCentered(drawList, ImVec2(pos.x + size.x * 0.5f, pos.y + size.y * 0.5f),
                                 textCol, opts.centerText, 0.0f, true);
            }
        }

        void DrawBar(const ImVec2& pos,
                     const ImVec2& size,
                     const float ratio,
                     const ImU32 fillColor,
                     const char* text,
                     const float scale)
        {
            DrawStatusBar(ImGui::GetWindowDrawList(), pos, size, ratio, fillColor, FStatusBarOptions{
                .bgCol = ColorU32(EColor::Background, 0.88f),
                .borderCol = ColorU32(EColor::BorderStrong, 0.65f),
                .rounding = 4.0f * scale,
                .centerText = text != nullptr ? std::string_view(text) : std::string_view{},
            });
        }

        void DrawTexturedBar(ImDrawList* drawList,
                             const ImTextureID bgTexture,
                             const ImTextureID fillTexture,
                             const ImTextureID frameTexture,
                             const ImVec2& pos,
                             const ImVec2& size,
                             const float ratio,
                             const std::string& text,
                             const float scale,
                             const ImU32 fillFallback,
                             const ImU32 textColor)
        {
            if (!drawList)
            {
                return;
            }

            const float rounding = 4.0f * scale;
            const ImVec2 max(pos.x + size.x, pos.y + size.y);
            const float clampedRatio = std::clamp(ratio, 0.0f, 1.0f);
            if (bgTexture)
            {
                drawList->AddImage(bgTexture, pos, max);
            }
            else
            {
                drawList->AddRectFilled(pos, max, ColorU32(EColor::Background, 0.88f), rounding);
            }

            const ImVec2 fillMax(pos.x + size.x * clampedRatio, pos.y + size.y);
            if (clampedRatio > 0.0f)
            {
                if (fillTexture)
                {
                    drawList->AddImage(fillTexture, pos, fillMax, ImVec2(0.0f, 0.0f), ImVec2(clampedRatio, 1.0f));
                }
                else
                {
                    drawList->AddRectFilled(pos, fillMax, fillFallback, rounding);
                }
            }

            if (frameTexture)
            {
                drawList->AddImage(frameTexture, pos, max);
            }
            else
            {
                drawList->AddRect(pos, max, ColorU32(EColor::BorderStrong, 0.65f), rounding);
            }

            if (!text.empty())
            {
                const ImVec2 textSize = ImGui::CalcTextSize(text.c_str());
                drawList->AddText(ImVec2(pos.x + (size.x - textSize.x) * 0.5f, pos.y + (size.y - textSize.y) * 0.5f),
                                  textColor, text.c_str());
            }
        }

        void DrawImageContain(ImDrawList* drawList,
                              const ImTextureID texture,
                              const ImVec2& textureSize,
                              const ImVec2& boxMin,
                              const ImVec2& boxMax,
                              const float padding,
                              const ImU32 tint)
        {
            if (!drawList || !texture)
            {
                return;
            }

            const float availWidth = std::max(1.0f, (boxMax.x - boxMin.x) - padding * 2.0f);
            const float availHeight = std::max(1.0f, (boxMax.y - boxMin.y) - padding * 2.0f);
            if (textureSize.x <= 0.0f || textureSize.y <= 0.0f)
            {
                drawList->AddImage(texture,
                                   ImVec2(boxMin.x + padding, boxMin.y + padding),
                                   ImVec2(boxMin.x + padding + availWidth, boxMin.y + padding + availHeight),
                                   ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), tint);
                return;
            }

            const float scale = std::min(availWidth / textureSize.x, availHeight / textureSize.y);
            const ImVec2 drawSize(textureSize.x * scale, textureSize.y * scale);
            const ImVec2 drawMin(boxMin.x + padding + (availWidth - drawSize.x) * 0.5f,
                                 boxMin.y + padding + (availHeight - drawSize.y) * 0.5f);
            drawList->AddImage(texture, drawMin, ImVec2(drawMin.x + drawSize.x, drawMin.y + drawSize.y),
                               ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), tint);
        }

        void DrawImageCover(ImDrawList* drawList,
                            const ImTextureID texture,
                            const ImVec2& textureSize,
                            const ImVec2& boxMin,
                            const ImVec2& boxMax,
                            const ImU32 tint)
        {
            if (!drawList || !texture)
            {
                return;
            }

            if (textureSize.x <= 0.0f || textureSize.y <= 0.0f)
            {
                drawList->AddImage(texture, boxMin, boxMax, ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), tint);
                return;
            }

            const float boxWidth = std::max(1.0f, boxMax.x - boxMin.x);
            const float boxHeight = std::max(1.0f, boxMax.y - boxMin.y);
            const float boxAspect = boxWidth / boxHeight;
            const float imageAspect = textureSize.x / textureSize.y;
            ImVec2 uvMin(0.0f, 0.0f);
            ImVec2 uvMax(1.0f, 1.0f);

            if (imageAspect > boxAspect)
            {
                const float visibleWidth = boxAspect / imageAspect;
                uvMin.x = (1.0f - visibleWidth) * 0.5f;
                uvMax.x = uvMin.x + visibleWidth;
            }
            else
            {
                const float visibleHeight = imageAspect / boxAspect;
                uvMin.y = (1.0f - visibleHeight) * 0.5f;
                uvMax.y = uvMin.y + visibleHeight;
            }

            drawList->AddImage(texture, boxMin, boxMax, uvMin, uvMax, tint);
        }

        void DrawNineSlicePanel(ImDrawList* drawList,
                                const ImTextureID texture,
                                const ImVec2& textureSize,
                                const ImVec2& min,
                                const ImVec2& max,
                                const ImU32 tint)
        {
            if (!drawList || !texture)
            {
                return;
            }

            if (textureSize.x < 3.0f || textureSize.y < 3.0f)
            {
                drawList->AddImage(texture, min, max, ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), tint);
                return;
            }

            const float width = std::max(1.0f, max.x - min.x);
            const float height = std::max(1.0f, max.y - min.y);
            const float leftPixels = std::floor(textureSize.x * 0.5f);
            const float rightPixels = std::max(0.0f, textureSize.x - leftPixels - 1.0f);
            const float topPixels = std::floor(textureSize.y * 0.5f);
            const float bottomPixels = std::max(0.0f, textureSize.y - topPixels - 1.0f);
            const float leftWidth = std::min(leftPixels, width * 0.5f);
            const float rightWidth = std::min(rightPixels, width * 0.5f);
            const float topHeight = std::min(topPixels, height * 0.5f);
            const float bottomHeight = std::min(bottomPixels, height * 0.5f);
            const float x0 = min.x;
            const float x1 = min.x + leftWidth;
            const float x2 = max.x - rightWidth;
            const float x3 = max.x;
            const float y0 = min.y;
            const float y1 = min.y + topHeight;
            const float y2 = max.y - bottomHeight;
            const float y3 = max.y;
            const float u0 = 0.0f;
            const float u1 = leftWidth / textureSize.x;
            const float u2 = 1.0f - rightWidth / textureSize.x;
            const float u3 = 1.0f;
            const float v0 = 0.0f;
            const float v1 = topHeight / textureSize.y;
            const float v2 = 1.0f - bottomHeight / textureSize.y;
            const float v3 = 1.0f;

            auto addPatch = [drawList, texture, tint](float ax, float ay, float bx, float by,
                                                      float au, float av, float bu, float bv)
            {
                drawList->AddImage(texture, ImVec2(ax, ay), ImVec2(bx, by), ImVec2(au, av), ImVec2(bu, bv), tint);
            };

            addPatch(x0, y0, x1, y1, u0, v0, u1, v1);
            addPatch(x1, y0, x2, y1, u1, v0, u2, v1);
            addPatch(x2, y0, x3, y1, u2, v0, u3, v1);
            addPatch(x0, y1, x1, y2, u0, v1, u1, v2);
            addPatch(x1, y1, x2, y2, u1, v1, u2, v2);
            addPatch(x2, y1, x3, y2, u2, v1, u3, v2);
            addPatch(x0, y2, x1, y3, u0, v2, u1, v3);
            addPatch(x1, y2, x2, y3, u1, v2, u2, v3);
            addPatch(x2, y2, x3, y3, u2, v2, u3, v3);
        }

        void DrawPanel(ImDrawList* drawList,
                       const ImTextureID texture,
                       const ImVec2& min,
                       const ImVec2& max,
                       const ImU32 fallbackColor,
                       const float rounding,
                       const ImU32 tint,
                       const ImVec2& textureSize)
        {
            if (!drawList)
            {
                return;
            }
            if (texture)
            {
                DrawNineSlicePanel(drawList, texture, textureSize, min, max, tint);
                return;
            }
            drawList->AddRectFilled(min, max, fallbackColor, rounding);
        }

        void DrawFullscreenDim(const ImGuiViewport* viewport, const float alpha)
        {
            if (!viewport)
            {
                return;
            }
            ImGui::GetBackgroundDrawList()->AddRectFilled(
                viewport->Pos,
                ImVec2(viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y),
                ColorU32(EColor::Background, std::clamp(alpha, 0.0f, 1.0f)));
        }

        bool BeginAppModal(const char* title, const float dimAlpha, const ImGuiWindowFlags flags)
        {
            return BeginModal(FModalConfig{
                .PopupId = title,
                .OverrideStyle = false,
                .ClampToAnchorViewport = false,
                .BackdropDimAlpha = dimAlpha,
                .AutoOpen = true,
                .ExtraFlags = flags,
            });
        }

        void EndAppModal()
        {
            EndModal();
        }

        bool TryProjectWorldToScreen(const NextGameInstanceBase& gameInstance,
                                     const glm::vec3& worldPos,
                                     ImVec2& outScreenPos)
        {
            if (ImGui::GetCurrentContext() == nullptr)
            {
                return false;
            }
            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            if (!viewport || viewport->Size.x <= 1.0f || viewport->Size.y <= 1.0f)
            {
                return false;
            }

            glm::vec2 viewportUv{};
            if (!Runtime::EngineHelper::TryProjectWorldToViewportUvForGame(
                    gameInstance, viewport->Size.x / viewport->Size.y, worldPos, viewportUv))
            {
                return false;
            }

            outScreenPos = ImVec2(viewport->Pos.x + viewportUv.x * viewport->Size.x,
                                  viewport->Pos.y + viewportUv.y * viewport->Size.y);
            return true;
        }

        bool TryProjectWorldToScreen(const glm::vec3& worldPos,
                                     ImVec2& outScreenPos,
                                     const NextGameInstanceBase* gameInstance)
        {
            if (gameInstance != nullptr)
            {
                return TryProjectWorldToScreen(*gameInstance, worldPos, outScreenPos);
            }

            if (ImGui::GetCurrentContext() == nullptr)
            {
                return false;
            }
            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            if (!viewport || viewport->Size.x <= 1.0f || viewport->Size.y <= 1.0f)
            {
                return false;
            }

            glm::vec2 viewportUv{};
            if (!Runtime::EngineHelper::TryProjectWorldToViewportUv(worldPos, viewportUv))
            {
                return false;
            }

            outScreenPos = ImVec2(viewport->Pos.x + viewportUv.x * viewport->Size.x,
                                  viewport->Pos.y + viewportUv.y * viewport->Size.y);
            return true;
        }

        bool TryProjectWorldToScreen(const glm::mat4& viewProjection,
                                     const ImVec2 viewportPos,
                                     const ImVec2 viewportSize,
                                     const glm::vec3& worldPos,
                                     ImVec2& outScreenPos,
                                     const float ndcLimit) noexcept
        {
            const glm::vec4 clip = viewProjection * glm::vec4(worldPos, 1.0f);
            if (clip.w <= 0.0f)
            {
                return false;
            }
            const glm::vec3 ndc = glm::vec3(clip) / clip.w;
            if (ndc.x < -ndcLimit || ndc.x > ndcLimit || ndc.y < -ndcLimit || ndc.y > ndcLimit)
            {
                return false;
            }
            outScreenPos = ImVec2(
                viewportPos.x + (ndc.x * 0.5f + 0.5f) * viewportSize.x,
                viewportPos.y + (-ndc.y * 0.5f + 0.5f) * viewportSize.y);
            return true;
        }

        bool DrawWorldHealthBar(ImDrawList* drawList,
                                const glm::vec3& worldPos,
                                const float fraction,
                                const ImVec2 size,
                                const ImVec2 screenOffset,
                                const ImU32 fillOverride,
                                const NextGameInstanceBase* gameInstance)
        {
            if (drawList == nullptr)
            {
                return false;
            }

            ImVec2 screenPos{};
            if (!TryProjectWorldToScreen(worldPos, screenPos, gameInstance))
            {
                return false;
            }

            const float s = GetUiScale();
            const ImVec2 scaledSize(size.x * s, size.y * s);
            const ImVec2 topLeft(
                std::floor(screenPos.x + screenOffset.x * s - scaledSize.x * 0.5f),
                std::floor(screenPos.y + screenOffset.y * s - scaledSize.y * 0.5f));

            const ImU32 fillCol = (fillOverride != 0) ? fillOverride : GetHealthColorU32(fraction);
            DrawStatusBar(drawList, topLeft, scaledSize, fraction, fillCol);
            return true;
        }

        bool DrawWorldBillboardTag(ImDrawList* drawList,
                                   const glm::vec3& worldPos,
                                   const std::string_view text,
                                   const ImU32 accentCol,
                                   const ImVec2 screenOffset,
                                   const NextGameInstanceBase* gameInstance)
        {
            if (drawList == nullptr || text.empty())
            {
                return false;
            }

            ImVec2 screenPos{};
            if (!TryProjectWorldToScreen(worldPos, screenPos, gameInstance))
            {
                return false;
            }

            const float s = GetUiScale();
            ImFont* font = ImGui::GetFont();
            const float fontSize = ImGui::GetFontSize();
            const ImVec2 textSize = CalcTextSizeWithSize(font, fontSize, text);

            const float padX = 7.0f * s;
            const float padY = 3.0f * s;
            const ImVec2 boxSize(textSize.x + padX * 2.0f, textSize.y + padY * 2.0f);
            const ImVec2 center(screenPos.x + screenOffset.x * s, screenPos.y + screenOffset.y * s);
            const ImVec2 pMin(std::floor(center.x - boxSize.x * 0.5f), std::floor(center.y - boxSize.y * 0.5f));
            const ImVec2 pMax(pMin.x + boxSize.x, pMin.y + boxSize.y);

            const float rounding = 4.0f * s;
            const ImU32 bgCol = ColorU32(EColor::SurfaceElevated, 0.88f);
            const ImU32 borderCol = (accentCol != 0) ? accentCol : ColorU32(EColor::Border, 0.90f);

            drawList->AddRectFilled(pMin, pMax, bgCol, rounding);
            drawList->AddRect(pMin, pMax, borderCol, rounding, 0, 1.0f);
            drawList->AddText(font, fontSize, ImVec2(pMin.x + padX, pMin.y + padY), ColorU32(EColor::Text),
                              text.data(), text.data() + text.size());
            return true;
        }

        bool DrawWorldFloatingText(ImDrawList* drawList,
                                   const glm::vec3& worldPos,
                                   const std::string_view text,
                                   const float normalizedAge,
                                   const ImU32 baseCol,
                                   const float risePixels,
                                   const float fontSize,
                                   const NextGameInstanceBase* gameInstance)
        {
            if (drawList == nullptr || text.empty())
            {
                return false;
            }

            const float t = std::clamp(normalizedAge, 0.0f, 1.0f);
            if (t >= 1.0f)
            {
                return false;
            }

            ImVec2 screenPos{};
            if (!TryProjectWorldToScreen(worldPos, screenPos, gameInstance))
            {
                return false;
            }

            const float s = GetUiScale();
            const float alphaMul = 1.0f - t * t;
            ImVec4 colVec = ImGui::ColorConvertU32ToFloat4(baseCol);
            colVec.w *= alphaMul;

            const ImU32 fadedCol = ImGui::ColorConvertFloat4ToU32(colVec);
            const ImU32 shadowCol = IM_COL32(0, 0, 0, static_cast<int>(180.0f * alphaMul));

            const float sz = ResolveFontSize(fontSize);
            ImFont* font = ImGui::GetFont();
            const ImVec2 textSize = CalcTextSizeWithSize(font, sz, text);
            const ImVec2 pos(
                std::floor(screenPos.x - textSize.x * 0.5f),
                std::floor(screenPos.y - risePixels * s * t - textSize.y * 0.5f));

            DrawTextShadowed(drawList, pos, fadedCol, text, sz, shadowCol);
            return true;
        }

        void DrawCrosshair(ImDrawList* drawList,
                           const ImVec2 center,
                           const float gap,
                           const float length,
                           const float thickness,
                           const ImU32 col,
                           const bool centerDot)
        {
            if (drawList == nullptr)
            {
                return;
            }

            const float s = GetUiScale();
            const float g = gap * s;
            const float l = length * s;
            const float t = std::max(1.0f, thickness * s);
            const ImU32 shadowCol = IM_COL32(0, 0, 0, 140);

            drawList->AddLine(ImVec2(center.x - g - l, center.y + 1.0f), ImVec2(center.x - g, center.y + 1.0f), shadowCol, t);
            drawList->AddLine(ImVec2(center.x + g, center.y + 1.0f), ImVec2(center.x + g + l, center.y + 1.0f), shadowCol, t);
            drawList->AddLine(ImVec2(center.x + 1.0f, center.y - g - l), ImVec2(center.x + 1.0f, center.y - g), shadowCol, t);
            drawList->AddLine(ImVec2(center.x + 1.0f, center.y + g), ImVec2(center.x + 1.0f, center.y + g + l), shadowCol, t);

            drawList->AddLine(ImVec2(center.x - g - l, center.y), ImVec2(center.x - g, center.y), col, t);
            drawList->AddLine(ImVec2(center.x + g, center.y), ImVec2(center.x + g + l, center.y), col, t);
            drawList->AddLine(ImVec2(center.x, center.y - g - l), ImVec2(center.x, center.y - g), col, t);
            drawList->AddLine(ImVec2(center.x, center.y + g), ImVec2(center.x, center.y + g + l), col, t);

            if (centerDot)
            {
                drawList->AddCircleFilled(center, std::max(1.5f, t * 0.75f), col);
            }
        }

        void DrawSelectionBox(ImDrawList* drawList,
                              const ImVec2 pMin,
                              const ImVec2 pMax,
                              const ImU32 accentCol)
        {
            if (drawList == nullptr)
            {
                return;
            }

            const ImVec2 bMin(std::min(pMin.x, pMax.x), std::min(pMin.y, pMax.y));
            const ImVec2 bMax(std::max(pMin.x, pMax.x), std::max(pMin.y, pMax.y));

            const ImVec4 baseVec = (accentCol != 0)
                ? ImGui::ColorConvertU32ToFloat4(accentCol)
                : Color(EColor::Accent);

            const ImU32 fillCol = ImGui::ColorConvertFloat4ToU32(ImVec4(baseVec.x, baseVec.y, baseVec.z, 0.16f));
            const ImU32 borderCol = ImGui::ColorConvertFloat4ToU32(ImVec4(baseVec.x, baseVec.y, baseVec.z, 0.85f));

            drawList->AddRectFilled(bMin, bMax, fillCol, 2.0f);
            drawList->AddRect(bMin, bMax, borderCol, 2.0f, 0, 1.5f);
        }

        ImVec2 DrawKeyHintPill(const std::string_view keyText,
                               const std::string_view actionText,
                               const ImU32 keyAccentCol)
        {
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            const float s = GetUiScale();
            ImFont* font = ImGui::GetFont();
            const float fontSize = ImGui::GetFontSize();

            const ImVec2 keyTextSize = CalcTextSizeWithSize(font, fontSize, keyText);
            const ImVec2 actionTextSize = CalcTextSizeWithSize(font, fontSize, actionText);

            const float keyPadX = 6.0f * s;
            const float keyPadY = 2.0f * s;
            const float gap = 6.0f * s;
            const float pillHeight = std::max(keyTextSize.y, actionTextSize.y) + keyPadY * 2.0f;
            const float keyBoxWidth = std::max(pillHeight, keyTextSize.x + keyPadX * 2.0f);
            const float totalWidth = keyBoxWidth + (actionText.empty() ? 0.0f : (gap + actionTextSize.x));

            const ImVec2 pos = ImGui::GetCursorScreenPos();
            const ImVec2 keyMax(pos.x + keyBoxWidth, pos.y + pillHeight);
            const float rounding = 4.0f * s;

            const ImU32 keyBg = ColorU32(EColor::SurfaceElevated, 0.92f);
            const ImU32 keyBorder = (keyAccentCol != 0) ? keyAccentCol : ColorU32(EColor::BorderStrong, 0.90f);
            const ImU32 keyCol = (keyAccentCol != 0) ? keyAccentCol : ColorU32(EColor::Text);

            drawList->AddRectFilled(pos, keyMax, keyBg, rounding);
            drawList->AddRect(pos, keyMax, keyBorder, rounding, 0, 1.0f);
            drawList->AddText(font, fontSize,
                              ImVec2(std::floor(pos.x + (keyBoxWidth - keyTextSize.x) * 0.5f),
                                     std::floor(pos.y + (pillHeight - keyTextSize.y) * 0.5f)),
                              keyCol, keyText.data(), keyText.data() + keyText.size());

            if (!actionText.empty())
            {
                drawList->AddText(font, fontSize,
                                  ImVec2(std::floor(keyMax.x + gap),
                                         std::floor(pos.y + (pillHeight - actionTextSize.y) * 0.5f)),
                                  ColorU32(EColor::TextMuted), actionText.data(), actionText.data() + actionText.size());
            }

            const ImVec2 totalSize(totalWidth, pillHeight);
            ImGui::Dummy(totalSize);
            return totalSize;
        }
    }
}
