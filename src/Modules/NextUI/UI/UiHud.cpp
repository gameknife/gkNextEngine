#include "Modules/NextUI/UI/UiHud.hpp"

#include "Engine/Runtime/Utilities/NextEngineHelper.hpp"
#include "Modules/NextUI/ImGuiScaling.hpp"

#include <algorithm>
#include <cmath>

namespace NextUI::Hud
{
    namespace
    {
        float ResolveFontSize(float requestedFontSize) noexcept
        {
            if (requestedFontSize > 0.0f)
            {
                return requestedFontSize;
            }
            return ImGui::GetFontSize();
        }

        ImVec2 CalcTextSizeWithSize(ImFont* font, float fontSize, std::string_view text) noexcept
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
    } // namespace

    ImVec4 GetHealthColor(float fraction, float alpha) noexcept
    {
        const float clamped = std::clamp(fraction, 0.0f, 1.0f);
        if (clamped > 0.5f)
        {
            return Foundation::Color(Foundation::EColor::Success, alpha);
        }
        if (clamped > 0.25f)
        {
            return Foundation::Color(Foundation::EColor::Warning, alpha);
        }
        return Foundation::Color(Foundation::EColor::Danger, alpha);
    }

    ImU32 GetHealthColorU32(float fraction, float alpha) noexcept
    {
        return ImGui::ColorConvertFloat4ToU32(GetHealthColor(fraction, alpha));
    }

    void DrawTextShadowed(
        ImDrawList* drawList,
        ImVec2 pos,
        ImU32 col,
        std::string_view text,
        float fontSize,
        ImU32 shadowCol,
        ImVec2 shadowOffset)
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

    void DrawTextCentered(
        ImDrawList* drawList,
        ImVec2 center,
        ImU32 col,
        std::string_view text,
        float fontSize,
        bool shadowed)
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

    void DrawTextOutlined(
        ImDrawList* drawList,
        ImVec2 pos,
        ImU32 col,
        std::string_view text,
        float fontSize,
        ImU32 outlineCol)
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

    void DrawStatusBar(
        ImDrawList* drawList,
        ImVec2 pos,
        ImVec2 size,
        float fraction,
        ImU32 fillCol,
        const FStatusBarOptions& opts)
    {
        if (drawList == nullptr || size.x <= 0.0f || size.y <= 0.0f)
        {
            return;
        }

        const float clamped = std::clamp(fraction, 0.0f, 1.0f);
        const ImU32 bgCol = (opts.bgCol != 0)
            ? opts.bgCol
            : Foundation::ColorU32(Foundation::EColor::Background, 0.85f);
        const ImU32 borderCol = (opts.borderCol != 0)
            ? opts.borderCol
            : Foundation::ColorU32(Foundation::EColor::Border, 0.90f);
        const float rounding = (opts.rounding >= 0.0f)
            ? opts.rounding
            : std::min(4.0f * Scaling::GetUiScale(), size.y * 0.5f);

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
            const ImU32 textCol = (opts.textCol != 0)
                ? opts.textCol
                : Foundation::ColorU32(Foundation::EColor::Text);
            DrawTextCentered(
                drawList,
                ImVec2(pos.x + size.x * 0.5f, pos.y + size.y * 0.5f),
                textCol,
                opts.centerText,
                0.0f,
                true);
        }
    }

    bool TryProjectWorldToScreen(
        const glm::vec3& worldPos,
        ImVec2& outScreenPos,
        const NextGameInstanceBase* gameInstance)
    {
        if (gameInstance != nullptr)
        {
            return Runtime::EngineHelper::TryProjectWorldToScreenForGame(*gameInstance, worldPos, outScreenPos);
        }
        return Runtime::EngineHelper::TryProjectWorldToScreen(worldPos, outScreenPos);
    }

    bool TryProjectWorldToScreen(
        const glm::mat4& viewProjection,
        ImVec2 viewportPos,
        ImVec2 viewportSize,
        const glm::vec3& worldPos,
        ImVec2& outScreenPos,
        float ndcLimit) noexcept
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

    bool DrawWorldHealthBar(
        ImDrawList* drawList,
        const glm::vec3& worldPos,
        float fraction,
        ImVec2 size,
        ImVec2 screenOffset,
        ImU32 fillOverride,
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

        const float s = Scaling::GetUiScale();
        const ImVec2 scaledSize(size.x * s, size.y * s);
        const ImVec2 topLeft(
            std::floor(screenPos.x + screenOffset.x * s - scaledSize.x * 0.5f),
            std::floor(screenPos.y + screenOffset.y * s - scaledSize.y * 0.5f));

        const ImU32 fillCol = (fillOverride != 0) ? fillOverride : GetHealthColorU32(fraction);
        DrawStatusBar(drawList, topLeft, scaledSize, fraction, fillCol);
        return true;
    }

    bool DrawWorldBillboardTag(
        ImDrawList* drawList,
        const glm::vec3& worldPos,
        std::string_view text,
        ImU32 accentCol,
        ImVec2 screenOffset,
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

        const float s = Scaling::GetUiScale();
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
        const ImU32 bgCol = Foundation::ColorU32(Foundation::EColor::SurfaceElevated, 0.88f);
        const ImU32 borderCol = (accentCol != 0)
            ? accentCol
            : Foundation::ColorU32(Foundation::EColor::Border, 0.90f);

        drawList->AddRectFilled(pMin, pMax, bgCol, rounding);
        drawList->AddRect(pMin, pMax, borderCol, rounding, 0, 1.0f);
        drawList->AddText(
            font,
            fontSize,
            ImVec2(pMin.x + padX, pMin.y + padY),
            Foundation::ColorU32(Foundation::EColor::Text),
            text.data(),
            text.data() + text.size());

        return true;
    }

    bool DrawWorldFloatingText(
        ImDrawList* drawList,
        const glm::vec3& worldPos,
        std::string_view text,
        float normalizedAge,
        ImU32 baseCol,
        float risePixels,
        float fontSize,
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

        const float s = Scaling::GetUiScale();
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

    void DrawCrosshair(
        ImDrawList* drawList,
        ImVec2 center,
        float gap,
        float length,
        float thickness,
        ImU32 col,
        bool centerDot)
    {
        if (drawList == nullptr)
        {
            return;
        }

        const float s = Scaling::GetUiScale();
        const float g = gap * s;
        const float l = length * s;
        const float t = std::max(1.0f, thickness * s);
        const ImU32 shadowCol = IM_COL32(0, 0, 0, 140);

        // Subtle shadow behind crosshair arms
        drawList->AddLine(ImVec2(center.x - g - l, center.y + 1.0f), ImVec2(center.x - g, center.y + 1.0f), shadowCol, t);
        drawList->AddLine(ImVec2(center.x + g, center.y + 1.0f), ImVec2(center.x + g + l, center.y + 1.0f), shadowCol, t);
        drawList->AddLine(ImVec2(center.x + 1.0f, center.y - g - l), ImVec2(center.x + 1.0f, center.y - g), shadowCol, t);
        drawList->AddLine(ImVec2(center.x + 1.0f, center.y + g), ImVec2(center.x + 1.0f, center.y + g + l), shadowCol, t);

        // Crisp arms
        drawList->AddLine(ImVec2(center.x - g - l, center.y), ImVec2(center.x - g, center.y), col, t);
        drawList->AddLine(ImVec2(center.x + g, center.y), ImVec2(center.x + g + l, center.y), col, t);
        drawList->AddLine(ImVec2(center.x, center.y - g - l), ImVec2(center.x, center.y - g), col, t);
        drawList->AddLine(ImVec2(center.x, center.y + g), ImVec2(center.x, center.y + g + l), col, t);

        if (centerDot)
        {
            drawList->AddCircleFilled(center, std::max(1.5f, t * 0.75f), col);
        }
    }

    void DrawSelectionBox(
        ImDrawList* drawList,
        ImVec2 pMin,
        ImVec2 pMax,
        ImU32 accentCol)
    {
        if (drawList == nullptr)
        {
            return;
        }

        const ImVec2 bMin(std::min(pMin.x, pMax.x), std::min(pMin.y, pMax.y));
        const ImVec2 bMax(std::max(pMin.x, pMax.x), std::max(pMin.y, pMax.y));

        const ImVec4 baseVec = (accentCol != 0)
            ? ImGui::ColorConvertU32ToFloat4(accentCol)
            : Foundation::Color(Foundation::EColor::Accent);

        const ImU32 fillCol = ImGui::ColorConvertFloat4ToU32(ImVec4(baseVec.x, baseVec.y, baseVec.z, 0.16f));
        const ImU32 borderCol = ImGui::ColorConvertFloat4ToU32(ImVec4(baseVec.x, baseVec.y, baseVec.z, 0.85f));

        drawList->AddRectFilled(bMin, bMax, fillCol, 2.0f);
        drawList->AddRect(bMin, bMax, borderCol, 2.0f, 0, 1.5f);
    }

    ImVec2 DrawKeyHintPill(
        std::string_view keyText,
        std::string_view actionText,
        ImU32 keyAccentCol)
    {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const float s = Scaling::GetUiScale();
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

        const ImU32 keyBg = Foundation::ColorU32(Foundation::EColor::SurfaceElevated, 0.92f);
        const ImU32 keyBorder = (keyAccentCol != 0)
            ? keyAccentCol
            : Foundation::ColorU32(Foundation::EColor::BorderStrong, 0.90f);
        const ImU32 keyCol = (keyAccentCol != 0)
            ? keyAccentCol
            : Foundation::ColorU32(Foundation::EColor::Text);

        drawList->AddRectFilled(pos, keyMax, keyBg, rounding);
        drawList->AddRect(pos, keyMax, keyBorder, rounding, 0, 1.0f);
        drawList->AddText(
            font,
            fontSize,
            ImVec2(
                std::floor(pos.x + (keyBoxWidth - keyTextSize.x) * 0.5f),
                std::floor(pos.y + (pillHeight - keyTextSize.y) * 0.5f)),
            keyCol,
            keyText.data(),
            keyText.data() + keyText.size());

        if (!actionText.empty())
        {
            drawList->AddText(
                font,
                fontSize,
                ImVec2(
                    std::floor(keyMax.x + gap),
                    std::floor(pos.y + (pillHeight - actionTextSize.y) * 0.5f)),
                Foundation::ColorU32(Foundation::EColor::TextMuted),
                actionText.data(),
                actionText.data() + actionText.size());
        }

        const ImVec2 totalSize(totalWidth, pillHeight);
        ImGui::Dummy(totalSize);
        return totalSize;
    }
} // namespace NextUI::Hud
