#pragma once

#include "Engine/Common/CoreMinimal.hpp"

#include <imgui.h>

namespace KongLie3D::Style
{
    inline constexpr float UiScale = 1.5f;
    inline constexpr ImVec4 Accent(0.24f, 0.56f, 0.96f, 1.0f);
    inline constexpr ImVec4 Hostile(0.90f, 0.29f, 0.28f, 1.0f);
    inline constexpr ImVec4 Highlight(0.94f, 0.67f, 0.18f, 1.0f);
    inline constexpr ImVec4 Surface(0.118f, 0.125f, 0.137f, 0.96f);
    inline constexpr ImVec4 SurfaceAlt(0.145f, 0.153f, 0.169f, 0.96f);
    inline constexpr ImVec4 Border(0.220f, 0.235f, 0.259f, 0.85f);
    inline constexpr ImVec4 TextDim(0.58f, 0.61f, 0.66f, 1.0f);
}

namespace KongLie3D
{
    inline constexpr float ScaleUi(float value)
    {
        return value * Style::UiScale;
    }

    inline ImVec2 ScaleUi(float x, float y)
    {
        return ImVec2(ScaleUi(x), ScaleUi(y));
    }
}

namespace KongLie3D::KongLieFonts
{
    extern ImFont* Body;
    extern ImFont* Title;
    extern ImFont* Display;
}

namespace KongLie3D
{
    void ApplyKongLieImGuiStyle();
    void PushPanelStyle();
    void PopPanelStyle();
}
