#include "core/system_overlay_state.hpp"
#include "core/data/file_io.hpp"
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>

namespace core = endfield::core;
using ehud::data::Json;
namespace {
unsigned checks{};
void check(bool value, const std::string& why) { ++checks; if (!value) throw std::runtime_error(why); }

core::Module moduleNamed(std::string_view name) {
    for (unsigned n = 0; n <= static_cast<unsigned>(core::Module::profile); ++n)
        if (core::moduleIdentifier(static_cast<core::Module>(n)) == name) return static_cast<core::Module>(n);
    throw std::runtime_error("Unknown source module " + std::string(name));
}
int phaseIndex(core::SystemOverlayPhase value) { return static_cast<int>(value); }

void raw(const Json& source) {
    core::SystemOverlayState state;
    std::size_t rows{};
    for (const auto& row : source["raw"].array()) {
        const auto& v = row.array();
        if (v.size() == 1) { state = {}; continue; }
        const auto op = v[0].integer();
        core::SystemOverlayAction action;
        int closed = -1;
        switch (op) {
        case 0: action = state.toggle(); break;
        case 1: action = state.requestClose(); break;
        case 2: action = state.requestClose(true); break;
        case 3: action = state.didOpen(state.generation()); break;
        // The Swift run passes generation-1/-2 or +/-1 for these stale tokens;
        // every stale token leaves the state unchanged, as recorded.
        case 4: action = state.didOpen(state.generation() - 1); break;
        case 5: closed = state.didClose(state.generation()) ? 1 : 0; break;
        case 6: closed = state.didClose(state.generation() - 1) ? 1 : 0; break;
        default: state.forceClose(); break;
        }
        const int kind = action.kind == core::SystemOverlayAction::Kind::none ? 0 : action.kind == core::SystemOverlayAction::Kind::open ? 1 : 2;
        check(kind == v[1].integer(), "Raw SystemOverlayState action kind, row " + std::to_string(rows));
        if (kind) check(action.token == v[2].integer(), "Raw SystemOverlayState token");
        check(closed == v[3].integer(), "Raw didClose result");
        check(phaseIndex(state.phase()) == v[4].integer(), "Raw phase, row " + std::to_string(rows));
        check(state.generation() == v[5].integer(), "Raw generation");
        check(state.closeAfterOpening() == (v[6].integer() != 0), "Raw closeAfterOpening");
        check(state.active() == (v[7].integer() != 0), "Raw isActive");
        ++rows;
    }
    check(rows == 400 * 24, "Every seeded raw transition was replayed");
}

std::string effectText(const core::SystemOverlayEffect& e) {
    using K = core::SystemOverlayEffect::Kind;
    switch (e.kind) {
    case K::open: return "open:" + std::to_string(e.token) + ":" + std::string(core::moduleIdentifier(e.module));
    case K::close: return "close:" + std::to_string(e.token);
    case K::preview: return "preview";
    case K::terminate: return "terminate";
    default: return {};
    }
}

void scripts(const Json& source) {
    std::size_t total{};
    for (const auto& script : source["scripts"].array()) {
        core::SystemOverlayLifecycle life;
        bool defaults{};
        unsigned stops{};
        double now{};
        const auto& rows = script["rows"].array();
        const auto& events = script["events"].array();
        check(rows.size() == events.size(), "One snapshot per scripted event");
        for (std::size_t index = 0; index < rows.size(); ++index) {
            const auto& row = rows[index];
            const auto event = events[index].string();
            const auto colon = event.find(':');
            const auto name = event.substr(0, colon);
            const auto argument = colon == std::string::npos ? std::string{} : event.substr(colon + 1);
            core::SystemOverlayEffects effects;
            if (name == "launched") defaults = true;
            else if (name == "launch") {
                core::SystemOverlayStartup startup;
                std::size_t at{};
                while (at <= argument.size() && !argument.empty()) {
                    const auto end = std::min(argument.find(',', at), argument.size());
                    const auto flag = argument.substr(at, end - at);
                    if (flag == "--login") startup.login = true;
                    else if (flag == "--no-onboarding") startup.noOnboarding = true;
                    else if (flag == "--settings") startup.settings = true;
                    else if (flag == "--power") startup.power = true;
                    else if (flag == "--preview") startup.preview = true;
                    else throw std::runtime_error("Unknown scripted launch argument");
                    at = end + 1;
                }
                effects = life.launch(startup, defaults);
            }
            else if (name == "hotkey") effects = life.toggle();
            else if (name == "trayOpen") effects = life.openOverlay();
            else if (name == "traySettings") effects = life.openSettingsModule(core::Module::system);
            else if (name == "trayAbout") effects = life.openSettingsModule(core::Module::about);
            else if (name == "trayWorkMode") effects = life.openWorkMode(now);
            else if (name == "reopen") effects = life.reopen();
            else if (name == "select") effects = life.selectModule(moduleNamed(argument));
            else if (name == "opened") effects = life.didOpen(life.generation());
            else if (name == "openedStale") effects = life.didOpen(life.generation() - 1);
            else if (name == "closed") effects = life.didClose(life.generation());
            else if (name == "closedStale") effects = life.didClose(life.generation() - 1);
            else if (name == "close") effects = life.close();
            else if (name == "force") effects = life.forceClose();
            else if (name == "quit") effects = life.requestQuit();
            else if (name == "suspend") life.setSuspended(true);
            else if (name == "resume") life.setSuspended(false);
            else if (name == "editing") life.setEditingPosition(argument == "on");
            else if (name == "advance") { now += std::stod(argument); effects = life.advance(now); }
            else throw std::runtime_error("Unknown scripted event " + event);
            const auto where = " after " + event + " (script event " + std::to_string(index) + ")";
            Json::Array texts;
            for (const auto& e : effects) {
                if (e.kind == core::SystemOverlayEffect::Kind::markLaunched) defaults = true;
                if (e.kind == core::SystemOverlayEffect::Kind::quitAccepted) ++stops;
                if (auto text = effectText(e); !text.empty()) texts.emplace_back(std::move(text));
            }
            const auto& expected = row["effects"].array();
            check(texts.size() == expected.size(), "Effect count" + where);
            for (std::size_t n = 0; n < texts.size(); ++n) check(texts[n].string() == expected[n].string(), "Effect order/value" + where);
            check(std::string(core::systemOverlayPhaseName(life.phase())) == row["phase"].string(), "Phase" + where);
            check(life.generation() == row["generation"].integer(), "Generation" + where);
            check(life.state().closeAfterOpening() == row["closeAfterOpening"].boolean(), "Queued focus-loss close" + where);
            check(row["view"].isNull() == !life.viewModule().has_value(), "Live presentation existence" + where);
            if (life.viewModule()) check(core::moduleIdentifier(*life.viewModule()) == row["view"].string(), "Live selected module" + where);
            check(core::moduleIdentifier(life.lastModule()) == row["last"].string(), "lastSystemModule" + where);
            check(row["request"].isNull() == !life.initialModuleRequest().has_value(), "initialModuleRequest existence" + where);
            if (life.initialModuleRequest()) check(core::moduleIdentifier(*life.initialModuleRequest()) == row["request"].string(), "initialModuleRequest" + where);
            check(life.quitRequested() == row["quitRequested"].boolean(), "quitRequested" + where);
            check(life.terminating() == row["terminating"].boolean(), "terminating" + where);
            check(stops == row["shortcutStops"].integer(), "Shortcut stops" + where);
            check(defaults == row["hasLaunched"].boolean(), "hasLaunched marker" + where);
            const auto& timers = row["timers"].array();
            check(life.pendingSelections() == timers.size(), "Pending delayed selections" + where);
            for (std::size_t n = 0; n < timers.size(); ++n)
                check(*life.pendingSelectionDue(n) - now == timers[n].number(), "Delayed selection deadline" + where);
            ++total;
        }
    }
    check(total > 300, "Scripted lifecycle coverage");
}

void contracts() {
    core::SystemOverlayLifecycle life;
    auto first = life.launch({}, false);
    check(first.size() == 2 && first[0].kind == core::SystemOverlayEffect::Kind::markLaunched &&
          first[1].kind == core::SystemOverlayEffect::Kind::open && first[1].module == core::Module::map,
          "First run persists the marker, then opens Map first");
    check(life.toggle().empty() && life.toggle().empty(), "Repeated shortcut strokes during opening are ignored");
    (void)life.didOpen(life.generation());
    auto closing = life.toggle();
    check(closing.size() == 1 && closing[0].kind == core::SystemOverlayEffect::Kind::close, "Open overlay toggles closed");
    check(life.toggle().empty() && life.reopen().empty() && life.openOverlay().empty(), "Hotkey/relaunch during closing never reopens");
    (void)life.didClose(life.generation());
    auto quit = life.openSettingsModule(core::Module::about);
    check(quit.size() == 1 && quit[0].module == core::Module::about, "Tray About opens About as the initial module");
    (void)life.didOpen(life.generation());
    auto confirmed = life.requestQuit();
    check(confirmed.size() == 2 && confirmed[0].kind == core::SystemOverlayEffect::Kind::quitAccepted &&
          confirmed[1].kind == core::SystemOverlayEffect::Kind::close, "Confirmed quit stops the shortcut and plays the close");
    check(life.toggle().empty() && life.reopen().empty(), "Terminating ignores summon requests");
    auto done = life.didClose(life.generation());
    check(done.size() == 1 && done[0].kind == core::SystemOverlayEffect::Kind::terminate, "Quit terminates only after concealment");
    auto cancelled = life.cancelTermination();
    check(cancelled.size() == 1 && cancelled[0].kind == core::SystemOverlayEffect::Kind::restartShortcut && !life.terminating(),
          "A failed document drain restores the summon path");
    auto reopened = life.openSettingsModule(core::Module::system);
    check(reopened.size() == 1 && reopened[0].module == core::Module::system, "Failed save can reopen the affected module");
    // Windows tray Quit.
    core::SystemOverlayLifecycle tray;
    auto immediate = tray.requestTermination();
    check(immediate.size() == 2 && immediate[1].kind == core::SystemOverlayEffect::Kind::terminate, "Tray Quit of a closed HUD drains immediately");
    core::SystemOverlayLifecycle visible;
    (void)visible.toggle();
    auto animated = visible.requestTermination();
    check(animated.size() == 1 && animated[0].kind == core::SystemOverlayEffect::Kind::quitAccepted && visible.state().closeAfterOpening(),
          "Tray Quit during opening closes after the opening completes");
    auto after = visible.didOpen(visible.generation());
    check(after.size() == 1 && after[0].kind == core::SystemOverlayEffect::Kind::close, "Queued close follows the opening");
    auto finished = visible.didClose(visible.generation());
    check(finished.size() == 1 && finished[0].kind == core::SystemOverlayEffect::Kind::terminate, "Tray Quit terminates after the close");
    bool rejected{};
    try { (void)visible.advance(std::nan("")); } catch (const std::invalid_argument&) { rejected = true; }
    check(rejected, "Nonfinite caller clocks reject instead of spinning");
}
} // namespace

int main(int argc, char** argv) {
    try {
        check(argc == 2, "Pass the explicit Mac lifecycle oracle");
        const auto bytes = ehud::data::detail::readFile(argv[1], 4 * 1024 * 1024);
        check(bytes.has_value(), "Read the bounded lifecycle oracle");
        const auto source = Json::parse(*bytes);
        check(source["provenance"]["compiledUnchanged"].array().size() == 1, "Unchanged SystemOverlayState source");
        check(source["entranceDuration"].number() == core::SystemOverlayLifecycle::entranceDuration, "SystemHUDView entrance duration");
        raw(source);
        scripts(source);
        contracts();
        std::cout << "System overlay lifecycle: " << checks << " source/contract checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "System overlay lifecycle failed after " << checks << ": " << e.what() << '\n';
        return 1;
    }
}
