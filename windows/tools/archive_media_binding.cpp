#include "tools/archive_media_binding.hpp"
#ifdef _WIN32
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <deque>
#include <stdexcept>

namespace endfield::tools {
namespace {
namespace n = native;
namespace m = modules;
namespace d = modules::design;
using J = m::ArchiveJson;
void need(bool ok, const char* why) { if (!ok) throw std::invalid_argument(why); }
m::NotesMediaKind kind(const J& j) { m::validateArchiveMedia(j); const auto& s = j["kind"].string(); return s == "image" ? m::NotesMediaKind::image : s == "gif" ? m::NotesMediaKind::gif : m::NotesMediaKind::video; }
std::optional<double> duration(const J& j) { return j["duration"].isNull() ? std::nullopt : std::optional(j["duration"].number()); }
std::string key(std::string_view prefix) { static std::atomic<std::uint64_t> serial{1}; return "archive." + std::string(prefix) + "." + std::to_string(serial.fetch_add(1, std::memory_order_relaxed)); }
std::string path(const std::filesystem::path& p) { const auto bytes = p.u8string(); return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size()); }
constexpr std::array<n::Vertex, 4> vertices{{{{0, 0, 0}, {0, 0}}, {{1, 0, 0}, {1, 0}}, {{1, 1, 0}, {1, 1}}, {{0, 1, 0}, {0, 1}}}};
constexpr std::array<std::uint32_t, 6> indices{0, 1, 2, 0, 2, 3};
// ArchiveGalleryThumbnails.cacheLimit plus its wanted set: posters published at once.
constexpr std::size_t posterLimit = 18 + 12;
}

struct ArchiveMediaBinding::Impl : std::enable_shared_from_this<Impl> {
    struct Asset {
        J reference; std::string key; n::NotesImageAccess access; m::NotesMediaKind kind{};
        std::shared_ptr<const n::NotesImageFrame> frame;
        std::uint64_t observedFrame{}, uploadedFrame{}, appliedPoster{};
        HRESULT observedError{S_OK};
        bool uploaded{}, imageSubmitted{}, videoSubmitted{}, failed{};
        std::optional<std::string> error;
    };
    struct Poster {
        J reference; std::shared_ptr<Asset> asset; std::shared_ptr<const n::LayerMemoryImage> image;
        std::vector<std::function<void(std::optional<d::ImageSource>)>> waiting;
        bool released{}, settled{};
        std::uint64_t used{};
    };
    struct Import { m::ArchiveMediaRequest request; std::uint64_t serial{}; std::vector<ArchiveMediaImportFile> files; std::vector<std::optional<n::NotesImageCompletion>> results; };
    n::NativeMediaRequestBroker& broker; n::NativeMediaRequestBroker::Client client; n::Renderer& renderer; n::LayerImageSource& images;
    ArchiveMediaBindingOptions options;
    std::string meshID{key("quad")};
    bool meshUploaded{}, alive{true}, visible{}, requestsDirty{true};
    double time{};
    std::uint64_t used{}, importSerial{1};
    std::optional<std::string> document;
    std::size_t index{};
    std::shared_ptr<Asset> selected;
    std::vector<std::shared_ptr<Asset>> assets;                 // every asset with broker or GPU resources
    std::deque<Poster> posters;
    std::optional<Import> import;
    bool publishedPlaying{};
    double publishedTime{};
    std::optional<std::string> publishedError;
    std::optional<Frame> published;
    Impl(n::NativeMediaRequestBroker& b, n::NativeMediaRequestBroker::Client c, n::Renderer& r, n::LayerImageSource& i, ArchiveMediaBindingOptions o)
        : broker(b), client(c), renderer(r), images(i), options(std::move(o)) {
        need(client && options.resolve && options.errorText, "Archive media needs the broker client and explicit access / error contracts");
        assets.reserve(32);
    }
    void clock(double t) { need(std::isfinite(t), "Archive media requires a finite owner clock"); time = std::max(time, t); }
    std::shared_ptr<Asset> asset(const J& reference, std::string_view prefix) {
        auto a = std::make_shared<Asset>();
        a->reference = reference; a->key = key(prefix); a->kind = kind(reference);
        try {
            a->access = options.resolve(reference);
            need(ehud::data::validWindowsFilePath(path(a->access.path)), "Archive media access must use a validated absolute Windows locator");
        } catch (...) { a->failed = true; a->error = options.errorText(E_ACCESSDENIED); }
        assets.push_back(a);
        return a;
    }
    void submit() {
        if (!requestsDirty) return;
        std::vector<n::NotesImagePlaybackRequest> still; std::vector<n::NotesVideoRequest> movie;
        still.reserve(posters.size() + 1); movie.reserve(1);
        const auto add = [&](const std::shared_ptr<Asset>& a, int dimension, bool poster) {
            if (!a || a->failed) return;
            n::NotesImageRequest image{a->key, path(a->access.path), 1, dimension, poster || a->kind == m::NotesMediaKind::image, a->access.lease, false, a->kind == m::NotesMediaKind::video};
            still.push_back({image, poster || a->kind == m::NotesMediaKind::video ? m::NotesMediaKind::image : a->kind});
            if (!poster && a->kind == m::NotesMediaKind::video && broker.hasVideoPlayback())
                movie.push_back({a->key, image.path, 1, 512, duration(a->reference), a->access.lease, true});
        };
        if (visible) {
            add(selected, 512, false);
            for (auto& p : posters) if (!p.settled && !p.waiting.empty()) add(p.asset, 256, true);
        }
        broker.setImages(client, 1, still, time, !visible);
        for (const auto& r : still) for (auto& a : assets) if (a->key == r.image.key) a->imageSubmitted = true;
        broker.setVideos(client, 1, movie, time, !visible);
        for (const auto& r : movie) for (auto& a : assets) if (a->key == r.key) a->videoSubmitted = true;
        requestsDirty = false;
        // A settled poster's still is in the memory image: its decoder request goes.
        for (auto& p : posters) if (p.settled && p.asset && p.asset->imageSubmitted && broker.retireImage(client, p.asset->key)) { p.asset->imageSubmitted = false; p.asset->access.lease.reset(); }
    }
    void answer(Poster& p) {
        std::optional<d::ImageSource> source;
        if (p.image) source = d::ImageSource{d::ImageSource::Kind::memory, p.image->key(), {}, p.image->revision()};
        auto waiting = std::move(p.waiting);
        p.waiting.clear();
        for (auto& w : waiting) if (w) w(source);
    }
    // Released posters stay published (their tiles may still show them); the
    // oldest released one goes past the bound.
    void evict() {
        while (posters.size() > posterLimit) {
            auto it = posters.end();
            for (auto p = posters.begin(); p != posters.end(); ++p) if (p->released && p->waiting.empty() && (it == posters.end() || p->used < it->used)) it = p;
            if (it == posters.end()) break;
            if (it->image) images.retire(it->image->key());
            posters.erase(it);
        }
    }
    struct Imported { m::ArchiveMediaRequest request; std::vector<J> references; std::optional<std::string> error; };
    std::optional<Imported> inspect() {
        if (!import) return std::nullopt;
        auto batch = broker.drainInspections(client, import->serial);
        if (batch.empty()) return std::nullopt;
        for (auto& result : batch)
            for (std::size_t k = 0; k < import->files.size(); ++k) if (result.key == "import." + std::to_string(import->serial) + "." + std::to_string(k)) import->results[k] = std::move(result);
        if (std::any_of(import->results.begin(), import->results.end(), [](const auto& r) { return !r; })) return std::nullopt;
        auto complete = std::move(*import);
        import.reset();
        broker.cancelInspections(client, ++importSerial);
        std::vector<J> references; std::optional<std::string> error;
        for (std::size_t k = 0; k < complete.files.size(); ++k) {
            const auto& r = *complete.results[k];
            try {
                if (FAILED(r.result) || !r.info) throw std::runtime_error("Media validation failed");
                const auto& i = *r.info;
                const auto type = i.kind == m::NotesMediaKind::image ? "image" : i.kind == m::NotesMediaKind::gif ? "gif" : "video";
                references.push_back(J::parse(ehud::data::makeWindowsMediaReference(complete.files[k].path, complete.files[k].displayName, int(i.pixelWidth), int(i.pixelHeight), type,
                    i.kind == m::NotesMediaKind::image ? std::nullopt : std::optional(i.duration), int(i.frameCount))));
            } catch (...) { error = options.errorText(FAILED(r.result) ? r.result : E_INVALIDARG); }
        }
        return Imported{complete.request, std::move(references), std::move(error)};
    }
};

ArchiveMediaBinding::ArchiveMediaBinding(n::NativeMediaRequestBroker& b, n::NativeMediaRequestBroker::Client c, n::Renderer& r, n::LayerImageSource& i, ArchiveMediaBindingOptions o)
    : impl_(std::make_shared<Impl>(b, c, r, i, std::move(o))) {}
ArchiveMediaBinding::~ArchiveMediaBinding() { auto i = impl_; i->alive = false; try { (void)releaseResources(i->time); } catch (...) {} }

// syncMedia (:1110-1130): the same document, index and reference keep the presentation.
void ArchiveMediaBinding::show(std::string_view document, std::size_t index, const J& reference) {
    auto& i = *impl_;
    if (i.selected && i.document == document && i.index == index && i.selected->reference == reference) return;
    i.document = std::string(document); i.index = index;
    i.selected = i.asset(reference, "media");
    i.requestsDirty = true;
    i.published.reset();
    i.submit();
}
void ArchiveMediaBinding::clear() {
    auto& i = *impl_;
    if (!i.selected && !i.document) return;
    i.selected.reset(); i.document.reset(); i.index = 0;
    i.requestsDirty = true; i.published.reset();
    i.publishedPlaying = false; i.publishedTime = 0; i.publishedError.reset();
    i.submit();
}
bool ArchiveMediaBinding::playing() const { return impl_->publishedPlaying; }
std::optional<std::string> ArchiveMediaBinding::error() const { return impl_->publishedError; }
double ArchiveMediaBinding::currentTime() const { return impl_->publishedTime; }
void ArchiveMediaBinding::togglePlayback() {
    auto& i = *impl_;
    if (!i.visible || !i.selected) return;
    const bool done = i.selected->kind == m::NotesMediaKind::video ? i.broker.toggleVideo(i.client, i.selected->key, i.time)
        : i.selected->kind == m::NotesMediaKind::gif ? i.broker.toggleImage(i.client, i.selected->key, i.time) : false;
    if (done) (void)refresh(i.time);
}
void ArchiveMediaBinding::seek(double seconds) {
    auto& i = *impl_;
    if (!i.visible || !i.selected || i.selected->kind != m::NotesMediaKind::video || !std::isfinite(seconds)) return;
    if (i.broker.seekVideo(i.client, i.selected->key, std::max(0.0, seconds))) (void)refresh(i.time);
}
void ArchiveMediaBinding::decodePoster(const J& reference, std::function<void(std::optional<d::ImageSource>)> done) {
    auto& i = *impl_;
    // A poster of this reference already published (released through a
    // stand-by, or wanted again) answers at the next refresh without decoding.
    for (auto& p : i.posters) if (p.reference == reference) { p.released = false; p.used = ++i.used; p.waiting.push_back(std::move(done)); if (!p.settled) i.requestsDirty = true; return; }
    Impl::Poster p;
    p.reference = reference; p.used = ++i.used; p.waiting.push_back(std::move(done));
    try { p.asset = i.asset(reference, "poster"); } catch (...) { p.settled = true; }
    if (p.asset && p.asset->failed) p.settled = true;
    i.posters.push_back(std::move(p));
    i.requestsDirty = true;
    i.evict();
}
void ArchiveMediaBinding::releasePoster(const d::ImageSource& source) {
    auto& i = *impl_;
    for (auto& p : i.posters) if (p.image && p.image->key() == source.key) p.released = true;
    i.evict();
}
void ArchiveMediaBinding::setVisible(bool value, double t) {
    auto& i = *impl_;
    i.clock(t);
    if (i.visible == value) return;
    i.visible = value;
    i.requestsDirty = true;
    if (!value) { (void)cancelImport(); if (i.selected) { i.selected->error.reset(); i.selected->observedError = S_OK; } }
    i.submit();
}
bool ArchiveMediaBinding::refresh(double t) {
    auto& i = *impl_;
    i.clock(t);
    bool changed{};
    if (auto imported = i.inspect()) { changed = true; if (onImported) onImported(imported->request, std::move(imported->references), std::move(imported->error)); }
    // Posters: a decoded still becomes a memory image; failures answer none.
    for (auto& p : i.posters) {
        if (p.settled) { if (!p.waiting.empty()) { i.answer(p); changed = true; } continue; }
        const auto* record = p.asset ? i.broker.findImage(i.client, p.asset->key) : nullptr;
        if (record && record->frame) {
            const auto& f = *record->frame;
            try { p.image = i.images.publish(p.asset->key, record->frameRevision, f.width, f.height, f.straightRGBA); } catch (...) { p.image.reset(); }
            p.settled = true; i.requestsDirty = true;
            i.answer(p); changed = true;
        } else if ((record && FAILED(record->error)) || (p.asset && p.asset->failed)) {
            p.settled = true; i.requestsDirty = true;
            i.answer(p); changed = true;
        }
    }
    i.evict();
    bool playing{};
    double progress{};
    std::optional<std::string> error;
    std::optional<Frame> frame;
    if (i.selected) {
        auto& a = *i.selected;
        if (const auto* record = i.broker.findImage(i.client, a.key)) {
            if (record->frame) {
                a.frame = record->frame; a.observedFrame = record->frameRevision;
                if (a.kind == m::NotesMediaKind::video && a.appliedPoster != record->frameRevision
                    && i.broker.setVideoPoster(i.client, a.key, 1, record->frameRevision, record->frame.get(), record->error)) a.appliedPoster = record->frameRevision;
            }
            if (FAILED(record->error) && a.kind != m::NotesMediaKind::video && a.observedError != record->error) { a.error = i.options.errorText(record->error); a.observedError = record->error; }
            if (a.kind == m::NotesMediaKind::gif) playing = record->state == m::NotesMediaState::playing;
        }
        if (a.kind == m::NotesMediaKind::video) {
            if (const auto* v = i.broker.findVideo(i.client, a.key)) {
                if (!v->textureID.empty() && v->width && v->height) frame = Frame{v->textureID, v->width, v->height};
                playing = v->state == m::NotesMediaState::playing;
                progress = v->pendingSeek.value_or(v->currentTime);
                if (FAILED(v->error) && a.observedError != v->error) { a.error = i.options.errorText(v->error); a.observedError = v->error; }
            } else if (!i.broker.hasVideoPlayback() && a.observedError != E_NOTIMPL) { a.error = i.options.errorText(E_NOTIMPL); a.observedError = E_NOTIMPL; }
        }
        if (!frame && a.frame) {
            if (!a.uploaded || a.uploadedFrame != a.observedFrame) {
                i.renderer.setTexture(a.key, a.observedFrame, {a.frame->width, a.frame->height, a.frame->straightRGBA});
                a.uploadedFrame = a.observedFrame; a.uploaded = true; changed = true;
            }
            frame = Frame{a.key, a.frame->width, a.frame->height};
        }
        error = a.error;
        if (frame && !i.meshUploaded) { i.renderer.setMesh(i.meshID, 1, {vertices, indices}); i.meshUploaded = true; }
    }
    const bool state = playing != i.publishedPlaying || error != i.publishedError;
    const bool moved = progress != i.publishedTime;
    if (frame != i.published) { i.published = frame; changed = true; }
    i.publishedPlaying = playing; i.publishedTime = progress; i.publishedError = error;
    i.submit();
    if (state && onStateChange) { changed = true; onStateChange(); }
    if (moved && onProgress) onProgress();
    return changed || moved;
}
std::optional<ArchiveMediaBinding::Frame> ArchiveMediaBinding::frame() const { return impl_->published; }
const std::string& ArchiveMediaBinding::mesh() const noexcept { return impl_->meshID; }
bool ArchiveMediaBinding::beginImport(const m::ArchiveMediaRequest& request, std::span<const ArchiveMediaImportFile> files, double t) {
    auto& i = *impl_;
    i.clock(t);
    if (!request.remaining || files.empty()) return false;
    const auto count = std::min({std::size_t{16}, request.remaining, files.size()});
    Impl::Import next;
    next.request = request; next.serial = i.importSerial + 1;
    next.files.assign(files.begin(), files.begin() + std::ptrdiff_t(count));
    next.results.resize(count);
    std::vector<n::NotesImageRequest> requests;
    requests.reserve(count);
    for (std::size_t k = 0; k < count; ++k) {
        const auto& f = next.files[k];
        need(ehud::data::validWindowsFilePath(f.path) && !f.displayName.empty() && f.displayName.size() <= 4096 && J::validUtf8(f.displayName), "Invalid explicit Archive import file");
        requests.push_back({"import." + std::to_string(next.serial) + "." + std::to_string(k), f.path, 1, 64, false, f.accessLease, true});
    }
    i.broker.setInspections(i.client, next.serial, requests);
    i.importSerial = next.serial;
    i.import = std::move(next);
    return true;
}
bool ArchiveMediaBinding::cancelImport() {
    auto& i = *impl_;
    if (!i.import) return false;
    i.broker.cancelInspections(i.client, ++i.importSerial);
    i.import.reset();
    return true;
}
bool ArchiveMediaBinding::collectRetired() {
    auto& i = *impl_;
    bool okay = true;
    for (auto it = i.assets.begin(); it != i.assets.end();) {
        auto& a = **it;
        if (it->use_count() > 1) { ++it; continue; }
        if (a.uploaded && !i.renderer.removeTexture(a.key)) { okay = false; ++it; continue; }
        a.uploaded = false;
        if (a.imageSubmitted && !i.broker.retireImage(i.client, a.key)) { okay = false; ++it; continue; }
        a.imageSubmitted = false;
        if (a.videoSubmitted && !i.broker.retireVideo(i.client, a.key)) { okay = false; ++it; continue; }
        it = i.assets.erase(it);
    }
    return okay;
}
bool ArchiveMediaBinding::releaseResources(double t) {
    auto& i = *impl_;
    i.clock(t);
    (void)cancelImport();
    i.visible = false;
    i.selected.reset(); i.document.reset(); i.published.reset();
    for (auto& p : i.posters) { if (p.image) i.images.retire(p.image->key()); p.image.reset(); p.asset.reset(); if (!p.waiting.empty()) { p.settled = true; i.answer(p); } }
    i.posters.clear();
    i.broker.hideImages(i.client, i.time);
    i.broker.hideVideos(i.client, i.time);
    bool okay = collectRetired();
    if (i.meshUploaded) { if (i.renderer.removeMesh(i.meshID)) i.meshUploaded = false; else okay = false; }
    return okay;
}
std::size_t ArchiveMediaBinding::posters() const noexcept { return std::size_t(std::count_if(impl_->posters.begin(), impl_->posters.end(), [](const auto& p) { return p.image != nullptr; })); }
} // namespace endfield::tools
#endif
