// The shared centre overlay: the port of Sources/HUDDesignOverlay.swift
// (HUDConfirmState :28-50, HUDConfirmBar :56-201, HUDToastState :213-223,
// HUDToast :227-311, HUDCenterOverlay :316-525). LIB-59..67.
//
// Timers and completion blocks are Context deadlines on the caller's clock:
// the 120 ms confirm callback, the 1.5 s toast, the .18 s collapse and the
// .24 s toast fade. A bar or toast shown again while its previous hide is
// still running cancels that hide's "hidden" step and runs its completion at
// once (Core Animation calls a transaction's completion when its animations
// are removed; the Mac's stale block would also set isHidden on the newly
// shown bar or toast, which this port does not copy - recorded open question).
// The content dim is core::ContentDimTrack (dimFilter); the host applies it
// with Renderer::setContentFilter. The spec's pixel-step blocks on the bar's
// top edge are deliberately not drawn (owner request, round 4).
#include "modules/hud_design_overlay.hpp"
#include <algorithm>
#include <utility>

namespace endfield::modules::design {
namespace {
const std::array<double, 4> bandStops{0, .06, .94, 1}, hairlineStops{0, .2, .8, 1}, toastLineStops{0, .3, .7, 1};
const std::array<double, 6> markerStops{0, .6, .6, .75, .75, 1};
// CAGradientLayer startPoint (0, .5) -> endPoint (1, .5), drawn into the layer's bounds.
void horizontal(Layer& layer, std::span<const Color> colors, std::span<const double> stops) {
    layer.setContent(gradient(layer.id(), localBounds(layer), colors, stops, {0, .5}, {1, .5}));
}
std::string decimal(int value) { return std::to_string(value); }
// Deadlines::cancel(0) would drop every token-less item: cancel only real tokens.
void cancel(Deadlines& deadlines, Deadlines::Token& token) noexcept { if (token) deadlines.cancel(token); token = 0; }

// HUDCenterOverlay.OutsidePress (:348-352): presses outside the bar cancel a
// request whose pills both confirm. Its layer is the overlay's own; no role.
class OutsidePress final : public Control {
public:
    OutsidePress(Environment& env, Layer& overlay, const Layer& bar) : Control(env, "confirm.outside", "", overlay), bar_(&bar) {}
    bool contains(Point p) const override {
        const Rect f = bar_->frame();
        return !(p.x >= f.x && p.x < f.x + f.width && p.y >= f.y && p.y < f.y + f.height);
    }
    std::optional<AccessibilityRole> accessibilityRole() const override { return std::nullopt; }
private:
    const Layer* bar_;
};
}

// ---------------------------------------------------------------------------
// HUDConfirmState (:28-50)
// ---------------------------------------------------------------------------
void ConfirmState::present(ConfirmRequest request) { request_ = std::move(request); decision_.reset(); }
std::optional<ConfirmState::Decision> ConfirmState::key(const KeyInput& k) {
    if (k.virtualKey == key::escape) return decide(Decision::cancel);
    if (k.isReturn()) return decide(Decision::confirm);       // Return and keypad Enter (VK_RETURN, extended)
    return std::nullopt;
}
std::optional<ConfirmState::Decision> ConfirmState::decide(Decision value) {
    if (!isOpen()) return std::nullopt;
    decision_ = value;
    return value;
}
void ConfirmState::setCountdown(int value) { if (request_ && request_->countdown) request_->countdown = value; }
void ConfirmState::close() noexcept { request_.reset(); decision_.reset(); }

// ---------------------------------------------------------------------------
// HUDConfirmBar (:56-201)
// ---------------------------------------------------------------------------
struct ConfirmBar::PillSet {
    LayerArena arena; ControlSet controls; Builder builder;
    PillSet(Environment& env, std::uint64_t serial)
        : arena("confirm.pills." + std::to_string(serial), &env.deadlines), builder{env, arena, controls} {}
};
ConfirmBar::ConfirmBar(Environment& env, Deadlines::Token)
    : env_(&env), arena_("confirm", &env.deadlines), builder_{env, arena_, controls_} {
    const Rect bounds{0, 0, size.width, size.height};
    layer_ = &builder_.layer("confirm");
    band_ = &builder_.layer("band");
    column_ = &builder_.layer("text");
    marker_ = &builder_.layer("marker");
    title_ = &makeText(builder_, "title", "", type::confirmTitle(env.palette), {});
    message_ = &makeText(builder_, "message", "", type::confirmDescription(env.palette), {});
    layer_->setBounds(size); layer_->setZPosition(order::confirmBar);
    layer_->setAnimatable();                             // bandOut: scaleY and opacity
    band_->setFrame(bounds);
    band_->setAnimatable();                              // bandIn: the wipe from the centre
    for (std::size_t i = 0; i < hairlines_.size(); ++i) {
        Layer& hairline = builder_.layer("hairline." + std::to_string(i));
        hairline.setFrame({size.width * .05, i == 0 ? 0 : size.height - 1, size.width * .9, 1});
        band_->add(hairline); hairlines_[i] = &hairline;
    }
    marker_->setFrame({0, 1.5, 12, 18});
    column_->setFrame({46, 18, 320, 80});
    column_->setAnimatable();                            // the text column enters from the left
    for (Layer* child : {marker_, title_, message_}) column_->add(*child);
    layer_->add(*band_); layer_->add(*column_);
    layer_->setHidden(true);
    applyPalette();
}
ConfirmBar::~ConfirmBar() { cancel(env_->deadlines, hideToken_); }

// applyPalette (:99-108): the standard band in the current Theme; configure
// takes it for every request that is not an alert.
void ConfirmBar::applyPalette() {
    const auto& p = env_->palette;
    const Color charcoal = p.tone(0x181818, .97), edge = p.tone(0x181818, 0);
    const std::array<Color, 4> band{edge, charcoal, charcoal, edge};
    horizontal(*band_, band, bandStops);
    const std::array<Color, 4> hairline{p.foreground(0), p.foreground(.22), p.foreground(.22), p.foreground(0)};
    for (Layer* line : hairlines_) horizontal(*line, hairline, hairlineStops);
}

// configure (:110-162).
void ConfirmBar::configure(const ConfirmRequest& request) {
    Environment& env = *env_;
    const auto& p = env.palette;
    for (Control* pill : pills_) pill->layer().removeFromParent();
    retiredPills_ = std::move(pillSet_);
    pillSet_ = std::make_unique<PillSet>(env, ++pillSerial_);
    const Pill::Kind kind = request.danger ? Pill::Kind::danger : Pill::Kind::confirm;
    const bool twin = request.cancelPillConfirms;
    const std::string cancelTitle = request.cancelTitle.empty() ? core::localized("Cancel", "取消", env.language) : request.cancelTitle;
    auto& cancel = pillSet_->builder.make<Pill>(request.id + ":cancel", cancelTitle, twin ? request.glyph() : Icon::x,
        twin ? kind : Pill::Kind::cancel);
    auto& confirm = pillSet_->builder.make<Pill>(request.id + ":confirm", request.confirmTitle, request.glyph(), kind);
    cancel.onActivate = [this, twin] { if (onDecision) onDecision(twin ? ConfirmState::Decision::confirm : ConfirmState::Decision::cancel); };
    confirm.onActivate = [this] { if (onDecision) onDecision(ConfirmState::Decision::confirm); };
    pills_ = {&cancel, &confirm};
    const bool alert = request.style == ConfirmRequest::Style::alert;
    const Color tone = alert ? white(1) : request.danger ? fixed::red : p.accentInk();
    if (alert) {
        const std::array<Color, 4> band{withAlpha(fixed::red, 0), withAlpha(fixed::red, .97), withAlpha(fixed::red, .97), withAlpha(fixed::red, 0)};
        horizontal(*band_, band, bandStops);
    } else {
        const Color charcoal = p.tone(0x181818, .97), edge = p.tone(0x181818, 0);
        const std::array<Color, 4> band{edge, charcoal, charcoal, edge};
        horizontal(*band_, band, bandStops);             // the standard band again after an alert
    }
    const std::array<Color, 6> marker{tone, tone, clear, clear, tone, tone};
    horizontal(*marker_, marker, markerStops);
    const double columnWidth = size.width - 46 - 34 - 16 - std::max(cancel.layer().bounds().width, confirm.layer().bounds().width);
    column_->setFrame({46, 18, columnWidth, 80});
    // Without a description the title may wrap, up to three lines; one too
    // long for three (wide twin pills) steps down to 14 px and four.
    editText(*title_, [&](TextBlock& block) {
        block.runs = {{request.title, alert ? type::confirmTitle(p).colored(white(1)) : type::confirmTitle(p)}};
        int lines = request.message.empty() && !request.countdown ? 3 : 1;
        if (lines == 3 && preferredWidth(block, env.text) > (columnWidth - 22) * 2.7) {
            block.setStyle(block.style().sized(14));
            lines = 4;
        }
        block.maximumLines = lines;
        block.setLineFrame({22, 0, columnWidth - 22, block.style().size * 1.25 * lines});
    });
    editText(*message_, [&](TextBlock& block) {
        if (request.countdown) {
            block.runs = {{decimal(*request.countdown), type::confirmCountdown(p)}};
            block.maximumLines = 1;
            block.setLineFrame({0, 21.25 + 8, columnWidth, 22});
        } else {
            block.runs = {{request.message, alert ? type::confirmDescription(p).colored(white(.9)) : type::confirmDescription(p)}};
            block.maximumLines = 2;
            block.setLineFrame({0, 21.25 + 8, columnWidth, 36});
        }
    });
    double y = 18;
    for (Control* pill : pills_) {
        Layer& l = pill->layer();
        const Size s = l.bounds();
        l.setFrame({size.width - 34 - s.width, y, s.width, s.height});
        l.setAnimatable();                               // enters from the right
        layer_->add(l);
        y += s.height + 11;
    }
}
void ConfirmBar::setCountdown(int value) {
    editText(*message_, [&](TextBlock& block) { block.runs = {{decimal(value), type::confirmCountdown(env_->palette)}}; });
}
// show (:169-178): bandIn (.3 SNAP from the centre), then the text from the left and the pills from the right.
void ConfirmBar::show(const motion::Clock& clock) {
    cancel(env_->deadlines, hideToken_);
    if (auto completion = std::exchange(pendingCompletion_, {})) completion();
    layer_->setHidden(false);
    layer_->setValue(Channel::opacity, 1); layer_->setValue(Channel::scaleY, 1);   // removes hud.bandOut
    motion::wipe(*band_, WipeOrigin::centerX, duration::bandIn, clock, easing::snap);
    motion::enter(*column_, {-12, 0}, clock, duration::confirmText, duration::confirmTextDelay);
    for (std::size_t i = 0; i < pills_.size(); ++i)
        motion::enter(pills_[i]->layer(), {12, 0}, clock, duration::confirmText, duration::pillDelay + double(i) * duration::pillStep);
}
// hide (:181-197): bandOut collapses to a line and fades (.18 easeIn), then hides.
void ConfirmBar::hide(bool animated, const motion::Clock& clock, std::function<void()> completion) {
    cancel(env_->deadlines, hideToken_);
    if (auto previous = std::exchange(pendingCompletion_, {})) previous();
    auto hidden = [this] {
        layer_->setHidden(true);
        layer_->setValue(Channel::opacity, 1); layer_->setValue(Channel::scaleY, 1);  // the model never left 1
        for (Control* pill : pills_) pill->clearInteractionState();
    };
    if (!animated || clock.reduceMotion || layer_->hidden()) { hidden(); if (completion) completion(); return; }
    motion::animate(*layer_, Channel::scaleY, .04, duration::bandOut, clock, easing::easeIn);
    motion::animate(*layer_, Channel::opacity, 0, duration::bandOut, clock, easing::easeIn);
    pendingCompletion_ = std::move(completion);
    hideToken_ = env_->deadlines.token();
    env_->deadlines.after(clock.now + duration::bandOut, hideToken_, [this, hidden] {
        hideToken_ = 0;
        hidden();
        if (auto done = std::exchange(pendingCompletion_, {})) done();
    });
}
std::vector<Control*> ConfirmBar::pills() const { return pills_; }
std::string ConfirmBar::titleText() const { return textOf(*title_).text(); }
std::string ConfirmBar::messageText() const { return textOf(*message_).text(); }
double ConfirmBar::titleSize() const { return textOf(*title_).style().size; }
int ConfirmBar::titleLines() const { return textOf(*title_).maximumLines; }

// ---------------------------------------------------------------------------
// HUDToastState (:213-223)
// ---------------------------------------------------------------------------
std::uint64_t ToastState::show(ToastRequest toast) { current_ = std::move(toast); return ++generation_; }
bool ToastState::expire(std::uint64_t token) {
    if (token != generation_ || !current_) return false;
    current_.reset();
    return true;
}
void ToastState::clear() noexcept { current_.reset(); ++generation_; }

// ---------------------------------------------------------------------------
// HUDToast (:227-311)
// ---------------------------------------------------------------------------
Toast::Toast(Environment& env, Deadlines::Token)
    : env_(&env), arena_("toast", &env.deadlines), builder_{env, arena_, controls_} {
    const double width = size.width;
    layer_ = &builder_.layer("toast");
    line_ = &builder_.layer("line");
    box_ = &builder_.layer("box");
    corner_ = &builder_.layer("box.corner");
    glyph_ = &makeIcon(builder_, "box.glyph", Icon::check, 26, env.palette.foreground(1));
    title_ = &makeText(builder_, "title", "", type::toastTitle(env.palette), {0, 88, width, 14}, TextAlignment::center);
    caption_ = &makeText(builder_, "caption", "", type::toastCaption(env.palette), {0, 110, width, 11.5}, TextAlignment::center);
    underline_ = &builder_.layer("underline");
    layer_->setBounds(size); layer_->setZPosition(order::toast); layer_->setHidden(true);
    layer_->setAnimatable();                             // the fade out rises 6 px
    line_->setFrame({0, 44, width, 1});
    line_->setAnimatable();                              // grows from its centre
    for (std::size_t i = 0; i < ticks_.size(); ++i) {
        ticks_[i] = &builder_.layer("line.tick." + std::to_string(i));
        ticks_[i]->setFrame({i == 0 ? width * .2 : width * .8, -3, 1, 7});
        line_->add(*ticks_[i]);
    }
    box_->setFrame({width / 2 - 30, 14, 60, 60});
    box_->setAnimatable();                               // pop
    corner_->setFrame({48, 0, 12, 12});
    box_->add(*glyph_); box_->add(*corner_);
    title_->setAnimatable(); caption_->setAnimatable();  // rise 8 px
    underline_->setFrame({width / 2 - 11, 130, 22, 2});
    underline_->setAnimatable();                         // grows from its centre
    for (Layer* child : {line_, box_, title_, caption_, underline_}) layer_->add(*child);
    applyPalette();
}
Toast::~Toast() { cancel(env_->deadlines, hideToken_); }

// applyPalette (:267-276).
void Toast::applyPalette() {
    const auto& p = env_->palette;
    const std::array<Color, 4> line{p.foreground(0), p.foreground(.65), p.foreground(.65), p.foreground(0)};
    horizontal(*line_, line, toastLineStops);
    for (Layer* tick : ticks_) setBackground(*tick, p.foreground(1));
    setBorder(*box_, p.tone(0x161616), p.tone(0x4A4A4A));
    setPath(*corner_, PathBuilder{}.move({0, 0}).line({12, 0}).line({12, 12}).close(), p.accentInk());
    setBackground(*underline_, p.accentInk());
    setIcon(*glyph_, icon_, 26, p.foreground(1));
    editText(*title_, [&](TextBlock& block) { block.setStyle(type::toastTitle(p)); });
    editText(*caption_, [&](TextBlock& block) { block.setStyle(type::toastCaption(p)); });
}

// show (:278-293).
void Toast::show(const ToastRequest& toast, const motion::Clock& clock) {
    cancel(env_->deadlines, hideToken_);
    if (auto completion = std::exchange(pendingCompletion_, {})) completion();
    layer_->removeAllMotion();
    const auto& p = env_->palette;
    icon_ = toast.icon;
    setIcon(*glyph_, icon_, 26, p.foreground(1));
    glyph_->setPosition({30, 30});
    editText(*title_, [&](TextBlock& block) { block.setText(toast.title); });
    editText(*caption_, [&](TextBlock& block) { block.setText(toast.caption); block.language = toast.captionLanguage; });
    layer_->setHidden(false);
    layer_->setValue(Channel::opacity, 1); layer_->setValue(Channel::translateY, 0);
    motion::grow(*line_, .35, clock);
    motion::pop(*box_, clock, duration::pop, .08);
    motion::enter(*title_, {0, 8}, clock, .3, .14);
    motion::enter(*caption_, {0, 8}, clock, .3, .2);
    motion::grow(*underline_, .3, clock, .25);
}
// hide (:296-308): fadeOut (.24 easeIn, rising 6 px), then hidden.
void Toast::hide(bool animated, const motion::Clock& clock, std::function<void()> completion) {
    cancel(env_->deadlines, hideToken_);
    if (auto previous = std::exchange(pendingCompletion_, {})) previous();
    if (!animated || clock.reduceMotion || layer_->hidden()) {
        layer_->setHidden(true);
        if (completion) completion();
        return;
    }
    motion::animate(*layer_, Channel::opacity, 0, duration::toastHide, clock, easing::easeIn);
    motion::animate(*layer_, Channel::translateY, -6, duration::toastHide, clock, easing::easeIn);
    pendingCompletion_ = std::move(completion);
    hideToken_ = env_->deadlines.token();
    env_->deadlines.after(clock.now + duration::toastHide, hideToken_, [this] {
        hideToken_ = 0;
        layer_->setHidden(true);
        if (auto done = std::exchange(pendingCompletion_, {})) done();
    });
}
std::pair<std::string, std::string> Toast::texts() const { return {textOf(*title_).text(), textOf(*caption_).text()}; }

// ---------------------------------------------------------------------------
// HUDCenterOverlay (:316-525)
// ---------------------------------------------------------------------------
CenterOverlay::CenterOverlay(Environment& env)
    : env_(&env), token_(env.deadlines.token()), arena_("overlay", &env.deadlines), builder_{env, arena_, controls_} {
    layer_ = &builder_.layer("center.overlay");
    dim_ = &builder_.layer("center.dim");
    layer_->setAnchorPoint({0, 0}); layer_->setFrame(metric::box);
    // The 850 px dim circle centred on the disc (stage 975, 575 -> box 295, 305).
    dim_->setFrame({295 - 425, 305 - 425, 850, 850});
    dim_->setZPosition(order::dim);
    dim_->setAnimatable();                               // opacity .22 E
    dim_->setValue(Channel::opacity, 0);
    bar_ = std::make_unique<ConfirmBar>(env, token_);
    toast_ = std::make_unique<Toast>(env, token_);
    bar_->layer().setFrame({ConfirmBar::origin.x, ConfirmBar::origin.y, ConfirmBar::size.width, ConfirmBar::size.height});
    toast_->layer().setFrame({Toast::origin.x, Toast::origin.y, Toast::size.width, Toast::size.height});
    for (Layer* child : {dim_, &bar_->layer(), &toast_->layer()}) { child->setPartRoot(true); layer_->add(*child); }
    bar_->onDecision = [this](ConfirmState::Decision decision) { decide(decision); };
    outside_ = &controls_.make<OutsidePress>(env, *layer_, bar_->layer());
    outside_->onActivate = [this] { decide(ConfirmState::Decision::cancel); };
    applyDim();
}
CenterOverlay::~CenterOverlay() {
    cancel(env_->deadlines, token_); cancel(env_->deadlines, toastTimer_);
    router_.set({});
}

void CenterOverlay::applyPalette() { toast_->applyPalette(); bar_->applyPalette(); applyDim(); }
// applyDim (:380-384): black in dark, a pale veil in Light.
void CenterOverlay::applyDim() {
    const auto& p = env_->palette;
    const std::array<Color, 3> colors{p.tone(0x000000, .62), p.tone(0x000000, .62), p.tone(0x000000, 0)};
    const std::array<double, 3> stops{0, .821, .99};
    dim_->setContent(gradient(dim_->id(), localBounds(*dim_), colors, stops, {.5, .5}, {1, 1}, GradientKind::radial));
}

// confirm (:389-398).
void CenterOverlay::confirm(ConfirmRequest request, std::function<void()> onConfirm, std::function<void()> onCancel) {
    clearToast();
    confirm_.present(request);
    onConfirm_ = std::move(onConfirm); onCancel_ = std::move(onCancel);
    bar_->configure(request);
    std::vector<Control*> controls;
    if (request.cancelPillConfirms) controls.push_back(outside_);
    for (Control* pill : bar_->pills()) controls.push_back(pill);
    router_.set(std::move(controls));
    bar_->show(env_->clock());
    setDimmed(true);
    notify();
}
void CenterOverlay::setCountdown(int value) {
    confirm_.setCountdown(value);
    bar_->setCountdown(value);
}
// handleKey (:406-410): Esc / Return decide; every other key is swallowed while capturing.
bool CenterOverlay::handleKey(const KeyInput& key) {
    if (!capturesInput()) return false;
    if (const auto decision = confirm_.key(key)) finish(*decision);
    return true;
}
bool CenterOverlay::decide(ConfirmState::Decision decision) {
    if (!confirm_.decide(decision)) return false;
    finish(decision);
    return true;
}
// finish (:419-436): Cancel calls back at once, Confirm after 120 ms (at once under Reduce Motion).
void CenterOverlay::finish(ConfirmState::Decision decision) {
    auto onConfirm = std::exchange(onConfirm_, {});
    auto onCancel = std::exchange(onCancel_, {});
    router_.set({});
    const auto clock = env_->clock();
    bar_->hide(true, clock, [this] {
        if (!confirm_.decision()) return;                // a new request was presented meanwhile
        confirm_.close();
        notify();
    });
    if (!toastState_.current()) setDimmed(false);
    notify();
    if (decision == ConfirmState::Decision::cancel) { if (onCancel) onCancel(); return; }
    if (!onConfirm) return;
    if (clock.reduceMotion) onConfirm();
    else env_->deadlines.after(clock.now + duration::confirmCallback, token_, std::move(onConfirm));
}
// dismissConfirm (:439-447): no decision, no callback.
void CenterOverlay::dismissConfirm() {
    if (!confirm_.request()) return;
    onConfirm_ = {}; onCancel_ = {};
    confirm_.close();
    router_.set({});
    bar_->hide(false, env_->clock());
    if (!toastState_.current()) setDimmed(false);
    notify();
}
// showToast (:449-461).
void CenterOverlay::showToast(ToastRequest request, std::function<void()> completion) {
    if (confirm_.isOpen()) return;                       // dropped: its completion never runs
    cancel(env_->deadlines, toastTimer_);
    auto previous = std::exchange(toastCompletion_, std::move(completion));
    if (previous) previous();
    const auto token = toastState_.show(request);
    const auto clock = env_->clock();
    toast_->show(request, clock);
    setDimmed(true);
    toastTimer_ = env_->deadlines.token();
    env_->deadlines.after(clock.now + duration::toastVisible, toastTimer_, [this, token] { expireToast(token); });
}
// expireToast (:463-470): only the toast this timer was started for.
void CenterOverlay::expireToast(std::uint64_t token) {
    if (!toastState_.expire(token)) return;
    toastTimer_ = 0;
    toast_->hide(true, env_->clock(), std::exchange(toastCompletion_, {}));
    if (!confirm_.isOpen()) setDimmed(false);
}
// clearToast (:472-480).
void CenterOverlay::clearToast() {
    cancel(env_->deadlines, toastTimer_);
    if (!toastState_.current()) return;
    toastState_.clear();
    toast_->hide(false, env_->clock());
    if (auto completion = std::exchange(toastCompletion_, {})) completion();
}
// setDimmed (:483-488) + setContentDimmed (:494-515): radial dim .22 E and the content filter .25 E.
void CenterOverlay::setDimmed(bool on) {
    if (on == dimmed_) return;
    dimmed_ = on;
    const auto clock = env_->clock();
    motion::animate(*dim_, Channel::opacity, on ? 1 : 0, duration::dim, clock);
    filter_.set(on, clock.now, clock.reduceMotion);
}
// reset (:520-524): everything off at once (HUD close).
void CenterOverlay::reset() {
    dismissConfirm(); clearToast();
    dim_->setValue(Channel::opacity, 0);
    filter_.reset();
    dimmed_ = false;
}

// Pointer routing while capturing (the host routes like the quit card).
bool CenterOverlay::pointerDown(Point point) {
    if (!capturesInput()) return false;
    router_.pointerDown(point, *layer_);
    return true;
}
void CenterOverlay::pointerDragged(Point point) { router_.pointerDragged(point, *layer_); }
void CenterOverlay::pointerUp(Point point) { router_.pointerUp(point, *layer_); }
// SystemHUDView.updateControlHighlights: the overlay's targets see the pointer only while it captures input.
void CenterOverlay::pointerMoved(std::optional<Point> point, bool pressed) {
    updateHighlights(*layer_, capturesInput() ? point : std::nullopt, pressed);
}
void CenterOverlay::accessibilityItems(std::vector<AccessibilityItem>& out) const {
    if (capturesInput()) router_.accessibilityItems(*layer_, out);
}
std::uint64_t CenterOverlay::contentRevision() const noexcept {
    const auto pills = bar_->pills();
    const std::array<std::uint64_t, 4> key{arena_.revision(), bar_->layer().arena().revision(), toast_->layer().arena().revision(),
        pills.empty() ? 0 : reinterpret_cast<std::uintptr_t>(&pills.front()->layer().arena()) ^ pills.front()->layer().arena().revision()};
    if (key != revisionKey_) { revisionKey_ = key; ++revision_; }
    return revision_;
}
} // namespace endfield::modules::design
