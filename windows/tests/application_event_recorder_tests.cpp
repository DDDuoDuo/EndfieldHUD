// SystemEventRecorder port against the unchanged Mac recorder.
#include "core/application_event_recorder.hpp"
#include "core/data/file_io.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace core = endfield::core;
namespace m = endfield::modules;
using ehud::data::Json;
namespace {
unsigned checks{};
void check(bool value, const std::string& why) { ++checks; if (!value) throw std::runtime_error(why); }
std::map<std::string, std::string> devices(const Json& value) {
    std::map<std::string, std::string> out;
    for (const auto& [id, name] : value.object()) out.emplace(id, name.string());
    return out;
}
double number(const Json& value) { return value.isString() ? std::nan("") : value.number(); }
m::WorkModeKind kind(std::string_view v) { return v == "stopwatch" ? m::WorkModeKind::stopwatch : m::WorkModeKind::countdown; }
m::WorkModePhase phase(std::string_view v) {
    using P = m::WorkModePhase;
    return v == "running" ? P::running : v == "paused" ? P::paused : v == "stopped" ? P::stopped : v == "completed" ? P::completed : P::idle;
}
void oracle(const Json& source) {
    check(source["provenance"]["compiledUnchanged"].array().size() == 1, "Unchanged Mac recorder");
    std::size_t steps{};
    for (const auto& script : source["scripts"].array()) {
        std::vector<std::pair<m::EventKind, m::EventMetadata>> recorded;
        core::ApplicationEventRecorder recorder([&](m::EventKind k, m::EventMetadata metadata) { recorded.emplace_back(k, std::move(metadata)); });
        for (const auto& row : script.array()) {
            recorded.clear();
            const auto& step = row["step"].array();
            const auto operation = step[0].string();
            const auto& v = step[1];
            if (operation == "battery") {
                const auto& b = v.array();
                core::RecorderBattery value{b[0].isNull() ? std::nullopt : std::optional<int>(int(b[0].integer())), b[1].boolean(), b[2].boolean(), b[3].boolean(), b[4].boolean()};
                recorder.receiveBattery(value);
            } else if (operation == "work") {
                const auto& w = v.array();
                m::WorkModeSnapshot value; value.kind = kind(w[0].string()); value.phase = phase(w[1].string()); value.duration = w[2].number();
                recorder.receiveWork(value);
            } else if (operation == "config") {
                const auto& c = v.array();
                core::RecorderDisplaySettings value{c[0].string(), c[1].string(), c[3].string(), c[2].isNull() ? std::nullopt : std::optional<std::string>(c[2].string())};
                recorder.receiveConfiguration(value);
            } else if (operation == "displays") recorder.receiveDisplays(devices(v));
            else if (operation == "audio") recorder.receiveAudioDevices(devices(v));
            else if (operation == "crop") recorder.receiveProfileCrop(number(v.array()[0]), number(v.array()[1]));
            else throw std::runtime_error("Unknown recorder operation");
            const auto& expected = row["records"].array();
            check(recorded.size() == expected.size(), "Recorded event count for " + operation + " step " + std::to_string(steps));
            for (std::size_t n = 0; n < expected.size(); ++n) {
                check(std::string(m::eventKindKey(recorded[n].first)) == expected[n]["kind"].string(), "Event kind " + expected[n]["kind"].string());
                const auto& metadata = expected[n]["metadata"].object();
                check(recorded[n].second.size() == metadata.size(), "Metadata key count");
                for (const auto& [key, value] : metadata) {
                    const auto found = recorded[n].second.find(key);
                    check(found != recorded[n].second.end() && found->second == value.string(), "Metadata " + key);
                }
            }
            ++steps;
        }
    }
    check(steps == 46, "Every scripted transition replayed");
    // Settings records map onto the same Mac preference keys.
    auto settings = ehud::data::Settings::defaults();
    settings.set("centerLogo", "custom"); settings.set("centerLogoRevision", "rev");
    const auto display = core::RecorderDisplaySettings::from(settings);
    check(display.clockStyle == "digital" && display.centerLogo == "custom" && display.centerLogoRevision == "rev" && display.alertMetric == "battery", "Preference mapping");
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 2, "Pass the Mac recorder oracle");
        const auto bytes = ehud::data::detail::readFile(argv[1], 1024 * 1024);
        check(bytes.has_value(), "Read the bounded oracle");
        oracle(Json::parse(*bytes));
        std::cout << "Event recorder: " << checks << " source checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Event recorder failed after " << checks << ": " << e.what() << '\n';
        return 1;
    }
}
