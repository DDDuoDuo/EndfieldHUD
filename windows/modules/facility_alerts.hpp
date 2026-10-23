#pragma once
// Facility alerts (spec §3.29): the PC treated as an outpost. The port of
// Sources/FacilityAlertMonitor.swift (FacilityAlertKind, the readings,
// FacilityAlert, FacilityAlertMonitor). SHELL-73, SHELL-74, SHELL-75.
//
// Portable and explicit-clock: the monitor holds the alert list and the
// edge-triggered conditions exactly as the Mac's; the native owner
// (native/facility_owner.hpp) supplies the readers, the worker and the
// cadence. No timer, thread, observer or OS call here.
//
// Windows sources (D4, native/facility_sources.hpp):
//  - disk: the Storage page's startup-volume figure (readWindowsStartupCapacity:
//    GetDiskFreeSpaceExW free-to-caller over total of the system volume), so
//    the alert and the page agree, as the Mac shares StorageController's;
//  - memory pressure: Windows has no pressure levels. facilityMemoryPressure
//    maps the memory load (physical memory in use, the Activity page's
//    total-minus-available, dwMemoryLoad's basis) and the system's
//    LowMemoryResourceNotification onto the Mac's levels: warning from 90 %
//    in use, critical from 95 % or while Windows signals low memory, normal
//    otherwise (the Mac warns when the kernel starts reclaiming in earnest and
//    is critical when it is about to run out);
//  - thermal: no documented user-mode thermal state or temperature: the
//    reader is absent, the heat kind is never raised on Windows (recorded
//    platform gap; the kind, its strings and its rules stay for parity).
// Declared but not wired, as on the Mac (SHELL-74): the side-effect hints
// isStorageLow / isTemperatureHigh / temperatureCelsius exist for parity;
// Storage's red cells and Battery's orange temperature use their own rules.
#include "core/localization.hpp"
#include "core/motion.hpp"
#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace endfield::modules {
// FacilityAlertKind (:7-33): raw values are the spec's alert ids.
enum class FacilityAlertKind : std::uint8_t { disk, memory, heat };
inline constexpr std::array<FacilityAlertKind, 3> facilityAlertKinds{FacilityAlertKind::disk, FacilityAlertKind::memory, FacilityAlertKind::heat};
std::string_view facilityAlertID(FacilityAlertKind) noexcept;        // "disk", "mem", "heat"
// Clicking the banner or the tag opens this module (Storage, Activity Monitor, Power).
core::Module facilityAlertModule(FacilityAlertKind) noexcept;
// The title in `language` (the UI language, English, or Simplified Chinese
// for the CJK name): LOW DISK SPACE / 存储空间不足, ...
std::string facilityAlertTitle(FacilityAlertKind, core::Language);

// FacilityDiskReading (:36-48): nullopt unless 0 < total and 0 <= available <= total.
struct FacilityDiskReading {
    std::string volumeName;
    std::int64_t totalBytes{}, availableBytes{};
    static std::optional<FacilityDiskReading> make(std::string volumeName, std::int64_t totalBytes, std::int64_t availableBytes);
    double freeFraction() const noexcept { return double(availableBytes) / double(totalBytes); }
    int freePercent() const noexcept;                                // (fraction x 100).rounded()
    bool operator==(const FacilityDiskReading&) const = default;
};
// FacilityMemoryPressure (:51-64).
enum class FacilityMemoryPressure : std::uint8_t { normal, warning, critical };
// The Windows mapping (see above): `used` / `total` bytes in use and the
// LowMemoryResourceNotification state; nullopt without a total.
std::optional<FacilityMemoryPressure> facilityMemoryPressure(std::uint64_t used, std::uint64_t total, bool lowMemory) noexcept;
inline constexpr double facilityMemoryWarningLoad = .90, facilityMemoryCriticalLoad = .95;
// FacilityMemoryUsage (:67-77): used clamped to total; nullopt without a total.
struct FacilityMemoryUsage {
    std::uint64_t usedBytes{}, totalBytes{};
    static std::optional<FacilityMemoryUsage> make(std::uint64_t usedBytes, std::uint64_t totalBytes);
    int usedPercent() const noexcept;
    bool operator==(const FacilityMemoryUsage&) const = default;
};
struct FacilityMemoryReading {
    FacilityMemoryPressure pressure{};
    std::optional<FacilityMemoryUsage> usage; // read only while pressure is elevated or the alert is active
    bool operator==(const FacilityMemoryReading&) const = default;
};
// FacilityThermalState / FacilityThermalReading (:85-107). No Windows source.
enum class FacilityThermalState : std::uint8_t { nominal, fair, serious, critical };
struct FacilityThermalReading {
    FacilityThermalState state{};
    std::optional<double> temperatureCelsius;
    bool operator==(const FacilityThermalReading&) const = default;
};
// The latest value of every source; nullopt means unavailable (or not sampled yet).
struct FacilityReadings {
    std::optional<FacilityDiskReading> disk;
    std::optional<FacilityMemoryReading> memory;
    std::optional<FacilityThermalReading> thermal;
    bool operator==(const FacilityReadings&) const = default;
};

// FacilityAlert (:111-200): one listed alert. Every string renders in the
// language passed, so a language change re-renders listed alerts.
struct FacilityAlert {
    struct Measurement {
        FacilityAlertKind kind{};
        std::optional<FacilityDiskReading> disk;
        std::optional<FacilityMemoryReading> memory;
        std::optional<FacilityThermalReading> heat;
        bool operator==(const Measurement&) const = default;
    };
    Measurement measurement;
    // True from the raise until FacilityAlertMonitor::markPresented: the
    // banner is shown only for new alerts; the tag stack shows every one.
    bool isNew{true};
    FacilityAlertKind kind() const noexcept { return measurement.kind; }
    std::string_view id() const noexcept { return facilityAlertID(kind()); }
    core::Module module() const noexcept { return facilityAlertModule(kind()); }
    std::string title(core::Language l) const { return facilityAlertTitle(kind(), l); }
    std::string englishTitle() const { return facilityAlertTitle(kind(), core::Language::english); }
    std::string chineseTitle() const { return facilityAlertTitle(kind(), core::Language::simplifiedChinese); }
    // The banner's big orange value and the tag's bold orange number.
    std::string value(core::Language) const;
    // The small label under the banner value.
    std::string unit(core::Language) const;
    // The banner's grey detail line ("" for memory without a usage).
    std::string detail(core::Language) const;
    // Tag text = label, bold orange value, then the optional suffix:
    // DISK 8% FREE, RAM 92% PRESSURE, HEAT 92°C.
    std::string tagLabel(core::Language) const;
    std::optional<std::string> tagSuffix(core::Language) const; // the unit; none for heat
    std::string tagText(core::Language) const;
    bool operator==(const FacilityAlert&) const = default;
};

// FacilityAlertMonitor (:217-443). Owner thread only. While disabled it holds
// no worker job and forgets everything; enabled, the owner asks for samples
// (checkNow) on its cadence and wake; reads run on the injected worker.
//
// Raising is edge-triggered: an alert is listed when its condition starts and
// never again while that condition lasts, so a dismissed alert stays away
// until the condition clears and then occurs again. A condition clears only
// after recoveryHold seconds of recovered readings (disk also needs
// diskClearFreeFraction free); a recovered alert leaves the list.
class FacilityAlertMonitor final {
public:
    static constexpr double pollInterval = 60, diskRaiseFreeFraction = .10, diskClearFreeFraction = .12, recoveryHold = 120;
    // FacilityAlertSources (:208-245) without the observers and the repeating
    // timer (the owner drives the cadence). A reader returning nullopt is an
    // unavailable source. runWorker runs `read` off the owner thread and
    // `apply` back on it (tests: synchronously). clock: monotonic seconds.
    struct Sources {
        std::function<std::optional<FacilityDiskReading>()> readDisk;
        std::function<std::optional<FacilityMemoryPressure>()> readMemoryPressure;
        std::function<std::optional<FacilityMemoryUsage>()> readMemoryUsage;
        std::function<std::optional<FacilityThermalState>()> readThermalState; // empty: no thermal source (Windows)
        std::function<std::optional<double>()> readTemperature;
        std::function<void(std::function<FacilityReadings()> read, std::function<void(FacilityReadings)> apply)> runWorker;
        std::function<double()> clock;
    };
    explicit FacilityAlertMonitor(Sources);
    FacilityAlertMonitor(const FacilityAlertMonitor&) = delete;
    FacilityAlertMonitor& operator=(const FacilityAlertMonitor&) = delete;

    // Listed alerts in raise order (the tag stack order).
    const std::vector<FacilityAlert>& alerts() const noexcept { return alerts_; }
    // The latest sample. Not covered by onChange.
    const FacilityReadings& readings() const noexcept { return readings_; }
    // Called after every change to alerts() or the hints below.
    std::function<void()> onChange;
    // Bound to Display > Facility alerts: off clears every alert and stops
    // monitoring; on starts it and checks every source at once.
    bool isEnabled() const noexcept { return enabled_; }
    void setEnabled(bool);
    std::vector<FacilityAlert> newAlerts() const;
    const FacilityAlert* alert(FacilityAlertKind) const noexcept;
    // Side effects (declared, not wired: see the header comment).
    bool isStorageLow() const noexcept { return active(FacilityAlertKind::disk); }
    bool isTemperatureHigh() const noexcept { return active(FacilityAlertKind::heat); }
    std::optional<double> temperatureCelsius() const noexcept;
    // Re-read every source now (after the boot log, the cadence, a wake).
    // Ignored while off; a request during a sample coalesces into one more.
    void checkNow();
    // A reading taken outside the worker (Windows: the memory of the host's
    // Activity system samples). Unavailable members keep their last value.
    void apply(const FacilityReadings&);
    // What the next memory / thermal reading should include (Mac read()):
    // usage while the memory alert's condition is active, temperature while
    // the heat condition is.
    bool wantsMemoryUsage() const noexcept { return active(FacilityAlertKind::memory); }
    bool wantsTemperature() const noexcept { return active(FacilityAlertKind::heat); }
    bool sampling() const noexcept { return sampling_; }
    // The user crossed the tag out. It stays away while its condition lasts.
    void dismiss(FacilityAlertKind);
    // The banner for this alert was shown (or deliberately skipped).
    void markPresented(FacilityAlertKind);
    // The Mac read() with the sources: disk, memory (usage when elevated or
    // wanted), thermal (temperature at serious+ or wanted).
    static FacilityReadings read(const Sources&, bool wantsMemoryUsage, bool wantsTemperature);
private:
    struct Condition { bool isActive{}; std::optional<double> recoverySince; };
    enum class Signal : std::uint8_t { raise, hold, recover };
    struct Published { std::vector<FacilityAlert> alerts; bool storageLow{}, temperatureHigh{}; std::optional<double> temperature; bool operator==(const Published&) const = default; };
    Sources sources_;
    std::vector<FacilityAlert> alerts_;
    FacilityReadings readings_;
    std::map<FacilityAlertKind, Condition> conditions_;
    std::uint64_t generation_{};
    bool enabled_{}, sampling_{}, needsSample_{};
    bool active(FacilityAlertKind k) const noexcept { const auto c = conditions_.find(k); return c != conditions_.end() && c->second.isActive; }
    void start();
    void stop();
    void requestSample();
    void applySample(const FacilityReadings&, double now);
    void step(const FacilityAlert::Measurement&, double now);
    static Signal signal(const FacilityAlert::Measurement&) noexcept;
    Published published() const;
    template <class F> void mutate(F&& change) {
        const auto before = published();
        change();
        if (!(published() == before) && onChange) onChange();
    }
};
} // namespace endfield::modules
