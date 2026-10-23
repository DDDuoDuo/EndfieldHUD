// Work Mode and Calendar behind the design owners app/application.cpp builds
// (tools::DesignModuleOwner over their canvases, the shared overlay owner's
// Context) on a hidden window and WARP: composition, idle frames that never
// rasterize, a running session whose one-second ticks repaint only the digits
// and at most one tick chunk, the controller's deadlines through the owner's
// service hooks while the HUD is closed (a countdown completes hidden), park
// on close, and the Calendar grid formatted by the native ICU civil context.
// Prints the residency and frame costs (the port's performance record).
// Synthetic controller and in-memory Calendar repository; no input injection,
// no visible window, no notification centre.
#include "tools/hud_design_owner.hpp"
#include "tools/hud_design_overlay_owner.hpp"
#include "modules/calendar_canvas.hpp"
#include "modules/work_mode_canvas.hpp"
#include "native/calendar_civil.hpp"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#include <algorithm>
#include <chrono>
#include <deque>
#include <iostream>
#include <stdexcept>
using namespace endfield;
namespace d = modules::design;
namespace m = modules;
namespace gpu = native;
namespace {
unsigned checks{};
void check(bool v, const char* s) { ++checks; if (!v) throw std::runtime_error(s); }
struct Window {
    HWND hwnd{};
    Window() {
        WNDCLASSW c{}; c.lpfnWndProc = DefWindowProcW; c.hInstance = GetModuleHandleW(nullptr); c.lpszClassName = L"EndfieldWorkCalendarOwnerHidden";
        check(RegisterClassW(&c) != 0, "Register owned fixture");
        hwnd = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP, c.lpszClassName, L"Synthetic", WS_POPUP, 0, 0, 1280, 800, nullptr, nullptr, c.hInstance, nullptr);
        check(hwnd && !IsWindowVisible(hwnd), "Synthetic window hidden");
    }
    ~Window() { if (hwnd) DestroyWindow(hwnd); UnregisterClassW(L"EndfieldWorkCalendarOwnerHidden", GetModuleHandleW(nullptr)); }
};
double ms(auto&& f) { const auto s = std::chrono::steady_clock::now(); f(); return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - s).count(); }
struct Repository final : m::CalendarRepository {
    m::CalendarFile value;
    bool exists() override { return !value.events.empty(); }
    m::CalendarFile load() override { return value; }
    void save(const m::CalendarFile& v) override { value = v; }
};
struct Harness {
    Window window;
    gpu::Renderer renderer;
    gpu::LayerRasterizer raster;
    gpu::LayerComposition composition;
    std::unique_ptr<tools::DesignOverlayOwner> overlay;
    core::ModulePresentation modules{core::Module::notes};
    core::source::DesktopChromeSettings chrome;
    core::Matrix4 center;
    double time{};
    explicit Harness(const std::filesystem::path& shader) {
        renderer.initialize(window.hwnd, 1280, 800, {gpu::Driver::warpForTests, shader, gpu::RenderTarget::offscreenForTests});
        renderer.setCamera(gpu::layerViewportProjection(1280, 800));
        tools::DesignOverlayOptions o; o.raster.pixelsPerPoint = 2; o.window = window.hwnd;
        overlay = std::make_unique<tools::DesignOverlayOwner>(raster, o);
        chrome.viewport = {0, 0, 1280, 800};
        center = core::source::DesktopChromeLayout::make(chrome, {}, {}).designToScreen;
        const app::ClientMetrics metrics{1280, 800, 96, 1, 1280, 800};
        overlay->resize(metrics);
        app::ModuleAppearance appearance; appearance.accentSRGB = {250. / 255, 212. / 255, 31. / 255};
        overlay->setAppearance(appearance, 0); overlay->setOverlayVisible(true, 0);
        modules.setDesignScreen(core::Module::workMode, true); modules.setDesignScreen(core::Module::calendar, true);
    }
    ~Harness() { composition.detach(renderer); overlay->release(renderer); }
    std::unique_ptr<tools::DesignModuleOwner> owner(std::string name, std::unique_ptr<d::Canvas> canvas, tools::DesignOwnerOptions o = {}) {
        o.name = std::move(name); o.raster.pixelsPerPoint = 2;
        auto core = std::make_unique<tools::DesignOwnerCore>(raster, overlay->context(), std::move(o));
        core->add(std::move(canvas));
        auto result = std::make_unique<tools::DesignModuleOwner>(std::move(core));
        result->resize({1280, 800, 96, 1, 1280, 800});
        app::ModuleAppearance appearance; appearance.accentSRGB = {250. / 255, 212. / 255, 31. / 255};
        result->setAppearance(appearance, time); result->setOverlayVisible(true, time);
        return result;
    }
    double frame(tools::DesignModuleOwner& owner, double advance = 1. / 60) {
        return ms([&] {
            time += advance;
            (void)overlay->deadline(time); (void)owner.deadline(time);
            const auto s = modules.sample(time).presentation;
            chrome.module = s.requested;
            const app::ModuleFrame f{center, chrome, s, 1, time, true};
            overlay->update(f); owner.update(f);
            overlay->upload(renderer); owner.upload(renderer);
            std::vector<gpu::LayerCompositionEntry> entries;
            for (const auto& e : owner.entries(s.current.module)) entries.push_back(e);
            if (s.incoming) for (const auto& e : owner.entries(s.incoming->module)) entries.push_back(e);
            for (const auto& e : overlay->modalEntries()) entries.push_back(e);
            composition.setEntries(renderer, entries);
            composition.present(renderer);
            renderer.draw(false);
            owner.collected(renderer); overlay->collected(renderer);
        });
    }
    void settle(tools::DesignModuleOwner& owner, unsigned frames = 60) { for (unsigned n = 0; n < frames; ++n) frame(owner); }
};

void workMode(Harness& h) {
    m::WorkModeCanvas* shown{};
    m::WorkModeController controller({[&] { if (shown) shown->controllerChanged(); }, {}});
    tools::DesignOwnerOptions o;
    o.nextWakeTime = [&](double) { return controller.nextDeadline(); };
    o.deadline = [&](double t) { const auto r = controller.notificationRevision(); controller.wake(t); return r != controller.notificationRevision(); };
    m::WorkModeCanvasOptions co; co.clock = [&] { return h.time; };
    auto canvas = std::make_unique<m::WorkModeCanvas>(h.overlay->context(), controller, std::move(co));
    auto& work = *canvas; shown = canvas.get();
    auto owner = h.owner("workMode", std::move(canvas), std::move(o));
    h.modules.select(core::Module::workMode, h.time);
    const auto raster0 = h.raster.stats(); const auto gpu0 = h.renderer.stats().resourceBytes;
    const double first = h.frame(*owner);
    h.settle(*owner);
    auto scene = owner->core().sceneStats(core::Module::workMode);
    check(scene && scene->surfaces > 0 && !owner->requiresFrames(h.time), "Work Mode composes and settles (no frames while idle)");
    const auto shownRaster = h.raster.stats();
    double idle{};
    for (unsigned n = 0; n < 60; ++n) idle += h.frame(*owner);
    check(h.raster.stats().rasterizations == shownRaster.rasterizations, "Idle Work Mode frames rasterize nothing");
    // Pointer moves across the presets and actions: hover is numbers (lazy hover surfaces paint once).
    const auto row = owner->core().clientRect({145, 331, 300, 82});
    check(row.has_value(), "The presets project to the client");
    const auto moved0 = h.raster.stats().rasterizations;
    double moves{};
    for (unsigned n = 0; n < 120; ++n) {
        const double x = row->x + row->width * double(n % 60) / 60, y = row->y + row->height * (n < 60 ? .2 : .8);
        moves += ms([&] { (void)owner->pointer({app::PointerKind::move, app::PointerButton::none, x, y}, h.time); }) + h.frame(*owner);
    }
    const auto moved = h.raster.stats().rasterizations - moved0;
    const auto repeat0 = h.raster.stats().rasterizations;
    for (unsigned n = 0; n < 120; ++n) {
        const double x = row->x + row->width * double(n % 60) / 60, y = row->y + row->height * (n < 60 ? .2 : .8);
        (void)owner->pointer({app::PointerKind::move, app::PointerButton::none, x, y}, h.time); h.frame(*owner);
    }
    check(h.raster.stats().rasterizations - repeat0 <= moved, "Repeated hover passes rasterize no more than the first");
    (void)owner->pointer({app::PointerKind::leave, app::PointerButton::none, 0, 0}, h.time);
    h.settle(*owner, 30);
    // A running session: the blink is the one live loop; each second repaints the digits and at most one tick chunk.
    work.primaryAction(); h.frame(*owner);
    check(owner->requiresFrames(h.time) && controller.snapshot(h.time).phase == m::WorkModePhase::running, "A running session blinks its state square");
    h.settle(*owner, 40);
    const auto running0 = h.raster.stats().rasterizations;
    double running{};
    for (unsigned n = 0; n < 180; ++n) running += h.frame(*owner);
    const double perSecond = double(h.raster.stats().rasterizations - running0) / 3;
    check(perSecond >= 1 && perSecond <= 3, "A second repaints the digits (and the tick chunk that changed) only");
    // Hidden: the countdown keeps its completion deadline through the owner's service and completes.
    const auto deadline = controller.completionDeadline();
    owner->overlayClosing(h.time); h.overlay->overlayClosing(h.time);
    check(deadline && owner->nextWakeTime(h.time) == deadline, "The closed HUD keeps the countdown's completion deadline (service hook)");
    h.time = *deadline + .01;
    check(owner->deadline(h.time) && controller.snapshot(h.time).phase == m::WorkModePhase::completed, "A countdown completes while the HUD is closed");
    // Reopen, switch to another module, close there: the page keeps no artwork.
    h.overlay->setOverlayVisible(true, h.time); owner->setOverlayVisible(true, h.time);
    h.settle(*owner, 20);
    h.modules.select(core::Module::notes, h.time);
    h.settle(*owner, 40);
    owner->overlayClosing(h.time); h.overlay->overlayClosing(h.time);
    (void)owner->hostEvent({app::HostEvent::Kind::overlayConcealed}, h.time);
    h.frame(*owner);
    scene = owner->core().sceneStats(core::Module::workMode);
    check(scene && scene->residentPixelBytes == 0 && scene->parks >= 1, "Closing the HUD on another module parks the Work Mode artwork");
    std::cout << "PERF workMode firstFrameMs=" << first << " surfaces=" << owner->core().sceneStats(core::Module::workMode)->surfaces
              << " rasterEntriesShown=" << shownRaster.entries - raster0.entries << " rasterMBShown=" << double(shownRaster.resourceBytes - raster0.resourceBytes) / 1048576
              << " rendererMBShown=" << double(h.renderer.stats().resourceBytes - gpu0) / 1048576 << " idleFrameMs=" << idle / 60
              << " pointerMoveMs=" << moves / 120 << " firstHoverPassRasterizations=" << moved
              << " runningFrameMs=" << running / 180 << " runningRasterizationsPerSecond=" << perSecond << '\n';
    owner->release(h.renderer);
    shown = nullptr;
}

void calendar(Harness& h, const std::filesystem::path&) {
    auto repository = std::make_shared<Repository>();
    const double now = *m::calendarUTC().timestamp({2026, 10, 4}, 10, 0);
    for (int n = 1; n <= 4; ++n) {
        m::CalendarEvent e; char id[48]; std::snprintf(id, sizeof id, "C0000000-0000-4000-8000-%012d", n);
        e.id = id; e.title = "Synthetic event " + std::to_string(n); e.details = "Owned fixture"; e.day = {2026, 10, n < 3 ? 4 : 10 + n}; e.created = e.modified = now + n;
        repository->value.events.push_back(e);
    }
    std::deque<std::pair<std::function<void()>, std::function<void(std::exception_ptr)>>> jobs;
    const auto drain = [&] { while (!jobs.empty()) { auto j = std::move(jobs.front()); jobs.pop_front(); std::exception_ptr e; try { j.first(); } catch (...) { e = std::current_exception(); } j.second(e); } };
    gpu::CalendarCivilContext civil(u"UTC", "en_US");
    m::CalendarCanvas* shown{};
    m::CalendarStateOptions so;
    so.text = gpu::nativeCalendarTextRules();
    so.now = [&] { return now; }; so.zone = [&] { return civil.timeZone(); }; so.newID = [] { return std::string("C0000000-0000-4000-8000-000000000099"); };
    so.scheduling.authorization = [](bool, auto done) { done(m::CalendarPermission::authorized); };
    so.scheduling.reconcile = [](auto, auto, auto done) { done({}); };
    so.scheduling.cancelObsolete = [](auto, auto done) { done(); };
    so.changed = [&] { if (shown) shown->controllerChanged(); };
    m::CalendarState state(repository, {[&](auto work, auto done) { jobs.push_back({std::move(work), std::move(done)}); return true; }}, std::move(so));
    m::CalendarCanvasOptions co; co.formats = civil.formats(); co.text = gpu::nativeCalendarTextRules();
    auto canvas = std::make_unique<m::CalendarCanvas>(h.overlay->context(), state, std::move(co));
    auto& cal = *canvas; shown = canvas.get();
    auto owner = h.owner("calendar", std::move(canvas));
    h.modules.select(core::Module::calendar, h.time);
    const auto raster0 = h.raster.stats(); const auto gpu0 = h.renderer.stats().resourceBytes;
    const double first = h.frame(*owner);
    drain(); h.frame(*owner); drain();
    h.settle(*owner);
    check(state.loaded() && state.events().size() == 4 && cal.eventsForSelectedDay().size() == 2, "The Calendar loads through its FIFO and shows the day's events");
    std::vector<d::AccessibilityItem> items; cal.accessibilityItems(items);
    const auto title = std::find_if(items.begin(), items.end(), [](const auto& i) { return i.id == "calendar.monthPicker"; });
    check(title != items.end() && title->label == "October 2026" && civil.format({2026, 10, 4}, "yMMMMd", core::Language::japanese) == "2026年10月4日",
        "The month title and day labels come from the native ICU formats (DateFormatter templates)");
    const auto shownRaster = h.raster.stats();
    double idle{};
    for (unsigned n = 0; n < 60; ++n) idle += h.frame(*owner);
    check(h.raster.stats().rasterizations == shownRaster.rasterizations && !owner->requiresFrames(h.time), "Idle Calendar frames rasterize nothing");
    // The month arrow navigates (sub-page swap) and settles.
    check(cal.performAccessibilityAction("calendar.nextMonth", h.time), "Next month through its accessible control");
    double swap{};
    for (unsigned n = 0; n < 90; ++n) swap += h.frame(*owner);
    check(cal.month() == m::CalendarDay{2026, 11, 1} && !owner->requiresFrames(h.time), "The month swap lands and settles");
    h.modules.select(core::Module::notes, h.time);
    h.settle(*owner, 40);
    owner->overlayClosing(h.time); h.overlay->overlayClosing(h.time);
    (void)owner->hostEvent({app::HostEvent::Kind::overlayConcealed}, h.time);
    h.frame(*owner);
    const auto scene = owner->core().sceneStats(core::Module::calendar);
    check(scene && scene->residentPixelBytes == 0 && scene->parks >= 1, "Closing the HUD on another module parks the Calendar artwork");
    std::cout << "PERF calendar firstFrameMs=" << first << " rasterEntriesShown=" << shownRaster.entries - raster0.entries
              << " rasterMBShown=" << double(shownRaster.resourceBytes - raster0.resourceBytes) / 1048576
              << " rendererMBShown=" << double(h.renderer.stats().resourceBytes - gpu0) / 1048576 << " idleFrameMs=" << idle / 60
              << " monthSwapFrameMs=" << swap / 90 << '\n';
    owner->release(h.renderer);
    shown = nullptr;
}
}

int wmain(int argc, wchar_t** argv) {
    const auto hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    try {
        check(argc == 2, "Pass native/hud.hlsl");
        { Harness h(argv[1]); workMode(h); }
        { Harness h(argv[1]); calendar(h, argv[1]); }
        std::cout << "PASS " << checks << " Work Mode / Calendar owner checks\n";
        if (SUCCEEDED(hr)) CoUninitialize();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL after " << checks << ": " << e.what() << '\n';
        if (SUCCEEDED(hr)) CoUninitialize();
        return 1;
    }
}
#else
int main() { return 0; }
#endif
