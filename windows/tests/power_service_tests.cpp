// Synthetic battery-class reports and an injected reader only: no battery
// device, power notification, settings file or user data is touched.
#include "native/power_service.hpp"
#include <atomic>
#include <condition_variable>
#include <iostream>
#include <mutex>

using namespace endfield;
using namespace endfield::native;
using Capacity = modules::BatteryReading::Capacity;

namespace {
unsigned checks{};
void check(bool value, const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
BatteryDeviceReport absolute(std::uint32_t remaining, std::uint32_t full) {
    BatteryDeviceReport r;
    r.capabilities = BatteryDeviceReport::systemBattery;
    r.designedCapacity = 60'000;
    r.fullChargedCapacity = full;
    r.remainingCapacity = remaining;
    return r;
}

void decoder() {
    const std::vector<BatteryDeviceReport> one{absolute(41'500, 52'000)};
    check(decodeBatteryCapacity(one, 80) == Capacity{41'500, 52'000, "mWh"}, "Matching mWh pair from the class driver");
    const std::vector<BatteryDeviceReport> two{absolute(20'000, 24'000), absolute(30'000, 46'000)};
    check(decodeBatteryCapacity(two, 71) == Capacity{50'000, 70'000, "mWh"}, "Two system batteries aggregate like the OS percentage");
    auto relative = absolute(64, 100);
    relative.capabilities |= BatteryDeviceReport::relativeCapacity;
    check(decodeBatteryCapacity(std::vector{relative}, 64) == Capacity{64, 100, "%"}, "Relative hardware scale is never labelled mWh");
    auto ups = absolute(1'000, 9'000);
    ups.capabilities |= BatteryDeviceReport::shortTerm;
    check(decodeBatteryCapacity(std::vector{ups, absolute(41'500, 52'000)}, 80) == Capacity{41'500, 52'000, "mWh"},
          "A UPS never stands in for the machine battery");
    auto external = absolute(5'000, 9'000);
    external.capabilities = 0;
    check(decodeBatteryCapacity(std::vector{external}, 55) == Capacity{55, 100, "%"}, "Non-system batteries fall back to percent");
    auto unknown = absolute(BatteryDeviceReport::unknownCapacity, 52'000);
    check(decodeBatteryCapacity(std::vector{unknown, absolute(1, 50'000)}, 12) == Capacity{12, 100, "%"},
          "A partial set never mixes estimated and measured energy");
    auto noStatus = absolute(0, 52'000);
    noStatus.remainingCapacity.reset();
    check(decodeBatteryCapacity(std::vector{noStatus}, 12) == Capacity{12, 100, "%"}, "Failed status query falls back");
    check(decodeBatteryCapacity(std::vector{absolute(52'001, 52'000)}, 100) == Capacity{100, 100, "%"},
          "Overfull hardware reports follow the source pair rule");
    check(!decodeBatteryCapacity(std::vector{absolute(52'001, 52'000)}, std::nullopt), "Unknown percentage stays unavailable");
    check(decodeBatteryCapacity({}, 33) == Capacity{33, 100, "%"}, "No class device keeps the explicit percent pair");
    static_assert(!batteryHealthCategoryAvailable, "Windows has no OS battery condition category");
}

void conversion() {
    BatterySnapshot s;
    check(!batteryReadingFromSnapshot(s).present, "Unavailable provider is no battery");
    s.available = true;
    check(!batteryReadingFromSnapshot(s).present, "Unknown presence is never fabricated");
    s.present = true; s.ac_connected = true; s.charging = false; s.percent = 100; s.fully_charged = true;
    auto r = batteryReadingFromSnapshot(s);
    check(r.present && r.pluggedIn && !r.charging && r.fullyCharged && r.percentage == 100u && !r.capacity, "Full on AC");
    s.ac_connected = false; s.charging = true; s.fully_charged = false; s.percent = 40;
    r = batteryReadingFromSnapshot(s);
    check(!r.pluggedIn && !r.charging, "Explicit offline AC wins over a contradictory charging flag");
    s.present = false;
    check(batteryReadingFromSnapshot(s) == modules::BatteryReading{}, "Desktop without a battery");
}

struct Gate {
    std::mutex mutex;
    std::condition_variable condition;
    bool open{true};
    std::atomic<unsigned> calls{};
    void wait() {
        ++calls;
        std::unique_lock lock(mutex);
        condition.wait(lock, [&] { return open; });
    }
    void set(bool value) { { std::lock_guard lock(mutex); open = value; } condition.notify_all(); }
};

void coalescing() {
    std::atomic<unsigned> notified{};
    app::UtilityExecutor executor([&] { ++notified; });
    Gate gate;
    std::vector<modules::BatteryReading> published;
    std::uint32_t remaining = 30'000;
    PowerService service(executor, [&](const modules::BatteryReading& r) { published.push_back(r); },
        [&] { gate.wait(); BatteryDeviceRead read; read.reports.push_back(absolute(remaining, 52'000)); return read; });
    BatterySnapshot s;
    s.available = true; s.present = true; s.ac_connected = false; s.charging = false; s.percent = 57;
    gate.set(false);
    service.receive(s);
    check(published.size() == 1 && published[0].capacity == Capacity{57, 100, "%"} && service.capacityPending() &&
          service.capacityReads() == 1, "First event publishes at once and starts one capacity read");
    // A WM_POWERBROADCAST storm while the read is blocked: one trailing read.
    for (unsigned n = 0; n < 200; ++n) { s.percent = 57 - n % 2; service.receive(s); }
    check(service.capacityReads() == 1, "No read is queued behind the in-flight read");
    remaining = 29'000;
    gate.set(true);
    executor.waitIdle();
    executor.drain();
    executor.waitIdle();
    executor.drain();
    check(service.capacityReads() == 2 && gate.calls == 2 && !service.capacityPending(),
          "The whole burst costs exactly one trailing read");
    check(service.reading().capacity == Capacity{29'000, 52'000, "mWh"}, "Trailing read publishes the newest pair");
    const auto publishes = service.publishedReadings();
    service.receive(s);
    executor.waitIdle();
    executor.drain();
    check(service.publishedReadings() == publishes && service.capacityReads() == 3, "Unchanged reading does not republish");
    // No battery: no device reads at all.
    BatterySnapshot desktop;
    desktop.available = true; desktop.present = false;
    service.receive(desktop);
    check(service.capacityReads() == 3 && !service.reading().present && !service.reading().capacity,
          "Desktop machines never open the battery class");
    executor.waitIdle();
    executor.drain();
    // Stop is terminal: later events and stale completions are ignored.
    service.receive(s);
    gate.set(false);
    service.stop();
    const auto count = published.size();
    gate.set(true);
    executor.waitIdle();
    executor.drain();
    service.receive(s);
    check(published.size() == count, "Stopped service publishes nothing");
    check(notified > 0, "Completions arrive through the owner notification");
    executor.shutdown();
}
} // namespace

int main() {
    try {
        decoder();
        conversion();
        coalescing();
        std::cout << "Power service: " << checks << " synthetic checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Power service after " << checks << " checks: " << error.what() << '\n';
        return 1;
    }
}
