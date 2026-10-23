#pragma once
// DirectWrite measurement and casing for the design library: the native
// implementation of modules::design::TextMeasurer (hud_design_text.hpp) over
// the shared LayerRasterizer, i.e. HUDText.width / HUDText.metrics
// (Sources/HUDDesignText.swift:36-58) with the font set of
// Sources/HUDDesignFonts.swift (HarmonyOS Sans + the region's Noto cascade,
// weight 600 -> 700, tnum; LIB-F12, F19, F20, F25, F27).
//
// Measurement uses LayerRasterizer::measureSourceText with the SAME design
// text descriptor the leaf is painted from (modules::design::textNode), so a
// measured width and the painted line agree byte for byte in font choice.
// Results are cached per (runs, region, font revision); the cache is dropped on
// a font revision change (language / font event). Content events only, UI
// thread; never on the frame path. No layout survives a call.
#include "modules/hud_design_control.hpp"
#include "modules/hud_design_text.hpp"
#ifdef _WIN32
#include "native/layer_raster.hpp"
#include <unordered_map>

namespace endfield::native {
class NativeDesignTextMeasurer final : public modules::design::TextMeasurer {
public:
    explicit NativeDesignTextMeasurer(LayerRasterizer&);
    ~NativeDesignTextMeasurer() override;
    double width(std::span<const modules::design::TextRun>, modules::design::TextRegion) override;
    modules::design::LineMetrics metrics(const modules::design::TextStyle&, modules::design::TextRegion, double scale = 1) override;
    struct Stats { std::uint64_t measurements{}, cacheHits{}, flushes{}; std::size_t entries{}; };
    Stats stats() const noexcept;
    // Bounded cache (oldest half dropped past `limit` entries).
    static constexpr std::size_t limit = 4096;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
// en_US_POSIX uppercase through ICU's root locale (u_strToUpper), the rule
// native/profile_text.cpp already applies (shared helper, not a copy).
modules::design::TextCasing nativeUppercase();
// Swift Character semantics (String.count, prefix(n): extended grapheme
// clusters through ICU's character break iterator) for maximumLength, secure
// bullets and redaction: graphemeCount / graphemePrefix (native/profile_text.cpp,
// the segmentation behind Profile's ProfileTextRules::characters / prefix,
// shared, not copied) on text of any length. The overlay owner puts them in
// the design Environment (Environment::characters).
modules::design::CharacterRules nativeCharacterRules();
} // namespace endfield::native
#endif
