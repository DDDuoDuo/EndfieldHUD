// Map accessibility contract (modules/map_accessibility.hpp) against the
// unchanged Mac sources (tools/map_accessibility_reference.py ->
// map_accessibility_source.json): status/help text, toolbar and pin buttons
// in source order with source geometry, pin labels and the 12 Hz cadence.
// Portable; synthetic in-memory map state only.
#include "modules/map_accessibility.hpp"
#include "core/data/file_io.hpp"
#include "core/data/json.hpp"
#include <array>
#include <cmath>
#include <iostream>

namespace {
using namespace endfield;
namespace m = modules;
using J = ehud::data::Json;
using L = core::Language;
unsigned checks{};
void check(bool value, const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
constexpr std::array<std::pair<L, const char*>, 5> languages{{{L::english, "english"}, {L::simplifiedChinese, "simplifiedChinese"},
    {L::traditionalChinese, "traditionalChinese"}, {L::japanese, "japanese"}, {L::korean, "korean"}}};
core::Rect rect(const J& v) { const auto& a = v.array(); return {a[0].number(), a[1].number(), a[2].number(), a[3].number()}; }

void contract(const J& root) {
    check(root["source"]["authority"].string() == "ca04f142185c7de40acd8523bdb563195d90a1d1" && root["cadenceHz"].integer() == 12,
          "Oracle pinned to the Mac authority");
    m::MapSnapshot snapshot;
    const m::MapViewport viewport;
    const std::array<core::Point, 3> screens{{{200, 200}, {260, 200}, {230, 262}}};
    const std::array<m::MapPinStyle, 3> styles{m::MapPinStyle::yellow, m::MapPinStyle::player, m::MapPinStyle::green};
    for (std::size_t n = 0; n < screens.size(); ++n) {
        const auto world = m::mapWorld(screens[n], viewport);
        snapshot.pins.push_back({"A0000000-0000-4000-8000-00000000000" + std::to_string(n + 1), world.x, world.y, 0, styles[n]});
    }
    snapshot.viewport = viewport;
    m::MapSnapshot committed;
    m::MapPersistence persistence{[&](const m::MapSnapshot& s) { committed = s; }, [] { return std::string("A0000000-0000-4000-8000-000000000009"); }, [] { return 0.0; }};
    m::MapState state(snapshot, persistence);
    const auto label = [&](const m::MapState& s, std::size_t index, L language) {
        const auto& pin = s.pins()[index];
        return m::mapPinTitle(pin.style, language) + " " + std::to_string(index + 1) + ", " + m::mapCoordinateDescription(pin.x, pin.y);
    };
    for (const auto& [language, key] : languages) {
        const auto a = m::mapAccessibility(state, language);
        check(a.status == root["status"][key].string() && a.help == a.status + root["help"][key].string(), "Status and help follow the Mac canvas in five languages");
        check(a.bounds == core::Rect{0, 0, 440, 440}, "The status element covers the canvas");
        check(a.elements.size() == 7, "Three toolbar controls, the pin button and three visible pins");
        for (std::size_t n = 0; n < 3; ++n) {
            const auto& expected = root["toolbar"].array()[n];
            check(a.elements[n].id == expected["id"].string() && a.elements[n].label == expected[key].string() && a.elements[n].rect == rect(expected["rect"]),
                  "Toolbar ids, labels and geometry are the source toolbarActions");
        }
        check(a.elements[0].enabled && a.elements[1].enabled && a.elements[2].enabled, "Zoom and reset are enabled at the starting zoom");
        const auto& add = root["addPin"];
        check(a.elements[3].id == "map:addPin" && a.elements[3].label == add[key].string() && a.elements[3].rect == rect(add["rect"]) && a.elements[3].enabled,
              "Place pin follows the source action");
        for (std::size_t n = 0; n < 3; ++n) {
            const auto& element = a.elements[4 + n];
            check(element.id == "map:pin:" + snapshot.pins[n].id && element.label == label(state, n, language) && element.enabled,
                  "Pins read their style title, 1-based index and coordinates");
            check(element.rect == m::mapMarkerHitRect(styles[n], m::mapScreen(snapshot.pins[n].x, snapshot.pins[n].y, viewport)), "Pins use their hit rectangles");
        }
        check(m::mapPinTitle(m::MapPinStyle::yellow, language) == root["pinTitles"]["yellow"][key].string() &&
              m::mapPinTitle(m::MapPinStyle::green, language) == root["pinTitles"]["green"][key].string() &&
              m::mapPinTitle(m::MapPinStyle::player, language) == root["pinTitles"]["player"][key].string(), "Pin style titles follow WorldMapPinArtwork");
    }
    // Clicking a pin cycles its style, selects it and shows coordinates: the
    // source then offers "Remove selected pin" after the place-pin control.
    (void)state.setActive(true);
    check(state.perform(m::MapAction::pin, snapshot.pins[0].id).changed && state.selection() == snapshot.pins[0].id && !state.error(),
          "A pin is selected through its action");
    for (const auto& [language, key] : languages) {
        const auto a = m::mapAccessibility(state, language);
        const auto& remove = root["deletePin"];
        check(a.elements.size() == 8 && a.elements[4].id == "map:deletePin" && a.elements[4].label == remove[key].string() && a.elements[4].rect == rect(remove["rect"]),
              "Remove selected pin follows place pin");
        check(a.elements[5].label == label(state, 0, language) && a.elements[5].label.starts_with(m::mapPinTitle(m::MapPinStyle::green, language)),
              "The cycled pin reads its new style");
    }
    const auto failed = m::mapAccessibility(state, L::english, std::string("Country shapes could not be loaded."));
    // The source appends its ". Drag to pan..." sentence to any status label.
    check(failed.status == "Country shapes could not be loaded." && failed.help == failed.status + root["help"]["english"].string(),
          "A visible failure replaces the status label");
    // The zoom limits disable their controls.
    auto deep = snapshot;
    deep.viewport.zoom = m::MapViewport::maximumZoom;
    m::MapState zoomed(deep);
    const auto z = m::mapAccessibility(zoomed, L::english);
    check(!z.elements[0].enabled && z.elements[1].enabled && !z.elements[3].enabled, "Maximum zoom disables Zoom in; a read-only map cannot place pins");
}

void cadence() {
    m::MapAccessibilityCadence c;
    check(c.request(10, false), "Idle content publishes at once");
    c.published(10);
    check(c.request(10.01, false), "Without a gesture every change publishes at once");
    c.published(10.01);
    check(!c.request(10.02, true) && c.deadline() && std::abs(*c.deadline() - (10.01 + 1.0 / 12)) < 1e-12, "A gesture defers to 1/12 s after the last publication");
    check(!c.request(10.05, true) && std::abs(*c.deadline() - (10.01 + 1.0 / 12)) < 1e-12, "One pending publication only");
    check(!c.due(10.05, true) && c.due(10.01 + 1.0 / 12, true), "The pending publication is due at its deadline");
    c.published(10.01 + 1.0 / 12);
    check(!c.deadline() && !c.request(10.12, true), "The next gesture change waits again");
    check(c.due(10.13, false), "The gesture's end flushes the pending publication at once");
    c.published(10.13);
    check(c.request(10.13 + 1.0 / 12, true), "A full interval later a gesture change publishes at once");
    c.published(10.3);
    check(!c.request(10.31, true), "Pending again");
    c.cancel();
    check(!c.deadline() && !c.due(20, false), "Cancel drops the pending publication");
}
} // namespace

int main(int argc, char** argv) {
    try {
        check(argc == 2, "Usage: map_accessibility_tests map_accessibility_source.json");
        const auto raw = ehud::data::detail::readFile(argv[1], 1024 * 1024);
        check(raw.has_value(), "The Mac oracle fixture exists");
        contract(J::parse(*raw));
        cadence();
        std::cout << "PASS " << checks << " Map accessibility checks\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL after " << checks << ": " << e.what() << '\n';
        return 1;
    }
}
