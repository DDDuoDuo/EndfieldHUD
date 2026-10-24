#include "modules/archive_canvas.hpp"
#include "modules/hud_design_badges.hpp"
#include "modules/notes_media_presentation.hpp"
#include <algorithm>
#include <cmath>
#include <map>
#include <numbers>

namespace endfield::modules {
namespace d = design;
using design::Point;
using design::Rect;
namespace {
constexpr std::size_t maximumMedia = 16;
constexpr double degrees = std::numbers::pi / 180;
std::u16string utf16(std::string_view s) { return archiveUTF16(s); }
std::string utf8(std::u16string_view s) { return archiveUTF8(s); }
std::uint32_t units(std::string_view s) { return std::uint32_t(utf16(s).size()); }
std::optional<std::size_t> indexOf(std::span<const ArchiveSummary> entries, std::string_view id) {
    for (std::size_t i = 0; i < entries.size(); ++i) if (entries[i].id == id) return i;
    return std::nullopt;
}
std::string failureMessage(std::exception_ptr failure) {
    try { if (failure) std::rethrow_exception(failure); } catch (const std::exception& e) { return e.what(); } catch (...) { return "The archive could not be saved."; }
    return {};
}
// A disabled control around a static layer (HUDControl(id:env:layer:), the
// locked page's chip): it sits in the header's actions, takes no press and
// has no label (out of the accessibility tree).
class StaticControl final : public d::Control {
public:
    StaticControl(d::Builder& b, std::string id, d::Layer& layer) : Control(b.env, std::move(id), {}, layer) { setEnabled(false); }
    bool pointerDown(Point) override { return false; }
};
// The relinkable error line of an imported Mac attachment (Windows, v1.2.0
// "mediaRelink"): a press chooses its file again.
class RelinkLine final : public d::Control {
public:
    RelinkLine(d::Builder& b, std::string id, std::string label, d::Layer& line) : Control(b.env, std::move(id), std::move(label), line) {
        setTarget(d::localBounds(line));
    }
};
}

// ---------------------------------------------------------------------------
// ArchiveCategoryPlate (:1431-1493)
// ---------------------------------------------------------------------------
class ArchiveCanvas::CategoryPlate final : public d::Control {
public:
    CategoryPlate(d::Builder& b, std::string id, std::string title, std::size_t count, bool isNew, bool selected, double width)
        : Control(b.env, id, title, b.layer(id)), selected_(selected) {
        const Rect bounds{0, 0, width, ArchiveCanvas::plateHeight};
        auto& l = layer();
        l.setBounds({bounds.width, bounds.height});
        l.setAnimatable();
        auto& plate = b.layer(id + ".plate");
        plate.setFrame(bounds);
        const std::array<d::Color, 2> colors = selected ? std::array<d::Color, 2>{d::fixed::charcoal, d::fixed::charcoal} : std::array<d::Color, 2>{d::rgb(0xDCDDD8), d::rgb(0xCBCCC7)};
        plate.setContent(d::plate(plate.id(), bounds, colors, {}, 10));
        lit_ = &b.layer(id + ".lit");
        lit_->setFrame(bounds);
        lit_->setAnimatable();
        lit_->setValue(d::Channel::opacity, 0);
        const std::array<d::Color, 2> lit = selected ? std::array<d::Color, 2>{d::rgb(0x282828), d::rgb(0x282828)} : std::array<d::Color, 2>{d::rgb(0xE9EAE5), d::rgb(0xD7D8D3)};
        lit_->setContent(d::plate(lit_->id(), bounds, lit, {}, 10));
        name_ = &d::makeText(b, id + ".name", title, d::TextStyle{14, d::FontWeight::medium, selected ? d::rgb(0xF2F2F2) : d::fixed::nearBlack, 1.25},
            {16, 7, width - 16 - 12, 17.5});
        auto& tally = d::makeText(b, id + ".count", d::format::pad2(std::int64_t(count)),
            d::TextStyle{9.5, d::FontWeight::bold, d::rgb(selected ? 0x9A9A9A : 0x666666), 1, .06, false, true}, {16, 26, width - 28, 9.5});
        l.add(plate); l.add(*lit_); l.add(*name_); l.add(tally);
        if (selected) {
            auto& bar = b.layer(id + ".bar");
            bar.setFrame({6, 10, 3, ArchiveCanvas::plateHeight - 20});
            d::setBackground(bar, b.env.palette.yellow());
            l.add(bar);
        }
        if (isNew) {
            auto& badge = d::badge::newBadge(b, id + ".new");
            badge.setPosition({width - 14 - badge.bounds().width / 2, 5 + badge.bounds().height / 2});
            l.add(badge);
        }
        setTarget(bounds, d::chamfer(bounds, 10), true);
    }
    // The rename field replaces the name (118 x 24, two px higher, :1477-1485).
    void beginRename(d::TextField& field) {
        name_->setHidden(true);
        field.layer().setFrame({16, 5, 118, 24});
        layer().add(field.layer());
    }
    void hoverChanged() override {
        const bool on = hovered() && enabled();
        d::motion::animate(layer(), d::Channel::translateX, on ? 3 : 0, d::duration::hover, clock());
        d::motion::animate(*lit_, d::Channel::opacity, on ? 1 : 0, d::duration::hover, clock(), d::easing::ease);
    }
    std::optional<d::AccessibilityRole> accessibilityRole() const override { return d::AccessibilityRole::tab; }
    void accessibilityItems(const d::Layer& root, std::vector<d::AccessibilityItem>& out) const override {
        if (label.empty()) return;
        auto item = accessibilityItem(root, d::AccessibilityRole::tab);
        item.on = selected_;
        out.push_back(std::move(item));
    }
private:
    bool selected_; d::Layer *lit_, *name_;
};

// ---------------------------------------------------------------------------
// ArchiveSeekBar (:1675-1697): a 4 px #252525 track with a yellow fill; a
// press or drag seeks. The fill is a full-width surface scaled from its left
// edge, so progress is a pose number.
// ---------------------------------------------------------------------------
class ArchiveCanvas::SeekBar final : public d::Control {
public:
    SeekBar(d::Builder& b, std::string id, std::string label, double width) : Control(b.env, id, std::move(label), b.layer(id)) {
        auto& l = layer();
        l.setBounds({width, 14});
        auto& track = b.layer(id + ".track");
        track.setFrame({0, 5, width, 4});
        d::setBackground(track, d::fixed::sliderTrack);
        fill_ = &b.layer(id + ".fill");
        fill_->setAnchorPoint({0, .5});
        fill_->setFrame({0, 0, width, 4});
        d::setBackground(*fill_, b.env.palette.yellow());
        fill_->setAnimatable();
        fill_->setValue(d::Channel::scaleX, 0);
        track.add(*fill_); l.add(track);
        setTarget(d::localBounds(l));
    }
    d::Layer& fill() const noexcept { return *fill_; }
    std::function<void(double)> onSeek;
    bool pointerDown(Point p) override { if (!enabled()) return false; seek(p); return true; }
    void pointerDragged(Point p) override { seek(p); }
    void pointerUp(Point) override {}
    std::optional<d::AccessibilityRole> accessibilityRole() const override { return std::nullopt; }
private:
    d::Layer* fill_;
    void seek(Point p) { if (onSeek) onSeek(std::clamp(p.x / std::max(1.0, layer().bounds().width), 0.0, 1.0)); }
};

// ---------------------------------------------------------------------------
// ArchiveMediaStrip (:1699-1751)
// ---------------------------------------------------------------------------
class ArchiveCanvas::MediaStrip final {
public:
    MediaStrip(d::Builder& b, const ArchiveJson& reference, std::size_t index, std::size_t count, bool playing,
        const std::optional<std::string>& failed, bool relinkable, d::Size size, const std::function<std::string(std::string_view, std::string_view)>& t)
        : layer_(&b.layer("archive.media")) {
        auto& l = *layer_;
        l.setBounds(size);
        const auto& palette = b.env.palette;
        d::setBorder(l, palette.tone(0x1C1C1C), palette.token(d::ThemeToken::ringField));
        remove_ = &b.make<d::IconButton>("removeMedia", t("Remove media", "移除媒体"), d::Icon::x);
        if (count > 1) {
            previous_ = &b.make<d::IconButton>("mediaPrevious", t("Previous", "上一个"), d::Icon::left);
            next_ = &b.make<d::IconButton>("mediaNext", t("Next", "下一个"), d::Icon::right);
        }
        const std::string kind = reference["kind"].isString() ? reference["kind"].string() : "image";
        if (kind != "image") play_ = &b.make<d::IconButton>("mediaPlay", playing ? t("Pause", "暂停") : t("Play", "播放"), playing ? d::Icon::pause : d::Icon::play);
        // A 16:10 preview as tall as the strip allows; the details sit to its right.
        preview_ = &b.layer("archive.media.preview");
        preview_->setFrame({8, 8, std::round((size.height - 16) * 1.6), size.height - 16});
        d::setBackground(*preview_, d::rgb(0x0E0E0E));
        preview_->setMasksToBounds(true);
        l.add(*preview_);
        const double left = preview_->frame().x + preview_->frame().width + 12;
        const std::string name = reference["displayName"].isString() ? reference["displayName"].string() : std::string{};
        auto& title = d::makeText(b, "archive.media.name", name, d::TextStyle{13, d::FontWeight::medium, palette.token(d::ThemeToken::textOnDark)},
            {left, 12, size.width - left - 12, 13});
        auto& detail = d::makeText(b, "archive.media.detail",
            failed ? *failed : d::format::pad2(std::int64_t(index + 1)) + " / " + d::format::pad2(std::int64_t(count)),
            d::TextStyle{9.5, d::FontWeight::bold, failed ? palette.token(d::ThemeToken::danger) : palette.token(d::ThemeToken::caption), 1, failed ? 0 : .06, false, true},
            {left, 31, size.width - left - 12, 9.5});
        l.add(title); l.add(detail);
        if (failed && relinkable) relink_ = &b.make<RelinkLine>("mediaRelink", *failed, detail);
        double x = left;
        for (auto* button : {previous_, next_, play_, remove_}) {
            if (!button) continue;
            button->layer().setFrame({x, size.height - 8 - 30, 30, 30});
            l.add(button->layer());
            x += 36;
        }
        if (kind == "video") {
            seek_ = &b.make<SeekBar>("mediaSeek", t("Seek", "进度"), size.width - x - 6 - 12);
            seek_->layer().setOrigin({x + 6, size.height - 8 - 22});
            l.add(seek_->layer());
        }
    }
    d::Layer& layer() const noexcept { return *layer_; }
    d::Layer& preview() const noexcept { return *preview_; }
    d::IconButton *previous_{}, *next_{}, *play_{}, *remove_{};
    SeekBar* seek_{};
    d::Control* relink_{};
    std::vector<d::Control*> controls() const {
        std::vector<d::Control*> out;
        for (d::Control* c : std::initializer_list<d::Control*>{previous_, next_, play_, remove_, seek_, relink_}) if (c) out.push_back(c);
        return out;
    }
private:
    d::Layer *layer_, *preview_;
};

// ---------------------------------------------------------------------------
// ArchiveLockedPanel (:1753-1900): hatched bars sized to the title and each
// wrapped line, the rotated CLASSIFIED stamp and the password bar. The text
// is drawn only once the password is verified (reveal), never while locked.
// ---------------------------------------------------------------------------
class ArchiveCanvas::LockedPanel final {
public:
    static d::TextStyle titleStyle(const d::Palette& p) { return {18, d::FontWeight::bold, p.foreground(1), 1.3}; }
    static d::TextStyle lineStyle(const d::Palette& p) { return {13, d::FontWeight::regular, p.tone(0xDDDDDD), 1.45}; }
    LockedPanel(d::Builder& b, const std::string& title, const std::string& body, d::Size size, const std::function<std::string(std::string_view, std::string_view)>& t)
        : builder_(&b), layer_(&b.layer("archive.classified")), passwordBar_(&b.layer("archive.passwordBar")), stamp_(&b.layer("archive.stamp")) {
        auto& env = b.env;
        auto& l = *layer_;
        l.setBounds(size);
        d::setBorder(l, env.palette.tone(0x141414), env.palette.token(d::ThemeToken::separator));
        l.setMasksToBounds(true);
        const double inner = size.width - 36;
        double y = 16;
        std::vector<std::string> lines{title};
        for (auto& line : wrapped(body, env.characters)) lines.push_back(std::move(line));
        for (std::size_t index = 0; index < lines.size() && y < size.height; ++index) {
            const auto style = index == 0 ? titleStyle(env.palette) : lineStyle(env.palette);
            const double height = style.size * style.lineHeight;
            const double width = std::min(inner, std::max(4.0, env.text.width(lines[index], style)));
            auto& bar = d::redaction::bar(b, "archive.bar." + std::to_string(index), d::Size{width + 6, height + 2}, false,
                d::redaction::classifiedFill, d::redaction::classifiedStripe);
            bar.setOrigin({18 - 3, y - 1});
            l.add(bar); bars_.push_back(&bar); texts_.push_back(lines[index]);
            y += height + (index == 0 ? 16 : 9);
        }
        auto& stampText = d::makeText(b, "archive.stamp.text", t("CLASSIFIED", "机密"),
            d::TextStyle{15, d::FontWeight::black, d::fixed::red, 1, .24}, {}, d::TextAlignment::center);
        const double width = std::ceil(d::preferredWidth(d::textOf(stampText), env.text)) + 20 + 6;
        stamp_->setFrame({size.width - 26 - width, 22, width, 15 + 10 + 6});
        d::setBorder(*stamp_, std::nullopt, d::fixed::red, 3);
        stamp_->setAnimatable();
        stamp_->setValue(d::Channel::opacity, .9);
        stamp_->setValue(d::Channel::rotation, -8 * degrees);
        d::setLine(stampText, {0, 8, width, 15});
        stamp_->add(stampText);
        l.add(*stamp_);
        passwordBar_->setFrame({18, size.height - 16 - 52, size.width - 36, 52});
        d::setBorder(*passwordBar_, env.palette.tone(0x1A1A1A, .96), env.palette.token(d::ThemeToken::ringField));
        passwordBar_->setAnimatable();
        auto& edge = b.layer("archive.passwordBar.edge");
        edge.setFrame({0, 0, 3, 52});
        d::setBackground(edge, d::fixed::red);
        auto& lock = d::makeIcon(b, "archive.passwordBar.lock", d::Icon::lock, 18, d::fixed::red);
        lock.setFrame({14, 17, 18, 18});
        passwordBar_->add(edge); passwordBar_->add(lock);
        l.add(*passwordBar_);
    }
    // The body wrapped at 52 characters (only what can be visible, :1810-1812).
    static std::vector<std::string> wrapped(const std::string& body, const d::CharacterRules& rules) {
        auto lines = d::redaction::wrapped(rules.prefix(body, 1600), 52, rules);
        if (lines.size() > 16) lines.resize(16);
        return lines;
    }
    d::Layer& layer() const noexcept { return *layer_; }
    // Field width beside an Unlock button of `width` (lock icon, gaps, padding).
    double fieldWidth(double width) const { return passwordBar_->bounds().width - 44 - 8 - width - 10; }
    void showPassword(d::TextField& field, d::Button& unlock) {
        field.layer().setOrigin({44, 10});
        unlock.layer().setOrigin({passwordBar_->bounds().width - 10 - unlock.layer().bounds().width, 10});
        passwordBar_->add(field.layer()); passwordBar_->add(unlock.layer());
    }
    // A missing or unreadable password: the reason and the recovery actions (:1829-1846).
    void showRecovery(const std::string& message, std::span<d::Button* const> buttons) {
        double right = passwordBar_->bounds().width - 10;
        for (auto it = buttons.rbegin(); it != buttons.rend(); ++it) {
            auto& button = **it;
            button.place({0, 0});
            right -= button.layer().bounds().width;
            button.layer().setOrigin({right, 10});
            passwordBar_->add(button.layer());
            right -= 8;
        }
        auto& text = d::makeText(*builder_, "archive.passwordBar.reason", message,
            d::TextStyle{11.5, d::FontWeight::regular, builder_->env.palette.tone(0xDDDDDD), 1.3}, {44, 11, right - 44, 30});
        d::editText(text, [](d::TextBlock& block) { block.maximumLines = 2; });
        passwordBar_->add(text);
    }
    // Draws the title and lines beneath their bars (the demo's `.cl` text), so
    // the opening wipe uncovers the document. Only after unlocking (:1848-1862).
    void reveal() {
        if (revealed_) return;
        revealed_ = true;
        const auto& palette = builder_->env.palette;
        for (std::size_t index = 0; index < bars_.size(); ++index) {
            const auto style = index == 0 ? titleStyle(palette) : lineStyle(palette);
            const auto bar = bars_[index]->frame();
            auto& text = d::makeText(*builder_, "archive.revealed." + std::to_string(index), texts_[index], style,
                {bar.x + 3, bar.y + 1, layer_->bounds().width - 36, style.size * style.lineHeight});
            layer_->insertBelow(text, *bars_[index]);
        }
    }
    // Each bar grows from its left edge .4 s E, 50 ms apart (:1864-1873).
    void playSealing(const d::motion::Clock& clock) {
        if (clock.reduceMotion) return;
        for (std::size_t index = 0; index < bars_.size(); ++index) {
            auto& bar = *bars_[index];
            anchor(bar, 0);
            bar.setValue(d::Channel::scaleX, 0);
            d::motion::animate(bar, d::Channel::scaleX, 1, .4, clock, d::easing::standard, double(index) * .05);
        }
    }
    // Returns when the editor may open: n x 70 ms + 520 ms (:1875-1886).
    double playOpening(const d::motion::Clock& clock) {
        for (std::size_t index = 0; index < bars_.size(); ++index) {
            anchor(*bars_[index], 1);
            d::motion::animate(*bars_[index], d::Channel::scaleX, .001, .45, clock, d::easing::standard, double(index) * .07);
        }
        d::motion::animate(*stamp_, d::Channel::opacity, 0, .3, clock, d::easing::ease);
        d::motion::animate(*stamp_, d::Channel::scaleX, 1.3, .4, clock);
        d::motion::animate(*stamp_, d::Channel::scaleY, 1.3, .4, clock);
        d::motion::animate(*passwordBar_, d::Channel::opacity, 0, .3, clock, d::easing::ease);
        d::motion::animate(*passwordBar_, d::Channel::translateY, 10, .3, clock);
        return double(bars_.size()) * .07 + .52;
    }
    // Wrong password: +-6 px over .35 s (:1888-1895).
    void shake(const d::motion::Clock& clock) {
        if (clock.reduceMotion) return;
        static constexpr std::array<double, 6> values{0, -6, 6, -6, 6, 0}, times{0, .2, .4, .6, .8, 1};
        d::motion::keyframes(*passwordBar_, d::Channel::translateX, values, times, .35, clock, d::easing::linear, 1, true);
    }
private:
    d::Builder* builder_;
    d::Layer *layer_, *passwordBar_, *stamp_;
    std::vector<d::Layer*> bars_;
    std::vector<std::string> texts_;
    bool revealed_{};
    static void anchor(d::Layer& bar, double x) { const auto frame = bar.frame(); bar.setAnchorPoint({x, .5}); bar.setFrame(frame); }
};

// ---------------------------------------------------------------------------
// ArchiveGalleryThumbnails (:1902-1956): static posters only; a bounded set of
// waiting requests, one decode at a time and a small LRU. Paging replaces
// pending work; hiding discards every bitmap.
// ---------------------------------------------------------------------------
struct ArchiveCanvas::Posters {
    static constexpr std::size_t wantedLimit = 12, cacheLimit = 18;
    struct Cached { ArchiveJson reference; std::optional<d::ImageSource> image; };
    ArchivePosterDecoder decode;
    std::function<void(const d::ImageSource&)> release;
    std::function<void(const std::string&, const std::optional<d::ImageSource>&)> onChange;
    void drop(const std::optional<d::ImageSource>& image) { if (image && release) release(*image); }
    std::map<std::string, Cached, std::less<>> cache;
    std::vector<std::string> order;
    std::vector<std::pair<std::string, ArchiveJson>> wanted, pending;
    std::optional<std::pair<std::string, ArchiveJson>> inFlight;
    bool working{};
    std::uint64_t generation{};
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
    ~Posters() { *alive = false; }
    static const ArchiveJson* find(const std::vector<std::pair<std::string, ArchiveJson>>& v, std::string_view id) {
        for (const auto& [key, value] : v) if (key == id) return &value;
        return nullptr;
    }
    bool contains(std::string_view id) const { return cache.contains(id); }
    std::optional<d::ImageSource> image(std::string_view id, const ArchiveJson* reference) const {
        const auto it = cache.find(id);
        if (it == cache.end() || (reference && it->second.reference != *reference)) return std::nullopt;
        return it->second.image;
    }
    void setWanted(std::vector<std::pair<std::string, ArchiveJson>> values) {
        if (values.size() > wantedLimit) values.resize(wantedLimit);
        wanted.clear();
        for (auto& [id, reference] : values) {                      // uniquing keys: the last value wins
            std::erase_if(wanted, [&](const auto& w) { return w.first == id; });
            wanted.emplace_back(std::move(id), std::move(reference));
        }
        std::erase_if(pending, [&](const auto& p) { const auto* w = find(wanted, p.first); return !w || *w != p.second; });
        for (const auto& [id, reference] : wanted) {
            const auto cached = cache.find(id);
            if (cached != cache.end() && cached->second.reference == reference) continue;
            if (inFlight && inFlight->first == id && inFlight->second == reference) continue;
            std::erase_if(pending, [&](const auto& p) { return p.first == id; });
            pending.emplace_back(id, reference);
        }
        pump();
    }
    void request(const std::string& id, const ArchiveJson& reference) {
        if (!find(wanted, id) && wanted.size() >= wantedLimit) return;
        std::erase_if(wanted, [&](const auto& w) { return w.first == id; });
        wanted.emplace_back(id, reference);
        if (const auto cached = cache.find(id); cached != cache.end() && cached->second.reference == reference) { if (onChange) onChange(id, cached->second.image); return; }
        if (!inFlight || inFlight->first != id || inFlight->second != reference) {
            std::erase_if(pending, [&](const auto& p) { return p.first == id; });
            pending.emplace_back(id, reference);
        }
        pump();
    }
    void clear() { ++generation; inFlight.reset(); wanted.clear(); pending.clear(); for (const auto& [id, c] : cache) { (void)id; drop(c.image); } cache.clear(); order.clear(); }
    void pump() {
        if (working || pending.empty() || !decode) return;
        auto next = pending.front();
        pending.erase(pending.begin());
        working = true; inFlight = next;
        const auto captured = generation;
        decode(next.second, [this, alive = alive, captured, next](std::optional<d::ImageSource> image) {
            if (!*alive) return;
            working = false; inFlight.reset();
            const auto* w = find(wanted, next.first);
            if (generation == captured && w && *w == next.second) {
                if (const auto old = cache.find(next.first); old != cache.end() && old->second.image != image) drop(old->second.image);
                cache[next.first] = Cached{next.second, image};
                std::erase(order, next.first); order.push_back(next.first);
                while (order.size() > cacheLimit) { drop(cache[order.front()].image); cache.erase(order.front()); order.erase(order.begin()); }
                if (onChange) onChange(next.first, image);
            } else drop(image);
            pump();
        });
    }
};

// ---------------------------------------------------------------------------
// ArchiveCanvas
// ---------------------------------------------------------------------------
ArchiveCanvas::ArchiveCanvas(d::Context& context, ArchiveState& state, ArchiveCanvasServices services)
    : Canvas(core::Module::archive, context), state_(&state), services_(std::move(services)), posters_(std::make_unique<Posters>()) {
    // A document unlocked in an earlier HUD session opens locked again.
    state_->lockSelected();
    posters_->decode = services_.posters;
    posters_->release = services_.releasePoster;
    posters_->onChange = [this](const std::string& id, const std::optional<d::ImageSource>& image) { if (auto* t = tile(id)) t->setPoster(image); };
    openingTimer_ = timerGroup();
    soonTimer_ = timerGroup();
    if (services_.media) {
        services_.media->onStateChange = [this] { mediaStateChanged(); };
        services_.media->onProgress = [this] { updateProgress(); };
    }
}
ArchiveCanvas::~ArchiveCanvas() {
    if (services_.media) { services_.media->onStateChange = nullptr; services_.media->onProgress = nullptr; services_.media->clear(); }
    if (body_) body_->dispose();
}
void ArchiveCanvas::setBodyHost(ArchiveBodyEditorHost* host) { bodyHost_ = host; if (body_) body_->setHost(host); }

// --- Lifecycle (:142-197) ----------------------------------------------------------------
void ArchiveCanvas::show(double now) {
    activateController();
    leaving_ = false;
    // Bring the page up to date before the entrance plays on it.
    env().now = std::max(env().now, now);
    if (renderedLanguage() && renderedLanguage() == env().language && (needsRefresh_ || !renderedStructure_ || structure() != *renderedStructure_)) rerender();
    Canvas::show(now);
    if (body_ && documentID_) body_->attach();
}
void ArchiveCanvas::willEndInteraction() { if (isActive()) if (auto focus = currentFocus()) focusMemory_ = std::move(focus); }
void ArchiveCanvas::didActivate() {
    leaving_ = false;
    activateController();
    if (state_->error()) state_->retryPendingSaves();
    if (needsRefresh_ || !renderedStructure_ || structure() != *renderedStructure_) refresh();
    if (body_ && documentID_) body_->attach();
    if (const auto& entry = state_->selected(); entry && renderedStructure_ && renderedStructure_->page.kind == PageKind::editor) syncMedia(*entry);
    loadVisibleThumbnails();
    restoreFocus();
    focusPasswordSoon();
}
void ArchiveCanvas::didDeactivate() {
    leaving_ = true;
    finishOpening(false);
    unlocking_.reset();
    if (body_) body_->suspend();
    clearMedia();
    ++thumbnailGeneration_; metadataRequests_.clear(); posters_->clear();
    panel_ = false;
    if (services_.cancelMedia) services_.cancelMedia();
    ++mediaGeneration_;
    reportEdits();
    state_->flush();
    // Leaving the module relocks a classified document; its text leaves memory with the editor.
    if (state_->selectedClassification() != ArchiveClassificationStatus::unclassified && !state_->isSelectedLocked()) {
        discardDocument();
        state_->lockSelected();
    }
}
void ArchiveCanvas::activateController() {
    if (controllerActivated_) return;
    controllerActivated_ = true;
    state_->activate();
}
// The HUD closed: the Mac's canvas ends with the HUD (deinit: the last
// canvas deactivates its controller, :128-137) and the next opening builds
// a new one (init relocks, :121-122; the filter starts at All). Windows keeps
// the canvas object, so it ends its session here.
void ArchiveCanvas::endSession() {
    finishOpening(false);
    unlocking_.reset(); pendingOpen_.reset(); sealing_.reset(); focusMemory_.reset(); keptCategoryOffset_.reset();
    discardDocument();
    clearMedia();
    ++thumbnailGeneration_; metadataRequests_.clear(); thumbnailReferences_.clear(); posters_->clear();
    reportEdits();
    if (controllerActivated_) state_->deactivate();
    controllerActivated_ = false;
    state_->lockSelected();
    filter_ = {}; mediaIndex_ = 0;
    needsRefresh_ = true;
}

// --- Controller changes (:199-243) --------------------------------------------------------
void ArchiveCanvas::stateChanged() {
    if (pendingOpen_) {
        if (state_->selected() && state_->selected()->id == *pendingOpen_) { pendingOpen_.reset(); mediaIndex_ = 0; navigate(1); return; }
        if (state_->error()) pendingOpen_.reset();
    }
    reportError();
    refreshWhenPossible();
}
// Re-renders when the structure changed, otherwise updates in place. Waits
// while the page is leaving, a menu or pop-over is open, or a category is
// being renamed.
void ArchiveCanvas::refreshWhenPossible() {
    if (!renderedLanguage()) return;
    if (holding_ || transitioning() || leaving_ || rename_ || menuOpen() || popoverOpen()) { needsRefresh_ = true; return; }
    refresh();
}
void ArchiveCanvas::refresh() {
    needsRefresh_ = false;
    if (!renderedStructure_ || structure() != *renderedStructure_) rerender(); else { for (auto& binder : binders_) binder(); for (auto& binder : popoverBinders_) binder(); }
    focusPasswordSoon();
}
// DispatchQueue.main.async: one turn later (a deadline at the current time).
void ArchiveCanvas::refreshSoon() { after(0, soonTimer_, [this] { if (needsRefresh_) refreshWhenPossible(); }); }
void ArchiveCanvas::reportError() {
    const auto error = state_->error();
    const auto previous = std::exchange(reportedError_, error);
    if (!error || error == previous || !isActive()) return;
    toast(d::Icon::warn, "Archive error", "档案库错误", errorText(*error));
}
ArchiveCanvas::PageState ArchiveCanvas::currentPage() const {
    const auto& entry = state_->selected();
    if (!entry) return {};
    if (state_->isSelectedLocked() || unlocking_ == entry->id || opening_ == entry->id) return {PageKind::locked, entry->id};
    return {PageKind::editor, entry->id};
}
ArchiveCanvas::PageState ArchiveCanvas::displayedPage() const { return renderedStructure_ ? renderedStructure_->page : PageState{}; }
ArchiveCanvas::Structure ArchiveCanvas::structure() const {
    Structure value;
    value.page = currentPage();
    value.language = env().language;
    switch (value.page.kind) {
    case PageKind::list: {
        value.filter = filter_;
        const auto entries = state_->entries();
        value.total = entries.size();
        std::map<std::string, std::size_t, std::less<>> counts;
        for (const auto& entry : entries) { if (entry.categoryID) ++counts[*entry.categoryID]; else ++value.uncategorized; }
        for (const auto& c : state_->categories()) value.categories.push_back({c.id, c.name, c.isNew, counts.contains(c.id) ? counts.at(c.id) : 0});
        for (const auto& s : filteredDocuments()) {
            const auto number = indexOf(entries, s.id);
            value.documents.push_back({s.id, number ? *number + 1 : 0, s.title, s.date, s.isClassified, s.isClassified ? 0 : s.mediaCount});
        }
        break;
    }
    case PageKind::editor: case PageKind::locked: {
        const auto& entry = state_->selected();
        value.categoryName = categoryName(entry ? entry->categoryID : std::nullopt);
        value.classification = state_->selectedClassification();
        if (value.page.kind == PageKind::editor && entry) {
            value.media = entry->media.size();
            value.mediaIndex = std::min(mediaIndex_, entry->media.empty() ? 0 : entry->media.size() - 1);
            value.playing = services_.media && services_.media->playing();
            value.mediaFailed = services_.media && services_.media->error().has_value();
        }
        break;
    }
    }
    return value;
}
std::vector<ArchiveSummary> ArchiveCanvas::filteredDocuments() const {
    std::vector<ArchiveSummary> out;
    for (const auto& s : state_->entries()) {
        const bool keep = filter_.kind == FilterKind::all || (filter_.kind == FilterKind::uncategorized ? !s.categoryID : s.categoryID == filter_.category);
        if (keep) out.push_back(s);
    }
    return out;
}
std::string ArchiveCanvas::categoryName(const std::optional<std::string>& id) const {
    if (id) for (const auto& c : state_->categories()) if (c.id == *id) return c.name;
    return text("Uncategorized", "未分类");
}
const ArchiveCanvas::CategoryPlate* ArchiveCanvas::plate(std::string_view id) const {
    for (const auto& [key, p] : plates_) if (key == id) return p;
    return nullptr;
}
d::ArchiveTile* ArchiveCanvas::tile(std::string_view id) const {
    for (const auto& [key, t] : tiles_) if (key == id) return t;
    return nullptr;
}

// --- Pages (:295-317) ----------------------------------------------------------------------
void ArchiveCanvas::buildPage(d::Page& page) {
    binders_.clear(); needsRefresh_ = false;
    if (filter_.kind == FilterKind::category && std::none_of(state_->categories().begin(), state_->categories().end(), [&](const auto& c) { return c.id == filter_.category; })) filter_ = {};
    const auto current = structure();
    renderedStructure_ = current;
    lockedPanel_ = nullptr; passwordField_ = nullptr; unlockButton_ = nullptr; formatBar_ = nullptr; progressFill_ = nullptr; preview_ = nullptr;
    categoryArea_ = nullptr; gridArea_ = nullptr;
    // The Mac builds the next page when the swap lands, so the outgoing
    // document's editor and media leave with their page; here the page is
    // built when the swap starts (Q4), so they are discarded when the new
    // page installs (didInstall).
    switch (current.page.kind) {
    case PageKind::list: buildList(page, current); break;
    case PageKind::editor:
        plates_.clear(); tiles_.clear();
        buildEditor(page);
        break;
    case PageKind::locked:
        plates_.clear(); tiles_.clear();
        buildLocked(page, current.page.document);
        break;
    }
    // Pop-over fields stay registered across in-place re-renders.
    for (auto* field : popoverFields_) registerField(*field);
}

void ArchiveCanvas::didInstall(d::Page&) {
    if (renderedStructure_ && renderedStructure_->page.kind != PageKind::editor) { discardDocument(); clearMedia(); }
}

// --- List view (:319-439) ------------------------------------------------------------------
void ArchiveCanvas::buildList(d::Page& page, const Structure& current) {
    plates_.clear(); tiles_.clear();
    const auto documents = filteredDocuments();
    shownDocuments_ = documents;
    auto& create = page.make<d::Button>("new", text("New", "新建"), d::Icon::plus, d::Button::Kind::primary, d::Button::Height::compact);
    create.place({0, 0});
    create.onActivate = [this] { createDocument(); };
    header().configure({"ARCHIVE"}, "MOD-04", title("Archive", "档案库"), d::format::pad2(std::int64_t(documents.size())) + " DOCS", false, {&create});
    buildCategories(page, current);
    buildGrid(page, documents);
}
void ArchiveCanvas::buildCategories(d::Page& page, const Structure& current) {
    const double top = d::metric::headerHeight;
    struct Row { Filter filter; std::string title; std::size_t count; bool isNew; };
    std::vector<Row> rows{{{FilterKind::all, {}}, text("All", "全部"), current.total, false},
        {{FilterKind::uncategorized, {}}, text("Uncategorized", "未分类"), current.uncategorized, false}};
    for (const auto& c : current.categories) rows.push_back({{FilterKind::category, c.id}, c.name, c.count, c.isNew});
    const double height = columnHeight - 30 - plateGap;
    // 4 px beyond the column so the hover shift is never clipped.
    auto& area = page.scrollArea("categories", {0, top, columnWidth + 4, height});
    area.setContentHeight(double(rows.size()) * (plateHeight + plateGap) - plateGap);
    if (keptCategoryOffset_) { area.setOffset(*keptCategoryOffset_); keptCategoryOffset_.reset(); } else page.restoreOffset(area);
    categoryArea_ = &area;
    const double width = area.maximumOffset() > 0 ? columnWidth - 10 : columnWidth;
    for (std::size_t index = 0; index < rows.size(); ++index) {
        const auto& row = rows[index];
        const std::string id = row.filter.kind == FilterKind::all ? "category:all" : row.filter.kind == FilterKind::uncategorized ? "category:uncategorized" : "category:" + row.filter.category;
        auto& plate = page.make<CategoryPlate>(id, row.title, row.count, row.isNew, row.filter == filter_, width);
        plate.onActivate = [this, filter = row.filter] { selectFilter(filter); };
        plate.layer().setOrigin({0, double(index) * (plateHeight + plateGap)});
        area.content().add(plate.layer());
        plate.layer().setPartRoot(true);
        page.add(plate);
        const auto frame = plate.layer().frame();
        if (frame.y < area.offset() + height && frame.y + frame.height > area.offset()) page.stagger(plate.layer());
        if (row.filter.kind == FilterKind::category) plates_.emplace_back(row.filter.category, &plate);
    }
    const bool fixedFilter = filter_.kind != FilterKind::category;
    auto& ops = page.makeLayer("archive.categoryOps");
    ops.setFrame({0, addRect.y, columnWidth, 30});
    page.layer().add(ops);
    struct Op { d::Icon icon; const char* id; std::string label; };
    const std::array<Op, 3> buttons{{{d::Icon::plus, "addCategory", text("Add category", "添加分类")},
        {d::Icon::pen, "renameCategory", text("Rename category", "重命名分类")}, {d::Icon::minus, "removeCategory", text("Remove category", "删除分类")}}};
    for (std::size_t n = 0; n < buttons.size(); ++n) {
        const std::string id = buttons[n].id;
        auto& button = page.make<d::IconButton>(id, buttons[n].label, buttons[n].icon);
        button.layer().setFrame({double(n) * 36, 0, 30, 30});
        ops.add(button.layer());
        button.setEnabled(id == "addCategory" || !fixedFilter);
        button.onActivate = [this, id] {
            if (id == "addCategory") addCategory(); else if (id == "renameCategory") beginRename(); else confirmRemoveCategory();
        };
        page.add(button);
    }
    page.stagger(ops);
}
void ArchiveCanvas::buildGrid(d::Page& page, const std::vector<ArchiveSummary>& documents) {
    auto& area = page.scrollArea("docs", gridRect);
    gridArea_ = &area;
    const std::size_t rows = (documents.size() + 2) / 3;
    area.setContentHeight(documents.empty() ? 258 : 8 + double(rows) * tileHeight + double(rows > 0 ? rows - 1 : 0) * 12);
    page.restoreOffset(area);
    tileWidth_ = std::floor((area.contentWidth() - 8 - 24) / 3);
    if (documents.empty()) {
        auto& empty = d::emptyState::make(page.builder(), "archive.empty", "EMPTY", std::nullopt, std::nullopt, {area.contentWidth() - 8, 250});
        empty.setOrigin({4, 4});
        area.content().add(empty);
        page.stagger(empty);
        return;
    }
    layoutTiles(page, true);
    after(0, soonTimer_, [this] { loadVisibleThumbnails(); });
}
// Only tiles in (or touching) the viewport exist; scrolling adds and
// removes them, so a large archive keeps a small layer tree.
void ArchiveCanvas::layoutTiles(d::Page& page, bool stagger) {
    if (!gridArea_) return;
    auto& area = *gridArea_;
    const double pitch = tileHeight + 12;
    const auto first = std::size_t(std::max(0.0, std::floor((area.offset() - 4) / pitch))) * 3;
    const auto last = std::min(shownDocuments_.size(), std::size_t(std::max(0.0, std::ceil((area.offset() + area.layer().bounds().height - 4) / pitch) + 1)) * 3);
    bool changed{};
    for (auto it = tiles_.begin(); it != tiles_.end();) {
        const bool kept = std::any_of(shownDocuments_.begin() + std::ptrdiff_t(std::min(first, shownDocuments_.size())), shownDocuments_.begin() + std::ptrdiff_t(std::max(first, last)),
            [&](const auto& s) { return s.id == it->first; });
        if (kept) { ++it; continue; }
        it->second->layer().removeFromParent();
        page.remove(*it->second);
        it = tiles_.erase(it); changed = true;
    }
    const auto entries = state_->entries();
    for (std::size_t index = first; index < last; ++index) {
        const auto& summary = shownDocuments_[index];
        if (tile(summary.id)) continue;
        const auto date = services_.dotted ? services_.dotted(summary.date) : std::string{};
        const std::string title = summary.isClassified ? "[ " + text("CLASSIFIED", "机密") + " ]" : (summary.title.empty() ? text("Untitled", "未命名") : summary.title);
        const auto number = indexOf(entries, summary.id);
        auto& t = page.make<d::ArchiveTile>("doc:" + summary.id, int(number ? *number + 1 : index + 1), title, d::ArchiveTile::Extension::txt,
            std::optional<std::string>("DOC"), std::nullopt, date, tileWidth_, 96.0, false,
            summary.isClassified ? d::ArchiveTile::Style::classified : d::ArchiveTile::Style::standard);
        t.label = (summary.isClassified ? text("Classified document", "机密文档") : title) + ", " + date;
        t.onActivate = [this, id = summary.id] { openDocument(id); };
        if (!summary.isClassified) {
            const ArchiveJson* reference = summary.thumbnail ? &*summary.thumbnail : Posters::find(thumbnailReferences_, summary.id);
            t.setPoster(posters_->image(summary.id, reference));
        }
        t.layer().setOrigin({4 + double(index % 3) * (tileWidth_ + 12), 4 + double(index / 3) * pitch});
        area.content().add(t.layer());
        t.layer().setPartRoot(true);
        page.add(t); tiles_.emplace_back(summary.id, &t); changed = true;
        if (stagger) page.stagger(t.layer());
    }
    if (changed && &page == &this->page()) { refreshRouter(); if (context().onInputStateChange) context().onInputStateChange(); }
}
void ArchiveCanvas::selectFilter(const Filter& next) {
    if (next == filter_ || transitioning()) return;
    std::vector<Filter> order{{FilterKind::all, {}}, {FilterKind::uncategorized, {}}};
    for (const auto& c : state_->categories()) order.push_back({FilterKind::category, c.id});
    const auto position = [&](const Filter& f) { const auto it = std::find(order.begin(), order.end(), f); return it == order.end() ? 0 : it - order.begin(); };
    const double direction = position(next) > position(filter_) ? 1 : -1;
    keptCategoryOffset_ = categoryArea_ ? std::optional(categoryArea_->offset()) : std::nullopt;
    filter_ = next;
    navigate(0, direction);
    if (next.kind == FilterKind::category) { state_->markCategoryViewed(next.category); stateChanged(); }
}
void ArchiveCanvas::addCategory() {
    const auto id = state_->addCategory(services_.newID(), services_.foundationNow(), env().language);
    stateChanged();
    if (!id) return;
    for (const auto& c : state_->categories()) if (c.id == *id) { toast(d::Icon::plus, "Category added", "已添加分类", c.name); return; }
}
void ArchiveCanvas::confirmRemoveCategory() {
    if (filter_.kind != FilterKind::category) return;
    const auto found = std::find_if(state_->categories().begin(), state_->categories().end(), [&](const auto& c) { return c.id == filter_.category; });
    if (found == state_->categories().end()) return;
    const std::string id = found->id;
    const std::string_view names[]{found->name};
    d::ConfirmRequest request;
    request.id = "removeCategory";
    request.title = text("Remove {0}?", "删除{0}？", names);
    request.message = text("Its documents move to Uncategorized.", "其中的文档将移至未分类。");
    request.confirmTitle = text("Remove", "删除");
    request.danger = true; request.icon = d::Icon::trash;
    confirm(std::move(request), [this, id] {
        filter_ = {};
        state_->deleteCategory(id);
        stateChanged();
        refreshWhenPossible();
        toast(d::Icon::trash, "Category removed", "文档已移至未分类");
    });
}

// --- Category rename (:469-503) --------------------------------------------------------------
void ArchiveCanvas::beginRename() {
    if (rename_ || !isActive() || transitioning() || filter_.kind != FilterKind::category) return;
    const auto found = std::find_if(state_->categories().begin(), state_->categories().end(), [&](const auto& c) { return c.id == filter_.category; });
    auto* plate = const_cast<CategoryPlate*>(this->plate(filter_.category));
    if (found == state_->categories().end() || !plate) return;
    if (categoryArea_) categoryArea_->reveal(plate->layer().frame());
    const std::string id = found->id;
    d::TextFieldOptions options;
    options.maximumLength = archiveMaximumCategoryNameLength;
    options.selectsAllOnBegin = true;
    options.inset = {6, 0};
    auto& field = page().make<d::TextField>("categoryName", text("Category name", "分类名称"), found->name, "", d::Size{118, 24}, options);
    field.onChange = [this, id, &field](const std::string& value) { field.setInvalid(state_->categoryNameIssue(value, id).has_value()); };
    field.onCommit = [this, id](const std::string& value) { finishRename(id, value); };
    field.onCancel = [this, id] { finishRename(id, std::nullopt); };
    plate->beginRename(field);
    rename_ = Rename{id, &field};
    registerField(field); page().add(field); refreshRouter();
    field.beginEditing();
}
// Enter or blur commits a valid, changed, unique name; Esc (nullopt) cancels.
void ArchiveCanvas::finishRename(const std::string& id, std::optional<std::string> value) {
    if (!rename_ || rename_->id != id) return;
    auto* field = rename_->field;
    rename_.reset();
    page().remove(*field);
    if (value && state_->renameCategory(id, *value).result == ArchiveCategoryRenameResult::renamed) {
        stateChanged();
        for (const auto& c : state_->categories()) if (c.id == id) { toast(d::Icon::pen, "Category renamed", "分类已重命名", c.name); break; }
    }
    if (transitioning() || leaving_) needsRefresh_ = true; else rerender();
}

// --- Opening and closing documents (:505-534) ---------------------------------------------------
void ArchiveCanvas::openDocument(const std::string& id) {
    if (pendingOpen_ || transitioning()) return;
    pendingOpen_ = id;
    state_->select(id);
    stateChanged();
}
void ArchiveCanvas::createDocument() {
    if (transitioning() || pendingOpen_) return;
    const std::optional<std::string> category = filter_.kind == FilterKind::category ? std::optional(filter_.category) : std::nullopt;
    reportEdits();
    mediaIndex_ = 0;
    holding([&] { state_->create(services_.newID(), services_.foundationNow(), category); stateChanged(); });
    if (!state_->selected()) { refresh(); return; }
    navigate(1);
}
void ArchiveCanvas::closeDocument() {
    if (!state_->selected() || transitioning()) return;
    finishOpening(false);
    unlocking_.reset();
    reportEdits();
    holding([&] { state_->select(std::nullopt); stateChanged(); });
    navigate(-1);
}
void ArchiveCanvas::reportEdits() {
    if (!edited_) return;
    edited_ = false;
    state_->recordAction("edited", documentID_.value_or(state_->selected() ? state_->selected()->id : std::string{}));
}

// --- Editor (:536-660) -------------------------------------------------------------------------
Rect ArchiveCanvas::bodyRect() const {
    auto rect = bodyRectFull;
    if (const auto& entry = state_->selected(); entry && !entry->media.empty()) rect.height = mediaRect.y - 8 - rect.y;
    return rect;
}
void ArchiveCanvas::buildEditor(d::Page& page) {
    const auto& selected = state_->selected();
    if (!selected) return;
    const ArchiveEntry entry = *selected;
    documentParts(entry);
    auto& category = page.make<d::Dropdown>("category", text("Category", "分类"), categoryName(entry.categoryID), std::nullopt, d::Dropdown::Height::regular, 178.0);
    category.onActivate = [this, &category] { if (menuOpen()) closeMenu(); else openCategoryMenu(category); };
    header().configure({"ARCHIVE", categoryName(entry.categoryID)}, "MOD-04", title("Document", "文档"),
        services_.dotted ? services_.dotted(entry.date) : std::string{}, true, {&category});
    wireBack(page, [this] { closeDocument(); });

    auto& titleField = *title_;
    titleField.layer().setOrigin({titleRect.x, titleRect.y});
    page.layer().add(titleField.layer());
    registerField(titleField); page.add(titleField); page.stagger(titleField.layer());

    auto& media = page.make<d::Button>("media", text("Media", "媒体"), d::Icon::image, d::Button::Kind::secondary, d::Button::Height::compact);
    media.place({0, 0});
    media.onActivate = [this, &media] { if (menuOpen()) closeMenu(); else openMediaMenu(media); };
    auto& bar = page.own<NotesFormatBar>(*this, "format", formatState(), archive_rich_text::families(services_.fonts), 156.0, 72.0, std::vector<d::Control*>{&media});
    bar.setOnFormat([this](const core::notes::FormatChange& change) { applyFormat(change); });
    bar.layer().setOrigin({barRect.x, barRect.y});
    page.layer().add(bar.layer());
    page.add(bar.controls()); page.stagger(bar.layer());
    formatBar_ = &bar;
    body_->formatsSelection = [this, &bar](Point point) { return bar.formatsSelection(point, root()); };

    auto& body = *body_;
    const auto rect = bodyRect();
    body.setSize({rect.width, rect.height});
    body.layer().setOrigin({rect.x, rect.y});
    page.layer().add(body.layer());
    page.add(body); page.stagger(body.layer());
    if (!leaving_) body.attach();

    if (!entry.media.empty()) buildMedia(page, entry); else clearMedia();

    auto& footer = page.makeLayer("archive.footer");
    const bool classified = state_->selectedClassification() != ArchiveClassificationStatus::unclassified;
    auto& protect = page.make<d::Button>(classified ? "declassify" : "classify", classified ? text("Declassify", "取消机密") : text("Classify", "设为机密"),
        classified ? d::Icon::unlock : d::Icon::lock, d::Button::Kind::secondary, d::Button::Height::compact);
    auto& remove = page.make<d::Button>("delete", text("Delete", "删除"), d::Icon::trash, d::Button::Kind::secondary, d::Button::Height::compact);
    remove.place({0, 0}); protect.place({0, 0});
    footer.setFrame({0, footerY, d::metric::boxWidth, 26});
    remove.layer().setOrigin({d::metric::boxWidth - remove.layer().bounds().width, 0});
    protect.layer().setOrigin({remove.layer().frame().x - 8 - protect.layer().bounds().width, 0});
    footer.add(protect.layer()); footer.add(remove.layer());
    page.layer().add(footer);
    protect.onActivate = [this, classified, &protect] { if (classified) confirmDeclassify(); else openClassify(protect); };
    remove.onActivate = [this] { confirmDelete(); };
    binders_.push_back([this, &protect, &remove] {
        const bool busy = state_->isClassificationBusy();
        protect.setEnabled(!busy); remove.setEnabled(!busy);
    });
    binders_.back()();
    page.add(protect); page.add(remove); page.stagger(footer);
}
// The title field and the body editor of the open document, reused by
// in-place re-renders so focus, selection, composition and undo survive
// (:602-628). Windows makes both once in the canvas's arena (layer ids are
// unique there) and reloads them for another document, which also drops
// the previous one's undo, focus and composition like the Mac's new objects.
void ArchiveCanvas::documentParts(const ArchiveEntry& entry) {
    if (documentID_ == entry.id && title_ && body_) return;
    discardDocument();
    if (!title_) {
        d::TextFieldOptions options;
        options.style = d::TextStyle{18, d::FontWeight::bold, env().palette.token(d::ThemeToken::textOnDark)};
        options.maximumLength = 200;
        options.inset = {12, 0};
        title_ = &canvasBuilder().make<d::TextField>("title", text("Title", "标题"), entry.title, text("Title", "标题"), d::Size{titleRect.width, titleRect.height}, options);
        title_->onChange = [this](const std::string& value) { titleChanged(value); };
        // Return commits and flushes (:614); a commit without typing (UIA
        // SetValue on Windows) carries the text in too.
        title_->onCommit = [this](const std::string& value) { titleChanged(value); state_->flush(); };
        title_->onTab = [this](bool backward) { if (!backward && body_) body_->focus(std::nullopt); };
    } else {
        title_->label = text("Title", "标题");
        title_->setPlaceholder(text("Title", "标题"));
        title_->setText(entry.title);
        title_->frame().setState(title_->frame().state());
    }
    if (!body_) {
        body_ = &canvasBuilder().make<ArchiveBodyArea>("body", text("Content", "内容"), text("Start writing", "开始书写"), std::uint32_t(2 * 1024 * 1024));
        body_->setHost(bodyHost_);
        body_->onChange = [this] { bodyChanged(); };
        body_->onSelectionChange = [this] { updateFormatBar(); };
        body_->onEndEditing = [this] { state_->flush(); };
        body_->onFormatShortcut = [this](core::notes::FormatKind kind) { core::notes::FormatChange change; change.kind = kind; applyFormat(change); };
        body_->onFocusChange = [this](bool focused) {
            if (focused && context().onLockParallax) context().onLockParallax();
            updateFormatBar();
            if (context().onInputStateChange) context().onInputStateChange();
        };
    } else body_->label = text("Content", "内容");
    const auto text16 = utf16(entry.body);
    body_->load(text16, archive_rich_text::loaded(text16, entry.bodyRichText, entry.bodyStyle), archive_rich_text::typingStyle(entry));
    documentID_ = entry.id;
}
void ArchiveCanvas::discardDocument() {
    if (title_ && title_->editing()) title_->endEditing(true);
    if (body_) body_->dispose();
    documentID_.reset();
}
void ArchiveCanvas::titleChanged(const std::string& value) {
    const auto& selected = state_->selected();
    if (!selected || selected->id != documentID_ || state_->isSelectedLocked()) return;
    std::string clean;
    for (const char c : value) if (c != '\0') clean.push_back(c);
    clean = env().characters.prefix(clean, 200);
    if (clean == selected->title) return;
    auto entry = *selected;
    // A plain field edits the title; earlier per-run title styling no longer matches it.
    entry.title = std::move(clean); entry.titleRichText.reset();
    if (state_->update(std::move(entry), services_.foundationNow(), ownerTime())) edited_ = true;
    stateChanged();
}
void ArchiveCanvas::bodyChanged() {
    const auto& selected = state_->selected();
    if (!body_ || !selected || selected->id != documentID_ || state_->isSelectedLocked() || body_->hasMarkedText()) return;
    auto& document = body_->document();
    // Over 2 MiB of UTF-8: the excess is deleted at the last whole-character
    // boundary, keeping the formatting of the kept text (:649-654).
    const auto value = utf8(document.text());
    if (value.size() > archiveMaximumBodyBytes) {
        std::size_t bytes = archiveMaximumBodyBytes;
        while (bytes > 0 && (static_cast<unsigned char>(value[bytes]) & 0xC0) == 0x80) --bytes;
        const auto kept = units(std::string_view(value).substr(0, bytes));
        body_->replace({kept, std::uint32_t(document.text().size())}, u"");
    }
    auto entry = *selected;
    entry.body = utf8(document.text());
    // ArchiveRichText.capture: an empty text captures an empty run list (not nil).
    entry.bodyRichText = document.coveringRichText();
    entry.bodyStyle = document.typingStyle();
    if (state_->update(std::move(entry), services_.foundationNow(), ownerTime())) edited_ = true;
    body_->textChanged();
    stateChanged();
}

// --- Format bar (:685-706) ------------------------------------------------------------------------
// What the bar shows: the selection (or typing style) while the body is
// focused, otherwise the whole document.
NotesFormatState ArchiveCanvas::formatState() const {
    NotesFormatState state;
    if (!body_ || !documentID_) { state.family = std::string(archive_rich_text::defaultFamily); state.size = archive_rich_text::newDocumentSize; return state; }
    const auto style = archive_rich_text::displayedStyle(body_->document(), body_->isFocused());
    state.family = archive_rich_text::family(style, services_.fonts);
    state.size = style.fontSize;
    state.bold = style.bold; state.italic = style.italic; state.underline = style.underline; state.strikethrough = style.strikethrough;
    state.color = archive_rich_text::hex(style.color);
    return state;
}
void ArchiveCanvas::updateFormatBar() { if (formatBar_) formatBar_->show(formatState()); }
// Formatting applies to the selection while the body is focused (the
// typing style with no selection), otherwise to the whole document.
void ArchiveCanvas::applyFormat(const core::notes::FormatChange& change) {
    if (!body_ || !documentID_ || state_->isSelectedLocked()) return;
    const bool focused = body_->isFocused();
    body_->applyFormat(archive_rich_text::stored(change), archive_rich_text::formatRange(body_->document(), focused), focused);
    bodyChanged();
    updateFormatBar();
}

// --- Header category menu (:708-722) --------------------------------------------------------------
void ArchiveCanvas::openCategoryMenu(const d::Control& anchor) {
    const auto& selected = state_->selected();
    if (!selected) return;
    const std::string document = selected->id;
    std::vector<d::Menu::Item> items;
    d::Menu::Item none; none.id = "uncategorized"; none.title = text("Uncategorized", "未分类"); none.selected = !selected->categoryID;
    items.push_back(std::move(none));
    for (const auto& c : state_->categories()) { d::Menu::Item item; item.id = c.id; item.title = c.name; item.selected = c.id == selected->categoryID; items.push_back(std::move(item)); }
    openMenu("moveTo", std::move(items), anchor, std::nullopt, [this, document](const d::Menu::Item& item) {
        const auto& current = state_->selected();
        if (!current || current->id != document) return;
        const std::optional<std::string> target = item.id == "uncategorized" ? std::nullopt : std::optional(item.id);
        if (target == current->categoryID) return;
        state_->moveSelected(target, services_.foundationNow(), ownerTime());
        stateChanged();
        if (!state_->selected() || state_->selected()->categoryID != target) return;
        toast(d::Icon::folder, "Moved", "已移动", categoryName(target));
    });
}

// --- Delete and classify (:724-847) ----------------------------------------------------------------
void ArchiveCanvas::confirmDelete() {
    const auto& selected = state_->selected();
    if (!selected) return;
    const std::string id = selected->id;
    d::ConfirmRequest request;
    request.id = "deleteDocument";
    request.title = text("Delete this document?", "删除此文档？");
    request.message = selected->title.empty() ? text("Untitled", "未命名") : selected->title;
    request.confirmTitle = text("Delete", "删除");
    request.danger = true; request.icon = d::Icon::trash;
    confirm(std::move(request), [this, id] {
        if (!state_->selected() || state_->selected()->id != id) return;
        edited_ = false;
        holding([&] { state_->deleteSelected(); stateChanged(); });
        navigate(-1);
        toast(d::Icon::trash, "Document deleted", "文档已删除");
    });
}
void ArchiveCanvas::confirmDeclassify() {
    if (!state_->selected()) return;
    d::ConfirmRequest request;
    request.id = "declassify";
    request.title = text("Remove classification?", "取消机密？");
    request.message = text("The password is deleted and the text stays readable.", "密码将被删除，文本保持可读。");
    request.confirmTitle = text("Declassify", "取消机密");
    request.icon = d::Icon::unlock;
    confirm(std::move(request), [this] {
        state_->declassifySelected([this](std::exception_ptr failure) {
            if (!failure) toast(d::Icon::unlock, "Declassified", "已取消机密");
            else toast(d::Icon::warn, "Declassify failed", "无法取消机密", errorText(failureMessage(failure)));
            stateChanged();
        });
        stateChanged();
    });
}
// Two password fields (>= 4 characters, matching), Cancel / Classify (:753-775).
void ArchiveCanvas::openClassify(const d::Control& anchor) {
    const auto& selected = state_->selected();
    if (!selected) return;
    const std::string id = selected->id;
    endEditing(true);
    auto form = passwordForm(title("Classify", "设为机密"), 300, "classifySubmit", text("Classify", "设为机密"), std::nullopt, [this, id](std::string password) {
        if (!state_->selected() || state_->selected()->id != id) return;
        state_->classifySelected(std::move(password), [this, id](std::exception_ptr failure) {
            if (!failure) {
                sealing_ = id;
                closePopover();
                toast(d::Icon::lock, "Document classified", "已设为机密");
            } else toast(d::Icon::warn, "Classify failed", "无法设为机密", errorText(failureMessage(failure)));
            stateChanged();
        });
        stateChanged();
    }, {});
    auto fields = form.fields;
    openPopover(std::move(form.popover), &anchor, std::nullopt, std::move(form.controls));
    popoverFields_ = fields;
    if (!fields.empty()) fields.front()->beginEditing();
}
// The shared pop-over form of Classify and Recover: New / Confirm password,
// then [extra] Cancel and the primary action. Validation turns the offending
// field red (too short or long first, then a mismatch) (:777-832).
ArchiveCanvas::PasswordForm ArchiveCanvas::passwordForm(const d::ModuleTitle& heading, double width, std::string submitID, std::string submitTitle,
    std::optional<std::pair<std::string, std::string>> extra, std::function<void(std::string)> submit, std::function<void()> extraAction) {
    const auto lines = heading.lines(env().language);
    const double contentHeight = 32 + 8 + 32 + 10 + 26;
    auto popover = std::make_unique<d::Popover>(env(), env().text.uppercase ? env().text.uppercase(lines.title) : d::asciiUppercase(lines.title),
        lines.subtitle.empty() ? std::nullopt : std::optional(lines.subtitle), width, contentHeight, false);
    auto& b = popover->builder();
    d::TextFieldOptions options;
    options.secure = true; options.inset = {12, 0};
    const double inner = width - 24;
    auto& first = b.make<d::TextField>("newPassword", text("New password", "新密码"), "", text("New password", "新密码"), d::Size{inner, 32}, options);
    auto& second = b.make<d::TextField>("confirmPassword", text("Confirm password", "确认密码"), "", text("Confirm password", "确认密码"), d::Size{inner, 32}, options);
    auto& cancelButton = b.make<d::Button>("passwordCancel", text("Cancel", "取消"), std::nullopt, d::Button::Kind::secondary, d::Button::Height::compact);
    auto& action = b.make<d::Button>(std::move(submitID), std::move(submitTitle), d::Icon::lock, d::Button::Kind::primary, d::Button::Height::compact);
    d::Button* extraButton = extra ? &b.make<d::Button>(extra->first, extra->second, d::Icon::unlock, d::Button::Kind::secondary, d::Button::Height::compact) : nullptr;
    for (auto* button : {&cancelButton, &action, extraButton}) if (button) button->place({0, 0});
    first.layer().setOrigin({0, 0});
    second.layer().setOrigin({0, 40});
    double x = inner;
    for (auto* button : {&action, &cancelButton, extraButton}) {
        if (!button) continue;
        x -= button->layer().bounds().width;
        button->layer().setOrigin({x, 82});
        x -= 6;
    }
    auto& content = popover->content();
    content.add(first.layer()); content.add(second.layer()); content.add(cancelButton.layer()); content.add(action.layer());
    if (extraButton) content.add(extraButton->layer());
    const auto validate = [this, &first, &second, &action, submit] {
        if (!action.enabled()) return;
        const auto issue = archivePasswordIssue(first.text(), second.text(), state_->textRules());
        if (issue == ArchivePasswordIssue::tooShort || issue == ArchivePasswordIssue::tooLong) {
            first.setInvalid(true); second.setInvalid(false);
            first.beginEditing(std::pair{0u, units(first.text())});
        } else if (issue == ArchivePasswordIssue::mismatch) {
            first.setInvalid(false); second.setInvalid(true);
            second.beginEditing(std::pair{0u, units(second.text())});
        } else { first.setInvalid(false); second.setInvalid(false); submit(first.text()); }
    };
    first.onTab = [&second](bool) { second.beginEditing(); };
    second.onTab = [&first](bool) { first.beginEditing(); };
    first.onReturn = validate; second.onReturn = validate;
    for (auto* field : {&first, &second}) field->onCancel = [this] { closePopoverAndFields(); };
    cancelButton.onActivate = [this] { closePopoverAndFields(); };
    action.onActivate = validate;
    if (extraButton) extraButton->onActivate = extraAction;
    // Pop-over binders die with the pop-over (didClosePopover).
    popoverBinders_.push_back([this, &action, extraButton] {
        const bool idle = !state_->isClassificationBusy();
        action.setEnabled(idle);
        if (extraButton) extraButton->setEnabled(idle);
    });
    popoverBinders_.back()();
    registerField(first); registerField(second);
    PasswordForm form;
    form.controls = {&first, &second, &cancelButton, &action};
    if (extraButton) form.controls.push_back(extraButton);
    form.fields = {&first, &second};
    form.popover = std::move(popover);
    return form;
}
// closePopover (:834-842): the form's fields end without commit; a refresh
// follows (didClosePopover).
void ArchiveCanvas::closePopoverAndFields() {
    for (auto* field : std::exchange(popoverFields_, {})) { field->onCancel = nullptr; field->onCommit = nullptr; if (field->editing()) field->endEditing(false); }
    closePopover();
}
void ArchiveCanvas::didClosePopover() {
    popoverFields_.clear();
    popoverBinders_.clear();
    refreshSoon();
}
void ArchiveCanvas::didCloseMenu() { refreshSoon(); }

// --- Locked view (:849-1006) ----------------------------------------------------------------------
void ArchiveCanvas::buildLocked(d::Page& page, const std::string& id) {
    const auto& selected = state_->selected();
    if (!selected) return;
    const ArchiveEntry entry = *selected;
    auto& chip = page.own<d::Chip>("archive.classifiedChip", text("Classified", "机密"), d::Chip::Variant::custom, d::Icon::lock,
        std::pair{d::fixed::red, d::white(1)});
    auto& badge = page.make<StaticControl>("classifiedChip", chip.layer());
    header().configure({"ARCHIVE", categoryName(entry.categoryID), "CLASSIFIED"}, "MOD-04", title("Document", "机密文档"), "LOCKED", true, {&badge});
    wireBack(page, [this] { closeDocument(); });
    const auto t = [this](std::string_view en, std::string_view zh) { return text(en, zh); };
    auto& panel = page.own<LockedPanel>(entry.title.empty() ? text("Untitled", "未命名") : entry.title, entry.body, d::Size{lockedRect.width, lockedRect.height}, t);
    panel.layer().setOrigin({lockedRect.x, lockedRect.y});
    page.layer().add(panel.layer());
    page.stagger(panel.layer());
    lockedPanel_ = &panel;
    if (sealing_ == id) { sealing_.reset(); panel.playSealing(clock()); }
    const auto status = state_->selectedClassification();
    if (status == ArchiveClassificationStatus::passwordMissing || status == ArchiveClassificationStatus::vaultUnavailable) {
        const auto message = archiveClassificationErrorText(status == ArchiveClassificationStatus::passwordMissing
            ? ArchiveClassificationErrorCode::passwordMissing : ArchiveClassificationErrorCode::vaultUnavailable, env().language);
        auto& recoverButton = page.make<d::Button>("recover", text("Recover", "恢复访问"), d::Icon::unlock, d::Button::Kind::primary);
        recoverButton.onActivate = [this, &recoverButton] { openRecovery(recoverButton); };
        std::vector<d::Button*> buttons{&recoverButton};
        if (status == ArchiveClassificationStatus::vaultUnavailable) {
            auto& retry = page.make<d::Button>("retry", text("Retry", "重试"), d::Icon::refresh, d::Button::Kind::secondary);
            retry.onActivate = [this] { state_->refreshSelectedClassification(); stateChanged(); };
            buttons.insert(buttons.begin(), &retry);
        }
        panel.showRecovery(message, buttons);
        binders_.push_back([this, buttons] {
            const bool busy = state_->isClassificationBusy();
            for (auto* button : buttons) button->setEnabled(!busy);
        });
        for (auto* button : buttons) page.add(*button);
    } else {
        d::TextFieldOptions options;
        options.secure = true; options.inset = {12, 0};
        auto& unlock = page.make<d::Button>("unlock", text("Unlock", "解锁"), d::Icon::unlock, d::Button::Kind::primary);
        unlock.place({0, 0});
        auto& field = page.make<d::TextField>("password", text("Password", "密码"), "", text("Password", "密码"),
            d::Size{panel.fieldWidth(unlock.layer().bounds().width), 32}, options);
        field.onReturn = [this] { submitUnlock(); };
        unlock.onActivate = [this] { submitUnlock(); };
        panel.showPassword(field, unlock);
        registerField(field);
        page.add(field); page.add(unlock);
        passwordField_ = &field; unlockButton_ = &unlock;
        binders_.push_back([this, &unlock, &field] {
            const bool idle = !state_->isClassificationBusy() && !opening_;
            unlock.setEnabled(idle); field.setEnabled(!opening_);
        });
    }
    for (auto& binder : binders_) binder();
    focusPasswordSoon();
}
// The password field takes focus whenever the locked view is shown, one
// run-loop turn later (:909-918).
void ArchiveCanvas::focusPasswordSoon() {
    after(0, soonTimer_, [this] {
        if (!isActive() || transitioning() || popoverOpen() || menuOpen() || opening_ || unlocking_) return;
        auto* field = passwordField_;
        if (!field || field->editing() || !d::isVisible(field->layer(), root()) || context().overlay().capturesInput()) return;
        field->beginEditing();
    });
}
void ArchiveCanvas::submitUnlock() {
    auto* field = passwordField_;
    const auto& selected = state_->selected();
    if (!field || !selected || opening_ || unlocking_ || state_->isClassificationBusy()) return;
    const std::string id = selected->id;
    if (field->editing()) field->endEditing(true);
    const auto password = field->text();
    unlocking_ = id;
    state_->unlockSelected(password, [this, id](ArchiveUnlockResult result, std::string message) {
        unlocking_.reset();
        switch (result) {
        case ArchiveUnlockResult::unlocked:
            if (state_->selected() && state_->selected()->id == id) { playOpening(id); stateChanged(); return; }
            refreshWhenPossible();
            break;
        case ArchiveUnlockResult::wrongPassword: {
            auto* f = passwordField_;
            if (!f) return;
            f->setInvalid(true);
            if (lockedPanel_) lockedPanel_->shake(clock());
            if (f->editing()) f->endEditing(true);
            f->beginEditing(std::pair{0u, units(f->text())});
            break;
        }
        case ArchiveUnlockResult::failed:
            toast(d::Icon::warn, "Unlock failed", "无法解锁", errorText(message));
            refreshWhenPossible();
            break;
        default:
            refreshWhenPossible();
            break;
        }
        stateChanged();
    });
    stateChanged();
}
// The bars wipe away line by line, the stamp fades and grows, the bar
// drops; then the editor opens in place (spec 3.9.4 `.opening`, :947-969).
void ArchiveCanvas::playOpening(const std::string& id) {
    opening_ = id;
    if (passwordField_) passwordField_->setText("");
    for (auto& binder : binders_) binder();
    if (state_->selected() && state_->selected()->id == id && lockedPanel_) lockedPanel_->reveal();
    const double duration = lockedPanel_ ? lockedPanel_->playOpening(clock()) : 0;
    cancel(openingTimer_);
    after(env().reduceMotion ? 0 : duration, openingTimer_, [this] { finishOpening(true); });
}
void ArchiveCanvas::finishOpening(bool announce) {
    if (!opening_) return;
    cancel(openingTimer_);
    opening_.reset();
    if (!announce) { needsRefresh_ = true; return; }
    refreshWhenPossible();
    toast(d::Icon::unlock, "Document unlocked", "已解除封存");
}
// For a missing or unreadable password: Windows owner authentication (Windows
// Hello or the account password; the Mac's macOS owner authentication),
// then a new password (the document stays classified and opens) or
// Declassify (:971-1006).
void ArchiveCanvas::openRecovery(const d::Control& anchor) {
    const auto& selected = state_->selected();
    if (!selected) return;
    const std::string id = selected->id;
    auto form = passwordForm(title("Recover", "恢复访问"), 330, "recoverSubmit", text("Save", "保存"),
        std::pair{std::string("recoverDeclassify"), text("Declassify", "取消机密")},
        [this, id](std::string password) { recover(id, std::move(password)); }, [this, id] { recover(id, std::nullopt); });
    auto fields = form.fields;
    openPopover(std::move(form.popover), &anchor, std::nullopt, std::move(form.controls));
    popoverFields_ = fields;
    if (!fields.empty()) fields.front()->beginEditing();
}
void ArchiveCanvas::recover(const std::string& id, std::optional<std::string> password) {
    if (!state_->selected() || state_->selected()->id != id || recovering_) return;
    endEditing(true);
    recovering_ = true;
    const bool declassify = !password;
    state_->recoverSelectedClassification(std::move(password), text("restore access to a classified document", "恢复对机密文档的访问"),
        [this, declassify](std::exception_ptr failure) {
            recovering_ = false;
            std::optional<ArchiveClassificationErrorCode> code;
            try { if (failure) std::rethrow_exception(failure); } catch (const ArchiveClassificationError& e) { code = e.code(); } catch (...) {}
            if (!failure) {
                closePopover();
                if (declassify) toast(d::Icon::unlock, "Declassified", "已取消机密");
                else toast(d::Icon::unlock, "Access restored", "已恢复访问");
            } else if (code != ArchiveClassificationErrorCode::cancelled) {
                toast(d::Icon::warn, "Recovery failed", "无法恢复访问", errorText(failureMessage(failure)));
            }
            stateChanged();
        });
    stateChanged();
}

// --- Media (:1008-1141) ---------------------------------------------------------------------------
void ArchiveCanvas::openMediaMenu(const d::Control& anchor) {
    const auto& selected = state_->selected();
    if (!selected || selected->media.size() >= maximumMedia) {
        toast(d::Icon::warn, "Media not added", "未添加媒体", text("A document holds up to 16 items.", "每个文档最多 16 项。"));
        return;
    }
    const auto choices = services_.shelfChoices ? services_.shelfChoices() : std::vector<NotesShelfChoice>{};
    // "Choose in Finder" names the Mac file browser; Windows names its own
    // (the explicit five-language label of v1.2.0, shared with Notes and Projection).
    std::vector<d::Menu::Item> items;
    d::Menu::Item finder; finder.id = "finder"; finder.title = std::string(core::translated(core::chooseInFileExplorer, env().language)); items.push_back(std::move(finder));
    if (!choices.empty()) { d::Menu::Item shelf; shelf.id = "shelf"; shelf.title = text("Choose from Shelf", "从暂存架选择"); items.push_back(std::move(shelf)); }
    const std::string anchorID = anchor.id();
    openMenu("media", std::move(items), anchor, std::nullopt, [this, choices, anchorID](const d::Menu::Item& item) {
        if (item.id == "finder") { chooseFiles(); return; }
        std::vector<d::Menu::Item> shelf;
        for (const auto& c : choices) { d::Menu::Item entry; entry.id = c.id; entry.title = c.title; entry.enabled = c.supported && c.available; shelf.push_back(std::move(entry)); }
        const auto& controls = page().controls();
        const auto found = std::find_if(controls.begin(), controls.end(), [&](const d::Control* c) { return c->id() == anchorID; });
        if (found == controls.end()) return;
        openMenu("shelf", std::move(shelf), **found, 220.0, [this](const d::Menu::Item& choice) {
            const auto& selected = state_->selected();
            if (!selected || !services_.requestMedia) return;
            ArchiveMediaRequest request{ArchiveMediaRequest::Kind::shelf, selected->id, choice.id, maximumMedia - std::min(maximumMedia, selected->media.size()), 0, ++mediaGeneration_};
            if (!services_.requestMedia(request)) --mediaGeneration_;
        });
    });
}
void ArchiveCanvas::chooseFiles() {
    const auto& selected = state_->selected();
    if (!selected || panel_ || !services_.requestMedia) return;
    ArchiveMediaRequest request{ArchiveMediaRequest::Kind::chooseFiles, selected->id, {}, maximumMedia - std::min(maximumMedia, selected->media.size()), 0, ++mediaGeneration_};
    panel_ = services_.requestMedia(request);
}
// Windows (v1.2.0 "mediaRelink"): choose the imported Mac attachment's file
// again; the reply replaces exactly this attachment, in its place.
void ArchiveCanvas::relinkMedia() {
    const auto& selected = state_->selected();
    if (!selected || panel_ || !services_.requestMedia || mediaIndex_ >= selected->media.size() || !archiveMacMediaReference(selected->media[mediaIndex_])) return;
    ArchiveMediaRequest request{ArchiveMediaRequest::Kind::relink, selected->id, {}, 1, mediaIndex_, ++mediaGeneration_};
    panel_ = services_.requestMedia(request);
}
// Files from the panel, the shelf or a drop on the open document (<= 16
// per document, :1046-1074); a relink replaces its attachment.
bool ArchiveCanvas::receiveMedia(const ArchiveMediaRequest& request, std::vector<ArchiveJson> references, std::optional<std::string> error) {
    if (request.kind != ArchiveMediaRequest::Kind::shelf) panel_ = false;
    const auto& selected = state_->selected();
    if (request.generation != mediaGeneration_ || !selected || selected->id != request.document || state_->isSelectedLocked()
        || !renderedStructure_ || renderedStructure_->page.kind != PageKind::editor) return false;
    for (const auto& r : references) validateArchiveMedia(r);
    auto entry = *selected;
    if (request.kind == ArchiveMediaRequest::Kind::relink) {
        // A relink keeps the attachment's place; a movie stays a movie, a picture a picture.
        if (references.empty() || request.index >= entry.media.size() || !archiveMacMediaReference(entry.media[request.index])
            || (entry.media[request.index]["kind"] == ArchiveJson("video")) != (references.front()["kind"] == ArchiveJson("video"))) {
            if (error || !references.empty()) {
                const auto unsupported = notesMediaErrorText(NotesMediaError::unsupportedFormat);
                toast(d::Icon::warn, "Media not added", "未添加媒体", references.empty() ? *error : core::localized(unsupported.english, unsupported.simplifiedChinese, env().language));
            }
            return true;
        }
        auto mac = entry.media[request.index];
        const auto path = references.front()["windowsPath"].isString() ? references.front()["windowsPath"].string() : std::string{};
        entry.media[request.index] = std::move(references.front());
        const auto id = entry.id;
        // Record first (saved at once), then the Mac import relink ledger row.
        if (state_->update(std::move(entry), services_.foundationNow(), ownerTime()) && !path.empty()) { edited_ = true; (void)state_->mediaRelinked(id, std::move(mac), path); }
        presentationKey_.reset();
        stateChanged();
        return true;
    }
    const auto count = std::min(maximumMedia - std::min(maximumMedia, entry.media.size()), references.size());
    if (count > 0) {
        const auto first = references.front()["displayName"].isString() ? references.front()["displayName"].string() : std::string{};
        mediaIndex_ = entry.media.size();
        for (std::size_t n = 0; n < count; ++n) entry.media.push_back(std::move(references[n]));
        const auto id = entry.id;
        if (state_->update(std::move(entry), services_.foundationNow(), ownerTime())) { state_->recordAction("mediaAdded", id); edited_ = true; }
        toast(d::Icon::image, "Media added", "已添加媒体", count == 1 ? first : first + " +" + std::to_string(count - 1));
        stateChanged();
        refreshWhenPossible();
    } else if (error) toast(d::Icon::warn, "Media not added", "未添加媒体", *error);
    return true;
}
std::optional<ArchiveMediaRequest> ArchiveCanvas::dropRequest() {
    const auto& selected = state_->selected();
    if (!isActive() || capturesPointer() || panel_ || !selected || state_->isSelectedLocked() || !renderedStructure_
        || renderedStructure_->page.kind != PageKind::editor || selected->media.size() >= maximumMedia) return std::nullopt;
    return ArchiveMediaRequest{ArchiveMediaRequest::Kind::chooseFiles, selected->id, {}, maximumMedia - selected->media.size(), 0, ++mediaGeneration_};
}
void ArchiveCanvas::buildMedia(d::Page& page, const ArchiveEntry& entry) {
    mediaIndex_ = std::min(mediaIndex_, entry.media.size() - 1);
    if (!leaving_) syncMedia(entry);
    const auto& reference = entry.media[mediaIndex_];
    const auto t = [this](std::string_view en, std::string_view zh) { return text(en, zh); };
    const bool playing = services_.media && services_.media->playing();
    const auto failed = services_.media ? services_.media->error() : std::nullopt;
    auto& strip = page.own<MediaStrip>(reference, mediaIndex_, entry.media.size(), playing, failed,
        archiveMacMediaReference(reference), d::Size{mediaRect.width, mediaRect.height}, t);
    strip.layer().setOrigin({mediaRect.x, mediaRect.y});
    page.layer().add(strip.layer());
    if (strip.previous_) strip.previous_->onActivate = [this] { stepMedia(-1); };
    if (strip.next_) strip.next_->onActivate = [this] { stepMedia(1); };
    if (strip.play_) strip.play_->onActivate = [this] { if (services_.media) services_.media->togglePlayback(); };
    strip.remove_->onActivate = [this] { removeMedia(); };
    if (strip.relink_) strip.relink_->onActivate = [this] { relinkMedia(); };
    if (strip.seek_) strip.seek_->onSeek = [this](double fraction) {
        const auto& selected = state_->selected();
        if (!services_.media || !selected || mediaIndex_ >= selected->media.size()) return;
        const auto& duration = selected->media[mediaIndex_]["duration"];
        if (duration.isNumber()) services_.media->seek(duration.number() * fraction);
    };
    progressFill_ = strip.seek_ ? &strip.seek_->fill() : nullptr;
    preview_ = &strip.preview();
    updateProgress();
    page.add(strip.controls()); page.stagger(strip.layer());
}
void ArchiveCanvas::stepMedia(int step) {
    const auto& selected = state_->selected();
    if (!selected || selected->media.empty()) return;
    const auto count = selected->media.size();
    mediaIndex_ = (mediaIndex_ + std::size_t(step < 0 ? count - 1 : 1)) % count;
    refreshWhenPossible();
}
void ArchiveCanvas::removeMedia() {
    const auto& selected = state_->selected();
    if (!selected || mediaIndex_ >= selected->media.size() || state_->isSelectedLocked()) return;
    auto entry = *selected;
    auto removed = entry.media[mediaIndex_];
    entry.media.erase(entry.media.begin() + std::ptrdiff_t(mediaIndex_));
    mediaIndex_ = std::min(mediaIndex_, entry.media.empty() ? 0 : entry.media.size() - 1);
    const auto id = entry.id;
    if (state_->update(std::move(entry), services_.foundationNow(), ownerTime())) {
        state_->recordAction("mediaRemoved", id); edited_ = true;
        // A removed imported Mac attachment leaves the relink ledger behind its save (Windows).
        if (archiveMacMediaReference(removed)) (void)state_->mediaRemoved(id, std::move(removed));
    }
    stateChanged();
    refreshWhenPossible();
}
void ArchiveCanvas::syncMedia(const ArchiveEntry& entry) {
    if (entry.media.empty() || !services_.media) { clearMedia(); return; }
    const auto index = std::min(mediaIndex_, entry.media.size() - 1);
    const auto& reference = entry.media[index];
    const auto key = entry.id + ":" + std::to_string(index) + ":" + reference.encode(1 << 20);
    if (presentationKey_ == key) return;
    presentationKey_ = key;
    services_.media->show(entry.id, index, reference);
}
void ArchiveCanvas::clearMedia() {
    presentationKey_.reset();
    if (services_.media) services_.media->clear();
}
// Loading reports from show(): coalesced outside the build (:1118-1127).
void ArchiveCanvas::mediaStateChanged() {
    if (mediaRefreshQueued_) return;
    mediaRefreshQueued_ = true;
    after(0, soonTimer_, [this] { mediaRefreshQueued_ = false; refreshWhenPossible(); });
}
void ArchiveCanvas::updateProgress() {
    const auto& selected = state_->selected();
    if (!progressFill_ || !services_.media || !selected || mediaIndex_ >= selected->media.size()) return;
    const auto& duration = selected->media[mediaIndex_]["duration"];
    if (!duration.isNumber()) return;
    progressFill_->setValue(d::Channel::scaleX, std::clamp(services_.media->currentTime() / std::max(.01, duration.number()), 0.0, 1.0));
}

// --- Focus (Windows B14: the field and caret survive a module switch, :1143-1167) ---------------
bool ArchiveCanvas::editingOtherText() const { return body_ && documentID_ && body_->isFocused(); }
void ArchiveCanvas::endOtherEditing(bool) { if (body_) body_->blur(); }
std::optional<ArchiveCanvas::Focus> ArchiveCanvas::currentFocus() const {
    if (!documentID_ || !renderedStructure_ || renderedStructure_->page.kind != PageKind::editor || renderedStructure_->page.document != *documentID_) return std::nullopt;
    if (title_ && title_->editing()) if (const auto range = title_->selectedRange()) return Focus{*documentID_, Field::title, *range};
    if (body_ && body_->isFocused()) {
        const auto r = body_->document().selection().range;
        return Focus{*documentID_, Field::body, {r.start, r.end - r.start}};
    }
    return std::nullopt;
}
void ArchiveCanvas::restoreFocus() {
    auto focus = std::exchange(focusMemory_, std::nullopt);
    if (!focus || focus->document != documentID_ || !renderedStructure_ || renderedStructure_->page.kind != PageKind::editor
        || renderedStructure_->page.document != focus->document || transitioning()) return;
    if (focus->field == Field::title) { if (title_) title_->beginEditing(focus->selection); }
    else if (body_) body_->focus(core::text::Range{focus->selection.first, focus->selection.first + focus->selection.second});
}

// --- Input routing (:1169-1211) --------------------------------------------------------------------
// The body keeps focus while the format bar edits its selection; a
// double-click on the selected category plate renames it.
bool ArchiveCanvas::interceptPointerDown(Point point, const d::PointerInput& input) {
    if (body_ && documentID_ && body_->isFocused() && !menuOpen() && !popoverOpen() && !(body_->formatsSelection && body_->formatsSelection(point))) body_->blur();
    if (input.clickCount == 2 && !menuOpen() && !popoverOpen() && !rename_ && filter_.kind == FilterKind::category) {
        if (const auto* p = plate(filter_.category); p && d::isVisible(p->layer(), root()) && [&] { const auto r = p->rect(root());
                return point.x >= r.x && point.y >= r.y && point.x < r.x + r.width && point.y < r.y + r.height; }() && d::isUnclipped(p->layer(), point, root())) {
            beginRename();
            return true;
        }
    }
    return false;
}
bool ArchiveCanvas::handleScroll(Point point, Point delta) {
    if (body_ && documentID_ && d::isVisible(body_->layer(), root())) {
        const auto r = body_->rect(root());
        if (point.x >= r.x && point.y >= r.y && point.x < r.x + r.width && point.y < r.y + r.height) { body_->scroll(delta.y); return true; }
    }
    if (gridArea_ && d::isVisible(gridArea_->layer(), root())) {
        const auto r = gridArea_->layer().frame();
        if (point.x >= r.x && point.y >= r.y && point.x < r.x + r.width && point.y < r.y + r.height) {
            if (gridArea_->scroll(delta.y)) { layoutTiles(page(), false); loadVisibleThumbnails(); }
            return true;
        }
    }
    return false;
}
// Cmd+N (exactly Command; Windows: exactly Ctrl) creates a document (:1206-1211).
bool ArchiveCanvas::handleKey(const d::KeyInput& key) {
    if (!key.control || key.alt || key.win) return false;   // Shift does not matter, as on the Mac
    const char32_t c = key.character >= U'A' && key.character <= U'Z' ? key.character + 32 : key.character;
    if (c != U'n') return false;
    createDocument();
    return true;
}

// --- Accessibility (:1213-1231) ----------------------------------------------------------------------
// Documents scrolled out of the grid have no tile; the screen reader still
// lists them (at the grid's frame) and can open them.
void ArchiveCanvas::accessibilityItems(std::vector<d::AccessibilityItem>& out) const {
    const auto before = out.size();
    Canvas::accessibilityItems(out);
    if (out.size() == before || !renderedStructure_ || renderedStructure_->page.kind != PageKind::list || !gridArea_ || shownDocuments_.size() <= tiles_.size()) return;
    const auto frame = d::convert(d::localBounds(gridArea_->layer()), gridArea_->layer(), root());
    for (const auto& summary : shownDocuments_) {
        if (tile(summary.id)) continue;
        const auto date = services_.dotted ? services_.dotted(summary.date) : std::string{};
        const auto title = summary.isClassified ? text("Classified document", "机密文档") : (summary.title.empty() ? text("Untitled", "未命名") : summary.title);
        d::AccessibilityItem item;
        item.id = "doc:" + summary.id; item.label = title + ", " + date; item.role = d::AccessibilityRole::button; item.rect = frame;
        out.push_back(std::move(item));
    }
}
bool ArchiveCanvas::accessibilityAction(std::string_view id) {
    if (!id.starts_with("doc:")) return false;
    const std::string document(id.substr(4));
    if (tile(document) || std::none_of(shownDocuments_.begin(), shownDocuments_.end(), [&](const auto& s) { return s.id == document; })) return false;
    openDocument(document);
    return true;
}
// UIA Value on the body (Windows: the v1.2.0 field peers' SetValue): the
// whole text is replaced through the editor (one undo group).
bool ArchiveCanvas::accessibilityText(std::string_view id, std::string_view value) {
    if (!body_ || !documentID_ || id != body_->id() || state_->isSelectedLocked()) return false;
    if (!body_->attach()) return false;
    if (!body_->replace({0, std::uint32_t(body_->document().text().size())}, utf16(value))) return false;
    bodyChanged();
    return true;
}

// --- Gallery thumbnails (:1233-1254) -------------------------------------------------------------
void ArchiveCanvas::loadVisibleThumbnails() {
    if (!isActive() || !renderedStructure_ || renderedStructure_->page.kind != PageKind::list) { posters_->setWanted({}); return; }
    std::vector<const ArchiveSummary*> visible;
    for (const auto& s : shownDocuments_) if (tile(s.id) && !s.isClassified && s.mediaCount > 0 && visible.size() < Posters::wantedLimit) visible.push_back(&s);
    std::erase_if(thumbnailReferences_, [&](const auto& r) { return std::none_of(visible.begin(), visible.end(), [&](const auto* s) { return s->id == r.first; }); });
    std::vector<std::pair<std::string, ArchiveJson>> wanted;
    for (const auto* s : visible) {
        if (s->thumbnail) wanted.emplace_back(s->id, *s->thumbnail);
        else if (const auto* r = Posters::find(thumbnailReferences_, s->id)) wanted.emplace_back(s->id, *r);
    }
    posters_->setWanted(std::move(wanted));
    const auto generation = thumbnailGeneration_;
    for (const auto* s : visible) {
        if (s->thumbnail || Posters::find(thumbnailReferences_, s->id) || std::find(metadataRequests_.begin(), metadataRequests_.end(), s->id) != metadataRequests_.end()
            || posters_->contains(s->id)) continue;
        metadataRequests_.push_back(s->id);
        const std::string id = s->id;
        state_->loadThumbnail(id, [this, id, generation, alive = posters_->alive](std::optional<ArchiveJson> reference) {
            if (!*alive || thumbnailGeneration_ != generation) return;
            std::erase(metadataRequests_, id);
            if (!reference || !tile(id)) return;
            thumbnailReferences_.emplace_back(id, *reference);
            posters_->request(id, *reference);
        });
    }
}

// --- Strings ---------------------------------------------------------------------------------------------
std::string ArchiveCanvas::errorText(std::string_view message) const { return archiveErrorText(message, env().language); }
std::optional<std::string> ArchiveCanvas::formattingField() const {
    if (body_ && documentID_ && body_->isFocused()) return "body";
    if (title_ && title_->editing()) return "title";
    return std::nullopt;
}
std::string archiveErrorText(std::string_view message, core::Language language) {
    // ArchiveError (ArchiveStore.swift:99-111); the Windows store words its
    // unreadable archive "The archive ..." where the Mac says "This archive ...".
    struct Pair { std::string_view windows, english, chinese; };
    static constexpr std::array<Pair, 5> pairs{{
        {"The archive could not be read. The original was preserved.", "This archive could not be read. The original was preserved.", "无法读取档案，原始数据已保留。"},
        {"This archive could not be read. The original was preserved.", "This archive could not be read. The original was preserved.", "无法读取档案，原始数据已保留。"},
        {"The archive could not be saved.", "The archive could not be saved.", "无法保存档案。"},
        {"This archive needs a newer app version.", "This archive needs a newer app version.", "此档案需要更新版本的应用。"},
        {"The archive is full.", "The archive is full.", "档案库已满。"}}};
    for (const auto& p : pairs) if (message == p.windows) return core::localized(p.english, p.chinese, language);
    for (const auto code : {ArchiveClassificationErrorCode::passwordMissing, ArchiveClassificationErrorCode::vaultUnavailable})
        if (message == archiveClassificationErrorText(code, core::Language::english)) return archiveClassificationErrorText(code, language);
    return std::string(message);
}
} // namespace endfield::modules
