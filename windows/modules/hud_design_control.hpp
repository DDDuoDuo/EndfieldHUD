#pragma once
// The control base of the redesign: the port of Sources/HUDDesignControl.swift
// (HUDAccessibilityItem, HUDReadout, HUDControl, HUDInteractionRouter), the
// redesign target pass of Sources/HUDControlHighlightLayer.swift
// (update(in:point:pressed:), :143-176) and the HUDDesignEnvironment of
// Sources/HUDDesignMotion.swift:6-26. LIB-11, LIB-14..16, LIB-81, LIB-93.
//
// Ownership: controls are created by buildPage (or a menu, pop-over, header,
// overlay) and owned by that object's ControlSet; their layers belong to the
// same object's LayerArena. Callbacks stored on a control (onActivate,
// onChange, ...) may capture the page's own controls and layers: they die
// together. Callbacks that outlive the build (menu onPick, confirm/toast
// completions, deadlines) capture the canvas and plain values only.
//
// Two winners, as on the Mac (LIB-15, LIB-93): the HOVER highlight goes to
// the enabled, visible, unclipped non-container target with the SMALLEST
// area containing the point (ties: the later one in paint order); container
// targets (row plates, cards, scroll strips) stay highlighted while the point
// is inside them and are pressed only when no non-container target is hit.
// The PRESS goes to the TOPMOST (last registered) enabled visible unclipped
// control whose contains() accepts the point; it tracks the drag until release.
#include "modules/hud_design_icons.hpp"
#include "modules/hud_design_layer.hpp"

namespace endfield::modules::design {

// Swift Character semantics on UTF-8 (String.count, String(prefix(n)),
// dropFirst(n): extended grapheme clusters), used for maximumLength, secure
// bullets and the redaction widths and wrap. These limits are data rules
// (WINDOWS-MIGRATION.md section 7, codec trap 7), so PRODUCTION INJECTS THE
// ICU RULES: the native host fills Environment::characters from ICU's
// character break iterator (the native/profile_text.cpp rules of
// ProfileTextRules::characters / prefix). portableCharacterRules() is only the
// default for portable tests without ICU: an approximation of UAX #29 for the
// scripts the HUD shows (CR LF, combining marks, variation selectors, ZWJ emoji
// sequences, skin tones, tags, regional-indicator flags, Hangul jamo).
struct CharacterRules {
    std::function<std::size_t(std::string_view)> count;                  // String.count
    std::function<std::string(std::string_view, std::size_t)> prefix;    // String(prefix(n))
    std::string_view dropFirst(std::string_view utf8, std::size_t n) const { return utf8.substr(prefix(utf8, n).size()); } // dropFirst(n)
};
CharacterRules portableCharacterRules();

// The shared rendering/motion environment of every component (one per
// design::Context; the canvas sets `now` before dispatching each event).
struct Environment {
    Palette palette;
    TextServices text;
    Deadlines deadlines;
    core::Language language{core::Language::english}; // resolved (never system)
    double now{};
    bool reduceMotion{};
    bool recordingMode{};
    CharacterRules characters = portableCharacterRules();   // production: the injected ICU rules (see above)
    motion::Clock clock() noexcept { return {now, reduceMotion, &deadlines}; }
};

// Keyboard input in Windows virtual-key terms (the Mac key codes 53 Esc, 36
// Return, 76 keypad Enter, 123-126 arrows, 51 Delete, 117 forward Delete map
// to VK_ESCAPE, VK_RETURN, VK_RETURN + extended, VK_LEFT/RIGHT/DOWN/UP,
// VK_BACK, VK_DELETE). `character` is the key's unmodified character
// (charactersIgnoringModifiers, lowercased by the receiver when it compares).
namespace key {
inline constexpr std::uint32_t back = 0x08, tab = 0x09, enter = 0x0D, shift = 0x10, control = 0x11, menu = 0x12;
inline constexpr std::uint32_t escape = 0x1B, space = 0x20, pageUp = 0x21, pageDown = 0x22, end = 0x23, home = 0x24;
inline constexpr std::uint32_t left = 0x25, up = 0x26, right = 0x27, down = 0x28, deleteForward = 0x2E;
}
struct KeyInput {
    std::uint32_t virtualKey{};
    char32_t character{};
    bool shift{}, control{}, alt{}, win{};
    bool extended{};   // keypad Enter
    bool repeat{};
    // The Mac's command / control / option set: such keys pass through the
    // canvas's own handling (HUDDesignCanvas.keyDown, :591-592).
    bool commandModifiers() const noexcept { return control || alt || win; }
    bool isReturn() const noexcept { return virtualKey == key::enter; }
};
enum class PointerButton : std::uint8_t { left, right };
struct PointerInput {
    Point point;                 // content-box px
    PointerButton button{PointerButton::left};
    bool shift{}, control{};
    unsigned clickCount{1};
};
// Cursors a canvas may ask for (HUDDesignCanvas.cursor(at:)): the Settings
// module grip and the Map use the hands; text fields the I-beam.
enum class Cursor : std::uint8_t { standard, text, openHand, closedHand };

// HUDAccessibilityItem.Role (LIB-16) plus `edit` (a design text field) and
// `group` (a valued container: rules, readouts with children).
enum class AccessibilityRole : std::uint8_t { button, toggle, popUp, menuItem, tab, slider, text, edit, group };
struct AccessibilityItem {
    std::string id, label, help;
    AccessibilityRole role{AccessibilityRole::button};
    bool on{};                     // toggle on, menu item / tab selected
    std::string value;             // popUp value, text value, edit text, slider description
    double number{}, minimum{}, maximum{1}, step{1}; // slider
    Rect rect;                     // content-box px
    bool enabled{true}, focused{};
    // Scrolled out of its clipping list (a long menu's rows): still listed, like
    // the rows of a scrolled NSMenu, but offscreen (UIA IsOffscreen, no hit).
    bool offscreen{};
    bool operator==(const AccessibilityItem&) const = default;
};

class Control {
public:
    Control(Environment&, std::string id, std::string label, Layer& layer);
    virtual ~Control();
    Control(const Control&) = delete;
    Control& operator=(const Control&) = delete;
    const std::string& id() const noexcept { return id_; }
    Layer& layer() const noexcept { return *layer_; }
    Environment& env() const noexcept { return *env_; }
    std::string label;
    std::function<void()> onActivate;
    bool hovered() const noexcept { return hovered_; }
    bool pressed() const noexcept { return pressed_; }
    bool enabled() const noexcept { return enabled_; }
    void setEnabled(bool);                 // disabling clears hover/press
    // Installs or moves the invisible target (layer coordinates).
    void setTarget(Rect, std::optional<PathBuilder> path = {}, bool container = false);
    const HitTarget* target() const noexcept { return target_; }
    // Hover/press off (the control left the screen). Virtual like the Mac's:
    // a grip ends its drag at the start point, a long menu's track resets its bar.
    virtual void clearInteractionState();

    virtual void hoverChanged() {}
    virtual void pressChanged() {}
    virtual void enabledChanged() {}
    // Router hooks; points are in this control's layer coordinates.
    virtual bool contains(Point) const;    // default: the target rect (else bounds)
    virtual bool pointerDown(Point);       // default: enabled (tracks until release)
    virtual void pointerDragged(Point) {}
    virtual void pointerUp(Point);         // default: activate() when still inside
    virtual void activate();               // default: onActivate when enabled
    // nullopt keeps the control out of the tree; an empty label does too.
    virtual std::optional<AccessibilityRole> accessibilityRole() const { return AccessibilityRole::button; }
    virtual void accessibilityItems(const Layer& root, std::vector<AccessibilityItem>&) const;
    Rect rect(const Layer& root) const;    // the target's (or layer's) rect in root coordinates
protected:
    void receive(bool hovered, bool pressed);
    // One item of `role` for this control (id, label, rect, enabled); subclasses add their values.
    AccessibilityItem accessibilityItem(const Layer& root, AccessibilityRole) const;
    motion::Clock clock() const noexcept { return env_->clock(); }
    const Palette& palette() const noexcept { return env_->palette; }
    const TextServices& text() const noexcept { return env_->text; }
private:
    Environment* env_;
    std::string id_;
    Layer* layer_;
    HitTarget* target_{};
    bool hovered_{}, pressed_{}, enabled_{true};
};

// HUDReadout: static text with a value (live readouts, plates, settings
// values); never takes the pointer.
class Readout final : public Control {
public:
    Readout(Environment&, std::string id, std::string label, std::string value, Layer& frameLayer);
    std::string value;
    bool contains(Point) const override { return false; }
    bool pointerDown(Point) override { return false; }
    std::optional<AccessibilityRole> accessibilityRole() const override { return AccessibilityRole::text; }
    void accessibilityItems(const Layer& root, std::vector<AccessibilityItem>&) const override;
};

// Owner of a page's (menu's, header's, ...) controls.
class ControlSet final {
public:
    template <class T, class... A> T& make(A&&... args) {
        auto owned = std::make_unique<T>(std::forward<A>(args)...);
        auto& result = *owned;
        controls_.push_back(std::move(owned));
        return result;
    }
    std::size_t size() const noexcept { return controls_.size(); }
    Control* find(std::string_view id) const noexcept;
    void clearInteractionState() noexcept;
private:
    std::vector<std::unique_ptr<Control>> controls_;
};

// What a component constructor needs: the environment, the arena its layers
// live in and the set that owns sub-controls (segments, tabs, card actions).
// Page, Menu, Popover, Header and the overlay each expose one.
struct Builder {
    Environment& env;
    LayerArena& layers;
    ControlSet& controls;
    Layer& layer(std::string_view id) { return layers.make(id); }
    template <class T, class... A> T& make(A&&... args) { return controls.make<T>(*this, std::forward<A>(args)...); }
};

// ---------------------------------------------------------------------------
// CALayer shorthands shared by every component file (controls, badges, scroll,
// cards, menu, header, redaction, text field): a layer's own drawing is ONE
// node whose frame is the layer's bounds (Layer::setContent), exactly like a
// CALayer's backgroundColor / border / CAShapeLayer path. Content events only.
// ---------------------------------------------------------------------------
Rect localBounds(const Layer&) noexcept;              // (0, 0, bounds)
// backgroundColor (+ circular cornerRadius).
void setBackground(Layer&, Color, double cornerRadius = 0);
// backgroundColor (optional) + borderColor/borderWidth; the border is drawn above the fill.
void setBorder(Layer&, std::optional<Color> fill, Color border, double width = 1, double cornerRadius = 0);
// CAShapeLayer: `path` filled in layer coordinates.
void setPath(Layer&, const PathBuilder&, Color fill);
// A colour the Mac changes on hover, press or selection (backgroundColor /
// borderColor / fillColor / text colour animations): the content is authored
// in WHITE and the colour is the layer's model tint, so the change is a pose
// number, never a raster. Marks the layer animatable.
void setTintedBackground(Layer&, Color, double cornerRadius = 0);
void setTintedBorder(Layer&, Color, double width = 1, double cornerRadius = 0);
void setTintedPath(Layer&, const PathBuilder&, Color);
// HUDTextLayer: a text layer with one run of `style`; `setLine` moves its CSS
// line box, `editText` changes the block in place (frame follows the line box).
Layer& makeText(Builder&, std::string_view id, std::string text, const TextStyle&, Rect lineFrame,
    TextAlignment = TextAlignment::left);
void setLine(Layer& textLayer, Rect lineFrame);
// Reads a text layer's block without touching its revision (Layer::text() non-const marks content changed).
const TextBlock& textOf(const Layer& textLayer);
template <class F> void editText(Layer& textLayer, F&& edit) {
    TextBlock block = textLayer.text() ? *textLayer.text() : TextBlock{};
    edit(block);
    textLayer.setText(std::move(block));
}
// HUDIconLayer: a `size` square icon layer (position it like the Mac does);
// setIcon swaps the glyph in place (HUDIconButton.setIcon, Volume's mute).
Layer& makeIcon(Builder&, std::string_view id, Icon, double size, Color);
void setIcon(Layer&, Icon, double size, Color);
// CGPath(roundedRect:cornerWidth:cornerHeight:) as cubics (pill and task-action hit paths).
PathBuilder roundedRect(Rect, double radius);


// HUDInteractionRouter (:112-145).
class Router final {
public:
    void set(std::vector<Control*>);       // drops a tracked control that left
    const std::vector<Control*>& controls() const noexcept { return controls_; }
    Control* control(Point rootPoint, const Layer& root) const;
    bool pointerDown(Point rootPoint, const Layer& root);   // true when a control was hit
    void pointerDragged(Point rootPoint, const Layer& root);
    void pointerUp(Point rootPoint, const Layer& root);
    void cancel() noexcept { tracking_ = nullptr; }
    bool tracking() const noexcept { return tracking_ != nullptr; }
    Control* tracked() const noexcept { return tracking_; }
    void accessibilityItems(const Layer& root, std::vector<AccessibilityItem>&) const;
private:
    std::vector<Control*> controls_;
    Control* tracking_{};
};

// The redesign target pass (HUDControlHighlightLayer.update): `point` in root
// coordinates (nullopt: the pointer left), `pressed` the button state.
// Converts points only at targets and clip boundaries; skips decorative
// subtrees. Content events and pointer moves only.
void updateHighlights(const Layer& root, std::optional<Point> point, bool pressed);

// HUDControl.applyHoverMotion (HUDDesignButtons.swift:6-18): hover lifts
// (translateY -lift, .22 E) and scales (hoverScale); press scales pressScale
// in .10 (back out .22); the brighten overlay fades .22 ease. Disabled: rest.
void applyHoverMotion(Control&, double lift, double hoverScale, double pressScale, Layer* brighten);
} // namespace endfield::modules::design
