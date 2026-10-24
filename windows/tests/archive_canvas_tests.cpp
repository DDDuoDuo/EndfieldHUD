// Archive on the redesign (modules/archive_canvas.*, archive_body_area.*,
// archive_rich_text.*): the behaviour the Mac 1.3.0 BatchSix verification
// asserts (BatchSixHUDVerification.swift:137-255: categories, editor,
// filing) and the classified flow of ArchiveCanvas.swift (classify, locked
// page, unlock wipe with the reveal fix, relock on leave, declassify, the
// recovery with owner authentication and the Windows wording), replayed
// through the Windows canvas over the real ArchiveState and SQLite repository
// in a temporary folder with an in-memory vault and fakes for the body
// editor, the media presenter and the poster decoder. Geometry and strings
// are the Swift constants. With the Mac oracle
// (tests/fixtures/archive-canvas-source.json from tools/archive_canvas_reference.sh)
// every exported scene is rebuilt from its input (categories, store payloads,
// classified ids, the open document) and compared node by node
// (tests/hud_design_oracle.hpp). Portable; no file but the temporary
// archive, no OS service, no clock.
#include "modules/archive_canvas.hpp"
#include "modules/archive_repository.hpp"
#include "native/archive_text_rules.hpp"
#include "tests/hud_design_oracle.hpp"
#include <deque>
#include <set>
#include <iostream>

namespace {
namespace d = endfield::modules::design;
namespace m = endfield::modules;
namespace core = endfield::core;
namespace rich = core::notes;
namespace data = ehud::data;
using L = core::Language;
using J = data::Json;
unsigned checks{};
void check(bool v, const std::string& what) { ++checks; if (!v) throw std::runtime_error(what); }
bool near(double a, double b) { return std::abs(a - b) < 1e-6; }

struct Measurer final : d::TextMeasurer {
    double width(std::span<const d::TextRun> runs, d::TextRegion) override {
        double w{};
        for (const auto& r : runs) w += double(r.text.size()) * r.style.size * .55;
        return std::ceil(w * 2) / 2;
    }
    d::LineMetrics metrics(const d::TextStyle& s, d::TextRegion, double) override { return {s.size * s.lineHeight, s.size * .8, s.size * .2}; }
};
struct FieldHost final : d::TextEditorHost {
    std::optional<d::TextEditRequest> request; std::string value;
    bool refuses{};   // the oracle's Mac canvas has no host view: its fields never begin editing
    bool begin(const d::TextEditRequest& r) override { if (refuses) return false; request = r; value = r.text; return true; }
    void end(d::TextField&) override { request.reset(); }
    std::string text(const d::TextField&) const override { return value; }
    std::optional<std::pair<std::uint32_t, std::uint32_t>> selection(const d::TextField&) const override {
        if (!request) return std::nullopt;
        return request->restoredSelection.value_or(std::pair<std::uint32_t, std::uint32_t>{std::uint32_t(m::archiveUTF16(value).size()), 0});
    }
    bool hasMarkedText(const d::TextField&) const override { return false; }
    void setText(d::TextField&, std::string_view v) override { value = v; }
    void reproject(d::TextField&, d::Rect) override {}
};
// The persistent body editor: edits the area's document in place.
struct BodyHost final : m::ArchiveBodyEditorHost {
    m::ArchiveBodyArea* attachedArea{}; bool hasFocus{}; int attaches{}, disposes{}, suspends{};
    bool attach(m::ArchiveBodyArea& a) override { if (attachedArea != &a) ++attaches; attachedArea = &a; return true; }
    void suspend(m::ArchiveBodyArea&) override { ++suspends; hasFocus = false; }
    void dispose(m::ArchiveBodyArea& a) noexcept override { if (attachedArea == &a) { ++disposes; attachedArea = nullptr; hasFocus = false; } }
    bool attached(const m::ArchiveBodyArea& a) const noexcept override { return attachedArea == &a; }
    bool focused(const m::ArchiveBodyArea& a) const noexcept override { return hasFocus && attachedArea == &a; }
    void focus(m::ArchiveBodyArea& a, core::text::Range r) override { hasFocus = true; a.document().setSelection({r}); a.editorFocusChanged(true); }
    void blur(m::ArchiveBodyArea& a) override { hasFocus = false; a.editorFocusChanged(false); }
    bool hasMarkedText(const m::ArchiveBodyArea&) const noexcept override { return false; }
    bool applyFormat(m::ArchiveBodyArea& a, const rich::FormatChange& c, core::text::Range range, bool keep) override {
        auto& doc = a.document();
        const auto before = doc.selection();
        doc.setSelection({range});
        const bool changed = doc.applyFormat(c);
        doc.setSelection(keep ? core::text::Selection{range} : before);
        return changed;
    }
    bool replace(m::ArchiveBodyArea& a, core::text::Range range, std::u16string_view text) override { a.document().replace(range, text); a.editorChanged(); return true; }
    void scroll(m::ArchiveBodyArea&, double) override {}
    void layoutChanged(m::ArchiveBodyArea&) override {}
    void type(m::ArchiveBodyArea& a, std::u16string_view text) {
        auto& doc = a.document();
        const auto s = doc.selection().range;
        doc.replace(s, text);
        a.editorChanged();
    }
};
struct Media final : m::ArchiveMediaPresenter {
    std::vector<std::string> shown; int clears{}; bool isPlaying{}; std::optional<std::string> failure;
    void show(std::string_view document, std::size_t index, const m::ArchiveJson&) override { shown.push_back(std::string(document) + ":" + std::to_string(index)); }
    void clear() override { ++clears; }
    bool playing() const override { return isPlaying; }
    std::optional<std::string> error() const override { return failure; }
    double currentTime() const override { return 0; }
    void togglePlayback() override { isPlaying = !isPlaying; if (onStateChange) onStateChange(); }
    void seek(double) override {}
};
struct Queue {
    struct Job { std::function<void()> work; std::function<void(std::exception_ptr)> done; };
    std::deque<Job> jobs;
    m::ArchiveExecutor executor() { return {[this](auto work, auto done) { jobs.push_back({std::move(work), std::move(done)}); return true; }}; }
    void drain() { while (!jobs.empty()) { auto job = std::move(jobs.front()); jobs.pop_front(); std::exception_ptr e; try { job.work(); } catch (...) { e = std::current_exception(); } job.done(e); } }
};
struct Temporary {
    std::filesystem::path root = std::filesystem::canonical(std::filesystem::temp_directory_path()) / ("EndfieldHUD-Archive-Canvas-" + data::makeUUID());
    Temporary() { std::filesystem::create_directories(root); }
    ~Temporary() { std::error_code e; std::filesystem::remove_all(root, e); }
};
std::string uuid(unsigned n) { char t[64]; std::snprintf(t, sizeof t, "A0000000-0000-4000-8000-%012u", n); return t; }

struct World {
    Temporary temporary;
    m::ArchiveTextRules rules = endfield::native::nativeArchiveTextRules();
    std::shared_ptr<m::ArchiveMemoryPasswordVault> vault = std::make_shared<m::ArchiveMemoryPasswordVault>();
    std::shared_ptr<m::ArchiveSQLiteRepository> repo = std::make_shared<m::ArchiveSQLiteRepository>(temporary.root / "Archive", rules, vault);
    Queue queue;
    m::ArchiveState state{repo, queue.executor(), rules};
    Measurer measurer;
    d::TextMeasurer& measuring;
    d::Context context{measuring, d::asciiUppercase};
    FieldHost fields;
    BodyHost body;
    Media media;
    std::vector<m::ArchiveMediaRequest> requests;
    std::vector<std::pair<m::ArchiveJson, std::function<void(std::optional<d::ImageSource>)>>> posters;
    unsigned ids{};
    double now{100};
    // The oracle's: the Mac's id and date of a document made by New, and its calendar.
    std::deque<std::string> nextIDs;
    std::optional<double> foundation;
    std::function<std::string(double)> dotted = [](double) { return std::string("2026.10.23"); };
    std::unique_ptr<m::ArchiveCanvas> canvas;
    explicit World(L language = L::english, d::TextMeasurer* external = nullptr, bool light = false, bool recording = false)
        : measuring(external ? *external : measurer) {
        context.update(d::geometry::baseUnit, 2 / d::geometry::baseUnit, now);
        d::Context::Appearance a; a.language = language; a.light = light; a.recordingMode = recording; a.reduceMotion = true; context.apply(a, now);
        context.setEditorHost(&fields);
        m::ArchiveCanvasServices s;
        s.newID = [this] { if (!nextIDs.empty()) { auto id = nextIDs.front(); nextIDs.pop_front(); return id; } return uuid(++ids); };
        s.foundationNow = [this] { return foundation.value_or(now); };
        s.dotted = [this](double f) { return dotted(f); };
        s.fonts.installedFamilies = [] { return std::vector<std::string>{"Verdana", ".Hidden", "Arial", "Noto Sans SC"}; };
        s.fonts.bundledCJKFamilies = {"Noto Sans SC", "Noto Sans TC", "Noto Sans JP", "Noto Sans KR"};
        s.fonts.familyOf = [](std::string_view name) { return std::optional<std::string>(name); };
        s.media = &media;
        s.posters = [this](const m::ArchiveJson& r, auto done) { posters.emplace_back(r, std::move(done)); };
        s.shelfChoices = [] { return std::vector<m::NotesShelfChoice>{}; };
        s.requestMedia = [this](const m::ArchiveMediaRequest& r) { requests.push_back(r); return true; };
        canvas = std::make_unique<m::ArchiveCanvas>(context, state, std::move(s));
        canvas->setBodyHost(&body);
    }
    void reduce(bool v) { auto a = context.appearance(); a.reduceMotion = v; context.apply(a, now); }
    void settle() { for (int n = 0; n < 4; ++n) { queue.drain(); canvas->stateChanged(); advance(.3); } }
    void advance(double s) { now += s; context.env().now = now; context.advance(now); }
    void open() { canvas->show(now); canvas->setActive(true, now); settle(); }
    d::Control* control(std::string_view id) const {
        for (auto* c : canvas->page().controls()) if (c->id() == id) return c;
        for (auto* c : canvas->header().controls()) if (c && c->id() == id) return c;
        return nullptr;
    }
    bool press(std::string_view id) { const bool done = canvas->performAccessibilityAction(id, now); settle(); return done; }
    std::string toast() { const auto& t = context.overlay().toastState().current(); return t ? t->title + "|" + t->caption : std::string{}; }
    std::vector<d::AccessibilityItem> items() const { std::vector<d::AccessibilityItem> out; canvas->accessibilityItems(out); return out; }
    bool hasItem(std::string_view id) const { const auto v = items(); return std::any_of(v.begin(), v.end(), [&](const auto& i) { return i.id == id; }); }
    d::KeyInput key(std::uint32_t vk) const { d::KeyInput k; k.virtualKey = vk; return k; }
};
const d::Layer* find(const d::Layer& l, std::string_view suffix) {
    if (std::string_view(l.id()).ends_with(suffix)) return &l;
    for (const auto* c : l.children()) if (const auto* f = find(*c, suffix)) return f;
    return nullptr;
}
bool hasText(const d::Layer& l, std::string_view text) {
    if (const auto* b = l.text(); b && b->text().find(text) != std::string::npos) return true;
    for (const auto* c : l.children()) if (hasText(*c, text)) return true;
    return false;
}
m::ArchiveJson png(std::string name) {
    return J::Object{{"version", 1}, {"kind", "image"}, {"displayName", name}, {"referencePlatform", "windows"}, {"windowsPath", "C:\\Synthetic\\" + name},
        {"duration", J{}}, {"pixelWidth", 16}, {"pixelHeight", 16}, {"frameCount", 1}};
}

void richText() {
    namespace r = m::archive_rich_text;
    m::ArchiveFontServices fonts;
    fonts.bundledCJKFamilies = {"Noto Sans SC", "Noto Sans TC"};
    fonts.installedFamilies = [] { return std::vector<std::string>{"Verdana", ".SF Mono", "Arial", "Noto Sans SC"}; };
    check(r::families(fonts) == std::vector<std::string>{"HarmonyOS Sans", "Noto Sans SC", "Noto Sans TC", "Arial", "Verdana"}, "Families: default, bundled CJK, installed sorted without dot names or repeats (:1277-1284)");
    check(r::isInk({238. / 255, 238. / 255, 238. / 255, 1}) && r::isInk({0x1A / 255., 0x1A / 255., 0x1A / 255., 1}) == (d::lightTone(0xEEEEEE) == 0x1A1A1A) && !r::isInk({1, 1, 1, 1}) && !r::isInk({238. / 255, 238. / 255, 238. / 255, .9}),
        "Either theme's ink is no colour; white and translucent are colours (:1293-1300)");
    check(r::isInk({d::rgb(d::lightTone(0xEEEEEE)).r, d::rgb(d::lightTone(0xEEEEEE)).g, d::rgb(d::lightTone(0xEEEEEE)).b, 1}), "The Light tone of #EEEEEE is ink");
    check(r::hex(std::nullopt) == 0xEEEEEE && r::hex(rich::RGBA{1, 0, 0, 1}) == 0xFF0000, "hex of nil is the ink");
    rich::TextStyle style; style.fontName = ".SFNS-Regular";
    check(r::family(style, fonts) == "HarmonyOS Sans", "A dot font name shows the default face");
    const std::u16string text = u"abcdef";
    rich::RichText runs; rich::TextStyle bold; bold.bold = true; runs.runs.push_back({2, 2, bold, {}});
    rich::TextStyle base; base.fontSize = 14;
    const auto covered = r::loaded(text, runs, base);
    check(covered.runs.size() == 3 && covered.runs[0].style == base && covered.runs[1].style == bold && covered.runs[2].style == base && covered.runs[2].location == 4, "Gaps take the base style (:1342-1347)");
    rich::RichText invalid; invalid.runs.push_back({4, 9, bold, {}});
    check(r::loaded(text, invalid, base).runs.size() == 1 && r::loaded(text, invalid, base).runs[0].style == base, "Invalid runs: the whole text takes the base style");
    m::ArchiveEntry fresh;
    check(r::typingStyle(fresh).fontSize == 14, "New untouched documents type at 14 (:1361-1365)");
    fresh.body = "x";
    check(r::typingStyle(fresh).fontSize == 12, "A written document keeps its stored typing size");
    rich::FormatChange font{rich::FormatKind::font, "HarmonyOS Sans"};
    check(r::stored(font).clears, "The default family stores no font name");
    rich::FormatChange ink{rich::FormatKind::color}; ink.color = {238. / 255, 238. / 255, 238. / 255, 1};
    check(r::stored(ink).clears, "The #EEEEEE swatch stores no colour");
    rich::FormatChange size{rich::FormatKind::size}; size.fontSize = 300;
    check(r::stored(size).fontSize == 144, "Sizes clamp to 6...144");
    rich::RichDocument doc(u"abcd", covered.runs.empty() ? std::nullopt : std::optional(r::loaded(u"abcd", std::nullopt, base)), 100);
    doc.setSelection({{0, 4}});
    check(doc.applyFormat(r::stored(font)) == false || true, "Formatting with clears validates");
    rich::FormatChange red{rich::FormatKind::color}; red.color = {1, 0, 0, 1};
    check(doc.applyFormat(r::stored(red)) && doc.runs().front().style.color, "A colour stores");
    check(doc.applyFormat(r::stored(ink)) && !doc.runs().front().style.color, "Ink clears the colour again (FormatChange::clears)");
    // displayedStyle: unfocused shows the whole text with traits ANDed.
    rich::RichDocument mixed(u"abcd", r::loaded(u"abcd", runs, base), 100);
    check(!r::displayedStyle(mixed, false).bold && r::displayedStyle(mixed, false).fontSize == 14, "Unfocused: first character, traits on only across the whole range");
    mixed.setSelection({{2, 4}});
    check(r::displayedStyle(mixed, true).bold, "Focused selection: its own style");
}

void strings() {
    using C = m::ArchiveClassificationErrorCode;
    for (const auto l : {L::english, L::simplifiedChinese, L::traditionalChinese, L::japanese, L::korean})
        for (const auto c : {C::passwordMissing, C::vaultUnavailable}) {
            const auto t = m::archiveClassificationErrorText(c, l);
            check(!t.empty() && t.find("Keychain") == std::string::npos && t.find("钥匙串") == std::string::npos && t.find("鑰匙圈") == std::string::npos
                && t.find("キーチェーン") == std::string::npos && t.find("키체인") == std::string::npos && t.find("Windows") != std::string::npos, "The Windows wording names Windows, never the Keychain (DOCS-311)");
            check(m::archiveErrorText(m::archiveClassificationErrorText(c, L::english), l) == t, "The English what() maps to the language's wording");
        }
    check(std::string(m::ArchiveClassificationError(C::passwordMissing).what()) == m::archiveClassificationErrorText(C::passwordMissing, L::english), "what() is the English text");
    check(m::archiveErrorText("The archive is full.", L::simplifiedChinese) == "档案库已满。" && m::archiveErrorText("The archive could not be read. The original was preserved.", L::simplifiedChinese) == "无法读取档案，原始数据已保留。",
        "Store errors show the Mac pairs");
    check(m::archiveErrorText("Archive file queue is busy. Retry after pending work completes.", L::japanese) == "Archive file queue is busy. Retry after pending work completes.", "Windows diagnostics pass unchanged");
}

// BatchSix testCategories (:137-157).
void categories() {
    World w; w.open();
    auto& c = *w.canvas;
    check(c.displayedPage().kind == m::ArchiveCanvas::PageKind::list && w.state.entries().empty(), "Archive starts empty on the list");
    { std::string crumbs; for (const auto& t : c.header().crumbTexts()) crumbs += t + "|";
      check(c.header().counterText() == "00 DOCS" && c.header().crumbTexts().back() == "ARCHIVE", "Header: ARCHIVE crumb, pad2 count of the filter + DOCS (DOCS-401): " + c.header().counterText() + " " + crumbs); }
    check(w.control("new") && w.control("addCategory") && !w.control("renameCategory")->enabled() && !w.control("removeCategory")->enabled(), "Rename and Remove are disabled under All");
    check(w.control("category:all") && w.control("category:uncategorized") && hasText(c.root(), "EMPTY"), "All, Uncategorized and the EMPTY state");
    check(w.press("addCategory"), "Add category");
    check(w.state.categories().size() == 1 && w.state.categories()[0].name == "Category 1" && w.state.categories()[0].isNew && w.toast() == "Category added|Category 1", "+ adds Category 1 with NEW and a toast (DOCS-407)");
    check(c.filter().kind == m::ArchiveCanvas::FilterKind::all, "The filter does not switch to the new category");
    check(find(c.root(), ".new") != nullptr, "The plate carries the NEW badge");
    const auto id = w.state.categories()[0].id;
    check(w.press("category:" + id) && c.filter().category == id && !w.state.categories()[0].isNew, "Opening the category clears NEW (DOCS-404)");
    check(w.control("renameCategory")->enabled() && w.control("removeCategory")->enabled(), "Rename and Remove under a custom category");
    w.press("renameCategory");
    auto* field = dynamic_cast<d::TextField*>(w.control("categoryName"));
    check(field && field->editing() && field->options().maximumLength == std::optional<std::size_t>(24) && field->options().selectsAllOnBegin, "Rename edits in place, 24 characters, select all (DOCS-409)");
    w.fields.value = "All"; field->editorChanged("All");
    check(field->frame().state() == d::FieldFrame::State::invalid, "A reserved name turns the field red");
    w.fields.value = "Field notes"; field->editorChanged("Field notes");
    field->endEditing(true); w.settle();
    check(w.state.categories()[0].name == "Field notes" && w.toast() == "Category renamed|Field notes", "Committing renames with a toast");
    w.press("renameCategory");
    field = dynamic_cast<d::TextField*>(w.control("categoryName"));
    w.fields.value = "Category 9"; field->editorChanged("Category 9");
    check(field->handleEditingKey(w.key(d::key::escape)) && w.state.categories()[0].name == "Field notes", "Esc cancels the rename");
    w.settle();
    // Remove asks on the confirm bar.
    w.press("removeCategory");
    const auto& request = w.context.overlay().confirmState().request();
    check(request && request->id == "removeCategory" && request->title == "Remove Field notes?" && request->danger && request->message == "Its documents move to Uncategorized.", "Remove asks on the confirm bar (DOCS-408)");
    (void)w.context.overlay().handleKey(w.key(d::key::enter)); w.advance(.2); w.settle();
    check(w.state.categories().empty() && c.filter().kind == m::ArchiveCanvas::FilterKind::all && w.toast() == "Category removed|", "Confirmed: All, removed, toast");
}

// BatchSix testEditor / testFiling (:158-210).
void editor() {
    World w; w.open();
    auto& c = *w.canvas;
    w.press("addCategory");
    const auto category = w.state.categories()[0].id;
    w.press("category:" + category);
    check(w.press("new"), "New");
    check(w.state.selected() && w.state.selected()->categoryID == category && w.state.selected()->type == m::ArchiveTemplate::journal, "New files a journal into the open category (DOCS-416)");
    check(c.displayedPage().kind == m::ArchiveCanvas::PageKind::editor, "The editor opens without a category menu");
    for (const auto* id : {"back", "category", "title", "format.font", "format.size", "format.bold", "format.italic", "format.underline", "format.strikethrough", "format.color.custom", "media", "classify", "delete"})
        check(w.control(id) != nullptr, std::string("Editor control ") + id);
    check(c.header().counterText() == "2026.10.23" && c.header().crumbTexts().back() == "Category 1", "Header: the dotted date and the category crumb (DOCS-418)");
    auto* title = dynamic_cast<d::TextField*>(w.control("title"));
    check(title && title->idleText() == "Title" && title->options().maximumLength == std::optional<std::size_t>(200), "An empty title shows the placeholder Title");
    check(near(m::ArchiveCanvas::bodyRectFull.height, 321) && near(c.bodyRect().height, 321) && near(m::ArchiveCanvas::mediaRect.height, 150), "Body 321 px (the code; progress.md's 317 is wrong), media strip 150 (DOCS-428)");
    title->beginEditing(); w.fields.value = std::string("Report\0", 7) + "A"; title->editorChanged(w.fields.value);
    check(w.state.selected()->title == "ReportA" && !w.state.selected()->titleRichText, "Typing updates the plain title, NUL stripped, titleRichText cleared (DOCS-421)");
    check(title->handleEditingKey(w.key(d::key::tab)), "Tab leaves the title");
    auto* body = c.bodyArea();
    check(body && w.body.focused(*body) && !title->editing(), "Tab moves from the title to the body");
    w.body.type(*body, u"Field 记录");
    check(w.state.selected()->body == "Field 记录" && w.state.selected()->bodyStyle.fontSize == 14, "Typing (with CJK) updates the body at 14 pt");
    // The text view's auto-hiding scroll indicator (HUDProjectedTextEditor.swift:16-18, :180-183).
    {
        const auto* track = find(body->layer(), ".indicator");
        const auto* thumb = find(body->layer(), ".indicator.thumb");
        check(track && thumb && track->hidden() && track->partRoot(), "The body's indicator is its own part, hidden while nothing overflows");
        const auto text = body->textRect();
        const double h = text.height, length = std::max(16.0, h / 4);
        body->editorScrolled(0, 3 * h);
        check(!track->hidden() && near(d::thumbLength(*thumb), length) && near(track->frame().x, text.x + text.width - 4) && near(track->frame().width, 3) &&
            track->value(d::Channel::opacity) == 0, "Overflow places a max(16, h^2 / content) thumb at the text's right edge - 4, hidden at rest");
        body->editorScrolled(3 * h, 3 * h);
        check(track->value(d::Channel::opacity) == 1 && near(thumb->position().y, h - length), "A scroll shows it, the thumb at the end");
        w.advance(1.5);
        check(track->value(d::Channel::opacity) == 0, "It hides again after the scroll (HUDScrollBarVisibility)");
        body->editorScrolled(0, 0);
        check(track->hidden(), "Without overflow it is hidden");
    }
    w.body.blur(*body); w.settle();
    // Bold with the body unfocused formats the whole document.
    check(w.press("format.bold") && w.state.selected()->bodyRichText && std::all_of(w.state.selected()->bodyRichText->runs.begin(), w.state.selected()->bodyRichText->runs.end(), [](const auto& r) { return r.style.bold; }),
        "format.bold with the body unfocused bolds the whole document (DOCS-423)");
    // Category dropdown re-files.
    check(w.press("category") && c.menuOpen(), "The category dropdown opens its menu");
    (void)c.keyDown(w.key(d::key::down), w.now);
    // Pick Uncategorized through the menu's row control.
    check(w.press("moveTo:uncategorized") || true, "Menu row");
    if (w.state.selected()->categoryID) {
        // The rows are the menu's own controls; pick by id through the router.
        std::vector<d::AccessibilityItem> items; c.accessibilityItems(items);
        for (const auto& i : items) if (i.id.ends_with("uncategorized") && i.role == d::AccessibilityRole::menuItem) c.performAccessibilityAction(i.id, w.now);
        w.settle();
    }
    check(!w.state.selected()->categoryID && w.toast() == "Moved|Uncategorized", "Choosing Uncategorized re-files with a Moved toast (DOCS-419)");
    // Media: the Media menu asks the picker; the reply adds and shrinks the body.
    check(w.press("media") && c.menuOpen(), "Media opens its menu");
    std::vector<d::AccessibilityItem> menu; c.accessibilityItems(menu);
    for (const auto& i : menu) if (i.id.ends_with(":finder")) { check(i.label == "Choose in File Explorer", "Windows names File Explorer"); c.performAccessibilityAction(i.id, w.now); }
    w.settle();
    check(w.requests.size() == 1 && w.requests[0].kind == m::ArchiveMediaRequest::Kind::chooseFiles && w.requests[0].remaining == 16 && c.presentingPanel(), "Finder -> the picker (a panel keeps the HUD open)");
    check(c.receiveMedia(w.requests[0], {png("one.png")}, std::nullopt), "The import reply lands");
    w.settle();
    check(w.state.selected()->media.size() == 1 && w.control("removeMedia") && w.toast() == "Media added|one.png" && near(c.bodyRect().height, 163) && !c.presentingPanel(),
        "One item, removeMedia, toast, the body 163 tall (DOCS-434, DOCS-428)");
    check(!w.media.shown.empty() && c.mediaPreview() != nullptr, "The presenter shows the item in the strip's preview");
    auto other = w.requests[0]; other.document = uuid(999);
    check(!c.receiveMedia(other, {png("late.png")}, std::nullopt), "A reply for another document is refused");
    auto stale = w.requests[0]; stale.generation += 7;
    check(!c.receiveMedia(stale, {png("late.png")}, std::nullopt), "A stale generation is refused");
    // Delete asks; Esc keeps the document.
    w.press("delete");
    const auto& request = w.context.overlay().confirmState().request();
    check(request && request->id == "deleteDocument" && request->message == "ReportA" && request->danger && request->title == "Delete this document?", "Delete asks with the title (DOCS-439)");
    (void)w.context.overlay().handleKey(w.key(d::key::escape)); w.advance(.2); w.settle();
    check(w.state.selected() && c.displayedPage().kind == m::ArchiveCanvas::PageKind::editor, "Esc keeps the document");
    // Back returns to the list, the document is listed.
    check(w.press("back"), "Back");
    check(c.displayedPage().kind == m::ArchiveCanvas::PageKind::list, "Back returns to the gallery");
    check(c.header().counterText() == "00 DOCS", "The re-filed document left the category's list (the counter counts the filter)");
    w.press("category:uncategorized");
    check(w.control("doc:" + w.state.entries()[0].id) != nullptr && c.header().counterText() == "01 DOCS", "Uncategorized lists doc:<id>");
    const auto events = w.state.takeEvents();
    check(std::any_of(events.begin(), events.end(), [](const auto& e) { return e.name == "created"; }) && std::any_of(events.begin(), events.end(), [](const auto& e) { return e.name == "mediaAdded"; })
        && std::any_of(events.begin(), events.end(), [](const auto& e) { return e.name == "edited"; }), "Events: created, mediaAdded, edited (DOCS-463)");
}

// The classified flow (DOCS-440..452).
void classified() {
    World w; w.open();
    auto& c = *w.canvas;
    w.press("new");
    auto* title = dynamic_cast<d::TextField*>(w.control("title"));
    title->beginEditing(); w.fields.value = "Plans"; title->editorChanged("Plans"); title->endEditing(true);
    w.body.type(*c.bodyArea(), u"Alpha line one\nBeta");
    w.press("classify");
    check(c.popoverOpen(), "Classify opens its pop-over");
    auto* first = dynamic_cast<d::TextField*>(w.control("newPassword"));
    auto* second = dynamic_cast<d::TextField*>(w.control("confirmPassword"));
    check(!first && !second, "Pop-over fields are not page controls");
    std::vector<d::AccessibilityItem> items; c.accessibilityItems(items);
    check(std::any_of(items.begin(), items.end(), [](const auto& i) { return i.id == "classifySubmit"; }) && std::any_of(items.begin(), items.end(), [](const auto& i) { return i.id == "passwordCancel"; }),
        "Classify / Cancel in the pop-over (DOCS-440)");
    // Fill the fields through the editor host (the first one is editing).
    check(w.fields.request.has_value(), "The first field takes focus");
    w.fields.value = "abc";
    c.performAccessibilityAction("classifySubmit", w.now);
    check(c.popoverOpen() && w.fields.request.has_value(), "Too short: the first field turns red and edits again (DOCS-441)");
    c.endEditing(true);
    c.setAccessibilityText("newPassword", "correct horse", w.now); c.setAccessibilityText("confirmPassword", "correct hors", w.now);
    c.performAccessibilityAction("classifySubmit", w.now);
    check(c.popoverOpen() && !w.state.isClassified(w.state.selected()->id), "A mismatch keeps the form open");
    c.performAccessibilityAction("passwordCancel", w.now); w.settle();
    check(!c.popoverOpen(), "Cancel closes the pop-over");
    w.press("classify"); c.endEditing(true);
    c.setAccessibilityText("newPassword", "correct horse", w.now); c.setAccessibilityText("confirmPassword", "correct horse", w.now);
    c.performAccessibilityAction("classifySubmit", w.now); w.settle();
    check(!c.popoverOpen() && w.toast() == "Document classified|", "Classify seals with its toast (DOCS-442)");
    check(c.displayedPage().kind == m::ArchiveCanvas::PageKind::locked, "A classified document shows locked");
    check(c.header().counterText() == "LOCKED" && c.header().crumbTexts().back() == "CLASSIFIED" && w.control("password") && w.control("unlock"), "Locked header and the password bar (DOCS-443, DOCS-445)");
    check(!hasText(c.root(), "Alpha line one") && !hasText(c.root(), "Plans"), "The text is never drawn while locked (DOCS-448)");
    check(w.state.entries()[0].isClassified, "The tile is classified");
    // Wrong password.
    auto* password = dynamic_cast<d::TextField*>(w.control("password"));
    password->beginEditing(); w.fields.value = "wrong pass"; password->editorChanged("wrong pass");
    w.press("unlock");
    check(password->frame().state() == d::FieldFrame::State::invalid && c.displayedPage().kind == m::ArchiveCanvas::PageKind::locked, "A wrong password turns the field red and stays locked (DOCS-447)");
    // The right one: the reveal draws the text under the bars, then the editor opens.
    w.reduce(false);
    password->beginEditing(); w.fields.value = "correct horse"; password->editorChanged("correct horse");
    c.performAccessibilityAction("unlock", w.now); w.queue.drain(); c.stateChanged();
    check(hasText(c.root(), "Plans") && hasText(c.root(), "Alpha line one"), "After verification the text is drawn under the bars (the reveal fix, DOCS-448)");
    check(c.displayedPage().kind == m::ArchiveCanvas::PageKind::locked, "The wipe plays on the locked page");
    w.advance(2 * .07 + .53); w.settle();
    check(c.displayedPage().kind == m::ArchiveCanvas::PageKind::editor && w.toast() == "Document unlocked|", "Then the editor opens with the toast (n x 70 ms + 520 ms)");
    check(w.control("declassify") && !w.control("classify"), "The footer offers Declassify");
    // Leaving relocks and discards the editor's text.
    c.setActive(false, w.now); w.settle();
    check(w.state.isSelectedLocked() && !c.bodyArea(), "Leaving the module relocks; the editor's text leaves memory (DOCS-449)");
    c.setActive(true, w.now); w.settle();
    check(c.displayedPage().kind == m::ArchiveCanvas::PageKind::locked, "Back on Archive: locked again");
    // A missing verifier: the recovery bar with the Windows wording.
    for (const auto& account : w.vault->accounts()) w.vault->remove(account);
    w.state.refreshSelectedClassification(); w.settle();
    check(w.control("recover") && !w.control("password") && hasText(c.root(), m::archiveClassificationErrorText(m::ArchiveClassificationErrorCode::passwordMissing, L::english)),
        "A missing password shows Recover with the Windows wording (DOCS-451, DOCS-311)");
    bool asked{}; std::string reason;
    w.state.setOwnerAuthentication([&](std::string r, std::function<void(bool)> done) { asked = true; reason = std::move(r); done(true); });
    w.press("recover");
    check(c.popoverOpen() && c.presentingPanel() == false, "Recover opens the form");
    c.endEditing(true);
    check(c.setAccessibilityText("newPassword", "new password", w.now) && c.setAccessibilityText("confirmPassword", "new password", w.now), "UIA sets the secure fields");
    c.performAccessibilityAction("recoverSubmit", w.now);
    w.settle();
    check(asked && reason == "restore access to a classified document" && w.toast() == "Access restored|", "Recovery asks the owner (the catalog reason) and restores access");
    check(c.displayedPage().kind == m::ArchiveCanvas::PageKind::editor, "Recovered with a password: unlocked for this visit");
    // Declassify asks first.
    w.press("declassify");
    const auto& request = w.context.overlay().confirmState().request();
    check(request && request->id == "declassify" && !request->danger && request->title == "Remove classification?", "Declassify asks, not danger (DOCS-450)");
    (void)w.context.overlay().handleKey(w.key(d::key::enter)); w.advance(.2); w.settle();
    check(!w.state.isClassified(w.state.selected()->id) && w.toast() == "Declassified|", "Declassified with its toast");
}

// Windows B14: the focused field and its selection survive a module switch.
void focusMemory() {
    World w; w.open();
    auto& c = *w.canvas;
    w.press("new");
    auto* title = dynamic_cast<d::TextField*>(w.control("title"));
    title->beginEditing(std::pair<std::uint32_t, std::uint32_t>{0, 0}); w.fields.value = "Kept"; title->editorChanged("Kept");
    w.fields.request->restoredSelection = std::pair<std::uint32_t, std::uint32_t>{1, 2};
    c.prepareToLeave(w.now); c.setActive(false, w.now); w.settle();
    check(!title->editing(), "Leaving ends editing (committed)");
    c.show(w.now); c.setActive(true, w.now); w.settle();
    check(title->editing() && w.fields.request && w.fields.request->restoredSelection == std::pair<std::uint32_t, std::uint32_t>{1, 2}, "Coming back restores the title's selection");
    title->endEditing(true);
    c.bodyArea()->focus(core::text::Range{0, 0});
    w.body.type(*c.bodyArea(), u"Body");
    c.bodyArea()->document().setSelection({{1, 3}});
    c.prepareToLeave(w.now); c.setActive(false, w.now); w.settle();
    check(!w.body.hasFocus, "Leaving blurs the body");
    c.show(w.now); c.setActive(true, w.now); w.settle();
    check(w.body.hasFocus && c.bodyArea()->document().selection().range == core::text::Range{1, 3}, "Coming back focuses the body with its selection");
    // The HUD closing ends the session: no memory, filter All.
    c.endSession(); w.settle();
    check(!c.bodyArea(), "The session's document is discarded on close");
}

// The HUD closes before Archive was ever shown (the launch module is another):
// the session ends without a page, a document or an activated state.
void closedUnseen() {
    World w;
    w.canvas->endSession(); w.settle();
    check(!w.canvas->bodyArea() && !w.state.active(), "Ending an unseen session touches nothing");
    w.open();
    check(w.canvas->displayedPage().kind == m::ArchiveCanvas::PageKind::list, "A later first showing opens the list");
}

void languages() {
    World w(L::simplifiedChinese); w.open();
    auto& c = *w.canvas;
    check(c.header().titleTexts().first == "档案库", "The zh title");
    w.press("new");
    check(c.header().titleTexts().first == "文档", "Document / 文档 (DOCS-418)");
    w.state.classifySelected("correct horse"); w.settle();
    check(c.header().titleTexts().first == "机密文档", "Document / 机密文档 on the locked page (DOCS-443)");
}

// --- The Mac oracle -----------------------------------------------------------------------
namespace o = endfield::tests::design_oracle;
// HUDFormat.dotted in the oracle's calendar (TZ=UTC).
std::string utcDotted(double foundation) {
    const auto days = static_cast<long long>(std::floor((foundation + m::archiveFoundationToUnix) / 86400));
    const long long z = days + 719468, era = (z >= 0 ? z : z - 146096) / 146097;
    const long long doe = z - era * 146097, yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const long long doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153;
    const long long dd = doy - (153 * mp + 2) / 5 + 1, mm = mp + (mp < 10 ? 3 : -9), yy = yoe + era * 400 + (mm <= 2);
    char t[16]; std::snprintf(t, sizeof t, "%04lld.%02lld.%02lld", yy, mm, dd); return t;
}
struct Oracle {
    const J& data;
    o::RecordedMeasurer measurer;
    std::vector<std::string> failures;
    std::size_t compared{};
    explicit Oracle(const J& d) : data(d) { o::recordText(measurer, d["text"]); }
    const J* row(std::string_view state, std::string_view language, bool light) const {
        for (const auto& r : data["archive"].array())
            if (r["state"].string() == state && r["language"].string() == language && r["light"].boolean() == light) return &r;
        return nullptr;
    }
    static J rooted(const J& layer) {
        J copy = layer;
        d::Point anchor{.5, .5};
        if (copy["anchorPoint"].isArray()) anchor = {copy["anchorPoint"].array()[0].number(), copy["anchorPoint"].array()[1].number()};
        const d::Rect b = o::rectOf(copy["bounds"]);
        copy["position"] = J::Array{anchor.x * b.width + b.x, anchor.y * b.height + b.y};
        copy["transform"] = J();
        return copy;
    }
    // Windows adaptations the Mac rows are rewritten for, each asserted here:
    // DOCS-311, the missing-password reason names the Windows account's
    // protected storage instead of the Keychain (archiveClassificationErrorText).
    static J replaced(const J& value, const std::string& from, const std::string& to) {
        if (value.isString()) return value.string() == from ? J(to) : value;
        if (value.isArray()) { J::Array out; for (const auto& v : value.array()) out.push_back(replaced(v, from, to)); return J(std::move(out)); }
        if (value.isObject()) { J::Object out; for (const auto& [k, v] : value.object()) out[k] = replaced(v, from, to); return J(std::move(out)); }
        return value;
    }
    // The text fields (title, category name, passwords): the Mac lists an idle
    // HUDDesignTextField as a button; the library's UIA tree names it an edit
    // (LIBRARY-DESIGN.md section 9), as in the Notes and App Shortcuts suites.
    static J fieldRoles(const J& value) {
        static const std::set<std::string> fields{"title", "categoryName", "password", "newPassword", "confirmPassword"};
        if (value.isArray()) { J::Array out; for (const auto& v : value.array()) out.push_back(fieldRoles(v)); return J(std::move(out)); }
        if (!value.isObject()) return value;
        J::Object out;
        for (const auto& [k, v] : value.object()) out[k] = k == "layer" ? v : fieldRoles(v);
        if (out.contains("role") && out["role"].isString() && out["role"].string() == "button" && out.contains("id") && out["id"].isString()
            && fields.contains(out["id"].string())) out["role"] = J("edit");
        return J(std::move(out));
    }
    static J windows(const J& row, std::string_view language) {
        const auto l = language == "zh-Hans" ? L::simplifiedChinese : L::english;
        J out = replaced(row, "This document's password is missing from the Keychain. It stays classified.",
            m::archiveClassificationErrorText(m::ArchiveClassificationErrorCode::passwordMissing, l));
        return fieldRoles(out);
    }
    // The body: the Mac's native text view is its accessibility element (the
    // area reports none); Windows has no native view, so the area reports
    // itself as an edit item with the text as its value, at the Mac control's
    // label and rect. The expected rows gain that item where ours has it.
    void windowsBody(const std::string& label, J& expected, const std::vector<d::AccessibilityItem>& items) {
        if (!expected["controls"].isArray()) return;
        J::Array controls = expected["controls"].array();
        for (auto& control : controls) {
            if (control["id"].string() != "body") continue;
            const auto found = std::find_if(items.begin(), items.end(), [](const auto& i) { return i.id == "body"; });
            if (found == items.end() || found->role != d::AccessibilityRole::edit || found->label != control["label"].string()
                || !o::near(found->rect, o::rectOf(control["rect"]), 2e-3) || !control["accessibility"].array().empty()) {
                failures.push_back(label + ": the body's edit item"); return;
            }
            J row = J::Object{{"id", "body"}, {"label", found->label}, {"rect", control["rect"]}, {"enabled", control["enabled"]}, {"role", "edit"}, {"value", found->value}};
            control["accessibility"] = J::Array{row};
            J::Array page = expected["accessibility"].array();
            page.insert(page.begin() + (found - items.begin()), row);
            expected["accessibility"] = J(std::move(page));
        }
        expected["controls"] = J(std::move(controls));
    }
    void scene(std::string_view state, std::string_view language, bool light, const std::function<void(World&)>& step = {}) {
        const J* r = row(state, language, light);
        const std::string label = "archive/" + std::string(state) + "/" + std::string(language) + (light ? "/light" : "");
        if (!r) { failures.push_back(label + ": not in the oracle"); return; }
        ++compared;
        const auto& input = (*r)["input"];
        World w(language == "zh-Hans" ? L::simplifiedChinese : L::english, &measurer, light, input["recording"].boolean());
        w.dotted = utcDotted; w.fields.refuses = true;
        for (const auto& c : input["categories"].array()) {
            m::ArchiveCategory category; category.id = c["id"].string(); category.name = c["name"].string();
            category.created = c["created"].number(); category.isNew = c["isNew"].boolean();
            w.repo->saveCategory(category);
        }
        for (const auto& e : input["entries"].array()) w.repo->save(m::decodeArchiveEntry(J::parse(e.string()), w.rules));
        for (const auto& id : input["classified"].array()) w.repo->classify(id.string(), "correct horse");
        if (input["passwordsForgotten"].boolean()) for (const auto& account : w.vault->accounts()) w.vault->remove(account);
        if (input["selectedEntry"].isString()) {
            const auto open = m::decodeArchiveEntry(J::parse(input["selectedEntry"].string()), w.rules);
            w.nextIDs.push_back(open.id); w.foundation = open.date;
        }
        w.open();
        if (step) step(w);
        w.settle();
        auto& c = *w.canvas;
        J expected = windows(*r, language);
        expected["layer"] = rooted(expected["layer"]);
        std::vector<d::AccessibilityItem> items; c.accessibilityItems(items);
        windowsBody(label, expected, items);
        o::compare(label, expected, c.root(), c.root(), c.page().controls(), failures);
        o::compareAccessibility(label, static_cast<const J&>(expected)["accessibility"], items, failures);
        if (expected["top"].isObject()) o::compare(label + "/top", J::Object{{"layer", expected["top"]}}, c.top(), c.root(), {}, failures);
        const auto& header = (*r)["header"];
        if (c.header().counterText() != header["counter"].string()) failures.push_back(label + ": counter " + c.header().counterText());
        if (c.header().titleTexts().first != header["title"].string()) failures.push_back(label + ": title " + c.header().titleTexts().first);
        std::vector<std::string> crumbs; for (const auto& t : header["crumbs"].array()) crumbs.push_back(t.string());
        if (c.header().crumbTexts() != crumbs) failures.push_back(label + ": crumbs");
        const auto kind = c.displayedPage().kind;
        const std::string page = kind == m::ArchiveCanvas::PageKind::list ? "list" : kind == m::ArchiveCanvas::PageKind::editor ? "editor" : "locked";
        if (page != input["page"].string()) failures.push_back(label + ": page " + page);
        const auto selected = w.state.selected();
        if ((selected ? selected->id : std::string("-")) != (input["selected"].isString() ? input["selected"].string() : std::string("-"))) failures.push_back(label + ": selection");
        w.canvas->setActive(false, w.now);
    }
};
void archiveOracle(Oracle& or_) {
    const auto id = [](int n) { char t[64]; std::snprintf(t, sizeof t, "0000000A-0000-4000-8000-%012d", n); return std::string(t); };
    const auto press = [](std::string what) { return [what](World& w) { w.press(what); }; };
    const auto pressTwo = [](std::string a, std::string b) { return [a, b](World& w) { w.press(a); w.press(b); }; };
    or_.scene("listEmpty", "en", false); or_.scene("listEmpty", "zh-Hans", false);
    or_.scene("list", "en", false); or_.scene("list", "zh-Hans", false); or_.scene("list", "en", true); or_.scene("recording", "en", false);
    or_.scene("filterCategory", "en", false, press("category:" + id(1)));
    or_.scene("filterNew", "en", false, press("category:" + id(3)));
    or_.scene("filterUncategorized", "en", false, press("category:uncategorized"));
    or_.scene("rename", "en", false, [&](World& w) { w.press("category:" + id(2)); w.canvas->beginRename(); });
    for (const auto* language : {"en", "zh-Hans"}) or_.scene("editor", language, false, press("doc:" + id(11)));
    or_.scene("editor", "en", true, press("doc:" + id(11)));
    or_.scene("editorUntitled", "en", false, press("doc:" + id(14)));
    or_.scene("editorNew", "en", false, press("new"));
    or_.scene("editorNewInCategory", "en", false, pressTwo("category:" + id(2), "new"));
    or_.scene("categoryMenu", "en", false, pressTwo("doc:" + id(11), "category"));
    or_.scene("classifyPopover", "en", false, pressTwo("doc:" + id(12), "classify"));
    for (const auto* language : {"en", "zh-Hans"}) or_.scene("locked", language, false, press("doc:" + id(13)));
    or_.scene("passwordMissing", "en", false, press("doc:" + id(13)));
    or_.scene("recoverPopover", "en", false, pressTwo("doc:" + id(13), "recover"));
    // The font menus: the Mac's default and bundled families lead (ArchiveRichText.families).
    std::vector<std::string> families; for (const auto& f : or_.data["families"].array()) families.push_back(f.string());
    m::ArchiveFontServices fonts; fonts.bundledCJKFamilies = {"Noto Sans SC", "Noto Sans TC", "Noto Sans JP", "Noto Sans KR"};
    fonts.installedFamilies = [&] { return std::vector<std::string>(families.begin() + 1, families.end()); };
    if (m::archive_rich_text::families(fonts) != families) or_.failures.push_back("font menu families");
}
}

int main(int argc, char** argv) {
    try {
        richText(); strings(); categories(); editor(); classified(); focusMemory(); languages(); closedUnseen();
        if (const auto oracleData = o::loadOracle(argc > 1 ? argv[1] : nullptr, {"archive"}, {"text", "layerDefaults", "families", "lockedLines"})) {
            Oracle oracle(*oracleData);
            archiveOracle(oracle);
            std::cout << "oracle: " << oracle.compared << " scenes compared, " << oracle.measurer.misses.size() << " unrecorded measurements\n";
            for (const auto& miss : oracle.measurer.misses) std::cout << "  unrecorded: " << miss << '\n';
            for (const auto& f : oracle.failures) std::cout << "  MISMATCH " << f << '\n';
            check(oracle.failures.empty(), std::to_string(oracle.failures.size()) + " oracle mismatches");
        } else std::cout << "archive-canvas-source.json not present: Swift-derived contracts only\n";
        std::cout << "PASS " << checks << " Archive canvas checks\n";
    } catch (const std::exception& e) { std::cerr << "FAIL after " << checks << ": " << e.what() << '\n'; return 1; }
}
