#include "modules/now_playing_canvas.hpp"
#include "modules/now_playing_lyrics_sources.hpp"
#include <algorithm>
#include <cmath>

namespace endfield::modules {
namespace d = design;
using d::Channel;
using d::Point;
using d::Rect;
namespace {
bool contains(Rect r, Point p) noexcept { return p.x >= r.x && p.y >= r.y && p.x < r.x + r.width && p.y < r.y + r.height; }
void removeMotion(d::Layer& layer) { layer.removeAllMotion(); for (auto* child : layer.children()) removeMotion(*child); }
// Hatch-L (spec section 1.6) as one vector path: 1 px stripes 3.5 px apart at -45 degrees (:277-288).
d::PathBuilder hatch(d::Size size) {
    d::PathBuilder path;
    const double spacing = 3.5 * std::sqrt(2.0), thickness = std::sqrt(2.0);
    for (double offset = -size.height; offset < size.width; offset += spacing)
        path.move({offset, size.height}).line({offset + thickness, size.height}).line({offset + thickness + size.height, 0}).line({offset + size.height, 0}).close();
    return path;
}
d::Layer& textLayer(d::Builder& b, std::string_view id, std::string value, const d::TextStyle& style, Rect frame, d::TextAlignment alignment = d::TextAlignment::left) {
    return d::makeText(b, id, std::move(value), style, frame, alignment);
}
}

std::string NowPlayingCanvas::time(std::optional<double> value) { return nowPlayingTime(value); }

// NowPlayingLyricWindow (:613-682): line k sits in the middle slot at 14 px,
// its neighbours at 12.5 px and .36, the rest hidden. Only k +/- 2 have
// layers; a cue change slides them one slot (.22 s ease-in-out) and eases
// opacity and size. Rows are reused (a page arena never shrinks).
class NowPlayingCanvas::LyricWindow {
public:
    static constexpr double lineHeight = 20;
    static d::TextStyle regular(const d::Palette& p) { return {12.5, d::FontWeight::regular, p.tone(0xF0F0F0)}; }
    static d::TextStyle current(const d::Palette& p) { return {14, d::FontWeight::medium, p.tone(0xF0F0F0)}; }
    LyricWindow(d::Builder& b, Rect frame) : b_(&b) {
        layer_ = &b.layer("nowPlaying.lyrics");
        layer_->setAnimatable();                    // the .24 s fade of the whole block
        layer_->setFrame(frame); layer_->setMasksToBounds(true); layer_->setValue(Channel::opacity, 0);
    }
    d::Layer& layer() const noexcept { return *layer_; }
    void setLyrics(std::shared_ptr<const NowPlayingLyrics> next) {
        if (next == lyrics_) return;
        lyrics_ = std::move(next); index_.reset();
        for (auto& [line, row] : rows_) { row->removeFromParent(); pool_.push_back(row); }
        rows_.clear();
    }
    void show(double elapsed, bool animated) {
        if (!lyrics_) return;
        const auto& p = b_->env.palette;
        const auto clock = b_->env.clock();
        const auto next = lyrics_->currentIndex(elapsed);
        if (index_ == next) return;
        const auto previous = index_;
        index_ = next;
        const bool slide = animated && !clock.reduceMotion && previous && std::abs(*previous - next) <= 2;
        const auto count = std::ptrdiff_t(lyrics_->lines().size());
        for (auto it = rows_.begin(); it != rows_.end();) {
            if (it->first >= next - 2 && it->first <= next + 2) { ++it; continue; }
            it->second->removeFromParent(); pool_.push_back(it->second);
            it = rows_.erase(it);
        }
        for (auto line = std::max<std::ptrdiff_t>(0, next - 2); line <= next + 2 && line < count; ++line) {
            const auto found = rows_.find(line);
            d::Layer* existing = found == rows_.end() ? nullptr : found->second;
            const auto distance = std::abs(line - next);
            const auto style = distance == 0 ? current(p) : regular(p);
            const double opacity = distance == 0 ? 1 : distance == 1 ? .36 : 0;
            // Where the line was before this cue (a new row starts there, hidden).
            std::optional<double> fromY;
            if (existing) fromY = existing->position().y + existing->presented(Channel::translateY, clock.now);
            else if (previous) fromY = lineHeight * double(line - *previous + 1) + lineHeight / 2;
            const double fromOpacity = existing ? existing->presented(Channel::opacity, clock.now) : 0;
            const double fromSize = existing ? d::textOf(*existing).style().size : style.size;
            d::Layer* row = existing;
            if (!row) row = take(lyrics_->lines()[std::size_t(line)].text, style);
            d::editText(*row, [&](d::TextBlock& block) { block.setStyle(style); });
            d::setLine(*row, {0, lineHeight * double(line - next + 1), layer_->bounds().width, lineHeight});
            row->setValue(Channel::translateY, 0); row->setValue(Channel::scaleX, 1); row->setValue(Channel::scaleY, 1);
            row->setValue(Channel::opacity, opacity);
            if (!existing) { layer_->add(*row); rows_[line] = row; }
            if (!slide || !fromY) continue;
            const double toY = row->position().y;
            row->setValue(Channel::translateY, *fromY - toY);
            d::motion::animate(*row, Channel::translateY, 0, .22, clock, d::easing::easeInOut);
            row->setValue(Channel::opacity, fromOpacity);
            d::motion::animate(*row, Channel::opacity, opacity, .22, clock, d::easing::easeInOut);
            if (fromSize != style.size) {
                row->setValue(Channel::scaleX, fromSize / style.size); row->setValue(Channel::scaleY, fromSize / style.size);
                d::motion::animate(*row, Channel::scaleX, 1, .22, clock, d::easing::easeInOut);
                d::motion::animate(*row, Channel::scaleY, 1, .22, clock, d::easing::easeInOut);
            }
        }
    }
private:
    d::Builder* b_;
    d::Layer* layer_;
    std::shared_ptr<const NowPlayingLyrics> lyrics_;
    std::optional<std::ptrdiff_t> index_;
    std::map<std::ptrdiff_t, d::Layer*> rows_;
    std::vector<d::Layer*> pool_;
    std::size_t serial_{};
    d::Layer* take(const std::string& value, const d::TextStyle& style) {
        if (!pool_.empty()) {
            auto* row = pool_.back(); pool_.pop_back();
            d::editText(*row, [&](d::TextBlock& block) { block.setText(value); });
            return row;
        }
        auto& row = textLayer(*b_, "nowPlaying.lyric." + std::to_string(serial_++), value, style, {}, d::TextAlignment::center);
        row.setAnimatable();
        return &row;
    }
};

// NowPlayingSeekBar (:684-793): the dark slider at 22 px with a 3 px track,
// a 13 px thumb and no ladder. Dragging previews (the bubble shows mm:ss);
// release commits once. Between clock ticks the bar runs linearly.
class NowPlayingCanvas::SeekBar final : public d::Control {
public:
    SeekBar(d::Builder& b, double width) : Control(b.env, "seek", core::localized("Playback position", "播放进度", b.env.language), b.layer("seek")), b_(&b), width_(width) {
        const auto& p = palette();
        auto& l = layer();
        l.setBounds({width, 22});
        auto& track = b.layer("seek.track");
        track.setFrame({0, 8, width, 3}); d::setBackground(track, p.token(d::ThemeToken::ringField));
        // The fill: the gradient across the full width, scaled from the left
        // (the CAGradientLayer spans its own width, so the stops scale with it).
        fill_ = &b.layer("seek.fill");
        fill_->setAnimatable();
        fill_->setAnchorPoint({0, .5}); fill_->setBounds({width, 3}); fill_->setPosition({0, 9.5});
        const std::array<d::Color, 2> colors{p.legible(p.yellowLow()), p.accentInk()};
        fill_->setContent(d::gradient(fill_->id(), {0, 0, width, 3}, colors, {}, {0, .5}, {1, .5}));
        fill_->setValue(Channel::scaleX, 0);
        thumb_ = &b.layer("seek.thumb");
        thumb_->setAnimatable();
        thumb_->setBounds({9, 13}); thumb_->setPosition({0, 9.5});
        d::setBackground(*thumb_, p.tone(0xEDEDED));
        auto& core = b.layer("seek.core");
        core.setFrame({3.5, 3, 2, 7}); d::setBackground(core, p.tone(0x111111));
        thumb_->add(core);
        l.add(track); l.add(*fill_); l.add(*thumb_);
        setTarget({-5, -3, width + 10, 28});
    }
    double value() const noexcept { return value_; }
    double duration() const noexcept { return duration_; }
    bool dragging() const noexcept { return dragging_; }
    std::function<void(double)> onScrub;
    std::function<void(std::optional<double>)> onRelease;   // nullopt: the gesture was interrupted
    d::Layer* bubbleHost{};
    // Shows `seconds`; while playing the bar runs on to `destination` over `interval`.
    void show(double seconds, double total, std::optional<double> destination = {}, double interval = 0) {
        if (dragging_) return;
        value_ = seconds; duration_ = std::max(0.0, total);
        place(x(seconds), destination ? std::optional(x(*destination)) : std::nullopt, interval);
    }
    bool pointerDown(Point p) override {
        if (!enabled() || duration_ <= 0) return false;
        dragging_ = true;
        scrub(p);
        showBubble();
        return true;
    }
    void pointerDragged(Point p) override { if (dragging_) scrub(p); }
    void pointerUp(Point) override {
        if (!dragging_) return;
        dragging_ = false; if (bubble_) bubble_->setVisible(false, clock());
        if (onRelease) onRelease(value_);
    }
    // An interrupted drag (track change, module change) ends without a seek.
    void cancelDrag() {
        if (!dragging_) return;
        dragging_ = false; if (bubble_) bubble_->setVisible(false, clock());
        if (onRelease) onRelease(std::nullopt);
    }
    void clearInteractionState() override { Control::clearInteractionState(); cancelDrag(); }
    std::optional<d::AccessibilityRole> accessibilityRole() const override { return d::AccessibilityRole::slider; }
    void accessibilityItems(const d::Layer& root, std::vector<d::AccessibilityItem>& out) const override {
        if (label.empty()) return;
        auto item = accessibilityItem(root, d::AccessibilityRole::slider);
        item.number = value_; item.minimum = 0; item.maximum = std::max(1.0, duration_); item.step = 5;
        item.value = NowPlayingCanvas::time(duration_ > 0 ? std::optional(value_) : std::nullopt);
        out.push_back(std::move(item));
    }
private:
    d::Builder* b_;
    double width_, value_{}, duration_{};
    bool dragging_{};
    d::Layer *fill_, *thumb_;
    std::unique_ptr<d::SliderBubble> bubble_;
    double x(double seconds) const noexcept { return duration_ > 0 ? std::min(1.0, std::max(0.0, seconds / duration_)) * width_ : 0; }
    void place(double start, std::optional<double> end = {}, double interval = 0) {
        const double target = end.value_or(start);
        fill_->setValue(Channel::scaleX, target / width_); thumb_->setValue(Channel::translateX, target);
        if (target == start || interval <= 0 || env().reduceMotion) return;
        fill_->setValue(Channel::scaleX, start / width_); thumb_->setValue(Channel::translateX, start);
        d::motion::animate(*fill_, Channel::scaleX, target / width_, interval, clock(), d::easing::linear);
        d::motion::animate(*thumb_, Channel::translateX, target, interval, clock(), d::easing::linear);
    }
    void scrub(Point p) {
        value_ = std::min(1.0, std::max(0.0, p.x / width_)) * duration_;
        place(x(value_));
        updateBubble();
        if (onScrub) onScrub(value_);
    }
    void showBubble() {
        if (!bubbleHost) return;
        if (!bubble_) bubble_ = std::make_unique<d::SliderBubble>(*b_, "seek.bubble");
        if (bubble_->layer().parent() != bubbleHost) bubbleHost->add(bubble_->layer());
        updateBubble();
        bubble_->setVisible(true, clock());
    }
    void updateBubble() {
        if (!dragging_ || !bubble_ || !bubbleHost) return;
        if (const auto anchor = d::convert(Point{x(value_), 2}, layer(), *bubbleHost)) bubble_->show(NowPlayingCanvas::time(value_), *anchor);
    }
};

// NowPlayingVolumePopup (:795-859): the vertical pop-up: `70%` label, 6 px
// track, accent fill from the bottom and a 16 x 6 knob. A press or drag sets
// 1 - (y - 30) / 106, rounded to a whole percent.
class NowPlayingCanvas::VolumePopup final : public d::Control {
public:
    static constexpr Rect track{19, 30, 6, 106};
    explicit VolumePopup(d::Builder& b) : Control(b.env, "appVolume", core::localized("Playing app volume", "当前播放应用音量", b.env.language), b.layer("appVolume")) {
        const auto& p = palette();
        auto& l = layer();
        const Rect bounds{0, 0, 44, 150};
        l.setAnimatable();                          // the menu wipe
        l.setBounds({bounds.width, bounds.height});
        auto plate = d::bordered(l.id() + ".plate", bounds, p.token(d::ThemeToken::panel), p.token(d::ThemeToken::ringMenu), 1);
        d::addShadow(plate, d::Shadow{d::black(1), .6, 20, {0, 18}, d::PathBuilder().rect(bounds)});
        l.setContent(std::move(plate));
        text_ = &textLayer(b, "appVolume.text", "", d::TextStyle{10, d::FontWeight::bold, p.token(d::ThemeToken::textOnDark), 1, 0, false, true},
            {0, 9, bounds.width, 10}, d::TextAlignment::center);
        auto& rail = b.layer("appVolume.track");
        rail.setFrame(track); d::setBackground(rail, p.token(d::ThemeToken::separator));
        fill_ = &b.layer("appVolume.fill");
        fill_->setAnimatable();
        fill_->setAnchorPoint({.5, 1}); fill_->setBounds({track.width, track.height}); fill_->setPosition({track.x + track.width / 2, track.y + track.height});
        d::setBackground(*fill_, p.accentInk());
        knob_ = &b.layer("appVolume.knob");
        knob_->setAnimatable();
        knob_->setBounds({16, 6}); d::setBackground(*knob_, p.token(d::ThemeToken::textOnDark));
        l.add(*text_); l.add(rail); l.add(*fill_); l.add(*knob_);
        setTarget(bounds);
    }
    std::optional<double> value() const noexcept { return value_; }
    bool dragging() const noexcept { return dragging_; }
    std::function<void(double, bool)> onChange;   // (value 0...1, final on release)
    void show(std::optional<double> next) {
        value_ = next;
        const double fraction = std::min(1.0, std::max(0.0, next.value_or(0)));
        const std::string shown = next ? std::to_string(int(std::round(*next * 100))) + "%" : std::string("—");
        if (d::textOf(*text_).text() != shown) d::editText(*text_, [&](d::TextBlock& block) { block.setText(shown); });
        fill_->setValue(Channel::scaleY, fraction);
        knob_->setPosition({track.x + track.width / 2, track.y + track.height - track.height * fraction});
        knob_->setHidden(!next);
    }
    bool pointerDown(Point p) override {
        if (!enabled()) return false;
        dragging_ = true; select(p);
        return true;
    }
    void pointerDragged(Point p) override { if (dragging_) select(p); }
    void pointerUp(Point) override {
        if (!dragging_) return;
        dragging_ = false;
        if (onChange) onChange(value_.value_or(0), true);
    }
    void clearInteractionState() override { Control::clearInteractionState(); dragging_ = false; }
    void enabledChanged() override { fill_->setValue(Channel::opacity, enabled() ? 1 : .35); knob_->setValue(Channel::opacity, enabled() ? 1 : .35); }
    std::optional<d::AccessibilityRole> accessibilityRole() const override { return d::AccessibilityRole::slider; }
    void accessibilityItems(const d::Layer& root, std::vector<d::AccessibilityItem>& out) const override {
        if (label.empty()) return;
        auto item = accessibilityItem(root, d::AccessibilityRole::slider);
        item.number = value_.value_or(0) * 100; item.minimum = 0; item.maximum = 100; item.step = 2;
        item.value = d::textOf(*text_).text();
        out.push_back(std::move(item));
    }
private:
    d::Layer *text_, *fill_, *knob_;
    std::optional<double> value_;
    bool dragging_{};
    void select(Point p) {
        const double next = std::round(std::min(1.0, std::max(0.0, 1 - (p.y - track.y) / track.height)) * 100) / 100;
        show(next);
        if (onChange) onChange(next, false);
    }
};

// NowPlayingPlayButton (:861-892): the 54 px play / pause circle in the
// primary gradient; hover scales it 1.06 and brightens it, a press .95.
class NowPlayingCanvas::PlayButton final : public d::Control {
public:
    explicit PlayButton(d::Builder& b) : Control(b.env, "playPause", core::localized("Play", "播放", b.env.language), b.layer("playPause")) {
        const auto& p = palette();
        auto& l = layer();
        const Rect bounds{0, 0, 54, 54};
        l.setAnimatable();                           // hover and press scale, disabled .35
        l.setBounds({54, 54});
        auto& face = b.layer("playPause.face");
        face.setFrame(bounds);
        const auto colors = p.accent().primaryButton();
        const std::array<double, 3> locations{0, .5, 1};
        auto node = d::gradient(face.id(), bounds, colors, locations, {.5, 0}, {.5, 1});
        node["cornerRadius"] = 27;
        face.setContent(std::move(node));
        brighten_ = &b.layer("playPause.brighten");
        brighten_->setFrame(bounds); d::setBackground(*brighten_, d::white(.06), 27);
        brighten_->setAnimatable(d::SurfaceRole::lazy); brighten_->setValue(Channel::opacity, 0);
        glyph_ = &d::makeIcon(b, "playPause.glyph", d::Icon::play, 22, d::fixed::nearBlack);
        glyph_->setPosition({27, 27});
        l.add(face); l.add(*brighten_); l.add(*glyph_);
        setTarget(bounds, d::PathBuilder().ellipse(bounds));
    }
    bool playing() const noexcept { return playing_; }
    void setPlaying(bool v) {
        if (v == playing_) return;
        playing_ = v;
        d::setIcon(*glyph_, v ? d::Icon::pause : d::Icon::play, 22, d::fixed::nearBlack);
        label = v ? core::localized("Pause", "暂停", env().language) : core::localized("Play", "播放", env().language);
    }
    void hoverChanged() override { d::applyHoverMotion(*this, 0, 1.06, .95, brighten_); }
    void pressChanged() override { d::applyHoverMotion(*this, 0, 1.06, .95, brighten_); }
    void enabledChanged() override { layer().setValue(Channel::opacity, enabled() ? 1 : .35); hoverChanged(); }
private:
    d::Layer *brighten_, *glyph_;
    bool playing_{};
};

NowPlayingCanvas::NowPlayingCanvas(d::Context& context, NowPlayingCanvasServices services)
    : Canvas(core::Module::nowPlaying, context), services_(std::move(services)) {
    setShowsHeader(false);
}
NowPlayingCanvas::~NowPlayingCanvas() {
    if (presented_ && services_.setPresented) try { services_.setPresented(false); } catch (...) {}
}

const NowPlayingViewInput& NowPlayingCanvas::input() const {
    static const NowPlayingViewInput none;
    return services_.input ? services_.input() : none;
}
NowPlayingCanvas::Structure NowPlayingCanvas::structure() const { return {env().language, input().track.has_value()}; }
// Source and title of the track on the page; another one is a track change (:149).
std::optional<std::string> NowPlayingCanvas::identity() const {
    const auto& in = input();
    if (!in.track) return std::nullopt;
    return std::to_string(int(nowPlayingSourceForAppUserModelID(in.application))) + "\x1f" + in.track->title;
}
// The old cover stays under the new one until its fade ends (no deadline:
// the owner asks every frame it draws).
std::array<NowPlayingCanvas::CoverView, 2> NowPlayingCanvas::coverViews(double now) const noexcept {
    std::array<CoverView, 2> out{};
    if (!art_[0]) return out;
    if (covers_[0] && now < fadeEnd_) out[0] = {art_[0], covers_[0]};
    if (covers_[1]) out[1] = {art_[1], covers_[1]};
    return out;
}

// --- Presentation (:114-167) -------------------------------------------------------------
void NowPlayingCanvas::setPresented(bool value, double now) {
    env().now = std::max(env().now, now);
    if (value == presented_) return;
    presented_ = value;
    if (value) {
        if (services_.setPresented) services_.setPresented(true);
        refresh(now);
    } else {
        stopClock(); closeVolume(); if (seek_) seek_->cancelDrag();
        // Nothing keeps running on the hidden page (bar runs, lyric slides, fades).
        if (renderedLanguage()) removeMotion(page().layer());
        if (services_.setPresented) services_.setPresented(false);
    }
}
void NowPlayingCanvas::renderScaleChanged() { closeVolume(); Canvas::renderScaleChanged(); }
void NowPlayingCanvas::closePopups() { Canvas::closePopups(); closeVolume(); }
void NowPlayingCanvas::refresh(double now) {
    env().now = std::max(env().now, now);
    if (!presented_) return;
    const auto& in = input();
    if (seekGesture_ && (seekGesture_->first != in.session || !in.track || !in.track->sameIdentity(seekGesture_->second)) && seek_) seek_->cancelDrag();
    if (volumeGesture_ && volumeGesture_->first != in.session) { if (popup_) popup_->clearInteractionState(); volumeGesture_.reset(); }
    const auto id = identity();
    const auto previous = shownTrack_;
    shownTrack_ = id;
    if (!renderedLanguage()) return;
    if (!renderedStructure_ || structure() != *renderedStructure_) { closeVolume(); rerender(); }
    else if (id != previous && isActive()) navigate(takeDirection());
    else if (id != previous) rerender();
    else update(true);
}
double NowPlayingCanvas::takeDirection() {
    const auto pending = pendingDirection_;
    pendingDirection_.reset();
    if (!pending || pending->second <= now()) return 1;
    return pending->first;
}

// --- Page (:169-288) -------------------------------------------------------------------------
void NowPlayingCanvas::buildPage(d::Page& page) {
    renderedStructure_ = structure();
    if (services_.setArtworkPixels) services_.setArtworkPixels(nowPlayingCoverPixelSize(Layout::cover.width, env().text.boxScale));
    volumeOpen_ = false; volumeGesture_.reset(); displayed_.reset(); covers_ = {}; lyricsVisible_ = false; art_ = {};
    parts_.clear();
    const auto& track = input().track;
    auto& cover = buildCover(page, track);
    auto lyrics = std::make_shared<LyricWindow>(page.builder(), Layout::lyrics);
    lyricWindow_ = lyrics.get(); parts_.push_back(lyrics);
    auto& timeline = buildTimeline(page);
    auto& transport = page.makeLayer("nowPlaying.transport");
    auto& extras = page.makeLayer("nowPlaying.extras");
    auto& previous = page.make<d::IconButton>("previous", text("Previous track", "上一首"), d::Icon::prev, d::IconButton::Style::dark, true, true);
    auto& play = page.make<PlayButton>();
    auto& next = page.make<d::IconButton>("next", text("Next track", "下一首"), d::Icon::next, d::IconButton::Style::dark, true, true);
    auto& volume = page.make<d::IconButton>("volume", text("App volume", "应用音量"), d::Icon::vol);
    auto& lyricsToggle = page.make<d::IconButton>("lyrics", text("Lyrics", "歌词"), d::Icon::lyric);
    previous.onActivate = [this] { previousTrack(); };
    play.onActivate = [this] { if (services_.perform) services_.perform({NowPlayingIntentKind::playPause, input().session}); };
    next.onActivate = [this] { nextTrack(); };
    volume.onActivate = [this] { toggleVolume(); };
    lyricsToggle.onActivate = [this] { toggleLyrics(); };
    transport.setFrame(Layout::transport); extras.setFrame(Layout::extras);
    previous.layer().setOrigin({0, 5});
    play.layer().setOrigin({54, 0});
    next.layer().setOrigin({118, 5});
    volume.layer().setOrigin({0, 0});
    lyricsToggle.layer().setOrigin({38, 0});
    transport.add(previous.layer()); transport.add(play.layer()); transport.add(next.layer());
    extras.add(volume.layer()); extras.add(lyricsToggle.layer());
    for (d::Layer* layer : {&cover, &lyrics->layer(), &timeline, &transport, &extras}) page.layer().add(*layer);
    for (d::Layer* layer : {&cover, &lyrics->layer(), &timeline, &transport, &extras}) page.stagger(*layer);
    // The volume pop-up of this page: hidden until opened (the Mac makes one
    // per opening; one per page here, a page arena never shrinks).
    auto& panel = page.make<VolumePopup>();
    panel.layer().setFrame(Layout::popup); panel.layer().setZPosition(5); panel.layer().setHidden(true);
    panel.onChange = [this](double value, bool final) { adjustVolume(value, final); };
    page.layer().add(panel.layer());
    popup_ = &panel;
    // The track for UI Automation (title / artist / album, or nothing playing) on the cover; it never takes a press.
    auto& readout = page.builder().controls.make<d::Readout>(env(), "track", text("Now Playing", "当前播放"), trackStatus(), cover);
    trackReadout_ = &readout;
    page.add(readout);
    page.add(*seek_);
    page.add(std::vector<d::Control*>{&previous, &play, &next, &volume, &lyricsToggle});
    previousButton_ = &previous; playButton_ = &play; nextButton_ = &next; volumeButton_ = &volume; lyricsButton_ = &lyricsToggle;
    lyrics->setLyrics(track ? input().lyrics : nullptr);
    update(false);
}
// The cover (spec section 3.12 Cover): the art with the title shade, or the
// hatched nothing-playing plate. The ring also stands in for missing artwork.
// The art itself is the owner's texture between this plate and its top part.
d::Layer& NowPlayingCanvas::buildCover(d::Page& page, const std::optional<NowPlayingTrack>& track) {
    const auto& p = env().palette;
    auto& b = page.builder();
    const d::Size size{Layout::cover.width, Layout::cover.height};
    const Rect bounds{0, 0, size.width, size.height};
    auto& cover = page.makeLayer("nowPlaying.cover");
    cover.setFrame(Layout::cover); cover.setMasksToBounds(true);
    d::setBackground(cover, d::fixed::counterBackground);
    auto& top = page.makeLayer("nowPlaying.cover.top");
    top.setFrame(bounds); top.setPartRoot(true);
    auto& ring = page.makeLayer("nowPlaying.cover.ring");
    ring.setBounds({72, 72});
    d::setBorder(ring, std::nullopt, d::rgb(0x999999, .4), 2, 36);   // fixed, not a tone (:223)
    cover_ = &cover;
    if (!track) {
        auto& stripes = page.makeLayer("nowPlaying.cover.hatch");
        stripes.setFrame(bounds); d::setPath(stripes, hatch(size), d::white(.14));
        auto& label = textLayer(b, "nowPlaying.cover.label", text("No music playing", "未在播放音乐"), d::type::label(p),
            {0, (size.height - 96) / 2 + 86, size.width, 10}, d::TextAlignment::center);
        // 72 px ring, 14 px gap and the 10 px label, centred as one block.
        ring.setPosition({size.width / 2, (size.height - 96) / 2 + 36});
        top.add(stripes); top.add(ring); top.add(label);
        cover.add(top);
        placeholder_ = nullptr; titleText_ = nullptr; artistText_ = nullptr;
        return cover;
    }
    for (std::size_t n = 0; n < 2; ++n) {
        art_[n] = &page.makeLayer("nowPlaying.cover.art." + std::to_string(n));
        art_[n]->setAnimatable();                   // the cover cross-fade
        art_[n]->setFrame(bounds); art_[n]->setValue(Channel::opacity, 0);
        cover.add(*art_[n]);
    }
    auto& shade = page.makeLayer("nowPlaying.cover.shade");
    shade.setFrame({0, size.height - 116, size.width, 116});
    const std::array<d::Color, 3> colors{d::black(0), d::black(.76), d::black(.9)};
    const std::array<double, 3> locations{0, .3, 1};
    shade.setContent(d::gradient(shade.id(), d::localBounds(shade), colors, locations, {.5, 0}, {.5, 1}));
    auto& title = textLayer(b, "nowPlaying.cover.title", track->title, d::TextStyle{21, d::FontWeight::bold, d::white(1), 1.2},
        {20, size.height - 38 - 21 * 1.2, size.width - 40, 21 * 1.2});
    auto& artist = textLayer(b, "nowPlaying.cover.artist", track->artist, d::TextStyle{14, d::FontWeight::regular, d::white(.8), 1.2},
        {20, size.height - 16 - 14 * 1.2, size.width - 40, 14 * 1.2});
    ring.setPosition({size.width / 2, size.height / 2});
    top.add(ring); top.add(shade); top.add(title); top.add(artist);
    cover.add(top);
    placeholder_ = &ring; titleText_ = &title; artistText_ = &artist;
    return cover;
}
// The dark seek bar with the elapsed and total times under it (spec section 3.12 Seek).
d::Layer& NowPlayingCanvas::buildTimeline(d::Page& page) {
    auto& b = page.builder();
    auto& group = page.makeLayer("nowPlaying.timeline");
    auto& bar = page.make<SeekBar>(Layout::timeline.width);
    bar.bubbleHost = &top();
    bar.onScrub = [this](double value) { scrub(value); };
    bar.onRelease = [this](std::optional<double> value) { endScrub(value); };
    const d::TextStyle style{10.5, d::FontWeight::medium, env().palette.tone(0x999999), 1.2, 0, false, true};
    // Times: margin-top -2 under the 22 px bar.
    auto& elapsed = textLayer(b, "nowPlaying.elapsed", "", style, {0, 20, 200, 10.5 * 1.2});
    auto& total = textLayer(b, "nowPlaying.duration", "", style, {Layout::timeline.width - 200, 20, 200, 10.5 * 1.2}, d::TextAlignment::right);
    group.setFrame(Layout::timeline);
    bar.layer().setOrigin({0, 0});
    group.add(bar.layer()); group.add(elapsed); group.add(total);
    seek_ = &bar; elapsedText_ = &elapsed; durationText_ = &total;
    return group;
}

// --- In-place updates (:290-398) ---------------------------------------------------------------
std::string NowPlayingCanvas::trackStatus() const {
    const auto& t = input().track;
    if (!t) return text("No music playing", "未在播放音乐");
    std::string out;
    for (const auto* part : {&t->title, &t->artist, &t->album}) {
        if (part->empty()) continue;
        if (!out.empty()) out += " / ";
        out += *part;
    }
    return out;
}
void NowPlayingCanvas::update(bool animated) {
    const auto& in = input();
    const auto& track = in.track;
    const auto controls = nowPlayingControls(in);
    const auto playLabel = playButton_->label;
    playButton_->setPlaying(track && track->isPlaying);
    playButton_->setEnabled(controls.playPause);
    previousButton_->setEnabled(controls.previous);
    nextButton_->setEnabled(controls.next);
    volumeButton_->setEnabled(controls.volume); volumeButton_->setOn(volumeOpen_);
    lyricsButton_->setEnabled(controls.lyrics); lyricsButton_->setOn(showsLyrics_);
    if (track && titleText_) {
        if (d::textOf(*titleText_).text() != track->title) d::editText(*titleText_, [&](d::TextBlock& b) { b.setText(track->title); });
        if (d::textOf(*artistText_).text() != track->artist) d::editText(*artistText_, [&](d::TextBlock& b) { b.setText(track->artist); });
    }
    trackReadout_->value = trackStatus();
    const auto total = time(track ? track->duration : std::nullopt);
    if (d::textOf(*durationText_).text() != total) d::editText(*durationText_, [&](d::TextBlock& b) { b.setText(total); });
    showArt(animated);
    seek_->setEnabled(canSeek());
    lyricWindow_->setLyrics(track ? in.lyrics : nullptr);
    setLyricsVisible(showsLyrics_ && track && in.lyrics && in.lyrics->hasContent(), animated);
    renderPopup();
    updateClock(animated);
    if (playButton_->label != playLabel && context().onInputStateChange) context().onInputStateChange();
}
bool NowPlayingCanvas::canSeek() const { return nowPlayingControls(input()).seek; }
// Art cross-fades (.2 s ease-in-out) whenever the decoded cover changes: the
// new cover over the old one, which is dropped once the fade ends.
void NowPlayingCanvas::showArt(bool animated) {
    if (!art_[0]) return;
    const auto& in = input();
    NowPlayingCover image = in.coverAvailable && services_.artwork ? services_.artwork() : nullptr;
    if (image == displayed_) return;
    displayed_ = image;
    const auto clock = env().clock();
    if (animated && !clock.reduceMotion) {
        const auto old = covers_[1];
        covers_ = {old, image};
        fadeEnd_ = clock.now + .2;
        art_[0]->setValue(Channel::opacity, old ? 1 : 0);
        if (image) { art_[1]->setValue(Channel::opacity, 0); d::motion::animate(*art_[1], Channel::opacity, 1, .2, clock, d::easing::easeInOut); }
        else d::motion::animate(*art_[0], Channel::opacity, 0, .2, clock, d::easing::easeInOut);
    } else {
        covers_ = {nullptr, image};
        fadeEnd_ = 0;
        art_[0]->setValue(Channel::opacity, 0); art_[1]->setValue(Channel::opacity, 1);
    }
    if (placeholder_) placeholder_->setHidden(image != nullptr);
}
// The whole lyric block fades (.24 s) when lyrics are toggled, arrive or end.
void NowPlayingCanvas::setLyricsVisible(bool visible, bool animated) {
    if (!lyricWindow_ || (visible == lyricsVisible_ && animated)) return;
    lyricsVisible_ = visible;
    d::motion::animate(lyricWindow_->layer(), Channel::opacity, visible ? 1 : 0, animated ? .24 : 0, env().clock(), d::easing::ease);
}
std::optional<double> NowPlayingCanvas::displayedElapsed() const {
    if (seek_ && seek_->dragging()) return seek_->value();
    return nowPlayingElapsed(input(), now());
}
void NowPlayingCanvas::updateClock(bool animated) {
    const auto elapsed = displayedElapsed();
    const auto value = time(elapsed);
    if (d::textOf(*elapsedText_).text() != value) d::editText(*elapsedText_, [&](d::TextBlock& b) { b.setText(value); });
    lyricWindow_->show(elapsed.value_or(0), animated && lyricsVisible_);
    reconcileClock();
}
// One deadline at the next whole second or lyric cue while playing; the bar
// runs on to that moment, so a tick never snaps it (:349-378).
void NowPlayingCanvas::reconcileClock() {
    const double current = now();
    const auto& in = input();
    const auto& track = in.track;
    const double duration = track && track->duration ? *track->duration : 0;
    const bool dragging = seek_ && seek_->dragging();
    const auto elapsed = nowPlayingElapsed(in, current);
    if (!presented_ || !track || !elapsed || !std::isfinite(*elapsed)) {
        stopClock(); if (!dragging && seek_) seek_->show(0, duration); return;
    }
    const auto deadline = nowPlayingDisplayDeadline(*track, current, true, in.lyrics.get(), lyricsVisible_, dragging, in.playbackRate);
    if (!deadline) { stopClock(); if (!dragging) seek_->show(*elapsed, duration); return; }
    // An existing deadline within half a millisecond is kept.
    const double at = clockDeadline_ && std::abs(*clockDeadline_ - *deadline) < .0005 ? *clockDeadline_ : *deadline;
    clockDeadline_ = at;
    if (!dragging) seek_->show(*elapsed, duration, nowPlayingElapsed(in, at), at - current);
}
std::optional<double> NowPlayingCanvas::nextWakeTime(double) const { return presented_ ? clockDeadline_ : std::nullopt; }
void NowPlayingCanvas::wake(double now) {
    env().now = std::max(env().now, now);
    if (!clockDeadline_ || now < *clockDeadline_) return;
    clockDeadline_.reset();
    tick();
}
// The tick: the readouts (the end-of-track refresh is the controller's).
void NowPlayingCanvas::tick() { if (presented_) updateClock(true); }

// --- Transport (:400-448) -----------------------------------------------------------------------
// Previous restarts the track after its first 3 s (in place); otherwise it
// goes to the previous track, which enters from the left.
void NowPlayingCanvas::previousTrack() {
    const auto& in = input();
    if (!in.track || !services_.perform) return;
    if (const auto elapsed = nowPlayingElapsed(in, now()); elapsed && *elapsed > 3 && canSeek())
        services_.perform({NowPlayingIntentKind::seek, in.session, 0});
    else {
        pendingDirection_ = std::pair(-1.0, now() + 5);
        services_.perform({NowPlayingIntentKind::previous, in.session});
    }
}
void NowPlayingCanvas::nextTrack() {
    pendingDirection_ = std::pair(1.0, now() + 5);
    if (services_.perform) services_.perform({NowPlayingIntentKind::next, input().session});
}
void NowPlayingCanvas::toggleLyrics() { showsLyrics_ = !showsLyrics_; update(true); }
// Dragging previews the time and lyrics locally; release seeks once.
void NowPlayingCanvas::scrub(double value) {
    if (!seekGesture_ && input().track) { seekGesture_ = std::pair(input().session, *input().track); stopClock(); }
    const auto shown = time(value);
    if (d::textOf(*elapsedText_).text() != shown) d::editText(*elapsedText_, [&](d::TextBlock& b) { b.setText(shown); });
    lyricWindow_->show(value, lyricsVisible_);
}
void NowPlayingCanvas::endScrub(std::optional<double> value) {
    const auto gesture = std::move(seekGesture_);
    seekGesture_.reset();
    const auto& in = input();
    if (value && gesture && gesture->first == in.session && in.track && in.track->sameIdentity(gesture->second) && services_.perform)
        services_.perform({NowPlayingIntentKind::seek, in.session, *value});
    update(true);
}
void NowPlayingCanvas::skip(double seconds) {
    const auto& in = input();
    const auto elapsed = displayedElapsed();
    if (!canSeek() || !in.track->duration || !elapsed || !services_.perform) return;
    services_.perform({NowPlayingIntentKind::seek, in.session, std::min(*in.track->duration, std::max(0.0, *elapsed + seconds))});
}

// --- Volume pop-up (:450-547) ---------------------------------------------------------------------
// The playing app's per-app mixer route (the owner resolves only an
// unambiguous audio process and applies the route rules: 100 % on a failed
// route retires it, untouched direct playback stays untouched at 100 %).
bool NowPlayingCanvas::setAppVolume(double value) {
    const auto& in = input();
    if (!presented_ || !std::isfinite(value) || !in.volumeAvailable || !services_.perform) return false;
    return services_.perform({NowPlayingIntentKind::volume, in.session, std::min(1.0, std::max(0.0, value))});
}
void NowPlayingCanvas::toggleVolume() { if (volumeOpen_) closeVolume(); else openVolume(); }
void NowPlayingCanvas::openVolume() {
    if (volumeOpen_ || !input().track || !popup_) return;
    Canvas::closePopups();
    popup_->layer().setHidden(false);
    page().add(*popup_);
    volumeOpen_ = true;
    renderPopup();
    volumeButton_->setOn(true);
    d::motion::wipe(popup_->layer(), d::WipeOrigin::top, d::duration::menuWipe, env().clock(), d::easing::standard, 0, .4);
    refreshRouter();
    if (context().onInputStateChange) context().onInputStateChange();
}
void NowPlayingCanvas::closeVolume() {
    if (!volumeOpen_) return;
    volumeOpen_ = false; volumeGesture_.reset();
    if (popup_) {
        popup_->clearInteractionState();
        page().remove(*popup_);
        d::motion::removeWipe(popup_->layer());
        popup_->layer().setHidden(true);
    }
    if (volumeButton_) volumeButton_->setOn(false);
    refreshRouter();
    if (context().onInputStateChange) context().onInputStateChange();
}
void NowPlayingCanvas::adjustVolume(double value, bool final) {
    const auto& in = input();
    if (!volumeGesture_) volumeGesture_ = std::pair(in.session, in.volumeRoute);
    if (volumeGesture_->first == in.session && volumeGesture_->second == in.volumeRoute) (void)setAppVolume(value);
    if (final) { volumeGesture_.reset(); renderPopup(); }
}
void NowPlayingCanvas::renderPopup() {
    if (!popup_ || !volumeOpen_ || popup_->dragging()) return;
    popup_->setEnabled(input().volumeAvailable);
    popup_->show(input().volume);
}

// --- Input (:549-588) -----------------------------------------------------------------------------
// A press outside the pop-up and its button only closes the pop-up (spec section 1.12).
bool NowPlayingCanvas::interceptPointerDown(Point point, const d::PointerInput&) {
    if (!volumeOpen_ || menuOpen() || popoverOpen() || contains(Layout::popup, point) || (volumeButton_ && contains(volumeButton_->rect(root()), point))) return false;
    closeVolume();
    return true;
}
// Space plays or pauses, Left / Right seek 5 s, Esc closes the volume pop-up.
bool NowPlayingCanvas::handleKey(const d::KeyInput& key) {
    if (key.commandModifiers() || key.shift) return false;
    switch (key.virtualKey) {
    case d::key::escape:
        if (!volumeOpen_) return false;
        closeVolume(); return true;
    case d::key::space:
        if (!playButton_ || !playButton_->enabled()) return false;
        if (services_.perform) services_.perform({NowPlayingIntentKind::playPause, input().session});
        return true;
    case d::key::left: case d::key::right:
        if (!canSeek()) return false;
        skip(key.virtualKey == d::key::left ? -5 : 5);
        return true;
    default: return false;
    }
}
bool NowPlayingCanvas::adjustAccessibilitySlider(std::string_view id, double value, double now) {
    env().now = std::max(env().now, now);
    if (id == "seek") {
        if (canSeek() && services_.perform) services_.perform({NowPlayingIntentKind::seek, input().session, value});
        return true;
    }
    if (id == "appVolume") { (void)setAppVolume(value / 100); return true; }
    return Canvas::adjustAccessibilitySlider(id, value, now);
}
} // namespace endfield::modules
