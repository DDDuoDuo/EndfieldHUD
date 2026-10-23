#pragma once
#include <array>
#include <cstdint>
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
// The CJK region of text (Mac HUDTextLanguage, Sources/HUDDesignFonts.swift
// :102-155), the one owner of these rules for the design library
// (modules/hud_design_text.hpp) and the rasterizer (native/font_resources.hpp).
// Latin letters and digits always come from HarmonyOS Sans; Han, Kana and
// Hangul from the Noto Sans family of this region (SC, TC, JP, KR), and the
// region's tag is every run's locale. There is no English region: English
// text is tagged with the UI's region, exactly as the Mac tags it.
enum class TextRegion : std::uint8_t { simplifiedChinese, traditionalChinese, japanese, korean };
inline constexpr std::array<TextRegion,4> textRegions{TextRegion::simplifiedChinese,TextRegion::traditionalChinese,
    TextRegion::japanese,TextRegion::korean};
// BCP 47 tags of the Mac ("zh-Hans", "zh-Hant", "ja", "ko"), used verbatim as
// DirectWrite locale names; only these exact tags name a region.
std::string_view regionTag(TextRegion) noexcept;
std::optional<TextRegion> regionFromTag(std::string_view) noexcept;
// HUDTextLanguage.tag(naming:): a language's own name (日本語 in the language
// menu) is set in its own region; English and System are untagged (nullopt).
std::optional<TextRegion> regionNaming(Language) noexcept;
// HUDTextLanguage.firstRegion(in:): the first preferred language that
// resolves (each alone, as L10n.resolveLanguage([identifier])) to a CJK
// language, zh-Hans when there is none. Windows passes
// GetUserPreferredUILanguages (native preferredSystemRegion, the one reader),
// the counterpart of Locale.preferredLanguages.
TextRegion firstRegion(std::span<const std::string_view> preferredLanguages);
// HUDTextLanguage.tag(for:): the UI language decides; English and System use
// `systemRegion` (firstRegion of the preferred languages).
TextRegion regionFor(Language, TextRegion systemRegion) noexcept;
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
