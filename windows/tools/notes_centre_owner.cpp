#include "tools/notes_centre_owner.hpp"
#ifdef _WIN32
#include "tools/design_body_route.hpp"
#include "tools/notes_preview.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace endfield::tools {
namespace {
namespace d = modules::design;
namespace m = modules;
namespace gpu = native;
void need(bool v, const char* s) { if (!v) throw std::invalid_argument(s); }
std::optional<double> earlier(std::optional<double> a, std::optional<double> b) { return a && b ? std::min(*a, *b) : a ? a : b; }
}

struct NotesCentreOwner::Impl {
    d::Context& context;
    NotesPreview& desktop;
    NotesCentreOwnerOptions options;
    std::unique_ptr<ehud::data::NotesCentreStore> store;
    std::unique_ptr<gpu::NativeArchiveBodyEditor> body;
    std::unique_ptr<DesignBodyRoute> route;
    std::unique_ptr<DesignOwnerCore> core;
    m::NotesCanvas* canvas{};
    struct Insertion { const d::Layer* after{}; gpu::LayerScene* scene{}; bool operator==(const Insertion&) const = default; };
    std::optional<Insertion> inserted;
    app::ClientMetrics metrics;
    bool overlay{true}, focused{true};
    double time{};
    Impl(d::Context& c, NotesPreview& n, NotesCentreOwnerOptions o) : context(c), desktop(n), options(std::move(o)) {}
    void clock(double t) { need(std::isfinite(t), "Notes centre owner needs finite time"); time = std::max(time, t); }
    // Direct canvas calls run at the owner's time with this core's editor host.
    void enter(double t) { clock(t); auto& env = context.env(); env.now = std::max(env.now, time); core->bindEditorHost(); }
    bool accepting() const { return overlay && core->inputModule() == core::Module::notes; }
    bool free() const { return accepting() && !canvas->capturesPointer() && !context.overlay().capturesInput() && !canvas->transitioning(); }
    bool noteFocused() const { const auto* a = canvas->noteArea(); return a && a->isFocused(); }
    bool keysFree() const { return accepting() && noteFocused() && !context.overlay().capturesInput() && !canvas->capturesPointer(); }
    // The note editor's pose and its insertion after the note's part.
    void compose() {
        const auto pose = core->pose(core::Module::notes);
        std::optional<gpu::LayerCompositionEntry> entry;
        Insertion wanted;
        if (pose && metrics.pixelWidth && metrics.pixelHeight) {
            const auto& palette = context.env().palette;
            route->pose(*pose, metrics, canvas->root(), focused, context.env().reduceMotion, time, palette.tone(0xEEEEEE), palette.accentInk(), d::withAlpha(palette.yellow(), .35));
            const auto glyphs = body->entry();
            if (glyphs && canvas->noteArea()) { wanted = {&canvas->noteArea()->layer(), glyphs->scene}; entry = *glyphs; }
        }
        const auto next = entry ? std::optional(wanted) : std::nullopt;
        if (next == inserted) return;
        inserted = next;
        core->setInsertion(core::Module::notes, next ? next->after : nullptr, entry);
    }
};

app::ModuleRouting NotesCentreOwner::routing0() noexcept {
    app::ModuleRouting r;
    r.pointerCapture = 6; r.pointer = 3; r.wheel = 3; r.key = 3; r.filterKey = 3; r.message = 2;
    r.messageRetriesPendingModule = r.messageRetriesPendingClose = true; r.finishEditing = 1; r.flush = 1;
    return r;
}

NotesCentreOwner::NotesCentreOwner(gpu::LayerRasterizer& r, d::Context& context, NotesPreview& desktop, NotesCentreOwnerOptions o)
    : impl_(std::make_unique<Impl>(context, desktop, std::move(o))) {
    auto& i = *impl_;
    auto& opt = i.options;
    need(opt.owner.fields.window && opt.bodyMessage, "The Notes centre needs the HUD window and its note message");
    m::NotesCanvasOptions canvasOptions;
    try { i.store = std::make_unique<ehud::data::NotesCentreStore>(opt.dataRoot, opt.unixNow); }
    catch (const std::exception& e) { canvasOptions.storageError = e.what(); i.store = std::make_unique<ehud::data::NotesCentreStore>(std::nullopt, opt.unixNow); }
    // Mac NotesCanvas(store: nil): no data root, or a file that cannot be read.
    canvasOptions.storageAvailable = opt.dataRoot && !canvasOptions.storageError && i.store->loadIssue() != ehud::data::NotesCentreStore::LoadIssue::unavailable;
    gpu::NativeArchiveBodyEditorOptions body;
    body.window = opt.owner.fields.window; body.threadManager = opt.owner.fields.threadManager; body.clientID = opt.owner.fields.clientID;
    body.ownerMessage = opt.bodyMessage; body.raster = opt.owner.raster;
    body.clipboard = opt.clipboard ? opt.clipboard : gpu::systemNotesClipboard();
    body.editMenu = opt.editMenu;
    i.body = std::make_unique<gpu::NativeArchiveBodyEditor>(r, std::move(body));
    i.route = std::make_unique<DesignBodyRoute>(*i.body, opt.owner.fields.window, opt.bodyMessage);
    i.core = std::make_unique<DesignOwnerCore>(r, context, opt.owner);
    Impl* self = &i;
    canvasOptions.onTool = [self](std::string_view action) { (void)self->desktop.tool(action, self->time); };
    canvasOptions.onChooseFiles = [self](d::Point p) { self->desktop.chooseFiles({p.x, p.y}); };
    canvasOptions.onChooseShelf = [self](d::Point p) { self->desktop.chooseShelf({p.x, p.y}); };
    canvasOptions.chooseFileTitle = opt.chooseFileTitle;
    canvasOptions.families = m::notesFormat::families(opt.fonts.bundledCJKFamilies, opt.fonts.installedFamilies ? opt.fonts.installedFamilies() : std::vector<std::string>{});
    canvasOptions.fonts = opt.fonts;
    canvasOptions.noteEditor = i.body.get();
    auto canvas = std::make_unique<m::NotesCanvas>(context, *i.store, std::move(canvasOptions));
    i.canvas = canvas.get();
    i.core->add(std::move(canvas));
    desktop.setCentre(i.canvas);
}
NotesCentreOwner::~NotesCentreOwner() { impl_->desktop.setCentre(nullptr); }

m::NotesCanvas& NotesCentreOwner::canvas() noexcept { return *impl_->canvas; }
const ehud::data::NotesCentreStore& NotesCentreOwner::store() const noexcept { return *impl_->store; }
DesignOwnerCore& NotesCentreOwner::core() noexcept { return *impl_->core; }
gpu::NativeArchiveBodyEditor& NotesCentreOwner::body() noexcept { return *impl_->body; }

std::string_view NotesCentreOwner::name() const noexcept { return impl_->core->options().name; }
app::ModuleRouting NotesCentreOwner::routing() const noexcept { return impl_->core->options().routing; }
bool NotesCentreOwner::presents(core::Module module) const noexcept { return module == core::Module::notes; }
void NotesCentreOwner::resize(const app::ClientMetrics& metrics) { impl_->metrics = metrics; impl_->core->resize(metrics); }
void NotesCentreOwner::setAppearance(const app::ModuleAppearance& a, double t) { auto& i = *impl_; i.clock(t); i.core->setAppearance(a, t); }
void NotesCentreOwner::setOverlayVisible(bool value, double t) {
    auto& i = *impl_;
    i.clock(t);
    i.overlay = value;
    if (!value) i.route->cancel();
    i.core->setOverlayVisible(value, t);
}
void NotesCentreOwner::overlayClosing(double t) { cancelInteraction(t); setOverlayVisible(false, t); }
void NotesCentreOwner::update(const app::ModuleFrame& frame) {
    auto& i = *impl_;
    i.clock(frame.time);
    i.focused = frame.focused;
    i.core->update(frame);
    i.compose();
}
void NotesCentreOwner::prewarm(const app::ModuleFrame& frame) { auto& i = *impl_; i.clock(frame.time); i.core->prewarm(frame); }
bool NotesCentreOwner::requiresFrames(double t) const { const auto& i = *impl_; return i.core->requiresFrames(t) || i.body->requiresFrames(std::max(t, i.time)); }
std::optional<double> NotesCentreOwner::nextWakeTime(double t) const {
    const auto& i = *impl_;
    return earlier(i.core->nextWakeTime(t), i.overlay ? i.body->nextWakeTime(std::max(t, i.time)) : std::nullopt);
}
bool NotesCentreOwner::deadline(double t) { auto& i = *impl_; i.clock(t); return i.core->deadline(t); }
void NotesCentreOwner::upload(gpu::Renderer& r) { impl_->core->upload(r); }
std::span<const gpu::LayerCompositionEntry> NotesCentreOwner::entries(core::Module module) { return impl_->core->entries(module); }
void NotesCentreOwner::collected(gpu::Renderer& r) { auto& i = *impl_; i.core->collected(r); (void)i.body->collectRetired(r); }
void NotesCentreOwner::release(gpu::Renderer& r) {
    auto& i = *impl_;
    i.core->setInsertion(core::Module::notes, nullptr, std::nullopt);
    i.inserted.reset();
    i.core->release(r);
    need(i.body->releaseResources(r), "Notes centre note artwork still published at teardown");
}
bool NotesCentreOwner::covers(core::Point p) const { return impl_->core->covers(p); }
bool NotesCentreOwner::pointerLocked() const { return impl_->route->captured() || impl_->core->pointerLocked(); }
bool NotesCentreOwner::capturesPointer() const { return impl_->route->captured() || impl_->core->capturesPointer(); }
bool NotesCentreOwner::capturesWheel() const { return impl_->core->capturesWheel(); }
bool NotesCentreOwner::capturesKeys() const { const auto& i = *impl_; return i.core->capturesKeys() || (i.accepting() && i.noteFocused()); }
bool NotesCentreOwner::suppressesKeyFilters() const { return impl_->core->suppressesKeyFilters(); }
// The note's text view lies above the canvas (NotesCanvas.swift:539): a press
// in its text goes to the editor unless a menu, a pop-over or the confirm bar
// takes the pointer; the canvas's other field ends its editing first.
bool NotesCentreOwner::pointer(const app::PointerEvent& e, double t) {
    auto& i = *impl_;
    i.enter(t);
    if (i.route->pointer(e, i.metrics.scale, i.free(), [&i] { if (auto* field = i.canvas->editingField()) field->endEditing(true); })) return true;
    return i.core->pointer(e, t);
}
bool NotesCentreOwner::wheel(const app::WheelEvent& e, double t) { return impl_->core->wheel(e, t); }
bool NotesCentreOwner::key(const app::KeyEvent& e, double t) {
    auto& i = *impl_;
    i.enter(t);
    if (i.route->key(e, i.keysFree())) return true;
    return i.core->key(e, t);
}
bool NotesCentreOwner::filterKey(const app::NativeMessage& m) {
    auto& i = *impl_;
    if (i.route->filterKey(m, i.keysFree())) return true;
    return i.core->filterKey(m);
}
bool NotesCentreOwner::message(const app::NativeMessage& m, double t) {
    auto& i = *impl_;
    i.clock(t);
    if (m.window == i.options.owner.fields.window && m.message == i.options.bodyMessage) { i.enter(t); return i.route->message(m); }
    return i.core->message(m, t);
}
void NotesCentreOwner::focus(bool value, double t) { auto& i = *impl_; i.clock(t); i.focused = value; i.core->focus(value, t); i.body->setFocused(value); }
void NotesCentreOwner::cancelInteraction(double t) { auto& i = *impl_; i.clock(t); i.route->cancel(); i.core->cancelInteraction(t); }
bool NotesCentreOwner::finishEditing(double t) { return impl_->core->finishEditing(t); }
// The quit / session-end drain: the note's draft is written (saveNote) with
// the field commit; centre edits are written as they happen.
bool NotesCentreOwner::flush(double t) { auto& i = *impl_; i.enter(t); (void)i.core->finishEditing(t); i.canvas->willLeave(); return true; }
bool NotesCentreOwner::hostEvent(const app::HostEvent& e, double t) { auto& i = *impl_; i.clock(t); return i.core->hostEvent(e, t); }
std::optional<app::ModuleCursor> NotesCentreOwner::cursorAt(core::Point p) const {
    const auto& i = *impl_;
    if (i.route->textCursorAt(p, i.accepting() && !i.canvas->capturesPointer())) return app::ModuleCursor::text;
    return i.core->cursorAt(p);
}
bool NotesCentreOwner::automationRefresh(IRawElementProviderFragmentRoot* root) { return impl_->core->automationRefresh(root); }
std::size_t NotesCentreOwner::automationCount() const { return impl_->core->automationCount(); }
long NotesCentreOwner::automationChild(std::size_t n, IRawElementProviderFragment** out) const { return impl_->core->automationChild(n, out); }
long NotesCentreOwner::automationFromPoint(double x, double y, IRawElementProviderFragment** out) const { return impl_->core->automationFromPoint(x, y, out); }
void NotesCentreOwner::automationDisconnect() noexcept { impl_->core->automationDisconnect(); }
std::optional<core::Module> NotesCentreOwner::automationModule() const noexcept { return impl_->core->automationModule(); }
} // namespace endfield::tools
#endif
