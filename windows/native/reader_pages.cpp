#include "native/reader_pages.hpp"
#ifdef _WIN32
#include "core/source_camera.hpp"
#include <algorithm>
#include <cmath>

namespace endfield::native {
namespace {
namespace d = modules::design;
using M = core::Matrix4;
void need(bool v, const char* s) { if (!v) throw std::invalid_argument(s); }
constexpr std::size_t slotCount = modules::ReaderCanvas::maximumPageLayers;
constexpr std::array<Vertex, 4> vertices{{{{0, 0, 0}, {0, 0}}, {{1, 0, 0}, {1, 0}}, {{1, 1, 0}, {1, 1}}, {{0, 1, 0}, {0, 1}}}};
constexpr std::array<std::uint32_t, 6> indices{0, 1, 2, 0, 2, 3};
// Provider pixels are premultiplied (RGBA or BGRA); the renderer takes straight RGBA.
std::vector<std::uint8_t> straight(const modules::ReaderPagePixels& p) {
    need(p.width && p.height && p.width <= 900 && p.height <= 1000 && p.bytes.size() == std::size_t(p.width) * p.height * 4, "Invalid Reader page pixels");
    const bool rgba = p.format == modules::ReaderPagePixels::Format::rgba8Premultiplied;
    std::vector<std::uint8_t> out(p.bytes.size());
    for (std::size_t n = 0; n < out.size(); n += 4) {
        const unsigned a = p.bytes[n + 3];
        const std::array<unsigned, 3> c{rgba ? p.bytes[n] : p.bytes[n + 2], p.bytes[n + 1], rgba ? p.bytes[n + 2] : p.bytes[n]};
        for (unsigned k = 0; k < 3; ++k) out[n + k] = static_cast<std::uint8_t>(a ? std::min(255u, (c[k] * 255u + a / 2) / a) : 0);
        out[n + 3] = static_cast<std::uint8_t>(a);
    }
    return out;
}
}

struct NativeReaderPages::Impl {
    struct Slot {
        std::string texture;
        std::shared_ptr<const modules::ReaderPagePixels> pixels, uploaded;
        std::uint64_t revision{};
        bool resident{};
    };
    struct View { const d::Layer* layer{}; std::size_t slot{}; double cropTop{}, cropHeight{1}; };
    LayerScene carrier;
    std::string prefix, mesh;
    std::array<Slot, slotCount> slots;
    std::array<View, slotCount> views{};
    std::size_t viewCount{};
    std::array<DrawObject, slotCount> draws;
    Renderer* renderer{};
    bool meshResident{};
    NativeReaderPagesStats statistics;
    explicit Impl(LayerRasterizer& r) : carrier(r) {
        static std::uint64_t serial{};
        prefix = "reader/pages/" + std::to_string(++serial);
        mesh = prefix + "/quad";
        carrier.load(ehud::data::Json::Object{{"bounds", ehud::data::Json::Array{0, 0, 1, 1}}, {"children", ehud::data::Json::Array{}}}, {});
        for (std::size_t n = 0; n < slotCount; ++n) {
            slots[n].texture = prefix + "/texture/" + std::to_string(n);
            auto& draw = draws[n];
            draw.sourceID = prefix + "/draw/" + std::to_string(n);
            draw.meshID = mesh;
            draw.opacity = 0;
            draw.masks.reserve(8);
        }
    }
};

NativeReaderPages::NativeReaderPages(LayerRasterizer& r) : impl_(std::make_unique<Impl>(r)) {}
NativeReaderPages::~NativeReaderPages() = default;

void NativeReaderPages::setViews(std::span<const modules::ReaderCanvas::PageView> views) {
    auto& i = *impl_;
    need(views.size() <= slotCount, "At most six Reader page layers");
    for (const auto& v : views) need(v.layer && v.pixels && std::isfinite(v.cropTop) && std::isfinite(v.cropHeight) && v.cropHeight > 0, "Invalid Reader page view");
    // Keep a slot for every view whose pixels are already resident there.
    std::array<bool, slotCount> taken{};
    std::array<std::optional<std::size_t>, slotCount> chosen{};
    for (std::size_t n = 0; n < views.size(); ++n)
        for (std::size_t s = 0; s < slotCount; ++s)
            if (!taken[s] && i.slots[s].pixels == views[n].pixels) { taken[s] = true; chosen[n] = s; break; }
    for (std::size_t n = 0; n < views.size(); ++n) {
        if (chosen[n]) continue;
        for (std::size_t s = 0; s < slotCount; ++s) if (!taken[s]) { taken[s] = true; chosen[n] = s; i.slots[s].pixels = views[n].pixels; break; }
    }
    for (std::size_t s = 0; s < slotCount; ++s) if (!taken[s]) i.slots[s].pixels.reset();
    i.viewCount = views.size();
    for (std::size_t n = 0; n < views.size(); ++n) i.views[n] = {views[n].layer, *chosen[n], views[n].cropTop, views[n].cropHeight};
}

void NativeReaderPages::updatePose(const NativeDesignScenePose& pose, const d::Layer& root, const d::Layer* clip) {
    auto& i = *impl_;
    need(pose.boxWorld.finite() && std::isfinite(pose.opacity) && pose.ownerMasks.size() <= 4, "Invalid Reader page pose");
    // The viewport clip (masksToBounds); its opacity is in each page's chain.
    std::optional<PlaneMask> clipMask;
    if (clip) {
        const auto c = d::sampleLayer(*clip, root, pose.time);
        if (c.visible) clipMask = PlaneMask{core::source::inverseSourceMatrix(pose.boxWorld * designMatrix(c.world)), {0, 0, clip->bounds().width, clip->bounds().height}, 0};
    }
    for (std::size_t n = 0; n < slotCount; ++n) {
        auto& draw = i.draws[n];
        draw.masks.clear();
        draw.opacity = 0;
        draw.shutter = pose.shutter;
        if (n >= i.viewCount) { draw.textureID.clear(); draw.world = M{}; continue; }
        const auto& view = i.views[n];
        const auto s = d::sampleLayer(*view.layer, root, pose.time);
        const auto size = view.layer->bounds();
        const auto world = pose.boxWorld * designMatrix(s.world);
        // contentsRect y / height: the raster's visible band fills the layer.
        const double height = size.height / view.cropHeight;
        draw.world = world * M::translation(0, -view.cropTop * height, 0) * M::scale(size.width, height, 1);
        draw.textureID = i.slots[view.slot].texture;
        draw.opacity = s.visible && size.width > 0 && size.height > 0 ? pose.opacity * float(s.opacity) : 0.f;
        draw.masks.push_back({core::source::inverseSourceMatrix(world), {0, 0, size.width, size.height}, 0});
        if (clipMask) draw.masks.push_back(*clipMask);
        for (const auto& owner : pose.ownerMasks) draw.masks.push_back(owner);
    }
    ++i.statistics.poses;
}

void NativeReaderPages::upload(Renderer& r) {
    auto& i = *impl_;
    need(!i.renderer || i.renderer == &r, "Reader pages belong to one renderer");
    i.renderer = &r;
    if (!i.meshResident) { (void)r.setMesh(i.mesh, 1, {vertices, indices}); i.meshResident = true; }
    i.carrier.uploadResources(r);
    for (auto& slot : i.slots) {
        if (!slot.pixels || slot.pixels == slot.uploaded) continue;
        const auto bytes = straight(*slot.pixels);
        (void)r.setTexture(slot.texture, ++slot.revision, {slot.pixels->width, slot.pixels->height, bytes});
        slot.uploaded = slot.pixels; slot.resident = true;
        ++i.statistics.pixelUploads;
    }
}
LayerCompositionEntry NativeReaderPages::entry() noexcept { return {&impl_->carrier, impl_->draws}; }
void NativeReaderPages::collected(Renderer& r) {
    auto& i = *impl_;
    for (auto& slot : i.slots) {
        if (slot.pixels || !slot.resident) continue;
        if (r.removeTexture(slot.texture)) { slot.resident = false; slot.uploaded.reset(); }
    }
}
bool NativeReaderPages::release(Renderer& r) {
    auto& i = *impl_;
    for (auto& slot : i.slots) if (slot.resident) { if (!r.removeTexture(slot.texture)) return false; slot.resident = false; slot.uploaded.reset(); }
    if (i.meshResident) { (void)r.removeMesh(i.mesh); i.meshResident = false; }
    (void)i.carrier.releaseResources(r);
    i.renderer = nullptr;
    return true;
}
std::size_t NativeReaderPages::pixelBytes() const noexcept {
    const auto& i = *impl_;
    std::size_t bytes{};
    for (std::size_t n = 0; n < slotCount; ++n) {
        const auto& p = i.slots[n].pixels;
        if (!p) continue;
        bool repeated{};
        for (std::size_t k = 0; k < n; ++k) repeated |= i.slots[k].pixels == p;
        if (!repeated) bytes += p->bytes.size();
    }
    return bytes;
}
NativeReaderPagesStats NativeReaderPages::stats() const noexcept {
    auto s = impl_->statistics;
    for (const auto& slot : impl_->slots) if (slot.resident) { ++s.residentTextures; if (slot.uploaded) s.residentBytes += slot.uploaded->bytes.size(); }
    return s;
}
} // namespace endfield::native
#endif
