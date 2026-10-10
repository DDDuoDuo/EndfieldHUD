#pragma once
// Port of Sources/SystemEventRecorder.swift: records state transitions into
// the Event Log, never a timer tick or global user activity. The first value
// of every stream is a baseline and invents no connection event. Hardware
// identifiers only deduplicate the in-memory snapshot; only labels are logged.
#include "core/data/data_store.hpp"
#include "modules/event_log.hpp"
#include "modules/work_mode.hpp"
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <utility>

namespace endfield::core {
struct RecorderBattery {
    std::optional<int> percentage;
    bool pluggedIn{}, charging{}, fullyCharged{}, hasBattery{};
    bool operator==(const RecorderBattery&) const = default;
};
struct RecorderDisplaySettings {
    std::string clockStyle, centerLogo, alertMetric;
    std::optional<std::string> centerLogoRevision;
    bool operator==(const RecorderDisplaySettings&) const = default;
    static RecorderDisplaySettings from(const ehud::data::Settings&);
};
class ApplicationEventRecorder final {
public:
    using Record = std::function<void(modules::EventKind, modules::EventMetadata)>;
    explicit ApplicationEventRecorder(Record);
    void receiveConfiguration(const RecorderDisplaySettings&);
    void receiveProfileCrop(double backgroundZoom, double thumbnailZoom);
    void receiveBattery(const RecorderBattery&);
    void receiveWork(const modules::WorkModeSnapshot&);
    void receiveAudioDevices(const std::map<std::string, std::string>&);
    void receiveDisplays(const std::map<std::string, std::string>&);
    static std::string batteryState(const RecorderBattery&);
private:
    Record record_;
    std::optional<RecorderBattery> battery_;
    std::optional<modules::WorkModeSnapshot> work_;
    std::optional<std::map<std::string, std::string>> audioDevices_, displays_;
    std::optional<RecorderDisplaySettings> displaySettings_;
    std::optional<std::pair<double, double>> profileCrop_;
    void topology(const std::optional<std::map<std::string, std::string>>& previous, const std::map<std::string, std::string>& current,
                  modules::EventKind connected, modules::EventKind disconnected);
};
} // namespace endfield::core
