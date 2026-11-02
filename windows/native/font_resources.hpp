#pragma once
#include "core/localization.hpp"
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

struct IDWriteFactory;
struct IDWriteFontCollection;
struct IDWriteFontFallback;
struct IDWriteTextFormat;
namespace endfield::native {
class LayerRasterizer;
// The CJK region of text (Mac HUDTextLanguage): core::TextRegion, whose rules
// (tags, the UI-language decision, the first CJK preferred language, names)
// live once in core/localization.hpp for the rasterizer and the design library.
using LayerFontLanguage=core::TextRegion;
// The user's preferred UI languages in order (GetUserPreferredUILanguages
// MUI_LANGUAGE_NAME, the counterpart of Locale.preferredLanguages): the one
// reader of that list, called on configuration, WM_SETTINGCHANGE and startup
// dialogs only (never per draw). Not the regional format: dates and numbers
// keep GetUserDefaultLocaleName (native/calendar_civil.cpp).
#ifdef _WIN32
std::vector<std::string> preferredUILanguages();
// -> the first CJK region (core::firstRegion), zh-Hans when none (LIB-F14).
core::TextRegion preferredSystemRegion();
// -> the System UI language (core::resolveLanguage, L10n.resolveLanguage of
// Locale.preferredLanguages): the first en/ja/ko/zh entry, English when none (LIB-L1).
core::Language preferredSystemLanguage();
#endif
// HUDTextLanguage.systemFamily(for:): only when a region's Noto file is absent
// from fonts/ (a damaged install; the package ships all four). Windows
// counterparts of PingFang TC, Hiragino Sans, Apple SD Gothic Neo, PingFang SC.
constexpr std::string_view layerSystemFontFamily(LayerFontLanguage l)noexcept{
    return l==LayerFontLanguage::traditionalChinese?"Microsoft JhengHei UI":l==LayerFontLanguage::japanese?"Yu Gothic UI":l==LayerFontLanguage::korean?"Malgun Gothic":"Microsoft YaHei UI";}

// Immutable configuration-event snapshot of the application's existing font
// resources. COM references are owned, so layouts on the borrowed MTA worker
// may outlive the UI rasterizer. No loader, font installation, HWND or raster
// operation belongs to this value. Do not call the UI rasterizer on that worker.
// fallbackFamily() is the standard face (HarmonyOS Sans) and locale() the
// region tag; formats in that face need fontFallback() (applyFontFallback) for
// the region's Noto cascade, as every rasterizer format has it.
class LayerFontResources final {
public:
    LayerFontResources()=default;
    explicit operator bool()const noexcept;
    IDWriteFactory*factory()const noexcept;
    IDWriteFontCollection*systemCollection()const noexcept;
    IDWriteFontCollection*bundledCollection()const noexcept;
    IDWriteFontFallback*fontFallback()const noexcept;
    // Sets fontFallback() on a format of factory() (IDWriteTextFormat1).
    long applyFontFallback(IDWriteTextFormat*)const noexcept;
    std::string_view fallbackFamily()const noexcept;
    std::string_view locale()const noexcept;
    std::uint64_t revision()const noexcept;
private:
    struct Impl;std::shared_ptr<const Impl>impl_;
    LayerFontResources(IDWriteFactory*,IDWriteFontCollection*,IDWriteFontCollection*,IDWriteFontFallback*,
        std::string family,std::string locale,std::uint64_t revision);
    friend class LayerRasterizer;
};
} // namespace endfield::native
