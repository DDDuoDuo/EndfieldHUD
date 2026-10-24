#pragma once
// The shared rich-text format bar of the redesign and its parts: the port of
// Sources/NotesFormattingControls.swift (Mac 1.3.0) NotesFormatState,
// NotesFormatBar (:86-258), NotesFormatToggle (:260-340) and
// NotesColorWheelControl (:342-433). DOCS-230..235, DOCS-238 (the colour
// pop-over applies once per release, with a 10-100 % brightness slider).
//
// One bar for the Notes centre TEXT note (NotesCanvas, id "notes.format",
// the demo's families) and the Archive editor (id "format", Archive's family
// list, its trailing Media button). It never edits text: the owner passes the
// caret or selection style with show() (in place, no rebuild) and applies what
// onFormat reports (core::notes::FormatChange: font carries the picked FAMILY
// name in fontName; the owner maps the default face to "no font name" and
// decides on/off of B / I / U / S over the range, RichDocument::applyFormat).
// Menus and the colour pop-over open through the owning canvas, so a focused
// editor keeps its selection when the owner leaves presses on controls() (and
// formatsSelection()) to the bar.
//
// The colour pop-over (notesColorPicker, Mac NotesFormatBar.colorPicker) is
// shared by this bar, the desktop notes' colour pop-up and Projection's pen
// colour: the 156 px wheel (hue = angle, saturation = radius) and a Brightness
// slider; onColor(0xRRGGBB) fires on the wheel's release and the slider's
// release only (no undo step per pointer move). Colours are the Mac's
// NSColor(calibratedHue:saturation:brightness:) converted to sRGB
// (core::source::genericRGBToSRGB, the verified Generic RGB conversion); the
// starting hue / saturation / brightness are read from the sRGB colour (the
// Mac reads them in deviceRGB, which is sRGB on the displays the HUD targets:
// recorded adaptation).
//
// The wheel raster is the pinned 192 x 192 PNG the v1.2.0 wheel used (1.3.0
// generates the same raster, NotesColorWheelControl.raster =
// NotesColorWheelView.wheel): the wheel draws the requiredSourceImage
// notesColorWheelImageKey, which the owner resolves in its DesignImages to
// notesColorWheelArtwork() under the resources/notes-format asset root.
#include "core/notes_rich_text.hpp"
#include "modules/hud_design_page.hpp"
#include <array>
#include <memory>

namespace endfield::modules {

// NotesFormatState (:75-84): what the bar shows. color is 0xRRGGBB.
struct NotesFormatState {
    std::string family{"HarmonyOS Sans"};
    double size{14};
    bool bold{}, italic{}, underline{}, strikethrough{};
    std::uint32_t color{0xEEEEEE};
    bool operator==(const NotesFormatState&) const = default;
};

namespace notesFormat {
inline constexpr std::string_view defaultFamily = "HarmonyOS Sans";   // NotesNoteStyle.defaultFamily
inline constexpr std::array<double, 16> sizes{9, 10, 11, 12, 13, 14, 16, 18, 20, 24, 28, 32, 36, 48, 64, 72};
inline constexpr double height = 26;
// The five swatches: light ink, the theme colour (HUDColor.yellow, so it
// follows the theme), blue, red, green.
std::array<std::uint32_t, 5> colors(const design::Palette&);
std::uint32_t hex(design::Color) noexcept;          // sRGB, each channel clamped and rounded
design::Color color(std::uint32_t) noexcept;        // HUDColor.rgb(hex)
core::notes::RGBA rgba(std::uint32_t) noexcept;
std::uint32_t hex(const core::notes::RGBA&) noexcept;
std::string sizeTitle(double);                       // String(Int(size.rounded()))
// NotesFormatBar.families (:95-101): the demo's 17 in order, kept when it is
// the default face, a bundled family or an installed one.
std::vector<std::string> families(std::span<const std::string> bundled, std::span<const std::string> installed);
// NSColor(calibratedHue:saturation:brightness:alpha: 1) in sRGB.
design::Color calibratedHSB(double hue, double saturation, double brightness);
}

// The wheel raster (see the header comment).
inline constexpr std::string_view notesColorWheelImageKey = "notes.colorWheel";
inline constexpr std::string_view notesColorWheelSHA256 = "244c34ad474b15c242f9bd62cbf195272cbf7b880acbeaf0021e237d6a22abd2";
ehud::data::Json notesColorWheelArtwork();          // {asset: "raster/<sha>.png", sha256}

// NotesFormatToggle (:260-340), the `.fmt` toggle: 28 x 26 by default;
// #262626 (tone) with a 1 px ringField ring, #888 while hovered and enabled,
// no ring when on; glyph bold 12 pt (10 below 26 px), #DDD (tone); on = the
// theme yellow with a #111 glyph. Fill .12 ease, ring .22 ease; press .94;
// disabled .35. Glyphs: B, I (slanted m21 -0.2: HarmonyOS has no italic), U
// (7 px rule at round(h x .73)), S (9 px rule at round(h / 2) - .5), a text
// label, a check (tick .62 x side, shown only when on) and a colour chip
// (inset 4, 1 px #111 border). The desktop notes use the same plate at their
// own size for every card button with a text glyph, the TODO check and the
// colour button. Accessibility: label and chip are buttons, the rest toggles.
class NotesFormatToggle final : public design::Control {
public:
    enum class Glyph : std::uint8_t { bold, italic, underline, strikethrough, label, check, chip };
    NotesFormatToggle(design::Builder&, std::string id, std::string label, Glyph, bool isOn,
        design::Size = {28, 26}, std::string text = {});
    bool isOn() const noexcept { return on_; }
    void setOn(bool);
    void setChip(design::Color);
    Glyph glyph() const noexcept { return glyph_; }
    void hoverChanged() override;
    void pressChanged() override;
    void enabledChanged() override;
    std::optional<design::AccessibilityRole> accessibilityRole() const override;
    void accessibilityItems(const design::Layer& root, std::vector<design::AccessibilityItem>&) const override;
private:
    Glyph glyph_; bool on_;
    design::Layer *fill_, *ring_, *ink_, *tick_{}, *chip_{};
    void render(bool animated);
};

// NotesColorWheelControl (:346-433): the shared raster with a 14 px marker
// (2 px #111 border, a 1.5 px white .9 ring outside, filled with the picked
// colour). Press and drag pick hue and saturation, clamped to the rim:
// onPick(false) while dragging, onPick(true) on release. Accessibility
// activation steps the hue by 30 degrees and raises the saturation to .5,
// then reports a final pick.
class NotesColorWheel final : public design::Control {
public:
    NotesColorWheel(design::Builder&, std::string id, std::string label, double side, double hue, double saturation, double brightness);
    double hue() const noexcept { return hue_; }
    double saturation() const noexcept { return saturation_; }
    double brightness() const noexcept { return brightness_; }
    design::Color color() const;
    void setBrightness(double);
    std::function<void(bool final)> onPick;
    // NotesColorWheelControl.unit / pick: wheel points in 0...1.
    static design::Point unit(double hue, double saturation);
    struct Pick { double hue{}, saturation{}; design::Point unit; };
    static Pick pick(design::Point unit);
    bool pointerDown(design::Point) override;
    void pointerDragged(design::Point) override;
    void pointerUp(design::Point) override;
    void activate() override;
private:
    double side_, hue_, saturation_, brightness_;
    design::Layer *marker_, *markerFill_;
    void render();
    void pickAt(design::Point, bool final);
};

// NotesFormatBar.colorPicker (:229-257): a pop-over 156 + 24 wide with the
// wheel (`<id>.wheel`, "Color wheel"/颜色轮盘) and the Brightness slider
// (`<id>.brightness`, "Brightness"/亮度, 10...100 step 1, percent, 156 wide,
// dark, no ticks, its bubble on `bubbleHost`). The slider re-tints the marker
// live. Callbacks capture only plain values and the onColor function: the
// pop-over may be closed while they wait.
struct NotesColorPicker {
    std::unique_ptr<design::Popover> popover;
    std::vector<design::Control*> controls;     // [wheel, brightness]
};
NotesColorPicker notesColorPicker(design::Environment&, std::string id, std::string title, std::uint32_t color,
    design::Layer* bubbleHost, std::function<void(std::uint32_t)> onColor);

// NotesFormatBar (:92-258): font dropdown (small, fontWidth), size dropdown
// (small, sizeWidth, integer text), B / I / U / S toggles (28 x 26), the five
// swatches (16) and the custom colour swatch (16, the conic wheel), then the
// owner's trailing controls; 6 px apart, each centred in the 26 px height;
// width = sum - 6. Font menu: the families, the current one selected,
// minimum width max(fontWidth, 200); size menu: the 16 sizes. A press on a
// dropdown while any menu is open closes that menu; the custom swatch toggles
// the colour pop-over ("TEXT COLOR"/文字颜色) and reports a colour only when it
// differs from the shown one. The custom swatch rings whenever the colour is
// none of the presets (exact 0xRRGGBB compare). Constructed through
// Page::own (or any Builder) during buildPage; it lives with that page.
class NotesFormatBar final {
public:
    NotesFormatBar(design::Builder&, design::Canvas&, std::string id, NotesFormatState, std::vector<std::string> families,
        double fontWidth = 150, double sizeWidth = 72, std::vector<design::Control*> trailing = {});
    ~NotesFormatBar();
    NotesFormatBar(const NotesFormatBar&) = delete;
    NotesFormatBar& operator=(const NotesFormatBar&) = delete;
    design::Layer& layer() const noexcept { return *layer_; }
    const std::vector<design::Control*>& controls() const noexcept { return controls_; }
    const NotesFormatState& state() const noexcept;
    // Set by the owner; the menus and the pop-over call the value current
    // when they report (they never call into a bar that died meanwhile).
    void setOnFormat(std::function<void(const core::notes::FormatChange&)>);
    // In place (a caret move or a selection change): no rebuild.
    void show(const NotesFormatState&);
    // A format control (not a trailing one) is under this root point: a
    // press there edits the selection, so the editor keeps its focus.
    bool formatsSelection(design::Point rootPoint, const design::Layer& root) const;
private:
    struct Shared;
    std::shared_ptr<Shared> shared_;
    design::Canvas* canvas_;
    std::string id_;
    std::vector<std::string> families_;
    design::Layer* layer_;
    design::Dropdown *font_, *size_;
    design::Swatch* custom_;
    std::vector<std::pair<NotesFormatToggle*, bool NotesFormatState::*>> toggles_;
    std::vector<std::pair<design::Swatch*, std::uint32_t>> swatches_;
    std::vector<design::Control*> controls_;
    std::size_t trailing_{};
    void renderColor();
    void toggleMenu(design::Dropdown&, std::vector<design::Menu::Item>, std::optional<double> minimumWidth,
        std::function<std::optional<core::notes::FormatChange>(const std::string&)>);
    void openColorWheel();
};
} // namespace endfield::modules
