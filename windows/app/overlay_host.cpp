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
#include <exception>
#include <system_error>

namespace endfield::app {
namespace {
constexpr UINT frameMessage = WM_APP + 0x31d;
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
    FrameRequestBatch frames;
    ClientMetrics client;
    OverlayHostStats counts;
    std::exception_ptr callbackError;
    HCURSOR cursor{};
    double timerDeadline{}, lastX{}, lastY{};
    bool ready{}, visible{}, minimized{}, focused{}, stopped{}, armed{},
        mouseTracking{}, cursorSuspended{}, cursorOwned{}, appActive{true};
    unsigned menuDepth{}, sizeMoveDepth{};
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
    }
    void releaseCursor() noexcept {
        // Only undo a cursor actually installed by this host, and never replace
        // a text/resize/drag cursor selected subsequently by a native control.
        if (cursorOwned && cursor && GetCursor() == cursor) {
            SetCursor(LoadCursorW(nullptr, IDC_ARROW));
            ++counts.cursorReleases;
        }
        cursorOwned = false;
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
        const double remaining = std::max(0.0000001, deadline - monotonicSeconds());
        LARGE_INTEGER due{};
        due.QuadPart = -static_cast<LONGLONG>(std::ceil(remaining * 10000000));
        if (!SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE)) fail("SetWaitableTimer");
        timerDeadline = deadline;
        armed = true;
        ++counts.timerArms;
    }
    void refreshSchedule() {
        const auto before = plan.interval();
        const auto result = plan.refresh(demand, ready && visible && window && IsWindowVisible(window) && !minimized && !stopped);
        if (!plan.canPresent()) { cancelFrames(); return; }
        if (!result.afterSeconds) cancelTimer();
        else if (!armed || before != result.afterSeconds) {
            cancelTimer();
            arm(monotonicSeconds() + *result.afterSeconds);
        }
        if (result.present) postFrame();
    }
    void timerReady() {
        if (!armed) return;
        armed = false;
        ++counts.timerWakes;
        const auto result = plan.wake();
        if (result.present) postFrame();
        if (result.afterSeconds && window && !stopped) {
            const auto now = monotonicSeconds();
            auto deadline = timerDeadline + *result.afterSeconds;
            // No catch-up burst after a busy frame, sleep, resize, or debugger.
            if (deadline <= now) deadline = now + *result.afterSeconds;
            arm(deadline);
        }
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
        if(message>=WM_APP&&message<=0xbfff&&message!=frameMessage&&ready&&handlers&&handlers->appMessage){
            const auto result=handlers->appMessage({target,message,w,l});
            if(result)return static_cast<LRESULT>(*result);
            if(window!=target||callbacks!=handlers)return 0;
        }
        switch (message) {
        case WM_NCDESTROY:
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
        case frameMessage:
            if (frames.consume(static_cast<std::uintptr_t>(w), window && !stopped && plan.canPresent() &&
                visible && !minimized) && handlers && handlers->frame) {
                ++counts.frames;
                handlers->frame(monotonicSeconds());
            }
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
            const auto* rect = reinterpret_cast<const RECT*>(l);
            if (rect && !SetWindowPos(target, nullptr, rect->left, rect->top,
                rect->right - rect->left, rect->bottom - rect->top, SWP_NOZORDER | SWP_NOACTIVATE))
                fail("Apply WM_DPICHANGED rectangle");
            updateMetrics();
            if (ready) postFrame();
            return 0;
        }
        case WM_SETTINGCHANGE:
            if(w==0||w==SPI_SETWHEELSCROLLLINES||w==SPI_SETWHEELSCROLLCHARS)refreshWheelSettings();
            break;
        case WM_DISPLAYCHANGE:
            if(ready&&handlers&&handlers->displayChanged)handlers->displayChanged();
            break;
        case WM_SETFOCUS: case WM_KILLFOCUS:
            focused = message == WM_SETFOCUS;
            if (!focused) { releaseCursor(); releaseCapture(); }
            if (ready && handlers && handlers->focus) handlers->focus(focused);
            break;
        case WM_ACTIVATEAPP:
            appActive = w != 0;
            if (!appActive) releaseCursor();
            break;
        case WM_ENABLE:
            if (!w) releaseCursor();
            break;
        case WM_ENTERMENULOOP: ++menuDepth; releaseCursor(); break;
        case WM_EXITMENULOOP: if (menuDepth) --menuDepth; break;
        case WM_ENTERSIZEMOVE: ++sizeMoveDepth; releaseCursor(); break;
        case WM_EXITSIZEMOVE: if (sizeMoveDepth) --sizeMoveDepth; break;
        case WM_SETCURSOR:
            if (cursor && !cursorSuspended && !menuDepth && !sizeMoveDepth && appActive && focused &&
                visible && IsWindowVisible(target) && IsWindowEnabled(target) && !stopped &&
                reinterpret_cast<HWND>(w) == target && LOWORD(l) == HTCLIENT && HIWORD(l) != 0 &&
                GetForegroundWindow() == target && GetFocus() == target) {
                SetCursor(cursor);
                cursorOwned = true;
                ++counts.cursorSets;
                return TRUE;
            }
            break;
        case WM_CLOSE:
            if (ready && handlers && handlers->closeRequested) handlers->closeRequested();
            else ShowWindow(target, SW_HIDE);
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
            const PointerEvent event{kindFor(message), buttonFor(message, w), lastX, lastY,
                static_cast<std::uint32_t>(GET_KEYSTATE_WPARAM(w))};
            const bool handled = ready && handlers && handlers->pointer && handlers->pointer(event);
            if (window != target) return 0;
            if (ready) postFrame();
            if (handled) return buttonFor(message, w) == PointerButton::extra1 ||
                buttonFor(message, w) == PointerButton::extra2 ? TRUE : 0;
            break;
        }
        case WM_MOUSELEAVE: case WM_CAPTURECHANGED: {
            mouseTracking = false;
            if (ready && handlers && handlers->pointer) handlers->pointer({message == WM_MOUSELEAVE ?
                PointerKind::leave : PointerKind::captureLost, PointerButton::none, lastX, lastY, 0});
            if (ready) postFrame();
            break;
        }
        case WM_MOUSEWHEEL: case WM_MOUSEHWHEEL: {
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
    p.stopped = p.ready = p.visible = p.minimized = p.focused = p.mouseTracking = false;
    p.menuDepth = p.sizeMoveDepth = 0; p.appActive = true; p.exitCode = 0;
    const auto window = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP | WS_EX_TOOLWINDOW, windowClass,
        options.title.c_str(), WS_POPUP, options.x, options.y,
        static_cast<int>(options.pixelWidth), static_cast<int>(options.pixelHeight),
        static_cast<HWND>(options.ownerWindow), nullptr, GetModuleHandleW(nullptr), &p);
    if (!window) { p.rethrow(); fail("CreateWindowEx overlay"); }
    p.rethrow();
    const UINT dpi = GetDpiForWindow(window);
    p.client.dpi = dpi ? dpi : 96;
    p.updateMetrics(); p.ready = true;
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
    const auto window=p.window;
    // An explicit owner request must not inherit a launcher's STARTUPINFO
    // SW_HIDE. Preserve geometry, style and topmost status; show(false) also
    // preserves keyboard focus and activation.
    const UINT flags=SWP_SHOWWINDOW|SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|(activate?0u:SWP_NOACTIVATE);
    if(!SetWindowPos(window,nullptr,0,0,0,0,flags))fail("Show owned overlay");
    p.rethrow();
    if(p.window==window&&!p.stopped){
        // WM_SHOWWINDOW precedes completed native visibility. A refresh in
        // that callback can still see IsWindowVisible=false, so rebuild the
        // gate now or a stable preexisting demand can remain asleep forever.
        p.visible=IsWindowVisible(window)!=FALSE;
        p.minimized=IsIconic(window)!=FALSE;
        p.refreshSchedule();
        if(activate&&p.visible)SetForegroundWindow(window);
    }
    p.rethrow();
}
void OverlayHost::hide() {
    auto& p = *impl_; p.requireThread();
    p.visible = false; p.cancelFrames(); p.releaseCursor();
    if (p.window) ShowWindow(p.window, SW_HIDE);
    p.rethrow();
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
    if(capture&&p.window&&IsWindowVisible(p.window))SetCapture(p.window);
    else if(!capture)p.releaseCapture();
}
void OverlayHost::setCursor(void* cursor) {
    auto& p = *impl_; p.requireThread(); p.rethrow();
    if (p.cursor == cursor) return;
    p.releaseCursor(); p.cursor = static_cast<HCURSOR>(cursor);
}
void OverlayHost::suspendCursor(bool suspended) {
    auto& p = *impl_; p.requireThread(); p.rethrow();
    p.cursorSuspended = suspended;
    if (suspended) p.releaseCursor();
}
bool OverlayHost::pumpOnce(std::uint32_t timeout) {
    auto& p = *impl_; p.requireThread(); p.rethrow();
    if (!p.window || p.stopped) return false;
    const DWORD count = p.armed ? 1 : 0;
    const auto status = MsgWaitForMultipleObjectsEx(count, count ? &p.timer : nullptr,
        timeout, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    if (status == WAIT_FAILED) fail("MsgWaitForMultipleObjectsEx overlay");
    if (count && status == WAIT_OBJECT_0) p.timerReady();
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
        if(keyboard&&(message.hwnd==owner||IsChild(owner,message.hwnd))&&handlers&&handlers->beforeKeyTranslation){
            bool consumed{};
            try{consumed=handlers->beforeKeyTranslation({message.hwnd,message.message,message.wParam,message.lParam});}
            catch(...){p.stopped=true;p.cancelFrames();p.releaseCursor();throw;}
            if(!p.window||p.stopped)break;
            // A filter may recreate its owner. Never translate an old queued
            // key into the replacement editor, even if Windows reuses HWND.
            if(consumed||p.window!=owner||p.callbacks!=handlers)continue;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
        p.rethrow();
        if (!p.window || p.stopped) break;
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
    return result;
}
} // namespace endfield::app
#endif
