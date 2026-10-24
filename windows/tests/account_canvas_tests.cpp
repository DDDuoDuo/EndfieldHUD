// Contracts of the redesigned Account page (modules/account_canvas.*) and the
// 1.3.0 account presentation it shows: the rules of Sources/HUDAccountCanvas.swift
// (each check cites the Swift line it ports) over a synthetic controller, and,
// when the Mac oracle is given (tests/fixtures/account-canvas-source.json from
// tools/account_canvas_reference.sh), every recorded scene node by node with the
// Mac's own text widths, its controls, accessibility items and the toast /
// confirm it raised, plus the Mac controller's presentation, gauge label and
// field-chip cache bytes for the same Account/profile-cache.json. The Windows
// controller decodes exactly the cache the Mac controller read. Synthetic data
// only: no network (no request is ever started), memory vault and cache.
#include "modules/account_canvas.hpp"
#include "modules/hypergryph_account_sanity_gauge.hpp"
#include "tests/hud_design_oracle.hpp"
#include <cmath>
#include <ctime>
#include <deque>
#include <iostream>
#include <map>
#include <stdexcept>

namespace d = endfield::modules::design;
namespace m = endfield::modules;
namespace h = endfield::modules::hypergryph;
using endfield::core::Language;
using namespace endfield::tests::design_oracle;
namespace {
unsigned checks{};
void check(bool v, const std::string& what) { ++checks; if (!v) throw std::runtime_error(what); }
constexpr h::Time wall = 781'000'000;

// No request is ever started: nothing here connects or refreshes through the service.
struct NoService final : h::AccountService {
    h::RequestHandle refreshCredentials(const std::string&, h::Region, std::function<void(h::Outcome<h::Credentials>)>) override { throw std::logic_error("no network"); }
    h::RequestHandle refreshCredentials(const h::Credentials&, h::Region, std::function<void(h::Outcome<h::Credentials>)>) override { throw std::logic_error("no network"); }
    h::RequestHandle bindings(const h::Credentials&, h::Region, std::function<void(h::Outcome<std::vector<h::Role>>)>) override { throw std::logic_error("no network"); }
    h::RequestHandle profile(const h::Role&, const h::Credentials&, std::function<void(h::Outcome<h::Snapshot>)>) override { throw std::logic_error("no network"); }
};
struct Queue final : h::AccountWorkQueue {
    void run(std::function<void()> work, std::function<void(std::exception_ptr)> done) override {
        std::exception_ptr error; try { work(); } catch (...) { error = std::current_exception(); } done(error);
    }
};
struct File final : h::AccountCacheFile {
    std::optional<std::string> bytes;
    std::optional<std::string> read() override { return bytes; }
    bool exists() override { return bytes.has_value(); }
    void write(const std::string& value) override { bytes = value; }
};
// DateFormatter "MM/dd HH:mm:ss" in UTC (the oracle runs with TZ=UTC).
h::LocalDateFields utc(h::Time time) {
    const auto seconds = static_cast<std::time_t>(std::floor(h::toUnix(time)));
    std::tm v{};
#ifdef _WIN32
    gmtime_s(&v, &seconds);
#else
    gmtime_r(&seconds, &v);
#endif
    return {v.tm_mon + 1, v.tm_mday, v.tm_hour, v.tm_min, v.tm_sec};
}
// One HypergryphAccountController over a cache file's bytes (nullopt: absent).
struct Account {
    NoService service; h::MemoryCredentialVault vault; Queue queue; File file;
    std::unique_ptr<h::HypergryphAccountController> controller;
    Account(std::optional<std::string> cache, Language language) {
        file.bytes = std::move(cache);
        h::AccountControllerOptions o; o.now = [] { return wall; }; o.localDate = utc; o.language = language;
        controller = std::make_unique<h::HypergryphAccountController>(service, vault, queue, &file, nullptr, nullptr, nullptr, o);
    }
};

struct World {
    d::TextMeasurer& measurer;
    d::Context context;
    double now{100};
    explicit World(d::TextMeasurer& m) : measurer(m), context(m, d::asciiUppercase) { context.update(d::geometry::baseUnit, 2 / d::geometry::baseUnit, now); }
    void appearance(Language language, bool light = false) {
        d::Context::Appearance a; a.language = language; a.light = light; a.reduceMotion = true;
        context.apply(a, now);
    }
    void advance(double seconds) { now += seconds; context.advance(now); }
    std::string toast() {
        const auto& t = context.overlay().toastState().current();
        return t ? t->title + "|" + t->caption : std::string();
    }
    std::string confirm() {
        const auto& r = context.overlay().confirmState().request();
        return r ? r->title + "|" + r->message + "|" + r->confirmTitle + (r->danger ? "|danger" : "") : std::string();
    }
};
std::vector<d::Control*> controls(d::Canvas& canvas) {
    auto out = canvas.header().controls();
    for (auto* c : canvas.page().controls()) out.push_back(c);
    return out;
}
d::Control* control(d::Canvas& canvas, std::string_view id) {
    for (d::Control* c : controls(canvas)) if (c->id() == id) return c;
    return nullptr;
}
std::vector<std::string> ids(const m::AccountCanvas& canvas) {
    std::vector<d::AccessibilityItem> items; canvas.accessibilityItems(items);
    std::vector<std::string> out; for (const auto& i : items) out.push_back(i.id);
    return out;
}
std::string actionName(const h::AccountAction& a) {
    using K = h::AccountAction::Kind;
    switch (a.kind) {
    case K::connect: return "connect:" + std::string(h::regionChoiceName(a.region));
    case K::refresh: return "refresh";
    case K::disconnect: return "disconnect";
    case K::selectRegion: return "selectRegion:" + std::string(h::regionChoiceName(a.region));
    case K::selectRole: return "selectRole:" + a.roleID;
    case K::selectHeaderMode: return "selectHeaderMode:" + std::string(h::headerModeName(a.headerMode));
    case K::setSyncProfile: return std::string("setSyncProfile:") + (a.enabled ? "true" : "false");
    case K::setSyncAvatar: return std::string("setSyncAvatar:") + (a.enabled ? "true" : "false");
    case K::setSyncField: return "setSyncField:" + std::string(ehud::data::profileSyncFieldName(a.syncField)) + ":" + (a.enabled ? "true" : "false");
    }
    return {};
}

// A synthetic linked presentation (Swift-derived contracts without the oracle).
h::AccountPresentation linkedPresentation() {
    h::AccountPresentation p;
    p.region = h::AccountPresentation::RegionChoice::global; p.isLinked = true; p.status = h::AccountPresentation::Status::connected;
    p.roles = {{"ef", "Endfield / Endmin#1234", "Asia"}, {"ak", "Arknights / Doctor", ""}}; p.selectedRoleID = "ef";
    p.headerMode = h::AccountPresentation::HeaderMode::endfield; p.lastSync = "Updated 10/01 08:25:40";
    h::Snapshot s; s.observedAt = wall - 60; s.stamina = h::Stamina{42, 360, wall - 60 + 318 * 432., wall - 60};
    p.snapshot = s;
    return p;
}

void contracts() {
    FakeMeasurer measurer;
    World w(measurer); w.appearance(Language::english);
    m::AccountCanvas canvas(w.context);
    double clock = wall;
    canvas.wallClock = [&] { return clock; };
    std::vector<std::string> actions;
    canvas.onAction = [&](const h::AccountAction& a) { actions.push_back(actionName(a)); };
    canvas.update({});
    canvas.show(w.now); canvas.setActive(true, w.now);
    // Unlinked (:190-202, verification L43-45): only the region switch and Connect.
    check(ids(canvas) == std::vector<std::string>{"region:0", "region:1", "connect"}, "The unlinked page offers only Region and Connect");
    check(canvas.header().counterText() == "OFFLINE" && canvas.header().crumbTexts().back() == "ACCOUNT", "Header ACCOUNT / OFFLINE (:169-170)");
    control(canvas, "connect")->activate();
    check(actions == std::vector<std::string>{"connect:china"}, "Connect sends the page's region (:197)");
    actions.clear();
    control(canvas, "region:0")->activate();
    check(actions.empty(), "Selecting the current region sends nothing (:175)");
    control(canvas, "region:1")->activate();
    check(actions == std::vector<std::string>{"selectRegion:global"}, "Selecting Global sends selectRegion (:176)");
    actions.clear();
    // Linking: the page slides forward and toasts (:102-104).
    auto p = linkedPresentation();
    h::AccountPresentation global; global.region = p.region;
    canvas.update(global);
    check(w.toast().empty(), "A region change slides the page and never toasts (:98-101)");
    canvas.update(p);
    check(w.toast() == "Account linked|Global", "Linking toasts Account linked with the region title (:104)");
    w.context.overlay().reset();
    check(control(canvas, "disconnect") && !control(canvas, "reconnect") && control(canvas, "sanity") && control(canvas, "field:avatar"), "Linked controls (:155-168)");
    const auto sanity = canvas.renderedSanity();
    check(sanity && sanity->current == "42" && sanity->limit == "/ 360" && sanity->filled == 3 && sanity->next == "00:06:12" && sanity->full == "38:08:36",
        "The hero projects the snapshot on the wall clock: 42 / 360, round(42 / 360 x 24) cells, HH:MM:SS (:408-415)");
    check(control(canvas, "sanity")->label == "Sanity 42 / 360", "The sanity plate speaks Sanity current / max (:428)");
    // The 1 s clock (:135-146): only while active, linked and with stamina.
    const auto wake = canvas.nextWakeTime(w.now);
    check(wake && std::abs(*wake - (w.now + 1)) < 1e-9, "The recovery readout ticks once a second");
    clock += 1;
    w.advance(1); canvas.wake(w.now);
    check(canvas.renderedSanity()->next == "00:06:11", "A tick re-renders the countdowns");
    canvas.setActive(false, w.now);
    check(!canvas.nextWakeTime(w.now), "Deactivation stops the clock (:130)");
    canvas.setActive(true, w.now);
    // Refresh then its end (:108-113, :222-228).
    control(canvas, "refresh")->activate();
    check(actions == std::vector<std::string>{"refresh"}, "Refresh sends refresh");
    actions.clear();
    auto busy = p; busy.status = h::AccountPresentation::Status::refreshing;
    canvas.update(busy);
    check(!control(canvas, "refresh")->enabled() && !control(canvas, "disconnect")->enabled() && !control(canvas, "region:1")->enabled() &&
          !control(canvas, "role")->enabled() && control(canvas, "syncProfile")->enabled() && control(canvas, "field:name")->enabled(),
          "Busy disables refresh, disconnect, the segments and the role; the toggle and chips stay");
    canvas.update(p);
    check(w.toast() == "Sanity refreshed|42 / 360", "The finished refresh toasts Sanity refreshed with current / max");
    w.context.overlay().reset();
    // needsReconnect (:156-162, :228).
    auto reconnect = p; reconnect.needsReconnect = true;
    canvas.update(reconnect);
    check(control(canvas, "reconnect") && !control(canvas, "refresh")->enabled(), "Reconnect shows and refresh is disabled while a sign-in is required");
    control(canvas, "reconnect")->activate();
    check(actions == std::vector<std::string>{"connect:global"}, "Reconnect connects the region");
    actions.clear();
    canvas.update(p);
    // Chips (:245-277): a local toggle re-wraps and sends setSyncField.
    auto* levels = control(canvas, "field:levels");
    const double before = levels->layer().bounds().width;
    levels->activate();
    check(actions == std::vector<std::string>{"setSyncField:levels:false"} && levels->layer().bounds().width == before - 15, "A chip toggles locally (the check changes its width)");
    actions.clear();
    // Disconnect (:279-287): the confirm bar; only a confirmed, still linked page sends.
    control(canvas, "disconnect")->activate();
    check(w.confirm() == "Disconnect this account?|Global|Disconnect|danger", "Disconnect asks through the shared confirm bar");
    w.context.overlay().decide(d::ConfirmState::Decision::confirm);
    w.advance(.2);
    check(actions == std::vector<std::string>{"disconnect"}, "Confirming sends disconnect");
    auto unlinked = p; unlinked.isLinked = false; unlinked.roles.clear(); unlinked.snapshot.reset();
    canvas.update(unlinked);
    check(w.toast() == "Disconnected|", "The asked-for unlink toasts Disconnected (:105)");
    // Status messages toast while active (:114-116).
    w.context.overlay().reset();
    auto failed = unlinked; failed.statusMessage = "Sign-in could not finish. Please try again.";
    canvas.update(failed);
    check(w.toast() == "Notice|Sign-in could not finish. Please try again.", "A new status message toasts Notice");
    // gaugeCountdown (HUDAccountGauge.countdown with hours).
    check(h::gaugeCountdown(std::nullopt, wall, true) == "—" && h::gaugeCountdown(wall + 360'000'009, wall, true) == "99999:59:59", "Countdown bounds");
}

// --- The Mac oracle -------------------------------------------------------------------------------
struct Oracle {
    const Json& data;
    RecordedMeasurer measurer;
    std::vector<std::string> failures;
    std::size_t compared{};
    explicit Oracle(const Json& d) : data(d) {
        recordText(measurer, d["text"]);
        for (const auto& row : d["canvas"].array()) if (recordMicroTag(measurer, row["layer"])) break;
    }
    const Json* row(std::string_view state, std::string_view language = "en", bool light = false) const {
        for (const auto& r : data["canvas"].array())
            if (r["state"].string() == state && r["language"].string() == language && r["light"].boolean() == light) return &r;
        return nullptr;
    }
    static Json rooted(const Json& layer) {
        Json copy = layer;
        Point anchor{.5, .5};
        if (copy["anchorPoint"].isArray()) anchor = {copy["anchorPoint"].array()[0].number(), copy["anchorPoint"].array()[1].number()};
        const Rect b = rectOf(copy["bounds"]);
        copy["position"] = Json::Array{anchor.x * b.width + b.x, anchor.y * b.height + b.y};
        copy["transform"] = Json();
        return copy;
    }
    void scene(std::string_view state, m::AccountCanvas& canvas, World& w, std::vector<std::string>& actions, std::string_view language = "en", bool light = false) {
        const Json* r = row(state, language, light);
        const std::string label = "account/" + std::string(state) + "/" + std::string(language) + (light ? "/light" : "");
        if (!r) { failures.push_back(label + ": not in the oracle"); return; }
        ++compared;
        Json expected = *r;
        expected["layer"] = rooted((*r)["layer"]);
        const auto list = controls(canvas);
        compare(label, expected, canvas.root(), canvas.root(), list, failures);
        std::vector<d::AccessibilityItem> items; canvas.accessibilityItems(items);
        compareAccessibility(label, static_cast<const Json&>(expected)["accessibility"], items, failures);
        if ((*r)["top"].isObject()) compare(label + "/top", Json::Object{{"layer", (*r)["top"]}}, canvas.top(), canvas.root(), {}, failures);
        if (canvas.header().counterText() != (*r)["header"]["counter"].string()) failures.push_back(label + ": counter " + canvas.header().counterText());
        if (canvas.header().titleTexts().first != (*r)["header"]["title"].string()) failures.push_back(label + ": title " + canvas.header().titleTexts().first);
        const auto& toast = (*r)["toast"];
        const std::string expectedToast = toast.isObject() ? toast["title"].string() + "|" + toast["caption"].string() : std::string();
        if (w.toast() != expectedToast) failures.push_back(label + ": toast '" + w.toast() + "' vs '" + expectedToast + "'");
        const auto& confirm = (*r)["confirm"];
        const std::string expectedConfirm = confirm.isObject() ? confirm["title"].string() + "|" + confirm["message"].string() + "|" +
            confirm["confirm"].string() + (confirm["danger"].boolean() ? "|danger" : "") : std::string();
        if (w.confirm() != expectedConfirm) failures.push_back(label + ": confirm '" + w.confirm() + "' vs '" + expectedConfirm + "'");
        std::vector<std::string> mac; for (const auto& a : (*r)["actions"].array()) mac.push_back(a.string());
        if (mac != actions) failures.push_back(label + ": actions differ");
        actions.clear();
    }
};
std::optional<std::string> cacheOf(const Json& input) { return input["cache"].isString() ? std::optional(input["cache"].string()) : std::nullopt; }
Language languageOf(std::string_view name) { return name == "zh-Hans" ? Language::simplifiedChinese : Language::english; }
// The cache's keys as decoded by Windows against the Mac's own bytes.
bool sameCache(const std::string& windows, const std::string& mac) {
    const auto a = Json::parse(windows), b = Json::parse(mac);
    for (const auto* key : {"syncProfile", "partialSyncProfile", "unsyncedFields", "header", "region", "version", "syncAvatar"})
        if ((a.contains(key) != b.contains(key)) || (a.contains(key) && a[key].encode() != b[key].encode())) return false;
    return true;
}

void controllerOracle(Oracle& o) {
    for (const auto& row : o.data["controller"].array()) {
        const auto language = languageOf(row["language"].string());
        Account account(row["cache"].string(), language);
        const auto p = account.controller->presentation();
        const auto& e = row["presentation"];
        const std::string label = "controller/" + row["state"].string() + "/" + row["language"].string();
        check(h::regionChoiceTitle(p.region, language) == e["regionTitle"].string() && p.isLinked == e["linked"].boolean() &&
              p.needsReconnect == e["needsReconnect"].boolean() && std::string(h::statusName(p.status)) == e["status"].string() &&
              p.statusMessage == e["statusMessage"].string() && p.accountName == e["accountName"].string() && p.lastSync == e["lastSync"].string() &&
              p.syncProfile == e["syncProfile"].boolean() && p.syncAvatar == e["syncAvatar"].boolean() && p.snapshot.has_value() == e["snapshot"].boolean(),
              label + ": presentation (Mainland China title, status, lastSync, snapshot)");
        check(p.roles.size() == e["roles"].array().size(), label + ": roles");
        for (std::size_t i = 0; i < p.roles.size(); ++i) {
            const auto& r = e["roles"].array()[i];
            check(p.roles[i].id == r["id"].string() && p.roles[i].title == r["title"].string() && p.roles[i].subtitle == r["subtitle"].string(),
                label + ": role title '" + p.roles[i].title + "' (\" / \" separator, MODS-872)");
        }
        std::vector<std::string> fields; for (const auto f : ehud::data::profileSyncFieldOrder) if (p.syncFields.contains(f)) fields.emplace_back(ehud::data::profileSyncFieldName(f));
        std::sort(fields.begin(), fields.end());
        std::vector<std::string> macFields; for (const auto& f : e["syncFields"].array()) macFields.push_back(f.string());
        check(fields == macFields, label + ": syncFields");
        const auto gauge = account.controller->gaugeValue({true, 1500, 60});
        const auto& g = row["gauge"].array();
        check(gauge.value == g[0].string() && gauge.accessibilityLabel == g[1].string() && gauge.visible == g[2].boolean(),
            label + ": header gauge '" + gauge.accessibilityLabel + "' (\", \" separator, MODS-873)");
        check(h::regionChoiceTitle(h::AccountPresentation::RegionChoice::china, language) == row["chinaTitle"].string() &&
              h::regionChoiceTitle(h::AccountPresentation::RegionChoice::global, language) == row["globalTitle"].string(), label + ": region titles");
        for (std::size_t i = 0; i < 6; ++i)
            check(h::syncFieldTitle(ehud::data::profileSyncFieldOrder[i], language) == row["fieldTitles"].array()[i].string(), label + ": field chip title");
    }
    // Field chips through the controller (MODS-868, MODS-870).
    const auto& sync = o.data["syncFields"];
    Account account(sync["cache"].string(), Language::english);
    const auto perform = [&](h::AccountAction::Kind kind, std::optional<ehud::data::ProfileSyncField> field, bool enabled) {
        h::AccountAction a; a.kind = kind; a.enabled = enabled; if (field) a.syncField = *field;
        account.controller->perform(a);
    };
    using F = ehud::data::ProfileSyncField; using K = h::AccountAction::Kind;
    const std::array<std::tuple<K, std::optional<F>, bool>, 6> steps{{{K::setSyncField, F::levels, false}, {K::setSyncField, F::uid, false},
        {K::setSyncProfile, std::nullopt, true}, {K::setSyncField, F::levels, true}, {K::setSyncField, F::uid, true}, {K::setSyncProfile, std::nullopt, false}}};
    for (std::size_t i = 0; i < steps.size(); ++i) {
        perform(std::get<0>(steps[i]), std::get<1>(steps[i]), std::get<2>(steps[i]));
        const auto& e = sync["steps"].array()[i];
        check(account.file.bytes && sameCache(*account.file.bytes, e["cache"].string()), "syncFields/" + e["step"].string() + ": the cache keys match the Mac encoder");
        check(account.controller->presentation().syncProfile == e["syncProfile"].boolean(), "syncFields/" + e["step"].string() + ": the switch");
    }
    for (const auto& row : o.data["countdowns"].array()) {
        const auto offset = row["offset"].isNull() ? std::optional<double>{} : std::optional(row["offset"].number());
        check(h::gaugeCountdown(offset ? std::optional(wall + *offset) : std::nullopt, wall, true) == row["text"].string(), "Countdown " + row["text"].string());
    }
}

void canvasOracle(Oracle& o) {
    World w(o.measurer); w.appearance(Language::english);
    m::AccountCanvas canvas(w.context);
    canvas.wallClock = [] { return wall; };
    std::vector<std::string> actions;
    canvas.onAction = [&](const h::AccountAction& a) { actions.push_back(actionName(a)); };
    std::unique_ptr<Account> account;
    const auto load = [&](std::string_view state, Language language = Language::english) {
        const auto* r = o.row(state, language == Language::english ? "en" : "zh-Hans");
        account = std::make_unique<Account>(r ? cacheOf((*r)["input"]) : std::nullopt, language);
        return account->controller->presentation();
    };
    const auto show = [&] { canvas.show(w.now); };
    const auto press = [&](std::string_view id) { if (auto* c = control(canvas, id)) c->activate(); else o.failures.push_back(std::string("no control ") + std::string(id)); };
    canvas.update(load("offline"));
    show(); canvas.setActive(true, w.now);
    o.scene("offline", canvas, w, actions);
    press("region:1");
    o.scene("offlineRegionPress", canvas, w, actions);
    canvas.update(load("offlineGlobal"));
    o.scene("offlineGlobal", canvas, w, actions);
    w.context.overlay().reset();
    canvas.update(load("linked"));
    o.scene("linked", canvas, w, actions);
    w.context.overlay().reset();
    const auto rowCentre = [&](std::string_view id) { const auto r = control(canvas, id)->rect(canvas.root()); return Point{r.x + r.width / 2, r.y + r.height / 2}; };
    canvas.pointerMoved(rowCentre("row:role"), false, w.now);
    o.scene("hoverRow", canvas, w, actions);
    canvas.pointerMoved(std::nullopt, false, w.now);
    press("role");
    o.scene("roleMenu", canvas, w, actions);
    canvas.closeMenu();
    press("header");
    o.scene("headerMenu", canvas, w, actions);
    canvas.closeMenu();
    press("sanity");
    o.scene("sanityPress", canvas, w, actions);
    w.context.overlay().reset();
    press("field:levels");
    o.scene("chipOff", canvas, w, actions);
    h::AccountAction off; off.kind = h::AccountAction::Kind::setSyncField; off.syncField = ehud::data::ProfileSyncField::levels; off.enabled = false;
    account->controller->perform(off);
    canvas.update(account->controller->presentation());
    o.scene("chipsAfterController", canvas, w, actions);
    press("disconnect");
    o.scene("disconnectConfirm", canvas, w, actions);
    w.context.overlay().dismissConfirm();
    canvas.update(load("busy"));
    press("refresh");
    auto busy = account->controller->presentation(); busy.status = h::AccountPresentation::Status::refreshing;
    canvas.update(busy);
    o.scene("busy", canvas, w, actions);
    canvas.update(account->controller->presentation());
    o.scene("refreshed", canvas, w, actions);
    w.context.overlay().reset();
    auto failed = account->controller->presentation(); failed.status = h::AccountPresentation::Status::failed;
    failed.statusMessage = (*o.row("failed"))["input"]["overrides"]["statusMessage"].string();
    canvas.update(failed);
    o.scene("failed", canvas, w, actions);
    w.context.overlay().reset();
    canvas.update(load("reconnect"));
    o.scene("reconnect", canvas, w, actions);
    w.context.overlay().reset();
    canvas.update(load("noSnapshot"));
    o.scene("noSnapshot", canvas, w, actions);
    canvas.update(load("linked"));
    w.appearance(Language::simplifiedChinese); canvas.setActive(false, w.now); show(); canvas.setActive(true, w.now);
    canvas.update(load("linked", Language::simplifiedChinese));
    o.scene("linked", canvas, w, actions, "zh-Hans");
    press("header");
    o.scene("headerMenu", canvas, w, actions, "zh-Hans");
    canvas.closeMenu();
    // The Mac keeps the presentation it last received (made in Chinese).
    w.appearance(Language::english); canvas.setActive(false, w.now); show(); canvas.setActive(true, w.now);
    w.appearance(Language::english, true); show();
    o.scene("linked", canvas, w, actions, "en", true);
    w.appearance(Language::english, false); show();
    press("disconnect");
    w.context.overlay().decide(d::ConfirmState::Decision::confirm);
    w.advance(.2);
    canvas.update(load("unlinked"));
    o.scene("unlinked", canvas, w, actions);
    canvas.setActive(false, w.now);
}
}

int main(int argc, char** argv) {
    try {
        contracts();
        if (const auto data = loadOracle(argc > 1 ? argv[1] : nullptr, {"canvas"}, {"text", "layerDefaults", "controller", "syncFields", "countdowns", "wall"})) {
            Oracle oracle(*data);
            controllerOracle(oracle);
            canvasOracle(oracle);
            std::cout << "oracle: " << oracle.compared << " scenes compared, " << oracle.measurer.misses.size() << " unrecorded measurements\n";
            for (const auto& miss : oracle.measurer.misses) std::cout << "  unrecorded: " << miss << '\n';
            for (const auto& f : oracle.failures) std::cout << "  MISMATCH " << f << '\n';
            check(oracle.failures.empty(), std::to_string(oracle.failures.size()) + " oracle mismatches");
        } else std::cout << "account-canvas-source.json not present: Swift-derived contracts only\n";
        std::cout << "account_canvas_tests: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "account_canvas_tests failed after " << checks << " checks: " << e.what() << '\n';
        return 1;
    }
}
