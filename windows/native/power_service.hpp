#pragma once
#include "app/utility_executor.hpp"
#include "modules/battery_presentation.hpp"
#include "native/battery_capacity.hpp"
#include "native/system_services.hpp"
#include <cstdint>
#include <functional>
#include <memory>

// The Power area's single observer of the shared SystemServices battery
// provider (RegisterPowerSettingNotification + GetSystemPowerStatus, coalesced
// WM_POWERBROADCAST). It never registers a second notification, polls, or
// starts a timer: each provider change publishes immediately, and the
// battery-class capacity pair is refreshed on the shared utility worker with
// at most one read in flight and one trailing read for any burst of events
// (the Mac BatteryMonitor bounds registry work the same way).
namespace endfield::native {
// SystemServices snapshot -> the Mac BatterySnapshot fields. An unavailable
// or unknown reading is "no battery", never a fabricated value.
modules::BatteryReading batteryReadingFromSnapshot(const BatterySnapshot&) noexcept;

class PowerService final {
public:
    using Reader = std::function<BatteryDeviceRead()>; // utility worker only
    using Changed = std::function<void(const modules::BatteryReading&)>; // owner thread
    // The executor must outlive this service (or call stop() first). Changed
    // runs from receive() and from the executor's owner-thread drain().
    PowerService(app::UtilityExecutor&, Changed, Reader = {});
    ~PowerService();
    PowerService(const PowerService&) = delete;
    PowerService& operator=(const PowerService&) = delete;
    // One provider change (owner thread). Identical merged readings do not
    // republish; capacity is re-read for every accepted event.
    void receive(const BatterySnapshot&);
    void receive(modules::BatteryReading base);
    const modules::BatteryReading& reading() const noexcept;
    bool hasReading() const noexcept;
    bool capacityPending() const noexcept;
    std::uint64_t capacityReads() const noexcept;      // reads submitted
    std::uint64_t publishedReadings() const noexcept;  // Changed invocations
    std::int32_t lastError() const noexcept;
    // Terminal: invalidates the route; no further reads or callbacks.
    void stop() noexcept;
private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};
} // namespace endfield::native
