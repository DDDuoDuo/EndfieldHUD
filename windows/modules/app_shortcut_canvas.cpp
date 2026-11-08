// AppShortcutCanvas and AppShortcutTile (Sources/AppShortcutCanvas.swift, Mac 1.3.0),
// statement by statement (line numbers cite it).
#include "modules/app_shortcut_canvas.hpp"
#include "modules/hud_design_badges.hpp"
#include "modules/hud_design_format.hpp"
#include <algorithm>

namespace endfield::modules {
namespace d = design;
namespace {
// The 40 x 40 app icon tile (:442-480): the shortcut's artwork with an inset
// ring rgba(0,0,0,.25) and, on its lower right corner, a dark chip with the
// yellow chooser caret of the dropdowns (Display > Center logo), so it reads
// as a chooser; it opens Choose icon. The caret turns while that menu is open
// (MenuAnchor) and nudges on hover.
class AppShortcutTile final : public d::Control, public d::MenuAnchor {
public:
    static constexpr double badgeSide = 14;
    AppShortcutTile(d::Builder& b, std::string id, std::string label, const d::Menu::IconTile& artwork)
        : Control(b.env, id, std::move(label), b.layer(id)), picture_(&b.layer(id + ".icon")), brighten_(&b.layer(id + ".brighten")),
          caret_(&d::Dropdown::caretLayer(b, id + ".caret", {8, 5}, {badgeSide / 2 + .5, badgeSide / 2 + .5})) {
        const d::Rect bounds{0, 0, artwork.size.width, artwork.size.height};
        layer().setBounds(artwork.size); layer().setAnimatable();
        draw(artwork);
        d::Layer& ring = b.layer(id + ".ring");
        ring.setFrame(bounds); d::setBorder(ring, std::nullopt, d::black(.25), 1);
        brighten_->setFrame(bounds); d::setBackground(*brighten_, d::white(.07)); brighten_->setAnimatable();
        brighten_->setValue(d::Channel::opacity, 0);
        d::Layer& badge = b.layer(id + ".badge");
        badge.setFrame({bounds.width - badgeSide, bounds.height - badgeSide, badgeSide, badgeSide});
        const auto& palette = b.env.palette;
        badge.setContent(d::shape(badge.id(), d::localBounds(badge), d::chamfer({.5, .5, badgeSide - 1, badgeSide - 1}, 4, d::topLeft),
            palette.token(d::ThemeToken::control), palette.token(d::ThemeToken::ringField)));
        badge.add(*caret_);
        for (d::Layer* part : {picture_, brighten_, &ring, &badge}) layer().add(*part);
        setTarget(bounds);
    }
    bool isOpen() const noexcept override { return open_; }
    void setOpen(bool open) override { if (open != open_) { open_ = open; renderCaret(); } }
    // The tile's colour and picture, in place (an icon the owner published
    // after the row was built), the row and its entrance left as they are.
    void setArtwork(const d::Menu::IconTile& artwork) { if (artwork != shown_) draw(artwork); }
    const d::Menu::IconTile& artwork() const noexcept { return shown_; }
    void hoverChanged() override { d::applyHoverMotion(*this, 1, 1, .965, brighten_); renderCaret(); }
    void pressChanged() override { d::applyHoverMotion(*this, 1, 1, .965, brighten_); }
    std::optional<d::AccessibilityRole> accessibilityRole() const override { return d::AccessibilityRole::popUp; }
    void accessibilityItems(const d::Layer& root, std::vector<d::AccessibilityItem>& out) const override {
        if (label.empty()) return;
        auto item = accessibilityItem(root, d::AccessibilityRole::popUp); item.value = label; // .popUp(label)
        out.push_back(std::move(item));
    }
private:
    d::Layer *picture_, *brighten_, *caret_;
    d::Menu::IconTile shown_;
    bool open_{};
    void renderCaret() { d::Dropdown::renderCaret(*caret_, open_, hovered() && enabled(), clock()); }
    void draw(const d::Menu::IconTile& artwork) {
        shown_ = artwork;
        d::setBackground(layer(), artwork.fill.value_or(d::clear));
        picture_->setFrame(artwork.frame.value_or(d::Rect{5, 5, artwork.size.width - 10, artwork.size.height - 10}));
        // A nil CGImage draws nothing (an unreadable icon).
        if (!artwork.image.key.empty()) picture_->setContent(d::image(picture_->id(), d::localBounds(*picture_), artwork.image, "resizeAspect", std::nullopt, true));
        else picture_->clearContent();
    }
};
}

AppShortcutCanvas::AppShortcutCanvas(d::Context& context, const AppShortcutArtwork& artwork, AppShortcutCanvasServices services)
    : Canvas(core::Module::addApp, context), artwork_(&artwork), services_(std::move(services)) { timers_ = timerGroup(); }
AppShortcutCanvas::~AppShortcutCanvas() = default;

// MARK: Store (:27-46)
void AppShortcutCanvas::setStore(ShortcutFile file) { store_ = std::move(file); storeError_.reset(); reload(*store_); }
void AppShortcutCanvas::setStoreError(std::string message) {
    store_.reset(); storeError_ = std::move(message);
    if (renderedLanguage()) rerender();
}
void AppShortcutCanvas::didActivate() {
    if (store_) reload(*store_);
    if (storeError_) showError(*storeError_);
}
void AppShortcutCanvas::didDeactivate() { cancelReorder(); closeChooser(); dropTarget_ = false; }
// reload (:40-46): rebuilds from the store when its records changed.
void AppShortcutCanvas::reload(const ShortcutFile& file) {
    if (store_) store_ = file;
    if (file.items == items_ && renderedLanguage()) return;
    items_ = file.items;
    if (renderedLanguage()) rerender();
}
void AppShortcutCanvas::resetSession() {
    if (isActive()) return;
    dropTarget_ = false; relink_.reset();
    Canvas::resetSession();
}
// Each tile takes its artwork in place: the Mac has every icon at build, so
// a late one must not rebuild the page (and drop its entrance) here.
void AppShortcutCanvas::iconsChanged() { if (renderedLanguage()) for (const auto& bind : binders_) bind(); }
void AppShortcutCanvas::showError(std::string message) { toast(d::Icon::warn, "Notice", "提示", std::move(message)); }
const ShortcutRecord* AppShortcutCanvas::item(std::string_view id) const {
    const auto found = std::find_if(items_.begin(), items_.end(), [&](const auto& v) { return v.id == id; });
    return found == items_.end() ? nullptr : &*found;
}

// MARK: Drop, paste, import (:52-95)
void AppShortcutCanvas::setDropTarget(bool value) {
    const bool next = value && isActive() && !chooser_;
    if (next == dropTarget_) return;
    dropTarget_ = next;
    if (next && context().onLockParallax) context().onLockParallax();
    if (renderedLanguage()) rerender();
}
bool AppShortcutCanvas::acceptsApplications(std::span<const std::string> paths) const {
    return paths.size() == 1 && services_.isApplication && services_.isApplication(paths.front());
}
bool AppShortcutCanvas::importDrop(std::span<const std::string> paths) {
    if (!isActive() || chooser_) return false;
    std::vector<std::string> urls;
    for (const auto& p : paths) if (services_.isApplication && services_.isApplication(p)) urls.push_back(p);
    return !urls.empty() && importPaths(urls);
}
bool AppShortcutCanvas::importPaths(std::span<const std::string> paths) {
    if (!store_) { showError(text("Shortcut storage is unavailable.", "快捷方式存储不可用。")); return false; }
    if (paths.size() != 1) { showError(text("Choose one application to customize.", "请选择一个应用进行设置。")); return false; }
    AppShortcutRequest request; request.kind = AppShortcutRequest::Kind::add; request.path = paths.front();
    if (relink_) request.id = *std::exchange(relink_, std::nullopt);
    return submit(std::move(request));
}
bool AppShortcutCanvas::submit(AppShortcutRequest request) {
    if (!services_.request) return false;
    // Listed first: the owner may answer at once (a path that names no application).
    pending_.push_back(request);
    if (services_.request(request)) return true;
    if (const auto found = std::find(pending_.begin(), pending_.end(), request); found != pending_.end()) pending_.erase(found);
    return false;
}
// The owner's completion of the Mac's synchronous store calls.
void AppShortcutCanvas::requestFinished(const AppShortcutRequest& request, std::optional<ShortcutFile> committed, std::optional<std::string> error) {
    const auto found = std::find(pending_.begin(), pending_.end(), request);
    if (found == pending_.end()) return;
    pending_.erase(found);
    using K = AppShortcutRequest::Kind;
    const bool dropped = landing_ && *landing_ == request;
    if (dropped) landing_.reset();
    if (error || !committed) {
        showError(error.value_or(std::string{}));
        // save: a failed rename shows the saved name again (:224-226); move:
        // a refused write (the file changed on disk) puts the rows back in
        // the saved order (:229-230, :256-258).
        if (request.kind == K::save || request.kind == K::move) rerender();
        return;
    }
    // AppShortcutStore.save / remove committed.
    switch (request.kind) {
    case K::add: {
        const auto before = items_;
        revealsEnd_ = request.id.empty();
        reload(*committed);
        const auto saved = std::find_if(items_.rbegin(), items_.rend(), [&](const auto& v) {
            return request.id.empty() ? std::none_of(before.begin(), before.end(), [&](const auto& b) { return b.id == v.id; }) : v.id == request.id; });
        if (saved != items_.rend() && onSaved) onSaved(*saved);
        // Windows relink of an imported shortcut: its row updates, no toast.
        if (request.id.empty()) toast(d::Icon::plus, "App added", "已添加到右侧列表", text("Click the icon to change it", "点击图标即可更换"));
        revealsEnd_ = false;
        break;
    }
    case K::save:
        reload(*committed);
        if (const auto* saved = item(request.id); saved && onSaved) onSaved(*saved);
        break;
    case K::remove:
        reload(*committed);
        if (onRemoved) onRemoved(request.name);
        toast(d::Icon::trash, "Shortcut removed", "应用未被卸载");
        break;
    case K::move: {
        const auto before = itemIDs();
        if (dropped) {
            // dragRow's drop (:256-265): the rows already show the new order;
            // the page order (accessibility) follows once the row has landed.
            if (store_) store_ = *committed;
            items_ = committed->items;
            if (onReordered) onReordered();
            after(d::ListReorder::landingDelay(env().reduceMotion), timers_, [this] { if (isActive() && !reorder_) rerender(); });
            break;
        }
        // moveRow (:220-230): the list and the right-hand order follow, the moved row in view.
        reload(*committed);
        if (itemIDs() == before) break;
        if (onReordered) onReordered();
        const auto slot = std::find_if(items_.begin(), items_.end(), [&](const auto& v) { return v.id == request.id; });
        if (list_ && slot != items_.end()) list_->reveal({0, double(slot - items_.begin()) * pitch, list_->contentWidth(), rowHeight});
        break;
    }
    }
}
std::vector<std::string> AppShortcutCanvas::itemIDs() const {
    std::vector<std::string> ids;
    for (const auto& v : items_) ids.push_back(v.id);
    return ids;
}

// MARK: Page (:99-145)
void AppShortcutCanvas::buildPage(d::Page& page) {
    built_ = true;
    binders_.clear(); rows_.clear(); tiles_.clear(); grips_.clear(); list_ = nullptr; reorder_.reset();
    auto& add = page.make<d::Button>("apps:add", text("Add App", "添加应用"), d::Icon::plus, d::Button::Kind::primary, d::Button::Height::compact);
    add.place({0, 0});
    add.setEnabled(store_.has_value());
    add.onActivate = [this] { chooseApplication(); };
    header().configure({"SHORTCUTS"}, "MOD-18", title("Shortcuts", "应用快捷方式"), d::format::pad2(std::int64_t(items_.size())), false, {&add});
    const double top = d::metric::headerHeight;
    const double height = std::min(420., d::metric::boxHeight - top);
    if (items_.empty()) {
        d::Layer& empty = d::emptyState::make(page.builder(), "apps.empty", "NO SHORTCUTS");
        empty.setOrigin({0, top}); page.layer().add(empty);
        page.stagger(empty);
    } else {
        const double content = double(items_.size()) * pitch - d::metric::rowGap;
        auto& scroll = page.scrollArea("apps", {0, top, d::metric::boxWidth, height});
        scroll.setContentHeight(content);
        list_ = &scroll;
        const double width = scroll.contentWidth();
        for (std::size_t index = 0; index < items_.size(); ++index) {
            auto& row = makeRow(items_[index], width, page);
            row.layer().setOrigin({0, double(index) * pitch});
            scroll.content().add(row.layer());
            page.stagger(row.layer());
        }
        page.restoreOffset(scroll);
        if (revealsEnd_) scroll.setOffset(scroll.maximumOffset());
    }
    revealsEnd_ = false;
    if (dropTarget_) {
        d::Layer& ring = page.makeLayer("apps.drop");
        ring.setFrame({0, top, d::metric::boxWidth, height});
        d::setBorder(ring, std::nullopt, env().palette.yellow(), 1);
        d::Layer& bar = page.makeLayer("apps.drop.bar");
        bar.setFrame({0, 0, 3, height}); d::setBackground(bar, env().palette.yellow());
        ring.add(bar);
        page.layer().add(ring);
    }
}

// makeRow (:148-196): row plate 56, padding 8 10 6 16: grip (in the left
// padding), tile, name field, Open, Remove.
d::RowPlate& AppShortcutCanvas::makeRow(const ShortcutRecord& record, double width, d::Page& page) {
    auto& row = page.make<d::RowPlate>("apps:" + record.id, "", std::nullopt, d::RowPlate::Variant::standard, "›", width, rowHeight);
    const double centre = 8 + (rowHeight - 14) / 2;
    const std::string id = record.id;
    // The row's left edge: drag to reorder; a press opens the item menu (Change icon, move,
    // Remove), which is also how the keyboard and screen readers reach those actions.
    auto& grip = page.make<d::DragGrip>("apps:" + record.id + ":grip", text("App options", "应用选项") + ", " + record.name,
        d::Size{14, rowHeight}, d::Point{5, centre - 5});
    auto* gripPointer = &grip;
    grip.onActivate = [this, id, gripPointer] { openItemMenu(id, gripPointer); };
    grip.onDrag = [this, id, gripPointer](d::DragGrip::Phase phase, d::Point point) {
        if (const auto rootPoint = d::convert(point, gripPointer->layer(), root())) dragRow(id, phase, *rootPoint);
    };
    auto& tile = page.make<AppShortcutTile>("apps:" + record.id + ":icon", text("Change icon", "更换图标") + ", " + record.name,
        artwork(record.iconPreset, record));
    binders_.push_back([this, &tile, id] { if (const auto* current = item(id)) tile.setArtwork(artwork(current->iconPreset, *current)); });
    tile.onActivate = [this, &tile, id] {
        if (menuOpen()) { closeMenu(); return; }
        if (const auto* current = item(id)) openIconMenu(*current, tile);
    };
    auto& removeButton = page.make<d::IconButton>("apps:" + record.id + ":remove", text("Remove", "移除") + " " + record.name, d::Icon::x);
    removeButton.onActivate = [this, id] { if (const auto* current = item(id)) confirmRemove(*current); };
    auto& openButton = page.make<d::IconButton>("apps:" + record.id + ":open", text("Open", "打开") + " " + record.name, d::Icon::ext, d::IconButton::Style::light);
    openButton.onActivate = [this, id] { if (const auto* current = item(id)) open(*current); };
    const double fieldX = 16 + 40 + 14;
    const double removeX = width - 10 - 30, openX = removeX - 8 - 30;
    d::TextFieldOptions options; options.light = true; options.maximumLength = nameLimit;
    auto& field = page.make<d::TextField>("apps:" + record.id + ":name", text("Shortcut name", "快捷方式名称"), record.name, "",
        d::Size{openX - 8 - fieldX, 30}, options);
    field.onCommit = [this, id, &field](const std::string& value) { rename(id, value, field); };
    registerField(field);
    grip.layer().setOrigin({0, 0});
    tile.layer().setOrigin({16, centre - 20});
    field.layer().setOrigin({fieldX, centre - 15});
    openButton.layer().setOrigin({openX, centre - 15});
    removeButton.layer().setOrigin({removeX, centre - 15});
    for (d::Layer* part : {&grip.layer(), &tile.layer(), &field.layer(), &openButton.layer(), &removeButton.layer()}) row.layer().add(*part);
    page.add(row);
    page.add(std::vector<d::Control*>{&grip, &tile, &field, &openButton, &removeButton});
    rows_.emplace_back(id, &row); tiles_.emplace_back(id, &tile); grips_.push_back(&grip);
    return row;
}

// openItemMenu (:198-217): the grip's press: Change icon, Move up, Move down, Remove.
void AppShortcutCanvas::openItemMenu(const std::string& id, const d::Control* anchor) {
    const auto found = std::find_if(items_.begin(), items_.end(), [&](const auto& v) { return v.id == id; });
    const auto row = std::find_if(rows_.begin(), rows_.end(), [&](const auto& r) { return r.first == id; });
    if (found == items_.end() || row == rows_.end()) return;
    if (menuOpen()) { closeMenu(); return; }
    const auto index = found - items_.begin();
    const auto count = std::ptrdiff_t(items_.size());
    std::vector<d::Menu::Item> entries(4);
    entries[0].id = "icon"; entries[0].title = text("Change icon", "更换图标");
    entries[1].id = "up"; entries[1].title = text("Move up", "上移"); entries[1].separatorBefore = true; entries[1].enabled = index > 0;
    entries[2].id = "down"; entries[2].title = text("Move down", "下移"); entries[2].enabled = index + 1 < count;
    entries[3].id = "remove"; entries[3].title = text("Remove", "移除"); entries[3].danger = true; entries[3].separatorBefore = true;
    const d::Rect anchorRect = anchor ? anchor->rect(root()) : row->second->rect(root());
    openMenu("apps:item", std::move(entries), anchorRect, anchor, 168, [this, id, index](const d::Menu::Item& picked) {
        const auto* current = item(id);
        if (!current) return;
        if (picked.id == "icon") { if (auto* anchorTile = iconTile(id)) openIconMenu(*current, *anchorTile); }
        else if (picked.id == "up") moveRow(id, index - 1);
        else if (picked.id == "down") moveRow(id, index + 1);
        else confirmRemove(*current);
    });
}
// moveRow (:219-230): Move up / Move down: one store write, then the list and
// the right-hand order follow (requestFinished).
void AppShortcutCanvas::moveRow(const std::string& id, std::ptrdiff_t index) {
    if (!store_) return;
    AppShortcutRequest request; request.kind = AppShortcutRequest::Kind::move; request.id = id; request.index = index;
    (void)submit(std::move(request));
}

// MARK: Reordering by the grip (:232-280)
d::ListReorder::Rows AppShortcutCanvas::reorderRows() const {
    return [this](const std::string& id) -> d::Layer* {
        const auto row = std::find_if(rows_.begin(), rows_.end(), [&](const auto& r) { return r.first == id; });
        return row == rows_.end() ? nullptr : &row->second->layer();
    };
}
// Lift, drag and drop as the Notes TODO cards (ListReorder); the drop is one
// store write. Nothing lifts with one row, the chooser or a drop.
void AppShortcutCanvas::dragRow(const std::string& id, d::DragGrip::Phase phase, d::Point point) {
    if (!list_) return;
    auto& list = *list_;
    const auto local = d::convert(point, root(), list.content());
    if (!local) return;
    switch (phase) {
    case d::DragGrip::Phase::began: {
        d::Layer* row = reorderRows()(id);
        if (items_.size() < 2 || chooser_ || dropTarget_ || landing_ || !row) return;
        closePopups(); endEditing(true);
        reorder_ = d::ListReorder::lift(id, itemIDs(), *row, local->y, 0, pitch, clock());
        if (reorder_ && context().onLockParallax) context().onLockParallax();
        break;
    }
    case d::DragGrip::Phase::moved:
        if (!reorder_ || reorder_->id() != id) return;
        reorder_->move(point, list, root(), reorderRows(), clock());
        break;
    case d::DragGrip::Phase::ended: {
        if (!reorder_ || reorder_->id() != id) return;
        // Closing the HUD or leaving the module mid-drag puts the row back (Mac 2ca2482).
        if (cancellingGestures()) { cancelReorder(); return; }
        const d::ListReorder state = std::move(*reorder_);
        reorder_.reset();
        state.drop(reorderRows()(id), clock());
        if (state.order() == itemIDs() || !store_) return;
        AppShortcutRequest request; request.kind = AppShortcutRequest::Kind::move; request.id = id; request.index = state.index();
        landing_ = request;
        // An owner that refuses the request at once: the rows return to the saved order.
        if (!submit(std::move(request))) { landing_.reset(); rerender(); }
        break;
    }
    }
}
// A lifted row returns to its slot (the module was left mid-drag).
void AppShortcutCanvas::cancelReorder() {
    if (!reorder_) return;
    const d::ListReorder state = std::move(*reorder_);
    reorder_.reset();
    state.cancel(itemIDs(), reorderRows());
}
std::optional<d::Cursor> AppShortcutCanvas::cursor(std::optional<d::Point> point) const {
    if (!reorder_ && !(isActive() && items_.size() > 1 && !chooser_)) return std::nullopt;
    return d::DragGrip::cursor(reorder_.has_value(), grips_, point, root());
}
d::DragGrip* AppShortcutCanvas::rowGrip(std::string_view id) const {
    const std::string wanted = "apps:" + std::string(id) + ":grip";
    for (auto* g : grips_) if (g->id() == wanted) return g;
    return nullptr;
}
d::Control* AppShortcutCanvas::iconTile(std::string_view id) const {
    const auto found = std::find_if(tiles_.begin(), tiles_.end(), [&](const auto& t) { return t.first == id; });
    return found == tiles_.end() ? nullptr : found->second;
}
// dragRowForTesting (:375-382).
void AppShortcutCanvas::dragRow(const std::string& id, int slot) {
    auto* g = rowGrip(id);
    if (!g) return;
    const auto start = d::convert(d::Point{7, 28}, g->layer(), root());
    if (!start) return;
    const auto found = std::find_if(items_.begin(), items_.end(), [&](const auto& v) { return v.id == id; });
    d::ListReorder::simulate(*start, slot - (found == items_.end() ? 0 : int(found - items_.begin())), pitch,
        [this, &id](d::DragGrip::Phase phase, d::Point point) { dragRow(id, phase, point); });
}

// openIconMenu (:192-200): every preset as the tile it would make, in an icon grid.
void AppShortcutCanvas::openIconMenu(const ShortcutRecord& record, const d::Control& anchor) {
    std::vector<d::Menu::Item> choices;
    for (const auto& preset : artwork_->presets()) {
        d::Menu::Item choice; choice.id = preset.id; choice.title = artwork_->title(preset.id, env().language);
        choice.selected = preset.id == record.iconPreset; choice.icon = artwork(preset.id, record);
        choices.push_back(std::move(choice));
    }
    const std::string id = record.id;
    openMenu("apps:iconMenu", std::move(choices), anchor, 0., [this, id](const d::Menu::Item& picked) {
        const auto* current = item(id);
        if (!current || !artwork_->preset(picked.id) || picked.id == current->iconPreset) return;
        save(id, current->name, picked.id);
    });
}

// rename (:202-208) and save (:210-222).
void AppShortcutCanvas::rename(const std::string& id, const std::string& value, d::TextField& field) {
    const auto* record = item(id);
    if (!record) return;
    const auto name = services_.trimmed ? services_.trimmed(value) : value;
    if (name.empty()) { field.setText(record->name); return; }
    if (name == record->name) return;
    save(id, name, record->iconPreset);
}
void AppShortcutCanvas::save(const std::string& id, std::string name, std::string preset) {
    if (!store_) return;
    AppShortcutRequest request; request.kind = AppShortcutRequest::Kind::save; request.id = id; request.name = std::move(name); request.icon = std::move(preset);
    (void)submit(std::move(request));
}

// confirmRemove (:224-237).
void AppShortcutCanvas::confirmRemove(const ShortcutRecord& record) {
    d::ConfirmRequest request;
    request.id = "apps:remove";
    const std::array<std::string_view, 1> argument{record.name};
    request.title = text("Remove {0}?", "移除 {0}？", argument);
    request.message = text("The app itself stays installed.", "应用本身不会被卸载。");
    request.confirmTitle = text("Remove", "移除");
    request.danger = true; request.icon = d::Icon::trash;
    const std::string id = record.id, name = record.name;
    confirm(std::move(request), [this, id, name] {
        if (!store_ || !item(id)) return;
        AppShortcutRequest removal; removal.kind = AppShortcutRequest::Kind::remove; removal.id = id; removal.name = name;
        (void)submit(std::move(removal));
    });
}

// Open (:166-169): the overlay owner launches after the HUD closed. Windows:
// an imported Mac shortcut has no Windows target yet and relinks instead.
void AppShortcutCanvas::open(const ShortcutRecord& record) {
    if (!record.native()) { chooseApplication(record.id); return; }
    if (services_.launch) services_.launch(record.id);
    toast(d::Icon::ext, "Opening", "正在打开", record.name);
}

// MARK: Application chooser (:241-274)
void AppShortcutCanvas::chooseApplication(std::optional<std::string> relink) {
    if (!store_ || chooser_ || !services_.choose) return;
    endEditing(true);
    chooser_ = true; relink_ = std::move(relink);
    if (!services_.choose()) { chooser_ = false; relink_.reset(); return; }
    if (context().onInputStateChange) context().onInputStateChange();
}
void AppShortcutCanvas::chooserFinished(std::optional<std::vector<std::string>> paths) {
    if (!chooser_) return;
    chooser_ = false;
    if (context().onInputStateChange) context().onInputStateChange();
    if (!isActive()) { relink_.reset(); return; }
    if (paths) (void)importPaths(*paths);
    relink_.reset();
}
void AppShortcutCanvas::closeChooser() {
    relink_.reset();
    if (!std::exchange(chooser_, false)) return;
    if (services_.cancelChooser) services_.cancelChooser();
}

// Ctrl+V adds an application copied in Explorer (:277-284): Command alone
// on the Mac, Ctrl alone here.
bool AppShortcutCanvas::handleKey(const d::KeyInput& key) {
    if (!key.control || key.shift || key.alt || key.win || (key.character != U'v' && key.character != U'V') || chooser_) return false;
    std::vector<std::string> urls;
    if (services_.pasteboard) for (auto& p : services_.pasteboard()) if (services_.isApplication && services_.isApplication(p)) urls.push_back(std::move(p));
    if (urls.empty()) return false;
    (void)importPaths(urls);
    return true;
}

// MARK: Icons (:291-320)
// artwork: the app's own icon on its darkened average colour, or the preset
// glyph, dark on the theme accent (white .93 on that colour for an App icon
// without one).
d::Menu::IconTile AppShortcutCanvas::artwork(std::string_view preset, const ShortcutRecord& record) const {
    d::Menu::IconTile tile; tile.size = {40, 40};
    const d::Rect box{5, 5, 30, 30};
    const bool original = preset == "original";
    if (original) if (const auto icon = services_.icon ? services_.icon(record.id) : std::nullopt) {
        tile.image = *icon; tile.frame = box; tile.fill = originalColor(record);
        return tile;
    }
    if (const auto* art = artwork_->preset(preset)) {
        tile.image = {d::ImageSource::Kind::asset, art->file, art->sha256, 0};
        tile.frame = art->frame;
    } else tile.frame = box;
    tile.fill = original ? originalColor(record) : env().palette.yellow();
    return tile;
}
d::Color AppShortcutCanvas::originalColor(const ShortcutRecord& record) const {
    const auto color = services_.tileColor ? services_.tileColor(record.id) : std::nullopt;
    return color.value_or(d::rgb(0x2A2A2A));
}
} // namespace endfield::modules
