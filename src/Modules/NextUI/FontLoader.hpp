#pragma once

#include "Engine/Common/CoreMinimal.hpp"

#include <imgui.h>
#include <string_view>

namespace NextUI::FontLoader
{
    struct FFontRequest
    {
        std::string filePath;
        float pixelSize = 16.0f;
        bool includeChineseFull = false;
        const char* extraGlyphsUtf8 = nullptr;
        const ImWchar* glyphRanges = nullptr;
        const ImFontConfig* fontConfig = nullptr;
        float rasterizerDensity = 1.0f;
        bool warnOnFailure = true;
        bool setAsDefault = false;
    };

    const ImWchar* GetFontAwesomeGlyphRanges();
    void MergeFontAwesomeIcons(float basePixelSize, float rasterizerDensity = 1.0f);
    ImFont* Load(const FFontRequest& request);
    ImFont* LoadDefaultUiFont(std::string_view fontPath = "assets/fonts/Roboto-Regular.ttf",
                              float pixelSize = 16.0f,
                              float rasterizerDensity = 1.0f,
                              bool mergeIconsAndFallback = true);
}
