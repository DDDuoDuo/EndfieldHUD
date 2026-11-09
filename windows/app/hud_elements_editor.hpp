#pragma once
// Display > HUD elements > Edit position and size (Mac SystemHUDView's
// overlay placement, Sources/SystemHUDView.swift at 2c047e0: beginOverlay-
// Placement, installOverlayEditor, layoutOverlayEditor, overlayPointerDown /
// Dragged / Up, finishOverlayPlacement, designCursor).
//
// The HUD shows one place-and-confirm strip (modules::design::PlacementEditor
// at full size) with Title size, Sanity size and Alerts size (50 to 200 %),
// Cancel and Confirm; pressing and dragging the title, the Sanity gauge or the
// alert tags on the HUD moves it. Corner marks (ArchiveTile::bracketPath at
// inset 0 around each drawn frame grown by 6, in the accent ink) show every
// element's slot. The strip sits 8 points beside the last element dragged
// (at first beside all three while they sit together, else beside the
// title), below it or above it near the bottom, and holds still while one of
// its sliders or an element has the pointer. Confirm (or Return) saves every
// placement as drawn, once and only if something changed; Cancel, Esc, the
// closed HUD once hidden or its input switching off drops the draft. A theme flip
// rebuilds the strip from the draft without its entrance.
//
// ModuleOwner role: registered right after the shared design overlay. While
// placing it captures the pointer, the wheel and the keys (capture routes 0,
// ahead of every module), locks the parallax and answers the cursor for every
// point (closed hand while an element is dragged, the normal cursor over the
// strip, the open hand over an element's hit frame, the normal cursor
// elsewhere). The strip and the marks are its topEntries (the HUD's top
// layer): the marks on the core plane, the strip on the content box. Its
// sliders and pills are its UI Automation items. No timer, thread or window.
// The HUD close ends the editor's press and takes no input from its start;
// the strip, the marks and the draft stay drawn and fade with the close, and
// the hidden HUD (overlayConcealed, overlayRetracted) drops the draft.
#ifdef _WIN32
#include "app/module_owner.hpp"
#include "core/hud_overlay_geometry.hpp"
#include "modules/hud_design_overlay.hpp"
#include "modules/hud_design_page.hpp"
#include "native/hud_design_scene.hpp"
#include <array>
#include <functional>
#include <memory>
#include <optional>

namespace endfield::app {
struct HudElementsEditorOptions {
    native::LayerRasterOptions raster;
    void* window{};                                  // the HUD HWND (UIA screen rects)
    std::function<double()> now;
    std::function<void()> changed;                   // present a frame (+ UIA refresh)
    // overlayNatural: an element's size at 100 % in core-plane points for
    // `layout` (the title's text and size, the alerts shown now).
    std::function<core::OverlaySize(core::OverlayGeometry::Element, const ehud::data::OverlayLayout&)> natural;
    // The shown layout moved (the draft changed, or placing began or ended).
    std::function<void()> layoutChanged;
    // Confirm with a draft that differs from `saved`: one settings write.
    std::function<void(const ehud::data::OverlayLayout&)> save;
};
class HudElementsEditor final : public ModuleOwner {
public:
    using Element = core::OverlayGeometry::Element;
    HudElementsEditor(native::LayerRasterizer&, modules::design::Context&, HudElementsEditorOptions);
    ~HudElementsEditor() override;
    // beginOverlayPlacement with the saved layout (the host checked that the
    // HUD takes module input and nothing else is placed or confirmed).
    bool begin(const ehud::data::OverlayLayout& saved, double time);
    // finishOverlayPlacement(confirm: false): Cancel, Esc, the HUD closing.
    void cancel(double time);
    bool placing() const noexcept;
    const ehud::data::OverlayLayout* draft() const noexcept;
    // Verification: the strip, the drawn frames (core-plane points, in
    // OverlayGeometry::elements order), a core-plane or strip-local point in
    // logical client coordinates through the last posed frame.
    const modules::design::PlacementEditor* strip() const noexcept;
    std::array<core::Rect, 3> frames() const;
    std::optional<core::Point> clientPoint(core::Point corePlane) const;
    std::optional<core::Point> stripClientPoint(core::Point stripLocal) const;
    std::optional<Element> dragged() const noexcept;

    std::string_view name() const noexcept override { return "hudElements"; }
    ModuleRouting routing() const noexcept override;
    void resize(const ClientMetrics&) override;
    void setAppearance(const ModuleAppearance&, double) override;
    void setOverlayVisible(bool, double) override;
    void overlayClosing(double) override;
    void update(const ModuleFrame&) override;
    bool requiresFrames(double) const override;
    void upload(native::Renderer&) override;
    std::span<const native::LayerCompositionEntry> topEntries() override;
    void collected(native::Renderer&) override;
    void release(native::Renderer&) override;
    bool covers(core::Point) const override;
    bool pointerLocked() const override;
    bool capturesPointer() const override;
    bool capturesWheel() const override;
    bool capturesKeys() const override;
    bool suppressesKeyFilters() const override;
    bool pointer(const PointerEvent&, double) override;
    bool wheel(const WheelEvent&, double) override;
    bool key(const KeyEvent&, double) override;
    void cancelInteraction(double) override;
    bool hostEvent(const HostEvent&, double) override;
    std::optional<ModuleCursor> cursorAt(core::Point) const override;
    bool automationRefresh(IRawElementProviderFragmentRoot*) override;
    std::size_t automationCount() const override;
    long automationChild(std::size_t, IRawElementProviderFragment**) const override;
    long automationFromPoint(double, double, IRawElementProviderFragment**) const override;
    void automationDisconnect() noexcept override;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace endfield::app
#endif
