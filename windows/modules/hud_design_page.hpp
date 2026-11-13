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
//               design::Button::Kind::secondary, design::Button::Height::compact);
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
//           list.setContentHeight(contentHeight); page.restoreOffset(list);   // in-place re-renders only
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
    // Reduce Motion, Recording mode (recordingModeChanged: a shown canvas
    // re-renders in place now, a built one that is not shown when next
    // shown). Returns true when anything visible changed.
    bool apply(const Appearance&, double now);
    const Appearance& appearance() const noexcept { return appearance_; }
    // Design units per content px (baseUnit; the display adaptation is in the
    // centre plane already) and the backing scale of one box px; a change > 1e-4
    // / 1e-3 re-renders every built canvas in place (renderScaleChanged).
    void update(double unit, double windowScale, double now);
    double unit() const noexcept { return unit_; }
    double boxScale() const noexcept { return env_.text.boxScale; }
    // Closes every canvas's menus and pop-overs, then presents the bar
    // (HUDDesignContext.confirm; Canvas::confirm commits its own editing
    // first).
    void confirm(ConfirmRequest, std::function<void()> onConfirm, std::function<void()> onCancel = {});
    // The keyboard focus left the design fields' text views
    // (HUDDesignTextField.textDidEndEditing, :281-286: once the first
    // responder is no longer the field's text view, the field commits). On
    // the Mac any press in the HUD view outside an editing text view makes
    // SystemHUDView (or another text view) first responder; on Windows every
    // press reaches only the owner that takes it, so the owners report it
    // here: a press a design owner takes (for every canvas but `except`, the
    // canvas that got the press: it commits its own fields itself,
    // Canvas::pointerDown), a press Notes takes, and a press no module owner
    // takes (the shell's clock, logo, navigation; DesignOverlayOwner observes
    // those last in the pointer chain). Each editing field commits
    // (TextField::editorFocusLost) and a focused body editor blurs (its
    // textDidEndEditing save); nothing else changes.
    void editorFocusMoved(const Canvas* except = nullptr);
    // Every canvas's open menu closes (SystemHUDView.beginOverlayPlacement:
    // `if activeDesignCanvas?.isMenuOpen == true { closeMenu() }`); pop-overs
    // stay.
    void closeMenus();
    void toast(ToastRequest, std::function<void()> completion = {});
    // L10n.text / core::localized in the context language.
    std::string text(std::string_view english, std::string_view simplifiedChinese,
        std::span<const std::string_view> arguments = {}) const;
    // The host clock (OPEN-B5-1, the overlay owner sets it from the host's
    // area clock): data that reaches a canvas outside a frame (an executor
    // drain) first catches env().now up to it (Canvas::catchUpClock), so
    // motion it starts begins now, not at the last frame. Never moves it back.
    void setHostClock(std::function<double()> clock) { hostClock_ = std::move(clock); }
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
    // Pages every canvas has built so far (makePage): the work bound of an
    // appearance change.
    std::uint64_t pagesBuilt() const noexcept { return pagesBuilt_; }
private:
    friend class Canvas;
    Environment env_;
    Appearance appearance_;
    std::unique_ptr<CenterOverlay> overlay_;
    std::vector<Canvas*> canvases_;
    TextEditorHost* editorHost_{};
    std::function<double()> hostClock_;
    double unit_{geometry::baseUnit};
    std::uint64_t pagesBuilt_{};
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
    // Call after filling an area: an in-place re-render keeps its position
    // (a sub-navigation, a Back or a return to the module has none).
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
    std::uint64_t serial_{};                   // the page's id serial (prefix ".page<serial>")
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
    // A key that acts on something outside the page (a player) still works
    // during a sub-page swap; other module keys are dropped until the page
    // lands (Esc and Q / E still retarget the swap).
    virtual bool handlesKeyDuringSwap(const KeyInput&) { return false; }
    virtual bool interceptKey(const KeyInput&) { return false; }   // precedence 3 (hotkey capture)
    virtual bool handleExtraKey(const KeyInput&) { return false; } // precedence 11
    virtual bool handlePointerDown(Point, const PointerInput&) { return false; } // non-control surfaces
    virtual void handlePointerDragged(Point) {}
    virtual void handlePointerUp(Point) {}
    virtual bool handleRightPointerDown(Point) { return false; }
    virtual void handlePointerMoved(std::optional<Point>) {}
    // Before every press, ahead of menus and the router (the Mac canvases'
    // pointerDown overrides, e.g. ArchiveCanvas.swift:1184-1195): true consumes it.
    virtual bool interceptPointerDown(Point, const PointerInput&) { return false; }
    // A module editor besides the canvas's text fields (Archive's persistent
    // body editor, ArchiveCanvas.swift:1145-1150): editingText() includes it
    // and endEditing() ends it too, after the fields.
    virtual bool editingOtherText() const { return false; }
    virtual void endOtherEditing(bool /*commit*/) {}
    // prepareToLeave and setActive(false) call this first, before they close
    // popups and end editing (the Mac's overrides that run before super:
    // ArchiveCanvas.swift:152-160 remembers the focused field and selection).
    virtual void willEndInteraction() {}
    // UIA actions on items that are not routed controls (Archive's documents
    // scrolled out of the grid, ArchiveCanvas.swift:1227-1231) and text set on
    // an editor that is not a TextField (its body): true when handled; tried
    // before the router's controls.
    virtual bool accessibilityAction(std::string_view /*id*/) { return false; }
    virtual bool accessibilityText(std::string_view /*id*/, std::string_view /*value*/) { return false; }
    // After a menu or pop-over closed, however it closed (pick, Esc, outside
    // press, closePopups): the Mac canvases' closeMenu / closePopover
    // overrides (ArchiveCanvas.swift:834-847 drops its pop-over fields and
    // refreshes soon). The closed object's controls must not be used again.
    virtual void didCloseMenu() {}
    virtual void didClosePopover() {}
    virtual bool handleScroll(Point, Point /*delta*/) { return false; } // box px, + down; before scroll areas
    virtual bool handleMagnify(Point, double) { return false; }
    virtual std::optional<Cursor> cursor(std::optional<Point>) const { return std::nullopt; }
    // Default: a rendered canvas re-renders in place (HUDDesignCanvas
    // .recordingModeChanged, :229; .renderScaleChanged, :231) when shown; one
    // that is not shown re-renders when next shown (deferRerender).
    virtual void recordingModeChanged();
    virtual void renderScaleChanged();
    virtual void paletteChanged();              // default: close popups; active -> re-render now
    // Reduce Motion changed (verbs already honour it; the Mac host calls a
    // module's refreshMotionPreference for its live loops, Work Mode's blink).
    virtual void reduceMotionChanged() {}
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
    // HUDDesignCanvas.isCancellingGestures: true while leaving or deactivating
    // ends the controls' gestures; an `ended` a control reports then is an
    // interruption, not a drop.
    bool cancellingGestures() const noexcept { return cancellingGestures_; }
    std::optional<core::Language> renderedLanguage() const noexcept { return renderedLanguage_; }
    double now() const noexcept { return env().now; }
    motion::Clock clock() const noexcept { return env().clock(); }
    // env().now moves forward to the context's host clock (setHostClock), if
    // any: a module's data notice that arrives outside a frame calls it before
    // it starts motion. The second form catches up to a time a canvas's own
    // owner gives (Media Assembly). Neither moves it back.
    void catchUpClock();
    void catchUpClock(double hostTime);
    std::string text(std::string_view english, std::string_view simplifiedChinese, std::span<const std::string_view> arguments = {}) const;
    ModuleTitle title(std::string_view english, std::string_view simplifiedChinese) const;
    // In-place re-render (LIB-75): no entrance of its own, every scroll area
    // keeps its offset by id, except for a module that was left
    // (showsFromTop), which comes back at the top; during a sub-page swap the
    // new page starts at the top and stays held invisible until the swap
    // lands. Text fields that are not editing are dropped. A re-render while
    // the module entrance still plays (data arriving as the module opens or
    // switches in) carries that entrance on in phase (playPageEntrance with a
    // negative delay): rows already shown stay shown, the rest still enter.
    // The new page keeps the replaced page's layer ids unless that page holds
    // an editing field (makePage), so the scene reuses every part whose
    // artwork is unchanged instead of rasterizing the whole page again.
    void rerender();
    // The same call (a re-render keeps ids wherever that is safe).
    void rerenderKeepingIds() { rerender(); }
    // Shown: attached (show() .. prepareToLeave()) or active.
    bool isShown() const noexcept { return attached_ || active_; }
    // The module was left (prepareToLeave): its next showing builds every
    // list from the top. show() and setActive(true) clear it.
    bool showsFromTop() const noexcept { return showsFromTop_; }
    // Keeps the scroll a module set for its next showing (a reveal made while
    // it was hidden, a File Shelf drag's retract) instead of starting at the top.
    void keepsScrollOnNextShow() noexcept { showsFromTop_ = false; }
    // A change the Mac applies to every built page at once (Recording mode,
    // render scale; a module's own broadcast) that this hidden canvas applies
    // when next shown: show / setActive re-render it (recordingDeferred is
    // true for that build). A shown canvas re-renders instead.
    void deferRerender() noexcept { deferredStale_ = true; }
    void deferRecordingMode() noexcept { deferRerender(); }
    // True while buildPage runs for a deferred change alone (deferRerender):
    // the Mac rebuilt this page at the change, while it was hidden, so
    // effects a build starts (Notes' redaction wipe) ran out unseen and must
    // not play now that the page is shown.
    bool recordingDeferred() const noexcept { return deferredBuild_; }
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
    // Completes a pending swap at once. `concealed` (a module leaving, the
    // HUD's close): the old page and header finish their leave and end
    // transparent; the next page lands, without an entrance, when the module
    // shows again (show, setActive(true)), or a navigate or new session drops
    // the old page first. transitioning() is false meanwhile.
    void settleSwap(bool concealed = false);
    bool transitioning() const noexcept { return swapDeadline_.has_value(); }
    // A swap not landed yet: in flight (transitioning) or settled concealed
    // and waiting for the module to show again. Until it lands, header()
    // outside a build is the leaving header and rerender() rebuilds only the
    // staged page.
    bool swapPending() const noexcept { return swapDeadline_ || concealedSwap_; }
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
    // Virtual like HUDDesignCanvas.closePopups (Now Playing also closes its
    // volume pop-up, NowPlayingCanvas.swift:135); every internal caller
    // (navigate, leave, deactivate, palette, openMenu / openPopover, confirm)
    // dispatches to it.
    virtual void closePopups();
    // HUDDesignCanvas.keepsClosingPopupsDrawn / retirePopup (2e08932): while
    // the module leaves (prepareToLeave, setActive(false)) a menu or pop-over
    // that closes stops taking input but stays drawn to leave with the page;
    // the next showing (show, setActive(true)) or a new session removes it.
    // Every other close removes it at once. `remove` takes the closed popup's
    // layers out of the tree; capture only `this` or owning pointers.
    bool keepsClosingPopupsDrawn() const noexcept { return keepsClosingPopupsDrawn_; }
    void retirePopup(std::function<void()> remove, bool keepingDrawn);
    bool menuOpen() const noexcept { return menu_ != nullptr; }
    bool popoverOpen() const noexcept { return popover_ != nullptr; }
    // Text fields (one edits at a time; others end with commit).
    void registerField(TextField&);
    void endEditing(bool commit);
    // HUDDesignCanvas.suspendInput (:556): inline editors commit and gestures
    // end (a slider drag without commit) while the page stays drawn, menus,
    // pop-overs and a running sub-page swap included (the HUD's close: they
    // leave with the module). navigate, prepareToLeave and setActive(false)
    // run it too. A module with gestures of its own ends them here too
    // (calling this first), so they stop when the page they act on leaves.
    virtual void suspendInput();
    // The field's text view stopped being first responder: every editing
    // field of this canvas commits (TextField::editorFocusLost; see
    // Context::editorFocusMoved), and other text it edits ends
    // (endOtherEditing(true): the body editors blur, saving).
    void editorFocusLost();
    // refreshTextFieldProjection: an editing field a re-render moved follows on screen.
    void refreshTextFieldProjection();
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
    // Components that outlive page builds (the Mac's lazy `var` text fields of
    // Work Mode and Calendar, re-registered and re-added by every buildPage):
    // made with this builder they live in the canvas's own arena, whose
    // content changes the scene tracks like the page's.
    Builder& canvasBuilder() noexcept { return builder_; }
    // An arena of the module's own (Media Assembly's stage, built again for a
    // new palette) whose content the scene tracks like the canvas's; nullptr
    // stops tracking. Create it with &env().deadlines for completions.
    void trackArena(const LayerArena* arena) noexcept { tracked_ = arena; }
    // True while buildPage builds a sub-page swap's next page (Q4: built when
    // the swap starts, installed when it lands); the shown page is still page().
    bool buildingStagedPage() const noexcept { return staging_; }
    // Runs after a built page is installed (in place, at once, or when a
    // sub-page swap lands). The Mac builds the next page when the swap lands;
    // here it is built when the swap starts (Q4), so a canvas-owned component
    // shown on both pages (Calendar's fields) joins the new page here and
    // leaves with the old one meanwhile.
    virtual void didInstall(Page&) {}

    // --- Host-facing (tools/hud_design_owner.hpp) ---------------------------------
    // makeContent: reveal a settled page; rebuild a stale one (built for
    // another language or palette generation). Virtual like the Mac's
    // makeContent(for:style:): a module re-syncs its model around it (Clipboard,
    // Event Log, Volume) and calls Canvas::show.
    virtual void show(double now);
    void setActive(bool, double now);
    // The HUD closed (overlayConcealed): the Mac drops SystemHUDView and
    // builds every canvas anew at the next opening (OverlayController
    // .tearDownSystemPresentation `systemView = nil`), so what a canvas
    // instance holds starts over. While inactive, a scrolled page is built
    // again at offset 0 (an unscrolled one stays);
    // modules reset their own canvas state (selection, sub-page, tab,
    // drafts) and call this.
    virtual void resetSession();
    // HUDDirectionalContent: the entrance after the module swap (header wipe +
    // stagger from `offset` content px); nothing under Reduce Motion. The
    // two-argument form (the Mac's playEntrance(from:), which a sub-page swap
    // also calls) plays it at once; playEntrance(from:delay:) holds it back
    // `delay` s (the HUD's open, until the centre shows). The canvas records
    // the entrance (offset and start) until the module leaves, so a page
    // re-rendered in place while it plays carries it on (rerender).
    void playEntrance(Point offset, double now) { playEntrance(offset, now, 0); }
    void playEntrance(Point offset, double now, double delay);
    // HUDDesignCanvas.playPageEntrance(from:delay:): the page's part of the
    // entrance (the header keeps its own), the row stagger plus a subclass's
    // extras built with the page (Work Mode's inner title wipe, the File
    // Shelf card). A page rebuilt mid-entrance replays it with a negative
    // `delay` (how long ago the entrance began), resuming in phase.
    virtual void playPageEntrance(Point offset, double delay);
    // Before the content leaves: close popups, end editing WITH commit, cancel
    // gestures (sliders without commit), settle a pending swap concealed, willLeave.
    void prepareToLeave(double now);
    // Virtual like HUDDesignCanvas.pointerDown(at:event:) (Media Assembly
    // resets a tool slider on a double click first); overrides call it.
    virtual bool pointerDown(Point, const PointerInput&, double now);
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
    // HUDDesignCanvas.control(atBox:): the enabled control a left press at
    // this point would pick, so a right-click resolves its target exactly as
    // a left-click does (null while inactive or swapping).
    Control* control(Point) const;
    // Virtual like HUDDesignCanvas.capturesPointer (Now Playing's open volume
    // pop-up captures too, NowPlayingCanvas.swift:29).
    virtual bool capturesPointer() const { return menuOpen() || popoverOpen(); }
    bool dragging() const noexcept { return router_.tracking(); }
    // HUDDesignCanvas.isInputLocked (freezes parallax): overridable like the
    // Mac's (Settings adds shortcut recording, App Shortcuts its chooser).
    virtual bool inputLocked() const { return dragging() || editingText(); }
    // Keys belong to this canvas before the host's own key handling and key
    // filters (SystemHUDView's Settings capture monitor while a shortcut records).
    virtual bool capturesKeys() const { return false; }
    // Accessibility (LIB-81): items only while active and not swapping.
    // Virtual like HUDDesignCanvas.accessibilityItems (Volume leaves out the
    // app sliders scrolled out of its list).
    virtual void accessibilityItems(std::vector<AccessibilityItem>&) const;
    bool performAccessibilityAction(std::string_view id, double now);
    // Commits (value, true) when changed. Virtual like the Mac's (Media
    // Assembly's sticker and crop-edge sliders are no controls).
    virtual bool adjustAccessibilitySlider(std::string_view id, double value, double now);
    bool setAccessibilityText(std::string_view id, std::string_view text, double now);
    // HUDDesignCanvas.refreshRouter: the router takes the page's controls
    // again after a module added or removed some in place (Event Log's
    // virtualised rows, EventLogCanvas.swift:338).
    void refreshRouter();
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
    LayerArena arena_;                          // root, top, canvas-owned components
    ControlSet set_;
    Builder builder_;
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
    // Closed popups kept drawn while the module leaves (retirePopup); after
    // the graveyard, so they go first when the canvas dies.
    std::vector<std::function<void()>> departedPopups_;
    bool keepsClosingPopupsDrawn_{};
    std::unique_ptr<Menu> menu_;
    std::unique_ptr<Popover> popover_;
    std::optional<std::string> menuAnchor_;
    std::vector<Control*> popoverControls_;
    std::vector<TextField*> fields_;
    Router router_;
    bool active_{}, showsHeader_{true}, attached_{}, deferredStale_{}, deferredBuild_{}, showsFromTop_{}, cancellingGestures_{};
    std::optional<core::Language> renderedLanguage_;
    std::uint64_t renderedPalette_{}, pageSerial_{}, structure_{};
    const LayerArena* tracked_{};
    mutable std::array<std::uint64_t, 8> revisionKey_{};
    mutable std::uint64_t revision_{};
    std::optional<double> swapDeadline_;
    bool concealedSwap_{};                      // settleSwap(true) left the old page leaving; pending_ lands on reveal
    Point swapOffset_;
    // The entrance playing on the page (HUDDesignCanvas.entrance: its offset
    // and start), until the module leaves; an in-place re-render carries it.
    struct Entrance { Point offset; double start{}; };
    std::optional<Entrance> entrance_;
    static double entranceLength() noexcept;
    Layer* leaving_{};                          // the old page's layer while it leaves
    Deadlines::Token swapToken_{}, collectToken_{};
    std::vector<Deadlines::Token> timerGroups_;
    void begin(double now);                     // host entry: the clock
    ModuleHeader* currentHeader() const noexcept { return headers_[currentHeader_].get(); }
    std::unique_ptr<Page> makePage(const std::vector<std::pair<std::string, double>>& offsets, bool staged);
    void install(std::unique_ptr<Page>, bool swapHeader);
    void completeSwap();
    std::vector<std::pair<std::string, double>> currentOffsets() const;
    void cancelGestures();
    void finishLeave();
    void revealSettledPage();
    void landConcealedSwap();
    void closePopupsForLeave();
    void removeDepartedPopups();
    bool stale() const noexcept;
    void rerenderIfStale();
    void bury(std::shared_ptr<void>);
    void forgetFields(const Layer& subtree);
    void pruneRetired();
    Control* findControl(std::string_view id) const;
    void notifyInput() const;
};
} // namespace endfield::modules::design
