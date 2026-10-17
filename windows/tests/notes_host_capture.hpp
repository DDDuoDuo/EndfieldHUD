#pragma once
// The HUD host's pointer capture around one owner, for hidden owner suites
// (Notes drawing, Projection drawing).
//
// A hidden OverlayHost (app/overlay_host.cpp, the production window owner)
// receives real mouse messages sent to its HWND and translates them into
// PointerEvents, and every WM_CAPTURECHANGED into PointerKind::captureLost,
// exactly as in EndfieldHUD.exe. After the owner answered, the harness takes
// or releases the capture the way the Application does
// (app/application_events.cpp):
//   pressRelease   routePointer with the default PointerPolicy: a consumed
//                  press (down/double click, any button) captures, a consumed
//                  release releases (Notes and every registry owner);
//   pointerLocked  the Projection route: capturePointer(owner.pointerLocked())
//                  after every event.
// OverlayHost::capturePointer only captures a VISIBLE window; these windows
// stay hidden, so the harness performs the same Win32 calls itself:
//   always         SetCapture on every capture request (the host at
//                  checkpoint 128);
//   whenNotHeld    SetCapture only when the window does not already hold the
//                  capture (the requested host change).
// Windows answers SetCapture on the window that already holds the capture
// with WM_CAPTURECHANGED naming that same window (selfRecaptureNotifies), so
// with `always` a repeated capture request reaches the owner as a capture
// loss in the middle of its gesture.
//
// Synthetic messages only: the window is never shown or activated, and no
// global input, cursor or system setting is touched.
#ifdef _WIN32
#include "app/overlay_host.hpp"
#include <windows.h>
#include <cmath>
#include <functional>
#include <stdexcept>
#include <utility>

namespace endfield::tests {
enum class HostCaptureRoute { pressRelease, pointerLocked };
enum class HostCaptureCall { always, whenNotHeld };

class HostCaptureHarness final {
public:
    using Owner = std::function<bool(const app::PointerEvent&)>;
    using Locked = std::function<bool()>;
    HostCaptureHarness(HostCaptureRoute route, HostCaptureCall call, Owner owner, Locked locked = {})
        : route_(route), call_(call), owner_(std::move(owner)), locked_(std::move(locked)) {
        if (!owner_ || (route_ == HostCaptureRoute::pointerLocked && !locked_))
            throw std::invalid_argument("Host capture harness needs its owner route");
        app::OverlayCallbacks callbacks;
        callbacks.pointer = [this](const app::PointerEvent& e) { return routeEvent(e); };
        app::OverlayOptions options;
        options.title = L"Hidden synthetic HUD capture";
        host_.create(options, std::move(callbacks));
        window_ = static_cast<HWND>(host_.hwnd());
        if (!window_ || IsWindowVisible(window_)) throw std::runtime_error("Host capture harness window must stay hidden");
    }
    ~HostCaptureHarness() {
        active_ = false;
        if (window_ && GetCapture() == window_) ReleaseCapture();
        try { host_.destroy(); } catch (...) {}
    }
    HostCaptureHarness(const HostCaptureHarness&) = delete;
    HostCaptureHarness& operator=(const HostCaptureHarness&) = delete;

    // Sends one real mouse message at a logical client point (the host divides
    // by its own DPI scale) and returns whether the owner consumed it. Any
    // owner exception the host caught inside the callback is rethrown here.
    bool send(UINT message, WPARAM keys, double x, double y) {
        const double scale = host_.metrics().scale;
        const auto px = static_cast<short>(std::lround(x * scale)), py = static_cast<short>(std::lround(y * scale));
        handled_ = false;
        SendMessageW(window_, message, keys, MAKELPARAM(static_cast<WORD>(px), static_cast<WORD>(py)));
        host_.invalidate(); // rethrows a callback failure; hidden: presents nothing
        return handled_;
    }
    bool holdsCapture() const noexcept { return window_ && GetCapture() == window_; }
    unsigned captureLosses() const noexcept { return losses_; }
    HWND window() const noexcept { return window_; }

    // Windows' answer to SetCapture on a window that already holds the
    // capture, on a private hidden window: true when it sent that window
    // WM_CAPTURECHANGED naming the window itself while still holding it.
    static bool selfRecaptureNotifies() {
        struct Probe {
            static LRESULT CALLBACK procedure(HWND h, UINT m, WPARAM w, LPARAM l) {
                if (m == WM_CAPTURECHANGED) {
                    auto* self = reinterpret_cast<Probe*>(GetWindowLongPtrW(h, GWLP_USERDATA));
                    if (self && reinterpret_cast<HWND>(l) == h && GetCapture() == h) ++self->selfNotices;
                }
                return DefWindowProcW(h, m, w, l);
            }
            unsigned selfNotices{};
        } probe;
        WNDCLASSW type{}; type.lpfnWndProc = Probe::procedure; type.hInstance = GetModuleHandleW(nullptr);
        type.lpszClassName = L"EndfieldHostCaptureProbe";
        const ATOM atom = RegisterClassW(&type);
        if (!atom) throw std::runtime_error("Register capture probe");
        const HWND h = CreateWindowExW(WS_EX_TOOLWINDOW, type.lpszClassName, L"Hidden capture probe", WS_POPUP,
            -32000, -32000, 8, 8, nullptr, nullptr, type.hInstance, nullptr);
        if (!h) { UnregisterClassW(type.lpszClassName, type.hInstance); throw std::runtime_error("Create capture probe"); }
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&probe));
        SetCapture(h);
        const bool held = GetCapture() == h;
        SetCapture(h);
        const bool stillHeld = GetCapture() == h;
        ReleaseCapture();
        SetWindowLongPtrW(h, GWLP_USERDATA, 0);
        DestroyWindow(h);
        UnregisterClassW(type.lpszClassName, type.hInstance);
        if (!held || !stillHeld) throw std::runtime_error("Hidden capture probe could not hold the capture");
        return probe.selfNotices == 1;
    }

private:
    bool routeEvent(const app::PointerEvent& e) {
        if (!active_) return false;
        if (e.kind == app::PointerKind::captureLost) ++losses_;
        const bool handled = owner_(e);
        if (route_ == HostCaptureRoute::pressRelease) {
            if (handled && (e.kind == app::PointerKind::down || e.kind == app::PointerKind::doubleClick)) capture(true);
            if (handled && e.kind == app::PointerKind::up) capture(false);
        } else capture(locked_());
        if (e.kind != app::PointerKind::captureLost) handled_ = handled;
        return handled;
    }
    void capture(bool value) {
        if (!value) { if (GetCapture() == window_) ReleaseCapture(); return; }
        if (call_ == HostCaptureCall::always || GetCapture() != window_) SetCapture(window_);
    }
    app::OverlayHost host_;
    HWND window_{};
    HostCaptureRoute route_;
    HostCaptureCall call_;
    Owner owner_;
    Locked locked_;
    unsigned losses_{};
    bool handled_{}, active_{true};
};
} // namespace endfield::tests
#endif
