#include "tools/archive_owner.hpp"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "native/archive_owner_authentication.hpp"
#include "native/hud_design_scene.hpp"
#include "tools/design_body_route.hpp"
#include "core/notes_font_names.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace endfield::tools {
namespace {
namespace d = modules::design;
namespace m = modules;
namespace gpu = native;
using Matrix = core::Matrix4;
void need(bool v, const char* s) { if (!v) throw std::invalid_argument(s); }
std::optional<double> earlier(std::optional<double> a, std::optional<double> b) { return a && b ? std::min(*a, *b) : a ? a : b; }
ehud::data::Json blank() {
    return ehud::data::Json::Object{{"bounds", ehud::data::Json::Array{0, 0, 0, 0}}, {"position", ehud::data::Json::Array{0, 0}}, {"anchorPoint", ehud::data::Json::Array{0, 0}}, {"children", ehud::data::Json::Array{}}};
}
}

struct ArchiveOwner::Impl {
    gpu::LayerRasterizer& raster;
    d::Context& context;
    app::ArchiveService& service;
    ArchiveOwnerOptions options;
    std::unique_ptr<ArchiveMediaBinding> media;
    std::unique_ptr<gpu::NativeArchiveBodyEditor> body;
    std::unique_ptr<DesignBodyRoute> route;     // the body's routing beside the core (shared with the Notes centre)
    std::unique_ptr<gpu::NativeArchiveOwnerAuthentication> authentication;
    std::unique_ptr<DesignOwnerCore> core;
    m::ArchiveCanvas* canvas{};
    gpu::LayerScene carrier;            // carries the media record when the body editor is not drawn
    std::array<gpu::DrawObject, 1> mediaDraw{};
    struct Insertion { const d::Layer* after{}; gpu::LayerScene* scene{}; std::size_t draws{}; std::string texture; bool operator==(const Insertion&) const = default; };
    std::optional<Insertion> inserted;
    app::ClientMetrics metrics;
    bool overlay{true}, focused{true}, releasePending{};
    double time{};
    Impl(gpu::LayerRasterizer& r, d::Context& c, app::ArchiveService& s, ArchiveOwnerOptions o) : raster(r), context(c), service(s), options(std::move(o)), carrier(r) {}
    void clock(double t) { need(std::isfinite(t), "Archive owner needs finite time"); time = std::max(time, t); }
    // Direct canvas calls (outside the core's entry points) run at the
    // owner's time with this core's editor host, as the core's own do.
    void enter(double t) { clock(t); auto& env = context.env(); env.now = std::max(env.now, time); core->bindEditorHost(); }
    bool accepting() const { return overlay && core->inputModule() == core::Module::archive; }
    bool bodyFocused() const { const auto* a = canvas->bodyArea(); return a && a->isFocused(); }
    // The canvas takes input and nothing above it takes the pointer.
    bool free() const { return accepting() && !canvas->capturesPointer() && !context.overlay().capturesInput() && !canvas->transitioning(); }
    bool keysToBody() const { return accepting() && bodyFocused() && !context.overlay().capturesInput() && !canvas->capturesPointer(); }
    // The body editor's pose, the media record and their insertion into the
    // canvas's scene (after the media strip's part, else the body's, so the
    // top layer's menus and pop-overs draw above both).
    void compose() {
        const auto pose = core->pose(core::Module::archive);
        std::optional<gpu::LayerCompositionEntry> entry;
        Insertion wanted;
        if (pose && metrics.pixelWidth && metrics.pixelHeight) {
            const auto& palette = context.env().palette;
            route->pose(*pose, metrics, canvas->root(), focused, context.env().reduceMotion, time,
                palette.tone(0xEEEEEE), palette.accentInk(), d::withAlpha(palette.yellow(), .35));
            const auto* preview = canvas->mediaPreview();
            const auto frame = media->frame();
            std::size_t draws{};
            if (preview && frame && frame->width && frame->height && d::isVisible(*preview, canvas->root())) {
                const auto layer = d::sampleLayer(*preview, canvas->root(), time);
                if (layer.visible) {
                    // NotesMediaPresentation.layer: aspect-fit (resizeAspect) in the 16:10 preview.
                    const auto box = preview->bounds();
                    const double scale = std::min(box.width / frame->width, box.height / frame->height);
                    const double w = frame->width * scale, h = frame->height * scale;
                    auto& draw = mediaDraw[0];
                    draw.sourceID = "archive.media.preview"; draw.meshID = media->mesh(); draw.textureID = frame->texture;
                    draw.world = pose->boxWorld * gpu::designMatrix(layer.world) * Matrix::translation((box.width - w) / 2, (box.height - h) / 2) * Matrix::scale(w, h);
                    draw.opacity = float(std::clamp(double(pose->opacity) * layer.opacity, 0.0, 1.0));
                    draws = 1;
                }
            }
            const auto glyphs = body->entry();
            const auto* bodyLayer = canvas->bodyArea() ? &canvas->bodyArea()->layer() : nullptr;
            const d::Layer* after = draws ? preview->parent() : glyphs ? bodyLayer : nullptr;
            auto* scene = glyphs ? glyphs->scene : draws ? &carrier : nullptr;
            if (after && scene) {
                wanted = {after, scene, draws, draws ? frame->texture : std::string{}};
                entry = gpu::LayerCompositionEntry{scene, std::span<const gpu::DrawObject>(mediaDraw.data(), draws)};
            }
        }
        const auto next = entry ? std::optional(wanted) : std::nullopt;
        if (next == inserted) return;
        inserted = next;
        core->setInsertion(core::Module::archive, next ? next->after : nullptr, entry);
    }
};

std::string archiveDottedDate(double foundation) {
    if (!std::isfinite(foundation)) return {};
    // Foundation seconds -> FILETIME (100 ns since 1601) -> local SYSTEMTIME.
    const double unix = foundation + m::archiveFoundationToUnix;
    const double ticks = (unix + 11644473600.0) * 1e7;
    if (!(ticks >= 0 && ticks < 9.2e18)) return {};
    const auto value = static_cast<std::uint64_t>(ticks);
    FILETIME utc{DWORD(value & 0xffffffffu), DWORD(value >> 32)};
    SYSTEMTIME system{}, local{};
    DYNAMIC_TIME_ZONE_INFORMATION zone{};
    if (!FileTimeToSystemTime(&utc, &system) || GetDynamicTimeZoneInformation(&zone) == TIME_ZONE_ID_INVALID
        || !SystemTimeToTzSpecificLocalTimeEx(&zone, &system, &local)) return {};
    return d::format::dotted(local.wYear, local.wMonth, local.wDay);
}
m::ArchiveFontServices archiveFontServices(gpu::LayerRasterizer& raster) {
    m::ArchiveFontServices fonts;
    const auto bundled = raster.bundledFontFamilies();
    // The Latin family (HarmonyOS Sans) is the default face, listed first by the canvas.
    for (std::size_t n = 1; n < bundled.size(); ++n) fonts.bundledCJKFamilies.push_back(bundled[n]);
    gpu::LayerRasterizer* r = &raster;
    fonts.installedFamilies = [r] { return r->installedFontFamilies(); };
    fonts.familyOf = [r](std::string_view name) -> std::optional<std::string> {
        if (auto shared = r->bundledFamilyOf(name)) return shared;
        if (const auto mapped = core::notes::notesWindowsFontFamily(name)) return std::string(*mapped);
        const auto& installed = r->installedFontFamilies();
        if (std::binary_search(installed.begin(), installed.end(), name, std::less<>{})) return std::string(name);
        return std::nullopt;
    };
    return fonts;
}

app::ModuleRouting ArchiveOwner::routing0() noexcept {
    app::ModuleRouting r;
    r.pointerCapture = 6; r.pointer = 3; r.wheel = 3; r.key = 3; r.filterKey = 3; r.message = 2;
    r.messageRetriesPendingModule = r.messageRetriesPendingClose = true; r.finishEditing = 1; r.flush = 1;
    return r;
}

ArchiveOwner::ArchiveOwner(gpu::LayerRasterizer& r, d::Context& context, app::ArchiveService& service, gpu::NativeMediaRequestBroker& broker,
    gpu::NativeMediaRequestBroker::Client client, gpu::Renderer& renderer, gpu::LayerImageSource& images, ArchiveOwnerOptions o)
    : impl_(std::make_unique<Impl>(r, context, service, std::move(o))) {
    auto& i = *impl_;
    auto& opt = i.options;
    need(opt.newID && opt.foundationNow && opt.dotted, "Archive needs identity, clock and date services");
    need(opt.owner.fields.window && opt.bodyMessage, "Archive needs the HUD window and its body message");
    i.carrier.load(blank(), opt.owner.raster);
    opt.owner.raster.memoryImages = &images;
    i.media = std::make_unique<ArchiveMediaBinding>(broker, client, renderer, images, opt.media);
    gpu::NativeArchiveBodyEditorOptions body;
    body.window = opt.owner.fields.window; body.threadManager = opt.owner.fields.threadManager; body.clientID = opt.owner.fields.clientID;
    body.ownerMessage = opt.bodyMessage; body.raster = opt.owner.raster;
    body.clipboard = opt.clipboard ? opt.clipboard : gpu::systemNotesClipboard();
    body.editMenu = opt.editMenu;
    i.body = std::make_unique<gpu::NativeArchiveBodyEditor>(r, std::move(body));
    i.route = std::make_unique<DesignBodyRoute>(*i.body, opt.owner.fields.window, opt.bodyMessage);
    // Recovery's owner proof (DOCS-336): Windows Hello or the account password.
    if (opt.ownerAuthentication) service.state().setOwnerAuthentication(opt.ownerAuthentication);
    else {
        need(opt.authenticationMessage != 0, "Archive needs a message for its owner authentication");
        i.authentication = std::make_unique<gpu::NativeArchiveOwnerAuthentication>(opt.owner.fields.window, opt.authenticationMessage);
        service.state().setOwnerAuthentication(i.authentication->authenticator());
    }
    i.core = std::make_unique<DesignOwnerCore>(r, context, opt.owner);
    m::ArchiveCanvasServices services;
    services.newID = opt.newID; services.foundationNow = opt.foundationNow; services.dotted = opt.dotted;
    services.fonts = opt.fonts;
    services.media = i.media.get();
    Impl* self = &i;
    services.posters = [self](const m::ArchiveJson& reference, std::function<void(std::optional<d::ImageSource>)> done) { self->media->decodePoster(reference, std::move(done)); };
    services.releasePoster = [self](const d::ImageSource& image) { self->media->releasePoster(image); };
    services.shelfChoices = opt.shelfChoices;
    services.requestMedia = opt.requestMedia;
    services.cancelMedia = [self] { (void)self->media->cancelImport(); if (self->options.cancelMedia) self->options.cancelMedia(); };
    auto canvas = std::make_unique<m::ArchiveCanvas>(context, service.state(), std::move(services));
    i.canvas = canvas.get();
    i.canvas->setBodyHost(i.body.get());
    i.core->add(std::move(canvas));
    i.media->onImported = [self](const m::ArchiveMediaRequest& request, std::vector<m::ArchiveJson> references, std::optional<std::string> error) {
        self->enter(self->time);
        (void)self->canvas->receiveMedia(request, std::move(references), std::move(error));
    };
}
ArchiveOwner::~ArchiveOwner() {
    auto& i = *impl_;
    i.media->onImported = {};
    i.service.state().setOwnerAuthentication({});
}

void ArchiveOwner::stateChanged(double t) { auto& i = *impl_; i.enter(t); i.canvas->stateChanged(); }
bool ArchiveOwner::importFiles(const m::ArchiveMediaRequest& request, std::span<const ArchiveMediaImportFile> files, double t) {
    auto& i = *impl_;
    i.enter(t);
    if (!i.media->beginImport(request, files, i.time)) { (void)i.canvas->receiveMedia(request, {}, std::nullopt); return false; }
    return true;
}
void ArchiveOwner::importFailed(const m::ArchiveMediaRequest& request, std::optional<std::string> error, double t) {
    auto& i = *impl_;
    i.enter(t);
    (void)i.canvas->receiveMedia(request, {}, std::move(error));
}
std::optional<m::ArchiveMediaRequest> ArchiveOwner::dropRequest(double t) {
    auto& i = *impl_;
    i.enter(t);
    if (!i.accepting()) return std::nullopt;
    return i.canvas->dropRequest();
}
bool ArchiveOwner::refreshMedia(double t) { auto& i = *impl_; i.enter(t); return i.media->refresh(i.time); }
m::ArchiveCanvas& ArchiveOwner::canvas() noexcept { return *impl_->canvas; }
const m::ArchiveState& ArchiveOwner::state() const noexcept { return impl_->service.state(); }
DesignOwnerCore& ArchiveOwner::core() noexcept { return *impl_->core; }
ArchiveMediaBinding& ArchiveOwner::media() noexcept { return *impl_->media; }
gpu::NativeArchiveBodyEditor& ArchiveOwner::body() noexcept { return *impl_->body; }

std::string_view ArchiveOwner::name() const noexcept { return impl_->core->options().name; }
app::ModuleRouting ArchiveOwner::routing() const noexcept { return impl_->core->options().routing; }
bool ArchiveOwner::presents(core::Module module) const noexcept { return module == core::Module::archive; }
void ArchiveOwner::resize(const app::ClientMetrics& metrics) { impl_->metrics = metrics; impl_->core->resize(metrics); }
void ArchiveOwner::setAppearance(const app::ModuleAppearance& a, double t) { auto& i = *impl_; i.clock(t); i.core->setAppearance(a, t); }
void ArchiveOwner::setOverlayVisible(bool value, double t) {
    auto& i = *impl_;
    i.clock(t);
    if (value) i.releasePending = false;
    i.overlay = value;
    if (!value) i.route->cancel();
    i.core->setOverlayVisible(value, t);
    if (!value) i.media->setVisible(false, i.time);
}
void ArchiveOwner::overlayClosing(double t) { cancelInteraction(t); setOverlayVisible(false, t); }
void ArchiveOwner::update(const app::ModuleFrame& frame) {
    auto& i = *impl_;
    i.clock(frame.time);
    i.focused = frame.focused;
    i.core->update(frame);
    // NotesMediaPresentation lives only while the editor page is active
    // (didDeactivate clears it; DOCS-437): decoding and playback follow.
    i.media->setVisible(i.overlay && i.canvas->isActive() && i.core->pose(core::Module::archive).has_value(), i.time);
    i.compose();
}
void ArchiveOwner::prewarm(const app::ModuleFrame& frame) { auto& i = *impl_; i.clock(frame.time); i.core->prewarm(frame); }
bool ArchiveOwner::requiresFrames(double t) const { const auto& i = *impl_; return i.core->requiresFrames(t) || i.body->requiresFrames(std::max(t, i.time)); }
std::optional<double> ArchiveOwner::nextWakeTime(double t) const {
    const auto& i = *impl_;
    // The .35 s save debounce runs whatever the HUD shows (v1.2.0's deadline).
    return earlier(earlier(i.core->nextWakeTime(t), i.overlay ? i.body->nextWakeTime(std::max(t, i.time)) : std::nullopt), i.service.state().saveDeadline());
}
bool ArchiveOwner::deadline(double t) {
    auto& i = *impl_;
    i.clock(t);
    const bool woke = i.core->deadline(t);
    (void)i.service.state().flushIfDue(i.time);
    return woke;
}
void ArchiveOwner::upload(gpu::Renderer& r) { impl_->core->upload(r); }
std::span<const gpu::LayerCompositionEntry> ArchiveOwner::entries(core::Module module) { return impl_->core->entries(module); }
void ArchiveOwner::collected(gpu::Renderer& r) {
    auto& i = *impl_;
    i.core->collected(r);
    (void)i.body->collectRetired(r);
    (void)i.media->collectRetired();
    // The HUD finished closing: the media go once no publication names them.
    if (i.releasePending && i.media->releaseResources(i.time)) i.releasePending = false;
}
void ArchiveOwner::release(gpu::Renderer& r) {
    auto& i = *impl_;
    i.core->setInsertion(core::Module::archive, nullptr, std::nullopt);
    i.inserted.reset();
    i.core->release(r);
    need(i.body->releaseResources(r) && i.carrier.releaseResources(r), "Archive body artwork still published at teardown");
    (void)i.media->releaseResources(i.time);
}
bool ArchiveOwner::covers(core::Point p) const { return impl_->core->covers(p); }
bool ArchiveOwner::pointerLocked() const { return impl_->route->captured() || impl_->core->pointerLocked(); }
bool ArchiveOwner::capturesPointer() const { return impl_->route->captured() || impl_->core->capturesPointer(); }
bool ArchiveOwner::capturesWheel() const { return impl_->core->capturesWheel(); }
bool ArchiveOwner::capturesKeys() const { const auto& i = *impl_; return i.core->capturesKeys() || (i.accepting() && i.bodyFocused()); }
bool ArchiveOwner::suppressesKeyFilters() const { return impl_->core->suppressesKeyFilters(); }
// The body's text view lies above the canvas (textView(atHost:) :1171-1176):
// a press in its text goes to the editor unless a menu, a pop-over or the
// confirm bar takes the pointer; the title gives up its focus first.
bool ArchiveOwner::pointer(const app::PointerEvent& e, double t) {
    auto& i = *impl_;
    i.enter(t);
    if (i.route->pointer(e, i.metrics.scale, i.free(), [&i] { if (auto* field = i.canvas->editingField()) field->endEditing(true); })) return true;
    return i.core->pointer(e, t);
}
bool ArchiveOwner::wheel(const app::WheelEvent& e, double t) { return impl_->core->wheel(e, t); }
bool ArchiveOwner::key(const app::KeyEvent& e, double t) {
    auto& i = *impl_;
    i.enter(t);
    if (i.route->key(e, i.keysToBody())) return true;
    return i.core->key(e, t);
}
bool ArchiveOwner::filterKey(const app::NativeMessage& m) {
    auto& i = *impl_;
    if (i.route->filterKey(m, i.keysToBody())) return true;
    return i.core->filterKey(m);
}
bool ArchiveOwner::message(const app::NativeMessage& m, double t) {
    auto& i = *impl_;
    i.enter(t);
    if (i.route->message(m)) return true;
    if (i.authentication && m.window == i.options.owner.fields.window && i.authentication->message(m.message, m.wParam, m.lParam)) return true;
    return i.core->message(m, t);
}
void ArchiveOwner::focus(bool value, double t) { auto& i = *impl_; i.clock(t); i.focused = value; i.core->focus(value, t); i.body->setFocused(value); }
void ArchiveOwner::cancelInteraction(double t) {
    auto& i = *impl_;
    i.clock(t);
    i.route->cancel();
    i.core->cancelInteraction(t);
}
bool ArchiveOwner::finishEditing(double t) { return impl_->core->finishEditing(t); }
bool ArchiveOwner::flush(double t) { auto& i = *impl_; (void)i.core->finishEditing(t); return i.service.flush(); }
bool ArchiveOwner::hostEvent(const app::HostEvent& e, double t) {
    auto& i = *impl_;
    i.clock(t);
    bool changed = i.core->hostEvent(e, t);
    // The HUD closed: the Mac's canvas (and its session) ends with it.
    if (e.kind == app::HostEvent::Kind::overlayConcealed) {
        i.enter(t);
        i.canvas->endSession();
        i.media->setVisible(false, i.time);
        i.releasePending = true;
        changed = true;
    }
    return changed;
}
std::optional<app::ModuleCursor> ArchiveOwner::cursorAt(core::Point p) const {
    const auto& i = *impl_;
    if (i.route->textCursorAt(p, i.accepting() && !i.canvas->capturesPointer())) return app::ModuleCursor::text;
    return i.core->cursorAt(p);
}
bool ArchiveOwner::automationRefresh(IRawElementProviderFragmentRoot* root) { return impl_->core->automationRefresh(root); }
std::size_t ArchiveOwner::automationCount() const { return impl_->core->automationCount(); }
long ArchiveOwner::automationChild(std::size_t n, IRawElementProviderFragment** out) const { return impl_->core->automationChild(n, out); }
long ArchiveOwner::automationFromPoint(double x, double y, IRawElementProviderFragment** out) const { return impl_->core->automationFromPoint(x, y, out); }
void ArchiveOwner::automationDisconnect() noexcept { impl_->core->automationDisconnect(); }
std::optional<core::Module> ArchiveOwner::automationModule() const noexcept { return impl_->core->automationModule(); }
} // namespace endfield::tools
#endif
