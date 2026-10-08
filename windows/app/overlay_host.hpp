#pragma once

#include "core/motion.hpp"
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>

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
private:
    void planNext();
    core::FrameDemand demand_;
    core::FrameDemandGate gate_;
    std::optional<double> nextInterval_;
    unsigned sourceTicks_{};
    bool canPresent_{};
};

// A single deferred presentation ticket. cancel() invalidates even a message
// already queued for an earlier opening; another opening gets a fresh ticket.
class FrameRequestBatch {
public:
    std::optional<std::uintptr_t> request(bool allowed);
    bool consume(std::uintptr_t ticket, bool allowed);
    void cancel();
    bool pending() const { return pending_; }
private:
    std::uintptr_t generation_{1};
    bool pending_{};
};

struct ClientMetrics {
    std::uint32_t pixelWidth{}, pixelHeight{}, dpi{96};
    double scale{1}, width{}, height{}; // logical client coordinates, top-left
    bool operator==(const ClientMetrics&) const = default;
};
enum class PointerKind { move, down, up, doubleClick, leave, captureLost };
enum class PointerButton { none, left, right, middle, extra1, extra2 };
struct PointerEvent {
    PointerKind kind{};
    PointerButton button{};
    double x{}, y{}; // signed logical client coordinates; never screen coordinates
    std::uint32_t modifiers{}; // original Win32 MK_* bits
};
struct WheelEvent {
    double x{}, y{}, steps{}; // one wheel detent = 1, fractions retained
    bool horizontal{};
    std::uint32_t modifiers{};
};
enum class KeyKind { down, up, character, unicodeCharacter };
struct KeyEvent {
    KeyKind kind{};
    std::uint32_t value{}; // virtual key, UTF-16 code unit, or WM_UNICHAR scalar
    std::uint16_t repeat{};
    std::uint8_t scanCode{};
    bool system{}, extended{}, alt{}, previouslyDown{};
};
struct OverlayCallbacks {
    std::function<void(double)> frame; // monotonic seconds; VisibilityClock is caller-owned
    std::function<bool(const PointerEvent&)> pointer;
    std::function<bool(const WheelEvent&)> wheel;
    std::function<bool(const KeyEvent&)> key;
    std::function<void(bool)> focus;
    std::function<void(const ClientMetrics&)> resize;
    std::function<void()> closeRequested; // caller may animate then hide; default hides
};
struct OverlayOptions {
    std::wstring title{L"EndfieldHUD"};
    int x{}, y{}; // screen pixels, supplied by caller
    std::uint32_t pixelWidth{1280}, pixelHeight{800};
    void* ownerWindow{}; // optional HWND; borrowed
};
struct OverlayHostStats {
    std::uint64_t frameRequests{}, framePosts{}, frames{}, timerArms{}, timerWakes{},
        timerCancels{}, pointerMessages{}, cursorSets{}, cursorReleases{};
    bool timerArmed{}, framePending{}, visible{}, focused{};
};

// All methods and destruction belong to the creating UI thread. This is an
// HWND/message-loop owner only: no renderer, providers, hotkeys, global input,
// process DPI-policy changes, timer-resolution changes, workers, or polling.
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
    void show(bool activate = true);
    void hide();
    void setFrameDemand(core::FrameDemand);
    void invalidate(); // many requests -> one deferred frame; hidden requests do nothing
    // Borrowed HCURSOR; never destroyed, hidden, or periodically reasserted.
    // Set only in WM_SETCURSOR for this active window's own client hit. Child
    // controls/nonclient regions/default IME routing remain native.
    void setCursor(void* cursor);
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
