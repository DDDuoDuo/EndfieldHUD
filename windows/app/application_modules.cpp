#include "app/application_modules.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "app/archive_service.hpp"
#include "modules/reader_localization.hpp"
#include "core/source_color.hpp"
#include "native/clipboard_assets.hpp"
#include "native/reader_owner.hpp"
#include "native/volume_strings.hpp"
#include "tools/activity_preview.hpp"
#include "tools/archive_media_binding.hpp"
#include "tools/archive_options.hpp"
#include "tools/archive_preview.hpp"
#include "tools/battery_preview.hpp"
#include "tools/calendar_preview.hpp"
#include "tools/clipboard_preview.hpp"
#include "tools/event_log_preview.hpp"
#include "tools/map_preview.hpp"
#include "tools/notes_preview.hpp"
#include "tools/orbipom_preview.hpp"
#include "tools/reader_preview.hpp"
#include "tools/settings_preview.hpp"
#include "tools/shelf_preview.hpp"
#include "tools/storage_preview.hpp"
#include "tools/volume_preview.hpp"
#include "tools/work_mode_preview.hpp"
#include "native/volume_accessibility.hpp"
#include "native/volume_automation.hpp"
#include "native/work_mode_automation.hpp"
#include "app/areas/notes_automation_binding.hpp"
#include "tools/reader_automation.hpp"
#include "tools/map_automation.hpp"
#include "tools/calendar_automation.hpp"
#include "tools/orbipom_automation.hpp"
#include "tools/clipboard_automation.hpp"
#include "tools/event_log_automation.hpp"
#include "tools/activity_automation.hpp"
#include "tools/storage_automation.hpp"
#include <array>
#include <cmath>
#include <concepts>

namespace endfield::app {
namespace source = core::source;
namespace gpu = native;

std::string moduleCaption(std::string_view target, core::Language language, std::string_view fallback) {
    if(target=="notes")return core::localized("Notes","便笺",language);
    if(target=="fileShelf")return core::localized("Temporary File Shelf","文件暂存架",language);
    if(target=="clipboard")return core::localized("Clipboard Cache","剪贴板",language);
    if(target=="volume")return core::localized("Volume","音量",language);
    if(target=="account")return core::localized("Account Linking","账户绑定",language);
    if(target=="nowPlaying")return core::localized("Now Playing","当前播放",language);
    if(target=="projection")return core::localized("Projection","投影",language);
    if(target=="reader")return core::localized("E-Reader","阅读器",language);
    if(target=="archive")return core::localized("Archive","档案库",language);
    if(target=="mediaAssembly")return core::localized("Media Assembly","影像加工",language);
    if(target=="calendar")return core::localized("Calendar","日历",language);
    if(target=="minigame")return core::localized("Closure's Minigame","可露希尔的小游戏",language);
    if(target=="workMode")return core::localized("Work Mode","工作模式",language);
    if(target=="eventLog")return core::localized("Event Log","事件日志",language);
    if(target=="map")return core::localized("Map","地图",language);
    if(target=="addApp")return core::localized("+ Add App","+ 添加应用",language);
    if(target=="system")return core::localized("System","系统",language);
    if(target=="display")return core::localized("Display","显示",language);
    if(target=="hotkeys")return core::localized("Hotkeys","快捷键",language);
    if(target=="about")return core::localized("About","关于",language);
    if(target=="storage")return core::localized("Storage","存储",language);
    if(target=="activityMonitor")return core::localized("Activity Monitor","活动监视器",language);
    if(target=="power")return core::localized("Power","电源",language);
    if(target=="profile")return core::localized("Personal Profile","个人名片",language);
    return std::string(fallback);
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
modules::StorageAppearance storageAppearance(core::Language language, bool dark, std::array<double, 4> accent) {
    const auto available=source::sourceWhiteBlend({accent[0],accent[1],accent[2]},.42);
    return {dark,2,language,accent,std::array<double,4>{available[0],available[1],available[2],accent[3]}};
}
bool presentationShows(const core::ModulePresentationSample& sample, core::Module module) noexcept {
    return sample.current.module == module || (sample.incoming && sample.incoming->module == module);
}
native::VolumeStrings volumeStrings(core::Language language) {
    // The audio area's five-language catalog (native/volume_strings.hpp),
    // including the per-app routing captions; the language is already resolved.
    return native::volumeStrings(language);
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
// True when a logical client point lies inside the convex quad a canvas rect
// projects to: its four corners one by one through `project` (canvas rect ->
// logical client rect; a zero-size rect is that corner). Equivalent to the
// Mac's unprojected logicalRect.contains for a planar module plane; no
// allocation.
template <class Project> bool insideProjectedRect(core::Point point, core::Rect canvas, Project&& project) {
    if (!std::isfinite(point.x) || !std::isfinite(point.y) || !(canvas.width > 0) || !(canvas.height > 0)) return false;
    const std::array<core::Point, 4> corners{{{canvas.x, canvas.y}, {canvas.x + canvas.width, canvas.y},
        {canvas.x + canvas.width, canvas.y + canvas.height}, {canvas.x, canvas.y + canvas.height}}};
    std::array<core::Point, 4> quad{};
    for (std::size_t n = 0; n < corners.size(); ++n) {
        const std::optional<core::Rect> projected = project(core::Rect{corners[n].x, corners[n].y, 0, 0});
        if (!projected || !std::isfinite(projected->x) || !std::isfinite(projected->y)) return false;
        quad[n] = {projected->x, projected->y};
    }
    double sign = 0;
    for (std::size_t n = 0; n < quad.size(); ++n) {
        const auto& a = quad[n]; const auto& b = quad[(n + 1) % quad.size()];
        const double cross = (b.x - a.x) * (point.y - a.y) - (b.y - a.y) * (point.x - a.x);
        if (cross == 0) continue;
        if (sign == 0) sign = cross;
        else if ((sign > 0) != (cross > 0)) return false;
    }
    return sign != 0;
}

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

class ClipboardModule final : public ModuleOwner {
    tools::ClipboardPreview& c_;
    const gpu::NativeClipboardAssets& assets_;
    gpu::NativeScreenReaderPeers peers_; // Mac HUDClipboardInteraction peers (last: disconnects first)
public:
    ClipboardModule(tools::ClipboardPreview& c, const gpu::NativeClipboardAssets& a, gpu::ScreenReaderPeerHooks peerHooks)
        : c_(c), assets_(a), peers_(tools::clipboardAutomation(c, std::move(peerHooks))) {}
    bool automationRefresh(IRawElementProviderFragmentRoot* root) override { return peers_.refresh(root); }
    std::size_t automationCount() const override { return peers_.size(); }
    long automationChild(std::size_t i, IRawElementProviderFragment** out) const override { return peers_.child(i, out); }
    long automationFromPoint(double x, double y, IRawElementProviderFragment** out) const override { return peers_.fromPoint(x, y, out); }
    void automationDisconnect() noexcept override { peers_.disconnect(); }
    std::string_view name() const noexcept override { return "clipboard"; }
    ModuleRouting routing() const noexcept override { ModuleRouting r; r.pointer = 9; r.wheel = 5; r.key = 5; return r; }
    bool presents(core::Module m) const noexcept override { return m == core::Module::clipboard; }
    void resize(const ClientMetrics& m) override { c_.resize(m); }
    void setAppearance(const ModuleAppearance& a, double) override {
        c_.setLanguage(a.language); c_.setAppearance({a.dark, 2, a.accent}, assets_.images()); c_.setReduceMotion(a.reduceMotion);
    }
    // A close while Clipboard is not shown releases its artwork and row
    // thumbnails; the HUD's end of a close releases what is left
    // (tools/clipboard_preview.hpp setOverlayVisible / conceal).
    void setOverlayVisible(bool v, double t) override { c_.setOverlayVisible(v, t); }
    void overlayClosing(double t) override { c_.cancelInteraction(); c_.setOverlayVisible(false, t); }
    bool hostEvent(const HostEvent& e, double) override { if (e.kind == HostEvent::Kind::overlayConcealed) c_.conceal(); return false; }
    void update(const ModuleFrame& f) override { c_.update(f.center, f.chrome, f.presentation, f.opacity, f.time); }
    void prewarm(const ModuleFrame& f) override { previewPrewarm(c_, f); }
    bool requiresFrames(double t) const override { return c_.requiresFrames(t); }
    void upload(gpu::Renderer& r) override { c_.upload(r); }
    Entries entries(core::Module) override { return c_.entries(); }
    void collected(gpu::Renderer& r) override { c_.collected(r); }
    void release(gpu::Renderer& r) override { c_.release(r); }
    bool covers(core::Point p) const override { return c_.covers(p); }
    bool pointerLocked() const override { return c_.pointerLocked(); }
    bool pointer(const PointerEvent& e, double t) override { return c_.pointer(e, t); }
    bool wheel(const WheelEvent& e, double t) override { return c_.wheel(e, t); }
    bool key(const KeyEvent& e, double t) override { return c_.key(e, t); }
    void focus(bool v, double) override { if (!v) c_.cancelInteraction(); }
};

class VolumeModule final : public ModuleOwner {
    tools::VolumePreview& v_;
    std::optional<core::Language> language_;
    VolumeModuleHooks hooks_;
    // Created on the first UI Automation refresh (a client asked the HUD).
    std::unique_ptr<gpu::NativeVolumeAutomation> automation_;
    IRawElementProviderFragmentRoot* automationRoot_{};
    core::Language automationLanguage_{core::Language::english};
    // A UIA press changes the page outside the host's input routes.
    bool accepted(bool value) { if (value && hooks_.changed) hooks_.changed(); return value; }
public:
    VolumeModule(tools::VolumePreview& v, VolumeModuleHooks hooks) : v_(v), hooks_(std::move(hooks)) {}
    ~VolumeModule() override { automation_.reset(); }
    // HUDVolumeInteraction.layoutAccessibility: projected buttons and sliders.
    // A new group (another root) re-creates the fragments under it.
    bool automationRefresh(IRawElementProviderFragmentRoot* root) override {
        if (!hooks_.toScreen || !hooks_.clock) return false;
        bool rebuilt = false;
        if (automation_ && automationRoot_ != root) { rebuilt = automation_->size() > 0; automation_->disconnect(); automation_.reset(); }
        if (!automation_) {
            gpu::VolumeAutomationHost h;
            h.root = root;
            h.elements = [this] { return v_.acceptsInput() ? gpu::volumeAccessibility(v_.state(), gpu::volumeAccessibilityStrings(automationLanguage_)) : gpu::VolumeAccessibility{}; };
            h.toScreen = [this](core::Rect canvas) -> std::optional<core::Rect> { const auto client = v_.clientRect(canvas); return client ? hooks_.toScreen(*client) : std::nullopt; };
            h.invoke = [this](std::string_view id) { return accepted(v_.perform(id, hooks_.clock())); };
            h.setValue = [this](std::string_view id, double value) { return accepted(v_.setAccessibleValue(id, value, hooks_.clock())); };
            automation_ = std::make_unique<gpu::NativeVolumeAutomation>(std::move(h)); automationRoot_ = root;
        }
        return automation_->refresh() || rebuilt;
    }
    std::size_t automationCount() const override { return automation_ ? automation_->size() : 0; }
    long automationChild(std::size_t index, IRawElementProviderFragment** out) const override {
        if (!automation_) { if (out) *out = nullptr; return E_INVALIDARG; }
        return automation_->child(index, out);
    }
    long automationFromPoint(double x, double y, IRawElementProviderFragment** out) const override {
        if (!automation_) { if (out) *out = nullptr; return S_OK; }
        return automation_->fromPoint(x, y, out);
    }
    void automationDisconnect() noexcept override { if (automation_) automation_->disconnect(); }
    std::string_view name() const noexcept override { return "volume"; }
    ModuleRouting routing() const noexcept override { ModuleRouting r; r.pointer = 10; r.wheel = 6; r.key = 6; return r; }
    bool presents(core::Module m) const noexcept override { return m == core::Module::volume; }
    void resize(const ClientMetrics& m) override { v_.resize(m); }
    void setAppearance(const ModuleAppearance& a, double) override {
        v_.setStyle({a.dark, a.accent}); v_.setReduceMotion(a.reduceMotion);
        // Language events now reach Volume too (the former preview never set it).
        if (language_ != a.language) { v_.setStrings(volumeStrings(a.language)); language_ = a.language; }
        automationLanguage_ = a.language;
    }
    // VolumePreview builds its artwork when first shown (or by the launch
    // prewarm as the selected module) and releases it once the HUD closes on
    // another module; the module shown at the close keeps it.
    void prewarm(const ModuleFrame& f) override { v_.prewarm(f.presentation); }
    void setOverlayVisible(bool v, double t) override { v_.setOverlayVisible(v, t); }
    void overlayClosing(double t) override { v_.cancelInteraction(t); v_.setOverlayVisible(false, t); }
    bool hostEvent(const HostEvent& e, double) override { if (e.kind == HostEvent::Kind::overlayConcealed) v_.conceal(); return false; }
    void update(const ModuleFrame& f) override { v_.update(f.center, f.chrome, f.presentation, f.opacity, f.time); }
    bool requiresFrames(double t) const override { return v_.requiresFrames(t); }
    void upload(gpu::Renderer& r) override { v_.upload(r); }
    Entries entries(core::Module) override { return v_.entries(); }
    void collected(gpu::Renderer& r) override { v_.collected(r); }
    void release(gpu::Renderer& r) override { v_.release(r); }
    bool covers(core::Point p) const override { return v_.covers(p); }
    bool pointerLocked() const override { return v_.pointerLocked(); }
    bool pointer(const PointerEvent& e, double t) override { return v_.pointer(e, t); }
    bool wheel(const WheelEvent& e, double t) override { return v_.wheel(e, t); }
    bool key(const KeyEvent& e, double t) override { return v_.key(e, t); }
    void focus(bool v, double t) override { if (!v) v_.cancelInteraction(t); }
};

class EventLogModule final : public ModuleOwner {
    tools::EventLogPreview& e_;
    gpu::NativeScreenReaderPeers peers_; // Mac HUDEventLogInteraction peers (last: disconnects first)
public:
    EventLogModule(tools::EventLogPreview& e, gpu::ScreenReaderPeerHooks peerHooks) : e_(e), peers_(tools::eventLogAutomation(e, std::move(peerHooks))) {}
    bool automationRefresh(IRawElementProviderFragmentRoot* root) override { return peers_.refresh(root); }
    std::size_t automationCount() const override { return peers_.size(); }
    long automationChild(std::size_t i, IRawElementProviderFragment** out) const override { return peers_.child(i, out); }
    long automationFromPoint(double x, double y, IRawElementProviderFragment** out) const override { return peers_.fromPoint(x, y, out); }
    void automationDisconnect() noexcept override { peers_.disconnect(); }
    std::string_view name() const noexcept override { return "eventLog"; }
    ModuleRouting routing() const noexcept override { ModuleRouting r; r.pointer = 13; r.wheel = 7; r.key = 7; return r; }
    bool presents(core::Module m) const noexcept override { return m == core::Module::eventLog; }
    void resize(const ClientMetrics& m) override { e_.resize(m); }
    void setAppearance(const ModuleAppearance& a, double) override { e_.setLanguage(a.language); e_.setAppearance({a.dark, 2, a.accent}); e_.setReduceMotion(a.reduceMotion); }
    void setOverlayVisible(bool v, double t) override { e_.setOverlayVisible(v, t); }
    void overlayClosing(double t) override { e_.cancelInteraction(); e_.setOverlayVisible(false, t); }
    bool hostEvent(const HostEvent& e, double) override { if (e.kind == HostEvent::Kind::overlayConcealed) e_.conceal(); return false; }
    void update(const ModuleFrame& f) override { e_.update(f.center, f.chrome, f.presentation, f.opacity, f.time); }
    void prewarm(const ModuleFrame& f) override { previewPrewarm(e_, f); }
    bool requiresFrames(double t) const override { return e_.requiresFrames(t); }
    void upload(gpu::Renderer& r) override { e_.upload(r); }
    Entries entries(core::Module) override { return e_.entries(); }
    void collected(gpu::Renderer& r) override { e_.collected(r); }
    void release(gpu::Renderer& r) override { e_.release(r); }
    bool covers(core::Point p) const override { return e_.covers(p); }
    bool pointerLocked() const override { return e_.pointerLocked(); }
    bool pointer(const PointerEvent& e, double t) override { return e_.pointer(e, t); }
    bool wheel(const WheelEvent& e, double t) override { return e_.wheel(e, t); }
    bool key(const KeyEvent& e, double t) override { return e_.key(e, t); }
    void focus(bool v, double) override { if (!v) e_.cancelInteraction(); }
};

class WorkModeModule final : public ModuleOwner {
    tools::WorkModePreview& w_;
    VolumeModuleHooks hooks_;
    // HUDWorkModeInteraction.layoutAccessibility (native/work_mode_automation.hpp).
    std::unique_ptr<gpu::NativeProfileAutomation> automation_;
    IRawElementProviderFragmentRoot* automationRoot_{};
    core::Language automationLanguage_{core::Language::english};
public:
    WorkModeModule(tools::WorkModePreview& w, VolumeModuleHooks hooks) : w_(w), hooks_(std::move(hooks)) {}
    ~WorkModeModule() override { automation_.reset(); }
    bool automationRefresh(IRawElementProviderFragmentRoot* root) override {
        if (!hooks_.toScreen || !hooks_.clock) return false;
        bool rebuilt = false;
        if (!automation_ || automationRoot_ != root) {
            if (automation_) { rebuilt = automation_->size() > 0; automation_->disconnect(); }
            gpu::ProfileAutomationHost h; h.root = root;
            // The open custom duration field is the focused Edit peer (HUDWorkDurationField).
            h.controls = [this] { return w_.clientRect(modules::WorkModePresentation::bounds()) ? gpu::workModeAutomationControls(w_.state(), automationLanguage_, w_.editingText()) : std::vector<gpu::ProfileAutomationControl>{}; };
            h.toScreen = [this](core::Rect canvas) -> std::optional<core::Rect> { const auto client = w_.clientRect(canvas); return client ? hooks_.toScreen(*client) : std::nullopt; };
            h.invoke = [this](std::string_view id) { const bool done = w_.perform(id, hooks_.clock()); if (done && hooks_.changed) hooks_.changed(); return done; };
            h.setText = [this](std::string_view id, std::string_view utf8) { if (id != gpu::workModeDurationEditorID) return false; const bool done = w_.setEditingText(utf8, hooks_.clock()); if (done && hooks_.changed) hooks_.changed(); return done; };
            automation_ = std::make_unique<gpu::NativeProfileAutomation>(std::move(h)); automationRoot_ = root;
        }
        return automation_->refresh() || rebuilt;
    }
    std::size_t automationCount() const override { return automation_ ? automation_->size() : 0; }
    long automationChild(std::size_t index, IRawElementProviderFragment** out) const override {
        if (!automation_) { if (out) *out = nullptr; return E_INVALIDARG; }
        return automation_->child(index, out);
    }
    long automationFromPoint(double x, double y, IRawElementProviderFragment** out) const override {
        if (!automation_) { if (out) *out = nullptr; return S_OK; }
        return automation_->fromPoint(x, y, out);
    }
    void automationDisconnect() noexcept override { if (automation_) automation_->disconnect(); }
    std::string_view name() const noexcept override { return "workMode"; }
    ModuleRouting routing() const noexcept override {
        ModuleRouting r; r.pointer = 15; r.key = 8; r.filterKey = 2; r.message = 3; r.messageRetriesPendingClose = true; r.finishEditing = 4; return r;
    }
    bool presents(core::Module m) const noexcept override { return m == core::Module::workMode; }
    void resize(const ClientMetrics& m) override { w_.resize(m); }
    void setAppearance(const ModuleAppearance& a, double t) override {
        w_.setLanguage(a.language); w_.setAppearance({a.dark, 2, a.accent}); w_.setReduceMotion(a.reduceMotion, t);
        automationLanguage_ = a.language == core::Language::system ? core::Language::english : a.language;
    }
    void setOverlayVisible(bool v, double t) override { w_.setOverlayVisible(v, t); }
    void overlayClosing(double t) override { w_.cancelInteraction(t); w_.setOverlayVisible(false, t); }
    void update(const ModuleFrame& f) override { w_.update(f.center, f.chrome, f.presentation, f.opacity, f.time); }
    void prewarm(const ModuleFrame& f) override { previewPrewarm(w_, f); }
    bool requiresFrames(double t) const override { return w_.requiresFrames(t); }
    std::optional<double> nextWakeTime(double) const override { return w_.nextWakeTime(); }
    bool deadline(double t) override { const auto revision = w_.state().revision(); w_.wake(t); return revision != w_.state().revision(); }
    void upload(gpu::Renderer& r) override { w_.upload(r); }
    Entries entries(core::Module) override { return w_.entries(); }
    void collected(gpu::Renderer& r) override { w_.collected(r); }
    void release(gpu::Renderer& r) override { w_.release(r); }
    bool covers(core::Point p) const override { return w_.covers(p); }
    bool pointerLocked() const override { return w_.pointerLocked(); }
    bool pointer(const PointerEvent& e, double t) override { return w_.pointer(e, t); }
    bool key(const KeyEvent& e, double t) override { return w_.key(e, t); }
    bool filterKey(const NativeMessage& m) override { return w_.filterKey(m); }
    bool message(const NativeMessage& m, double t) override { return w_.message(m, t); }
    void focus(bool v, double t) override { w_.focus(v, t); }
    bool finishEditing(double t) override { return w_.finishEditing(false, t); }
    // HUDWorkModeInteraction: the open duration field is a HUDProjectedTextEditor
    // over durationEditorRect (the rect its pointer routing also tests).
    std::optional<ModuleCursor> cursorAt(core::Point p) const override {
        if constexpr (TextCursorPreview<tools::WorkModePreview>) return previewTextCursor(w_, p);
        else {
            if (!w_.editing()) return std::nullopt;
            const bool inside = insideProjectedRect(p, modules::WorkModePresentation::durationEditorRect(), [this](core::Rect r) { return w_.clientRect(r); });
            return inside ? std::optional(ModuleCursor::text) : std::nullopt;
        }
    }
};

class BatteryModule final : public ModuleOwner {
    tools::BatteryPreview& b_;
public:
    explicit BatteryModule(tools::BatteryPreview& b) : b_(b) {}
    std::string_view name() const noexcept override { return "power"; }
    ModuleRouting routing() const noexcept override { ModuleRouting r; r.pointer = 14; r.pointerPolicy = {false, false, false}; return r; }
    bool presents(core::Module m) const noexcept override { return m == core::Module::power; }
    void resize(const ClientMetrics& m) override { b_.resize(m); }
    void setAppearance(const ModuleAppearance& a, double t) override { b_.setAppearance({a.dark, 2, a.language, a.accent}); b_.setReduceMotion(a.reduceMotion, t); }
    void overlayClosing(double t) override { b_.cancelInteraction(t); }
    void update(const ModuleFrame& f) override { b_.update(f.center, f.chrome, f.presentation, f.opacity, f.time); }
    void prewarm(const ModuleFrame& f) override { previewPrewarm(b_, f); }
    bool requiresFrames(double t) const override { return b_.requiresFrames(t); }
    void upload(gpu::Renderer& r) override { b_.upload(r); }
    Entries entries(core::Module) override { return b_.entries(); }
    void collected(gpu::Renderer& r) override { b_.collected(r); }
    void release(gpu::Renderer& r) override { b_.release(r); }
    bool covers(core::Point p) const override { return b_.covers(p); }
    bool pointer(const PointerEvent& e, double t) override { return b_.pointer(e, t); }
};

class SettingsModule final : public ModuleOwner {
    tools::SettingsPreview& s_;
public:
    explicit SettingsModule(tools::SettingsPreview& s) : s_(s) {}
    std::string_view name() const noexcept override { return "settings"; }
    ModuleRouting routing() const noexcept override {
        ModuleRouting r; r.pointerCapture = 1; r.pointer = 2; r.wheelCapture = 1; r.wheel = 2; r.keyCapture = 1; r.key = 2;
        r.pointerPolicy = {false, true, false}; return r;
    }
    bool presents(core::Module m) const noexcept override {
        return m == core::Module::system || m == core::Module::display || m == core::Module::hotkeys || m == core::Module::about;
    }
    void resize(const ClientMetrics& m) override { s_.resize(m); }
    void setAppearance(const ModuleAppearance& a, double t) override { s_.setAppearance({a.dark, 2, a.accent}); s_.setLanguage(a.language); s_.setReduceMotion(a.reduceMotion, t); }
    void setOverlayVisible(bool v, double t) override { s_.setOverlayVisible(v, t); }
    void overlayClosing(double t) override { s_.setOverlayVisible(false, t); }
    void update(const ModuleFrame& f) override { s_.update(f.center, f.chrome, f.presentation, f.opacity, f.time); }
    void prewarm(const ModuleFrame& f) override { previewPrewarm(s_, f); }
    bool requiresFrames(double t) const override { return s_.requiresFrames(t); }
    std::optional<double> nextWakeTime(double now) const override { return s_.nextWakeTime(now); }
    bool deadline(double t) override { s_.wake(t); return true; }
    void upload(gpu::Renderer& r) override { s_.upload(r); }
    Entries entries(core::Module) override { return s_.entries(false); }
    Entries modalEntries() override { return s_.modalEntries(); }
    void collected(gpu::Renderer& r) override { s_.collected(r); }
    void release(gpu::Renderer& r) override { s_.release(r); }
    bool covers(core::Point p) const override { return s_.covers(p); }
    bool pointerLocked() const override { return s_.pointerLocked(); }
    bool capturesPointer() const override { return s_.modalActive() || s_.pointerLocked(); }
    bool capturesWheel() const override { return s_.modalActive(); }
    bool capturesKeys() const override { return s_.modalActive() || s_.controller().capturingShortcut(); }
    bool suppressesKeyFilters() const override { return capturesKeys(); }
    bool pointer(const PointerEvent& e, double t) override { return s_.pointer(e, t); }
    bool wheel(const WheelEvent& e, double t) override { return s_.wheel(e, t); }
    bool key(const KeyEvent& e, double t) override { return s_.key(e, t); }
    void focus(bool v, double t) override { if (!v) s_.cancelInteraction(t); }
};

class StorageModule final : public ModuleOwner {
    tools::StoragePreview& s_;
    StorageModuleHooks hooks_;
    gpu::NativeScreenReaderPeers peers_; // Mac HUDTelemetryInteraction peers (last: disconnects first)
    double clock() const { return hooks_.clock(); }
public:
    StorageModule(tools::StoragePreview& s, StorageModuleHooks hooks, gpu::ScreenReaderPeerHooks peerHooks)
        : s_(s), hooks_(std::move(hooks)), peers_(tools::storageAutomation(s, std::move(peerHooks))) {}
    bool automationRefresh(IRawElementProviderFragmentRoot* root) override { return peers_.refresh(root); }
    std::size_t automationCount() const override { return peers_.size(); }
    long automationChild(std::size_t i, IRawElementProviderFragment** out) const override { return peers_.child(i, out); }
    long automationFromPoint(double x, double y, IRawElementProviderFragment** out) const override { return peers_.fromPoint(x, y, out); }
    void automationDisconnect() noexcept override { peers_.disconnect(); }
    std::string_view name() const noexcept override { return "storage"; }
    ModuleRouting routing() const noexcept override { ModuleRouting r; r.pointer = 11; return r; }
    bool presents(core::Module m) const noexcept override { return m == core::Module::storage; }
    void resize(const ClientMetrics& m) override {
        if (hasPixels(m)) s_.resize(m);
        s_.setVisible(hasPixels(m) && hooks_.presented(), clock());
    }
    void setAppearance(const ModuleAppearance& a, double t) override { s_.setAppearance(storageAppearance(a.language, a.dark, a.accent), t); s_.setReduceMotion(a.reduceMotion, t); }
    void setOverlayVisible(bool v, double t) override { s_.setVisible(v, t); }
    void overlayClosing(double t) override { s_.cancelInteraction(t); s_.setVisible(false, t); }
    // HUD fully closed: a Storage page that is not shown keeps no raster artwork.
    bool hostEvent(const HostEvent& e, double) override { if (e.kind == HostEvent::Kind::overlayConcealed) s_.conceal(); return false; }
    void update(const ModuleFrame& f) override { s_.update(f.center, f.chrome, f.presentation, f.opacity, f.time); }
    void prewarm(const ModuleFrame& f) override { previewPrewarm(s_, f); }
    bool requiresFrames(double t) const override { return s_.requiresFrames(t); }
    std::optional<double> nextWakeTime(double) const override { return s_.nextWakeTime(); }
    bool deadline(double t) override { return s_.deadline(t); }
    void upload(gpu::Renderer& r) override { s_.upload(r); }
    Entries entries(core::Module) override { return s_.entries(); }
    void collected(gpu::Renderer& r) override { s_.collected(r); }
    void release(gpu::Renderer& r) override { s_.release(r); }
    bool covers(core::Point p) const override { return s_.covers(p); }
    bool pointerLocked() const override { return s_.pointerLocked(); }
    bool pointer(const PointerEvent& e, double t) override { return s_.pointer(e, t); }
    void focus(bool v, double) override { if (!v) s_.cancelInteraction(clock()); }
};

class ActivityModule final : public ModuleOwner {
    tools::ActivityPreview& a_;
    gpu::NativeScreenReaderPeers peers_; // Mac HUDTelemetryInteraction peers (last: disconnects first)
public:
    ActivityModule(tools::ActivityPreview& a, gpu::ScreenReaderPeerHooks peerHooks) : a_(a), peers_(tools::activityAutomation(a, std::move(peerHooks))) {}
    bool automationRefresh(IRawElementProviderFragmentRoot* root) override { return peers_.refresh(root); }
    std::size_t automationCount() const override { return peers_.size(); }
    long automationChild(std::size_t i, IRawElementProviderFragment** out) const override { return peers_.child(i, out); }
    long automationFromPoint(double x, double y, IRawElementProviderFragment** out) const override { return peers_.fromPoint(x, y, out); }
    void automationDisconnect() noexcept override { peers_.disconnect(); }
    std::string_view name() const noexcept override { return "activityMonitor"; }
    ModuleRouting routing() const noexcept override { ModuleRouting r; r.pointer = 12; r.wheel = 8; r.key = 9; return r; }
    bool presents(core::Module m) const noexcept override { return m == core::Module::activityMonitor; }
    void resize(const ClientMetrics& m) override { if (hasPixels(m)) a_.resize(m); }
    void setAppearance(const ModuleAppearance& a, double) override { a_.setAppearance({a.dark, 2, a.language, a.accent}); a_.setReduceMotion(a.reduceMotion); }
    void setOverlayVisible(bool v, double t) override { a_.setOverlayVisible(v, t); }
    void overlayClosing(double t) override { a_.cancelInteraction(); a_.setOverlayVisible(false, t); }
    // HUD fully closed: hidden Activity pages keep no raster artwork.
    bool hostEvent(const HostEvent& e, double) override { if (e.kind == HostEvent::Kind::overlayConcealed) a_.conceal(); return false; }
    void update(const ModuleFrame& f) override { a_.update(f.center, f.chrome, f.presentation, f.opacity, f.time); }
    void prewarm(const ModuleFrame& f) override { previewPrewarm(a_, f); }
    bool requiresFrames(double t) const override { return a_.requiresFrames(t); }
    void upload(gpu::Renderer& r) override { a_.upload(r); }
    Entries entries(core::Module) override { return a_.entries(); }
    void collected(gpu::Renderer& r) override { a_.collected(r); }
    void release(gpu::Renderer& r) override { a_.release(r); }
    bool covers(core::Point p) const override { return a_.covers(p); }
    bool pointerLocked() const override { return a_.pointerLocked(); }
    bool pointer(const PointerEvent& e, double t) override { return a_.pointer(e, t); }
    bool wheel(const WheelEvent& e, double t) override { return a_.wheel(e, t); }
    bool key(const KeyEvent& e, double t) override { return a_.key(e, t); }
    void focus(bool v, double) override { if (!v) a_.cancelInteraction(); }
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

class CalendarModule final : public ModuleOwner {
    tools::CalendarPreview& c_;
    CalendarModuleHooks hooks_;
    gpu::NativeScreenReaderPeers peers_; // CalendarAXButton and editor peers (last: disconnects first)
public:
    CalendarModule(tools::CalendarPreview& c, CalendarModuleHooks hooks, gpu::ScreenReaderPeerHooks peerHooks)
        : c_(c), hooks_(std::move(hooks)), peers_(tools::calendarAutomation(c, std::move(peerHooks))) {}
    bool automationRefresh(IRawElementProviderFragmentRoot* root) override { return peers_.refresh(root); }
    std::size_t automationCount() const override { return peers_.size(); }
    long automationChild(std::size_t i, IRawElementProviderFragment** out) const override { return peers_.child(i, out); }
    long automationFromPoint(double x, double y, IRawElementProviderFragment** out) const override { return peers_.fromPoint(x, y, out); }
    void automationDisconnect() noexcept override { peers_.disconnect(); }
    std::string_view name() const noexcept override { return "calendar"; }
    ModuleRouting routing() const noexcept override {
        ModuleRouting r; r.pointerCapture = 3; r.pointer = 4; r.wheelCapture = 3; r.wheel = 9; r.keyCapture = 2; r.key = 10;
        r.filterKey = 4; r.filterRequiresOwnerTarget = true; r.message = 1; r.messageRetriesPendingModule = r.messageRetriesPendingClose = true;
        r.finishEditing = 2; r.flush = 2; r.pointerPolicy = {false, true, false}; return r;
    }
    bool presents(core::Module m) const noexcept override { return m == core::Module::calendar; }
    void resize(const ClientMetrics& m) override { if (hasPixels(m)) c_.resize(m); }
    void setAppearance(const ModuleAppearance& a, double t) override {
        c_.setAppearance({a.dark, a.accent}, t); c_.setLanguage(a.language, t); c_.setReduceMotion(a.reduceMotion, t);
        if (hooks_.languageChanged) hooks_.languageChanged(a.language);
    }
    void setOverlayVisible(bool v, double t) override { c_.setOverlayVisible(v, t); }
    void overlayClosing(double t) override { c_.setOverlayVisible(false, t); if (hooks_.afterClosing) hooks_.afterClosing(); }
    void update(const ModuleFrame& f) override { c_.update(f.center, f.chrome, f.presentation, f.opacity, f.time); if (hooks_.afterUpdate) hooks_.afterUpdate(); }
    void prewarm(const ModuleFrame& f) override { previewPrewarm(c_, f); }
    bool requiresFrames(double t) const override { return c_.requiresFrames(t); }
    void upload(gpu::Renderer& r) override { c_.upload(r); }
    Entries entries(core::Module) override { return c_.entries(); }
    void collected(gpu::Renderer& r) override { c_.collected(r); }
    void release(gpu::Renderer& r) override { c_.release(r); }
    bool covers(core::Point p) const override { return c_.covers(p); }
    bool capturesPointer() const override { return c_.capturesPointer(); }
    bool capturesWheel() const override { return c_.capturesPointer(); }
    bool capturesKeys() const override { return c_.capturesPointer(); }
    bool pointer(const PointerEvent& e, double t) override { return c_.pointer(e, t); }
    bool wheel(const WheelEvent& e, double t) override { return c_.wheel(e, t); }
    bool key(const KeyEvent& e, double t) override { return c_.key(e, t); }
    bool filterKey(const NativeMessage& m) override { return c_.filterKey(m); }
    bool message(const NativeMessage& m, double t) override { return c_.message(m, t); }
    void focus(bool v, double t) override { c_.focus(v, t); }
    bool releasesSelection(double t) override { return c_.dismissMenu(false, t); }
    bool finishEditing(double t) override { return c_.dismissMenu(false, t); }
    bool flush(double) override { return !hooks_.flush || hooks_.flush(); }
    // CalendarFieldTextView editors of the event menu (the preview's own hit test).
    std::optional<ModuleCursor> cursorAt(core::Point p) const override { return previewTextCursor(c_, p); }
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
std::unique_ptr<ModuleOwner> clipboardModule(tools::ClipboardPreview& o, const gpu::NativeClipboardAssets& a, gpu::ScreenReaderPeerHooks peers) { return std::make_unique<ClipboardModule>(o, a, std::move(peers)); }
std::unique_ptr<ModuleOwner> volumeModule(tools::VolumePreview& o, VolumeModuleHooks hooks) { return std::make_unique<VolumeModule>(o, std::move(hooks)); }
std::unique_ptr<ModuleOwner> eventLogModule(tools::EventLogPreview& o, gpu::ScreenReaderPeerHooks peers) { return std::make_unique<EventLogModule>(o, std::move(peers)); }
std::unique_ptr<ModuleOwner> workModeModule(tools::WorkModePreview& o, VolumeModuleHooks hooks) { return std::make_unique<WorkModeModule>(o, std::move(hooks)); }
std::unique_ptr<ModuleOwner> batteryModule(tools::BatteryPreview& o) { return std::make_unique<BatteryModule>(o); }
std::unique_ptr<ModuleOwner> settingsModule(tools::SettingsPreview& o) { return std::make_unique<SettingsModule>(o); }
std::unique_ptr<ModuleOwner> storageModule(tools::StoragePreview& o, StorageModuleHooks h, gpu::ScreenReaderPeerHooks peers) { return std::make_unique<StorageModule>(o, std::move(h), std::move(peers)); }
std::unique_ptr<ModuleOwner> activityModule(tools::ActivityPreview& o, gpu::ScreenReaderPeerHooks peers) { return std::make_unique<ActivityModule>(o, std::move(peers)); }
std::unique_ptr<ModuleOwner> readerModule(tools::ReaderPreview& o, gpu::ReaderOwner& r, gpu::LayerRasterizer& raster, std::function<void(const WheelEvent&, double)> observed, gpu::ScreenReaderPeerHooks peers) {
    return std::make_unique<ReaderModule>(o, r, raster, std::move(observed), std::move(peers));
}
std::unique_ptr<ModuleOwner> calendarModule(tools::CalendarPreview& o, CalendarModuleHooks h, gpu::ScreenReaderPeerHooks peers) { return std::make_unique<CalendarModule>(o, std::move(h), std::move(peers)); }
std::unique_ptr<ModuleOwner> mapModule(tools::MapPreview& o, gpu::ScreenReaderPeerHooks peers) { return std::make_unique<MapModule>(o, std::move(peers)); }
std::unique_ptr<ModuleOwner> minigameModule(tools::OrbiPomPreview& o, gpu::ScreenReaderPeerHooks peers) { return std::make_unique<MinigameModule>(o, std::move(peers)); }
std::unique_ptr<ModuleOwner> archiveModule(tools::ArchivePreview& o, ArchiveService* s, tools::ArchiveMediaBinding* m, std::function<double()> clock, std::function<void()> changed) {
    return std::make_unique<ArchiveModule>(o, s, m, std::move(clock), std::move(changed));
}
} // namespace endfield::app
