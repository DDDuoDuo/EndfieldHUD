// The Windows-only "Import macOS data…" flow and card (app/areas/
// mac_import_flow.hpp, mac_import_card.hpp) and the restart-handoff outcome
// note (core/migration/mac_import_outcome.hpp), over synthetic exports in
// temporary directories only. The shared UtilityExecutor is drained
// explicitly; no window, dialog, timer or real user data is involved.
#include "mac_import_test_support.hpp"
#include "app/areas/mac_import_card.hpp"
#include "app/areas/mac_import_flow.hpp"
#include "modules/hud_design_page.hpp"
#include "core/migration/mac_import_outcome.hpp"
#include <cmath>
#include <functional>
#include <set>

namespace {
namespace a = endfield::areas;
using L = endfield::core::Language;
using Step = a::MacImportStep;
constexpr std::array<L, 5> languages{L::english, L::simplifiedChinese, L::traditionalChinese, L::japanese, L::korean};

std::string utf8(const fs::path& p) { const auto t = p.u8string(); return std::string(reinterpret_cast<const char*>(t.data()), t.size()); }
fs::path utf8Path(std::string_view text) { return fs::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size())); }
bool contains(std::string_view text, std::string_view part) { return text.find(part) != std::string_view::npos; }

// A flow over one executor with a recording host.
struct Harness {
    endfield::app::UtilityExecutor executor{[] {}};
    fs::path destination;
    bool inProcess{true}, restartAvailable{true}, restartAccepts{true};
    unsigned choices{}, cancelledChoices{}, changes{}, forgotten{};
    std::string chooserTitle;
    std::shared_ptr<m::MacImportSession> restarted;
    std::unique_ptr<a::MacImportFlow> flow;
    explicit Harness(fs::path d) : destination(std::move(d)) {
        a::MacImportFlow::Host host;
        host.chooseFolder = [this](const std::string& title, const std::string&) { ++choices; chooserTitle = title; return true; };
        host.cancelChoice = [this] { ++cancelledChoices; };
        host.inProcess = [this] { return inProcess; };
        host.restart = [this](std::shared_ptr<m::MacImportSession> s) { if (!restartAccepts) return false; restarted = std::move(s); return true; };
        host.restartAvailable = [this] { return restartAvailable; };
        host.forgetOutcome = [this] { ++forgotten; m::removeMacImportOutcome(destination); };
        host.changed = [this] { ++changes; };
        host.unixNow = [] { return 1760000000.0; };
        flow = std::make_unique<a::MacImportFlow>(executor, destination, [] { return platform(); }, std::move(host));
    }
    ~Harness() { flow.reset(); executor.waitIdle(); executor.drain(); executor.shutdown(); }
    void settle() {
        for (int guard = 0; guard < 400 && flow->busy(); ++guard) { executor.waitIdle(); executor.drain(); flow->utilityCompleted(); }
        check(!flow->busy(), "The flow settles through the shared executor");
    }
    const a::MacImportCardContent& c() const { return flow->content(); }
    void choose(const fs::path& folder) {
        const auto before = choices;
        flow->activate(1);
        check(choices == before + 1 && flow->step() == Step::choosing && !c().enabled[1], "Choose Folder asks the shared dialog once and waits");
        flow->folderChosen(false, false, {utf8(folder)});
    }
};

void strings() {
    std::set<std::string> entries;
    for (const auto l : languages) {
        const auto title = a::macImportEntryTitle(l);
        check(!title.empty() && contains(title, "macOS"), "The Settings row title is localized and names macOS");
        entries.insert(title);
        std::set<std::string> stores;
        for (const auto store : m::macImportStores) stores.insert(a::macImportStoreTitle(store, l));
        check(stores.size() == m::macImportStores.size() && !stores.contains(""), "Every data set has a distinct localized name");
    }
    check(entries.size() == languages.size(), "Five distinct languages for the Settings row");
    // Store names come from the shared five-language HUD catalog.
    for (const auto& [en, zh] : std::initializer_list<std::pair<std::string_view, std::string_view>>{{"Settings", "设置"}, {"Notes", "便笺"}, {"Archive", "档案库"},
             {"Personal Profile", "个人名片"}, {"Temporary File Shelf", "文件暂存架"}, {"E-Reader", "阅读器"}, {"Calendar", "日历"}, {"Map", "地图"},
             {"+ Add App", "+ 添加应用"}, {"Event Log", "事件日志"}, {"Account Linking", "账户绑定"}, {"Center logo", "中心标志"}, {"Cancel", "取消"},
             {"Close", "关闭"}, {"Back", "返回"}})
        check(endfield::core::translationEntry(en, zh) != nullptr, "Catalog entry exists for " + std::string(en));
    check(a::macImportStoreTitle(m::MacImportStore::notes, L::japanese) == "\xE3\x83\xA1\xE3\x83\xA2", "Japanese Notes name from the catalog");
}

void outcomeCodec() {
    m::MacImportOutcome o;
    o.state = m::MacImportOutcome::State::completed;
    o.sourceVersion = "1.2.0"; o.sourceBuild = "18"; o.backup = "C:\\Users\\Doctor\\AppData\\Local\\EndfieldHUD\\v1.backup-20261010T000000Z-1234abcd";
    o.stores = {{m::MacImportStore::notes, m::MacImportStatus::importedWithWarnings, 4, 1}, {m::MacImportStore::archive, m::MacImportStatus::rejectedInvalid, 0, 0}};
    o.relinkItems = 3; o.warnings = {"kept \xE6\xA1\xA3"}; o.profileSyncLocked = true; o.launchAtLogin = false; o.customShortcutUntranslated = true;
    o.remindersNeedReconcile = true; o.replacesPreviousImport = true; o.finishedAt = 1760000000.25;
    const auto bytes = m::encodeMacImportOutcome(o);
    check(m::decodeMacImportOutcome(bytes) == o, "The outcome note round-trips exactly");
    m::MacImportOutcome failure = m::failedMacImportOutcome(m::MacImportOutcome::State::failed, m::MacImportErrorCode::conflict, "busy", nullptr, 5);
    check(m::decodeMacImportOutcome(m::encodeMacImportOutcome(failure)) == failure && failure.stores.empty(), "A failure without staging round-trips");
    auto newer = J::parse(bytes); newer["version"] = 2;
    try { (void)m::decodeMacImportOutcome(newer.encode()); check(false, "A newer note is rejected"); }
    catch (const m::MacImportError& e) { check(e.code() == m::MacImportErrorCode::newerVersion, "A newer outcome note reports newerVersion"); }
    for (const auto* broken : {"{}", "[]", "not json", R"({"format":"EndfieldHUD.macImportOutcome","version":1})"})
        rejects([&] { (void)m::decodeMacImportOutcome(broken); }, "A malformed outcome note is rejected");
    auto duplicate = J::parse(bytes); auto rows = duplicate["stores"].array(); rows.push_back(rows[0]); duplicate["stores"] = rows;
    rejects([&] { (void)m::decodeMacImportOutcome(duplicate.encode()); }, "Duplicate store rows are rejected");
    rejects([&] { (void)m::macImportOutcomePath("relative/v1"); }, "The outcome path needs an absolute root");
    Temporary t;
    const auto root = t.root / "v1";
    check(m::macImportOutcomePath(root) == t.root / "v1.mac-import-outcome.json", "The note sits next to the data root");
    check(!m::readMacImportOutcome(root), "No note: nothing to report");
    m::writeMacImportOutcome(root, o);
    m::writeMacImportOutcome(root, failure); // replaces the earlier note atomically
    check(m::readMacImportOutcome(root) == failure && !fs::exists(root), "The newest note wins and the root is never created");
    std::size_t files{};
    for (const auto& entry : fs::directory_iterator(t.root)) { (void)entry; ++files; }
    check(files == 1, "Writing the note leaves no temporary file behind");
    check(m::removeMacImportOutcome(root) && !fs::exists(m::macImportOutcomePath(root)) && m::removeMacImportOutcome(root), "Removing the note is idempotent");
}

void cardState() {
    a::MacImportCardState s;
    check(!s.key(0x0D, false, 0), "A hidden card consumes nothing");
    s.show(1);
    check(s.presented() && s.focused() == 0 && s.showings() == 1 && s.opacity(1) == 1 && !s.requiresFrames(1), "Shown at rest with Cancel focused");
    check(*s.key(0x0D, false, 2) == a::MacImportCardAction::left, "Return activates the focused Cancel");
    check(*s.key(0x09, false, 2) == a::MacImportCardAction::none && s.focused() == 1 && *s.key(0x20, false, 2) == a::MacImportCardAction::right, "Tab moves to the confirm button");
    check(*s.key(0x0D, true, 2) == a::MacImportCardAction::none, "Repeated Return never confirms");
    check(*s.key(0x28, true, 2) == a::MacImportCardAction::scrollDown && *s.key(0x26, false, 2) == a::MacImportCardAction::scrollUp &&
          *s.key(0x22, false, 2) == a::MacImportCardAction::pageDown && *s.key(0x21, false, 2) == a::MacImportCardAction::pageUp, "Arrow and page keys scroll, repeats allowed");
    check(*s.key(0x1B, false, 2) == a::MacImportCardAction::escape, "Escape is reported for the flow to decide");
    s.setEnabled({true, false}, 3);
    check(s.focused() == 0, "Disabling the focused confirm button moves focus to Cancel");
    check(*s.key(0x27, false, 3) == a::MacImportCardAction::none && s.focused() == 0 && !s.focus(1, 3), "A disabled button cannot take focus");
    s.setEnabled({true, true}, 6);
    check(s.focus(1, 6) && s.focused() == 1, "A press or SetFocus moves the focus");
    s.resetFocus(6.5);
    check(s.focused() == 0, "A new step returns focus to Cancel");
    s.dismiss(true, 7);
    check(s.presented() && s.dismissing() && *s.key(0x0D, false, 7.05) == a::MacImportCardAction::none && !s.focus(1, 7.05), "Input stays consumed during the .14 s fade");
    check(std::abs(s.opacity(7.07) - .5) < 1e-9 && s.requiresFrames(7.1), "The fade is numeric");
    check(!s.refresh(7.1) && s.refresh(7.14) && !s.presented(), "The fade completes after .14 s");
    s.show(7.5);
    check(s.showings() == 2, "Each showing from hidden plays the entrance");
    s.dismiss(true, 7.6); s.show(7.65);
    check(s.showings() == 2 && s.opacity(7.65) == 1, "Re-showing a fading card restores it without a new entrance");
    s.setReduceMotion(true, 8);
    s.dismiss(true, 9);
    check(!s.presented(), "Reduce Motion hides at once");
}

// Character-count widths: only text emission and layout need it here.
struct Measurer final : endfield::modules::design::TextMeasurer {
    double width(std::span<const endfield::modules::design::TextRun> runs, endfield::modules::design::TextRegion) override {
        double w{}; for (const auto& r : runs) w += double(r.text.size()) * r.style.size * .5; return std::ceil(w * 2) / 2; }
    endfield::modules::design::LineMetrics metrics(const endfield::modules::design::TextStyle& s, endfield::modules::design::TextRegion, double scale) override {
        return {s.size * s.lineHeight * scale, .928 * s.size * scale, .244 * s.size * scale}; }
};

void cardPanel() {
    namespace d = endfield::modules::design;
    Measurer measurer;
    d::Context context(measurer, [](std::string_view t) { std::string u(t); for (auto& c : u) if (c >= 'a' && c <= 'z') c = char(c - 32); return u; });
    a::MacImportPanel panel(context.env());
    a::MacImportCardContent content;
    content.title = "Review import"; content.message = "Ready."; content.buttons = {"Cancel", "Import"}; content.position = "1 / 12";
    for (std::size_t n = 0; n < a::macImportCardRows; ++n) content.rows[n] = {"Row " + std::to_string(n), "Ready", a::MacImportTone::primary};
    panel.setContent(content);
    const auto g = panel.geometry();
    const auto same = [](endfield::core::Rect x, endfield::core::Rect y) { return x.x == y.x && x.y == y.y && x.width == y.width && x.height == y.height; };
    check(same(g.card, {60, 39, a::macImportCardWidth, a::macImportCardHeight}) && same(panel.root().frame(), d::metric::box), "The panel is centred in the content box");
    check(g.confirm.x + g.confirm.width == g.card.width - 22 && g.cancel.x + g.cancel.width + 10 == g.confirm.x && g.cancel.y == g.confirm.y &&
          g.confirm.y + g.confirm.height + 16 == g.card.height, "Primary right, secondary before it, 22 / 16 px insets");
    check(g.rowTop + g.rowHeight * double(a::macImportCardRows) <= g.list.y + g.list.height && g.list.y + g.list.height < g.cancel.y &&
          g.bar.y + g.bar.height <= g.list.y && g.message.y + g.message.height <= g.bar.y, "Header, bar, list and buttons do not overlap");
    const auto revision = panel.root().arena().revision();
    panel.setContent(content);
    check(panel.root().arena().revision() == revision, "Equal content changes nothing");
    a::MacImportCardState state;
    state.show(0);
    content.progress = .5;
    panel.setContent(content);
    panel.pose(state, 0);
    const auto progressRevision = panel.root().arena().revision();
    content.progress = .75;
    panel.setContent(content);
    panel.pose(state, 0);
    check(panel.root().arena().revision() == progressRevision, "Progress moves only pose numbers (no re-raster)");
    // The panel's layers by id (arena prefix "macImport").
    std::function<const d::Layer*(const d::Layer&, std::string_view)> find = [&](const d::Layer& l, std::string_view id) -> const d::Layer* {
        if (l.id() == id) return &l;
        for (const auto* child : l.children()) if (const auto* f = find(*child, id)) return f;
        return nullptr;
    };
    const auto* fill = find(panel.root(), "macImport.fill");
    const auto* ring = find(panel.root(), "macImport.cancel.focus");
    const auto* caption = find(panel.root(), "macImport.caption");
    check(fill && std::abs(fill->value(d::Channel::scaleX) - .75) < 1e-9 && fill->value(d::Channel::opacity) == 1, "The fill scales with the progress");
    check(ring && ring->value(d::Channel::opacity) == 1 && find(panel.root(), "macImport.confirm.focus")->value(d::Channel::opacity) == 0, "The focus ring marks the focused button");
    check(caption && d::textOf(*caption).text() == "ENDFIELDHUD / SYSTEM", "The system caption");
    content.detail = "The folder is not a complete export.";
    panel.setContent(content);
    const auto* detail = find(panel.root(), "macImport.detail");
    check(detail && !detail->hidden() && find(panel.root(), "macImport.row0")->hidden(), "A detail text replaces the list rows");
    content.enabled = {true, false};
    panel.setContent(content);
    int activated = -1;
    panel.onActivate = [&](int n) { activated = n; };
    const auto confirm = panel.geometry().confirm;
    const endfield::core::Point at{g.card.x + confirm.x + confirm.width / 2, g.card.y + confirm.y + confirm.height / 2};
    (void)panel.router().pointerDown(at, panel.root()); panel.router().pointerUp(at, panel.root());
    check(activated == -1, "A disabled button cannot be clicked");
    const auto cancel = panel.geometry().cancel;
    const endfield::core::Point left{g.card.x + cancel.x + cancel.width / 2, g.card.y + cancel.y + cancel.height / 2};
    check(panel.router().pointerDown(left, panel.root()), "The cancel button takes the press");
    panel.router().pointerUp(left, panel.root());
    check(activated == 0, "Clicking Cancel answers left");
}
void introAndChoice() {
    Temporary t;
    const auto destination = t.root / "EndfieldHUD" / "v1";
    fs::create_directories(destination.parent_path());
    Harness h(destination);
    check(!h.flow->presented() && h.flow->step() == Step::hidden, "The flow starts hidden");
    h.flow->open();
    check(h.flow->step() == Step::intro && h.c().buttons[1] == "Choose Folder\xE2\x80\xA6" && h.c().enabled == std::array<bool, 2>{true, true} &&
          h.c().rows[0].label == "Settings" && h.c().rows[11].label == "Center logo" && h.c().position.empty(), "Intro lists the twelve data sets");
    h.flow->activate(0);
    check(h.flow->step() == Step::hidden, "Cancel closes the intro");
    h.flow->open();
    h.flow->activate(1);
    check(h.flow->step() == Step::choosing && h.chooserTitle == "Choose the exported macOS data folder", "The dialog gets the localized title");
    h.flow->activate(0);
    check(h.cancelledChoices == 1 && h.flow->step() == Step::choosing, "Cancel ends the dialog; its completion returns");
    h.flow->folderChosen(true, false, {});
    check(h.flow->step() == Step::intro && h.c().detail.empty(), "A cancelled dialog returns to the intro");
    h.flow->activate(1);
    h.flow->folderChosen(false, false, {"C:\\a", "C:\\b"});
    check(h.flow->step() == Step::intro && h.c().detail == "Choose one exported folder." && h.c().rows[0].label.empty(), "Two folders are refused with a notice");
    h.flow->activate(1);
    h.flow->folderChosen(false, true, {});
    check(h.c().detail == "The file picker is unavailable.", "A failed dialog is reported");
    h.flow->setLanguage(L::japanese);
    check(h.c().title == "macOS \xE3\x81\xAE\xE3\x83\x87\xE3\x83\xBC\xE3\x82\xBF\xE3\x82\x92\xE8\xAA\xAD\xE3\x81\xBF\xE8\xBE\xBC\xE3\x81\xBF\xE3\x81\xBE\xE3\x81\x99\xE3\x81\x8B\xEF\xBC\x9F",
          "A language change re-renders the card");
    h.flow->setLanguage(L::english);
    // Choosing the manifest file means its folder; a missing export fails.
    h.flow->activate(1);
    h.flow->folderChosen(false, false, {utf8(t.root / "missing" / "manifest.json")});
    check(h.flow->exportRoot() == t.root / "missing" && h.flow->step() == Step::staging, "The manifest's folder is the export root");
    h.settle();
    check(h.flow->step() == Step::failed && h.c().title == "Import failed" && contains(h.c().message, "Your data was not changed.") && !h.c().detail.empty(),
          "A missing export fails with the importer's detail: " + h.c().message);
    check(!fs::exists(destination) && siblingsClean(destination), "A failed staging writes nothing");
    h.flow->activate(1);
    check(h.flow->step() == Step::choosing, "Choose Folder from the failure reopens the dialog");
    h.flow->folderChosen(true, false, {});
    h.flow->activate(0);
    check(h.flow->step() == Step::hidden, "Cancel hides");
}

void inProcessImport() {
    Temporary t;
    auto e = fullExport(t.root / "export");
    e.write();
    const auto destination = t.root / "EndfieldHUD" / "v1";
    fs::create_directories(destination.parent_path());
    Harness h(destination);
    h.flow->open();
    h.choose(e.root);
    check(h.flow->step() == Step::staging && h.c().enabled == std::array<bool, 2>{true, false} && h.c().progress && h.c().title == "Checking export\xE2\x80\xA6",
          "Staging shows progress and keeps Import disabled");
    std::optional<double> lastProgress;
    bool sawValidating = false;
    for (int guard = 0; guard < 400 && h.flow->step() == Step::staging; ++guard) {
        h.executor.waitIdle(); h.executor.drain();
        if (h.flow->step() != Step::staging) break;
        check(h.c().progress && (!lastProgress || *h.c().progress >= *lastProgress), "Progress never moves backwards");
        lastProgress = h.c().progress;
        sawValidating = sawValidating || contains(h.c().message, "Checking data sets");
    }
    check(sawValidating, "Validation progress names the data sets");
    check(h.flow->step() == Step::review && h.c().title == "Review import", "Staging ends in the review");
    const auto& rows = h.c().rows;
    check(rows[0].label == "Settings" && rows[2].label == "Archive" && rows[2].value == "Unreadable \xC2\xB7 skipped" && rows[2].tone == a::MacImportTone::alert &&
          rows[8].value == "Not in export" && rows[8].tone == a::MacImportTone::secondary && rows[1].value == "Ready \xC2\xB7 1 to relink",
          "Per data set validation report: " + rows[2].value + " / " + rows[1].value);
    check(contains(h.c().message, "10 of 12 data sets are ready.") && contains(h.c().message, "1 cannot be imported and will be skipped.") &&
          contains(h.c().message, "Current data is kept as a backup.") && h.c().buttons[1] == "Skip and Import" && h.c().enabled[1],
          "Review explains the skipped store: " + h.c().message);
    h.flow->activate(1);
    check(h.flow->step() == Step::committing && h.c().title == "Importing\xE2\x80\xA6", "Explicit consent skips the rejected store and commits");
    h.settle();
    check(h.flow->step() == Step::report && h.c().title == "Import complete" && contains(h.c().message, "Imported 10 data sets from EndfieldHUD 1.2.0 for Mac.") &&
          contains(h.c().message, "3 file references need relinking") && h.c().buttons[0] == "Relink List (3)" && h.c().enabled[0],
          "Final report: " + h.c().message);
    check(fs::exists(destination / "Migration" / "import.json") && !fs::exists(destination / "Archive") && siblingsClean(destination) && backups(destination) == 0,
          "The commit activated the new root without a backup (none existed)");
    check(h.flow->outcome() && h.flow->outcome()->state == m::MacImportOutcome::State::completed && !h.flow->outcomeRecorded() &&
          !fs::exists(m::macImportOutcomePath(destination)), "An in-process commit reports directly (no note)");
    // Report rows: Calendar reminders follow-up, then the stores.
    check(h.c().rows[0].label == "Account Linking" && h.c().rows[0].value == "Sign in again" && h.c().rows[1].label == "Calendar" &&
          h.c().rows[1].value == "Reminders rescheduled" && h.flow->listSize() == 14 && h.c().position == "1\xE2\x80\x93" "12 / 14", "Report follow-ups precede the stores");
    check(h.flow->scroll(5) && h.flow->firstRow() == 2 && h.c().position == "3\xE2\x80\x93" "14 / 14" && !h.flow->scroll(1), "The list scrolls and clamps");
    h.flow->escape();
    check(h.flow->step() == Step::hidden && h.forgotten == 0, "Escape on the report is Done");
    // Relink list through the report.
    h.flow->open();
    check(h.flow->step() == Step::intro, "Reopening after Done starts a new import");
    h.flow->activate(0);
}

void restartHandoff() {
    Temporary t;
    auto e = fullExport(t.root / "export");
    e.write();
    const auto destination = t.root / "EndfieldHUD" / "v1";
    write(destination / "WindowsOnly" / "keep.txt", "keep");
    m::MacImportOptions consent; consent.acceptRejected = {m::MacImportStore::archive};
    {   // An earlier import (over Windows data: first backup) exists; the flow
        // asks before replacing it.
        m::MacImportSession earlier(e.root, destination, platform(), consent);
        earlier.stage();
        earlier.commit();
    }
    const auto before = tree(destination);
    Harness h(destination);
    h.inProcess = false;
    // A folder that is not an export is reported as such, even over an import.
    fs::create_directories(t.root / "not-an-export");
    h.flow->open();
    h.choose(t.root / "not-an-export");
    h.settle();
    check(h.flow->step() == Step::failed && contains(h.c().message, "not a complete EndfieldHUD Mac export") && contains(h.c().detail, "manifest.json") &&
          tree(destination) == before, "A folder without a manifest is not an export: " + h.c().message);
    h.flow->activate(0);
    h.flow->open();
    h.choose(e.root);
    h.settle();
    check(h.flow->step() == Step::replace && h.c().buttons[1] == "Import Again" && tree(destination) == before, "An earlier import asks for explicit replacement");
    h.flow->activate(1);
    h.settle();
    check(h.flow->step() == Step::review && h.c().buttons[1] == "Skip and Import" && contains(h.c().message, "EndfieldHUD will save, close, import and reopen"),
          "Production review explains the restart: " + h.c().message);
    h.restartAvailable = false;
    h.flow->setLanguage(L::korean); h.flow->setLanguage(L::english); // rebuild
    check(!h.c().enabled[1] && contains(h.c().message, "This build cannot restart"), "Without the host restart the import cannot be confirmed");
    h.restartAvailable = true;
    h.restartAccepts = false;
    h.flow->setLanguage(L::korean); h.flow->setLanguage(L::english);
    h.flow->activate(1);
    check(h.flow->step() == Step::review && !h.restarted && contains(h.c().message, "could not save everything"), "A refused restart keeps the reviewed import");
    h.restartAccepts = true;
    h.flow->activate(1);
    check(h.flow->step() == Step::restarting && h.restarted && h.c().enabled == std::array<bool, 2>{false, false} && tree(destination) == before,
          "Confirming hands the reviewed session to the restart; nothing is written yet");
    // The host's save failed: the restart was declined, the import stays.
    h.flow->restartDeclined();
    check(h.flow->step() == Step::review && contains(h.c().message, "could not save everything") && h.c().enabled[1], "A declined restart returns to the review");
    h.flow->activate(1);
    check(h.flow->step() == Step::restarting, "The review can be confirmed again");
    // The owner is torn down; the host runs the work after every store closed.
    auto session = h.restarted;
    h.restarted.reset();
    h.flow.reset();
    h.executor.waitIdle(); h.executor.drain();
    check(!siblingsClean(destination), "The handed-off session keeps its private folder across the teardown");
    const auto outcome = m::commitMacImportSession(*session, destination, 1760000100);
    session.reset();
    check(outcome.state == m::MacImportOutcome::State::completed && !outcome.backup.empty() && fs::exists(utf8Path(outcome.backup)) && backups(destination) == 2 &&
          siblingsClean(destination), "The post-shutdown commit completes with a backup: " + std::string(m::macImportOutcomeStateName(outcome.state)) + " " + outcome.error);
    check(read(destination / "WindowsOnly" / "keep.txt") == "keep", "Windows-only data is carried");
    check(m::readMacImportOutcome(destination) == outcome, "The outcome is recorded next to the root");
    // The next run shows the recorded outcome once.
    Harness next(destination);
    m::MacRelinkLedger ledger(destination);
    next.flow->showOutcome(*m::readMacImportOutcome(destination), ledger.unresolved());
    check(next.flow->step() == Step::report && next.flow->outcomeRecorded() && next.c().rows[0].label == "Previous data" &&
          next.c().rows[0].value == utf8(utf8Path(outcome.backup).filename()), "The next run reports the backup folder");
    next.flow->activate(0);
    check(next.flow->step() == Step::relink && next.c().title == "Needs relinking" && next.c().buttons[0] == "Back" &&
          next.flow->listSize() == ledger.unresolved().size() && !next.c().rows[0].label.empty(), "The relink list names every unresolved reference");
    std::set<std::string> modules;
    for (std::size_t n = 0; n < next.flow->listSize() && n < a::macImportCardRows; ++n) modules.insert(next.c().rows[n].value);
    check(modules.contains("Notes") && modules.contains("E-Reader") && modules.contains("Temporary File Shelf"), "Each reference names its module");
    next.flow->activate(0);
    check(next.flow->step() == Step::report, "Back returns to the report");
    next.flow->activate(1);
    check(next.flow->step() == Step::hidden && next.forgotten == 1 && !fs::exists(m::macImportOutcomePath(destination)), "Done forgets the recorded outcome");
}

void declinedDiscard() {
    Temporary t;
    auto e = fullExport(t.root / "export");
    e.write();
    const auto destination = t.root / "EndfieldHUD" / "v1";
    fs::create_directories(destination.parent_path());
    Harness h(destination);
    h.inProcess = false;
    h.flow->open();
    h.choose(e.root);
    h.settle();
    h.flow->activate(1);
    check(h.flow->step() == Step::restarting && h.restarted, "Restart requested");
    h.flow->restartDeclined();
    h.restarted.reset(); // the host dropped its unrun work
    h.flow->activate(0);
    check(h.flow->step() == Step::cancelling && h.c().enabled == std::array<bool, 2>{false, false}, "Cancelling a declined import discards it on the worker");
    h.settle();
    check(h.flow->step() == Step::hidden && siblingsClean(destination) && !fs::exists(destination), "The discarded import leaves nothing behind");
    // A new import can start right away.
    h.flow->open();
    h.choose(e.root);
    check(h.flow->step() == Step::staging, "A new import starts after the discard");
    h.flow->activate(0);
    check(h.flow->step() == Step::cancelling, "Cancel during staging");
    h.settle();
    check(h.flow->step() == Step::hidden && siblingsClean(destination) && !fs::exists(destination), "A cancelled staging leaves nothing behind");
    // Cancel from the review discards the staged copy too.
    h.flow->open();
    h.choose(e.root);
    h.settle();
    check(h.flow->step() == Step::review, "Staged again");
    h.flow->activate(0);
    h.settle();
    check(h.flow->step() == Step::hidden && siblingsClean(destination), "Cancel in the review removes the private folder");
}

void failedOutcome() {
    Temporary t;
    auto e = fullExport(t.root / "export");
    e.write();
    const auto destination = t.root / "EndfieldHUD" / "v1";
    write(destination / "Notes" / "keep.txt", "windows");
    const auto before = tree(destination);
    m::MacImportOptions options; options.acceptRejected = {m::MacImportStore::archive};
    options.beforeCommitStep = [](m::MacImportCommitStep step) { if (step == m::MacImportCommitStep::activateStage) throw m::MacImportError(m::MacImportErrorCode::conflict, "Injected rename conflict"); };
    m::MacImportSession session(e.root, destination, platform(), options);
    session.stage();
    const auto outcome = m::commitMacImportSession(session, destination, 7);
    check(outcome.state == m::MacImportOutcome::State::failed && outcome.code == m::MacImportErrorCode::conflict && outcome.error == "Injected rename conflict" &&
          outcome.stores.size() == m::macImportStores.size(), "A failed post-shutdown commit is recorded with its statuses");
    check(tree(destination) == before && backups(destination) == 0 && siblingsClean(destination), "The failed commit rolled the root back");
    Harness h(destination);
    h.flow->showOutcome(*m::readMacImportOutcome(destination), {});
    check(h.flow->step() == Step::failed && h.c().title == "Import failed" && contains(h.c().message, "Some files were in use or changed.") &&
          h.c().detail == "Injected rename conflict", "The next run explains the failure");
    h.flow->activate(0);
    check(h.flow->step() == Step::hidden && h.forgotten == 1 && !fs::exists(m::macImportOutcomePath(destination)), "Closing forgets the failure note");
    // A cancelled outcome has no detail and no error category.
    h.flow->showOutcome(m::failedMacImportOutcome(m::MacImportOutcome::State::cancelled, m::MacImportErrorCode::cancelled, "Import cancelled", nullptr, 8), {});
    check(h.c().title == "Import cancelled" && h.c().message == "Your data was not changed." && h.c().detail.empty(), "A cancelled outcome is reported plainly");
    h.flow->activate(0);
    // Every language renders every step without falling back to English.
    for (const auto l : {L::simplifiedChinese, L::traditionalChinese, L::japanese, L::korean}) {
        h.flow->setLanguage(l);
        h.flow->showOutcome(outcome, {});
        check(h.c().title != "Import failed" && !h.c().message.empty() && h.c().buttons[1] != "Choose Folder\xE2\x80\xA6", "Localized failure card");
        h.flow->activate(0);
        h.flow->open();
        check(h.c().title != "Import macOS data?" && h.c().buttons[0] != "Cancel", "Localized intro");
        h.flow->activate(0);
    }
}
}

int main() {
    try {
        strings();
        outcomeCodec();
        cardState();
        cardPanel();
        introAndChoice();
        inProcessImport();
        restartHandoff();
        declinedDiscard();
        failedOutcome();
        std::cout << "PASS " << checks << " macOS import flow checks; temporary synthetic exports only\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL after " << checks << ": " << error.what() << '\n';
        return 1;
    }
}
