// Contracts of the redesigned File Shelf canvas (modules/file_shelf_canvas.*):
// the rules of Sources/FileShelfCanvas.swift (each check cites the Swift line
// it ports) over a synthetic in-memory store, and, when the Mac oracle is
// given (tests/fixtures/file-shelf-source.json from
// tools/file_shelf_reference.sh), every recorded scene node by node with the
// Mac's own text widths, its controls and accessibility items, the Mac's
// selection-rule sequence replayed step by step, and the formatting functions
// and UTType kinds. Synthetic data only: no file is touched.
#include "modules/file_shelf_canvas.hpp"
#include "tests/hud_design_oracle.hpp"
#include <iostream>
#include <map>
#include <stdexcept>

namespace d = endfield::modules::design;
namespace m = endfield::modules;
using endfield::core::Language;
using namespace endfield::tests::design_oracle;
namespace {
unsigned checks{};
void check(bool v, const std::string& what) { ++checks; if (!v) throw std::runtime_error(what); }
d::KeyInput keyOf(std::uint32_t vk, char32_t c = 0, bool shift = false, bool control = false) {
    d::KeyInput k; k.virtualKey = vk; k.character = c; k.shift = shift; k.control = control; return k;
}
d::Control* control(d::Canvas& canvas, std::string_view id) {
    for (d::Control* c : canvas.page().controls()) if (c->id() == id) return c;
    return nullptr;
}
const d::Layer* findLayer(const d::Layer& layer, std::string_view suffix) {
    if (layer.id().ends_with(suffix)) return &layer;
    for (const auto* child : layer.children()) if (const auto* hit = findLayer(*child, suffix)) return hit;
    return nullptr;
}
bool hasText(const d::Layer& layer, std::string_view text) {
    if (const auto* block = layer.text(); block && block->text() == text) return true;
    for (const auto* child : layer.children()) if (hasText(*child, text)) return true;
    return false;
}
d::Point centre(const d::Control& c, const d::Layer& root) { const auto r = c.rect(root); return {r.x + r.width / 2, r.y + r.height / 2}; }

struct World {
    d::TextMeasurer& measurer;
    d::Context context;
    double now{100};
    explicit World(d::TextMeasurer& m) : measurer(m), context(m, d::asciiUppercase) { context.update(d::geometry::baseUnit, 2 / d::geometry::baseUnit, now); }
    void appearance(Language language, bool light = false, bool reduce = true, bool recording = false) {
        d::Context::Appearance a; a.language = language; a.light = light; a.reduceMotion = reduce; a.recordingMode = recording;
        context.apply(a, now);
    }
    void advance(double seconds) { now += seconds; context.advance(now); }
    void show(d::Canvas& c) { c.show(now); c.setActive(true, now); }
    std::string toast() const {
        const auto& t = const_cast<d::Context&>(context).overlay().toastState().current();
        return t ? t->title + "|" + t->caption : std::string();
    }
};

// An in-memory reference store with the service hooks recorded.
struct Store {
    std::vector<m::FileShelfItem> items;
    std::vector<std::string> added, removed, opened, revealed, relinked, drags, icons;
    std::vector<std::size_t> cleared;
    std::vector<std::string> reads;
    unsigned chooses{}, pastes{}, deactivations{}, next{1};
    bool openOK{true}, failAdd{};
    static m::FileShelfItem file(std::string id, std::string name, std::optional<std::int64_t> bytes = 1234, bool directory = false) {
        m::FileShelfItem i; i.id = std::move(id); i.name = name; i.lastKnownPath = "C:\\Users\\Op\\Documents\\" + name; i.typeDescription = "Document";
        i.byteCount = directory ? std::nullopt : bytes; i.isDirectory = directory; return i;
    }
    m::FileShelfServices services() {
        m::FileShelfServices s;
        s.items = [this] { return items; };
        s.refresh = [] {};
        s.add = [this](std::span<const std::string> paths) {
            if (failAdd) throw std::runtime_error("Synthetic add failure");
            for (const auto& p : paths) {
                if (std::any_of(items.begin(), items.end(), [&](const auto& i) { return i.lastKnownPath == p; })) continue;
                auto i = file("new-" + std::to_string(next++), p.substr(p.find_last_of('\\') + 1)); i.lastKnownPath = p; items.push_back(i);
            }
        };
        s.remove = [this](std::string_view id) { std::erase_if(items, [&](const auto& i) { return i.id == id; }); };
        s.clear = [this] { items.clear(); };
        s.describe = [](const std::exception& e, m::FileShelfServices::Failure) { return std::string("Described: ") + e.what(); };
        s.chooseFiles = [this] { ++chooses; };
        s.deactivate = [this] { ++deactivations; };
        s.open = [this](std::string_view id) { opened.emplace_back(id); return openOK; };
        s.reveal = [this](std::string_view id) { revealed.emplace_back(id); return true; };
        s.relink = [this](std::string_view id) { relinked.emplace_back(id); };
        s.beginDrag = [this](std::vector<std::string> ids, std::string primary) { std::string row = primary + ":"; for (const auto& i : ids) row += i + ","; drags.push_back(row); };
        s.paste = [this] { ++pastes; return true; };
        s.readProperties = [this](const m::FileShelfItem& i) { reads.push_back(i.id); return true; };
        s.itemsAdded = [this](const std::vector<std::string>& names) { for (const auto& n : names) added.push_back(n); };
        s.itemRemoved = [this](const std::string& name) { removed.push_back(name); };
        s.cleared = [this](std::size_t n) { cleared.push_back(n); };
        s.iconsWanted = [this](const std::vector<std::string>& ids) { icons = ids; };
        s.homeFolder = "C:\\Users\\Op";
        return s;
    }
};
std::set<std::string, std::less<>> ids(std::initializer_list<const char*> list) { std::set<std::string, std::less<>> out; for (const auto* i : list) out.insert(i); return out; }

void formats() {
    using C = m::FileShelfCanvas;
    // tileKind / extensionLabel / kindText (:682-712) on Windows paths.
    auto pdf = Store::file("1", "Report.PDF");
    check(C::tileKind(pdf) == C::Extension::pdf && C::extensionLabel(pdf) == "PDF" && C::kindText(pdf, Language::english) == "PDF file"
        && C::kindText(pdf, Language::simplifiedChinese) == "PDF 文件", "A file: its upper-cased extension, '<EXT> file' (:699-711)");
    auto folder = Store::file("2", "Talos.app", {}, true);
    check(C::tileKind(folder) == C::Extension::txt && C::extensionLabel(folder) == "DIR" && C::kindText(folder, Language::english) == "Folder",
        "Windows has no packages: a folder with an extension is a folder (DIR, Folder)");
    auto plain = Store::file("3", "Readme"); plain.typeDescription = "";
    check(C::extensionLabel(plain) == "FILE" && C::kindText(plain, Language::english) == "File" && C::kindText(plain, Language::japanese) == "ファイル",
        "No extension and no type: FILE / File");
    plain.typeDescription = "Text Document"; check(C::kindText(plain, Language::english) == "Text Document", "No extension: the system's type name");
    for (const auto& [name, kind] : std::vector<std::pair<std::string, C::Extension>>{{"a.JPG", C::Extension::img}, {"b.mp3", C::Extension::vid},
            {"c.tar.gz", C::Extension::zip}, {"d.iso", C::Extension::zip}, {"e.docx", C::Extension::txt}, {"f.mkv", C::Extension::txt}, {".bashrc", C::Extension::txt}})
        check(C::tileKind(Store::file("x", name)) == kind, "UTType kind of " + name);
    check(C::extensionOf("C:\\a\\.bashrc").empty() && C::extensionOf("C:\\a\\trailing.").empty() && C::extensionOf("/a/b.c/") == "c" && C::extensionOf("x.tar.gz") == "gz",
        "URL.pathExtension: a leading dot or a trailing one has none");
    // location (:715-717): the containing folder, the user profile as "~".
    auto in = Store::file("4", "x.txt"); in.lastKnownPath = "C:\\Users\\Op\\Documents\\Talos\\x.txt";
    check(C::location(in, "C:\\Users\\Op") == "~\\Documents\\Talos" && C::location(in, "c:\\users\\op\\") == "~\\Documents\\Talos", "WHERE abbreviates the profile folder");
    in.lastKnownPath = "C:\\Users\\Op\\x.txt"; check(C::location(in, "C:\\Users\\Op") == "~", "A file in the profile folder is in ~");
    in.lastKnownPath = "C:\\Users\\Operator\\x.txt"; check(C::location(in, "C:\\Users\\Op") == "C:\\Users\\Operator", "Only whole folders abbreviate");
    in.lastKnownPath = "C:\\x.txt"; check(C::location(in, "C:\\Users\\Op") == "C:\\", "A file at a drive root is in that root");
    in.lastKnownPath = "/Users/doctor/Desktop/Imported plan.key"; check(C::location(in, "C:\\Users\\Op") == "/Users/doctor/Desktop", "A Mac-imported path keeps its folder");
    // sizeTag (:719-721) / ShelfItem.sizeLabel.
    auto gone = Store::file("5", "gone.png", 10); gone.availabilityError = "missing";
    check(C::sizeTag(gone, Language::english) == std::optional<std::string>("Unavailable") && C::sizeTag(gone, Language::simplifiedChinese) == std::optional<std::string>("不可用"),
        "An unavailable reference shows Unavailable");
    check(C::sizeTag(Store::file("6", "a.bin", 2'412'345), Language::english) == std::optional<std::string>("2.4 MB") && !C::sizeTag(folder, Language::english),
        "A file shows ByteCountFormatter .file; a folder no tag");
    check(m::shelfText(m::ShelfText::revealInExplorer, Language::english) == "Reveal in File Explorer" && m::shelfText(m::ShelfText::revealedInExplorer, Language::korean) == "파일 탐색기에서 표시됨",
        "Windows names File Explorer");
    for (unsigned n = 0; n <= unsigned(m::ShelfText::newerVersion); ++n)
        for (auto language : {Language::english, Language::simplifiedChinese, Language::traditionalChinese, Language::japanese, Language::korean})
            check(!m::shelfText(m::ShelfText(n), language).empty(), "Every Windows-only Shelf text exists in every language");
    // GridLayout (:197-222).
    const C::GridLayout four(590, 4, 14), three(392, 3, 14), fits(590, 4, 8);
    check(four.contentHeight == 10 + 4 * 124 + 3 * 12 && four.height == 420 && four.tileWidth == std::floor((590 - 10 - 4 - 36) / 4.0), "4 columns; the scroll bar column only while it scrolls");
    check(fits.height == fits.contentHeight && fits.tileWidth == std::floor((590 - 10 - 36) / 4.0), "A grid that fits reserves no bar column");
    check(three.tileWidth == 118 && three.frame(4).x == 5 + 130 && three.frame(4).y == 5 + 136, "3 columns beside the card; frames every tile + 12 / 136");
    const auto window = four.indices(0, 14);
    check(window.first == 0 && window.second == 14, "Visible rows and one either side are built");
    const C::GridLayout tall(590, 4, 100);
    const auto scrolled = tall.indices(136 * 10, 100);
    check(scrolled.first == 8 * 4 && scrolled.second == 15 * 4, "A scrolled grid builds rows first-1 ... last+1");
}

void canvasRules() {
    FakeMeasurer measurer;
    World w(measurer); w.appearance(Language::english);
    Store store;
    for (int n = 1; n <= 6; ++n) store.items.push_back(Store::file("id" + std::to_string(n), "File " + std::to_string(n) + ".txt", 100 * n));
    store.items.push_back(Store::file("dir", "Operations", {}, true));
    m::FileShelfCanvas canvas(w.context, store.services(), std::string("Pending store error"));
    w.show(canvas);
    // Header (:154-163).
    check(canvas.header().counterText() == "07 FILES" && canvas.header().crumbTexts().back() == "FILE SHELF", "Counter pad2(count) FILES");
    check(canvas.header().titleTexts().first == "File Shelf", "Title File Shelf");
    check(w.toast() == "Notice|Pending store error", "An error before the first render toasts on activation (:109-112)");
    check(canvas.columnsForTesting() == 4 && canvas.builtTileCountForTesting() == 7 && !canvas.hasPropertiesCardForTesting(), "4 columns, every tile built, no card");
    auto* tile = control(canvas, "file:2");
    check(tile && tile->label == "File 2.txt, TXT file, 200 bytes", "Tile label: name, kind, size (:746)");
    std::vector<d::AccessibilityItem> items; canvas.accessibilityItems(items);
    check(std::any_of(items.begin(), items.end(), [](const auto& i) { return i.id == "file:2" && i.role == d::AccessibilityRole::tab && !i.on; }), "Tiles are tabs (:772)");
    // Selection rules (:392-403): pointer-up resolves the click.
    const auto press = [&](std::string_view tileID, bool shift = false, bool control = false, unsigned clicks = 1) {
        auto* c = ::control(canvas, tileID); const auto p = centre(*c, canvas.root());
        d::PointerInput input{p, d::PointerButton::left, shift, control, clicks};
        check(canvas.pointerDown(p, input, w.now), "A tile takes the press");
        canvas.pointerUp(p, w.now);
    };
    press("file:2");
    check(canvas.selectedIDs() == ids({"id2"}) && canvas.hasPropertiesCardForTesting() && canvas.columnsForTesting() == 3, "A click selects one tile; its card shows beside 3 columns");
    check(store.reads == std::vector<std::string>{"id2"}, "The card reads its properties once (:370-386)");
    check(hasText(canvas.root(), "—"), "Properties show — until they arrive (:303)");
    m::ShelfFileProperties props; props.path = store.items[1].lastKnownPath; props.created = m::ShelfLocalTime{2026, 10, 17, 21, 4}; props.modified = m::ShelfLocalTime{2026, 1, 2, 3, 4};
    canvas.propertiesRead("id2", props);
    check(hasText(canvas.root(), "2026.10.17 21:04") && hasText(canvas.root(), "2026.01.02 03:04"), "CREATED / MODIFIED: HUDFormat.dotted with the time, in place (:363)");
    check(hasText(canvas.root(), "~\\Documents") && hasText(canvas.root(), "TXT file") && hasText(canvas.root(), "200 bytes"), "KIND, SIZE and WHERE rows");
    {
        std::vector<d::AccessibilityItem> read; canvas.accessibilityItems(read);
        const auto row = [&](std::string_view id) { return std::find_if(read.begin(), read.end(), [&](const auto& i) { return i.id == id; }); };
        check(row("props.name") != read.end() && row("props.name")->label == "File 2.txt" && row("props.row0") != read.end() && row("props.row0")->label == "KIND"
            && row("props.row0")->value == "TXT file" && row("props.row3")->value == "2026.10.17 21:04" && row("props.row3")->role == d::AccessibilityRole::text,
            "The card's name and rows are UI Automation text (Windows addition)");
    }
    press("file:2");
    check(canvas.selectedIDs().empty() && !canvas.hasPropertiesCardForTesting() && canvas.columnsForTesting() == 4, "Clicking the only selected tile deselects it");
    press("file:2"); press("file:4", true);
    check(canvas.selectedIDs() == ids({"id2", "id4"}) && !canvas.hasPropertiesCardForTesting(), "Shift-click toggles into a multi-selection; no card");
    press("file:2", false, true);
    check(canvas.selectedIDs() == ids({"id4"}), "Ctrl-click toggles a tile off");
    press("file:5"); press("file:5", false, false, 2);
    check(canvas.selectedIDs() == ids({"id5"}), "A double-click keeps its tile selected");
    // Empty space (:405-413).
    check(canvas.pointerDown({129, 300}, {{129, 300}}, w.now) && canvas.selectedIDs().empty(), "A press between tiles clears the selection");
    check(!canvas.pointerDown({500, 300}, {{500, 300}}, w.now), "Nothing selected: the press is not taken");
    // Keys (:616-651).
    check(canvas.keyDown(keyOf(d::key::right), w.now) && canvas.selectedIDs() == ids({"id6"}) && canvas.focusID() == std::optional<std::string>("id6"),
        "Right moves from the focus, which a deselecting press keeps");
    check(canvas.keyDown(keyOf(d::key::down), w.now) && canvas.selectedIDs() == ids({"dir"}), "Down moves by the column count (3 beside the card), clamped to the last tile");
    check(canvas.keyDown(keyOf(d::key::up), w.now) && canvas.selectedIDs() == ids({"id4"}), "Up moves back by 3");
    check(canvas.keyDown(keyOf(d::key::right, 0, true), w.now) && canvas.selectedIDs() == ids({"id4", "id5"}), "Shift extends from the anchor");
    check(canvas.keyDown(keyOf(d::key::space, U' '), w.now) && canvas.selectedIDs() == ids({"id5"}), "Space selects the focused tile out of several");
    check(canvas.keyDown(keyOf(d::key::space, U' '), w.now) && canvas.selectedIDs().empty(), "Space closes the card");
    check(canvas.keyDown(keyOf(d::key::space, U' '), w.now) && canvas.selectedIDs() == ids({"id5"}), "Space opens the focused tile's card");
    check(canvas.keyDown(keyOf('O', U'o', false, true), w.now) && store.opened == std::vector<std::string>{"id5"} && w.toast() == "Opened|File 5.txt", "Ctrl+O opens the card's item, toasting its name (:515-518)");
    store.openOK = false; canvas.keyDown(keyOf('O', U'o', false, true), w.now); store.openOK = true;
    check(store.opened.size() == 2 && w.toast() == "Opened|File 5.txt", "A failed open adds no toast (the service reported why)");
    check(canvas.keyDown(keyOf('R', U'r', false, true), w.now) && store.revealed == std::vector<std::string>{"id5"} && w.toast() == "Revealed in File Explorer|~\\Documents",
        "Ctrl+R reveals, toasting the location (:520-523)");
    check(canvas.keyDown(keyOf('V', U'v', false, true), w.now) && store.pastes == 1, "Ctrl+V imports the clipboard's files");
    // Delete (:638): references only, one toast, the selection cleared (:526-542).
    canvas.click("id6", true);
    check(canvas.keyDown(keyOf(d::key::deleteForward), w.now) && store.items.size() == 5 && store.removed == std::vector<std::string>{"File 5.txt", "File 6.txt"},
        "Delete removes the selected references, each recorded");
    check(canvas.selectedIDs().empty() && w.toast() == "Removed from shelf|", "Removal clears the selection and toasts once, English caption empty");
    check(canvas.keyDown(keyOf(d::key::back), w.now) && store.items.size() == 5 && w.toast() == "Removed from shelf|", "Delete with nothing selected removes nothing");
    // The context menu (:662-678).
    check(canvas.handleRightPointerDown(centre(*control(canvas, "file:1"), canvas.root())) && canvas.menuOpen() && canvas.selectedIDs().empty(), "Right-click opens the tile menu without selecting");
    canvas.closeMenu();
    check(!canvas.handleRightPointerDown({560, 480}), "Right-click off the tiles opens nothing");
    // Imports (:466-484): no selection change, the names recorded, the grid at its bottom.
    canvas.click("id1");
    const std::vector<std::string> paths{"C:\\Users\\Op\\Desktop\\Added.png", store.items[0].lastKnownPath};
    check(canvas.importPaths(paths) && store.added == std::vector<std::string>{"Added.png"} && canvas.selectedIDs() == ids({"id1"}), "Adding files no longer selects them");
    check(canvas.importPaths(std::vector<std::string>{paths[1]}) && store.added.size() == 1, "Re-dropping a reference adds and records nothing");
    store.failAdd = true;
    check(!canvas.importPaths(paths) && w.toast() == "Notice|Described: Synthetic add failure", "A failed add toasts the described error (:478-482)");
    store.failAdd = false;
    // Drag selection (:447-450): unavailable references never leave.
    store.items[1].availabilityError = "missing"; canvas.storeChanged();
    canvas.click("id2", true);
    check(canvas.dragSelection("id1") == std::vector<std::string>{"id1"} && canvas.dragSelection("id3") == std::vector<std::string>{"id3"}, "An unavailable selected reference is never dragged");
    check(control(canvas, "file:2")->label == "File 2.txt, TXT file, Unavailable", "An unavailable tile says Unavailable");
    // A press that travels 5 px is a drag of the selection, never a click (:758-768).
    auto* first = control(canvas, "file:1"); const auto p = centre(*first, canvas.root());
    canvas.pointerDown(p, {p}, w.now); canvas.pointerDragged({p.x + 3, p.y}, w.now);
    check(store.drags.empty(), "Less than 5 px is no drag");
    canvas.pointerDragged({p.x + 6, p.y}, w.now); canvas.pointerUp({p.x + 6, p.y}, w.now);
    check(store.drags == std::vector<std::string>{"id1:id1,"} && canvas.selectedIDs() == ids({"id1", "id2"}), "The drag carries the available selection; the release clicks nothing");
    check(!store.icons.empty() && store.icons.front() == "id1", "The pressed tile's Shell icon is asked first");
    // Clear (:544-578) under Reduce Motion: confirm, then empty at once.
    canvas.confirmClear();
    const auto& request = w.context.overlay().confirmState().request();
    check(request && request->title == "Remove 6 files from the shelf?" && request->message == "Original files stay where they are." && request->confirmTitle == "Remove all" && request->danger,
        "Clear asks through the confirm bar");
    w.context.overlay().decide(d::ConfirmState::Decision::confirm); w.advance(.2);
    check(store.items.empty() && store.cleared == std::vector<std::size_t>{6} && w.toast() == "Shelf cleared|", "Confirming clears the references and toasts");
    check(canvas.header().counterText() == "00 FILES" && hasText(canvas.root(), "[ SHELF EMPTY ]"), "The empty state");
    check(store.chooses == 0 && (canvas.perform("shelf:add"), store.chooses == 1), "Add opens the chooser");
    // Leaving: the chooser is cancelled through onDeactivate (:114-117).
    canvas.prepareToLeave(w.now); canvas.setActive(false, w.now);
    check(store.deactivations == 1, "Deactivation tells the service");
}

void clearMotion() {
    FakeMeasurer measurer;
    World w(measurer); w.appearance(Language::simplifiedChinese, false, false);
    Store store;
    for (int n = 1; n <= 3; ++n) store.items.push_back(Store::file("id" + std::to_string(n), "F" + std::to_string(n) + ".txt"));
    m::FileShelfCanvas canvas(w.context, store.services());
    w.show(canvas);
    canvas.clearShelf();
    check(canvas.clearingForTesting() && store.items.size() == 3, "Clear plays `gone` before emptying (:555-563)");
    canvas.click("id1");
    check(canvas.selectedIDs().empty(), "Clicks are ignored while clearing");
    w.advance(.37); check(store.items.size() == 3, "Not before 380 ms");
    w.advance(.02); check(store.items.empty() && store.cleared == std::vector<std::size_t>{3} && w.toast() == "暂存架已清空|Shelf cleared", "Emptied after 380 ms, toasted in the UI language");
    // A clear that is still animating when the shelf leaves finishes at once (no deadline runs hidden).
    for (int n = 1; n <= 2; ++n) store.items.push_back(Store::file("x" + std::to_string(n), "X.txt"));
    canvas.storeChanged(); canvas.clearShelf();
    check(canvas.clearingForTesting(), "Clearing again");
    canvas.prepareToLeave(w.now); canvas.setActive(false, w.now);
    check(!canvas.clearingForTesting() && store.items.empty() && store.cleared.size() == 2, "Leaving finishes the clear");
}

void relinkAndRecording() {
    FakeMeasurer measurer;
    World w(measurer); w.appearance(Language::english);
    Store store;
    store.items.push_back(Store::file("kept", "Kept.txt"));
    auto imported = Store::file("mac", "Imported plan.key"); imported.lastKnownPath = "/Users/doctor/Desktop/Imported plan.key";
    imported.availabilityError = "Needs relinking"; imported.awaitingRelink = true; store.items.push_back(imported);
    m::FileShelfCanvas canvas(w.context, store.services());
    w.show(canvas);
    canvas.click("mac");
    auto* relink = control(canvas, "props.relink");
    check(relink && relink->enabled() && !control(canvas, "props.open") && !control(canvas, "props.reveal")->enabled(), "An imported item's card offers Relink in place of Open");
    relink->activate();
    check(store.relinked == std::vector<std::string>{"mac"}, "Relink asks the service for the file picker");
    check(store.reads.empty(), "An unavailable reference is never read");
    check(canvas.perform("shelf:mac:relink") && store.relinked.size() == 2 && !canvas.perform("shelf:kept:relink"), "The relink action exists only for imported items");
    // Recording mode (:124-127, :746, :517): labels redacted, no toast detail.
    w.appearance(Language::english, false, true, true);
    canvas.click("kept");
    check(control(canvas, "file:1")->label == "Redacted, TXT file, 1 KB", "Tiles speak Redacted");
    check(findLayer(canvas.root(), "props.name.redacted.redaction.bar") && findLayer(canvas.root(), "props.row2.redacted.redaction.bar"), "The card name and WHERE are redaction bars");
    canvas.open("kept");
    check(w.toast() == "Opened|", "Recording mode: the toast has no detail");
}

void virtualisation() {
    FakeMeasurer measurer;
    World w(measurer); w.appearance(Language::english);
    Store store;
    for (int n = 1; n <= 60; ++n) store.items.push_back(Store::file("id" + std::to_string(n), "F" + std::to_string(n) + ".txt"));
    m::FileShelfCanvas canvas(w.context, store.services());
    w.show(canvas);
    check(canvas.builtTileCountForTesting() == 5 * 4, "Only the visible rows and one beyond are built (:217-221)");
    check(canvas.scroll({200, 300}, {0, 136 * 6}, w.now) && canvas.builtTileCountForTesting() == 7 * 4 && control(canvas, "file:41") && !control(canvas, "file:1"),
        "Scrolling builds entering tiles and drops leaving ones from the router");
    canvas.scroll({200, 300}, {0, -136 * 6}, w.now);
    check(control(canvas, "file:1") && canvas.builtTileCountForTesting() == 5 * 4, "Tiles return when scrolled back");
    check(!canvas.scroll({300, 60}, {0, 50}, w.now) && canvas.scrollOffset() == 0, "The wheel scrolls the grid only over it (:256-260)");
    canvas.keyDown(keyOf(d::key::end), w.now);
    check(canvas.scrollOffset() > 0 && control(canvas, "file:60"), "End scrolls to the bottom");
}

// --- The Mac oracle -------------------------------------------------------------------
struct Oracle {
    const Json& data;
    RecordedMeasurer measurer;
    std::vector<std::string> failures;
    std::size_t compared{};
    explicit Oracle(const Json& d) : data(d) { recordText(measurer, d["text"]); }
    const Json* row(std::string_view state, std::string_view language = "en", bool light = false) const {
        for (const auto& r : data["shelf"].array())
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
    bool menuByTitle{};
    static Json windowsNames(const Json& value) {
        if (value.isString()) {
            std::string text = value.string();
            for (const auto& [mac, windows] : std::vector<std::pair<std::string, std::string>>{{"Reveal in Finder", m::shelfText(m::ShelfText::revealInExplorer, Language::english)},
                    {"在访达中显示", m::shelfText(m::ShelfText::revealInExplorer, Language::simplifiedChinese)}})
                if (text == mac) text = windows;
            return Json(text);
        }
        if (value.isArray()) { Json::Array out; for (const auto& v : value.array()) out.push_back(windowsNames(v)); return Json(std::move(out)); }
        if (value.isObject()) { Json::Object out; for (const auto& [k, v] : value.object()) out[k] = windowsNames(v); return Json(std::move(out)); }
        return value;
    }
    void scene(std::string_view state, m::FileShelfCanvas& canvas, std::string_view language = "en", bool light = false) {
        const Json* r = row(state, language, light);
        const std::string label = "shelf/" + std::string(state) + "/" + std::string(language) + (light ? "/light" : "");
        if (!r) { failures.push_back(label + ": not in the oracle"); return; }
        ++compared;
        // Windows names File Explorer where the Mac names Finder (shelfText).
        Json expected = windowsNames(*r);
        expected["layer"] = rooted(expected["layer"]);
        const auto before = failures.size();
        compare(label, expected, canvas.root(), canvas.root(), canvas.page().controls(), failures);
        std::vector<d::AccessibilityItem> items; canvas.accessibilityItems(items);
        // The card's text items are a Windows addition (FileShelfCanvas::accessibilityItems).
        std::erase_if(items, [](const d::AccessibilityItem& i) { return i.id == "props.name" || i.id.starts_with("props.row"); });
        compareAccessibility(label, static_cast<const Json&>(expected)["accessibility"], items, failures);
        if (expected["top"].isObject()) compare(label + "/top", Json::Object{{"layer", expected["top"]}}, canvas.top(), canvas.root(), {}, failures);
        // The open menu is as wide as its longest title: "Reveal in File Explorer"
        // makes the Windows menu wider than the Mac's 170 px. Its rows are checked
        // by title, order, danger and separator below instead of by geometry.
        if (menuByTitle) {
            std::vector<std::string> kept(failures.begin(), failures.begin() + std::ptrdiff_t(before));
            for (std::size_t n = before; n < failures.size(); ++n) if (failures[n].find("menu") == std::string::npos) kept.push_back(failures[n]);
            failures = std::move(kept);
            std::vector<std::string> titles;
            for (const auto& item : items) if (item.id.starts_with("shelf.menu:")) titles.push_back(item.id + "=" + item.label);
            if (titles != std::vector<std::string>{"shelf.menu:open=Open", "shelf.menu:reveal=Reveal in File Explorer", "shelf.menu:remove=Remove from shelf"})
                failures.push_back(label + ": menu rows");
        }
        if (canvas.header().counterText() != (*r)["header"]["counter"].string()) failures.push_back(label + ": counter " + canvas.header().counterText());
        const auto& columns = (*r)["columns"];
        if ((columns.isNumber() ? std::optional<int>(int(columns.number())) : std::nullopt) != canvas.columnsForTesting()) failures.push_back(label + ": columns");
        if (std::size_t((*r)["builtTiles"].number()) != canvas.builtTileCountForTesting()) failures.push_back(label + ": built tiles " + std::to_string(canvas.builtTileCountForTesting()));
    }
};
std::vector<m::FileShelfItem> itemsOf(const Json& input) {
    std::vector<m::FileShelfItem> out;
    for (const auto& i : input["items"].array()) {
        m::FileShelfItem v; v.id = i["id"].string(); v.name = i["name"].string(); v.lastKnownPath = i["lastKnownPath"].string();
        v.typeDescription = i["typeDescription"].string(); v.isDirectory = i["isDirectory"].boolean();
        if (i.contains("byteCount")) v.byteCount = std::int64_t(i["byteCount"].number());
        if (i.contains("availabilityError")) v.availabilityError = i["availabilityError"].string();
        out.push_back(std::move(v));
    }
    return out;
}
std::optional<m::ShelfLocalTime> timeOf(const Json& v) {
    if (!v.isArray()) return std::nullopt;
    const auto& a = v.array();
    return m::ShelfLocalTime{int(a[0].number()), int(a[1].number()), int(a[2].number()), int(a[3].number()), int(a[4].number())};
}
// Rebuilds one recorded scene: the store's items, the properties the card
// read, the selection, the offset; then the scene's own step.
void oracleScene(Oracle& o, std::string_view state, std::string_view language, bool light, const std::function<void(m::FileShelfCanvas&, World&)>& step = {}) {
    const Json* r = o.row(state, language, light);
    if (!r) { o.failures.push_back(std::string(state) + ": not in the oracle"); return; }
    const auto& input = (*r)["input"];
    World w(o.measurer);
    w.appearance(language == "zh-Hans" ? Language::simplifiedChinese : Language::english, light, true, input["recording"].boolean());
    Store store; store.items = itemsOf(input);
    auto services = store.services(); services.homeFolder = o.data["home"].string();
    m::FileShelfCanvas canvas(w.context, std::move(services));
    w.show(canvas);
    std::set<std::string, std::less<>> selected;
    for (const auto& id : input["selected"].array()) selected.insert(id.string());
    canvas.select(selected);
    for (const auto& [id, p] : input["properties"].object()) {
        m::ShelfFileProperties props; props.path = p["path"].string(); props.created = timeOf(p["created"]); props.modified = timeOf(p["modified"]);
        if (p["itemCount"].isNumber()) props.itemCount = std::int64_t(p["itemCount"].number());
        canvas.propertiesRead(id, props);
    }
    if (const double offset = input["offset"].number(); offset > 0) canvas.scroll({200, 300}, {0, 260}, w.now);
    if (step) step(canvas, w);
    o.scene(state, canvas, language, light);
}
void shelfOracle(Oracle& o) {
    for (const auto* state : {"grid", "cardFile", "cardFolder", "multi", "scrolled", "recordingCard", "cardVideo", "unavailable", "empty"}) oracleScene(o, state, "en", false);
    oracleScene(o, "cardVideo", "zh-Hans", false);
    oracleScene(o, "cardVideo", "en", true);
    oracleScene(o, "empty", "zh-Hans", false);
    oracleScene(o, "hoverTile", "en", false, [](m::FileShelfCanvas& c, World& w) { c.pointerMoved(centre(*control(c, "file:2"), c.root()), false, w.now); });
    // The Mac scrolled down and back before these two (its bar still shows).
    const auto scrolledBack = [](m::FileShelfCanvas& c, World& w) { c.scroll({200, 300}, {0, 260}, w.now); c.scroll({200, 300}, {0, -1000}, w.now); };
    o.menuByTitle = true;
    oracleScene(o, "contextMenu", "en", false, [&](m::FileShelfCanvas& c, World& w) { scrolledBack(c, w); c.rightPointerDown({220, 81 + 5 + 60}, w.now); });
    o.menuByTitle = false;
    oracleScene(o, "dropTarget", "en", false, [&](m::FileShelfCanvas& c, World& w) { scrolledBack(c, w); c.setDropTarget(true); });
    // The selection rules, step by step, on the oracle's items.
    World w(o.measurer); w.appearance(Language::english);
    Store store; store.items = itemsOf((*o.row("grid"))["input"]);
    m::FileShelfCanvas canvas(w.context, store.services());
    w.show(canvas);
    const auto id = [&](int n) { return store.items[std::size_t(n)].id; };
    const auto key = [&](std::uint32_t vk, bool shift = false) { canvas.keyDown(keyOf(vk, vk == d::key::space ? U' ' : 0, shift), w.now); };
    const std::vector<std::function<void()>> steps{
        [&] { canvas.click(id(3)); }, [&] { canvas.click(id(3)); }, [&] { canvas.click(id(3)); }, [&] { canvas.click(id(5), true); },
        [&] { canvas.click(id(7), true); }, [&] { canvas.click(id(5), true); }, [&] { canvas.click(id(7)); }, [&] { canvas.click(id(7), false, 2); },
        [&] { canvas.pointerDown({129, 300}, {{129, 300}}, w.now); }, [&] { canvas.click(id(1)); canvas.pointerDown({300, 40}, {{300, 40}}, w.now); },
        [&] { key(d::key::right); }, [&] { key(d::key::down); }, [&] { key(d::key::down, true); }, [&] { key(d::key::left, true); },
        [&] { key(d::key::space); }, [&] { key(d::key::space); }, [&] { key(d::key::space); }, [&] { key(d::key::up); },
        [&] { canvas.select({}); key(d::key::left); }};
    const auto& rules = o.data["rules"].array();
    if (rules.size() != steps.size()) o.failures.push_back("rules: step count");
    for (std::size_t n = 0; n < std::min(rules.size(), steps.size()); ++n) {
        steps[n]();
        std::vector<double> selected;
        for (std::size_t k = 0; k < store.items.size(); ++k) if (canvas.selectedIDs().contains(store.items[k].id)) selected.push_back(double(k));
        std::vector<double> expected;
        for (const auto& v : rules[n]["selected"].array()) expected.push_back(v.number());
        const auto focus = canvas.focusID() ? std::optional<double>(double(std::find_if(store.items.begin(), store.items.end(), [&](const auto& i) { return i.id == *canvas.focusID(); }) - store.items.begin())) : std::nullopt;
        const auto& f = rules[n]["focus"];
        if (selected != expected || focus != (f.isNumber() ? std::optional(f.number()) : std::nullopt) || canvas.hasPropertiesCardForTesting() != rules[n]["card"].boolean())
            o.failures.push_back("rules: " + rules[n]["step"].string());
        ++checks;
    }
    // Formatting and kinds.
    for (const auto& f : o.data["formats"].array()) {
        m::FileShelfItem v; v.lastKnownPath = f["path"].string(); v.name = v.lastKnownPath.substr(v.lastKnownPath.find_last_of('/') + 1);
        v.isDirectory = f["isDirectory"].boolean(); v.typeDescription = f["typeDescription"].string();
        if (f["byteCount"].isNumber()) v.byteCount = std::int64_t(f["byteCount"].number());
        if (f["availabilityError"].isString()) v.availabilityError = f["availabilityError"].string();
        static constexpr std::array<std::string_view, 5> kinds{"pdf", "img", "zip", "vid", "txt"};
        const auto tag = m::FileShelfCanvas::sizeTag(v, Language::english);
        if (kinds[std::size_t(m::FileShelfCanvas::tileKind(v))] != f["kind"].string() || m::FileShelfCanvas::extensionLabel(v) != f["extensionLabel"].string()
            || m::FileShelfCanvas::kindText(v, Language::english) != f["kindText"].string() || m::FileShelfCanvas::location(v, o.data["home"].string()) != f["location"].string()
            || (tag ? Json(*tag) : Json()) != f["sizeTag"])
            o.failures.push_back("format of " + v.lastKnownPath);
        ++checks;
    }
    for (const auto& k : o.data["kinds"].array()) {
        static constexpr std::array<std::string_view, 5> kinds{"pdf", "img", "zip", "vid", "txt"};
        m::FileShelfItem v; v.lastKnownPath = "C:\\x\\sample." + k["extension"].string();
        if (kinds[std::size_t(m::FileShelfCanvas::tileKind(v))] != k["kind"].string()) o.failures.push_back("kind of ." + k["extension"].string());
        ++checks;
    }
}
}

int main(int argc, char** argv) {
    try {
        formats(); canvasRules(); clearMotion(); relinkAndRecording(); virtualisation();
        if (const auto data = loadOracle(argc > 1 ? argv[1] : nullptr, {"shelf"}, {"text", "layerDefaults", "rules", "formats", "kinds", "home"})) {
            Oracle oracle(*data);
            shelfOracle(oracle);
            std::cout << "oracle: " << oracle.compared << " scenes compared, " << oracle.measurer.misses.size() << " unrecorded measurements\n";
            for (const auto& miss : oracle.measurer.misses) std::cout << "  unrecorded: " << miss << '\n';
            for (const auto& f : oracle.failures) std::cout << "  MISMATCH " << f << '\n';
            check(oracle.failures.empty(), std::to_string(oracle.failures.size()) + " oracle mismatches");
        } else std::cout << "file-shelf-source.json not present: Swift-derived contracts only\n";
        std::cout << "file_shelf_canvas_tests: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "file_shelf_canvas_tests failed after " << checks << " checks: " << e.what() << '\n';
        return 1;
    }
}
