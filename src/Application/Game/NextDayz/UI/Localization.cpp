#include "Engine/Common/CoreMinimal.hpp"
#include "Localization.hpp"

#include <fstream>
#include <charconv>
#include <unordered_map>

#include <imgui.h>
#include <nlohmann/json.hpp>

#include "Engine/Runtime/Platform/UserPaths.hpp"

namespace NextDayz::L10n
{
    namespace
    {
        ELanguage CurrentLanguage = ELanguage::Chinese;
        bool PersistPreference = false;
        std::filesystem::path PreferencePath;

        const std::unordered_map<std::string_view, std::string_view>& Catalog()
        {
            static const std::unordered_map<std::string_view, std::string_view> catalog = {
#define ND_TEXT(en, zh) {en, zh},
#include "Translations.inl"
#undef ND_TEXT
            };
            return catalog;
        }
    }

    ELanguage Language() { return CurrentLanguage; }

    std::string_view Translate(std::string_view source, ELanguage language)
    {
        if (language == ELanguage::Chinese)
        {
            const auto& catalog = Catalog();
            if (const auto found = catalog.find(source); found != catalog.end()) return found->second;
        }
        return source;
    }

    const char* Tr(const char* source) { return Translate(source, CurrentLanguage).data(); }
    const char* Tr(const std::string& source) { return Translate(source, CurrentLanguage).data(); }
    std::string_view Tr(std::string_view source) { return Translate(source, CurrentLanguage); }

    std::string Localize(std::string_view source, ELanguage language)
    {
        const auto translated = Translate(source, language);
        if (language == ELanguage::English || translated != source) return std::string(translated);
        // LootSystem::DescribeContents stores canonical names with optional
        // quantities. Parse that small display grammar without changing saves.
        const size_t more = source.rfind(" (+");
        if (more != std::string_view::npos && source.ends_with(" more)"))
        {
            const auto number = source.substr(more + 3, source.size() - more - 9);
            size_t count = 0;
            const auto parsed = std::from_chars(number.data(), number.data() + number.size(), count);
            if (parsed.ec == std::errc() && parsed.ptr == number.data() + number.size())
                return fmt::format("{}（另有 {} 项）", Localize(source.substr(0, more), language), count);
        }
        const size_t quantity = source.rfind(" x");
        if (quantity != std::string_view::npos)
        {
            const auto number = source.substr(quantity + 2);
            size_t count = 0;
            const auto parsed = std::from_chars(number.data(), number.data() + number.size(), count);
            if (parsed.ec == std::errc() && parsed.ptr == number.data() + number.size())
                return fmt::format("{} x{}", Localize(source.substr(0, quantity), language), count);
        }
        const size_t separator = source.find(", ");
        if (separator != std::string_view::npos)
            return Localize(source.substr(0, separator), language) + "、" + Localize(source.substr(separator + 2), language);
        return std::string(source);
    }

    const char* Label(const char* source)
    {
        static std::unordered_map<std::string, std::array<std::string, 2>> labels;
        auto [found, inserted] = labels.try_emplace(source);
        if (inserted)
        {
            const std::string_view original(source);
            const size_t suffix = original.find("##");
            const auto visible = original.substr(0, suffix);
            const auto id = original.find("###");
            const std::string stableId(id == std::string_view::npos ? original : original.substr(id + 3));
            found->second = {std::string(Translate(visible, ELanguage::Chinese)) + "###" + stableId,
                             std::string(visible) + "###" + stableId};
        }
        return found->second[CurrentLanguage == ELanguage::Chinese ? 0 : 1].c_str();
    }

    void SetLanguage(ELanguage language)
    {
        CurrentLanguage = language;
        if (!PersistPreference) return;
        try
        {
            std::filesystem::create_directories(PreferencePath.parent_path());
            std::ofstream output(PreferencePath);
            output << nlohmann::json{{"language", language == ELanguage::Chinese ? "zhCN" : "en"}}.dump(2);
            if (!output) SPDLOG_WARN("[NextDayz] failed to save language preference: {}", PreferencePath.string());
        }
        catch (const std::exception& error)
        {
            SPDLOG_WARN("[NextDayz] failed to save language preference: {}", error.what());
        }
    }

    void Initialize(bool validation, std::filesystem::path preferencePath)
    {
        PersistPreference = !validation;
        CurrentLanguage = ELanguage::Chinese;
        if (!validation)
        {
            PreferencePath = preferencePath.empty()
                ? NextPlatform::UserPaths::GetUserDataDir("NextDayz") / "settings.json" : std::move(preferencePath);
            std::ifstream input(PreferencePath);
            const auto settings = nlohmann::json::parse(input, nullptr, false);
            if (settings.is_object() && settings.contains("language") && settings["language"].is_string() && settings["language"] == "en")
                CurrentLanguage = ELanguage::English;
        }
    }

    void DrawLanguageSelector()
    {
        ImGui::PushID("NextDayzLanguage");
        ImGui::TextUnformatted("语言 / Language (F12)");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(130.0f);
        int language = CurrentLanguage == ELanguage::Chinese ? 0 : 1;
        const char* languages[] = {"简体中文", "English"};
        if (ImGui::Combo("##language", &language, languages, 2))
            SetLanguage(language == 0 ? ELanguage::Chinese : ELanguage::English);
        ImGui::PopID();
    }
}
