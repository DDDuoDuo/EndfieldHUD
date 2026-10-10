// Source: windows/tools/charge_indicator_reference.sh (unchanged DisplayPolicy,
// OverlayGeometry, AppConfiguration.normalized, SystemEventRecorder battery
// section, DeviceBatteryProvider, BatteryCapacityReading and the AppDelegate
// battery bodies against an inert overlay stand-in). Synthetic values only.
#include "modules/charge_overlay.hpp"
#include "modules/power_policy.hpp"
#include <cmath>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <sstream>

using namespace endfield;
using namespace endfield::modules;
using ehud::data::Json;

namespace {
unsigned checks{};
void check(bool value, const std::string& message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
void near(double actual, double expected, double tolerance, const std::string& message) {
    check(std::abs(actual - expected) <= tolerance, message + " (actual " + std::to_string(actual) + ", source " + std::to_string(expected) + ")");
}
double number(const Json& value) {
    if (value.isString()) {
        const auto s = value.string();
        return s == "nan" ? NAN : s == "inf" ? INFINITY : s == "-inf" ? -INFINITY : std::stod(s);
    }
    return value.number();
}
BatteryReading battery(const Json& d) {
    BatteryReading r;
    if (!d["percentage"].isNull()) r.percentage = static_cast<unsigned>(d["percentage"].number());
    r.pluggedIn = d["pluggedIn"].boolean(); r.charging = d["charging"].boolean();
    r.fullyCharged = d["fullyCharged"].boolean(); r.present = d["hasBattery"].boolean();
    if (!d["capacity"].isNull())
        r.capacity = BatteryReading::Capacity{static_cast<unsigned>(d["capacity"]["current"].number()),
                                              static_cast<unsigned>(d["capacity"]["maximum"].number()), d["capacity"]["unit"].string()};
    return r;
}
std::string describe(const BatteryReading& s) {
    return "p=" + (s.percentage ? std::to_string(*s.percentage) : std::string("nil")) + ",b=" + (s.present ? "1" : "0") +
           ",a=" + (s.pluggedIn ? "1" : "0") + ",c=" + (s.charging ? "1" : "0") + ",f=" + (s.fullyCharged ? "1" : "0");
}
std::string decimal(double value) {
    // Swift "\(Double)" for the fixture's 0.0 / 3.0 durations.
    std::ostringstream out;
    out << value;
    auto text = out.str();
    if (text.find('.') == std::string::npos) text += ".0";
    return text;
}

struct Fixture {
    Json root;
    std::map<std::string, BatteryReading> snapshots;
    explicit Fixture(Json value) : root(std::move(value)) {
        for (const auto& row : root["snapshots"].array()) snapshots[row["name"].string()] = battery(row);
        check(snapshots.size() == 7, "Seven named source snapshots");
    }
    const BatteryReading& snapshot(const std::string& name) const { return snapshots.at(name); }
};

void policy(const Fixture& f) {
    unsigned compared{};
    for (const auto& row : f.root["policy"].array()) {
        std::optional<BatteryReading> previous;
        if (row["previous"].string() != "nil") previous = f.snapshot(row["previous"].string());
        const auto action = chargeDisplayAction(f.snapshot(row["snapshot"].string()), previous, row["mode"].string() == "always");
        check(chargeDisplayActionKey(action) == row["action"].string(),
              "DisplayPolicy " + row["mode"].string() + " " + row["previous"].string() + " -> " + row["snapshot"].string());
        ++compared;
    }
    check(compared == 2 * 7 * 8, "All source display-policy cases compared");
}

// The fixture is in source bottom-left desktop points. y' = -y maps it to the
// Windows y-down virtual desktop; at one pixel per point the values are exact.
core::Rect flipped(const std::vector<double>& r) { return {r[0], -(r[1] + r[3]), r[2], r[3]}; }
std::vector<double> numbers(const Json& value) {
    std::vector<double> out;
    for (const auto& item : value.array()) out.push_back(number(item));
    return out;
}
void geometry(const Fixture& f) {
    unsigned clamps{}, anchors{}, normalized{};
    for (const auto& row : f.root["geometry"].array()) {
        const auto kind = row["kind"].string();
        const auto screen = flipped(numbers(row["screen"]));
        const auto expected = numbers(kind == "normalize" ? row["position"] : row["anchor"]);
        const auto label = kind + " " + row["screen"].encode();
        if (kind == "clamp") {
            const auto p = numbers(row["point"]);
            const auto a = chargeClampedAnchor({p[0], -p[1]}, screen, number(row["scale"]), row["editing"].boolean(), 1);
            near(a.x, expected[0], 1e-9, label + " clamped x");
            near(-a.y, expected[1], 1e-9, label + " clamped y");
            ++clamps;
        } else if (kind == "anchor") {
            ChargeAlertSettings settings;
            settings.customPlacement = row["placement"].string() == "custom";
            const auto position = numbers(row["position"]);
            settings.customX = position[0]; settings.customY = position[1];
            settings.customScreenID = 7;
            settings.scale = number(row["scale"]);
            const auto a = chargeAnchor(settings, screen, row["editing"].boolean(), 1);
            near(a.x, expected[0], 1e-9, label + " anchor x");
            near(-a.y, expected[1], 1e-9, label + " anchor y");
            ++anchors;
        } else {
            const auto p = numbers(row["point"]);
            const auto position = chargeNormalizedPosition({p[0], -p[1]}, ChargeWorkArea{9, screen, 1});
            near(position.x, expected[0], 1e-12, label + " normalized x");
            near(position.y, expected[1], 1e-12, label + " normalized y");
            check(row["screenID"].isNull() ? !position.screenID : position.screenID == 9u, label + " screen identity");
            ++normalized;
        }
    }
    check(clamps == 336 && anchors == 240 && normalized == 15, "All source geometry cases compared");
    // Pixel density: Mac point margins scale by the monitor's pixels per point.
    const core::Rect work{0, 0, 2880, 1800};
    const auto top = chargeAnchor(ChargeAlertSettings{}, work, false, 2);
    check(top.x == 1440 && top.y == 60, "Top-center anchor at 200% keeps 30-point margin");
    const auto layout = chargePanelLayout({1440, 60}, 1, true, 2);
    check(layout.window.width == 600 && layout.indicator.height == 168 && layout.window.height == 200 &&
          layout.cancel.x == 1440 - 68 && layout.confirm.x == 1452 && layout.cancel.y == layout.window.y + 132,
          "Editor layout keeps the indicator centered with unscaled 28-point controls below it");
}

void normalization(const Fixture& f) {
    for (const auto& row : f.root["normalization"].array()) {
        ChargeAlertSettings settings;
        settings.displayDuration = number(row["duration"]);
        settings.scale = number(row["scale"]);
        settings.accentHex = row["accent"].string();
        settings.customX = settings.scale; settings.customY = -settings.scale;
        const auto n = settings.normalized();
        const auto label = "normalization " + row["accent"].string();
        near(n.displayDuration, number(row["normalizedDuration"]), 0, label + " duration");
        near(n.scale, number(row["normalizedScale"]), 0, label + " scale");
        check(n.accentHex == row["normalizedAccent"].string(), label + " accent");
        const auto position = numbers(row["position"]);
        near(n.customX, position[0], 0, label + " position x");
        near(n.customY, position[1], 0, label + " position y");
        const auto rgb = numbers(row["accentRGB"]);
        const auto accent = n.accent();
        for (std::size_t i = 0; i < 3; ++i) near(accent[i], rgb[i], 1e-12, label + " accent color");
    }
    // Persisted-settings reader: tolerant decoding with source fallbacks.
    Json fields = Json::Object{{"displayMode", "always"}, {"displayDuration", 120}, {"accentHex", "#3fa9f5"}, {"theme", "light"},
                               {"scale", 0.1}, {"placement", "custom"}, {"customScreenID", 42}, {"customPositionX", 2},
                               {"customPositionY", 0.25}, {"alertMetric", "network"}, {"batteryAlertsEnabled", false},
                               {"devicePopupEnabled", false}, {"reduceMotion", true}, {"lowPowerVisualMode", true}};
    const auto s = ChargeAlertSettings::fromSettings(fields);
    check(s.alwaysMode && s.displayDuration == 60 && s.accentHex == "3FA9F5" && s.theme == ChargeTheme::light && s.scale == 0.65 &&
          s.customPlacement && s.customScreenID == 42u && s.customX == 1 && s.customY == 0.25 && s.metric == ChargeMetric::network &&
          !s.alertsEnabled && !s.devicePopupEnabled && s.reduceMotion && s.lowPowerVisualMode, "Saved alert settings round trip");
    Json invalid = Json::Object{{"displayMode", 3}, {"alertMetric", "gpu"}, {"customScreenID", -1}, {"scale", "large"}};
    const auto d = ChargeAlertSettings::fromSettings(invalid);
    check(!d.alwaysMode && d.metric == ChargeMetric::battery && !d.customScreenID && d.scale == 1 && d.alertsEnabled &&
          d.devicePopupEnabled, "Invalid saved alert settings fall back like the source decoder");
}

void recorder(const Fixture& f) {
    PowerEventRecorder recorder;
    unsigned events{};
    for (const auto& row : f.root["recorder"].array()) {
        const auto actual = recorder.receive(f.snapshot(row["snapshot"].string()));
        const auto& expected = row["events"].array();
        check(actual.size() == expected.size(), "Recorder events for " + row["snapshot"].string());
        for (std::size_t i = 0; i < actual.size(); ++i) {
            const auto kind = actual[i].kind == EventKind::powerConnected ? "powerConnected"
                            : actual[i].kind == EventKind::powerDisconnected ? "powerDisconnected" : "batteryStateChanged";
            check(kind == expected[i]["kind"].string(), "Recorder kind for " + row["snapshot"].string());
            EventMetadata metadata;
            for (const auto& [key, value] : expected[i]["metadata"].object()) metadata.emplace(key, value.string());
            check(actual[i].metadata == metadata, "Allowlisted recorder metadata for " + row["snapshot"].string());
            ++events;
        }
    }
    check(events >= 8, "Source recorder transitions compared");
}

void devices(const Fixture& f) {
    static_assert(accessoryBatteryCapability == AccessoryBatteryCapability::notProvided);
    for (const auto& row : f.root["devices"].array()) {
        const auto actual = deviceBatteryReadings(f.snapshot(row["snapshot"].string()), core::Language::english);
        const auto& expected = row["readings"].array();
        check(actual.size() == 1 && expected.size() == 1, "One host reading, no invented accessories");
        const auto& e = expected[0];
        const auto availability = actual[0].availability == DeviceBatteryAvailability::available ? "available"
                                : actual[0].availability == DeviceBatteryAvailability::unavailable ? "unavailable" : "noBattery";
        check(actual[0].id == e["id"].string() && actual[0].host && e["kind"].string() == "mac" && availability == e["availability"].string(),
              "Device reading " + row["snapshot"].string());
        check(e["percentage"].isNull() ? !actual[0].percentage : actual[0].percentage == static_cast<unsigned>(e["percentage"].number()),
              "Device percentage " + row["snapshot"].string());
        // The source label is "This Mac"; Windows names the same host row neutrally.
        check(e["name"].string() == "This Mac" && actual[0].name == "This PC", "Neutral Windows host label");
    }
    BatteryReading invalid; invalid.present = true; invalid.percentage = 101;
    check(deviceBatteryReadings(invalid, core::Language::english)[0].availability == DeviceBatteryAvailability::unavailable,
          "Out-of-range percentage stays unavailable");
    for (auto language : {core::Language::simplifiedChinese, core::Language::traditionalChinese, core::Language::japanese, core::Language::korean})
        check(!deviceHostName(language).empty() && deviceHostName(language) != "This PC", "Localized host label");
}

void capacities(const Fixture& f) {
    unsigned compared{};
    for (const auto& row : f.root["capacities"].array()) {
        const auto& p = row["properties"];
        auto value = [&](const char* key) -> std::optional<double> {
            if (!p.contains(key)) return std::nullopt;
            const auto text = p[key].string();
            if (text == "true" || text == "false") return std::nullopt; // CFBoolean is not a capacity
            return std::stod(text);
        };
        std::optional<unsigned> percentage;
        if (!row["percentage"].isNull()) {
            const double v = row["percentage"].number();
            if (v >= 0) percentage = static_cast<unsigned>(v);
        }
        // BatteryCapacityReading.fromRegistry: raw pair, legacy pair, explicit percent.
        auto result = batteryCapacityPair(value("AppleRawCurrentCapacity"), value("AppleRawMaxCapacity"), "mAh");
        if (!result) result = batteryCapacityPair(value("CurrentCapacity"), value("MaxCapacity"), "mAh");
        if (!result) result = batteryCapacityPercent(percentage);
        const auto label = "capacity " + p.encode() + " " + row["percentage"].encode();
        if (row["result"].isNull()) check(!result, label + " stays unavailable");
        else {
            check(result.has_value(), label + " exists");
            check(result->current == static_cast<unsigned>(row["result"]["current"].number()) &&
                  result->maximum == static_cast<unsigned>(row["result"]["maximum"].number()) && result->unit == row["result"]["unit"].string(),
                  label + " pair");
        }
        ++compared;
    }
    check(compared == 55, "All source capacity rules compared");
    check(batteryCapacityPair(41'500, 52'000, "mWh") == BatteryReading::Capacity{41'500, 52'000, "mWh"} &&
          !batteryCapacityPair(52'001, 52'000, "mWh") && !batteryCapacityPair(1, 0xFFFFFFFFu, "mWh"),
          "Windows mWh pairs follow the same rules; unknown sentinels are rejected");
}

// AppDelegate battery bodies: the same inert overlay stand-in the oracle used.
class RecordingPresenter final : public ChargeAlertPresenter {
public:
    std::vector<std::string> log;
    bool isVisible{}, isPersistent{}, isEditing{}, isSystem{}, isProjection{};
    bool visible() const override { return isVisible; }
    bool persistent() const override { return isPersistent; }
    bool editingPosition() const override { return isEditing; }
    bool systemOverlayActive() const override { return isSystem; }
    bool projectionActive() const override { return isProjection; }
    void update(const BatteryReading& s, bool preview, double) override {
        log.push_back("update(" + describe(s) + ",preview=" + (preview ? "1" : "0") + ")");
    }
    void show(bool persistent, double duration, bool replay, double) override {
        if (isEditing || isSystem || isProjection) { log.push_back("show-ignored"); return; }
        log.push_back(std::string("show(persistent=") + (persistent ? "1" : "0") + ",duration=" + decimal(duration) +
                      ",replay=" + (replay ? "1" : "0") + ")");
        isVisible = true; isPersistent = persistent;
    }
    void hide(bool, double) override { log.push_back("hide"); isPersistent = false; isVisible = false; }
};

void delegate(const Fixture& f) {
    unsigned rows{};
    for (const auto& run : f.root["delegate"].array()) {
        const auto name = run["name"].string();
        RecordingPresenter presenter;
        std::optional<PowerAlertCoordinator> owner(std::in_place, presenter);
        auto& coordinator = *owner;
        const auto language = name == "menuChinese" ? core::Language::simplifiedChinese : core::Language::english;
        for (const auto& row : run["rows"].array()) {
            const auto step = row["step"].string();
            const auto before = presenter.log.size();
            std::vector<std::string> extra;
            if (step.starts_with("receive:")) coordinator.receive(f.snapshot(step.substr(8)), 0);
            else if (step == "dismissed") { presenter.isVisible = false; presenter.isPersistent = false; }
            else if (step == "preview") coordinator.preview(0);
            else if (step == "always" || step == "disable") {
                // The oracle assigns the stored configuration before any
                // reading (no configurationChanged): start from that setting.
                check(rows == 0 || before == 0, name + " configures before readings");
                ChargeAlertSettings settings;
                if (step == "always") settings.alwaysMode = true; else settings.alertsEnabled = false;
                owner.emplace(presenter, settings);
            }
            else if (step == "hudOpen") presenter.isSystem = true;
            else if (step == "hudClosed") presenter.isSystem = false;
            else if (step == "edit") presenter.isEditing = true;
            else if (step == "editDone") { presenter.isEditing = false; coordinator.presentLatestSnapshot(0); }
            else if (step == "suspend") {
                // The oracle's stand-in only sets the flag. AppDelegate.suspend
                // also hides the alert without animation and drops a preview.
                coordinator.suspend(PowerSuspension::system, 0);
                extra.push_back("hide");
            } else if (step == "resume") check(coordinator.resume(PowerSuspension::system, std::nullopt, 0), name + " resume");
            else throw std::runtime_error("Unknown delegate step " + step);
            std::vector<std::string> calls(presenter.log.begin() + static_cast<std::ptrdiff_t>(before), presenter.log.end());
            std::vector<std::string> expected;
            for (const auto& call : row["calls"].array()) expected.push_back(call.string());
            expected.insert(expected.end(), extra.begin(), extra.end());
            std::string joined;
            for (const auto& c : calls) joined += c + ";";
            check(calls == expected, name + " " + step + " calls: " + joined);
            check(presenter.isVisible == row["visible"].boolean() && presenter.isPersistent == row["persistent"].boolean(),
                  name + " " + step + " visibility");
            const auto preview = coordinator.previewSnapshot() ? describe(*coordinator.previewSnapshot()) : std::string("nil");
            check(preview == row["preview"].string(), name + " " + step + " preview ownership");
            const auto menu = coordinator.snapshot() ? powerTrayDescription(*coordinator.snapshot(), language) : std::string();
            check(menu == row["menu"].string(), name + " " + step + " tray status: " + menu);
            check(row["tooltip"].string() == (menu.empty() ? std::string() : powerTrayTooltip(*coordinator.snapshot(), language)),
                  name + " " + step + " tray tooltip");
            ++rows;
        }
    }
    check(rows >= 60, "All AppDelegate battery rows compared");
    // Multi-reason wake: presentation resumes only after the last reason clears.
    RecordingPresenter presenter;
    PowerAlertCoordinator coordinator(presenter);
    coordinator.receive(f.snapshot("battery19"), 0);
    coordinator.suspend(PowerSuspension::system, 1);
    coordinator.suspend(PowerSuspension::display, 1);
    check(!coordinator.resume(PowerSuspension::session, std::nullopt, 2), "Unmatched wake is ignored");
    check(!coordinator.resume(PowerSuspension::system, f.snapshot("connected49"), 2) && coordinator.suspended(),
          "Display still asleep keeps presentation suspended");
    presenter.log.clear();
    check(coordinator.resume(PowerSuspension::display, std::nullopt, 3) && presenter.log.size() == 2 &&
          presenter.log[1] == "show(persistent=0,duration=3.0,replay=1)", "A plug change during sleep shows exactly once");
    presenter.log.clear();
    check(!coordinator.resume(PowerSuspension::display, std::nullopt, 4) && presenter.log.empty(), "Duplicate wake is inert");
    coordinator.terminate(5);
    presenter.log.clear();
    coordinator.receive(f.snapshot("battery19"), 6);
    coordinator.preview(6);
    check(presenter.log.empty(), "Termination stops presentation");
}

void tray() {
    BatteryReading charging; charging.present = charging.pluggedIn = charging.charging = true; charging.percentage = 63;
    for (auto language : {core::Language::english, core::Language::simplifiedChinese, core::Language::traditionalChinese,
                          core::Language::japanese, core::Language::korean}) {
        check(powerTrayDescription(charging, language).starts_with("63% · ") && !powerPreviewMenuTitle(language).empty(),
              "Localized tray status and preview item");
    }
    // Every user-visible Power string has its own catalog translation.
    BatteryReading none, plugged, battery;
    plugged.present = plugged.pluggedIn = true; plugged.percentage = 50;
    battery.present = true; battery.percentage = 50;
    for (auto language : {core::Language::simplifiedChinese, core::Language::traditionalChinese, core::Language::japanese,
                          core::Language::korean}) {
        const auto english = core::Language::english;
        check(powerTrayDescription(none, language) != powerTrayDescription(none, english) &&
              powerPreviewMenuTitle(language) != powerPreviewMenuTitle(english) &&
              chargeModeTitle(plugged, language) != chargeModeTitle(plugged, english) &&
              chargeModeTitle(battery, language) != chargeModeTitle(battery, english) &&
              chargePowerState(charging, language) != chargePowerState(charging, english) &&
              chargePowerState(plugged, language) != chargePowerState(plugged, english) &&
              chargePowerState(battery, language) != chargePowerState(battery, english) &&
              chargePositionButtonLabel(true, language) != chargePositionButtonLabel(true, english) &&
              chargePositionButtonLabel(false, language) != chargePositionButtonLabel(false, english),
              "Five-language Power strings never fall back to English");
    }
    const auto demo = chargeDemoReading();
    check(demo.percentage == 75u && demo.present && demo.pluggedIn && demo.charging && demo.capacity &&
          demo.capacity->unit == "mAh", "Source demo snapshot");
}

// OverlayController presentation rules on the explicit clock.
void overlay() {
    ChargeOverlayState s;
    BatteryReading r; r.present = r.pluggedIn = true; r.percentage = 49;
    s.update(r, false, 0);
    const auto revision = s.contentRevision();
    s.update(r, false, 0);
    check(s.contentRevision() == revision, "Identical reading is not a content event");
    check(!s.nextDeadline() && !s.windowVisible() && !s.requiresFrames(0), "Closed alert owns no deadline, frame or window");
    s.show(false, 3, true, 10);
    check(s.windowVisible() && s.visible() && s.stage() == ChargeStage::circle && s.nextDeadline() == 10.20 && s.requiresFrames(10.1),
          "Show orders the panel front and starts the source entrance");
    auto e = s.advance(11.5);
    check(e.presentationCompleted && s.stage() == ChargeStage::compact && s.nextDeadline() && std::abs(*s.nextDeadline() - 14.5) < 1e-9,
          "Dismissal deadline starts after the 1.5 s entrance completes");
    check(!s.requiresFrames(12), "Settled compact indicator requests no frames");
    s.show(false, 3, false, 12);
    check(std::abs(*s.nextDeadline() - 15) < 1e-9 && s.stage() == ChargeStage::compact, "Non-replay show only restarts the deadline");
    e = s.advance(15);
    check(e.dismissed && s.visible() && s.windowVisible() && s.requiresFrames(15.1), "Deadline starts the animated exit");
    e = s.advance(15.64);
    check(e.windowHidden && !s.windowVisible() && !s.visible() && !s.nextDeadline(), "Exit orders the panel out when its fade completes");
    // Duration clamp and persistent mode.
    s.show(false, 0.2, true, 20);
    check(s.requestedDuration() == 1, "Duration below one second is clamped");
    s.show(true, NAN, true, 20.1);
    check(s.requestedDuration() == 5 && s.persistent(), "Non-finite duration uses the source five seconds");
    s.advance(30);
    check(s.visible() && !s.nextDeadline(), "Persistent alert has no dismissal deadline");
    // Replaying while exiting cancels the exit completion.
    s.hide(true, 31);
    s.show(false, 2, true, 31.2);
    s.advance(31.7);
    check(s.windowVisible() && s.visible(), "A replay during exit keeps the panel");
    // HUD open: hide without animation first, then show is refused.
    s.hide(false, 40);
    check(!s.windowVisible() && !s.visible() && !s.nextDeadline(), "Unanimated hide releases the panel immediately");
    s.setSystemOverlayActive(true);
    s.show(false, 3, true, 41);
    check(!s.visible(), "No alert while the HUD is open");
    s.setSystemOverlayActive(false);
    s.setProjectionActive(true);
    s.show(false, 3, true, 41);
    check(!s.visible(), "No alert while projecting");
    s.setProjectionActive(false);
    // Reduce Motion: compact immediately, no frames.
    s.setReduceMotion(true);
    s.show(false, 3, true, 50);
    e = s.advance(50);
    check(e.presentationCompleted && s.stage() == ChargeStage::compact && !s.requiresFrames(50) && s.nextDeadline() == 53.0,
          "Reduce Motion presents the compact alert without animation");
    // Telemetry demand follows the panel only for non-battery metrics.
    auto settings = s.settings();
    check(!s.telemetryDemand(), "Battery metric needs no Activity sampler");
    settings.metric = ChargeMetric::cpu;
    s.setSettings(settings);
    check(s.telemetryDemand(), "CPU metric attaches while visible");
    s.hide(false, 51);
    check(!s.telemetryDemand(), "Hidden alert releases telemetry");
}

void editing() {
    ChargeOverlayState s;
    ChargeAlertSettings settings;
    settings.customPlacement = true;
    settings.customScreenID = 77;
    settings.customX = 0.25; settings.customY = 0.5;
    s.setSettings(settings);
    // Two monitors: a 200% internal panel and a 100% external one to its right.
    const std::vector<ChargeWorkArea> screens{{11, {0, 0, 2880, 1760}, 2}, {22, {2880, 0, 1920, 1040}, 1}};
    s.setScreens(screens, 0);
    check(s.selectedScreen()->id == 11, "Disconnected saved screen falls back to the preferred internal display");
    const auto anchor = *s.anchor();
    check(anchor.x == 720 && anchor.y == 880, "Saved fractions place the anchor (y from the bottom)");
    s.show(false, 3, true, 0);
    BatteryReading demo = chargeDemoReading();
    check(s.beginPositionEditing(demo, 1) && s.editingPosition() && s.windowVisible() && s.persistent() &&
          s.stage() == ChargeStage::compact && !s.nextDeadline() && s.previewContent(), "Editor shows the compact preview without deadlines");
    s.hide(true, 2);
    s.show(false, 3, true, 2);
    check(s.editingPosition() && s.windowVisible() && !s.nextDeadline(), "Alerts cannot interrupt position editing");
    s.beginDrag({720, 880});
    check(s.drag({3500, 500}) && s.selectedScreen()->id == 22, "Dragging onto another monitor retargets the draft");
    const auto draft = *s.draftAnchor();
    check(draft.x == 3500 && draft.y == 500, "Draft follows the pointer offset");
    s.drag({4790, 1030});
    check(s.draftAnchor()->x == 4800 - 150 && s.draftAnchor()->y == 1040 - 58, "Clamped by the editing margins of the target screen");
    s.endDrag();
    const auto confirmed = s.confirmPosition(3);
    check(confirmed && confirmed->screenID == 22u && std::abs(confirmed->x - (4650.0 - 2880) / 1920) < 1e-12 &&
          std::abs(confirmed->y - 58.0 / 1040) < 1e-12, "Confirm returns normalized fractions of the target work area");
    check(!s.editingPosition() && !s.windowVisible() && !s.visible(), "Confirm closes the editor without animation");
    check(s.beginPositionEditing(demo, 4), "Editing restarts");
    s.beginDrag(*s.draftAnchor());
    s.drag({100, 100});
    s.discardPosition(5);
    check(!s.editingPosition() && !s.windowVisible(), "Discard closes without a position");
    s.setSystemOverlayActive(true);
    check(!s.beginPositionEditing(demo, 6) && !s.editingPosition(), "The HUD must close before editing");
}
} // namespace

int main(int argc, char** argv) {
    try {
        check(argc == 2, "Pass charge-indicator-source.json");
        std::ifstream input(argv[1], std::ios::binary);
        std::stringstream bytes;
        bytes << input.rdbuf();
        Fixture fixture{Json::parse(bytes.str(), 64 * 1024 * 1024)};
        check(fixture.root["liveServices"].boolean() == false, "Detached oracle");
        policy(fixture);
        geometry(fixture);
        normalization(fixture);
        recorder(fixture);
        devices(fixture);
        capacities(fixture);
        delegate(fixture);
        tray();
        overlay();
        editing();
        std::cout << "Power policy: " << checks << " original-source checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Power policy after " << checks << " checks: " << error.what() << '\n';
        return 1;
    }
}
