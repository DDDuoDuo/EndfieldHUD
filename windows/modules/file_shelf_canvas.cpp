#include "modules/file_shelf_canvas.hpp"
#include "modules/hud_design_badges.hpp"
#include <algorithm>
#include <array>
#include <cmath>

namespace endfield::modules {
namespace d = design;
namespace {
using Set = std::set<std::string, std::less<>>;
constexpr double cardWidth = 186, cardInner = 186 - 24;
bool separator(char c) noexcept { return c == '/' || c == '\\'; }
std::string lowered(std::string_view s) { std::string out(s); for (auto& c : out) if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a'); return out; }
bool contains(const std::vector<std::string_view>& list, std::string_view value) { return std::find(list.begin(), list.end(), value) != list.end(); }
// UTType(filenameExtension:) conformance of FileShelfCanvas.tileKind (:688-696)
// as macOS 15 declares it (computed with UniformTypeIdentifiers on the Mac
// that generated tests/fixtures/file-shelf-source.json): .pdf, .image,
// .audiovisualContent, .archive or .diskImage. Dynamic types (no declaring
// app) conform to none and stay .txt.
const std::vector<std::string_view>& imageExtensions() {
    static const std::vector<std::string_view> list{"3fr", "ai", "arw", "avif", "bmp", "cr2", "cr3", "crw", "dib", "dng", "erf", "exr", "gif", "hdr",
        "heic", "heif", "hif", "icns", "ico", "j2k", "jp2", "jpe", "jpeg", "jpg", "jpx", "jxl", "mrw", "nef", "nrw", "orf", "pbm", "pct", "pef", "pgm",
        "pic", "pict", "png", "ppm", "psd", "raf", "raw", "rw2", "sr2", "srf", "srw", "svg", "svgz", "tga", "tif", "tiff", "webp", "xbm"};
    return list;
}
const std::vector<std::string_view>& audiovisualExtensions() {
    static const std::vector<std::string_view> list{"3g2", "3gp", "aac", "ac3", "adts", "aif", "aifc", "aiff", "amr", "asf", "au", "avi", "caf", "dv",
        "eac3", "f4v", "flac", "flv", "kar", "m2ts", "m2v", "m4a", "m4b", "m4p", "m4r", "m4v", "mid", "midi", "mov", "mp3", "mp4", "mpe", "mpeg", "mpg",
        "mts", "mxf", "oga", "ogg", "ogv", "qt", "rm", "rmvb", "snd", "ts", "wav", "wave", "webm", "wma", "wmv"};
    return list;
}
const std::vector<std::string_view>& archiveExtensions() {
    static const std::vector<std::string_view> list{"7z", "arj", "bz2", "cab", "cdr", "cpio", "deb", "dmg", "gz", "gzip", "img", "iso", "jar", "lha",
        "lzh", "lzma", "mpkg", "msi", "pkg", "rar", "rpm", "sit", "sitx", "sparsebundle", "sparseimage", "tar", "tbz", "tbz2", "tgz", "toast", "txz",
        "udif", "war", "xar", "xip", "xz", "z", "zip", "zipx"};
    return list;
}
// Windows-only text: English, Simplified Chinese, Traditional Chinese,
// Japanese, Korean (Windows Explorer's names; the relink wording of the Mac
// import card).
struct TextRow { std::string_view en, zhHans, zhHant, ja, ko; };
constexpr std::array<TextRow, 15> windowsText{{
    {"Reveal in File Explorer", "在文件资源管理器中显示", "在檔案總管中顯示", "エクスプローラーで表示", "파일 탐색기에서 보기"},
    {"Revealed in File Explorer", "已在文件资源管理器中显示", "已在檔案總管中顯示", "エクスプローラーで表示しました", "파일 탐색기에서 표시됨"},
    {"Unable to show the file in File Explorer", "无法在文件资源管理器中显示文件", "無法在檔案總管中顯示檔案", "エクスプローラーでファイルを表示できません", "파일 탐색기에서 파일을 표시할 수 없습니다"},
    {"Relink", "重新关联", "重新連結", "再リンク", "다시 연결"},
    {"Needs relinking", "需要重新关联", "需要重新連結", "再リンクが必要", "다시 연결 필요"},
    {"Relink “{0}”", "重新关联“{0}”", "重新連結「{0}」", "「{0}」を再リンク", "‘{0}’ 다시 연결"},
    {"Relink", "重新关联", "重新連結", "再リンク", "다시 연결"},
    {"Choose exactly one item to relink.", "请只选择一个项目进行重新关联。", "請只選擇一個項目進行重新連結。", "再リンクする項目を1つだけ選択してください。", "다시 연결할 항목을 하나만 선택하세요."},
    {"The file chooser could not open.", "无法打开文件选择窗口。", "無法開啟檔案選擇視窗。", "ファイル選択ウインドウを開けませんでした。", "파일 선택 창을 열 수 없습니다."},
    {"This item isn't a file on disk. Save it to a folder first, then add it.", "此项目不是磁盘上的文件。请先将其保存到文件夹，再添加。",
     "此項目不是磁碟上的檔案。請先將其儲存到資料夾，再新增。", "この項目はディスク上のファイルではありません。フォルダに保存してから追加してください。",
     "이 항목은 디스크에 있는 파일이 아닙니다. 먼저 폴더에 저장한 다음 추가하세요."},
    {"Choose a file or folder that is available on this PC.", "请选择此电脑上可访问的文件或文件夹。", "請選擇此電腦上可存取的檔案或資料夾。",
     "このPCでアクセスできるファイルまたはフォルダを選択してください。", "이 PC에서 사용할 수 있는 파일이나 폴더를 선택하세요."},
    {"the original may have been deleted, or it is not accessible.", "原始项目可能已被删除或无法访问。", "原始項目可能已被刪除或無法存取。",
     "元の項目が削除されたか、アクセスできない可能性があります。", "원본 항목이 삭제되었거나 액세스할 수 없습니다."},
    {"The shelf has too many references to save.", "暂存架引用过多，无法保存。", "暫存架引用過多，無法儲存。", "シェルフの参照が多すぎて保存できません。", "보관함 참조가 너무 많아 저장할 수 없습니다."},
    {"The saved shelf changed outside this window. Restart EndfieldHUD before making changes.", "暂存架数据已在外部更改，请重新启动 EndfieldHUD 后再编辑。",
     "暫存架資料已在外部更改，請重新啟動 EndfieldHUD 後再編輯。", "保存済みのシェルフが外部で変更されました。EndfieldHUDを再起動してから編集してください。",
     "다른 창이나 앱에서 보관함이 변경되었습니다. EndfieldHUD를 재시작한 후 변경하세요."},
    {"This file shelf was saved by a newer version of EndfieldHUD.", "此暂存架由较新版本的 EndfieldHUD 保存。", "此暫存架由較新版本的 EndfieldHUD 儲存。",
     "このファイルシェルフは新しいバージョンのEndfieldHUDで保存されています。", "이 파일 보관함은 더 최신 버전의 EndfieldHUD에서 저장되었습니다."},
}};
static_assert(windowsText.size() == std::size_t(ShelfText::newerVersion) + 1);
}

std::string shelfText(ShelfText value, core::Language language) {
    const auto& row = windowsText[std::size_t(value)];
    switch (language) {
    case core::Language::simplifiedChinese: return std::string(row.zhHans);
    case core::Language::traditionalChinese: return std::string(row.zhHant);
    case core::Language::japanese: return std::string(row.ja);
    case core::Language::korean: return std::string(row.ko);
    default: return std::string(row.en);
    }
}
std::optional<std::string> FileShelfItem::sizeLabel() const {
    if (isDirectory || !byteCount) return std::nullopt;
    return d::format::fileSize(*byteCount);
}

// --- ShelfTile (:731-773): the shared archive tile plus the press that
// becomes either a click (selection) or, after 5 px, a drag-out ------------------
class FileShelfCanvas::Tile final : public d::Control {
public:
    Tile(d::Builder& b, const FileShelfItem& item, int index, double width, bool redacted, FileShelfCanvas& canvas)
        : Control(b.env, "file:" + std::to_string(index), spokenLabel(item, redacted, b.env), b.layer("file:" + std::to_string(index))),
          itemID(item.id), number(index), canvas_(&canvas) {
        const auto language = b.env.language;
        tile = &b.make<d::ArchiveTile>("tile:" + std::to_string(index), index, item.name, tileKind(item), extensionLabel(item),
            sizeTag(item, language), std::nullopt, width, 100.0, redacted);
        auto& l = layer();
        l.setAnimatable();                 // the add entrance and the Clear `gone`
        l.setPartRoot(true);
        l.setBounds(tile->layer().bounds());
        tile->layer().setFrame({0, 0, l.bounds().width, l.bounds().height});
        l.add(tile->layer());
    }
    d::ArchiveTile* tile{};
    std::string itemID;
    int number{};
    static std::string spokenLabel(const FileShelfItem& item, bool redacted, const d::Environment& environment) {
        const auto language = environment.language;
        std::string out = redacted ? core::localized("Redacted", "已隐藏", language) : item.name;
        out += ", " + kindText(item, language);
        if (const auto size = sizeTag(item, language)) out += ", " + *size;
        return out;
    }
    bool pointerDown(d::Point point) override {
        const auto& event = canvas_->press_;
        press_ = Press{point, event.shift || event.control, std::max(1u, event.clickCount)};
        dragging_ = false;
        canvas_->pressedTile_ = itemID;
        canvas_->notifyIcons();
        return true;
    }
    void pointerDragged(d::Point point) override {
        if (!press_ || dragging_ || std::hypot(point.x - press_->point.x, point.y - press_->point.y) < 5) return;
        dragging_ = true;
        canvas_->beginDrag(itemID);
    }
    void pointerUp(d::Point point) override {
        const auto press = std::exchange(press_, std::nullopt);
        const bool dragged = std::exchange(dragging_, false);
        canvas_->pressedTile_.reset();
        if (!press || dragged || !contains(point)) return;
        canvas_->click(itemID, press->toggle, press->clicks);
    }
    void clearInteractionState() override { Control::clearInteractionState(); press_.reset(); dragging_ = false; }
    void activate() override { canvas_->click(itemID); }
    std::optional<d::AccessibilityRole> accessibilityRole() const override { return d::AccessibilityRole::tab; }
    void accessibilityItems(const d::Layer& root, std::vector<d::AccessibilityItem>& out) const override {
        if (label.empty()) return;
        auto item = accessibilityItem(root, d::AccessibilityRole::tab);
        item.on = tile->selected();
        out.push_back(std::move(item));
    }
private:
    struct Press { d::Point point; bool toggle{}; unsigned clicks{1}; };
    FileShelfCanvas* canvas_;
    std::optional<Press> press_;
    bool dragging_{};
};

FileShelfCanvas::FileShelfCanvas(d::Context& context, FileShelfServices services, std::optional<std::string> error)
    : Canvas(core::Module::fileShelf, context), services_(std::move(services)), pendingError_(std::move(error)) {
    if (services_.available && services_.items) items_ = services_.items();
    clearTimer_ = timerGroup();
}

double FileShelfCanvas::scrollOffset() const noexcept { return grid_ ? grid_->offset() : 0; }
const FileShelfItem* FileShelfCanvas::item(std::string_view id) const {
    const auto it = std::find_if(items_.begin(), items_.end(), [&](const auto& i) { return i.id == id; });
    return it == items_.end() ? nullptr : &*it;
}
FileShelfCanvas::Structure FileShelfCanvas::structure() const { return {items_, cardID()}; }
std::optional<std::string> FileShelfCanvas::cardID() const { return selected_.size() == 1 ? std::optional(*selected_.begin()) : std::nullopt; }

// --- Lifecycle (:101-142) -----------------------------------------------------------
// Bookmarks resolve before the module is revealed, so activation never
// replaces tiles that just finished their entrance (no polling).
void FileShelfCanvas::show(double now) {
    env().now = std::max(env().now, now);
    refreshStore();
    if (renderedLanguage() && renderedStructure_ && structure() != *renderedStructure_) rerender();
    Canvas::show(now);
}
void FileShelfCanvas::didActivate() {
    if (!renderedStructure_ || structure() != *renderedStructure_) rerender();
    if (auto error = std::exchange(pendingError_, std::nullopt)) showError(std::move(*error));
}
void FileShelfCanvas::didDeactivate() {
    if (dropTarget_) setDropTarget(false);
    if (services_.deactivate) services_.deactivate();
}
// No deadline runs while the HUD is hidden: a Clear still animating when the
// shelf leaves finishes now (the Mac's asyncAfter fires 0.38 s after it began).
void FileShelfCanvas::willLeave() { if (clearing_) { cancel(clearTimer_); finishClear(); } }
void FileShelfCanvas::playEntrance(d::Point offset, double now) {
    Canvas::playEntrance(offset, now);
    if (card_) d::motion::enter(*card_, {24, 0}, clock(), .3);
}
void FileShelfCanvas::recordingModeChanged() {
    Canvas::recordingModeChanged();
    if (env().recordingMode && isActive()) { d::Layer* layers[]{&page().layer()}; d::redaction::playEntrance(layers, clock()); }
}
void FileShelfCanvas::refreshStore() {
    if (!services_.available) return;
    try { if (services_.refresh) services_.refresh(); }
    catch (const std::exception& e) { pendingError_ = services_.describe ? services_.describe(e, FileShelfServices::Failure::refreshing) : std::string(e.what()); }
    properties_.clear();
    reloadItems();
}
void FileShelfCanvas::reloadItems() {
    items_ = services_.available && services_.items ? services_.items() : std::vector<FileShelfItem>{};
    Set ids;
    for (const auto& i : items_) ids.insert(i.id);
    std::erase_if(selected_, [&](const std::string& id) { return !ids.contains(id); });
    if (focus_ && !ids.contains(*focus_)) focus_.reset();
    if (anchor_ && !ids.contains(*anchor_)) anchor_.reset();
}
void FileShelfCanvas::storeChanged() {
    reloadItems();
    if (renderedLanguage() && (!renderedStructure_ || structure() != *renderedStructure_)) rerender();
    inputChanged();
}

// --- Page (:149-193) ----------------------------------------------------------------
void FileShelfCanvas::buildPage(d::Page& page) {
    renderedStructure_ = structure();
    tiles_.clear(); parked_.clear(); cardBinder_ = nullptr; card_ = nullptr; grid_ = nullptr; cardRows_.clear();
    const auto previousCard = std::exchange(renderedCard_, std::nullopt);
    auto& add = page.make<d::Button>("shelf.add", text("Add", "添加"), d::Icon::plus, d::Button::Kind::primary, d::Button::Height::compact);
    add.place({0, 0});
    add.onActivate = [this] { chooseFiles(); };
    auto& clear = page.make<d::Button>("shelf.clear", text("Clear", "清空"), d::Icon::trash, d::Button::Kind::secondary, d::Button::Height::compact);
    clear.place({0, 0});
    clear.setEnabled(!items_.empty());
    clear.onActivate = [this] { confirmClear(); };
    const std::string count = d::format::pad2(std::int64_t(items_.size()));
    const std::array<std::string_view, 1> counter{count};
    header().configure({"FILE SHELF"}, "MOD-02", title("File Shelf", "文件暂存架"), text("{0} FILES", "{0} 个文件", counter), false, {&add, &clear});
    const double top = d::metric::headerHeight;
    d::Rect body;
    if (items_.empty()) {
        layout_.reset();
        auto& empty = d::emptyState::bilingual(page.builder(), "shelf.empty", "SHELF EMPTY", "拖入文件以暂存");
        empty.setOrigin({0, top});
        page.layer().add(empty);
        page.stagger(empty);
        body = empty.frame();
    } else {
        const auto id = cardID();
        const FileShelfItem* shown = id ? item(*id) : nullptr;
        const double width = d::metric::boxWidth - (shown ? cardWidth + 12 : 0);
        layout_.emplace(width, shown ? 3 : 4, items_.size());
        auto& area = page.scrollArea("shelf.grid", {0, top, width, layout_->height});
        grid_ = &area;
        area.setContentHeight(layout_->contentHeight);
        page.restoreOffset(area);
        syncTiles(page, area.offset(), false);
        body = area.layer().frame();
        if (shown) buildCard(*shown, page, top, previousCard != shown->id);
    }
    auto& ring = page.makeLayer("shelf.drop");
    ring.setFrame(body);
    ring.setAnimatable();
    d::setBorder(ring, d::withAlpha(env().palette.yellow(), .05), env().palette.accentInk(), 1);
    ring.setValue(d::Channel::opacity, dropTarget_ ? 1 : 0);
    page.layer().add(ring);
    dropRing_ = &ring;
    renderedSelection_ = selected_;
    notifyIcons();
}
// rerender (:510-513): a reveal asked for before the first render scrolls now.
void FileShelfCanvas::didInstall(d::Page&) {
    if (!pendingReveal_.empty()) { pendingReveal_.clear(); scrollGrid(1e12); }
}

FileShelfCanvas::GridLayout::GridLayout(double width, int columnCount, std::size_t count) : columns(columnCount) {
    const auto rows = (count + std::size_t(columns) - 1) / std::size_t(columns);
    contentHeight = padding * 2 + double(rows) * tileHeight + double(rows > 0 ? rows - 1 : 0) * gap;
    height = std::min(420.0, contentHeight);
    // The 4 px scroll bar takes room only when the grid scrolls.
    const double inner = width - padding * 2 - (contentHeight > height ? 4 : 0);
    tileWidth = std::floor((inner - gap * double(columns - 1)) / double(columns));
}
d::Rect FileShelfCanvas::GridLayout::frame(std::size_t index) const noexcept {
    return {padding + double(index % std::size_t(columns)) * (tileWidth + gap), padding + double(index / std::size_t(columns)) * pitch(), tileWidth, tileHeight};
}
// Tiles worth building: the visible rows and one either side.
std::pair<std::size_t, std::size_t> FileShelfCanvas::GridLayout::indices(double offset, std::size_t count) const noexcept {
    const long long first = std::max(0LL, (long long)std::floor((offset - padding) / pitch()) - 1);
    const long long last = (long long)std::ceil((offset + height - padding) / pitch()) + 1;
    const auto cols = (long long)columns;
    const auto lower = std::min((long long)count, first * cols);
    return {std::size_t(lower), std::size_t(std::max(lower, std::min((long long)count, std::max(first, last) * cols)))};
}

// Builds the tiles in view and drops the ones that left it (:226-248); the
// hover pass and accessibility only ever see a few rows. A tile that left is
// kept (parked) with its page and comes back as it was: layers live as long as
// their page, so rebuilding it on every scroll would only grow the page.
void FileShelfCanvas::syncTiles(d::Page& page, double offset, bool routing) {
    if (!grid_ || !layout_) return;
    const auto [begin, end] = layout_->indices(offset, items_.size());
    Set wanted;
    for (auto n = begin; n < end; ++n) wanted.insert(items_[n].id);
    bool changed{};
    for (auto it = tiles_.begin(); it != tiles_.end();) {
        if (wanted.contains(it->first)) { ++it; continue; }
        Tile& tile = *it->second;
        tile.clearInteractionState();
        tile.layer().removeFromParent();
        page.remove(tile);
        parked_.emplace(it->first, &tile);
        it = tiles_.erase(it); changed = true;
    }
    const bool recording = env().recordingMode;
    for (auto n = begin; n < end; ++n) {
        const auto& value = items_[n];
        if (tiles_.contains(value.id)) continue;
        Tile* tile{};
        if (const auto kept = parked_.find(value.id); kept != parked_.end()) { tile = kept->second; parked_.erase(kept); }
        else tile = &page.make<Tile>(value, int(n) + 1, layout_->tileWidth, recording, *this);
        const bool selected = selected_.contains(value.id);
        tile->tile->setSelected(selected && renderedSelection_.contains(value.id), false);
        tile->layer().setFrame(layout_->frame(n));
        grid_->content().add(tile->layer());
        if (selected) tile->tile->setSelected(true);
        page.add(*tile);
        if (!routing) page.stagger(tile->layer());
        tiles_.emplace(value.id, tile); changed = true;
    }
    if (routing && changed) { refreshRouter(); inputChanged(); notifyIcons(); }
}
std::optional<d::Rect> FileShelfCanvas::tileRect(std::string_view id) const {
    const auto it = tiles_.find(id);
    if (it == tiles_.end() || !it->second->layer().parent()) return std::nullopt;
    return d::convert(d::localBounds(it->second->layer()), it->second->layer(), root());
}
bool FileShelfCanvas::handleScroll(d::Point point, d::Point delta) {
    if (!grid_ || !d::isVisible(grid_->layer(), root())) return false;
    const auto local = d::convert(point, root(), grid_->layer());
    const auto size = grid_->layer().bounds();
    if (!local || local->x < 0 || local->y < 0 || local->x >= size.width || local->y >= size.height) return false;
    if (grid_->scroll(delta.y)) syncTiles(page(), grid_->offset(), true);
    return true;
}
void FileShelfCanvas::scrollGrid(double offset) {
    if (!grid_) return;
    grid_->setOffset(offset);
    syncTiles(page(), grid_->offset(), true);
}
void FileShelfCanvas::revealTile(std::string_view id) {
    if (!grid_ || !layout_) return;
    const auto it = std::find_if(items_.begin(), items_.end(), [&](const auto& i) { return i.id == id; });
    if (it == items_.end()) return;
    auto frame = layout_->frame(std::size_t(it - items_.begin()));
    frame.y -= GridLayout::padding; frame.height += GridLayout::padding * 2;
    grid_->reveal(frame);
    syncTiles(page(), grid_->offset(), true);
}

// --- Properties card (spec 2.16, :277-386) ---------------------------------------------
void FileShelfCanvas::buildCard(const FileShelfItem& value, d::Page& page, double top, bool replaying) {
    const double x = d::metric::boxWidth - cardWidth;
    const auto language = env().language;
    const bool redacted = env().recordingMode;
    const bool unavailable = value.availabilityError.has_value();
    auto& b = page.builder();
    auto& card = page.makeLayer("shelf.props");
    auto& plate = page.makeLayer("props.plate");
    auto& preview = page.makeLayer("props.preview");
    auto& doc = d::ArchiveTile::docGlyph(b, "props.doc", tileKind(value), extensionLabel(value));
    // h4: 700 13 px / 1.25, breaks anywhere.
    const d::TextStyle nameStyle{.size = 13, .weight = d::FontWeight::bold, .color = d::fixed::nearBlack, .lineHeight = 1.25};
    const std::vector<d::TextRun> nameRuns{{value.name, nameStyle}};
    const double nameLines = redacted ? 1 : double(std::max<std::size_t>(1, env().text.lineCount(nameRuns, cardInner, 3)));
    double y = 12 + 84 + 10 + nameLines * 16.25 + 8;
    // dl: auto + 1fr columns, gap 5 x 8; dt 700 9 px / 1.6 #666 +.08em, dd 400 11 px / 1.3.
    const d::TextStyle termStyle{.size = 9, .weight = d::FontWeight::bold, .color = d::rgb(0x666666), .lineHeight = 1.6, .tracking = .08};
    const d::TextStyle valueStyle{.size = 11, .weight = d::FontWeight::regular, .color = d::fixed::nearBlack, .lineHeight = 1.3};
    using Live = std::function<std::string(const ShelfFileProperties*)>;
    struct Row { std::string term, value; bool privateValue{}; Live live; };
    const auto dotted = [](const std::optional<ShelfLocalTime>& t) {
        return t ? d::format::dotted(t->year, t->month, t->day, std::pair{t->hour, t->minute}) : std::string("—");
    };
    std::vector<Row> rows;
    rows.push_back({text("KIND", "类型"), kindText(value, language), false, {}});
    if (value.isDirectory && !isPackage(value))
        rows.push_back({text("ITEMS", "项目"), "", false, [this](const ShelfFileProperties* p) {
            if (!p || !p->itemCount) return std::string("—");
            const std::string n = std::to_string(*p->itemCount);
            const std::array<std::string_view, 1> count{n};
            return text("{0} items", "{0} 项", count);
        }});
    else rows.push_back({text("SIZE", "大小"), value.sizeLabel().value_or("—"), false, {}});
    rows.push_back({text("WHERE", "位置"), location(value, services_.homeFolder), true, {}});
    rows.push_back({text("CREATED", "创建"), "", false, [dotted](const ShelfFileProperties* p) { return dotted(p ? p->created : std::nullopt); }});
    rows.push_back({text("MODIFIED", "修改"), "", false, [dotted](const ShelfFileProperties* p) { return dotted(p ? p->modified : std::nullopt); }});
    double termWidth = 0;
    for (const auto& row : rows) termWidth = std::max(termWidth, env().text.width(row.term, termStyle));
    termWidth = std::ceil(termWidth);
    const double valueX = 12 + termWidth + 8, valueWidth = cardInner - termWidth - 8;
    std::vector<std::pair<d::Layer*, Live>> live;
    preview.setFrame({12, 12, cardInner, 84});
    const std::array<d::Color, 2> well{d::rgb(0x8A8885), d::rgb(0xC9C9C7)};
    preview.setContent(d::plate(preview.id(), d::localBounds(preview), well, {}, 0));
    doc.setPosition({cardInner / 2, 42});
    preview.add(doc);
    card.add(preview);
    const d::Rect nameLine{12, 106, cardInner, nameLines * 16.25};
    const std::string spokenRedacted = core::localized("Redacted", "已隐藏", language);
    if (redacted) { card.add(d::redaction::bar(b, "props.name.redacted", nameLine, nameStyle, 150)); cardRows_.push_back({"props.name", spokenRedacted, nullptr, {}, nameLine}); }
    else { auto& name = d::makeText(b, "props.name", value.name, nameStyle, nameLine); editText(name, [](d::TextBlock& t) { t.maximumLines = 3; }); card.add(name); cardRows_.push_back({"props.name", value.name, nullptr, {}, nameLine}); }
    const auto* known = [&]() -> const ShelfFileProperties* { const auto it = properties_.find(value.id); return it == properties_.end() ? nullptr : &it->second; }();
    for (std::size_t n = 0; n < rows.size(); ++n) {
        const auto& row = rows[n];
        const std::string key = "props.row" + std::to_string(n);
        card.add(d::makeText(b, key + ".term", row.term, termStyle, {12, y, termWidth + 1, 14.4}));
        const std::string shown = row.live ? row.live(known) : row.value;
        const std::vector<d::TextRun> runs{{shown, valueStyle}};
        const double lines = row.privateValue && !redacted ? double(std::max<std::size_t>(1, env().text.lineCount(runs, valueWidth, 3))) : 1;
        const d::Rect line{valueX, y, valueWidth, lines * 14.3};
        if (row.privateValue && redacted) { card.add(d::redaction::bar(b, key + ".redacted", line, valueStyle, 96)); cardRows_.push_back({key, row.term, nullptr, spokenRedacted, line}); }
        else {
            auto& field = d::makeText(b, key + ".value", shown, valueStyle, line);
            editText(field, [&](d::TextBlock& t) { t.maximumLines = row.privateValue ? 3 : 1; });
            card.add(field);
            if (row.live) live.emplace_back(&field, row.live);
            cardRows_.push_back({key, row.term, &field, {}, line});
        }
        y += std::max(14.4, line.height) + 5;
    }
    y += 10 - 5;
    const std::string id = value.id;
    // Windows: an imported Mac item offers Relink where an available one offers Open.
    d::Button* primary{};
    if (value.awaitingRelink) {
        primary = &page.make<d::Button>("props.relink", shelfText(ShelfText::relink, language), std::nullopt, d::Button::Kind::primary, d::Button::Height::compact);
        primary->onActivate = [this, id] { if (services_.relink) services_.relink(id); };
    } else {
        primary = &page.make<d::Button>("props.open", text("Open", "打开"), std::nullopt, d::Button::Kind::primary, d::Button::Height::compact);
        primary->setEnabled(!unavailable);
        primary->onActivate = [this, id] { open(id); };
    }
    primary->place({12, y});
    auto& revealButton = page.make<d::IconButton>("props.reveal", shelfText(ShelfText::revealInExplorer, language), d::Icon::folder);
    auto& removeButton = page.make<d::IconButton>("props.remove", text("Remove from shelf", "从暂存架移除"), d::Icon::x);
    revealButton.setEnabled(!unavailable);
    revealButton.onActivate = [this, id] { reveal(std::string_view(id)); };
    removeButton.onActivate = [this, id] { remove({id}); };
    const double height = y + 30 + 12;
    revealButton.layer().setOrigin({primary->layer().frame().x + primary->layer().frame().width + 6, y});
    removeButton.layer().setOrigin({revealButton.layer().frame().x + revealButton.layer().frame().width + 6, y});
    card.setFrame({x, top, cardWidth, height});
    card.setAnimatable();                  // enters from the right
    card.setPartRoot(true);
    plate.setFrame(d::localBounds(card));
    const std::array<d::Color, 2> plateColors{d::fixed::plateHigh, d::rgb(0xCFD0CB)};
    plate.setContent(d::plate(plate.id(), d::localBounds(plate), plateColors, {}, 12));
    card.insert(plate, 0);
    card.add(primary->layer()); card.add(revealButton.layer()); card.add(removeButton.layer());
    page.layer().add(card);
    page.add(std::vector<d::Control*>{primary, &revealButton, &removeButton});
    card_ = &card;
    cardBinder_ = [this, id, live] {
        const auto it = properties_.find(id);
        const ShelfFileProperties* read = it == properties_.end() ? nullptr : &it->second;
        for (const auto& [layer, update] : live) editText(*layer, [&](d::TextBlock& t) { t.setText(update(read)); });
    };
    if (replaying && isActive()) d::motion::enter(card, {24, 0}, clock(), .3);
    renderedCard_ = value.id;
    loadProperties(value);
}
void FileShelfCanvas::loadProperties(const FileShelfItem& value) {
    if (value.availabilityError || propertiesRequest_ == value.id || !services_.readProperties) return;
    if (const auto it = properties_.find(value.id); it != properties_.end() && it->second.path == value.lastKnownPath) return;
    if (!services_.readProperties(value)) return;
    propertiesRequest_ = value.id;
}
void FileShelfCanvas::propertiesRead(const std::string& id, ShelfFileProperties result) {
    if (propertiesRequest_ == id) propertiesRequest_.reset();
    if (!item(id)) return;
    properties_[id] = std::move(result);
    if (cardID() == id && cardBinder_) cardBinder_();
}

// --- Selection (spec 3.7, :388-456) -----------------------------------------------------
void FileShelfCanvas::click(std::string_view id, bool toggle, unsigned clickCount) {
    if (clearing_ || !item(id)) return;
    const std::string key(id);
    if (toggle) { if (!selected_.erase(key)) selected_.insert(key); }
    else if (clickCount >= 2) selected_ = {key};
    else selected_ = selected_ == Set{key} ? Set{} : Set{key};
    focus_ = key; anchor_ = key;
    selectionChanged();
}
// Records the press's modifiers and click count (the Mac's NSApp.currentEvent)
// for the tile the router gives it to; never consumes it.
bool FileShelfCanvas::interceptPointerDown(d::Point, const d::PointerInput& input) { press_ = input; return false; }
// A press on empty space in the body (around or between the tiles, the empty
// grid rows) clears the selection; the properties card and the header keep
// theirs. Focus and the Shift anchor stay for the keyboard (:405-413).
bool FileShelfCanvas::handlePointerDown(d::Point point, const d::PointerInput&) {
    if (selected_.empty() || clearing_ || point.y < d::metric::headerHeight || point.x < 0 || point.y < 0 || point.x >= d::metric::boxWidth || point.y >= d::metric::boxHeight) return false;
    if (card_) { const auto f = card_->frame(); if (point.x >= f.x && point.y >= f.y && point.x < f.x + f.width && point.y < f.y + f.height) return false; }
    select({});
    return true;
}
void FileShelfCanvas::select(Set ids) {
    std::erase_if(ids, [&](const std::string& id) { return !item(id); });
    selected_ = std::move(ids);
    selectionChanged();
}
void FileShelfCanvas::selectionChanged() {
    if (structure() != renderedStructure_) rerender();
    else {
        for (auto& [id, tile] : tiles_) tile->tile->setSelected(selected_.contains(id));
        renderedSelection_ = selected_;
        notifyIcons();
    }
    inputChanged();
}
// Arrow keys move through the grid; Shift extends from the anchor (:430-443).
void FileShelfCanvas::moveSelection(int step, bool extending) {
    if (items_.empty() || clearing_) return;
    const auto indexOf = [&](const std::optional<std::string>& id) -> std::optional<long long> {
        if (!id) return std::nullopt;
        const auto it = std::find_if(items_.begin(), items_.end(), [&](const auto& i) { return i.id == *id; });
        return it == items_.end() ? std::nullopt : std::optional<long long>(it - items_.begin());
    };
    const auto current = indexOf(focus_);
    const long long count = (long long)items_.size();
    const long long index = std::min(count - 1, std::max(0LL, current ? *current + step : (step < 0 ? count - 1 : 0)));
    const std::string id = items_[std::size_t(index)].id;
    focus_ = id;
    if (const auto anchor = indexOf(anchor_); extending && anchor) {
        selected_.clear();
        for (long long n = std::min(*anchor, index); n <= std::max(*anchor, index); ++n) selected_.insert(items_[std::size_t(n)].id);
    } else { selected_ = {id}; anchor_ = id; }
    selectionChanged();
    revealTile(id);
}
// The drag carries the selection when it starts on a selected tile;
// unavailable references can be selected but never exported (:447-450).
std::vector<std::string> FileShelfCanvas::dragSelection(std::string_view primary) const {
    const bool whole = selected_.contains(primary);
    std::vector<std::string> out;
    for (const auto& i : items_) if ((whole ? selected_.contains(i.id) : i.id == primary) && !i.availabilityError) out.push_back(i.id);
    return out;
}
void FileShelfCanvas::beginDrag(const std::string& id) {
    if (clearing_) return;
    auto ids = dragSelection(id);
    if (!ids.empty() && services_.beginDrag) services_.beginDrag(std::move(ids), id);
}
// Space shows the properties card of the keyboard's tile, or closes it (:654-660).
void FileShelfCanvas::toggleProperties() {
    if (selected_.size() == 1) { select({}); return; }
    std::optional<std::string> focus = focus_ && (selected_.empty() || selected_.contains(*focus_)) ? focus_ : std::nullopt;
    if (!focus) for (const auto& i : items_) if (selected_.contains(i.id)) { focus = i.id; break; }
    if (!focus && !items_.empty()) focus = items_.front().id;
    if (!focus) return;
    focus_ = focus;
    select({*focus});
}

// --- Actions (:460-592) -------------------------------------------------------------
void FileShelfCanvas::chooseFiles() {
    if (!services_.available) { reportUnavailable(); return; }
    if (services_.chooseFiles) services_.chooseFiles();
}
bool FileShelfCanvas::importPaths(std::span<const std::string> paths) {
    if (!services_.available) { reportUnavailable(); return false; }
    if (paths.empty()) return false;
    Set previous;
    for (const auto& i : services_.items()) previous.insert(i.id);
    try {
        services_.add(paths);
        reloadItems();
        std::vector<std::string> names; Set added;
        for (const auto& i : items_) if (!previous.contains(i.id)) { names.push_back(i.name); added.insert(i.id); }
        if (added.empty()) { inputChanged(); return true; } // existing references are valid drops, too
        if (services_.itemsAdded) services_.itemsAdded(names);
        reveal(added, true);
        return true;
    } catch (const std::exception& e) {
        reloadItems();
        if (renderedLanguage() && structure() != renderedStructure_) rerender();
        showError(services_.describe ? services_.describe(e, FileShelfServices::Failure::importing) : std::string(e.what()));
        return false;
    }
}
// A drop added these while the HUD was open: show them when the shelf is
// next on screen (:495-498).
void FileShelfCanvas::revealItems(std::span<const std::string> ids, bool animated) {
    reloadItems();
    Set shown;
    for (const auto& id : ids) if (item(id)) shown.insert(id);
    reveal(shown, animated);
}
// Add (spec 3.7): scrolls the grid to the bottom; new tiles enter from below (:501-508).
void FileShelfCanvas::reveal(const Set& ids, bool animated) {
    if (ids.empty()) return;
    if (!renderedLanguage()) { pendingReveal_.insert(ids.begin(), ids.end()); return; }
    if (structure() != renderedStructure_) rerender();
    scrollGrid(1e12);
    inputChanged();
    if (!animated || !isActive()) return;
    for (const auto& id : ids) if (const auto it = tiles_.find(id); it != tiles_.end()) d::motion::enter(it->second->layer(), {0, 16}, clock());
}
void FileShelfCanvas::open(std::string_view id) {
    const auto* value = item(id);
    if (!value || value->availabilityError || !services_.open) return;
    if (services_.open(id)) opened(id);
}
void FileShelfCanvas::opened(std::string_view id) {
    const auto* value = item(id);
    if (!value) return;
    toast(d::Icon::ext, "Opened", "已打开", env().recordingMode ? std::nullopt : std::optional(value->name));
}
void FileShelfCanvas::reveal(std::string_view id) {
    const auto* value = item(id);
    if (!value || value->availabilityError || !services_.reveal) return;
    const std::string where = location(*value, services_.homeFolder);
    if (!services_.reveal(id)) return;
    const auto language = env().language;
    // toast(icon:, "Revealed in Finder", "已在访达中显示", detail:) with Windows' name;
    // the English caption stays empty in English as the library's pair toast does.
    d::ToastRequest request;
    request.icon = d::Icon::folder;
    request.title = shelfText(ShelfText::revealedInExplorer, language);
    request.caption = env().recordingMode ? (language == core::Language::english ? std::string() : shelfText(ShelfText::revealedInExplorer, core::Language::english)) : where;
    toast(std::move(request));
}
// Removes references only (spec: removes at once and clears the selection) (:526-542).
void FileShelfCanvas::remove(std::vector<std::string> ids) {
    if (!services_.available) { reportUnavailable(); return; }
    if (ids.empty() || clearing_) return;
    try {
        for (const auto& id : ids) {
            std::optional<std::string> name;
            for (const auto& i : services_.items()) if (i.id == id) { name = i.name; break; }
            services_.remove(id);
            if (name && services_.itemRemoved) services_.itemRemoved(*name);
        }
        selected_.clear();
        reloadItems(); rerender();
        toast(d::Icon::x, "Removed from shelf", "从暂存架上移除");
    } catch (const std::exception& e) {
        reloadItems(); rerender();
        showError(services_.describe ? services_.describe(e, FileShelfServices::Failure::changing) : std::string(e.what()));
    }
    inputChanged();
}
void FileShelfCanvas::confirmClear() {
    if (items_.empty() || clearing_) return;
    const std::string count = std::to_string(items_.size());
    const std::array<std::string_view, 1> argument{count};
    d::ConfirmRequest request;
    request.id = "shelf.clear";
    request.title = text("Remove {0} files from the shelf?", "从暂存架移除 {0} 个文件？", argument);
    request.message = text("Original files stay where they are.", "原文件保持不变。");
    request.confirmTitle = text("Remove all", "全部移除");
    request.danger = true;
    request.icon = d::Icon::trash;
    confirm(std::move(request), [this] { clearShelf(); });
}
// Every tile plays `gone` 30 ms apart; the shelf empties after 380 ms (:555-578).
void FileShelfCanvas::clearShelf() {
    if (!services_.available || items_.empty() || clearing_) return;
    clearing_ = true;
    std::vector<Tile*> shown;
    for (auto& [id, tile] : tiles_) shown.push_back(tile);
    std::sort(shown.begin(), shown.end(), [](const Tile* a, const Tile* b) { return a->number < b->number; });
    const auto c = clock();
    for (std::size_t index = 0; index < shown.size(); ++index) {
        const double delay = double(index) * .03;
        auto& layer = shown[index]->layer();
        d::motion::animate(layer, d::Channel::opacity, 0, .28, c, d::easing::standard, delay);
        d::motion::animate(layer, d::Channel::scaleX, .85, .28, c, d::easing::standard, delay);
        d::motion::animate(layer, d::Channel::scaleY, .85, .28, c, d::easing::standard, delay);
    }
    inputChanged();
    if (c.reduceMotion || shown.empty()) finishClear();
    else after(.38, clearTimer_, [this] { finishClear(); });
}
void FileShelfCanvas::finishClear() {
    if (!clearing_) return;
    clearing_ = false;
    const std::size_t count = services_.items ? services_.items().size() : 0;
    try { services_.clear(); }
    catch (const std::exception& e) { showError(services_.describe ? services_.describe(e, FileShelfServices::Failure::changing) : std::string(e.what())); }
    selected_.clear();
    reloadItems();
    if (renderedLanguage()) rerender();
    inputChanged();
    if (!items_.empty() || count == 0) return;
    if (services_.cleared) services_.cleared(count);
    if (isActive()) toast(d::Icon::check, "Shelf cleared", "暂存架已清空");
}
// Incoming file drag feedback over the grid (or the empty state) (:581-585).
void FileShelfCanvas::setDropTarget(bool value) {
    if (dropTarget_ == value) return;
    dropTarget_ = value;
    if (dropRing_) d::motion::animate(*dropRing_, d::Channel::opacity, value ? 1 : 0, .14, clock(), d::easing::ease);
}
void FileShelfCanvas::showError(std::string message) {
    if (!renderedLanguage()) { pendingError_ = std::move(message); return; }
    toast(d::Icon::warn, "Notice", "提示", std::move(message));
}
void FileShelfCanvas::deferError(std::string message) {
    if (isActive()) showError(std::move(message)); else pendingError_ = std::move(message);
}
void FileShelfCanvas::reportUnavailable() { showError(text("File shelf storage is unavailable.", "文件暂存架存储不可用。")); }

bool FileShelfCanvas::perform(std::string_view value) {
    if (value == "shelf:add") { chooseFiles(); return true; }
    if (value == "shelf:clear") { confirmClear(); return true; }
    if (!value.starts_with("shelf:")) return false;
    const auto rest = value.substr(6);
    const auto colon = rest.rfind(':');
    if (colon == std::string_view::npos) return false;
    const std::string id(rest.substr(0, colon));
    const auto verb = rest.substr(colon + 1);
    const auto* found = item(id);
    if (!found) return false;
    if (verb == "select") click(id);
    else if (verb == "open") open(id);
    else if (verb == "reveal") reveal(std::string_view(id));
    else if (verb == "remove") remove({id});
    else if (verb == "relink" && found->awaitingRelink && services_.relink) services_.relink(id);
    else return false;
    return true;
}

// --- Keyboard and context menu (:614-678) --------------------------------------------------
bool FileShelfCanvas::handleKey(const d::KeyInput& key) {
    const bool command = key.control || key.alt || key.win;
    if (!command) {
        const int columns = layout_ ? layout_->columns : 4;
        std::optional<int> step;
        switch (key.virtualKey) {
        case d::key::left: step = -1; break;
        case d::key::right: step = 1; break;
        case d::key::down: step = columns; break;
        case d::key::up: step = -columns; break;
        default: break;
        }
        if (step) { moveSelection(*step, key.shift); return true; }
    }
    if (!command && !key.shift) {
        const double pageStep = (grid_ ? grid_->layer().bounds().height : 0) - 40;
        switch (key.virtualKey) {
        case d::key::pageDown: scrollGrid(scrollOffset() + pageStep); return true;
        case d::key::pageUp: scrollGrid(scrollOffset() - pageStep); return true;
        case d::key::home: scrollGrid(0); return true;
        case d::key::end: scrollGrid(1e12); return true;
        case d::key::space: toggleProperties(); return true;
        case d::key::back: case d::key::deleteForward: {
            std::vector<std::string> ids;
            for (const auto& i : items_) if (selected_.contains(i.id)) ids.push_back(i.id);
            remove(std::move(ids));
            return true;
        }
        default: break;
        }
    }
    // Cmd-O / Cmd-R / Cmd-V: Ctrl alone on Windows.
    if (key.control && !key.alt && !key.win && !key.shift) {
        const char32_t c = key.character >= U'A' && key.character <= U'Z' ? key.character - U'A' + U'a' : key.character;
        const char32_t v = key.virtualKey >= 'A' && key.virtualKey <= 'Z' ? char32_t(key.virtualKey - 'A' + 'a') : c;
        if (v == U'o') { if (const auto id = cardID()) open(*id); return true; }
        if (v == U'r') { if (const auto id = cardID()) reveal(std::string_view(*id)); return true; }
        if (v == U'v') return services_.paste && services_.paste();
    }
    return false;
}
bool FileShelfCanvas::handleRightPointerDown(d::Point point) {
    const Tile* hit{};
    for (const auto& [id, tile] : tiles_) {
        const auto& layer = tile->layer();
        if (!d::isVisible(layer, root()) || !d::isUnclipped(layer, point, root())) continue;
        if (const auto local = d::convert(point, root(), layer); local && tile->contains(*local)) { hit = tile; break; }
    }
    if (!hit) return false;
    const auto* value = item(hit->itemID);
    if (!value) return false;
    const bool available = !value->availabilityError;
    const auto language = env().language;
    std::vector<d::Menu::Item> menu;
    if (value->awaitingRelink) menu.push_back({.id = "relink", .title = shelfText(ShelfText::relink, language)});
    else menu.push_back({.id = "open", .title = text("Open", "打开"), .enabled = available});
    menu.push_back({.id = "reveal", .title = shelfText(ShelfText::revealInExplorer, language), .enabled = available});
    menu.push_back({.id = "remove", .title = text("Remove from shelf", "从暂存架移除"), .danger = true, .separatorBefore = true});
    const std::string id = value->id;
    openMenu("shelf.menu", std::move(menu), d::Rect{point.x, point.y, 1, 1}, nullptr, 170.0, [this, id](const d::Menu::Item& pick) {
        if (pick.id == "open") open(id);
        else if (pick.id == "reveal") this->reveal(std::string_view(id));
        else if (pick.id == "relink") { if (services_.relink) services_.relink(id); }
        else this->remove({id});
    });
    return true;
}
void FileShelfCanvas::accessibilityItems(std::vector<d::AccessibilityItem>& out) const {
    Canvas::accessibilityItems(out);
    if (!isActive() || transitioning() || !card_ || !card_->parent()) return;
    for (const auto& row : cardRows_) {
        d::AccessibilityItem item;
        item.id = row.id; item.label = row.label; item.role = d::AccessibilityRole::text;
        item.value = row.value ? d::textOf(*row.value).text() : row.redacted;
        item.rect = d::convert(row.line, *card_, root());
        out.push_back(std::move(item));
    }
}
bool FileShelfCanvas::inputLocked() const { return Canvas::inputLocked() || (services_.inputLocked && services_.inputLocked()); }
bool FileShelfCanvas::presentingPanel() const { return services_.presentingPanel && services_.presentingPanel(); }

// The Shell icons a drag-out may need: the pressed tile, the selection, then
// the built tiles (at most the provider's eight), on content events only.
void FileShelfCanvas::notifyIcons() const {
    if (!services_.iconsWanted) return;
    std::vector<std::string> ids;
    const auto add = [&](const std::string& id) { if (std::find(ids.begin(), ids.end(), id) == ids.end()) ids.push_back(id); };
    if (pressedTile_ && item(*pressedTile_)) add(*pressedTile_);
    for (const auto& i : items_) if (selected_.contains(i.id) && !i.availabilityError) add(i.id);
    std::vector<std::pair<int, std::string>> built;
    for (const auto& [id, tile] : tiles_) built.emplace_back(tile->number, id);
    std::sort(built.begin(), built.end());
    for (const auto& [number, id] : built) if (const auto* i = item(id); i && !i->availabilityError) add(id);
    services_.iconsWanted(ids);
}
void FileShelfCanvas::inputChanged() const { if (context().onInputStateChange) context().onInputStateChange(); }

// --- Formatting (spec 2.15, 2.16, :680-721) -------------------------------------------
std::string FileShelfCanvas::extensionOf(std::string_view path) {
    while (!path.empty() && separator(path.back())) path.remove_suffix(1);
    std::size_t start = path.size();
    while (start > 0 && !separator(path[start - 1])) --start;
    const auto name = path.substr(start);
    const auto dot = name.rfind('.');
    if (dot == std::string_view::npos || dot == 0 || dot + 1 == name.size()) return {};
    return std::string(name.substr(dot + 1));
}
// Windows has no bundle packages (folders whose extension conforms to
// .package): every folder is a folder.
bool FileShelfCanvas::isPackage(const FileShelfItem&) noexcept { return false; }
FileShelfCanvas::Extension FileShelfCanvas::tileKind(const FileShelfItem& value) {
    const auto ext = lowered(extensionOf(value.lastKnownPath));
    if ((value.isDirectory && !isPackage(value)) || ext.empty()) return Extension::txt;
    if (ext == "pdf") return Extension::pdf;
    if (contains(imageExtensions(), ext)) return Extension::img;
    if (contains(audiovisualExtensions(), ext)) return Extension::vid;
    if (contains(archiveExtensions(), ext)) return Extension::zip;
    return Extension::txt;
}
std::string FileShelfCanvas::extensionLabel(const FileShelfItem& value) {
    if (value.isDirectory && !isPackage(value)) return "DIR";
    const auto ext = extensionOf(value.lastKnownPath);
    return ext.empty() ? "FILE" : d::asciiUppercase(ext);
}
std::string FileShelfCanvas::kindText(const FileShelfItem& value, core::Language language) {
    if (isPackage(value) && !value.typeDescription.empty()) return value.typeDescription;
    if (value.isDirectory) return core::localized("Folder", "文件夹", language);
    const auto ext = d::asciiUppercase(extensionOf(value.lastKnownPath));
    if (ext.empty()) return value.typeDescription.empty() ? core::localized("File", "文件", language) : value.typeDescription;
    const std::array<std::string_view, 1> argument{ext};
    return core::localized("{0} file", "{0} 文件", language, argument);
}
// WHERE: the containing folder, "~" for the user profile folder (:715-717).
std::string FileShelfCanvas::location(const FileShelfItem& value, std::string_view home) {
    std::string_view path(value.lastKnownPath);
    while (path.size() > 1 && separator(path.back())) path.remove_suffix(1);
    auto cut = path.size();
    while (cut > 0 && !separator(path[cut - 1])) --cut;
    std::string folder(path.substr(0, cut));
    // deletingLastPathComponent keeps a root ("C:\", "/") and drops other trailing separators.
    while (folder.size() > 1 && separator(folder.back()) && !(folder.size() == 3 && folder[1] == ':')) folder.pop_back();
    if (folder.empty()) return path.empty() || !separator(path.front()) ? std::string() : std::string(1, path.front());
    while (home.size() > 1 && separator(home.back())) home.remove_suffix(1);
    if (!home.empty() && folder.size() >= home.size() && lowered(folder.substr(0, home.size())) == lowered(home)
        && (folder.size() == home.size() || separator(folder[home.size()])))
        return "~" + folder.substr(home.size());
    return folder;
}
std::optional<std::string> FileShelfCanvas::sizeTag(const FileShelfItem& value, core::Language language) {
    if (value.availabilityError) return core::localized("Unavailable", "不可用", language);
    return value.sizeLabel();
}
} // namespace endfield::modules
