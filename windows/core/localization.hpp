#pragma once
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace endfield::core {
enum class Language { system,english,simplifiedChinese,traditionalChinese,japanese,korean };
// Preference identifiers match the existing saved Mac settings verbatim.
std::optional<Language> languageFromSetting(std::string_view) noexcept;
std::string_view languageSetting(Language) noexcept;
Language resolveLanguage(std::span<const std::string_view> preferred);
bool isChinese(Language) noexcept;
bool isCJK(Language) noexcept;
struct TranslationEntry {
    std::string_view english,simplifiedChinese,traditionalChinese,japanese,korean;
};
std::span<const TranslationEntry> translationCatalog() noexcept;
const TranslationEntry* translationEntry(std::string_view english,std::string_view simplified) noexcept;
// Each numeric placeholder is rendered once. Inserted text is never rescanned.
std::optional<std::string> renderTranslation(std::string_view,std::span<const std::string_view> arguments);
// Caller resolves System once on a preference/OS-language notification, not
// every draw. Unknown source pairs return readable English (Chinese for zh).
std::string localized(std::string_view english,std::string_view simplified,Language,
    std::span<const std::string_view> arguments={});
}
