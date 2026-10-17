#pragma once

#include "core/motion.hpp"
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace endfield::app {

struct FrameWake {
    bool present{};
    std::optional<double> afterSeconds;
};

// Pure adapter around the unchanged source gate. Previewing its next two ticks
// lets an ambient-only window sleep through the tick that presents nothing.
// Thus both the first presentation and every subsequent presentation retain
// the source's times; finite motion never loses a tick. No clock is owned here.
class FrameWakePlan {
public:
    FrameWake refresh(core::FrameDemand demand, bool windowVisible);
    FrameWake wake();
    void cancel();
    bool canPresent() const { return canPresent_; }
    std::optional<double> interval() const { return nextInterval_; }
    // Source ticks the next wake covers: 1 when every tick presents (finite
    // motion), 2 for the alternate ambient tick; 0 without a wake.
    unsigned ticksPerWake() const { return sourceTicks_; }
    const core::FrameDemand& demand() const { return demand_; }
private:
    void planNext();
    core::FrameDemand demand_;
    core::FrameDemandGate gate_;
    std::optional<double> nextInterval_;
    unsigned sourceTicks_{};
    bool canPresent_{};
};

// The display refresh paces finite motion: the source gate's normal finite
// tick (60 Hz, every tick presents) becomes one frame per refresh of the
// display the window presents on - 60, 144 or 165 Hz - instead of a fixed
// 60 Hz. Ambient idle (every second 60 Hz tick: 30 Hz) and low-power motion
// (30 Hz) keep the source cadence, and nothing that does not animate gets a
// frame. Every frame samples the clock, so the time-based source curves are
// unchanged: only more of their samples are shown. Pure: no clock, timer or
// display query.
class DisplayPacing {
public:
    // The display's refresh period in seconds. None, or a period outside
    // 1/1000..1/20 s, keeps the source 60 Hz cadence.
    void setRefreshPeriod(std::optional<double> seconds);
    std::optional<double> refreshPeriod() const { return period_; }
    // Normal-rate finite motion (a transition or a finite animation, not low
    // power): the cadence that follows the display.
    static bool paced(const FrameWakePlan&);
    // Seconds from one frame to the next for this plan: the refresh period
    // for paced motion, the plan's own interval otherwise, none when idle.
    std::optional<double> interval(const FrameWakePlan&) const;
private:
    std::optional<double> period_;
};

// A single deferred presentation ticket. cancel() invalidates even a message
// already queued for an earlier opening; another opening gets a fresh ticket.
class FrameRequestBatch {
public:
    std::optional<std::uintptr_t> request(bool allowed);
    bool consume(std::uintptr_t ticket, bool allowed);
    void cancel();
    bool pending() const { return pending_; }
    // The pending request this ticket names (not consumed or cancelled).
    bool current(std::uintptr_t ticket) const { return pending_ && ticket == generation_; }
private:
    std::uintptr_t generation_{1};
    bool pending_{};
};

// Two reasons share one native waitable timer. A module deadline never advances
// animation cadence; taking an expired deadline consumes it before callbacks.
class WakeDeadlines {
public:
    struct Due { std::optional<double> frame; bool external{}; };
    void setFrame(std::optional<double>);
    void setExternal(std::optional<double>);
    std::optional<double> frame() const { return frame_; }
    std::optional<double> next() const;
    Due takeDue(double now);
    void clear() noexcept { frame_.reset(); external_.reset(); }
private:
    std::optional<double> frame_, external_;
};

struct ClientMetrics {
    std::uint32_t pixelWidth{}, pixelHeight{}, dpi{96};
    double scale{1}, width{}, height{}; // logical client coordinates, top-left
    bool operator==(const ClientMetrics&) const = default;
};
enum class PointerKind { move, down, up, doubleClick, leave, captureLost };
enum class PointerButton { none, left, right, middle, extra1, extra2 };
struct PointerSample { double x{}, y{}; }; // signed logical client coordinates
struct PointerEvent {
    PointerKind kind{};
    PointerButton button{};
    double x{}, y{}; // signed logical client coordinates; never screen coordinates
    std::uint32_t modifiers{}; // original Win32 MK_* bits
    // Moves only: the positions the pointer passed through since the
    // previous pointer event, oldest first, before (x, y). Windows keeps one
    // WM_MOUSEMOVE per read of the queue; while this window holds the
    // capture (a drag, such as a Projection stroke) the host recovers the
    // samples it coalesced from the system mouse history, so a stroke keeps
    // every sample of a 1000 Hz mouse while it renders once per frame.
    // Borrowed for the duration of the callback.
    std::span<const PointerSample> history;
};
// One entry of the system mouse history (GetMouseMovePointsEx): screen
// pixels and the input's message time in milliseconds.
struct MouseHistoryPoint {
    std::int32_t x{}, y{};
    std::uint32_t time{};
    bool operator==(const MouseHistoryPoint&) const = default;
};
// The entries of a newest-first mouse history that the pointer passed
// through after `previous` (the last position already delivered) and before
// `current` (this move's own entry), appended oldest first to `out`. The
// walk stops at `previous` or at the first entry older than it (message
// times wrap around); entries equal to `current` are skipped. Pure; returns
// the number appended.
std::size_t newerMouseHistory(std::span<const MouseHistoryPoint> newestFirst, MouseHistoryPoint current,
    MouseHistoryPoint previous, std::vector<MouseHistoryPoint>& out);
struct WheelEvent {
    double x{}, y{}, steps{}; // one wheel detent = 1, fractions retained
    bool horizontal{};
    std::uint32_t modifiers{};
    std::uint32_t linesPerStep{3}; // cached OS setting; UINT32_MAX is page scrolling
};
// WheelEvent::modifiers bit of the Ctrl key (MK_CONTROL).
inline constexpr std::uint32_t wheelControlKey = 0x0008;
// A precision touchpad's pinch reaches a window that registers no
// DirectManipulation viewport as Ctrl+wheel: WM_MOUSEWHEEL with MK_CONTROL in
// fractional detents (spreading the fingers turns the wheel forward). This is
// the source magnifyWithEvent magnification for it, 1 + amount scaling the
// content: one detent is the source mouse-wheel zoom step exp(.12)
// (WorldMapCanvas, MediaAssemblyCanvas), so Ctrl with a mouse wheel zooms by
// those steps. nullopt for any other wheel event. The Map, the Reader and
// Media Assembly take it like the Mac's pinch (SystemHUDView.magnify); every
// other owner keeps the wheel, as the Mac does for Control with the wheel.
inline std::optional<double> pinchMagnification(const WheelEvent& e) noexcept {
    if (!(e.modifiers & wheelControlKey) || e.horizontal || !std::isfinite(e.steps) || e.steps == 0) return std::nullopt;
    return std::exp(e.steps * .12) - 1;
}
enum class KeyKind { down, up, character, unicodeCharacter };
struct KeyEvent {
    KeyKind kind{};
    std::uint32_t value{}; // virtual key, UTF-16 code unit, or WM_UNICHAR scalar
    std::uint16_t repeat{};
    std::uint8_t scanCode{};
    bool system{}, extended{}, alt{}, previouslyDown{};
};
enum class SessionEnd { query, ending, cancelled };
// The cursor the owner wants at a client point (WM_SETCURSOR): the borrowed
// source cursor (setCursor), or the system I-beam over an open text editor.
enum class OverlayCursor : std::uint8_t { source, text };
struct NativeMessage {
    void* window{};
    std::uint32_t message{};
    std::uintptr_t wParam{};
    std::intptr_t lParam{};
};
struct OverlayCallbacks {
    std::function<void(double)> frame; // monotonic seconds; VisibilityClock is caller-owned
    std::function<bool(const PointerEvent&)> pointer;
    std::function<bool(const WheelEvent&)> wheel;
    std::function<bool(const KeyEvent&)> key;
    std::function<void(bool)> focus;
    std::function<void(const ClientMetrics&)> resize;
    std::function<void()> closeRequested; // caller may animate then hide; default hides
    // TSF must consume keys before TranslateMessage can enqueue WM_CHAR.
    // Only queued keys for this window, its children or root-owned native
    // panels reach this callback. Route by NativeMessage.window before TSF.
    std::function<bool(const NativeMessage&)> beforeKeyTranslation;
    // Module-owned WM_APP...0xBFFF, registered Shell notifications, WM_HOTKEY,
    // WM_POWERBROADCAST, WM_CLIPBOARDUPDATE, WM_TIMECHANGE and (when the owner
    // registered for it) WM_WTSSESSION_CHANGE; frame tickets stay reserved.
    // Returning no value leaves default handling in place. These routes add no
    // service registrations and work while the overlay is hidden.
    std::function<std::optional<std::intptr_t>(const NativeMessage&)> appMessage;
    std::function<void()> displayChanged; // notification only; no enumeration/poll
    std::function<void(double)> deadline; // one-shot, same monotonic clock as frame
    // App activation differs from keyboard focus moving into an owned file
    // dialog or editor. The owner applies its configurable close policy.
    std::function<void(bool)> applicationActive;
    // WM_QUERYENDSESSION (query) and WM_ENDSESSION (ending/cancelled) for
    // logoff, restart and Restart Manager. reason carries the ENDSESSION_*
    // lParam bits. The owner drains durable writes synchronously inside the
    // callback; the host always allows the session to end.
    std::function<void(SessionEnd, std::uint32_t reason)> sessionEnding;
    // WM_GETOBJECT (UI Automation/MSAA object request). A value is the
    // LRESULT to return (e.g. UiaReturnRawElementProvider); none falls back
    // to default handling. May arrive before the first frame or while hidden.
    std::function<std::optional<std::intptr_t>(const NativeMessage&)> getObject;
    // WM_SETCURSOR over this window's own client area while it owns the
    // cursor (visible, enabled, active, focused, foreground; no menu,
    // size/move loop or suspension): which cursor to show at that signed
    // logical client point. Called on every cursor update, so it must not
    // allocate or change state. Absent: always the source cursor.
    std::function<OverlayCursor(double x, double y)> cursorAt;
    // Low-latency presentation through the owner's swap-chain admission (its
    // renderer's DXGI frame latency waitable object; at most one frame
    // queued). presentationWaitable answers that object (a borrowed HANDLE)
    // or null when the owner presents without one; presentationHeld whether
    // the next frame already holds an admission; presentationAdmitted that
    // the pump's wait on the object succeeded (the admission now belongs to
    // the next frame). With them, a requested frame waits in the pump while
    // input keeps being dispatched, then runs the moment the swap chain
    // accepts a frame - sampling the newest input, presenting without
    // blocking or queueing - and display-paced finite motion asks for each
    // next frame at once, admitted once per refresh. An object that admits
    // nothing within 50 ms (an occluded or suspended target) runs the frame
    // anyway. Absent: frames run when posted, motion on the waitable timer.
    std::function<void*()> presentationWaitable;
    std::function<bool()> presentationHeld;
    std::function<void()> presentationAdmitted;
};
struct OverlayOptions {
    std::wstring title{L"EndfieldHUD"};
    int x{}, y{}; // screen pixels, supplied by caller
    std::uint32_t pixelWidth{1280}, pixelHeight{800};
    void* ownerWindow{}; // optional HWND; borrowed
    // The window covers one monitor (the HUD and Projection): a per-monitor
    // DPI change fits it to its monitor's pixel bounds instead of Windows'
    // suggested rectangle, which need not equal them for a covering popup.
    bool coverMonitor{};
    // Hidden benchmarks only (watch_session_preview --idle-trace, with an
    // offscreen renderer): the window is never shown, yet show() makes it
    // presented for frame scheduling - the same frame clock, waitable timer,
    // source gate and display pacing as a shown window - and hide() ends that.
    bool offscreenScheduling{};
    // Hidden benchmarks only: pace finite motion by this display refresh (Hz)
    // instead of the window's monitor, which a service session lacks. 0: the monitor.
    double refreshRate{};
    // Hidden tests only: stand-ins for the two steps of show() and hide()
    // that the user would see, so a test drives the real show/hide path -
    // the topmost band included, applied to the real never-shown window -
    // with no visible window and no focus change on the desktop.
    // testVisibility(shown, activate) replaces making the window visible (or
    // hidden) and answers whether it now counts as shown; testForeground()
    // replaces SetForegroundWindow and answers whether Windows granted it.
    // Empty in production.
    std::function<bool(bool shown, bool activate)> testVisibility;
    std::function<bool()> testForeground;
};
struct OverlayHostStats {
    std::uint64_t frameRequests{}, framePosts{}, frames{}, timerArms{}, timerWakes{},
        timerCancels{}, pointerMessages{}, cursorSets{}, cursorReleases{}, textCursorSets{};
    bool timerArmed{}, framePending{}, visible{}, focused{};
    std::uint64_t externalWakes{};
    // Presentation gate: frames that waited in the pump for an admission,
    // admissions the pump took, frames run after the 50 ms bound without
    // one, and whether a frame waits now.
    std::uint64_t presentationWaits{}, presentationAdmissions{}, presentationFallbacks{};
    bool presentationGated{};
    // Coalesced samples recovered from the mouse history for drag moves.
    std::uint64_t pointerHistorySamples{};
    // Refresh rate of the window's display (Hz, 0 unknown) and the frames
    // finite motion presented at that pace.
    double refreshRate{};
    std::uint64_t pacedFrames{};
    // Z-band and activation (show/hide): whether the window is in the
    // topmost band now (WS_EX_TOPMOST); activating shows that asked for the
    // foreground and those Windows' foreground lock refused; whether a shown
    // window still waits for its activation (the first click on it) and the
    // activations that ended such a wait.
    bool topmost{}, activationPending{};
    std::uint64_t foregroundRequests{}, foregroundRefusals{}, deferredActivations{};
};

// All methods and destruction belong to the creating UI thread. This is an
// HWND/message-loop owner only: no renderer, providers, hotkeys, global input,
// process DPI-policy changes, timer-resolution changes, workers, or polling.
// (The owner's presentation admission is a borrowed handle it waits on; only
// while a frame waits for it does a short USER timer let a nested modal loop
// admit that frame.)
// Callbacks may hide/destroy this host's HWND but must not delete the host itself.
// Windows handles are opaque in this header to keep the scheduling tests portable.
class OverlayHost final {
public:
    OverlayHost();
    ~OverlayHost();
    OverlayHost(const OverlayHost&) = delete;
    OverlayHost& operator=(const OverlayHost&) = delete;
    void create(const OverlayOptions&, OverlayCallbacks = {}); // always initially hidden
    void destroy(); // idempotent; release the renderer/DComp target before this
    void* hwnd() const noexcept;
    ClientMetrics metrics() const;
    // Shows the window in the topmost band, above every normal application
    // window however it was opened (Mac: the HUD panel's .statusBar level),
    // keeping its geometry and style; an explicit show also overrides a
    // launcher's STARTUPINFO SW_HIDE. Every call raises it to the top of that
    // band. activate also asks for the foreground (Mac: NSApp.activate
    // ignoringOtherApps). Windows' foreground lock refuses that to a process
    // that may not set the foreground window - a run Setup started without
    // passing that right on, or after the user's input went elsewhere: the
    // window then stays shown above the other windows, inactive, and the
    // first click on it activates it (WM_MOUSEACTIVATE answers MA_ACTIVATE).
    // No input is injected and no thread input is attached. show(false)
    // keeps keyboard focus and activation as they are.
    void show(bool activate = true);
    // Hides the window and leaves the topmost band: a hidden window is never
    // topmost.
    void hide();
    // Moves and sizes the window to these screen pixels (Projection on its
    // retained display) and changes nothing else: z-order, visibility and
    // activation stay as they are, so a hidden window never enters the
    // topmost band here (only show() does) and a shown one keeps its place in
    // it. false when Windows refuses the move or no window exists.
    bool place(int x, int y, int pixelWidth, int pixelHeight);
    void setFrameDemand(core::FrameDemand);
    // Caller aggregates its next media/save/countdown deadline. It shares the
    // existing waitable timer, survives hide, and is consumed before callback.
    // Cancel with nullopt. Stop/destroy cancel both animation and external work.
    void setDeadline(std::optional<double> monotonicSeconds);
    static double clockNow(); // use this epoch for frame/module deadlines
    void invalidate(); // many requests -> one deferred frame; hidden requests do nothing
    // Borrowed HCURSOR; never destroyed, hidden, or periodically reasserted.
    // Set only in WM_SETCURSOR for this active window's own client hit. Child
    // controls/nonclient regions/default IME routing remain native. Where
    // OverlayCallbacks::cursorAt answers text, the shared system I-beam
    // (IDC_IBEAM) is shown instead through the same path.
    void setCursor(void* cursor);
    // Re-answers WM_SETCURSOR at the current pointer position through that
    // same path, only while this window already owns the cursor (e.g. after
    // a click opened or closed an editor under a still pointer). Event-driven;
    // never called from a timer or frame.
    void refreshCursor();
    void capturePointer(bool capture); // own HWND only, released on hide/focus loss
    void suspendCursor(bool suspended); // bracket native dialogs/OLE drag sessions
    // false means stop/destroy/WM_QUIT. INFINITE is the ordinary idle wait.
    // The pump dispatches this thread's messages, as required for child/IME windows.
    bool pumpOnce(std::uint32_t timeoutMilliseconds = 0xffffffffu);
    int run();
    void requestStop(); // UI thread only; cancels pending work, no PostQuitMessage
    OverlayHostStats stats() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace endfield::app
