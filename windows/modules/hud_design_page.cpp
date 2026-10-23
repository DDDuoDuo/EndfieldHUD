// Page, canvas and context of the redesign: the port of
// Sources/HUDDesignScreen.swift (HUDDesignContext :25-97, HUDDesignPage
// :102-138, HUDDesignCanvas :144-653) and the canvas half of
// Sources/HUDModuleContent.swift's directional switch. LIB-72..82, LIB-99..101.
//
// Adaptations (recorded in the L3 report):
//  - Q4: a sub-page swap builds the next page when it starts (navigate), not
//    at the 130 ms swap, so its rasterization never lands on the swap frame.
//    buildPage configures the canvas's header, so the next page configures a
//    second (staging) header that replaces the leaving one when the swap
//    lands: the leaving header keeps its content while it fades, exactly as
//    on the Mac. A rerender during a swap rebuilds the pending page (the Mac
//    installs a held page that the swap replaces; same pixels).
//  - Swift keeps replaced objects alive while their callbacks run (ARC). Here
//    replaced pages, closed menus and pop-overs are buried and destroyed by a
//    deadline at the current time, never while one of their controls is on
//    the stack (a tab's onSelect that navigates, a row that re-renders); a
//    replaced page that still holds the editing text field lives until that
//    field stops editing.
//  - "Detached root" (HUDDesignCanvas.navigate's root.superlayer == nil) is
//    "not shown": show() attaches, prepareToLeave() detaches.
//  - Hover and press highlights: the owner calls pointerMoved for every pointer
//    event with the button state (SystemHUDView.updateControlHighlights).
#include "modules/hud_design_page.hpp"
#include <algorithm>
#include <cmath>
#include <utility>

namespace endfield::modules::design {
namespace {
using Offsets = std::vector<std::pair<std::string, double>>;
const double* findOffset(const Offsets& offsets, std::string_view id) noexcept {
    for (const auto& [key, value] : offsets) if (key == id) return &value;
    return nullptr;
}
// Dictionary.merge { _, new in new }.
void mergeOffsets(Offsets& into, const Offsets& from) {
    for (const auto& [key, value] : from) {
        auto it = std::find_if(into.begin(), into.end(), [&](const auto& item) { return item.first == key; });
        if (it == into.end()) into.emplace_back(key, value); else it->second = value;
    }
}
bool inside(Rect r, Point p) noexcept { return p.x >= r.x && p.x < r.x + r.width && p.y >= r.y && p.y < r.y + r.height; }
bool anyEditing(const std::vector<TextField*>& fields, const Layer& subtree) {
    return std::any_of(fields.begin(), fields.end(), [&](TextField* f) { return f->editing() && f->layer().isDescendant(subtree); });
}
bool animatingUnder(const Layer& layer, double now) noexcept {
    if (layer.animatable() && layer.animating(now)) return true;
    for (const Layer* child : layer.children()) if (animatingUnder(*child, now)) return true;
    return false;
}
constexpr Size box{metric::boxWidth, metric::boxHeight};
}

// ---------------------------------------------------------------------------
// HUDDesignContext (:25-97)
// ---------------------------------------------------------------------------
Context::Context(TextMeasurer& measurer, TextCasing uppercase) {
    env_.text.measurer = &measurer;
    env_.text.uppercase = std::move(uppercase);
    env_.text.uiRegion = regionFor(env_.language, appearance_.systemRegion);
    overlay_ = std::make_unique<CenterOverlay>(env_);
}
Context::~Context() = default;

// HUDRuntimeAppearance.applyPalette + refreshPalette (:72-77), L10n.language,
// Reduce Motion and setRecordingMode (:79-83).
bool Context::apply(const Appearance& next, double now) {
    env_.now = now;
    if (next == appearance_) return false;
    const bool paletteChanged = env_.palette.apply(next.accentHex, next.light);
    const bool recordingChanged = next.recordingMode != env_.recordingMode;
    const bool motionChanged = next.reduceMotion != env_.reduceMotion;
    const bool changed = paletteChanged || recordingChanged || next.language != env_.language
        || next.systemRegion != appearance_.systemRegion || next.reduceMotion != env_.reduceMotion;
    appearance_ = next;
    env_.language = next.language;
    env_.text.uiRegion = regionFor(next.language, next.systemRegion);
    env_.reduceMotion = next.reduceMotion;
    env_.recordingMode = next.recordingMode;
    const std::vector<Canvas*> canvases = canvases_;
    if (paletteChanged) {
        overlay_->applyPalette();
        for (Canvas* canvas : canvases) canvas->paletteChanged();
    }
    if (recordingChanged) for (Canvas* canvas : canvases) canvas->recordingModeChanged();
    if (motionChanged) for (Canvas* canvas : canvases) canvas->reduceMotionChanged();
    return changed;
}
// update(unit:renderScale:) (:56-65).
void Context::update(double unit, double windowScale, double now) {
    env_.now = now;
    const bool unitChanged = std::abs(unit - unit_) > 1e-4;
    const double scale = design::boxScale(windowScale, unit);
    const bool scaleChanged = std::abs(scale - env_.text.boxScale) > 1e-3;
    unit_ = unit;
    if (!unitChanged && !scaleChanged) return;
    env_.text.boxScale = scale;
    if (invalidate) invalidate();                        // the overlay re-emits at the new scale
    for (Canvas* canvas : std::vector<Canvas*>(canvases_)) canvas->renderScaleChanged();
}
// confirm (:85-89): every canvas closes its popups first.
void Context::confirm(ConfirmRequest request, std::function<void()> onConfirm, std::function<void()> onCancel) {
    for (Canvas* canvas : std::vector<Canvas*>(canvases_)) canvas->closePopups();
    overlay_->confirm(std::move(request), std::move(onConfirm), std::move(onCancel));
    if (onInputStateChange) onInputStateChange();
}
void Context::toast(ToastRequest request, std::function<void()> completion) { overlay_->showToast(std::move(request), std::move(completion)); }
std::string Context::text(std::string_view english, std::string_view simplifiedChinese, std::span<const std::string_view> arguments) const {
    return core::localized(english, simplifiedChinese, env_.language, arguments);
}
std::optional<double> Context::nextWakeTime() const noexcept { return env_.deadlines.next(); }
bool Context::advance(double now) { env_.now = now; return env_.deadlines.advance(now); }
void Context::registerCanvas(Canvas& canvas) { canvases_.push_back(&canvas); }
void Context::unregisterCanvas(Canvas& canvas) noexcept { std::erase(canvases_, &canvas); }

// ---------------------------------------------------------------------------
// HUDDesignPage (:102-138)
// ---------------------------------------------------------------------------
Page::Page(Context& context, std::string_view idPrefix)
    : arena_(std::string(idPrefix), &context.env().deadlines), builder_{context.env(), arena_, set_} {
    layer_ = &arena_.make("design.page");
    layer_->setFrame(metric::box);
    layer_->setAnimatable();                             // the sub-page leave and the concealed hold
}
Page::~Page() = default;
Tabs& Page::makeTabs(std::string id, std::vector<Tabs::Title> titles, int selectedIndex, double width, bool keycaps, bool compact) {
    return own<Tabs>(std::move(id), std::move(titles), selectedIndex, width, keycaps, compact);
}
void Page::add(Control& control) { controls_.push_back(&control); }
void Page::add(std::span<Control* const> controls) { controls_.insert(controls_.end(), controls.begin(), controls.end()); }
void Page::remove(Control& control) { std::erase(controls_, &control); }
void Page::stagger(Layer& layer) {
    if (!layer.animatable()) layer.setAnimatable();       // an entrance is opacity + translation
    staggered_.push_back(&layer);
}
ScrollArea& Page::scrollArea(std::string id, Rect frame, Layer* parent) {
    auto area = std::make_unique<ScrollArea>(builder_, id, frame);
    (parent ? *parent : *layer_).add(area->layer());
    scrolls_.push_back(std::move(area));
    return *scrolls_.back();
}
void Page::restoreOffset(ScrollArea& area) {
    if (const double* offset = findOffset(restored_, area.id())) area.setOffset(*offset);
}

// ---------------------------------------------------------------------------
// HUDDesignCanvas (:144-653)
// ---------------------------------------------------------------------------
Canvas::Canvas(core::Module module, Context& context)
    : module_(module), context_(&context), prefix_(core::moduleIdentifier(module)),
      arena_(prefix_ + ".canvas", &context.env().deadlines), builder_{context.env(), arena_, set_} {
    root_ = &arena_.make("design.root");
    top_ = &arena_.make("design.top");
    root_->setAnchorPoint({0, 0});
    root_->setFrame(metric::box);                        // the 590 x 510 box, not clipped
    top_->setFrame(metric::box);
    top_->setZPosition(order::menu);
    renderedPalette_ = context.env().palette.generation();
    page_ = std::make_unique<Page>(context, prefix_ + ".page0");
    root_->add(page_->layer()); root_->add(*top_);
    context.registerCanvas(*this);
}
Canvas::~Canvas() {
    context_->unregisterCanvas(*this);
    auto& deadlines = context_->env().deadlines;
    for (Deadlines::Token token : {swapToken_, collectToken_}) if (token) deadlines.cancel(token);
    for (Deadlines::Token token : timerGroups_) deadlines.cancel(token);
    router_.set({});
}
void Canvas::begin(double now) { env().now = now; }
void Canvas::notifyInput() const { if (context_->onInputStateChange) context_->onInputStateChange(); }
void Canvas::recordingModeChanged() { if (renderedLanguage_) rerender(); }
void Canvas::renderScaleChanged() { if (renderedLanguage_) rerender(); }
void Canvas::paletteChanged() {
    closePopups();
    if (active_ && renderedLanguage_) rerender();
}

// The header is made with the first page that asks for it (most canvases of
// an opening are never shown, :152-156); the staging header with the first
// sub-page built at navigate time.
ModuleHeader& Canvas::header() {
    const std::size_t index = staging_ ? 1 - currentHeader_ : currentHeader_;
    auto& slot = headers_[index];
    if (!slot) {
        slot = std::make_unique<ModuleHeader>(env(), prefix_ + ".header" + std::to_string(index));
        slot->layer().setAnimatable();                   // leave and hold with the page
        slot->layer().setHidden(!showsHeader_);
        headerPalette_[index] = env().palette.generation();
        if (!staging_) root_->insertBelow(slot->layer(), *top_);
    }
    return *slot;
}
void Canvas::setShowsHeader(bool shows) {
    showsHeader_ = shows;
    for (auto& header : headers_) if (header) header->layer().setHidden(!shows);
}
std::string Canvas::text(std::string_view english, std::string_view simplifiedChinese, std::span<const std::string_view> arguments) const {
    return core::localized(english, simplifiedChinese, env().language, arguments);
}
ModuleTitle Canvas::title(std::string_view english, std::string_view simplifiedChinese) const {
    return ModuleTitle::make(english, simplifiedChinese, env().language);
}
bool Canvas::stale() const noexcept {
    return !renderedLanguage_ || *renderedLanguage_ != env().language || renderedPalette_ != env().palette.generation();
}

// --- Rendering (:289-401) ----------------------------------------------------
void Canvas::rerender() {
    if (swapDeadline_) {                                 // the swap lands its own, rebuilt page
        if (pending_) bury(std::shared_ptr<Page>(std::move(pending_)));
        pending_ = makePage(pendingOffsets_, true);
        return;
    }
    install(makePage(currentOffsets(), false), false);
}
std::vector<std::pair<std::string, double>> Canvas::currentOffsets() const {
    Offsets offsets;
    for (const auto& area : page_->scrollAreas()) if (!findOffset(offsets, area->id())) offsets.emplace_back(area->id(), area->offset());
    return offsets;
}
// navigate(dx:dy:) (:301-336).
void Canvas::navigate(double dx, double dy) {
    const Point offset = subpageOffset(dx, dy);
    closePopups(); endEditing(true); cancelGestures();
    // Going back (dx < 0) returns each scroll area to where it was left.
    mergeOffsets(retainedOffsets_, currentOffsets());
    pendingOffsets_ = dx < 0 ? retainedOffsets_ : Offsets{};
    auto& deadlines = env().deadlines;
    const auto clock = env().clock();
    if (clock.reduceMotion || !attached_) {
        if (swapDeadline_) {
            deadlines.cancel(swapToken_); swapToken_ = 0; swapDeadline_.reset();
            finishLeave();
            if (pending_) bury(std::shared_ptr<Page>(std::move(pending_)));
        }
        install(makePage(pendingOffsets_, false), false);
        return;
    }
    // A swap in flight (Q / E pressed again) is retargeted and lands on time.
    if (swapDeadline_) deadlines.cancel(swapToken_);
    else {
        leaving_ = &page_->layer();
        motion::leave(*leaving_, offset, clock);
        if (showsHeader_ && currentHeader()) motion::leave(currentHeader()->layer(), offset, clock);
        router_.set({});
        swapDeadline_ = clock.now + duration::leave;
    }
    swapOffset_ = offset;
    if (pending_) bury(std::shared_ptr<Page>(std::move(pending_)));
    pending_ = makePage(pendingOffsets_, true);
    swapToken_ = deadlines.token();
    deadlines.after(*swapDeadline_, swapToken_, [this] { completeSwap(); });
}
// The swap work item (:322-333): one step, leave finished, new page in, entrance.
void Canvas::completeSwap() {
    swapToken_ = 0; swapDeadline_.reset();
    finishLeave();
    install(std::move(pending_), true);
    playEntrance(swapOffset_, env().now);                // self.playEntrance(from:) (:330)
    notifyInput();
}
// settleSwap(concealed:) (:342-353).
void Canvas::settleSwap(bool concealed) {
    if (!swapDeadline_) return;
    env().deadlines.cancel(swapToken_); swapToken_ = 0; swapDeadline_.reset();
    finishLeave();
    install(std::move(pending_), true);
    if (concealed) {
        motion::hold(page_->layer());
        if (showsHeader_ && currentHeader()) motion::hold(currentHeader()->layer());
    }
}
void Canvas::revealSettledPage() {
    if (swapDeadline_) return;
    motion::release(page_->layer(), MotionSlot::hold);
    if (showsHeader_ && currentHeader()) motion::release(currentHeader()->layer(), MotionSlot::hold);
}
void Canvas::finishLeave() {
    if (leaving_) {
        leaving_->removeMotion(MotionSlot::leave);
        leaving_->removeFromParent();
        leaving_ = nullptr;
    }
    if (auto* header = currentHeader()) header->layer().removeMotion(MotionSlot::leave);
}
// makePage(restoring:) (:375-382).
std::unique_ptr<Page> Canvas::makePage(const std::vector<std::pair<std::string, double>>& offsets, bool staged) {
    auto next = std::make_unique<Page>(*context_, prefix_ + ".page" + std::to_string(++pageSerial_));
    next->restored_ = offsets;
    std::erase_if(fields_, [](TextField* field) { return !field->editing(); });
    staging_ = staged;
    try { buildPage(*next); } catch (...) { staging_ = false; throw; }
    staging_ = false;
    for (const auto& area : next->scrolls_) {
        if (const double* offset = findOffset(offsets, area->id())) area->setOffset(*offset);
        for (Layer* row : area->content().children()) row->setPartRoot(true);
    }
    for (Layer* child : next->layer().children()) child->setPartRoot(true);
    return next;
}
// install (:384-401).
void Canvas::install(std::unique_ptr<Page> next, bool swapHeader) {
    const auto generation = env().palette.generation();
    renderedLanguage_ = env().language;
    renderedPalette_ = generation;
    if (swapHeader && headers_[1 - currentHeader_]) {
        if (auto* old = currentHeader()) {
            old->layer().removeMotion(MotionSlot::leave); old->layer().removeMotion(MotionSlot::hold);
            old->layer().removeFromParent();
            for (Control* control : old->controls()) control->clearInteractionState();
        }
        currentHeader_ = 1 - currentHeader_;
        root_->insertBelow(currentHeader()->layer(), *top_);
    }
    if (showsHeader_ && currentHeader() && headerPalette_[currentHeader_] != generation) {
        headerPalette_[currentHeader_] = generation;
        currentHeader()->applyPalette();
    }
    std::unique_ptr<Page> old = std::exchange(page_, std::move(next));
    if (old && &old->layer() != leaving_) old->layer().removeFromParent();
    root_->insert(page_->layer(), 0);
    if (old) {
        for (Control* control : old->controls())
            if (std::find(page_->controls().begin(), page_->controls().end(), control) == page_->controls().end()) control->clearInteractionState();
        if (anyEditing(fields_, old->layer())) retired_.push_back(std::move(old));
        else bury(std::shared_ptr<Page>(std::move(old)));
    }
    ++structure_;
    didInstall(*page_);
    refreshRouter();
    notifyInput();
}
void Canvas::refreshRouter() {
    std::vector<Control*> controls;
    if (showsHeader_ && currentHeader()) controls = currentHeader()->controls();
    controls.insert(controls.end(), page_->controls().begin(), page_->controls().end());
    controls.insert(controls.end(), fields_.begin(), fields_.end());
    if (popover_) {
        controls.insert(controls.end(), popoverControls_.begin(), popoverControls_.end());
        if (Control* close = popover_->closeButton()) controls.push_back(close);
    }
    if (menu_) for (Control* control : menu_->controls()) controls.push_back(control);
    router_.set(std::move(controls));
}
void Canvas::bury(std::shared_ptr<void> object) {
    if (!object) return;
    graveyard_.push_back(std::move(object));
    if (collectToken_) return;
    auto& deadlines = env().deadlines;
    collectToken_ = deadlines.token();
    deadlines.after(env().now, collectToken_, [this] { collectToken_ = 0; auto dead = std::move(graveyard_); graveyard_.clear(); });
}
void Canvas::pruneRetired() {
    for (auto it = retired_.begin(); it != retired_.end();) {
        if (anyEditing(fields_, (*it)->layer())) { ++it; continue; }
        std::erase_if(fields_, [&](TextField* field) { return field->layer().isDescendant((*it)->layer()); });
        bury(std::shared_ptr<Page>(std::move(*it)));
        it = retired_.erase(it);
    }
}
Control* Canvas::findControl(std::string_view id) const {
    for (Control* control : router_.controls()) if (control->id() == id) return control;
    for (Control* control : page_->controls()) if (control->id() == id) return control;
    if (auto* header = currentHeader()) for (Control* control : header->controls()) if (control->id() == id) return control;
    return nullptr;
}

// --- Menus and pop-overs (:414-479) -------------------------------------------
Menu& Canvas::openMenu(std::string id, std::vector<Menu::Item> items, const Control& anchor, std::optional<double> minimumWidth,
    std::function<void(const Menu::Item&)> onPick) {
    return openMenu(std::move(id), std::move(items), anchor.rect(*root_), &anchor, minimumWidth, std::move(onPick));
}
Menu& Canvas::openMenu(std::string id, std::vector<Menu::Item> items, Rect anchorRect, const Control* anchor,
    std::optional<double> minimumWidth, std::function<void(const Menu::Item&)> onPick) {
    closePopups();
    menu_ = std::make_unique<Menu>(env(), std::move(id), std::move(items), minimumWidth.value_or(anchorRect.width), arena_.token());
    menu_->place(anchorRect, box);
    Menu* raw = menu_.get();
    // Picking closes the menu, then calls onPick (the menu is buried, not destroyed, meanwhile).
    menu_->onPick = [this, raw, pick = std::move(onPick)](const Menu::Item& item) {
        if (menu_.get() != raw) return;
        const Menu::Item chosen = item;
        const auto callback = pick;
        closeMenu();
        if (callback) callback(chosen);
    };
    menu_->onClose = [this, raw] { if (menu_.get() == raw) closeMenu(); };
    top_->add(menu_->layer());
    menuAnchor_.reset();
    if (anchor) {
        menuAnchor_ = anchor->id();
        if (auto* dropdown = dynamic_cast<Dropdown*>(findControl(anchor->id()))) dropdown->setOpen(true);
    }
    menu_->playEntrance(env().clock());
    ++structure_;
    refreshRouter();
    notifyInput();
    return *menu_;
}
void Canvas::closeMenu() {
    if (!menu_) return;
    std::shared_ptr<Menu> menu(std::move(menu_));
    if (menuAnchor_) if (auto* dropdown = dynamic_cast<Dropdown*>(findControl(*menuAnchor_))) dropdown->setOpen(false);
    menuAnchor_.reset();
    for (Control* control : menu->controls()) control->clearInteractionState();
    menu->layer().removeFromParent();
    bury(std::move(menu));
    ++structure_;
    refreshRouter();
    notifyInput();
}
void Canvas::openPopover(std::unique_ptr<Popover> popover, const Control* anchor, std::optional<Rect> anchorRect, std::vector<Control*> controls) {
    closePopups();
    popover->place(anchorRect.value_or(anchor ? anchor->rect(*root_) : Rect{}), box);
    Popover* raw = popover.get();
    popover->onClose = [this, raw] { if (popover_.get() == raw) closePopover(); };
    top_->add(popover->layer());
    popover_ = std::move(popover);
    popoverControls_ = std::move(controls);
    popover_->playEntrance(env().clock());
    ++structure_;
    refreshRouter();
    notifyInput();
}
void Canvas::closePopover() {
    if (!popover_) return;
    std::shared_ptr<Popover> popover(std::move(popover_));
    for (Control* control : popoverControls_) control->clearInteractionState();
    if (Control* close = popover->closeButton()) close->clearInteractionState();
    popoverControls_.clear();
    forgetFields(popover->layer());
    popover->layer().removeFromParent();
    bury(std::move(popover));
    ++structure_;
    refreshRouter();
    notifyInput();
}
void Canvas::closePopups() { closeMenu(); closePopover(); }

// --- Text fields (:483-498) ------------------------------------------------------
void Canvas::registerField(TextField& field) {
    if (std::find(fields_.begin(), fields_.end(), &field) == fields_.end()) fields_.push_back(&field);
    field.setEditorHost(context_->editorHost());
    // One editing field per canvas (HUDDesignTextField.beginEditing :152-153).
    field.willBeginEditing = [this](TextField& self) {
        for (TextField* other : std::vector<TextField*>(fields_)) if (other != &self && other->editing()) other->endEditing(true);
    };
    field.editingStateChanged = [this](TextField& self) {
        if (self.editing() && context_->onLockParallax) context_->onLockParallax();
        if (!self.editing()) pruneRetired();
        notifyInput();
    };
}
void Canvas::forgetFields(const Layer& subtree) {
    for (TextField* field : std::vector<TextField*>(fields_)) if (field->layer().isDescendant(subtree) && field->editing()) field->endEditing(true);
    std::erase_if(fields_, [&](TextField* field) { return field->layer().isDescendant(subtree); });
}
void Canvas::endEditing(bool commit) {
    for (TextField* field : std::vector<TextField*>(fields_)) if (field->editing()) field->endEditing(commit);
}
void Canvas::refreshTextFieldProjection() { for (TextField* field : fields_) field->refreshProjection(); }
bool Canvas::editingText() const noexcept { return std::any_of(fields_.begin(), fields_.end(), [](TextField* f) { return f->editing(); }); }
TextField* Canvas::editingField() const noexcept {
    const auto it = std::find_if(fields_.begin(), fields_.end(), [](TextField* f) { return f->editing(); });
    return it == fields_.end() ? nullptr : *it;
}

// --- Shared overlay and helpers (:626-652) -------------------------------------------
void Canvas::confirm(ConfirmRequest request, std::function<void()> onConfirm, std::function<void()> onCancel) {
    endEditing(true); cancelGestures();
    context_->confirm(std::move(request), std::move(onConfirm), std::move(onCancel));
}
void Canvas::toast(ToastRequest request, std::function<void()> completion) { context_->toast(std::move(request), std::move(completion)); }
// The result toast (:637-641): English UIs show the English title alone.
void Canvas::toast(Icon icon, std::string_view english, std::string_view simplifiedChinese, std::optional<std::string> detail) {
    const bool isEnglish = env().language == core::Language::english;
    std::string caption = detail ? std::move(*detail) : (isEnglish ? std::string() : std::string(english));
    context_->toast(ToastRequest{icon, text(english, simplifiedChinese), std::move(caption), std::nullopt});
}
void Canvas::wireBack(Page& page, std::function<void()> action) {
    if (Control* back = header().back()) back->onActivate = action;
    page.back = std::move(action);
}
void Canvas::hostBubble(Slider& slider) { slider.setBubbleHost(top_); }
Deadlines::Token Canvas::timerGroup() {
    const auto token = env().deadlines.token();
    timerGroups_.push_back(token);
    return token;
}
void Canvas::after(double delay, Deadlines::Token group, std::function<void()> work) { env().deadlines.after(now() + delay, group, std::move(work)); }
void Canvas::cancel(Deadlines::Token group) { if (group) env().deadlines.cancel(group); }

// --- Host-facing lifecycle (:241-282) ---------------------------------------------
// makeContent: reveal a settled page, rebuild a stale one.
void Canvas::show(double now) {
    begin(now);
    attached_ = true;
    revealSettledPage();
    if (stale()) rerender();
}
void Canvas::setActive(bool active, double now) {
    begin(now);
    if (active == active_) return;
    active_ = active;
    if (active) {
        revealSettledPage();
        if (stale()) rerender();
        didActivate();
    } else {
        closePopups(); endEditing(true); cancelGestures(); settleSwap(true);
        retainedOffsets_.clear();
        didDeactivate();
    }
    notifyInput();
}
// HUDDirectionalContent.playEntrance(from:) (:249-253).
void Canvas::playEntrance(Point offset, double now) {
    begin(now);
    const auto clock = env().clock();
    if (clock.reduceMotion) return;
    if (showsHeader_) header().playEntrance(clock);
    motion::stagger(page_->staggered(), offset, clock);
}
// prepareToLeave (:255-261).
void Canvas::prepareToLeave(double now) {
    begin(now);
    closePopups();
    endEditing(true);
    cancelGestures();
    settleSwap(true);
    willLeave();
    attached_ = false;
}
void Canvas::cancelGestures() {
    for (Control* control : router_.controls()) {
        if (auto* slider = dynamic_cast<Slider*>(control)) slider->cancelDrag();
        control->clearInteractionState();
    }
    router_.cancel();
}

// --- Pointer input, content-box px (:502-577) -----------------------------------------
bool Canvas::pointerDown(Point point, const PointerInput& input, double now) {
    begin(now);
    if (!active_ || swapDeadline_) return false;
    // The first press outside an open menu or pop-over only closes it; a press
    // on its anchor closes it too (the anchor toggles).
    if (menu_) {
        if (inside(menu_->layer().frame(), point)) router_.pointerDown(point, *root_); else closeMenu();
        return true;
    }
    if (popover_) {
        if (inside(popover_->layer().frame(), point)) router_.pointerDown(point, *root_); else closePopover();
        return true;
    }
    std::vector<TextField*> editing;
    for (TextField* field : fields_) if (field->editing()) editing.push_back(field);
    if (!editing.empty() && std::none_of(editing.begin(), editing.end(), [&](TextField* f) { return inside(f->rect(*root_), point); }))
        for (TextField* field : editing) field->endEditing(true);
    if (router_.pointerDown(point, *root_)) {
        if (router_.tracking() && context_->onLockParallax) context_->onLockParallax();
        notifyInput();
        return true;
    }
    if (handlePointerDown(point, input)) { notifyInput(); return true; }
    return false;
}
void Canvas::pointerDragged(Point point, double now) {
    begin(now);
    if (!active_) return;
    if (router_.tracking()) router_.pointerDragged(point, *root_); else handlePointerDragged(point);
}
void Canvas::pointerUp(Point point, double now) {
    begin(now);
    if (!active_) return;
    if (router_.tracking()) { router_.pointerUp(point, *root_); notifyInput(); }
    else handlePointerUp(point);
}
bool Canvas::rightPointerDown(Point point, double now) {
    begin(now);
    if (!active_ || swapDeadline_) return false;
    if (capturesPointer()) { closePopups(); return true; }
    return handleRightPointerDown(point);
}
void Canvas::pointerMoved(std::optional<Point> point, bool buttonDown, double now) {
    begin(now);
    updateHighlights(*root_, point, buttonDown);
    if (active_) handlePointerMoved(point);
}
// An enabled control is drawn under this point: the host gives the press to the module before the shell.
bool Canvas::containsControl(Point point) const {
    return active_ && !swapDeadline_ && router_.control(point, *root_) != nullptr;
}
// Wheel: an open menu (always consumes), a pop-over, the module's zoom surfaces, then scroll areas.
bool Canvas::scroll(Point point, Point delta, double now) {
    begin(now);
    if (!active_) return false;
    if (menu_) {
        if (inside(menu_->layer().frame(), point)) menu_->scroll(delta.y);
        return true;
    }
    if (popover_) return true;
    if (handleScroll(point, delta)) return true;
    const auto areas = page_->scrollAreas();
    for (auto it = areas.rbegin(); it != areas.rend(); ++it) {
        ScrollArea& area = **it;
        if (!isVisible(area.layer(), *root_)) continue;
        const auto local = convert(point, *root_, area.layer());
        // The topmost visible area under the pointer consumes the wheel even at its limit (LIB-79).
        if (local && inside(localBounds(area.layer()), *local)) { area.scroll(delta.y); return true; }
    }
    return false;
}
bool Canvas::magnify(Point point, double magnification, double now) {
    begin(now);
    if (!active_ || capturesPointer()) return false;
    return handleMagnify(point, magnification);
}

// --- Keyboard (:581-602; the host handles the boot log and the confirm bar first) ---
bool Canvas::keyDown(const KeyInput& input, double now) {
    begin(now);
    if (!active_) return false;
    if (interceptKey(input)) return true;
    if (menu_) return menu_->handleKey(input);
    if (popover_) {
        if (input.virtualKey != key::escape) return false;
        closePopover();
        return true;
    }
    if (editingText()) return false;
    if (handleKey(input)) return true;
    if (input.commandModifiers()) return false;
    if (input.virtualKey == key::escape) {
        if (!page_->back) return false;
        const auto back = page_->back;                   // the page may be replaced by its own action
        back();
        return true;
    }
    if (page_->tabs) {
        const char32_t c = input.character >= U'A' && input.character <= U'Z' ? input.character + 32 : input.character;
        if (c == U'q') { page_->tabs->cycle(-1); return true; }
        if (c == U'e') { page_->tabs->cycle(1); return true; }
    }
    return handleExtraKey(input);
}

// --- Accessibility (:606-622) ----------------------------------------------------------
void Canvas::accessibilityItems(std::vector<AccessibilityItem>& out) const {
    if (!active_ || swapDeadline_) return;
    router_.accessibilityItems(*root_, out);
}
bool Canvas::performAccessibilityAction(std::string_view id, double now) {
    begin(now);
    for (Control* control : router_.controls()) if (control->id() == id) { control->activate(); return true; }
    return false;
}
bool Canvas::adjustAccessibilitySlider(std::string_view id, double value, double now) {
    begin(now);
    for (Control* control : router_.controls()) {
        auto* slider = control->id() == id ? dynamic_cast<Slider*>(control) : nullptr;
        if (!slider) continue;
        const double before = slider->value();
        slider->setValue(value);
        if (slider->value() != before && slider->onChange) slider->onChange(slider->value(), true);
        return true;
    }
    return false;
}
// UIA ValuePattern.SetValue on a design text field (a Windows peer; the Mac's
// stand-ins have no text role): the text is set and committed.
bool Canvas::setAccessibilityText(std::string_view id, std::string_view value, double now) {
    begin(now);
    for (Control* control : router_.controls()) {
        auto* field = control->id() == id ? dynamic_cast<TextField*>(control) : nullptr;
        if (!field || !field->enabled()) continue;
        field->setText(std::string(value));
        if (field->onCommit) field->onCommit(field->text());
        return true;
    }
    return false;
}

// --- Native scene support ------------------------------------------------------------------
std::uint64_t Canvas::contentRevision() const noexcept {
    const auto* header = currentHeader();
    const std::array<std::uint64_t, 6> key{arena_.revision(), page_->layer().arena().revision(),
        header ? header->layer().arena().revision() : 0, menu_ ? menu_->layer().arena().revision() : 0,
        popover_ ? popover_->layer().arena().revision() : 0, structure_};
    if (key != revisionKey_) { revisionKey_ = key; ++revision_; }
    return revision_;
}
bool Canvas::requiresFrames(double now) const { return animatingUnder(*root_, now); }
std::optional<double> Canvas::nextDeadline(double now) const {
    std::optional<double> next = swapDeadline_;
    if (const auto wake = nextWakeTime(now)) next = next ? std::min(*next, *wake) : *wake;
    return next;
}
} // namespace endfield::modules::design
