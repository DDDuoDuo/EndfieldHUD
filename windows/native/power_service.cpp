#include "native/power_service.hpp"
#include "modules/power_policy.hpp"
#include <utility>

namespace endfield::native {
modules::BatteryReading batteryReadingFromSnapshot(const BatterySnapshot& s) noexcept {
    modules::BatteryReading r;
    if (!s.available || s.present != true) return r;
    r.present = true;
    if (s.percent && *s.percent <= 100) r.percentage = s.percent;
    r.pluggedIn = s.ac_connected == true;
    r.charging = r.pluggedIn && s.charging == true;
    r.fullyCharged = r.pluggedIn && !r.charging && s.fully_charged == true;
    return r;
}

struct PowerService::Impl {
    app::UtilityExecutor* executor;
    app::UtilityExecutor::Route route{};
    Changed changed;
    Reader reader;
    modules::BatteryReading base, published;
    bool hasBase{}, hasPublished{}, pending{}, trailing{}, stopped{}, deviceRead{};
    std::vector<BatteryDeviceReport> reports;
    std::int32_t error{};
    std::uint64_t reads{}, publishes{};

    modules::BatteryReading merged() const {
        auto value = base;
        if (!value.present) return value;
        // Before the first device read, and whenever hardware gives no
        // matching absolute pair, the source's explicit percent pair is used.
        value.capacity = deviceRead ? decodeBatteryCapacity(reports, value.percentage) : modules::batteryCapacityPercent(value.percentage);
        return value;
    }
    void publish() {
        if (!hasBase || stopped) return;
        auto value = merged();
        if (hasPublished && value == published) return;
        published = std::move(value);
        hasPublished = true;
        ++publishes;
        if (changed) changed(published);
    }
    static void request(const std::shared_ptr<Impl>& self) {
        if (self->stopped || !self->base.present) return;
        if (self->pending) { self->trailing = true; return; }
        auto result = std::make_shared<BatteryDeviceRead>();
        auto reader = self->reader;
        std::weak_ptr<Impl> weak = self;
        const bool accepted = self->executor->submit(self->route, [result, reader] { *result = reader(); },
            [weak, result](std::exception_ptr failure) {
                const auto owner = weak.lock();
                if (!owner || owner->stopped) return;
                owner->pending = false;
                if (!failure) {
                    owner->reports = std::move(result->reports);
                    owner->error = result->error;
                    owner->deviceRead = true;
                    owner->publish();
                }
                if (owner->trailing) { owner->trailing = false; request(owner); }
            });
        if (!accepted) { self->trailing = true; return; } // retried by the next power event
        self->pending = true;
        self->trailing = false;
        ++self->reads;
    }
};

PowerService::PowerService(app::UtilityExecutor& executor, Changed changed, Reader reader) : impl_(std::make_shared<Impl>()) {
    impl_->executor = &executor;
    impl_->route = executor.makeRoute();
    impl_->changed = std::move(changed);
    impl_->reader = reader ? std::move(reader) : Reader([] { return readBatteryDevices(); });
}
PowerService::~PowerService() { stop(); }
void PowerService::receive(const BatterySnapshot& snapshot) { receive(batteryReadingFromSnapshot(snapshot)); }
void PowerService::receive(modules::BatteryReading base) {
    auto& i = *impl_;
    if (i.stopped) return;
    base.capacity.reset();
    base.health.reset(); // no Windows condition category
    i.base = std::move(base);
    i.hasBase = true;
    i.publish();
    Impl::request(impl_);
}
const modules::BatteryReading& PowerService::reading() const noexcept { return impl_->published; }
bool PowerService::hasReading() const noexcept { return impl_->hasPublished; }
bool PowerService::capacityPending() const noexcept { return impl_->pending; }
std::uint64_t PowerService::capacityReads() const noexcept { return impl_->reads; }
std::uint64_t PowerService::publishedReadings() const noexcept { return impl_->publishes; }
std::int32_t PowerService::lastError() const noexcept { return impl_->error; }
void PowerService::stop() noexcept {
    if (!impl_ || impl_->stopped) return;
    impl_->stopped = true;
    impl_->pending = impl_->trailing = false;
    try { impl_->executor->invalidate(impl_->route); } catch (...) {}
}
} // namespace endfield::native
