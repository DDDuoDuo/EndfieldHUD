// Red power button confirmation against the unchanged HUDQuitConfirmationView
// (default quit content), plus its Windows key/pointer/fade lifecycle.
#include "app/quit_confirmation_state.hpp"
#include "core/data/file_io.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

namespace app = endfield::app;
namespace core = endfield::core;
namespace m = endfield::modules;
using ehud::data::Json;
namespace {
unsigned checks{};
void check(bool value, const std::string& why) { ++checks; if (!value) throw std::runtime_error(why); }
void close(double a, double b, const std::string& why) { check(std::abs(a - b) < 2e-6, why); }
void box(core::Rect r, const Json& v, const std::string& why) {
    const auto& a = v.array();
    close(r.x, a[0].number(), why + " x"); close(r.y, a[1].number(), why + " y"); close(r.width, a[2].number(), why + " width"); close(r.height, a[3].number(), why + " height");
}

void oracle(const Json& source) {
    check(source["actualSource"].boolean() && !source["windowCreated"].boolean() && source["provenance"]["modifications"].array().empty(), "Unchanged detached source view");
    const auto& cases = source["cases"].array();
    check(cases.size() == 12, "Both languages, three widths, both themes");
    for (const auto& row : cases) {
        app::QuitConfirmationState state;
        state.setLanguage(row["chinese"].boolean() ? core::Language::simplifiedChinese : core::Language::english);
        m::SettingsAppearance appearance; appearance.dark = row["dark"].boolean();
        state.setAppearance(appearance);
        const core::Rect viewport{0, 0, row["width"].number(), 800};
        box(m::SettingsSafety::geometry(viewport).card, row["card"], "Source card frame");
        // The view's own layer dims the surround: black .30 dark / .22 light.
        close(row["background"].array()[3].number(), appearance.dark ? .30 : .22, "Source dim alpha");
        check(row["accessibilityLabel"].string() == state.strings().title && row["accessibilityHelp"].string() == state.strings().message, "Accessible card name/help");
        const auto art = app::prepareQuitConfirmationArtwork(state, viewport, appearance);
        const auto& children = art.layers["children"].array();
        check(children.size() == 9, "Four artwork leaves plus five source text cells");
        const auto& controls = row["controls"].array();
        check(controls.size() == 5, "Source title, message, caption and two buttons");
        for (unsigned n = 0; n < 5; ++n) {
            const auto& text = children[n + 4];
            const auto& control = controls[n];
            const auto& p = text["position"].array();
            const auto& b = text["bounds"].array();
            auto expected = control["frame"].array();
            if (n >= 3) { expected[1] = expected[1].number() + control["titleRect"].array()[1].number(); expected[3] = control["titleRect"].array()[3]; }
            box({p[0].number(), p[1].number(), b[2].number(), b[3].number()}, expected, "Source text cell " + std::to_string(n));
            check(text["text"]["string"] == control["value"], "Source string " + control["value"].string());
            check(text["text"]["font"]["postScriptName"] == control["font"]["name"], "Source font face");
            close(text["text"]["fontSize"].number(), control["font"]["size"].number(), "Source font size");
            if (n < 3) for (unsigned c = 0; c < 4; ++c) close(text["text"]["foregroundColor"]["sRGB"].array()[c].number(), control["color"].array()[c].number(), "Source text color");
        }
        check(controls[3]["accessibility"].string() == state.strings().cancelAccessibility && controls[4]["accessibility"].string() == state.strings().confirmAccessibility,
              "Accessible button names");
        const auto& plate = children[0];
        const auto& sourcePlate = row["artwork"].array()[0];
        check(plate["shape"]["path"].array().size() == sourcePlate["path"].array().size(), "Source cut-corner topology");
        for (std::size_t n = 0; n < sourcePlate["path"].array().size(); ++n) {
            const auto& a = plate["shape"]["path"].array()[n];
            const auto& b = sourcePlate["path"].array()[n];
            check(a["op"] == b["op"], "Source path op");
            for (std::size_t k = 0; k < a["points"].array().size(); ++k) {
                close(a["points"].array()[k].array()[0].number() + .5, b["points"].array()[k].array()[0].number(), "Source path x");
                close(a["points"].array()[k].array()[1].number() + .5, b["points"].array()[k].array()[1].number(), "Source path y");
            }
        }
        for (unsigned c = 0; c < 4; ++c) {
            close(plate["shape"]["fillColor"]["sRGB"].array()[c].number(), sourcePlate["fill"].array()[c].number(), "Source card fill");
            close(plate["shape"]["strokeColor"]["sRGB"].array()[c].number(), sourcePlate["stroke"].array()[c].number(), "Source card stroke");
        }
        close(plate["shape"]["lineWidth"].number(), sourcePlate["lineWidth"].number(), "Source card stroke width");
        for (unsigned c = 0; c < 4; ++c) close(children[1]["backgroundColor"]["sRGB"].array()[c].number(), row["artwork"].array()[1]["background"].array()[c].number(), "Source header line");
        // Button plates: the detached view has no first responder, so compare
        // against the unfocused source paint; the focused Cancel uses accent .9/1.25.
        for (unsigned n = 0; n < 2; ++n) {
            const auto& source = row["artwork"].array()[2 + n];
            const auto paint = state.buttonPaint(n, 0);
            for (unsigned c = 0; c < 4; ++c) close(paint.fill[c], source["fill"].array()[c].number(), "Source button fill");
            if (n == 1) { for (unsigned c = 0; c < 4; ++c) close(paint.stroke[c], source["stroke"].array()[c].number(), "Source unfocused button stroke"); close(paint.width, source["lineWidth"].number(), "Source unfocused width"); }
            else { close(paint.width, 1.25, "Focused Cancel width"); close(paint.stroke[3], .90, "Focused Cancel accent stroke"); }
        }
    }
}
} // namespace

int main(int argc, char** argv) {
    try {
        check(argc == 2, "Pass the detached source quit confirmation fixture");
        const auto bytes = ehud::data::detail::readFile(argv[1], 4 * 1024 * 1024);
        check(bytes.has_value(), "Read the bounded fixture");
        oracle(Json::parse(*bytes));
        std::cout << "Quit confirmation: " << checks << " source checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Quit confirmation failed after " << checks << ": " << e.what() << '\n';
        return 1;
    }
}
