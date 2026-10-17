// Source: windows/tools/charge_indicator_reference.sh (unchanged HUDChargeBadge
// with an injected scheduler, HUDDeploymentFlicker, frozen renderer clock).
#include "modules/charge_badge.hpp"
#include <cmath>
#include <fstream>
#include <iostream>
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
    if (value.isString()) { const auto s = value.string(); return s == "nan" ? NAN : s == "inf" ? INFINITY : s == "-inf" ? -INFINITY : std::stod(s); }
    return value.number();
}
std::vector<double> numbers(const Json& value) {
    std::vector<double> out;
    for (const auto& item : value.array()) out.push_back(number(item));
    return out;
}

void constants(const Json& badge) {
    auto rect = [](core::Rect r) { return std::vector<double>{r.x, r.y, r.width, r.height}; };
    auto same = [](const std::vector<double>& a, const std::vector<double>& b, const std::string& m) {
        check(a.size() == b.size(), m);
        for (std::size_t i = 0; i < a.size(); ++i) near(a[i], b[i], 1e-9, m);
    };
    same(rect(ChargeBadgeState::frame()), numbers(badge["frame"]), "Badge frame");
    same(rect(ChargeBadgeState::compactHitRect()), numbers(badge["compactHitRect"]), "Compact hit capsule");
    same(rect(ChargeBadgeState::circleHitRect()), numbers(badge["circleHitRect"]), "Circle hit capsule");
    same({ChargeBadgeState::center.x, ChargeBadgeState::center.y}, numbers(badge["center"]), "Badge center");
    near(ChargeBadgeState::rendererScale, number(badge["rendererScale"]), 1e-7, "Renderer scale");
    near(ChargeBadgeState::entranceDelay, number(badge["entranceDelay"]), 0, "Entrance delay");
    near(ChargeBadgeState::compactHoldDuration, number(badge["compactHoldDuration"]), 0, "Fixed compact hold");
    near(ChargeBadgeState::entranceDuration, number(badge["entranceDuration"]), 1e-12, "Entrance duration");
    near(ChargeBadgeState::exitDuration, number(badge["exitDuration"]), 1e-12, "Exit duration");
}

void phases(const Json& badge) {
    std::vector<core::Point> probes;
    for (const auto& p : badge["probes"].array()) probes.push_back({number(p.array()[0]), number(p.array()[1])});
    const ChargeIndicatorAppearance appearance{};
    BatteryReading demo;
    demo.percentage = 75; demo.present = demo.pluggedIn = demo.charging = true;
    const auto normal = chargeIndicatorColors(appearance, demo, ChargeMetric::battery, 0.75, ChargeStage::compact, false);
    const auto hovered = chargeIndicatorColors(appearance, demo, ChargeMetric::battery, 0.75, ChargeStage::compact, true);
    ChargeBadgeState state;
    state.setBorder(normal.borderWidth, normal.border, hovered.borderWidth, hovered.border, 0);
    // The source script, re-expressed on the explicit clock.
    struct Action { double at; std::function<void(double)> run; };
    std::vector<Action> actions{
        {0, [&](double t) { state.animateEntrance(t, 7); }},
        {5.5, [&](double t) { state.setHovered(true, true, t); }},
        {5.9, [&](double t) { state.setHovered(false, true, t); }},
        {6.4, [&](double t) { state.setHovered(true, true, t); }},
        {6.5, [&](double t) { state.animateExit(t, 8); }},
        {8.0, [&](double t) { state.animateEntrance(t, 9); }},
        {8.2, [&](double t) { state.animateExit(t, 10); }},
        {9.0, [&](double t) { state.setStable(true, t); }},
        {9.1, [&](double t) { state.setHovered(true, true, t); }},
        {9.2, [&](double t) { state.setStable(false, t); }},
        {9.9, [&](double) { state.setReduceMotion(true); }},
        {10.0, [&](double t) { state.animateEntrance(t, 11); }},
        {10.1, [&](double t) { state.animateExit(t, 12); state.advance(t); }},
    };
    std::size_t next = 0;
    unsigned rows{};
    for (const auto& row : badge["rows"].array()) {
        const double t = number(row["t"]);
        while (next < actions.size() && actions[next].at <= t + 1e-9) { state.advance(actions[next].at); actions[next].run(actions[next].at); ++next; }
        state.advance(t);
        const auto label = row["label"].string() + " t=" + std::to_string(t);
        check(std::string(chargeStageKey(state.stage())) == row["stage"].string(), label + " stage " + std::string(chargeStageKey(state.stage())));
        check(state.hovered() == row["hovered"].boolean(), label + " hover state");
        const auto hit = state.hitRect(t);
        const auto source = numbers(row["hitRect"]);
        near(hit.x, source[0], 3e-3, label + " hit x");
        near(hit.y, source[1], 3e-3, label + " hit y");
        near(hit.width, source[2], 3e-3, label + " hit width");
        near(hit.height, source[3], 3e-3, label + " hit height");
        const auto expected = row["contains"].string();
        check(expected.size() == probes.size(), label + " probe count");
        for (std::size_t i = 0; i < probes.size(); ++i)
            check(state.contains(probes[i], t) == (expected[i] == '1'),
                  label + " contains probe (" + std::to_string(probes[i].x) + "," + std::to_string(probes[i].y) + ")");
        const auto pending = numbers(row["pending"]);
        const auto deadline = state.nextDeadline();
        if (pending.empty()) check(!deadline || state.phase() == ChargeBadgeState::Phase::entering || state.phase() == ChargeBadgeState::Phase::exiting,
                                   label + " no badge deadline remains scheduled");
        else {
            // The injected source scheduler holds the badge's own delayed reveal
            // (0.58) or compact hold (3); it never exceeds that delay from now.
            check(deadline.has_value() && *deadline > t && *deadline - t <= pending.front() + 1e-9, label + " source scheduler deadline");
        }
        ++rows;
    }
    check(rows >= 40, "All source badge rows compared");
}

void contracts() {
    ChargeBadgeState state;
    check(!state.contains(ChargeBadgeState::center, 0) && state.hitRect(0).width == 0, "Hidden badge claims no blank-space clicks");
    state.animateEntrance(100, 1);
    check(state.phase() == ChargeBadgeState::Phase::waiting && state.nextDeadline() == 100.58 && !state.requiresFrames(100.3),
          "Delayed reveal is a single deadline, not frames, and cannot capture input");
    const auto events = state.advance(200);
    check(events.entranceCompleted && state.phase() == ChargeBadgeState::Phase::presented && state.stage() == ChargeStage::circle,
          "A late wake runs the reveal, entrance and compact hold in source order");
    check(!state.requiresFrames(200) && !state.nextDeadline(), "Resting circle requests no frames or deadlines");
    state.setHovered(true, true, 201);
    check(state.requiresFrames(201.1) && state.contains({ChargeBadgeState::center.x + 100, ChargeBadgeState::center.y}, 201),
          "Hover acquires the whole capsule immediately while the morph animates");
    state.advance(202);
    check(state.stage() == ChargeStage::compact && !state.requiresFrames(202), "Hover expansion settles");
    state.animateExit(300, 2);
    check(state.hitRect(300).width == 0 && state.requiresFrames(300.1), "Retraction releases input and animates");
    check(state.advance(300.64).exitCompleted && state.phase() == ChargeBadgeState::Phase::hidden && !state.requiresFrames(300.7),
          "Retraction completes with the source 0.64 s");
    // Flicker: opening and closing pulses stay within the documented bounds.
    for (std::uint64_t seed = 0; seed < 64; ++seed) {
        const auto open = deploymentFlickerSequence(true, 0.18, 0.14, seed);
        const auto close = deploymentFlickerSequence(false, 0.11, 0.07, seed);
        check(open.count >= 10 && open.count <= DeploymentFlickerSequence::capacity && close.count >= 6, "Pulse counts");
        check(open.offset(0) == 0 && open.offset(open.end() + 1) == 0 && close.offset(close.end()) == 0, "Flicker never outlives itself");
    }
}

void flicker(const Json& rows) {
    unsigned compared{};
    for (const auto& row : rows.array()) {
        const auto durationText = row["duration"].string();
        std::optional<double> duration;
        if (durationText == "nan") duration = NAN;
        else if (durationText != "nil") duration = std::stod(durationText);
        const auto seed = std::stoull(row["seed"].string());
        const auto s = deploymentFlickerSequence(row["opening"].boolean(), duration, number(row["delay"]), seed);
        const auto times = numbers(row["keyTimes"]), offsets = numbers(row["offsets"]);
        const auto label = "flicker seed " + row["seed"].string() + (row["opening"].boolean() ? " open " : " close ") + durationText;
        check(s.count == times.size() && times.size() == offsets.size(), label + " keyframe count");
        for (std::size_t i = 0; i < s.count; ++i) {
            near(s.keyTimes[i], times[i], 1e-12, label + " key time");
            near(s.offsets[i], offsets[i], 1e-12, label + " opacity offset");
        }
        near(s.duration, number(row["sequenceDuration"]), 1e-12, label + " duration");
        near(s.delay, number(row["sequenceDelay"]), 1e-12, label + " delay");
        ++compared;
    }
    check(compared >= 200, "All seeded source flicker sequences compared");
}
} // namespace

int main(int argc, char** argv) {
    try {
        check(argc == 2, "Pass charge-indicator-source.json");
        std::ifstream input(argv[1], std::ios::binary);
        std::stringstream bytes;
        bytes << input.rdbuf();
        const auto root = Json::parse(bytes.str(), 64 * 1024 * 1024);
        constants(root["badge"]);
        phases(root["badge"]);
        contracts();
        flicker(root["flicker"]);
        std::cout << "Charge badge: " << checks << " original-source checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Charge badge after " << checks << " checks: " << error.what() << '\n';
        return 1;
    }
}
