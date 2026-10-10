#pragma once
#include "modules/charge_overlay.hpp"
#include "native/charge_indicator_scene.hpp"
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

// The floating desktop charge alert (OverlayController's shared panel in its
// charging role): a separate top-most, no-activate, click-through tool window
// with its own DirectComposition target, sized by OverlayGeometry on the
// selected display's work area at the alert scale and that monitor's DPI.
// It is driven entirely by ChargeOverlayState on the host's frame clock: the
// window is shown only while the alert is visible and renders only while it
// animates; position editing makes it input-capable (drag, cancel/confirm
// buttons, Escape/Return) until the edit finishes.
//
// Platform note: a top-most window cannot cover an exclusive-fullscreen
// Direct3D swap chain (borderless fullscreen is covered), and the renderer
// API binds one device per HWND, so this panel owns a second small device.
namespace endfield::native {
struct ChargePanelOptions {
    std::filesystem::path shader;
    // Hidden tests: WARP + offscreen target and the window is never shown.
    bool hiddenForTests{};
};
#ifdef _WIN32
// Monitor work areas (physical pixels), per-monitor DPI, stable identity from
// the monitor device path, and the internal panel preferred like the Mac's
// built-in display (else the primary). Read-only; diagnostics may call it.
struct ChargeScreenScan {
    std::vector<modules::ChargeWorkArea> screens;
    std::optional<std::size_t> preferred;
    std::vector<std::wstring> identities; // device path (or GDI name) per screen
};
ChargeScreenScan scanChargeScreens();
// Extended window styles of the alert: click-through layered first.
std::uint32_t chargeAlertWindowStyle(bool clickThroughLayered) noexcept;

class ChargeIndicatorPanel final {
public:
    using PositionFinished = std::function<void(std::optional<modules::ChargePosition>)>;
    ChargeIndicatorPanel(LayerRasterizer&, ChargePanelOptions, PositionFinished = {});
    ~ChargeIndicatorPanel();
    ChargeIndicatorPanel(const ChargeIndicatorPanel&) = delete;
    ChargeIndicatorPanel& operator=(const ChargeIndicatorPanel&) = delete;
    // The presenter driven by PowerAlertCoordinator.
    modules::ChargeOverlayState& state() noexcept;
    const modules::ChargeOverlayState& state() const noexcept;
    // Resolved appearance (theme System already resolved by the host).
    void setAppearance(const modules::ChargeIndicatorAppearance&);
    // Shared Activity sample for a non-battery metric (state().telemetryDemand()).
    void setTelemetry(const std::optional<modules::ChargeTelemetry>&);
    // Re-enumerates monitors (work areas, per-monitor DPI, internal display).
    // The panel window handles WM_DISPLAYCHANGE/WM_DPICHANGED/work-area
    // changes itself; hosts may also call this on their own notifications.
    // Hidden test panels keep their injected screens.
    void refreshScreens();
    // Explicit screens for hidden tests.
    void setScreens(std::span<const modules::ChargeWorkArea>, std::optional<std::size_t> preferred);
    // Frame clock: runs due state deadlines, shows/hides/positions the
    // window and renders. No work happens while the alert is hidden.
    void advance(double time);
    bool requiresFrames(double time) const;
    std::optional<double> nextWakeTime() const;
    // Position editing (the host closed the HUD first). The editor takes
    // keyboard focus; finishing reports the normalized position (or none).
    bool beginPositionEditing(const modules::BatteryReading& previewValue, double time);
    // Input in virtual-desktop pixels; the window procedure forwards real
    // messages here. Tests inject the same calls on the hidden window.
    enum class PointerKind { down, move, up, cancel };
    bool pointer(PointerKind, core::Point screen, double time);
    bool key(unsigned virtualKey, double time);
    void* window() const noexcept;
    bool windowVisible() const noexcept;      // ShowWindow state (false in hidden tests)
    core::Rect windowRect() const noexcept;   // last positioned rectangle, pixels
    std::string accessibilityLabel() const;
    // Why the last presentation could not create its window/device (empty
    // when it could); retried on the next presentation, never per frame.
    std::string lastFailure() const;
    // Test readback of the last rendered frame (hidden tests only).
    Readback readbackForTests();
    // Releases the window, device and textures (terminate/suspend).
    void shutdown() noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
#endif
} // namespace endfield::native
