#include "app/overlay_host.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace endfield::app {
void FrameWakePlan::planNext() {
    sourceTicks_ = 0;
    nextInterval_.reset();
    auto preview = gate_;
    const auto first = preview.tick(demand_);
    if (!first.timerInterval) return;
    sourceTicks_ = 1;
    if (!first.present) {
        const auto second = preview.tick(demand_);
        if (!second.present || second.timerInterval != first.timerInterval)
            throw std::logic_error("Source gate no longer has a bounded alternate ambient tick");
        sourceTicks_ = 2;
    }
    nextInterval_ = *first.timerInterval * sourceTicks_;
}
FrameWake FrameWakePlan::refresh(core::FrameDemand demand, bool windowVisible) {
    demand_ = demand;
    demand_.presented = demand.presented && windowVisible;
    const auto result = gate_.refresh(demand_);
    canPresent_ = result.present;
    planNext();
    return {result.present, nextInterval_};
}
FrameWake FrameWakePlan::wake() {
    if (!nextInterval_) return {};
    bool present = false;
    for (unsigned tick = 0; tick < sourceTicks_; ++tick)
        present = gate_.tick(demand_).present || present;
    planNext();
    return {present, nextInterval_};
}
void FrameWakePlan::cancel() {
    demand_ = {};
    gate_.reset();
    nextInterval_.reset();
    sourceTicks_ = 0;
    canPresent_ = false;
}
void DisplayPacing::setRefreshPeriod(std::optional<double> seconds) {
    period_ = seconds && std::isfinite(*seconds) && *seconds >= 1.0 / 1000 && *seconds <= 1.0 / 20 ? seconds : std::nullopt;
}
bool DisplayPacing::paced(const FrameWakePlan& plan) {
    // FrameDemandGate's finite motion: a transition phase or a finite
    // animation; its alternate ambient wakes are not paced.
    const auto& demand = plan.demand();
    return plan.interval() && !demand.lowPower &&
        (demand.phase != core::VisibilityPhase::visible || demand.finiteAnimation);
}
std::optional<double> DisplayPacing::interval(const FrameWakePlan& plan) const {
    if (paced(plan) && period_) return period_;
    return plan.interval();
}
std::size_t newerMouseHistory(std::span<const MouseHistoryPoint> newestFirst, MouseHistoryPoint current,
    MouseHistoryPoint previous, std::vector<MouseHistoryPoint>& out) {
    const auto first = out.size();
    // Message times are GetTickCount milliseconds: compare across wraparound.
    const auto older = [](std::uint32_t a, std::uint32_t b) { return static_cast<std::int32_t>(a - b) < 0; };
    for (const auto& point : newestFirst) {
        if (point == previous || older(point.time, previous.time)) break;
        if (point == current || older(current.time, point.time)) continue;
        out.push_back(point);
    }
    std::reverse(out.begin() + static_cast<std::ptrdiff_t>(first), out.end());
    return out.size() - first;
}
std::optional<std::uintptr_t> FrameRequestBatch::request(bool allowed) {
    if (!allowed || pending_) return {};
    if (++generation_ == 0) ++generation_;
    pending_ = true;
    return generation_;
}
bool FrameRequestBatch::consume(std::uintptr_t ticket, bool allowed) {
    if (!pending_ || ticket != generation_) return false;
    pending_ = false;
    return allowed;
}
void FrameRequestBatch::cancel() {
    pending_ = false;
    if (++generation_ == 0) ++generation_;
}
namespace {
void validDeadline(std::optional<double> value) {
    if (value && (!std::isfinite(*value) || *value < 0))
        throw std::invalid_argument("Wake deadline requires finite nonnegative time");
}
}
void WakeDeadlines::setFrame(std::optional<double> value) { validDeadline(value); frame_=value; }
void WakeDeadlines::setExternal(std::optional<double> value) { validDeadline(value); external_=value; }
std::optional<double> WakeDeadlines::next() const {
    return frame_ && external_ ? std::min(*frame_,*external_) : frame_ ? frame_ : external_;
}
WakeDeadlines::Due WakeDeadlines::takeDue(double now) {
    validDeadline(now); Due result;
    if (frame_ && *frame_ <= now) { result.frame=frame_; frame_.reset(); }
    if (external_ && *external_ <= now) { result.external=true; external_.reset(); }
    return result;
}
} // namespace endfield::app

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#include <array>
#include <exception>
#include <cstdio>
#include <system_error>

namespace endfield::app {
namespace {
constexpr UINT frameMessage = WM_APP + 0x31d;
// While a frame waits for the presentation gate, a nested modal loop (a file
// dialog, a drag, a menu) dispatches messages instead of pumpOnce: this
// short-lived timer lets it admit (or after 50 ms run) the waiting frame.
constexpr UINT_PTR gateTimer = 0x45484744;
constexpr double gateBound = .05;
constexpr wchar_t windowClass[] = L"EndfieldSourceOverlayHost.v1";
[[noreturn]] void fail(const char* operation) {
    throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), operation);
}
double monotonicSeconds() {
    static const double frequency = [] {
        LARGE_INTEGER value{};
        if (!QueryPerformanceFrequency(&value)) fail("QueryPerformanceFrequency");
        return static_cast<double>(value.QuadPart);
    }();
    LARGE_INTEGER value{};
    if (!QueryPerformanceCounter(&value)) fail("QueryPerformanceCounter");
    return static_cast<double>(value.QuadPart) / frequency;
}
bool sameDemand(const core::FrameDemand& a, const core::FrameDemand& b) {
    return a.phase == b.phase && a.presented == b.presented && a.onScreen == b.onScreen &&
        a.canAdvanceTransition == b.canAdvanceTransition && a.pendingOpening == b.pendingOpening &&
        a.finiteAnimation == b.finiteAnimation && a.ambientEnabled == b.ambientEnabled &&
        a.reduceMotion == b.reduceMotion && a.lowPower == b.lowPower;
}
PointerButton buttonFor(UINT message, WPARAM value) {
    switch (message) {
    case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_LBUTTONDBLCLK: return PointerButton::left;
    case WM_RBUTTONDOWN: case WM_RBUTTONUP: case WM_RBUTTONDBLCLK: return PointerButton::right;
    case WM_MBUTTONDOWN: case WM_MBUTTONUP: case WM_MBUTTONDBLCLK: return PointerButton::middle;
    case WM_XBUTTONDOWN: case WM_XBUTTONUP: case WM_XBUTTONDBLCLK:
        return GET_XBUTTON_WPARAM(value) == XBUTTON1 ? PointerButton::extra1 : PointerButton::extra2;
    default: return PointerButton::none;
    }
}
PointerKind kindFor(UINT message) {
    switch (message) {
    case WM_LBUTTONDOWN: case WM_RBUTTONDOWN: case WM_MBUTTONDOWN: case WM_XBUTTONDOWN:
        return PointerKind::down;
    case WM_LBUTTONUP: case WM_RBUTTONUP: case WM_MBUTTONUP: case WM_XBUTTONUP:
        return PointerKind::up;
    case WM_LBUTTONDBLCLK: case WM_RBUTTONDBLCLK: case WM_MBUTTONDBLCLK: case WM_XBUTTONDBLCLK:
        return PointerKind::doubleClick;
    default: return PointerKind::move;
    }
}
} // namespace

struct OverlayHost::Impl {
    HWND window{};
    HANDLE timer{};
    DWORD thread{GetCurrentThreadId()};
    std::shared_ptr<OverlayCallbacks> callbacks;
    core::FrameDemand demand;
    FrameWakePlan plan;
    WakeDeadlines deadlines;
    FrameRequestBatch frames;
    ClientMetrics client;
    OverlayHostStats counts;
    std::exception_ptr callbackError;
    HCURSOR cursor{};
    // The shared system I-beam (never destroyed) and the cursor this host
    // last installed (either one), so a release never undoes another's.
    HCURSOR textCursor{LoadCursorW(nullptr, IDC_IBEAM)}, installed{};
    // The executable's drawn grab hands (shared resources, never destroyed;
    // null without them), at the system cursor size.
    static HCURSOR resourceCursor(int id) {
        return static_cast<HCURSOR>(LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(id), IMAGE_CURSOR, 0, 0, LR_DEFAULTSIZE | LR_SHARED));
    }
    HCURSOR openHandCursor{resourceCursor(overlayOpenHandCursorResource)}, closedHandCursor{resourceCursor(overlayClosedHandCursorResource)};
    double timerDeadline{}, lastX{}, lastY{};
    // Display pacing, the frame waiting for the swap chain's admission and
    // the interval the frame deadline was planned with.
    DisplayPacing pacing;
    std::optional<std::uintptr_t> gated;
    double gatedSince{};
    std::optional<double> frameInterval;
    // The last pointer position delivered (screen pixels, message time) and
    // the coalesced samples recovered for the current drag move.
    std::optional<MouseHistoryPoint> lastMouse;
    std::vector<MouseHistoryPoint> historyPoints;
    std::vector<PointerSample> historySamples;
    bool ready{}, visible{}, minimized{}, focused{}, stopped{}, armed{},
        mouseTracking{}, cursorSuspended{}, cursorOwned{}, appActive{true};
    // pumpOnce itself is dispatching a frame message (not a nested modal
    // loop, which never waits on the presentation gate).
    bool ownFrameDispatch{};
    unsigned menuDepth{}, sizeMoveDepth{};
    bool coverMonitor{};
    // OverlayOptions::offscreenScheduling / refreshRate (hidden benchmarks).
    bool offscreen{};
    double fixedRefresh{};
    // OverlayOptions::testVisibility / testForeground / testBand (hidden
    // tests); bandState is the stand-in band's answer.
    std::function<bool(bool, bool)> testVisibility;
    std::function<bool()> testForeground;
    std::function<bool(bool)> testBand;
    bool bandState{};
    // An activating show the foreground lock refused, until the window is
    // activated (the first click on it, or a later granted request).
    bool activationPending{};
    // A shown window that Windows kept out of the topmost band (see
    // enterBand), until a later attempt enters it.
    bool bandPending{};
    // Whether the window presents now: natively visible, or presented for
    // scheduling by an offscreen benchmark (or a hidden test's stand-in)
    // that never shows it.
    bool presented() const { return window && (offscreen || testVisibility ? visible : IsWindowVisible(window) != FALSE); }
    bool topmost() const {
        if (testBand) return window && bandState;
        return window && (GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
    }
    // The topmost band (Mac: the HUD panel's .statusBar level, above every
    // normal window): entered by show() before the window appears, left by
    // hide() once it is hidden. Only the z-order changes, never geometry,
    // visibility or activation.
    void setBand(HWND target, bool topmostBand) {
        if (testBand) { bandState = testBand(topmostBand); return; }
        (void)SetWindowPos(target, topmostBand ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
    // Windows applies its foreground rules to raising a window too: for a
    // process that may not set the foreground window, SetWindowPos into the
    // topmost band succeeds without an error yet leaves the window out of
    // the band, just behind the foreground window (laptop, logged-in session
    // with a game in front). So the band is judged by the window's own style
    // afterwards, a refusal is counted, and the next activation of the
    // window (when the process holds that right) enters it.
    void enterBand(HWND target) {
        ++counts.bandRequests;
        setBand(target, true);
        if (window != target) return;
        if (topmost()) { if (std::exchange(bandPending, false)) ++counts.deferredBands; }
        else { bandPending = true; ++counts.bandRefusals; }
    }
    // An activating show asks for the foreground once. Refused (the foreground
    // lock), the window waits for the activation Windows allows: a click on
    // it, or a later opening that holds the right (which also enters the band
    // if that was refused too).
    void requestForeground(HWND target) {
        ++counts.foregroundRequests;
        const bool granted = testForeground ? testForeground() : SetForegroundWindow(target) != FALSE;
        if (!granted) { ++counts.foregroundRefusals; activationPending = true; }
        else if (std::exchange(activationPending, false)) ++counts.deferredActivations;
    }
    // Hides the window (or a hidden test's stand-in) and leaves the band.
    void conceal(HWND target) {
        activationPending = bandPending = false;
        if (testVisibility) { (void)testVisibility(false, false); visible = false; if (ready && window == target) refreshSchedule(); }
        else ShowWindow(target, SW_HIDE);
        if (window == target && topmost()) setBand(target, false);
    }
    UINT wheelLines{3},wheelChars{3};
    void refreshWheelSettings(){SystemParametersInfoW(SPI_GETWHEELSCROLLLINES,0,&wheelLines,0);SystemParametersInfoW(SPI_GETWHEELSCROLLCHARS,0,&wheelChars,0);}
    void releaseCapture(){if(window&&GetCapture()==window)ReleaseCapture();}
    int exitCode{};

    ~Impl() {
        // Public ownership contract requires destruction on this UI thread.
        if (GetCurrentThreadId() != thread) std::terminate();
        if (window) DestroyWindow(window);
        if (timer) { CancelWaitableTimer(timer); CloseHandle(timer); }
    }
    void requireThread() const {
        if (GetCurrentThreadId() != thread) throw std::logic_error("OverlayHost belongs to its creating UI thread");
    }
    void rethrow() {
        if (callbackError) { auto error = std::exchange(callbackError, {}); std::rethrow_exception(error); }
    }
    void cancelTimer() noexcept {
        if (!timer) return;
        if (armed) { CancelWaitableTimer(timer); ++counts.timerCancels; }
        // Cancellation alone does not clear an already-signaled timer. It is
        // auto-reset, so consume that stale signal without a wait/callback.
        WaitForSingleObject(timer, 0);
        armed = false;
    }
    void cancelFrames() noexcept {
        cancelTimer();
        plan.cancel();
        frames.cancel();
        clearGate(); frameInterval.reset();
        deadlines.setFrame({});
        if (stopped || !window) deadlines.clear();
    }
    void clearGate() noexcept {
        if (gated && window) KillTimer(window, gateTimer);
        gated.reset();
    }
    // The owner's frame latency object while a frame must still wait for
    // its admission; null when there is none or the next frame holds one.
    static HANDLE gateObject(const std::shared_ptr<OverlayCallbacks>& handlers) {
        if (!handlers || !handlers->presentationWaitable) return nullptr;
        const auto object = static_cast<HANDLE>(handlers->presentationWaitable());
        if (!object || (handlers->presentationHeld && handlers->presentationHeld())) return nullptr;
        return object;
    }
    // Display-paced frames follow one another through the owner's gate.
    bool gateChains() const {
        const auto handlers = callbacks;
        return handlers && handlers->presentationWaitable && handlers->presentationWaitable() != nullptr;
    }
    // A frame may start now: no gate, an admission held, or the object
    // admits one at once (taken here and reported to the owner).
    bool admitFrame(const std::shared_ptr<OverlayCallbacks>& handlers) {
        const auto object = gateObject(handlers);
        if (!object) return true;
        if (WaitForSingleObject(object, 0) != WAIT_OBJECT_0) return false;
        ++counts.presentationAdmissions;
        if (handlers->presentationAdmitted) handlers->presentationAdmitted();
        return true;
    }
    // The waitable-timer interval to the next frame: the display refresh for
    // paced finite motion (none while the gate chains those frames), the
    // source cadence otherwise.
    std::optional<double> timerInterval() const {
        if (DisplayPacing::paced(plan) && gateChains()) return {};
        return pacing.interval(plan);
    }
    void refreshDisplayRate() {
        std::optional<double> period;
        MONITORINFOEXW monitor{}; monitor.cbSize = sizeof(monitor);
        DEVMODEW mode{}; mode.dmSize = sizeof(mode);
        if (fixedRefresh > 0) period = 1.0 / fixedRefresh;
        else if (window && GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor) &&
            EnumDisplaySettingsW(monitor.szDevice, ENUM_CURRENT_SETTINGS, &mode) && mode.dmDisplayFrequency > 1)
            period = 1.0 / mode.dmDisplayFrequency;
        const auto before = pacing.refreshPeriod();
        pacing.setRefreshPeriod(period);
        counts.refreshRate = pacing.refreshPeriod() ? 1 / *pacing.refreshPeriod() : 0;
        if (ready && before != pacing.refreshPeriod() && plan.canPresent()) {
            const auto interval = timerInterval();
            frameInterval = interval;
            deadlines.setFrame(interval ? std::optional<double>(monotonicSeconds() + *interval) : std::optional<double>());
            syncTimer();
        }
    }
    // Runs the frame of a still-current ticket; display-paced motion then
    // asks for its next frame (or, when this frame presented nothing and so
    // kept its admission, the timer brings it a refresh later: no spinning).
    void runFrame(std::uintptr_t ticket, const std::shared_ptr<OverlayCallbacks>& handlers) {
        if (!frames.consume(ticket, window && !stopped && plan.canPresent() && visible && !minimized) || !handlers || !handlers->frame) return;
        ++counts.frames;
        const bool paced = DisplayPacing::paced(plan);
        if (paced) ++counts.pacedFrames;
        handlers->frame(monotonicSeconds());
        if (!window || stopped || !plan.canPresent() || !DisplayPacing::paced(plan) || !gateChains()) return;
        if (gateObject(callbacks)) postFrame();
        else if (const auto period = pacing.interval(plan)) { deadlines.setFrame(monotonicSeconds() + *period); syncTimer(); }
    }
    void runGated() {
        if (!gated) return;
        const auto ticket = *gated; clearGate();
        const auto handlers = callbacks;
        runFrame(ticket, handlers);
    }
    void releaseCursor() noexcept {
        // Only undo a cursor actually installed by this host, and never replace
        // a text/resize/drag cursor selected subsequently by a native control.
        if (cursorOwned && installed && GetCursor() == installed) {
            SetCursor(LoadCursorW(nullptr, IDC_ARROW));
            ++counts.cursorReleases;
        }
        cursorOwned = false;
        installed = nullptr;
    }
    // The one cursor path (WM_SETCURSOR and refreshCursor): while this active,
    // focused foreground window shows its own client area, install the source
    // cursor, or the I-beam where the owner reports an open text editor.
    bool ownsCursorAt(HWND target) const {
        return (cursor || textCursor) && !cursorSuspended && !menuDepth && !sizeMoveDepth && appActive && focused &&
            visible && IsWindowVisible(target) && IsWindowEnabled(target) && !stopped &&
            GetForegroundWindow() == target && GetFocus() == target;
    }
    bool applyCursor(HWND target, const std::shared_ptr<OverlayCallbacks>& handlers) {
        POINT point{};
        OverlayCursor wanted = OverlayCursor::source;
        if (handlers && handlers->cursorAt && GetCursorPos(&point) && ScreenToClient(target, &point))
            wanted = handlers->cursorAt(point.x / client.scale, point.y / client.scale);
        if (window != target) return false;
        HCURSOR chosen = cursor;
        if (wanted == OverlayCursor::text && textCursor) chosen = textCursor;
        else if (wanted == OverlayCursor::openHand && openHandCursor) chosen = openHandCursor;
        else if (wanted == OverlayCursor::closedHand && closedHandCursor) chosen = closedHandCursor;
        if (!chosen) return false;
        SetCursor(chosen);
        installed = chosen;
        cursorOwned = true;
        ++counts.cursorSets;
        if (chosen == textCursor) ++counts.textCursorSets;
        else if (chosen == openHandCursor || chosen == closedHandCursor) ++counts.handCursorSets;
        return true;
    }
    void postFrame() {
        ++counts.frameRequests;
        const auto ticket = frames.request(window && !stopped && plan.canPresent());
        if (!ticket) return;
        if (!PostMessageW(window, frameMessage, static_cast<WPARAM>(*ticket), 0)) {
            frames.cancel(); fail("PostMessage frame");
        }
        ++counts.framePosts;
    }
    void arm(double deadline) {
        // Windows' signed 100-ns due time also bounds extremely distant caller
        // deadlines; a clamped native wake only rechecks the actual deadline.
        const double remaining = std::clamp(deadline - monotonicSeconds(), 0.0000001, 86400. * 365);
        LARGE_INTEGER due{};
        due.QuadPart = -static_cast<LONGLONG>(std::ceil(remaining * 10000000));
        if (!SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE)) fail("SetWaitableTimer");
        timerDeadline = deadline;
        armed = true;
        ++counts.timerArms;
    }
    void syncTimer() {
        const auto next=deadlines.next();
        if (!next || !window || stopped || !ready) { cancelTimer(); return; }
        if (armed && timerDeadline==*next) return;
        cancelTimer(); arm(*next);
    }
    void refreshSchedule() {
        const auto before = frameInterval;
        const auto result = plan.refresh(demand, ready && visible && presented() && !minimized && !stopped);
        if (!plan.canPresent()) { cancelFrames(); syncTimer(); return; }
        const auto interval = timerInterval();
        if (!interval) deadlines.setFrame({});
        else if (!deadlines.frame() || before != interval)
            deadlines.setFrame(monotonicSeconds() + *interval);
        frameInterval = interval;
        syncTimer();
        if (result.present || (DisplayPacing::paced(plan) && gateChains())) postFrame();
    }
    void timerReady() {
        if (!armed) return;
        armed = false;
        ++counts.timerWakes;
        const auto now=monotonicSeconds();
        const auto due=deadlines.takeDue(now);
        if (due.frame) {
            const auto result = plan.wake();
            if (result.present) postFrame();
            const auto interval = timerInterval();
            frameInterval = interval;
            if (interval && window && !stopped) {
            auto deadline = *due.frame + *interval;
            // No catch-up burst after a busy frame, sleep, resize, or debugger.
            if (deadline <= now) deadline = now + *interval;
            deadlines.setFrame(deadline);
            }
        }
        if (due.external && window && !stopped) {
            ++counts.externalWakes;
            const auto handlers=callbacks;
            if (handlers && handlers->deadline) handlers->deadline(now);
        }
        syncTimer();
    }
    // A drag move's coalesced samples: the system mouse history between the
    // previous delivered position and this move (GetMouseMovePointsEx, which
    // knows only real input; a posted or sent move has none).
    std::span<const PointerSample> mouseHistory(HWND target, UINT message, LPARAM l) {
        historySamples.clear();
        POINT origin{0, 0};
        if (!ClientToScreen(target, &origin)) { lastMouse.reset(); return {}; }
        const MouseHistoryPoint current{origin.x + GET_X_LPARAM(l), origin.y + GET_Y_LPARAM(l), static_cast<std::uint32_t>(GetMessageTime())};
        const auto previous = std::exchange(lastMouse, current);
        if (message != WM_MOUSEMOVE || !previous || GetCapture() != target) return {};
        MOUSEMOVEPOINT input{};
        input.x = current.x & 0xffff; input.y = current.y & 0xffff; input.time = current.time;
        std::array<MOUSEMOVEPOINT, 64> found{};
        const int count = GetMouseMovePointsEx(sizeof(MOUSEMOVEPOINT), &input, found.data(), static_cast<int>(found.size()), GMMP_USE_DISPLAY_POINTS);
        if (count <= 1) return {};
        std::array<MouseHistoryPoint, 64> points{};
        for (int k = 0; k < count; ++k) {
            // Display points left of or above the primary monitor arrive as
            // unsigned 16-bit values.
            const auto x = found[k].x > 32767 ? found[k].x - 65536 : found[k].x, y = found[k].y > 32767 ? found[k].y - 65536 : found[k].y;
            points[static_cast<std::size_t>(k)] = {x, y, static_cast<std::uint32_t>(found[k].time)};
        }
        historyPoints.clear();
        newerMouseHistory(std::span(points.data(), static_cast<std::size_t>(count)), current, *previous, historyPoints);
        for (const auto& point : historyPoints)
            historySamples.push_back({(point.x - origin.x) / client.scale, (point.y - origin.y) / client.scale});
        counts.pointerHistorySamples += historySamples.size();
        return historySamples;
    }
    void updateMetrics() {
        RECT rect{};
        if (!window || !GetClientRect(window, &rect)) return;
        auto next = client;
        next.pixelWidth = static_cast<std::uint32_t>(std::max(0L, rect.right - rect.left));
        next.pixelHeight = static_cast<std::uint32_t>(std::max(0L, rect.bottom - rect.top));
        next.scale = static_cast<double>(next.dpi) / 96;
        next.width = next.pixelWidth / next.scale;
        next.height = next.pixelHeight / next.scale;
        const bool changed = next != client;
        client = next;
        const auto handlers = callbacks;
        if (ready && changed && handlers && handlers->resize) handlers->resize(next);
    }
    static LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM w, LPARAM l) noexcept {
        auto* self = reinterpret_cast<Impl*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            self = static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
            self->window = window;
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (!self) return DefWindowProcW(window, message, w, l);
        try { return self->message(window, message, w, l); }
        catch (...) {
            // Never unwind through USER32. Propagate on the next pump/API edge,
            // with all scheduling stopped even if a callback threw during paint.
            self->callbackError = std::current_exception();
            try{std::rethrow_exception(self->callbackError);}catch(const std::exception&error){std::fprintf(stderr,"HUD callback failed: %s\n",error.what());std::fflush(stderr);}catch(...){std::fputs("HUD callback failed: unknown exception\n",stderr);std::fflush(stderr);}
            self->stopped = true;
            self->cancelFrames();
            self->releaseCursor();
            return message == WM_NCCREATE ? FALSE : 0;
        }
    }
    LRESULT message(HWND target, UINT message, WPARAM w, LPARAM l) {
        // A callback may destroy/recreate this HWND. Its captured state must
        // outlive that operation, without copying/allocating functions per event.
        const auto handlers = callbacks;
        // WM_WTSSESSION_CHANGE (0x02B1) arrives only after the owner registered
        // for session notifications (lock/disconnect suspend the HUD).
        const bool ownerNotice=(message>=WM_APP&&message<=0xffff&&message!=frameMessage)||message==WM_HOTKEY||message==WM_POWERBROADCAST||message==WM_CLIPBOARDUPDATE||message==WM_TIMECHANGE||message==WM_DEVICECHANGE||message==0x02B1;
        if(ownerNotice&&ready&&handlers&&handlers->appMessage){
            const auto result=handlers->appMessage({target,message,w,l});
            if(result)return static_cast<LRESULT>(*result);
            if(window!=target||callbacks!=handlers)return 0;
        }
        switch (message) {
        case WM_NCDESTROY:
            deadlines.clear();
            cancelFrames();
            releaseCursor();
            ready = visible = focused = false;
            window = nullptr;
            SetWindowLongPtrW(target, GWLP_USERDATA, 0);
            return DefWindowProcW(target, message, w, l);
        case WM_ERASEBKGND: return 1; // DComp owns every pixel, including transparency
        case WM_PAINT: {
            PAINTSTRUCT paint{};
            BeginPaint(target, &paint); EndPaint(target, &paint);
            if (ready) postFrame();
            return 0;
        }
        case frameMessage: {
            const auto ticket = static_cast<std::uintptr_t>(w);
            if (!frames.current(ticket)) return 0;
            // The swap chain still holds the previous frame: wait in the pump
            // (input keeps flowing) instead of queueing or blocking. A nested
            // modal loop runs the frame as before.
            if (ownFrameDispatch && window && !stopped && visible && !minimized && plan.canPresent() && !admitFrame(handlers)) {
                if (!gated) { gatedSince = monotonicSeconds(); ++counts.presentationWaits; SetTimer(target, gateTimer, USER_TIMER_MINIMUM, nullptr); }
                gated = ticket;
                return 0;
            }
            runFrame(ticket, handlers);
            return 0;
        }
        case WM_TIMER:
            if (w != gateTimer) break;
            if (!gated || !frames.current(*gated)) { clearGate(); return 0; }
            if (admitFrame(handlers)) runGated();
            else if (gatedSince + gateBound <= monotonicSeconds()) { ++counts.presentationFallbacks; runGated(); }
            return 0;
        case WM_SHOWWINDOW:
            visible = w != 0;
            if (!visible) { mouseTracking = false; releaseCursor(); releaseCapture(); }
            if(window!=target||callbacks!=handlers)return 0;
            if (ready) refreshSchedule();
            break;
        case WM_SIZE:
            minimized = w == SIZE_MINIMIZED;
            if (minimized) releaseCursor();
            updateMetrics();
            if (ready) refreshSchedule();
            break;
        case WM_DPICHANGED: {
            if (LOWORD(w) != 0) client.dpi = LOWORD(w);
            const auto* suggested = reinterpret_cast<const RECT*>(l);
            RECT fitted{};
            if (suggested && coverMonitor) {
                // A monitor-covering popup keeps its monitor's exact pixel
                // bounds; the suggested rectangle only scales the old size.
                MONITORINFO info{}; info.cbSize = sizeof(info);
                const auto monitor = MonitorFromRect(suggested, MONITOR_DEFAULTTONEAREST);
                if (monitor && GetMonitorInfoW(monitor, &info)) fitted = info.rcMonitor; else fitted = *suggested;
            } else if (suggested) fitted = *suggested;
            const auto* rect = suggested ? &fitted : nullptr;
            if (rect && !SetWindowPos(target, nullptr, rect->left, rect->top,
                rect->right - rect->left, rect->bottom - rect->top, SWP_NOZORDER | SWP_NOACTIVATE))
                fail("Apply WM_DPICHANGED rectangle");
            updateMetrics();
            refreshDisplayRate();
            if (ready) postFrame();
            return 0;
        }
        case WM_SETTINGCHANGE:
            if(w==0||w==SPI_SETWHEELSCROLLLINES||w==SPI_SETWHEELSCROLLCHARS)refreshWheelSettings();
            // Calendar/clock owners reconcile locale and zone changes through
            // this same event boundary. Native wheel settings are refreshed
            // first even when the owner consumes the notification.
            if(ready&&handlers&&handlers->appMessage){
                const auto result=handlers->appMessage({target,message,w,l});
                if(result)return static_cast<LRESULT>(*result);
                if(window!=target||callbacks!=handlers)return 0;
            }
            break;
        case WM_DISPLAYCHANGE:
            refreshDisplayRate();
            if(ready&&handlers&&handlers->displayChanged)handlers->displayChanged();
            break;
        case WM_WINDOWPOSCHANGED:
            // The window may now present on another display.
            if (ready && visible) refreshDisplayRate();
            break;
        case WM_MOUSEACTIVATE:
            // A click activates the window (never MA_NOACTIVATE): how a window
            // shown without the foreground takes it, as Windows allows.
            if (window == target) return MA_ACTIVATE;
            break;
        case WM_ACTIVATE:
            if (window == target && LOWORD(w) != WA_INACTIVE) {
                if (std::exchange(activationPending, false)) ++counts.deferredActivations;
                // Active, the process may raise its window: the band refused earlier.
                if (bandPending && visible) enterBand(target);
            }
            break; // the default procedure gives this window the keyboard focus
        case WM_SETFOCUS: case WM_KILLFOCUS:
            focused = message == WM_SETFOCUS;
            if (!focused) { releaseCursor(); releaseCapture(); }
            if(window!=target||callbacks!=handlers)return 0;
            if (ready && handlers && handlers->focus) handlers->focus(focused);
            break;
        case WM_ACTIVATEAPP: {
            const bool changed=appActive!=(w!=0);appActive = w != 0;
            if (!appActive) releaseCursor();
            if(window!=target||callbacks!=handlers)return 0;
            if(appActive&&bandPending&&visible)enterBand(target);
            if(changed&&ready&&handlers&&handlers->applicationActive)handlers->applicationActive(appActive);
            break;
        }
        case WM_ENABLE:
            if (!w) releaseCursor();
            break;
        case WM_ENTERMENULOOP: ++menuDepth; releaseCursor(); break;
        case WM_EXITMENULOOP: if (menuDepth) --menuDepth; break;
        case WM_ENTERSIZEMOVE: ++sizeMoveDepth; releaseCursor(); break;
        case WM_EXITSIZEMOVE: if (sizeMoveDepth) --sizeMoveDepth; break;
        case WM_SETCURSOR:
            if (reinterpret_cast<HWND>(w) == target && LOWORD(l) == HTCLIENT && HIWORD(l) != 0 &&
                ownsCursorAt(target) && applyCursor(target, handlers))
                return TRUE;
            if (window != target) return 0;
            break;
        case WM_CLOSE:
            if (ready && handlers && handlers->closeRequested) handlers->closeRequested();
            else conceal(target);
            return 0;
        case WM_QUERYENDSESSION:
            if (ready && handlers && handlers->sessionEnding) handlers->sessionEnding(SessionEnd::query, static_cast<std::uint32_t>(l));
            return TRUE;
        case WM_ENDSESSION:
            if (ready && handlers && handlers->sessionEnding)
                handlers->sessionEnding(w ? SessionEnd::ending : SessionEnd::cancelled, static_cast<std::uint32_t>(l));
            return 0;
        case WM_MOUSEMOVE: case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_LBUTTONDBLCLK:
        case WM_RBUTTONDOWN: case WM_RBUTTONUP: case WM_RBUTTONDBLCLK:
        case WM_MBUTTONDOWN: case WM_MBUTTONUP: case WM_MBUTTONDBLCLK:
        case WM_XBUTTONDOWN: case WM_XBUTTONUP: case WM_XBUTTONDBLCLK: {
            ++counts.pointerMessages;
            lastX = GET_X_LPARAM(l) / client.scale;
            lastY = GET_Y_LPARAM(l) / client.scale;
            if (visible && !mouseTracking) {
                TRACKMOUSEEVENT tracking{sizeof(TRACKMOUSEEVENT), TME_LEAVE, target, 0};
                mouseTracking = TrackMouseEvent(&tracking) != FALSE;
            }
            const auto history = mouseHistory(target, message, l);
            const PointerEvent event{kindFor(message), buttonFor(message, w), lastX, lastY,
                static_cast<std::uint32_t>(GET_KEYSTATE_WPARAM(w)), history};
            const bool handled = ready && handlers && handlers->pointer && handlers->pointer(event);
            if (window != target) return 0;
            if (ready) postFrame();
            if (handled) return buttonFor(message, w) == PointerButton::extra1 ||
                buttonFor(message, w) == PointerButton::extra2 ? TRUE : 0;
            break;
        }
        case WM_MOUSELEAVE: case WM_CAPTURECHANGED: {
            // lParam names the window gaining the capture. Windows answers
            // SetCapture on the window that already holds the capture with
            // WM_CAPTURECHANGED naming that same window: nothing was lost, so
            // the owner's gesture (a Projection or Notes stroke) continues.
            if (message == WM_CAPTURECHANGED && reinterpret_cast<HWND>(l) == target) break;
            mouseTracking = false;
            if (ready && handlers && handlers->pointer) handlers->pointer({message == WM_MOUSELEAVE ?
                PointerKind::leave : PointerKind::captureLost, PointerButton::none, lastX, lastY, 0});
            if (ready) postFrame();
            break;
        }
        case WM_MOUSEWHEEL: case WM_MOUSEHWHEEL: {
            static_assert(MK_CONTROL == wheelControlKey, "pinchMagnification reads the Win32 Ctrl key bit");
            POINT point{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
            if (!ScreenToClient(target, &point)) fail("Wheel ScreenToClient");
            const WheelEvent event{point.x / client.scale, point.y / client.scale,
                static_cast<double>(GET_WHEEL_DELTA_WPARAM(w)) / WHEEL_DELTA,
                message == WM_MOUSEHWHEEL, static_cast<std::uint32_t>(GET_KEYSTATE_WPARAM(w)),message==WM_MOUSEHWHEEL?wheelChars:wheelLines};
            const bool handled = ready && handlers && handlers->wheel && handlers->wheel(event);
            if (window != target) return 0;
            if (ready) postFrame();
            if (handled) return 0;
            break;
        }
        case WM_KEYDOWN: case WM_KEYUP: case WM_SYSKEYDOWN: case WM_SYSKEYUP:
        case WM_CHAR: case WM_SYSCHAR: case WM_UNICHAR: {
            if (message == WM_UNICHAR && w == UNICODE_NOCHAR) return TRUE;
            const auto bits = static_cast<std::uintptr_t>(l);
            const KeyEvent event{
                message == WM_UNICHAR ? KeyKind::unicodeCharacter :
                (message == WM_CHAR || message == WM_SYSCHAR) ? KeyKind::character :
                (message == WM_KEYUP || message == WM_SYSKEYUP) ? KeyKind::up : KeyKind::down,
                static_cast<std::uint32_t>(w), static_cast<std::uint16_t>(bits & 0xffff),
                static_cast<std::uint8_t>((bits >> 16) & 0xff),
                message == WM_SYSKEYDOWN || message == WM_SYSKEYUP || message == WM_SYSCHAR,
                (bits & (1ull << 24)) != 0, (bits & (1ull << 29)) != 0, (bits & (1ull << 30)) != 0};
            if (ready && handlers && handlers->key && handlers->key(event)) return 0;
            break;
        }
        case WM_GETOBJECT:
            if (handlers && handlers->getObject) {
                const auto result = handlers->getObject({target, message, w, l});
                if (window != target) return 0;
                if (result) return static_cast<LRESULT>(*result);
            }
            break;
        default: break; // Includes every WM_IME_* message and native nonclient behavior.
        }
        return window == target ? DefWindowProcW(target, message, w, l) : 0;
    }
};

OverlayHost::OverlayHost() : impl_(std::make_unique<Impl>()) {}
OverlayHost::~OverlayHost() = default;
void OverlayHost::create(const OverlayOptions& options, OverlayCallbacks callbacks) {
    auto& p = *impl_; p.requireThread(); p.rethrow();
    if (p.window) throw std::logic_error("OverlayHost already owns a window");
    if (options.pixelWidth == 0 || options.pixelHeight == 0 ||
        options.pixelWidth > 16384 || options.pixelHeight > 16384)
        throw std::invalid_argument("Overlay client dimensions must be in1..16384");
    static const ATOM registered = [] {
        WNDCLASSEXW type{}; type.cbSize = sizeof(type);
        type.style = CS_DBLCLKS;
        type.lpfnWndProc = Impl::procedure;
        type.hInstance = GetModuleHandleW(nullptr);
        type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        type.lpszClassName = windowClass;
        const auto atom = RegisterClassExW(&type);
        if (!atom) fail("RegisterClassEx overlay");
        return atom;
    }();
    (void)registered;
    if (!p.timer) {
        // Per-object high-resolution wake, supported since Windows10 1803.
        // It does not change system/process timer resolution. Older Windows10
        // still uses the same single timer, at its available native precision.
        constexpr DWORD highResolution = 0x00000002;
        p.timer = CreateWaitableTimerExW(nullptr, nullptr, highResolution, TIMER_MODIFY_STATE | SYNCHRONIZE);
        if (!p.timer && GetLastError() == ERROR_INVALID_PARAMETER)
            p.timer = CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_MODIFY_STATE | SYNCHRONIZE);
        if (!p.timer) fail("CreateWaitableTimerEx overlay");
    }
    p.refreshWheelSettings();
    p.callbacks = std::make_shared<OverlayCallbacks>(std::move(callbacks));
    p.demand = {}; p.plan.cancel(); p.client = {}; p.counts = {};
    p.gated.reset(); p.frameInterval.reset(); p.lastMouse.reset(); p.ownFrameDispatch = false;
    p.stopped = p.ready = p.visible = p.minimized = p.focused = p.mouseTracking = false;
    p.menuDepth = p.sizeMoveDepth = 0; p.appActive = true; p.exitCode = 0; p.coverMonitor = options.coverMonitor;
    p.offscreen = options.offscreenScheduling;
    p.fixedRefresh = std::isfinite(options.refreshRate) && options.refreshRate > 0 ? options.refreshRate : 0;
    p.testVisibility = options.testVisibility; p.testForeground = options.testForeground; p.testBand = options.testBand;
    p.activationPending = p.bandPending = p.bandState = false;
    const auto window = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP | WS_EX_TOOLWINDOW, windowClass,
        options.title.c_str(), WS_POPUP, options.x, options.y,
        static_cast<int>(options.pixelWidth), static_cast<int>(options.pixelHeight),
        static_cast<HWND>(options.ownerWindow), nullptr, GetModuleHandleW(nullptr), &p);
    if (!window) { p.rethrow(); fail("CreateWindowEx overlay"); }
    p.rethrow();
    const UINT dpi = GetDpiForWindow(window);
    p.client.dpi = dpi ? dpi : 96;
    p.updateMetrics(); p.ready = true; p.refreshDisplayRate();
    const auto handlers = p.callbacks;
    const auto initialMetrics = p.client;
    if (handlers && handlers->resize) handlers->resize(initialMetrics);
}
void OverlayHost::destroy() {
    auto& p = *impl_; p.requireThread();
    p.cancelFrames(); p.releaseCursor();
    if (p.window && !DestroyWindow(p.window)) fail("DestroyWindow overlay");
    p.callbacks = {};
    if (p.timer) { CloseHandle(p.timer); p.timer = nullptr; }
    p.rethrow();
}
void* OverlayHost::hwnd() const noexcept { return impl_->window; }
ClientMetrics OverlayHost::metrics() const { impl_->requireThread(); return impl_->client; }
void OverlayHost::show(bool activate) {
    auto& p = *impl_; p.requireThread(); p.rethrow();
    if (!p.window || p.stopped) throw std::logic_error("Cannot show stopped/uncreated overlay");
    if (p.offscreen) {
        // Presented for scheduling only: no ShowWindow, focus or activation.
        p.visible = true; p.minimized = false; p.refreshSchedule(); p.rethrow(); return;
    }
    const auto window=p.window;
    // Above every normal application window: the band is entered before the
    // window appears (or the window raised to its top when already shown),
    // as far as Windows' foreground rules allow (enterBand).
    p.enterBand(window);
    p.rethrow();
    if(p.window!=window||p.stopped)return;
    // An explicit owner request must not inherit a launcher's STARTUPINFO
    // SW_HIDE. Preserve geometry and style; show(false) also preserves
    // keyboard focus and activation.
    if(p.testVisibility)p.visible=p.testVisibility(true,activate);
    else{
        const UINT flags=SWP_SHOWWINDOW|SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|(activate?0u:SWP_NOACTIVATE);
        if(!SetWindowPos(window,nullptr,0,0,0,0,flags))fail("Show owned overlay");
    }
    p.rethrow();
    if(p.window==window&&!p.stopped){
        // WM_SHOWWINDOW precedes completed native visibility. A refresh in
        // that callback can still see IsWindowVisible=false, so rebuild the
        // gate now or a stable preexisting demand can remain asleep forever.
        if(!p.testVisibility)p.visible=IsWindowVisible(window)!=FALSE;
        p.minimized=IsIconic(window)!=FALSE;
        p.refreshSchedule();
        if(activate&&p.visible&&p.window==window)p.requestForeground(window);
    }
    p.rethrow();
}
void OverlayHost::hide() {
    auto& p = *impl_; p.requireThread();
    p.visible = false; p.cancelFrames(); p.releaseCursor();
    if (p.window && !p.offscreen) p.conceal(p.window);
    else p.activationPending = p.bandPending = false;
    p.syncTimer();
    p.rethrow();
}
bool OverlayHost::place(int x, int y, int width, int height) {
    auto& p = *impl_; p.requireThread(); p.rethrow();
    if (!p.window) return false;
    // Geometry only: never HWND_TOPMOST here, whatever the window shows.
    const bool placed = SetWindowPos(p.window, nullptr, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE) != FALSE;
    p.rethrow();
    return placed;
}
double OverlayHost::clockNow() { return monotonicSeconds(); }
void OverlayHost::setDeadline(std::optional<double> deadline) {
    auto& p=*impl_;p.requireThread();p.rethrow();
    if (!p.window || p.stopped) throw std::logic_error("Cannot schedule an uncreated/stopped overlay");
    if (deadline && (!p.callbacks || !p.callbacks->deadline))
        throw std::logic_error("External wake needs its owner callback");
    p.deadlines.setExternal(deadline);p.syncTimer();
}
void OverlayHost::setFrameDemand(core::FrameDemand demand) {
    auto& p = *impl_; p.requireThread(); p.rethrow();
    if (sameDemand(demand, p.demand)) return;
    p.demand = demand;
    p.refreshSchedule();
}
void OverlayHost::invalidate() { auto& p = *impl_; p.requireThread(); p.rethrow(); p.postFrame(); }
void OverlayHost::capturePointer(bool capture) {
    auto&p=*impl_;p.requireThread();p.rethrow();
    // Owners may request the capture on every event of a gesture (Projection
    // asks for pointerLocked() after each one): take it only when this window
    // does not hold it yet, so a drag is never re-captured mid-stroke.
    if(capture){if(p.window&&IsWindowVisible(p.window)&&GetCapture()!=p.window)SetCapture(p.window);}
    else p.releaseCapture();
}
void OverlayHost::setCursor(void* cursor) {
    auto& p = *impl_; p.requireThread(); p.rethrow();
    if (p.cursor == cursor) return;
    p.releaseCursor(); p.cursor = static_cast<HCURSOR>(cursor);
}
void OverlayHost::refreshCursor() {
    auto& p = *impl_; p.requireThread(); p.rethrow();
    // Only a cursor this window already shows is re-answered: the pointer is
    // over its client area and no other window, control or loop took it.
    if (!p.window || !p.cursorOwned || !p.installed || GetCursor() != p.installed || !p.ownsCursorAt(p.window)) return;
    POINT point{};
    if (!GetCursorPos(&point) || WindowFromPoint(point) != p.window) return;
    const auto handlers = p.callbacks;
    (void)p.applyCursor(p.window, handlers);
    p.rethrow();
}
void OverlayHost::suspendCursor(bool suspended) {
    auto& p = *impl_; p.requireThread(); p.rethrow();
    p.cursorSuspended = suspended;
    if (suspended) p.releaseCursor();
}
bool OverlayHost::pumpOnce(std::uint32_t timeout) {
    auto& p = *impl_; p.requireThread(); p.rethrow();
    if (!p.window || p.stopped) return false;
    // A frame waiting for the swap chain's admission waits here, with the
    // input: its ticket stays pending, so new requests coalesce into it, and
    // it runs after the input that arrived with its admission.
    HANDLE gate = nullptr; bool runGated = false;
    if (p.gated) {
        if (!p.frames.current(*p.gated)) p.clearGate();
        else if (!(gate = Impl::gateObject(p.callbacks))) runGated = true; // admitted, or no gate any more
        else if (p.gatedSince + gateBound <= monotonicSeconds()) { gate = nullptr; runGated = true; ++p.counts.presentationFallbacks; }
    }
    DWORD wait = timeout;
    if (gate) wait = std::min<DWORD>(wait, static_cast<DWORD>(std::ceil(std::max(0., p.gatedSince + gateBound - monotonicSeconds()) * 1000)));
    std::array<HANDLE, 2> handles{}; DWORD count = 0;
    if (gate) handles[count++] = gate;
    const DWORD timerSlot = p.armed ? count : MAXDWORD;
    if (p.armed) handles[count++] = p.timer;
    const auto status = runGated ? static_cast<DWORD>(WAIT_TIMEOUT) :
        MsgWaitForMultipleObjectsEx(count, count ? handles.data() : nullptr, wait, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    if (status == WAIT_FAILED) fail("MsgWaitForMultipleObjectsEx overlay");
    if (gate && status == WAIT_OBJECT_0) {
        // The wait consumed the admission: it belongs to the gated frame.
        ++p.counts.presentationAdmissions; runGated = true;
        const auto handlers = p.callbacks;
        if (handlers && handlers->presentationAdmitted) handlers->presentationAdmitted();
    } else if (timerSlot != MAXDWORD && status == WAIT_OBJECT_0 + timerSlot) {
        try { p.timerReady(); }
        catch (...) { p.stopped=true;p.cancelFrames();p.releaseCursor();throw; }
    } else if (gate && status == WAIT_TIMEOUT && p.gatedSince + gateBound <= monotonicSeconds()) {
        ++p.counts.presentationFallbacks; runGated = true;
    }
    p.rethrow();
    MSG message{};
    // A finite batch prevents a producer from starving the one animation clock.
    // Another pump immediately observes remaining input through INPUTAVAILABLE.
    for (unsigned dispatched = 0; dispatched < 256 && PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE); ++dispatched) {
        if (message.message == WM_QUIT) {
            p.exitCode = static_cast<int>(message.wParam); p.stopped = true; p.cancelFrames(); break;
        }
        const auto owner=p.window;
        const auto handlers=p.callbacks;
        const bool keyboard=message.message==WM_KEYDOWN||message.message==WM_KEYUP||
            message.message==WM_SYSKEYDOWN||message.message==WM_SYSKEYUP;
        if(keyboard&&(message.hwnd==owner||IsChild(owner,message.hwnd)||GetAncestor(message.hwnd,GA_ROOTOWNER)==owner)&&handlers&&handlers->beforeKeyTranslation){
            bool consumed{};
            try{consumed=handlers->beforeKeyTranslation({message.hwnd,message.message,message.wParam,message.lParam});}
            catch(...){p.stopped=true;p.cancelFrames();p.releaseCursor();throw;}
            if(!p.window||p.stopped)break;
            // A filter may recreate its owner. Never translate an old queued
            // key into the replacement editor, even if Windows reuses HWND.
            if(consumed||p.window!=owner||p.callbacks!=handlers)continue;
        }
        TranslateMessage(&message);
        p.ownFrameDispatch = message.message == frameMessage && message.hwnd == owner;
        DispatchMessageW(&message);
        p.ownFrameDispatch = false;
        p.rethrow();
        if (!p.window || p.stopped) break;
    }
    if (runGated && p.window && !p.stopped) {
        try { p.runGated(); }
        catch (...) { p.stopped=true;p.cancelFrames();p.releaseCursor();throw; }
    }
    return p.window && !p.stopped;
}
int OverlayHost::run() { while (pumpOnce()) {} return impl_->exitCode; }
void OverlayHost::requestStop() {
    auto& p = *impl_; p.requireThread(); p.stopped = true; p.cancelFrames(); p.releaseCursor();
}
OverlayHostStats OverlayHost::stats() const {
    const auto& p = *impl_; p.requireThread();
    auto result = p.counts;
    result.timerArmed = p.armed; result.framePending = p.frames.pending();
    result.visible = p.visible; result.focused = p.focused;
    result.presentationGated = p.gated.has_value();
    result.topmost = p.topmost(); result.activationPending = p.activationPending; result.bandPending = p.bandPending;
    return result;
}
} // namespace endfield::app
#endif
