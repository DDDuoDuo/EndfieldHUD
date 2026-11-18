// AppShortcutCanvas and AppShortcutTile (Sources/AppShortcutCanvas.swift, Mac 1.3.0),
// statement by statement (line numbers cite it).
#include "modules/app_shortcut_canvas.hpp"
#include "modules/hud_design_badges.hpp"
#include "modules/hud_design_format.hpp"
#include <algorithm>

namespace endfield::modules {
namespace d = design;
namespace {
// Layered translucent plates matching the HUD right-side navigation tiles.
class ShortcutGridTile final : public d::Control {
public:
    ShortcutGridTile(d::Builder& b, std::string id, std::string name, double side, d::Menu::IconTile art)
        : Control(b.env, id, name, b.layer(id)), picture_(&b.layer(id + ".picture")),
          plate_(&b.layer(id + ".plate")), ring_(&b.layer(id + ".ring")) {
        layer().setBounds({side, side}); layer().setAnimatable(); layer().setPartRoot(true); setTarget({0, 0, side, side});
        auto& rear = b.layer(id + ".rear");
        rear.setFrame({1, 11, side - 10, side - 13});
        d::setBorder(rear, d::white(.20), d::white(.55), 1.5, 7); layer().add(rear);
        auto& rearRing = b.layer(id + ".rearRing");
        rearRing.setFrame({4, 14, side - 16, side - 19});
        d::setBorder(rearRing, std::nullopt, d::black(.28), 1, 5); layer().add(rearRing);
        plate_->setFrame({7, 1, side - 9, side - 12}); layer().add(*plate_);
        ring_->setFrame({10, 4, side - 15, side - 18}); layer().add(*ring_);
        picture_->setFrame({(side - 32) / 2 + 2, 10, 32, 32}); layer().add(*picture_);
        setArtwork(art);
        const auto caption = b.env.characters.count(name) > 8 ? b.env.characters.prefix(name, 7) + "…" : name;
        layer().add(d::makeText(b, id + ".label", caption,
            d::TextStyle{12, d::FontWeight::medium, d::rgb(0x151716)}, {10, side - 40, side - 18, 24}, d::TextAlignment::center));
        checkbox_=&b.layer(id+".checkbox"); checkbox_->setFrame({side-23,5,18,18}); layer().add(*checkbox_);
        check_=&b.layer(id+".check");check_->setFrame({2,2,14,14});checkbox_->add(*check_);
        check_->setContent(d::iconNode(check_->id(),d::Icon::check,14,d::black(1),{7,7}));
        redraw();
    }
    void setArtwork(const d::Menu::IconTile& art) {
        if(shown_ && *shown_==art)return;shown_=art;
        if (!art.image.key.empty()) picture_->setContent(d::image(picture_->id(), d::localBounds(*picture_), art.image, "resizeAspect", std::nullopt, true));
        else picture_->clearContent();
    }
    void accessibilityItems(const d::Layer& root,std::vector<d::AccessibilityItem>& out) const override {
        auto item=accessibilityItem(root,draggable_?d::AccessibilityRole::toggle:d::AccessibilityRole::button);
        item.on=selected_;out.push_back(std::move(item));
    }
    void setSelected(bool selected) { if(selected_==selected)return;selected_ = selected; redraw(); }
    void hoverChanged() override { redraw(); }
    void pressChanged() override { redraw(); }
    std::function<void(d::Point)> onDrop;
    std::function<void(d::Point)> onDrag;
    void setDraggable(bool value) { draggable_ = value; redraw(); }
    bool pointerDown(d::Point p) override {
        start_ = p; origin_ = {layer().frame().x, layer().frame().y}; dragged_ = false; return enabled();
    }
    d::Point displacement(d::Point p) const {
        return {p.x + layer().frame().x - origin_.x - start_.x,
                p.y + layer().frame().y - origin_.y - start_.y};
    }
    void pointerDragged(d::Point p) override {
        const auto delta = displacement(p);
        if (std::abs(delta.x) + std::abs(delta.y) > 8) dragged_ = true;
        if (dragged_ && draggable_) {
            layer().setZPosition(10);
            layer().setOrigin({origin_.x + delta.x, origin_.y + delta.y});
            if(onDrag)onDrag(start_);
        }
    }
    void pointerUp(d::Point p) override {
        const bool dragged = std::exchange(dragged_, false);
        const auto delta = displacement(p);
        layer().setOrigin(origin_); layer().setZPosition(0);
        // Either callback can rebuild the page and destroy this control.
        if (dragged) { const auto drop = onDrop; if (drop) drop(delta); }
        else Control::pointerUp(p);
    }
private:
    void redraw() {
        checkbox_->setHidden(!draggable_);check_->setHidden(!selected_);
        d::setBorder(*checkbox_,selected_?env().palette.yellow():d::white(.95),d::black(.7),1.5,3);
        const bool highlighted = selected_ || hovered();
        d::setBorder(*plate_, d::white(pressed() ? .62 : highlighted ? .90 : .78),
            selected_ ? env().palette.yellow() : d::white(.75), 1.5, 7);
        d::setBorder(*ring_, std::nullopt, selected_ ? d::black(.55) : d::black(.33), 1, 5);
    }
    std::optional<d::Menu::IconTile> shown_;
    d::Layer *picture_, *plate_, *ring_, *checkbox_{}, *check_{}; d::Point start_{}, origin_{}; bool dragged_{}, selected_{}, draggable_{};
};

// Same palette, yellow gradient, underline and typography as Clipboard's Tabs;
// retained custom controls additionally support group dragging/multi-selection.
class ShortcutTab final : public d::Control {
public:
    ShortcutTab(d::Builder& b,std::string id,std::string name,bool selected,double width)
      : Control(b.env,id,name,b.layer(id)),selected_(selected),width_(width) {
        layer().setBounds({width,41});setTarget({0,0,width,37});
        fill_=&b.layer(id+".fill");fill_->setFrame({0,0,width,37});layer().add(*fill_);
        underline_=&b.layer(id+".underline");underline_->setFrame({width*.32,40,width*.36,3});layer().add(*underline_);
        auto& separator=b.layer(id+".separator");separator.setFrame({width-1,10,1,14});d::setBackground(separator,b.env.palette.tone(0x4A4A4A));layer().add(separator);
        const auto caption=b.env.characters.count(name)>8?b.env.characters.prefix(name,7)+"…":name;
        text_=&d::makeText(b,id+".label",caption,d::type::tab(b.env.palette).sized(13),{4,9,width-8,19},d::TextAlignment::center);text_->setAnimatable();layer().add(*text_);draw();
    }
    void select(bool value){if(value==selected_)return;selected_=value;draw();}
    void hoverChanged()override{draw();}
    std::function<void(d::Point)> onDrop;
    bool pointerDown(d::Point p)override{start_=p;dragged_=false;return enabled();}
    void pointerDragged(d::Point p)override{if(std::abs(p.x-start_.x)+std::abs(p.y-start_.y)>8)dragged_=true;}
    void pointerUp(d::Point p)override{const bool dragged=std::exchange(dragged_,false);const auto drop=onDrop;if(dragged){if(drop)drop({p.x-start_.x,p.y-start_.y});}else Control::pointerUp(p);}
    std::optional<d::AccessibilityRole> accessibilityRole()const override{return d::AccessibilityRole::tab;}
private:
    void draw(){
        const auto& p=env().palette;
        if(selected_){const std::array<d::Color,3> colors{p.yellowHigh(),p.yellow(),p.yellow()};const std::array<double,3> stops{0,.45,1};fill_->setContent(d::plate(fill_->id(),{0,0,width_,37},colors,stops,0));}
        else d::setBackground(*fill_,hovered()?p.foreground(.06):d::clear);
        underline_->setHidden(!selected_);d::setBackground(*underline_,p.accentInk());
        d::editText(*text_,[&](d::TextBlock& block){block.setStyle(d::type::tab(p).sized(13).weighted(selected_?d::FontWeight::bold:d::FontWeight::medium).colored(d::white(1)));});
        text_->setTint(selected_?d::fixed::nearBlack:hovered()?p.foreground(1):p.token(d::ThemeToken::tabText));
    }
    d::Layer *fill_,*underline_,*text_;d::Point start_{};bool dragged_{},selected_{};double width_{};
};

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
    : Canvas(core::Module::addApp, context), artwork_(&artwork), services_(std::move(services)) { timers_ = timerGroup(); gridTimers_=timerGroup(); }
AppShortcutCanvas::~AppShortcutCanvas() = default;

// MARK: Store (:27-46)
void AppShortcutCanvas::setStore(ShortcutFile file) { store_ = std::move(file); storeError_.reset(); reload(*store_); }
void AppShortcutCanvas::setStoreError(std::string message) {
    store_.reset(); groupID_.clear(); items_.clear(); storeError_ = std::move(message);
    if (renderedLanguage()) rerender();
}
void AppShortcutCanvas::didActivate() {
    if (store_) reload(*store_);
    if (storeError_) showError(*storeError_);
}
void AppShortcutCanvas::didDeactivate() { cancelReorder(); closeChooser(); dropTarget_ = false; }
// reload (:40-46): rebuilds from the store when its records changed.
void AppShortcutCanvas::reload(const ShortcutFile& file) {
    if (!restoredGroup_) { groupID_=file.activeGroupID; restoredGroup_=true; }
    if (store_) store_ = file;
    if (!groupID_.empty() && std::none_of(file.groups.begin(), file.groups.end(), [&](const auto& g) { return g.id == groupID_; })) { groupID_.clear(); }
    items_.clear();
    items_ = shortcutItems(file,groupID_);
    std::erase_if(selectedItems_, [this, &file](const auto& id) { return groupsOverview_ ? std::none_of(file.groups.begin(), file.groups.end(), [&](const auto& group) { return group.id == id; }) : !item(id); });
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
    return !groupsOverview_ && !paths.empty() && services_.isApplication && std::all_of(paths.begin(), paths.end(), services_.isApplication);
}
bool AppShortcutCanvas::importDrop(std::span<const std::string> paths) {
    if (!isActive() || chooser_) return false;
    std::vector<std::string> urls;
    for (const auto& p : paths) if (services_.isApplication && services_.isApplication(p)) urls.push_back(p);
    return !urls.empty() && importPaths(urls);
}
bool AppShortcutCanvas::importPaths(std::span<const std::string> paths) {
    if (groupsOverview_) return false;
    if (!store_) { showError(text("Shortcut storage is unavailable", "快捷方式存储不可用")); return false; }
    if (paths.size() != 1) {
        if (relink_) { showError(text("Choose one application to customize", "请选择一个应用进行设置")); return false; }
        bool accepted = false;
        for (const auto& path : paths) accepted = importPaths(std::span<const std::string>(&path, 1)) || accepted;
        return accepted;
    }
    AppShortcutRequest request; request.kind = AppShortcutRequest::Kind::add; request.path = paths.front(); request.groupID = groupID_;
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
        if (request.kind == K::save || request.kind == K::move || request.kind == K::saveGroup || request.kind == K::moveGroup) rerender();
        return;
    }
    // AppShortcutStore.save / remove committed.
    switch (request.kind) {
    case K::saveGroup: {
        const auto before = store_->groups;
        store_ = *committed;
        if (request.groupID.empty()) {
            const auto created = std::find_if(committed->groups.begin(), committed->groups.end(), [&](const auto& g) {
                return std::none_of(before.begin(), before.end(), [&](const auto& old) { return old.id == g.id; });
            });
            if (created != committed->groups.end()) { groupID_ = created->id; groupsOverview_ = false; }
        }
        reload(*committed); break;
    }
    case K::membership: case K::selectGroup: case K::scanDesktop: case K::restoreDesktop:
    case K::moveGroup:
    case K::removeGroup:
        reload(*committed); break;
    case K::add: {
        const auto before = items_;
        revealsEnd_ = request.id.empty();
        reload(*committed);
        const auto saved = std::find_if(items_.rbegin(), items_.rend(), [&](const auto& v) {
            return request.id.empty() ? std::none_of(before.begin(), before.end(), [&](const auto& b) { return b.id == v.id; }) : v.id == request.id; });
        if (saved != items_.rend() && onSaved) onSaved(*saved);
        // Windows relink of an imported shortcut: its row updates, no toast.
        if (request.id.empty()) {
            toast(d::Icon::plus, "Shortcut added", "已添加快捷方式");
        }
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
            items_.clear();
            items_=shortcutItems(*committed,groupID_);
            if (onReordered) onReordered();
            after(d::ListReorder::landingDelay(env().reduceMotion), timers_, [this] { if (isActive() && !reorder_) rerender(); });
            break;
        }
        // moveRow (:220-230): the list and the right-hand order follow, the moved row in view.
        reload(*committed);
        if (itemIDs() == before) break;
        if (onReordered) onReordered();
        const auto slot = std::find_if(items_.begin(), items_.end(), [&](const auto& v) { return v.id == request.id; });
        if (list_ && slot != items_.end()) list_->reveal({0, double((slot - items_.begin()) / appGridColumns) * appGridPitch, list_->contentWidth(), appGridPitch-appGridGap});
        break;
    }
    }
}
// A move (a grip drop or Move up / Move down) the owner has not answered:
// items_ is the order before it, so no other reorder starts from it.
bool AppShortcutCanvas::moving() const {
    return std::any_of(pending_.begin(), pending_.end(), [](const auto& r) { return r.kind == AppShortcutRequest::Kind::move; });
}
std::vector<std::string> AppShortcutCanvas::itemIDs() const {
    std::vector<std::string> ids;
    for (const auto& v : items_) ids.push_back(v.id);
    return ids;
}

void AppShortcutCanvas::showGroup(std::string id) {
    endEditing(true); closeMenu(); closeChooser();
    editingGroup_ = false; groupsOverview_ = false; groupID_ = std::move(id); selectedItem_.clear(); selectedItems_.clear();
    if (store_) { reload(*store_); AppShortcutRequest request;request.kind=AppShortcutRequest::Kind::selectGroup;request.groupID=groupID_;(void)submit(std::move(request)); }
}
void AppShortcutCanvas::showGroups() {
    endEditing(true);closeMenu();closeChooser();editingGroup_=true;groupsOverview_=true;
    selectedItem_.clear();selectedItems_.clear();if(renderedLanguage())rerender();
}
void AppShortcutCanvas::createGroup() {
    if (!store_) { showError(text("Shortcut storage is unavailable", "快捷方式存储不可用")); return; }
    AppShortcutRequest request; request.kind = AppShortcutRequest::Kind::saveGroup;
    request.name = text("New group", "新建分组");
    (void)submit(std::move(request));
}
void AppShortcutCanvas::removeGroup() {
    const auto id = groupID_;
    if(id.empty())return;
    d::ConfirmRequest prompt; prompt.id = "apps:delete-group";
    prompt.title = text("Delete this group?", "删除这个分组？");
    prompt.message = text("Delete the group; keep its shortcuts in overview", "仅删除分组，快捷方式保留在总览中");
    prompt.confirmTitle = text("Delete", "删除"); prompt.danger = true; prompt.icon = d::Icon::trash;
    confirm(std::move(prompt), [this, id] {
        AppShortcutRequest request; request.kind = AppShortcutRequest::Kind::removeGroup; request.groupID = id;
        (void)submit(std::move(request));
    });
}
bool AppShortcutCanvas::interceptPointerDown(d::Point point, const d::PointerInput& input) {
    if (input.button != d::PointerButton::left || menuOpen() || editingText()) return false;
    for (const auto& [id, tile] : tiles_) {
        if (groupsOverview_) break;
        if (!d::isUnclipped(tile->layer(), point, root())) continue;
        const auto local = d::convert(point, root(), tile->layer());
        if (!local || !tile->contains(*local)) continue;
        if (!editingGroup_) selectedItem_ = id;
        if (input.clickCount == 2 && !editingGroup_ && !groupsOverview_) { if (const auto* entry = item(id)) open(*entry); return true; }
        return false;
    }
    // Buttons, fields and scroll controls retain selection; empty canvas clears it.
    const auto hit = [&](d::Control* control) {
        const auto local = d::convert(point, root(), control->layer());
        return local && d::isUnclipped(control->layer(), point, root()) && control->contains(*local);
    };
    for (auto* control : header().controls()) if (hit(control)) return false;
    for (auto* control : page().controls()) if (hit(control)) return false;
    const bool selected=!selectedItems_.empty();selectedItem_.clear(); selectedItems_.clear(); refreshSelection();
    if(selected)rerender();
    return false;
}
void AppShortcutCanvas::refreshSelection() {
    for (const auto& [id, control] : tiles_)
        static_cast<ShortcutGridTile*>(control)->setSelected(!groupsOverview_ && (editingGroup_ ? std::find(selectedItems_.begin(), selectedItems_.end(), id) != selectedItems_.end() : selectedItem_ == id));
    for (const auto& [id,control]:tabs_) static_cast<ShortcutTab*>(control)->select(groupsOverview_ ? std::find(selectedItems_.begin(),selectedItems_.end(),id)!=selectedItems_.end() : groupID_==id);
    for (auto* control : header().controls()) if ((control->id() == "group:delete-shortcut" || control->id() == "groups:delete")) control->setEnabled(editingGroup_ && !selectedItems_.empty());
}
void AppShortcutCanvas::confirmRemoveSelected() {
    if (!editingGroup_ || selectedItems_.empty()) return;
    if (groupsOverview_) {
        const auto selected = selectedItems_;
        d::ConfirmRequest prompt; prompt.id = "groups:remove-groups";
        const auto count = std::to_string(selected.size());
        const std::array<std::string_view, 1> arguments{count};
        prompt.title = text("Delete {0} groups?", "删除选中的 {0} 个分组？", arguments);
        prompt.message = text("Groups and their shortcut references are removed; original files are kept", "仅删除分组，快捷方式保留在总览中");
        prompt.confirmTitle = text("Delete", "删除"); prompt.danger = true; prompt.icon = d::Icon::trash;
        confirm(std::move(prompt), [this, selected] {
            selectedItems_.clear(); refreshSelection();
            AppShortcutRequest request;request.kind=AppShortcutRequest::Kind::removeGroup;request.ids=selected;
            (void)submit(std::move(request));
        });
        return;
    }
    std::vector<ShortcutRecord> selected;
    for (const auto& id : selectedItems_) if (const auto* record = item(id)) selected.push_back(*record);
    if (selected.empty()) return;
    d::ConfirmRequest prompt; prompt.id = "groups:remove-selected";
    const auto count = std::to_string(selected.size());
    const std::array<std::string_view, 1> arguments{count};
    prompt.title = text("Remove {0} shortcuts?", "移除选中的 {0} 个快捷方式？", arguments);
    prompt.message = groupID_.empty()?text("Remove from overview and all groups; desktop files are kept", "从总览及所有分组移除，原桌面文件保持不变"):text("Remove from this group only; keep in overview", "仅从当前分组移除，总览中仍然保留");
    prompt.confirmTitle = text("Remove", "移除"); prompt.danger = true; prompt.icon = d::Icon::trash;
    confirm(std::move(prompt), [this, selected, group=groupID_] {
        selectedItems_.clear(); selectedItem_.clear(); refreshSelection();
        AppShortcutRequest request;request.kind=AppShortcutRequest::Kind::remove;request.groupID=group;request.id=selected.front().id;
        for(const auto& record:selected)request.ids.push_back(record.id);
        (void)submit(std::move(request));
    });
}

// MARK: Page (:99-145)
void AppShortcutCanvas::buildPage(d::Page& page) { buildGroupPage(page); }
void AppShortcutCanvas::buildGroupsPage(d::Page& page) { buildGroupPage(page); }
void AppShortcutCanvas::buildGroupPage(d::Page& page) {
    cancel(gridTimers_);gridDragID_.clear();
    built_=true;binders_.clear();rows_.clear();tiles_.clear();tabs_.clear();grips_.clear();list_=nullptr;tabContent_=nullptr;reorder_.reset();
    auto& more=page.make<d::Button>("group:options",text("Actions","操作"),d::Icon::grid,d::Button::Kind::primary,d::Button::Height::compact);
    more.setEnabled(store_.has_value());more.onActivate=[this,&more]{optionsMenu(more);};
    std::vector<d::Control*> actions{&more};
    if(editingGroup_){
        auto& done=page.make<d::Button>("group:done",text("Done","完成"),d::Icon::check,d::Button::Kind::primary,d::Button::Height::compact);
        done.onActivate=[this]{endEditing(true);editingGroup_=false;groupsOverview_=false;selectedItems_.clear();rerender();};actions.push_back(&done);
    }
    header().configure({"APPLICATIONS"},"",title("Apps","应用"),d::format::pad2(std::int64_t(items_.size())),false,actions);
    const double top=d::metric::headerHeight;
    const auto groupCount=store_?store_->groups.size():0;
    tabRows_=groupCount>4?2:1;
    tabColumns_=std::max<std::size_t>(1,(groupCount+tabRows_-1)/tabRows_);
    tabWidth_=d::metric::boxWidth/double(std::min<std::size_t>(7,1+tabColumns_));
    const double tabHeight=43+(tabRows_-1)*48;
    auto& bar=page.makeLayer("groups:bar");bar.setFrame({0,top,d::metric::boxWidth,tabHeight});d::setBorder(bar,env().palette.token(d::ThemeToken::tabBar),env().palette.token(d::ThemeToken::ringTab),1);page.layer().add(bar);
    auto& overview=page.make<ShortcutTab>("groups:overview",text("Overview","总览"),groupID_.empty(),tabWidth_);
    overview.layer().setOrigin({0,top+(tabRows_-1)*24});page.layer().add(overview.layer());page.add(overview);tabs_.emplace_back("",&overview);
    overview.onActivate=[this]{showGroup({});};
    auto& viewport=page.makeLayer("groups:viewport");viewport.setFrame({tabWidth_,top-3,d::metric::boxWidth-tabWidth_,tabHeight+6});viewport.setMasksToBounds(true);page.layer().add(viewport);
    tabOffset_=std::clamp(tabOffset_,0.,std::max(0.,(double(tabColumns_)*tabWidth_)-(d::metric::boxWidth-tabWidth_)));
    auto& content=page.makeLayer("groups:tabs");content.setFrame({-tabOffset_,3,double(tabColumns_)*tabWidth_,tabHeight});viewport.add(content);tabContent_=&content;
    if(store_)for(std::size_t index=0;index<store_->groups.size();++index) {
        const auto group=store_->groups[index];
        auto& tab=page.make<ShortcutTab>("groups:tab:"+group.id,group.name,groupsOverview_?std::find(selectedItems_.begin(),selectedItems_.end(),group.id)!=selectedItems_.end():groupID_==group.id,tabWidth_);
        tab.layer().setOrigin({double(index%tabColumns_)*tabWidth_,double(index/tabColumns_)*48});content.add(tab.layer());page.add(tab);tabs_.emplace_back(group.id,&tab);
        tab.onActivate=[this,id=group.id]{
            if(editingGroup_ && groupsOverview_){const auto found=std::find(selectedItems_.begin(),selectedItems_.end(),id);if(found==selectedItems_.end())selectedItems_.push_back(id);else selectedItems_.erase(found);rerender();}
            else {const bool editing=editingGroup_;showGroup(id);editingGroup_=editing;if(editing)rerender();}
        };
        tab.onDrop=[this,id=group.id,index](d::Point delta){
            if(!editingGroup_||cancellingGestures()||!store_)return;
            const auto row=std::clamp<std::ptrdiff_t>(std::ptrdiff_t(index/tabColumns_)+std::ptrdiff_t(std::round(delta.y/48)),0,tabRows_-1);
            const auto column=std::clamp<std::ptrdiff_t>(std::ptrdiff_t(index%tabColumns_)+std::ptrdiff_t(std::round(delta.x/tabWidth_)),0,std::ptrdiff_t(tabColumns_)-1);
            AppShortcutRequest r;r.kind=AppShortcutRequest::Kind::moveGroup;r.groupID=id;
            r.index=std::min(row*std::ptrdiff_t(tabColumns_)+column,std::ptrdiff_t(store_->groups.size())-1);
            (void)submit(std::move(r));
        };
    }
    double gridTop=top+54+(tabRows_-1)*48;
    if(editingGroup_) {
        std::string value,renameID;
        const bool groupMode=groupsOverview_ || selectedItems_.empty();
        if(groupMode){
            renameID=groupsOverview_ && selectedItems_.size()==1?selectedItems_[0]:groupID_;
            if(store_)for(const auto& group:store_->groups)if(group.id==renameID)value=group.name;
        }else if(selectedItems_.size()==1)if(const auto* entry=item(selectedItems_[0])){renameID=entry->id;value=entry->name;}
        d::TextFieldOptions options;options.light=true;options.maximumLength=128;
        auto& name=page.make<d::TextField>("group:rename",groupMode?text("Group name","分组名称"):text("Shortcut name","快捷方式名称"),value,
            groupMode?text("Select a group tab to rename","切换到分组即可重命名"):text("Select one shortcut to rename","选择一个快捷方式以重命名"),d::Size{d::metric::boxWidth,30},options);
        name.setEnabled(!renameID.empty());name.layer().setOrigin({0,gridTop});page.layer().add(name.layer());page.add(name);registerField(name);
        name.onCommit=[this,renameID,groupMode,value](const std::string& name){if(renameID.empty()||name==value)return;AppShortcutRequest r;r.name=name;if(groupMode){r.kind=AppShortcutRequest::Kind::saveGroup;r.groupID=renameID;}else{r.kind=AppShortcutRequest::Kind::save;r.id=renameID;if(const auto* current=item(renameID))r.icon=current->iconPreset;} (void)submit(std::move(r));};
        gridTop+=40;
    }
    if(editingGroup_){
        const auto count=std::to_string(selectedItems_.size());
        const std::array<std::string_view,1> arguments{count};
        auto& status=d::makeText(page.builder(),"group:selection-count",text("Selected: {0}","已选 {0} 项",arguments),d::TextStyle{12,d::FontWeight::medium,d::white(1)},{0,gridTop+8,100,20},d::TextAlignment::left);
        page.layer().add(status);
        const auto addAction=[&](const char* id,std::string title,d::Icon icon,double x,double width,auto callback){
            auto& button=page.make<d::Button>(id,std::move(title),icon,d::Button::Kind::secondary,d::Button::Height::compact);
            button.place({x,gridTop},width);button.setEnabled(!selectedItems_.empty());button.onActivate=callback;page.layer().add(button.layer());page.add(button);
        };
        if(!groupsOverview_)addAction("group:move-selected",text("Move to group","移到分组"),d::Icon::grid,100,140,[this]{for(auto* anchor:this->page().controls())if(anchor->id()=="group:move-selected"){membershipMenu(*anchor,true);break;}});
        addAction("group:delete-selected",groupsOverview_?text("Delete groups","删除分组"):groupID_.empty()?text("Delete shortcuts","删除快捷方式"):text("Remove from group","移出分组"),d::Icon::trash,250,170,[this]{confirmRemoveSelected();});
        addAction("group:clear-selected",text("Clear selection","取消选择"),d::Icon::x,430,160,[this]{selectedItems_.clear();selectedItem_.clear();rerender();});
        gridTop+=42;
    }
    constexpr double gap=appGridGap;constexpr int columns=appGridColumns;
    auto& scroll=page.scrollArea("group:"+groupID_,{0,gridTop,d::metric::boxWidth,d::metric::boxHeight-gridTop});list_=&scroll;
    const double side=(d::metric::boxWidth-d::ScrollArea::stripWidth-(columns-1)*gap)/columns;
    const auto count=(items_.size()+columns-1)/columns;scroll.setContentHeight(count?count*(side+gap)-gap:0);
    for(std::size_t index=0;index<items_.size();++index){
        const auto& record=items_[index];const auto id=record.id;
        auto& tile=page.make<ShortcutGridTile>("group:item:"+id,record.unavailable?text("Unavailable: ","失效：")+record.name:record.name,side,artwork(record.iconPreset,record));
        tile.setDraggable(editingGroup_&&!groupsOverview_);tile.setEnabled(!groupsOverview_);tile.layer().setOrigin({double(index%columns)*(side+gap),double(index/columns)*(side+gap)});
        tile.setSelected(!groupsOverview_ && (editingGroup_?std::find(selectedItems_.begin(),selectedItems_.end(),id)!=selectedItems_.end():selectedItem_==id));
        tile.onActivate=[this,id]{if(editingGroup_){const auto found=std::find(selectedItems_.begin(),selectedItems_.end(),id);if(found==selectedItems_.end())selectedItems_.push_back(id);else selectedItems_.erase(found);rerender();}else{selectedItem_=id;refreshSelection();}};
        tile.onDrag=[this,id,&tile](d::Point local){if(const auto point=d::convert(local,tile.layer(),root()))previewGridDrag(id,*point);};
        tile.onDrop=[this,id](d::Point){const auto target=gridDragTarget_;const bool save=!gridDragID_.empty()&&!cancellingGestures();cancelGridDrag();if(save)moveRow(id,target);};
        binders_.push_back([this,&tile,id]{if(const auto* current=item(id))tile.setArtwork(artwork(current->iconPreset,*current));});
        scroll.content().add(tile.layer());page.add(tile);tiles_.emplace_back(id,&tile);
    }
    if(items_.empty()){auto& empty=d::emptyState::make(page.builder(),"shortcuts.empty",text("No shortcuts — add one or refresh desktop","暂无快捷方式，可添加或刷新桌面"));empty.setOrigin({0,gridTop});page.layer().add(empty);}
    page.restoreOffset(scroll);
}
void AppShortcutCanvas::previewGridDrag(const std::string& id,d::Point point) {
    if(!editingGroup_||groupsOverview_||!list_||tiles_.empty()||cancellingGestures())return;
    const bool beginning=gridDragID_.empty();gridDragID_=id;gridDragPoint_=point;
    const auto found=std::find_if(tiles_.begin(),tiles_.end(),[&](const auto& tile){return tile.first==id;});
    if(found==tiles_.end())return;
    const auto source=std::size_t(found-tiles_.begin());const auto frame=found->second->layer().frame();
    const int column=std::clamp(int(std::round(frame.x/appGridPitch)),0,appGridColumns-1);
    const int row=std::max(0,int(std::round(frame.y/appGridPitch)));
    gridDragTarget_=std::min<std::size_t>(row*appGridColumns+column,tiles_.size()-1);
    // Use fixed grid slots, as the HUD right-hand arrangement does: animated
    // neighbours never become moving hit targets and cannot flip the order back.
    for(std::size_t index=0;index<tiles_.size();++index){
        if(index==source)continue;
        auto slot=index;
        if(source<gridDragTarget_ && index>source && index<=gridDragTarget_)--slot;
        else if(source>gridDragTarget_ && index>=gridDragTarget_ && index<source)++slot;
        auto& layer=tiles_[index].second->layer();
        const double x=double(slot%appGridColumns)*appGridPitch-layer.frame().x;
        const double y=double(slot/appGridColumns)*appGridPitch-layer.frame().y;
        if(layer.value(d::Channel::translateX)!=x)d::motion::animate(layer,d::Channel::translateX,x,d::ListReorder::glide,clock());
        if(layer.value(d::Channel::translateY)!=y)d::motion::animate(layer,d::Channel::translateY,y,d::ListReorder::glide,clock());
    }
    if(beginning)after(.03,gridTimers_,[this]{scrollGridDrag();});
}
void AppShortcutCanvas::scrollGridDrag() {
    if(gridDragID_.empty()||!list_||!isActive())return;
    const auto local=d::convert(gridDragPoint_,root(),list_->layer());
    if(local && local->x>=0 && local->x<=list_->layer().bounds().width){
        constexpr double edge=36;
        const double height=list_->layer().bounds().height;
        const double speed=local->y<edge?-std::clamp((edge-local->y)/edge,0.,1.):local->y>height-edge?std::clamp((local->y-height+edge)/edge,0.,1.):0;
        const double before=list_->offset();list_->setOffset(before+speed*15,false);
        const double change=list_->offset()-before;
        if(change!=0){
            const auto found=std::find_if(tiles_.begin(),tiles_.end(),[&](const auto& tile){return tile.first==gridDragID_;});
            if(found!=tiles_.end()){auto& layer=found->second->layer();layer.setOrigin({layer.frame().x,layer.frame().y+change});}
            list_->noteScroll();previewGridDrag(gridDragID_,gridDragPoint_);
        }
    }
    after(.03,gridTimers_,[this]{scrollGridDrag();});
}
void AppShortcutCanvas::cancelGridDrag() {
    cancel(gridTimers_);
    if(gridDragID_.empty())return;
    gridDragID_.clear();
    for(std::size_t index=0;index<tiles_.size();++index){
        auto& layer=tiles_[index].second->layer();
        layer.setOrigin({double(index%appGridColumns)*appGridPitch,double(index/appGridColumns)*appGridPitch});
        layer.setValue(d::Channel::translateX,0);layer.setValue(d::Channel::translateY,0);layer.setZPosition(0);
    }
}
bool AppShortcutCanvas::handleScroll(d::Point point,d::Point delta) {
    const double top=d::metric::headerHeight;
    if(tabContent_&&store_&&point.y>=top-3&&point.y<=top+44+(tabRows_-1)*48&&point.x>=tabWidth_){
        tabOffset_=std::clamp(tabOffset_+(delta.x?delta.x:delta.y)*2,0.,std::max(0.,double(tabColumns_)*tabWidth_-(d::metric::boxWidth-tabWidth_)));
        tabContent_->setOrigin({-tabOffset_,3});return true;
    }
    if(list_){const auto local=d::convert(point,root(),list_->layer());if(local && local->x>=0 && local->x<list_->layer().bounds().width && local->y>=0 && local->y<list_->layer().bounds().height){
        list_->setOffset(list_->offset()+delta.y*2,true);return true;
    }}
    return false;
}

void AppShortcutCanvas::membershipMenu(const d::Control& anchor,bool move) {
    if(!store_||selectedItems_.empty())return;
    std::vector<d::Menu::Item> entries;
    for(const auto& group:store_->groups){if(group.id==groupID_)continue;d::Menu::Item option;option.id=group.id;option.title=group.name;entries.push_back(option);}
    if(entries.empty()){showError(text("Create another group first","请先新建一个分组"));return;}
    const auto ids=selectedItems_;const auto source=groupID_;
    openMenu("groups:membership",std::move(entries),anchor.rect(root()),&anchor,210,[this,ids,source,move](const auto& picked){AppShortcutRequest r;r.kind=AppShortcutRequest::Kind::membership;r.ids=ids;r.sourceGroupID=source;r.groupID=picked.id;r.moveMembership=move;(void)submit(std::move(r));});
}
void AppShortcutCanvas::optionsMenu(const d::Control& anchor) {
    std::vector<d::Menu::Item> entries;
    const auto add=[&](std::string id,std::string name,bool enabled=true){d::Menu::Item option;option.id=std::move(id);option.title=std::move(name);option.enabled=enabled;entries.push_back(std::move(option));};
    add("add",text("Add app","添加应用"));add("new",text("New group","新建分组"));
    add("edit",editingGroup_?text("Done editing","完成编辑"):text("Edit","编辑"));
    add("delete-group",text("Delete current group","删除当前分组"),!groupID_.empty());
    if(editingGroup_)add("delete",groupsOverview_?text("Delete selected groups","删除选中分组"):text("Remove selected apps","移除选中应用"),!selectedItems_.empty());
    if(editingGroup_)add("groups",groupsOverview_?text("Select shortcuts","选择快捷方式"):text("Select multiple groups","选择多个分组"));
    if(editingGroup_&&!groupsOverview_){add("copy",text("Add selected to group","选中项添加到分组"),!selectedItems_.empty());add("move",text("Move selected to group","选中项移至分组"),!selectedItems_.empty());add("relink",text("Choose target again","重新选择目标"),selectedItems_.size()==1);}
    add("refresh",text("Refresh desktop shortcuts","刷新桌面快捷方式"));add("restore",text("Restore hidden desktop shortcuts","恢复已移除的桌面快捷方式"));
    openMenu("shortcuts:options",std::move(entries),anchor.rect(root()),&anchor,250,[this,&anchor](const auto& picked){
        if(picked.id=="add"){if(groupsOverview_){groupsOverview_=false;selectedItems_.clear();rerender();}chooseApplication();}
        else if(picked.id=="new")createGroup();
        else if(picked.id=="edit"){endEditing(true);editingGroup_=!editingGroup_;groupsOverview_=false;selectedItem_.clear();selectedItems_.clear();rerender();}
        else if(picked.id=="delete-group")removeGroup();
        else if(picked.id=="delete")confirmRemoveSelected();
        else if(picked.id=="groups"){editingGroup_=true;groupsOverview_=!groupsOverview_;selectedItems_.clear();selectedItem_.clear();rerender();}
        else if(picked.id=="copy"||picked.id=="move")membershipMenu(anchor,picked.id=="move");
        else if(picked.id=="relink"){if(selectedItems_.size()==1)chooseApplication(selectedItems_[0]);}
        else {AppShortcutRequest r;r.kind=picked.id=="refresh"?AppShortcutRequest::Kind::scanDesktop:AppShortcutRequest::Kind::restoreDesktop;(void)submit(std::move(r));}
    });
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
    // While a move is unanswered items_ still holds the old order (the Mac's
    // write is synchronous): Move up and Move down wait for the answer.
    const bool settled = !moving();
    std::vector<d::Menu::Item> entries(4);
    entries[0].id = "icon"; entries[0].title = text("Change icon", "更换图标");
    entries[1].id = "up"; entries[1].title = text("Move up", "上移"); entries[1].separatorBefore = true; entries[1].enabled = settled && index > 0;
    entries[2].id = "down"; entries[2].title = text("Move down", "下移"); entries[2].enabled = settled && index + 1 < count;
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
    if (!store_ || moving()) return;
    AppShortcutRequest request; request.kind = AppShortcutRequest::Kind::move; request.id = id;
    request.groupID=groupID_;request.index=index;
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
// store write. Nothing lifts with one row, the chooser, a drop or an
// unanswered move.
void AppShortcutCanvas::dragRow(const std::string& id, d::DragGrip::Phase phase, d::Point point) {
    if (!list_) return;
    auto& list = *list_;
    const auto local = d::convert(point, root(), list.content());
    if (!local) return;
    switch (phase) {
    case d::DragGrip::Phase::began: {
        d::Layer* row = reorderRows()(id);
        if (items_.size() < 2 || chooser_ || dropTarget_ || moving() || !row) return;
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
        AppShortcutRequest request; request.kind = AppShortcutRequest::Kind::move; request.id = id;
        const auto& targetID = items_[state.index()].id;
        const auto target = std::find_if(store_->items.begin(), store_->items.end(), [&](const auto& entry) { return entry.id == targetID; });
        request.index = target - store_->items.begin();
        landing_ = request;
        // An owner that refuses the request at once: the rows return to the saved order.
        if (!submit(std::move(request))) { landing_.reset(); rerender(); }
        break;
    }
    }
}
// A lifted row returns to its slot (the module was left mid-drag).
void AppShortcutCanvas::cancelReorder() {
    cancelGridDrag();
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
    request.message = text("The app itself stays installed", "应用本身不会被卸载");
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
    if (!record.native() || record.unavailable) { chooseApplication(record.id); return; }
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
    if (!editingGroup_ && !key.commandModifiers() && key.isReturn()) {
        if (const auto* selected = item(selectedItem_)) open(*selected);
        return true;
    }
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
