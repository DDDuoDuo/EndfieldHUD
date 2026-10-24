#include "tools/reader_canvas_owner.hpp"
#ifdef _WIN32
#include "modules/reader_localization.hpp"
#include <cmath>
#include <filesystem>
#include <set>

namespace endfield::tools {
namespace {
namespace d = modules::design;
namespace gpu = native;
void need(bool v, const char* s) { if (!v) throw std::invalid_argument(s); }
std::filesystem::path utf8Path(std::string_view s) { return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(s.data()), s.size())); }
std::string fileName(std::string_view path) {
    const auto slash = path.find_last_of("/\\");
    return std::string(slash == std::string_view::npos ? path : path.substr(slash + 1));
}
}

struct ReaderCanvasOwner::Impl {
    gpu::ReaderOwner& owner;
    app::UtilityExecutor& executor;
    app::UtilityExecutor::Route route;
    ReaderCanvasOwnerOptions options;
    std::unique_ptr<DesignOwnerCore> core;
    modules::ReaderCanvas* canvas{};
    gpu::NativeReaderPages pages;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
    std::optional<ReaderImportAction> action;
    std::uint64_t generation{}, relinkGeneration{}, shelfGeneration{};
    bool importPending{};
    std::optional<std::string> relinkBook;
    std::uint64_t seenRevision{};
    const d::Layer* insertedAfter{};
    bool insertion{};
    double time{};
    Impl(gpu::LayerRasterizer& r, gpu::ReaderOwner& o, app::UtilityExecutor& e, ReaderCanvasOwnerOptions opts)
        : owner(o), executor(e), route(e.makeRoute()), options(std::move(opts)), pages(r) {}
    modules::ReaderState& state() { return owner.state(); }
    void clock(double t) { need(std::isfinite(t), "Reader owner needs finite time"); time = std::max(time, t); }
    // Canvas code outside a host entry (state changes, completions) reads the context clock.
    void now() { auto& env = canvas->context().env(); env.now = std::max(env.now, time); }
    void queue(ReaderImportAction::Kind kind, std::string shelf, std::string relink, std::string caption) {
        action = ReaderImportAction{kind, std::move(shelf), ++generation, relink, std::move(caption)};
        importPending = true;
        relinkBook = relink.empty() ? std::nullopt : std::optional(relink);
        relinkGeneration = relink.empty() ? 0 : generation;
        shelfGeneration = kind == ReaderImportAction::Kind::useShelf ? generation : 0;
        if (options.actionQueued) options.actionQueued();
    }
    void invalidate() { ++generation; importPending = false; action.reset(); relinkBook.reset(); }
    void syncState() {
        const auto revision = state().revision();
        if (revision == seenRevision) return;
        seenRevision = revision;
        now();
        canvas->stateChanged();
    }
    // The paper's part carries the page draws right after it (below the ribbon).
    void place() {
        const auto* clip = canvas->pageClip();
        const d::Layer* paper = clip ? clip->parent() : nullptr;
        const auto pose = core->pose(core::Module::reader);
        if (paper && pose) {
            pages.setViews(canvas->pageViews());
            pages.updatePose(*pose, canvas->root(), clip);
            if (!insertion || insertedAfter != paper) { core->setInsertion(core::Module::reader, paper, pages.entry()); insertion = true; insertedAfter = paper; }
        } else if (insertion) {
            core->setInsertion(core::Module::reader, nullptr, std::nullopt);
            insertion = false; insertedAfter = nullptr;
        }
    }
};

ReaderCanvasOwner::ReaderCanvasOwner(gpu::LayerRasterizer& r, d::Context& context, gpu::ReaderOwner& owner, app::UtilityExecutor& executor, ReaderCanvasOwnerOptions options)
    : impl_(std::make_unique<Impl>(r, owner, executor, std::move(options))) {
    auto& i = *impl_;
    i.core = std::make_unique<DesignOwnerCore>(r, context, i.options.design);
    modules::ReaderCanvasServices services;
    services.shelfChoices = i.options.shelfChoices;
    services.fontFamilies = i.options.fontFamilies;
    services.newID = i.options.newID;
    services.presentPanel = [&i](std::optional<std::string> title, std::string relink) {
        const auto caption = title ? *title : core::localized("Choose file", "选择文件", i.canvas->env().language);
        i.queue(ReaderImportAction::Kind::chooseLocal, {}, std::move(relink), caption);
        return true;
    };
    services.closePanel = [&i] { i.invalidate(); };
    services.openShelf = [&i](std::string id) { i.queue(ReaderImportAction::Kind::useShelf, std::move(id), {}, {}); };
    // ReaderCanvas.isReachable on the shared utility executor: a Windows
    // reference whose file exists; an imported Mac locator never opens here
    // (it needs Relink...).
    services.checkMissing = [&i](std::vector<modules::ReaderBook> books, std::function<void(std::set<std::string>)> done) {
        auto result = std::make_shared<std::set<std::string>>();
        std::weak_ptr<bool> alive = i.alive;
        (void)i.executor.submit(i.route, [books = std::move(books), result] {
            for (const auto& book : books) {
                std::error_code error;
                const bool reachable = book.platform == modules::ReaderReferencePlatform::windows && std::filesystem::is_regular_file(utf8Path(book.windowsPath), error);
                if (!reachable) result->insert(book.id);
            }
        }, [alive, result, done = std::move(done)](std::exception_ptr error) {
            const auto living = alive.lock();
            if (!living || !*living || error) return;
            done(std::move(*result));
        });
    };
    auto canvas = std::make_unique<modules::ReaderCanvas>(context, owner.state(), std::move(services));
    i.canvas = canvas.get();
    i.core->add(std::move(canvas));
    // SystemHUDView ReaderCanvas.onEvent: the Event Log records imported /
    // deleted / bookmarked / settings; page turns ("progress") no longer.
    owner.state().setEventObserver([&i](std::string_view action) {
        if (action != "progress" && i.options.event) i.options.event(action);
        i.now();
        i.canvas->handleEvent(action);
    });
    i.seenRevision = owner.state().revision();
}
ReaderCanvasOwner::~ReaderCanvasOwner() {
    auto& i = *impl_;
    *i.alive = false;
    i.executor.invalidate(i.route);
    i.owner.state().setEventObserver({});
}
modules::ReaderCanvas& ReaderCanvasOwner::canvas() noexcept { return *impl_->canvas; }
DesignOwnerCore& ReaderCanvasOwner::core() noexcept { return *impl_->core; }

// --- Import route ---------------------------------------------------------------
std::optional<ReaderImportAction> ReaderCanvasOwner::takeImportAction() { return std::exchange(impl_->action, std::nullopt); }
bool ReaderCanvasOwner::importRequestCurrent(std::uint64_t g) const noexcept { const auto& i = *impl_; return i.canvas->isActive() && i.importPending && g == i.generation; }
bool ReaderCanvasOwner::beginImport(std::uint64_t g, std::string_view path, double t) {
    auto& i = *impl_;
    i.clock(t);
    if (!importRequestCurrent(g)) return false;
    i.now();
    i.canvas->panelClosed();
    // importBook (:709-712): a chosen or dropped file is named by its file
    // name; a shelf entry by its title (the canvas named it); a relink not.
    if (i.relinkGeneration != g && i.shelfGeneration != g) i.canvas->importBegan(fileName(path), false);
    return true;
}
bool ReaderCanvasOwner::receiveImport(std::uint64_t g, modules::ReaderBook book, double t) {
    auto& i = *impl_;
    i.clock(t);
    if (!importRequestCurrent(g)) return false;
    i.now();
    i.importPending = false;
    const auto language = i.canvas->env().language;
    try {
        if (i.relinkBook && i.relinkGeneration == g) {
            const auto id = *std::exchange(i.relinkBook, std::nullopt);
            std::weak_ptr<bool> alive = i.alive;
            return i.state().relink(id, std::move(book.windowsPath), [&i, alive, id](bool success) {
                const auto living = alive.lock();
                if (!living || !*living) return;
                i.now();
                i.canvas->relinked(id, success);
            });
        }
        return i.state().addValidated(std::move(book));
    } catch (const modules::ReaderError& e) {
        i.canvas->importFailed(modules::readerErrorMessage(e.code(), language));
    }
    return false;
}
bool ReaderCanvasOwner::receiveImportError(std::uint64_t g, std::optional<std::string> error, double t) {
    auto& i = *impl_;
    i.clock(t);
    if (!importRequestCurrent(g)) return false;
    if (error) need(error->size() <= 4096 && ehud::data::Json::validUtf8(*error) && error->find('\0') == std::string::npos, "Invalid Reader import error");
    i.now();
    i.importPending = false;
    if (i.relinkGeneration == g) i.relinkBook.reset();
    i.canvas->panelClosed();
    i.canvas->importFailed(error.value_or(std::string{}));
    return true;
}
bool ReaderCanvasOwner::acceptsDrop(core::Point client) const {
    const auto& i = *impl_;
    if (!i.canvas->acceptsImport() || i.core->capturesPointer() || i.canvas->panelOpen() || i.canvas->context().overlay().capturesInput()) return false;
    const auto box = i.core->boxPoint(client);
    if (!box) return false;
    const auto bounds = i.canvas->hitBounds();
    return box->x >= bounds.x && box->y >= bounds.y && box->x < bounds.x + bounds.width && box->y < bounds.y + bounds.height;
}
std::optional<std::uint64_t> ReaderCanvasOwner::beginDrop(double t) {
    auto& i = *impl_;
    i.clock(t);
    if (!i.canvas->acceptsImport()) return std::nullopt;
    i.now();
    i.canvas->prepareImport();
    i.action.reset(); i.relinkBook.reset(); i.relinkGeneration = 0; i.shelfGeneration = 0;
    i.importPending = true;
    return ++i.generation;
}
void ReaderCanvasOwner::queueCapacityAvailable() { impl_->state().queueCapacityAvailable(); }
ReaderScrollSnapshot ReaderCanvasOwner::scrollSnapshot() const {
    const auto& i = *impl_;
    ReaderScrollSnapshot s;
    s.time = i.time; s.offset = i.canvas->scrollOffset(); s.zoom = i.canvas->imageView().zoom; s.panY = i.canvas->imageView().pan.y;
    s.stateRevision = i.owner.state().revision(); s.turnSequence = i.canvas->pageTurnSequence();
    s.providerBusy = i.owner.state().busy(); s.turning = i.canvas->turningPage();
    for (const auto& view : i.canvas->pageViews()) if (s.pageCount < s.pages.size()) s.pages[s.pageCount++] = view.layer->frame();
    return s;
}

// --- ModuleOwner ------------------------------------------------------------------------
app::ModuleRouting ReaderCanvasOwner::routing() const noexcept { return impl_->options.design.routing; }
void ReaderCanvasOwner::resize(const app::ClientMetrics& m) { impl_->core->resize(m); }
void ReaderCanvasOwner::setAppearance(const app::ModuleAppearance& a, double t) {
    auto& i = *impl_;
    i.clock(t);
    i.core->setAppearance(a, t);
    if (i.options.appearance) i.options.appearance(a, t);
}
void ReaderCanvasOwner::setOverlayVisible(bool v, double t) { impl_->clock(t); impl_->core->setOverlayVisible(v, t); }
void ReaderCanvasOwner::overlayClosing(double t) { impl_->clock(t); impl_->core->overlayClosing(t); }
void ReaderCanvasOwner::update(const app::ModuleFrame& f) {
    auto& i = *impl_;
    i.clock(f.time);
    i.syncState();
    i.core->update(f);
    i.place();
}
void ReaderCanvasOwner::prewarm(const app::ModuleFrame& f) { auto& i = *impl_; i.clock(f.time); i.syncState(); i.core->prewarm(f); }
bool ReaderCanvasOwner::requiresFrames(double t) const { const auto& i = *impl_; return i.core->requiresFrames(t) || (i.core->pose(core::Module::reader) && i.canvas->pagesAnimating(t)); }
std::optional<double> ReaderCanvasOwner::nextWakeTime(double t) const { return impl_->core->nextWakeTime(t); }
bool ReaderCanvasOwner::deadline(double t) { impl_->clock(t); return impl_->core->deadline(t); }
void ReaderCanvasOwner::upload(gpu::Renderer& r) {
    auto& i = *impl_;
    i.core->upload(r);
    if (i.insertion) i.pages.upload(r);
}
std::span<const gpu::LayerCompositionEntry> ReaderCanvasOwner::entries(core::Module m) { return impl_->core->entries(m); }
void ReaderCanvasOwner::collected(gpu::Renderer& r) { impl_->core->collected(r); impl_->pages.collected(r); }
void ReaderCanvasOwner::release(gpu::Renderer& r) {
    auto& i = *impl_;
    if (i.insertion) { i.core->setInsertion(core::Module::reader, nullptr, std::nullopt); i.insertion = false; i.insertedAfter = nullptr; }
    i.core->release(r);
    need(i.pages.release(r), "Reader page textures still published at teardown");
}
bool ReaderCanvasOwner::covers(core::Point p) const { return impl_->core->covers(p); }
bool ReaderCanvasOwner::pointerLocked() const { return impl_->core->pointerLocked(); }
bool ReaderCanvasOwner::capturesPointer() const { return impl_->core->capturesPointer(); }
bool ReaderCanvasOwner::capturesWheel() const { return impl_->core->capturesWheel(); }
bool ReaderCanvasOwner::capturesKeys() const { return impl_->core->capturesKeys(); }
bool ReaderCanvasOwner::pointer(const app::PointerEvent& e, double t) { auto& i = *impl_; i.clock(t); i.syncState(); return i.core->pointer(e, t); }
bool ReaderCanvasOwner::wheel(const app::WheelEvent& e, double t) {
    auto& i = *impl_;
    i.clock(t);
    i.syncState();
    // hasPreciseScrollingDeltas: a precision touchpad sends fractional detents.
    i.canvas->setWheelPrecise(std::isfinite(e.steps) && std::abs(e.steps - std::round(e.steps)) > 1e-6);
    const bool handled = i.core->wheel(e, t);
    if (handled && i.options.wheelObserved) i.options.wheelObserved(e, t);
    return handled;
}
bool ReaderCanvasOwner::key(const app::KeyEvent& e, double t) { auto& i = *impl_; i.clock(t); i.syncState(); return i.core->key(e, t); }
bool ReaderCanvasOwner::message(const app::NativeMessage& m, double t) { impl_->clock(t); return impl_->core->message(m, t); }
void ReaderCanvasOwner::focus(bool v, double t) { impl_->clock(t); impl_->core->focus(v, t); }
void ReaderCanvasOwner::cancelInteraction(double t) { impl_->clock(t); impl_->core->cancelInteraction(t); }
bool ReaderCanvasOwner::flush(double) { return impl_->owner.flush(); }
bool ReaderCanvasOwner::hostEvent(const app::HostEvent& e, double t) { impl_->clock(t); return impl_->core->hostEvent(e, t); }
std::optional<app::ModuleCursor> ReaderCanvasOwner::cursorAt(core::Point p) const { return impl_->core->cursorAt(p); }
bool ReaderCanvasOwner::automationRefresh(IRawElementProviderFragmentRoot* root) { return impl_->core->automationRefresh(root); }
std::size_t ReaderCanvasOwner::automationCount() const { return impl_->core->automationCount(); }
long ReaderCanvasOwner::automationChild(std::size_t n, IRawElementProviderFragment** out) const { return impl_->core->automationChild(n, out); }
long ReaderCanvasOwner::automationFromPoint(double x, double y, IRawElementProviderFragment** out) const { return impl_->core->automationFromPoint(x, y, out); }
void ReaderCanvasOwner::automationDisconnect() noexcept { impl_->core->automationDisconnect(); }
std::optional<core::Module> ReaderCanvasOwner::automationModule() const noexcept { return impl_->core->automationModule(); }
// Memory trace: the page pixels the canvas holds (shared with ReaderState);
// the document and its cache live on the shared executor only while shown.
app::ModuleResidency ReaderCanvasOwner::residency() const {
    const auto s = impl_->pages.stats();
    return {impl_->pages.pixelBytes(), s.residentBytes, 0};
}
} // namespace endfield::tools
#endif
