#include "Engine/Common/CoreMinimal.hpp"
#include "BakeGlb.hpp"

#include "Application/Game/NextDayz/Data/ItemDefs.hpp"
#include "Application/Game/NextDayz/Net/ProtocolVersion.hpp"

#include <glm/gtc/packing.hpp>

namespace NextDayzBake
{
    namespace
    {
        void ExportIcon(const fs::path& source, const fs::path& destination)
        {
            const auto bytes = ReadBytes(source);
            constexpr std::string_view magic = "NDZICON1";
            constexpr size_t header = 32;
            if (bytes.size() < header || std::memcmp(bytes.data(), magic.data(), magic.size()) != 0)
            {
                throw std::runtime_error("Invalid icon header: " + source.string());
            }
            uint32_t width, height;
            uint64_t expected;
            std::memcpy(&width, bytes.data() + 16, sizeof(width));
            std::memcpy(&height, bytes.data() + 20, sizeof(height));
            std::memcpy(&expected, bytes.data() + 24, sizeof(expected));
            const auto pixels = std::span<const uint8_t>(bytes).subspan(header);
            if (!width || !height || width > 1024 || height > 1024 || pixels.size() != size_t{width} * height * 8)
            {
                throw std::runtime_error("Invalid icon size: " + source.string());
            }
            uint64_t hash = 14695981039346656037ull;
            for (const uint8_t byte : pixels) hash = (hash ^ byte) * 1099511628211ull;
            if (hash != expected) throw std::runtime_error("Invalid icon pixels: " + source.string());

            std::vector<uint8_t> rgba(pixels.size() / 2);
            for (size_t i = 0; i < rgba.size(); ++i)
            {
                uint16_t half;
                std::memcpy(&half, pixels.data() + i * 2, 2);
                float value = glm::unpackHalf1x16(half);
                if (!std::isfinite(value)) value = 0.f;
                value = std::clamp(value, 0.f, 1.f);
                if (i % 4 != 3)
                {
                    value = value <= 0.0031308f ? 12.92f * value : 1.055f * std::pow(value, 1.f / 2.4f) - 0.055f;
                }
                rgba[i] = static_cast<uint8_t>(std::lround(value * 255.f));
            }
            SaveWebp(destination, rgba.data(), width, height);
        }
    }

    void BakeShared(const fs::path& destination)
    {
        const fs::path translations = "src/Application/Game/NextDayz/UI/Translations.inl";
        std::vector<std::string> dependencies{
            translations.generic_string(), "tools/audio/nextdayz_sfx.py",
            "src/Application/Game/NextDayz/Data/ItemDefs.cpp",
            "src/Application/Game/NextDayz/UI/ItemIconCache.cpp",
            "assets/shaders/NextDayz.InventoryPreview.comp.slang",
        };
        fs::create_directories(destination / "i18n");
        Json english = Json::object(), chinese = Json::object();
#define ND_TEXT(en, zh) english.emplace(en, en); chinese.emplace(en, zh);
#include "Application/Game/NextDayz/UI/Translations.inl"
#undef ND_TEXT
        WriteJson(destination / "i18n/en.json", english);
        WriteJson(destination / "i18n/zh-CN.json", chinese);

        const fs::path audio = "assets/sounds/nextdayz";
        if (!fs::is_directory(audio))
        {
            throw std::runtime_error("Generate audio with tools/audio/nextdayz_sfx.py before baking shared assets");
        }
        fs::create_directories(destination / "audio");
        size_t audioCount = 0;
        for (const auto& entry : fs::directory_iterator(audio))
        {
            if (!entry.is_regular_file() || entry.path().extension() != ".wav") continue;
            fs::copy_file(entry.path(), destination / "audio" / entry.path().filename());
            dependencies.push_back(entry.path().generic_string());
            ++audioCount;
        }
        if (!audioCount) throw std::runtime_error("No NextDayz WAV files to bake");

        fs::create_directories(destination / "icons");
        const fs::path icons = "out/web/nextdayz/icons-desktop";
        const auto items = NextDayz::AllItemDefs();
        for (const auto& item : items)
        {
            const auto source = icons / (std::string(item.id) + ".thumb");
            if (!fs::is_regular_file(source))
            {
                throw std::runtime_error("Missing desktop icon " + source.string() + "; export the NextDayz item icon cache first");
            }
            ExportIcon(source, destination / "icons" / (std::string(item.id) + ".webp"));
            dependencies.push_back(source.generic_string());
        }
        WriteJson(destination / "bake.json", {{"protocol", NextDayz::Net::kProtocolVersion},
            {"dependencies", dependencies}, {"audio", audioCount}, {"icons", items.size()}, {"translations", chinese.size()}});
    }
}
