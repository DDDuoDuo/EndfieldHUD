// AppShortcutCanvas and AppShortcutTile (Sources/AppShortcutCanvas.swift, Mac 1.3.0),
// statement by statement (line numbers cite it).
#include "modules/app_shortcut_canvas.hpp"
#include "modules/hud_design_badges.hpp"
#include "modules/hud_design_format.hpp"
#include <algorithm>

namespace endfield::modules {
namespace d = design;
namespace {
// The 40 x 40 app icon tile (:322-347): the shortcut's artwork with an inset
// ring rgba(0,0,0,.25); it opens Choose icon.
class AppShortcutTile final : public d::Control {
public:
    AppShortcutTile(d::Builder& b, std::string id, std::string label, const d::Menu::IconTile& artwork)
        : Control(b.env, id, std::move(label), b.layer(id)), brighten_(&b.layer(id + ".brighten")) {
        const d::Rect bounds{0, 0, artwork.size.width, artwork.size.height};
        layer().setBounds(artwork.size); layer().setAnimatable();
        d::setBackground(layer(), artwork.fill.value_or(d::clear));
        d::Layer& picture = b.layer(id + ".icon");
        picture.setFrame(artwork.frame.value_or(d::Rect{5, 5, bounds.width - 10, bounds.height - 10}));
        // A nil CGImage draws nothing (an unreadable icon).
        if (!artwork.image.key.empty()) picture.setContent(d::image(picture.id(), d::localBounds(picture), artwork.image, "resizeAspect", std::nullopt, true));
        d::Layer& ring = b.layer(id + ".ring");
        ring.setFrame(bounds); d::setBorder(ring, std::nullopt, d::black(.25), 1);
        brighten_->setFrame(bounds); d::setBackground(*brighten_, d::white(.07)); brighten_->setAnimatable();
        brighten_->setValue(d::Channel::opacity, 0);
        for (d::Layer* part : {&picture, brighten_, &ring}) layer().add(*part);
        setTarget(bounds);
    }
    void hoverChanged() override { d::applyHoverMotion(*this, 1, 1, .965, brighten_); }
    void pressChanged() override { d::applyHoverMotion(*this, 1, 1, .965, brighten_); }
    std::optional<d::AccessibilityRole> accessibilityRole() const override { return d::AccessibilityRole::popUp; }
    void accessibilityItems(const d::Layer& root, std::vector<d::AccessibilityItem>& out) const override {
        if (label.empty()) return;
        auto item = accessibilityItem(root, d::AccessibilityRole::popUp); item.value = label; // .popUp(label)
        out.push_back(std::move(item));
    }
private:
    d::Layer* brighten_;
};
}

AppShortcutCanvas::AppShortcutCanvas(d::Context& context, const AppShortcutArtwork& artwork, AppShortcutCanvasServices services)
    : Canvas(core::Module::addApp, context), artwork_(&artwork), services_(std::move(services)) {}
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
void AppShortcutCanvas::didDeactivate() { closeChooser(); dropTarget_ = false; }
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
    if (renderedLanguage()) for (const auto& area : page().scrollAreas()) area->setOffset(0);
}
void AppShortcutCanvas::iconsChanged() { if (renderedLanguage() && !editingText()) rerender(); }
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
    if (error || !committed) {
        showError(error.value_or(std::string{}));
        if (request.kind == K::save) rerender(); // save: a failed rename shows the saved name again (:224-226)
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
        if (request.id.empty()) toast(d::Icon::plus, "App added", "已添加到右侧列表");
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
    }
}

// MARK: Page (:99-145)
void AppShortcutCanvas::buildPage(d::Page& page) {
    built_ = true;
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
        const double content = double(items_.size()) * (rowHeight + d::metric::rowGap) - d::metric::rowGap;
        auto& scroll = page.scrollArea("apps", {0, top, d::metric::boxWidth, height});
        scroll.setContentHeight(content);
        const double width = scroll.contentWidth();
        for (std::size_t index = 0; index < items_.size(); ++index) {
            auto& row = makeRow(items_[index], width, page);
            row.layer().setOrigin({0, double(index) * (rowHeight + d::metric::rowGap)});
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

// makeRow (:148-189): row plate 56, padding 8 10 6 16: tile, name field, Open, Remove.
d::RowPlate& AppShortcutCanvas::makeRow(const ShortcutRecord& record, double width, d::Page& page) {
    auto& row = page.make<d::RowPlate>("apps:" + record.id, "", std::nullopt, d::RowPlate::Variant::standard, "›", width, rowHeight);
    const double centre = 8 + (rowHeight - 14) / 2;
    auto& tile = page.make<AppShortcutTile>("apps:" + record.id + ":icon", text("Choose icon", "选择图标") + ", " + record.name,
        artwork(record.iconPreset, record));
    const std::string id = record.id;
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
    tile.layer().setOrigin({16, centre - 20});
    field.layer().setOrigin({fieldX, centre - 15});
    openButton.layer().setOrigin({openX, centre - 15});
    removeButton.layer().setOrigin({removeX, centre - 15});
    for (d::Layer* part : {&tile.layer(), &field.layer(), &openButton.layer(), &removeButton.layer()}) row.layer().add(*part);
    page.add(row);
    page.add(std::vector<d::Control*>{&tile, &field, &openButton, &removeButton});
    return row;
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
