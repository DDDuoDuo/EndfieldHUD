// Contract suite of the facility area owner (native/facility_owner.hpp) over
// an in-memory ApplicationHost, the shared overlay owner's design Context, a
// hidden window and WARP. Synthetic readings only (no disk, memory, battery,
// calendar, settings file or Event Log of this machine): the daily boot log
// (the day consumed at the first opening, the deferred deploy entrance and
// the concealed centre, the lines from the published sources, any key skips,
// the entrance from below and the checks at the end, no log on the next
// opening, Reduce Motion), the monitor's cadence on the host's drains and its
// own fallback wake, the banner flat at the top and the tags on the core plane
// composed and pressed through their projections (Storage opens, x dismisses),
// the settings switch, closing and release. No input injection.
#include "native/facility_owner.hpp"
#ifdef _WIN32
#include "tools/hud_design_overlay_owner.hpp" // before <windows.h>: rpcndr.h defines `small`
#include "app/application_host.hpp"
#include "core/data/data_store.hpp"
#include "core/source_desktop_chrome.hpp"
#include "native/layer_scene.hpp"
#include "native/renderer.hpp"
#include <windows.h>
#include <objbase.h>
#include <dbt.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <map>
#include <stdexcept>
#include <typeinfo>

namespace {
using namespace endfield;
namespace a = app;
namespace c = core;
namespace m = modules;
namespace b = modules::boot;
namespace gpu = native;
unsigned checks{};
void check(bool v, const std::string& s) { ++checks; if (!v) throw std::runtime_error(s); }
constexpr unsigned width = 1280, height = 800;
struct Window {
    HWND h{};
    Window() {
        WNDCLASSW k{}; k.lpfnWndProc = DefWindowProcW; k.hInstance = GetModuleHandleW(nullptr); k.lpszClassName = L"EndfieldFacilityOwnerHidden";
        check(RegisterClassW(&k) != 0, "Register the hidden fixture window");
        h = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP, k.lpszClassName, L"Synthetic", WS_POPUP, 0, 0, width, height, nullptr, nullptr, k.hInstance, nullptr);
        check(h && !IsWindowVisible(h), "The fixture window stays hidden");
    }
    ~Window() { if (h) DestroyWindow(h); UnregisterClassW(L"EndfieldFacilityOwnerHidden", GetModuleHandleW(nullptr)); }
};
class FakeHost final : public a::ApplicationHost {
public:
    FakeHost(gpu::Renderer& r, gpu::LayerRasterizer& raster) : renderer_(r), raster_(raster) {}
    double clock{};
    ehud::data::Settings configuration = ehud::data::Settings::defaults();
    a::ModuleAppearance look;
    c::Module selected{c::Module::clipboard};
    std::optional<c::SystemOverlayPhase> phase;
    bool visible{};
    a::ClientMetrics metrics_{width, height, 96, 1, width, height};
    unsigned frames{}, settingsUpdates{};
    std::vector<c::Module> chosen;
    a::ApplicationAreaMode mode() const noexcept override { return a::ApplicationAreaMode::hiddenCoverage; }
    double now() const override { return clock; }
    const ehud::data::Settings& settings() const noexcept override { return configuration; }
    const a::ModuleAppearance& appearance() const noexcept override { return look; }
    c::Module selectedModule() const noexcept override { return selected; }
    c::SystemOverlayPhase overlayPhase() const noexcept override { return phase.value_or(visible ? c::SystemOverlayPhase::open : c::SystemOverlayPhase::closed); }
    bool overlayVisible() const noexcept override { return visible; }
    bool focused() const noexcept override { return true; }
    const a::ClientMetrics& metrics() const noexcept override { return metrics_; }
    gpu::Renderer& renderer() noexcept override { return renderer_; }
    gpu::LayerRasterizer& rasterizer() noexcept override { return raster_; }
    a::UtilityExecutor* utility() noexcept override { return nullptr; }
    gpu::SystemServices* systemServices() noexcept override { return nullptr; }
    gpu::AudioRuntime* audio() noexcept override { return nullptr; }
    gpu::NativeMediaRequestBroker* mediaBroker() noexcept override { return nullptr; }
    const m::WorkModeController* workMode() const noexcept override { return nullptr; }
    c::ApplicationEventRecorder* eventRecorder() noexcept override { return nullptr; }
    void requestFrame() override { ++frames; }
    void recordEvent(m::EventKind, m::EventMetadata) override {}
    void selectModule(c::Module module) override { selected = module; chosen.push_back(module); }
    void openModule(c::Module) override {}
    void closeOverlay() override {}
    bool updateSettings(const std::function<void(ehud::data::Settings&)>& mutate) override { mutate(configuration); ++settingsUpdates; return true; }
    bool chooseFiles(const a::ApplicationFileRequest&, std::function<void(a::ApplicationFileResult)>) override { return false; }
    void cancelFileChoice() override {}
    std::uint32_t registerMessage() override { return WM_APP + 0x500; }
protected:
    void publishService(std::string_view key, void* value, const std::type_info& type) override {
        if (!value) { if (const auto found = services_.find(key); found != services_.end()) services_.erase(found); return; }
        services_[std::string(key)] = {value, &type};
    }
    void* findService(std::string_view key, const std::type_info& type) const override {
        const auto found = services_.find(key);
        return found == services_.end() || *found->second.second != type ? nullptr : found->second.first;
    }
private:
    gpu::Renderer& renderer_;
    gpu::LayerRasterizer& raster_;
    std::map<std::string, std::pair<void*, const std::type_info*>, std::less<>> services_;
};
c::ModulePresentationSample sample(c::Module module) {
    c::ModulePresentationSample s;
    s.selected = s.requested = module;
    s.current.module = module; s.current.designScreen = true; s.current.hostClip = false;
    return s;
}
// A layer-local point through a plane world -> logical client point (the owner's projection).
std::optional<c::Point> client(const c::Matrix4& world, c::Point local) {
    const auto camera = gpu::layerViewportProjection(width, height);
    const auto projection = c::Projection::viewport(camera * world, width, height);
    return projection.project(local);
}

void run(const std::filesystem::path& shader) {
    Window window;
    gpu::Renderer renderer;
    renderer.initialize(window.h, width, height, {gpu::Driver::warpForTests, shader, gpu::RenderTarget::offscreenForTests});
    gpu::LayerRasterizer raster;
    gpu::LayerComposition composition;
    tools::DesignOverlayOptions overlayOptions;
    overlayOptions.raster.pixelsPerPoint = 1; overlayOptions.window = window.h;
    tools::DesignOverlayOwner overlay(raster, overlayOptions);
    FakeHost host(renderer, raster);
    host.look.language = c::Language::english; host.look.accentSRGB = {250. / 255, 212. / 255, 31. / 255};
    // The host's boot-log sources (battery, today's events).
    b::BootLogSources sources;
    sources.battery = [] { return std::optional(b::BootLogBattery{82, true, b::BootLogBattery::Level::green}); };
    sources.eventsToday = [](double) { return std::vector<std::string>{"Stand-up"}; };
    host.publish<b::BootLogSources>(b::bootLogSourcesKey, sources);
    // Synthetic readers: the disk's free bytes and the memory in use are the test's.
    std::int64_t free = 500'000'000'000;
    std::uint64_t used = 8'000'000'000;
    unsigned diskReads{}, changes{};
    b::BootLogDate today{2026, 10, 23, 9, 30};
    facility::FacilityOwnerOptions options;
    options.raster.pixelsPerPoint = 1; options.window = window.h; options.changed = [&] { ++changes; };
    options.sources.disk = [&](std::string_view) { ++diskReads; return m::FacilityDiskReading::make("Local Disk", 1'000'000'000'000, free); };
    options.sources.memory = [&] { return std::optional(gpu::WindowsFacilityMemory{used, 16'000'000'000, false}); };
    options.sources.localDate = [&] { return today; };
    options.sources.uptime = [] { return 90'061.0; };
    options.sources.localDay = [](double) { return std::optional<std::int64_t>{}; };
    options.version = "1.3.0";
    auto owner = std::make_unique<facility::FacilityOwner>(host, overlay.context(), raster, options);
    // The host's stage hooks (application.cpp connectAreas).
    auto* stage = host.find<b::BootLogStage>(b::bootLogStageKey);
    check(stage == &owner->stage(), "The owner publishes the boot-log stage");
    std::optional<bool> deferred, concealed;
    std::optional<c::MotionPoint> entered;
    stage->deferEntrance = [&](bool v) { deferred = v; };
    stage->concealCentre = [&](bool v) { concealed = v; };
    stage->enterCentre = [&](c::MotionPoint p, double) { entered = p; };
    check(owner->monitor().isEnabled() && diskReads == 1 && owner->monitor().alerts().empty(), "Enabled at construction, sampled at once, nominal");

    const a::ClientMetrics metrics{width, height, 96, 1, width, height};
    owner->resize(metrics); overlay.resize(metrics);
    overlay.setAppearance(host.look, 0); owner->setAppearance(host.look, 0);
    c::source::DesktopChromeSettings chrome; chrome.viewport = {0, 0, double(width), double(height)};
    const auto center = c::Matrix4::translation(140, 80) * c::Matrix4::scale(1, 1);
    double time{};
    const auto frame = [&](double t) {
        time = host.clock = t;
        (void)overlay.deadline(t); (void)owner->deadline(t);
        const auto s = sample(host.selected);
        const a::ModuleFrame f{center, chrome, s, 1, t, true};
        overlay.update(f); owner->update(f);
        overlay.upload(renderer); owner->upload(renderer);
        std::vector<gpu::LayerCompositionEntry> entries;
        for (const auto& e : owner->planeEntries()) entries.push_back(e);
        const auto modal = owner->modalEntries();
        for (const auto& e : modal) entries.push_back(e);
        for (const auto& e : overlay.modalEntries()) entries.push_back(e);
        composition.setEntries(renderer, entries);
        composition.present(renderer);
        renderer.draw(false);
        owner->collected(renderer); overlay.collected(renderer);
        return std::pair{owner->planeEntries().size(), modal.size()};
    };

    // --- Today's first opening: the log ------------------------------------------
    host.visible = true; host.phase = c::SystemOverlayPhase::opening;
    overlay.setOverlayVisible(true, 1); owner->setOverlayVisible(true, 1);
    check(deferred == true && concealed == true && owner->playsBootLogOnOpen(), "The first opening of the day defers the entrance and conceals the centre");
    check(host.configuration.dailyBootLogLastDay() == "2026-10-23", "The day is consumed at the opening");
    frame(1.1);
    check(!owner->bootLog().isPlaying() && !owner->capturesPointer(), "The log waits for the opening to finish");
    host.phase = c::SystemOverlayPhase::open;
    const auto [plane0, modal0] = frame(1.2);
    check(owner->bootLog().isPlaying() && owner->capturesPointer() && owner->capturesKeys() && modal0 > 0 && plane0 == 0, "Interactive: the log plays on the box plane");
    const auto rows = owner->bootLog().rowTexts();
    const auto has = [&](std::string_view s) { return std::find(rows.begin(), rows.end(), s) != rows.end(); };
    check(has("> BOOT SEQUENCE  2026.10.23 09:30") && has("> UPTIME ................ 1D 01H 01M") && has("> POWER ................. 82%  CHARGE MODE")
        && has("> CALENDAR .............. 1 EVENT TODAY") && has("Stand-up") && has("> FACILITY .............. NOMINAL")
        && has("ALL SYSTEMS ONLINE. WELCOME BACK, ENDMINISTRATOR."), "The lines come from the sources, the monitor's disk and the defaults");
    check(owner->bootLog().terminalText() == "ENDFIELD INDUSTRIES TERMINAL v1.3.0", "The terminal names the version");
    frame(1.6);
    // Any key skips: the wipe-up, then the centre enters from below and the alerts are checked.
    free = 40'000'000'000;
    a::KeyEvent key; key.kind = a::KeyKind::down; key.value = 'A';
    check(owner->key(key, 1.7), "Keys belong to the log while it plays");
    check(owner->bootLog().isFinishing(), "Any key skips to the wipe-up");
    frame(2.1);
    check(!owner->bootLog().isPlaying() && concealed == false && entered && *entered == c::ShellPosition::subpageOffset(0, 1), "The centre returns and enters from below");
    check(owner->monitor().alert(m::FacilityAlertKind::disk) != nullptr, "The end of the log checks the facility at once");
    const auto [plane1, modal1] = frame(2.2);
    check(owner->banner().isShowing() && plane1 > 0 && modal1 > 0, "The new alert shows its banner and its tag");
    check(owner->monitor().newAlerts().empty(), "The banner marked the alert presented");

    // --- The banner: flat at the top; a click opens Storage ----------------------
    const double unit = m::design::geometry::baseUnit;
    const auto layout = c::source::DesktopChromeLayout::make(chrome, center, {});
    const auto bannerWorld = layout.designToScreen * c::Matrix4::translation(500, (14 - layout.designOrigin.y) / layout.designScale)
        * c::Matrix4::scale(unit, unit) * c::Matrix4::translation(-270, 0);
    const auto top = client(bannerWorld, {270, 0});
    check(top && std::abs(top->y - 14) < 1e-6 && std::abs(top->x - (layout.designOrigin.x + 500 * layout.designScale)) < 1e-6, "The banner hangs 14 points below the top, centred on the design");
    const auto press = [&](c::Point p, double t) {
        a::PointerEvent e; e.kind = a::PointerKind::down; e.button = a::PointerButton::left; e.x = p.x; e.y = p.y;
        const bool taken = owner->pointer(e, t);
        e.kind = a::PointerKind::up; (void)owner->pointer(e, t + .05);
        return taken;
    };
    const auto bannerCentre = client(bannerWorld, {270, 35});
    check(bannerCentre && owner->covers(*bannerCentre), "The banner covers its plate");
    check(press(*bannerCentre, 2.3), "The banner takes the press");
    check(!host.chosen.empty() && host.chosen.back() == c::Module::storage, "A banner click opens Storage");
    frame(2.7);
    check(!owner->banner().isShowing(), "The clicked banner left");

    // --- The tag: on the core plane; x dismisses ---------------------------------
    const auto tagWorld = center * c::Matrix4::translation(262, 52) * c::Matrix4::scale(unit, unit) * c::Matrix4::translation(-272, 0);
    const auto tag = owner->tags().tag(m::FacilityAlertKind::disk);
    check(tag && tag->text == "DISK 4% FREE", "The tag shows the disk alert");
    const auto dismiss = client(tagWorld, {tag->frame.x + tag->frame.width - 14, tag->frame.y + 14});
    check(dismiss && press(*dismiss, 2.8), "x takes the press");
    check(owner->monitor().alerts().empty() && owner->monitor().isStorageLow(), "x dismisses the alert; its condition stays");
    frame(3.2); frame(3.5);
    check(owner->tags().empty(), "The tag struck through, collapsed and left");
    check(!owner->covers(*dismiss), "Nothing left to press");

    // --- Cadence: drains re-read from 60 s, the fallback wake after 65 s ----------
    const auto reads = diskReads;
    a::HostEvent drain{a::HostEvent::Kind::utilityCompleted, 0};
    (void)owner->hostEvent(drain, 30);
    check(diskReads == reads, "A drain within the minute reads only the memory");
    used = 15'500'000'000;
    (void)owner->hostEvent(drain, 40);
    check(owner->monitor().alert(m::FacilityAlertKind::memory) != nullptr, "The memory pressure follows the drains (critical from 95 %)");
    frame(40.1);
    check(owner->banner().isShowing() && owner->banner().current()->kind() == m::FacilityAlertKind::memory && owner->banner().texts()[4] == "97%",
        "A raise while open shows its banner at once (usage read while elevated)");
    frame(45.2); frame(45.5);
    check(!owner->banner().isShowing(), "The banner counts down 5 s and leaves");
    (void)owner->hostEvent(drain, 63);
    check(diskReads == reads + 1, "A drain a minute after the last full read reads everything");
    const auto wake = owner->nextWakeTime(63);
    check(wake && std::abs(*wake - (63 + 65)) < 1e-9, "Without drains the owner wakes itself 65 s later");
    (void)owner->deadline(128);
    check(diskReads == reads + 2, "The fallback wake reads");
    // A volume arrival (WM_DEVICECHANGE) reads at once, without consuming the notice.
    DEV_BROADCAST_VOLUME volume{}; volume.dbcv_size = sizeof volume; volume.dbcv_devicetype = DBT_DEVTYP_VOLUME;
    check(!owner->message({window.h, WM_DEVICECHANGE, DBT_DEVICEARRIVAL, reinterpret_cast<std::intptr_t>(&volume)}, 128.05) && diskReads == reads + 3, "A mounted volume reads the disk at once");
    DEV_BROADCAST_HDR port{}; port.dbch_size = sizeof port; port.dbch_devicetype = DBT_DEVTYP_PORT;
    check(!owner->message({window.h, WM_DEVICECHANGE, DBT_DEVICEARRIVAL, reinterpret_cast<std::intptr_t>(&port)}, 128.06) && diskReads == reads + 3, "Other devices read nothing");
    frame(128.1);
    check(!owner->banner().isShowing(), "A listed alert shows its banner once");

    // --- Closing and the next opening of the day ---------------------------------
    owner->overlayClosing(130); overlay.overlayClosing(130);
    host.visible = false;
    check(!owner->banner().isShowing(), "Closing hides the banner at once");
    (void)owner->hostEvent({a::HostEvent::Kind::overlayConcealed, 0}, 131);
    frame(131);
    host.visible = true; host.phase = c::SystemOverlayPhase::opening;
    overlay.setOverlayVisible(true, 140); owner->setOverlayVisible(true, 140);
    check(deferred == false && !owner->playsBootLogOnOpen(), "The second opening of the day plays no log");
    host.phase = c::SystemOverlayPhase::open;
    frame(140.2);
    check(!owner->bootLog().isPlaying() && !owner->capturesPointer(), "No log, no capture");
    check(owner->tags().kinds() == std::vector{m::FacilityAlertKind::memory}, "Listed alerts keep their tags across openings");
    // Reduce Motion: the next day plays nothing and keeps the day.
    owner->overlayClosing(150); overlay.overlayClosing(150); host.visible = false;
    today.day = 24;
    host.look.reduceMotion = true;
    overlay.setAppearance(host.look, 151); owner->setAppearance(host.look, 151);
    host.visible = true; host.phase = c::SystemOverlayPhase::open;
    overlay.setOverlayVisible(true, 152); owner->setOverlayVisible(true, 152);
    check(!owner->playsBootLogOnOpen() && host.configuration.dailyBootLogLastDay() == "2026-10-23", "Reduce Motion never plays or consumes the log");
    // Facility alerts off: everything clears.
    host.configuration.set("facilityAlerts", ehud::data::Json(false));
    check(owner->hostEvent({a::HostEvent::Kind::settingsChanged, 0}, 153) && !owner->monitor().isEnabled() && owner->monitor().alerts().empty(), "Off stops the monitor and clears the list");
    check(!owner->nextWakeTime(153), "No wake while off");
    frame(154); frame(154.5);
    owner->overlayClosing(160); overlay.overlayClosing(160); host.visible = false;
    frame(160);
    owner->release(renderer); overlay.release(renderer);
    owner.reset();
    check(host.find<b::BootLogStage>(b::bootLogStageKey) == nullptr, "The stage is withdrawn with the owner");
}
}

int main(int argc, char** argv) {
    try {
        if (argc < 2) throw std::runtime_error("usage: facility_owner_tests <hud.hlsl>");
        const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED); // WIC (the rasterizer's images)
        check(SUCCEEDED(com), "COM");
        struct Uninitialize { ~Uninitialize() { CoUninitialize(); } } uninitialize;
        run(argv[1]);
        std::cout << "facility owner contracts passed (" << checks << " checks)\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAILED: " << e.what() << '\n';
        return 1;
    }
}
#else
int main() { return 0; }
#endif
