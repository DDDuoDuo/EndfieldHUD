#include "app/quit_confirmation.hpp"
#include "core/source_desktop_chrome.hpp"
#include "native/layer_scene.hpp"
#include "native/renderer.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace endfield::app {
namespace {
namespace gpu = native;
namespace m = modules;
using Json = ehud::data::Json;
using Matrix = core::Matrix4;
using Rect = core::Rect;
void need(bool b, const char* s) { if (!b) throw std::invalid_argument(s); }
Json blank() { return Json::Object{{"bounds", Json::Array{0, 0, 0, 0}}, {"children", Json::Array{}}}; }
bool contains(Rect a, Rect b) { return a.x <= b.x && a.y <= b.y && a.x + a.width >= b.x + b.width && a.y + a.height >= b.y + b.height; }
Rect intersection(Rect a, Rect b) {
    const auto x = std::max(a.x, b.x), y = std::max(a.y, b.y);
    return {x, y, std::max(0., std::min(a.x + a.width, b.x + b.width) - x), std::max(0., std::min(a.y + a.height, b.y + b.height) - y)};
}

// Retained projected card + exact-opacity surround, the same decomposition as
// NativeSettingsSafety (one shared source confirmation view): one private
// native group covers the card and its local dim; four disjoint exterior
// rectangles apply the same root alpha without a fullscreen target.
class NativeQuitCard final {
public:
    NativeQuitCard(QuitConfirmationState& s, gpu::LayerRasterizer& r, gpu::LayerRasterOptions o)
        : state_(s), options_(std::move(o)), card_(r), carrier_(r) {
        carrier_.load(blank(), options_);
        static std::atomic<std::uint64_t> next{};
        groupID_ = "quit.confirmation." + std::to_string(next.fetch_add(1));
        quadID_ = groupID_ + ".dim";
        local_.reserve(10);
        for (unsigned n = 0; n < 4; ++n) { published_[n].sourceID = groupID_ + ".surround." + std::to_string(n); published_[n].meshID = quadID_; published_[n].linearTint = {0, 0, 0, 1}; }
    }
    bool syncContent(Rect viewport, double now) {
        need(std::isfinite(now), "Invalid quit confirmation time");
        if (seen_ == state_.revision() && viewport_ == viewport && !artwork_.surfaces.empty()) return updateFeedback(now);
        auto art = prepareQuitConfirmationArtwork(state_, viewport, state_.appearance(), now);
        if (viewport_ != viewport) cover_ = {};
        if (artwork_.surfaces.empty()) {
            card_.load(art.layers, options_);
            need(card_.report().unsupported.empty(), "Unsupported quit confirmation artwork");
            placements_.resize(art.surfaces.size());
            for (std::size_t n = 0; n < art.surfaces.size(); ++n) {
                const auto index = card_.surfaceIndex(art.surfaces[n].id);
                need(index.has_value(), "Missing quit confirmation leaf");
                placements_[n] = {*index, art.surfaces[n].local, 1, {}};
            }
            rasterizations_ += art.surfaces.size();
            contentEpoch_ = card_.contentRevision();
        } else {
            // Stable leaf identities: only changed leaves (two small plates on
            // focus/hover, all labels on a language change) are re-rasterized.
            const auto& before = artwork_.layers["children"].array();
            const auto& after = art.layers["children"].array();
            need(before.size() == after.size(), "Unexpected quit confirmation shape");
            for (std::size_t n = 0; n < after.size(); ++n)
                if ((n == 2 || n == 3 ? buttonLeaves_[n - 2] : before[n]) != after[n]) {
                    card_.updateLocalContent(art.surfaces[n].id, ++contentEpoch_, after[n], options_); ++rasterizations_;
                }
        }
        for (unsigned n = 0; n < 2; ++n) { buttonLeaves_[n] = art.layers["children"].array()[n + 2]; painted_[n] = state_.buttonPaint(n, now); }
        artwork_ = std::move(art); seen_ = state_.revision(); viewport_ = viewport;
        return true;
    }
    void updatePose(Rect viewport, const Matrix& tilt, double now, float opacityScale) {
        need(viewport_ == viewport && seen_ == state_.revision() && tilt.finite(), "Synchronize the quit confirmation before placement");
        const auto geometry = m::SettingsSafety::geometry(viewport);
        const auto phase = state_.pose(now);
        Matrix transformed = tilt;
        transformed.values[13] = phase.travel; // Source CABasicAnimation targets transform.translation.y.
        cardWorld_ = Matrix::translation(geometry.card.x + geometry.card.width * .5, geometry.card.y + 95) * transformed * Matrix::translation(-geometry.card.width * .5, -95);
        hit_ = core::Projection{{cardWorld_.values[0], cardWorld_.values[4], cardWorld_.values[12], cardWorld_.values[1], cardWorld_.values[5], cardWorld_.values[13],
                                 cardWorld_.values[3], cardWorld_.values[7], cardWorld_.values[15]}};
        std::optional<Rect> bounds;
        const auto include = [&](core::Point p) {
            const auto q = hit_.project(p);
            need(bool(q), "Invalid quit confirmation projection");
            if (!bounds) bounds = Rect{q->x, q->y, 0, 0};
            else {
                const auto x = std::min(bounds->x, q->x), y = std::min(bounds->y, q->y);
                const auto right = std::max(bounds->x + bounds->width, q->x), bottom = std::max(bounds->y + bounds->height, q->y);
                *bounds = {x, y, right - x, bottom - y};
            }
        };
        for (auto p : std::array<core::Point, 4>{{{-2, -2}, {geometry.card.width + 2, -2}, {geometry.card.width + 2, 192}, {-2, 192}}}) include(p);
        const auto visible = intersection(*bounds, viewport);
        need(visible.width > 0 && visible.height > 0, "Quit confirmation has no visible coverage");
        if (!contains(cover_, visible)) {
            constexpr double grid = 64;
            const double left = std::floor(visible.x / grid) * grid, top = std::floor(visible.y / grid) * grid;
            const double right = std::ceil((visible.x + visible.width) / grid) * grid, bottom = std::ceil((visible.y + visible.height) / grid) * grid;
            cover_ = intersection({left, top, right - left, bottom - top}, viewport);
        }
        for (std::size_t n = 0; n < placements_.size(); ++n) placements_[n].world = cardWorld_ * artwork_.surfaces[n].local;
        card_.setPlacements(placements_);
        const auto& r = cover_;
        const std::array<Rect, 4> surround{{{viewport.x, viewport.y, viewport.width, r.y - viewport.y},
                                           {viewport.x, r.y, r.x - viewport.x, r.height},
                                           {r.x + r.width, r.y, viewport.x + viewport.width - r.x - r.width, r.height},
                                           {viewport.x, r.y + r.height, viewport.width, viewport.y + viewport.height - r.y - r.height}}};
        const auto dim = state_.appearance().dark ? .30 : .22;
        for (unsigned n = 0; n < 4; ++n) {
            const auto b = surround[n];
            published_[n].world = Matrix::translation(b.x, b.y) * Matrix::scale(b.width, b.height);
            published_[n].opacity = float(phase.opacity * dim * opacityScale);
        }
        published_[4].opacity = float(phase.opacity * opacityScale);
        published_[4].world = {};
        localOpacity_ = float(dim);
        posed_ = true;
    }
    std::optional<int> actionAt(core::Point p) const {
        if (!posed_ || !state_.presented()) return {};
        const auto q = hit_.unproject(p);
        if (!q) return {};
        const auto g = m::SettingsSafety::geometry(*viewport_);
        if (g.cancel.contains(*q)) return 0;
        if (g.confirm.contains(*q)) return 1;
        return {};
    }
    std::optional<core::Point> actionPoint(bool confirm) const {
        if (!posed_) return {};
        const auto g = m::SettingsSafety::geometry(*viewport_);
        const auto r = confirm ? g.confirm : g.cancel;
        return hit_.project({r.x + r.width * .5, r.y + r.height * .5});
    }
    void upload(gpu::Renderer& r) {
        need(posed_, "Place the quit confirmation before uploading");
        if (!quadUploaded_) {
            const std::array<gpu::Vertex, 4> vertices{{{{0, 0, 0}, {0, 0}, {1, 1, 1, 1}}, {{1, 0, 0}, {1, 0}, {1, 1, 1, 1}}, {{1, 1, 0}, {1, 1}, {1, 1, 1, 1}}, {{0, 1, 0}, {0, 1}, {1, 1, 1, 1}}}};
            constexpr std::array<std::uint32_t, 6> indices{0, 1, 2, 0, 2, 3};
            r.setMesh(quadID_, 1, {vertices, indices});
            quadUploaded_ = true;
        }
        if (uploadedCardRevision_ != card_.resourceRevision()) { card_.uploadResources(r); uploadedCardRevision_ = card_.resourceRevision(); }
        const auto draws = card_.prepareDraws();
        if (local_.empty()) {
            local_.resize(draws.size() + 1);
            auto& background = local_[0];
            background.sourceID = groupID_ + ".localDim"; background.meshID = quadID_; background.linearTint = {0, 0, 0, 1};
            for (std::size_t n = 0; n < draws.size(); ++n) { local_[n + 1] = draws[n]; local_[n + 1].masks.reserve(8); }
        }
        auto& background = local_[0];
        background.opacity = localOpacity_;
        background.world = Matrix::translation(cover_.x, cover_.y) * Matrix::scale(cover_.width, cover_.height);
        for (std::size_t n = 0; n < draws.size(); ++n) {
            auto& target = local_[n + 1];
            const auto& source = draws[n];
            need(target.sourceID == source.sourceID && target.meshID == source.meshID && target.textureID == source.textureID, "Quit confirmation identities changed");
            target.world = source.world; target.opacity = source.opacity; target.masks.assign(source.masks.begin(), source.masks.end());
        }
        if (!uploaded_ || uploadedCoverage_ != cover_) { r.configureNativeGroup(groupID_, {cover_, options_.pixelsPerPoint}, local_); groupOwned_ = true; uploadedCoverage_ = cover_; }
        else r.setNativeGroupDraws(groupID_, local_);
        const auto& output = r.nativeGroupOutput(groupID_);
        if (!uploaded_) { published_[4].sourceID = output.sourceID; published_[4].meshID = output.meshID; published_[4].textureID = output.textureID; uploaded_ = true; }
        card_.collectRetiredResources(r);
    }
    gpu::LayerCompositionEntry entry() { need(uploaded_, "Upload the quit confirmation before publishing"); return {&carrier_, published_}; }
    bool releaseResources(gpu::Renderer& r) {
        if (groupOwned_ && !r.removeNativeGroup(groupID_)) return false;
        uploaded_ = groupOwned_ = false; uploadedCardRevision_ = 0;
        bool ok = card_.releaseResources(r) && carrier_.releaseResources(r);
        if (quadUploaded_) { if (r.removeMesh(quadID_)) quadUploaded_ = false; else ok = false; }
        return ok;
    }
private:
    bool updateFeedback(double now) {
        bool changed = false;
        for (unsigned n = 0; n < 2; ++n) {
            const auto paint = state_.buttonPaint(n, now);
            if (paint == painted_[n]) continue;
            auto next = buttonLeaves_[n];
            const auto color = [](const m::SettingsSafetyColor& c) { return Json::Object{{"sRGB", Json::Array{c[0], c[1], c[2], c[3]}}}; };
            next["shape"]["fillColor"] = color(paint.fill); next["shape"]["strokeColor"] = color(paint.stroke); next["shape"]["lineWidth"] = paint.width;
            card_.updateLocalContent(artwork_.surfaces[n + 2].id, ++contentEpoch_, next, options_);
            buttonLeaves_[n] = std::move(next); painted_[n] = paint; ++rasterizations_; changed = true;
        }
        return changed;
    }
    QuitConfirmationState& state_;
    gpu::LayerRasterOptions options_;
    gpu::LayerScene card_, carrier_;
    m::SettingsArtworkPart artwork_;
    std::array<Json, 2> buttonLeaves_;
    std::array<m::SettingsSafetyButtonPaint, 2> painted_{};
    std::vector<gpu::LayerPlacement> placements_;
    std::vector<gpu::DrawObject> local_;
    std::array<gpu::DrawObject, 5> published_;
    std::string groupID_, quadID_;
    std::uint64_t seen_{~std::uint64_t{0}}, contentEpoch_{}, uploadedCardRevision_{};
    std::optional<Rect> viewport_, uploadedCoverage_;
    Rect cover_{};
    Matrix cardWorld_;
    core::Projection hit_;
    float localOpacity_{.30f};
    bool posed_{}, uploaded_{}, groupOwned_{}, quadUploaded_{};
    std::size_t rasterizations_{};
};

class QuitConfirmationModule final : public QuitConfirmationOwner {
    gpu::LayerRasterizer& raster_;
    QuitConfirmationCallbacks callbacks_;
    QuitConfirmationState state_;
    std::unique_ptr<NativeQuitCard> card_;
    std::array<gpu::LayerCompositionEntry, 1> modal_{};
    ClientMetrics metrics_;
    double time_{};
    void answer(std::optional<QuitAnswer> value, double time) {
        if (!value || *value == QuitAnswer::none) return;
        if (*value == QuitAnswer::cancel) { state_.dismiss(true, time); if (callbacks_.changed) callbacks_.changed(time); return; }
        // confirmQuit: accepted synchronously so a focus loss cannot cancel a
        // queued quit while the card stays; the closing path removes it.
        if (callbacks_.confirmed) callbacks_.confirmed(time);
    }
public:
    QuitConfirmationModule(gpu::LayerRasterizer& raster, QuitConfirmationCallbacks callbacks) : raster_(raster), callbacks_(std::move(callbacks)) {}
    std::string_view name() const noexcept override { return "quitConfirmation"; }
    ModuleRouting routing() const noexcept override {
        ModuleRouting r; r.pointerCapture = 0; r.wheelCapture = 0; r.keyCapture = 0; r.pointerPolicy = {false, false, false}; return r;
    }
    void present(double time) override { time_ = std::max(time_, time); state_.show(time_); }
    void hide(double time) override { time_ = std::max(time_, time); state_.hideImmediately(time_); }
    bool presented() const noexcept override { return state_.presented(); }
    const QuitConfirmationState& state() const noexcept override { return state_; }
    std::optional<core::Point> actionPoint(bool confirm) const override { return card_ ? card_->actionPoint(confirm) : std::nullopt; }
    void resize(const ClientMetrics& m) override { metrics_ = m; }
    void setAppearance(const ModuleAppearance& a, double time) override {
        time_ = std::max(time_, time);
        state_.setLanguage(a.language); state_.setAppearance({a.dark, 2, a.accent}); state_.setReduceMotion(a.reduceMotion, time_);
    }
    void overlayClosing(double) override {} // The card fades with the closing canvas and hides at concealment.
    void update(const ModuleFrame& f) override {
        time_ = std::max(time_, f.time);
        state_.refresh(time_);
        if (!state_.presented() || !metrics_.width || !metrics_.height) return;
        if (!card_) {
            gpu::LayerRasterOptions options; options.pixelsPerPoint = 2; options.paddingPoints = 1;
            card_ = std::make_unique<NativeQuitCard>(state_, raster_, options);
        }
        const Rect viewport{0, 0, metrics_.width, metrics_.height};
        card_->syncContent(viewport, time_);
        const auto layout = core::source::DesktopChromeLayout::make(f.chrome, f.center, {});
        card_->updatePose(viewport, m::SettingsSafety::centeredSourceTransform(f.center, layout.designScale), time_, f.opacity);
    }
    bool requiresFrames(double time) const override { return state_.requiresFrames(std::max(time, time_)); }
    void upload(gpu::Renderer& r) override { if (card_ && state_.presented()) card_->upload(r); }
    std::span<const gpu::LayerCompositionEntry> modalEntries() override {
        if (!card_ || !state_.presented()) return {};
        modal_[0] = card_->entry();
        return modal_;
    }
    void collected(gpu::Renderer& r) override {
        if (card_ && !state_.presented()) { need(card_->releaseResources(r), "Quit confirmation still published"); card_.reset(); }
    }
    void release(gpu::Renderer& r) override { if (card_) { need(card_->releaseResources(r), "Quit confirmation still published at teardown"); card_.reset(); } }
    bool covers(core::Point) const override { return state_.presented(); }
    bool capturesPointer() const override { return state_.presented(); }
    bool capturesWheel() const override { return state_.presented(); }
    bool capturesKeys() const override { return state_.presented(); }
    bool suppressesKeyFilters() const override { return state_.presented(); }
    bool pointer(const PointerEvent& e, double time) override {
        if (!state_.presented()) return false;
        time_ = std::max(time_, time);
        const auto action = card_ ? card_->actionAt({e.x, e.y}) : std::nullopt;
        if (e.kind == PointerKind::down && e.button == PointerButton::left) answer(state_.pointerDown(action, time_), time_);
        else if (e.kind == PointerKind::up && e.button == PointerButton::left) answer(state_.pointerUp(action, time_), time_);
        else if (e.kind == PointerKind::move) state_.pointerMove(action, time_);
        else if (e.kind == PointerKind::leave || e.kind == PointerKind::captureLost) { (void)state_.pointerUp({}, time_); state_.pointerMove({}, time_); }
        return true;
    }
    bool wheel(const WheelEvent&, double) override { return state_.presented(); }
    bool key(const KeyEvent& e, double time) override {
        if (!state_.presented()) return false;
        time_ = std::max(time_, time);
        if (e.kind == KeyKind::down) answer(state_.key(e.value, e.previouslyDown, time_), time_);
        return true;
    }
};
}

std::unique_ptr<QuitConfirmationOwner> makeQuitConfirmation(gpu::LayerRasterizer& raster, QuitConfirmationCallbacks callbacks) {
    return std::make_unique<QuitConfirmationModule>(raster, std::move(callbacks));
}
} // namespace endfield::app
