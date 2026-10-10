// ModuleRegistry contracts: routing order, capture chains, failure isolation
// and release order. Synthetic owners only; no window, renderer or GPU.
#include "app/module_owner.hpp"
#include "app/quit_confirmation_state.hpp"
#include "native/renderer.hpp"
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace app = endfield::app;
namespace core = endfield::core;
namespace {
unsigned checks{};
void check(bool value, const std::string& why) { ++checks; if (!value) throw std::runtime_error(why); }

struct Probe final : app::ModuleOwner {
    std::string id;
    app::ModuleRouting route;
    std::vector<std::string>* log{};
    bool consumes{}, captures{}, throwsOnUpdate{}, essentialOwner{}, covering{};
    core::Module module{core::Module::notes};
    unsigned updates{}, releases{}, appearance{};
    Probe(std::string name, std::vector<std::string>& journal) : id(std::move(name)), log(&journal) {}
    std::string_view name() const noexcept override { return id; }
    app::ModuleRouting routing() const noexcept override { return route; }
    bool essential() const noexcept override { return essentialOwner; }
    bool presents(core::Module m) const noexcept override { return m == module; }
    void setAppearance(const app::ModuleAppearance&, double) override { ++appearance; }
    void update(const app::ModuleFrame&) override { if (throwsOnUpdate) throw std::out_of_range("synthetic failure with /private/path"); ++updates; }
    bool covers(core::Point) const override { return covering; }
    bool capturesPointer() const override { return captures; }
    bool pointer(const app::PointerEvent&, double) override { log->push_back(id); return consumes; }
    bool key(const app::KeyEvent&, double) override { log->push_back(id); return consumes; }
    void release(endfield::native::Renderer&) override { ++releases; log->push_back("release:" + id); }
};

void routing() {
    std::vector<std::string> journal;
    Probe notes("notes", journal), settings("settings", journal), archive("archive", journal), shelf("shelf", journal);
    notes.route.pointer = 1; settings.route.pointer = 2; settings.route.pointerCapture = 1; archive.route.pointer = 3; shelf.route.pointer = 8;
    notes.route.key = 1; settings.route.key = 2;
    app::ModuleRegistry registry;
    // Registration order differs from routing order on purpose.
    registry.add(shelf); registry.add(archive); registry.add(notes); registry.add(settings);
    bool threw{};
    try { registry.add(notes); } catch (const std::logic_error&) { threw = true; }
    check(threw, "Duplicate registration is rejected");
    using C = app::ModuleRegistry::Chain;
    const app::PointerEvent e{app::PointerKind::move, app::PointerButton::none, 1, 1};
    auto* consumed = registry.first(C::pointer, [&](app::ModuleOwner& o, const app::ModuleRouting&) { return o.pointer(e, 0); });
    check(!consumed && journal == std::vector<std::string>{"notes", "settings", "archive", "shelf"}, "Pointer chain follows routing priority, not registration order");
    journal.clear();
    settings.captures = true; settings.consumes = true;
    consumed = registry.first(C::pointerCapture, [&](app::ModuleOwner& o, const app::ModuleRouting&) { return o.capturesPointer() && o.pointer(e, 0); });
    check(consumed == &settings && journal == std::vector<std::string>{"settings"}, "Capture chain runs only capturing owners and stops at the consumer");
    journal.clear();
    notes.consumes = true;
    consumed = registry.first(C::key, [&](app::ModuleOwner& o, const app::ModuleRouting&) { return o.key({}, 0); });
    check(consumed == &notes && journal == std::vector<std::string>{"notes"}, "Key chain stops at the first consumer");
    check(registry.first(C::wheel, [](app::ModuleOwner&, const app::ModuleRouting&) { return true; }) == nullptr, "Owners without a wheel route are excluded");
    archive.module = core::Module::archive;
    check(registry.presenter(core::Module::archive) == &archive && registry.presenter(core::Module::map) == nullptr, "Presenter lookup");
    // Mutation during routing is rejected.
    threw = false;
    Probe late("late", journal);
    registry.forEach([&](app::ModuleOwner&) { if (!threw) { try { registry.add(late); } catch (const std::logic_error&) { threw = true; } } });
    check(threw && !registry.contains(late), "Registry cannot change while routing");
    // Release in reverse registration order.
    journal.clear();
    endfield::native::Renderer renderer; // never initialized: probes own no GPU resources
    registry.releaseAll(renderer);
    check((journal == std::vector<std::string>{"release:settings", "release:notes", "release:archive", "release:shelf"}), "GPU release is reverse registration order");
    registry.remove(archive);
    check(!registry.contains(archive) && registry.size() == 3, "Removal");
}

void isolation() {
    std::vector<std::string> journal;
    Probe notes("notes", journal), map("map", journal), clipboard("clipboard", journal);
    notes.essentialOwner = true; map.throwsOnUpdate = true; map.covering = true; map.module = core::Module::map;
    map.route.pointer = 1; clipboard.route.pointer = 2;
    std::vector<app::ModuleFailure> observed;
    app::ModuleRegistry registry(true);
    registry.setFailureObserver([&](const app::ModuleFailure& f) { observed.push_back(f); });
    registry.add(notes); registry.add(map); registry.add(clipboard);
    const core::Matrix4 center;
    const core::source::DesktopChromeSettings chrome{};
    const core::ModulePresentationSample presentation{};
    const app::ModuleFrame frame{center, chrome, presentation, 1, 0, true};
    registry.forEach([&](app::ModuleOwner& o) { o.update(frame); }, "frame");
    check(notes.updates == 1 && clipboard.updates == 1 && !registry.enabled(map), "A throwing owner is disabled; the others still update");
    check(observed.size() == 1 && observed[0].owner == "map" && observed[0].operation == "frame" &&
          observed[0].what.find("/private/path") == std::string::npos, "Failure report names the owner without the exception message");
    registry.forEach([&](app::ModuleOwner& o) { o.update(frame); }, "frame");
    check(observed.size() == 1 && clipboard.updates == 2, "A disabled owner is skipped afterwards");
    check(!registry.covers({0, 0}) && registry.presenter(core::Module::map) == nullptr, "A disabled owner neither covers input nor presents content");
    journal.clear();
    const app::PointerEvent e{app::PointerKind::down, app::PointerButton::left, 1, 1};
    (void)registry.first(app::ModuleRegistry::Chain::pointer, [&](app::ModuleOwner& o, const app::ModuleRouting&) { return o.pointer(e, 0); });
    check(journal == std::vector<std::string>{"clipboard"}, "Input skips the disabled owner");
    endfield::native::Renderer renderer;
    registry.releaseAll(renderer);
    check(map.releases == 1, "A disabled owner still releases its GPU resources");
    // Essential owners are never isolated: their failure still ends the run.
    notes.throwsOnUpdate = true;
    bool threw{};
    try { registry.forEach([&](app::ModuleOwner& o) { o.update(frame); }, "frame"); } catch (const std::out_of_range&) { threw = true; }
    check(threw && registry.enabled(notes), "Essential owner failures propagate");
    // Without isolation (development previews) every failure propagates.
    app::ModuleRegistry strict;
    Probe failing("failing", journal);
    failing.throwsOnUpdate = true;
    strict.add(failing);
    threw = false;
    try { strict.forEach([&](app::ModuleOwner& o) { o.update(frame); }); } catch (const std::out_of_range&) { threw = true; }
    check(threw && strict.enabled(failing), "Development registries keep failures fatal");
}

void quitState() {
    app::QuitConfirmationState card;
    card.setLanguage(core::Language::simplifiedChinese);
    check(card.strings().title == "退出 EndfieldHUD？" && card.strings().confirm == "退出", "Localized quit content");
    card.setLanguage(core::Language::english);
    check(card.strings().title == "Quit EndfieldHUD?" && card.strings().message == "The application will quit after the HUD closes." &&
          card.strings().cancel == "Cancel" && card.strings().confirmAccessibility == "Quit EndfieldHUD application", "Source quit content");
    check(!card.key(0x1B, false, 0).has_value(), "Hidden card consumes nothing");
    card.show(1);
    check(card.presented() && card.focused() == 0 && card.pose(1).opacity == 0 && card.pose(1).travel == 12, "Reveal starts transparent and 12pt deep with Cancel focused");
    check(card.pose(1.17).opacity == 1 && card.pose(1.25).travel == 0 && !card.requiresFrames(1.3), "Reveal settles after .16/.20 s");
    check(card.key(0x0D, false, 1.4) == app::QuitAnswer::cancel, "Return activates focused Cancel");
    card.dismiss(true, 1.4);
    check(card.presented() && card.key(0x0D, false, 1.45) == app::QuitAnswer::none, "Input stays consumed during the .14 s fade");
    check(!card.refresh(1.53) && card.refresh(1.54) && !card.presented(), "Fade completes after .14 s");
    card.show(2);
    check(card.key(0x27, false, 2.3) == app::QuitAnswer::none && card.focused() == 1, "Right focuses Quit");
    check(card.key(0x0D, true, 2.31) == app::QuitAnswer::none, "Repeats are ignored");
    check(card.key(0x09, false, 2.32) == app::QuitAnswer::none && card.focused() == 0, "Tab toggles focus");
    check(card.key(0x09, false, 2.33) == app::QuitAnswer::none && card.focused() == 1, "Tab toggles back");
    check(card.key(0x20, false, 2.4) == app::QuitAnswer::confirm && card.submitted(), "Space confirms the focused Quit");
    check(card.key(0x1B, false, 2.5) == app::QuitAnswer::none && card.presented(), "A confirmed card ignores further input until teardown");
    card.hideImmediately(3);
    card.show(4);
    check(card.pointerDown(1, 4.3) == app::QuitAnswer::none && card.pressed() == 1 && card.focused() == 1, "Press focuses its button");
    check(card.pointerUp(0, 4.4) == app::QuitAnswer::none, "Release outside the pressed button does nothing");
    check(card.pointerDown(0, 4.5) == app::QuitAnswer::none && card.pointerUp(0, 4.6) == app::QuitAnswer::cancel, "Click on Cancel");
    card.hideImmediately(5);
    card.setReduceMotion(true, 5);
    card.show(6);
    check(card.pose(6).opacity == 1 && !card.pose(6).animated, "Reduce Motion presents without animation");
    card.dismiss(true, 6.1);
    check(!card.presented(), "Reduce Motion dismisses immediately");
    const auto art = app::prepareQuitConfirmationArtwork(card, {0, 0, 1280, 800}, card.appearance());
    const auto& children = art.layers["children"].array();
    check(children.size() == 9 && art.surfaces.size() == 9, "Card artwork: plate, line, two plates, title, message, caption, two labels");
    const auto geometry = endfield::modules::SettingsSafety::geometry({0, 0, 1280, 800});
    check(geometry.card.width == 390 && geometry.card.height == 190, "Shared source card geometry");
    check(children[4]["text"]["string"].string() == "Quit EndfieldHUD?" && children[6]["text"]["string"].string() == "ENDFIELDHUD / SYSTEM", "Card text content");
}
} // namespace

int main() {
    try {
        routing();
        isolation();
        quitState();
        std::cout << "Module registry/quit confirmation: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Module registry/quit confirmation failed after " << checks << ": " << e.what() << '\n';
        return 1;
    }
}
