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
    // Two images per cover view (its backdrop and its art).
    std::array<Slot, 4> slots;
    std::array<DrawObject, 4> draws;
    bool meshResident{};
    NativeNowPlayingCoverStats statistics;
    explicit Impl(LayerRasterizer& r) : carrier(r) {
        static std::uint64_t serial{};
        prefix = "nowPlaying/cover/" + std::to_string(++serial);
        mesh = prefix + "/quad";
        carrier.load(ehud::data::Json::Object{{"bounds", ehud::data::Json::Array{0, 0, 1, 1}}, {"children", ehud::data::Json::Array{}}}, {});
        for (std::size_t n = 0; n < slots.size(); ++n) slots[n].texture = prefix + "/texture/" + std::to_string(n);
        for (std::size_t n = 0; n < draws.size(); ++n) {
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
    // Paint order: the bottom view's backdrop and art, then the top view's.
    std::array<modules::NowPlayingCover, 4> images{};
    for (std::size_t n = 0; n < views.size(); ++n) { images[n * 2] = views[n].backdrop; images[n * 2 + 1] = views[n].image; }
    if (!views[0].image) images[0].reset();
    if (!views[1].image) images[2].reset();
    // Keep each image in the slot that already holds it (no re-upload).
    std::array<std::optional<std::size_t>, 4> chosen{};
    std::array<bool, 4> taken{};
    for (std::size_t n = 0; n < images.size(); ++n)
        for (std::size_t s = 0; s < i.slots.size() && images[n]; ++s)
            if (!taken[s] && i.slots[s].image == images[n]) { taken[s] = true; chosen[n] = s; break; }
    for (std::size_t n = 0; n < images.size(); ++n) {
        if (chosen[n] || !images[n]) continue;
        for (std::size_t s = 0; s < i.slots.size(); ++s) if (!taken[s]) { taken[s] = true; chosen[n] = s; i.slots[s].image = images[n]; break; }
    }
    for (std::size_t s = 0; s < i.slots.size(); ++s) if (!taken[s]) i.slots[s].image.reset();
    for (std::size_t n = 0; n < images.size(); ++n) {
        auto& draw = i.draws[n];
        draw.masks.clear();
        draw.opacity = 0;
        draw.shutter = pose.shutter;
        const auto& view = views[n / 2];
        const bool backdrop = n % 2 == 0;
        if (!images[n] || !view.layer || !chosen[n]) { draw.textureID.clear(); draw.world = M{}; continue; }
        const auto s = d::sampleLayer(*view.layer, root, pose.time);
        const auto size = view.layer->bounds();
        const auto& image = *images[n];
        if (!s.visible || size.width <= 0 || size.height <= 0 || !image.width || !image.height) { draw.textureID.clear(); continue; }
        const auto world = pose.boxWorld * designMatrix(s.world);
        if (backdrop) {
            // resizeAspectFill: the backdrop covers the layer, centred.
            const double scale = std::max(size.width / image.width, size.height / image.height);
            const double w = image.width * scale, h = image.height * scale;
            draw.world = world * M::translation((size.width - w) / 2, (size.height - h) / 2, 0) * M::scale(w, h, 1);
        } else {
            // The canvas's frame: aspect-filled, or a small cover at its own pixels.
            draw.world = world * M::translation(view.frame.x, view.frame.y, 0) * M::scale(view.frame.width, view.frame.height, 1);
        }
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
    for (const auto& draw : impl_->draws) if (!draw.textureID.empty()) ++s.drawnTextures;
    return s;
}
} // namespace endfield::native
#endif
