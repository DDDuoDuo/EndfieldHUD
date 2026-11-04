#include "native/now_playing_cover.hpp"
#ifdef _WIN32
#include "core/source_camera.hpp"
#include <algorithm>
#include <cmath>

namespace endfield::native {
namespace {
namespace d = modules::design;
using M = core::Matrix4;
void need(bool v, const char* s) { if (!v) throw std::invalid_argument(s); }
constexpr std::array<Vertex, 4> vertices{{{{0, 0, 0}, {0, 0}}, {{1, 0, 0}, {1, 0}}, {{1, 1, 0}, {1, 1}}, {{0, 1, 0}, {0, 1}}}};
constexpr std::array<std::uint32_t, 6> indices{0, 1, 2, 0, 2, 3};
}

struct NativeNowPlayingCover::Impl {
    struct Slot {
        std::string texture;
        modules::NowPlayingCover image, uploaded;
        std::uint64_t revision{};
        bool resident{};
    };
    LayerScene carrier;
    std::string prefix, mesh;
    std::array<Slot, 2> slots;
    std::array<DrawObject, 2> draws;
    bool meshResident{};
    NativeNowPlayingCoverStats statistics;
    explicit Impl(LayerRasterizer& r) : carrier(r) {
        static std::uint64_t serial{};
        prefix = "nowPlaying/cover/" + std::to_string(++serial);
        mesh = prefix + "/quad";
        carrier.load(ehud::data::Json::Object{{"bounds", ehud::data::Json::Array{0, 0, 1, 1}}, {"children", ehud::data::Json::Array{}}}, {});
        for (std::size_t n = 0; n < slots.size(); ++n) {
            slots[n].texture = prefix + "/texture/" + std::to_string(n);
            auto& draw = draws[n];
            draw.sourceID = prefix + "/draw/" + std::to_string(n);
            draw.meshID = mesh;
            draw.opacity = 0;
            draw.masks.reserve(6);
        }
    }
};

NativeNowPlayingCover::NativeNowPlayingCover(LayerRasterizer& r) : impl_(std::make_unique<Impl>(r)) {}
NativeNowPlayingCover::~NativeNowPlayingCover() = default;

void NativeNowPlayingCover::updatePose(const NativeDesignScenePose& pose, const d::Layer& root, std::span<const modules::NowPlayingCanvas::CoverView, 2> views) {
    auto& i = *impl_;
    need(pose.boxWorld.finite() && std::isfinite(pose.opacity) && pose.ownerMasks.size() <= 4, "Invalid Now Playing cover pose");
    // Keep each cover in the slot that already holds it (no re-upload).
    std::array<std::optional<std::size_t>, 2> chosen{};
    std::array<bool, 2> taken{};
    for (std::size_t n = 0; n < views.size(); ++n)
        for (std::size_t s = 0; s < i.slots.size() && views[n].image; ++s)
            if (!taken[s] && i.slots[s].image == views[n].image) { taken[s] = true; chosen[n] = s; break; }
    for (std::size_t n = 0; n < views.size(); ++n) {
        if (chosen[n] || !views[n].image) continue;
        for (std::size_t s = 0; s < i.slots.size(); ++s) if (!taken[s]) { taken[s] = true; chosen[n] = s; i.slots[s].image = views[n].image; break; }
    }
    for (std::size_t s = 0; s < i.slots.size(); ++s) if (!taken[s]) i.slots[s].image.reset();
    // Paint order: the bottom view's draw first.
    for (std::size_t n = 0; n < views.size(); ++n) {
        auto& draw = i.draws[n];
        draw.masks.clear();
        draw.opacity = 0;
        draw.shutter = pose.shutter;
        const auto& view = views[n];
        if (!view.image || !view.layer || !chosen[n]) { draw.textureID.clear(); draw.world = M{}; continue; }
        const auto s = d::sampleLayer(*view.layer, root, pose.time);
        const auto size = view.layer->bounds();
        const auto& image = *view.image;
        if (!s.visible || size.width <= 0 || size.height <= 0 || !image.width || !image.height) { draw.textureID.clear(); continue; }
        const auto world = pose.boxWorld * designMatrix(s.world);
        // resizeAspectFill: the image covers the layer, centred, cropped by the mask.
        const double scale = std::max(size.width / image.width, size.height / image.height);
        const double w = image.width * scale, h = image.height * scale;
        draw.world = world * M::translation((size.width - w) / 2, (size.height - h) / 2, 0) * M::scale(w, h, 1);
        draw.textureID = i.slots[*chosen[n]].texture;
        draw.opacity = pose.opacity * float(s.opacity);
        draw.masks.push_back({core::source::inverseSourceMatrix(world), {0, 0, size.width, size.height}, 0});
        for (const auto& owner : pose.ownerMasks) draw.masks.push_back(owner);
    }
    ++i.statistics.poses;
}
void NativeNowPlayingCover::upload(Renderer& r) {
    auto& i = *impl_;
    if (!i.meshResident) { (void)r.setMesh(i.mesh, 1, {vertices, indices}); i.meshResident = true; }
    i.carrier.uploadResources(r);
    for (auto& slot : i.slots) {
        if (!slot.image || slot.image == slot.uploaded) continue;
        const auto& image = *slot.image;
        need(image.straightRGBA.size() == std::size_t(image.width) * image.height * 4, "Invalid Now Playing cover pixels");
        (void)r.setTexture(slot.texture, ++slot.revision, {image.width, image.height, image.straightRGBA, TextureColorSpace::encodedSRGB});
        slot.uploaded = slot.image; slot.resident = true;
        ++i.statistics.uploads;
    }
}
LayerCompositionEntry NativeNowPlayingCover::entry() noexcept { return {&impl_->carrier, impl_->draws}; }
void NativeNowPlayingCover::collected(Renderer& r) {
    auto& i = *impl_;
    for (auto& slot : i.slots) {
        if (slot.image || !slot.resident) continue;
        if (r.removeTexture(slot.texture)) { slot.resident = false; slot.uploaded.reset(); }
    }
}
bool NativeNowPlayingCover::release(Renderer& r) {
    auto& i = *impl_;
    for (auto& slot : i.slots) if (slot.resident) { if (!r.removeTexture(slot.texture)) return false; slot.resident = false; slot.uploaded.reset(); }
    if (i.meshResident) { (void)r.removeMesh(i.mesh); i.meshResident = false; }
    (void)i.carrier.releaseResources(r);
    return true;
}
NativeNowPlayingCoverStats NativeNowPlayingCover::stats() const noexcept {
    auto s = impl_->statistics;
    for (const auto& slot : impl_->slots) if (slot.resident) { ++s.residentTextures; if (slot.uploaded) s.residentBytes += slot.uploaded->straightRGBA.size(); }
    return s;
}
} // namespace endfield::native
#endif
