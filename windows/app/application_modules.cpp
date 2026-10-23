#include "app/application_modules.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "app/archive_service.hpp"
#include "modules/reader_localization.hpp"
#include "core/source_color.hpp"
#include "modules/module_titles.hpp"
#include "native/reader_owner.hpp"
#include "tools/archive_media_binding.hpp"
#include "tools/archive_options.hpp"
#include "tools/archive_preview.hpp"
#include "tools/map_preview.hpp"
#include "tools/notes_preview.hpp"
#include "tools/orbipom_preview.hpp"
#include "tools/reader_preview.hpp"
#include "tools/shelf_preview.hpp"
#include "app/areas/notes_automation_binding.hpp"
#include "tools/reader_automation.hpp"
#include "tools/map_automation.hpp"
#include "tools/orbipom_automation.hpp"
#include <array>
#include <cmath>
#include <concepts>

namespace endfield::app {
namespace source = core::source;
namespace gpu = native;

std::string moduleCaption(std::string_view target, core::Language language, std::string_view fallback) {
    return modules::moduleTitle(target, language).value_or(std::string(fallback)); // HUDModule.title
}
tools::NotesPreviewPreferences notesPreferences(core::Language language, bool dark, bool reduceMotion, std::array<double, 4> accent) {
    tools::NotesPreviewPreferences out;out.dark=dark;out.reduceMotion=reduceMotion;
    auto&w=out.workspace;w.palette=modules::NotesPalette::source(dark,accent);w.editor={{dark?.12:.96,dark?.12:.96,dark?.12:.96,1},accent};w.compositionColor=w.palette.primary;w.eraserColor=dark?std::array<double,4>{1,69./255.,58./255.,1}:std::array<double,4>{1,59./255.,48./255.,1};
    const auto t=[language](std::string_view en,std::string_view zh){return core::localized(en,zh,language);};auto&s=w.strings;
    s.textTitle=t("TEXT","文字");s.placeholder=t("Double-click to write…","双击输入…");s.pin=t("Pin note","固定便笺");s.unpin=t("Unpin note","取消固定");s.remove=t("Delete note","删除便笺");s.edit=t("Edit text","编辑文字");s.select=t("Text","文字");s.grow=t("Enlarge note","放大便笺");s.shrink=t("Reduce note size","缩小便笺");s.format={t("Font size","字号"),t("Font","字体"),t("Color","颜色"),t("Text style","特殊")};
    s.todoTitle=t("TODO","待办");s.itemPlaceholder=t("New item…","新事项…");s.addItem=t("+ Add item","+ 添加事项");s.checkItem=t("Check","勾选");s.uncheckItem=t("Uncheck","取消勾选");s.editItem=t("Edit item","编辑事项");s.moveUp=t("Move up","上移");s.moveDown=t("Move down","下移");s.removeItem=t("Delete item","删除事项");s.addItemAction=t("Add item","添加事项");s.imageTitle=t("IMAGE/VIDEO","图片/视频");s.drawingTitle=t("DRAWING","画画");s.drawingColor=t("Drawing color","画笔颜色");out.controls=tools::archivePreviewStrings(language).menus;return out;
}
modules::ReaderStrings readerStrings(core::Language language) {
    // Five-language Reader captions (modules/reader_localization.hpp): the
    // Windows "Choose in File Explorer" row and ReaderStrings.language, which
    // renders typed ReaderState errors (Mac ReaderError.errorDescription).
    return modules::readerStrings(language);
}
bool presentationShows(const core::ModulePresentationSample& sample, core::Module module) noexcept {
    return sample.current.module == module || (sample.incoming && sample.incoming->module == module);
}

namespace {
using Entries = std::span<const gpu::LayerCompositionEntry>;
bool hasPixels(const ClientMetrics& m) { return m.pixelWidth && m.pixelHeight; }

// ModuleOwner::cursorAt for built-in owners whose previews live in an area:
// the preview's open-editor hit test, used as soon as the preview declares
// `bool textCursorAt(core::Point logicalClient) const noexcept`.
template <class Preview> concept TextCursorPreview = requires(const Preview& preview, core::Point point) {
    { preview.textCursorAt(point) } -> std::convertible_to<bool>;
};
// ModuleOwner::prewarm for built-in owners whose previews live in an area:
// forwarded as soon as the preview declares
// `void prewarm(const core::Matrix4& center, const core::source::DesktopChromeSettings& chrome,
//               const core::ModulePresentationSample& presentation, double time)`
// (the first opening's frame while the HUD is still closed; see module_owner.hpp).
template <class Preview> concept PrewarmPreview = requires(Preview& preview, const ModuleFrame& frame) {
    preview.prewarm(frame.center, frame.chrome, frame.presentation, frame.time);
};
template <class Preview> void previewPrewarm(Preview& preview, const ModuleFrame& frame) {
    if constexpr (PrewarmPreview<Preview>) preview.prewarm(frame.center, frame.chrome, frame.presentation, frame.time);
    else { (void)preview; (void)frame; }
}
template <class Preview> std::optional<ModuleCursor> previewTextCursor(const Preview& preview, core::Point point) {
    if constexpr (TextCursorPreview<Preview>) return preview.textCursorAt(point) ? std::optional(ModuleCursor::text) : std::nullopt;
    else { (void)preview; (void)point; return std::nullopt; }
}
// The previews whose open editors show the I-beam; a changed signature
// would otherwise silently keep the source cursor over their fields.
static_assert(TextCursorPreview<tools::NotesPreview> && TextCursorPreview<tools::ArchivePreview>);

class NotesModule final : public ModuleOwner {
    tools::NotesPreview& n_;
    std::function<double()> clock_;
    std::function<void()> changed_;
    // HUDNotesInteraction accessibility views (native/notes_automation.hpp),
    // created on the first refresh for this owner's group.
    endfield::notes::NotesAutomationBinding automation_{[this](IRawElementProviderFragmentRoot* root) { return n_.automation(root, clock_, changed_); }};
public:
    NotesModule(tools::NotesPreview& n, std::function<double()> clock, std::function<void()> changed) : n_(n), clock_(std::move(clock)), changed_(std::move(changed)) {}
    ~NotesModule() override { automation_.disconnect(); }
    bool automationRefresh(IRawElementProviderFragmentRoot* root) override { return clock_ && automation_.refresh(root); }
    std::size_t automationCount() const override { return automation_.count(); }
    long automationChild(std::size_t i, IRawElementProviderFragment** out) const override { return automation_.child(i, out); }
    long automationFromPoint(double x, double y, IRawElementProviderFragment** out) const override { return automation_.fromPoint(x, y, out); }
    void automationDisconnect() noexcept override { automation_.disconnect(); }
    std::string_view name() const noexcept override { return "notes"; }
    bool essential() const noexcept override { return true; } // owns module selection and the shared TSF manager
    // Pinned cards show over every module; the group is still named Notes.
    std::optional<core::Module> automationModule() const noexcept override { return core::Module::notes; }
    ModuleRouting routing() const noexcept override {
        ModuleRouting r; r.pointer = 1; r.wheel = 1; r.key = 1; r.filterKey = 5; r.filterRequiresOwnerTarget = true;
        r.message = 4; r.messageRetriesPendingClose = true; r.finishEditing = 3; return r;
    }
    void resize(const ClientMetrics& m) override { n_.resize(m); }
    void setAppearance(const ModuleAppearance& a, double t) override { n_.applyPreferences(notesPreferences(a.language, a.dark, a.reduceMotion, a.accent), t); }
    void setOverlayVisible(bool v, double t) override { if (v) n_.setMediaActive(true, t); }
    void overlayClosing(double t) override { n_.setMediaActive(false, t, true); }
    void update(const ModuleFrame& f) override { n_.update(f.center, f.chrome, f.opacity, f.time, f.focused); }
    void prewarm(const ModuleFrame& f) override { previewPrewarm(n_, f); }
    bool requiresFrames(double t) const override { return n_.requiresFrames(t); }
    std::optional<double> nextWakeTime(double) const override { return n_.nextWakeTime(); }
    bool deadline(double t) override { return n_.deadline(t); }
    void upload(gpu::Renderer& r) override { n_.upload(r); }
    Entries floatingEntries() override { return n_.entries(); }
    void collected(gpu::Renderer& r) override { n_.collected(r); }
    void release(gpu::Renderer& r) override { n_.release(r); }
    bool covers(core::Point p) const override { return n_.covers(p); }
    bool pointerLocked() const override { return n_.pointerLocked(); }
    bool pointer(const PointerEvent& e, double t) override { return n_.pointer(e, t); }
    bool wheel(const WheelEvent& e, double t) override { return n_.wheel(e, t); }
    bool key(const KeyEvent& e, double t) override { return n_.key(e, t); }
    bool filterKey(const NativeMessage& m) override { return n_.filterKey(m); }
    bool message(const NativeMessage& m, double t) override { return n_.message(m, t); }
    void focus(bool v, double) override { n_.focus(v); }
    bool finishEditing(double) override { return n_.finish(); }
    // HUDNotesInteraction's projected editor (the preview's own hit test).
    std::optional<ModuleCursor> cursorAt(core::Point p) const override { return previewTextCursor(n_, p); }
};

class ShelfModule final : public ModuleOwner {
    tools::ShelfPreview& s_;
public:
    explicit ShelfModule(tools::ShelfPreview& s) : s_(s) {}
    std::string_view name() const noexcept override { return "fileShelf"; }
    ModuleRouting routing() const noexcept override { ModuleRouting r; r.pointer = 8; r.wheel = 4; r.key = 4; r.filterKey = 1; r.message = 5; return r; }
    bool presents(core::Module m) const noexcept override { return m == core::Module::fileShelf; }
    void resize(const ClientMetrics& m) override { s_.resize(m); }
    void setAppearance(const ModuleAppearance& a, double) override { s_.setLanguage(a.language); s_.setAppearance(a.dark, a.accent); }
    void overlayClosing(double) override { s_.cancelPanels(); s_.cancelInteraction(); }
    // HUD fully closed: a Shelf that is not shown releases its raster artwork;
    // the shown one settles its hover highlights and keeps no bitmap for those
    // that do not show (tools/shelf_preview.hpp conceal()).
    bool hostEvent(const HostEvent& e, double) override { if (e.kind == HostEvent::Kind::overlayConcealed) s_.conceal(); return false; }
    void update(const ModuleFrame& f) override { s_.update(f.center, f.chrome, f.presentation, f.opacity, f.time); }
    void prewarm(const ModuleFrame& f) override { previewPrewarm(s_, f); }
    bool requiresFrames(double t) const override { return s_.requiresFrames(t); }
    void upload(gpu::Renderer& r) override { s_.upload(r); }
    Entries entries(core::Module) override { return s_.entries(); }
    void collected(gpu::Renderer& r) override { s_.collected(r); }
    void release(gpu::Renderer& r) override { s_.release(r); }
    bool covers(core::Point p) const override { return s_.covers(p); }
    bool pointerLocked() const override { return s_.pointerLocked(); }
    bool pointer(const PointerEvent& e, double t) override { return s_.pointer(e, t); }
    bool wheel(const WheelEvent& e, double t) override { return s_.wheel(e, t); }
    bool key(const KeyEvent& e, double t) override { return s_.key(e, t); }
    bool filterKey(const NativeMessage& m) override { return s_.filterKey(m); }
    bool message(const NativeMessage& m, double t) override { return s_.message(m, t); }
    void focus(bool v, double) override { if (!v) s_.cancelInteraction(); }
    // HUDShelfActionButton peers (native/shelf_automation.hpp) owned by the Shelf.
    bool automationRefresh(IRawElementProviderFragmentRoot* root) override { return s_.automationRefresh(root); }
    std::size_t automationCount() const override { return s_.automationCount(); }
    long automationChild(std::size_t index, IRawElementProviderFragment** out) const override { return s_.automationChild(index, out); }
    long automationFromPoint(double x, double y, IRawElementProviderFragment** out) const override { return s_.automationFromPoint(x, y, out); }
    void automationDisconnect() noexcept override { s_.automationDisconnect(); }
};

bool modifierDown() {
#ifdef _WIN32
    return (GetKeyState(VK_CONTROL) & 0x8000) || (GetKeyState(VK_MENU) & 0x8000) || (GetKeyState(VK_SHIFT) & 0x8000);
#else
    return false;
#endif
}

class ReaderModule final : public ModuleOwner {
    tools::ReaderPreview& r_;
    gpu::ReaderOwner& owner_;
    gpu::LayerRasterizer& raster_;
    std::function<void(const WheelEvent&, double)> wheelObserved_;
    gpu::NativeScreenReaderPeers peers_; // ReaderAXButton peers (last: disconnects first)
public:
    ReaderModule(tools::ReaderPreview& r, gpu::ReaderOwner& o, gpu::LayerRasterizer& raster, std::function<void(const WheelEvent&, double)> observed, gpu::ScreenReaderPeerHooks peerHooks)
        : r_(r), owner_(o), raster_(raster), wheelObserved_(std::move(observed)), peers_(tools::readerAutomation(r, std::move(peerHooks))) {}
    bool automationRefresh(IRawElementProviderFragmentRoot* root) override { return peers_.refresh(root); }
    std::size_t automationCount() const override { return peers_.size(); }
    long automationChild(std::size_t i, IRawElementProviderFragment** out) const override { return peers_.child(i, out); }
    long automationFromPoint(double x, double y, IRawElementProviderFragment** out) const override { return peers_.fromPoint(x, y, out); }
    void automationDisconnect() noexcept override { peers_.disconnect(); }
    std::string_view name() const noexcept override { return "reader"; }
    ModuleRouting routing() const noexcept override { ModuleRouting r; r.pointerCapture = 4; r.pointer = 5; r.wheel = 10; r.key = 11; r.flush = 3; return r; }
    bool presents(core::Module m) const noexcept override { return m == core::Module::reader; }
    void resize(const ClientMetrics& m) override { if (hasPixels(m)) r_.resize(m); }
    void setAppearance(const ModuleAppearance& a, double) override {
        r_.setAppearance({a.dark, a.accent}); r_.setStrings(readerStrings(a.language)); r_.setReduceMotion(a.reduceMotion); owner_.setFonts(raster_.retainedFontResources());
    }
    void setOverlayVisible(bool v, double t) override { r_.setOverlayVisible(v, t); }
    void overlayClosing(double t) override { r_.cancelInteraction(t); r_.setOverlayVisible(false, t); }
    void update(const ModuleFrame& f) override { r_.update(f.center, f.chrome, f.presentation, f.opacity, f.time); }
    void prewarm(const ModuleFrame& f) override { previewPrewarm(r_, f); }
    bool requiresFrames(double t) const override { return r_.requiresFrames(t); }
    std::optional<double> nextWakeTime(double) const override { return r_.nextWakeTime(); }
    bool deadline(double t) override { return r_.deadline(t); }
    void upload(gpu::Renderer& r) override { r_.upload(r); }
    Entries entries(core::Module) override { return r_.entries(); }
    void collected(gpu::Renderer& r) override { r_.collected(r); }
    void release(gpu::Renderer& r) override { r_.release(r); }
    bool covers(core::Point p) const override { return r_.covers(p); }
    bool capturesPointer() const override { return r_.capturesPointer(); }
    bool pointer(const PointerEvent& e, double t) override { return r_.pointer(e, t); }
    bool wheel(const WheelEvent& e, double t) override {
        const bool handled = r_.wheel(e, t);
        if (handled && wheelObserved_) wheelObserved_(e, t);
        return handled;
    }
    bool key(const KeyEvent& e, double t) override { return r_.key(e, modifierDown(), t); }
    void focus(bool v, double t) override { if (!v) r_.cancelInteraction(t); }
    bool flush(double) override { return owner_.flush(); }
    // Memory trace: canvas page pixels and the open document's buffers.
    ModuleResidency residency() const override { const auto r = r_.residency(); return {r.cpuBytes, r.gpuBytes, r.threads}; }
};

class MapModule final : public ModuleOwner {
    tools::MapPreview& m_;
    gpu::NativeScreenReaderPeers peers_; // HUDWorldMapInteraction status and buttons (last: disconnects first)
public:
    MapModule(tools::MapPreview& m, gpu::ScreenReaderPeerHooks peerHooks) : m_(m), peers_(tools::mapAutomation(m, std::move(peerHooks))) {}
    bool automationRefresh(IRawElementProviderFragmentRoot* root) override { return peers_.refresh(root); }
    std::size_t automationCount() const override { return peers_.size(); }
    long automationChild(std::size_t i, IRawElementProviderFragment** out) const override { return peers_.child(i, out); }
    long automationFromPoint(double x, double y, IRawElementProviderFragment** out) const override { return peers_.fromPoint(x, y, out); }
    void automationDisconnect() noexcept override { peers_.disconnect(); }
    std::string_view name() const noexcept override { return "map"; }
    ModuleRouting routing() const noexcept override {
        ModuleRouting r; r.pointerCapture = 5; r.pointer = 6; r.wheel = 11; r.key = 12;
        r.pointerPolicy = {true, true, true}; r.wheelRefreshesPointerLock = r.deadlineRefreshesPointerLock = true; return r;
    }
    bool presents(core::Module m) const noexcept override { return m == core::Module::map; }
    void resize(const ClientMetrics& m) override { if (hasPixels(m)) m_.resize(m); }
    void setAppearance(const ModuleAppearance& a, double t) override {
        gpu::MapAppearance appearance; appearance.dark = a.dark; appearance.reducedMotion = a.reduceMotion; appearance.ambient = a.ambient && !a.lowPower; // the Mac stops the pin pulse in low-power mode
        appearance.accent = {a.accent[0], a.accent[1], a.accent[2], a.accent[3]};
        m_.setAppearance(std::move(appearance), t); m_.setLanguage(a.language, t);
    }
    void setOverlayVisible(bool v, double t) override { m_.setOverlayVisible(v, t); }
    void overlayClosing(double t) override { m_.cancelInteraction(t); m_.setOverlayVisible(false, t); }
    void update(const ModuleFrame& f) override { m_.update(f.center, f.chrome, f.presentation, f.opacity, f.time); }
    void prewarm(const ModuleFrame& f) override { previewPrewarm(m_, f); }
    bool requiresFrames(double t) const override { return m_.requiresFrames(t); }
    std::optional<double> nextWakeTime(double) const override { return m_.nextWakeTime(); }
    bool deadline(double t) override { return m_.deadline(t); }
    void upload(gpu::Renderer& r) override { m_.upload(r); }
    Entries entries(core::Module) override { return m_.entries(); }
    void release(gpu::Renderer& r) override { m_.release(r); }
    bool covers(core::Point p) const override { return m_.covers(p); }
    bool pointerLocked() const override { return m_.pointerLocked(); }
    bool capturesPointer() const override { return m_.state().dragging(); }
    bool pointer(const PointerEvent& e, double t) override { return m_.pointer(e, t); }
    bool wheel(const WheelEvent& e, double t) override { return m_.wheel(e, t); }
    bool key(const KeyEvent& e, double t) override { return m_.key(e, t); }
    void focus(bool v, double t) override { if (!v) m_.cancelInteraction(t); }
    // Memory trace: player images, feather, kept terrain detail/backdrop and geography.
    ModuleResidency residency() const override { const auto r = m_.residency(); return {r.cpuBytes, r.gpuBytes, r.threads}; }
};

class MinigameModule final : public ModuleOwner {
    tools::OrbiPomPreview& g_;
    gpu::NativeScreenReaderPeers peers_; // OrbiPomAXButton peers and the rules group (last: disconnects first)
public:
    MinigameModule(tools::OrbiPomPreview& g, gpu::ScreenReaderPeerHooks peerHooks) : g_(g), peers_(tools::orbiPomAutomation(g, std::move(peerHooks))) {}
    bool automationRefresh(IRawElementProviderFragmentRoot* root) override { return peers_.refresh(root); }
    std::size_t automationCount() const override { return peers_.size(); }
    long automationChild(std::size_t i, IRawElementProviderFragment** out) const override { return peers_.child(i, out); }
    long automationFromPoint(double x, double y, IRawElementProviderFragment** out) const override { return peers_.fromPoint(x, y, out); }
    void automationDisconnect() noexcept override { peers_.disconnect(); }
    std::string_view name() const noexcept override { return "minigame"; }
    ModuleRouting routing() const noexcept override {
        ModuleRouting r; r.pointerCapture = 2; r.pointer = 7; r.wheelCapture = 2; r.wheel = 12; r.key = 13;
        r.pointerPolicy = {false, false, true}; return r;
    }
    bool presents(core::Module m) const noexcept override { return m == core::Module::minigame; }
    void resize(const ClientMetrics& m) override { if (hasPixels(m)) g_.resize(m); }
    void setAppearance(const ModuleAppearance& a, double t) override {
        modules::OrbiPomAppearance appearance; appearance.dark = a.dark; appearance.reducedMotion = a.reduceMotion; appearance.language = a.language;
        appearance.accent = {a.accent[0], a.accent[1], a.accent[2], a.accent[3]}; g_.setAppearance(std::move(appearance), t);
        // Mac reconcileClock: a 1/30 s simulation tick in low-power visual mode.
        g_.setLowPowerVisualMode(a.lowPower, t);
    }
    // Mac OrbiPomCanvas willSleep/didWake (production force-conceal already pauses it).
    bool hostEvent(const HostEvent& e, double t) override {
        if (e.kind == HostEvent::Kind::suspended) g_.setSystemSuspended(true, t);
        else if (e.kind == HostEvent::Kind::resumed) g_.setSystemSuspended(false, t);
        return false;
    }
    void setOverlayVisible(bool v, double t) override { g_.setOverlayVisible(v, t); }
    void overlayClosing(double t) override { g_.cancelInteraction(t); g_.setOverlayVisible(false, t); }
    void update(const ModuleFrame& f) override { g_.update(f.center, f.chrome, f.presentation, f.opacity, f.time); }
    void prewarm(const ModuleFrame& f) override { previewPrewarm(g_, f); }
    bool requiresFrames(double t) const override { return g_.requiresFrames(t); }
    void upload(gpu::Renderer& r) override { g_.upload(r); }
    Entries entries(core::Module) override { return g_.entries(); }
    void collected(gpu::Renderer& r) override { g_.collected(r); }
    void release(gpu::Renderer& r) override { g_.release(r); }
    bool covers(core::Point p) const override { return g_.covers(p); }
    bool capturesPointer() const override { return g_.state().rulesPresented(); }
    bool capturesWheel() const override { return g_.state().rulesPresented(); }
    bool pointer(const PointerEvent& e, double t) override { return g_.pointer(e, t); }
    bool wheel(const WheelEvent& e, double t) override { return g_.wheel(e, t); }
    bool key(const KeyEvent& e, double t) override { return g_.key(e, t); }
    void focus(bool v, double t) override { g_.setForeground(v, t); if (!v) g_.cancelInteraction(t); }
    // Memory trace: the game VM heap and snapshot buffers (asks QuickJS for its heap size).
    ModuleResidency residency() const override { const auto r = g_.residency(); return {r.cpuBytes, r.gpuBytes, r.threads}; }
};

class ArchiveModule final : public ModuleOwner {
    tools::ArchivePreview& a_;
    ArchiveService* service_;
    tools::ArchiveMediaBinding* media_;
    std::function<double()> clock_;
    std::function<void()> changed_;
    bool posed_{}; // the last forwarded frame showed Archive
    // HUDArchiveInteraction.layoutAccessibility (native/notes_automation.hpp).
    endfield::notes::NotesAutomationBinding automation_{[this](IRawElementProviderFragmentRoot* root) { return a_.automation(root, clock_, changed_); }};
public:
    ArchiveModule(tools::ArchivePreview& a, ArchiveService* s, tools::ArchiveMediaBinding* m, std::function<double()> clock, std::function<void()> changed)
        : a_(a), service_(s), media_(m), clock_(std::move(clock)), changed_(std::move(changed)) {}
    ~ArchiveModule() override { automation_.disconnect(); }
    bool automationRefresh(IRawElementProviderFragmentRoot* root) override { return clock_ && automation_.refresh(root); }
    std::size_t automationCount() const override { return automation_.count(); }
    long automationChild(std::size_t i, IRawElementProviderFragment** out) const override { return automation_.child(i, out); }
    long automationFromPoint(double x, double y, IRawElementProviderFragment** out) const override { return automation_.fromPoint(x, y, out); }
    void automationDisconnect() noexcept override { automation_.disconnect(); }
    std::string_view name() const noexcept override { return "archive"; }
    ModuleRouting routing() const noexcept override {
        ModuleRouting r; r.pointerCapture = 6; r.pointer = 3; r.wheel = 3; r.key = 3; r.filterKey = 3; r.message = 2;
        r.messageRetriesPendingModule = r.messageRetriesPendingClose = true; r.finishEditing = 1; r.flush = 1; return r;
    }
    bool presents(core::Module m) const noexcept override { return m == core::Module::archive; }
    void resize(const ClientMetrics& m) override { if (hasPixels(m)) a_.resize(m); }
    void setAppearance(const ModuleAppearance& a, double t) override {
        a_.setAppearance({a.dark, 2, a.accent, modules::ArchiveColor{1, 159. / 255., 10. / 255., 1}});
        auto strings = tools::archivePreviewStrings(a.language);
        a_.setStrings(std::move(strings.view), std::move(strings.menus), std::move(strings.categoryNamePlaceholder), t);
        a_.setReduceMotion(a.reduceMotion, t);
    }
    void setOverlayVisible(bool v, double t) override { a_.setOverlayVisible(v, t); }
    void overlayClosing(double t) override { a_.cancelInteraction(t); a_.setOverlayVisible(false, t); }
    // ArchivePreview::update builds the retained view whenever the Archive's
    // content changed while active (13-17 ms and about 3.4 MB of rasters).
    // Frames reach it only while Archive is shown, plus the first frame after
    // (it takes its pose down), so an Archive that is never opened builds
    // nothing; the first showing builds it. Switched away inside the open HUD
    // it stands by (ArchivePreview::standby): the built view stays and the
    // switch back only restores its parked textures; the HUD closing
    // (setOverlayVisible(false)) releases it as before.
    void update(const ModuleFrame& f) override {
        const bool shown = presentationShows(f.presentation, core::Module::archive);
        if (!shown && !posed_) return;
        a_.update(f.center, f.chrome, f.presentation, f.opacity, f.time);
        posed_ = shown;
    }
    // Launch prewarm: the view is built while closed only when Archive is the
    // first opening's module (through the preview's own prewarm once it
    // declares one); otherwise nothing is built until Archive is opened.
    void prewarm(const ModuleFrame& f) override {
        if constexpr (PrewarmPreview<tools::ArchivePreview>) previewPrewarm(a_, f);
        else if (f.presentation.requested == core::Module::archive && presentationShows(f.presentation, core::Module::archive)) {
            a_.update(f.center, f.chrome, f.presentation, 0.f, f.time); posed_ = true;
        }
    }
    bool requiresFrames(double t) const override { return a_.requiresFrames(t); }
    std::optional<double> nextWakeTime(double) const override { return a_.nextWakeTime(); }
    bool deadline(double t) override { return a_.deadline(t); }
    void upload(gpu::Renderer& r) override { a_.upload(r); }
    Entries entries(core::Module) override { return a_.entries(); }
    void collected(gpu::Renderer& r) override { a_.collected(r); }
    // Archive fields borrow Notes' activated TSF manager; its media groups
    // detach before their textures and before either borrowed owner.
    void release(gpu::Renderer& r) override { a_.release(r); }
    bool covers(core::Point p) const override { return a_.covers(p); }
    bool pointerLocked() const override { return a_.pointerLocked(); }
    bool capturesPointer() const override { return a_.pointerLocked(); }
    bool pointer(const PointerEvent& e, double t) override { return a_.pointer(e, t); }
    bool wheel(const WheelEvent& e, double t) override { return a_.wheel(e, t); }
    bool key(const KeyEvent& e, double t) override { return a_.key(e, t); }
    bool filterKey(const NativeMessage& m) override { return a_.filterKey(m); }
    bool message(const NativeMessage& m, double t) override { return a_.message(m, t); }
    void focus(bool v, double t) override { a_.focus(v, t); }
    // A module switch commits the text and saves but keeps the focused field
    // (its formatting controls stay on the face, nothing is built again on
    // either side of the switch); marked text defers the switch.
    bool releasesSelection(double t) override { return a_.commitEditing(t); }
    bool finishEditing(double t) override { return a_.finishEditing(t); }
    bool flush(double t) override { return a_.finishEditing(t) && (!service_ || service_->flush()); }
    // HUDArchiveInteraction's projected title/date/body/category editors.
    std::optional<ModuleCursor> cursorAt(core::Point p) const override { return previewTextCursor(a_, p); }
};
} // namespace

std::unique_ptr<ModuleOwner> notesModule(tools::NotesPreview& o, std::function<double()> clock, std::function<void()> changed) { return std::make_unique<NotesModule>(o, std::move(clock), std::move(changed)); }
std::unique_ptr<ModuleOwner> shelfModule(tools::ShelfPreview& o) { return std::make_unique<ShelfModule>(o); }
std::unique_ptr<ModuleOwner> readerModule(tools::ReaderPreview& o, gpu::ReaderOwner& r, gpu::LayerRasterizer& raster, std::function<void(const WheelEvent&, double)> observed, gpu::ScreenReaderPeerHooks peers) {
    return std::make_unique<ReaderModule>(o, r, raster, std::move(observed), std::move(peers));
}
std::unique_ptr<ModuleOwner> mapModule(tools::MapPreview& o, gpu::ScreenReaderPeerHooks peers) { return std::make_unique<MapModule>(o, std::move(peers)); }
std::unique_ptr<ModuleOwner> minigameModule(tools::OrbiPomPreview& o, gpu::ScreenReaderPeerHooks peers) { return std::make_unique<MinigameModule>(o, std::move(peers)); }
std::unique_ptr<ModuleOwner> archiveModule(tools::ArchivePreview& o, ArchiveService* s, tools::ArchiveMediaBinding* m, std::function<double()> clock, std::function<void()> changed) {
    return std::make_unique<ArchiveModule>(o, s, m, std::move(clock), std::move(changed));
}
} // namespace endfield::app
