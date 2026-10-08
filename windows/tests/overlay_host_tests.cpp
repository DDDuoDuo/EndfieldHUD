#include "app/overlay_host.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#include <chrono>
#include <thread>
#endif

using namespace endfield::app;
using namespace endfield::core;
namespace {
unsigned checks{};
void check(bool condition, const char* message) {
    ++checks; if (!condition) throw std::runtime_error(message);
}
void checkNear(double a, double b, const char* message) { check(std::abs(a - b) < 1e-10, message); }
FrameDemand visibleDemand() {
    FrameDemand demand; demand.phase = VisibilityPhase::visible;
    demand.presented = demand.onScreen = true;
    return demand;
}

// Compare presentation times with the ORIGINAL gate one source tick at a time,
// including every phase, power setting, and all four ways it can be suppressed.
// This detects a30Hz first-tick phase shift and losing finite frames, not merely
// whether our host reports a preferred timer interval.
void scheduling() {
    for (int phase = 0; phase < 4; ++phase) {
        for (unsigned bits = 0; bits < 512; ++bits) {
            auto demand = visibleDemand();
            demand.phase = static_cast<VisibilityPhase>(phase);
            demand.presented = (bits & 1) != 0;
            demand.onScreen = (bits & 2) != 0;
            demand.canAdvanceTransition = (bits & 4) != 0;
            demand.pendingOpening = (bits & 8) != 0;
            demand.finiteAnimation = (bits & 16) != 0;
            demand.ambientEnabled = (bits & 32) != 0;
            demand.reduceMotion = (bits & 64) != 0;
            demand.lowPower = (bits & 128) != 0;
            const bool shown = (bits & 256) != 0;
            FrameWakePlan plan;
            FrameDemandGate source;
            auto gated = demand; gated.presented = demand.presented && shown;
            const auto original = source.refresh(gated);
            auto wake = plan.refresh(demand, shown);
            check(wake.present == original.present && plan.canPresent() == original.present, "refresh matches original source demand");
            check(wake.afterSeconds.has_value() == original.timerInterval.has_value(), "timer eligibility matches source");
            if (!original.timerInterval) {
                for (unsigned i = 0; i < 10; ++i) {
                    const auto idle = plan.wake();
                    check(!idle.present && !idle.afterSeconds, "closed/reduced/idle cannot invent callbacks");
                }
                continue;
            }
            double sourceTime = 0, hostTime = 0;
            unsigned expectedFrames = 0, wakes = 0;
            for (unsigned i = 0; i < 180; ++i) {
                sourceTime += *original.timerInterval;
                if (!source.tick(gated).present) continue;
                check(wake.afterSeconds.has_value(), "live presentation has one scheduled wake");
                hostTime += *wake.afterSeconds;
                wake = plan.wake(); ++wakes; ++expectedFrames;
                check(wake.present, "each host wake actually presents");
                checkNear(sourceTime, hostTime, "source presentation phase and times preserved exactly");
            }
            check(wakes == expectedFrames, "no empty ambient timer wake");
            plan.cancel();
            check(!plan.canPresent() && !plan.interval() && !plan.wake().present, "cancellation blocks stale timer signal");
        }
    }
    // Finite motion changes gate parity. Refresh must preserve that parity when
    // switching to ambient, including whether the next source tick is skipped.
    for (unsigned finiteTicks = 0; finiteTicks < 5; ++finiteTicks) {
        auto demand = visibleDemand(); demand.finiteAnimation = true;
        FrameDemandGate source; FrameWakePlan plan;
        source.refresh(demand); plan.refresh(demand, true);
        for (unsigned i = 0; i < finiteTicks; ++i) {
            check(source.tick(demand).present && plan.wake().present, "finite ticks preserved before ambient transition");
        }
        demand.finiteAnimation = false; demand.ambientEnabled = true;
        auto wake = plan.refresh(demand, true); source.refresh(demand);
        double expectedTime = 0, actualTime = 0;
        for (unsigned i = 0; i < 20; ++i) {
            expectedTime += 1.0 / 60;
            if (!source.tick(demand).present) continue;
            actualTime += *wake.afterSeconds; wake = plan.wake();
            check(wake.present, "ambient remains a useful wake after finite transition");
            checkNear(expectedTime, actualTime, "finite to ambient retains original source parity");
        }
    }
    auto demand = visibleDemand(); demand.ambientEnabled = true;
    FrameWakePlan plan;
    checkNear(*plan.refresh(demand, true).afterSeconds, 1.0 / 60, "first ambient frame retains source half-period");
    checkNear(*plan.wake().afterSeconds, 1.0 / 30, "subsequent ambient wake cadence30Hz");
    demand.lowPower = true;
    check(!plan.refresh(demand, true).afterSeconds, "low power disables stable ambient");
    demand.finiteAnimation = true;
    checkNear(*plan.refresh(demand, true).afterSeconds, 1.0 / 30, "low power finite cadence30Hz");
    demand.lowPower = false;
    checkNear(*plan.refresh(demand, true).afterSeconds, 1.0 / 60, "normal finite cadence60Hz");
    check(!plan.refresh(demand, false).present && !plan.interval(), "hidden overrides even finite demanded motion");
}
void coalescing() {
    FrameRequestBatch batch;
    for (unsigned i = 0; i < 1000; ++i) check(!batch.request(false), "hidden input never queues a frame");
    const auto first = batch.request(true);
    check(first && batch.pending(), "first allowed invalidation queues a frame");
    for (unsigned i = 0; i < 1000; ++i) check(!batch.request(true), "pointer flood coalesces into one pending frame");
    check(batch.consume(*first, true) && !batch.pending(), "batch consumed once");
    const auto second = batch.request(true);
    check(second && !batch.consume(*first, true) && batch.pending(), "duplicate old frame cannot consume the newer request");
    batch.cancel();
    const auto reopened = batch.request(true);
    check(reopened && !batch.consume(*second, true) && batch.pending(), "queued old opening cannot consume reopened frame");
    check(batch.consume(*reopened, true), "reopening can present its own frame");
    const auto concealed = batch.request(true);
    check(concealed && !batch.consume(*concealed, false) && !batch.pending(), "concealment drops already queued frame");
}

#ifdef _WIN32
void drain(OverlayHost& host) { for (unsigned i = 0; i < 8; ++i) host.pumpOnce(0); }
void nativeWindow() {
    OverlayHost host;
    std::vector<PointerEvent> pointer;
    std::vector<WheelEvent> wheel;
    std::vector<KeyEvent> keys;
    std::vector<bool> focus;
    std::vector<ClientMetrics> sizes;
    unsigned frames = 0, closes = 0;
    OverlayCallbacks callbacks;
    callbacks.frame = [&](double) { ++frames; };
    callbacks.pointer = [&](const auto& event) { pointer.push_back(event); return true; };
    callbacks.wheel = [&](const auto& event) { wheel.push_back(event); return true; };
    callbacks.key = [&](const auto& event) { keys.push_back(event); return true; };
    callbacks.focus = [&](bool value) { focus.push_back(value); };
    callbacks.resize = [&](const auto& value) { sizes.push_back(value); };
    callbacks.closeRequested = [&] { ++closes; host.hide(); };
    const auto cursorBefore = GetCursor();
    host.create({L"Endfield owned hidden host fixture", 0, 0, 128, 96, nullptr}, callbacks);
    const auto window = static_cast<HWND>(host.hwnd());
    check(window && IsWindow(window) && !IsWindowVisible(window), "owned host is created hidden");
    const auto style = GetWindowLongPtrW(window, GWL_STYLE);
    const auto extended = GetWindowLongPtrW(window, GWL_EXSTYLE);
    check((style & WS_POPUP) && !(style & WS_CAPTION), "borderless source composition owner");
    check((extended & WS_EX_NOREDIRECTIONBITMAP) && !(extended & WS_EX_LAYERED), "native DirectComposition transparency without a GDI bitmap");
    check(sizes.size() == 1 && sizes[0] == host.metrics(), "one initial logical client metrics callback");
    auto demand = visibleDemand(); demand.finiteAnimation = true;
    host.setFrameDemand(demand);
    for (unsigned i = 0; i < 1000; ++i) host.invalidate();
    drain(host);
    auto stats = host.stats();
    check(!stats.timerArmed && !stats.framePending && stats.timerArms == 0 && stats.framePosts == 0 && frames == 0,
        "hidden finite demand and invalidation flood schedule no work");
    for (unsigned i = 0; i < 4; ++i) host.pumpOnce(25);
    check(host.stats().timerWakes == 0 && frames == 0, "bounded actual message waits produce no hidden idle callbacks");
    const double scale = host.metrics().scale;
    SendMessageW(window, WM_MOUSEMOVE, MK_SHIFT | MK_CONTROL, MAKELPARAM(-12, 18));
    SendMessageW(window, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(20, 30));
    SendMessageW(window, WM_LBUTTONUP, 0, MAKELPARAM(20, 30));
    SendMessageW(window, WM_XBUTTONDBLCLK, MAKEWPARAM(MK_XBUTTON1, XBUTTON1), MAKELPARAM(21, 31));
    SendMessageW(window, WM_MOUSELEAVE, 0, 0);
    SendMessageW(window, WM_CAPTURECHANGED, 0, 0);
    check(pointer.size() == 6 && pointer[0].kind == PointerKind::move, "native own-window mouse routing");
    checkNear(pointer[0].x, -12 / scale, "negative pointer coordinate stays signed");
    checkNear(pointer[0].y, 18 / scale, "pointer converted to logical client space");
    check(pointer[0].modifiers == (MK_SHIFT | MK_CONTROL), "pointer modifier bits retained");
    check(pointer[1].kind == PointerKind::down && pointer[1].button == PointerButton::left &&
        pointer[2].kind == PointerKind::up && pointer[3].button == PointerButton::extra1 &&
        pointer[3].kind == PointerKind::doubleClick && pointer[4].kind == PointerKind::leave &&
        pointer[5].kind == PointerKind::captureLost, "buttons double-click leave and capture loss retain their meaning");
    POINT ownPoint{24, 36}; ClientToScreen(window, &ownPoint);
    SendMessageW(window, WM_MOUSEWHEEL, MAKEWPARAM(MK_SHIFT, 60), MAKELPARAM(ownPoint.x, ownPoint.y));
    SendMessageW(window, WM_MOUSEHWHEEL, MAKEWPARAM(0, static_cast<WORD>(-120)), MAKELPARAM(ownPoint.x, ownPoint.y));
    check(wheel.size() == 2 && !wheel[0].horizontal && wheel[1].horizontal, "native wheel axes retained");
    checkNear(wheel[0].x, 24 / scale, "wheel screen point becomes own client logical point");
    checkNear(wheel[0].y, 36 / scale, "wheel Y conversion");
    checkNear(wheel[0].steps, .5, "precision wheel fraction retained");
    checkNear(wheel[1].steps, -1, "horizontal wheel direction retained");
    SendMessageW(window, WM_KEYDOWN, 'A', 2 | (0x1e << 16) | (1ull << 30));
    SendMessageW(window, WM_KEYUP, 'A', 1 | (0x1e << 16) | (1ull << 31));
    SendMessageW(window, WM_CHAR, 0x4e2d, 1);
    SendMessageW(window, WM_UNICHAR, 0x1f600, 1);
    check(SendMessageW(window, WM_UNICHAR, UNICODE_NOCHAR, 0) == TRUE, "WM_UNICHAR capability query");
    check(keys.size() == 4 && keys[0].kind == KeyKind::down && keys[0].repeat == 2 &&
        keys[0].scanCode == 0x1e && keys[0].previouslyDown && keys[1].kind == KeyKind::up &&
        keys[2].kind == KeyKind::character && keys[2].value == 0x4e2d &&
        keys[3].kind == KeyKind::unicodeCharacter && keys[3].value == 0x1f600, "key and text payloads are not remapped");
    const auto beforeIME = keys.size();
    SendMessageW(window, WM_IME_ENDCOMPOSITION, 0, 0);
    check(keys.size() == beforeIME, "IME is not intercepted as a shortcut/key event");
    SendMessageW(window, WM_SETFOCUS, 0, 0);
    SendMessageW(window, WM_KILLFOCUS, 0, 0);
    check(focus == std::vector<bool>{true, false}, "focus routing without requesting foreground or keyboard focus");
    RECT suggested{4, 6, 244, 126};
    SendMessageW(window, WM_DPICHANGED, MAKEWPARAM(192, 192), reinterpret_cast<LPARAM>(&suggested));
    const auto dpi = host.metrics();
    check(dpi.dpi == 192 && dpi.pixelWidth == 240 && dpi.pixelHeight == 120 && dpi.scale == 2 &&
        dpi.width == 120 && dpi.height == 60, "DPI applies suggested own-window rectangle and logical scale");
    check(!IsWindowVisible(window), "DPI and resize do not show test window");
    host.setCursor(LoadCursorW(nullptr, IDC_CROSS));
    host.suspendCursor(true); host.suspendCursor(false);
    SendMessageW(window, WM_ENTERMENULOOP, 0, 0);
    SendMessageW(window, WM_EXITMENULOOP, 0, 0);
    SendMessageW(window, WM_ACTIVATEAPP, FALSE, 0);
    SendMessageW(window, WM_CLOSE, 0, 0);
    check(closes == 1 && IsWindow(window) && !IsWindowVisible(window), "close callback owns transition/hide without automatic destruction");
    stats = host.stats();
    check(stats.cursorSets == 0 && stats.cursorReleases == 0 && GetCursor() == cursorBefore,
        "hidden fixture never installs or changes the user cursor");
    check(!stats.timerArmed && !stats.framePending && frames == 0, "input and DPI never bypass hidden scheduling gate");
    bool wrongThread = false;
    std::thread other([&] { try { host.invalidate(); } catch (const std::logic_error&) { wrongThread = true; } });
    other.join(); check(wrongThread, "foreign-thread mutation rejected before HWND state access");
    host.requestStop(); check(!host.pumpOnce(0), "stop exits without posting process WM_QUIT");
    host.destroy();
    check(!IsWindow(window) && !host.hwnd() && !host.stats().timerArmed, "explicit destroy releases own window and wake handle");
    host.destroy();
    host.create({L"Recreated hidden host", 0, 0, 32, 32, nullptr});
    check(host.hwnd() && !IsWindowVisible(static_cast<HWND>(host.hwnd())), "same owner can create a clean hidden lifetime");
    host.destroy();
    for (unsigned i = 0; i < 12; ++i) {
        OverlayHost temporary; temporary.create({L"Bounded hidden teardown", 0, 0, 16, 16, nullptr});
        temporary.setFrameDemand(demand);
    }
}
void callbackFailures() {
    OverlayHost host;
    OverlayCallbacks callbacks;
    callbacks.pointer = [](const auto&) -> bool { throw std::runtime_error("owned fixture callback failure"); };
    host.create({L"Hidden callback failure", 0, 0, 16, 16, nullptr}, callbacks);
    SendMessageW(static_cast<HWND>(host.hwnd()), WM_MOUSEMOVE, 0, 0);
    bool propagated = false;
    try { host.pumpOnce(0); } catch (const std::runtime_error&) { propagated = true; }
    check(propagated && !host.stats().timerArmed && !host.stats().framePending, "callback exception stays inside USER32 and surfaces with scheduling canceled");
    host.destroy();

    // Destruction is explicitly permitted inside a callback. Keep a capture
    // alive until its body returns, even when destroy() releases the callback
    // set and create() installs a replacement during the same native dispatch.
    auto sentinel = std::make_shared<int>(73);
    const std::weak_ptr<int> weak = sentinel;
    bool captureSurvived = false;
    OverlayCallbacks replacing;
    replacing.pointer = [&, retained = sentinel](const auto&) {
        host.destroy();
        host.create({L"Hidden replacement inside callback", 0, 0, 16, 16, nullptr});
        captureSurvived = !weak.expired() && *retained == 73;
        return true;
    };
    host.create({L"Hidden captured callback", 0, 0, 16, 16, nullptr}, std::move(replacing));
    sentinel.reset();
    SendMessageW(static_cast<HWND>(host.hwnd()), WM_MOUSEMOVE, 0, 0);
    check(captureSurvived && weak.expired(), "callback capture survives HWND destroy/recreate and releases after dispatch");
    check(host.hwnd() && !IsWindowVisible(static_cast<HWND>(host.hwnd())), "callback replacement remains hidden");
    host.destroy();
}
#endif
}
int main() {
    try {
        scheduling(); coalescing();
#ifdef _WIN32
        nativeWindow(); callbackFailures();
        std::cout << checks << " source scheduling and hidden native host checks passed\n";
#else
        std::cout << checks << " portable source scheduling/coalescing checks passed; Win32 host tests require Windows\n";
#endif
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "After " << checks << " checks: " << error.what() << '\n'; return 1;
    }
}
