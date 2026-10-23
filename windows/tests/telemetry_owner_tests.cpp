// Storage, Activity Monitor and Battery through their design owners
// (tools/activity_owner, tools/storage_owner, the host's Battery owner shape)
// on a hidden window and WARP: composition, readings arriving in the shared
// executor's drain (a frame for the changed page, surfaces updated in place),
// the app-icon loads, the disk read / menu / choice / remembered key, the
// charging pulse as the only live loop, settled pages asking for no frames,
// UI Automation and parking on close. Synthetic readings, apps, icons and
// disks only (no volume, process, icon or battery of this machine is read);
// PostMessage-free; no input injection, no visible window.
#include "modules/battery_canvas.hpp"
#include "tools/activity_owner.hpp"
#include "tools/hud_design_overlay_owner.hpp"
#include "tools/storage_owner.hpp"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <mutex>
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
        WNDCLASSW c{}; c.lpfnWndProc = DefWindowProcW; c.hInstance = GetModuleHandleW(nullptr); c.lpszClassName = L"EndfieldTelemetryOwnerHidden";
        check(RegisterClassW(&c) != 0, "Register owned fixture");
        hwnd = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP, c.lpszClassName, L"Synthetic", WS_POPUP, 0, 0, 1000, 640, nullptr, nullptr, c.hInstance, nullptr);
        check(hwnd && !IsWindowVisible(hwnd), "Synthetic window hidden");
    }
    ~Window() { if (hwnd) DestroyWindow(hwnd); UnregisterClassW(L"EndfieldTelemetryOwnerHidden", GetModuleHandleW(nullptr)); }
};
core::ModulePresentationSample sample(core::Module module) {
    core::ModulePresentationSample s;
    s.selected = s.requested = module;
    s.current.module = module; s.current.contentFrame = core::moduleLocalContentFrame(module);
    s.current.designScreen = true; s.current.hostClip = false; s.current.attached = true; s.current.opacity = 1;
    s.acceptsModuleInput = true;
    return s;
}
m::ActivitySnapshot reading(double t, double cpu) {
    m::ActivitySnapshot s; s.timestamp = t; s.uptime = t; s.cpuPercent = cpu; s.gpuPercent = 20 + cpu / 4;
    s.memory = m::ActivityMemory{6000000000ULL, 16000000000ULL, {}}; s.upload = 1000 * cpu; s.download = 40000; s.diskRead = 5000; s.diskWrite = 3000;
    return s;
}
m::StorageVolume disk(std::string id, std::string name, std::int64_t total, std::int64_t available, bool startup) {
    return {std::move(id), startup ? "C:\\" : "D:\\", !startup, startup, {std::move(name), total, available, 1}};
}

void run(const std::filesystem::path& shader) {
    Window window;
    gpu::Renderer renderer;
    renderer.initialize(window.hwnd, 1000, 640, {gpu::Driver::warpForTests, shader, gpu::RenderTarget::offscreenForTests});
    gpu::LayerRasterizer raster;
    gpu::LayerComposition composition;
    app::UtilityExecutor executor([] {});
    tools::DesignOverlayOptions overlayOptions;
    overlayOptions.raster.pixelsPerPoint = 1; overlayOptions.window = window.hwnd;
    tools::DesignOverlayOwner overlay(raster, overlayOptions);
    auto& context = overlay.context();
    unsigned presented{};
    const auto options = [&](std::string name, const app::ModuleRouting& routing) {
        tools::DesignOwnerOptions o; o.name = std::move(name); o.routing = routing; o.raster.pixelsPerPoint = 1;
        o.changed = [&] { ++presented; };
        return o;
    };

    // --- Activity Monitor: synthetic readings, apps and icons --------------------------
    std::vector<std::pair<bool, bool>> demand;
    tools::ActivityServiceOptions activityOptions;
    activityOptions.compareNames = [](std::string_view a, std::string_view b) { return a.compare(b); };
    activityOptions.initial = reading(1, 10);
    activityOptions.apps.timestamp = 1; activityOptions.apps.uptime = 1;
    for (unsigned n = 0; n < 14; ++n) {
        m::ActivityApp a; a.identity = {"app" + std::to_string(n), "Synthetic " + std::to_string(n), n % 3 ? "fixture-icon-" + std::to_string(n) : "", {100 + n}};
        a.cpuPercent = double(n * 7 % 11); a.memoryBytes = 100000000ULL * (n + 1); a.gpuPercent = double(n % 4); a.threads = 6 + n;
        activityOptions.apps.items.push_back(std::move(a));
    }
    activityOptions.demandChanged = [&](bool system, bool apps, double) { demand.push_back({system, apps}); };
    std::mutex loadsMutex; unsigned loads{}; std::vector<std::string> loadedKeys;
    activityOptions.iconExecutor = &executor;
    activityOptions.iconLoader = [&](const std::string& key, unsigned pixels) -> std::optional<gpu::ActivityIconPixels> {
        { std::lock_guard lock(loadsMutex); ++loads; loadedKeys.push_back(key); }
        if (key.ends_with("4")) return std::nullopt; // unavailable artwork: the generic tile
        gpu::ActivityIconPixels icon{pixels, pixels, std::vector<std::uint8_t>(std::size_t(pixels) * pixels * 4, 0)};
        for (std::size_t k = 0; k < icon.straightRGBA.size(); k += 4) { icon.straightRGBA[k] = 40; icon.straightRGBA[k + 1] = 160; icon.straightRGBA[k + 2] = 220; icon.straightRGBA[k + 3] = 255; }
        return icon;
    };
    tools::ActivityService activity(std::move(activityOptions));
    auto activityOwner = activity.makeOwner(raster, context, options("activityMonitor", tools::ActivityService::routing()));
    auto& activityCore = static_cast<tools::DesignModuleOwner&>(*activityOwner).core();

    // --- Storage: a synthetic read on the shared executor -------------------------------
    std::mutex disksMutex; std::vector<m::StorageVolume> disks{disk("sys", "System", 1000000000000, 350000000000, true), disk("data", "Data", 2000000000000, 1500000000000, false)};
    unsigned reads{}, settings{};
    std::string remembered;
    tools::StorageServiceOptions storageOptions;
    storageOptions.utility = &executor;
    storageOptions.read = [&](const std::string& startupName) {
        std::lock_guard lock(disksMutex); ++reads;
        m::StorageCapacityResult result; result.volumes = disks;
        if (disks.empty()) result.startup = m::StorageCapacity{startupName, 100, 50, 1};
        return result;
    };
    storageOptions.startupName = [] { return std::string("Startup disk"); };
    double clock{};
    storageOptions.clock = [&] { return clock; };
    storageOptions.openSettings = [&] { ++settings; };
    storageOptions.loadVolume = [&] { return remembered; };
    storageOptions.saveVolume = [&](const std::string& id) { remembered = id; };
    tools::StorageService storage(std::move(storageOptions));
    auto storageOwner = storage.makeOwner(raster, context, options("storage", tools::StorageService::routing()));
    auto& storageCore = static_cast<tools::DesignModuleOwner&>(*storageOwner).core();

    // --- Battery: the host's owner shape (app/application.cpp constructBattery) ----------
    auto batteryCanvas = std::make_unique<m::BatteryCanvas>(context);
    auto* battery = batteryCanvas.get();
    unsigned alerts{};
    battery->onOpenBatteryAlert = [&] { ++alerts; };
    app::ModuleRouting batteryRouting; batteryRouting.pointer = 14;
    auto batteryCore = std::make_unique<tools::DesignOwnerCore>(raster, context, options("power", batteryRouting));
    batteryCore->add(std::move(batteryCanvas));
    tools::DesignModuleOwner batteryOwner(std::move(batteryCore));

    std::vector<app::ModuleOwner*> owners{&overlay, activityOwner.get(), storageOwner.get(), &batteryOwner};
    // A failed check unwinds through here: detach the composition while the
    // owners' surfaces it lists still exist.
    struct Detach {
        gpu::LayerComposition& composition; gpu::Renderer& renderer; bool done{};
        ~Detach() { if (!done) composition.detach(renderer); }
    } detach{composition, renderer};
    const app::ClientMetrics metrics{1000, 640, 96, 1, 1000, 640};
    app::ModuleAppearance appearance; appearance.accentSRGB = {250. / 255, 212. / 255, 31. / 255};
    for (auto* o : owners) { o->resize(metrics); o->setAppearance(appearance, 0); o->setOverlayVisible(true, 0); }
    core::source::DesktopChromeSettings chrome; chrome.viewport = {0, 0, 1000, 640};
    const auto center = core::Matrix4{};
    const auto frame = [&](core::Module module, double t) {
        clock = t;
        chrome.module = module;
        const auto s = sample(module);
        const app::ModuleFrame f{center, chrome, s, 1, t, true};
        for (auto* o : owners) { o->update(f); o->upload(renderer); }
        std::vector<gpu::LayerCompositionEntry> entries;
        for (auto* o : owners) if (o != &overlay) for (const auto& e : o->entries(module)) entries.push_back(e);
        for (const auto& e : overlay.modalEntries()) entries.push_back(e);
        composition.setEntries(renderer, entries);
        composition.present(renderer);
        renderer.draw(false);
        for (auto* o : owners) o->collected(renderer);
        return entries.size();
    };
    const auto settle = [&](core::Module module, double from, double seconds) {
        double t = from;
        for (; t < from + seconds; t += 1. / 30) { for (auto* o : owners) (void)o->deadline(t); frame(module, t); }
        return t;
    };
    const auto drain = [&](double t) {
        executor.waitIdle(); executor.drain();
        bool changed{};
        for (auto* o : owners) changed = o->hostEvent({app::HostEvent::Kind::utilityCompleted, 0}, t) || changed;
        return changed;
    };
    const auto requiresFrames = [&](double t) { for (auto* o : owners) if (o->requiresFrames(t)) return true; return false; };

    // --- Activity -------------------------------------------------------------------------
    check(frame(core::Module::activityMonitor, 0) > 0 && activity.canvas() && activity.canvas()->isActive(), "Activity composes and activates");
    check(demand.size() == 1 && demand.back() == std::pair{true, false}, "Shown, Activity asks for 1 Hz system samples");
    double t = settle(core::Module::activityMonitor, .02, 1.2);
    check(!requiresFrames(t), "A settled Overview asks for no frames (no telemetry loop)");
    const auto before = *activityCore.sceneStats(core::Module::activityMonitor);
    const auto rasterBefore = raster.stats().rasterizations;
    activity.providerState().receive(reading(2, 55));
    check(drain(t) && !drain(t), "A sample delivered in the drain presents one frame for the changed page");
    frame(core::Module::activityMonitor, t + .01);
    const auto after = *activityCore.sceneStats(core::Module::activityMonitor);
    check(after.surfacesUpdated > before.surfacesUpdated && after.partsLoaded == before.partsLoaded && raster.stats().rasterizations > rasterBefore,
        "A sample re-rasterizes only the changed graph and readout surfaces");
    check(activityOwner->automationRefresh(nullptr) && activityOwner->automationCount() >= 7, "The rows and tabs are listed for UI Automation");
    check(activity.canvas()->perform("activity:apps", t + .02) && demand.back() == std::pair{true, true}, "The Apps tab asks for app readings");
    t = settle(core::Module::activityMonitor, t + .03, .8);
    check(drain(t) && loads > 0, "Missing icons load on the shared executor; their arrival presents a frame");
    t = settle(core::Module::activityMonitor, t, .3);
    (void)drain(t); t = settle(core::Module::activityMonitor, t, .2); (void)drain(t);
    const auto iconStats = activity.iconStats();
    check(iconStats && iconStats->loaded >= 1 && iconStats->unavailable >= 1 && iconStats->images == iconStats->loaded, "Loaded icons are published; unavailable artwork is not");
    check(activity.canvas()->perform("activity:sort:name", t) && activity.canvas()->perform("activity:sort:cpu", t + .01), "Sorting re-binds the rows");
    t = settle(core::Module::activityMonitor, t + .02, .8);
    (void)drain(t); t = settle(core::Module::activityMonitor, t, .3); (void)drain(t);
    {
        // Rows that show other apps load theirs; an app's icon (or its
        // unavailable artwork) is never loaded twice.
        std::lock_guard lock(loadsMutex);
        auto keys = loadedKeys; std::sort(keys.begin(), keys.end());
        check(std::adjacent_find(keys.begin(), keys.end()) == keys.end(), "Cached icons are kept across sorts (no second load)");
    }
    check(!requiresFrames(t), "Settled Apps rows ask for no frames");

    // --- Storage --------------------------------------------------------------------------
    t += .1;
    check(frame(core::Module::storage, t) > 0 && storage.controller().active() && demand.back() == std::pair{false, false}, "Switching to Storage stops Activity's demand and reads the disks");
    check(drain(t) && reads == 1 && storage.controller().volumes().size() == 2, "The read lists the disks; its completion presents a frame");
    t = settle(core::Module::storage, t, 1);
    auto* storagePage = storage.canvas();
    check(storagePage->header().counterText() == "65.0%", "The startup disk's used share");
    check(storagePage->performAccessibilityAction("volume", t) && storagePage->menuOpen() && storageOwner->capturesPointer(), "The disk menu opens and captures the pointer");
    t = settle(core::Module::storage, t, .3);
    check(storagePage->performAccessibilityAction("volume:data", t) && remembered == "data", "Choosing a disk remembers it");
    t = settle(core::Module::storage, t, .5);
    check(drain(t) && storagePage->header().counterText() == "25.0%", "The chosen disk's fresh read updates the page");
    // The remembered key restored to defaults (settingsChanged) shows the startup disk again.
    remembered.clear();
    check(storageOwner->hostEvent({app::HostEvent::Kind::settingsChanged, 0}, t) && !storage.controller().selectedVolume(), "Restore Defaults forgets the disk");
    (void)drain(t); t = settle(core::Module::storage, t, .4);
    check(storagePage->header().counterText() == "65.0%", "...and the startup disk shows again");
    // A volume removal re-lists (WM_DEVICECHANGE).
    { std::lock_guard lock(disksMutex); disks.pop_back(); }
    const auto readsBefore = reads;
    check(storage.volumesChanged(t) && drain(t) && reads == readsBefore + 1 && storage.controller().volumes().size() == 1, "A mount event re-lists the disks");
    check(storagePage->perform("storage:settings", t) && settings == 1, "Storage Settings hands off to the host");
    t = settle(core::Module::storage, t, 2);
    check(!requiresFrames(t) && storageCore.nextWakeTime(t).has_value(), "Settled Storage asks for no frames; its 60 s read timer stays");

    // --- Battery -------------------------------------------------------------------------
    m::BatteryReading r; r.present = true; r.percentage = 48; r.pluggedIn = true; r.charging = true; r.cycleCount = 210;
    battery->update(r, m::ChargeMetric::battery, t);
    t += .1;
    check(frame(core::Module::power, t) > 0 && battery->isActive() && battery->liveAnimationCount() == 3, "Charging: the next three cells pulse while Battery shows");
    t = settle(core::Module::power, t, 1);
    check(requiresFrames(t), "The pulse is the one live loop");
    check(battery->performAccessibilityAction("battery.reading", t) && alerts == 1, "BAR READING opens the Battery alert settings");
    r.charging = false; r.pluggedIn = false; r.minutesToEmpty = 95;
    battery->update(r, m::ChargeMetric::battery, t);
    t = settle(core::Module::power, t, .6);
    check(!requiresFrames(t) && battery->liveAnimationCount() == 0, "On battery nothing animates");

    // --- Close on another module: no artwork kept ----------------------------------------
    for (auto* o : owners) o->setOverlayVisible(false, t);
    frame(core::Module::notes, t);
    for (auto* o : owners) (void)o->hostEvent({app::HostEvent::Kind::overlayConcealed, 0}, t);
    frame(core::Module::notes, t + .01);
    const auto activityScene = *activityCore.sceneStats(core::Module::activityMonitor);
    const auto storageScene = *storageCore.sceneStats(core::Module::storage);
    check(activityScene.residentPixelBytes == 0 && activityScene.parks >= 1 && storageScene.residentPixelBytes == 0 && storageScene.parks >= 1,
        "Closed on another module, Activity and Storage keep no raster artwork");
    check(!storage.controller().active() && demand.back() == std::pair{false, false}, "Closing stops every reader");
    composition.detach(renderer); detach.done = true;
    for (auto* o : owners) o->release(renderer);
    check(!IsWindowVisible(window.hwnd) && renderer.stats().presents == 0, "The fixture never shows a window");
    (void)presented;
}
}
int wmain(int argc, wchar_t** argv) {
    const auto com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    try {
        check(SUCCEEDED(com) && argc == 2, "Pass the shader");
        run(argv[1]);
        CoUninitialize();
        std::cout << "Telemetry owners: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        if (SUCCEEDED(com)) CoUninitialize();
        std::cerr << "Telemetry owners after " << checks << ": " << e.what() << '\n';
        return 1;
    }
}
#else
int main() { return 0; }
#endif
