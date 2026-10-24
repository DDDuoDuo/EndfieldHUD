// PersonalProfileCanvas (Sources/PersonalProfileCanvas.swift, Mac 1.3.0),
// statement by statement; line numbers cite it.
#include "modules/profile_canvas.hpp"
#include "modules/settings.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <tuple>

namespace endfield::modules {
namespace d = design;
using d::Color; using d::Json; using d::Layer; using d::Point; using d::Rect;

namespace {
constexpr Color ink = d::rgb(0xF5F5F5);
// Style (:1237-1257). The accent styles take the card accent when built.
namespace style {
const d::TextStyle title{12, d::FontWeight::bold, ink, 1, .16};
const d::TextStyle titleSmall{11, d::FontWeight::medium, d::rgb(0xF5F5F5, .55), 1, .2};
const d::TextStyle name{22, d::FontWeight::bold, ink, 30. / 22};
const d::TextStyle tag{22, d::FontWeight::regular, d::rgb(0xF5F5F5, .6), 30. / 22};
const d::TextStyle dateKind{10, d::FontWeight::bold, d::white(1), 1, .06, true};
const d::TextStyle dateValue{12, d::FontWeight::bold, d::rgb(0x292929), 1, 0, false, true};
const d::TextStyle uid{13, d::FontWeight::regular, d::rgb(0xF5F5F5, .65), 1, 0, false, true};
const d::TextStyle levelLabel{14, d::FontWeight::regular, d::white(1)};
const d::TextStyle levelValue{25, d::FontWeight::medium, d::white(1), 1, 0, false, true};
d::TextStyle counterValue(Color accent) { return {27, d::FontWeight::medium, accent, 1.05, 0, false, true}; }
d::TextStyle counterLabel(Color accent) { return {11, d::FontWeight::regular, accent}; }
d::TextStyle region(Color accent) { return {11.5, d::FontWeight::bold, accent, 1, .08}; }
const d::TextStyle workLabel{12, d::FontWeight::regular, ink, 1.2};
const d::TextStyle workValue{24, d::FontWeight::medium, ink, 1, 0, false, true};
const d::TextStyle workUnit{10, d::FontWeight::regular, d::rgb(0xF5F5F5, .65), 1.2};
const d::TextStyle intro{13, d::FontWeight::medium, ink, 1.5};
const d::TextStyle toolbar{12, d::FontWeight::medium, ink};
const d::TextStyle popoverLabel{12, d::FontWeight::regular, d::rgb(0xDDDDDD)};
}
Color srgb(std::array<double, 3> c) { return {c[0], c[1], c[2], 1}; }
// PersonalProfileCanvas.color(_:) (:1200): a hex, else the palette yellow.
std::optional<Color> hexColor(std::string_view hex) {
    unsigned value{};
    const auto r = std::from_chars(hex.data(), hex.data() + hex.size(), value, 16);
    if (hex.size() != 6 || r.ec != std::errc{} || r.ptr != hex.data() + hex.size()) return std::nullopt;
    return d::rgb(value);
}
Rect bounds(d::Size s) { return {0, 0, s.width, s.height}; }
constexpr std::array<ProfileField, 7> geometryFields{ProfileField::backgroundWidth, ProfileField::backgroundZoom, ProfileField::backgroundOffsetX,
    ProfileField::backgroundOffsetY, ProfileField::thumbnailZoom, ProfileField::thumbnailOffsetX, ProfileField::thumbnailOffsetY};
constexpr std::array<ProfileField, 3> portraitFields{ProfileField::avatarZoom, ProfileField::avatarOffsetX, ProfileField::avatarOffsetY};
constexpr std::array<ProfileField, 3> counters{ProfileField::operatorsCount, ProfileField::weaponsCount, ProfileField::archivesCount};
// PersonalProfileField.shown (:66).
constexpr std::array<ProfileField, 11> shown{ProfileField::name, ProfileField::tag, ProfileField::playerID, ProfileField::introduction,
    ProfileField::awakeningDate, ProfileField::birthday, ProfileField::permissionLevel, ProfileField::explorationLevel,
    ProfileField::operatorsCount, ProfileField::weaponsCount, ProfileField::archivesCount};
// sliderSpec (:1020-1029).
std::pair<d::Slider::Range, d::ValueFormat> sliderSpec(ProfileField f) {
    switch (f) {
    case ProfileField::backgroundWidth: return {{400, 900, 1, std::nullopt}, d::ValueFormat::integer};
    case ProfileField::backgroundZoom: return {{1, 20, .01, std::nullopt}, d::ValueFormat::scale};
    case ProfileField::backgroundOffsetX: return {{-400, 400, 1, 0}, d::ValueFormat::signed_};
    case ProfileField::backgroundOffsetY: return {{-250, 250, 1, 0}, d::ValueFormat::signed_};
    case ProfileField::thumbnailZoom: case ProfileField::avatarZoom: return {{1, 5, .01, std::nullopt}, d::ValueFormat::scale};
    default: return {{-100, 100, 1, 0}, d::ValueFormat::signedPercent};
    }
}
// HUDProfileLevelArtwork.makeLayer (HUDPortraitArtwork.swift:171-214), in a
// `frame`-sized shape node (the authored 34 x 32 grid scaled).
Json levelArtwork(std::string id, bool authority, Rect frame, Color color) {
    const double sx = frame.width / 34, sy = frame.height / 32;
    d::PathBuilder path;
    const auto polygon = [&](std::initializer_list<Point> points) {
        bool first = true;
        for (const auto& p : points) { if (first) path.move(p); else path.line(p); first = false; }
        path.close();
    };
    if (authority) {
        // Three staggered folded plates, with the front face left open.
        polygon({{5, 10}, {15, 17}, {15, 30}, {5, 23}});
        path.move({10, 6}).line({22, 14}).line({22, 28});
        path.move({17, 2}).line({29, 10}).line({29, 25});
        d::StrokeStyle stroke; stroke.width = frame.width / 34 * 2.7;
        return d::shape(std::move(id), frame, path.transformed(sx, 0, 0, sy, 0, 0), std::nullopt, color, stroke);
    }
    // Solid eye crest with a circular aperture (even-odd, its own sublayer) and serrated side fins.
    polygon({{8, 8}, {12, 4}, {12, 10}});
    polygon({{22, 8}, {25, 4}, {26, 12}});
    polygon({{8, 24}, {12, 28}, {12, 22}});
    polygon({{22, 24}, {25, 28}, {26, 20}});
    polygon({{5, 5}, {7, 10}, {6, 16}, {7, 22}, {5, 27}, {3, 21}, {3, 11}});
    polygon({{1, 8}, {2, 13}, {2, 19}, {1, 24}, {0, 20}, {0, 12}});
    polygon({{29, 5}, {27, 10}, {28, 16}, {27, 22}, {29, 27}, {31, 21}, {31, 11}});
    polygon({{33, 8}, {32, 13}, {32, 19}, {33, 24}, {34, 20}, {34, 12}});
    Json node = d::shape(id, frame, path.transformed(sx, 0, 0, sy, 0, 0), color);
    d::PathBuilder eye;
    eye.ellipse({7, 6, 20, 20}).ellipse({13, 12, 8, 8});
    d::addChildren(node, {d::shape(id + ".aperture", {0, 0, frame.width, frame.height}, eye.transformed(sx, 0, 0, sy, 0, 0), color, std::nullopt, {}, true)});
    return node;
}
}

// The pop-over's second Follow HUD button (its secondary kind) shares the
// first's id "theme:follow" as on the Mac (one of the two is always hidden),
// so it lives in an arena of its own beside the pop-over's.
struct ProfileSideArena {
    d::LayerArena arena;
    d::ControlSet controls;
    d::Builder builder;
    explicit ProfileSideArena(d::Environment& env) : arena("profile.side", &env.deadlines), builder{env, arena, controls} {}
};

// ProfileEditable (:1292-1332): hover draws a 1 px accent ring with a soft
// accent glow (.22 s); a press opens the inline editor. The control layer is
// animatable: vChg plays on it.
class ProfileEditable final : public d::Control {
public:
    ProfileEditable(d::Builder& b, std::string id, std::string label) : Control(b.env, id, std::move(label), b.layer(id)) {
        layer().setAnimatable();
        ring_ = &b.layer(id + ".ring");
        ring_->setAnimatable(); ring_->setValue(d::Channel::opacity, 0); ring_->setZPosition(1);
        layer().add(*ring_);
    }
    void setAccent(Color c) { accent_ = c; renderRing(); }
    void setFrame(Rect frame, std::vector<Layer*> content) {
        for (Layer* l : content_) l->removeFromParent();
        content_ = content;
        layer().setFrame(frame);
        ring_->setFrame(bounds({frame.width, frame.height}));
        renderRing();
        for (Layer* l : content) layer().insertBelow(*l, *ring_);
        setTarget(bounds({frame.width, frame.height}));
    }
    void hoverChanged() override {
        d::motion::animate(*ring_, d::Channel::opacity, hovered() && enabled() ? 1 : 0, d::duration::hover, clock(), d::easing::ease);
    }
    void enabledChanged() override { hoverChanged(); }
private:
    Layer* ring_; std::vector<Layer*> content_; Color accent_{d::rgb(d::standardAccent)};
    // The glow hugs the ring: a 1 px outline as the shadow path.
    void renderRing() {
        const auto b = d::localBounds(*ring_);
        Json node = d::bordered(ring_->id(), b, std::nullopt, accent_, 1);
        d::PathBuilder outline;
        outline.rect(b).move({1, 1}).line({1, b.height - 1}).line({b.width - 1, b.height - 1}).line({b.width - 1, 1}).close();
        d::addShadow(node, d::Shadow{accent_, .3, 7, {0, 0}, outline});
        ring_->setContent(std::move(node));
    }
};

// ProfileCircleButton (:1334-1394): ⋯ (light, three dots; hover scale 1.08
// and a 3 px accent ring at 40 %) and the eye (dark; hover .5 and 1.08).
class ProfileCircleButton final : public d::Control {
public:
    enum class Style : std::uint8_t { more, eye };
    ProfileCircleButton(d::Builder& b, std::string id, std::string label, Style style) : Control(b.env, id, std::move(label), b.layer(id)), style_(style) {
        const double side = style == Style::more ? 28 : 32;
        const Rect box{0, 0, side, side};
        Layer& l = layer();
        l.setAnimatable(); l.setBounds({side, side});
        disc_ = &b.layer(id + ".circle"); disc_->setFrame(box);
        halo_ = &b.layer(id + ".circle.ring"); halo_->setFrame({-3, -3, side + 6, side + 6});
        glyph_ = &b.layer(id + ".circle.glyph"); glyph_->setFrame(box);
        if (style == Style::more) {
            d::setBackground(*disc_, d::rgb(0xF5F5F5, .88), side / 2);
            halo_->setAnimatable(); halo_->setValue(d::Channel::opacity, 0);
            l.add(*halo_); l.add(*disc_);
            // 16 x 4 strip of r 1.6 dots every 5.5 px.
            d::PathBuilder dots;
            for (int index = 0; index < 3; ++index) dots.ellipse({6 + 2.75 + index * 5.5 - 1.6, 14 - 1.6, 3.2, 3.2});
            glyph_->setContent(d::shape(glyph_->id(), box, dots, d::rgb(0x1F1F1F)));
            l.add(*glyph_);
        } else {
            d::setTintedBackground(*disc_, d::black(.3), side / 2);
            l.add(*disc_);
            // The custom 20 px eye: almond + iris, slashed while the text shows.
            const auto scaled = [](const d::PathBuilder& p) { return p.transformed(20. / 24, 0, 0, 20. / 24, 6, 6); };
            d::PathBuilder eye;
            eye.move({2, 12}).quadratic({12, 1}, {22, 12}).quadratic({12, 23}, {2, 12}).close();
            eye.ellipse({12 - 3.2, 12 - 3.6, 6.4, 7.2});
            d::PathBuilder line; line.move({4, 21}).line({20, 3});
            d::StrokeStyle stroke; stroke.width = 1.6 * 20 / 24;
            glyph_->setContent(d::shape(glyph_->id(), box, scaled(eye), std::nullopt, ink, stroke));
            slash_ = &b.layer(id + ".circle.slash"); slash_->setFrame(box);
            slash_->setContent(d::shape(slash_->id(), box, scaled(line), std::nullopt, ink, stroke));
            l.add(*glyph_); l.add(*slash_);
        }
        d::PathBuilder target; target.ellipse(box);
        setTarget(box, target);
    }
    void setAccent(Color c) { if (style_ == Style::more) d::setBorder(*halo_, std::nullopt, withAlpha(c, .4), 3, halo_->bounds().width / 2); }
    void setSlashed(bool on) { if (slash_ && slash_->hidden() == on) slash_->setHidden(!on); }
    void hoverChanged() override {
        const bool on = hovered() && enabled();
        d::motion::animate(layer(), d::Channel::scaleX, on ? 1.08 : 1, d::duration::hover, clock());
        d::motion::animate(layer(), d::Channel::scaleY, on ? 1.08 : 1, d::duration::hover, clock());
        if (style_ == Style::more) d::motion::animate(*halo_, d::Channel::opacity, on ? 1 : 0, d::duration::hover, clock(), d::easing::ease);
        else d::motion::animateTint(*disc_, d::black(on ? .5 : .3), d::duration::hover, clock(), d::easing::ease);
    }
    void enabledChanged() override { hoverChanged(); }
private:
    Style style_; Layer *disc_, *halo_, *glyph_, *slash_{};
};

// ProfileThemeButton (:1396-1421): `Card theme` + image icon; hover fills
// rgba(245,245,245,.1) and lifts 1 px.
class ProfileThemeButton final : public d::Control {
public:
    ProfileThemeButton(d::Builder& b, std::string id, std::string title) : Control(b.env, id, title, b.layer(id)) {
        Layer& l = layer(); l.setAnimatable();
        Layer& text = d::makeText(b, id + ".text", title, style::toolbar, {});
        const double textWidth = std::ceil(d::preferredWidth(d::textOf(text), b.env.text));
        const Rect box{0, 0, 10 + textWidth + 8 + 15 + 10, 30};
        l.setBounds({box.width, box.height});
        fill_ = &b.layer(id + ".fill"); fill_->setFrame(box); d::setBackground(*fill_, d::rgb(0xF5F5F5, .1));
        fill_->setAnimatable(); fill_->setValue(d::Channel::opacity, 0);
        d::setLine(text, {10, 9, textWidth + 1, 12});
        Layer& icon = d::makeIcon(b, id + ".icon", d::Icon::image, 15, ink);
        icon.setOrigin({10 + textWidth + 8, 7.5});
        l.add(*fill_); l.add(text); l.add(icon);
        setTarget(box);
    }
    void hoverChanged() override {
        const bool on = hovered() && enabled();
        d::motion::animate(*fill_, d::Channel::opacity, on ? 1 : 0, d::duration::hover, clock(), d::easing::ease);
        d::motion::animate(layer(), d::Channel::translateY, on ? -1 : 0, d::duration::hover, clock());
    }
private:
    Layer* fill_;
};

// --- Geometry -------------------------------------------------------------------------
Rect ProfileCanvas::maskFrame() noexcept {
    // HUDCentreBackdrop.frame (430 px around box (295, 304)) offset by -cardOrigin.
    const auto c = d::centreBackdropCentre; const auto r = d::centreBackdropRadius;
    return {c.x - r - cardOrigin.x, c.y - r - cardOrigin.y, 2 * r, 2 * r};
}
Rect ProfileCanvas::artHostFrame() noexcept {
    const auto m = maskFrame();
    const double x0 = std::min(artFrame.x, m.x), y0 = std::min(artFrame.y, m.y);
    const double x1 = std::max(artFrame.x + artFrame.width, m.x + m.width), y1 = std::max(artFrame.y + artFrame.height, m.y + m.height);
    return {x0, y0, x1 - x0, y1 - y0};
}
// artBoxFrame (:666-670): w = width x 1.58, h = w x .685, centred in the
// 950 x 650 layer and offset by X x 1.58, Y x 1.3.
Rect ProfileCanvas::artBoxFrame(const PersonalProfile& p) noexcept {
    const double width = p.backgroundWidth * 1.58, height = width * .685;
    return {(artFrame.width - width) / 2 + p.backgroundOffsetX * 1.58, (artFrame.height - height) / 2 + p.backgroundOffsetY * 1.3, width, height};
}
namespace {
// The art box layer's fixed model size: its frame changes are a scale of it
// (the box keeps the .685 aspect, so CA's bounds animation is a uniform scale).
constexpr d::Size artReference{600 * 1.58, 600 * 1.58 * .685};
}

// --- Construction, store ------------------------------------------------------------------
ProfileCanvas::ProfileCanvas(d::Context& c, ProfileTextRules rules, ProfileDateRules dates, ProfileCanvasServices services)
    : Canvas(core::Module::profile, c), text_(std::move(rules)), dates_(std::move(dates)), services_(std::move(services)) {
    if (!dates_.noon || !dates_.format) throw std::invalid_argument("Personal ID needs the Gregorian date rules");
}
void ProfileCanvas::setDateRules(ProfileDateRules dates) {
    if (!dates.noon || !dates.format) throw std::invalid_argument("Personal ID needs the Gregorian date rules");
    dates_ = std::move(dates);
    if (renderedLanguage()) rerenderWhenIdle();
}
ProfileCanvas::~ProfileCanvas() {
    stopHoursTimer();
    if (store_ && observer_) store_->removeObserver(observer_);
}
void ProfileCanvas::setStore(ProfileState* store) {
    if (store_ == store) return;
    if (store_ && observer_) store_->removeObserver(observer_);
    store_ = store; observer_ = 0; errorMessage_.reset();
    if (store_) {
        observer_ = store_->observe([this] { storeChanged(); });
        profile_ = store_->profile(); locked_ = store_->syncLocked();
    }
    loaded_ = store_ != nullptr;
    if (renderedLanguage()) rerenderWhenIdle();
}
void ProfileCanvas::setStoreError(PersonalProfile defaults, std::string error) {
    if (store_ && observer_) store_->removeObserver(observer_);
    store_ = nullptr; observer_ = 0;
    profile_ = std::move(defaults); errorMessage_ = std::move(error); loaded_ = true;
    if (renderedLanguage()) rerenderWhenIdle();
}
void ProfileCanvas::setHudAccent(std::array<double, 3> accent) {
    if (accent == hudAccent_) return;
    hudAccent_ = accent;
    if (renderedLanguage()) applyAccent();
}
std::array<double, 3> ProfileCanvas::resolvedAccent() const { return profileAccent(displayed(), hudAccent_); }
Color ProfileCanvas::accent() const { return srgb(resolvedAccent()); }
std::string ProfileCanvas::value(ProfileField field, const PersonalProfile& p) const { return profileValue(field, p, dates_); }

// --- Lifecycle (:161-199) -------------------------------------------------------------------
void ProfileCanvas::didActivate() {
    scheduleHours();
    refreshWorkDuration();
    refreshMinigameBest();
    if (!store_ && errorMessage_) toast(d::Icon::warn, "Notice", "提示", *errorMessage_);
}
void ProfileCanvas::didDeactivate() {
    stopHoursTimer();
    ++dialogGeneration_;
    if (colorPanel_ || chooser_) { colorPanel_ = false; chooser_.reset(); if (services_.dismissPanels) services_.dismissPanels(); }
    revertPreview(false);
}
// Timer(timeInterval: 30, repeats: true) while active (tolerance 5 s): one
// module timer group, rescheduled in place.
void ProfileCanvas::scheduleHours() {
    stopHoursTimer();
    hoursTimer_ = timerGroup();
    tickHours();
}
void ProfileCanvas::tickHours() {
    const auto group = hoursTimer_;
    after(30, group, [this, group] { if (hoursTimer_ != group) return; refreshWorkDuration(); tickHours(); });
}
void ProfileCanvas::stopHoursTimer() { if (hoursTimer_) { cancel(hoursTimer_); hoursTimer_ = {}; } }
// closePopover override (:179-183): an interrupted drag is reverted; the
// second Follow HUD button leaves with its pop-over (its arena detaches it
// while the closed pop-over still exists).
void ProfileCanvas::didClosePopover() {
    popoverBinders_.clear();                     // the Mac's weak captures: the closed pop-over's controls are gone
    if (side_) { trackArena(nullptr); side_.reset(); }
    revertPreview(true);
}
void ProfileCanvas::revertPreview(bool animated) {
    if (!preview_) return;
    preview_.reset();
    layoutGeometry(animated);
    if (services_.geometryPreview) services_.geometryPreview(profile_);
}
void ProfileCanvas::recordingModeChanged() {
    if (editing_ && editing_->field == ProfileField::playerID) editing_->text->endEditing(false);
    if (!renderedLanguage()) return;
    rerenderWhenIdle();
    if (env().recordingMode && refs_.uid) { Layer* uid = refs_.uid; d::redaction::playEntrance(std::span<Layer* const>(&uid, 1), clock()); }
}

// refreshWorkDuration / refreshMinigameBest (:201-228).
void ProfileCanvas::refreshWorkDuration() {
    if (!refs_.hours.value) return;
    const auto next = formattedHours();
    if (d::textOf(*refs_.hours.value).text() == next) return;
    setReadout(refs_.hours, next);
    if (isActive()) playValueChange(*refs_.hours.value);
}
void ProfileCanvas::refreshMinigameBest() {
    if (!refs_.best.value) return;
    const auto next = formattedBest();
    if (d::textOf(*refs_.best.value).text() == next) return;
    setReadout(refs_.best, next);
    if (isActive()) playValueChange(*refs_.best.value);
}
std::string ProfileCanvas::formattedBest() const {
    const auto score = services_.minigameBest ? services_.minigameBest() : 0;
    return score > 0 ? d::format::grouped(score) : "—";
}
std::string ProfileCanvas::formattedHours() const {
    const double live = services_.workSeconds ? services_.workSeconds() : 0;
    const double accumulated = displayed().accumulatedWorkSeconds;
    const double seconds = live >= accumulated ? live : accumulated; // Swift max(x, y)
    char s[400]{};
    std::snprintf(s, sizeof s, "%.2f", std::isfinite(seconds) ? std::max(0., seconds) / 3600 : 0.);
    return s;
}
std::string ProfileCanvas::hoursText() const { return refs_.hours.value ? d::textOf(*refs_.hours.value).text() : std::string(); }
std::string ProfileCanvas::bestText() const { return refs_.best.value ? d::textOf(*refs_.best.value).text() : std::string(); }
void ProfileCanvas::showError(std::string message) {
    errorMessage_ = message;
    if (isActive()) toast(d::Icon::warn, "Notice", "提示", std::move(message));
}
bool ProfileCanvas::canEdit(ProfileField field) const {
    if (!store_) return false;
    const auto owner = profileSyncField(field);
    return !owner || !store_->syncLocked().contains(*owner);
}
void ProfileCanvas::editabilityChanged() {
    if (editing_ && !canEdit(editing_->field)) editing_->text->endEditing(false);
    if (renderedLanguage()) rerenderWhenIdle();
}
void ProfileCanvas::rerenderWhenIdle() { if (editing_) needsRerender_ = true; else rerender(); }

// storeChanged (:252-278).
void ProfileCanvas::storeChanged() {
    if (!store_) return;
    const auto old = displayed();
    profile_ = store_->profile();
    if (store_->syncLocked() != locked_) { locked_ = store_->syncLocked(); editabilityChanged(); }
    if (!renderedLanguage()) return;
    const auto& next = displayed();
    std::vector<ProfileField> changed;
    for (const auto field : shown) if (value(field, old) != value(field, next)) changed.push_back(field);
    const bool structural = !changed.empty() || old.showsBirthday != next.showsBirthday
        || old.avatarFilename != next.avatarFilename || old.backgroundFilename != next.backgroundFilename;
    if (structural) {
        if (editing_) { needsRerender_ = true; return; }
        rerender();
        for (const auto field : changed) if (const auto it = refs_.values.find(field); it != refs_.values.end()) playValueChange(*it->second);
        if (old.showsBirthday != next.showsBirthday) {
            if (refs_.dateKind) playValueChange(*refs_.dateKind);
            if (const auto it = refs_.values.find(next.showsBirthday ? ProfileField::birthday : ProfileField::awakeningDate); it != refs_.values.end()) playValueChange(*it->second);
        }
    } else {
        if (old.themeColorHex != next.themeColorHex) applyAccent();
        layoutGeometry(true);
    }
    for (auto& binder : popoverBinders_) binder();
    refreshWorkDuration();
}
void ProfileCanvas::imagesChanged() {
    if (!renderedLanguage()) return;
    // A decode landed: the art or the portrait appears (the Mac had it at
    // build time); a prepared zoom step re-lays the portrait in place.
    const bool art = services_.backgroundReady && services_.backgroundReady() && displayed().backgroundFilename;
    const auto portrait = services_.portrait ? services_.portrait(portraitStep()) : ProfileCanvasServices::Portrait{};
    const bool wantsPortrait = displayed().avatarFilename && portrait.image;
    if (art != (refs_.artHost != nullptr) || wantsPortrait != (refs_.portrait != nullptr) || (!portrait.pending && !portrait.image) != (refs_.silhouette != nullptr)) rerenderWhenIdle();
    else layoutPortrait(true);
}

// --- Page (:282-313) ------------------------------------------------------------------------
void ProfileCanvas::buildPage(d::Page& page) {
    refs_ = Refs{}; accentBinders_.clear();
    const auto& p = displayed();
    header().configure({"PERSONAL ID"}, "MOD-15", title("Personal ID", "个人名片"), loaded_ ? "#" + p.tag : std::string(), false);
    if (!loaded_) return;     // the record is still being read
    Layer& wrapper = page.makeLayer("profile.wrapper");
    Layer& card = page.makeLayer("profile.card");
    Layer& main = page.makeLayer("profile.main");
    const Rect box = bounds(cardSize);
    wrapper.setFrame({cardOrigin.x, cardOrigin.y, cardSize.width, cardSize.height});
    card.setFrame(box); card.setMasksToBounds(true);
    main.setFrame(box); main.setAnimatable();
    refs_.wrapper = &wrapper;
    if (services_.backgroundReady && services_.backgroundReady() && p.backgroundFilename) buildArt(page, wrapper);
    buildPlate(page, card, refs_.artBox != nullptr);
    refs_.card = &card; refs_.main = &main;
    std::vector<d::Control*> controls;
    buildIdentity(page, main, controls);
    buildLevels(page, main, controls);
    buildIntroduction(page, main, controls);
    card.add(main); wrapper.add(card);
    buildToolbar(page, card, controls);
    page.layer().add(wrapper);
    main.setValue(d::Channel::opacity, textHidden_ ? 0 : 1);
    page.stagger(wrapper);
    page.add(controls);
    applyAccent();
    layoutGeometry(false);
    renderVisibility(false);
}

// buildArt (:317-331): the art spanning the disc. The pixels are the
// owner's (see the header); these layers carry its geometry and motion.
void ProfileCanvas::buildArt(d::Page& page, Layer& wrapper) {
    Layer& host = page.makeLayer("profile.art");
    Layer& box = page.makeLayer("profile.art.box");
    Layer& picture = page.makeLayer("profile.art.image");
    host.setFrame(artHostFrame());
    host.setPartRoot(true);                              // the owner's photo is composed right after this part
    host.setContent(d::solid(host.id(), {0, 0, 1, 1}, d::clear));
    box.setAnimatable(); box.setBounds(artReference); box.setPosition({0, 0});
    picture.setAnimatable(); picture.setBounds(artReference); picture.setPosition({artReference.width / 2, artReference.height / 2});
    box.add(picture); host.add(box);
    wrapper.add(host);
    refs_.artHost = &host; refs_.artBox = &box; refs_.artImage = &picture;
}

// buildPlate (:335-357): card fill rgba(12,12,12,.38) in chamfer 18, the
// 1 px #2E2E2E ring open at the chamfer and, with art, the darkening overlay.
void ProfileCanvas::buildPlate(d::Page& page, Layer& card, bool art) {
    const Rect box = bounds(cardSize);
    Layer& fill = page.makeLayer("profile.plate");
    Layer& ring = page.makeLayer("profile.ring");
    fill.setFrame(box); fill.setAnimatable();
    fill.setContent(d::shape(fill.id(), box, d::chamfer(box, 18), d::rgb(0x0C0C0C, .38)));
    ring.setFrame(box); ring.setAnimatable();
    const Rect r{box.x + .5, box.y + .5, box.width - 1, box.height - 1};
    d::PathBuilder edge;
    edge.move({r.x + r.width - 17.5, r.y}).line({r.x, r.y}).line({r.x, r.y + r.height}).line({r.x + r.width, r.y + r.height}).line({r.x + r.width, r.y + 17.5});
    ring.setContent(d::shape(ring.id(), box, edge, std::nullopt, d::rgb(0x2E2E2E)));
    card.add(fill); card.add(ring);
    refs_.plate = {&fill, &ring};
    if (!art) return;
    // overlayArtwork (:1205-1216): linear-gradient(90deg, rgba(0,0,0,.74) 0,
    // .52 52%, .08 100%) clipped to the chamfer, one raster per render scale.
    Layer& overlay = page.makeLayer("profile.overlay");
    overlay.setFrame(box); overlay.setAnimatable();
    const std::array<Color, 3> colors{d::black(.74), d::black(.52), d::black(.08)};
    const std::array<double, 3> locations{0, .52, 1};
    Json node = d::gradient(overlay.id(), box, colors, locations, {0, .5}, {1, .5});
    d::addShapeMask(node, d::chamfer(box, 18));
    overlay.setContent(std::move(node));
    card.add(overlay);
    refs_.overlay = &overlay;
}

// buildIdentity (:359-428).
void ProfileCanvas::buildIdentity(d::Page& page, Layer& main, std::vector<d::Control*>& controls) {
    const auto& p = displayed();
    // Title: PERSONAL PROFILE + 个人名片 (flipped for CJK UIs).
    const auto lines = title("PERSONAL PROFILE", "个人名片").lines(env().language);
    Layer& titleText = d::makeText(page.builder(), "profile.title", lines.title, style::title, {});
    Layer& subtitle = d::makeText(page.builder(), "profile.subtitle", lines.subtitle, style::titleSmall, {});
    d::editText(titleText, [&](d::TextBlock& block) { block.language = lines.titleLanguage; });
    const double titleWidth = std::ceil(d::preferredWidth(d::textOf(titleText), env().text));
    d::setLine(titleText, {22, 16, titleWidth + 1, 12});
    d::setLine(subtitle, {22 + titleWidth + 10, 16.5, std::ceil(d::preferredWidth(d::textOf(subtitle), env().text)) + 1, 11});
    // Portrait well and the tinted frame.
    Layer& well = page.makeLayer("profile.well");
    Layer& frame = page.makeLayer("profile.frame");
    well.setFrame(wellFrame); d::setBackground(well, d::rgb(0x383838, .85)); well.setMasksToBounds(true);
    frame.setFrame(avatarFrame);
    for (Layer* l : {&titleText, &subtitle, &well, &frame}) main.add(*l);
    refs_.frame = &frame; refs_.well = &well;
    const auto portrait = services_.portrait && p.avatarFilename ? services_.portrait(portraitStep()) : ProfileCanvasServices::Portrait{};
    if (portrait.image) {
        Layer& picture = page.makeLayer("profile.portrait");
        picture.setAnimatable(); picture.setPosition({wellFrame.width / 2, wellFrame.height / 2});
        well.add(picture);
        refs_.portrait = &picture;
    } else if (!portrait.pending) {
        Layer& silhouette = page.makeLayer("profile.silhouette");
        silhouette.setAnimatable(); silhouette.setPosition({wellFrame.width / 2, wellFrame.height / 2});
        well.add(silhouette);
        refs_.silhouette = &silhouette;
    }
    // Name + #tag (max-width 300, padding 0 4).
    auto& name = editable(page, ProfileField::name, main, controls);
    Layer& nameText = d::makeText(page.builder(), "profile.name.text", p.name, style::name, {});
    Layer& tagText = d::makeText(page.builder(), "profile.tag.text", "#" + p.tag, style::tag, {});
    const double tagWidth = std::ceil(d::preferredWidth(d::textOf(tagText), env().text));
    const double nameWidth = std::min(std::ceil(d::preferredWidth(d::textOf(nameText), env().text)), 292 - 2 - tagWidth);
    d::setLine(nameText, {4, 0, nameWidth + 1, 30});
    d::setLine(tagText, {4 + nameWidth + 2, 0, tagWidth + 1, 30});
    name.setFrame({132, 50, 4 + nameWidth + 2 + tagWidth + 4, 30}, {&nameText, &tagText});
    name.label = text("Edit name", "修改名称") + ", " + p.name + " #" + p.tag;
    refs_.values[ProfileField::tag] = &name.layer();
    // Registration mark.
    Layer& mark = page.makeLayer("profile.registration");
    Layer& bar = page.makeLayer("profile.registration.bar");
    mark.setFrame({138, 84, 64, 16});
    d::PathBuilder path;
    path.move({1, 1}).line({1, 10}).line({9, 10});
    path.move({48, 1}).line({57, 1}).line({57, 10});
    path.move({14, 1}).line({25, 10});
    path.move({25, 1}).line({14, 10});
    mark.setContent(d::shape(mark.id(), {0, 0, 64, 16}, path, std::nullopt, d::rgb(0xF5F5F5, .45)));
    bar.setFrame({138, 96, 26, 3.5}); d::setBackground(bar, d::rgb(0xF5F5F5, .75));
    main.add(mark); main.add(bar);
    buildDateChip(page, main, controls);
    buildUID(page, main, controls);
    // ⋯ menu.
    auto& more = page.make<ProfileCircleButton>("profile:menu", text("Edit personal profile", "编辑个人名片"), ProfileCircleButton::Style::more);
    more.layer().setFrame(moreFrame);
    main.add(more.layer());
    more.onActivate = [this, &more] { openMoreMenu(more); };
    accentBinders_.push_back([&more](Color c) { more.setAccent(c); });
    controls.push_back(&more);
    refs_.more = &more;
}

// buildDateChip (:430-463).
void ProfileCanvas::buildDateChip(d::Page& page, Layer& main, std::vector<d::Control*>& controls) {
    const auto& p = displayed();
    Layer& kindLayer = page.makeLayer("profile:toggleDateLabel");
    auto& kind = page.builder().controls.make<d::Control>(env(), "profile:toggleDateLabel",
        p.showsBirthday ? text("Show awakening day", "切换为苏醒日") : text("Show birthday", "切换为生日"), kindLayer);
    Layer& kindText = d::makeText(page.builder(), "profile.date.kind", p.showsBirthday ? text("Birthday", "生日") : text("Awakening", "苏醒日"), style::dateKind, {});
    Layer& chevrons = page.makeLayer("profile.date.chevrons");
    const double kindTextWidth = std::ceil(d::preferredWidth(d::textOf(kindText), env().text));
    const double kindWidth = 6 + 12 + 6 + kindTextWidth + 8;
    kindLayer.setAnimatable();                         // vChg when the kind flips
    kindLayer.setFrame({138, 98, kindWidth, 20});
    d::setBackground(kindLayer, d::black(.73));
    chevrons.setFrame({6, 5, 12, 10});
    d::setLine(kindText, {24, 5, kindTextWidth + 1, 10});
    kindLayer.add(chevrons); kindLayer.add(kindText);
    main.add(kindLayer);
    kind.setTarget(d::localBounds(kindLayer));
    kind.onActivate = [this] { toggleDateKind(); };
    accentBinders_.push_back([&chevrons](Color c) {
        d::PathBuilder path;
        path.move({0, 0}).line({5, 5}).line({0, 10}).close();
        path.move({6, 0}).line({11, 5}).line({6, 10}).close();
        chevrons.setContent(d::shape(chevrons.id(), {0, 0, 12, 10}, path, c));
    });
    controls.push_back(&kind);
    const auto field = p.showsBirthday ? ProfileField::birthday : ProfileField::awakeningDate;
    auto& valueControl = editable(page, field, main, controls);
    Layer& valueText = d::makeText(page.builder(), "profile.date.value", value(field, p), style::dateValue, {});
    const double valueWidth = std::ceil(d::preferredWidth(d::textOf(valueText), env().text));
    d::setLine(valueText, {10, 4, valueWidth + 1, 12});
    valueControl.setFrame({138 + kindWidth, 98, valueWidth + 20, 20}, {&valueText});
    d::setBackground(valueControl.layer(), d::white(.92));
    refs_.dateKind = &kindLayer;
}

// buildUID (:465-486).
void ProfileCanvas::buildUID(d::Page& page, Layer& main, std::vector<d::Control*>& controls) {
    Layer& prefix = d::makeText(page.builder(), "profile.uid.prefix", "UID ", style::uid, {});
    const double prefixWidth = std::ceil(d::preferredWidth(d::textOf(prefix), env().text));
    d::setLine(prefix, {138, 126, prefixWidth + 1, 13});
    main.add(prefix);
    if (env().recordingMode) {
        // Recording mode (spec §3.26): the Personal ID UID becomes a 120 px bar.
        Layer& bar = d::redaction::bar(page.builder(), "profile.uid", {138 + prefixWidth, 126, 120, 13}, style::uid, 120);
        main.add(bar);
        refs_.uid = &bar;
        return;
    }
    auto& uid = editable(page, ProfileField::playerID, main, controls);
    Layer& valueText = d::makeText(page.builder(), "profile.uid.value", displayed().displayedUID(), style::uid, {});
    const double width = std::ceil(d::preferredWidth(d::textOf(valueText), env().text));
    d::setLine(valueText, {4, 4, width + 1, 13});
    uid.setFrame({138 + prefixWidth - 4, 122, width + 8, 21}, {&valueText});
    refs_.uid = &uid.layer();
}

// buildLevels (:488-545).
void ProfileCanvas::buildLevels(d::Page& page, Layer& main, std::vector<d::Control*>& controls) {
    const auto& p = displayed();
    Layer& marks = page.makeLayer("profile.marks");
    marks.setFrame(bounds(cardSize));
    main.add(marks);
    accentBinders_.push_back([&marks](Color c) {
        d::PathBuilder path;
        for (const double y : {levelYs[0] - 14, countersY - 6, regionY - 2}) path.rect({8, y, 8, 3});
        marks.setContent(d::shape(marks.id(), bounds(cardSize), path, c));
    });
    for (const auto& [field, y, authority] : {std::tuple{ProfileField::permissionLevel, levelYs[0], true}, std::tuple{ProfileField::explorationLevel, levelYs[1], false}}) {
        const std::string id = std::string("profile.level.") + std::string(profileFieldID(field));
        Layer& bar = page.makeLayer(id);
        bar.setFrame({22, y, 300, 34}); d::setBackground(bar, d::black(.56));
        Layer& icon = page.makeLayer(id + ".icon");
        icon.setFrame({8, 5, 26, 24});
        icon.setContent(levelArtwork(icon.id(), authority, {0, 0, 26, 24}, d::white(1)));
        Layer& label = d::makeText(page.builder(), id + ".label", profileFieldTitle(field, env().language), style::levelLabel, {44, 10, 180, 14});
        bar.add(icon); bar.add(label);
        main.add(bar);
        auto& valueControl = editable(page, field, main, controls);
        Layer& valueText = d::makeText(page.builder(), id + ".value", value(field, p), style::levelValue, {}, d::TextAlignment::right);
        const double width = std::ceil(d::preferredWidth(d::textOf(valueText), env().text)) + 8;
        d::setLine(valueText, {4, 2, width - 8, 25});
        valueControl.setFrame({22 + 300 - 12 - width, y + 2.5, width, 29}, {&valueText});
    }
    for (std::size_t index = 0; index < counters.size(); ++index) {
        const auto field = counters[index];
        const std::string id = std::string("profile.counter.") + std::string(profileFieldID(field));
        auto& counter = editable(page, field, main, controls);
        Layer& valueText = d::makeText(page.builder(), id + ".value", value(field, p), style::counterValue(accent()), {9, 6, 78, 27 * 1.05});
        Layer& label = d::makeText(page.builder(), id + ".label", profileFieldTitle(field, env().language), style::counterLabel(accent()), {17, 6 + 27 * 1.05 + 3, 70, 11});
        Layer& bullet = page.makeLayer(id + ".bullet");
        bullet.setFrame({9, 6 + 27 * 1.05 + 3 + 4, 3, 3});
        counter.setFrame({22 + double(index) * 102, countersY, 96, 58}, {&valueText, &bullet, &label});
        d::setBackground(counter.layer(), d::rgb(0x030303, .32));
        accentBinders_.push_back([&valueText, &label, &bullet](Color c) {
            d::editText(valueText, [&](d::TextBlock& b) { b.setColor(c); });
            d::editText(label, [&](d::TextBlock& b) { b.setColor(withAlpha(c, .9)); });
            d::setBackground(bullet, withAlpha(c, .9));
        });
    }
    Layer& region = d::makeText(page.builder(), "profile.region", text("REGION CONSTRUCTION", "地区建设概况"), style::region(accent()), {22, regionY, 300, 11.5});
    accentBinders_.push_back([&region](Color c) { d::editText(region, [&](d::TextBlock& b) { b.setColor(c); }); });
    refs_.hours = plateReadout(page, "profile.work", text("Work Mode", "工作模式"), text("h", "小时"), readoutFrames[0]);
    refs_.best = plateReadout(page, "profile.minigameBest", text("OrbiPom! MERGE! best", "融合！山团团！最高分"), std::nullopt, readoutFrames[1]);
    main.add(region); main.add(*refs_.hours.layer); main.add(*refs_.best.layer);
    setReadout(refs_.hours, formattedHours()); setReadout(refs_.best, formattedBest());
}

// PlateReadout (:1259-1289): the label left, the 24 px tabular value and an
// optional unit flush right, e.g. `25.59 h`.
ProfileCanvas::Readout ProfileCanvas::plateReadout(d::Page& page, std::string_view id, std::string label, std::optional<std::string> unit, Rect frame) {
    Readout r;
    r.layer = &page.makeLayer(id);
    r.layer->setFrame(frame); d::setBackground(*r.layer, d::rgb(0xF5F5F5, .13));
    Layer& titleText = d::makeText(page.builder(), std::string(id) + ".label", std::move(label), style::workLabel, {12, (frame.height - 14.4) / 2, 160, 14.4});
    r.value = &d::makeText(page.builder(), std::string(id) + ".value", "", style::workValue, {});
    r.value->setAnimatable();                           // vChg
    r.layer->add(titleText); r.layer->add(*r.value);
    if (unit) { r.unit = &d::makeText(page.builder(), std::string(id) + ".unit", *unit, style::workUnit, {}); r.layer->add(*r.unit); }
    return r;
}
void ProfileCanvas::setReadout(Readout& r, const std::string& value) {
    const auto size = r.layer->bounds();
    double right = size.width - 12;
    if (r.unit) {
        const double unitWidth = std::ceil(d::preferredWidth(d::textOf(*r.unit), env().text));
        d::setLine(*r.unit, {right - unitWidth, (size.height - 12) / 2, unitWidth + 1, 12});
        right -= unitWidth + 6;
    }
    d::editText(*r.value, [&](d::TextBlock& b) { b.setText(value); });
    const double valueWidth = std::ceil(d::preferredWidth(d::textOf(*r.value), env().text));
    d::setLine(*r.value, {right - valueWidth, (size.height - 24) / 2, valueWidth + 1, 24});
}

// buildIntroduction (:547-600).
void ProfileCanvas::buildIntroduction(d::Page& page, Layer& main, std::vector<d::Control*>& controls) {
    Layer& marks = page.makeLayer("profile.quotes");
    Layer& bars = page.makeLayer("profile.quoteBars");
    Layer& pen = page.makeLayer("profile.pen");
    const Rect box = bounds(cardSize);
    for (Layer* l : {&marks, &bars, &pen}) l->setFrame(box);
    // addQuote (:586-600): two 4.5-unit strokes with a short tail in a 13.5 x
    // 10 box, x 1.4; the opening mark is the closing one turned 180 degrees.
    d::PathBuilder glyphs, accentBars;
    const auto addQuote = [&](Point origin, bool closing) {
        constexpr std::array<Point, 7> points{{{1, 0}, {5.5, 0}, {5.5, 6}, {3.2, 10}, {1.6, 10}, {2.9, 6}, {1, 6}}};
        const auto place = [&](Point q) { return Point{origin.x + q.x * quoteScale, origin.y + q.y * quoteScale}; };
        for (const double shift : {0., 7.}) {
            bool first = true;
            for (const auto& q : points) {
                const Point moved{q.x + shift, q.y};
                const Point at = place(closing ? moved : Point{quoteBox.width - moved.x, quoteBox.height - moved.y});
                if (first) glyphs.move(at); else glyphs.line(at);
                first = false;
            }
            glyphs.close();
        }
        accentBars.rect({origin.x + (closing ? quoteBox.width - 6 : 0) * quoteScale, origin.y + 13 * quoteScale, 6 * quoteScale, 1.5 * quoteScale});
    };
    addQuote({introFrame.x, 166}, false);
    addQuote({introFrame.x + introFrame.width - quoteBox.width * quoteScale, 305}, true);
    marks.setContent(d::shape(marks.id(), box, glyphs, d::white(.94)));
    d::PathBuilder edit;
    edit.move({544, 182}).line({556, 170}).line({560, 174}).line({548, 186}).line({543, 187}).close();
    edit.move({541, 190}).line({561, 190});
    d::StrokeStyle stroke; stroke.width = 1.3;
    pen.setContent(d::shape(pen.id(), box, edit, std::nullopt, ink, stroke));
    for (Layer* l : {&marks, &bars, &pen}) main.add(*l);
    accentBinders_.push_back([&bars, box, accentBars](Color c) { bars.setContent(d::shape(bars.id(), box, accentBars, c)); });
    auto& intro = editable(page, ProfileField::introduction, main, controls);
    const auto& introduction = displayed().introduction;
    Layer& textLayer = d::makeText(page.builder(), "profile.intro.text", introduction.empty() ? "…" : introduction, style::intro, {});
    const int maximumLines = int((introFrame.height - 8) / (13 * 1.5));
    d::editText(textLayer, [&](d::TextBlock& b) { b.maximumLines = maximumLines; });
    d::setLine(textLayer, {6, 4, introFrame.width - 12, double(maximumLines) * 13 * 1.5});
    intro.setFrame(introFrame, {&textLayer});
    intro.label = text("Edit introduction", "编辑个人介绍");
}

// buildToolbar (:602-623): right 20, bottom 16, gap 10: Card theme, divider, eye.
void ProfileCanvas::buildToolbar(d::Page& page, Layer& card, std::vector<d::Control*>& controls) {
    auto& eye = page.make<ProfileCircleButton>("profile:visibility", "", ProfileCircleButton::Style::eye);
    const Rect eyeFrame{cardSize.width - 20 - 32, cardSize.height - 16 - 32, 32, 32};
    Layer& divider = page.makeLayer("profile.toolbar.divider");
    auto& theme = page.make<ProfileThemeButton>("profile:backgroundMenu", text("Card theme", "更换名片主题"));
    const double themeWidth = theme.layer().bounds().width;
    eye.layer().setFrame(eyeFrame);
    divider.setFrame({eyeFrame.x - 10 - 1.3, eyeFrame.y + eyeFrame.height / 2 - 9, 1.3, 18});
    d::setBackground(divider, d::rgb(0xF5F5F5, .74));
    theme.layer().setFrame({divider.frame().x - 10 - themeWidth, eyeFrame.y + eyeFrame.height / 2 - 15, themeWidth, 30});
    for (Layer* l : {&theme.layer(), &divider, &eye.layer()}) card.add(*l);
    eye.onActivate = [this] { toggleTextHidden(); };
    theme.onActivate = [this, &theme] { if (popoverOpen()) closePopover(); else openThemePopover(theme); };
    refs_.eye = &eye; refs_.theme = &theme;
    controls.push_back(&theme); controls.push_back(&eye);
}

// editable (:625-635).
ProfileEditable& ProfileCanvas::editable(d::Page& page, ProfileField field, Layer& main, std::vector<d::Control*>& controls) {
    auto& control = page.make<ProfileEditable>("profile:" + std::string(profileFieldID(field)),
        profileFieldTitle(field, env().language) + ": " + value(field, displayed()));
    control.setEnabled(canEdit(field) && !textHidden_);
    control.onActivate = [this, field] { beginEdit(field); };
    main.add(control.layer());
    accentBinders_.push_back([&control](Color c) { control.setAccent(c); });
    refs_.values[field] = &control.layer();
    refs_.editables[field] = &control;
    controls.push_back(&control);
    return control;
}

// --- Accent, geometry, visibility (:637-733) ---------------------------------------------------
// Follow HUD uses the HUD accent; otherwise the card's own colour. It tints
// the frame, marks, counters, region label, quote bars, chevrons and editors.
void ProfileCanvas::applyAccent() {
    const auto color = accent();
    if (refs_.frame) {
        if (const auto frame = services_.frame ? services_.frame(resolvedAccent()) : std::nullopt)
            refs_.frame->setContent(d::image(refs_.frame->id(), d::localBounds(*refs_.frame), *frame, "resize"));
        else refs_.frame->clearContent();
    }
    for (auto& binder : accentBinders_) binder(color);
    for (auto& binder : popoverBinders_) binder();
}
// preparePortrait's key: one image per integer zoom step (ceil(zoom - .001)).
int ProfileCanvas::portraitStep() const { return int(std::ceil(std::min(20., std::max(1., displayed().avatarZoom)) - .001)); }
void ProfileCanvas::layoutGeometry(bool animated) { layoutArt(animated); layoutPortrait(animated); }

// layoutArt (:654-664): the box at artBoxFrame (in host coordinates), the
// zoom scaling the picture inside it (.2 s E). The box keeps its model size
// (artReference): its frame is a translation of its centre and a scale.
void ProfileCanvas::layoutArt(bool animated) {
    if (!refs_.artBox || !refs_.artImage) return;
    const auto& p = displayed();
    const double duration = animated ? .2 : 0;
    const auto host = artHostFrame();
    auto frame = artBoxFrame(p);
    frame.x += artFrame.x - host.x; frame.y += artFrame.y - host.y;
    const double zoom = std::min(20., std::max(1., p.backgroundZoom));
    const double scale = frame.width / artReference.width;
    d::motion::animate(*refs_.artBox, d::Channel::translateX, frame.x + frame.width / 2, duration, clock());
    d::motion::animate(*refs_.artBox, d::Channel::translateY, frame.y + frame.height / 2, duration, clock());
    d::motion::animate(*refs_.artBox, d::Channel::scaleX, scale, duration, clock());
    d::motion::animate(*refs_.artBox, d::Channel::scaleY, scale, duration, clock());
    d::motion::animate(*refs_.artImage, d::Channel::scaleX, zoom, duration, clock());
    d::motion::animate(*refs_.artImage, d::Channel::scaleY, zoom, duration, clock());
}

// layoutPortrait / preparePortrait (:672-708): the portrait pans and zooms
// with a layer transform using the shell card's crop semantics; the default
// silhouette moves as the demo's translate(X x .41, Y x .41) scale(Z). The
// surface is authored at the zoom step (bounds x step, scale zoom / step).
void ProfileCanvas::layoutPortrait(bool animated) {
    const auto& p = displayed();
    const double side = wellFrame.width, duration = animated ? .2 : 0;
    const double zoom = std::min(20., std::max(1., p.avatarZoom));
    const double x = std::min(1., std::max(-1., p.avatarOffsetX)), y = std::min(1., std::max(-1., p.avatarOffsetY));
    const int step = portraitStep();
    // A new representation (another step's raster): the presented scale is
    // converted at once so the glide continues from what shows.
    const auto represent = [&](Layer& layer, int from, int to) {
        if (from == to || from <= 0) return;
        for (const auto channel : {d::Channel::scaleX, d::Channel::scaleY})
            layer.setValue(channel, layer.presented(channel, now()) * from / to);
    };
    if (refs_.portrait) {
        const auto rendered = services_.portrait ? services_.portrait(step) : ProfileCanvasServices::Portrait{};
        if (!rendered.image) return;
        const auto& image = *rendered.image;
        const int previous = refs_.shownPortrait ? refs_.shownPortrait->step : 0;
        if (refs_.shownPortrait != image) {
            refs_.portrait->setBounds({image.fillWidth * side * image.step, image.fillHeight * side * image.step});
            refs_.portrait->setContent(d::image(refs_.portrait->id(), d::localBounds(*refs_.portrait), image.image, "resize"));
            represent(*refs_.portrait, previous, image.step);
            refs_.shownPortrait = image;
        }
        const Point shift{-x * (zoom * image.fillWidth - 1) * side / 2, -y * (zoom * image.fillHeight - 1) * side / 2};
        for (const auto channel : {d::Channel::scaleX, d::Channel::scaleY}) d::motion::animate(*refs_.portrait, channel, zoom / image.step, duration, clock());
        d::motion::animate(*refs_.portrait, d::Channel::translateX, shift.x, duration, clock());
        d::motion::animate(*refs_.portrait, d::Channel::translateY, shift.y, duration, clock());
    } else if (refs_.silhouette) {
        if (refs_.silhouetteStep != step) {
            const double k = side / 55 * step;
            d::PathBuilder path;
            path.ellipse({(28.05 - 8.25) * k, (19.5 - 8.5) * k, 16.5 * k, 17 * k});
            path.move({9.9 * k, 47.3 * k}).cubic({13.2 * k, 24.75 * k}, {42.35 * k, 24.75 * k}, {45.65 * k, 47.3 * k}).close();
            refs_.silhouette->setBounds({side * step, side * step});
            refs_.silhouette->setContent(d::shape(refs_.silhouette->id(), {0, 0, side * step, side * step}, path, d::rgb(0xF5F5F5, .63)));
            represent(*refs_.silhouette, refs_.silhouetteStep, step);
            refs_.silhouetteStep = step;
        }
        for (const auto channel : {d::Channel::scaleX, d::Channel::scaleY}) d::motion::animate(*refs_.silhouette, channel, zoom / step, duration, clock());
        d::motion::animate(*refs_.silhouette, d::Channel::translateX, x * 41, duration, clock());
        d::motion::animate(*refs_.silhouette, d::Channel::translateY, y * 41, duration, clock());
    }
}

// renderVisibility (:712-726): .main fades (.22 s), the overlay (.2 s) and the
// card fill and ring (.24 s); only the art and the toolbar remain.
void ProfileCanvas::renderVisibility(bool animated) {
    const bool hidden = textHidden_;
    if (refs_.main) d::motion::animate(*refs_.main, d::Channel::opacity, hidden ? 0 : 1, animated ? .22 : 0, clock(), d::easing::ease);
    if (refs_.overlay) d::motion::animate(*refs_.overlay, d::Channel::opacity, hidden ? 0 : 1, animated ? .2 : 0, clock(), d::easing::ease);
    for (Layer* layer : refs_.plate) d::motion::animate(*layer, d::Channel::opacity, hidden ? 0 : 1, animated ? .24 : 0, clock(), d::easing::ease);
    if (refs_.eye) {
        refs_.eye->setSlashed(!hidden);
        refs_.eye->label = hidden ? text("Show text", "显示文字") : text("Show background", "显示背景");
    }
    for (auto& [field, control] : refs_.editables) control->setEnabled(!hidden && canEdit(field));
    if (refs_.more) refs_.more->setEnabled(!hidden);
}
void ProfileCanvas::toggleTextHidden() {
    endEditing(true); closePopups();
    textHidden_ = !textHidden_;
    renderVisibility(true);
    if (context().onInputStateChange) context().onInputStateChange();
}
void ProfileCanvas::toggleDateKind() {
    if (!store_ || textHidden_) return;
    try { store_->update([](PersonalProfile& p) { p.showsBirthday = !p.showsBirthday; }); }
    catch (const std::exception& e) { showError(profileErrorText(e, env().language)); }
}

// --- Inline editors (:742-878) ------------------------------------------------------------------
// Opens the inline editor exactly over the element: #212121, accent ring and
// bar, at least 220 wide for name and #, 60 otherwise.
void ProfileCanvas::beginEdit(ProfileField field) {
    if (textHidden_ || !canEdit(field) || (field == ProfileField::playerID && env().recordingMode) || !refs_.card) return;
    const auto anchorIt = refs_.editables.find(field == ProfileField::tag ? ProfileField::name : field);
    if (anchorIt == refs_.editables.end()) return;
    endEditing(true); closePopups();
    const Rect rect = anchorIt->second->layer().frame();
    const bool wide = field == ProfileField::name || field == ProfileField::tag, intro = field == ProfileField::introduction;
    const d::Size size{std::max(rect.width, wide ? 220. : 60.), rect.height};
    d::TextFieldOptions options;
    options.multiline = intro;
    options.maximumLength = profileTextLimit(field);
    options.selectsAllOnBegin = true;
    options.style = d::TextStyle{intro ? 13 : std::min(22., std::max(12., rect.height * .55)), d::FontWeight::bold, ink, intro ? 1.5 : 1};
    options.inset = {8, intro ? 6. : 0.};
    options.accent = accent(); options.background = d::rgb(0x212121);
    options.alignment = profileNumericField(field) ? d::TextAlignment::right : d::TextAlignment::left;
    const auto initial = field == ProfileField::tag ? displayed().tag : value(field, displayed());
    auto& editor = page().make<d::TextField>("edit:" + std::string(profileFieldID(field)), profileFieldTitle(field, env().language), initial, "", size, options);
    editor.layer().setOrigin({rect.x, rect.y});
    editor.layer().setZPosition(d::order::menu);
    editor.layer().setAnimatable();
    editor.layer().setValue(d::Channel::opacity, 0);
    refs_.card->add(editor.layer());
    d::motion::animate(editor.layer(), d::Channel::opacity, 1, .14, clock(), d::easing::easeOut);
    editor.onChange = [this, field, &editor](const std::string& t) { editor.setInvalid(!isValid(field, t)); };
    editor.onCommit = [this, field](const std::string& t) { finishEdit(field, t); };
    editor.onCancel = [this, field] { finishEdit(field, std::nullopt); };
    editing_ = Editing{field, &editor};
    registerField(editor);
    refreshRouter();
    editor.beginEditing();
}
// Enter, Tab and a press elsewhere commit; Esc cancels. Invalid values are
// discarded; a saved value re-renders the card and plays vChg.
void ProfileCanvas::finishEdit(ProfileField field, std::optional<std::string> raw) {
    if (!editing_ || editing_->field != field) return;
    const auto current = *editing_;
    editing_.reset();
    current.text->layer().removeFromParent();
    refreshRouter();
    if (raw && isValid(field, *raw) && *raw != (field == ProfileField::tag ? displayed().tag : value(field, displayed()))) commit(field, *raw);
    if (needsRerender_) { needsRerender_ = false; rerender(); }
    if (context().onInputStateChange) context().onInputStateChange();
}
// isValid (:794-813).
bool ProfileCanvas::isValid(ProfileField field, std::string_view raw, const ProfileTextRules& text_, const ProfileDateRules* dates) {
    const auto value = text_.trim(raw, true);
    const auto digits = [](std::string_view v) { return !v.empty() && std::all_of(v.begin(), v.end(), [](char c) { return c >= '0' && c <= '9'; }); };
    const auto number = [&](std::int64_t lo, std::int64_t hi) {
        if (!digits(value)) return false;
        const auto n = swiftInteger(value);
        return n && *n >= lo && *n <= hi;
    };
    switch (field) {
    case ProfileField::name: return !value.empty() && text_.characters(value) <= 20 && !text_.hasControl(value);
    case ProfileField::tag: {
        const std::string_view tagDigits = value.starts_with('#') ? std::string_view(value).substr(1) : std::string_view(value);
        return !tagDigits.empty() && tagDigits.size() <= 10 && digits(tagDigits);
    }
    case ProfileField::playerID: return text_.characters(value) <= 64;
    case ProfileField::introduction: return text_.characters(raw) <= 150;
    case ProfileField::awakeningDate: return dates && profileParseAwakeningDate(value, *dates).has_value();
    case ProfileField::birthday: return profileParseBirthday(value).has_value();
    case ProfileField::permissionLevel: return number(1, 60);
    case ProfileField::explorationLevel: return number(1, 7);
    case ProfileField::operatorsCount: case ProfileField::weaponsCount: case ProfileField::archivesCount: return number(0, 99'999);
    default: return false;
    }
}
// commit (:815-840).
void ProfileCanvas::commit(ProfileField field, const std::string& raw) {
    if (!canEdit(field)) return;
    if (!store_) { showError(core::localized("Profile storage is unavailable.", "个人名片存储不可用。", env().language)); return; }
    const auto input = text_.trim(raw, true);
    errorMessage_.reset();
    try {
        store_->update([&](PersonalProfile& p) {
            switch (field) {
            case ProfileField::playerID: p.playerIDOverride = input; break;
            case ProfileField::name: p.name = input; break;
            case ProfileField::tag: p.tag = input.starts_with('#') ? input.substr(1) : input; break;
            case ProfileField::introduction: p.introduction = input; break;
            case ProfileField::awakeningDate:
                if (const auto date = profileParseAwakeningDate(input, dates_)) { p.awakeningDate = *date; p.hasManualAwakeningDate = true; }
                break;
            case ProfileField::birthday:
                if (const auto day = profileParseBirthday(input)) { p.birthdayMonth = day->first; p.birthdayDay = day->second; }
                break;
            case ProfileField::permissionLevel: p.permissionLevel = int(swiftInteger(input).value_or(p.permissionLevel)); break;
            case ProfileField::explorationLevel: p.explorationLevel = int(swiftInteger(input).value_or(p.explorationLevel)); break;
            case ProfileField::operatorsCount: p.operatorsCount = swiftInteger(input).value_or(p.operatorsCount); break;
            case ProfileField::weaponsCount: p.weaponsCount = swiftInteger(input).value_or(p.weaponsCount); break;
            case ProfileField::archivesCount: p.archivesCount = swiftInteger(input).value_or(p.archivesCount); break;
            default: break;
            }
        });
    } catch (const std::exception& e) { showError(profileErrorText(e, env().language)); }
}
// vChg .2 s E: the committed value fades up 3 px (playValueChange :880-886).
void ProfileCanvas::playValueChange(Layer& layer) {
    if (env().reduceMotion || !layer.animatable()) return;
    const double o = layer.value(d::Channel::opacity);
    const std::array<double, 2> fade{.3 * o, o}, rise{3, 0}, times{0, 1};
    d::motion::keyframes(layer, d::Channel::opacity, fade, times, .2, clock(), d::easing::standard);
    d::motion::keyframes(layer, d::Channel::translateY, rise, times, .2, clock(), d::easing::standard, 1, true);
}

// --- Menus and pop-overs (:890-1018) ------------------------------------------------------------
void ProfileCanvas::openMoreMenu(const d::Control& anchor) {
    const auto item = [](std::string id, std::string title, bool enabled = true) { d::Menu::Item i; i.id = std::move(id); i.title = std::move(title); i.enabled = enabled; return i; };
    std::vector<d::Menu::Item> items{
        item("name", text("Edit name", "修改名称"), canEdit(ProfileField::name)),
        item("tag", text("Edit #", "修改 #"), canEdit(ProfileField::tag)),
        item("picture", text("Change profile picture", "更换头像"), store_ != nullptr),
        item("portrait", text("Adjust portrait", "调整头像")),
        item("restore", text("Restore default picture", "恢复默认头像"), store_ != nullptr)};
    openMenu("profile.more", std::move(items), anchor, std::nullopt, [this](const d::Menu::Item& picked) {
        if (picked.id == "name") beginEdit(ProfileField::name);
        else if (picked.id == "tag") beginEdit(ProfileField::tag);
        else if (picked.id == "picture") chooseImage(ProfileImageKind::avatar);
        else if (picked.id == "portrait") { if (refs_.more) openPortraitPopover(*refs_.more); }
        else restoreAvatar();
    });
}
void ProfileCanvas::restoreAvatar() {
    if (!store_) return;
    try {
        store_->removeImage(ProfileImageKind::avatar);
        toast(d::Icon::reset, "Default picture", "已恢复默认头像");
    } catch (const std::exception& e) { showError(profileErrorText(e, env().language)); }
}
// Card theme + colour (350 wide, above the button): presets, custom colour,
// Follow HUD, Background / Default, then the art and thumbnail sliders.
void ProfileCanvas::openThemePopover(const d::Control& anchor) {
    const auto lines = title("CARD THEME", "名片主题").lines(env().language);
    const double width = 350, inner = width - 24;
    const double height = 36 + 32 + double(geometryFields.size()) * 34 + 17;
    closePopups();                               // openPopover would; the side arena must outlive this pop-over only
    auto popover = std::make_unique<d::Popover>(env(), lines.title, lines.subtitle, width, height);
    auto& b = popover->builder();
    std::vector<d::Control*> controls;
    popoverBinders_.clear();
    side_ = std::make_unique<ProfileSideArena>(env());
    trackArena(&side_->arena);
    for (std::size_t index = 0; index < settingsPresetAccents.size(); ++index) {
        const std::string hex(settingsPresetAccents[index]);
        auto& swatch = b.make<d::Swatch>("theme:" + hex, text("Card color ", "名片颜色 ") + "#" + hex, hexColor(hex).value_or(env().palette.yellow()));
        swatch.layer().setOrigin({double(index) * 32, 2});
        swatch.onActivate = [this, hex] { setThemeColor(hex); };
        popover->content().add(swatch.layer());
        popoverBinders_.push_back([this, &swatch, hex] { swatch.setSelected(displayed().themeColorHex == hex); });
        controls.push_back(&swatch);
    }
    auto& custom = b.make<d::Swatch>("theme:custom", text("Custom card color", "自定名片颜色"), std::nullopt);
    custom.layer().setOrigin({double(settingsPresetAccents.size()) * 32, 2});
    custom.onActivate = [this] { chooseColor(resolvedAccent()); };
    popover->content().add(custom.layer());
    popoverBinders_.push_back([this, &custom] {
        const auto& hex = displayed().themeColorHex;
        custom.setSelected(hex && std::find(settingsPresetAccents.begin(), settingsPresetAccents.end(), *hex) == settingsPresetAccents.end());
    });
    controls.push_back(&custom);
    for (const auto kind : {d::Button::Kind::primary, d::Button::Kind::secondary}) {
        auto& builder = kind == d::Button::Kind::primary ? b : side_->builder;
        auto& follow = builder.make<d::Button>("theme:follow", text("Follow HUD", "跟随浮层"), std::nullopt, kind, d::Button::Height::compact);
        follow.place({0, 0});
        follow.layer().setOrigin({inner - follow.layer().bounds().width, 1});
        follow.onActivate = [this] { toggleFollow(); };
        popover->content().add(follow.layer());
        popoverBinders_.push_back([this, &follow, kind] { follow.layer().setHidden((!displayed().themeColorHex) != (kind == d::Button::Kind::primary)); });
        controls.push_back(&follow);
    }
    const double half = (inner - 6) / 2;
    auto& background = b.make<d::Button>("theme:background", text("Background", "背景"), d::Icon::image, d::Button::Kind::secondary, d::Button::Height::compact);
    background.place({0, 36}, half);
    background.onActivate = [this] { chooseImage(ProfileImageKind::background); };
    auto& reset = b.make<d::Button>("theme:default", text("Default", "默认"), d::Icon::reset, d::Button::Kind::secondary, d::Button::Height::compact);
    reset.place({half + 6, 36}, half);
    reset.onActivate = [this] { resetBackground(); };
    popover->content().add(background.layer()); popover->content().add(reset.layer());
    controls.push_back(&background); controls.push_back(&reset);
    double y = 68;
    for (std::size_t index = 0; index < geometryFields.size(); ++index) {
        if (index == 4) {
            Layer& separator = b.layer("popover.separator");
            separator.setFrame({0, y + 8, inner, 1}); d::setBackground(separator, env().palette.token(d::ThemeToken::separator));
            popover->content().add(separator);
            y += 17;
        }
        controls.push_back(&sliderRow(*popover, geometryFields[index], y, inner));
        y += 34;
    }
    for (auto& binder : popoverBinders_) binder();
    openPopover(std::move(popover), &anchor, std::nullopt, std::move(controls));
}
// The portrait pop-up (330 wide). It opens below ⋯ so the portrait stays visible.
void ProfileCanvas::openPortraitPopover(const d::Control& anchor) {
    const auto lines = title("PORTRAIT", "调整头像").lines(env().language);
    const double width = 330;
    auto popover = std::make_unique<d::Popover>(env(), lines.title, lines.subtitle, width, 3 * 34);
    popoverBinders_.clear();
    std::vector<d::Control*> controls;
    for (std::size_t index = 0; index < portraitFields.size(); ++index) controls.push_back(&sliderRow(*popover, portraitFields[index], double(index) * 34, width - 24));
    d::Popover* raw = popover.get();
    openPopover(std::move(popover), &anchor, std::nullopt, std::move(controls));
    const auto more = anchor.rect(root());
    raw->layer().setOrigin({22, std::min(more.y + more.height + 8, d::metric::box.height - raw->layer().bounds().height)});
}
// `.prow` (:999-1018): label 118, dark slider, `[ value ]` (min-width 52).
d::Slider& ProfileCanvas::sliderRow(d::Popover& popover, ProfileField field, double y, double width) {
    auto& b = popover.builder();
    const auto id = std::string(profileFieldID(field));
    Layer& label = d::makeText(b, "popover." + id + ".label", profileFieldTitle(field, env().language), style::popoverLabel, {0, y + 11, 118, 12});
    const auto [range, format] = sliderSpec(field);
    auto& slider = b.make<d::Slider>("profile:" + id, profileFieldTitle(field, env().language), profileGeometryValue(field, displayed()), range, format, width - 126, true);
    slider.layer().setOrigin({126, y + 2});
    popover.content().add(label); popover.content().add(slider.layer());
    hostBubble(slider);
    slider.onChange = [this, field](double value, bool final) { sliderChanged(field, value, final); };
    popoverBinders_.push_back([this, &slider, field] { if (!slider.dragging()) slider.setValue(profileGeometryValue(field, displayed())); });
    return slider;
}
// Sliders repaint live from an in-memory preview and save once on release (:1032-1048).
void ProfileCanvas::sliderChanged(ProfileField field, double value, bool final) {
    auto next = displayed();
    profileApplyGeometry(field, value, next);
    preview_ = next;
    layoutGeometry(true);
    if (services_.geometryPreview) services_.geometryPreview(next);
    if (!final) return;
    preview_.reset();
    if (!store_) return;
    try {
        store_->update([&](PersonalProfile& saved) {
            for (const auto f : geometryFields) profileApplyGeometry(f, profileGeometryValue(f, next), saved);
            for (const auto f : portraitFields) profileApplyGeometry(f, profileGeometryValue(f, next), saved);
        });
    } catch (const std::exception& e) {
        showError(profileErrorText(e, env().language)); layoutGeometry(true);
        if (services_.geometryPreview) services_.geometryPreview(profile_);
    }
}
void ProfileCanvas::setThemeColor(std::optional<std::string> hex) {
    if (!store_) return;
    if (const auto& current = displayed().themeColorHex) lastThemeHex_ = *current;
    try { store_->update([&](PersonalProfile& p) { p.themeColorHex = std::move(hex); }); }
    catch (const std::exception& e) { showError(profileErrorText(e, env().language)); }
}
// On: the HUD accent; off: the card's last own colour (default #C9A2FF).
void ProfileCanvas::toggleFollow() { setThemeColor(displayed().themeColorHex ? std::nullopt : std::optional(lastThemeHex_)); }
void ProfileCanvas::setCustomColor(std::array<double, 3> c) {
    std::array<long long, 3> v{};
    for (std::size_t i = 0; i < 3; ++i) { if (!std::isfinite(c[i])) return; v[i] = std::llround(c[i] * 255); }
    char s[40]{};
    std::snprintf(s, sizeof s, "%02llX%02llX%02llX", v[0], v[1], v[2]);
    setThemeColor(std::string(s));
}
// Default: the art is removed and every background and thumbnail value reset.
void ProfileCanvas::resetBackground() {
    if (!store_) return;
    try { store_->removeImage(ProfileImageKind::background); } catch (const std::exception& e) { showError(profileErrorText(e, env().language)); }
}
void ProfileCanvas::imageImported(ProfileImageKind kind) {
    if (kind == ProfileImageKind::avatar) toast(d::Icon::user, "Picture changed", "已更换头像");
}

// --- File chooser and colour panel (:1113-1174) ---------------------------------------------------
void ProfileCanvas::chooseImage(ProfileImageKind kind) {
    if (chooser_ || colorPanel_ || !services_.chooseImage) return;
    endEditing(true);
    if (context().onLockParallax) context().onLockParallax();
    ++dialogGeneration_;
    if (!services_.chooseImage(kind)) return;
    chooser_ = kind;
    if (context().onInputStateChange) context().onInputStateChange();
}
void ProfileCanvas::chooserFinished(ProfileImageKind kind, std::optional<std::string> path) {
    if (!chooser_ || *chooser_ != kind) return;  // a chooser the module left (dialogGeneration)
    chooser_.reset();
    if (context().onInputStateChange) context().onInputStateChange();
    if (!isActive()) return;
    if (!path) return;
    if (!store_) { showError(core::localized("Profile storage is unavailable.", "个人名片存储不可用。", env().language)); return; }
    if (services_.importImage) (void)services_.importImage(kind, *path);
}
void ProfileCanvas::chooseColor(std::array<double, 3> initial) {
    if (colorPanel_ || chooser_ || !services_.chooseColor) return;
    endEditing(true);
    if (context().onLockParallax) context().onLockParallax();
    if (!services_.chooseColor(initial)) return;
    colorPanel_ = true;
    if (context().onInputStateChange) context().onInputStateChange();
}
void ProfileCanvas::colorChanged(std::array<double, 3> c) { if (isActive() && colorPanel_) setCustomColor(c); }
void ProfileCanvas::colorPanelClosed() {
    if (!colorPanel_) return;
    colorPanel_ = false;
    if (context().onInputStateChange) context().onInputStateChange();
}

// What the visible controls do (UIA invoke, hidden coverage).
bool ProfileCanvas::perform(std::string_view id, double now) {
    return performAccessibilityAction(id, now);
}
} // namespace endfield::modules
