#pragma once
// The list cards of the redesign: the port of Sources/HUDDesignCards.swift
// (HUDTaskCard, HUDListCard, HUDArchiveTile). LIB-45..47. Each card is a hover
// container that never takes the press itself (except the list card's copy
// press) and publishes its sub-controls through controls().
#include "modules/hud_design_controls.hpp"

namespace endfield::modules::design {

// HUDTaskCard (LIB-45): height 60, side zone 168; open/done plates cross-fade
// (.25 ease taskFlip: two static surfaces with opacity channels), micro tag
// (normal and inverted), title 14.5 medium (done F2F2F2), progress bar 150 x 3
// scaleX .0001 -> 1 (.35 E), count "0/1" / "1/1", COMPLETED watermark slide
// (30 -> 0, .45 E) and fade (.3 ease), check ring scale .6 -> 1 (.35 E); the
// action pill "In progress" / "Done" (label "<title>, <state>"); the delete x
// (22 x 22, 8 px left of the pill) fades in on card hover (.12) and stays
// accessible while transparent; the attention diamond. Recording mode: the
// title is a redaction bar and the label "Redacted"/已隐藏.
class TaskCard final : public Control {
public:
    static constexpr double height = 60, sideWidth = 168;
    // HUDTaskCard.titleStyle: 14.5 medium #111, line height 1.15 (Notes' in-place title editor).
    static const TextStyle& titleStyle();
    TaskCard(Builder&, std::string id, std::string title, bool done, bool attention, bool redacted, double width);
    bool done() const noexcept { return done_; }
    void toggle();                                 // setDone(!done, animated) then onToggle
    void setDone(bool, bool animated);
    void setAttention(bool);
    std::function<void(bool)> onToggle;
    std::function<void()> onDelete;
    std::vector<Control*> controls();              // [card, action, delete]
    bool pointerDown(Point) override { return false; }
    void hoverChanged() override;
    void accessibilityItems(const Layer& root, std::vector<AccessibilityItem>&) const override;
private:
    class Action; class Delete;                    // the pill ("<title>, <state>") and the x
    bool done_, attention_; std::string titleText_; Control *action_, *delete_;
    Layer *body_, *bodyDone_, *side_, *sideLight_, *sideDone_, *tag_, *tagDone_, *title_, *barTrack_, *bar_, *count_, *watermark_, *check_, *diamond_;
    void render(bool animated);
    void renderAction();                           // content: state, widths, colours
    void renderActionPlate();                      // the plate colour (hover: a tint)
};

// HUDListCard (LIB-46): height 58; plate E2E3DE -> CFD0CB chamfer 10;
// brighten .07; copy flash yellow .55 (1 -> 0 over .6 easeOut, not reduced);
// 3 px left bar (clear -> #111 on hover/focus, .1 ease); 44 x 44 type block
// (#1B1B1B, 18 px F2F2F2 glyph or a 44 x 35 aspect-fill thumbnail, a yellow
// strip with the type word); title 13.5 medium, meta 10.5 steel2; age column
// with the COPY reveal (opacity 0 and x 6 -> shown on hover, .14 E); row
// actions strip fading with hover/focus (.14), reachable while transparent;
// pinned corner triangle. Hover/focus: translateX 3 (.22 E), brighten,
// sheen (hover only). Recording: title bar (width rule) + 96 px meta bar.
class ListCard final : public Control {
public:
    enum class Kind : std::uint8_t { text, link, image, file }; // TEXT / LINK / IMAGE / FILE
    static constexpr double height = 58;
    ListCard(Builder&, std::string id, Kind, std::string title, std::string meta, std::string age, bool pinned,
        double width, std::optional<ImageSource> thumbnail = {}, std::string copyTitle = "COPY", bool redacted = false,
        std::vector<Control*> actions = {});
    bool focused() const noexcept { return focused_; }
    void setFocused(bool);
    void setAge(std::string);
    void flashCopied();
    std::vector<Control*> controls();              // [card] + actions
    bool contains(Point) const override;           // never over the action strip
    void hoverChanged() override;
    void accessibilityItems(const Layer& root, std::vector<AccessibilityItem>&) const override;
private:
    bool focused_{}; std::vector<Control*> actions_;
    Layer *plate_, *brighten_, *flash_, *leftBar_, *age_, *copy_, *strip_{};
    std::unique_ptr<Sheen> sheen_;
    void renderHighlight();
};

// HUDArchiveTile (LIB-47): height wellHeight (100) + 24; well gradient 8A8885
// -> BDBDBB -> D9D9D8 (classified 2A2A2A -> 151515); code bar and #nn; the doc
// glyph (40 x 50 folded page, shadow .25 r 6 y 6, ruled lines, extension label
// in the kind colour, rotated -6 deg) or an aspect-fill poster; size and date
// tags; label strip (selected: #151515 with a yellow label); selection
// brackets inset -5 -> -4 (path .16 E: two surfaces at the two insets
// cross-faded - recorded adaptation) and opacity .1 ease; hover: well -2 (.25),
// doc -6 -> -2 deg and +3 lift (.3), brighten .12. Classified: dark well and
// the red CLASSIFIED/机密 stamp. playRemoval: opacity 0 and scale .85 in .28,
// then completion (immediate under Reduce Motion). Recording: 84 px label bar.
class ArchiveTile final : public Control {
public:
    enum class Extension : std::uint8_t { pdf, img, zip, vid, txt }; // a file's own extension: extensionLabel
    enum class Style : std::uint8_t { standard, classified };
    ArchiveTile(Builder&, std::string id, int number, std::string title, Extension, std::optional<std::string> extensionLabel,
        std::optional<std::string> size, std::optional<std::string> date, double width, double wellHeight = 100,
        bool redacted = false, Style = Style::standard);
    void setPoster(std::optional<ImageSource>);
    void setSelected(bool, bool animated = true);
    bool selected() const noexcept { return selected_; }
    void playRemoval(std::function<void()> completion);
    void hoverChanged() override;
    // HUDArchiveTile.bracketPath: the selection brackets (also Settings' icon tiles at inset 0).
    static PathBuilder bracketPath(Rect bounds, double inset);
    // HUDArchiveTile.docGlyph (:404-425): the 40 x 50 folded page (centred on
    // its position), also the File Shelf properties card's preview.
    static Layer& docGlyph(Builder&, std::string_view id, Extension, const std::optional<std::string>& label);
private:
    bool selected_{}; Style style_; Builder* builder_;
    Layer *well_, *doc_, *poster_{}, *strip_, *label_, *brackets_, *bracketsRest_, *bracketsSelected_, *brighten_;
    void renderSelection(bool animated);
    static Layer& classifiedStamp(Builder&, std::string_view id, Point center);
};
} // namespace endfield::modules::design
