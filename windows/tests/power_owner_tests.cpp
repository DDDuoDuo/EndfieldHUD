// Hidden WARP fixture for the Power production owner: synthetic provider
// snapshots, an injected battery-class reader, injected screens and in-memory
// callbacks. No power device, settings file, Event Log store or visible window.
#ifdef _WIN32
#include "tools/power_owner.hpp"
#include <objbase.h>
#include <atomic>
#include <cmath>
#include <iostream>

namespace {
namespace gpu = endfield::native;
namespace c = endfield::core;
namespace m = endfield::modules;
namespace t = endfield::tools;
using Json = ehud::data::Json;
unsigned checks{};
void check(bool v, const std::string& message) {
    ++checks;
    if (!v) throw std::runtime_error(message);
}
gpu::BatterySnapshot snapshot(unsigned percent, bool ac, bool charging) {
    gpu::BatterySnapshot s;
    s.available = true; s.present = true; s.percent = percent; s.ac_connected = ac; s.charging = charging;
    s.fully_charged = ac && !charging && percent == 100;
    return s;
}

void run(const std::filesystem::path& shader) {
    std::atomic<unsigned> notified{};
    endfield::app::UtilityExecutor executor([&] { ++notified; });
    gpu::LayerRasterizer raster;
    std::vector<std::pair<m::EventKind, m::EventMetadata>> events;
    std::vector<m::BatteryReading> readings;
    std::vector<m::ChargePosition> saved;
    unsigned editsFinished{}, reads{};
    t::PowerOwnerCallbacks callbacks;
    callbacks.recordEvent = [&](m::EventKind kind, const m::EventMetadata& metadata) { events.emplace_back(kind, metadata); };
    callbacks.readingChanged = [&](const m::BatteryReading& r) { readings.push_back(r); };
    callbacks.savePosition = [&](const m::ChargePosition& p) { saved.push_back(p); };
    callbacks.positionEditFinished = [&] { ++editsFinished; };
    {
        t::PowerOwner owner(executor, raster, {shader, true}, callbacks, [&] {
            ++reads;
            gpu::BatteryDeviceRead read;
            gpu::BatteryDeviceReport report;
            report.capabilities = gpu::BatteryDeviceReport::systemBattery;
            report.fullChargedCapacity = 52'000;
            report.remainingCapacity = 41'500;
            read.reports.push_back(report);
            return read;
        });
        const std::vector<m::ChargeWorkArea> screens{{11, {0, 0, 1440, 900}, 1}};
        owner.panel().setScreens(screens, 0);
        owner.setSettings(Json::Object{}, true, c::Language::english, false, 0);
        auto drain = [&] { executor.waitIdle(); executor.drain(); };
        auto run = [&](double from, double to) { for (double time = from; time <= to + 1e-9; time += 1.0 / 60) owner.advance(time); };
        // Startup baseline: no alert, no event, percent pair first, then mWh.
        owner.receive(snapshot(19, false, false), 1);
        check(readings.size() == 1 && readings[0].capacity == m::BatteryReading::Capacity{19, 100, "%"} && events.empty() &&
              !owner.panel().state().visible(), "Startup reading is a baseline");
        drain();
        check(reads == 1 && readings.size() == 2 && readings[1].capacity == m::BatteryReading::Capacity{41'500, 52'000, "mWh"} &&
              !owner.panel().state().visible(), "Capacity pair arrives without an alert");
        check(owner.trayStatus(c::Language::english) == "19% · On battery" &&
              owner.trayTooltip(c::Language::simplifiedChinese) == "EndfieldHUD — 19% · 使用电池", "Tray status line");
        // Plug in: CHARGE MODE immediately, connection events recorded.
        owner.receive(snapshot(19, true, false), 2);
        check(events.size() == 2 && events[0].first == m::EventKind::powerConnected && events[0].second.at("state") == "connected" &&
              events[1].first == m::EventKind::batteryStateChanged, "Power connection recorded with allowlisted metadata");
        owner.advance(2);
        check(owner.panel().state().windowVisible() && owner.requiresFrames(2.05), "Plug-in presents the alert at once");
        check(owner.panel().accessibilityLabel() == "EndfieldHUD, Battery: 19%, Power connected", "Charge mode on AC, before charging begins");
        drain();
        run(2, 3.6);
        check(owner.panel().state().stage() == m::ChargeStage::compact && owner.nextWakeTime() && std::abs(*owner.nextWakeTime() - 6.5) < 1e-6,
              "Compact alert with a 3 s deadline");
        // Charging begins later: data stays live, no replay or deadline extension.
        owner.receive(snapshot(20, true, true), 4);
        owner.advance(4);
        check(owner.panel().accessibilityLabel() == "EndfieldHUD, Battery: 20%, Charging" && std::abs(*owner.nextWakeTime() - 6.5) < 1e-6,
              "Charging diagnostic updates without replaying");
        drain();
        run(6.5, 7.3);
        check(!owner.panel().state().windowVisible() && !owner.nextWakeTime() && !owner.requiresFrames(7.3), "Dismissed alert releases frames");
        // HUD open: no alert; closes; the next transition presents.
        owner.systemOverlayOpening(8);
        owner.receive(snapshot(21, false, false), 8.5);
        owner.advance(8.5);
        check(!owner.panel().state().windowVisible(), "No alert while the HUD is open");
        owner.systemOverlayClosed(9);
        drain();
        owner.receive(snapshot(21, true, false), 10);
        owner.advance(10);
        check(owner.panel().state().windowVisible(), "Alert resumes after the HUD closes");
        drain();
        run(10, 14);
        // Tray preview of the current reading.
        owner.preview(15);
        owner.advance(15);
        check(owner.panel().state().windowVisible() && owner.panel().state().previewContent(), "Tray preview presents with the preview label");
        run(15, 20);
        // Position editing from Settings (after the HUD closed).
        check(owner.editPosition(21) && owner.panel().state().editingPosition(), "Editor opens");
        owner.panel().pointer(gpu::ChargeIndicatorPanel::PointerKind::down, {720, 30}, 21);
        owner.panel().pointer(gpu::ChargeIndicatorPanel::PointerKind::move, {720, 830}, 21);
        owner.panel().pointer(gpu::ChargeIndicatorPanel::PointerKind::up, {720, 830}, 21);
        check(owner.panel().key(VK_RETURN, 22) && saved.size() == 1 && editsFinished == 1 && saved[0].screenID == 11u &&
              std::abs(saved[0].y - 70.0 / 900) < 1e-12, "Confirmed position is persisted through the callback");
        check(owner.coordinator().settings().customPlacement && std::abs(owner.coordinator().settings().customY - 70.0 / 900) < 1e-12,
              "The new placement applies at once");
        // Sleep with a plug change: shown exactly once on wake.
        owner.suspend(m::PowerSuspension::system, 30);
        owner.resume(m::PowerSuspension::system, snapshot(25, false, false), 31);
        owner.advance(31);
        check(owner.panel().state().windowVisible(), "Unplugged during sleep: shown once on wake");
        const auto rect = owner.panel().windowRect();
        check(std::abs(rect.y - (830 - 42)) < 1e-9, "Saved custom position is used");
        drain();
        run(31, 36);
        owner.terminate(40);
        check(!owner.panel().window(), "Termination releases the panel");
        owner.receive(snapshot(30, true, true), 41);
        check(!owner.panel().state().visible(), "No alert after termination");
    }
    executor.shutdown();
    check(notified > 0, "Capacity reads completed through the owner notification");
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    const auto result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    try {
        check(SUCCEEDED(result) && argc == 2, "Pass HLSL to the synthetic power owner test");
        run(argv[1]);
        CoUninitialize();
        std::cout << "Power owner: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        if (SUCCEEDED(result)) CoUninitialize();
        std::cerr << "Power owner after " << checks << ": " << e.what() << '\n';
        return 1;
    }
}
#else
int main() { return 0; }
#endif
