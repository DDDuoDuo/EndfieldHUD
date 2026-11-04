#include "modules/projection_toolbar.hpp"
#include "modules/notes_format_bar.hpp"
#include "modules/projection_settings.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>

namespace endfield::modules {
namespace d = design;
namespace {
std::string tr(const d::Environment& env, std::string_view english, std::string_view simplified) { return core::localized(english, simplified, env.language); }
std::string hexName(std::uint32_t hex) { char s[8]; std::snprintf(s, sizeof(s), "%06X", unsigned(hex & 0xFFFFFFu)); return s; }
d::Menu::Item item(std::string id, std::string title, bool enabled = true) { d::Menu::Item i; i.id = std::move(id); i.title = std::move(title); i.enabled = enabled; return i; }
bool inside(core::Rect r, core::Point p) noexcept { return p.x >= r.x && p.y >= r.y && p.x < r.x + r.width && p.y < r.y + r.height; }
bool animatingUnder(const d::Layer& layer, double now) {
    if (layer.animating(now)) return true;
    for (const auto* child : layer.children()) if (animatingUnder(*child, now)) return true;
    return false;
}
}

// --- ProjectionToolbar (ProjectionControls.swift) ------------------------------------
double ProjectionToolbar::topInset(double safeAreaTop, double visibleTop) noexcept {
    const double safe = std::isfinite(safeAreaTop) ? std::max(0., safeAreaTop) : 0;
    const double visible = std::isfinite(visibleTop) ? std::max(0., visibleTop) : 0;
    return std::max(64., std::max(safe, visible) + 24);
}
ProjectionToolbar::ProjectionToolbar(d::Builder& b, std::array<std::uint32_t, 6> palette) {
    const auto& env = b.env;
    layer_ = &b.layer("projection.toolbar");
    layer_->setPartRoot(true);
    custom_ = &b.make<d::Swatch>("color:custom", tr(env, "Custom color", "自定义颜色"), std::nullopt, 22.0);
    pen_ = &b.make<d::IconButton>("pen", tr(env, "Pen", "画笔"), d::Icon::pen);
    eraser_ = &b.make<d::IconButton>("eraser", tr(env, "Eraser", "橡皮擦"), d::Icon::eraser);
    brush_ = &b.make<d::Dropdown>("brush", tr(env, "Brush thickness", "画笔粗细"), "80 PX", std::nullopt, d::Dropdown::Height::compact, 84.0);
    background_ = &b.make<d::IconButton>("background", tr(env, "Background", "背景"), d::Icon::grid);
    auto& appearance = b.make<d::IconButton>("appearance", tr(env, "Background appearance", "背景外观"), d::Icon::eye);
    auto& media = b.make<d::IconButton>("media", tr(env, "Image/Video", "图片/视频"), d::Icon::image);
    auto& clear = b.make<d::Button>("clear", tr(env, "Clear all", "清空"), d::Icon::trash, d::Button::Kind::secondary, d::Button::Height::compact);
    auto& close = b.make<d::Button>("close", tr(env, "Return", "返回"), d::Icon::back, d::Button::Kind::primary, d::Button::Height::compact);
    double x = 12;
    const auto place = [&](d::Control& control) {
        control.layer().setOrigin({x, std::round((height - control.layer().bounds().height) / 2)});
        layer_->add(control.layer());
        x += control.layer().bounds().width + 8;
        controls_.push_back(&control);
    };
    int dividers = 0;
    const auto divider = [&] {
        auto& line = b.layer("projection.toolbar.divider." + std::to_string(dividers++));
        line.setFrame({x + 2, (height - 22) / 2, 1, 22});
        d::setBackground(line, env.palette.token(d::ThemeToken::ringIcon));
        layer_->add(line);
        x += 2 + 1 + 2 + 8;
    };
    for (const auto hex : palette) {
        const auto name = hexName(hex);
        auto& swatch = b.make<d::Swatch>("color:" + name, tr(env, "Pen color", "画笔颜色") + " #" + name, d::rgb(hex), 22.0);
        swatches_.emplace_back(hex, &swatch);
        place(swatch);
    }
    place(*custom_);
    divider();
    place(*pen_); place(*eraser_); place(*brush_);
    divider();
    place(*background_); place(appearance); place(media);
    divider();
    clear.place({0, 0}); place(clear);
    close.place({0, 0}); place(close);
    for (auto* control : controls_) control->onActivate = [this, id = control->id()] { if (onAction) onAction(id); };
    // HUDPanelChrome.apply (after the controls: its ring and corner mark lie above them).
    d::applyPanelChrome(b, *layer_, {x - 8 + 12, height});
}
d::Control* ProjectionToolbar::control(std::string_view id) const noexcept {
    for (auto* control : controls_) if (control->id() == id) return control;
    return nullptr;
}
void ProjectionToolbar::update(const ProjectionModel& model) {
    const auto color = ProjectionPenSettings::hex(model.color());
    bool matched = false;
    for (const auto& [hex, swatch] : swatches_) { swatch->setSelected(!model.erasing() && hex == color); matched = matched || hex == color; }
    custom_->setSelected(!model.erasing() && !matched);
    pen_->setOn(!model.erasing());
    eraser_->setOn(model.erasing());
    background_->setOn(model.backgroundEnabled());
    const auto width = std::to_string(std::int64_t(std::round(model.brushWidth()))) + " PX";
    if (brush_->valueText() != width) brush_->setValue(width);
}
void ProjectionToolbar::setOpen(std::optional<std::string_view> id) { brush_->setOpen(id && *id == "brush"); }

// --- ProjectionWorkspaceChrome (ProjectionWorkspaceView.swift) ----------------------------
ProjectionWorkspaceChrome::ProjectionWorkspaceChrome(d::Environment& env, ProjectionModel& model, std::array<std::uint32_t, 6> palette, ProjectionChromeServices services)
    : env_(&env), model_(&model), services_(std::move(services)), arena_("projection", &env.deadlines), builder_{env, arena_, set_} {
    chrome_ = &builder_.layer("chrome");
    chrome_->setBounds({1, 1});
    toolbar_ = std::make_unique<ProjectionToolbar>(builder_, palette);
    toolbar_->onAction = [this](std::string_view id) { perform(id); };
    // The status line (:32): 12 px bold onDarkTag, tracking .04, centred.
    messageLayer_ = &d::makeText(builder_, "message", "", d::TextStyle{12, d::FontWeight::bold, d::fixed::onDarkTag, 1, .04}, {0, 0, 1, 12}, d::TextAlignment::center);
    messageLayer_->setPartRoot(true);
    chrome_->add(toolbar_->layer());
    chrome_->add(*messageLayer_);
    token_ = arena_.token();
    refreshRouter();
}
ProjectionWorkspaceChrome::~ProjectionWorkspaceChrome() { env_->deadlines.cancel(token_); }

void ProjectionWorkspaceChrome::layout(core::Point size, double topInset) {
    size_ = {std::max(1., size.x), std::max(1., size.y)};
    chrome_->setBounds({size_.x, size_.y});
    const auto tool = toolbar_->contentSize();
    toolbar_->layer().setOrigin({std::max(6., std::round((size_.x - tool.width) / 2)), std::min(topInset, std::max(6., size_.y - tool.height - 8))});
    const auto bar = toolbarFrame();
    d::setLine(*messageLayer_, {std::max(10., size_.x / 2 - 280), bar.y + bar.height + 18, std::min(560., size_.x - 20), 12});
    positionPopup();
}
core::Rect ProjectionWorkspaceChrome::toolbarFrame() const noexcept { return toolbar_->layer().frame(); }
core::Rect ProjectionWorkspaceChrome::messageFrame() const noexcept { return d::textOf(*messageLayer_).lineFrame; }
void ProjectionWorkspaceChrome::setActive(bool value) {
    if (active_ == value) return;
    active_ = value;
    if (!value) {
        // setActive(false) (:95-109): the popup closes at once, the router forgets its press.
        closePopup(false);
        router_.cancel();
        tracking_ = false;
    }
}
void ProjectionWorkspaceChrome::refresh() { toolbar_->update(*model_); }
void ProjectionWorkspaceChrome::emit(ProjectionChromeEvent e) { if (services_.event) services_.event(e); }

// perform (:232-277).
void ProjectionWorkspaceChrome::perform(std::string_view action) {
    if (!active_) return;
    if (action.starts_with("color:")) {
        const auto digits = action.substr(6);
        std::uint32_t hex{};
        const auto parsed = std::from_chars(digits.data(), digits.data() + digits.size(), hex, 16);
        if (!digits.empty() && parsed.ec == std::errc{} && parsed.ptr == digits.data() + digits.size()) { setPen(hex); return; }
    }
    // A second press on the control that opened the menu or pop-over closes it.
    if (popup_ && (popup_->id == action || (popup_->id == "color" && action == "color:custom"))) { closePopup(); return; }
    if (action == "close") { if (services_.close) services_.close(); return; }
    if (action == "pen" || action == "eraser") {
        model_->setErasing(action == "eraser" ? !model_->erasing() : false);
        refresh();
        if (services_.penChanged) services_.penChanged();
        emit(ProjectionChromeEvent::brushChanged);
        return;
    }
    if (action == "background") {
        model_->setBackgroundEnabled(!model_->backgroundEnabled());
        refresh();
        if (services_.backgroundChanged) services_.backgroundChanged();
        emit(ProjectionChromeEvent::backgroundChanged);
        return;
    }
    if (action == "color" || action == "color:custom") {
        // Picks apply live without closing; one brushChanged when it closes.
        auto picker = notesColorPicker(*env_, "penColor", tr(*env_, "PEN COLOR", "画笔颜色"), ProjectionPenSettings::hex(model_->color()), chrome_,
            [this](std::uint32_t hex) { setPen(hex, false); });
        showPopover("color", std::move(picker.popover), std::move(picker.controls), "color:custom");
        return;
    }
    if (action == "brush") {
        auto popover = std::make_unique<d::Popover>(*env_, tr(*env_, "BRUSH", "画笔"), std::nullopt, 260, 54 - 8);
        auto& slider = adjustment(popover->builder(), "brush", tr(*env_, "Brush thickness", "画笔粗细"), model_->brushWidth(), 1, 80, d::ValueFormat::pixels,
            [this](double value) {
                model_->setBrushWidth(value);
                pendingMenuEvent_ = ProjectionChromeEvent::brushChanged;
                refresh();
                if (services_.penChanged) services_.penChanged();
            });
        d::Slider* sliders[]{&slider};
        popover = sliderPopover({}, sliders, std::move(popover));
        showPopover("brush", std::move(popover), {&slider}, "brush");
        return;
    }
    if (action == "appearance") {
        auto popover = std::make_unique<d::Popover>(*env_, tr(*env_, "BACKGROUND", "背景"), std::nullopt, 260, 2 * 54 - 8);
        const auto changed = [this] {
            pendingMenuEvent_ = ProjectionChromeEvent::backgroundChanged;
            refresh();
            if (services_.backgroundChanged) services_.backgroundChanged();
        };
        auto& darkness = adjustment(popover->builder(), "darkness", tr(*env_, "Darkness", "背景深度"), model_->darkness() * 100, 0, 100, d::ValueFormat::percent,
            [this, changed](double value) { model_->setDarkness(value / 100); changed(); });
        auto& blur = adjustment(popover->builder(), "blur", tr(*env_, "Blur", "模糊"), model_->blur() * 100, 0, 100, d::ValueFormat::percent,
            [this, changed](double value) { model_->setBlur(value / 100); changed(); });
        d::Slider* sliders[]{&darkness, &blur};
        popover = sliderPopover({}, sliders, std::move(popover));
        showPopover("appearance", std::move(popover), {&darkness, &blur}, "appearance");
        return;
    }
    if (action == "media") {
        // "Choose in Finder" is the File Explorer chooser on Windows.
        showMenu("media", {item("finder", std::string(core::translated(core::chooseInFileExplorer, env_->language))),
            item("shelf", tr(*env_, "Choose from Shelf", "从暂存架选择"))}, "media", [this](const d::Menu::Item& picked) {
            const bool shelf = picked.id == "shelf";
            closePopup();
            if (shelf) chooseShelf(); else if (services_.chooseFinder) services_.chooseFinder();
        });
        return;
    }
    if (action == "clear") { showClearConfirmation(); return; }
    if (action == "clear:confirm") clearContent();
}

void ProjectionWorkspaceChrome::setPen(std::uint32_t hex, bool closing) {
    if (closing && popup_ && popup_->id == "color") closePopup();
    model_->setColor(ProjectionPenSettings::color(hex));
    model_->setErasing(false);
    refresh();
    if (services_.penChanged) services_.penChanged();
    if (popup_ && popup_->id == "color") pendingMenuEvent_ = ProjectionChromeEvent::brushChanged;
    else emit(ProjectionChromeEvent::brushChanged);
}

// One labelled slider row of a workspace pop-over (:287-295).
d::Slider& ProjectionWorkspaceChrome::adjustment(d::Builder& b, std::string id, std::string title, double value, double minimum, double maximum,
    d::ValueFormat format, std::function<void(double)> change) {
    auto& slider = b.make<d::Slider>(std::move(id), std::move(title), std::min(maximum, std::max(minimum, value)),
        d::Slider::Range{minimum, maximum, 1, std::nullopt}, format, 236.0, true, true, false);
    slider.setBubbleHost(chrome_);
    slider.onChange = [change = std::move(change)](double v, bool) { change(v); };
    return slider;
}
// A pop-over of labelled sliders, 54 px a row (:298-310).
std::unique_ptr<d::Popover> ProjectionWorkspaceChrome::sliderPopover(std::string, std::span<d::Slider* const> sliders, std::unique_ptr<d::Popover> popover) {
    auto& b = popover->builder();
    for (std::size_t index = 0; index < sliders.size(); ++index) {
        auto& label = d::makeText(b, "label." + sliders[index]->id(), sliders[index]->label, d::TextStyle{12, d::FontWeight::regular, env_->palette.tone(0xDDDDDD)},
            {0, double(index) * 54, 236, 12});
        sliders[index]->layer().setOrigin({0, double(index) * 54 + 16});
        popover->content().add(label);
        popover->content().add(sliders[index]->layer());
    }
    return popover;
}
// showClearConfirmation (:312-327).
void ProjectionWorkspaceChrome::showClearConfirmation() {
    auto popover = std::make_unique<d::Popover>(*env_, tr(*env_, "CLEAR ALL", "清空"), std::nullopt, 280, 18 + 12 + 26);
    auto& b = popover->builder();
    auto& text = d::makeText(b, "clear.text", tr(*env_, "Clear drawings and media?", "清空绘画和媒体？"),
        d::TextStyle{12.5, d::FontWeight::regular, env_->palette.token(d::ThemeToken::menuText)}, {0, 2, 256, 12.5});
    auto& cancel = b.make<d::Button>("clear:cancel", tr(*env_, "Cancel", "取消"), std::nullopt, d::Button::Kind::secondary, d::Button::Height::compact);
    auto& confirm = b.make<d::Button>("clear:confirm", tr(*env_, "Clear all", "清空"), d::Icon::trash, d::Button::Kind::primary, d::Button::Height::compact);
    confirm.place({256 - confirm.preferredWidth(), 30});
    cancel.place({confirm.layer().frame().x - 8 - cancel.preferredWidth(), 30});
    cancel.onActivate = [this] { closePopup(); };
    confirm.onActivate = [this] { clearContent(); };
    popover->content().add(text); popover->content().add(cancel.layer()); popover->content().add(confirm.layer());
    showPopover("clear", std::move(popover), {&cancel, &confirm}, "clear");
}
void ProjectionWorkspaceChrome::clearContent() {
    showMessage("");
    const bool changed = services_.clearContent && services_.clearContent();
    closePopup();
    if (changed) emit(ProjectionChromeEvent::cleared);
}
// chooseShelf (:425-437): one HUDMenu row per shelf entry, enabled when
// supported and available; no entries at all shows the message instead.
void ProjectionWorkspaceChrome::chooseShelf() {
    const auto choices = services_.shelfChoices ? services_.shelfChoices() : std::vector<NotesShelfChoice>{};
    if (choices.empty()) { showMessage(tr(*env_, "The shelf has no images or videos.", "暂存架中没有图片或视频。")); return; }
    std::vector<d::Menu::Item> items;
    for (const auto& choice : choices) items.push_back(item(choice.id, choice.title, choice.supported && choice.available));
    showMenu("shelf", std::move(items), "media", [this](const d::Menu::Item& picked) {
        const auto id = picked.id;
        closePopup();
        if (services_.chooseShelf) services_.chooseShelf(id);
    });
}

// --- Menus and pop-overs (:343-401) -----------------------------------------------------
void ProjectionWorkspaceChrome::showPopover(std::string id, std::unique_ptr<d::Popover> popover, std::vector<d::Control*> controls, std::string anchor) {
    popover->onClose = [this] { closePopup(); };
    if (auto* close = popover->closeButton()) controls.push_back(close);
    auto* raw = popover.get();
    open(Popup{std::move(id), std::move(anchor), nullptr, std::move(popover), std::move(controls)});
    positionPopup();
    if (!env_->reduceMotion) raw->playEntrance(env_->clock());
}
void ProjectionWorkspaceChrome::showMenu(std::string id, std::vector<d::Menu::Item> items, std::string anchor, std::function<void(const d::Menu::Item&)> pick) {
    auto menu = std::make_unique<d::Menu>(*env_, std::move(id), std::move(items), 200, token_);
    auto* raw = menu.get();
    menu->onPick = [this, raw, pick = std::move(pick)](const d::Menu::Item& picked) {
        if (!popup_ || popup_->menu.get() != raw) return;
        const auto chosen = picked;
        pick(chosen);
    };
    menu->onClose = [this, raw] { if (popup_ && popup_->menu.get() == raw) closePopup(); };
    auto controls = menu->controls();
    const auto name = menu->id();
    open(Popup{name, std::move(anchor), std::move(menu), nullptr, std::move(controls)});
    positionPopup();
    if (!env_->reduceMotion) raw->playEntrance(env_->clock());
}
void ProjectionWorkspaceChrome::open(Popup popup) {
    if (popup_) closePopup(false);
    popup_ = std::move(popup);
    popup_->layer().setZPosition(1);
    chrome_->add(popup_->layer());
    toolbar_->setOpen(popup_->id);
    ++structure_;
    refreshRouter();
    pointerMoved(pointer_, false);
}
// Below its toolbar control, kept on screen (:369-376): pop-overs right-
// aligned to the anchor, menus left-aligned, 8 px under the toolbar.
void ProjectionWorkspaceChrome::positionPopup() {
    if (!popup_) return;
    const auto bar = toolbarFrame();
    const auto* anchor = toolbar_->control(popup_->anchor);
    const auto rect = anchor ? anchor->rect(*chrome_) : bar;
    auto& layer = popup_->layer();
    const auto size = layer.bounds();
    const double x = popup_->menu ? std::min(rect.x, size_.x - size.width - 8) : std::min(std::max(8., rect.x + rect.width - size.width), size_.x - size.width - 8);
    layer.setOrigin({x, bar.y + bar.height + 8});
}
// closePopup (:378-392): the deferred event, drags cancelled, then a .14 s
// fade before the layer goes (at once without motion or when inactive).
void ProjectionWorkspaceChrome::closePopup(bool animated) {
    if (!popup_) return;
    auto previous = std::make_shared<Popup>(std::move(*popup_));
    popup_.reset();
    toolbar_->setOpen(std::nullopt);
    if (const auto event = std::exchange(pendingMenuEvent_, std::nullopt)) emit(*event);
    for (auto* control : previous->controls) {
        if (auto* slider = dynamic_cast<d::Slider*>(control)) slider->cancelDrag();
        control->clearInteractionState();
    }
    const auto c = env_->clock();
    const bool fade = animated && active_ && !env_->reduceMotion;
    if (fade) d::motion::animate(previous->layer(), d::Channel::opacity, 0, .14, c, d::easing::ease);
    else previous->layer().removeFromParent();
    // Destroyed by a deadline, never while one of its callbacks runs.
    closing_.push_back(previous);
    std::weak_ptr<Popup> weak = previous;
    env_->deadlines.after(c.now + (fade ? .14 : 0), token_, [this, weak] {
        const auto done = weak.lock();
        if (!done) return;
        done->layer().removeFromParent();
        std::erase(closing_, done);
        ++structure_;
    });
    ++structure_;
    refreshRouter();
    pointerMoved(pointer_, false);
}
void ProjectionWorkspaceChrome::refreshRouter() {
    auto controls = toolbar_->controls();
    if (popup_) controls.insert(controls.end(), popup_->controls.begin(), popup_->controls.end());
    router_.set(std::move(controls));
}
std::optional<std::string> ProjectionWorkspaceChrome::popupID() const { return popup_ ? std::optional(popup_->id) : std::nullopt; }
void ProjectionWorkspaceChrome::showMessage(std::string text) {
    if (text == message_) return;
    message_ = std::move(text);
    d::editText(*messageLayer_, [&](d::TextBlock& block) { block.setText(message_); });
}

// --- Input (:479-572) ------------------------------------------------------------------
bool ProjectionWorkspaceChrome::contains(core::Point p) const { return inside(toolbarFrame(), p) || (popup_ && inside(popup_->layer().frame(), p)); }
void ProjectionWorkspaceChrome::pointerDown(core::Point p) {
    tracking_ = true;
    pointer_ = p;
    (void)router_.pointerDown(p, *chrome_);
    d::updateHighlights(*chrome_, p, true);
}
void ProjectionWorkspaceChrome::pointerDragged(core::Point p) { pointer_ = p; if (tracking_) router_.pointerDragged(p, *chrome_); }
void ProjectionWorkspaceChrome::pointerUp(core::Point p) {
    if (!tracking_) return;
    tracking_ = false;
    pointer_ = p;
    d::updateHighlights(*chrome_, p, false);
    router_.pointerUp(p, *chrome_);
}
void ProjectionWorkspaceChrome::pointerMoved(std::optional<core::Point> p, bool pressed) { pointer_ = p; d::updateHighlights(*chrome_, p, pressed); }
void ProjectionWorkspaceChrome::cancelInteraction() {
    router_.cancel();
    tracking_ = false;
    d::updateHighlights(*chrome_, std::nullopt, false);
}
bool ProjectionWorkspaceChrome::scroll(core::Point p, double delta) {
    if (popup_ && popup_->menu && inside(popup_->layer().frame(), p)) { popup_->menu->scroll(delta); return true; }
    return contains(p);
}
bool ProjectionWorkspaceChrome::key(const d::KeyInput& k) {
    if (popup_ && popup_->menu && popup_->menu->handleKey(k)) return true;
    if (k.virtualKey != d::key::escape) return false;
    if (popup_) closePopup(); else if (services_.close) services_.close();
    return true;
}

// --- Accessibility and content ------------------------------------------------------------
void ProjectionWorkspaceChrome::accessibilityItems(std::vector<d::AccessibilityItem>& out) const { if (active_) router_.accessibilityItems(*chrome_, out); }
bool ProjectionWorkspaceChrome::performControl(std::string_view id) {
    if (!active_) return false;
    for (auto* control : router_.controls()) if (control->id() == id) { control->activate(); return true; }
    return false;
}
bool ProjectionWorkspaceChrome::adjust(std::string_view id, double value) {
    if (!active_) return false;
    for (auto* control : router_.controls())
        if (auto* slider = control->id() == id ? dynamic_cast<d::Slider*>(control) : nullptr) {
            slider->setValue(value);
            if (slider->onChange) slider->onChange(slider->value(), true);
            return true;
        }
    return false;
}
std::uint64_t ProjectionWorkspaceChrome::contentRevision() const noexcept {
    const std::array<std::uint64_t, 4> key{arena_.revision(), popup_ ? popup_->layer().arena().revision() : 0, closing_.size(), structure_};
    if (key != revisionKey_) { revisionKey_ = key; ++revision_; }
    return revision_;
}
bool ProjectionWorkspaceChrome::requiresFrames(double now) const { return animatingUnder(*chrome_, now); }
std::optional<double> ProjectionWorkspaceChrome::nextDeadline() const noexcept { return env_->deadlines.next(); }
bool ProjectionWorkspaceChrome::advance(double now) { return env_->deadlines.advance(now); }
} // namespace endfield::modules
