#pragma once

#include <string>
#include <cstdint>
#include <filesystem>
#include <string_view>
#include <utility>
#include <type_traits>

#include <fmt/format.h>

namespace NextDayz::L10n
{
    enum class ELanguage { Chinese, English };

    ELanguage Language();
    void SetLanguage(ELanguage language);
    std::string_view Translate(std::string_view source, ELanguage language);
    std::string Localize(std::string_view source, ELanguage language);
    const char* Tr(const char* source);
    const char* Tr(const std::string& source);
    std::string_view Tr(std::string_view source);
    // Visible labels change language while their ImGui IDs remain stable.
    const char* Label(const char* source);
    void Initialize(bool validation, std::filesystem::path preferencePath = {});
    void DrawLanguageSelector();

    struct FText;
    namespace Detail
    {
        const std::string& Argument(const FText& text, ELanguage language);
        template <typename T>
        decltype(auto) Argument(const T& value, ELanguage language)
        {
            if constexpr (std::is_convertible_v<const T&, std::string_view>)
                return Localize(std::string_view(value), language);
            else
                return (value);
        }
    }

    template <typename... Args>
    std::string Format(fmt::format_string<Args...> source, Args&&... args)
    {
        const auto language = Language();
        const auto format = fmt::string_view(source);
        return fmt::format(fmt::runtime(Translate({format.data(), format.size()}, language)),
                           Detail::Argument(args, language)...);
    }

    // Queued messages retain both languages so switching also updates history.
    struct FText
    {
        std::string english;
        std::string chinese;

        FText() = default;
        FText(const char* source) : FText(std::string(source)) {}
        FText(std::string source)
            : english(std::move(source)), chinese(Localize(english, ELanguage::Chinese)) {}
        FText(std::string en, std::string zh) : english(std::move(en)), chinese(std::move(zh)) {}
        const std::string& Display() const { return Language() == ELanguage::Chinese ? chinese : english; }
        const char* c_str() const { return Display().c_str(); }
        bool empty() const { return english.empty(); }
        bool starts_with(std::string_view prefix) const { return english.starts_with(prefix); }
        void clear() { english.clear(); chinese.clear(); }
        FText& operator+=(const FText& other)
        {
            english += other.english;
            chinese += other.chinese;
            return *this;
        }
        bool operator==(const FText&) const = default;
        friend FText operator+(FText first, const FText& second) { return first += second; }
    };

    namespace Detail
    {
        inline const std::string& Argument(const FText& text, ELanguage language)
        {
            return language == ELanguage::Chinese ? text.chinese : text.english;
        }
    }

    template <typename... Args>
    FText Message(fmt::format_string<Args...> source, Args&&... args)
    {
        const auto format = fmt::string_view(source);
        return {fmt::format(fmt::runtime(std::string_view(format.data(), format.size())),
                            Detail::Argument(args, ELanguage::English)...),
                fmt::format(fmt::runtime(Translate({format.data(), format.size()}, ELanguage::Chinese)),
                            Detail::Argument(args, ELanguage::Chinese)...)};
    }
}

template <>
struct fmt::formatter<NextDayz::L10n::FText> : fmt::formatter<std::string_view>
{
    template <typename FormatContext>
    auto format(const NextDayz::L10n::FText& text, FormatContext& context) const
    {
        return fmt::formatter<std::string_view>::format(text.Display(), context);
    }
};
