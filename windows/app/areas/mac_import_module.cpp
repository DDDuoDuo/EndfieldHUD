#include "app/areas/mac_import_module.hpp"
#include "app/application_host.hpp"
#include "app/areas/mac_import_card.hpp"
#include "app/areas/mac_import_flow.hpp"
#include "app/areas/mac_import_handoff.hpp"
#include "app/utility_executor.hpp"
#include "core/data/file_io.hpp"
#include "core/migration/mac_import_files.hpp"
#include "core/migration/mac_import_native.hpp"
#include "core/migration/mac_import_outcome.hpp"
#include "core/migration/plist.hpp"
#include "core/source_desktop_chrome.hpp"
#include "modules/settings_safety.hpp"
#include "native/layer_scene.hpp"
#include "native/renderer.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace endfield::areas {
namespace {
namespace fs = std::filesystem;
namespace g = native;
namespace m = modules;
namespace mig = ehud::migration;
using app::ApplicationAreaMode;
using Json = ehud::data::Json;
using Matrix = core::Matrix4;
using Rect = core::Rect;
using Entries = std::span<const g::LayerCompositionEntry>;

void need(bool value, const char* why) { if (!value) throw std::runtime_error(std::string("macOS import: ") + why); }
Json blank() { return Json::Object{{"bounds", Json::Array{0, 0, 0, 0}}, {"children", Json::Array{}}}; }
bool contains(Rect a, Rect b) { return a.x <= b.x && a.y <= b.y && a.x + a.width >= b.x + b.width && a.y + a.height >= b.y + b.height; }
Rect intersection(Rect a, Rect b) {
    const auto x = std::max(a.x, b.x), y = std::max(a.y, b.y);
    return {x, y, std::max(0., std::min(a.x + a.width, b.x + b.width) - x), std::max(0., std::min(a.y + a.height, b.y + b.height) - y)};
}
double systemUnixNow() { return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count(); }
std::string utf8(const fs::path& path) { const auto t = path.u8string(); return std::string(reinterpret_cast<const char*>(t.data()), t.size()); }
fs::path utf8Path(std::string_view text) { return fs::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size())); }

// Retained projected card + exact-opacity surround: the decomposition of the
// red power button confirmation (app/quit_confirmation.cpp). One private
// native group covers the card and its local dim; four disjoint exterior
// rectangles apply the same root alpha without a fullscreen target. The leaf
// structure is fixed (mac_import_card.hpp), so a content event re-rasterizes
// only the leaves whose artwork changed; warm frames only re-pose.
class NativeImportCard final {
public:
    NativeImportCard(const MacImportCardState& s, g::LayerRasterizer& r, g::LayerRasterOptions o)
        : state_(s), options_(std::move(o)), card_(r), carrier_(r) {
        carrier_.load(blank(), options_);
        static std::atomic<std::uint64_t> next{};
        prefix_ = "macImport.card." + std::to_string(next.fetch_add(1));
        groupID_ = prefix_ + ".group";
        quadID_ = prefix_ + ".dim";
        local_.reserve(macImportCardLeaves + 1);
        for (unsigned n = 0; n < 4; ++n) {
            published_[n].sourceID = prefix_ + ".surround." + std::to_string(n);
            published_[n].meshID = quadID_;
            published_[n].linearTint = {0, 0, 0, 1};
        }
    }
    bool sync(const MacImportCardContent& content, std::uint64_t contentRevision, Rect viewport, double now) {
        need(std::isfinite(now), "Invalid import card time");
        if (loaded_ && seenState_ == state_.revision() && seenContent_ == contentRevision && viewport_ == viewport) return updateFeedback(now);
        auto art = prepareMacImportCardArtwork(content, state_, viewport, state_.appearance(), prefix_, now);
        if (viewport_ != viewport) cover_ = {};
        const auto& after = art.layers["children"].array();
        if (!loaded_) {
            card_.load(art.layers, options_);
            need(card_.report().unsupported.empty(), "Unsupported import card artwork");
            for (std::size_t n = 0; n < art.leaves.size(); ++n) {
                const auto index = card_.surfaceIndex(art.leaves[n].id);
                need(index.has_value(), "Missing import card leaf");
                placements_[n] = {*index, {}, 1, {}};
            }
            rasterizations_ += art.leaves.size();
            contentEpoch_ = card_.contentRevision();
            loaded_ = true;
        } else {
            const auto& before = artwork_.layers["children"].array();
            need(before.size() == after.size(), "Unexpected import card shape");
            for (std::size_t n = 0; n < after.size(); ++n) {
                const bool plate = n == static_cast<std::size_t>(MacImportLeaf::cancelPlate) || n == static_cast<std::size_t>(MacImportLeaf::confirmPlate);
                const auto& previous = plate ? buttonLeaves_[n - static_cast<std::size_t>(MacImportLeaf::cancelPlate)] : before[n];
                if (previous != after[n]) { card_.updateLocalContent(art.leaves[n].id, ++contentEpoch_, after[n], options_); ++rasterizations_; }
            }
        }
        for (unsigned n = 0; n < 2; ++n) {
            buttonLeaves_[n] = after[static_cast<std::size_t>(MacImportLeaf::cancelPlate) + n];
            painted_[n] = state_.buttonPaint(n, now);
        }
        artwork_ = std::move(art);
        seenState_ = state_.revision(); seenContent_ = contentRevision; viewport_ = viewport;
        return true;
    }
    void updatePose(Rect viewport, const Matrix& tilt, double now, float opacityScale) {
        need(viewport_ == viewport && seenState_ == state_.revision() && tilt.finite(), "Synchronize the import card before placement");
        const auto geometry = macImportCardGeometry(viewport);
        const auto phase = state_.pose(now);
        const double halfWidth = geometry.card.width * .5, halfHeight = macImportCardHeight * .5;
        Matrix transformed = tilt;
        transformed.values[13] = phase.travel; // Source CABasicAnimation targets transform.translation.y.
        cardWorld_ = Matrix::translation(geometry.card.x + halfWidth, geometry.card.y + halfHeight) * transformed * Matrix::translation(-halfWidth, -halfHeight);
        hit_ = core::Projection{{cardWorld_.values[0], cardWorld_.values[4], cardWorld_.values[12], cardWorld_.values[1], cardWorld_.values[5], cardWorld_.values[13],
                                 cardWorld_.values[3], cardWorld_.values[7], cardWorld_.values[15]}};
        std::optional<Rect> bounds;
        const auto include = [&](core::Point p) {
            const auto q = hit_.project(p);
            need(bool(q), "Invalid import card projection");
            if (!bounds) bounds = Rect{q->x, q->y, 0, 0};
            else {
                const auto x = std::min(bounds->x, q->x), y = std::min(bounds->y, q->y);
                const auto right = std::max(bounds->x + bounds->width, q->x), bottom = std::max(bounds->y + bounds->height, q->y);
                *bounds = {x, y, right - x, bottom - y};
            }
        };
        for (auto p : std::array<core::Point, 4>{{{-2, -2}, {geometry.card.width + 2, -2}, {geometry.card.width + 2, macImportCardHeight + 2}, {-2, macImportCardHeight + 2}}}) include(p);
        const auto visible = intersection(*bounds, viewport);
        need(visible.width > 0 && visible.height > 0, "Import card has no visible coverage");
        if (!contains(cover_, visible)) {
            constexpr double grid = 64;
            const double left = std::floor(visible.x / grid) * grid, top = std::floor(visible.y / grid) * grid;
            const double right = std::ceil((visible.x + visible.width) / grid) * grid, bottom = std::ceil((visible.y + visible.height) / grid) * grid;
            cover_ = intersection({left, top, right - left, bottom - top}, viewport);
        }
        for (std::size_t n = 0; n < placements_.size(); ++n) {
            placements_[n].world = cardWorld_ * artwork_.leaves[n].local;
            placements_[n].opacity = artwork_.leaves[n].opacity;
        }
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
        const auto g = macImportCardGeometry(*viewport_);
        if (g.cancel.contains(*q)) return 0;
        if (g.confirm.contains(*q)) return 1;
        return {};
    }
    std::optional<core::Point> actionPoint(int button) const {
        if (!posed_) return {};
        const auto g = macImportCardGeometry(*viewport_);
        const auto r = button ? g.confirm : g.cancel;
        return hit_.project({r.x + r.width * .5, r.y + r.height * .5});
    }
    std::optional<core::Point> listPoint() const {
        if (!posed_) return {};
        const auto g = macImportCardGeometry(*viewport_);
        return hit_.project({g.list.x + g.list.width * .5, g.list.y + g.list.height * .5});
    }
    void upload(g::Renderer& r) {
        need(posed_, "Place the import card before uploading");
        if (!quadUploaded_) {
            const std::array<g::Vertex, 4> vertices{{{{0, 0, 0}, {0, 0}, {1, 1, 1, 1}}, {{1, 0, 0}, {1, 0}, {1, 1, 1, 1}}, {{1, 1, 0}, {1, 1}, {1, 1, 1, 1}}, {{0, 1, 0}, {0, 1}, {1, 1, 1, 1}}}};
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
        need(local_.size() == draws.size() + 1, "Import card draw count changed");
        auto& background = local_[0];
        background.opacity = localOpacity_;
        background.world = Matrix::translation(cover_.x, cover_.y) * Matrix::scale(cover_.width, cover_.height);
        for (std::size_t n = 0; n < draws.size(); ++n) {
            auto& target = local_[n + 1];
            const auto& source = draws[n];
            if (target.sourceID != source.sourceID || target.meshID != source.meshID || target.textureID != source.textureID) {
                // A re-rasterized leaf binds its new texture (content events only).
                target.sourceID = source.sourceID; target.meshID = source.meshID; target.textureID = source.textureID; rebound_ = true;
            }
            target.world = source.world; target.opacity = source.opacity; target.masks.assign(source.masks.begin(), source.masks.end());
        }
        if (!uploaded_ || uploadedCoverage_ != cover_ || rebound_) { r.configureNativeGroup(groupID_, {cover_, options_.pixelsPerPoint}, local_); groupOwned_ = true; uploadedCoverage_ = cover_; rebound_ = false; }
        else r.setNativeGroupDraws(groupID_, local_);
        const auto& output = r.nativeGroupOutput(groupID_);
        if (!uploaded_) { published_[4].sourceID = output.sourceID; published_[4].meshID = output.meshID; published_[4].textureID = output.textureID; uploaded_ = true; }
        card_.collectRetiredResources(r);
    }
    g::LayerCompositionEntry entry() { need(uploaded_, "Upload the import card before publishing"); return {&carrier_, published_}; }
    bool releaseResources(g::Renderer& r) {
        if (groupOwned_ && !r.removeNativeGroup(groupID_)) return false;
        uploaded_ = groupOwned_ = false; uploadedCardRevision_ = 0;
        bool ok = card_.releaseResources(r) && carrier_.releaseResources(r);
        if (quadUploaded_) { if (r.removeMesh(quadID_)) quadUploaded_ = false; else ok = false; }
        return ok;
    }
    std::size_t rasterizations() const noexcept { return rasterizations_; }
private:
    // Button feedback (.12 s colour/width tracks) repaints only the two plates.
    bool updateFeedback(double now) {
        bool changed = false;
        for (unsigned n = 0; n < 2; ++n) {
            const auto paint = state_.buttonPaint(n, now);
            if (paint == painted_[n]) continue;
            auto next = buttonLeaves_[n];
            const auto color = [](const m::SettingsSafetyColor& c) { return Json::Object{{"sRGB", Json::Array{c[0], c[1], c[2], c[3]}}}; };
            next["shape"]["fillColor"] = color(paint.fill); next["shape"]["strokeColor"] = color(paint.stroke); next["shape"]["lineWidth"] = paint.width;
            const auto leaf = static_cast<std::size_t>(MacImportLeaf::cancelPlate) + n;
            card_.updateLocalContent(artwork_.leaves[leaf].id, ++contentEpoch_, next, options_);
            buttonLeaves_[n] = std::move(next); painted_[n] = paint; ++rasterizations_; changed = true;
        }
        return changed;
    }
    const MacImportCardState& state_;
    g::LayerRasterOptions options_;
    g::LayerScene card_, carrier_;
    MacImportArtwork artwork_;
    std::array<Json, 2> buttonLeaves_;
    std::array<m::SettingsSafetyButtonPaint, 2> painted_{};
    std::array<g::LayerPlacement, macImportCardLeaves> placements_{};
    std::vector<g::DrawObject> local_;
    std::array<g::DrawObject, 5> published_;
    std::string prefix_, groupID_, quadID_;
    std::uint64_t seenState_{~std::uint64_t{0}}, seenContent_{~std::uint64_t{0}}, contentEpoch_{}, uploadedCardRevision_{};
    std::optional<Rect> viewport_, uploadedCoverage_;
    Rect cover_{};
    Matrix cardWorld_;
    core::Projection hit_;
    float localOpacity_{.30f};
    bool loaded_{}, posed_{}, uploaded_{}, groupOwned_{}, quadUploaded_{}, rebound_{};
    std::size_t rasterizations_{};
};

// A recorded outcome of the previous run and the unresolved relink entries.
struct PendingOutcome { mig::MacImportOutcome outcome; std::vector<mig::MacRelinkItem> relink; };

class MacImportArea final : public app::ModuleOwner, public app::MacImportEntry {
public:
    MacImportArea(const app::ApplicationModuleContext& c, fs::path destination, fs::path goldenFixture)
        : host_(*c.host), raster_(c.rasterizer), utility_(*c.utility), mode_(c.mode), destination_(std::move(destination)), coverageRoot_(c.coverageRoot),
          goldenFixture_(std::move(goldenFixture)), metrics_(c.metrics), alive_(std::make_shared<bool>(true)) {
        outcomeRoute_ = utility_.makeRoute();
        const auto& a = host_.appearance();
        language_ = a.language;
        state_.setAppearance({a.dark, 2, a.accent});
        flow_ = makeFlow();
        host_.publish<app::MacImportEntry>(app::macImportEntryService, *this);
        // A restart handoff of the previous run recorded its outcome next to
        // the data root: read it (and the relink list) on the shared worker.
        loadOutcome();
    }
    ~MacImportArea() override {
        *alive_ = false;
        host_.withdraw(app::macImportEntryService);
        // A queued note removal still runs; no completion reaches this owner.
        utility_.invalidate(outcomeRoute_, false);
        if (flow_) flow_->shutdown();
    }
    MacImportArea(const MacImportArea&) = delete;
    MacImportArea& operator=(const MacImportArea&) = delete;

    // --- MacImportEntry ----------------------------------------------------------
    bool present(double time) override {
        time_ = std::max(time_, time);
        flow_->open();
        flowChanged();
        return true;
    }
    std::string title(core::Language language) const override { return macImportEntryTitle(language); }

    // --- ModuleOwner -------------------------------------------------------------
    std::string_view name() const noexcept override { return "macImport"; }
    app::ModuleRouting routing() const noexcept override {
        // A modal card: captures before every main chain, after the built-in
        // modal captures (0...6), like the red power button confirmation.
        app::ModuleRouting r; r.pointerCapture = 20; r.wheelCapture = 20; r.keyCapture = 20; r.pointerPolicy = {false, false, false}; return r;
    }
    void resize(const app::ClientMetrics& metrics) override { metrics_ = metrics; }
    void setAppearance(const app::ModuleAppearance& a, double time) override {
        time_ = std::max(time_, time);
        language_ = a.language;
        flow_->setLanguage(a.language);
        state_.setAppearance({a.dark, 2, a.accent});
        state_.setReduceMotion(a.reduceMotion, time_);
    }
    void setOverlayVisible(bool visible, double time) override {
        time_ = std::max(time_, time);
        overlayVisible_ = visible;
        if (!visible) { cancelInteraction(time_); return; }
        if (!presentPendingOutcome()) syncCard();
    }
    // The card fades with the closing canvas and hides at concealment.
    void overlayClosing(double time) override { cancelInteraction(time); overlayVisible_ = false; }
    void update(const app::ModuleFrame& f) override {
        time_ = std::max(time_, f.time);
        state_.refresh(time_);
        if (!state_.presented() || !metrics_.width || !metrics_.height) return;
        if (!card_) {
            g::LayerRasterOptions options; options.pixelsPerPoint = 2; options.paddingPoints = 1;
            card_ = std::make_unique<NativeImportCard>(state_, raster_, options);
        }
        const Rect viewport{0, 0, metrics_.width, metrics_.height};
        card_->sync(flow_->content(), flow_->revision(), viewport, time_);
        const auto layout = core::source::DesktopChromeLayout::make(f.chrome, f.center, {});
        card_->updatePose(viewport, m::SettingsSafety::centeredSourceTransform(f.center, layout.designScale), time_, f.opacity);
    }
    bool requiresFrames(double time) const override { return state_.requiresFrames(std::max(time, time_)); }
    void upload(g::Renderer& r) override { if (card_ && state_.presented()) card_->upload(r); }
    Entries modalEntries() override {
        if (!card_ || !state_.presented()) return {};
        modal_[0] = card_->entry();
        return modal_;
    }
    void collected(g::Renderer& r) override {
        if (card_ && !state_.presented()) { need(card_->releaseResources(r), "Import card still published"); card_.reset(); }
    }
    void release(g::Renderer& r) override { if (card_) { need(card_->releaseResources(r), "Import card still published at teardown"); card_.reset(); } }
    bool covers(core::Point) const override { return state_.presented(); }
    bool capturesPointer() const override { return state_.presented(); }
    bool capturesWheel() const override { return state_.presented(); }
    bool capturesKeys() const override { return state_.presented(); }
    bool suppressesKeyFilters() const override { return state_.presented(); }
    bool pointer(const app::PointerEvent& e, double time) override {
        if (!state_.presented()) return false;
        time_ = std::max(time_, time);
        const auto action = card_ ? card_->actionAt({e.x, e.y}) : std::nullopt;
        if (e.kind == app::PointerKind::down && e.button == app::PointerButton::left) answer(state_.pointerDown(action, time_));
        else if (e.kind == app::PointerKind::up && e.button == app::PointerButton::left) answer(state_.pointerUp(action, time_));
        else if (e.kind == app::PointerKind::move) state_.pointerMove(action, time_);
        else if (e.kind == app::PointerKind::leave || e.kind == app::PointerKind::captureLost) { (void)state_.pointerUp({}, time_); state_.pointerMove({}, time_); }
        host_.requestFrame();
        return true;
    }
    bool wheel(const app::WheelEvent& e, double time) override {
        if (!state_.presented()) return false;
        time_ = std::max(time_, time);
        if (!e.horizontal && std::isfinite(e.steps)) {
            // One list row per detent; fractions accumulate (precision wheels).
            wheelRemainder_ += e.steps;
            const auto rows = static_cast<std::ptrdiff_t>(std::trunc(wheelRemainder_));
            wheelRemainder_ -= static_cast<double>(rows);
            if (rows) (void)flow_->scroll(-rows);
        }
        return true;
    }
    bool key(const app::KeyEvent& e, double time) override {
        if (!state_.presented()) return false;
        time_ = std::max(time_, time);
        if (e.kind == app::KeyKind::down) answer(state_.key(e.value, e.previouslyDown, time_));
        host_.requestFrame();
        return true;
    }
    void focus(bool value, double time) override { if (!value) cancelInteraction(time); }
    void cancelInteraction(double time) override {
        time_ = std::max(time_, time);
        if (!state_.presented()) return;
        (void)state_.pointerUp({}, time_);
        state_.pointerMove({}, time_);
    }
    bool hostEvent(const app::HostEvent& e, double time) override {
        using K = app::HostEvent::Kind;
        time_ = std::max(time_, time);
        switch (e.kind) {
        case K::utilityCompleted:
            flow_->utilityCompleted();
            if (outcomeQueued_) loadOutcome(); // the shared queue was full at construction
            return false;
        case K::overlayConcealed:
            // The HUD is hidden: the card goes with it; a running import keeps
            // going and its card returns when the HUD opens again.
            state_.hideImmediately(time_);
            return false;
        default: return false;
        }
    }
    void coverage(app::ModuleCoverage& step) override;

private:
    bool coverageMode() const noexcept { return mode_ == ApplicationAreaMode::hiddenCoverage; }
    double now() const { return host_.now(); }
    double unixNow() {
        // Hidden coverage keeps report contents deterministic across cycles.
        if (coverageMode()) return 1760000000.0 + double(++coverageClock_);
        return systemUnixNow();
    }
    std::unique_ptr<MacImportFlow> makeFlow() {
        MacImportFlow::Host h;
        std::weak_ptr<bool> alive = alive_;
        h.chooseFolder = [this, alive](const std::string& caption, const std::string& accept) {
            if (coverageMode()) { ++coverageChooser_; chooserPending_ = true; return true; }
            return host_.chooseFiles(app::ApplicationFileRequest{caption, accept, accept}, [this, alive](app::ApplicationFileResult result) {
                if (const auto token = alive.lock(); !token || !*token) return;
                flow_->folderChosen(result.cancelled, result.failed, std::move(result.paths));
                flowChanged();
            });
        };
        h.cancelChoice = [this] {
            if (coverageMode()) { chooserCancelled_ = true; return; } // the driver delivers the cancelled completion
            host_.cancelFileChoice();
        };
        h.inProcess = [this] { return coverageMode() && coverageInProcess_; };
        h.restart = [this](std::shared_ptr<mig::MacImportSession> session) { return requestRestart(std::move(session)); };
        h.restartAvailable = [this] { return coverageMode() || host_.find<app::DataRootRestart>(app::dataRootRestartService) != nullptr; };
        h.forgetOutcome = [this] { forgetOutcome(); };
        h.changed = [this] { flowChanged(); };
        h.unixNow = [this] { return unixNow(); };
        const bool coverage = coverageMode();
        return std::make_unique<MacImportFlow>(utility_, destination_, [coverage] {
            auto platform = mig::nativeMacImportPlatform();
            if (coverage) platform.unixNow = [] { return 1760000000.0; };
            return platform;
        }, std::move(h));
    }
    // Production: the host's restart handoff closes every store first.
    bool requestRestart(std::shared_ptr<mig::MacImportSession> session) {
        const auto destination = destination_;
        const bool coverage = coverageMode();
        auto work = [session = std::move(session), destination, coverage] {
            (void)mig::commitMacImportSession(*session, destination, coverage ? 1760000000.0 : systemUnixNow());
        };
        std::weak_ptr<bool> alive = alive_;
        auto declined = [this, alive] {
            if (const auto token = alive.lock(); !token || !*token) return;
            flow_->restartDeclined();
            flowChanged();
        };
        if (coverage) {
            // Synthetic handoff: the driver plays the host (quiescence, the
            // post-shutdown work, a declined quit).
            coverageWork_ = std::move(work);
            coverageDeclined_ = std::move(declined);
            ++coverageRestarts_;
            return true;
        }
        auto* restart = host_.find<app::DataRootRestart>(app::dataRootRestartService);
        return restart && restart->restartWith(std::move(work), std::move(declined));
    }
    void loadOutcome() {
        auto result = std::make_shared<std::optional<PendingOutcome>>();
        std::weak_ptr<bool> alive = alive_;
        const auto destination = destination_;
        outcomeQueued_ = false;
        outcomeLoading_ = utility_.submit(outcomeRoute_, [destination, result] {
            try {
                auto outcome = mig::readMacImportOutcome(destination);
                if (!outcome) return;
                std::vector<mig::MacRelinkItem> relink;
                if (outcome->state == mig::MacImportOutcome::State::completed) {
                    try { relink = mig::MacRelinkLedger(destination).unresolved(); } catch (const std::exception&) {}
                }
                *result = PendingOutcome{std::move(*outcome), std::move(relink)};
            } catch (const std::exception&) {
                // An unreadable note cannot be reported: drop it (the import's
                // own Migration/import.json and backup are untouched).
                (void)mig::removeMacImportOutcome(destination);
            }
        }, [this, alive, result](std::exception_ptr) {
            if (const auto token = alive.lock(); !token || !*token) return;
            outcomeLoading_ = false;
            if (*result) pending_ = std::move(**result);
            presentPendingOutcome();
        });
        outcomeQueued_ = !outcomeLoading_;
    }
    void forgetOutcome() {
        const auto destination = destination_;
        if (!utility_.submit(outcomeRoute_, [destination] { (void)mig::removeMacImportOutcome(destination); }, [](std::exception_ptr) {}))
            (void)mig::removeMacImportOutcome(destination); // full queue: one small file, removed here
    }
    bool presentPendingOutcome() {
        if (!pending_ || !overlayVisible_ || flow_->presented()) return false;
        auto outcome = std::move(*pending_);
        pending_.reset();
        flow_->showOutcome(std::move(outcome.outcome), std::move(outcome.relink));
        flowChanged();
        return true;
    }
    void flowChanged() {
        const auto t = std::max(time_, now());
        time_ = t;
        state_.setEnabled(flow_->content().enabled, t);
        if (flow_->step() != step_) {
            const bool fromHidden = step_ == MacImportStep::hidden;
            step_ = flow_->step();
            if (!fromHidden) state_.resetFocus(t);
        }
        syncCard();
        // A recorded outcome that arrived while another card was open is
        // shown once that card closes.
        if (pending_ && !flow_->presented()) (void)presentPendingOutcome();
        host_.requestFrame();
    }
    void syncCard() {
        const auto t = std::max(time_, now());
        if (!flow_->presented()) { if (state_.presented() && !state_.dismissing()) state_.dismiss(true, t); }
        else if (overlayVisible_ && (!state_.presented() || state_.dismissing())) state_.show(t);
    }
    void answer(std::optional<MacImportCardAction> action) {
        if (!action) return;
        switch (*action) {
        case MacImportCardAction::left: flow_->activate(0); break;
        case MacImportCardAction::right: flow_->activate(1); break;
        case MacImportCardAction::escape: flow_->escape(); break;
        case MacImportCardAction::scrollUp: (void)flow_->scroll(-1); break;
        case MacImportCardAction::scrollDown: (void)flow_->scroll(1); break;
        case MacImportCardAction::pageUp: (void)flow_->scroll(-std::ptrdiff_t(macImportCardRows)); break;
        case MacImportCardAction::pageDown: (void)flow_->scroll(std::ptrdiff_t(macImportCardRows)); break;
        case MacImportCardAction::none: break;
        }
    }

    struct Driver;
    app::ApplicationHost& host_;
    g::LayerRasterizer& raster_;
    app::UtilityExecutor& utility_;
    ApplicationAreaMode mode_{};
    fs::path destination_, coverageRoot_, goldenFixture_;
    app::ClientMetrics metrics_;
    std::shared_ptr<bool> alive_;
    app::UtilityExecutor::Route outcomeRoute_{};
    core::Language language_{core::Language::english};
    MacImportCardState state_;
    std::unique_ptr<MacImportFlow> flow_;
    std::unique_ptr<NativeImportCard> card_;
    std::array<g::LayerCompositionEntry, 1> modal_{};
    std::optional<PendingOutcome> pending_;
    MacImportStep step_{MacImportStep::hidden};
    double time_{}, wheelRemainder_{};
    bool overlayVisible_{}, outcomeLoading_{}, outcomeQueued_{};
    // Hidden coverage only.
    bool coverageInProcess_{true}, chooserPending_{}, chooserCancelled_{};
    unsigned coverageChooser_{}, coverageRestarts_{};
    std::uint64_t coverageClock_{};
    std::function<void()> coverageWork_, coverageDeclined_;
};

// One module-less hidden-coverage step per combined cycle, on the card through
// projected pointer input and keys:
//   cycle 0  in-process import of the golden export: intro, Escape, folder,
//            bounded staging with progress, per data set review, commit with
//            progress, final report, relink list, Done;
//   cycle 1  the same destination again: replace prompt, review, the restart
//            handoff (declined once, then accepted), the post-shutdown commit
//            with backup and rollback guarantees, and the next run's report
//            read from the recorded outcome;
//   cycle 2  an invalid folder (failure card), cancelling a running staging,
//            and the keyboard path with a cancelled dialog.
// Each cycle ends hidden with the card's raster entries released.
struct MacImportArea::Driver {
    MacImportArea& a;
    app::ModuleCoverage& step;
    double t;
    const MacImportCardContent& content() const { return a.flow_->content(); }
    void frame(double dt = 1. / 60) { t += dt; step.present(t); }
    void frames(double seconds) { const double end = t + seconds; while (t < end) frame(); }
    void settle() {
        for (unsigned n = 0; n < 800 && (a.flow_->busy() || a.outcomeLoading_); ++n) { step.drain(); frame(); }
        step.check(!a.flow_->busy() && !a.outcomeLoading_, "The import settles through the shared executor");
        frames(.05);
    }
    void click(int button) {
        frames(.25); // the reveal and any feedback finished
        step.check(a.state_.presented() && a.card_ != nullptr, "The import card is presented");
        const auto p = a.card_->actionPoint(button);
        step.check(p.has_value(), "The card button projects onto the client");
        step.check(a.covers(*p) && a.capturesPointer() && a.capturesKeys() && a.capturesWheel() && a.suppressesKeyFilters(), "The card is modal");
        step.check(a.state_.enabled()[button], "The clicked button is enabled");
        a.pointer({app::PointerKind::move, app::PointerButton::none, p->x, p->y, 0}, t);
        a.pointer({app::PointerKind::down, app::PointerButton::left, p->x, p->y, 0}, t);
        a.pointer({app::PointerKind::up, app::PointerButton::left, p->x, p->y, 0}, t + .02);
        frame();
    }
    void key(unsigned vk) { a.key({app::KeyKind::down, vk}, t); frame(); }
    void open() {
        step.check(a.host_.find<app::MacImportEntry>(app::macImportEntryService) == &a, "The Settings entry service resolves to the area");
        step.check(a.present(t), "The Settings entry presents the card");
        frames(.3);
        step.check(a.flow_->step() == MacImportStep::intro && a.state_.presented() && !a.modalEntries().empty(), "The intro card is above the HUD");
        step.check(content().enabled == std::array<bool, 2>{true, true} && !content().rows[0].label.empty(), "The intro lists the data sets");
    }
    void hidden(const char* why) {
        frames(.3);
        step.check(!a.flow_->presented() && !a.state_.presented() && a.modalEntries().empty() && !a.card_ && !a.capturesPointer(), why);
    }
    // Choose Folder through the projected button; the dialog is synthetic.
    void choose(const fs::path& folder) {
        const auto before = a.coverageChooser_;
        click(1);
        step.check(a.coverageChooser_ == before + 1 && a.chooserPending_ && a.flow_->step() == MacImportStep::choosing && !a.state_.enabled()[1],
                   "Choose Folder asks the one shared dialog and waits");
        a.chooserPending_ = false;
        a.flow_->folderChosen(false, false, {utf8(folder)});
        a.flowChanged();
        frame();
    }
    void stage(const fs::path& folder) {
        choose(folder);
        step.check(a.flow_->step() == MacImportStep::staging && content().progress && !a.state_.enabled()[1], "Staging shows progress with Import disabled");
        double last = 0;
        for (unsigned n = 0; n < 800 && a.flow_->step() == MacImportStep::staging; ++n) {
            step.drain(); frame();
            if (a.flow_->step() != MacImportStep::staging) break;
            step.check(content().progress && *content().progress >= last, "Staging progress only moves forward");
            last = *content().progress;
        }
        settle();
    }
    void review(const Json& expected) {
        step.check(a.flow_->step() == MacImportStep::review && content().enabled[1], "Staging ends in the per data set review");
        const auto* summary = a.flow_->service().summary();
        step.check(summary != nullptr, "The review has a summary");
        for (const auto store : mig::macImportStores) {
            const auto name = std::string(mig::macImportStoreName(store));
            step.check(std::string(mig::macImportStatusName(summary->store(store).status)) == expected["stores"][name].string(),
                       "The Windows validators accept the golden export exactly like the Mac oracle");
        }
        step.check(summary->relink.size() == std::size_t(expected["relinkItems"].integer()) && summary->rejected().empty(), "Every Mac file reference is listed for relinking");
        for (std::size_t n = 0; n < mig::macImportStores.size(); ++n)
            step.check(content().rows[n].label == macImportStoreTitle(mig::macImportStores[n], a.language_) && !content().rows[n].value.empty(), "One report row per data set");
    }
    // The flow's own reset between two runs of the application.
    void nextRun() {
        a.flow_->shutdown();
        a.flow_ = a.makeFlow();
        a.step_ = MacImportStep::hidden;
        a.flowChanged();
        a.loadOutcome();
        settle();
    }
};

void MacImportArea::coverage(app::ModuleCoverage& step) {
    if (step.module) return; // presents no HUD module
    Driver d{*this, step, step.time};
    step.check(coverageMode() && host_.mode() == ApplicationAreaMode::hiddenCoverage && !coverageRoot_.empty() && destination_.parent_path() == coverageRoot_ / "Imported",
               "Hidden coverage imports into the area's own synthetic destination");
    step.check(!flow_->presented() && !state_.presented() && !flow_->busy(), "The import card is idle between cycles");
    const auto exportRoot = coverageRoot_ / "Export";
    d.settle(); // the construction-time outcome read (no note in coverage)
    // The Mac-generated golden export, materialized once (synthetic records
    // written by the unchanged Mac stores and the exporter script).
    const auto bytes = ehud::data::detail::readFile(goldenFixture_, 64 * 1024 * 1024);
    step.check(bytes.has_value(), "The golden export fixture is declared");
    const auto golden = Json::parse(*bytes, 64 * 1024 * 1024);
    if (!fs::exists(exportRoot)) {
        for (const auto& [path, encoded] : golden["files"].object()) {
            const auto decoded = mig::base64Decode(encoded.string());
            step.check(decoded && (mig::validExportPath(path) || path == "manifest.json"), "Golden export entries are valid");
            mig::files::writeNewFile(exportRoot / utf8Path(path), *decoded);
        }
        fs::create_directories(coverageRoot_ / "NotAnExport");
    }
    const auto& expected = golden["expected"];
    const auto clean = [&] {
        for (const auto& entry : fs::directory_iterator(destination_.parent_path()))
            if (utf8(entry.path().filename()).find(".import-") != std::string::npos) return false;
        return true;
    };
    const auto backups = [&] {
        std::size_t n{};
        for (const auto& entry : fs::directory_iterator(destination_.parent_path()))
            if (utf8(entry.path().filename()).starts_with(utf8(destination_.filename()) + ".backup-")) ++n;
        return n;
    };

    if (step.cycle == 0) {
        coverageInProcess_ = true;
        d.open();
        d.key(VK_ESCAPE);
        d.hidden("Escape closes the intro and releases the card");
        d.open();
        d.stage(exportRoot);
        d.review(expected);
        const auto rasterized = card_ ? card_->rasterizations() : 0;
        d.frames(.2);
        step.check(card_ && card_->rasterizations() == rasterized, "Warm frames re-pose the card without rasterizing");
        // Enter on the initially focused Cancel would cancel; click Import.
        d.click(1);
        step.check(flow_->step() == MacImportStep::committing || flow_->step() == MacImportStep::report, "Import starts the in-process commit");
        d.settle();
        step.check(flow_->step() == MacImportStep::report && fs::exists(destination_ / "Migration" / "import.json") && clean() && backups() == 0,
                   "The commit activated the imported root and removed its private folder");
        step.check(d.content().enabled[0] && d.content().buttons[0].find(std::to_string(expected["relinkItems"].integer())) != std::string::npos,
                   "The final report offers the relink list with its count");
        mig::MacRelinkLedger ledger(destination_);
        step.check(ledger.unresolved().size() == std::size_t(expected["relinkItems"].integer()), "Migration/relink.json lists every reference");
        d.click(0);
        step.check(flow_->step() == MacImportStep::relink && flow_->listSize() == ledger.unresolved().size() && !d.content().rows[0].label.empty(),
                   "The relink list names each reference and its module");
        d.key(VK_ESCAPE);
        step.check(flow_->step() == MacImportStep::report, "Escape returns from the relink list to the report");
        // The report list scrolls with the wheel when it outgrows the card.
        if (flow_->listSize() > macImportCardRows) {
            const auto p = card_->listPoint();
            step.check(p && wheel({p->x, p->y, -3, false, 0, 3}, d.t) && flow_->firstRow() > 0, "The wheel scrolls the report");
            d.key(VK_PRIOR);
            step.check(flow_->firstRow() == 0, "Page Up returns to the top");
        }
        d.click(1);
        d.hidden("Done closes the report");
    } else if (step.cycle == 1) {
        coverageInProcess_ = false; // the production restart handoff, played synthetically
        step.check(fs::exists(destination_ / "Migration" / "import.json"), "Cycle 1 starts from the earlier import");
        d.open();
        d.stage(exportRoot);
        step.check(flow_->step() == MacImportStep::replace && d.content().enabled[1], "An earlier import needs explicit replacement");
        d.click(1);
        d.settle();
        d.review(expected);
        const auto restarts = coverageRestarts_;
        d.click(1);
        step.check(flow_->step() == MacImportStep::restarting && coverageRestarts_ == restarts + 1 && coverageWork_ && !state_.enabled()[0] && !state_.enabled()[1],
                   "Import hands the reviewed session to the restart handoff");
        // The host's save failed: the quit was cancelled and nothing ran.
        auto declined = std::exchange(coverageDeclined_, {});
        coverageWork_ = {};
        declined();
        d.frame();
        step.check(flow_->step() == MacImportStep::review && d.content().enabled[1] && clean() == false, "A declined restart keeps the reviewed import");
        d.click(1);
        step.check(flow_->step() == MacImportStep::restarting && coverageWork_, "The review is confirmed again");
        // Every owner closed its stores (the synthetic destination is owned
        // by this area only); the post-shutdown work commits.
        auto work = std::exchange(coverageWork_, {});
        coverageDeclined_ = {};
        work();
        work = {}; // releases the session
        const auto outcome = mig::readMacImportOutcome(destination_);
        step.check(outcome && outcome->state == mig::MacImportOutcome::State::completed && !outcome->backup.empty() && backups() == 1 && clean(),
                   "The post-shutdown commit kept a backup, switched roots and recorded its outcome");
        d.nextRun();
        step.check(flow_->step() == MacImportStep::report && flow_->outcomeRecorded() && state_.presented(),
                   "The next run shows the recorded final report once");
        step.check(!d.content().rows[0].value.empty() && d.content().rows[0].value == utf8(utf8Path(outcome->backup).filename()), "The report names the backup folder");
        d.click(0);
        step.check(flow_->step() == MacImportStep::relink && flow_->listSize() == std::size_t(expected["relinkItems"].integer()), "The relink list is read from the imported root");
        d.click(1);
        d.hidden("Done closes the recorded report");
        step.drain(); // the note removal runs on the shared executor
        step.check(!fs::exists(mig::macImportOutcomePath(destination_)), "Done forgets the recorded outcome");
    } else {
        coverageInProcess_ = true;
        const auto before = backups();
        d.open();
        d.stage(coverageRoot_ / "NotAnExport");
        step.check(flow_->step() == MacImportStep::failed && !d.content().detail.empty() && d.content().enabled[0], "An invalid folder fails with the importer's reason");
        d.click(0);
        d.hidden("Close dismisses the failure");
        d.open();
        d.choose(exportRoot);
        step.check(flow_->step() == MacImportStep::staging, "A second staging starts");
        d.click(0);
        step.check(flow_->step() == MacImportStep::cancelling || flow_->step() == MacImportStep::hidden, "Cancel stops the staging");
        d.settle();
        d.hidden("A cancelled staging closes the card");
        step.check(clean() && backups() == before, "A cancelled staging leaves no private folder and no backup");
        // Keyboard: Tab to Choose Folder, Return opens the dialog, Escape
        // cancels it (its cancelled completion returns to the intro).
        d.open();
        d.key(VK_TAB);
        d.key(VK_RETURN);
        step.check(flow_->step() == MacImportStep::choosing && chooserPending_, "Tab and Return choose a folder");
        d.key(VK_ESCAPE);
        step.check(chooserCancelled_, "Escape cancels the dialog");
        chooserPending_ = chooserCancelled_ = false;
        flow_->folderChosen(true, false, {});
        flowChanged();
        d.frame();
        step.check(flow_->step() == MacImportStep::intro, "A cancelled dialog returns to the intro");
        d.key(VK_ESCAPE);
        d.hidden("Escape closes the intro");
    }
    step.check(!flow_->busy() && !pending_, "Nothing is left running after the cycle");
}
} // namespace

std::unique_ptr<app::ModuleOwner> macImportModule(const app::ApplicationModuleContext& context) {
    need(context.host && context.utility, "the host and the shared executor are required");
    fs::path destination;
    if (context.mode == ApplicationAreaMode::hiddenCoverage) {
        // Synthetic destination owned by this area; never the app data root.
        need(!context.coverageRoot.empty(), "hidden coverage needs the area's coverage root");
        const auto fixture = context.fixture ? context.fixture("macImport.goldenExport") : fs::path{};
        if (fixture.empty()) return nullptr;
        const auto imported = context.coverageRoot / "Imported";
        fs::create_directories(imported);
        destination = (imported / "EndfieldHUD").lexically_normal();
        return std::make_unique<MacImportArea>(context, destination, fixture);
    }
    if (context.dataRoot.empty() || !context.dataRoot.is_absolute()) return nullptr;
    destination = context.dataRoot.lexically_normal();
    if (destination.filename().empty()) destination = destination.parent_path();
    return std::make_unique<MacImportArea>(context, destination, fs::path{});
}
} // namespace endfield::areas
