#pragma once
// Page, canvas and context of the redesign: the port of
// Sources/HUDDesignScreen.swift (HUDDesignContext, HUDDesignPage,
// HUDDesignCanvas) and the canvas half of Sources/HUDModuleContent.swift's
// directional switch (HUDDirectionalContent). LIB-72..82, LIB-99..101.
//
// How a module canvas is written (the C++ shape of a Mac *Canvas.swift):
//
//   class ClipboardCanvas final : public design::Canvas {
//   public:
//       ClipboardCanvas(design::Context& c, ClipboardModel& m) : Canvas(core::Module::clipboard, c), model_(m) {}
//       void buildPage(design::Page& page) override {
//           auto& clear = page.make<design::Button>("clipboard.clear", text("Clear", "清空"), design::Icon::trash,
//               design::Button::Kind::secondary, design::Button::Height::small);
//           clear.place({0, 0});
//           clear.onActivate = [this] { confirmClear(); };          // capture the canvas, never the page
//           header().configure({"CLIPBOARD"}, "MOD-03", title("Clipboard", "剪贴板缓存"), counter(), false, {&clear});
//           auto& tabs = page.makeTabs("clipboard.filter", titles(), filterIndex(), design::metric::boxWidth);
//           tabs.onSelect = [this](int index, double direction) { filter_ = index; navigate(direction); };
//           tabs.layer().setOrigin({0, design::metric::headerHeight});
//           page.layer().add(tabs.layer()); page.tabs = &tabs; page.add(tabs.controls()); page.stagger(tabs.layer());
//           auto& list = page.scrollArea("clipboard.rows", {0, 125, 590, height});
//           for (...) { auto& card = page.make<design::ListCard>(...); list.content().add(card.layer());
//                       page.add(card.controls()); page.stagger(card.layer()); }
//           list.setContentHeight(contentHeight); page.restoreOffset(list);
//       }
//       bool handleKey(const design::KeyInput& k) override { ... }
//   };
//
// The owner core (tools/hud_design_owner.hpp) does everything else a
// tools/X_preview.cpp used to repeat: projection and unprojection through
// boxWorld, routing pointer / wheel / keys / text input here, the native
// scene, accessibility, prewarm, park on close and release.
//
// Timing: every host-facing entry takes the owner's `now`, stores it in the
// Environment and only then runs module code, so module code calls motion and
// deadlines without passing time (as the Swift uses the implicit CA clock).
#include "modules/hud_design_cards.hpp"
#include "modules/hud_design_header.hpp"
#include "modules/hud_design_menu.hpp"
#include "modules/hud_design_overlay.hpp"
#include "modules/hud_design_redaction.hpp"
#include "modules/hud_design_scroll.hpp"
#include "modules/hud_design_text_field.hpp"

namespace endfield::modules::design {
class Canvas;

// HUDDesignContext (LIB-72): one per HUD, shared by every design canvas and
// the overlay. The host (the native overlay owner, tools/
// hud_design_overlay_owner.hpp) creates it before every design owner and
// hands it to them (built-in owners directly; areas through
// ApplicationHost::find<design::Context>(contextServiceKey)).
inline constexpr std::string_view contextServiceKey = "hud.design.context";
class Context final {
public:
    struct Appearance {
        core::Language language{core::Language::english}; // resolved
        TextRegion systemRegion{TextRegion::simplifiedChinese}; // first CJK of the preferred UI languages
        std::string accentHex{"FAD41F"};
        bool light{}, reduceMotion{}, recordingMode{};
        bool operator==(const Appearance&) const = default;
    };
    Context(TextMeasurer&, TextCasing uppercase);
    ~Context();
    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;
    Environment& env() noexcept { return env_; }
    const Environment& env() const noexcept { return env_; }
    CenterOverlay& overlay() noexcept { return *overlay_; }
    // Appearance event: palette (HUDColor.apply + refreshPalette: the overlay
    // recolours, the ACTIVE canvas closes its popups and re-renders now, the
    // others when next shown), language (stale pages rebuild when shown),
    // Reduce Motion, Recording mode (every built canvas re-renders in place,
    // recordingModeChanged). Returns true when anything visible changed.
    bool apply(const Appearance&, double now);
    const Appearance& appearance() const noexcept { return appearance_; }
    // Design units per content px (baseUnit; the display adaptation is in the
    // centre plane already) and the backing scale of one box px; a change > 1e-4
    // / 1e-3 re-renders every built canvas in place (renderScaleChanged).
    void update(double unit, double windowScale, double now);
    double unit() const noexcept { return unit_; }
    double boxScale() const noexcept { return env_.text.boxScale; }
    // Closes every canvas's menus and pop-overs, ends editing (commit) and
    // drags, then presents the bar (HUDDesignContext.confirm).
    void confirm(ConfirmRequest, std::function<void()> onConfirm, std::function<void()> onCancel = {});
    void toast(ToastRequest, std::function<void()> completion = {});
    // L10n.text / core::localized in the context language.
    std::string text(std::string_view english, std::string_view simplifiedChinese,
        std::span<const std::string_view> arguments = {}) const;
    // Shared deadlines (overlay timers, scroll bars, canvases' module timers).
    std::optional<double> nextWakeTime() const noexcept;
    bool advance(double now);
    void setEditorHost(TextEditorHost* host) noexcept { editorHost_ = host; }
    TextEditorHost* editorHost() const noexcept { return editorHost_; }
    // Menus, pop-overs, editors, drags or the overlay changed: owners refresh
    // pointer capture, UIA and the parallax lock; a gesture or editor that
    // needs a still plane began (onLockParallax).
    std::function<void()> onInputStateChange, onLockParallax, invalidate;
    std::span<Canvas* const> canvases() const noexcept { return canvases_; }
private:
    friend class Canvas;
    Environment env_;
    Appearance appearance_;
    std::unique_ptr<CenterOverlay> overlay_;
    std::vector<Canvas*> canvases_;
    TextEditorHost* editorHost_{};
    double unit_{geometry::baseUnit};
    void registerCanvas(Canvas&);
    void unregisterCanvas(Canvas&) noexcept;
};

// HUDDesignPage (LIB-74): one page in box px (header y 0-81, body from 81).
// Owns the page's layers and controls; routed controls in registration order
// (later = on top); the stagger list in document order; scroll areas by id
// with offsets restored across in-place re-renders; the first tab bar (Q/E);
// the back action (Esc).
class Page final {
public:
    Page(Context&, std::string_view idPrefix);
    ~Page();
    Page(const Page&) = delete;
    Page& operator=(const Page&) = delete;
    Layer& layer() const noexcept { return *layer_; }
    Builder& builder() noexcept { return builder_; }
    Layer& makeLayer(std::string_view id) { return builder_.layer(id); }
    template <class T, class... A> T& make(A&&... args) { return builder_.make<T>(std::forward<A>(args)...); }
    // Non-control components owned by the page (tabs, chips, plates, cells).
    Tabs& makeTabs(std::string id, std::vector<Tabs::Title>, int selectedIndex, double width, bool keycaps = true, bool compact = false);
    template <class T, class... A> T& own(A&&... args) {
        auto owned = std::make_unique<T>(builder_, std::forward<A>(args)...);
        auto& result = *owned;
        parts_.push_back(std::shared_ptr<void>(std::move(owned)));
        return result;
    }
    void add(Control&);
    void add(std::span<Control* const>);
    void add(const std::vector<Control*>& controls) { add(std::span<Control* const>(controls)); }
    void remove(Control&);
    const std::vector<Control*>& controls() const noexcept { return controls_; }
    // HUDDesignPage.stagger: `layer` joins the entrance stagger (it becomes
    // animatable: an entrance is opacity + translation).
    void stagger(Layer&);
    std::span<Layer* const> staggered() const noexcept { return staggered_; }
    ScrollArea& scrollArea(std::string id, Rect frame, Layer* parent = nullptr);
    // Call after filling an area: a re-render keeps its position.
    void restoreOffset(ScrollArea&);
    std::span<const std::unique_ptr<ScrollArea>> scrollAreas() const noexcept { return scrolls_; }
    Tabs* tabs{};                              // the first tab bar (Q / E cycle it)
    std::function<void()> back;                // Esc (the header's back button)
private:
    friend class Canvas;
    LayerArena arena_;
    ControlSet set_;
    Builder builder_;
    Layer* layer_;
    std::vector<Control*> controls_;
    std::vector<Layer*> staggered_;
    std::vector<std::unique_ptr<ScrollArea>> scrolls_;
    std::vector<std::shared_ptr<void>> parts_;
    std::vector<std::pair<std::string, double>> restored_;
};

// HUDDesignCanvas (LIB-75..82): the redesigned module base.
class Canvas {
public:
    Canvas(core::Module, Context&);
    virtual ~Canvas();
    Canvas(const Canvas&) = delete;
    Canvas& operator=(const Canvas&) = delete;
    core::Module module() const noexcept { return module_; }
    Context& context() const noexcept { return *context_; }
    Environment& env() const noexcept { return context_->env(); }

    // --- Subclass hooks (the Mac's overridables) ---------------------------------
    // Builds the current page: configure header(), add layers to page.layer(),
    // register controls, stagger targets and scroll areas.
    virtual void buildPage(Page&) = 0;
    virtual void didActivate() {}
    virtual void didDeactivate() {}
    virtual void willLeave() {}                 // stop timers and loops
    virtual bool handleKey(const KeyInput&) { return false; }      // precedence 8
    virtual bool interceptKey(const KeyInput&) { return false; }   // precedence 3 (hotkey capture)
    virtual bool handleExtraKey(const KeyInput&) { return false; } // precedence 11
    virtual bool handlePointerDown(Point, const PointerInput&) { return false; } // non-control surfaces
    virtual void handlePointerDragged(Point) {}
    virtual void handlePointerUp(Point) {}
    virtual bool handleRightPointerDown(Point) { return false; }
    virtual void handlePointerMoved(std::optional<Point>) {}
    virtual bool handleScroll(Point, Point /*delta*/) { return false; } // box px, + down; before scroll areas
    virtual bool handleMagnify(Point, double) { return false; }
    virtual std::optional<Cursor> cursor(std::optional<Point>) const { return std::nullopt; }
    virtual void recordingModeChanged();        // default: re-render in place when rendered
    virtual void renderScaleChanged();          // default: re-render in place when rendered
    virtual void paletteChanged();              // default: close popups; active -> re-render now
    // Presses and wheel inside this rect belong to the module rather than the
    // shell (box px); modules that overflow the box widen it.
    virtual Rect hitBounds() const { return metric::box; }
    // Native panels (file chooser, colour wheel) keep the HUD up meanwhile.
    virtual bool presentingPanel() const { return false; }
    // Live readouts and module timers: wake the owner at the returned time.
    virtual std::optional<double> nextWakeTime(double) const { return std::nullopt; }
    virtual void wake(double) {}

    // --- Services for subclasses --------------------------------------------------
    ModuleHeader& header();                     // created with the first page
    bool showsHeader() const noexcept { return showsHeader_; }
    void setShowsHeader(bool);                  // false for Now Playing
    Layer& root() const noexcept { return *root_; }   // the 590 x 510 box, not clipped
    Layer& top() const noexcept { return *top_; }     // menus, pop-overs (z 30), the bubble (z 40)
    Page& page() const noexcept { return *page_; }
    bool isActive() const noexcept { return active_; }
    std::optional<core::Language> renderedLanguage() const noexcept { return renderedLanguage_; }
    double now() const noexcept { return env().now; }
    motion::Clock clock() const noexcept { return env().clock(); }
    std::string text(std::string_view english, std::string_view simplifiedChinese, std::span<const std::string_view> arguments = {}) const;
    ModuleTitle title(std::string_view english, std::string_view simplifiedChinese) const;
    // In-place re-render (LIB-75): no entrance, every scroll area keeps its
    // offset by id; during a sub-page swap the new page stays held invisible
    // until the swap lands. Text fields that are not editing are dropped.
    void rerender();
    // Sub-navigation (LIB-76): offset subpageOffset(dx, dy); closes popups,
    // ends editing (commit), cancels gestures; offsets of pages left are
    // retained and GOING BACK (dx < 0) restores them, forward starts at 0; the
    // page and header leave (.13 s, -0.6 x offset), the router empties and
    // 130 ms later the new page installs and plays the header + stagger
    // entrance. A second navigate during a swap RETARGETS it and lands on the
    // original deadline. Reduce Motion or a detached root (not shown): install
    // at once. Q4: the next page (and its own staging header) is BUILT here,
    // when the swap starts, and installed at the deadline.
    void navigate(double dx, double dy = 0);
    // Completes a pending swap at once; `concealed` keeps page and header held
    // invisible until the module shows again (a module leaving).
    void settleSwap(bool concealed = false);
    bool transitioning() const noexcept { return swapDeadline_.has_value(); }
    // Menus (LIB-78): one menu or pop-over at a time (opening closes the
    // other); the anchor control (held by id, re-resolved after a re-render)
    // gives the rect and minimum width and shows its open state; picking
    // closes the menu THEN calls onPick; closing clears the rows' hover.
    Menu& openMenu(std::string id, std::vector<Menu::Item>, const Control& anchor, std::optional<double> minimumWidth,
        std::function<void(const Menu::Item&)> onPick);
    Menu& openMenu(std::string id, std::vector<Menu::Item>, Rect anchorRect, const Control* anchor,
        std::optional<double> minimumWidth, std::function<void(const Menu::Item&)> onPick);
    void closeMenu();
    void openPopover(std::unique_ptr<Popover>, const Control* anchor, std::optional<Rect> anchorRect, std::vector<Control*> controls);
    void closePopover();
    void closePopups();
    bool menuOpen() const noexcept { return menu_ != nullptr; }
    bool popoverOpen() const noexcept { return popover_ != nullptr; }
    // Text fields (one edits at a time; others end with commit).
    void registerField(TextField&);
    void endEditing(bool commit);
    bool editingText() const noexcept;
    TextField* editingField() const noexcept;
    // Shared overlay: ends editing (commit) and cancels gestures first.
    void confirm(ConfirmRequest, std::function<void()> onConfirm, std::function<void()> onCancel = {});
    void toast(ToastRequest, std::function<void()> completion = {});
    // The result toast (LIB-82): title = the UI-language string of the pair;
    // caption = `detail` when given, else EMPTY in English and the English
    // string in every other language. Uppercase comes from the toast style.
    void toast(Icon, std::string_view english, std::string_view simplifiedChinese, std::optional<std::string> detail = {});
    void wireBack(Page&, std::function<void()> action); // header back button + Esc
    void hostBubble(Slider&);                  // the slider's bubble in the top layer
    // A module timer (age refresh, countdown): runs at now + delay unless the
    // group is cancelled; groups end with the canvas.
    Deadlines::Token timerGroup();
    void after(double delay, Deadlines::Token group, std::function<void()>);
    void cancel(Deadlines::Token group);

    // --- Host-facing (tools/hud_design_owner.hpp) ---------------------------------
    // makeContent: reveal a settled page; rebuild a stale one (built for
    // another language or palette generation).
    void show(double now);
    void setActive(bool, double now);           // deactivation forgets retained sub-page offsets
    // HUDDirectionalContent: the entrance after the module swap (header wipe +
    // stagger from `offset` content px); nothing under Reduce Motion.
    void playEntrance(Point offset, double now);
    // Before the content leaves: close popups, end editing WITH commit, cancel
    // gestures (sliders without commit), settle a pending swap concealed, willLeave.
    void prepareToLeave(double now);
    bool pointerDown(Point, const PointerInput&, double now);
    void pointerDragged(Point, double now);
    void pointerUp(Point, double now);
    bool rightPointerDown(Point, double now);
    // The highlight pass (updateHighlights on root) + the module hook: the
    // owner calls it for EVERY pointer event with the button state (moves,
    // presses, drags, releases), as SystemHUDView.updateControlHighlights.
    void pointerMoved(std::optional<Point>, bool buttonDown, double now);
    bool scroll(Point, Point delta, double now);
    bool magnify(Point, double magnification, double now);
    bool keyDown(const KeyInput&, double now);
    bool containsControl(Point) const;          // a press here goes to the module before the shell
    bool capturesPointer() const noexcept { return menuOpen() || popoverOpen(); }
    bool dragging() const noexcept { return router_.tracking(); }
    bool inputLocked() const noexcept { return dragging() || editingText(); } // freezes parallax
    // Accessibility (LIB-81): items only while active and not swapping.
    void accessibilityItems(std::vector<AccessibilityItem>&) const;
    bool performAccessibilityAction(std::string_view id, double now);
    bool adjustAccessibilitySlider(std::string_view id, double value, double now); // commits (value, true) when changed
    bool setAccessibilityText(std::string_view id, std::string_view text, double now);
    // Content generation for the native scene: bumps whenever any arena of
    // the canvas (page, header, menu, pop-over) changed content.
    std::uint64_t contentRevision() const noexcept;
    bool requiresFrames(double now) const;
    std::optional<double> nextDeadline(double now) const;
private:
    friend class Context;
    core::Module module_;
    Context* context_;
    std::string prefix_;
    LayerArena arena_;                          // root, top
    ControlSet set_;
    Layer *root_, *top_;
    // The shown header and the one a sub-page built at navigate time
    // configures until its swap lands (Q4: the next page is built when the
    // swap starts, so its header must not change the leaving one).
    std::array<std::unique_ptr<ModuleHeader>, 2> headers_;
    std::size_t currentHeader_{};
    std::array<std::uint64_t, 2> headerPalette_{};  // palette generation each header was last coloured for
    bool staging_{};                            // buildPage of the pending page
    std::unique_ptr<Page> page_, pending_;
    std::vector<std::unique_ptr<Page>> retired_; // replaced pages that still hold an editing field
    // Replaced pages, closed menus and pop-overs: destroyed by a deadline at
    // the current time, never while one of their callbacks is on the stack.
    std::vector<std::shared_ptr<void>> graveyard_;
    std::unique_ptr<Menu> menu_;
    std::unique_ptr<Popover> popover_;
    std::optional<std::string> menuAnchor_;
    std::vector<Control*> popoverControls_;
    std::vector<TextField*> fields_;
    Router router_;
    bool active_{}, showsHeader_{true}, attached_{};
    std::optional<core::Language> renderedLanguage_;
    std::uint64_t renderedPalette_{}, pageSerial_{}, structure_{};
    mutable std::array<std::uint64_t, 6> revisionKey_{};
    mutable std::uint64_t revision_{};
    std::optional<double> swapDeadline_;
    Point swapOffset_;
    Layer* leaving_{};                          // the old page's layer while it leaves
    std::vector<std::pair<std::string, double>> retainedOffsets_, pendingOffsets_;
    Deadlines::Token swapToken_{}, collectToken_{};
    std::vector<Deadlines::Token> timerGroups_;
    void begin(double now);                     // host entry: the clock
    ModuleHeader* currentHeader() const noexcept { return headers_[currentHeader_].get(); }
    std::unique_ptr<Page> makePage(const std::vector<std::pair<std::string, double>>& offsets, bool staged);
    void install(std::unique_ptr<Page>, bool swapHeader);
    void completeSwap();
    std::vector<std::pair<std::string, double>> currentOffsets() const;
    void refreshRouter();
    void cancelGestures();
    void finishLeave();
    void revealSettledPage();
    bool stale() const noexcept;
    void bury(std::shared_ptr<void>);
    void forgetFields(const Layer& subtree);
    void pruneRetired();
    Control* findControl(std::string_view id) const;
    void notifyInput() const;
};
} // namespace endfield::modules::design
