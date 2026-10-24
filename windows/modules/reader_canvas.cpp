#include "modules/reader_canvas.hpp"
#include "modules/hud_design_badges.hpp"
#include "modules/reader_localization.hpp"
#include <algorithm>
#include <cmath>

namespace endfield::modules {
namespace d = design;
using d::Channel;
using d::Point;
using d::Rect;
namespace {
constexpr double pi = 3.14159265358979323846;
constexpr double bodyTop = d::metric::headerHeight + 34 + 10;
std::string sub(std::string_view id, std::string_view part) { return std::string(id) + "." + std::string(part); }
bool contains(Rect r, Point p) noexcept { return p.x >= r.x && p.y >= r.y && p.x < r.x + r.width && p.y < r.y + r.height; }
int hexDigit(char c) noexcept { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }
// URL(fileURLWithPath:).pathExtension of a Windows or Mac reference path.
std::string extension(const ReaderBook& book) {
    const std::string path(readerReferencePath(book));
    const auto slash = path.find_last_of("/\\");
    const auto name = slash == std::string::npos ? path : path.substr(slash + 1);
    const auto dot = name.rfind('.');
    return dot == std::string::npos || dot == 0 ? std::string{} : name.substr(dot + 1);
}
std::string lowercase(std::string v) { for (auto& c : v) if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a'); return v; }
d::Menu::Item item(std::string id, std::string title) { d::Menu::Item i; i.id = std::move(id); i.title = std::move(title); return i; }
std::string tr(const d::Environment& env, std::string_view english, std::string_view simplified) { return core::localized(english, simplified, env.language); }
std::string key(const ReaderBook& b) { return b.id + (b.platform == ReaderReferencePlatform::windows ? "|w|" : "|m|") + std::string(readerReferencePath(b)); }
}

// --- ReaderBookTile (:1016-1119) ------------------------------------------------
class ReaderBookTile::More final : public d::Control {
public:
    More(d::Builder& b, std::string id, std::string label) : Control(b.env, id, std::move(label), b.layer(id)) {
        layer().setAnimatable();                                    // hover scale 1.08
        layer().setBounds({22, 22});
        plate_ = &b.layer(sub(this->id(), "plate"));
        plate_->setFrame({0, 0, 22, 22});
        // backgroundColor #141414 .8 -> .95 on hover: a tinted white circle.
        d::setTintedBackground(*plate_, d::rgb(0x141414, .8), 11);
        layer().add(*plate_);
        layer().add(d::makeText(b, sub(this->id(), "dots"), "⋯", d::TextStyle{12, d::FontWeight::black, d::white(1)}, {0, 5, 22, 12}, d::TextAlignment::center));
        setTarget({0, 0, 22, 22}, d::PathBuilder().ellipse({0, 0, 22, 22}));
    }
    void hoverChanged() override {
        const bool on = hovered() && enabled();
        const auto c = clock();
        d::motion::animateTint(*plate_, d::rgb(0x141414, on ? .95 : .8), d::duration::hover, c, d::easing::ease);
        d::motion::animate(layer(), Channel::scaleX, on ? 1.08 : 1, d::duration::hover, c);
        d::motion::animate(layer(), Channel::scaleY, on ? 1.08 : 1, d::duration::hover, c);
    }
private:
    d::Layer* plate_;
};

ReaderBookTile::ReaderBookTile(d::Builder& b, std::string id, int number, const ReaderBook& book, d::Color cover, bool missing, double width)
    : Control(b.env, id, book.title, b.layer(id)) {
    const Rect bounds{0, 0, width, wellHeight + 24};
    d::Layer& l = layer();
    l.setBounds({width, wellHeight + 24});
    auto& more = b.make<More>(this->id() + ":more", tr(b.env, "More", "更多") + ", " + book.title);
    more_ = &more;
    well_ = &b.layer(sub(this->id(), "well"));
    well_->setAnimatable();                                         // -2 px on hover
    well_->setFrame({0, 0, width, wellHeight});
    const Rect wellBounds{0, 0, width, wellHeight};
    const std::array<d::Color, 3> plateColors{d::rgb(0x8A8885), d::rgb(0xBDBDBB), d::rgb(0xD9D9D8)};
    const std::array<double, 3> stops{0, .55, 1};
    well_->setContent(d::plate(well_->id(), wellBounds, plateColors, stops, 0));
    well_->setMasksToBounds(true);
    brighten_ = &b.layer(sub(this->id(), "well.brighten"));
    brighten_->setFrame(wellBounds); brighten_->setAnimatable();
    d::setBackground(*brighten_, d::white(.12)); brighten_->setValue(Channel::opacity, 0);
    d::Layer& codeBar = b.layer(sub(this->id(), "code.bar"));
    codeBar.setFrame({0, 0, 34, 4}); d::setBackground(codeBar, d::fixed::counterBackground);
    d::Layer& code = d::makeText(b, sub(this->id(), "code"), "#" + d::format::pad2(number), d::TextStyle{6, d::FontWeight::bold, d::rgb(0x222222)}, {38, -1, 30, 6});
    // The cover: 58 x 82 in the book's colour, -4 degrees, a .35 black shadow r 7 y +8.
    d::Layer& coverLayer = b.layer(sub(this->id(), "cover"));
    coverLayer.setBounds({58, 82}); coverLayer.setPosition({width / 2, wellHeight / 2});
    auto face = d::solid(coverLayer.id(), {0, 0, 58, 82}, cover);
    d::addShadow(face, d::Shadow{d::black(1), .35, 7, {0, 8}, d::PathBuilder().rect({0, 0, 58, 82})});
    coverLayer.setContent(std::move(face));
    d::TextBlock coverTitle(book.title, d::TextStyle{9, d::FontWeight::black, d::white(1), 1.05});
    coverTitle.maximumLines = 4;
    coverTitle.lineFrame = {6, 82 - 6 - 9 * 1.05 * 4, 46, 9 * 1.05 * 4};
    d::Layer& coverText = b.layer(sub(this->id(), "cover.title"));
    coverText.setText(std::move(coverTitle));
    coverLayer.add(coverText);
    coverLayer.setBaseTransform(d::Affine::rotation(-4 * pi / 180));
    // The format tag: the path extension uppercased, 8.5 bold on #141414 .75.
    const auto upper = b.env.text.uppercase ? b.env.text.uppercase(extension(book)) : d::asciiUppercase(extension(book));
    const d::TextStyle tagStyle{8.5, d::FontWeight::bold, d::fixed::onDarkTag};
    const double tagWidth = std::ceil(d::preferredWidth(d::TextBlock(upper, tagStyle), b.env.text)) + 8;
    d::Layer& tag = b.layer(sub(this->id(), "format"));
    tag.setFrame({width - 4 - tagWidth, wellHeight - 3 - 4 - 12, tagWidth, 12});
    d::setBackground(tag, d::rgb(0x141414, .75));
    tag.add(d::makeText(b, sub(this->id(), "format.text"), upper, tagStyle, {4, 1.75, tagWidth - 8 + 1, 8.5}));
    d::Layer& track = b.layer(sub(this->id(), "progress"));
    track.setFrame({0, wellHeight - 3, width, 3}); d::setBackground(track, d::black(.25));
    d::Layer& fill = b.layer(sub(this->id(), "progress.fill"));
    fill.setFrame({0, 0, width * std::clamp(book.progress, 0., 1.), 3}); d::setBackground(fill, palette().yellow());
    track.add(fill);
    for (d::Layer* layer : {brighten_, &codeBar, &code, &coverLayer, &tag, &track}) well_->add(*layer);
    if (missing) {
        d::Layer& veil = b.layer(sub(this->id(), "missing"));
        veil.setFrame(wellBounds); d::setBackground(veil, d::black(.55));
        veil.add(d::makeText(b, sub(this->id(), "missing.text"), "MISSING", d::TextStyle{10, d::FontWeight::bold, palette().yellow(), 1, .1},
            {0, (wellHeight - 10) / 2, width, 10}, d::TextAlignment::center));
        well_->add(veil);
    }
    strip_ = &b.layer(sub(this->id(), "label"));
    strip_->setFrame({0, wellHeight, width, 24});
    label_ = &d::makeText(b, sub(this->id(), "label.text"), book.title, d::TextStyle{11.5, d::FontWeight::medium, d::fixed::nearBlack},
        {6, 6.25, width - 12, 11.5}, d::TextAlignment::center);
    strip_->add(*label_);
    more.layer().setFrame({width - 4 - 22, 8, 22, 22});
    more.layer().setZPosition(2);
    // The selection brackets: the .1 s fade on the container, the inset -5 ->
    // -4 path change (.16 s) as two prepared insets cross-faded (G2).
    brackets_ = &b.layer(sub(this->id(), "brackets"));
    bracketsRest_ = &b.layer(sub(this->id(), "brackets.rest"));
    bracketsSelected_ = &b.layer(sub(this->id(), "brackets.selected"));
    for (d::Layer* layer : {brackets_, bracketsRest_, bracketsSelected_}) { layer->setFrame(bounds); layer->setAnimatable(); }
    d::setPath(*bracketsRest_, d::ArchiveTile::bracketPath(bounds, -5), palette().yellow());
    d::setPath(*bracketsSelected_, d::ArchiveTile::bracketPath(bounds, -4), palette().yellow());
    brackets_->add(*bracketsRest_); brackets_->add(*bracketsSelected_);
    brackets_->setValue(Channel::opacity, 0);
    for (d::Layer* layer : {well_, strip_, &more.layer(), brackets_}) l.add(*layer);
    renderSelection(false);
    setTarget(bounds);
}
d::Control& ReaderBookTile::more() const noexcept { return *more_; }
void ReaderBookTile::setSelected(bool selected, bool animated) { if (selected != selected_) { selected_ = selected; renderSelection(animated); } }
void ReaderBookTile::renderSelection(bool animated) {
    const auto c = clock();
    d::motion::animate(*brackets_, Channel::opacity, selected_ ? 1 : 0, animated ? .1 : 0, c, d::easing::ease);
    d::motion::animate(*bracketsRest_, Channel::opacity, selected_ ? 0 : 1, animated ? .16 : 0, c);
    d::motion::animate(*bracketsSelected_, Channel::opacity, selected_ ? 1 : 0, animated ? .16 : 0, c);
    d::setBackground(*strip_, selected_ ? d::fixed::counterBackground : d::rgb(0xC4C4C6));
    d::editText(*label_, [&](d::TextBlock& block) { block.setColor(selected_ ? palette().yellow() : d::fixed::nearBlack); });
}
void ReaderBookTile::hoverChanged() {
    const bool on = hovered() && enabled();
    const auto c = clock();
    d::motion::animate(*brighten_, Channel::opacity, on ? 1 : 0, d::duration::hover, c, d::easing::ease);
    d::motion::animate(*well_, Channel::translateY, on ? -2 : 0, .25, c);
}

// --- ReaderCanvas: init and lifecycle (:93-117) -----------------------------------
ReaderCanvas::ReaderCanvas(d::Context& context, ReaderState& state, ReaderCanvasServices services)
    : Canvas(core::Module::reader, context), state_(&state), services_(std::move(services)) {
    tab_ = state.reading() && state.book() ? Tab::reading : Tab::library;
    tabSettled_ = state.book() != nullptr;
    // The page always renders on the paper tone at 1/1.4 (once, :98).
    state.setLayout(renderSize, false);
    detailTimer_ = timerGroup();
}
ReaderCanvas::~ReaderCanvas() { *alive_ = false; }

void ReaderCanvas::didActivate() {
    state_->setActive(true);
    refreshMissing();
}
void ReaderCanvas::didDeactivate() {
    closePanel();
    panStart_.reset();
    cancel(detailTimer_);
    settleTurn();
    state_->setActive(false);
}
void ReaderCanvas::willLeave() { closePanel(); }

// tab didSet (:19-21): any change settles the tab and remembers Reading.
void ReaderCanvas::setTab(Tab next) {
    if (next == tab_) return;
    tab_ = next;
    tabSettled_ = true;
    state_->setReading(tab_ == Tab::reading);
}
void ReaderCanvas::select(Tab next) {
    if (next == tab_) return;
    const double direction = int(next) > int(tab_) ? 1 : -1;
    setTab(next);
    navigate(direction);
}

std::string ReaderCanvas::errorText() const {
    if (!state_->error()) return {};
    if (const auto code = state_->errorCode()) return readerErrorMessage(*code, env().language);
    return *state_->error();
}
// The page layout of the Reading tab depends on the book, its page count and
// the direction, not on page turns, bookmarks or background loading (:119-129).
ReaderCanvas::Structure ReaderCanvas::structure() const {
    Structure s;
    s.tab = tab_; s.language = env().language;
    for (const auto& book : state_->library().books) {
        if (tab_ == Tab::library) s.books.push_back({book.id, book.title, std::int64_t(std::round(book.progress * 100)), missing_.count(book.id) > 0});
        else s.books.push_back({book.id, {}, 0, false});
    }
    if (const auto* book = state_->book()) {
        s.selected = book->id;
        if (tab_ == Tab::marks) for (const auto& mark : book->bookmarks) s.marks.push_back(mark.id);
    }
    const auto* current = state_->current();
    s.hasPage = current != nullptr;
    s.loading = !current && state_->loading();
    if (!current && state_->error()) s.error = errorText();
    if (current) s.pageCount = current->pageCount;
    s.awaiting = awaitingBook_.has_value();
    s.vertical = tab_ == Tab::reading && state_->library().preferences.vertical();
    return s;
}

void ReaderCanvas::stateChanged() { controllerChanged(); }
// controllerChanged (:131-142).
void ReaderCanvas::controllerChanged() {
    const std::optional<std::string> error = state_->error() ? std::optional(errorText()) : std::nullopt;
    if (error && error != lastError_ && isActive()) toast(d::Icon::warn, "Notice", "提示", *error);
    lastError_ = error;
    std::vector<std::string> books;
    for (const auto& book : state_->library().books) books.push_back(key(book));
    if (isActive() && books != checkedBooks_) refreshMissing();
    const auto* book = state_->book();
    const std::optional<std::string> id = book ? std::optional(book->id) : std::nullopt;
    if (id != imageBook_) { imageBook_ = id; imageView_ = ReaderImageView{}; }
    if (awaitingBook_ && id == awaitingBook_ && (state_->current() || state_->error())) awaitingBook_.reset();
    // The library loaded after this canvas was made: reopen the book that was being read.
    if (!tabSettled_ && book) { if (state_->reading()) setTab(Tab::reading); tabSettled_ = true; }
    if (!renderedLanguage()) return;
    // The Mac skips a change while a sub-page swap runs because it builds the
    // next page when the swap lands; Windows built it when the swap started
    // (LIBRARY-DESIGN Q4), so a changed structure rebuilds that pending page
    // (Canvas::rerender during a swap): an import that finishes loading while
    // the Reading tab slides in lands on the book, not on LOADING.
    if (transitioning()) { if (!renderedStructure_ || structure() != *renderedStructure_) rerender(); return; }
    if (!renderedStructure_ || structure() != *renderedStructure_) rerender(); else for (const auto& binder : std::vector(binders_)) binder();
    header().setCounter(counter());
}

// handle (:144-163).
void ReaderCanvas::handleEvent(std::string_view action) {
    if (action == "imported") {
        if (!pendingImport_) return;
        const auto [name, shelf] = *std::exchange(pendingImport_, std::nullopt);
        if (shelf) toast(d::Icon::folder, "From shelf", "从暂存架添加", name);
        else toast(d::Icon::open, "Book added", "已加入书库", name);
        refreshMissing();
        if (tab_ != Tab::reading) select(Tab::reading);
    } else if (action == "deleted") {
        if (std::exchange(pendingRemoval_, false)) toast(d::Icon::trash, "Removed", "已从书库移除");
    } else if (action == "bookmarked") {
        if (!pendingBookmark_) return;
        const auto [added, page] = *std::exchange(pendingBookmark_, std::nullopt);
        if (added) toast(d::Icon::bookmark, "Bookmarked", "已添加书签", "P. " + std::to_string(page));
        else toast(d::Icon::bookmark, "Bookmark removed", "已移除书签", "P. " + std::to_string(page));
    }
}

std::string ReaderCanvas::counter() const {
    if (tab_ == Tab::reading) if (const auto number = pageNumber()) return "P. " + std::to_string(number->current);
    return d::format::pad2(std::int64_t(state_->library().books.size()));
}

// --- Missing files (:177-203) ------------------------------------------------------
// Every book's file is checked off the UI thread (the host's utility
// executor); a set that differs from the shown one re-renders.
void ReaderCanvas::refreshMissing() {
    const auto token = ++missingGeneration_;
    const auto& books = state_->library().books;
    checkedBooks_.clear();
    for (const auto& book : books) checkedBooks_.push_back(key(book));
    if (!services_.checkMissing) return;
    std::weak_ptr<bool> alive = alive_;
    services_.checkMissing(books, [this, alive, token](std::set<std::string> result) {
        const auto living = alive.lock();
        if (!living || !*living || missingGeneration_ != token || result == missing_) return;
        missing_ = std::move(result);
        controllerChanged();
    });
}

// --- Pages (:207-367) -----------------------------------------------------------
void ReaderCanvas::buildPage(d::Page& page) {
    binders_.clear();
    renderedStructure_ = structure();
    auto& tabs = page.makeTabs("readerTabs", {{text("Open", "打开"), d::Icon::open}, {text("Library", "书库"), d::Icon::apps},
        {text("Reading", "阅读"), d::Icon::text}, {text("Marks", "书签"), d::Icon::bookmark}}, int(tab_), d::metric::boxWidth);
    tabs.onSelect = [this](int index, double direction) {
        if (index < 0 || index > 3) return;
        setTab(Tab(index));
        navigate(direction);
    };
    tabs.layer().setOrigin({0, d::metric::headerHeight});
    page.layer().add(tabs.layer());
    page.tabs = &tabs;
    page.add(tabs.controls());
    page.stagger(tabs.layer());
    switch (tab_) {
    case Tab::open: buildOpen(page); break;
    case Tab::library: buildLibrary(page); break;
    case Tab::reading: buildReading(page); break;
    case Tab::marks: buildMarks(page); break;
    }
    // Reading names the open book in the breadcrumb instead of on the paper.
    std::vector<std::string> crumbs{"E-READER"};
    if (tab_ == Tab::reading && state_->current()) if (const auto* book = state_->book()) {
        const auto& rules = env().characters;
        crumbs.push_back(rules.count(book->title) > 44 ? rules.prefix(book->title, 43) + "…" : book->title);
    }
    header().configure(std::move(crumbs), "MOD-10", title("E-Reader", "阅读器"), counter(), false);
}
// A page installed without the paper drops the page layers of the one that
// left (they were drawn while it left, as the Mac builds the next page late).
void ReaderCanvas::didInstall(d::Page&) {
    if (tab_ == Tab::reading && clip_) return;
    slots_ = {}; views_.clear();
    clip_ = column_ = ribbon_ = pageNumberText_ = nullptr;
    pageSlider_ = nullptr; bookmarkButton_ = previousButton_ = nextButton_ = settingsButton_ = nullptr;
    pendingTurn_.reset(); turnToken_.reset();
}

void ReaderCanvas::buildOpen(d::Page& page) {
    const d::Size size{d::metric::boxWidth, 300};
    auto& panel = d::emptyState::make(page.builder(), "reader.open", "OPEN A BOOK", std::string("PDF / EPUB / TXT"), std::nullopt, size, 16 + 32);
    auto& choose = page.make<d::Button>("chooseFile", text("Choose file", "选择文件"), d::Icon::open, d::Button::Kind::primary);
    auto& shelf = page.make<d::Button>("fromShelf", text("From shelf", "从暂存架"), d::Icon::folder, d::Button::Kind::secondary);
    const double total = choose.preferredWidth() + 8 + shelf.preferredWidth();
    const double top = d::emptyState::textBottom(size, true, 16 + 32) + 16;
    choose.place({std::round((size.width - total) / 2), std::round(top)});
    shelf.place({choose.layer().frame().x + choose.layer().frame().width + 8, std::round(top)});
    shelf.setEnabled(!availableShelfChoices().empty());
    choose.onActivate = [this] { chooseFile(); };
    shelf.onActivate = [this, &shelf] { openShelfMenu(shelf); };
    panel.setOrigin({0, bodyTop});
    panel.add(choose.layer()); panel.add(shelf.layer());
    page.layer().add(panel);
    page.add(std::vector<d::Control*>{&choose, &shelf});
    page.stagger(panel);
}

void ReaderCanvas::buildLibrary(d::Page& page) {
    const auto& books = state_->library().books;
    const Rect frame{0, bodyTop, d::metric::boxWidth, d::metric::boxHeight - bodyTop};
    if (books.empty()) {
        auto& empty = d::emptyState::make(page.builder(), "reader.empty", "NO BOOKS", std::string("PDF / EPUB / TXT"), std::nullopt, {frame.width, 300});
        empty.setOrigin({frame.x, frame.y});
        page.layer().add(empty);
        page.stagger(empty);
        return;
    }
    auto& scroll = page.scrollArea("library", frame);
    const auto rows = (books.size() + 3) / 4;
    const double tileHeight = ReaderBookTile::wellHeight + 24;
    scroll.setContentHeight(4 + double(rows) * (tileHeight + 12) - 12 + 4);
    const double width = scroll.contentWidth() - 8, tileWidth = std::floor((width - 3 * 12) / 4);
    const auto* open = state_->book();
    for (std::size_t index = 0; index < books.size(); ++index) {
        const auto& book = books[index];
        auto& tile = page.make<ReaderBookTile>("book:" + book.id, int(index) + 1, book, d::rgb(coverColor(book.id)), missing_.count(book.id) > 0, tileWidth);
        tile.setSelected(open && open->id == book.id, false);
        const std::string id = book.id;
        tile.onActivate = [this, id] { for (const auto& b : state_->library().books) if (b.id == id) { openBook(b); return; } };
        auto& more = tile.more();
        more.onActivate = [this, id, &more] {
            if (menuOpen()) { closeMenu(); return; }
            for (const auto& b : state_->library().books) if (b.id == id) { openBookMenu(b, more); return; }
        };
        tile.layer().setOrigin({4 + double(index % 4) * (tileWidth + 12), 4 + double(index / 4) * (tileHeight + 12)});
        scroll.content().add(tile.layer());
        page.add(std::vector<d::Control*>{&tile, &tile.more()});
        page.stagger(tile.layer());
    }
    page.restoreOffset(scroll);
}

void ReaderCanvas::buildReading(d::Page& page) {
    const Rect frame = pageFrame;
    slots_ = {}; views_.clear();
    clip_ = column_ = ribbon_ = pageNumberText_ = nullptr;
    pageSlider_ = nullptr; bookmarkButton_ = previousButton_ = nextButton_ = settingsButton_ = nullptr;
    pendingTurn_.reset(); turnToken_.reset();
    if (!state_->current() || !state_->book() || awaitingBook_) {
        const bool loading = state_->loading() || awaitingBook_;
        const std::string title = loading ? "LOADING" : state_->error() ? "UNAVAILABLE" : "OPEN A BOOK";
        auto& empty = d::emptyState::make(page.builder(), "reader.status", title, loading ? std::nullopt : std::optional<std::string>("PDF / EPUB / TXT"),
            std::nullopt, {frame.width, frame.height});
        empty.setOrigin({frame.x, frame.y});
        page.layer().add(empty);
        page.stagger(empty);
        return;
    }
    auto& paper = page.makeLayer("reader.paper");
    paper.setFrame(frame);
    d::setBorder(paper, d::rgb(0xEFEDE6), d::rgb(0xC9C7BF));   // ReaderDocument.paper, a 1 px #C9C7BF ring
    paper.setMasksToBounds(true);
    auto& clip = page.makeLayer("reader.viewport");
    clip.setFrame(pageViewport); clip.setMasksToBounds(true);
    auto& column = page.makeLayer("reader.column");
    column.setFrame({0, 0, pageViewport.width, pageViewport.height});
    column.setAnimatable();                                     // hud.reader.scroll
    clip.add(column);
    for (std::size_t n = 0; n < slots_.size(); ++n) {
        auto& layer = page.makeLayer("reader.page." + std::to_string(n));
        layer.setAnimatable();                                  // hud.reader.turn; drawn natively (pageViews)
        layer.setHidden(true);
        column.add(layer);
        slots_[n].layer = &layer;
    }
    // The bookmark ribbon: a notched 16 x 26 polygon in the theme yellow, its
    // own part so the native page draws lie between the paper and it.
    auto& mark = page.makeLayer("reader.ribbon");
    mark.setFrame({frame.width - 24 - 16, 0, 16, 26});
    d::setPath(mark, d::PathBuilder().move({0, 0}).line({16, 0}).line({16, 26}).line({8, 19.5}).line({0, 26}).close(), env().palette.yellow());
    mark.setPartRoot(true);
    paper.add(clip); paper.add(mark);
    page.layer().add(paper);
    clip_ = &clip; column_ = &column; ribbon_ = &mark;
    page.stagger(paper);

    // ◀ slider ▶  37 / 100  bookmark  settings, one 30 px row under the paper.
    auto& row = page.makeLayer("reader.controls");
    auto& previous = page.make<d::IconButton>("previous", text("Previous page", "上一页"), d::Icon::left);
    auto& next = page.make<d::IconButton>("next", text("Next page", "下一页"), d::Icon::right);
    auto& bookmark = page.make<d::IconButton>("bookmark", text("Bookmark", "书签"), d::Icon::bookmark);
    auto& gear = page.make<d::IconButton>("settings", text("Reading settings", "阅读设置"), d::Icon::gear);
    const double numberWidth = 64, gap = 8;
    const auto number = pageNumber();
    const double total = number ? double(number->total) : 100;
    auto& slider = page.make<d::Slider>("page", text("Page", "页码"), double(number ? number->current : 1),
        d::Slider::Range{1, std::max(2., total), 1, std::nullopt}, d::ValueFormat::page, d::metric::boxWidth - 4 * 30 - 5 * gap - numberWidth, true, false, false);
    hostBubble(slider);
    slider.onChange = [this](double value, bool final) {
        if (!final) return;
        jump(std::int64_t(std::round(value)));
        if (context().onInputStateChange) context().onInputStateChange();
    };
    previous.onActivate = [this] { turn(-1); };
    next.onActivate = [this] { turn(1); };
    bookmark.onActivate = [this] { toggleBookmark(); };
    gear.onActivate = [this] { toggleSettings(); };
    row.setFrame({0, controlsTop, d::metric::boxWidth, 30});
    double x = 0;
    for (d::Layer* layer : {&previous.layer(), &slider.layer(), &next.layer()}) {
        layer->setOrigin({x, 0}); row.add(*layer);
        x += layer->bounds().width + gap;
    }
    auto& numberText = d::makeText(page.builder(), "reader.number", "", d::TextStyle{10, d::FontWeight::bold, env().palette.tone(0x9A9A9A), 1, 0, false, true},
        {x, 10, numberWidth, 10}, d::TextAlignment::right);
    row.add(numberText);
    x += numberWidth + gap;
    for (d::Layer* layer : {&bookmark.layer(), &gear.layer()}) {
        layer->setOrigin({x, 0}); row.add(*layer);
        x += layer->bounds().width + gap;
    }
    page.layer().add(row);
    pageNumberText_ = &numberText;
    previousButton_ = &previous; nextButton_ = &next; bookmarkButton_ = &bookmark; pageSlider_ = &slider; settingsButton_ = &gear;
    page.add(std::vector<d::Control*>{&previous, &slider, &next, &bookmark, &gear});
    page.stagger(row);
    binders_.push_back([this] { renderPage(); });
    renderPage();
}

// renderPage (:369-385): pages, number, ribbon, slider and button states.
void ReaderCanvas::renderPage() {
    const auto* current = state_->current();
    if (!current || !clip_) return;
    const auto* book = state_->book();
    const bool bookmarked = book && std::any_of(book->bookmarks.begin(), book->bookmarks.end(), [&](const ReaderBookmark& m) { return m.location == current->location; });
    const auto number = pageNumber();
    const std::string numberText = number ? std::to_string(number->current) + " / " + std::to_string(number->total) : std::string{};
    if (d::textOf(*pageNumberText_).text() != numberText) d::editText(*pageNumberText_, [&](d::TextBlock& block) { block.setText(numberText); });
    ribbon_->setHidden(!bookmarked);
    layoutPages();
    current = state_->current();
    if (!current) return;
    if (pageSlider_ && !pageSlider_->dragging() && number) pageSlider_->setValue(double(number->current));
    const bool vertical = state_->library().preferences.vertical();
    previousButton_->setEnabled(current->previous.has_value() || (vertical && scrollOffset_ > 0));
    nextButton_->setEnabled(current->next.has_value());
    bookmarkButton_->setEnabled(true);
    bookmarkButton_->setOn(bookmarked);
}

// --- Page layers (:387-594) --------------------------------------------------------
// Top to bottom, the pages drop their top and bottom margins and run on in a
// column that keeps one margin above and below on the paper.
double ReaderCanvas::columnInset() const {
    return std::round(std::min(pageViewport.height * .4, state_->library().preferences.margin * textScale));
}
// A text page ends one line spacing below its last line, so the next page's first line follows on.
double ReaderCanvas::columnHeight(const ReaderPage& page) const {
    const double full = pageViewport.height - 2 * columnInset();
    if (!page.textHeight) return full;
    return std::min(full, std::round((*page.textHeight + state_->library().preferences.lineSpacing) * textScale));
}
// The rendered pages around the current one: the page that leads to it
// (exactly, when it was read from there) and up to three that follow.
ReaderCanvas::Column ReaderCanvas::column(const ReaderPage& current) const {
    Column out;
    const auto pages = state_->pages();
    for (const auto& p : pages) if (p.next && *p.next == current.location && !(p.location == current.location)) { out.before = &p; break; }
    if (!out.before && current.previous)
        for (const auto& p : pages) if (p.location == *current.previous && !(p.location == current.location)) { out.before = &p; break; }
    const ReaderPage* last = &current;
    while (out.after.size() < 3 && last->next) {
        const ReaderPage* found{};
        for (const auto& p : pages) if (p.location == *last->next) { found = &p; break; }
        if (!found) break;
        out.after.push_back(found);
        last = found;
    }
    return out;
}
ReaderCanvas::Slot& ReaderCanvas::pageLayer(const ReaderPage& page) {
    for (auto& slot : slots_) if (slot.location && *slot.location == page.location) return slot;
    Slot* spare = nullptr;
    for (auto& slot : slots_) if (!slot.location) { spare = &slot; break; }
    if (!spare) spare = &slots_.back();                         // never more than six are laid out
    spare->location = page.location;
    spare->pixels = page.pixels;
    spare->layer->setHidden(false);
    return *spare;
}
// Hides every page layer except `keep` (:436-443).
void ReaderCanvas::recycle(std::span<const ReaderLocation> keep) {
    for (auto& slot : slots_) {
        if (!slot.location || std::find(keep.begin(), keep.end(), *slot.location) != keep.end()) continue;
        slot.layer->removeAllMotion();
        slot.layer->setValue(Channel::translateX, 0);
        slot.layer->setHidden(true);
        slot.location.reset(); slot.pixels.reset();
    }
}
// layoutPages (:445-501): one page (left to right, or a zoomed picture), or
// the scrolled column (top to bottom).
void ReaderCanvas::layoutPages() {
    const auto* current = state_->current();
    if (!current || !clip_) return;
    const auto preferences = state_->library().preferences;
    if (columnKey_ != preferences) { columnKey_ = preferences; scrollOffset_ = 0; }
    const bool zoomed = current->illustration && imageView_.zoom > 1;
    const d::Size size{pageViewport.width, pageViewport.height};
    if (preferences.vertical() && !zoomed) {
        pendingTurn_.reset();
        if (columnLocation_ != current->location) { columnLocation_ = current->location; scrollOffset_ = 0; }
        clampOffset();
        current = state_->current();
        if (!current) return;
        // The whole rendered column stays laid out: a running scroll eases
        // across pages outside the viewport.
        const auto [before, after] = column(*current);
        std::vector<std::pair<const ReaderPage*, double>> placed{{current, -scrollOffset_}};
        if (before) placed.emplace_back(before, -scrollOffset_ - columnHeight(*before));
        double y = -scrollOffset_ + columnHeight(*current);
        for (const auto* p : after) { placed.emplace_back(p, y); y += columnHeight(*p); }
        std::vector<ReaderLocation> keep;
        for (const auto& [p, top] : placed) keep.push_back(p->location);
        recycle(keep);
        const double inset = columnInset();
        clip_->setFrame({pageViewport.x, pageViewport.y + inset, pageViewport.width, pageViewport.height - 2 * inset});
        column_->setFrame({0, 0, clip_->bounds().width, clip_->bounds().height});
        for (const auto& [p, top] : placed) {
            auto& slot = pageLayer(*p);
            slot.pixels = p->pixels;
            slot.cropTop = inset / size.height; slot.cropHeight = columnHeight(*p) / size.height;
            slot.layer->setFrame({0, top, size.width, columnHeight(*p)});
        }
        publishViews();
        return;
    }
    if (pendingTurn_ && !(pendingTurn_->from == current->location) && !env().reduceMotion) {
        const auto pending = *std::exchange(pendingTurn_, std::nullopt);
        slide(pending.from, *current, pending.direction);
        return;
    }
    if (!turnToken_) { const ReaderLocation keep[]{current->location}; recycle(keep); }
    auto& slot = pageLayer(*current);
    clip_->setFrame(pageViewport);
    column_->setFrame({0, 0, pageViewport.width, pageViewport.height});
    slot.cropTop = 0; slot.cropHeight = 1;
    const auto& detail = state_->imageDetail();
    if (detail && detail->view == imageView_ && detail->page.location == current->location) {
        slot.pixels = detail->page.pixels;
        slot.layer->setFrame({0, 0, size.width, size.height});
    } else {
        slot.pixels = current->pixels;
        const double z = current->illustration ? imageView_.zoom : 1;
        const core::Point pan = current->illustration ? imageView_.pan : core::Point{};
        slot.layer->setFrame({size.width * (1 - z) / 2 + pan.x * textScale, size.height * (1 - z) / 2 + pan.y * textScale, size.width * z, size.height * z});
    }
    publishViews();
}
// Left to right: the new page slides in over the paper while the old one
// leaves; only the page layers move (:503-533).
void ReaderCanvas::slide(const ReaderLocation& old, const ReaderPage& page, double direction) {
    const double width = pageViewport.width;
    Slot* outgoing = nullptr;
    for (auto& slot : slots_) if (slot.location && *slot.location == old) outgoing = &slot;
    const ReaderLocation keep[]{old, page.location};
    recycle(keep);
    auto& incoming = pageLayer(page);
    incoming.pixels = page.pixels;
    incoming.cropTop = 0; incoming.cropHeight = 1;
    incoming.layer->setFrame({0, 0, pageViewport.width, pageViewport.height});
    if (outgoing) { const auto f = outgoing->layer->frame(); outgoing->layer->setFrame({-direction * width, 0, f.width, f.height}); outgoing->layer->setZPosition(0); }
    incoming.layer->setZPosition(1);
    const auto token = ++turnSerial_;
    turnToken_ = token;
    const auto c = clock();
    for (Slot* slot : {&incoming, outgoing}) {
        if (!slot) continue;
        slot->layer->setValue(Channel::translateX, direction * width);
        d::motion::animate(*slot->layer, Channel::translateX, 0, .3, c, d::easing::standard);
    }
    d::motion::after(*incoming.layer, c.now + .3, c, [this, token] {
        if (turnToken_ != token) return;
        turnToken_.reset();
        if (const auto* current = state_->current()) { const ReaderLocation keep[]{current->location}; recycle(keep); } else recycle({});
        publishViews();
    });
    publishViews();
}
// Lands a running or pending page motion at once (:535-543).
void ReaderCanvas::settleTurn() {
    pendingTurn_.reset();
    if (!turnToken_) return;
    turnToken_.reset();
    if (column_) column_->setValue(Channel::translateY, 0);
    if (clip_ && !state_->library().preferences.vertical()) {
        if (const auto* current = state_->current()) { const ReaderLocation keep[]{current->location}; recycle(keep); } else recycle({});
    }
    for (auto& slot : slots_) if (slot.layer) slot.layer->setValue(Channel::translateX, 0);
    publishViews();
}
// Top to bottom: keeps the offset within the rendered column, moving the
// current page forward or back as its edge passes the viewport's top (:545-561).
void ReaderCanvas::clampOffset() {
    for (int n = 0; n < 8; ++n) {
        const auto* current = state_->current();
        if (!current) return;
        const auto [before, after] = column(*current);
        if (scrollOffset_ >= columnHeight(*current) && !after.empty()) {
            scrollOffset_ -= columnHeight(*current); columnLocation_ = after.front()->location; state_->jump(after.front()->location);
        } else if (scrollOffset_ < 0 && before) {
            scrollOffset_ += columnHeight(*before); columnLocation_ = before->location; state_->jump(before->location);
        } else {
            double below = columnHeight(*current);
            for (const auto* p : after) below += columnHeight(*p);
            scrollOffset_ = std::min(std::max(0., below - columnHeight(*current)), std::max(0., scrollOffset_));
            return;
        }
    }
}
// The top of a laid-out column page in the viewport (paper px, :563-572).
std::optional<double> ReaderCanvas::columnTop(const ReaderLocation& location) const {
    const auto* current = state_->current();
    if (!current) return std::nullopt;
    if (location == current->location) return -scrollOffset_;
    const auto [before, after] = column(*current);
    if (before && before->location == location) return -scrollOffset_ - columnHeight(*before);
    double y = -scrollOffset_ + columnHeight(*current);
    for (const auto* p : after) { if (p->location == location) return y; y += columnHeight(*p); }
    return std::nullopt;
}
// Scrolls the column by `delta` box px; `animated` eases there (keys,
// buttons, :574-594). The state's jump publishes its page change later (the
// host posts it), so the page is laid out here at once in every case.
void ReaderCanvas::scrollColumn(double delta, bool animated) {
    const auto* current = state_->current();
    if (!current || delta == 0) return;
    const auto anchor = current->location;
    settleTurn();
    const double oldTop = -scrollOffset_;
    scrollOffset_ += delta;
    clampOffset();
    renderPage();
    if (!animated || env().reduceMotion || !column_) return;
    const auto newTop = columnTop(anchor);
    if (!newTop || std::abs(oldTop - *newTop) <= .5) return;
    const auto token = ++turnSerial_;
    turnToken_ = token;
    const auto c = clock();
    column_->setValue(Channel::translateY, oldTop - *newTop);
    d::motion::animate(*column_, Channel::translateY, 0, .32, c, d::easing::standard);
    d::motion::after(*column_, c.now + .32, c, [this, token] { if (turnToken_ == token) turnToken_.reset(); });
}
void ReaderCanvas::publishViews() {
    views_.clear();
    for (const auto& slot : slots_) {
        if (!slot.layer || !slot.location || !slot.pixels || slot.layer->hidden()) continue;
        views_.push_back({slot.layer, slot.pixels, slot.cropTop, slot.cropHeight, slot.layer->zPosition()});
    }
    std::stable_sort(views_.begin(), views_.end(), [](const PageView& a, const PageView& b) { return a.zPosition < b.zPosition; });
}
bool ReaderCanvas::pagesAnimating(double now) const noexcept {
    if (column_ && column_->animating(now)) return true;
    for (const auto& slot : slots_) if (slot.layer && slot.location && slot.layer->animating(now)) return true;
    return false;
}

void ReaderCanvas::buildMarks(d::Page& page) {
    const auto* book = state_->book();
    const Rect frame{0, bodyTop, d::metric::boxWidth, d::metric::boxHeight - bodyTop};
    if (!book || book->bookmarks.empty()) {
        auto& empty = d::emptyState::make(page.builder(), "reader.marks.empty", "NO BOOKMARKS");
        empty.setOrigin({frame.x, frame.y});
        page.layer().add(empty);
        page.stagger(empty);
        return;
    }
    const auto& marks = book->bookmarks;
    auto& scroll = page.scrollArea("marks", frame);
    scroll.setContentHeight(double(marks.size()) * (d::metric::rowHeight + d::metric::rowGap) - d::metric::rowGap);
    const double width = scroll.contentWidth();
    for (std::size_t index = 0; index < marks.size(); ++index) {
        const auto& mark = marks[index];
        const auto number = ReaderCanvas::page(mark, *book);
        const auto ordinal = d::format::pad2(std::int64_t(index) + 1);
        const std::string_view arguments[]{ordinal};
        auto& row = page.make<d::RowPlate>("mark:" + mark.id, text("Bookmark {0}", "书签 {0}", arguments), "P. " + std::to_string(number),
            d::RowPlate::Variant::standard, "›", width);
        auto& go = page.make<d::Button>("mark:" + mark.id + ":go", text("Go", "前往"), std::nullopt, d::Button::Kind::secondary, d::Button::Height::compact);
        go.place({0, 0});
        const auto location = mark.location;
        go.onActivate = [this, location] {
            state_->jump(location);
            setTab(Tab::reading);
            navigate(-1);
        };
        auto& remove = page.make<d::IconButton>("mark:" + mark.id + ":remove", text("Remove bookmark", "移除书签") + " P. " + std::to_string(number), d::Icon::x);
        const std::string id = mark.id;
        remove.onActivate = [this, id] { state_->removeBookmark(id); };
        row.setCluster({&go.layer(), &remove.layer()});
        row.layer().setOrigin({0, double(index) * (d::metric::rowHeight + d::metric::rowGap)});
        scroll.content().add(row.layer());
        page.add(row);
        page.add(std::vector<d::Control*>{&go, &remove});
        page.stagger(row.layer());
    }
    page.restoreOffset(scroll);
}

// --- Library actions (:632-692) --------------------------------------------------------
// The sum of the UUID's 16 raw bytes, modulo 8 (withUnsafeBytes(of: id.uuid)).
std::uint32_t ReaderCanvas::coverColor(std::string_view uuid) {
    unsigned sum = 0;
    int high = -1;
    for (const char c : uuid) {
        const int v = hexDigit(c);
        if (v < 0) continue;
        if (high < 0) high = v; else { sum += unsigned(high * 16 + v); high = -1; }
    }
    return coverColors[sum % coverColors.size()];
}
// A book's page in the reader's numbering: PDF pages, otherwise P. 1-100.
std::int64_t ReaderCanvas::page(const ReaderBookmark& mark, const ReaderBook& book) {
    return lowercase(extension(book)) == "pdf" ? mark.location.section + 1 : std::max<std::int64_t>(1, std::int64_t(std::round(mark.progress * 100)));
}
std::optional<ReaderCanvas::PageNumber> ReaderCanvas::pageNumber() const {
    const auto* page = state_->current();
    if (!page) return std::nullopt;
    if (page->pageCount) return PageNumber{page->location.section + 1, *page->pageCount};
    return PageNumber{std::max<std::int64_t>(1, std::int64_t(std::round(page->progress * 100))), 100};
}
void ReaderCanvas::openBook(const ReaderBook& book) {
    if (missing_.count(book.id)) { relink(book); return; }
    const auto* open = state_->book();
    if (!open || open->id != book.id) { awaitingBook_ = book.id; state_->select(book.id); }
    setTab(Tab::reading);
    navigate(1);
}
void ReaderCanvas::openBookMenu(const ReaderBook& book, const d::Control& anchor) {
    auto open = item("open", text("Open", "打开")), relinkItem = item("relink", text("Relink…", "重新链接…")),
        remove = item("remove", text("Remove from library", "从书库移除"));
    remove.danger = true; remove.separatorBefore = true;
    const std::string id = book.id;
    openMenu("bookMenu", {open, relinkItem, remove}, anchor, 170.0, [this, id](const d::Menu::Item& item) {
        const auto& books = state_->library().books;
        const auto found = std::find_if(books.begin(), books.end(), [&](const ReaderBook& b) { return b.id == id; });
        if (found == books.end()) return;
        const auto book = *found;
        if (item.id == "open") openBook(book);
        else if (item.id == "relink") relink(book);
        else confirmRemove(book);
    });
}
void ReaderCanvas::confirmRemove(const ReaderBook& book) {
    const std::string_view arguments[]{book.title};
    d::ConfirmRequest request;
    request.id = "readerRemove";
    request.title = text("Remove {0} from the library?", "从书库移除 {0}？", arguments);
    request.message = text("The file itself stays on disk.", "文件本身仍保留在磁盘上。");
    request.confirmTitle = text("Remove", "移除");
    request.danger = true; request.icon = d::Icon::trash;
    const std::string id = book.id;
    confirm(std::move(request), [this, id] {
        pendingRemoval_ = true;
        state_->remove(id);
    });
}
void ReaderCanvas::relink(const ReaderBook& book) {
    const std::string_view arguments[]{book.title};
    presentPanel(text("Locate {0}", "查找 {0}", arguments), book.id);
}
// The chosen file replaced the book's reference (:684-690).
void ReaderCanvas::relinked(std::string_view id, bool success) {
    if (!success) return;
    const auto& books = state_->library().books;
    const auto found = std::find_if(books.begin(), books.end(), [&](const ReaderBook& b) { return b.id == id; });
    missing_.erase(std::string(id));
    toast(d::Icon::link2, "Book relinked", "已重新链接", found == books.end() ? std::optional<std::string>{} : std::optional(found->title));
    refreshMissing();
    controllerChanged();
}

// --- Open actions (:694-751) --------------------------------------------------------
std::vector<ReaderShelfChoice> ReaderCanvas::availableShelfChoices() const { return services_.shelfChoices ? services_.shelfChoices() : std::vector<ReaderShelfChoice>{}; }
void ReaderCanvas::chooseFile() { presentPanel(std::nullopt, {}); }
bool ReaderCanvas::acceptsImport() const noexcept { return isActive(); }
void ReaderCanvas::prepareImport() { closePopups(); }
void ReaderCanvas::importBegan(std::string name, bool shelf) { pendingImport_ = std::pair{std::move(name), shelf}; }
void ReaderCanvas::importFailed(std::string message) {
    pendingImport_.reset();
    if (isActive() && !message.empty()) toast(d::Icon::warn, "Notice", "提示", std::move(message));
}
void ReaderCanvas::openShelfMenu(const d::Control& anchor) {
    const auto choices = availableShelfChoices();
    if (choices.empty()) return;
    std::vector<d::Menu::Item> items;
    for (const auto& choice : choices) items.push_back(item(choice.id, choice.title));
    openMenu("shelf", std::move(items), anchor, 220.0, [this, choices](const d::Menu::Item& item) {
        const auto found = std::find_if(choices.begin(), choices.end(), [&](const ReaderShelfChoice& c) { return c.id == item.id; });
        if (found == choices.end() || !services_.openShelf) return;
        pendingImport_ = std::pair{found->title, true};
        services_.openShelf(found->id);
    });
}
// The chooser sheet (:728-746): one at a time; it keeps the HUD up and the plane still.
bool ReaderCanvas::presentPanel(std::optional<std::string> title, std::string relinkBook) {
    if (panelOpen_ || !services_.presentPanel) return false;
    if (!services_.presentPanel(std::move(title), std::move(relinkBook))) return false;
    panelOpen_ = true;
    if (context().onInputStateChange) context().onInputStateChange();
    return true;
}
void ReaderCanvas::panelClosed() {
    if (!std::exchange(panelOpen_, false)) return;
    if (context().onInputStateChange) context().onInputStateChange();
}
void ReaderCanvas::closePanel() {
    if (!std::exchange(panelOpen_, false)) return;
    if (services_.closePanel) services_.closePanel();
    if (context().onInputStateChange) context().onInputStateChange();
}

// --- Reading actions (:755-886) --------------------------------------------------------
// One page forward or back. Left to right the page slides; top to bottom the
// column eases to the next page's top (or back to this or the previous page's top).
void ReaderCanvas::turn(int direction) {
    if (tab_ != Tab::reading || !state_->active()) return;
    const auto* current = state_->current();
    if (!current) return;
    if (state_->library().preferences.vertical() && !(current->illustration && imageView_.zoom > 1)) {
        const auto [before, after] = column(*current);
        if (direction > 0) {
            if (!current->next) return;
            if (after.empty()) state_->next(); else scrollColumn(columnHeight(*current) - scrollOffset_, true);
        } else if (scrollOffset_ > .5) scrollColumn(-scrollOffset_, true);
        else if (before) scrollColumn(-columnHeight(*before), true);
        else if (current->previous) state_->previous();
        else return;
        ++pageTurnSequence_;
        return;
    }
    if (direction > 0 ? !current->next : !current->previous) return;
    settleTurn();
    if (!env().reduceMotion) pendingTurn_ = PendingTurn{current->location, double(direction)};
    if (direction > 0) state_->next(); else state_->previous();
    ++pageTurnSequence_;
}
void ReaderCanvas::jump(std::int64_t number) {
    const auto* current = state_->current();
    if (!current) return;
    settleTurn();
    if (current->pageCount) state_->jump((double(number) - .5) / double(std::max<std::int64_t>(1, *current->pageCount)));
    else state_->jump(number <= 1 ? 0 : double(number) / 100);
}
void ReaderCanvas::toggleBookmark() {
    const auto* current = state_->current();
    const auto* book = state_->book();
    const auto number = pageNumber();
    if (!current || !book || !number || !services_.newID) return;
    const bool added = std::none_of(book->bookmarks.begin(), book->bookmarks.end(), [&](const ReaderBookmark& m) { return m.location == current->location; });
    pendingBookmark_ = std::pair{added, number->current};
    state_->toggleBookmark(services_.newID());
}
// Settings pop-over (spec 3.15): direction, font, font size, line spacing and
// page margins; values change in place while it is open (:793-869).
void ReaderCanvas::toggleSettings() {
    if (popoverOpen()) { closePopover(); return; }
    if (settingsButton_) openSettings(*settingsButton_);
}
void ReaderCanvas::openSettings(const d::Control& anchor) {
    const double width = 270, inner = width - 24, rowHeight = 26;
    struct Stepper { const char* id; std::string title; double ReaderPreferences::* key; double minimum, maximum; };
    const std::array<Stepper, 3> steppers{{{"fontSize", text("Font size", "字号"), &ReaderPreferences::fontSize, 10, 32},
        {"lineSpacing", text("Line spacing", "行间距"), &ReaderPreferences::lineSpacing, 0, 18},
        {"margin", text("Page margins", "页边距"), &ReaderPreferences::margin, 6, 48}}};
    auto popover = std::make_unique<d::Popover>(env(), "", std::nullopt, width, 30 + 10 + double(steppers.size() + 1) * (rowHeight + 8) - 8, false);
    auto& b = popover->builder();
    std::vector<d::Control*> controls;
    auto& direction = b.make<d::SegmentedControl>("direction", text("Reading direction", "阅读方向"),
        std::vector<std::string>{text("Left to right", "从左到右"), text("Top to bottom", "从上到下")},
        state_->library().preferences.vertical() ? 1 : 0, inner);
    direction.onChange = [this](int index) { updatePreferences([index](ReaderPreferences& p) { p.setVertical(index == 1); }); };
    controls.push_back(&direction);
    for (auto* segment : direction.segments()) controls.push_back(segment);
    auto& font = b.make<d::Dropdown>("font", text("Font", "字体"), state_->library().preferences.fontName, std::nullopt, d::Dropdown::Height::compact, 150.0);
    font.onActivate = [this, &font] { openFontMenu(font); };
    controls.push_back(&font);
    std::vector<std::pair<std::string, d::Layer*>> rows{{text("Font", "字体"), &font.layer()}};
    auto updates = std::make_shared<std::vector<std::function<void()>>>();
    for (const auto& stepper : steppers) {
        auto& value = d::makeText(b, std::string("reader.stepper.") + stepper.id + ".value", "",
            d::TextStyle{12, d::FontWeight::bold, env().palette.token(d::ThemeToken::textOnDark), 1, 0, false, true}, {26, 7, 36, 12}, d::TextAlignment::center);
        auto& minus = b.make<d::IconButton>(std::string(stepper.id) + ":minus", stepper.title + " −", d::Icon::minus, d::IconButton::Style::dark, false, false, 26.0, 11.0);
        auto& plus = b.make<d::IconButton>(std::string(stepper.id) + ":plus", stepper.title + " +", d::Icon::plus, d::IconButton::Style::dark, false, false, 26.0, 11.0);
        auto& group = b.layer(std::string("reader.stepper.") + stepper.id);
        group.setBounds({26 + 36 + 26, rowHeight});
        minus.layer().setOrigin({0, 0}); plus.layer().setOrigin({26 + 36, 0});
        group.add(minus.layer()); group.add(value); group.add(plus.layer());
        const auto key = stepper.key; const double minimum = stepper.minimum, maximum = stepper.maximum;
        updates->push_back([this, &value, &minus, &plus, key, minimum, maximum] {
            const double current = state_->library().preferences.*key;
            const auto shown = std::to_string(std::int64_t(std::round(current)));
            if (d::textOf(value).text() != shown) d::editText(value, [&](d::TextBlock& block) { block.setText(shown); });
            minus.setEnabled(current > minimum);
            plus.setEnabled(current < maximum);
        });
        for (auto [button, step] : {std::pair{&minus, -1.0}, std::pair{&plus, 1.0}}) {
            // The buttons own the update list (it dies with the pop-over).
            button->onActivate = [this, key, minimum, maximum, step, updates] {
                updatePreferences([&](ReaderPreferences& p) { p.*key = std::min(maximum, std::max(minimum, p.*key + step)); });
                for (const auto& update : *updates) update();
            };
        }
        controls.push_back(&minus); controls.push_back(&plus);
        rows.emplace_back(stepper.title, &group);
    }
    auto& content = popover->content();
    direction.layer().setOrigin({0, 0});
    content.add(direction.layer());
    double y = 40;
    for (std::size_t n = 0; n < rows.size(); ++n) {
        const auto& [label, layer] = rows[n];
        content.add(d::makeText(b, "reader.settings.label." + std::to_string(n), label, d::TextStyle{12.5, d::FontWeight::regular, env().palette.tone(0xDDDDDD)},
            {0, y + (rowHeight - 12.5) / 2, 120, 12.5}));
        const double right = inner - layer->bounds().width;
        layer->setOrigin({right, y + (rowHeight - layer->bounds().height) / 2});
        content.add(*layer);
        y += rowHeight + 8;
    }
    for (const auto& update : *updates) update();
    openPopover(std::move(popover), &anchor, std::nullopt, std::move(controls));
}
void ReaderCanvas::openFontMenu(const d::Control& anchor) {
    const auto current = state_->library().preferences.fontName;
    auto families = services_.fontFamilies ? services_.fontFamilies() : std::vector<std::string>{};
    std::sort(families.begin(), families.end());
    std::vector<d::Menu::Item> items;
    for (const auto& family : families) { auto entry = item(family, family); entry.selected = family == current; items.push_back(std::move(entry)); }
    openMenu("font", std::move(items), anchor, 200.0, [this](const d::Menu::Item& item) {
        const auto name = item.id;
        updatePreferences([&](ReaderPreferences& p) { p.fontName = name; });
        if (settingsButton_) openSettings(*settingsButton_);
    });
}
void ReaderCanvas::updatePreferences(const std::function<void(ReaderPreferences&)>& change) {
    auto next = state_->library().preferences;
    change(next);
    if (!next.valid() || next == state_->library().preferences) return;
    state_->setPreferences(std::move(next));
}

// --- Zoom and wheel (illustrated pages, :888-973) ------------------------------------
d::Rect ReaderCanvas::viewport() const noexcept { return {pageViewport.x + pageFrame.x, pageViewport.y + pageFrame.y, pageViewport.width, pageViewport.height}; }
core::Point ReaderCanvas::renderPoint(Point point) const {
    const auto v = viewport();
    return {(point.x - v.x) / textScale - renderSize.x / 2, (point.y - v.y) / textScale - renderSize.y / 2};
}
void ReaderCanvas::zoom(double amount, Point point) {
    const auto* current = state_->current();
    if (!current || !current->illustration || !std::isfinite(amount)) return;
    // A zoomed picture fills the viewport on its own, the column at its top.
    if (imageView_.zoom <= 1) { scrollOffset_ = 0; settleTurn(); }
    const double old = imageView_.zoom, next = std::min(12., std::max(1., old * amount)), ratio = next / old;
    const auto p = renderPoint(point);
    auto view = imageView_;
    view.zoom = next;
    view.pan = {p.x - (p.x - view.pan.x) * ratio, p.y - (p.y - view.pan.y) * ratio};
    setImageView(view);
}
void ReaderCanvas::setImageView(ReaderImageView value) {
    value = value.clamped(renderSize);
    if (value == imageView_) return;
    imageView_ = value;
    state_->cancelImageDetail();
    layoutPages();
    cancel(detailTimer_);
    if (value.zoom <= 1) return;
    // One debounced, viewport-sized detail raster after the gesture.
    after(.12, detailTimer_, [this, value] { if (isActive() && imageView_ == value) state_->requestImageDetail(value); });
}
bool ReaderCanvas::handleMagnify(Point point, double magnification) {
    const auto* current = state_->current();
    if (tab_ != Tab::reading || !contains(viewport(), point) || !current || !current->illustration || !std::isfinite(magnification)) return false;
    zoom(std::max(.05, 1 + magnification), point);
    return true;
}
// handleScroll (:929-958). Windows adaptation: the Mac's ⌥ / ⌘ + wheel zoom
// is the Ctrl + wheel pinch (app::pinchMagnification, handleMagnify); a
// wheel has no gesture phases, so the phase-less rules apply (re-arm after
// .24 s); fractional detents are the precise (touchpad) threshold.
bool ReaderCanvas::handleScroll(Point point, Point delta) {
    const auto* current = state_->current();
    if (tab_ != Tab::reading || !contains(pageFrame, point) || !current) return false;
    if (current->illustration && imageView_.zoom > 1) {
        auto view = imageView_;
        view.pan.x -= delta.x / textScale; view.pan.y -= delta.y / textScale;
        setImageView(view);
        return true;
    }
    // Top to bottom is one continuous column: every delta moves it.
    if (state_->library().preferences.vertical()) {
        if (delta.y != 0 && std::abs(delta.y) >= std::abs(delta.x)) scrollColumn(delta.y, false);
        return true;
    }
    const double amount = delta.x, cross = delta.y;
    if (!(std::abs(amount) > std::abs(cross) * 1.1)) return true;
    const double time = now();
    if (time - wheel_.last >= .24) wheel_.consumed = false;
    if (wheel_.consumed) return true;
    wheel_.remainder += amount;
    if (std::abs(wheel_.remainder) >= (wheelPrecise_ ? 60 : 16)) {
        wheel_.consumed = true; wheel_.last = time;
        turn(wheel_.remainder > 0 ? 1 : -1);
        wheel_.remainder = 0;
    }
    return true;
}
bool ReaderCanvas::handlePointerDown(Point point, const d::PointerInput&) {
    if (tab_ != Tab::reading || !contains(viewport(), point)) return false;
    const auto* current = state_->current();
    if (current && current->illustration && imageView_.zoom > 1) {
        panStart_ = std::pair{point, imageView_.pan};
        if (context().onLockParallax) context().onLockParallax();
    }
    return true;
}
void ReaderCanvas::handlePointerDragged(Point point) {
    if (!panStart_) return;
    auto view = imageView_;
    view.pan = {panStart_->second.x + (point.x - panStart_->first.x) / textScale, panStart_->second.y + (point.y - panStart_->first.y) / textScale};
    setImageView(view);
}
void ReaderCanvas::handlePointerUp(Point) { panStart_.reset(); }

// --- Keys (:975-997) ------------------------------------------------------------
// Esc first resets a zoomed page.
bool ReaderCanvas::handleKey(const d::KeyInput& key) {
    if (tab_ != Tab::reading || key.virtualKey != d::key::escape || imageView_.zoom <= 1 || key.commandModifiers() || key.shift) return false;
    setImageView(ReaderImageView{});
    return true;
}
// Reading tab: ← / → turn pages (↑ / ↓ too when reading top to bottom; Space goes forward).
bool ReaderCanvas::handleExtraKey(const d::KeyInput& key) {
    if (tab_ != Tab::reading || key.commandModifiers() || key.shift) return false;
    const bool vertical = state_->library().preferences.vertical();
    switch (key.virtualKey) {
    case d::key::left: turn(-1); return true;
    case d::key::right: case d::key::space: turn(1); return true;
    case d::key::up: if (!vertical) return false; turn(-1); return true;
    case d::key::down: if (!vertical) return false; turn(1); return true;
    default: return false;
    }
}

// --- Accessibility (:999-1013) ------------------------------------------------------
// The page's text summary; activating it turns forward.
void ReaderCanvas::accessibilityItems(std::vector<d::AccessibilityItem>& out) const {
    Canvas::accessibilityItems(out);
    const auto* current = state_->current();
    if (tab_ != Tab::reading || !isActive() || transitioning() || !current) return;
    d::AccessibilityItem item;
    item.id = "page";
    if (current->summary.empty()) { if (const auto number = pageNumber()) item.label = "P. " + std::to_string(number->current) + " / " + std::to_string(number->total); }
    else item.label = current->summary;
    item.role = d::AccessibilityRole::button;
    item.rect = viewport();
    item.enabled = current->next.has_value();
    out.push_back(std::move(item));
}
bool ReaderCanvas::accessibilityAction(std::string_view id) {
    if (id != "page") return false;
    turn(1);
    return true;
}
} // namespace endfield::modules
