#include "tools/notes_desktop_controls.hpp"
#ifdef _WIN32
#include <map>
#include <stdexcept>

namespace endfield::tools {
namespace {
namespace d = modules::design;
namespace gpu = native;
void need(bool v, const char* why) { if (!v) throw std::logic_error(why); }
d::Rect rect(const core::Rect& r) noexcept { return {r.x, r.y, r.width, r.height}; }
// HUDControlHighlightLayer.update and the motion verbs read the environment's clock.
void advance(d::Environment& env, double time) { if (std::isfinite(time) && time > env.now) env.now = time; }
}

struct NotesDesktopControls::Impl {
    struct Card {
        modules::NotesCardControls model;
        gpu::NativeDesignScene scene;
        std::uint64_t synced{}; bool posed{};
        Card(d::Environment& env, std::string id, gpu::LayerRasterizer& r, const gpu::LayerRasterOptions& o, std::uint64_t key)
            : model(env, std::move(id)), scene(r, o, "notes.card." + std::to_string(key)) {}
    };
    // The confirmation: its own arena, rebuilt per request.
    struct Confirmation {
        d::LayerArena arena; d::ControlSet set; d::Builder builder;
        d::Layer* root;
        Confirmation(d::Environment& env) : arena("notes.deletion", &env.deadlines), builder{env, arena, set}, root(&arena.make("notes.deleteConfirmation")) {
            root->setAnchorPoint({0, 0}); root->setAnimatable(); root->setPartRoot(true);
        }
    };
    gpu::LayerRasterizer& raster; gpu::LayerRasterOptions options;
    d::Environment* env{};
    std::map<std::uint64_t, std::unique_ptr<Card>> cards;
    std::vector<std::unique_ptr<Card>> retired;
    std::optional<std::uint64_t> hovered, scrollHovered;
    std::unique_ptr<Confirmation> confirmation; std::optional<Deletion> deletion;
    std::vector<std::unique_ptr<Confirmation>> retiredConfirmations;
    gpu::NativeDesignScene deletionScene;
    std::uint64_t deletionRevision{}, deletionSynced{}, revision{};
    const Confirmation* syncedConfirmation{};             // the tree the deletion scene last emitted
    bool deletionPosed{};
    Impl(gpu::LayerRasterizer& r, gpu::LayerRasterOptions o) : raster(r), options(std::move(o)), deletionScene(r, options, "notes.deletion") {}
    Card* find(std::uint64_t key) const { const auto it = cards.find(key); return it == cards.end() ? nullptr : it->second.get(); }
    void sync(Card& c, double time) {
        const auto key = c.model.revision();
        if (c.synced == key) return;
        if (c.scene.syncContent(c.model.root(), env->text, key, options.pixelsPerPoint, time)) ++revision;
        c.synced = key;
    }
};

NotesDesktopControls::NotesDesktopControls(gpu::LayerRasterizer& r, gpu::LayerRasterOptions o) : impl_(std::make_unique<Impl>(r, std::move(o))) {}
NotesDesktopControls::~NotesDesktopControls() = default;
void NotesDesktopControls::setEnvironment(d::Environment* env) {
    auto& i = *impl_;
    need(i.cards.empty() && !i.confirmation, "Set the Notes controls environment before any card is drawn");
    i.env = env;
}
d::Environment* NotesDesktopControls::environment() const noexcept { return impl_->env; }

void NotesDesktopControls::cardContent(std::uint64_t key, const modules::NotesCardPresentation& card) {
    auto& i = *impl_;
    if (!i.env) return;
    auto* c = i.find(key);
    if (!c) { c = i.cards.emplace(key, std::make_unique<Impl::Card>(*i.env, card.noteID(), i.raster, i.options, key)).first->second.get(); ++i.revision; }
    c->model.update(card);
    i.sync(*c, i.env->now);
}
void NotesDesktopControls::cardPosed(std::uint64_t key, const core::Matrix4& world, float opacity, double time) {
    auto& i = *impl_;
    auto* c = i.find(key);
    if (!c) return;
    i.sync(*c, time);
    c->scene.updatePose({world, opacity, time, {}, {}});
    c->posed = true;
}
void NotesDesktopControls::cardRemoved(std::uint64_t key) {
    auto& i = *impl_;
    const auto it = i.cards.find(key);
    if (it == i.cards.end()) return;
    if (i.hovered == key) i.hovered.reset();
    if (i.scrollHovered == key) i.scrollHovered.reset();
    i.retired.push_back(std::move(it->second));
    i.cards.erase(it);
    ++i.revision;
}
void NotesDesktopControls::decorate(std::uint64_t key, std::vector<gpu::LayerCompositionEntry>& out) const {
    const auto* c = impl_->find(key);
    if (!c || !c->posed) return;
    for (const auto& e : c->scene.entries()) out.push_back(e);
}
void NotesDesktopControls::hover(std::optional<std::uint64_t> card, std::optional<core::Point> local, bool pressed, double time) {
    auto& i = *impl_;
    if (!i.env) return;
    advance(*i.env, time);
    if (i.hovered && i.hovered != card) if (auto* previous = i.find(*i.hovered)) previous->model.pointer(std::nullopt, false);
    i.hovered = card;
    if (auto* c = card ? i.find(*card) : nullptr) c->model.pointer(local ? std::optional<d::Point>(d::Point{local->x, local->y}) : std::nullopt, pressed);
}
// mouseMoved (:606-615): hovering a scrollable note shows its scroll bar.
void NotesDesktopControls::scrollHover(std::optional<std::uint64_t> card, double time) {
    auto& i = *impl_;
    if (!i.env || i.scrollHovered == card) return;
    advance(*i.env, time);
    if (i.scrollHovered) if (auto* previous = i.find(*i.scrollHovered)) previous->model.setScrollHovered(false);
    i.scrollHovered = card;
    if (auto* c = card ? i.find(*card) : nullptr) c->model.setScrollHovered(true);
}
void NotesDesktopControls::noteScroll(std::uint64_t key, double time) {
    auto& i = *impl_;
    if (!i.env) return;
    advance(*i.env, time);
    if (auto* c = i.find(key)) c->model.noteScroll();
}

// renderDeletionControls (:1491-1508) and requestDeletion's rise (:1466-1476).
void NotesDesktopControls::setDeletion(std::optional<Deletion> next, double time) {
    auto& i = *impl_;
    if (!i.env) return;
    advance(*i.env, time);
    const bool same = next && i.deletion && next->noteID == i.deletion->noteID;
    if (next && i.deletion && next->noteID == i.deletion->noteID && next->rects == i.deletion->rects && next->labels == i.deletion->labels) return;
    if (i.confirmation) i.retiredConfirmations.push_back(std::move(i.confirmation));
    i.deletion = std::move(next);
    ++i.deletionRevision; ++i.revision;
    if (!i.deletion) return;
    auto& env = *i.env;
    const bool reduce = env.reduceMotion;
    env.reduceMotion = true;                             // BuildGate: the buttons appear settled
    struct Restore { d::Environment& env; bool value; ~Restore() { env.reduceMotion = value; } } restore{env, reduce};
    i.confirmation = std::make_unique<Impl::Confirmation>(env);
    auto& c = *i.confirmation;
    for (std::size_t n = 0; n < 2; ++n) {
        const auto& r = i.deletion->rects[n];
        auto& button = c.builder.make<d::IconButton>(n == 0 ? "cancelDelete" : "confirmDelete", i.deletion->labels[n], n == 0 ? d::Icon::x : d::Icon::check,
            d::IconButton::Style::dark, false, false, r.width, 11);
        button.setOn(n == 1);
        button.layer().setFrame(rect(r));
        c.root->add(button.layer());
    }
    env.reduceMotion = reduce;
    // A new request rises 6 px into place over .16 s easeOut (a re-placement of
    // the same request, a resize or a move, does not).
    if (!same && !reduce) {
        c.root->setValue(d::Channel::translateY, -6);
        d::motion::animate(*c.root, d::Channel::translateY, 0, .16, env.clock(), d::easing::easeOut);
    }
}
void NotesDesktopControls::hoverDeletion(std::optional<core::Point> point, bool pressed, double time) {
    auto& i = *impl_;
    if (!i.env || !i.confirmation) return;
    advance(*i.env, time);
    d::updateHighlights(*i.confirmation->root, point ? std::optional<d::Point>(d::Point{point->x, point->y}) : std::nullopt, pressed && point.has_value());
}
void NotesDesktopControls::poseDeletion(const core::Matrix4& world, float opacity, double time) {
    auto& i = *impl_;
    if (!i.confirmation) { i.deletionPosed = false; return; }
    const auto key = i.deletionRevision + (i.confirmation->arena.revision() << 20);
    if (i.deletionSynced != key) { if (i.deletionScene.syncContent(*i.confirmation->root, i.env->text, key, i.options.pixelsPerPoint, time)) ++i.revision; i.deletionSynced = key; i.syncedConfirmation = i.confirmation.get(); }
    i.deletionScene.updatePose({world, opacity, time, {}, {}});
    i.deletionPosed = true;
}
std::span<const gpu::LayerCompositionEntry> NotesDesktopControls::deletionEntries() const noexcept {
    const auto& i = *impl_;
    return i.confirmation && i.deletionPosed ? i.deletionScene.entries() : std::span<const gpu::LayerCompositionEntry>{};
}

bool NotesDesktopControls::requiresFrames(double time) const {
    const auto& i = *impl_;
    for (const auto& [key, c] : i.cards) if (c->posed && (c->model.requiresFrames(time) || c->scene.requiresFrames(time))) return true;
    return i.confirmation && i.deletionPosed && (i.confirmation->root->animating(time) || i.deletionScene.requiresFrames(time));
}
std::uint64_t NotesDesktopControls::compositionRevision() const noexcept { return impl_->revision; }
void NotesDesktopControls::upload(gpu::Renderer& r) {
    auto& i = *impl_;
    for (auto& [key, c] : i.cards) if (c->posed && c->scene.uploadAnimations(r)) ++i.revision;
    if (i.confirmation && i.deletionPosed && i.deletionScene.uploadAnimations(r)) ++i.revision;
}
void NotesDesktopControls::collected(gpu::Renderer& r) {
    auto& i = *impl_;
    for (auto& [key, c] : i.cards) (void)c->scene.collectRetired(r);
    for (auto it = i.retired.begin(); it != i.retired.end();) if ((*it)->scene.releaseResources(r)) it = i.retired.erase(it); else ++it;
    (void)i.deletionScene.collectRetired(r);
    // A replaced confirmation's layers die once the scene emitted the new
    // one, or once a hidden confirmation's artwork left the GPU.
    const bool released = i.confirmation ? i.syncedConfirmation == i.confirmation.get() : i.deletionScene.releaseResources(r);
    if (released) { i.retiredConfirmations.clear(); if (!i.confirmation) i.syncedConfirmation = nullptr; }
}
bool NotesDesktopControls::release(gpu::Renderer& r) {
    auto& i = *impl_;
    bool done = true;
    for (auto& [key, c] : i.cards) done = c->scene.releaseResources(r) && done;
    for (auto& c : i.retired) done = c->scene.releaseResources(r) && done;
    done = i.deletionScene.releaseResources(r) && done;
    return done;
}
} // namespace endfield::tools
#endif
