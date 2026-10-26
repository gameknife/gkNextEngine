#pragma once

#include "Engine/Common/CoreMinimal.hpp"
#include "Modules/NextUI/UI/UiTheme.hpp"

#include <deque>
#include <imgui.h>

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
            ImVec4 infoAccent = Foundation::Color(Foundation::EColor::Blue);
            ImVec4 successAccent = Foundation::Color(Foundation::EColor::Success);
            ImVec4 warningAccent = Foundation::Color(Foundation::EColor::Warning);
            ImVec4 criticalAccent = Foundation::Color(Foundation::EColor::Danger);
            ImVec4 surface = Foundation::Color(Foundation::EColor::SurfaceElevated, 0.96f);
            ImVec4 text = Foundation::Color(Foundation::EColor::Text);
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
}
