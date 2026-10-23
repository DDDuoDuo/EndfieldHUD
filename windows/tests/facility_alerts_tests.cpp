// Facility alert contracts (Sources/FacilityAlertMonitor.swift, SHELL-73..75):
// the kinds' modules and strings in the five languages, the readings'
// validation and rounding, the Windows memory-pressure mapping (D4), and the
// monitor: enable/disable, edge triggering, dismiss, the disk hold band, the
// 120 s recovery hold, isNew/markPresented, unavailable sources, the usage
// and temperature read rules, sample coalescing and stale results.
#include "modules/facility_alerts.hpp"
#include <cmath>
#include <deque>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace {
namespace m = endfield::modules;
using endfield::core::Language;
using endfield::core::Module;
using m::FacilityAlertKind;
using m::FacilityMemoryPressure;
using m::FacilityThermalState;
unsigned checks{};
void check(bool v, const std::string& what) { ++checks; if (!v) throw std::runtime_error(what); }
template <class A, class B> void expect(const A& a, const B& b, const std::string& what) {
    ++checks;
    if (!(a == b)) { std::ostringstream s; s << what << ": got [" << a << "] expected [" << b << "]"; throw std::runtime_error(s.str()); }
}

m::FacilityAlert disk(std::int64_t total, std::int64_t available, std::string name = "Macintosh HD") {
    return {{FacilityAlertKind::disk, m::FacilityDiskReading::make(std::move(name), total, available), {}, {}}, true};
}
m::FacilityAlert memory(FacilityMemoryPressure p, std::optional<m::FacilityMemoryUsage> usage) {
    return {{FacilityAlertKind::memory, {}, m::FacilityMemoryReading{p, usage}, {}}, true};
}
m::FacilityAlert heat(FacilityThermalState s, std::optional<double> celsius) {
    return {{FacilityAlertKind::heat, {}, {}, m::FacilityThermalReading{s, celsius}}, true};
}

void strings() {
    expect(m::facilityAlertID(FacilityAlertKind::disk), std::string_view("disk"), "disk id");
    expect(m::facilityAlertID(FacilityAlertKind::memory), std::string_view("mem"), "memory id");
    expect(m::facilityAlertID(FacilityAlertKind::heat), std::string_view("heat"), "heat id");
    check(m::facilityAlertModule(FacilityAlertKind::disk) == Module::storage, "disk opens Storage");
    check(m::facilityAlertModule(FacilityAlertKind::memory) == Module::activityMonitor, "memory opens Activity Monitor");
    check(m::facilityAlertModule(FacilityAlertKind::heat) == Module::power, "heat opens Power");
    expect(m::facilityAlertTitle(FacilityAlertKind::disk, Language::english), std::string("LOW DISK SPACE"), "disk title");
    expect(m::facilityAlertTitle(FacilityAlertKind::disk, Language::simplifiedChinese), std::string("存储空间不足"), "disk title zh");
    expect(m::facilityAlertTitle(FacilityAlertKind::memory, Language::simplifiedChinese), std::string("内存压力过高"), "memory title zh");
    expect(m::facilityAlertTitle(FacilityAlertKind::heat, Language::english), std::string("OVERHEATING"), "heat title");
    expect(m::facilityAlertTitle(FacilityAlertKind::heat, Language::simplifiedChinese), std::string("设备过热"), "heat title zh");
    for (const auto l : {Language::traditionalChinese, Language::japanese, Language::korean})
        for (const auto k : m::facilityAlertKinds) {
            const auto t = m::facilityAlertTitle(k, l);
            check(!t.empty() && t != m::facilityAlertTitle(k, Language::english), "every title is translated in every CJK language");
        }
    // Disk: 8 % of 500 GB free.
    const auto d = disk(500'000'000'000, 40'000'000'000);
    expect(d.value(Language::english), std::string("8%"), "disk value");
    expect(d.unit(Language::english), std::string("FREE"), "disk unit");
    expect(d.unit(Language::simplifiedChinese), std::string("可用"), "disk unit zh");
    expect(d.detail(Language::english), std::string("Macintosh HD  40.0 GB left"), "disk detail");
    expect(d.detail(Language::simplifiedChinese), std::string("Macintosh HD  剩余 40.0 GB"), "disk detail zh");
    expect(d.tagText(Language::english), std::string("DISK 8% FREE"), "disk tag");
    expect(d.tagText(Language::simplifiedChinese), std::string("磁盘 8% 可用"), "disk tag zh");
    expect(disk(4'000'000'000'000, 1'860'000'000'000).detail(Language::english), std::string("Macintosh HD  1.86 TB left"), "terabytes from 1000 GB");
    expect(disk(1'000, 125).value(Language::english), std::string("13%"), "12.5 % rounds half away from zero");
    // Memory: usage when read, else the pressure word.
    const auto mem = memory(FacilityMemoryPressure::warning, m::FacilityMemoryUsage::make(14'720'000'000, 16'000'000'000));
    expect(mem.value(Language::english), std::string("92%"), "memory value");
    expect(mem.unit(Language::english), std::string("PRESSURE"), "memory unit");
    expect(mem.detail(Language::english), std::string("14.7 / 16.0 GB in use"), "memory detail");
    expect(mem.detail(Language::simplifiedChinese), std::string("已用 14.7 / 16.0 GB"), "memory detail zh");
    expect(mem.tagText(Language::english), std::string("RAM 92% PRESSURE"), "memory tag");
    expect(memory(FacilityMemoryPressure::warning, {}).value(Language::english), std::string("WARNING"), "warning without usage");
    expect(memory(FacilityMemoryPressure::critical, {}).value(Language::simplifiedChinese), std::string("危急"), "critical zh");
    expect(memory(FacilityMemoryPressure::critical, {}).detail(Language::english), std::string(), "no detail without usage");
    // Heat (no Windows source; the rules stay for parity).
    expect(heat(FacilityThermalState::serious, 91.6).value(Language::english), std::string("92°C"), "heat value");
    expect(heat(FacilityThermalState::serious, 91.6).unit(Language::english), std::string("BATTERY"), "heat unit with a temperature");
    expect(heat(FacilityThermalState::critical, {}).value(Language::english), std::string("CRITICAL"), "heat critical");
    expect(heat(FacilityThermalState::serious, {}).value(Language::simplifiedChinese), std::string("严重"), "heat serious zh");
    expect(heat(FacilityThermalState::serious, {}).unit(Language::english), std::string("THERMAL STATE"), "heat unit without temperature");
    expect(heat(FacilityThermalState::serious, {}).detail(Language::english), std::string("Fans at maximum"), "heat detail");
    expect(heat(FacilityThermalState::serious, 91.6).tagText(Language::english), std::string("HEAT 92°C"), "heat tag has no suffix");
    expect(heat(FacilityThermalState::serious, 91.6).tagText(Language::simplifiedChinese), std::string("温度 92°C"), "heat tag zh");
}

void readings() {
    check(!m::FacilityDiskReading::make("x", 0, 0), "a zero total is unavailable");
    check(!m::FacilityDiskReading::make("x", 10, 11), "more available than total is unavailable");
    check(!m::FacilityDiskReading::make("x", 10, -1), "negative available is unavailable");
    check(m::FacilityDiskReading::make("x", 10, 10).has_value(), "all free is a reading");
    check(!m::FacilityMemoryUsage::make(1, 0), "memory without a total");
    expect(m::FacilityMemoryUsage::make(20, 10)->usedBytes, std::uint64_t(10), "used clamps to total");
    // D4: warning from 90 % in use, critical from 95 % or while Windows signals low memory.
    check(m::facilityMemoryPressure(899, 1000, false) == FacilityMemoryPressure::normal, "89.9 % is normal");
    check(m::facilityMemoryPressure(900, 1000, false) == FacilityMemoryPressure::warning, "90 % warns");
    check(m::facilityMemoryPressure(949, 1000, false) == FacilityMemoryPressure::warning, "94.9 % warns");
    check(m::facilityMemoryPressure(950, 1000, false) == FacilityMemoryPressure::critical, "95 % is critical");
    check(m::facilityMemoryPressure(100, 1000, true) == FacilityMemoryPressure::critical, "the low-memory signal is critical");
    check(!m::facilityMemoryPressure(1, 0, true), "no total, no pressure");
}

// Synthetic sources: a manual clock and either a synchronous or a queued worker.
struct Rig {
    double now{100};
    std::optional<m::FacilityDiskReading> diskReading = m::FacilityDiskReading::make("Local Disk", 1000, 500);
    std::optional<FacilityMemoryPressure> pressure = FacilityMemoryPressure::normal;
    std::optional<m::FacilityMemoryUsage> usage = m::FacilityMemoryUsage::make(500, 1000);
    std::optional<FacilityThermalState> thermal;
    std::optional<double> temperature = 45;
    unsigned usageReads{}, temperatureReads{}, diskReads{};
    bool queued{};
    std::deque<std::pair<std::function<m::FacilityReadings()>, std::function<void(m::FacilityReadings)>>> jobs;
    m::FacilityAlertMonitor::Sources sources(bool withThermal) {
        m::FacilityAlertMonitor::Sources s;
        s.readDisk = [this] { ++diskReads; return diskReading; };
        s.readMemoryPressure = [this] { return pressure; };
        s.readMemoryUsage = [this] { ++usageReads; return usage; };
        if (withThermal) s.readThermalState = [this] { return thermal; };
        s.readTemperature = [this] { ++temperatureReads; return temperature; };
        s.runWorker = [this](auto read, auto apply) { if (queued) jobs.emplace_back(std::move(read), std::move(apply)); else apply(read()); };
        s.clock = [this] { return now; };
        return s;
    }
    void runJob() { auto job = std::move(jobs.front()); jobs.pop_front(); job.second(job.first()); }
};

void monitor() {
    Rig rig;
    m::FacilityAlertMonitor monitor(rig.sources(false));
    unsigned changes{};
    monitor.onChange = [&] { ++changes; };
    // Off: nothing is read.
    monitor.checkNow();
    check(rig.diskReads == 0 && monitor.alerts().empty(), "a disabled monitor reads nothing");
    // On: every source at once; nominal readings raise nothing.
    monitor.setEnabled(true);
    check(rig.diskReads == 1, "enabling samples at once");
    check(monitor.alerts().empty() && changes == 0, "nominal readings list nothing");
    check(monitor.readings().disk && monitor.readings().memory && !monitor.readings().thermal, "readings kept; no thermal source on Windows");
    check(rig.usageReads == 0, "memory usage is not read at normal pressure");
    // Disk below 10 %: raised, new.
    rig.diskReading = m::FacilityDiskReading::make("Local Disk", 1000, 99);
    monitor.checkNow();
    check(monitor.alerts().size() == 1 && monitor.alerts()[0].kind() == FacilityAlertKind::disk && monitor.alerts()[0].isNew, "disk below 10 % raises a new alert");
    check(monitor.isStorageLow(), "the storage hint follows the condition");
    expect(changes, 1u, "one change for the raise");
    monitor.markPresented(FacilityAlertKind::disk);
    check(monitor.newAlerts().empty() && monitor.alerts().size() == 1, "presented alerts stay listed but are not new");
    // The same condition again updates the measurement, never re-raises.
    rig.diskReading = m::FacilityDiskReading::make("Local Disk", 1000, 50);
    monitor.checkNow();
    check(monitor.alerts().size() == 1 && !monitor.alerts()[0].isNew && monitor.alerts()[0].measurement.disk->availableBytes == 50, "the listed alert follows the reading");
    // Dismissed: stays away while the condition lasts.
    monitor.dismiss(FacilityAlertKind::disk);
    check(monitor.alerts().empty() && monitor.isStorageLow(), "dismiss removes the alert, the condition stays");
    monitor.checkNow();
    check(monitor.alerts().empty(), "a dismissed alert stays away while its condition lasts");
    // 10...12 % holds (no recovery clock), >= 12 % recovers only after 120 s.
    rig.diskReading = m::FacilityDiskReading::make("Local Disk", 1000, 110);
    rig.now = 110; monitor.checkNow();
    rig.now = 400; monitor.checkNow();
    check(monitor.isStorageLow(), "the hold band never recovers");
    rig.diskReading = m::FacilityDiskReading::make("Local Disk", 1000, 120);
    rig.now = 500; monitor.checkNow();
    rig.now = 619; monitor.checkNow();
    check(monitor.isStorageLow(), "recovery needs 120 s of recovered readings");
    rig.diskReading = m::FacilityDiskReading::make("Local Disk", 1000, 115);
    rig.now = 630; monitor.checkNow();
    rig.diskReading = m::FacilityDiskReading::make("Local Disk", 1000, 200);
    rig.now = 640; monitor.checkNow();
    rig.now = 759; monitor.checkNow();
    check(monitor.isStorageLow(), "a hold reading restarts the recovery clock");
    rig.now = 760; monitor.checkNow();
    check(!monitor.isStorageLow(), "120 s of recovery clears the condition");
    // Cleared: the next low reading raises again (a new alert).
    rig.diskReading = m::FacilityDiskReading::make("Local Disk", 1000, 10);
    rig.now = 770; monitor.checkNow();
    check(monitor.alerts().size() == 1 && monitor.alerts()[0].isNew, "after clearing, the condition raises again");
    // Memory: any elevated pressure raises; usage is read while elevated or active.
    rig.pressure = FacilityMemoryPressure::warning;
    monitor.checkNow();
    check(monitor.alerts().size() == 2 && monitor.alerts()[1].kind() == FacilityAlertKind::memory, "memory joins the list in raise order");
    check(rig.usageReads == 1 && monitor.alerts()[1].measurement.memory->usage, "usage read while the pressure is elevated");
    rig.pressure = FacilityMemoryPressure::normal;
    rig.now = 800; monitor.checkNow();
    check(rig.usageReads == 2, "usage still read at normal pressure while the alert is active");
    check(monitor.alert(FacilityAlertKind::memory) && monitor.alert(FacilityAlertKind::memory)->measurement.memory->pressure == FacilityMemoryPressure::normal,
        "a recovering alert stays listed with its latest reading");
    rig.now = 920; monitor.checkNow();
    check(!monitor.alert(FacilityAlertKind::memory), "a recovered alert leaves the list");
    rig.now = 930; monitor.checkNow();
    check(rig.usageReads == 3, "after recovery, usage is not read at normal pressure (the clearing sample still wanted it)");
    rig.now = 940; monitor.checkNow();
    check(rig.usageReads == 3, "no usage read at normal pressure once inactive");
    // An unavailable source keeps its last reading and state.
    rig.diskReading.reset();
    monitor.checkNow();
    check(monitor.readings().disk && monitor.alert(FacilityAlertKind::disk), "an unavailable disk keeps its reading and alert");
    // External readings (the Activity samples' memory) apply like a sample.
    monitor.apply({{}, m::FacilityMemoryReading{FacilityMemoryPressure::critical, {}}, {}});
    check(monitor.alert(FacilityAlertKind::memory) && monitor.alert(FacilityAlertKind::memory)->value(Language::english) == "CRITICAL", "an applied memory reading raises");
    // Off clears everything; on samples again.
    monitor.setEnabled(false);
    check(monitor.alerts().empty() && !monitor.readings().disk && !monitor.isStorageLow(), "off clears alerts, conditions and readings");
    monitor.apply({{}, m::FacilityMemoryReading{FacilityMemoryPressure::critical, {}}, {}});
    check(monitor.alerts().empty(), "a disabled monitor ignores applied readings");
}

void thermalAndCoalescing() {
    Rig rig;
    rig.thermal = FacilityThermalState::nominal;
    m::FacilityAlertMonitor monitor(rig.sources(true));
    monitor.setEnabled(true);
    check(rig.temperatureReads == 0 && monitor.readings().thermal, "no temperature below serious");
    rig.thermal = FacilityThermalState::serious;
    monitor.checkNow();
    check(rig.temperatureReads == 1 && monitor.isTemperatureHigh() && monitor.temperatureCelsius() == 45.0, "serious raises heat with the temperature");
    rig.thermal = FacilityThermalState::fair;
    monitor.checkNow();
    check(rig.temperatureReads == 2, "temperature read while the heat condition is active");
    // A queued worker: a request during a sample coalesces into one more.
    Rig slow;
    slow.queued = true;
    m::FacilityAlertMonitor queued(slow.sources(false));
    queued.setEnabled(true);
    queued.checkNow(); queued.checkNow();
    check(slow.jobs.size() == 1 && queued.sampling(), "one read in flight");
    slow.diskReading = m::FacilityDiskReading::make("Local Disk", 1000, 1);
    slow.runJob();
    check(slow.jobs.size() == 1, "requests during the read coalesce into one more");
    slow.runJob();
    check(queued.alert(FacilityAlertKind::disk) && slow.jobs.empty() && !queued.sampling(), "the trailing read applies");
    // A result from before a disable / enable is discarded.
    queued.checkNow();
    queued.setEnabled(false);
    queued.setEnabled(true);
    check(slow.jobs.size() == 1, "a re-enable while sampling waits for the read in flight");
    slow.runJob();
    check(!queued.alert(FacilityAlertKind::disk), "a stale result from the previous generation is dropped");
    check(slow.jobs.size() == 1, "the enable's own request follows");
    slow.runJob();
    check(queued.alert(FacilityAlertKind::disk) != nullptr, "the fresh read applies");
}
}

int main() {
    try {
        strings();
        readings();
        monitor();
        thermalAndCoalescing();
        std::cout << "facility alert contracts passed (" << checks << " checks)\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAILED: " << e.what() << '\n';
        return 1;
    }
}
