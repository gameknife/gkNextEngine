#include "Modules/NextUI/FontLoader.hpp"

#include "Engine/Utilities/FileHelper.hpp"
#include "ThirdParty/fontawesome/IconsFontAwesome6.h"

#include <algorithm>
#include <filesystem>
#include <fmt/format.h>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace
{
    const ImWchar* ResolveGlyphRanges(ImFontAtlas& fontAtlas, bool includeChineseFull, const char* extraGlyphsUtf8)
    {
        if (!includeChineseFull && (!extraGlyphsUtf8 || extraGlyphsUtf8[0] == '\0'))
        {
            return fontAtlas.GetGlyphRangesDefault();
        }

        static std::unordered_map<std::string, ImVector<ImWchar>> cachedRanges;
        const std::string key = fmt::format("{}:{}", includeChineseFull ? "zh" : "default",
                                            extraGlyphsUtf8 ? extraGlyphsUtf8 : "");
        auto [iter, inserted] = cachedRanges.try_emplace(key);
        if (inserted)
        {
            ImFontGlyphRangesBuilder builder;
            builder.AddRanges(includeChineseFull ? fontAtlas.GetGlyphRangesChineseFull() : fontAtlas.GetGlyphRangesDefault());
            if (extraGlyphsUtf8 && extraGlyphsUtf8[0] != '\0')
            {
                builder.AddText(extraGlyphsUtf8);
            }
            builder.BuildRanges(&iter->second);
        }

        return iter->second.Data;
    }
}

namespace NextUI::FontLoader
{
    const ImWchar* GetFontAwesomeGlyphRanges()
    {
        static const ImWchar iconRange[] = {
            ICON_MIN_FA,
            ICON_MAX_FA,
            0,
        };
        return iconRange;
    }

    void MergeFontAwesomeIcons(const float basePixelSize, const float rasterizerDensity)
    {
        ImFontConfig iconConfig;
        iconConfig.MergeMode = true;
        iconConfig.GlyphMinAdvanceX = basePixelSize;
        iconConfig.GlyphOffset = ImVec2(0.0f, 0.0f);

        const float iconPixelSize = std::max(10.0f, basePixelSize - 2.0f);
        const ImWchar* iconRange = GetFontAwesomeGlyphRanges();
        constexpr const char* kIconFonts[] = {
            "assets/fonts/fa-regular-400.ttf",
            "assets/fonts/fa-solid-900.ttf",
            "assets/fonts/fa-brands-400.ttf",
        };

        for (const char* iconFontPath : kIconFonts)
        {
            Load(FFontRequest{
                .filePath = iconFontPath,
                .pixelSize = iconPixelSize,
                .includeChineseFull = false,
                .glyphRanges = iconRange,
                .fontConfig = &iconConfig,
                .rasterizerDensity = rasterizerDensity,
                .warnOnFailure = false,
            });
        }
    }

    ImFont* Load(const FFontRequest& request)
    {
        if (request.filePath.empty() || request.pixelSize <= 0.0f)
        {
            return nullptr;
        }

        ImGuiIO& io = ImGui::GetIO();
        const std::filesystem::path loosePath = Utilities::FileHelper::GetRuntimeFilePath(request.filePath);
        const ImWchar* ranges = request.glyphRanges ? request.glyphRanges :
            ResolveGlyphRanges(*io.Fonts, request.includeChineseFull, request.extraGlyphsUtf8);
        ImFontConfig fontConfig;
        const ImFontConfig* fontConfigPtr = request.fontConfig;
        if (request.rasterizerDensity > 0.0f && request.rasterizerDensity != 1.0f)
        {
            if (request.fontConfig != nullptr)
            {
                fontConfig = *request.fontConfig;
            }
            fontConfig.RasterizerDensity = request.rasterizerDensity;
            fontConfigPtr = &fontConfig;
        }

        ImFont* font = nullptr;
        if (std::filesystem::is_regular_file(loosePath))
        {
            font = io.Fonts->AddFontFromFileTTF(loosePath.string().c_str(), request.pixelSize, fontConfigPtr, ranges);
        }
        else
        {
            std::vector<uint8_t> fontData;
            if (Utilities::Package::FPackageFileSystem::GetInstance().LoadFile(request.filePath, fontData) && !fontData.empty())
            {
                void* data = IM_ALLOC(fontData.size());
                std::memcpy(data, fontData.data(), fontData.size());
                font = io.Fonts->AddFontFromMemoryTTF(data,
                                                      static_cast<int>(fontData.size()),
                                                      request.pixelSize,
                                                      fontConfigPtr,
                                                      ranges);
            }
        }

        if (!font)
        {
            if (request.warnOnFailure)
            {
                SPDLOG_ERROR("[FontLoader] failed to load font '{}'", request.filePath);
            }
            return nullptr;
        }

        if (request.setAsDefault)
        {
            io.FontDefault = font;
        }
        return font;
    }

    ImFont* LoadDefaultUiFont(std::string_view fontPath, const float pixelSize, const float rasterizerDensity,
                              const bool mergeIconsAndFallback)
    {
        ImFont* font = Load(FFontRequest{
            .filePath = std::string(fontPath),
            .pixelSize = pixelSize,
            .includeChineseFull = true,
            .rasterizerDensity = rasterizerDensity,
            .warnOnFailure = false,
            .setAsDefault = true,
        });

        constexpr std::string_view kFallbackFontPath = "assets/fonts/DroidSansFallback.ttf";
        if (font == nullptr && fontPath != kFallbackFontPath)
        {
            font = Load(FFontRequest{
                .filePath = std::string(kFallbackFontPath),
                .pixelSize = pixelSize,
                .includeChineseFull = true,
                .rasterizerDensity = rasterizerDensity,
                .warnOnFailure = true,
                .setAsDefault = true,
            });
            fontPath = kFallbackFontPath;
        }

        if (font != nullptr && mergeIconsAndFallback)
        {
            MergeFontAwesomeIcons(pixelSize, rasterizerDensity);

            if (fontPath != kFallbackFontPath)
            {
                ImFontConfig fallbackConfig;
                fallbackConfig.MergeMode = true;
                Load(FFontRequest{
                    .filePath = std::string(kFallbackFontPath),
                    .pixelSize = pixelSize,
                    .glyphRanges = ImGui::GetIO().Fonts->GetGlyphRangesChineseSimplifiedCommon(),
                    .fontConfig = &fallbackConfig,
                    .rasterizerDensity = rasterizerDensity,
                    .warnOnFailure = false,
                });
            }
        }
        return font;
    }
}
