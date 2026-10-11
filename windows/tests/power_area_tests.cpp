// Hidden WARP fixture for the Power module area (app/areas/power_module): the
// production factory over an in-memory ApplicationHost. It drives the owner's
// shared events, the private notice route, the area service, the Power page
// sink and the hidden coverage step itself, including the flows the hidden
// coverage harness cannot reach without closing its HUD (Edit position closes
// first, quitting, every suspension reason, Projection). Synthetic battery
// readings and a hidden alert window only: no power device, settings file,
// Event Log store, tray or visible window.
#ifdef _WIN32
#include "app/areas/power_module.hpp"
#include "app/application_host.hpp"
#include "app/utility_executor.hpp"
#include "core/data/data_store.hpp"
#include "core/source_desktop_chrome.hpp"
#include "modules/power_policy.hpp"
#include "native/layer_scene.hpp"
#include "native/renderer.hpp"
#include <objbase.h>
#include <uiautomation.h>
#include <wrl/client.h>
#include <atomic>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <map>
#include <typeinfo>
#include <vector>

namespace {
namespace fs = std::filesystem;
namespace gpu = endfield::native;
namespace c = endfield::core;
namespace a = endfield::app;
namespace m = endfield::modules;
namespace p = endfield::power;
using Json = ehud::data::Json;
using Kind = a::HostEvent::Kind;
unsigned checks{};
void check(bool v, const std::string& message) {
    ++checks;
    if (!v) throw std::runtime_error(message);
}
constexpr unsigned width = 1280, height = 800;
// The alert window of THIS test process only (another EndfieldHUD on the
// machine registers the same class name in its own process).
HWND ownAlertWindow() {
    constexpr wchar_t name[] = L"EndfieldHUDChargeAlertPanel";
    for (HWND h = FindWindowExW(nullptr, nullptr, name, nullptr); h; h = FindWindowExW(nullptr, h, name, nullptr)) {
        DWORD process{};
        GetWindowThreadProcessId(h, &process);
        if (process == GetCurrentProcessId()) return h;
    }
    return nullptr;
}
struct Window {
    HWND h{};
    Window() {
        WNDCLASSW w{};
        w.lpfnWndProc = DefWindowProcW;
        w.hInstance = GetModuleHandleW(nullptr);
        w.lpszClassName = L"EndfieldPowerAreaFixture";
        check(RegisterClassW(&w) != 0, "Register own fixture");
        h = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP, w.lpszClassName, L"Synthetic Power area only", WS_POPUP, 0, 0, width, height,
                            nullptr, nullptr, w.hInstance, nullptr);
        check(h && !IsWindowVisible(h), "Own fixture stays hidden");
    }
    ~Window() { if (h) DestroyWindow(h); UnregisterClassW(L"EndfieldPowerAreaFixture", GetModuleHandleW(nullptr)); }
};

// In-memory host: records host actions, applies preference updates on the
// next frame (as the Application does), owns no tray, provider or file.
class FakeHost final : public a::ApplicationHost {
public:
    FakeHost(gpu::Renderer& r, gpu::LayerRasterizer& raster, a::UtilityExecutor& executor) : renderer_(r), raster_(raster), executor_(executor) {}
    double clock{};
    ehud::data::Settings configuration = ehud::data::Settings::defaults();
    a::ModuleAppearance look;
    c::Module selected{c::Module::about};
    std::optional<c::Module> hovered; // source navigation entry under the pointer
    std::optional<c::SystemOverlayPhase> phase; // lifecycle phase override (else open/closed by visibility)
    bool visible{}, settingsPending{};
    a::ClientMetrics metrics_{width, height, 96, 1, width, height};
    unsigned frames{}, closes{}, settingsUpdates{}, recorded{};
    std::vector<c::Module> opened;
    std::uint32_t nextMessage{WM_APP + 0x1000};

    a::ApplicationAreaMode mode() const noexcept override { return a::ApplicationAreaMode::hiddenCoverage; }
    double now() const override { return clock; }
    const ehud::data::Settings& settings() const noexcept override { return configuration; }
    const a::ModuleAppearance& appearance() const noexcept override { return look; }
    c::Module selectedModule() const noexcept override { return selected; }
    c::SystemOverlayPhase overlayPhase() const noexcept override { return phase.value_or(visible ? c::SystemOverlayPhase::open : c::SystemOverlayPhase::closed); }
    bool overlayVisible() const noexcept override { return visible; }
    bool focused() const noexcept override { return true; }
    const a::ClientMetrics& metrics() const noexcept override { return metrics_; }
    std::optional<c::Module> hoveredModule() const noexcept override { return hovered; }
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
    void selectModule(c::Module module) override { selected = module; }
    void openModule(c::Module module) override { opened.push_back(module); }
    void closeOverlay() override { ++closes; }
    bool updateSettings(const std::function<void(ehud::data::Settings&)>& mutate) override {
        mutate(configuration);
        settingsPending = true;
        ++settingsUpdates;
        return true;
    }
    bool chooseFiles(const a::ApplicationFileRequest&, std::function<void(a::ApplicationFileResult)>) override { return false; }
    void cancelFileChoice() override {}
    std::uint32_t registerMessage() override { return nextMessage++; }
    std::size_t services() const noexcept { return services_.size(); }
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
    a::UtilityExecutor& executor_;
    std::map<std::string, std::pair<void*, const std::type_info*>, std::less<>> services_;
};

void run(const fs::path& shader) {
    Window window;
    gpu::Renderer renderer;
    renderer.initialize(window.h, width, height, {gpu::Driver::warpForTests, shader, gpu::RenderTarget::offscreenForTests});
    const auto camera = gpu::layerViewportProjection(width, height);
    renderer.setCamera(camera);
    gpu::LayerRasterizer raster;
    std::atomic<unsigned> notified{};
    a::UtilityExecutor executor([&] { ++notified; });
    {
        FakeHost host(renderer, raster, executor);
        host.look.language = c::Language::english;
        std::vector<m::BatteryReading> page;
        p::PowerPageSink sink = [&](const m::BatteryReading& r) { page.push_back(r); return true; };
        host.publish<p::PowerPageSink>(p::powerPageSinkKey, sink);
        const auto root = fs::temp_directory_path() / L"EndfieldHUD-power-area-fixture-unused";
        const a::ApplicationModuleContext context{raster, window.h, &executor, root, nullptr, 0, host.metrics_,
            [&] { return host.clock; }, [] {}, [](m::EventKind, m::EventMetadata) {},
            [&](std::string_view id) { return id == "shell.shader" ? shader : fs::path{}; },
            "powerCharge", a::ApplicationAreaMode::hiddenCoverage, &host, root, [](std::string_view) { return fs::path{}; }};
        auto owner = p::powerModule(context);
        auto* service = host.find<p::PowerAreaService>(p::PowerAreaService::key);
        check(owner && service && owner->name() == "powerCharge" && !owner->presents(c::Module::power), "The factory registers its owner and service");
        check(owner->automationModule() == c::Module::power, "The UI Automation group holding the Power entry carries the Power caption");
        const auto routing = owner->routing();
        check(routing.pointer == 1 && routing.message == 20 && routing.pointerCapture == a::ModuleRouting::noRoute && !routing.pointerPolicy.alwaysUpdateEnvironment,
              "Badge input routes after Notes and before the center modules; notices route through the message chain");
        const std::uint32_t notice = WM_APP + 0x1000;
        std::vector<bool> editing;
        service->setEditingObserver([&](bool value) { editing.push_back(value); });
        check(editing == std::vector<bool>{false}, "The editing observer starts with the current state");

        // Provider baseline: percent pair, then the battery-class pair.
        const auto drain = [&](double t) { executor.waitIdle(); executor.drain(); return owner->hostEvent({Kind::utilityCompleted, 0}, t); };
        check(page.size() == 1 && page[0].present && page[0].percentage == 63u && page[0].capacity == m::BatteryReading::Capacity{63, 100, "%"},
              "The startup reading reaches the Power page sink");
        check(drain(.1) && page.size() == 2 && page[1].capacity == m::BatteryReading::Capacity{32'760, 52'000, "mWh"} &&
              service->reading() == page[1], "The capacity pair arrives through the shared executor drain");
        check(service->trayStatus(c::Language::english) == "63% · On battery" &&
              service->previewMenuTitle(c::Language::system) == m::powerPreviewMenuTitle(c::Language::english), "Tray status and preview title");
        check(!service->telemetryDemand() && !owner->nextWakeTime(.1) && !owner->requiresFrames(.1), "Closed and idle: no wake, frames or telemetry");

        // Frames of the shared clock on the real core plane.
        c::source::DesktopChromeSettings chrome{{0, 0, width, height}, 1, {}, c::Module::power, true};
        const auto center = c::source::DesktopChromeLayout::make(chrome, {}, {}).designToScreen;
        const c::ModulePresentationSample sample{};
        const auto frame = [&](double t) {
            host.clock = std::max(host.clock, t);
            if (host.settingsPending) {
                host.settingsPending = false;
                owner->setAppearance(host.look, t);
                (void)owner->hostEvent({Kind::settingsChanged, 0}, t);
            }
            owner->update({center, chrome, sample, 1.f, t, true});
            owner->upload(renderer);
        };
        const auto frames = [&](double from, double to) { for (double t = from; t <= to + 1e-9; t += 1.0 / 60) frame(t); };
        const auto project = [&](double x, double y) {
            const auto q = c::Projection::viewport(camera * center, width, height).project({x, y});
            check(q.has_value(), "Design point projects");
            return *q;
        };
        const auto middle = project(500, 446 + 38), edge = project(600, 446 + 38);
        host.visible = true;
        owner->setOverlayVisible(true, 1);
        frame(1);
        const auto reveal = owner->nextWakeTime(1);
        check(owner->planeEntries().empty() && owner->floatingEntries().empty() && reveal && std::abs(*reveal - 1.58) < 1e-9 && !owner->requiresFrames(1.2),
              "The badge reveal is one wake 0.58 s after the HUD opens");
        check(!owner->deadline(1.3) && owner->deadline(1.58), "Only the due reveal asks the host for a frame");
        frames(1.58, 3.2);
        check(owner->planeEntries().size() == 1 && owner->floatingEntries().empty() && owner->covers(middle) && owner->covers(edge),
              "The compact capsule holds after the entrance, composed in the core-plane slot");
        frames(3.2, 6.8);
        check(owner->covers(middle) && !owner->covers(edge) && !owner->requiresFrames(6.8), "The hold collapses to the circle");
        check(owner->pointer({a::PointerKind::move, a::PointerButton::none, middle.x, middle.y, 0}, 6.9), "Hover acquires the circle");
        frames(6.9, 7.2);
        check(owner->covers(edge), "Hover expands the capsule");
        check(owner->pointer({a::PointerKind::down, a::PointerButton::left, edge.x, edge.y, 0}, 7.2) &&
              owner->pointer({a::PointerKind::up, a::PointerButton::left, edge.x, edge.y, 0}, 7.2) && host.selected == c::Module::power,
              "A badge click selects Power");
        owner->cancelInteraction(7.2);
        frames(7.2, 7.5);
        check(!owner->covers(edge), "Cancelled hover collapses the capsule");
        check(service->powerAccessibilityLabel() == "EndfieldHUD, Battery: 63%, On battery", "Power entry help");
        // The badge artwork is rasterized for the core plane's rest pose:
        // scaling the plane (the deployment lift, tilt, parallax) transforms
        // it without a re-raster; the HUD scale preference re-rasterizes it.
        {
            const auto zoomed = [&](double s, double t) {
                host.clock = std::max(host.clock, t);
                const auto pose = center * c::Matrix4::translation(500, 484) * c::Matrix4::scale(s, s) * c::Matrix4::translation(-500, -484);
                owner->update({pose, chrome, sample, 1.f, t, true});
                owner->upload(renderer);
            };
            const auto rasterized = raster.stats().rasterizations;
            for (unsigned n = 0; n <= 4; ++n) zoomed(1 + n * .4, 7.5 + n * .005);
            frame(7.53);
            check(raster.stats().rasterizations == rasterized && owner->covers(middle), "Scaling the core plane up to 2.6x re-rasterizes nothing");
            // The Settings page commits the HUD scale (not this area: no save counted).
            host.configuration.set("hudScale", Json(2.0));
            host.settingsPending = true;
            frame(7.54);
            const auto scaled = raster.stats().rasterizations;
            check(scaled > rasterized, "A 2x HUD scale re-rasterizes the badge for its new rest pose");
            host.configuration.set("hudScale", Json(1.0));
            host.settingsPending = true;
            frame(7.55);
            const auto restored = raster.stats().rasterizations;
            check(restored > scaled, "Restoring the HUD scale re-rasterizes it once more");
            frames(7.56, 7.58);
            check(raster.stats().rasterizations == restored && host.settingsUpdates == 0, "Frames at rest re-rasterize nothing");
        }

        // Settings > Edit position: the HUD closes first, then the editor.
        check(service->editPosition(7.6) && !service->editingPosition(), "The edit request is deferred");
        check(owner->message({window.h, notice, 0, 0}, 7.6) && host.closes == 1 && !service->editingPosition(), "The notice closes the open HUD first");
        check(owner->message({window.h, notice, 0, 0}, 7.65) && host.closes == 1, "One close per request");
        owner->overlayClosing(7.7);
        frames(7.7, 8.35);
        check(owner->planeEntries().empty() && !owner->covers(middle), "The badge retracts with the HUD close");
        host.visible = false;
        check(owner->hostEvent({Kind::overlayConcealed, 0}, 8.4) && service->editingPosition() && editing == std::vector<bool>{false, true},
              "Concealment begins the queued edit and notifies the observer");
        const auto panel = ownAlertWindow();
        check(panel && !IsWindowVisible(panel), "The editor uses the hidden coverage alert window");
        SendMessageW(panel, WM_KEYDOWN, VK_ESCAPE, 0);
        check(!service->editingPosition() && host.settingsUpdates == 0 && editing.back() == false, "Escape discards without saving");
        check(owner->message({window.h, notice, 0, 0}, 8.5) && host.opened == std::vector<c::Module>{c::Module::display},
              "The editor's end reopens Display on the next notice");
        // An edit requested while the HUD is concealed starts at once; a summon over it cancels it.
        check(service->editPosition(8.6) && owner->message({window.h, notice, 0, 0}, 8.6) && service->editingPosition() && host.closes == 1,
              "A concealed HUD edits at once");
        host.visible = true;
        owner->setOverlayVisible(true, 8.7);
        check(!service->editingPosition() && host.settingsUpdates == 0 && host.opened.size() == 1 && editing.back() == false,
              "Summoning the HUD cancels the editor without a reopen");
        frames(8.7, 9.4);

        // Always mode with a CPU metric: telemetry demand follows the visible alert.
        host.updateSettings([](ehud::data::Settings& s) { s.set("displayMode", Json("always")); s.set("alertMetric", Json("cpu")); });
        frame(9.5);
        check(service->telemetryDemand(), "The visible badge asks for CPU telemetry");
        service->setTelemetry(m::ChargeTelemetry{42.0, {}, {}, {}, {}, {}, {}}, 9.5);
        check(service->powerAccessibilityLabel().find("42") != std::string::npos, "The badge shows the shared CPU sample");
        owner->overlayClosing(9.6);
        frames(9.6, 10.3);
        host.visible = false;
        (void)owner->hostEvent({Kind::overlayConcealed, 0}, 10.4);
        check(service->telemetryDemand() && owner->nextWakeTime(10.4) && std::abs(*owner->nextWakeTime(10.4) - (10.4 + 1.0 / 60)) < 1e-9,
              "Closing the HUD returns the persistent alert, animating on the shared timer");
        double t = 10.4;
        for (unsigned guard = 0; guard < 600; ++guard) {
            const auto wake = owner->nextWakeTime(t);
            if (!wake) break;
            t = *wake;
            (void)owner->deadline(t);
        }
        check(t > 11.8 && t < 12.1 && !owner->nextWakeTime(t) && service->telemetryDemand(), "The persistent alert settles and stops waking");
        // Suspension reasons: the first reason suspends, the end of the last resumes.
        (void)owner->hostEvent({Kind::suspended, a::HostEvent::suspendDisplay | a::HostEvent::suspendSession}, 12);
        check(!service->telemetryDemand() && !service->editPosition(12), "Display sleep hides the alert and refuses edits");
        (void)owner->hostEvent({Kind::resumed, a::HostEvent::suspendSession}, 13);
        check(service->telemetryDemand(), "Waking from the last reason restores the persistent alert");
        // Projection holds the alert; a tray preview cannot replace it.
        service->setProjectionActive(true, 14);
        service->preview(14.1);
        check(!service->telemetryDemand() && !owner->nextWakeTime(14.1), "Projection hides the alert and refuses previews");
        service->setProjectionActive(false, 15);
        check(!service->telemetryDemand(), "Projection ending does not replay an alert");
        service->preview(15.1);
        check(service->telemetryDemand() && owner->nextWakeTime(15.1), "The tray preview replays the alert afterwards");
        // Quit accepted: alerts stop; a cancelled quit (document failure) reopens the HUD.
        check(owner->hostEvent({Kind::quitting, 0}, 16) && !service->telemetryDemand() && !service->editPosition(16), "Quitting hides the alert");
        (void)owner->hostEvent({Kind::overlayConcealed, 0}, 16.5);
        check(!service->telemetryDemand() && !owner->nextWakeTime(16.5), "No alert returns after the quit's close");
        host.visible = true;
        owner->setOverlayVisible(true, 17);
        owner->overlayClosing(17.5);
        host.visible = false;
        (void)owner->hostEvent({Kind::overlayConcealed, 0}, 18);
        check(service->telemetryDemand(), "A cancelled quit restores the alert after the next close");
        host.updateSettings([](ehud::data::Settings& s) { s.set("displayMode", Json("whenChargingStarts")); s.set("alertMetric", Json("battery")); });
        frame(18.5);
        for (double time = 18.5; time < 19.5; time += 1.0 / 60) (void)owner->deadline(time);
        check(!service->telemetryDemand() && !owner->nextWakeTime(19.5), "Leaving always mode hides the alert");

        // The hidden coverage step itself, twice, over this host (HUD open).
        host.visible = true;
        owner->setOverlayVisible(true, 20);
        frames(20, 26);
        double h = 26;
        const auto settle = [&](double seconds) { frames(h, h + seconds); h += seconds; };

        // SystemHUDView.updateHover over the host's hovered navigation module:
        // setHovered(target == .module(.power)) followed by the re-entrant
        // refreshChargeHover, which re-reads the badge under the pointer.
        const auto offBadge = project(500, 200);
        host.hovered = c::Module::power;
        settle(.5);
        check(owner->covers(middle) && !owner->covers(edge), "The Power navigation entry alone does not expand the circle");
        host.hovered.reset();
        settle(.1);
        check(owner->pointer({a::PointerKind::move, a::PointerButton::none, middle.x, middle.y, 0}, h), "Hover acquires the circle again");
        settle(.4);
        check(owner->covers(edge), "Hover expands the capsule again");
        check(!owner->pointer({a::PointerKind::move, a::PointerButton::none, offBadge.x, offBadge.y, 0}, h) && owner->covers(edge),
              "A move off the hovered badge waits for the host's navigation target");
        host.hovered = c::Module::power;
        settle(.6);
        check(owner->covers(edge), "Landing on the Power navigation entry holds the expanded capsule");
        host.hovered = c::Module::notes;
        settle(.6);
        check(owner->covers(middle) && !owner->covers(edge), "Another navigation entry releases the hover to the circle");
        check(owner->pointer({a::PointerKind::move, a::PointerButton::none, middle.x, middle.y, 0}, h), "Hover acquires the circle once more");
        settle(.4);
        host.hovered.reset();
        check(!owner->pointer({a::PointerKind::move, a::PointerButton::none, offBadge.x, offBadge.y, 0}, h) && owner->covers(edge), "The move off waits again");
        settle(.6);
        check(!owner->covers(edge), "No navigation target under the pointer releases the hover on the next frame");

        // The Power navigation entry for UI Automation: one Button fragment
        // framed on the badge (SystemHUDView's source-shell navigation button).
        {
            using Microsoft::WRL::ComPtr;
            const auto bstr = [](BSTR b) { std::wstring value = b ? std::wstring(b) : std::wstring(); SysFreeString(b); return value; };
            const auto wideUtf8 = [](const std::string& text) {
                std::wstring w(text.size(), L'\0');
                const int n = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), w.data(), static_cast<int>(w.size()));
                w.resize(n > 0 ? static_cast<std::size_t>(n) : 0);
                return w;
            };
            check(owner->automationCount() == 0 && owner->automationRefresh(nullptr) && owner->automationCount() == 1 && !owner->automationRefresh(nullptr),
                  "UI Automation lists the Power navigation entry once");
            ComPtr<IRawElementProviderFragment> entry, hit, parent;
            ComPtr<IRawElementProviderSimple> simple;
            ComPtr<IUnknown> pattern;
            ComPtr<IInvokeProvider> invoke;
            ComPtr<IValueProvider> value;
            check(owner->automationChild(0, &entry) == S_OK && SUCCEEDED(entry.As(&simple)) && owner->automationChild(1, &hit) == E_INVALIDARG,
                  "The entry is the owner's only fragment");
            check(SUCCEEDED(entry->Navigate(NavigateDirection_Parent, &parent)) && !parent, "Without a group the entry has no parent");
            check(SUCCEEDED(simple->GetPatternProvider(UIA_InvokePatternId, &pattern)) && pattern && SUCCEEDED(pattern.As(&invoke)), "Invoke pattern");
            pattern.Reset();
            check(SUCCEEDED(simple->GetPatternProvider(UIA_ValuePatternId, &pattern)) && pattern && SUCCEEDED(pattern.As(&value)), "Value pattern");
            const auto property = [&](PROPERTYID id) {
                VARIANT v;
                VariantInit(&v);
                std::wstring out;
                if (SUCCEEDED(simple->GetPropertyValue(id, &v)) && v.vt == VT_BSTR && v.bstrVal) out = v.bstrVal;
                VariantClear(&v);
                return out;
            };
            const auto integer = [&](PROPERTYID id) {
                VARIANT v;
                VariantInit(&v);
                long out = -1;
                if (SUCCEEDED(simple->GetPropertyValue(id, &v))) out = v.vt == VT_BOOL ? (v.boolVal == VARIANT_TRUE ? 1 : 0) : v.vt == VT_I4 ? v.lVal : -1;
                VariantClear(&v);
                return out;
            };
            BSTR raw{};
            BOOL readOnly{};
            check(property(UIA_NamePropertyId) == L"Power / Device Battery" && property(UIA_HelpTextPropertyId) == wideUtf8(service->powerAccessibilityLabel()) &&
                  property(UIA_HelpTextPropertyId) == L"EndfieldHUD, Battery: 63%, On battery" && integer(UIA_ControlTypePropertyId) == UIA_ButtonControlTypeId &&
                  integer(UIA_IsEnabledPropertyId) == 1 && integer(UIA_IsKeyboardFocusablePropertyId) == 0 && integer(UIA_IsOffscreenPropertyId) == 0,
                  "Navigation title, live charge help, an enabled button");
            check(SUCCEEDED(value->get_Value(&raw)) && bstr(raw) == L"Selected" && SUCCEEDED(value->get_IsReadOnly(&readOnly)) && readOnly &&
                  value->SetValue(L"Power") == static_cast<HRESULT>(UIA_E_INVALIDOPERATION), "The badge click's selection is the read-only value");
            UiaRect r{};
            check(SUCCEEDED(entry->get_BoundingRectangle(&r)) && r.width > 0 && r.height > 0 && r.width < 60 && owner->covers({r.left + r.width / 2, r.top + r.height / 2}),
                  "The frame is the badge circle's projected hit rect (fixture window at the screen origin)");
            ComPtr<IUnknown> x, y;
            check(owner->automationFromPoint(r.left + r.width / 2, r.top + r.height / 2, &hit) == S_OK && hit && SUCCEEDED(hit.As(&x)) && SUCCEEDED(entry.As(&y)) &&
                  x.Get() == y.Get(), "The hit test at the badge reaches the entry");
            hit.Reset();
            check(owner->automationFromPoint(offBadge.x, offBadge.y, &hit) == S_OK && !hit, "The hit test elsewhere finds nothing");
            host.selected = c::Module::about;
            check(!owner->automationRefresh(nullptr) && SUCCEEDED(value->get_Value(&raw)) && bstr(raw) == L"Not selected", "The value follows the selection");
            // activateNavigationButton: the press runs on the private notice.
            check(invoke->Invoke() == S_OK && host.selected == c::Module::about, "A press waits for the notice");
            check(owner->message({window.h, notice, 0, 0}, h) && host.selected == c::Module::power, "The notice selects Power");
            host.look.language = c::Language::simplifiedChinese;
            owner->setAppearance(host.look, h);
            check(property(UIA_NamePropertyId) == L"电源 / 设备电量" && SUCCEEDED(value->get_Value(&raw)) && bstr(raw) == L"已选择" &&
                  property(UIA_HelpTextPropertyId) == wideUtf8(service->powerAccessibilityLabel()), "The entry follows the language");
            host.look.language = c::Language::english;
            owner->setAppearance(host.look, h);
            // While the HUD closes, module input is refused (no press is queued).
            host.selected = c::Module::about;
            host.phase = c::SystemOverlayPhase::closing;
            owner->overlayClosing(h);
            check(integer(UIA_IsEnabledPropertyId) == 0 && invoke->Invoke() == static_cast<HRESULT>(UIA_E_ELEMENTNOTENABLED) &&
                  owner->message({window.h, notice, 0, 0}, h) && host.selected == c::Module::about, "A closing HUD disables the entry");
            host.selected = c::Module::power;
            settle(.7);
            check(owner->automationCount() == 1 && integer(UIA_IsOffscreenPropertyId) == 1, "The retracted badge leaves the entry without a frame");
            host.visible = false;
            host.phase.reset();
            (void)owner->hostEvent({Kind::overlayConcealed, 0}, h);
            VARIANT gone;
            VariantInit(&gone);
            check(owner->automationRefresh(nullptr) && owner->automationCount() == 0 &&
                  simple->GetPropertyValue(UIA_NamePropertyId, &gone) == static_cast<HRESULT>(UIA_E_ELEMENTNOTAVAILABLE) &&
                  invoke->Invoke() == static_cast<HRESULT>(UIA_E_ELEMENTNOTAVAILABLE), "The concealed HUD withdraws the entry");
            VariantClear(&gone);
            host.visible = true;
            host.phase = c::SystemOverlayPhase::opening;
            owner->setOverlayVisible(true, h);
            settle(.1);
            check(owner->automationRefresh(nullptr) && owner->automationCount() == 1, "Reopening lists a new entry");
            // allowsModuleInput is false until finishSystemOpening; the host's
            // refresh after didOpen keeps the entry and it reads enabled.
            ComPtr<IRawElementProviderFragment> reopened;
            ComPtr<IRawElementProviderSimple> reopenedSimple;
            ComPtr<IInvokeProvider> reopenedInvoke;
            const auto enabled = [&] {
                VARIANT v;
                VariantInit(&v);
                long out = -1;
                if (reopenedSimple && SUCCEEDED(reopenedSimple->GetPropertyValue(UIA_IsEnabledPropertyId, &v)) && v.vt == VT_BOOL) out = v.boolVal == VARIANT_TRUE ? 1 : 0;
                VariantClear(&v);
                return out;
            };
            check(owner->automationChild(0, &reopened) == S_OK && SUCCEEDED(reopened.As(&reopenedSimple)) && SUCCEEDED(reopened.As(&reopenedInvoke)) &&
                  enabled() == 0 && reopenedInvoke->Invoke() == static_cast<HRESULT>(UIA_E_ELEMENTNOTENABLED),
                  "An opening HUD lists the entry disabled and refuses presses");
            host.phase.reset();
            check(!owner->automationRefresh(nullptr) && owner->automationCount() == 1 && enabled() == 1,
                  "The refresh after opening completes keeps the entry and enables it");
            owner->automationDisconnect();
            check(owner->automationCount() == 0 && owner->automationRefresh(nullptr) && owner->automationCount() == 1, "Disconnect, then a refresh relists");
            owner->automationDisconnect();
        }
        settle(6);
        unsigned stepChecks{};
        double last = h;
        for (unsigned cycle = 0; cycle < 2; ++cycle) {
            a::ModuleCoverage step;
            step.cycle = cycle;
            step.time = last;
            step.present = [&](double at) { check(at >= last, "Coverage presents on a non-decreasing clock"); last = at; frame(at); };
            step.drain = [&] { (void)drain(last); };
            step.project = [](c::Point, double, double) { return std::optional<c::Point>{}; };
            step.check = [&](bool value, std::string_view why) { ++stepChecks; if (!value) throw std::runtime_error(std::string(why)); };
            step.message = [&](std::uint32_t id, std::uintptr_t w, std::intptr_t l) { return owner->message({window.h, id, w, l}, last); };
            owner->coverage(step);
            frames(last, last + 5); // the next cycle's modules
            last += 5;
        }
        check(stepChecks > 60 && host.selected == c::Module::power && host.settingsUpdates == 10, "The coverage step repeats over the same host");
        check(host.configuration == ehud::data::Settings::defaults(), "Coverage restores the preference record");

        owner->release(renderer);
        owner.reset();
        check(!host.find<p::PowerAreaService>(p::PowerAreaService::key) && host.services() == 1 && !ownAlertWindow(),
              "Destruction withdraws the service and closes the alert window");
        host.withdraw(p::powerPageSinkKey);
    }
    executor.shutdown();
    check(notified > 0 && renderer.stats().textures == 0 && !IsWindowVisible(window.h), "Capacity reads used the executor; the badge released its textures");
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    const auto result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    try {
        check(SUCCEEDED(result) && argc == 2, "Pass HLSL to the synthetic Power area test");
        run(argv[1]);
        CoUninitialize();
        std::cout << "Power area: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        if (SUCCEEDED(result)) CoUninitialize();
        std::cerr << "Power area after " << checks << ": " << e.what() << '\n';
        return 1;
    }
}
#else
int main() { return 0; }
#endif
