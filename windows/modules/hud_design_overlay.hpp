#pragma once
// The shared centre overlay, portable half: the port of
// Sources/HUDDesignOverlay.swift (HUDConfirmRequest, HUDConfirmState,
// HUDConfirmBar, HUDToastRequest, HUDToastState, HUDToast, HUDCenterOverlay).
// LIB-59..67. One per HUD (in design::Context), shared by every redesigned
// module; it replaces the separate quit card and the "Keep this UI scale?"
// card of v1.2.0, both confirm requests now (Phase B).
//
// Coordinates are content-box px; the overlay sits on the tilted plane like
// the content box (boxWorld) but is composed ABOVE every module (the native
// overlay owner's modalEntries). Timers are Context deadlines (the 120 ms
// confirm callback, the 1.5 s toast, the hide completions): no thread or
// timer. The pixel-step blocks of the spec's confirm bar are deliberately
// left out (owner request, round 4).
#include "modules/hud_design_controls.hpp"

namespace endfield::modules::design {

// HUDConfirmRequest (LIB-59, LIB-62).
struct ConfirmRequest {
    enum class Style : std::uint8_t { standard, alert }; // alert: red band, white text (Shipwide Defense)
    std::string id, title, message;
    std::optional<int> countdown;      // replaces the message with the bold accent number ("Keep this UI scale?")
    std::string confirmTitle;
    std::string cancelTitle;           // empty: core::localized("Cancel", "取消")
    bool danger{};
    std::optional<Icon> icon;          // default trash when danger, else check
    Style style{Style::standard};
    // The upper pill takes the confirm kind and glyph and CONFIRMS too (two
    // identical answers, e.g. both "Disable Shipwide Defense System" /
    // 关闭全舰防御系统); Esc or a press outside the bar still cancels.
    bool cancelPillConfirms{};
    Icon glyph() const noexcept { return icon.value_or(danger ? Icon::trash : Icon::check); }
    bool operator==(const ConfirmRequest&) const = default;
};
// HUDConfirmState: Esc cancels, Return / keypad Enter confirm, every other key
// is consumed; only the first decision counts.
class ConfirmState final {
public:
    enum class Decision : std::uint8_t { confirm, cancel };
    const std::optional<ConfirmRequest>& request() const noexcept { return request_; }
    std::optional<Decision> decision() const noexcept { return decision_; }
    bool isOpen() const noexcept { return request_ && !decision_; }
    void present(ConfirmRequest);
    std::optional<Decision> key(const KeyInput&);
    std::optional<Decision> decide(Decision);
    void setCountdown(int);            // only when the request has a countdown
    void close() noexcept;
private:
    std::optional<ConfirmRequest> request_;
    std::optional<Decision> decision_;
};

// HUDConfirmBand (Sources/HUDDesignOverlay.swift): the confirm bar's surface,
// shared by ConfirmBar and PlacementEditor so both read as one family. Band:
// transparent -> charcoal tone(181818, .97) -> charcoal -> transparent at
// 0/.06/.94/1 (alert: red .97); hairlines at y 0 and h - 1 over 5...95 %.
// The 12 x 18 double-bar marker (accentInk at creation; red for danger,
// white for alert) is made by marker() and positioned by its host. The band
// layer is animatable (the wipe from the centre). Layer ids in the builder's
// arena: "band", "hairline.0", "hairline.1" and "marker".
class ConfirmBand final {
public:
    ConfirmBand(Builder&, Size);
    Layer& layer() const noexcept { return *layer_; }
    void applyPalette();               // the standard band in the current Theme
    void setStyle(bool alert);         // the red band (Settings' facility-alert easter egg) or the standard one
    static Layer& marker(Builder&);
    static void setMarker(Layer&, Color tone);
private:
    Environment* env_; Layer* layer_;
    std::array<Layer*, 2> hairlines_{};
};

// HUDConfirmBar (LIB-60, LIB-61, LIB-63): 620 x 112 at box (-15, 370), z 20.
// The band, hairlines and marker are a ConfirmBand (see above); text column
// at (46, 18), width 620 - 46 - 34 - 16 - max(pill
// widths): title at x 22 - ONE line with a message or countdown, else up to
// 3 lines at 17 px, and a title wider than (column - 22) x 2.7 steps down to
// 14 px over 4 lines (line box size x 1.25 x lines); description 12 px up to
// 2 lines in 36 px at y 29.25, or the countdown (22 bold) in 22 px. Pills
// right-aligned 34 from the right, Cancel ABOVE Confirm from y 18, 11 apart.
// Show: band wipe from the centre .30 SNAP; text from x -12 (.30 E after
// .12); pills from x +12 (.30 after .16 + .06 i). Hide: scaleY -> .04 and
// opacity -> 0 in .18 easeIn, then hidden; pills' hover/press cleared.
// Showing again during the collapse cancels its "hidden" step and runs its
// completion at once (hud_design_overlay.cpp says why).
class ConfirmBar final {
public:
    static constexpr Size size{620, 112};
    static constexpr Point origin{-15, 370};
    ConfirmBar(Environment&, Deadlines::Token);
    ~ConfirmBar();
    Layer& layer() const noexcept { return *layer_; }
    void applyPalette();
    void configure(const ConfirmRequest&);
    void setCountdown(int);
    void show(const motion::Clock&);
    void hide(bool animated, const motion::Clock&, std::function<void()> completion = {});
    std::vector<Control*> pills() const;
    std::function<void(ConfirmState::Decision)> onDecision;
    std::string titleText() const;     // test hooks
    std::string messageText() const;
    double titleSize() const;
    int titleLines() const;
private:
    // The pills of one request live in their own arena and control set (a
    // request may repeat its id); the previous set is kept one request longer
    // because its pill may still be on the stack (a cancel callback that
    // opens the next request).
    struct PillSet;
    Environment* env_; LayerArena arena_; ControlSet controls_; Builder builder_;
    std::unique_ptr<ConfirmBand> band_;
    Layer *layer_, *column_, *marker_, *title_, *message_;
    std::vector<Control*> pills_;
    std::unique_ptr<PillSet> pillSet_, retiredPills_;
    std::uint64_t pillSerial_{};
    Deadlines::Token hideToken_{};
    std::function<void()> pendingCompletion_;   // the collapse's completion until it ran
};

// HUDPlacementDrag (Sources/HUDPlacementEditor.swift): the drag maths shared
// by placement editors. The element keeps its offset from the pointer from
// where the press began; the host clamps the result.
class PlacementDrag final {
public:
    bool active() const noexcept { return startAnchor_.has_value(); }
    void begin(Point anchor, Point pointer) noexcept { startAnchor_ = anchor; startPointer_ = pointer; }
    std::optional<Point> anchor(Point pointer) const noexcept;
    void end() noexcept { startAnchor_.reset(); startPointer_.reset(); }
    bool operator==(const PlacementDrag&) const = default;
private:
    std::optional<Point> startAnchor_, startPointer_;
};

// HUDPlacementEditor (Sources/HUDPlacementEditor.swift): the redesign's
// place-and-confirm strip. The element being placed follows a drag, the strip
// beside it holds its sliders and the Cancel and Confirm pills, Esc cancels
// and Return confirms. It is the confirm bar's band and marker (ConfirmBand)
// with the confirm bar's pills (Pill) and dark Slider rows, so it reads as the
// same family as every other confirmation.
//
// Geometry: a title column at x 46, max(48, widest title) wide, in 13 px
// medium titleText; sliders 236 wide at x 46 + titleWidth + 14, rows 49 apart
// centred from y 37; Cancel above Confirm right-aligned at sliderX + 236 + 26
// (46 without rows), from y 18, 49 apart; the marker at (22, 28). The strip
// is pillX + pillWidth + 34 by max(112, 49 x rows + 14). Entrance as the
// confirm bar's: the band wipes open from its centre, the marker and the rows
// from the left, the pills from the right.
//
// The host owns what is being placed and its clamping and composes `layer()`
// (the battery bar editor hosts it in the alert's own panel at 0.7; placing
// the HUD's own overlays hosts it on the HUD canvas). A press on the strip goes
// to its controls, any other press starts dragging the placed element
// (PlacementDrag). Slider bubbles are hosted in the strip's own layer, so a
// host keeps bubbleClearance above it. Its layers live in its own arena
// ("placement.*"); contentRevision() moves with them. No timer.
class PlacementEditor final {
public:
    // One slider row; its id is reported back through onChange.
    struct SliderSpec {
        std::string id, title;
        double value{};
        Slider::Range range;
        ValueFormat format{ValueFormat::percent};
    };
    static constexpr double bubbleClearance = 8, rowPitch = 49, sliderWidth = 236;
    static TextStyle titleStyle(const Palette&);   // 13 medium titleText
    PlacementEditor(Environment&, std::string id, std::vector<SliderSpec>);
    ~PlacementEditor();
    PlacementEditor(const PlacementEditor&) = delete;
    PlacementEditor& operator=(const PlacementEditor&) = delete;
    Layer& layer() const noexcept { return *layer_; }
    Size size() const noexcept { return layer_->bounds(); }
    const std::vector<Slider*>& sliders() const noexcept { return sliders_; }
    Pill& cancel() const noexcept { return *cancel_; }
    Pill& confirm() const noexcept { return *confirm_; }
    std::vector<Control*> controls() const;          // sliders, then Cancel and Confirm
    // A strip control holds the pointer (a host keeps the strip still meanwhile).
    bool adjusting() const noexcept { return router_.tracking(); }
    bool dragging() const noexcept { return drag_.active(); }
    bool isOpen() const noexcept { return state_.isOpen(); }
    // (slider id, value, final): final on release and on keyboard or accessibility steps.
    std::function<void(const std::string& id, double value, bool final)> onChange;
    std::function<void(ConfirmState::Decision)> onDecision;
    void show(const motion::Clock&);
    // Values: setValue / setRange change a slider without a callback (another
    // slider, a display change).
    std::optional<double> value(std::string_view id) const;
    void setValue(double, std::string_view id);
    void setRange(Slider::Range, double value, std::string_view id);
    // Esc cancels and Return / keypad Enter confirm, once; other keys are left to the host.
    bool key(const KeyInput&);
    // `point` is in `root`, the layer the strip sits in; `pointer` and `anchor`
    // are in the host's placement space (desktop pixels for the alert panel).
    void pointerDown(Point point, const Layer& root, Point pointer, Point anchor);
    // The placed element's proposed anchor while dragging; nullopt otherwise.
    std::optional<Point> pointerDragged(Point point, const Layer& root, Point pointer);
    void pointerUp(Point point, const Layer& root);
    // Ends any gesture without committing it (the editor closes, focus leaves).
    void cancelPointer();
    bool stripContains(Point, const Layer& root) const;
    // Hover and press feedback for the strip's controls only.
    void updateHighlights(const Layer& root, std::optional<Point>, bool pressed = false);
    void accessibilityItems(const Layer& root, std::vector<AccessibilityItem>&) const;
    void perform(std::string_view id);               // an accessibility press on a pill
    void adjust(std::string_view id, double value);  // an accessibility slider value (reported as final)
    // Where the strip goes beside `target` (the visible element), y down:
    // centred `gap` below it, or above it when only that fits, kept inside
    // `bounds` (centred on it when the strip is wider).
    static Rect frame(Size, Rect target, Rect bounds, double gap) noexcept;
    std::uint64_t contentRevision() const noexcept { return arena_.revision(); }
private:
    Environment* env_; LayerArena arena_; ControlSet controls_; Builder builder_;
    std::unique_ptr<ConfirmBand> band_;
    Layer *layer_, *marker_;
    std::vector<Layer*> titles_;
    std::vector<Slider*> sliders_;
    Pill *cancel_, *confirm_;
    Router router_;
    ConfirmState state_;
    PlacementDrag drag_;
    Slider* slider(std::string_view id) const noexcept;
    void decide(ConfirmState::Decision);
    void finish(ConfirmState::Decision);
};

// HUDToastRequest / HUDToastState (LIB-65, LIB-66).
struct ToastRequest {
    Icon icon{Icon::check};
    std::string title, caption;
    std::optional<TextRegion> captionLanguage;
    bool operator==(const ToastRequest&) const = default;
};
class ToastState final {
public:
    const std::optional<ToastRequest>& current() const noexcept { return current_; }
    std::uint64_t generation() const noexcept { return generation_; }
    std::uint64_t show(ToastRequest);
    bool expire(std::uint64_t token);  // only the toast it was started for
    void clear() noexcept;
private:
    std::optional<ToastRequest> current_;
    std::uint64_t generation_{};
};
// The 获得奖励 result popup (LIB-65): 600 x 150 at box (-5, 190), z 25. Line
// 1 px at y 44 (foreground 0 -> .65 -> .65 -> 0 at 0/.3/.7/1) with 1 x 7 ticks
// at 20 % and 80 %; icon box 60 x 60 at (270, 14) tone(161616) with 1 px
// tone(4A4A4A), a 12 px accentInk corner triangle, 26 px glyph foreground(1);
// title centred at y 88, caption at y 110 (optional language), underline 22 x
// 2 accentInk at (289, 130). Show: line grows .35 E; box pops after .08
// (.32 SNAP); title / caption rise 8 px (.3, delays .14 / .20); underline grows
// .3 after .25. Hide: opacity -> 0 and rise 6 px in .24 easeIn, then hidden.
class Toast final {
public:
    static constexpr Size size{600, 150};
    static constexpr Point origin{-5, 190};
    Toast(Environment&, Deadlines::Token);
    ~Toast();
    Layer& layer() const noexcept { return *layer_; }
    void applyPalette();
    void show(const ToastRequest&, const motion::Clock&);
    void hide(bool animated, const motion::Clock&, std::function<void()> completion = {});
    std::pair<std::string, std::string> texts() const;
private:
    Environment* env_; LayerArena arena_; ControlSet controls_; Builder builder_;
    Layer *layer_, *line_, *box_, *glyph_, *title_, *caption_, *underline_, *corner_;
    std::array<Layer*, 2> ticks_{};
    Icon icon_{Icon::check};
    Deadlines::Token hideToken_{};
    std::function<void()> pendingCompletion_;   // the fade's completion until it ran
};

// HUDCenterOverlay (LIB-64, LIB-66, LIB-67). Dim: an 850 x 850 radial
// gradient centred on box (295, 305), z 15, colours tone(000000, .62)
// (Light: white .62, a pale veil), .62, 0 at 0 / .821 / .99, opacity
// 0 -> 1 in .22 E. Content dimming as the demo (#center.dimmed{filter:
// brightness(.5) blur(1.5px)}): the MODULE CONTENT itself gets
// CIGaussianBlur 1.5 and CIExposureAdjust -2.2 EV, eased over .25 E, present
// only while needed. The easing is core::ContentDimTrack (core/motion.hpp, the
// one definition); dimFilter() reports its amount and the HOST applies it with
// Renderer::setContentFilter over the module entries (the radius is in
// centre-plane design units; core::source::planeScale converts it to output
// pixels). The dim and filter are on while a confirm bar OR a toast shows: a
// decision undims only when no toast shows; a toast expiring undims only
// when no confirm is open.
class CenterOverlay final {
public:
    explicit CenterOverlay(Environment&);
    ~CenterOverlay();
    Layer& layer() const noexcept { return *layer_; }      // 590 x 510 box, not clipped
    ConfirmBar& confirmBar() noexcept { return *bar_; }
    Toast& toast() noexcept { return *toast_; }
    const ConfirmState& confirmState() const noexcept { return confirm_; }
    const ToastState& toastState() const noexcept { return toastState_; }
    void applyPalette();
    bool isConfirming() const noexcept { return confirm_.isOpen(); }
    // Input belongs to the overlay until the bar finished collapsing (keys are
    // swallowed during the .18 s hide).
    bool capturesInput() const noexcept { return confirm_.request().has_value(); }
    // Opening clears any toast first (its completion runs). The host closes
    // every canvas's menus and pop-overs and ends editing and drags first
    // (Context::confirm).
    void confirm(ConfirmRequest, std::function<void()> onConfirm, std::function<void()> onCancel = {});
    void setCountdown(int);
    bool handleKey(const KeyInput&);   // true while capturing: Esc / Return decide, others swallowed
    // Cancel calls back at once; Confirm after 120 ms (at once under Reduce
    // Motion); the router empties at once; only the first decision counts.
    bool decide(ConfirmState::Decision);
    void dismissConfirm();             // no decision, no callback (module change, HUD close)
    // A toast while a confirm bar is open is DROPPED and its completion never
    // runs; a new toast replaces the current one (the previous completion runs
    // at once); the 1.5 s timer ends only the toast it started; the
    // completion runs after the hide.
    void showToast(ToastRequest, std::function<void()> completion = {});
    void clearToast();
    void reset();                      // HUD close: everything off at once
    bool dimmed() const noexcept { return dimmed_; }
    // core::ContentDimTrack::amount: 0...1 scaling blurRadius and exposure.
    double dimFilter(double now) const noexcept { return filter_.amount(now); }
    bool filterNeeded(double now) const noexcept { return filter_.needed(now); }
    // Pointer routing (box px): the pills, plus an outside-press control
    // covering the overlay minus the bar's frame for twin-pill requests.
    Router& router() noexcept { return router_; }
    bool pointerDown(Point);
    void pointerDragged(Point);
    void pointerUp(Point);
    void pointerMoved(std::optional<Point>, bool pressed);
    void accessibilityItems(std::vector<AccessibilityItem>&) const;
    // Content generation for the overlay's scene (the overlay, bar, pill and
    // toast arenas); motion is pose-only and never moves it.
    std::uint64_t contentRevision() const noexcept;
    std::function<void()> onChange;    // input state changed (host refreshes capture, UIA)
private:
    Environment* env_; Deadlines::Token token_; LayerArena arena_; ControlSet controls_; Builder builder_;
    Layer *layer_, *dim_;
    std::unique_ptr<ConfirmBar> bar_; std::unique_ptr<Toast> toast_;
    ConfirmState confirm_; ToastState toastState_; Router router_;
    std::function<void()> onConfirm_, onCancel_, toastCompletion_;
    Control* outside_{};
    Deadlines::Token toastTimer_{};
    mutable std::array<std::uint64_t, 4> revisionKey_{};
    mutable std::uint64_t revision_{};
    bool dimmed_{};
    core::ContentDimTrack filter_;
    void applyDim();
    void setDimmed(bool);
    void finish(ConfirmState::Decision);
    void expireToast(std::uint64_t token);
    void notify() const { if (onChange) onChange(); }
};
} // namespace endfield::modules::design
