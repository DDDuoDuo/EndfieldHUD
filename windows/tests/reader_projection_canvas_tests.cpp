// Contracts of the redesigned E-Reader and Projection pages (modules/reader_canvas.*,
// modules/projection_canvas.*, modules/projection_settings.*): the rules of
// Sources/ReaderCanvas.swift, ProjectionCanvas.swift and ProjectionSettings.swift
// (Mac 1.3.0; each check cites the Swift lines it ports) over a synthetic library and
// a provider of synthetic pages, and, when the Mac oracle is given
// (tests/fixtures/reader-projection-source.json from tools/reader_projection_reference.sh),
// every recorded scene node by node with the Mac's own text widths, its controls,
// accessibility items, the toast / confirm it raised and the page layers, rebuilt by
// the same steps over a Windows ReaderState whose provider returns exactly the pages
// the Mac controller rendered; plus the Mac ReaderStore's library.json `reading` key.
// Synthetic data only: no file is opened (the provider is a table), no settings file.
#include "modules/projection_canvas.hpp"
#include "modules/reader_canvas.hpp"
#include "tests/hud_design_oracle.hpp"
#include <cmath>
#include <deque>
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
d::KeyInput keyOf(std::uint32_t vk) { d::KeyInput k; k.virtualKey = vk; return k; }

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
d::Control& need(d::Canvas& canvas, std::string_view id) {
    auto* c = control(canvas, id);
    if (!c) {
        std::string ids;
        for (const auto* each : controls(canvas)) ids += " " + each->id();
        throw std::runtime_error("no control " + std::string(id) + " among" + ids);
    }
    return *c;
}
std::string itemID(const d::Canvas& canvas, std::string_view label) {
    std::vector<d::AccessibilityItem> items; canvas.accessibilityItems(items);
    for (const auto& i : items) if (i.label == label) return i.id;
    throw std::runtime_error("no accessibility item " + std::string(label));
}
d::Point centre(const d::Control& c, const d::Layer& root) { const auto r = c.rect(root); return {r.x + r.width / 2, r.y + r.height / 2}; }
bool hasText(const d::Layer& layer, std::string_view text) {
    if (const auto* block = layer.text(); block && block->text().find(text) != std::string::npos) return true;
    for (const auto* child : layer.children()) if (hasText(*child, text)) return true;
    return false;
}
// A swatch's selection, an icon button's or a toggle's on state.
bool on(d::Canvas& canvas, std::string_view id) {
    auto& c = need(canvas, id);
    if (const auto* swatch = dynamic_cast<const d::Swatch*>(&c)) return swatch->selected();
    if (const auto* button = dynamic_cast<const d::IconButton*>(&c)) return button->isOn();
    if (const auto* toggle = dynamic_cast<const d::Toggle*>(&c)) return toggle->isOn();
    throw std::runtime_error("no on state for " + std::string(id));
}
void tab(d::Canvas& canvas, std::size_t index) { canvas.page().tabs->controls()[index]->activate(); }

// --- Reader fixtures ------------------------------------------------------------------------------
std::string bookID(unsigned n) { char s[64]; std::snprintf(s, sizeof(s), "A0000000-0000-4000-8000-%012u", n); return s; }
struct Repository final : m::ReaderRepository {
    m::ReaderLibrary value;
    m::ReaderLibrary load() override { return value; }
    void save(const m::ReaderLibrary& v) override { value = v; }
};
struct Queue {
    struct Job { std::function<void()> work; std::function<void(std::exception_ptr)> done; };
    std::deque<Job> jobs;
    m::ReaderExecutor executor() { return {[this](auto w, auto d) { jobs.push_back({std::move(w), std::move(d)}); return true; }}; }
    bool one() {
        if (jobs.empty()) return false;
        auto job = std::move(jobs.front()); jobs.pop_front();
        std::exception_ptr error; try { job.work(); } catch (...) { error = std::current_exception(); }
        job.done(error);
        return true;
    }
};
// The provider: pages by (font size, direction, location), or without a
// table a synthetic run of `count` pages (the Swift-derived contracts).
struct Source final : m::ReaderDocumentSource {
    struct Key {
        std::string book; double fontSize; bool vertical; std::int64_t s, b, c;
        auto operator<=>(const Key&) const = default;
    };
    std::map<Key, m::ReaderPage> table;
    std::string opened;
    std::shared_ptr<m::ReaderPagePixels> pixels = std::make_shared<m::ReaderPagePixels>();
    int count{5}; std::optional<std::int64_t> pageCount; unsigned renders{};
    Source() { pixels->width = pixels->height = 200; pixels->bytes.resize(200 * 200 * 4); }  // the smallest raster the state accepts
    void open(const m::ReaderBook& book, const std::function<bool()>&) override { opened = book.id; }
    m::ReaderPage synthetic(int index) const {
        m::ReaderPage p; p.location.section = index; p.progress = count > 1 ? double(index) / (count - 1) : 0;
        p.summary = "Synthetic page " + std::to_string(index + 1); p.pixels = pixels; p.pageCount = pageCount;
        if (!pageCount) p.textHeight = 200;
        if (index + 1 < count) p.next = m::ReaderLocation{index + 1};
        if (index > 0) p.previous = m::ReaderLocation{index - 1};
        return p;
    }
    std::optional<m::ReaderPage> find(const m::ReaderPreferences& prefs, const m::ReaderLocation& l) const {
        if (table.empty()) return l.section >= 0 && l.section < count ? std::optional(synthetic(int(l.section))) : std::nullopt;
        const auto i = table.find({opened, prefs.fontSize, prefs.vertical(), l.section, l.block, l.character});
        return i == table.end() ? std::nullopt : std::optional(i->second);
    }
    std::vector<m::ReaderPage> render(std::optional<m::ReaderLocation> location, std::optional<double> progress, const m::ReaderPreferences& prefs,
        endfield::core::Point, bool, int ahead, const std::function<bool()>& cancelled) override {
        if (cancelled()) return {};
        ++renders;
        std::optional<m::ReaderPage> first;
        if (progress && table.empty()) first = synthetic(std::clamp(int(std::round(*progress * (count - 1))), 0, count - 1));
        else if (progress) {
            for (const auto& [k, p] : table)
                if (k.book == opened && k.fontSize == prefs.fontSize && k.vertical == prefs.vertical() && (!first || std::abs(p.progress - *progress) < std::abs(first->progress - *progress))) first = p;
        } else first = find(prefs, location.value_or(m::ReaderLocation{}));
        if (!first) throw m::ReaderError(m::ReaderErrorCode::unavailable);
        std::vector<m::ReaderPage> out{*first};
        auto last = *first;
        for (int n = 0; n < ahead && last.next; ++n) { const auto next = find(prefs, *last.next); if (!next) break; out.push_back(*next); last = *next; }
        if (first->previous) if (const auto before = find(prefs, *first->previous)) out.push_back(*before);
        return out;
    }
    m::ReaderPage detail(m::ReaderLocation l, const m::ReaderPreferences& prefs, endfield::core::Point, bool, m::ReaderImageView, const std::function<bool()>&) override {
        return find(prefs, l).value_or(synthetic(0));
    }
    void release() override {}
};
// One Reader over the state, as the owner wires it (tools/reader_canvas_owner.cpp):
// revision changes reach stateChanged, events handleEvent, the missing check
// completes on a later turn (the Mac's utility queue then the main queue).
struct ReaderRig {
    World& w;
    std::shared_ptr<Repository> repo = std::make_shared<Repository>();
    Queue queue;
    std::shared_ptr<Source> source = std::make_shared<Source>();
    std::unique_ptr<m::ReaderState> state;
    std::unique_ptr<m::ReaderCanvas> canvas;
    std::vector<m::ReaderShelfChoice> shelf;
    std::set<std::string> missing;
    std::deque<std::string> newIDs;
    std::vector<std::string> events, opened;
    std::deque<std::function<void()>> later;
    std::uint64_t seen{};
    ReaderRig(World& world, m::ReaderLibrary library) : w(world) {
        repo->value = std::move(library);
        state = std::make_unique<m::ReaderState>(repo, queue.executor(), source);
        m::ReaderCanvasServices s;
        s.shelfChoices = [this] { return shelf; };
        s.presentPanel = [](std::optional<std::string>, std::string) { return false; };
        s.closePanel = [] {};
        s.openShelf = [this](std::string id) { opened.push_back(std::move(id)); };
        s.checkMissing = [this](std::vector<m::ReaderBook>, std::function<void(std::set<std::string>)> done) {
            later.push_back([this, done = std::move(done)] { done(missing); });
        };
        s.fontFamilies = [] { return std::vector<std::string>{"Georgia", "Helvetica"}; };
        s.newID = [this] {
            if (newIDs.empty()) return std::string("B0000000-0000-4000-8000-000000000001");
            auto id = newIDs.front(); newIDs.pop_front(); return id;
        };
        canvas = std::make_unique<m::ReaderCanvas>(w.context, *state, std::move(s));
        state->setEventObserver([this](std::string_view action) { events.emplace_back(action); canvas->handleEvent(action); });
        seen = state->revision();
    }
    ~ReaderRig() { state->setEventObserver({}); canvas.reset(); }
    void sync() {
        for (int k = 0; k < 64; ++k) {
            bool worked = queue.one();
            if (state->revision() != seen) { seen = state->revision(); canvas->stateChanged(); worked = true; }
            if (!worked && !later.empty()) { auto f = std::move(later.front()); later.pop_front(); f(); worked = true; }
            if (!worked) return;
        }
        throw std::runtime_error("Reader rig did not settle");
    }
    void show() { canvas->show(w.now); sync(); }
    void activate(bool on) { canvas->setActive(on, w.now); sync(); }
};
m::ReaderLibrary syntheticLibrary(unsigned count, std::string_view extension = "txt") {
    m::ReaderLibrary l;
    for (unsigned n = 1; n <= count; ++n)
        l.books.push_back(m::windowsReaderReference(bookID(n), "C:\\Owned-reader-fixture\\Book " + std::to_string(n) + "." + std::string(extension), "Book " + std::to_string(n)));
    return l;
}

// --- Swift-derived contracts (no oracle) ----------------------------------------------------------
void readerContracts() {
    FakeMeasurer measurer;
    World w(measurer); w.appearance(Language::english);
    // coverColor (:634-637): the UUID's 16 bytes summed, mod 8.
    check(m::ReaderCanvas::coverColor(bookID(1)) == m::ReaderCanvas::coverColors[(0xA0 + 0x40 + 0x80 + 1) % 8], "Cover colour sums the UUID bytes");
    // page(of:in:) (:640-643): PDF pages from the section, otherwise P. 1-100 of the progress.
    auto pdf = m::windowsReaderReference(bookID(1), "C:\\Owned-reader-fixture\\a.PDF", "A"), txt = m::windowsReaderReference(bookID(2), "C:\\Owned-reader-fixture\\b.txt", "B");
    m::ReaderBookmark mark{"B0000000-0000-4000-8000-000000000001", {4}, .004};
    check(m::ReaderCanvas::page(mark, pdf) == 5 && m::ReaderCanvas::page(mark, txt) == 1, "Bookmark pages: PDF section + 1, text max(1, progress %)");
    mark.progress = .376; check(m::ReaderCanvas::page(mark, txt) == 38, "Text bookmark page rounds the progress");
    check(m::ReaderCanvas::pageFrame.y == d::metric::headerHeight + 40 && m::ReaderCanvas::pageViewport.width == 588 && m::ReaderCanvas::pageViewport.height == 349
          && m::ReaderCanvas::controlsTop == m::ReaderCanvas::pageFrame.y + 357, "Paper 590 x 351 under the tabs, the 588 x 349 page, controls 6 below");

    {   // An empty library: Library first, NO BOOKS; Reading shows OPEN A BOOK (:262-266, :294-300).
        ReaderRig r(w, {});
        r.show(); r.activate(true);
        check(r.canvas->tab() == m::ReaderCanvas::Tab::library && hasText(static_cast<d::Canvas&>(*r.canvas).page().layer(), "NO BOOKS"), "Empty library opens on Library with NO BOOKS");
        tab(*r.canvas, 0);
        check(!need(*r.canvas, "fromShelf").enabled(), "From shelf is disabled without a supported shelf entry (:247)");
        r.shelf = {{"S1", "Briefing.pdf"}};
        tab(*r.canvas, 1); tab(*r.canvas, 0);
        check(need(*r.canvas, "fromShelf").enabled(), "From shelf follows the shelf entries when the page builds");
        need(*r.canvas, "fromShelf").activate();
        check(r.canvas->menuOpen(), "From shelf opens the shelf menu (:714-726)");
        r.canvas->keyDown(keyOf(d::key::down), w.now); r.canvas->keyDown(keyOf(d::key::enter), w.now);
        check(r.opened == std::vector<std::string>{"S1"} && !r.canvas->menuOpen(), "Picking a shelf entry opens it through the host");
        tab(*r.canvas, 2);
        check(hasText(static_cast<d::Canvas&>(*r.canvas).page().layer(), "OPEN A BOOK"), "Reading without a book shows OPEN A BOOK");
        r.activate(false);
    }
    {   // The open book is reopened where it was left (:17-21, :95): reading flag + selection.
        auto library = syntheticLibrary(2); library.selected = bookID(1); library.reading = true;
        ReaderRig r(w, library);
        // The library loads on activation (ReaderController(loadStore:)): the tab follows once it has (:136-138).
        r.show(); r.activate(true);
        check(r.canvas->tab() == m::ReaderCanvas::Tab::reading, "A library left reading opens on the Reading tab");
        check(r.state->current() && control(*r.canvas, "bookmark") && r.canvas->header().counterText() == "P. 1", "The page shows at once with P. 1 in the counter");
        // Top to bottom (the default): the column keeps one margin above and below (:394-396, :466).
        check(r.canvas->pageClip() && std::abs(r.canvas->pageClip()->frame().y - (1 + 22)) < 1e-9 && std::abs(r.canvas->pageClip()->frame().height - (349 - 44)) < 1e-9,
              "Column clip insets the 16 pt margin x 1.4, rounded (22 px)");
        const auto views = r.canvas->pageViews();
        check(!views.empty() && views.size() <= m::ReaderCanvas::maximumPageLayers, "At most six page layers");
        check(std::abs(views[0].cropTop - 22. / 349) < 1e-9 && std::abs(views[0].cropHeight - std::round((200 + 2) * 1.4) / 349) < 1e-9,
              "A text page in the column is cropped to (textHeight + lineSpacing) x 1.4 below the margin");
        const auto turns = r.canvas->pageTurnSequence();
        need(*r.canvas, "next").activate(); r.sync();
        check(r.canvas->pageTurnSequence() == turns + 1 && r.state->current()->location.section == 1 && r.canvas->scrollOffset() == 0,
              "Next scrolls the column to the next page's top: that page becomes current at offset 0 (:547-561, :762-770)");
        // Leaving the tab clears the reading flag; Library keeps the selection.
        tab(*r.canvas, 1); r.sync();
        check(!r.state->reading() && r.canvas->tab() == m::ReaderCanvas::Tab::library, "Another tab clears the reading flag (:20)");
        tab(*r.canvas, 2); r.sync();
        check(r.state->reading(), "The Reading tab sets it again");
        // Paged: the wheel turns once per gesture (:943-957): 16 px detents, re-armed after .24 s idle.
        auto prefs = r.state->library().preferences; prefs.setVertical(false); r.state->setPreferences(prefs); r.sync();
        const auto paper = d::Point{295, m::ReaderCanvas::pageFrame.y + 100};
        const auto before = r.canvas->pageTurnSequence();
        r.canvas->setWheelPrecise(false);
        r.canvas->scroll(paper, {10, 0}, w.now); r.sync();
        check(r.canvas->pageTurnSequence() == before, "Below a detent the wheel waits");
        r.canvas->scroll(paper, {10, 0}, w.now); r.sync();
        check(r.canvas->pageTurnSequence() == before + 1, "A full detent turns one page");
        w.advance(.1); r.canvas->scroll(paper, {40, 0}, w.now); r.sync();
        check(r.canvas->pageTurnSequence() == before + 1, "The same gesture turns no further");
        w.advance(.3); r.canvas->scroll(paper, {20, 0}, w.now); r.sync();
        check(r.canvas->pageTurnSequence() == before + 2, "After .24 s idle the wheel turns again");
        // Bookmark (:786-791, :157-160): the toast names the page.
        need(*r.canvas, "bookmark").activate(); r.sync();
        check(r.state->book()->bookmarks.size() == 1 && w.toast().starts_with("Bookmarked|P. "), "Bookmark adds the mark and toasts its page");
        r.activate(false);
        check(r.state->current() != nullptr, "The page outlives deactivation (ReaderController.current)");
    }
}

// --- Projection fixtures --------------------------------------------------------------------------
struct Pen {
    std::vector<ehud::data::ProjectionSettings> saved;
    m::ProjectionPenSettings settings;
    explicit Pen(ehud::data::ProjectionSettings stored = {}) : settings(stored, 0xFFFA00, [this](const ehud::data::ProjectionSettings& v) { saved.push_back(v); }) {}
};
m::ProjectionModel sessionModel() { return m::ProjectionModel({0xFA / 255., 0xD4 / 255., 0x1F / 255., 1}, .5, .5); }
m::DrawingStroke stroke() { m::DrawingStroke s; s.points = {{.1, .1}, {.8, .4}}; s.width = 12; s.color = {1, .83, .12, 1}; return s; }

void projectionContracts() {
    FakeMeasurer measurer;
    World w(measurer); w.appearance(Language::english);
    {   // ProjectionSettings (:31-101).
        Pen pen;
        auto& s = pen.settings;
        check(s.values().color == 0xFFFA00 && s.values().width == 12 && !s.values().erasing && s.values().background && pen.saved.empty(),
              "Unsaved values: the theme colour, width 12, the pen, the dotted background; nothing written");
        check(s.palette() == std::array<std::uint32_t, 6>{0xFFFA00, 0xFAFAFA, 0x22A6F2, 0xE5383B, 0x8CE03C, 0x111111}, "Six presets, the theme colour first (:13)");
        s.update([](auto& v) { v.width = 100; });
        check(s.values().width == 80 && pen.saved.size() == 1 && pen.saved.back() == ehud::data::ProjectionSettings{0xFFFA00, 80, false, true},
              "Width clamps to 1...80 and the whole record is written, the colour included (:53, :98-101)");
        s.update([](auto& v) { v.width = NAN; v.color = 0xFF123456; });
        check(s.values().width == 80 && s.values().color == 0x123456, "A non-finite width keeps the old one; the colour is masked to RGB");
        const auto writes = pen.saved.size();
        s.update([](auto&) {});
        check(pen.saved.size() == writes, "An unchanged update writes nothing (:54)");
        check(!s.hasDrawing() && !s.clearDrawing(), "Without a session model nothing is drawn or cleared (:75-78)");
        auto model = sessionModel();
        s.attach(&model);
        check(m::ProjectionPenSettings::hex(model.color()) == 0x123456 && model.brushWidth() == 80, "Attach applies the saved values to the model (:44-47)");
        model.setErasing(true); model.setBackgroundEnabled(false); model.setBrushWidth(20);
        s.capture();
        check(s.values().erasing && !s.values().background && s.values().width == 20, "capture takes the workspace's own choices back (:61-72)");
        model.append(stroke());
        check(s.hasDrawing() && s.clearDrawing() && !s.hasDrawing(), "Strokes count as drawing; clearDrawing clears the session");
        s.requestMediaInsert();
        check(s.takeMediaInsertRequest() && !s.takeMediaInsertRequest(), "The media insert request is taken once (:84-87)");
        Pen loaded; loaded.settings.load({0x22A6F2, 30, true, false});
        check(loaded.settings.values() == m::ProjectionPenSettings::Values{0x22A6F2, 30, true, false} && loaded.saved.empty(), "A loaded record replaces the defaults without a write");
        s.attach(nullptr);
    }
    {   // The page (:39-97).
        Pen pen;
        auto model = sessionModel();
        int opened = 0;
        m::ProjectionCanvas canvas(w.context, pen.settings, [&] { ++opened; });
        canvas.show(w.now); canvas.setActive(true, w.now);
        check(canvas.header().counterText() == "BG ON" && !need(canvas, "clear").enabled(), "BG ON; Clear all is disabled without a drawing (:25, :188)");
        check(on(canvas, "color:FFFA00") && on(canvas, "pen") && !on(canvas, "eraser"), "The theme swatch and the pen are selected");
        need(canvas, "dotted").activate();
        check(canvas.header().counterText() == "BG OFF" && !pen.settings.values().background && !on(canvas, "background"), "The toggle turns the dots off everywhere");
        need(canvas, "color:E5383B").activate();
        check(pen.settings.values().color == 0xE5383B && on(canvas, "color:E5383B") && !on(canvas, "color:custom"), "A swatch sets the pen colour");
        canvas.setActive(false, w.now);
        pen.settings.attach(&model); model.append(stroke()); model.setColor(m::ProjectionPenSettings::color(0x123456));
        canvas.show(w.now); canvas.setActive(true, w.now);
        check(on(canvas, "color:custom") && need(canvas, "clear").enabled(), "Activation captures the workspace: a custom colour, Clear all enabled (:20-23)");
        need(canvas, "clear").activate();
        check(w.confirm() == "Clear the projection canvas?|Source files stay intact.|Clear all|danger", "Clear all asks first (:210-219)");
        w.context.overlay().decide(d::ConfirmState::Decision::confirm);
        check(model.drawing().strokes().empty() && w.toast() == "Canvas cleared|" && !need(canvas, "clear").enabled(), "Confirmed: cleared, toasted, disabled");
        w.context.overlay().reset();
        need(canvas, "media").activate();
        check(opened == 1 && pen.settings.mediaInsertRequested(), "Insert media asks the workspace and opens it (:205-208)");
        need(canvas, "open").activate();
        check(opened == 2 && w.toast() == "Projection|", "Open projection opens it and toasts (:88-91)");
        canvas.setActive(false, w.now);
        pen.settings.attach(nullptr);
    }
}

// --- The Mac oracle -------------------------------------------------------------------------------
Language languageOf(std::string_view name) { return name == "zh-Hans" ? Language::simplifiedChinese : Language::english; }
std::optional<m::ReaderLocation> locationOf(const Json& j) {
    if (!j.isArray()) return std::nullopt;
    const auto& a = j.array();
    return m::ReaderLocation{a[0].integer(), a[1].integer(), a[2].integer()};
}
// The Mac page layers hold the page images; the Windows ones are content-less
// (the native owner draws the textures at their poses): compared separately.
void stripPages(Json& n) {
    if (!n.isObject()) return;
    auto& o = n.object();
    const auto name = o.find("name");
    if (name != o.end() && name->second.isString() && name->second.string() == "reader.page") { o.erase("contents"); o.erase("contentsRect"); o.erase("contentsGravity"); }
    const auto children = o.find("children");
    if (children == o.end() || !children->second.isArray()) return;
    Json::Array kids = children->second.array();
    for (auto& c : kids) stripPages(c);
    children->second = std::move(kids);
}
struct Oracle {
    const Json& data;
    RecordedMeasurer measurer;
    std::vector<std::string> failures;
    std::size_t compared{};
    explicit Oracle(const Json& d) : data(d) {
        recordText(measurer, d["text"]);
        for (const auto& row : d["canvas"].array()) if (recordMicroTag(measurer, row["layer"])) break;
    }
    const Json* row(std::string_view section, std::string_view library, std::string_view state, std::string_view language, bool light) const {
        for (const auto& r : data[std::string(section)].array())
            if ((library.empty() || r["library"].string() == library) && r["state"].string() == state && r["language"].string() == language && r["light"].boolean() == light) return &r;
        return nullptr;
    }
    static Json rooted(const Json& layer) {
        Json copy = layer;
        Point anchor{.5, .5};
        if (copy["anchorPoint"].isArray()) anchor = {copy["anchorPoint"].array()[0].number(), copy["anchorPoint"].array()[1].number()};
        const Rect b = rectOf(copy["bounds"]);
        copy["position"] = Json::Array{anchor.x * b.width + b.x, anchor.y * b.height + b.y};
        copy["transform"] = Json();
        stripPages(copy);
        return copy;
    }
    // One scene: tree, controls, accessibility, top layer, header, toast, confirm.
    const Json* scene(std::string_view section, std::string_view library, std::string_view state, d::Canvas& canvas, World& w,
                      std::string_view language = "en", bool light = false) {
        const Json* r = row(section, library, state, language, light);
        const std::string label = std::string(section) + "/" + std::string(library) + (library.empty() ? "" : "/") + std::string(state) + "/" + std::string(language) + (light ? "/light" : "");
        if (!r) { failures.push_back(label + ": not in the oracle"); return nullptr; }
        ++compared;
        Json expected = *r;
        expected["layer"] = rooted((*r)["layer"]);
        const auto list = controls(canvas);
        compare(label, expected, canvas.root(), canvas.root(), list, failures);
        std::vector<d::AccessibilityItem> items; canvas.accessibilityItems(items);
        compareAccessibility(label, static_cast<const Json&>(expected)["accessibility"], items, failures);
        if ((*r)["top"].isObject()) compare(label + "/top", Json::Object{{"layer", (*r)["top"]}}, canvas.top(), canvas.root(), {}, failures);
        else if (canvas.menuOpen() || canvas.popoverOpen()) failures.push_back(label + ": a popup is open");
        if (canvas.header().counterText() != (*r)["header"]["counter"].string()) failures.push_back(label + ": counter " + canvas.header().counterText() + " vs " + (*r)["header"]["counter"].string());
        if (canvas.header().titleTexts().first != (*r)["header"]["title"].string()) failures.push_back(label + ": title " + canvas.header().titleTexts().first);
        const auto& toast = (*r)["toast"];
        const std::string expectedToast = toast.isObject() ? toast["title"].string() + "|" + toast["caption"].string() : std::string();
        if (w.toast() != expectedToast) failures.push_back(label + ": toast '" + w.toast() + "' vs '" + expectedToast + "'");
        const auto& confirm = (*r)["confirm"];
        const std::string expectedConfirm = confirm.isObject() ? confirm["title"].string() + "|" + confirm["message"].string() + "|" +
            confirm["confirm"].string() + (confirm["danger"].boolean() ? "|danger" : "") : std::string();
        if (w.confirm() != expectedConfirm) failures.push_back(label + ": confirm '" + w.confirm() + "' vs '" + expectedConfirm + "'");
        return r;
    }
    void reader(std::string_view library, std::string_view state, ReaderRig& rig, std::string_view language = "en", bool light = false) {
        const Json* r = scene("canvas", library, state, *rig.canvas, rig.w, language, light);
        if (!r) return;
        const std::string label = "canvas/" + std::string(library) + "/" + std::string(state) + "/" + std::string(language) + (light ? "/light" : "");
        if (int(rig.canvas->tab()) != (*r)["tab"].integer()) failures.push_back(label + ": tab " + std::to_string(int(rig.canvas->tab())));
        std::vector<std::string> missing(rig.canvas->missing().begin(), rig.canvas->missing().end()), mac;
        for (const auto& id : (*r)["missing"].array()) mac.push_back(id.string());
        if (missing != mac) failures.push_back(label + ": missing set");
        const auto& current = (*r)["current"];
        const auto* page = rig.state->current();
        if (current.isObject() != (page != nullptr) || (page && locationOf(current["location"]) != page->location))
            failures.push_back(label + ": current page");
        // Visible page layers: frame in the column, the crop of the raster.
        std::vector<std::pair<Rect, std::pair<double, double>>> macPages, ours;
        for (const auto& l : (*r)["pageLayers"].array())
            if (!l["hidden"].boolean()) { const auto c = rectOf(l["contentsRect"]); macPages.push_back({rectOf(l["frame"]), {c.y, c.height}}); }
        for (const auto& v : rig.canvas->pageViews())
            if (v.layer && !v.layer->hidden()) ours.push_back({v.layer->frame(), {v.cropTop, v.cropHeight}});
        const auto byY = [](const auto& a, const auto& b) { return a.first.y < b.first.y || (a.first.y == b.first.y && a.first.x < b.first.x); };
        std::sort(macPages.begin(), macPages.end(), byY); std::sort(ours.begin(), ours.end(), byY);
        bool same = macPages.size() == ours.size();
        for (std::size_t i = 0; same && i < ours.size(); ++i)
            same = near(macPages[i].first, ours[i].first, 2e-3) && near(macPages[i].second.first, ours[i].second.first, 1e-6) && near(macPages[i].second.second, ours[i].second.second, 1e-6);
        if (!same) {
            std::string text = label + ": page layers";
            for (const auto& p : ours) text += " W" + str(p.first) + "/" + std::to_string(p.second.first) + "+" + std::to_string(p.second.second);
            for (const auto& p : macPages) text += " M" + str(p.first) + "/" + std::to_string(p.second.first) + "+" + std::to_string(p.second.second);
            failures.push_back(text);
        }
    }
};

// The Mac controller's pages for the Windows provider.
void loadPages(Source& source, const Json& pages) {
    for (const auto& p : pages.array()) {
        m::ReaderPage page;
        page.location = *locationOf(p["location"]); page.next = locationOf(p["next"]); page.previous = locationOf(p["previous"]);
        page.progress = p["progress"].number(); page.summary = p["summary"].string(); page.illustration = p["illustration"].boolean();
        if (p["pageCount"].isNumber()) page.pageCount = p["pageCount"].integer();
        if (p["textHeight"].isNumber()) page.textHeight = p["textHeight"].number();
        page.pixels = source.pixels;
        source.table[{p["book"].string(), p["fontSize"].number(), p["vertical"].boolean(), page.location.section, page.location.block, page.location.character}] = page;
    }
}

void readerOracle(Oracle& o) {
    const auto& input = o.data["reader"];
    World w(o.measurer); w.appearance(Language::english);
    {   // An empty library.
        ReaderRig r(w, {});
        r.show(); r.activate(true);
        o.reader("empty", "library", r);
        tab(*r.canvas, 0);
        o.reader("empty", "open", r);
        for (const auto& s : input["shelf"].array()) if (s["supported"].boolean() && s["available"].boolean()) r.shelf.push_back({s["id"].string(), s["title"].string()});
        tab(*r.canvas, 1); tab(*r.canvas, 0);
        o.reader("empty", "openShelf", r);
        need(*r.canvas, "fromShelf").activate();
        o.reader("empty", "shelfMenu", r);
        r.canvas->closeMenu();
        tab(*r.canvas, 2);
        o.reader("empty", "reading", r);
        tab(*r.canvas, 3);
        o.reader("empty", "marks", r);
        w.appearance(Language::simplifiedChinese); r.activate(false); r.show(); r.activate(true);
        tab(*r.canvas, 0);
        o.reader("empty", "openShelf", r, "zh-Hans");
        w.appearance(Language::english);
        r.activate(false);
    }
    {   // Four books, one missing.
        m::ReaderLibrary library;
        for (const auto& b : input["books"].array())
            library.books.push_back(m::windowsReaderReference(b["id"].string(), "C:\\Owned-reader-fixture\\" + b["file"].string(), b["title"].string()));
        ReaderRig r(w, library);
        for (const auto& id : input["missing"].array()) r.missing.insert(id.string());
        for (const auto& id : input["bookmarkIDs"].array()) r.newIDs.push_back(id.string());
        loadPages(*r.source, input["pages"]);
        const auto tile = [&](unsigned n) -> d::Control& { return need(*r.canvas, "book:" + bookID(n)); };
        r.show(); r.activate(true);
        o.reader("books", "library", r);
        r.canvas->pointerMoved(centre(tile(1), r.canvas->root()), false, w.now);
        o.reader("books", "hoverTile", r);
        r.canvas->pointerMoved(std::nullopt, false, w.now);
        need(*r.canvas, "book:" + bookID(1) + ":more").activate();
        o.reader("books", "bookMenu", r);
        for (const auto k : {d::key::down, d::key::down, d::key::down, d::key::enter}) r.canvas->keyDown(keyOf(k), w.now);
        o.reader("books", "removeConfirm", r);
        w.context.overlay().dismissConfirm(); w.context.overlay().reset();
        tile(1).activate(); r.sync();
        o.reader("books", "reading", r);
        need(*r.canvas, "bookmark").activate(); r.sync();
        o.reader("books", "bookmarked", r);
        w.context.overlay().reset();
        need(*r.canvas, "settings").activate();
        o.reader("books", "settings", r);
        r.canvas->performAccessibilityAction(itemID(*r.canvas, "Left to right"), w.now); r.sync();
        o.reader("books", "settingsPaged", r);
        r.canvas->performAccessibilityAction("fontSize:plus", w.now); r.sync();
        o.reader("books", "settingsFontSize", r);
        r.canvas->keyDown(keyOf(d::key::escape), w.now);
        o.reader("books", "paged", r);
        need(*r.canvas, "next").activate(); r.sync();
        o.reader("books", "pagedNext", r);
        tab(*r.canvas, 3);
        o.reader("books", "marks", r);
        r.canvas->performAccessibilityAction("mark:" + input["bookmarkIDs"].array()[0].string() + ":remove", w.now); r.sync();
        o.reader("books", "marksEmpty", r);
        tab(*r.canvas, 1);
        o.reader("books", "librarySelected", r);
        tile(2).activate(); r.sync();
        o.reader("books", "readingPdf", r);
        w.appearance(Language::simplifiedChinese); r.activate(false); r.show(); r.activate(true);
        o.reader("books", "readingPdf", r, "zh-Hans");
        tab(*r.canvas, 1);
        o.reader("books", "librarySelected", r, "zh-Hans");
        w.appearance(Language::english); r.activate(false); r.show(); r.activate(true);
        w.appearance(Language::english, true); r.show();
        o.reader("books", "librarySelected", r, "en", true);
        tab(*r.canvas, 2); r.sync();
        o.reader("books", "readingPdf", r, "en", true);
        w.appearance(Language::english); r.show();
        r.activate(false);
        const auto& final = o.data["readerFinal"];
        if (r.state->reading() != final["reading"].boolean() || !r.state->book() || r.state->book()->id != final["selected"].string())
            o.failures.push_back("reader: the reading flag " + std::to_string(r.state->reading()) + " and the selection end as the Mac's");
    }
}

// library.json: `reading` is written only while true (ReaderStore.Archive, :48-50).
void storeOracle(const Json& steps) {
    for (const auto& step : steps.array()) {
        const auto mac = Json::parse(step["json"].string());
        const auto library = m::decodeReaderLibrary(mac);
        const auto windows = m::encodeReaderLibrary(library);
        const std::string label = "store/" + step["step"].string();
        check(library.reading == (mac.contains("reading") && mac["reading"].boolean()), label + ": reading decodes from the Mac library");
        check(windows.contains("reading") == mac.contains("reading") && (!mac.contains("reading") || windows["reading"] == mac["reading"]), label + ": reading encodes as the Mac writes it");
        check((library.selected ? Json(*library.selected) : Json()) == (mac.contains("selected") ? mac["selected"] : Json()), label + ": selection");
    }
}

void projectionOracle(Oracle& o) {
    World w(o.measurer); w.appearance(Language::english);
    Pen pen;
    auto model = sessionModel();
    int opened = 0;
    m::ProjectionCanvas canvas(w.context, pen.settings, [&] { ++opened; });
    const auto scene = [&](std::string_view state, std::string_view language = "en", bool light = false) {
        const Json* r = o.scene("projection", "", state, canvas, w, language, light);
        if (!r) return;
        const std::string label = "projection/" + std::string(state) + "/" + std::string(language) + (light ? "/light" : "");
        const auto& v = (*r)["values"];
        const auto& values = pen.settings.values();
        if (values.color != std::uint32_t(v["color"].integer()) || values.width != v["width"].number() || values.erasing != v["erasing"].boolean() || values.background != v["background"].boolean())
            o.failures.push_back(label + ": values");
        const auto& stored = (*r)["stored"];
        if (stored.isObject() != !pen.saved.empty()) o.failures.push_back(label + ": stored record presence");
        else if (stored.isObject()) {
            const auto& s = pen.saved.back();
            if (s.color != std::optional<std::uint32_t>(std::uint32_t(stored["color"].integer())) || s.width != stored["width"].number()
                || s.erasing != stored["erasing"].boolean() || s.background != stored["background"].boolean()) o.failures.push_back(label + ": stored record");
        }
        if (opened != (*r)["opened"].integer() || pen.settings.mediaInsertRequested() != (*r)["mediaInsert"].boolean()) o.failures.push_back(label + ": open / media request");
        if (model.drawing().strokes().size() != std::size_t((*r)["strokes"].integer()) && pen.settings.model()) o.failures.push_back(label + ": strokes");
    };
    canvas.show(w.now); canvas.setActive(true, w.now);
    scene("detached");
    canvas.setActive(false, w.now);
    pen.settings.attach(&model); model.append(stroke());
    canvas.show(w.now); canvas.setActive(true, w.now);
    scene("default");
    need(canvas, "eraser").activate(); scene("eraser");
    need(canvas, "dotted").activate(); scene("backgroundOff");
    need(canvas, "color:22A6F2").activate(); scene("cyan");
    canvas.adjustAccessibilitySlider("penWidth", 30, w.now); scene("penWidth");
    canvas.setActive(false, w.now);
    model.setColor(m::ProjectionPenSettings::color(0x123456));
    canvas.show(w.now); canvas.setActive(true, w.now);
    scene("custom");
    need(canvas, "color:custom").activate(); scene("colorPicker");
    canvas.keyDown(keyOf(d::key::escape), w.now);
    need(canvas, "clear").activate(); scene("clearConfirm");
    w.context.overlay().decide(d::ConfirmState::Decision::confirm); scene("cleared");
    w.context.overlay().reset();
    need(canvas, "media").activate(); scene("media");
    (void)pen.settings.takeMediaInsertRequest();
    need(canvas, "open").activate(); scene("open");
    w.context.overlay().reset();
    w.appearance(Language::simplifiedChinese); canvas.setActive(false, w.now); canvas.show(w.now); canvas.setActive(true, w.now);
    scene("custom", "zh-Hans");
    w.appearance(Language::english); canvas.setActive(false, w.now); canvas.show(w.now); canvas.setActive(true, w.now);
    w.appearance(Language::english, true); canvas.show(w.now);
    scene("custom", "en", true);
    w.appearance(Language::english); canvas.show(w.now);
    canvas.setActive(false, w.now);
    pen.settings.attach(nullptr);
}
} // namespace

int main(int argc, char** argv) {
    try {
        readerContracts();
        projectionContracts();
        if (const auto data = loadOracle(argc > 1 ? argv[1] : nullptr, {"canvas", "projection"}, {"text", "layerDefaults", "reader", "readerFinal", "store"})) {
            Oracle oracle(*data);
            readerOracle(oracle);
            projectionOracle(oracle);
            storeOracle((*data)["store"]);
            std::cout << "oracle: " << oracle.compared << " scenes compared, " << oracle.measurer.misses.size() << " unrecorded measurements\n";
            for (const auto& miss : oracle.measurer.misses) std::cout << "  unrecorded: " << miss << '\n';
            for (const auto& f : oracle.failures) std::cout << "  MISMATCH " << f << '\n';
            check(oracle.failures.empty(), std::to_string(oracle.failures.size()) + " oracle mismatches");
        } else std::cout << "reader-projection-source.json not present: Swift-derived contracts only\n";
        std::cout << "reader_projection_canvas_tests: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "reader_projection_canvas_tests failed after " << checks << " checks: " << e.what() << '\n';
        return 1;
    }
}
