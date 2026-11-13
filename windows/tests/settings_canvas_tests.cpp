// Settings on the redesign (modules/settings_canvas.*) against the Mac oracle
// (tests/fixtures/settings-source.json, produced by tools/settings_reference.sh
// from the unchanged Mac Sources at the pinned source commit): every scripted
// Mac state of the four canvases (System, Display with Battery alert, App
// icon, Modules and HUD elements, Hotkeys, About with Credits; menus open;
// dark / Light; English / Simplified Chinese; a changed module layout; alerts
// off; recording; the update strip; each scoped Restore asked, the System and
// Display ones confirmed) is replayed through the Windows canvases with the
// same controller steps (the pages not ported yet are listed in replay)
// and compared node by node (what each tree draws, the controls, the
// accessibility items, the header) with the Mac's recorded CoreText widths;
// the confirm requests and toasts the pages raise are compared field by
// field. Then the Windows-side contracts the oracle cannot show: Windows
// shortcut recording, the module list's drag and move buttons, menu picks
// that save the Windows keys (windowsDisplayID, batteryAlertsEnabled), the
// layout prompt's 12 s countdown and Keep, the Notice toast rules, the
// update strip's 650 ms steps and every string pair in the catalog.
// The one representation difference: HUDArtwork.triStrip is a cached raster
// on the Mac and three bands here (checked by geometry).
// Portable; synthetic preferences; no file, OS or clock.
#include "modules/settings_canvas.hpp"
#include "modules/hud_design_format.hpp"
#include "tests/hud_design_oracle.hpp"
#include <iostream>
#include <set>

namespace {
namespace d = endfield::modules::design;
namespace m = endfield::modules;
namespace core = endfield::core;
using namespace endfield::tests::design_oracle;
using ehud::data::Settings;
using L = core::Language;
unsigned checks{};
std::string lastCheck;
void check(bool v, const std::string& what) { ++checks; lastCheck = what; if (!v) throw std::runtime_error(what); }

constexpr std::array<core::Module, 4> modules{core::Module::system, core::Module::display, core::Module::hotkeys, core::Module::about};
std::string_view moduleName(core::Module m) {
    return m == core::Module::system ? "system" : m == core::Module::display ? "display" : m == core::Module::hotkeys ? "hotkeys" : "about";
}

// The Settings world of one replay: the context, the controller and the four canvases.
struct World {
    RecordedMeasurer& measurer;
    d::Context context;
    std::unique_ptr<m::SettingsController> controller;
    std::map<core::Module, std::unique_ptr<m::SettingsCanvas>> canvases;
    std::vector<Settings> saved;
    std::vector<bool> capture;
    std::vector<std::string> links, hooks;
    std::optional<std::string> registerError, loginError{"Synthetic login failure"};
    double now{100};
    L language{L::english};
    bool light{};
    World(RecordedMeasurer& recorded, m::SettingsCanvasHooks hooks0, m::SettingsUpdateStatus update) : measurer(recorded), context(recorded, d::asciiUppercase) {
        appearance();
        auto initial = Settings::defaults();
        initial.set("language", "english"); initial.set("theme", "dark"); initial.set("launchAtLogin", false); initial.set("reduceMotion", true);
        m::SettingsCallbacks callbacks;
        callbacks.persist = [this](const Settings& s) { saved.push_back(s); };
        callbacks.launchAtLogin = [this](bool) { return loginError; };
        callbacks.registerShortcut = [this](m::SettingsShortcut) { return registerError; };
        callbacks.shortcutCapture = [this](bool on) { capture.push_back(on); };
        callbacks.openLink = [this](std::string_view url) { links.emplace_back(url); };
        callbacks.checkUpdates = [this] { hooks.emplace_back("checkUpdates"); };
        callbacks.editBatteryPosition = [this] { hooks.emplace_back("editPosition"); };
        hooks0.editOverlay = [this] { hooks.emplace_back("editOverlay"); };
        hooks0.arrangeModules = [this](bool on) { hooks.emplace_back(on ? "arrangeOn" : "arrangeOff"); };
        controller = std::make_unique<m::SettingsController>(initial, callbacks, L::english);
        controller->setUpdateStatus(update);
        hooks0.chooseColor = [this] { hooks.emplace_back("chooseColor"); };
        hooks0.chooseLogo = [this] { hooks.emplace_back("chooseLogo"); };
        hooks0.logoSelection = [this] { hooks.emplace_back("logoSelection"); };
        for (const auto module : modules) canvases[module] = std::make_unique<m::SettingsCanvas>(module, context, *controller, hooks0);
    }
    void appearance(bool reduceMotion = true) {
        d::Context::Appearance a;
        a.language = language; a.accentHex = "FAD41F"; a.light = light; a.reduceMotion = reduceMotion;
        context.apply(a, now);
    }
    m::SettingsCanvas& operator[](core::Module module) { return *canvases.at(module); }
    void show(core::Module module) { auto& c = (*this)[module]; c.show(now); c.setActive(true, now); }
    void hide(core::Module module) { (*this)[module].setActive(false, now); }
    d::Control& control(core::Module module, std::string_view id) {
        for (auto* c : (*this)[module].page().controls()) if (c->id() == id) return *c;
        throw std::runtime_error("No control " + std::string(id) + " on " + std::string((*this)[module].section()) + " after check " + lastCheck);
    }
    void activate(core::Module module, std::string_view id) { auto& c = (*this)[module]; c.env().now = now; control(module, id).activate(); }
    template <class F> void update(F&& mutation) { auto next = controller->configuration(); mutation(next); controller->update(std::move(next), now); }
    void clear() { context.overlay().dismissConfirm(); context.overlay().clearToast(); }
    // As the owner core does: the canvas clock first, then Canvas::wake.
    void wake(core::Module module) { auto& c = (*this)[module]; c.env().now = std::max(c.env().now, now); c.wake(now); }
};

// One recorded Mac page against the Windows canvas in the same state.
void comparePage(World& w, core::Module module, const Json& row, std::vector<std::string>& failures, std::size_t& compared) {
    auto& canvas = w[module];
    const auto label = std::string(moduleName(module)) + "/" + row["state"].string() + "/" + row["language"].string() + (row["light"].boolean() ? "/light" : "");
    ++compared;
    std::vector<std::string> local;
    // The Mac root is centred on its host (frame -295, -255): compare in box px.
    Json expected = row;
    auto layer = std::as_const(expected)["layer"];
    layer["anchorPoint"] = Json::Array{0, 0}; layer["position"] = Json::Array{0, 0};
    expected["layer"] = std::move(layer);
    compare(label, expected, canvas.root(), canvas.root(), canvas.page().controls(), local);
    // HUDArtwork.triStrip is a cached raster on the Mac and three bands here
    // (hud_design_nodes triStrip): checked by geometry in about() instead.
    const auto strip = [](const std::string& f) {
        return f.find("Mac draws artwork (0, 95, 4, 38)") != std::string::npos || f.find("about.hero.strip") != std::string::npos;
    };
    for (auto& f : local) if (!strip(f)) failures.push_back(std::move(f));
    const auto& h = row["header"];
    const auto [title, subtitle] = canvas.header().titleTexts();
    if (title != h["title"].string() || subtitle != h["subtitle"].string()) failures.push_back(label + ": header title '" + title + "' / '" + subtitle + "'");
    if (canvas.header().counterText() != h["counter"].string()) failures.push_back(label + ": counter '" + canvas.header().counterText() + "'");
    std::vector<std::string> crumbs;
    for (const auto& c : h["crumbs"].array()) crumbs.push_back(c.string());
    if (canvas.header().crumbTexts() != crumbs) failures.push_back(label + ": crumbs");
    if (canvas.menuOpen() != row["menuOpen"].boolean()) failures.push_back(label + ": menu open");
}
void compareRequest(World& w, const Json& row, std::vector<std::string>& failures) {
    const auto label = "request " + row["name"].string();
    const auto& expected = row["request"];
    const auto& actual = w.context.overlay().confirmState().request();
    if (expected.isNull() != !actual.has_value()) { failures.push_back(label + ": presence"); return; }
    if (actual) {
        const auto cancel = actual->cancelTitle.empty() ? core::localized("Cancel", "取消", w.language) : actual->cancelTitle;
        if (actual->id != expected["id"].string() || actual->title != expected["title"].string() || actual->message != expected["message"].string()
            || actual->confirmTitle != expected["confirmTitle"].string() || cancel != expected["cancelTitle"].string() || actual->danger != expected["danger"].boolean()
            || d::iconName(actual->glyph()) != expected["icon"].string() || (actual->style == d::ConfirmRequest::Style::alert) != expected["alert"].boolean()
            || actual->cancelPillConfirms != expected["cancelPillConfirms"].boolean()
            || (expected["countdown"].isNull() ? actual->countdown.has_value() : actual->countdown != int(expected["countdown"].number())))
            failures.push_back(label + ": fields of '" + actual->title + "'");
    }
    const auto& toast = row["toast"];
    const auto& current = w.context.overlay().toastState().current();
    if (toast.isNull() != !current.has_value()) { failures.push_back(label + ": toast presence"); return; }
    if (current && (current->title != toast["title"].string() || current->caption != toast["caption"].string() || d::iconName(current->icon) != toast["icon"].string()))
        failures.push_back(label + ": toast '" + current->title + "' / '" + current->caption + "'");
}

// The replay of tools/settings_reference.swift's canvas script, step by step.
void replay(const Json& data, const Json& manifest) {
    RecordedMeasurer measurer;
    recordText(measurer, data["text"]);
    for (const auto& row : data["pages"].array()) if (recordMicroTag(measurer, row["layer"])) break;
    m::SettingsCanvasHooks hooks;
    hooks.displays = [] { return std::vector<m::SettingsDisplay>{{"00000000-0000-4000-8000-000000000010", "Fixture Display"}}; };
    for (const auto& icon : data["icons"].array())
        hooks.icons.push_back({icon["id"].string(), icon["title"].string(), {d::ImageSource::Kind::asset, "application-icons/" + icon["id"].string() + ".png", std::string(64, '0'), 0}});
    const auto& about = data["about"];
    hooks.about = {about["name"].string(), about["version"].string(), about["author"].string(), about["license"].string(), about["repository"].isString() ? about["repository"].string() : ""};
    std::map<std::string, d::Size> logoPixels;
    for (const auto& l : data["logos"].array())
        logoPixels[l["logo"].string() + "|" + RecordedMeasurer::number(l["fitting"].array()[0].number()) + "x" + RecordedMeasurer::number(l["fitting"].array()[1].number())]
            = {l["pixels"].array()[0].number(), l["pixels"].array()[1].number()};
    hooks.logoArtwork = [logoPixels](std::string_view logo, std::string_view, d::Size fitting) -> std::optional<m::SettingsLogoImage> {
        const auto found = logoPixels.find(std::string(logo) + "|" + RecordedMeasurer::number(fitting.width) + "x" + RecordedMeasurer::number(fitting.height));
        if (found == logoPixels.end()) return std::nullopt;
        return m::SettingsLogoImage{{d::ImageSource::Kind::memory, "settings.logo." + std::string(logo), {}, 1}, found->second};
    };
    for (const auto& [key, icon] : manifest["icons"].object())
        if (key.starts_with("module:")) hooks.moduleGlyphs.emplace(key.substr(7), icon["tree"]);
    m::SettingsUpdateStatus update;
    update.title = core::localized("Not checked yet", "尚未检查", L::english);
    update.automaticallyInstalls = true; update.canCheck = true; update.canSetAutomatic = true;
    World w(measurer, hooks, update);

    std::vector<std::string> failures, pendingFailures;
    std::size_t compared{}, page{}, asked{};
    const auto& pages = data["pages"].array();
    const auto& requests = data["requests"].array();
    // The Mac 1.3.1 Settings pages not ported yet: their recorded differences
    // are listed but do not fail until the item named here lands, and each
    // must still differ, so a page that comes to match is taken off the list.
    const std::set<std::string> pendingPages;
    const std::set<std::string> pendingIconTitles;
    std::set<std::string> pendingMatched;
    const auto row = [&](core::Module module, std::string_view state) {
        check(page < pages.size(), "The oracle has a page for " + std::string(state));
        const auto& expected = pages[page++];
        check(expected["module"].string() == moduleName(module) && expected["state"].string() == state, "Replay order: " + std::string(state) + " vs " + expected["state"].string());
        const auto key = std::string(moduleName(module)) + "/" + std::string(state);
        // Each scoped Restore's enabled state, also on a page still pending.
        for (const auto& control : expected["controls"].array()) {
            const auto& id = control["id"].string();
            if (id != "restore" && id != "displayRestore" && id != "overlayRestore") continue;
            const auto& controls = w[module].page().controls();
            const auto found = std::find_if(controls.begin(), controls.end(), [&](const d::Control* c) { return c->id() == id; });
            ++checks;
            if (found == controls.end() || (*found)->enabled() != control["enabled"].boolean()) failures.push_back(key + ": " + id + " enabled state");
        }
        if (!pendingPages.contains(key)) { comparePage(w, module, expected, failures, compared); return; }
        std::vector<std::string> local;
        comparePage(w, module, expected, local, compared);
        if (local.empty()) pendingMatched.insert(key);
        pendingFailures.insert(pendingFailures.end(), local.begin(), local.end());
    };
    const auto ask = [&](std::string_view name) {
        check(asked < requests.size() && requests[asked]["name"].string() == name, "Request order: " + std::string(name));
        compareRequest(w, requests[asked++], failures);
    };
    using M = core::Module;
    // System
    w.show(M::system);
    row(M::system, "main");
    w.activate(M::system, "language"); row(M::system, "menu.language"); w[M::system].closeMenu();
    w.activate(M::system, "screen"); row(M::system, "menu.screen"); w[M::system].closeMenu();
    w.activate(M::system, "restore"); ask("restore"); w.clear();
    w.controller->setStatus("Synthetic status"); ask("notice"); w.controller->setStatus(""); w.clear();
    w.hide(M::system);
    w.language = L::simplifiedChinese; w.appearance(); w.controller->setLanguage(w.language);
    w.show(M::system); row(M::system, "main"); w.hide(M::system);
    w.language = L::english; w.appearance(); w.controller->setLanguage(w.language);
    w.light = true; w.appearance();
    w.show(M::system); row(M::system, "main"); w.hide(M::system);
    w.light = false; w.appearance();
    // System's Restore confirmed (launch at login fails, so it stays off), then
    // at its defaults: the button is disabled.
    w.show(M::system);
    w.activate(M::system, "restore"); (void)w.context.overlay().decide(d::ConfirmState::Decision::confirm); ask("restoreConfirmed");
    row(M::system, "main.restored"); w.clear();
    w.loginError.reset(); w.update([](Settings& s) { s.set("launchAtLogin", true); });
    row(M::system, "main.atDefaults");
    w.update([](Settings& s) { s.set("launchAtLogin", false); s.set("language", "english"); }); w.loginError = "Synthetic login failure";
    w.hide(M::system); w.controller->setStatus(""); w.clear();
    // Display
    w.show(M::display);
    row(M::display, "main");
    (void)w[M::display].scroll({300, 300}, {0, 10000}, w.now);
    row(M::display, "main.bottom");
    w.activate(M::display, "displayRestore"); ask("displayRestore"); w.clear();
    w.activate(M::display, "recording"); ask("recordingOn");
    w.activate(M::display, "recording"); ask("recordingOff"); w.clear();
    (void)w[M::display].scroll({300, 300}, {0, -10000}, w.now);
    for (const auto menu : {"theme", "clockStyle", "centerLogo", "openModule"}) {
        w.activate(M::display, menu); row(M::display, std::string("menu.") + menu); w[M::display].closeMenu();
    }
    w.controller->previewScale(1.5, w.now); ask("layout");
    w.now += 13; w.controller->wake(w.now); ask("layoutReverted"); w.clear();
    w.controller->previewPosition(.2, 0, w.now); ask("position");
    (void)w.context.overlay().decide(d::ConfirmState::Decision::cancel); w.clear();
    w.activate(M::display, "battery"); row(M::display, "battery");
    w.activate(M::display, "method"); row(M::display, "menu.method"); w[M::display].closeMenu();
    w.update([](Settings& s) { s.set("batteryAlertsEnabled", false); });
    row(M::display, "battery.alertsOff");
    w.update([](Settings& s) { s.set("batteryAlertsEnabled", true); });
    { const auto back = w[M::display].page().back; back(); }
    w.activate(M::display, "appIcon"); row(M::display, "icons");
    { const auto back = w[M::display].page().back; back(); }
    // HUDApplicationIcon.title in Simplified Chinese: the tiles the Mac localizes.
    w.language = L::simplifiedChinese; w.appearance();
    w.activate(M::display, "appIcon");
    for (const auto& icon : data["icons"].array()) {
        const bool same = w.control(M::display, "appIcon:" + icon["id"].string()).label == icon["titleZh"].string();
        if (!pendingIconTitles.contains(icon["id"].string())) { if (!same) failures.push_back("icon title " + icon["id"].string()); }
        else if (same) pendingMatched.insert("icon title " + icon["id"].string());
        else pendingFailures.push_back("icon title " + icon["id"].string());
    }
    { const auto back = w[M::display].page().back; back(); }
    w.language = L::english; w.appearance();
    // Display > Modules
    w.activate(M::display, "modules"); row(M::display, "modules");
    w.update([](Settings& s) { auto l = s.moduleLayout(); l.move("clipboard", 0); l.setHidden("archive", true); s.setModuleLayout(l); });
    row(M::display, "modules.changed");
    w.activate(M::display, "modulesReset"); ask("modulesReset"); w.clear();
    w.update([](Settings& s) { s.setModuleLayout({}); });
    { const auto back = w[M::display].page().back; back(); }
    row(M::display, "main.back");
    // Display > HUD elements: its Restore asks, enabled once the page differs from its defaults.
    w.activate(M::display, "overlay"); row(M::display, "overlay");
    w.activate(M::display, "facility");
    check(dynamic_cast<d::Toggle&>(w.control(M::display, "facility")).isOn(), "Turning Show facility alerts off keeps the switch on while it asks");
    ask("facilityOff"); w.clear();
    w.update([](Settings& s) { auto l = s.overlayLayout(); l.showsTitle = false; s.setOverlayLayout(l); });
    row(M::display, "overlay.changed");
    w.activate(M::display, "overlayRestore"); ask("overlayRestore"); w.clear();
    w.update([](Settings& s) { s.setOverlayLayout({}); });
    { const auto back = w[M::display].page().back; back(); }
    // Display's Restore confirmed during a scale preview: the defaults written
    // at once, no countdown, the button disabled.
    w.update([](Settings& s) { s.set("recordingMode", false); s.set("clockStyle", "dial"); });
    w.controller->previewScale(1.5, w.now);
    w.activate(M::display, "displayRestore"); (void)w.context.overlay().decide(d::ConfirmState::Decision::confirm); ask("displayRestoreConfirmed");
    row(M::display, "main.restored"); w.clear();
    w.update([](Settings& s) { s.set("reduceMotion", true); });
    w.hide(M::display); w.controller->setStatus(""); w.clear();
    w.light = true; w.appearance();
    w.show(M::display); row(M::display, "main"); w.hide(M::display);
    w.light = false; w.appearance();
    w.language = L::simplifiedChinese; w.appearance(); w.controller->setLanguage(w.language);
    w.show(M::display); row(M::display, "main"); w.hide(M::display);
    w.language = L::english; w.appearance(); w.controller->setLanguage(w.language);
    // Hotkeys
    w.show(M::hotkeys);
    row(M::hotkeys, "main");
    w.activate(M::hotkeys, "capture"); row(M::hotkeys, "capture");
    w[M::hotkeys].cancelCapture();
    w.hide(M::hotkeys);
    // About
    w.show(M::about);
    row(M::about, "main");
    auto current = update;
    current.phase = m::SettingsUpdatePhase::current; current.latestVersion = "1.3.0"; current.title = core::localized("Up to date", "已是最新版本", L::english);
    current.releaseURL = "https://github.com/DDDuoDuo/EndfieldHUD/releases/tag/v1.3.0";
    w.controller->setUpdateStatus(current);
    row(M::about, "main.current");
    auto failed = current;
    failed.phase = m::SettingsUpdatePhase::failed; failed.detail = "Synthetic network failure"; failed.title = core::localized("Check failed, retry", "检查失败，请重试", L::english);
    w.controller->setUpdateStatus(failed); ask("updateFailed"); w.clear();
    row(M::about, "main.failed");
    w.controller->setUpdateStatus(update);
    w.activate(M::about, "credits"); row(M::about, "credits");
    w.hide(M::about);
    check(page == pages.size() && asked == requests.size(), "Every recorded Mac state replayed");
    std::cerr << "  Not ported yet: " << pendingFailures.size() << " differences on " << pendingPages.size() << " pages\n";
    for (const auto& key : pendingMatched) failures.push_back(key + " now matches the Mac: take it off the pending list");
    for (const auto& f : failures) std::cerr << "  " << f << '\n';
    if (!measurer.misses.empty()) { std::cerr << "  unrecorded widths: " << measurer.misses.size() << '\n'; for (std::size_t i = 0; i < std::min<std::size_t>(measurer.misses.size(), 12); ++i) std::cerr << "    " << measurer.misses[i] << '\n'; }
    check(failures.empty(), std::to_string(failures.size()) + " differences from the Mac in " + std::to_string(compared) + " pages");
    checks += unsigned(compared);
}
}

namespace {
// The Windows-side behaviour the oracle cannot script (Windows chords, menu
// picks that save Windows keys, the modules list's gestures, the prompt's
// countdown and Keep, the toast rules, the update strip's steps).
void contracts() {
    using M = core::Module;
    RecordedMeasurer measurer; // the deterministic .55 em widths
    m::SettingsCanvasHooks hooks;
    hooks.displays = [] { return std::vector<m::SettingsDisplay>{{"DISPLAY-A", "Studio"}, {"DISPLAY-B", "Studio"}}; };
    hooks.icons = {{"endfield", "Endfield", {d::ImageSource::Kind::asset, "application-icons/endfield-app.png", std::string(64, '0'), 0}},
                   {"watch", "Watch", {d::ImageSource::Kind::asset, "application-icons/watch-app.png", std::string(64, '0'), 0}},
                   {"babel", "Babel", {d::ImageSource::Kind::asset, "application-icons/babel-app.png", std::string(64, '0'), 0}}};
    hooks.about.repository = "https://github.com/DDDuoDuo/EndfieldHUD";
    m::SettingsUpdateStatus update;
    update.canCheck = true; update.canSetAutomatic = true; update.releaseURL = "https://github.com/DDDuoDuo/EndfieldHUD/releases";
    World w(measurer, hooks, update);
    auto& overlay = w.context.overlay();
    const auto toastTitle = [&] { return overlay.toastState().current() ? overlay.toastState().current()->title : std::string(); };
    const auto toastCaption = [&] { return overlay.toastState().current() ? overlay.toastState().current()->caption : std::string(); };
    const auto committed = [&] { return w.controller->committed(); };
    const auto accessibilityValue = [&](M module, std::string_view id) {
        std::vector<d::AccessibilityItem> items; w[module].accessibilityItems(items);
        for (const auto& i : items) if (i.id == id) return i.value;
        return std::string("<none>");
    };

    // --- Hotkeys: recording a Windows chord (HUDSettingsCanvas.capture) -------
    w.show(M::hotkeys);
    check(w.control(M::hotkeys, "hotkeyEnabled").label == "Open with hotkey", "Open with hotkey is the Mac row (the Windows-only Summon HUD with hotkey row is gone)");
    {
        std::vector<std::string> rows;
        for (auto* c : w[M::hotkeys].page().controls()) if (c->id().starts_with("row:")) rows.push_back(c->id());
        check(rows == std::vector<std::string>{"row:capture", "row:hotkeyEnabled", "row:keyReset"}, "Open with hotkey is the second row");
    }
    check(m::SettingsCanvas::keycaps({}) == std::vector<std::string>{"CTRL", "`"} && m::SettingsCanvas::keycaps({0x4B, 1 | 4 | 8}) == std::vector<std::string>{"ALT", "SHIFT", "WIN", "K"},
        "Keycaps name the Windows modifiers in order, then the key");
    w.activate(M::hotkeys, "capture");
    check(w.controller->capturingShortcut() && w[M::hotkeys].capturesKeys() && w[M::hotkeys].inputLocked() && w.capture == std::vector<bool>{true},
        "The keycap starts recording: keys belong to Settings, input is locked, the host pauses the hotkey");
    d::KeyInput key; key.virtualKey = d::key::control; key.control = true;
    check(w[M::hotkeys].keyDown(key, w.now) && w.controller->capturingShortcut() && !overlay.toastState().current(), "A modifier alone waits for the key");
    key = {}; key.virtualKey = 'K';
    check(w[M::hotkeys].keyDown(key, w.now) && w.controller->capturingShortcut() && toastTitle() == "Hotkey unavailable" && !toastCaption().empty(),
        "A key without a modifier is refused with its reason");
    overlay.clearToast();
    key.repeat = true; key.control = true;
    check(w[M::hotkeys].keyDown(key, w.now) && w.controller->capturingShortcut() && !overlay.toastState().current(), "A repeat is ignored");
    w.registerError = "Synthetic conflict";
    key.repeat = false; key.alt = true;
    check(w[M::hotkeys].keyDown(key, w.now) && w.controller->capturingShortcut() && toastTitle() == "Hotkey unavailable" && toastCaption() == "Synthetic conflict",
        "A chord the system refuses toasts its error and keeps recording");
    w.registerError.reset();
    check(w[M::hotkeys].keyDown(key, w.now) && !w.controller->capturingShortcut() && m::settingsShortcut(committed()) == m::SettingsShortcut{'K', 3}
        && toastTitle() == "Hotkey saved" && toastCaption() == "CTRL + ALT + K" && w.capture == std::vector<bool>{true, false}, "A modifier plus a key saves and toasts the keys");
    w.activate(M::hotkeys, "capture");
    key = {}; key.virtualKey = d::key::escape;
    overlay.clearToast();
    check(w[M::hotkeys].keyDown(key, w.now) && !w.controller->capturingShortcut() && !overlay.toastState().current(), "Esc ends recording without a toast");
    w.activate(M::hotkeys, "keyReset");
    check(m::settingsShortcut(committed()) == m::SettingsShortcut{} && toastTitle() == "Hotkey restored", "Restore default hotkey");
    w.activate(M::hotkeys, "hotkeyEnabled");
    check(!committed().summonHotkeyEnabled() && committed().fields["summonHotkeyEnabled"] == Json(false), "Open with hotkey off saves summonHotkeyEnabled");
    w.activate(M::hotkeys, "hotkeyEnabled");
    check(committed().summonHotkeyEnabled(), "and on again");
    w.activate(M::hotkeys, "capture");
    w.hide(M::hotkeys);
    check(!w.controller->capturingShortcut(), "Leaving Hotkeys ends a recording");
    w.clear();

    // --- System: display menu, restore -----------------------------------------
    w.show(M::system);
    check(w.control(M::system, "screen").label == "Display" && accessibilityValue(M::system, "screen") == "Pointer display", "The display field reads the pointer display");
    w.activate(M::system, "screen");
    check(w[M::system].menuOpen(), "The display menu opens");
    check(w[M::system].performAccessibilityAction("screen:DISPLAY-B", w.now) && committed().string("windowsDisplayID") == "DISPLAY-B"
        && committed().string("windowsDisplayName") == "Studio" && committed().boolean("openOnActiveDisplay"), "Picking a monitor saves the Windows display id and name");
    check(accessibilityValue(M::system, "screen") == "Studio", "The field shows the chosen monitor");
    w.activate(M::system, "screen");
    std::vector<std::string> titles;
    for (auto* c : w[M::system].page().controls()) (void)c;
    {
        std::vector<d::AccessibilityItem> items; w[M::system].accessibilityItems(items);
        for (const auto& i : items) if (i.role == d::AccessibilityRole::menuItem) titles.push_back(i.label);
    }
    check(titles == std::vector<std::string>{"Pointer display", "Main display", "Studio / 1", "Studio / 2"}, "Duplicate monitor names are numbered with ' / n'");
    check(w[M::system].performAccessibilityAction("screen:main", w.now) && committed().fields["windowsDisplayID"].isNull() && !committed().boolean("openOnActiveDisplay"),
        "Main display clears the saved monitor");
    w.update([](Settings& s) { s.set("windowsDisplayID", "GONE"); s.set("windowsDisplayName", "Old Display"); });
    check(accessibilityValue(M::system, "screen") == "Old Display (disconnected)", "A saved monitor that is gone reads as disconnected");
    w.activate(M::system, "screen");
    {
        std::vector<d::AccessibilityItem> items; w[M::system].accessibilityItems(items);
        const auto gone = std::find_if(items.begin(), items.end(), [](const auto& i) { return i.id == "screen:GONE"; });
        check(gone != items.end() && !gone->enabled && gone->on, "It is listed after the monitors, selected and disabled");
    }
    w[M::system].closeMenu();
    w.hide(M::system);
    // --- Display > Modules (HUDSettingsCanvas.buildModules, under Display) -----
    w.show(M::display);
    check(w.control(M::display, "modules").label == "Modules, 17 / 17", "Display's Modules row speaks the shown / total count");
    w.activate(M::display, "modules");
    check(w[M::display].section() == "modules", "Modules opens");
    auto& notes = w.control(M::display, "up:notes");
    check(!notes.enabled() && w.control(M::display, "down:power").enabled() == false, "The first row cannot move up, the last cannot move down");
    w.activate(M::display, "down:notes");
    check(committed().moduleLayout().order[1] == "notes" && committed().fields["hudModuleOrder"].isArray(), "Move down saves the order");
    const auto rowOf = [&](std::string_view module) -> d::Layer& { return w.control(M::display, "row:module:" + std::string(module)).layer(); };
    check(std::abs(rowOf("notes").frame().y + rowOf("notes").value(d::Channel::translateY) - 56) < 1e-9 && std::abs(rowOf("fileShelf").frame().y + rowOf("fileShelf").value(d::Channel::translateY)) < 1e-9,
        "The rows take their new slots (56 px apart)");
    w.activate(M::display, "up:notes");
    check(committed().moduleLayout().isDefault() && !committed().fields.contains("hudModuleOrder"), "Back to the default order removes the key");
    // Drag notes by its grip three rows down (the pointer in box px: the list starts one row pitch below the header, at
    // y 81 + 56 under Arrange on HUD; a row's grip centre at x 26, y 27).
    auto& canvas = w[M::display];
    check(canvas.cursor(d::Point{26, 137 + 27}) == d::Cursor::openHand && !canvas.cursor(d::Point{300, 137 + 27}), "The open hand shows over a grip only");
    d::PointerInput press; press.point = {26, 164};
    check(canvas.pointerDown({26, 164}, press, w.now), "The grip takes the press");
    canvas.pointerDragged({26, 168}, w.now);
    check(canvas.cursor(d::Point{26, 168}) == d::Cursor::closedHand, "The closed hand shows while dragging");
    canvas.pointerDragged({26, 164 + 3 * 56}, w.now);
    check(std::abs(rowOf("notes").frame().y + rowOf("notes").value(d::Channel::translateY) - 3 * 56) < 1e-9 && rowOf("notes").zPosition() == 10,
        "The dragged row follows the pointer above the others");
    check(std::abs(rowOf("archive").frame().y + rowOf("archive").value(d::Channel::translateY) - 2 * 56) < 1e-9, "The others reflow to the preview order");
    canvas.pointerUp({26, 164 + 3 * 56}, w.now);
    check(committed().moduleLayout().order[3] == "notes" && rowOf("notes").zPosition() == 0, "Dropping saves the slot");
    {
        // Esc or closing the HUD while a row is held (Mac b7a5443): the drag ends
        // inside cancelGestures, so the row goes back and the half-finished order is not saved.
        const auto savedOrder = committed().moduleLayout().order;
        const auto hold = [&] {
            const auto r = w.control(M::display, "grip:notes").rect(canvas.root());
            const d::Point grip{r.x + r.width / 2, r.y + r.height / 2};
            d::PointerInput down; down.point = grip;
            check(canvas.pointerDown(grip, down, w.now), "The grip takes the press");
            canvas.pointerDragged({grip.x, grip.y + 4}, w.now);
            canvas.pointerDragged({grip.x, grip.y - 2 * 56}, w.now);
            check(rowOf("notes").zPosition() == 10, "The notes row is held");
            return grip;
        };
        const auto saves = w.saved.size();
        auto grip = hold();
        d::KeyInput escape; escape.virtualKey = d::key::escape;
        check(canvas.keyDown(escape, w.now) && canvas.section() == "main", "Esc leaves Modules mid-drag");
        canvas.pointerUp({grip.x, grip.y - 2 * 56}, w.now);
        check(w.saved.size() == saves && committed().moduleLayout().order == savedOrder, "Esc mid-drag does not save the half-finished order");
        w.activate(M::display, "modules");
        grip = hold();
        w.hide(M::display);
        canvas.pointerUp({grip.x, grip.y - 2 * 56}, w.now);
        check(w.saved.size() == saves && committed().moduleLayout().order == savedOrder, "Closing the HUD mid-drag does not save the half-finished order");
        w.show(M::display);
        if (canvas.section() != "modules") w.activate(M::display, "modules");
        check(std::abs(rowOf("notes").frame().y + rowOf("notes").value(d::Channel::translateY) - 3 * 56) < 1e-9 && rowOf("notes").zPosition() == 0,
            "The row is back in its saved slot");
    }
    // At least one module stays visible.
    w.update([](Settings& s) { auto l = s.moduleLayout(); for (const auto& name : ehud::data::ModuleLayout::configurable) if (name != "power") l.setHidden(name, true); s.setModuleLayout(l); });
    check(!w.control(M::display, "show:power").enabled() && w.control(M::display, "show:notes").enabled(), "The last visible module's switch is disabled");
    check(w.control(M::display, "modulesReset").enabled(), "Restore default layout is enabled while the layout differs");
    w.activate(M::display, "modulesReset");
    check(overlay.decide(d::ConfirmState::Decision::confirm), "The reset asks first");
    (void)w.context.advance(w.now + 1);
    check(committed().moduleLayout().isDefault() && toastTitle() == "Layout restored" && !w.control(M::display, "modulesReset").enabled(), "Confirmed: the default layout, a toast, the button disabled");
    {
        // The crumbs and code (DISPLAY / MODULES, SYS-02) are compared with the Mac's in replay.
        const auto crumbs = w[M::display].header().crumbTexts();
        check(std::find(crumbs.begin(), crumbs.end(), "DISPLAY") < std::find(crumbs.begin(), crumbs.end(), "MODULES") && std::find(crumbs.begin(), crumbs.end(), "SYSTEM") == crumbs.end(),
            "Modules is a Display page");
    }
    auto& arrange = dynamic_cast<d::Toggle&>(w.control(M::display, "modulesArrange"));
    check(arrange.label == "Arrange on HUD, Drag the module buttons on the right" && !arrange.isOn() && !w[M::display].arrangingModules(),
        "Arrange on HUD sits above the list, off, its hint spoken with the switch");
    w.activate(M::display, "modulesArrange");
    check(arrange.isOn() && w[M::display].arrangingModules() && w.hooks.back() == "arrangeOn", "The switch turns arranging on and tells the HUD");
    { const auto saves = w.saved.size(), heard = w.hooks.size(); w.activate(M::display, "modulesArrange"); w.activate(M::display, "modulesArrange");
      check(w.saved.size() == saves && w.hooks.size() == heard + 2, "Arrange on HUD is never saved; the HUD hears each change"); }
    {
        // A drag on the HUD previews its order: the rows follow it, and nil returns them to the saved order.
        auto preview = committed().moduleLayout().order;
        std::rotate(preview.begin(), preview.begin() + 1, preview.begin() + 3); // fileShelf, clipboard, notes
        w[M::display].previewModuleOrder(preview);
        (void)w.context.advance(w.now + 1);
        check(std::abs(rowOf("notes").frame().y + rowOf("notes").value(d::Channel::translateY) - 2 * 56) < 1e-9 &&
              std::abs(rowOf("fileShelf").frame().y + rowOf("fileShelf").value(d::Channel::translateY)) < 1e-9, "The rows follow the HUD's preview");
        w.update([](Settings& s) { s.set("blurAmount", .4); });
        (void)w.context.advance(w.now + 2);
        check(std::abs(rowOf("notes").frame().y + rowOf("notes").value(d::Channel::translateY) - 2 * 56) < 1e-9, "A record applied meanwhile keeps the preview");
        w[M::display].previewModuleOrder(std::nullopt);
        (void)w.context.advance(w.now + 3);
        check(std::abs(rowOf("notes").frame().y + rowOf("notes").value(d::Channel::translateY)) < 1e-9, "Without a preview the rows return to the saved order");
        w[M::display].previewModuleOrder(preview);
    }
    { const auto back = w[M::display].page().back; back(); }
    check(w[M::display].section() == "main" && !w[M::display].arrangingModules() && w.hooks.back() == "arrangeOff", "Back returns to Display and turns arranging off");
    w.activate(M::display, "modules");
    check(std::abs(rowOf("notes").frame().y + rowOf("notes").value(d::Channel::translateY)) < 1e-9, "Arranging off dropped the preview");
    w.activate(M::display, "modulesArrange");
    w.hide(M::display);
    check(!w[M::display].arrangingModules() && w.hooks.back() == "arrangeOff", "Leaving the HUD turns arranging off");
    w.show(M::system);
    check(std::none_of(w[M::system].page().controls().begin(), w[M::system].page().controls().end(), [](const d::Control* c) { return c->id() == "modules"; }),
        "System no longer lists Modules");
    w.activate(M::system, "restore");
    check(overlay.confirmState().request()->id == "restore" && overlay.decide(d::ConfirmState::Decision::confirm), "Restore default settings asks");
    (void)w.context.advance(w.now + 1);
    check(toastTitle() == "Settings restored" && committed().fields["windowsDisplayID"].isNull(), "Confirmed: defaults and a toast");
    w.clear();
    // Notice toast: a new status in the same language only.
    w.controller->setStatus("First problem");
    check(toastTitle() == "Notice" && toastCaption() == "First problem", "A new status toasts a Notice");
    w.clear();
    w.controller->setStatus("First problem");
    check(!overlay.toastState().current(), "The same status does not toast again");
    w.language = L::simplifiedChinese; w.appearance(); w.controller->setLanguage(w.language);
    check(!overlay.toastState().current(), "A language change does not toast the standing status");
    w.controller->setStatus("第二个问题");
    check(toastTitle() == "提示" && toastCaption() == "第二个问题", "A new status in the new language toasts");
    w.clear();
    w.language = L::english; w.appearance(); w.controller->setLanguage(w.language); w.controller->setStatus("");
    w.clear();
    w.hide(M::system);

    // --- Display: layout prompt, facility alerts, logo, colours, battery -----
    w.show(M::display);
    auto& slider = dynamic_cast<d::Slider&>(w.control(M::display, "uiScale"));
    slider.setValue(1.5); slider.onChange(1.5, false);
    check(!w.controller->scalePending(), "Dragging the UI scale only stages it");
    slider.onChange(1.5, true);
    check(w.controller->scalePending() && overlay.confirmState().request() && overlay.confirmState().request()->countdown == 12, "Releasing it previews and asks with 12 s");
    const auto wake = w[M::display].nextWakeTime(w.now);
    check(wake && std::abs(*wake - (w.now + 1)) < 1e-9, "The next wake is the next second");
    w.now += 1;
    w.wake(M::display);
    check(overlay.confirmState().request()->countdown == 11 && *w[M::display].nextWakeTime(w.now) > w.now, "The countdown follows the controller's seconds");
    check(overlay.decide(d::ConfirmState::Decision::confirm), "Keep");
    (void)w.context.advance(w.now + 1);
    check(!w.controller->scalePending() && committed().number("hudScale") == 1.5 && toastTitle() == "UI scale kept" && toastCaption() == "1.50×", "Kept: saved with a toast of the new scale");
    w.clear();
    w.controller->previewScale(1.2, w.now);
    w.hide(M::display);
    check(!w.controller->scalePending(), "Leaving Display with the prompt open reverts the preview");
    w.clear();
    w.show(M::display);
    w.controller->previewScale(1.2, w.now);
    w.now += 12; w.wake(M::display);
    check(!overlay.confirmState().request() && toastTitle() == "UI scale reverted" && toastCaption() == "1.50×", "The deadline closes the bar and toasts once");
    w.clear();
    w.controller->wake(w.now + 1);
    check(!overlay.toastState().current(), "The controller's restore status is not toasted again");
    check(std::none_of(w[M::display].page().controls().begin(), w[M::display].page().controls().end(), [](const d::Control* c) { return c->id() == "facility"; }),
        "Facility alerts is no longer a Display row");
    // --- Display > HUD elements (buildOverlay) ---------------------------------
    w.activate(M::display, "overlay");
    check(w[M::display].section() == "overlay", "HUD elements opens from Display");
    {
        std::vector<std::string> ids;
        for (auto* c : w[M::display].page().controls()) if (dynamic_cast<d::RowPlate*>(c)) ids.push_back(c->id());
        check(ids == std::vector<std::string>{"row:overlayTitle", "row:overlayTitleText", "row:overlaySubtitleText", "row:overlaySanity", "row:facility", "overlayEdit", "row:overlayRestore"}
            && w[M::display].header().counterText() == "07", "HUD elements: seven rows in the Mac order, counter 07");
    }
    check(!w.control(M::display, "overlayRestore").enabled(), "Its Restore is disabled at the defaults");
    w.activate(M::display, "overlayTitle");
    check(!committed().overlayLayout().showsTitle && committed().fields["hudOverlayLayout"]["showsTitle"] == Json(false) && w.control(M::display, "overlayRestore").enabled(),
        "Show title off saves hudOverlayLayout.showsTitle and enables Restore");
    w.activate(M::display, "overlaySanity");
    check(!committed().overlayLayout().showsSanity && committed().fields["hudOverlayLayout"]["showsSanity"] == Json(false), "Show Sanity off saves hudOverlayLayout.showsSanity");
    w.activate(M::display, "overlayTitle"); w.activate(M::display, "overlaySanity");
    check(!committed().fields.contains("hudOverlayLayout") && !w.control(M::display, "overlayRestore").enabled(), "Both on again: the layout key is removed");
    // The text fields: 220 x 30, at most 24 characters, the default text as
    // the placeholder; a commit is cleaned and the field shows what was kept.
    {
        struct Host final : d::TextEditorHost {
            std::string typed;
            bool begin(const d::TextEditRequest&) override { return true; }
            void end(d::TextField&) override {}
            std::string text(const d::TextField&) const override { return typed; }
            std::optional<std::pair<std::uint32_t, std::uint32_t>> selection(const d::TextField&) const override { return std::nullopt; }
            bool hasMarkedText(const d::TextField&) const override { return false; }
            void setText(d::TextField&, std::string_view) override {}
            void reproject(d::TextField&, d::Rect) override {}
        } host;
        w.context.setEditorHost(&host);
        auto& field = dynamic_cast<d::TextField&>(w.control(M::display, "overlayTitleText"));
        check(field.layer().bounds().width == 220 && field.layer().bounds().height == 30 && field.options().maximumLength == std::optional<std::size_t>(24)
            && field.idleText() == "ENDFIELDHUD", "Title text: 220 x 30, 24 characters, ENDFIELDHUD as the placeholder");
        {
            std::vector<d::AccessibilityItem> items; w[M::display].accessibilityItems(items);
            check(std::count_if(items.begin(), items.end(), [](const auto& i) { return i.id == "overlayTitleText"; }) == 1, "The field is listed once, in its row");
        }
        field.beginEditing(); host.typed = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"; field.endEditing(true);
        check(committed().overlayLayout().titleText == "ABCDEFGHIJKLMNOPQRSTUVWX" && field.text() == "ABCDEFGHIJKLMNOPQRSTUVWX", "A commit keeps 24 characters");
        auto& subtitle = dynamic_cast<d::TextField&>(w.control(M::display, "overlaySubtitleText"));
        check(subtitle.idleText() == "SYSTEM INTERFACE", "Subtitle text shows SYSTEM INTERFACE as its placeholder");
        // The one-line field already joined pasted lines with spaces (joinedLines).
        subtitle.beginEditing(); host.typed = "  Rhodes Island  "; subtitle.endEditing(true);
        check(committed().overlayLayout().subtitleText == "Rhodes Island" && subtitle.text() == "Rhodes Island",
            "Cleaned: spaces trimmed, and the field shows the cleaned text");
        subtitle.beginEditing(); host.typed = "   "; subtitle.endEditing(true);
        check(committed().overlayLayout().subtitleText.empty() && subtitle.idleText() == "SYSTEM INTERFACE", "An empty field brings the default back");
        field.beginEditing();
        w.update([](Settings& s) { auto l = s.overlayLayout(); l.titleText = "Elsewhere"; s.setOverlayLayout(l); });
        check(field.editing() && field.text() == "ABCDEFGHIJKLMNOPQRSTUVWX", "A field being edited is not refreshed under the typing");
        host.typed = "Typed"; field.endEditing(true);
        check(committed().overlayLayout().titleText == "Typed", "Its commit saves what was typed");
        const std::string multilingualTitle = "\xe7\xbb\x88\xe6\x9c\xab \xe3\x81\xae \xed\x95\x9c";
        field.beginEditing(); host.typed = multilingualTitle; field.endEditing(true);
        check(committed().overlayLayout().titleText == multilingualTitle && field.text() == multilingualTitle,
            "A custom title preserves Chinese, Japanese and Korean text in the field and settings");
        w.update([](Settings& s) { auto l = s.overlayLayout(); l.titleText = "Talos"; s.setOverlayLayout(l); });
        check(field.text() == "Talos", "An idle field shows the saved text");
        // A longer saved text (no character rule: as a hand-edited file keeps
        // it) reads as the Mac decodes it, cut to 24, and is not written back.
        w.update([](Settings& s) { auto l = s.overlayLayout(); l.titleText = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123"; s.setOverlayLayout(l); });
        check(field.text() == "ABCDEFGHIJKLMNOPQRSTUVWX" && committed().overlayLayout().titleText == "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123",
            "An idle field shows a longer saved text cut to 24 characters, and the store keeps it");
        { const auto back = w[M::display].page().back; back(); }
        w.activate(M::display, "overlay");
        check(dynamic_cast<d::TextField&>(w.control(M::display, "overlayTitleText")).text() == "ABCDEFGHIJKLMNOPQRSTUVWX",
            "The rebuilt page starts the field with the text cut to 24 characters");
        w.update([](Settings& s) { auto l = s.overlayLayout(); l.titleText.clear(); s.setOverlayLayout(l); });
        w.context.setEditorHost(nullptr);
    }
    w.activate(M::display, "overlayEdit");
    check(w.hooks.back() == "editOverlay", "Edit position and size calls onEditOverlay");
    w.activate(M::display, "facility");
    check(overlay.confirmState().request()->cancelPillConfirms && overlay.decide(d::ConfirmState::Decision::cancel) && committed().facilityAlerts(), "Esc / outside keeps facility alerts on");
    w.activate(M::display, "facility");
    check(overlay.decide(d::ConfirmState::Decision::confirm), "Either pill disables them");
    (void)w.context.advance(w.now + 1);
    check(!committed().facilityAlerts() && w.control(M::display, "overlayRestore").enabled(), "Facility alerts are off, and HUD elements differs from its defaults");
    w.activate(M::display, "facility");
    check(committed().facilityAlerts() && !overlay.confirmState().isOpen(), "Turning them on needs no confirmation");
    w.clear();
    { const auto back = w[M::display].page().back; back(); }
    check(w[M::display].section() == "main", "Back returns to Display");
    w.activate(M::display, "customColor");
    check(w.hooks.back() == "chooseColor", "The custom swatch opens the colour wheel");
    w[M::display].setCustomColor({1, .5, 0});
    check(committed().string("accentHex") == "FF8000", "The wheel's colour is saved as hex");
    w.activate(M::display, "centerLogo");
    std::size_t tiles{}, rows{};
    {
        std::vector<d::AccessibilityItem> items; w[M::display].accessibilityItems(items);
        for (const auto& i : items) if (i.role == d::AccessibilityRole::menuItem) { ++rows; if (i.id != "logo:import") ++tiles; }
    }
    check(rows == 5 && tiles == 4, "The logo menu: four preset tiles, then Choose custom logo");
    check(w[M::display].performAccessibilityAction("logo:babel", w.now) && committed().string("centerLogo") == "babel" && w.hooks.back() == "logoSelection", "A preset is chosen (an import in flight is dropped)");
    w.activate(M::display, "centerLogo");
    check(w[M::display].performAccessibilityAction("logo:import", w.now) && w.hooks.back() == "chooseLogo", "Choose custom logo opens the picker");
    w[M::display].setCustomLogo("8C7E1C9E-3D0A-4E44-9B0F-3D3D0A0A0A0A");
    check(committed().string("centerLogo") == "custom" && committed().string("centerLogoRevision") == "8C7E1C9E-3D0A-4E44-9B0F-3D3D0A0A0A0A", "An import selects the custom logo");
    w.activate(M::display, "centerLogo");
    {
        std::vector<d::AccessibilityItem> items; w[M::display].accessibilityItems(items);
        check(std::none_of(items.begin(), items.end(), [](const auto& i) { return i.id == "logo:custom"; }), "An imported logo never gets a tile");
    }
    w[M::display].closeMenu();
    w[M::display].showImportError("Synthetic decode failure");
    check(toastTitle() == "Import failed" && toastCaption() == "Synthetic decode failure", "An import failure toasts");
    w.clear();
    (void)w[M::display].scroll({300, 300}, {0, 240}, w.now);
    const double scrolled = w[M::display].page().scrollAreas()[0]->offset();
    check(scrolled > 0, "The Display list scrolls");
    w.activate(M::display, "battery");
    { const auto back = w[M::display].page().back; back(); }
    check(w[M::display].page().scrollAreas()[0]->offset() == 0, "Back returns the Display list to the top");
    w.activate(M::display, "battery");
    w.update([](Settings& s) { s.set("batteryAlertsEnabled", false); });
    check(accessibilityValue(M::display, "method") == "Don't display", "Alerts switched off read as Don't display");
    w.activate(M::display, "method");
    check(w[M::display].performAccessibilityAction("method:never", w.now) && !committed().boolean("batteryAlertsEnabled") && !overlay.toastState().current(),
        "Choosing the shown value does nothing");
    w.activate(M::display, "method");
    check(w[M::display].performAccessibilityAction("method:always", w.now) && committed().boolean("batteryAlertsEnabled") && committed().string("displayMode") == "always",
        "Any method switches the alerts back on");
    w.activate(M::display, "method");
    check(w[M::display].performAccessibilityAction("method:never", w.now) && committed().string("displayMode") == "never" && toastTitle() == "Bar hidden", "Don't display toasts Bar hidden");
    w.activate(M::display, "metric");
    check(w[M::display].performAccessibilityAction("metric:gpu", w.now) && committed().string("alertMetric") == "gpu", "The GPU reading is offered");
    {
        std::vector<std::string> ids;
        for (auto* c : w[M::display].page().controls()) if (dynamic_cast<d::RowPlate*>(c)) ids.push_back(c->id());
        check(ids == std::vector<std::string>{"row:metric", "row:method", "row:duration", "row:placement", "editPosition"} && w[M::display].header().counterText() == "05"
            && w.control(M::display, "editPosition").label == "Edit position and size", "Battery alert: five rows, no Size slider, counter 05");
    }
    w.activate(M::display, "editPosition");
    check(w.hooks.back() == "editPosition", "Edit position and size hands over to the Power area");
    w.hide(M::display);
    check(w[M::display].section() == "battery", "Battery alert survives leaving Display");
    // MODS-638: a new HUD session (the Mac's new HUDSettingsCanvas, :19)
    // opens the main page with its list at the top (Canvas::resetSession).
    w[M::display].resetSession();
    check(w[M::display].section() == "main" && w[M::display].page().scrollAreas()[0]->offset() == 0, "A new HUD session opens Display on its main page at the top");
    w.show(M::display);
    (void)w[M::display].scroll({300, 300}, {0, 240}, w.now);
    check(w[M::display].page().scrollAreas()[0]->offset() > 0, "The Display list scrolls again");
    w.hide(M::display);
    w[M::display].resetSession();
    check(w[M::display].page().scrollAreas()[0]->offset() == 0, "A new HUD session builds a scrolled page at the top");
    w.show(M::display);
    // HUDApplicationIcon(savedValue:): a 1.3.0 "battery" reads as the watch,
    // so the picker selects the watch tile.
    w.update([](Settings& s) { s.set("applicationIcon", "battery"); });
    w.activate(M::display, "appIcon");
    {
        std::vector<d::AccessibilityItem> items; w[M::display].accessibilityItems(items);
        const auto on = [&](std::string_view id) {
            const auto found = std::find_if(items.begin(), items.end(), [&](const auto& i) { return i.id == id; });
            return found != items.end() && found->on;
        };
        check(on("appIcon:watch") && !on("appIcon:endfield") && !on("appIcon:babel"), "A saved battery icon selects the watch tile");
    }
    w.activate(M::display, "appIcon:babel");
    check(committed().string("applicationIcon") == "babel", "An app icon tile picks the icon");
    w.hide(M::display);
    check(w[M::display].section() == "main", "Other sub-pages return to the main page when Display is left");
    // Display > Open on (openModuleRow): Last opened, then every module the
    // navigation opens by its title (no Add App); a pick saves
    // hudOverlayLayout.openModule and the current choice writes nothing.
    w.show(M::display);
    check(accessibilityValue(M::display, "openModule") == "Last opened", "Open on reads Last opened by default");
    w.activate(M::display, "openModule");
    {
        std::vector<d::AccessibilityItem> items; w[M::display].accessibilityItems(items);
        std::vector<std::string> ids, labels;
        for (const auto& i : items) if (i.id.starts_with("openModule:")) { ids.push_back(i.id.substr(11)); labels.push_back(i.label); }
        std::vector<std::string> expected{"last"};
        for (const auto module : ehud::data::ModuleLayout::openable) expected.emplace_back(module);
        check(ids == expected && labels.front() == "Last opened" && labels[1] == "System" && labels[7] == "Notes", "Open on lists Last opened, then every openable module by its title");
        check(std::find(ids.begin(), ids.end(), "addApp") == ids.end(), "Add App is never a choice");
        const auto last = std::find_if(items.begin(), items.end(), [](const auto& i) { return i.id == "openModule:last"; });
        check(last != items.end() && last->on, "Last opened is selected while nothing is chosen");
    }
    {
        const auto saves = w.saved.size();
        check(w[M::display].performAccessibilityAction("openModule:last", w.now) && w.saved.size() == saves && !committed().fields.contains("hudOverlayLayout"),
            "Choosing the current Last opened writes nothing");
    }
    w.activate(M::display, "openModule");
    check(w[M::display].performAccessibilityAction("openModule:calendar", w.now) && committed().overlayLayout().openModule == std::optional<std::string>("calendar")
        && committed().fields["hudOverlayLayout"]["openModule"].string() == "calendar" && accessibilityValue(M::display, "openModule") == "Calendar",
        "A module pick saves hudOverlayLayout.openModule and the field shows its title");
    {
        const auto saves = w.saved.size();
        w.activate(M::display, "openModule");
        check(w[M::display].performAccessibilityAction("openModule:calendar", w.now) && w.saved.size() == saves, "Choosing the current module writes nothing");
    }
    w.activate(M::display, "openModule");
    check(w[M::display].performAccessibilityAction("openModule:last", w.now) && !committed().overlayLayout().openModule && !committed().fields.contains("hudOverlayLayout"),
        "Last opened clears the choice (the layout at its defaults is removed)");
    w.hide(M::display);
    // Display's Restore (restoreRow(.display), the last Display row): enabled
    // while Display differs from its defaults; it drops a pending scale
    // preview (no countdown, the default written at once), clears a staged
    // slider value and leaves System and the hotkey alone.
    w.show(M::display);
    // Every row plate, nav rows included; the header counter is the page's
    // row count (HUDFormat.pad2(rows.count)).
    const auto rowIds = [&](M module) {
        std::vector<std::string> ids;
        for (auto* c : w[module].page().controls()) if (dynamic_cast<d::RowPlate*>(c)) ids.push_back(c->id());
        return ids;
    };
    {
        const auto ids = rowIds(M::display);
        check(!ids.empty() && ids.back() == "row:displayRestore" && w.control(M::display, "row:displayRestore").label == "Restore default settings",
            "Restore default settings is the last Display row");
        check(w[M::display].header().counterText() == d::format::pad2(std::int64_t(ids.size())), "Display's counter counts its rows, Restore included");
    }
    check(w.control(M::display, "displayRestore").enabled(), "Display's Restore is enabled while Display differs from its defaults");
    const bool focusDefault = Settings::defaults().boolean("closeOnFocusLost");
    w.update([&](Settings& s) { s.set("closeOnFocusLost", !focusDefault); });
    const auto hotkey = committed().fields["windowsSummonShortcut"];
    dynamic_cast<d::Slider&>(w.control(M::display, "uiScale")).onChange(1.25, true);
    check(w.controller->scalePending() && overlay.confirmState().request()->id == "layout", "A scale preview asks to keep it");
    auto& staged = dynamic_cast<d::Slider&>(w.control(M::display, "uiScale"));
    staged.onChange(1.7, false);
    w.activate(M::display, "displayRestore");
    check(overlay.confirmState().request()->id == "displayRestore" && overlay.confirmState().request()->title == "Restore default settings?"
        && overlay.confirmState().request()->message == "Only Display and its pages are reset. System, the hotkey, notes, files and shortcuts are kept"
        && overlay.decide(d::ConfirmState::Decision::confirm), "Display's Restore asks with its own message");
    (void)w.context.advance(w.now + 1);
    check(!w.controller->scalePending() && !w.controller->confirmationRemaining(w.now) && !overlay.confirmState().isOpen()
        && committed().number("hudScale") == 1 && w.saved.back().number("hudScale") == 1 && toastTitle() == "Settings restored",
        "Confirmed: the default scale is written at once with no countdown, and a toast");
    auto& rebuilt = dynamic_cast<d::Slider&>(w.control(M::display, "uiScale"));
    check(&rebuilt != &staged && rebuilt.value() == 1, "The rebuilt UI scale slider shows the default, not the staged drag value");
    check(!w.control(M::display, "displayRestore").enabled() && committed().isDefault(Settings::Scope::display), "At its defaults, Display's Restore is disabled");
    check(committed().boolean("closeOnFocusLost") == !focusDefault && committed().fields["windowsSummonShortcut"] == hotkey, "System and the hotkey are kept");
    // Close hint (showsCloseHint), the row right after Battery alert: Display's
    // Restore resets it.
    {
        const auto ids = rowIds(M::display);
        const auto battery = std::find(ids.begin(), ids.end(), "battery");
        check(battery != ids.end() && battery + 1 != ids.end() && battery[1] == "row:closeHint" && w.control(M::display, "closeHint").label == "Close hint",
            "Close hint is the row right after Battery alert");
    }
    w.activate(M::display, "closeHint");
    check(!committed().showsCloseHint() && committed().fields["showsCloseHint"] == Json(false) && w.control(M::display, "displayRestore").enabled(),
        "Close hint off is saved and enables Display's Restore");
    w.activate(M::display, "closeHint");
    check(committed().showsCloseHint() && !w.control(M::display, "displayRestore").enabled(), "Back on, Display is at its defaults again");
    w.clear();
    w.hide(M::display);
    w.show(M::system);
    {
        const auto ids = rowIds(M::system);
        check(!ids.empty() && ids.back() == "row:restore" && w[M::system].header().counterText() == d::format::pad2(std::int64_t(ids.size())),
            "System's Restore is its last row and the counter counts its rows");
    }
    w.loginError.reset(); w.update([](Settings& s) { s.set("launchAtLogin", true); });
    check(w.control(M::system, "restore").enabled(), "System's Restore stays enabled while System differs");
    w.update([&](Settings& s) { s.set("closeOnFocusLost", focusDefault); });
    check(!w.control(M::system, "restore").enabled() && committed().isDefault(Settings::Scope::system), "At its defaults, System's Restore is disabled");
    w.update([](Settings& s) { s.set("launchAtLogin", false); }); w.loginError = "Synthetic login failure";
    w.hide(M::system);
    // HUD elements' Restore (restoreRow(.hudElements), its last row): its own
    // question, message and toast, and only its scope (Open on is kept).
    w.show(M::display);
    w.activate(M::display, "overlay");
    w.update([](Settings& s) {
        s.set("facilityAlerts", false); s.set("reduceMotion", true); s.set("closeOnFocusLost", false);
        auto l = s.overlayLayout(); l.showsSanity = false; l.openModule = "map"; s.setOverlayLayout(l);
    });
    check(!committed().isDefault(Settings::Scope::hudElements), "Facility alerts off differs from HUD elements' defaults");
    w.activate(M::display, "overlayRestore");
    check(overlay.confirmState().request()->id == "overlayRestore" && overlay.confirmState().request()->title == "Restore HUD elements?"
        && overlay.confirmState().request()->message == "Only this page's settings change" && overlay.confirmState().request()->confirmTitle == "Restore"
        && overlay.decide(d::ConfirmState::Decision::confirm), "HUD elements' Restore asks with its own copy");
    (void)w.context.advance(w.now + 1);
    check(toastTitle() == "HUD elements restored" && committed().facilityAlerts() && committed().isDefault(Settings::Scope::hudElements),
        "Confirmed: HUD elements is at its defaults, with its own toast");
    check(committed().boolean("reduceMotion") && !committed().boolean("closeOnFocusLost") && committed().overlayLayout().openModule == std::optional<std::string>("map")
        && committed().overlayLayout().showsSanity, "Display, System and Open on are kept");
    check(!w.control(M::display, "overlayRestore").enabled(), "At its defaults, HUD elements' Restore is disabled");
    w.update([](Settings& s) { auto l = s.overlayLayout(); l.openModule.reset(); s.setOverlayLayout(l); });
    w.clear();
    w.language = L::korean; w.appearance(); w.controller->setLanguage(w.language);
    w.update([](Settings& s) { s.set("facilityAlerts", false); });
    check(w[M::display].section() == "overlay", "A language change keeps HUD elements shown");
    w.activate(M::display, "overlayRestore");
    check(overlay.confirmState().request()->title == "HUD 요소를 기본값으로 복원할까요?" && overlay.confirmState().request()->message == "이 페이지의 설정만 복원됩니다"
        && overlay.decide(d::ConfirmState::Decision::confirm), "HUD elements' Restore copy comes from the catalog");
    (void)w.context.advance(w.now + 1);
    check(toastTitle() == "HUD 요소 복원됨", "HUD elements' toast comes from the catalog");
    w.clear();
    w.language = L::english; w.appearance(); w.controller->setLanguage(w.language);
    w.update([](Settings& s) { s.set("reduceMotion", false); s.set("closeOnFocusLost", true); });
    w.hide(M::display);
    // Left on a sub-page (the owner's leave: prepareToLeave, then inactive), Settings
    // shows the page it returns to when shown again, built before the entrance
    // plays (HUDSettingsCanvas.makeContent), at the top.
    {
        const auto leave = [&](M module) { w[module].prepareToLeave(w.now); w.hide(module); };
        const auto onPage = [&](M module, std::string_view id) {
            const auto& controls = w[module].page().controls();
            return std::any_of(controls.begin(), controls.end(), [&](const d::Control* c) { return c->id() == id; });
        };
        const auto area = [&](M module, std::string_view id) -> const d::ScrollArea* {
            for (const auto& a : w[module].page().scrollAreas()) if (a->id() == id) return a.get();
            return nullptr;
        };
        // The page show() built is the one the entrance staggers: its rows
        // enter one after another and activation swaps in nothing else.
        const auto staggersOnEntrance = [&](M module) {
            const auto* built = &w[module].page();
            const std::vector<d::Layer*> rows(built->staggered().begin(), built->staggered().end());
            if (rows.empty()) return false;
            w.appearance(false);
            w[module].playEntrance(d::entranceOffset(M::about, module), w.now);
            bool entering = true;
            for (std::size_t i = 0; i < rows.size(); ++i)
                entering = entering && rows[i]->animating(w.now + d::staggerDelay(int(i)) + .01)
                    && !rows[i]->animating(w.now + d::staggerDelay(int(i)) + d::duration::entrance + .01);
            w[module].setActive(true, w.now);
            const bool kept = &w[module].page() == built;
            w.appearance();
            return entering && kept;
        };
        w.show(M::display);
        w.activate(M::display, "modules");
        check(w[M::display].section() == "modules" && area(M::display, "modules"), "Display opens Modules");
        (void)w[M::display].scroll({300, 300}, {0, 240}, w.now);
        check(area(M::display, "modules")->offset() > 0, "The Modules list scrolls");
        leave(M::display);
        w[M::display].show(w.now);
        check(w[M::display].section() == "main" && onPage(M::display, "modules"),
            "Display left on Modules shows its main page before the entrance plays");
        check(staggersOnEntrance(M::display), "Display's main page plays the staggered entrance after Modules");
        w.activate(M::display, "modules");
        check(area(M::display, "modules") && area(M::display, "modules")->offset() == 0, "Modules opens again at the top");
        leave(M::display);
        w.show(M::system);
        check(staggersOnEntrance(M::system), "System's main page plays the staggered entrance");
        leave(M::system);
        w.show(M::display);
        (void)w[M::display].scroll({300, 300}, {0, 240}, w.now);
        check(area(M::display, "display") && area(M::display, "display")->offset() > 0, "The Display list scrolls before App icon");
        w.activate(M::display, "appIcon");
        leave(M::display);
        w[M::display].show(w.now);
        check(w[M::display].section() == "main" && onPage(M::display, "appIcon") && area(M::display, "display") && area(M::display, "display")->offset() == 0,
            "Display left on App icon shows its main page, at the top, before the entrance plays");
        check(staggersOnEntrance(M::display), "Display's main page plays the staggered entrance");
        (void)w[M::display].scroll({300, 300}, {0, 240}, w.now);
        check(w[M::display].page().scrollAreas()[0]->offset() > 0, "The Display list scrolls before Battery alert");
        w.activate(M::display, "battery");
        leave(M::display);
        w.show(M::display);
        check(w[M::display].section() == "battery", "Battery alert is kept when Display is left");
        { const auto back = w[M::display].page().back; back(); }
        check(w[M::display].section() == "main" && w[M::display].page().scrollAreas()[0]->offset() == 0, "Its Back lands on Display at the top");
        leave(M::display);
    }
    w.clear();

    // --- About: the update strip and links ------------------------------------
    w.show(M::about);
    check(accessibilityValue(M::about, "value:updates") == "Not checked yet", "Idle reads Not checked yet");
    w.activate(M::about, "updates");
    check(w.hooks.back() == "checkUpdates" && accessibilityValue(M::about, "value:updates") == "Connecting", "Check starts the strip");
    auto checking = update; checking.phase = m::SettingsUpdatePhase::checking;
    w.controller->setUpdateStatus(checking);
    check(!w.control(M::about, "updates").enabled(), "Check is disabled while checking");
    for (int step = 0; step < 4; ++step) { w.now += .05; (void)w.context.advance(w.now); }
    check(accessibilityValue(M::about, "value:updates") == "Verifying", "It advances to Verifying and holds there while checking");
    auto done = update; done.phase = m::SettingsUpdatePhase::current; done.latestVersion = "1.3.0";
    w.controller->setUpdateStatus(done);
    w.now += .05; (void)w.context.advance(w.now);
    check(accessibilityValue(M::about, "value:updates") == "Up to date" && w.control(M::about, "updates").enabled(), "Then the outcome: Up to date");
    // The open-time daily check (Windows checks as the HUD opens): its phase
    // and result bind in place, so a page still entering is not rebuilt.
    {
        w.controller->setUpdateStatus(update);
        w[M::about].prepareToLeave(w.now); w.hide(M::about);
        w.show(M::about);
        const auto* page = &w[M::about].page();
        const auto builds = w.context.pagesBuilt();
        w.controller->setUpdateStatus(checking);
        auto daily = update; daily.phase = m::SettingsUpdatePhase::available; daily.latestVersion = "1.3.2"; daily.title = "Update available";
        daily.releaseURL = "https://github.com/DDDuoDuo/EndfieldHUD/releases/tag/v1.3.2";
        w.controller->setUpdateStatus(daily);
        check(&w[M::about].page() == page && w.context.pagesBuilt() == builds, "The daily check's phases and result rebuild no page");
        check(accessibilityValue(M::about, "value:updates") == "Update available" && accessibilityValue(M::about, "value:latestRelease") == "1.3.2",
            "The strip and the latest release read the result in place");
        w.activate(M::about, "latestRelease:open");
        check(w.links.back() == daily.releaseURL, "The latest release link opens the result's release");
        w.controller->setUpdateStatus(done);
        check(accessibilityValue(M::about, "value:updates") == "Up to date" && w.context.pagesBuilt() == builds, "A later result binds in place too");
    }
    // The hero's tri-strip (HUDArtwork.triStrip): three equal bands magenta, mint, yellow at (0, 14, 4, 38).
    {
        const std::function<const d::Layer*(const d::Layer&)> find = [&](const d::Layer& l) -> const d::Layer* {
            if (l.id().ends_with("about.hero.strip")) return &l;
            for (const auto* c : l.children()) if (const auto* f = find(*c)) return f;
            return nullptr;
        };
        const auto* strip = find(w[M::about].root());
        check(strip && strip->frame() == d::Rect{0, 14, 4, 38} && strip->content() && (*strip->content())["children"].array().size() == 3, "The tri-strip is three bands of the hero");
        const auto& bands = (*strip->content())["children"].array();
        const std::array<d::Color, 3> colours{d::rgb(0xFF1AAC), d::rgb(0x00FFA2), d::rgb(0xFFFA00)};
        for (std::size_t i = 0; i < 3; ++i)
            check(near(rectOf(bands[i]["bounds"]).height, 38. / 3) && near(*rgbaOf(bands[i]["backgroundColor"]["sRGB"]), colours[i], 1. / 400), "Tri-strip band " + std::to_string(i));
    }
    w.activate(M::about, "github:open");
    check(w.links.back() == "https://github.com/DDDuoDuo/EndfieldHUD" && toastTitle() == "Opening GitHub" && toastCaption() == "DDDuoDuo/EndfieldHUD", "GitHub links toast their path");
    // BUG Report/Suggestions (HUDSettingsCanvas.feedbackURL): the Google form in every language but Simplified Chinese.
    w.activate(M::about, "feedback:open");
    check(w.links.back() == m::settingsFeedbackURL(core::Language::english) && w.links.back().starts_with("https://docs.google.com/forms/") && toastTitle() == "Opening link" && toastCaption() == "docs.google.com",
        "The feedback row opens the Google form and toasts its host");
    check(m::settingsFeedbackURL(core::Language::simplifiedChinese) == "https://wj.qq.com/s2/28548057/ejeu/" && m::settingsFeedbackURL(core::Language::traditionalChinese) == m::settingsFeedbackURL(core::Language::english),
        "Only Simplified Chinese opens the Tencent questionnaire");
    w.activate(M::about, "credits");
    w.activate(M::about, "credit:1:open");
    check(toastTitle() == "Opening link" && toastCaption() == "www.bilibili.com", "Other links toast their host");
    check(w.control(M::about, "row:credit:3").label == "Ronit Singh / FineTune", "Credit names use / (no middle dots)");
    w.hide(M::about);
    w.clear();
}

// The owner suite's sequence (tests/settings_owner_tests.cpp) on the portable
// layer: pages built, menus opened and closed, a sub-page visited, then the
// layout prompt's deadline passing with every deferred item due at once.
void deferredWork() {
    using M = core::Module;
    RecordedMeasurer measurer;
    m::SettingsCanvasHooks hooks;
    hooks.logoArtwork = [](std::string_view logo, std::string_view, d::Size) -> std::optional<m::SettingsLogoImage> {
        return m::SettingsLogoImage{{d::ImageSource::Kind::memory, "logo." + std::string(logo), {}, 1}, {200, 40}};
    };
    hooks.icons = {{"endfield", "Endfield", {d::ImageSource::Kind::asset, "application-icons/endfield-app.png", std::string(64, '0'), 0}}};
    m::SettingsUpdateStatus update;
    World w(measurer, hooks, update);
    w.now = 0;
    w.show(M::system);
    w.activate(M::system, "focus");
    w.now = .2; w.hide(M::system); w.show(M::display);
    w.activate(M::display, "modules");
    { const auto back = w[M::display].page().back; back(); }
    w.activate(M::display, "centerLogo"); w[M::display].closeMenu();
    w.activate(M::display, "centerLogo"); w[M::display].closeMenu();
    w.activate(M::display, "appIcon");
    { const auto back = w[M::display].page().back; back(); }
    w.now = .6; w.controller->previewScale(1.3, w.now);
    w.now = 1.6; w.wake(M::display);
    w.now = 12.6; w.wake(M::display);
    check(!w.context.overlay().isConfirming() && !w.controller->scalePending(), "The deadline reverts the preview and closes the bar");
    (void)w.context.advance(w.now);
    check(w.context.overlay().toastState().current() && w.context.overlay().toastState().current()->title == "UI scale reverted", "Every deferred item ran safely");
}

// HUDSettingsCanvas.controllerChanged rebuilds every built canvas at once (a
// CALayer rebuild); here a canvas that is not shown rebuilds when next shown,
// with the same page, and a language pick builds the shown page once, when the
// host's appearance brings the new language (the hook), never first in the old.
void broadcastBuilds() {
    using M = core::Module;
    RecordedMeasurer measurer;
    m::SettingsCanvasHooks hooks;
    hooks.language = [](const Settings& s) { const auto l = core::languageFromSetting(s.string("language")).value_or(L::system); return l == L::system ? L::english : l; };
    std::optional<M> reopens; // the module the closed HUD reopens on
    hooks.reopensOn = [&reopens](M module) { return reopens == module; };
    m::SettingsUpdateStatus update;
    World w(measurer, hooks, update);
    const auto leave = [&](M module) { w.hide(module); w[module].prepareToLeave(w.now); };
    for (const auto module : {M::system, M::hotkeys, M::about}) { w.show(module); leave(module); }
    w.show(M::display);
    auto pages = w.context.pagesBuilt();
    w.update([](Settings& s) { s.set("accentHex", "3399FF"); });
    check(w.context.pagesBuilt() == pages + 1, "An accent pick builds only the shown Display page (" + std::to_string(w.context.pagesBuilt() - pages) + ")");
    w.update([](Settings& s) { s.set("applicationIcon", "talos"); });
    check(w.context.pagesBuilt() == pages + 2, "An app-icon pick builds only the shown page");
    leave(M::display);
    w.show(M::system);
    check(w.context.pagesBuilt() == pages + 3, "A canvas built before the picks rebuilds once when next shown");
    pages = w.context.pagesBuilt();
    w.update([](Settings& s) { s.set("language", "simplifiedChinese"); });
    check(w.context.pagesBuilt() == pages && w[M::system].renderedLanguage() == L::english, "A language pick builds nothing in the old language");
    w.language = L::simplifiedChinese; w.appearance(); w.controller->setLanguage(L::simplifiedChinese);
    check(w.context.pagesBuilt() == pages + 1 && w[M::system].renderedLanguage() == L::simplifiedChinese, "The host's new language builds the shown page once, in it");
    w.update([](Settings& s) { s.set("language", "english"); });
    w.language = L::english; w.appearance(); w.controller->setLanguage(L::english);
    pages = w.context.pagesBuilt();
    const auto title = [&] { return static_cast<d::Dropdown&>(w.control(M::system, "language")).valueText(); };
    const auto shown = title();
    w.update([](Settings& s) { s.set("language", "system"); });
    check(w.context.pagesBuilt() == pages + 1 && title() != shown, "A pick that keeps the resolved language rebuilds the shown page at once (its dropdown)");
    // The HUD closed on System: a change while closed builds the page it
    // reopens on at once (not on the opening frame); the others defer.
    leave(M::system); reopens = M::system;
    pages = w.context.pagesBuilt();
    w.update([](Settings& s) { s.set("applicationIcon", "babel"); });
    check(w.context.pagesBuilt() == pages + 1, "A change while closed builds only the page the HUD reopens on (" + std::to_string(w.context.pagesBuilt() - pages) + ")");
    reopens.reset(); w.show(M::system);
    check(w.context.pagesBuilt() == pages + 1, "The reopened page builds nothing on the opening frame");
}

// Every L10n pair the canvas calls has its row in the generated catalog
// (all five languages): read from the source text, as the Mac's l10n checks do.
void catalogPairs(const char* source) {
    std::ifstream in(source, std::ios::binary); std::ostringstream text; text << in.rdbuf();
    const std::string s = text.str();
    std::size_t pairs{};
    const auto literal = [&](std::size_t& i) -> std::optional<std::string> {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\n')) ++i;
        if (i >= s.size() || s[i] != '"') return std::nullopt;
        std::string out;
        for (++i; i < s.size() && s[i] != '"'; ++i) { if (s[i] == '\\') ++i; out += s[i]; }
        ++i;
        return out;
    };
    for (const std::string_view call : {"text(", "toast(d::Icon::"}) {
        for (std::size_t at = s.find(call); at != std::string::npos; at = s.find(call, at + 1)) {
            std::size_t i = at + call.size();
            if (call != "text(") { while (i < s.size() && s[i] != ',') ++i; ++i; }
            const auto english = literal(i);
            if (!english) continue;
            while (i < s.size() && (s[i] == ',' || s[i] == ' ')) ++i;
            const auto chinese = literal(i);
            if (!chinese) continue;
            ++pairs;
            const auto* entry = core::translationEntry(*english, *chinese);
            check(entry && !entry->traditionalChinese.empty() && !entry->japanese.empty() && !entry->korean.empty(), "Catalog pair: " + *english + " / " + *chinese);
        }
    }
    check(pairs > 100, "The canvas's pairs were read (" + std::to_string(pairs) + ")");
}
}

int main(int argc, char** argv) {
    try {
        check(argc == 4, "Pass tests/fixtures/settings-source.json, resources/watch-appearance/manifest.json and modules/settings_canvas.cpp");
        const auto data = loadOracle(argv[1], {"pages", "requests"}, {"text", "layerDefaults", "logos", "about", "icons"});
        check(data.has_value(), "Read the Settings oracle");
        std::ifstream in(argv[2], std::ios::binary); std::ostringstream text; text << in.rdbuf();
        const auto manifest = Json::parse(text.str(), 8u * 1024 * 1024);
        replay(*data, manifest);
        contracts();
        deferredWork();
        broadcastBuilds();
        catalogPairs(argv[3]);
        std::cout << "Settings canvas: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Settings canvas after " << checks << " checks: " << e.what() << '\n';
        return 1;
    }
}
