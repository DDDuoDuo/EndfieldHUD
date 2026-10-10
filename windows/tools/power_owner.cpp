#include "tools/power_owner.hpp"
#ifdef _WIN32
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace endfield::tools {
namespace m = modules;

struct PowerOwner::Impl {
    PowerOwnerCallbacks callbacks;
    native::ChargeIndicatorPanel panel;
    m::PowerAlertCoordinator coordinator;
    m::PowerEventRecorder recorder;
    native::PowerService service;
    m::ChargeAlertSettings settings;
    double time{};
    bool terminated{};

    Impl(app::UtilityExecutor& executor, native::LayerRasterizer& raster, native::ChargePanelOptions options, PowerOwnerCallbacks c,
         native::PowerService::Reader reader)
        : callbacks(std::move(c)),
          panel(raster, std::move(options), [this](std::optional<m::ChargePosition> position) { positionFinished(position); }),
          coordinator(panel.state()),
          service(executor, [this](const m::BatteryReading& reading) { changed(reading); }, std::move(reader)) {}
    void clock(double t) {
        if (!std::isfinite(t)) throw std::invalid_argument("Invalid power owner time");
        time = std::max(time, t);
    }
    // One deduplicated provider reading (immediately, and again when the
    // capacity pair arrives from the utility worker).
    void changed(const m::BatteryReading& reading) {
        if (terminated) return;
        for (const auto& event : recorder.receive(reading))
            if (callbacks.recordEvent) callbacks.recordEvent(event.kind, event.metadata);
        coordinator.receive(reading, time);
        if (callbacks.readingChanged) callbacks.readingChanged(reading);
    }
    void positionFinished(const std::optional<m::ChargePosition>& position) {
        if (position && callbacks.savePosition) callbacks.savePosition(*position);
        if (position) {
            // The saved position applies at once; the settings event that
            // follows the queued save carries the same values.
            auto next = settings;
            next.customPlacement = true;
            next.customScreenID = position->screenID;
            next.customX = position->x;
            next.customY = position->y;
            settings = next.normalized();
            panel.state().setSettings(settings);
            coordinator.setSettings(settings, time);
        }
        coordinator.presentLatestSnapshot(time);
        if (callbacks.positionEditFinished) callbacks.positionEditFinished();
    }
};

PowerOwner::PowerOwner(app::UtilityExecutor& executor, native::LayerRasterizer& raster, native::ChargePanelOptions options,
                       PowerOwnerCallbacks callbacks, native::PowerService::Reader reader)
    : impl_(std::make_unique<Impl>(executor, raster, std::move(options), std::move(callbacks), std::move(reader))) {}
PowerOwner::~PowerOwner() {
    impl_->service.stop();
    impl_->panel.shutdown();
}
void PowerOwner::receive(const native::BatterySnapshot& snapshot, double time) {
    impl_->clock(time);
    impl_->service.receive(snapshot);
}
void PowerOwner::setSettings(const ehud::data::Json& fields, bool systemDark, core::Language language, bool reduceMotion, double time) {
    auto& i = *impl_;
    i.clock(time);
    if (language == core::Language::system) throw std::invalid_argument("Resolve the alert language in the host");
    auto settings = m::ChargeAlertSettings::fromSettings(fields);
    const bool dark = settings.theme == m::ChargeTheme::dark || (settings.theme == m::ChargeTheme::system && systemDark);
    i.panel.setAppearance({dark, settings.accent(), language});
    i.panel.state().setReduceMotion(reduceMotion);
    i.settings = settings;
    i.panel.state().setSettings(settings);
    i.coordinator.setSettings(settings, i.time);
}
void PowerOwner::preview(double time) { impl_->clock(time); impl_->coordinator.preview(impl_->time); }
bool PowerOwner::editPosition(double time) {
    auto& i = *impl_;
    i.clock(time);
    if (i.terminated || i.coordinator.suspended() || i.panel.state().systemOverlayActive()) return false;
    return i.panel.beginPositionEditing(i.coordinator.previewValue(), i.time);
}
void PowerOwner::systemOverlayOpening(double time) {
    auto& i = *impl_;
    i.clock(time);
    // toggleSystemOverlay: drop a manual preview, hide without animation,
    // then the shared panel belongs to the HUD until it closes.
    i.coordinator.systemOverlayToggled();
    i.panel.state().hide(false, i.time);
    i.panel.state().setSystemOverlayActive(true);
    i.panel.advance(i.time);
}
void PowerOwner::systemOverlayClosed(double time) {
    impl_->clock(time);
    impl_->panel.state().setSystemOverlayActive(false);
}
void PowerOwner::setProjectionActive(bool active, double time) {
    impl_->clock(time);
    impl_->panel.state().setProjectionActive(active);
}
void PowerOwner::suspend(m::PowerSuspension reason, double time) {
    auto& i = *impl_;
    i.clock(time);
    i.panel.state().discardPosition(i.time); // cancelPositionEditing
    i.coordinator.suspend(reason, i.time);
    i.panel.advance(i.time);
}
void PowerOwner::resume(m::PowerSuspension reason, const std::optional<native::BatterySnapshot>& fresh, double time) {
    auto& i = *impl_;
    i.clock(time);
    // AppDelegate.resume: read while still suspended (recorder, tray and
    // snapshot update without presenting), then present exactly once.
    if (fresh) i.service.receive(*fresh);
    // OverlayController.reposition: displays may have changed while asleep.
    if (i.coordinator.resume(reason, std::nullopt, i.time)) i.panel.refreshScreens();
}
void PowerOwner::setTelemetry(const std::optional<m::ChargeTelemetry>& telemetry) { impl_->panel.setTelemetry(telemetry); }
bool PowerOwner::telemetryDemand() const noexcept { return impl_->panel.state().telemetryDemand(); }
void PowerOwner::advance(double time) {
    impl_->clock(time);
    impl_->panel.advance(impl_->time);
}
bool PowerOwner::requiresFrames(double time) const { return impl_->panel.requiresFrames(time); }
std::optional<double> PowerOwner::nextWakeTime() const { return impl_->panel.nextWakeTime(); }
bool PowerOwner::hasReading() const noexcept { return impl_->service.hasReading(); }
const m::BatteryReading& PowerOwner::reading() const noexcept { return impl_->service.reading(); }
bool PowerOwner::devicePopupEnabled() const noexcept { return impl_->settings.devicePopupEnabled; }
std::string PowerOwner::trayStatus(core::Language language) const { return m::powerTrayDescription(reading(), language); }
std::string PowerOwner::trayTooltip(core::Language language) const { return m::powerTrayTooltip(reading(), language); }
std::vector<m::DeviceBatteryReading> PowerOwner::deviceReadings(core::Language language) const {
    return m::deviceBatteryReadings(reading(), language);
}
native::ChargeIndicatorPanel& PowerOwner::panel() noexcept { return impl_->panel; }
const m::PowerAlertCoordinator& PowerOwner::coordinator() const noexcept { return impl_->coordinator; }
void PowerOwner::terminate(double time) {
    auto& i = *impl_;
    i.clock(time);
    i.panel.state().discardPosition(i.time);
    i.coordinator.terminate(i.time);
    i.terminated = true;
    i.service.stop();
    i.panel.advance(i.time);
    i.panel.shutdown();
}
} // namespace endfield::tools
#endif
