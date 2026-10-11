// Calendar reminders area (app/areas/calendar_reminder_module) over an
// in-memory ApplicationHost: the registered factory per mode, the
// production-shaped route (clicks post the area's private WM_APP to the HUD
// window and are consumed through message()), its diagnostics service and
// the hidden coverage step. Test-only identity; the activator is never
// registered with COM, no process identity is established and no
// notification center, registry or Start-menu folder is touched.
#ifdef _WIN32
#include "app/areas/calendar_reminder_module.hpp"
#include "app/application_host.hpp"
#include "app/utility_executor.hpp"
#include "core/data/data_store.hpp"
#include "native/calendar_notification_identity.hpp"
#include "native/calendar_notifications.hpp"
#include "native/layer_scene.hpp"
#include "native/renderer.hpp"
#include <objbase.h>
#include <wrl/client.h>
#include <filesystem>
#include <iostream>
#include <map>
#include <vector>
#include <typeinfo>

namespace {
namespace fs = std::filesystem;
namespace a = endfield::app;
namespace c = endfield::core;
namespace m = endfield::modules;
namespace gpu = endfield::native;
namespace areas = endfield::areas;
using Microsoft::WRL::ComPtr;
unsigned checks{};
void check(bool value, const std::string& message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
constexpr const char* eventID = "11111111-2222-4333-8444-555555555555";

struct Window {
    HWND h{};
    Window() {
        h = CreateWindowExW(0, L"STATIC", L"Synthetic Calendar reminder route", WS_POPUP, 0, 0, 40, 40, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        check(h && !IsWindowVisible(h), "The fixture window stays hidden");
    }
    ~Window() { if (h) DestroyWindow(h); }
};

class FakeHost final : public a::ApplicationHost {
public:
    FakeHost(a::ApplicationAreaMode mode, gpu::Renderer& r, gpu::LayerRasterizer& raster, a::UtilityExecutor& executor)
        : mode_(mode), renderer_(r), raster_(raster), executor_(executor) {}
    ehud::data::Settings configuration = ehud::data::Settings::defaults();
    a::ModuleAppearance look;
    a::ClientMetrics metrics_{1280, 800, 96, 1, 1280, 800};
    unsigned selections{}, opens{}, closes{}, frames{}, recorded{};
    std::uint32_t nextMessage{WM_APP + 0x1000};

    a::ApplicationAreaMode mode() const noexcept override { return mode_; }
    double now() const override { return 1; }
    const ehud::data::Settings& settings() const noexcept override { return configuration; }
    const a::ModuleAppearance& appearance() const noexcept override { return look; }
    c::Module selectedModule() const noexcept override { return c::Module::map; }
    c::SystemOverlayPhase overlayPhase() const noexcept override { return c::SystemOverlayPhase::closed; }
    bool overlayVisible() const noexcept override { return false; }
    bool focused() const noexcept override { return false; }
    const a::ClientMetrics& metrics() const noexcept override { return metrics_; }
    gpu::Renderer& renderer() noexcept override { return renderer_; }
    gpu::LayerRasterizer& rasterizer() noexcept override { return raster_; }
    a::UtilityExecutor* utility() noexcept override { return &executor_; }
    gpu::SystemServices* systemServices() noexcept override { return nullptr; }
    gpu::AudioRuntime* audio() noexcept override { return nullptr; }
    gpu::NativeMediaRequestBroker* mediaBroker() noexcept override { return nullptr; }
    const m::WorkModeController* workMode() const noexcept override { return nullptr; }
    c::ApplicationEventRecorder* eventRecorder() noexcept override { return nullptr; }
    void requestFrame() override { ++frames; }
    void recordEvent(m::EventKind, m::EventMetadata) override { ++recorded; }
    void selectModule(c::Module) override { ++selections; }
    void openModule(c::Module) override { ++opens; }
    void closeOverlay() override { ++closes; }
    bool updateSettings(const std::function<void(ehud::data::Settings&)>&) override { return false; }
    bool chooseFiles(const a::ApplicationFileRequest&, std::function<void(a::ApplicationFileResult)>) override { return false; }
    void cancelFileChoice() override {}
    std::uint32_t registerMessage() override { return nextMessage++; }
    bool hostActions() const noexcept { return selections || opens || closes || recorded; }
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
    a::ApplicationAreaMode mode_;
    gpu::Renderer& renderer_;
    gpu::LayerRasterizer& raster_;
    a::UtilityExecutor& executor_;
    std::map<std::string, std::pair<void*, const std::type_info*>, std::less<>> services_;
};

struct Fixture {
    Window window;
    gpu::Renderer renderer; // never initialized: the area draws nothing
    gpu::LayerRasterizer raster;
    a::UtilityExecutor executor{[] {}};
    FakeHost host;
    fs::path root = fs::temp_directory_path() / L"EndfieldHUD-calendar-reminder-area-unused";
    explicit Fixture(a::ApplicationAreaMode mode) : host(mode, renderer, raster, executor) {}
    a::ApplicationModuleContext context() {
        return {raster, window.h, &executor, root, nullptr, 0, host.metrics_, [] { return 1.0; }, [] {}, [](m::EventKind, m::EventMetadata) {},
                [](std::string_view) { return fs::path{}; }, "calendarReminders", host.mode(), &host,
                host.mode() == a::ApplicationAreaMode::hiddenCoverage ? root : fs::path{}, [](std::string_view) { return fs::path{}; }};
    }
};
endfield::native::AppNotificationIdentity testIdentity() {
    auto identity = gpu::calendarNotificationIdentity("C:\\EndfieldHUD-Test\\App\\EndfieldHUD.exe", "C:\\EndfieldHUD-Test\\Programs");
    identity.appUserModelID = "EndfieldHUD.Tests.CalendarReminders";
    identity.activatorCLSID = "{5C487D23-3A24-491A-832E-8CF00D94D22B}";
    return identity;
}
std::wstring wide(const std::string& s) { return std::wstring(s.begin(), s.end()); } // ASCII fixtures only

void factoryModes() {
    for (const auto mode : {a::ApplicationAreaMode::developmentPreview}) {
        Fixture f(mode);
        check(!areas::calendarRemindersModule(f.context()), "Development previews keep their injected fixture notifications and no area");
    }
    Fixture f(a::ApplicationAreaMode::production);
    areas::CalendarRemindersOptions none;
    none.installed = [] { return std::optional<endfield::native::AppNotificationIdentity>{}; };
    check(!areas::makeCalendarRemindersArea(f.context(), none) && !f.host.find<areas::CalendarRemindersService>(areas::CalendarRemindersService::key),
          "Without the installer's registration the area is absent");
    check(!areas::makeCalendarRemindersArea(f.context(), {}), "Without an identity source the area is absent");
}

void productionRoute() {
    Fixture f(a::ApplicationAreaMode::production);
    areas::CalendarRemindersOptions options;
    options.installed = [] { return std::optional(testIdentity()); };
    auto owner = areas::makeCalendarRemindersArea(f.context(), options);
    auto* service = f.host.find<areas::CalendarRemindersService>(areas::CalendarRemindersService::key);
    check(owner && service && owner->name() == "calendarReminders" && service->appUserModelID() == "EndfieldHUD.Tests.CalendarReminders" &&
          !service->activatorRegistered(), "The area publishes its identity; tests never register the activator class");
    const auto routing = owner->routing();
    check(routing.message == 40 && routing.pointer == a::ModuleRouting::noRoute && routing.key == a::ModuleRouting::noRoute &&
          routing.flush == a::ModuleRouting::noRoute, "Only the message chain reaches the area");
    for (unsigned n = 0; n <= static_cast<unsigned>(c::Module::profile); ++n) check(!owner->presents(static_cast<c::Module>(n)), "The area presents no HUD module");
    check(!gpu::processAppUserModelID(), "The test process identity is never established");
    // The built-in Calendar owner's refresh (the host publishes it): the Mac
    // Calendar refreshes when a reminder click activates the app.
    std::vector<double> refreshed;
    areas::CalendarReminderActivationSink refresh = [&](double time) { refreshed.push_back(time); };
    f.host.publish<areas::CalendarReminderActivationSink>(areas::calendarReminderActivationKey, refresh);

    ComPtr<INotificationActivationCallback> callback;
    check(SUCCEEDED(service->createActivationCallback(&callback)) && callback, "The actual callback is available without COM registration");
    const auto id = wide("EndfieldHUD.Tests.CalendarReminders");
    const auto reminder = wide(gpu::calendarNotificationArguments(eventID, "1791622800|true|Title|gregorian|en"));
    check(SUCCEEDED(callback->Activate(id.c_str(), reminder.c_str(), nullptr, 0)) && SUCCEEDED(callback->Activate(id.c_str(), reminder.c_str(), nullptr, 0)),
          "Two clicks are accepted");
    check(SUCCEEDED(callback->Activate(id.c_str(), L"not json", nullptr, 0)), "A malformed launch string is queued for validation");
    check(FAILED(callback->Activate(L"Another.App", reminder.c_str(), nullptr, 0)), "Another application's click is refused");
    MSG msg{};
    unsigned posted{};
    std::uint32_t notice{};
    while (PeekMessageW(&msg, f.window.h, WM_APP, 0xBFFF, PM_REMOVE)) { ++posted; notice = msg.message; check(msg.wParam == 1, "The notice carries the route generation"); }
    check(posted == 1 && notice == WM_APP + 0x1000 && service->calendarActivations() == 0, "Clicks coalesce into one private notice on the HUD window");
    check(!owner->message({f.window.h, WM_APP + 0x0FFF, 1, 0}, 1), "Other messages pass through");
    check(refreshed.empty(), "Nothing refreshes before the owner's notice");
    check(owner->message({f.window.h, notice, 1, 0}, 1.5), "The private notice is consumed");
    check(service->calendarActivations() == 2 && service->ignoredActivations() == 1 && service->lastEvent() == std::optional<std::string>(eventID),
          "Calendar clicks name their event; malformed launches are ignored");
    check(refreshed == std::vector<double>{1.5} && service->calendarRefreshes() == 1, "One notice with reminder clicks refreshes Calendar once, at the notice time");
    check(owner->message({f.window.h, notice, 2, 0}, 1.6) && service->calendarActivations() == 2 && refreshed.size() == 1, "A stale route generation consumes and refreshes nothing");
    const auto deliver = [&](const wchar_t* arguments, double time) {
        check(SUCCEEDED(callback->Activate(id.c_str(), arguments, nullptr, 0)), "A further click is accepted");
        MSG next{};
        check(PeekMessageW(&next, f.window.h, WM_APP, 0xBFFF, PM_REMOVE) && next.message == notice, "A further click posts one notice");
        check(owner->message({f.window.h, notice, 1, 0}, time), "The further notice is consumed");
    };
    deliver(L"{\"type\":\"update\"}", 1.7);
    check(service->ignoredActivations() == 2 && refreshed.size() == 1, "A notice with only foreign launches refreshes nothing");
    f.host.withdraw(areas::calendarReminderActivationKey);
    deliver(reminder.c_str(), 1.8);
    check(service->calendarActivations() == 3 && refreshed.size() == 1 && service->calendarRefreshes() == 1,
          "Without the Calendar owner's refresh a click is still consumed and refreshes nothing");
    check(!f.host.hostActions() && !f.host.frames, "A reminder click opens, selects, closes and records nothing (Mac activation only)");
    // The source never routes a click that arrives after the area is gone.
    owner.reset();
    check(!f.host.find<areas::CalendarRemindersService>(areas::CalendarRemindersService::key), "Destruction withdraws the service");
    check(FAILED(callback->Activate(id.c_str(), reminder.c_str(), nullptr, 0)), "A retained COM callback is inert after the area is destroyed");
    check(!PeekMessageW(&msg, f.window.h, WM_APP, 0xBFFF, PM_REMOVE), "A late click posts nothing to the HUD window");
}

void hiddenCoverage() {
    Fixture f(a::ApplicationAreaMode::hiddenCoverage);
    auto owner = areas::calendarRemindersModule(f.context());
    auto* service = f.host.find<areas::CalendarRemindersService>(areas::CalendarRemindersService::key);
    check(owner && service && service->appUserModelID() == gpu::calendarNotificationAppUserModelID && !service->activatorRegistered(),
          "Hidden coverage constructs the synthetic identity without COM registration");
    unsigned assertions{}, routed{}, refreshes{};
    areas::CalendarReminderActivationSink refresh = [&](double) { ++refreshes; };
    f.host.publish<areas::CalendarReminderActivationSink>(areas::calendarReminderActivationKey, refresh);
    a::ModuleCoverage step;
    step.check = [&](bool value, std::string_view why) { ++assertions; check(value, std::string(why)); };
    step.present = [](double) {};
    step.drain = [] {};
    step.message = [&](std::uint32_t id, std::uintptr_t wParam, std::intptr_t lParam) { ++routed; return owner->message({f.window.h, id, wParam, lParam}, 2); };
    step.module = c::Module::calendar;
    owner->coverage(step);
    check(!assertions && !routed, "Module steps are ignored (no presented module)");
    step.module.reset();
    for (unsigned cycle = 0; cycle < 3; ++cycle) { step.cycle = cycle; owner->coverage(step); }
    check(assertions >= 30 && routed == 6 && service->calendarActivations() == 3 && service->ignoredActivations() == 3,
          "Each cycle routes one reminder click and one foreign launch through the message chain");
    check(refreshes == 3 && service->calendarRefreshes() == 3, "Each cycle's reminder notice refreshes the Calendar owner once");
    MSG msg{};
    check(!PeekMessageW(&msg, f.window.h, WM_APP, 0xBFFF, PM_REMOVE), "Coverage never posts to the HUD window");
    check(!f.host.hostActions(), "Coverage clicks take no host action");
}
} // namespace

int main() {
    try {
        if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) throw std::runtime_error("The test owns its STA");
        factoryModes();
        productionRoute();
        hiddenCoverage();
        CoUninitialize();
        std::cout << "PASS " << checks << " Calendar reminder area checks\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL after " << checks << ": " << e.what() << '\n';
        return 1;
    }
}
#else
int main() { return 0; }
#endif
