#include "core/application_event_recorder.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace endfield::core {
using K = modules::EventKind;
RecorderDisplaySettings RecorderDisplaySettings::from(const ehud::data::Settings& value) {
    RecorderDisplaySettings out{value.string("clockStyle"), value.string("centerLogo"), value.string("alertMetric"), {}};
    if (const auto& revision = value.fields["centerLogoRevision"]; revision.isString()) out.centerLogoRevision = revision.string();
    return out;
}
ApplicationEventRecorder::ApplicationEventRecorder(Record record) : record_(std::move(record)) {
    if (!record_) throw std::invalid_argument("Event recorder needs a log");
}
void ApplicationEventRecorder::receiveConfiguration(const RecorderDisplaySettings& next) {
    const auto previous = displaySettings_;
    displaySettings_ = next;
    if (!previous) return;
    if (previous->clockStyle != next.clockStyle) record_(K::displaySettingsChanged, {{"field", "clockStyle"}, {"value", next.clockStyle}});
    if (previous->centerLogo != next.centerLogo || (next.centerLogo == "custom" && previous->centerLogoRevision != next.centerLogoRevision)) {
        const bool imported = next.centerLogo == "custom" && next.centerLogoRevision && previous->centerLogoRevision != next.centerLogoRevision;
        record_(K::displaySettingsChanged, {{"field", "centerLogo"}, {"value", imported ? "customImported" : next.centerLogo}});
    }
    if (previous->alertMetric != next.alertMetric) record_(K::displaySettingsChanged, {{"field", "alertMetric"}, {"value", next.alertMetric}});
}
void ApplicationEventRecorder::receiveProfileCrop(double background, double thumbnail) {
    const auto normalized = [](double value) { return std::isfinite(value) ? std::min(20.0, std::max(1.0, value)) : 1.0; };
    const std::pair next{normalized(background), normalized(thumbnail)};
    const auto previous = profileCrop_;
    profileCrop_ = next;
    if (!previous) return;
    const bool b = previous->first != next.first, t = previous->second != next.second;
    if (!b && !t) return;
    record_(K::profileCropChanged, {{"target", b && t ? "both" : b ? "background" : "thumbnail"}});
}
std::string ApplicationEventRecorder::batteryState(const RecorderBattery& value) {
    if (!value.hasBattery) return "unavailable";
    if (value.charging) return "charging";
    if (value.fullyCharged) return "full";
    return value.pluggedIn ? "connected" : "battery";
}
void ApplicationEventRecorder::receiveBattery(const RecorderBattery& value) {
    const auto previous = battery_;
    battery_ = value;
    if (!previous) return;
    modules::EventMetadata metadata{{"state", batteryState(value)}};
    if (value.percentage) metadata["percentage"] = std::to_string(*value.percentage);
    // Missing battery data is not proof that a power cable was removed.
    if (previous->hasBattery && value.hasBattery && previous->pluggedIn != value.pluggedIn)
        record_(value.pluggedIn ? K::powerConnected : K::powerDisconnected, metadata);
    if (batteryState(*previous) != batteryState(value)) record_(K::batteryStateChanged, metadata);
}
void ApplicationEventRecorder::receiveWork(const modules::WorkModeSnapshot& value) {
    const auto previous = work_;
    work_ = value;
    if (!previous || previous->phase == value.phase) return;
    const auto& source = value.phase == modules::WorkModePhase::idle ? *previous : value;
    modules::EventMetadata metadata{{"kind", std::string(modules::workModeKindKey(source.kind))}};
    if (source.kind == modules::WorkModeKind::countdown && std::isfinite(source.duration))
        metadata["seconds"] = std::to_string(static_cast<std::int64_t>(std::min(86400.0, std::max(1.0, source.duration))));
    using P = modules::WorkModePhase;
    switch (value.phase) {
    case P::running: record_(previous->phase == P::paused ? K::workResumed : K::workStarted, metadata); break;
    case P::paused: record_(K::workPaused, metadata); break;
    case P::completed: record_(K::workCompleted, metadata); break;
    case P::idle:
        if (previous->active() || previous->phase == P::completed || previous->phase == P::stopped) record_(K::workReset, metadata);
        break;
    case P::stopped: break; // No Stop action is exposed in the HUD.
    }
}
void ApplicationEventRecorder::topology(const std::optional<std::map<std::string, std::string>>& previous, const std::map<std::string, std::string>& current,
                                        modules::EventKind connected, modules::EventKind disconnected) {
    if (!previous) return;
    for (const auto& [id, name] : *previous) if (!current.contains(id)) record_(disconnected, {{"device", name}});
    for (const auto& [id, name] : current) if (!previous->contains(id)) record_(connected, {{"device", name}});
}
void ApplicationEventRecorder::receiveAudioDevices(const std::map<std::string, std::string>& devices) {
    const auto previous = std::exchange(audioDevices_, devices);
    topology(previous, devices, K::audioDeviceConnected, K::audioDeviceDisconnected);
}
void ApplicationEventRecorder::receiveDisplays(const std::map<std::string, std::string>& devices) {
    const auto previous = std::exchange(displays_, devices);
    topology(previous, devices, K::displayConnected, K::displayDisconnected);
}
} // namespace endfield::core
