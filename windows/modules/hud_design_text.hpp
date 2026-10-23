#pragma once
// Centre text: the port of Sources/HUDDesignText.swift (HUDText, HUDTextLayer)
// and of the language and weight rules of Sources/HUDDesignFonts.swift
// (HUDTextLanguage, HUDFontWeight), plus HUDModuleTitle
// (Sources/HUDDesignHeader.swift:7-25). LIB-17, LIB-18, LIB-22, LIB-F12,
// LIB-F14, LIB-F16, LIB-F25..F28.
//
// Split of work. The portable model decides WHAT is drawn: the runs (already
// uppercased with the POSIX locale), the region, the CSS line box, the line
// budget and the shrink-to-fit scale. Width and line metrics are asked from a
// TextMeasurer once per content event (the native implementation,
// native/hud_design_text_metrics.hpp, shapes with the exact paint resolver of
// the shared LayerRasterizer; tests inject a measurer that answers from the
// Mac oracle's recorded widths). The rasterizer decides HOW (the A1 text keys
// below): greedy breaking, the last allowed line taking all remaining text
// with an end ellipsis, uniform line boxes from the primary face's strut, the
// HarmonyOS + regional Noto cascade, per-layout locale and tnum. No text is
// shaped per frame and no layout survives a measurement call.
#include "modules/hud_design_tokens.hpp"
#include "core/data/json.hpp"
#include "core/localization.hpp"
#include <functional>
#include <span>
#include <string>
#include <vector>

namespace endfield::modules::design {
using Json = ehud::data::Json;

enum class TextAlignment : std::uint8_t { left, center, right };
struct TextRun {
    std::string text; // UTF-8, as authored (uppercase applies at display)
    TextStyle style;
    bool operator==(const TextRun&) const = default;
};

// CJK region of a text (HUDTextLanguage): core::TextRegion and its rules
// (core/localization.hpp), shared with the rasterizer's LayerFontLanguage.
// The UI language decides; English and System use the system region: the
// first entry of the user's preferred UI languages that resolves to a CJK
// language (Windows: native::preferredSystemRegion, re-read on
// WM_SETTINGCHANGE "intl"; LIB-F14/F15), zh-Hans when there is none.
// regionNaming: a language's own name is set in its own face (简体中文
// zh-Hans, 繁體中文 zh-Hant, 日本語 ja, 한국어 ko); English and System are
// untagged (LIB-F16).
using core::TextRegion;
using core::regionTag;
using core::regionFromTag;
using core::regionFor;
using core::firstRegion;
using core::regionNaming;

// Uppercase with en_US_POSIX (no Turkish dotted I; ß -> SS), never the UI
// locale (LIB-F26). The full Unicode mapping is injected: native code passes
// ICU's root-locale u_strToUpper, the rule native/profile_text.cpp already
// applies for Profile (share that helper, do not copy it); portable tests may
// pass asciiUppercase.
using TextCasing = std::function<std::string(std::string_view)>;
std::string asciiUppercase(std::string_view utf8);

// A CSS line box (HUDText.LineMetrics): height = size x lineHeight; ascent and
// descent of the PRIMARY face (HarmonyOS Sans at the resolved weight), so a
// fallback CJK glyph never enlarges the box (LIB-F27).
struct LineMetrics {
    double height{}, ascent{}, descent{};
    double baseline() const noexcept { return (height - ascent - descent) / 2 + ascent; }
    bool operator==(const LineMetrics&) const = default;
};
// Content-event measurement. width(): typographic advance of ONE line,
// including the tracking after the last glyph (CSS letter-spacing), rounded
// up to .5 px (ceil(w x 2) / 2, HUDText.width). Implementations cache by
// (runs, region) and must not allocate per frame; they are never called on
// the frame path.
class TextMeasurer {
public:
    virtual ~TextMeasurer() = default;
    virtual double width(std::span<const TextRun>, TextRegion) = 0;
    virtual LineMetrics metrics(const TextStyle&, TextRegion, double scale = 1) = 0;
    double width(std::string_view text, const TextStyle& style, TextRegion region);
    // HUDText.lines(_:width:maximumLines:truncate:).count: the lines greedy
    // breaking gives the runs in `width` (at most maximumLines; 0 unlimited;
    // the last allowed line takes the rest). Content heights that follow the
    // text use it (Calendar's event plates, File Shelf, OrbiPom). The default
    // is the width ratio (no break opportunities); the native measurer breaks
    // exactly like the raster (LayerRasterizer::measureSourceText).
    virtual std::size_t lineCount(std::span<const TextRun>, TextRegion, double width, int maximumLines);
};

// HUDTextLayer as a value: the one text leaf of the redesign. lineFrame is the
// CSS line box in the parent and the emitted node's bounds; the Mac's layer
// frame is lineFrame padded by framePadding (ceil(maxSize x .25) x
// ceil(maxSize x .45), frozen when lineFrame is set, LIB-F29). On Windows the
// rasterizer pads the line box itself, so the box text fits into is always
// lineFrame; padding()/layerFrame() remain for comparing frames with the
// Mac oracle.
struct TextBlock {
    std::vector<TextRun> runs;
    std::optional<TextRegion> language; // nullopt follows the UI region
    TextAlignment alignment{TextAlignment::left};
    int maximumLines{1};                // 0 = unlimited; the last line takes the rest, end-ellipsised
    bool truncates{true};
    double minimumScale{1};             // single line only: shrink before truncating (LIB-18)
    Rect lineFrame;
    std::optional<Size> framePadding;   // frozen padding (see above)

    TextBlock() = default;
    TextBlock(std::string text, TextStyle style, TextAlignment = TextAlignment::left);
    std::string text() const;           // concatenated authored text
    const TextStyle& style() const;     // first run's style (HUDTextLayer.textStyle)
    void setText(std::string);          // one run in style()
    void setStyle(const TextStyle&);    // every run takes the style
    void setColor(Color);
    void setLineFrame(Rect);            // freezes framePadding
    Size padding() const noexcept;      // framePadding or the current rule
    Rect layerFrame() const noexcept;   // lineFrame outset by padding()
    bool operator==(const TextBlock&) const = default;
};
// What text builders need from the canvas on a content event: the measurer,
// the casing rule, the UI region (for blocks without their own language) and
// the box render scale (contentsScale of text leaves).
struct TextServices {
    TextMeasurer* measurer{};
    TextCasing uppercase;
    TextRegion uiRegion{TextRegion::simplifiedChinese};
    double boxScale{2};
    TextRegion region(const TextBlock&) const noexcept;
    std::string displayed(const TextRun&) const; // uppercase applied when the style asks (asciiUppercase without `uppercase`)
    // The measurer receives the DISPLAYED runs (uppercased, empty runs
    // dropped, as HUDText.attributed); no text measures 0 without asking.
    // Without a measurer width()/metrics() throw std::logic_error.
    double width(std::span<const TextRun>, std::optional<TextRegion> = {}) const;
    double width(std::string_view, const TextStyle&, std::optional<TextRegion> = {}) const;
    LineMetrics metrics(const TextStyle&, std::optional<TextRegion> = {}, double scale = 1) const;
    // TextMeasurer::lineCount of the displayed runs (0 for no text).
    std::size_t lineCount(std::span<const TextRun>, double width, int maximumLines, std::optional<TextRegion> = {}) const;
};
// HUDTextLayer.preferredWidth: single-line natural width at full size.
double preferredWidth(const TextBlock&, const TextServices&);

// ---------------------------------------------------------------------------
// JSON text descriptor: the keys the A1 rasterizer defines
// (native/layer_raster.hpp, "Text descriptor keys beyond the captured
// CATextLayer ones"). textNode() emits a "text" layer whose BOUNDS ARE THE CSS
// LINE BOX (block.lineFrame): the raster pads it by ceil(.25 x size) x
// ceil(.45 x size) for overhangs itself, centres the lines vertically with
// baselines from HarmonyOS Sans' strut, applies maximumLines (0 unlimited; the
// last line takes the rest, end ellipsis), truncation ("end" unless
// truncates is false) and minimumScale (one-line shrink-to-fit, decided at
// raster time like HUDTextLayer.draw). Keys written:
//   string (displayed: uppercased runs), runs[{utf16Range, attributes{fontSize,
//   fontWeight (resolved CSS weight), tabularFigures, tracking (em),
//   NSLanguage, NSColor{sRGB}}}], fontSize, fontWeight, tabularFigures,
//   tracking, foregroundColor (first run), alignment, lineHeight (multiple of
//   fontSize: the line-box switch), maximumLines, minimumScale, truncation,
//   language (the block's region, else the UI region), contentsScale
//   (textContentsScale(boxScale)). No font family: source UI faces render in
//   the standard faces (HarmonyOS Sans + the region's Noto cascade, LIB-F19).
// The measurer (native/hud_design_text_metrics.hpp) measures this same
// descriptor (LayerRasterizer::measureSourceText: width, lines, shrink scale).
Json textNode(std::string id, const TextBlock&, const TextServices&);

// HUDModuleTitle (LIB-22): English UI -> the English title alone, no
// subtitle; zh/ja/ko -> the UI-language title in its region with the English
// title as the untagged subtitle. Built from the source pair once per
// language event (core::localized).
struct ModuleTitle {
    std::string english, simplifiedChinese, localized;
    static ModuleTitle make(std::string_view english, std::string_view simplifiedChinese, core::Language);
    struct Lines { std::string title; std::optional<TextRegion> titleLanguage; std::string subtitle; };
    Lines lines(core::Language resolved) const;
    bool operator==(const ModuleTitle&) const = default;
};
} // namespace endfield::modules::design
