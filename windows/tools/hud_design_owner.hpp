#pragma once
// The reusable owner of redesigned modules: everything each tools/X_preview.cpp
// and its app/application_modules.cpp adapter repeat today (projection and
// unprojection, clientRect, covers, pointer / wheel / key routing, cursor,
// prewarm, open / close / park, upload / entries / collected / release, UI
// Automation, editor hosting), written once over modules::design::Canvas.
// Mac counterpart: the HUDModuleContent + HUDSourceWatchView glue around a
// HUDDesignCanvas (Sources/HUDModuleContent.swift beginDirectional / place /
// normalize, Sources/HUDDesignScreen.swift).
//
// A redesigned module is then: a Canvas subclass (portable), a factory that
// constructs DesignOwnerCore with its canvas(es), and DesignModuleOwner as its
// ModuleOwner (no hand-written adapter). Settings presents four modules from
// one owner: one canvas per module, sharing the core.
//
// Frame (update): for each canvas whose module is the current or incoming
// presentation surface, the core takes the surface's design pose from
// NativeModuleSurface (ModuleSurfacePose::contentWorld of a design screen =
// DesktopChromeLayout::boxWorld + the directional leave; no host clip, no
// report scale: A2's ModulePresentationSurface::designScreen / planeOffset /
// attached / entrance), shows / activates / deactivates the canvas on the
// selection edges (an unattached incoming screen is prepared, never drawn or
// hit), plays its entrance when ModulePresentationSurface::entrance appears
// (offset in content px), syncs and poses its NativeDesignScene and the text
// field host. Density: core::source::planeScale(boxWorld, box centre). Input: logical client -> physical ->
// unproject through boxWorld -> box px -> Canvas; keys: the text field host's
// filter (TSF) first, then the canvas precedence. Clipboard's artwork
// policy: a canvas not shown when the HUD closes (or once the close finished,
// overlayConcealed) keeps no artwork (NativeDesignScene::park); the shown one
// keeps it so reopening on it only re-uploads; a switch inside the open HUD
// only parks textures (releaseResources). Prewarm emits and rasterizes the
// first page off the opening frame. The context's one editor host follows
// the owner whose canvas runs (Context::setEditorHost before every canvas
// call). No timer, thread or window of its own.
#include "modules/hud_design_page.hpp"
#ifdef _WIN32
#include "app/module_owner.hpp"
#include "native/hud_design_automation.hpp"
#include "native/hud_design_scene.hpp"
#include "native/hud_design_text_field.hpp"

namespace endfield::tools {
struct DesignOwnerOptions {
    std::string name;                         // ModuleOwner::name (camelCase)
    app::ModuleRouting routing;               // design modules: pointer/wheel/key 9...99 as before
    native::LayerRasterOptions raster;        // pixelsPerPoint is replaced by the render scale
    native::DesignImages images;
    native::NativeDesignTextFieldOptions fields; // window, TSF, clipboard, edit menu (null manager: no fields)
    std::function<double()> now;              // the host clock (UIA actions, ScreenReaderPeerHooks::now)
    std::function<void()> changed;            // present a frame + automation refresh after a UIA action
    // The module's own reactions to host notifications (after the core's
    // own handling): an appearance event (the Context has already applied it:
    // the overlay owner is registered first) and host events (displays,
    // utility completions, ...). hostEvent's result is ModuleOwner::hostEvent's.
    std::function<void(const app::ModuleAppearance&, double)> appearance;
    std::function<bool(const app::HostEvent&, double)> hostEvent;
    // The module's own private window messages (picker, icon worker, drop
    // and utility notices the File Shelf posts to the HUD HWND), asked before
    // the text fields'; true when handled (ModuleOwner::message).
    std::function<bool(const app::NativeMessage&, double)> message;
    // The module's platform services beside its canvases that keep running
    // while none shows (Work Mode's countdown, Calendar's reminders): a wake
    // time and its deadline whatever the HUD's visibility (deadline's result:
    // something visible changed), after each frame's update, after the close
    // began, and the quit / session-end drain (ModuleRouting::flush; true
    // when nothing is left).
    std::function<std::optional<double>(double)> nextWakeTime;
    std::function<bool(double)> deadline;
    std::function<void(const app::ModuleFrame&)> updated;
    std::function<void(double)> closing;
    std::function<bool(double)> flush;
    // Memory the module's services keep resident beside the scenes
    // (ModuleOwner::residency: the minigame's VM heap); empty reports nothing.
    std::function<app::ModuleResidency()> residency;
};
// Raster density of design surfaces (LIBRARY-DESIGN.md R11, Q8): physical
// pixels per box px at the box centre (core::source::planeScale of boxWorld,
// logical client points, times the DPI scale), clamped 1...4 and quantized to
// .5; a value within .35 of `current` keeps it, so the parallax tilt never
// flips a step (each step re-renders every built canvas). The overlay owner
// feeds it to Context::update; owners raster at Context::boxScale().
double designDensity(const core::Matrix4& boxWorld, double dpiScale, double current);
class DesignOwnerCore final {
public:
    DesignOwnerCore(native::LayerRasterizer&, modules::design::Context&, DesignOwnerOptions);
    ~DesignOwnerCore();
    DesignOwnerCore(const DesignOwnerCore&) = delete;
    DesignOwnerCore& operator=(const DesignOwnerCore&) = delete;
    // The canvases this owner presents (one per HUD module).
    void add(std::unique_ptr<modules::design::Canvas>);
    modules::design::Canvas* canvas(core::Module) const noexcept;
    const DesignOwnerOptions& options() const noexcept;

    // --- The ModuleOwner contract, forwarded 1:1 by DesignModuleOwner ----------
    void resize(const app::ClientMetrics&);
    void setAppearance(const app::ModuleAppearance&, double);
    void setOverlayVisible(bool, double);
    void overlayClosing(double);
    void update(const app::ModuleFrame&);
    void prewarm(const app::ModuleFrame&);
    bool requiresFrames(double) const;
    std::optional<double> nextWakeTime(double) const;
    bool deadline(double);
    void upload(native::Renderer&);
    std::span<const native::LayerCompositionEntry> entries(core::Module);
    void collected(native::Renderer&);
    void release(native::Renderer&);
    bool covers(core::Point) const;
    bool pointerLocked() const;
    bool capturesPointer() const;
    bool capturesWheel() const;
    bool capturesKeys() const;
    bool suppressesKeyFilters() const;
    bool pointer(const app::PointerEvent&, double);
    bool wheel(const app::WheelEvent&, double);
    bool key(const app::KeyEvent&, double);
    bool key(const app::KeyEvent&, std::uint32_t nativeModifiers, double); // explicit modifiers (tests)
    bool filterKey(const app::NativeMessage&);
    bool message(const app::NativeMessage&, double);
    void focus(bool, double);
    void cancelInteraction(double);
    bool finishEditing(double);                // commits an editing field before close
    bool flush(double);                        // DesignOwnerOptions::flush
    bool hostEvent(const app::HostEvent&, double);
    std::optional<app::ModuleCursor> cursorAt(core::Point) const;
    bool automationRefresh(IRawElementProviderFragmentRoot*);
    std::size_t automationCount() const;
    long automationChild(std::size_t, IRawElementProviderFragment**) const;
    long automationFromPoint(double, double, IRawElementProviderFragment**) const;
    void automationDisconnect() noexcept;
    std::optional<core::Module> automationModule() const noexcept;
    app::ModuleResidency residency() const;

    // --- Owner services ---------------------------------------------------------
    std::optional<core::Module> inputModule() const noexcept;   // the canvas accepting input now
    std::optional<core::Rect> clientRect(core::Rect box) const; // box px -> logical client (bounds of the quad)
    std::optional<core::Point> boxPoint(core::Point logicalClient) const;
    // The retained scene of one presented canvas (resident pixels, parks,
    // generations; hidden coverage and owner tests). Empty for another module.
    std::optional<native::NativeDesignSceneStats> sceneStats(core::Module) const;
    // Owner-drawn media inside a canvas's scene (NativeDesignScene::setInsertion)
    // and this frame's design pose of that canvas (boxWorld with the switch
    // motion, module opacity, owner masks; nullopt while it is not drawn), so
    // a module owner wrapping this core (Media Assembly) poses its records
    // after update() exactly as the scene places its surfaces.
    void setInsertion(core::Module, const modules::design::Layer* after, std::optional<native::LayerCompositionEntry>);
    std::optional<native::NativeDesignScenePose> pose(core::Module) const;
    // The context's one editor host follows this core: a module owner that
    // drives its canvas directly (service completions, imports: Archive) calls
    // it first, as every core entry point does.
    void bindEditorHost();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// The generic ModuleOwner over a DesignOwnerCore: modules register this from
// their factory (built-in: app/application_modules.cpp; areas: their
// cmake/modules fragment) instead of writing an adapter.
class DesignModuleOwner final : public app::ModuleOwner {
public:
    explicit DesignModuleOwner(std::unique_ptr<DesignOwnerCore>);
    ~DesignModuleOwner() override;
    DesignOwnerCore& core() noexcept { return *core_; }
    std::string_view name() const noexcept override;
    app::ModuleRouting routing() const noexcept override;
    bool presents(core::Module) const noexcept override;
    void resize(const app::ClientMetrics&) override;
    void setAppearance(const app::ModuleAppearance&, double) override;
    void setOverlayVisible(bool, double) override;
    void overlayClosing(double) override;
    void update(const app::ModuleFrame&) override;
    void prewarm(const app::ModuleFrame&) override;
    bool requiresFrames(double) const override;
    std::optional<double> nextWakeTime(double) const override;
    bool deadline(double) override;
    void upload(native::Renderer&) override;
    std::span<const native::LayerCompositionEntry> entries(core::Module) override;
    void collected(native::Renderer&) override;
    void release(native::Renderer&) override;
    bool covers(core::Point) const override;
    bool pointerLocked() const override;
    bool capturesPointer() const override;
    bool capturesWheel() const override;
    bool capturesKeys() const override;
    bool suppressesKeyFilters() const override;
    bool pointer(const app::PointerEvent&, double) override;
    bool wheel(const app::WheelEvent&, double) override;
    bool key(const app::KeyEvent&, double) override;
    bool filterKey(const app::NativeMessage&) override;
    bool message(const app::NativeMessage&, double) override;
    void focus(bool, double) override;
    void cancelInteraction(double) override;
    bool finishEditing(double) override;
    bool flush(double) override;
    bool hostEvent(const app::HostEvent&, double) override;
    std::optional<app::ModuleCursor> cursorAt(core::Point) const override;
    bool automationRefresh(IRawElementProviderFragmentRoot*) override;
    std::size_t automationCount() const override;
    long automationChild(std::size_t, IRawElementProviderFragment**) const override;
    long automationFromPoint(double, double, IRawElementProviderFragment**) const override;
    void automationDisconnect() noexcept override;
    std::optional<core::Module> automationModule() const noexcept override;
    app::ModuleResidency residency() const override;
private:
    std::unique_ptr<DesignOwnerCore> core_;
};
} // namespace endfield::tools
#endif
