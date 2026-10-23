#include "native/hud_design_scene.hpp"
#ifdef _WIN32
#include "core/source_camera.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace endfield::native {
namespace {
namespace d = modules::design;
using Json = ehud::data::Json;
using Matrix = core::Matrix4;
constexpr std::size_t none = std::numeric_limits<std::size_t>::max();
void need(bool v, const char* s) { if (!v) throw std::invalid_argument(s); }
Matrix matrix(const d::Affine& a) {
    Matrix m;
    m.values[0] = a.a; m.values[1] = a.b; m.values[4] = a.c; m.values[5] = a.d; m.values[12] = a.tx; m.values[13] = a.ty;
    return m;
}
// sRGB tint -> the renderer's linear multiply (DrawObject::linearTint).
float linear(double c) { c = std::clamp(c, 0., 1.); return float(c <= .04045 ? c / 12.92 : std::pow((c + .055) / 1.055, 2.4)); }
std::array<float, 4> linearTint(const std::optional<d::Color>& c) {
    if (!c) return {1, 1, 1, 1};
    return {linear(c->r), linear(c->g), linear(c->b), float(std::clamp(c->a, 0., 1.))};
}
// A lazy surface keeps no bitmap while it does not show (Mac: a layer at
// opacity 0 keeps no backing store): its leaf holds one transparent point
// until it first shows and gets it back after fading out (Clipboard's lazy
// feedback, native/clipboard_scene.cpp).
Json placeholder(const std::string& id) {
    return Json::Object{{"id", id}, {"kind", "layer"}, {"bounds", Json::Array{0, 0, 1, 1}}, {"position", Json::Array{0, 0}}, {"anchorPoint", Json::Array{0, 0}},
        {"backgroundColor", Json::Object{{"sRGB", Json::Array{0, 0, 0, 0}}}}, {"children", Json::Array{}}};
}
// requiredSourceImage keys resolve to pinned assets or memory images before
// a load; a missing or malformed one aborts the generation (Settings' rule).
void resolveImages(Json& node, const DesignImages& images, unsigned depth) {
    need(depth <= 64, "Design artwork nests too deeply");
    if (!node.isObject()) return;
    const auto& view = std::as_const(node); // reads never insert keys
    if (view.contains("requiredSourceImage")) {
        need(view["requiredSourceImage"].isString(), "Invalid design image key");
        const auto it = images.find(view["requiredSourceImage"].string());
        need(it != images.end() && it->second.isObject(), "Missing design image artwork");
        const auto& c = it->second;
        const bool pinned = c["asset"].isString() && c["sha256"].isString() && c["sha256"].string().size() == 64;
        const bool memory = c["memoryImage"].isString() && c["revision"].isNumber();
        need(pinned || memory, "Invalid design image artwork");
        node["contents"] = c;
        node.erase("requiredSourceImage");
    }
    if (view["children"].isArray()) for (auto& child : const_cast<Json::Array&>(view["children"].array())) resolveImages(child, images, depth + 1);
}
// The deepest layer above (or equal to) both: a part's group layer.
const d::Layer* common(const d::Layer* a, const d::Layer* b) {
    for (const auto* x = a; x; x = x->parent()) for (const auto* y = b; y; y = y->parent()) if (x == y) return x;
    return nullptr;
}
bool intersects(const core::Rect& a, const core::Rect& b) { return a.x < b.x + b.width && b.x < a.x + a.width && a.y < b.y + b.height && b.y < a.y + a.height; }
core::Rect inflated(const core::Rect& r, double vertical) { return {r.x, r.y - vertical, r.width, r.height + 2 * vertical}; }
}

struct NativeDesignScene::Impl {
    struct Part {
        std::string id;
        Json full;                          // resolved layers, lazy surfaces with their content
        std::vector<d::PartSurface> surfaces;
        std::unique_ptr<LayerScene> scene;  // null: outside the visibility window, or parked
        // Group opacity: while the part fades, a pooled group draws its scene
        // (declared after `scene`: destroyed first). groupLayer: the deepest
        // layer above all its placements; groupWorld/groupOpacity: its pose
        // this frame (opacity without the module's); refused: the renderer's
        // group limits refused it, so this fade draws directly.
        std::unique_ptr<NativeLayerGroup> group;
        const d::Layer* groupLayer{};
        d::Affine groupWorld;
        float groupOpacity{1};
        bool wantsGroup{}, refused{};
        std::vector<std::size_t> index;     // part surface -> scene surface (none: no drawing)
        std::vector<std::uint8_t> lazy, painted, wanted;
        std::uint64_t paintRevision{};
        const d::Layer *root{}, *clip{};    // part root and its scroll clip (model tree of this generation)
        std::vector<LayerPlacement> placements; // one per resident surface
        std::vector<std::array<PlaneMask, 8>> masks;
        std::vector<std::size_t> binding;   // per placement: its binding
        std::vector<float> target;          // per surface: this frame's opacity (a lazy one painted at upload shows at once)
        std::size_t kept{};                 // placements handed to the scene
        std::size_t needed() const { std::size_t n{}; for (std::size_t s = 0; s < surfaces.size(); ++s) n += !lazy[s]; return n; }
    };
    LayerRasterizer& raster;
    LayerRasterOptions options;
    std::string prefix;
    DesignImages images;
    std::vector<std::unique_ptr<Part>> parts;
    std::vector<std::unique_ptr<LayerScene>> retired;
    d::Artwork artwork;
    std::vector<d::SurfacePose> poses;
    std::vector<std::pair<std::size_t, std::size_t>> slots;    // binding -> (part, surface of the part)
    std::vector<std::size_t> alphaSources;                      // binding -> the binding whose surface alpha clips it (R4b)
    std::vector<std::optional<d::PoseMask>> alphaClips;         // binding -> its clip's model rect when no surface of its part holds it
    std::vector<LayerCompositionEntry> entries;
    // Detached groups keep their target until the list without them is
    // published (collectRetired); then they wait in the pool, target-free.
    std::vector<std::unique_ptr<NativeLayerGroup>> pool, draining;
    // This frame's pose (place() runs again when a group attaches).
    Matrix inverseBox;
    float moduleOpacity{1};
    std::array<PlaneMask, 4> ownerMasks{};
    std::size_t ownerMaskCount{};
    std::optional<PlaneShutter> shutter;
    bool entriesChanged{};
    const d::Layer* root{};
    std::uint64_t revision{};
    double density{};
    bool synced{}, imagesDirty{}, posed{};
    double lastTime{};
    Matrix boxWorld;
    NativeDesignSceneStats statistics;
    Impl(LayerRasterizer& r, LayerRasterOptions o, std::string p, DesignImages i) : raster(r), options(std::move(o)), prefix(std::move(p)), images(std::move(i)) {
        need(!prefix.empty() && prefix.size() <= 200, "Design scene needs a source prefix");
        entries.reserve(64); retired.reserve(64); pool.reserve(64); draining.reserve(64);
    }
    void time(double now) const { need(std::isfinite(now) && now >= lastTime, "Design scene needs monotonic owner time"); }
    Json resting(const Part& p) const {
        auto layers = p.full;
        auto& nodes = const_cast<Json::Array&>(layers["children"].array());
        for (std::size_t n = 0; n < nodes.size() && n < p.surfaces.size(); ++n) if (p.lazy[n] && !p.painted[n]) nodes[n] = placeholder(p.surfaces[n].id);
        return layers;
    }
    // Loads (rasterizes) a part's resting layers into a new retained scene.
    void load(Part& p) {
        auto scene = std::make_unique<LayerScene>(raster);
        std::fill(p.painted.begin(), p.painted.end(), std::uint8_t{});
        scene->load(resting(p), options);
        need(scene->report().unsupported.empty(), "Design artwork contains unsupported features");
        p.scene = std::move(scene);
        ++statistics.partsLoaded;
        bindScene(p);
    }
    void bindScene(Part& p) {
        p.index.assign(p.surfaces.size(), none);
        if (p.scene) for (std::size_t n = 0; n < p.surfaces.size(); ++n) p.index[n] = p.scene->surfaceIndex(p.surfaces[n].id).value_or(none);
        order(p);
    }
    // The placements the scene takes come first, in binding order (surfaces
    // without drawing have no index); a frame only rewrites numbers in place.
    void order(Part& p) {
        if (p.binding.size() != p.placements.size() || slots.size() < p.binding.size()) { p.kept = 0; return; }
        std::stable_partition(p.binding.begin(), p.binding.end(), [&](std::size_t b) { return b < slots.size() && p.index.size() > slots[b].second && p.index[slots[b].second] != none; });
        p.kept = 0;
        for (const auto b : p.binding) p.kept += b < slots.size() && p.index.size() > slots[b].second && p.index[slots[b].second] != none;
        for (auto& placement : p.placements) placement.alphaMask.reset(); // texture edges are taken again
    }
    void retire(Part& p) { dropGroup(p); if (p.scene) { retired.push_back(std::move(p.scene)); p.index.assign(p.surfaces.size(), none); } }
    // The part draws directly again at once; the target goes after the next
    // publication (collectRetired).
    void dropGroup(Part& p) {
        if (!p.group) return;
        p.group->detach();
        draining.push_back(std::move(p.group));
        ++statistics.groupSwitches;
        entriesChanged = true;
    }
    // One part's placements for this frame's poses: in the module world, or,
    // while a group draws the part, in its group layer's plane (the group's
    // output carries that layer's world, the group opacity and the owner masks).
    void place(Part& p) {
        const bool grouped = p.group != nullptr;
        const auto inverseGroup = grouped ? p.groupWorld.inverse() : std::nullopt;
        need(!grouped || inverseGroup, "A grouped design part needs an invertible group layer");
        const auto mask = [&](const d::PoseMask& m) -> std::optional<PlaneMask> {
            const auto inverse = m.world.inverse();
            if (!inverse || !(m.bounds.width > 0) || !(m.bounds.height > 0)) return std::nullopt;
            return PlaneMask{grouped ? matrix(*inverse * p.groupWorld) : matrix(*inverse) * inverseBox, m.bounds, std::clamp(m.cornerRadius, 0., std::min(m.bounds.width, m.bounds.height) * .5)};
        };
        for (std::size_t k = 0; k < p.binding.size(); ++k) {
            const auto b = p.binding[k];
            const auto surface = slots[b].second;
            const auto& s = poses[b];
            auto& placement = p.placements[k];
            auto& masks = p.masks[k];
            const bool lazy = p.lazy[surface];
            const bool shows = s.visible && s.opacity > 0;
            if (lazy) p.wanted[surface] = shows;
            placement.surface = p.index[surface];
            placement.world = grouped ? matrix(*inverseGroup * s.world) : boxWorld * matrix(s.world);
            // Inside a group: the surface's opacity below the group layer.
            p.target[surface] = !shows ? 0.f : grouped ? std::min(1.f, s.opacity / p.groupOpacity) : moduleOpacity * s.opacity;
            placement.opacity = !lazy || p.painted[surface] ? p.target[surface] : 0.f;
            placement.linearTint = linearTint(s.tint);
            std::size_t count{};
            if (!grouped) for (std::size_t m = 0; m < ownerMaskCount; ++m) masks[count++] = ownerMasks[m];
            for (std::size_t m = 0; m < s.maskCount && count < masks.size(); ++m)
                if (const auto plane = mask(s.masks[m])) masks[count++] = *plane;
                else placement.opacity = p.target[surface] = 0; // clipped away entirely
            if (alphaClips[b] && count < masks.size()) if (const auto plane = mask(*alphaClips[b])) masks[count++] = *plane;
            if (const auto source = alphaSources[b]; source != none && p.scene && p.index[slots[source].second] != none) {
                const auto world = grouped ? matrix(*inverseGroup * poses[source].world) : boxWorld * matrix(poses[source].world);
                // The texture and its edges are taken once per load (content
                // event); a frame only moves the clip (allocation-free).
                if (!placement.alphaMask) placement.alphaMask = p.scene->surfaceAlphaMask(p.index[slots[source].second], world);
                else placement.alphaMask->worldToLocal = core::source::inverseSourceMatrix(world);
            } else placement.alphaMask.reset();
            placement.masks = std::span<const PlaneMask>(masks.data(), count);
        }
        if (!p.scene) return;
        // Surfaces without drawing (no index) sort last and stay out.
        p.scene->setPlacements(std::span<const LayerPlacement>(p.placements.data(), p.kept));
        p.scene->setGroupShutter(grouped ? std::nullopt : shutter);
        if (grouped && p.group->holdsTarget())
            p.group->setPose(boxWorld * matrix(p.groupWorld), moduleOpacity * p.groupOpacity, std::span<const PlaneMask>(ownerMasks.data(), ownerMaskCount), shutter);
    }
    // Visibility window (LIBRARY-DESIGN.md section 5): scroll content parts are
    // rasterized when they intersect their clip's viewport plus one viewport
    // of slack and dropped beyond two viewports; everything else is resident.
    // Model geometry (the scroll target), content events and scroll steps only.
    static bool within(const Part& p, double viewports) {
        if (!p.clip || !p.root) return true;
        const auto clip = core::Rect{0, 0, p.clip->bounds().width, p.clip->bounds().height};
        const auto r = d::convert(core::Rect{0, 0, p.root->bounds().width, p.root->bounds().height}, *p.root, *p.clip);
        return intersects(r, inflated(clip, clip.height * viewports));
    }
    void refreshEntries() {
        entries.clear();
        for (const auto& p : parts) if (p->scene) entries.push_back(p->group ? p->group->entry() : LayerCompositionEntry{p->scene.get(), {}});
    }
    void rebuildSlots() {
        slots.assign(artwork.bindings.size(), {none, none});
        alphaClips.assign(artwork.bindings.size(), std::nullopt);
        alphaSources.assign(artwork.bindings.size(), none);
        poses.assign(artwork.bindings.size(), {});
        for (auto& p : parts) { p->placements.clear(); p->masks.clear(); p->binding.clear(); p->target.assign(p->surfaces.size(), 0.f); p->kept = 0; p->groupLayer = nullptr; }
        for (std::size_t b = 0; b < artwork.bindings.size(); ++b) {
            const auto& binding = artwork.bindings[b];
            need(binding.part < parts.size() && binding.surface < parts[binding.part]->surfaces.size(), "Design binding outside its part");
            slots[b] = {binding.part, binding.surface};
            auto& owner = *parts[binding.part];
            owner.groupLayer = owner.groupLayer ? common(owner.groupLayer, binding.placement) : binding.placement;
            // R4b: the surface of the same part that draws the clip layer (its
            // own surface, else the static surface of its nearest placing
            // ancestor) lends its texture's alpha: the sheen inside its
            // chamfered plate. Another part's clip falls back to the clip
            // layer's model rect (the 11 px chamfer corner not cut).
            if (!binding.alphaClip) continue;
            for (const auto* l = binding.alphaClip; l && alphaSources[b] == none; l = l->parent())
                for (std::size_t o = 0; o < artwork.bindings.size(); ++o)
                    if (o != b && artwork.bindings[o].part == binding.part && artwork.bindings[o].placement == l) { alphaSources[b] = o; break; }
            if (alphaSources[b] == none && root) {
                const auto world = d::modelWorld(*binding.alphaClip, *root);
                alphaClips[b] = d::PoseMask{world, {0, 0, binding.alphaClip->bounds().width, binding.alphaClip->bounds().height}, 0};
            }
        }
        for (std::size_t b = 0; b < artwork.bindings.size(); ++b) {
            auto& p = *parts[slots[b].first];
            p.binding.push_back(b);
            p.placements.push_back({});
            p.masks.push_back({});
        }
        for (auto& p : parts) order(*p);
    }
    std::size_t entriesInUse() const { const auto s = raster.stats(); return s.entries; }
};

NativeDesignScene::NativeDesignScene(LayerRasterizer& r, LayerRasterOptions o, std::string prefix, DesignImages images)
    : impl_(std::make_unique<Impl>(r, std::move(o), std::move(prefix), std::move(images))) {}
NativeDesignScene::~NativeDesignScene() = default;
void NativeDesignScene::setImages(DesignImages images) { impl_->images = std::move(images); impl_->imagesDirty = true; }

bool NativeDesignScene::syncContent(const d::Layer& root, const d::TextServices& text, std::uint64_t revision, double pixelsPerPoint, double now) {
    auto& i = *impl_;
    i.time(now);
    need(std::isfinite(pixelsPerPoint) && pixelsPerPoint >= 1 && pixelsPerPoint <= 4, "Design density outside 1...4");
    const bool content = !i.synced || i.revision != revision || i.density != pixelsPerPoint || i.root != &root || i.imagesDirty;
    if (!content) {
        // A scroll step or glide (no content event): only the visibility
        // window may change which scroll parts are resident.
        bool changed{};
        std::size_t needed{};
        for (const auto& p : i.parts) if (!p->scene && Impl::within(*p, 1)) needed += p->needed();
        if (needed) need(i.entriesInUse() <= LayerRasterizer::maximumEntries && needed <= LayerRasterizer::maximumEntries - i.entriesInUse(), "Design scroll content exceeds the shared raster budget");
        for (auto& p : i.parts) {
            if (!p->scene && Impl::within(*p, 1)) { i.load(*p); changed = true; }
            else if (p->scene && !Impl::within(*p, 2)) { i.retire(*p); changed = true; }
        }
        if (changed) i.refreshEntries();
        i.lastTime = now;
        return changed;
    }
    ++i.statistics.emits;
    auto options = i.options;
    options.pixelsPerPoint = pixelsPerPoint;
    auto artwork = d::emit(root, {&text, i.prefix});
    // Plan the generation before touching anything: resolved JSON per part,
    // its predecessor (same id), reuse / per-surface update / load, and the
    // shared raster budget of what it would add.
    struct Plan { std::unique_ptr<Impl::Part> part; Impl::Part* old{}; enum class Kind { reuse, update, load } kind{Kind::load}; std::vector<std::size_t> changed; };
    std::vector<Plan> plans;
    plans.reserve(artwork.parts.size());
    std::vector<bool> used(i.parts.size());
    std::size_t needed{};
    const bool densityChanged = i.density != pixelsPerPoint;
    for (auto& source : artwork.parts) {
        Plan plan;
        plan.part = std::make_unique<Impl::Part>();
        auto& p = *plan.part;
        p.id = std::move(source.id);
        p.full = std::move(source.layers);
        p.surfaces = std::move(source.surfaces);
        need(p.full["children"].isArray() && p.full["children"].array().size() == p.surfaces.size(), "Design part layers and surfaces diverged");
        resolveImages(p.full, i.images, 0);
        p.lazy.assign(p.surfaces.size(), 0); p.painted.assign(p.surfaces.size(), 0); p.wanted.assign(p.surfaces.size(), 0);
        p.index.assign(p.surfaces.size(), none); // not resident until loaded
        for (std::size_t n = 0; n < p.surfaces.size(); ++n) p.lazy[n] = p.surfaces[n].role == d::SurfaceRole::lazy;
        for (std::size_t n = 0; n < i.parts.size(); ++n) if (!used[n] && i.parts[n]->id == p.id) { used[n] = true; plan.old = i.parts[n].get(); break; }
        if (plan.old && plan.old->scene && !densityChanged) {
            if (plan.old->full == p.full) plan.kind = Plan::Kind::reuse;
            else if (plan.old->surfaces == p.surfaces) {
                plan.kind = Plan::Kind::update;
                const auto& before = plan.old->full["children"].array();
                const auto& after = p.full["children"].array();
                for (std::size_t n = 0; n < p.surfaces.size(); ++n) if (before[n] != after[n]) plan.changed.push_back(n);
            }
        }
        plans.push_back(std::move(plan));
    }
    // Part roots and clips (model tree) decide the visibility window before
    // anything is rasterized.
    {
        // The part root: the shallowest layer placing one of its surfaces
        // (it places the part's static content or is the boundary itself).
        std::vector<std::size_t> depth(plans.size(), none);
        for (const auto& binding : artwork.bindings) {
            need(binding.part < plans.size() && binding.placement, "Design binding has no part");
            std::size_t level{};
            for (auto* l = binding.placement; l && l != &root; l = l->parent()) ++level;
            if (level < depth[binding.part]) { plans[binding.part].part->root = binding.placement; depth[binding.part] = level; }
        }
        for (auto& plan : plans) {
            auto& p = *plan.part;
            if (!p.root) continue;
            for (auto* l = p.root->parent(); l && l != &root; l = l->parent()) if (l->masksToBounds()) { p.clip = l; break; }
        }
    }
    for (auto& plan : plans) {
        const bool resident = Impl::within(*plan.part, plan.old && plan.old->scene ? 2 : 1);
        if (!resident) { plan.kind = Plan::Kind::load; continue; }
        if (plan.kind == Plan::Kind::load) needed += plan.part->needed();
    }
    const auto inUse = i.entriesInUse();
    need(inUse <= LayerRasterizer::maximumEntries && needed <= LayerRasterizer::maximumEntries - inUse, "Design generation exceeds the shared raster budget");
    // Execute: new loads first (they may throw; nothing retained changed yet).
    i.options = options;
    for (auto& plan : plans)
        if (plan.kind == Plan::Kind::load && Impl::within(*plan.part, plan.old && plan.old->scene ? 2 : 1)) i.load(*plan.part);
    for (auto& plan : plans) {
        auto& p = *plan.part;
        if (plan.kind == Plan::Kind::load) { if (plan.old) i.retire(*plan.old); continue; }
        // Reused or updated: the retained scene moves to the new generation.
        p.scene = std::move(plan.old->scene);
        p.group = std::move(plan.old->group);
        p.refused = plan.old->refused;
        p.painted = plan.old->painted;
        p.paintRevision = plan.old->paintRevision;
        i.bindScene(p);
        ++i.statistics.partsReused;
        for (const auto n : plan.changed) {
            if (p.lazy[n] && !p.painted[n]) continue; // a resting placeholder takes the new content when it shows
            const auto& node = p.full["children"].array()[n];
            bool updated{};
            if (p.index[n] != none) try { updated = p.scene->updateLocalContent(p.surfaces[n].id, ++p.paintRevision, node, options); } catch (const std::exception&) {}
            if (updated) { ++i.statistics.surfacesUpdated; for (auto& placement : p.placements) placement.alphaMask.reset(); continue; }
            // The surface cannot be replaced alone (it gained or lost its
            // drawing): the part loads again.
            i.retire(p);
            --i.statistics.partsReused;
            i.load(p);
            break;
        }
    }
    for (std::size_t n = 0; n < i.parts.size(); ++n) if (!used[n]) i.retire(*i.parts[n]);
    std::vector<std::unique_ptr<Impl::Part>> next;
    next.reserve(plans.size());
    for (auto& plan : plans) next.push_back(std::move(plan.part));
    i.parts = std::move(next);
    i.artwork = std::move(artwork);
    i.root = &root;
    i.options = options;
    i.revision = revision;
    i.density = pixelsPerPoint;
    i.synced = true;
    i.imagesDirty = false;
    i.posed = false;
    i.lastTime = now;
    ++i.statistics.generations;
    i.rebuildSlots();
    i.refreshEntries();
    return true;
}

void NativeDesignScene::updatePose(const NativeDesignScenePose& pose) {
    auto& i = *impl_;
    i.time(pose.time);
    need(i.synced, "Synchronize the design scene before its pose");
    need(pose.boxWorld.finite() && std::isfinite(pose.opacity) && pose.opacity >= 0 && pose.opacity <= 1 && pose.ownerMasks.size() <= 4, "Invalid design scene pose");
    if (pose.shutter) validatePlaneShutter(*pose.shutter);
    d::samplePoses(i.artwork, pose.time, i.poses);
    i.boxWorld = pose.boxWorld;
    i.inverseBox = core::source::inverseSourceMatrix(pose.boxWorld);
    i.moduleOpacity = pose.opacity;
    std::copy(pose.ownerMasks.begin(), pose.ownerMasks.end(), i.ownerMasks.begin());
    i.ownerMaskCount = pose.ownerMasks.size();
    i.shutter = pose.shutter;
    for (auto& part : i.parts) {
        auto& p = *part;
        // Group opacity (CALayer opacity composites the subtree as one image):
        // a part of two or more drawn surfaces whose group layer (with the
        // module) is translucent is drawn through a group; at rest directly.
        p.wantsGroup = false;
        if (p.scene && p.groupLayer && p.kept > 1) {
            const auto g = d::sampleLayer(*p.groupLayer, *i.root, pose.time);
            const double opacity = pose.opacity * g.opacity;
            if (g.visible && opacity > 0 && opacity < 1 - 1e-4 && g.world.inverse()) {
                p.groupWorld = g.world;
                p.groupOpacity = float(g.opacity);
                p.wantsGroup = true;
            }
        }
        if (!p.wantsGroup) { i.dropGroup(p); p.refused = false; }
        i.place(p);
    }
    if (i.entriesChanged) i.refreshEntries();
    i.posed = true;
    i.lastTime = pose.time;
    ++i.statistics.poses;
}

bool NativeDesignScene::uploadAnimations(Renderer& r) {
    auto& i = *impl_;
    need(i.posed, "Pose the design scene before its upload");
    bool changed = std::exchange(i.entriesChanged, false);
    // A lazy surface paints its content once it shows (before this frame is
    // drawn) and gives it back after fading out.
    for (auto& part : i.parts) {
        auto& p = *part;
        if (!p.scene) continue;
        for (std::size_t n = 0; n < p.surfaces.size(); ++n) {
            if (!p.lazy[n] || bool(p.wanted[n]) == bool(p.painted[n]) || p.index[n] == none) continue;
            const bool shows = p.wanted[n];
            p.scene->updateLocalContent(p.surfaces[n].id, ++p.paintRevision, shows ? p.full["children"].array()[n] : placeholder(p.surfaces[n].id), i.options);
            p.painted[n] = shows;
            ++(shows ? i.statistics.lazyPaints : i.statistics.lazyReleases);
            changed = true;
            // Painted before this frame is drawn: it shows at this frame's value.
            for (std::size_t k = 0; k < p.kept; ++k) if (p.placements[k].surface == p.index[n]) p.placements[k].opacity = shows ? p.target[n] : 0.f;
            p.scene->setPlacements(std::span<const LayerPlacement>(p.placements.data(), p.kept));
        }
        // A fade starts: a pooled group takes the part (its target is
        // allocated now). The renderer's group limits may refuse it (a part
        // larger than its pixel budget): that fade draws directly.
        bool attached{};
        if (p.wantsGroup && !p.group && !p.refused) {
            if (i.pool.empty()) i.pool.push_back(std::make_unique<NativeLayerGroup>(i.raster, i.prefix + ".group"));
            p.group = std::move(i.pool.back());
            i.pool.pop_back();
            i.place(p);
            try { p.group->attach(r, *p.scene, i.density); attached = true; }
            catch (const std::invalid_argument&) { i.pool.push_back(std::move(p.group)); p.refused = true; i.place(p); continue; }
            catch (...) { i.pool.push_back(std::move(p.group)); i.place(p); throw; }
            ++i.statistics.groupSwitches;
            changed = true;
            i.place(p); // the group's pose, now that it has a target
        }
        // Child constants (and a re-upload when a lazy surface painted or the
        // local ink left the target) before the frame is drawn.
        if (p.group && !attached) try { (void)p.group->updateLocal(r); }
            catch (const std::invalid_argument&) { i.dropGroup(p); p.refused = true; i.place(p); changed = true; }
    }
    if (changed) i.refreshEntries();
    i.entriesChanged = false;
    return changed;
}

bool NativeDesignScene::requiresFrames(double now) const {
    const auto& i = *impl_;
    i.time(now);
    return i.synced && d::requiresFrames(i.artwork, now);
}
std::span<const LayerCompositionEntry> NativeDesignScene::entries() const noexcept { return impl_->entries; }
bool NativeDesignScene::collectRetired(Renderer& r) {
    auto& i = *impl_;
    // Group targets go first (their children reference the retired scenes).
    for (auto& g : i.draining) if (g->releaseTarget(r)) i.pool.push_back(std::move(g));
    std::erase_if(i.draining, [](const auto& g) { return !g; });
    for (auto& scene : i.retired) if (!scene->releaseResources(r)) return false;
    i.retired.clear();
    return true;
}
bool NativeDesignScene::releaseResources(Renderer& r) {
    auto& i = *impl_;
    for (const auto& p : i.parts) if (p->group && p->group->published()) return false; // still composed
    for (auto& p : i.parts) i.dropGroup(*p);
    if (std::exchange(i.entriesChanged, false)) i.refreshEntries();
    bool ok = collectRetired(r) && i.draining.empty();
    for (auto& p : i.parts) if (p->scene) ok = p->scene->releaseResources(r) && ok;
    return ok;
}
bool NativeDesignScene::park(Renderer& r) noexcept {
    auto& i = *impl_;
    bool any = !i.retired.empty();
    for (const auto& p : i.parts) any |= bool(p->scene);
    if (!any) return false;
    // A composed part refuses (before anything is released): stay resident.
    try { if (!releaseResources(r)) return false; } catch (...) { return false; }
    // Destroying the scenes removes their shared raster entries; the next
    // syncContent rebuilds them from the canvas's current tree.
    for (auto& p : i.parts) p->scene.reset();
    i.parts.clear(); i.retired.clear(); i.entries.clear(); i.artwork = {}; i.poses.clear(); i.slots.clear(); i.alphaClips.clear();
    i.synced = false; i.posed = false; i.root = nullptr;
    ++i.statistics.parks;
    return true;
}
const core::Matrix4& NativeDesignScene::boxWorld() const noexcept { return impl_->boxWorld; }
NativeDesignSceneStats NativeDesignScene::stats() const noexcept {
    const auto& i = *impl_;
    auto s = i.statistics;
    s.parts = i.parts.size();
    for (const auto& p : i.parts) s.groups += p->group != nullptr;
    for (const auto& g : i.draining) s.groupTargets += g->holdsTarget();
    s.groupTargets += s.groups;
    s.retiredParts = i.retired.size();
    for (const auto& p : i.parts) {
        s.surfaces += p->surfaces.size();
        if (!p->scene) continue;
        s.rasterEntries += p->scene->report().surfaces;
        s.residentPixelBytes += p->scene->report().pixelBytes;
    }
    for (const auto& scene : i.retired) s.residentPixelBytes += scene->report().pixelBytes;
    return s;
}
} // namespace endfield::native
#endif
