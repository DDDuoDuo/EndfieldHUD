#include "app/application_modules.hpp"
#include "app/archive_service.hpp"
#include "core/source_color.hpp"
#include "native/clipboard_assets.hpp"
#include "native/reader_owner.hpp"
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
    const auto t=[language](std::string_view en,std::string_view zh){return core::localized(en,zh,language);};
    modules::ReaderStrings out;
    out.open=t("Open","打开");out.library=t("Library","书库");out.reading=t("Reading","阅读设置");out.bookmarks=t("Bookmarks","书签");
    out.title=t("E-Reader","阅读器");out.empty=t("Open a book to begin reading.","打开书籍开始阅读。");out.loading=t("Loading book…","正在载入书籍…");
    out.chooseFile=t("Choose file","选择文件");out.chooseShelf=t("Choose from Shelf","从暂存架选择");out.shelfTitle=out.chooseShelf;
    out.font=t("Font","字体");out.fontSize=t("Font size","字号");out.lineSpacing=t("Line spacing","行间距");out.margin=t("Margins","页边距");
    out.horizontal=t("Left to right","从左到右");out.vertical=t("Top to bottom","从上到下");return out;
}
modules::StorageAppearance storageAppearance(core::Language language, bool dark, std::array<double, 4> accent) {
    const auto available=source::sourceWhiteBlend({accent[0],accent[1],accent[2]},.42);
    return {dark,2,language,accent,std::array<double,4>{available[0],available[1],available[2],accent[3]}};
}
native::VolumeStrings volumeStrings(core::Language language) {
    // The English defaults and the Simplified Chinese source strings are the
    // catalog keys; every other language resolves through the shared catalog.
    const native::VolumeStrings en, zh = native::VolumeStrings::simplifiedChinese();
    if (language == core::Language::simplifiedChinese) return zh;
    native::VolumeStrings out;
    const auto t = [&](std::string native::VolumeStrings::*field) { out.*field = core::localized(en.*field, zh.*field, language); };
    for (auto field : {&native::VolumeStrings::title, &native::VolumeStrings::outputDevice, &native::VolumeStrings::inputDevice,
         &native::VolumeStrings::subtitle, &native::VolumeStrings::chooseConnected, &native::VolumeStrings::output, &native::VolumeStrings::input,
         &native::VolumeStrings::noDevice, &native::VolumeStrings::volume, &native::VolumeStrings::balance, &native::VolumeStrings::outputVolume,
         &native::VolumeStrings::balanceLabel, &native::VolumeStrings::appVolumeSuffix, &native::VolumeStrings::unavailable, &native::VolumeStrings::mute,
         &native::VolumeStrings::unmute, &native::VolumeStrings::headphonesBluetooth, &native::VolumeStrings::appVolume, &native::VolumeStrings::back,
         &native::VolumeStrings::chooseOutput, &native::VolumeStrings::chooseInput, &native::VolumeStrings::selected, &native::VolumeStrings::previousPage,
         &native::VolumeStrings::nextPage, &native::VolumeStrings::centered, &native::VolumeStrings::unsupportedBalance, &native::VolumeStrings::missingBalance,
         &native::VolumeStrings::deviceControls, &native::VolumeStrings::noHeadphones, &native::VolumeStrings::headphones, &native::VolumeStrings::outputSuffix,
         &native::VolumeStrings::noApps, &native::VolumeStrings::unsupportedApps, &native::VolumeStrings::noConnectedDevices, &native::VolumeStrings::starting,
         &native::VolumeStrings::stopping, &native::VolumeStrings::restore, &native::VolumeStrings::unsupportedRoute}) t(field);
    return out;
}

namespace {
using Entries = std::span<const gpu::LayerCompositionEntry>;
bool hasPixels(const ClientMetrics& m) { return m.pixelWidth && m.pixelHeight; }

class NotesModule final : public ModuleOwner {
    tools::NotesPreview& n_;
public:
    explicit NotesModule(tools::NotesPreview& n) : n_(n) {}
    std::string_view name() const noexcept override { return "notes"; }
    bool essential() const noexcept override { return true; } // owns module selection and the shared TSF manager
    ModuleRouting routing() const noexcept override {
        ModuleRouting r; r.pointer = 1; r.wheel = 1; r.key = 1; r.filterKey = 5; r.filterRequiresOwnerTarget = true;
        r.message = 4; r.messageRetriesPendingClose = true; r.finishEditing = 3; return r;
    }
    void resize(const ClientMetrics& m) override { n_.resize(m); }
    void setAppearance(const ModuleAppearance& a, double t) override { n_.applyPreferences(notesPreferences(a.language, a.dark, a.reduceMotion, a.accent), t); }
    void setOverlayVisible(bool v, double t) override { if (v) n_.setMediaActive(true, t); }
    void overlayClosing(double t) override { n_.setMediaActive(false, t, true); }
    void update(const ModuleFrame& f) override { n_.update(f.center, f.chrome, f.opacity, f.time, f.focused); }
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
    void update(const ModuleFrame& f) override { s_.update(f.center, f.chrome, f.presentation, f.opacity, f.time); }
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
};

class ClipboardModule final : public ModuleOwner {
    tools::ClipboardPreview& c_;
    const gpu::NativeClipboardAssets& assets_;
public:
    ClipboardModule(tools::ClipboardPreview& c, const gpu::NativeClipboardAssets& a) : c_(c), assets_(a) {}
    std::string_view name() const noexcept override { return "clipboard"; }
    ModuleRouting routing() const noexcept override { ModuleRouting r; r.pointer = 9; r.wheel = 5; r.key = 5; return r; }
    bool presents(core::Module m) const noexcept override { return m == core::Module::clipboard; }
    void resize(const ClientMetrics& m) override { c_.resize(m); }
    void setAppearance(const ModuleAppearance& a, double) override {
        c_.setLanguage(a.language); c_.setAppearance({a.dark, 2, a.accent}, assets_.images()); c_.setReduceMotion(a.reduceMotion);
    }
    void overlayClosing(double) override { c_.cancelInteraction(); }
    void update(const ModuleFrame& f) override { c_.update(f.center, f.chrome, f.presentation, f.opacity, f.time); }
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
public:
    explicit VolumeModule(tools::VolumePreview& v) : v_(v) {}
    std::string_view name() const noexcept override { return "volume"; }
    ModuleRouting routing() const noexcept override { ModuleRouting r; r.pointer = 10; r.wheel = 6; r.key = 6; return r; }
    bool presents(core::Module m) const noexcept override { return m == core::Module::volume; }
    void resize(const ClientMetrics& m) override { v_.resize(m); }
    void setAppearance(const ModuleAppearance& a, double) override {
        v_.setStyle({a.dark, a.accent}); v_.setReduceMotion(a.reduceMotion);
        // Language events now reach Volume too (the former preview never set it).
        if (language_ != a.language) { v_.setStrings(volumeStrings(a.language)); language_ = a.language; }
    }
    void overlayClosing(double t) override { v_.cancelInteraction(t); }
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
public:
    explicit EventLogModule(tools::EventLogPreview& e) : e_(e) {}
    std::string_view name() const noexcept override { return "eventLog"; }
    ModuleRouting routing() const noexcept override { ModuleRouting r; r.pointer = 13; r.wheel = 7; r.key = 7; return r; }
    bool presents(core::Module m) const noexcept override { return m == core::Module::eventLog; }
    void resize(const ClientMetrics& m) override { e_.resize(m); }
    void setAppearance(const ModuleAppearance& a, double) override { e_.setLanguage(a.language); e_.setAppearance({a.dark, 2, a.accent}); e_.setReduceMotion(a.reduceMotion); }
    void overlayClosing(double) override { e_.cancelInteraction(); }
    void update(const ModuleFrame& f) override { e_.update(f.center, f.chrome, f.presentation, f.opacity, f.time); }
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
public:
    explicit WorkModeModule(tools::WorkModePreview& w) : w_(w) {}
    std::string_view name() const noexcept override { return "workMode"; }
    ModuleRouting routing() const noexcept override {
        ModuleRouting r; r.pointer = 15; r.key = 8; r.filterKey = 2; r.message = 3; r.messageRetriesPendingClose = true; r.finishEditing = 4; return r;
    }
    bool presents(core::Module m) const noexcept override { return m == core::Module::workMode; }
    void resize(const ClientMetrics& m) override { w_.resize(m); }
    void setAppearance(const ModuleAppearance& a, double t) override { w_.setLanguage(a.language); w_.setAppearance({a.dark, 2, a.accent}); w_.setReduceMotion(a.reduceMotion, t); }
    void setOverlayVisible(bool v, double t) override { w_.setOverlayVisible(v, t); }
    void overlayClosing(double t) override { w_.cancelInteraction(t); w_.setOverlayVisible(false, t); }
    void update(const ModuleFrame& f) override { w_.update(f.center, f.chrome, f.presentation, f.opacity, f.time); }
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
    double clock() const { return hooks_.clock(); }
public:
    StorageModule(tools::StoragePreview& s, StorageModuleHooks hooks) : s_(s), hooks_(std::move(hooks)) {}
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
    void update(const ModuleFrame& f) override { s_.update(f.center, f.chrome, f.presentation, f.opacity, f.time); }
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
public:
    explicit ActivityModule(tools::ActivityPreview& a) : a_(a) {}
    std::string_view name() const noexcept override { return "activityMonitor"; }
    ModuleRouting routing() const noexcept override { ModuleRouting r; r.pointer = 12; r.wheel = 8; r.key = 9; return r; }
    bool presents(core::Module m) const noexcept override { return m == core::Module::activityMonitor; }
    void resize(const ClientMetrics& m) override { if (hasPixels(m)) a_.resize(m); }
    void setAppearance(const ModuleAppearance& a, double) override { a_.setAppearance({a.dark, 2, a.language, a.accent}); a_.setReduceMotion(a.reduceMotion); }
    void setOverlayVisible(bool v, double t) override { a_.setOverlayVisible(v, t); }
    void overlayClosing(double t) override { a_.cancelInteraction(); a_.setOverlayVisible(false, t); }
    void update(const ModuleFrame& f) override { a_.update(f.center, f.chrome, f.presentation, f.opacity, f.time); }
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
public:
    ReaderModule(tools::ReaderPreview& r, gpu::ReaderOwner& o, gpu::LayerRasterizer& raster, std::function<void(const WheelEvent&, double)> observed)
        : r_(r), owner_(o), raster_(raster), wheelObserved_(std::move(observed)) {}
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
};

class CalendarModule final : public ModuleOwner {
    tools::CalendarPreview& c_;
    CalendarModuleHooks hooks_;
public:
    CalendarModule(tools::CalendarPreview& c, CalendarModuleHooks hooks) : c_(c), hooks_(std::move(hooks)) {}
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
};

class MapModule final : public ModuleOwner {
    tools::MapPreview& m_;
public:
    explicit MapModule(tools::MapPreview& m) : m_(m) {}
    std::string_view name() const noexcept override { return "map"; }
    ModuleRouting routing() const noexcept override {
        ModuleRouting r; r.pointerCapture = 5; r.pointer = 6; r.wheel = 11; r.key = 12;
        r.pointerPolicy = {true, true, true}; r.wheelRefreshesPointerLock = r.deadlineRefreshesPointerLock = true; return r;
    }
    bool presents(core::Module m) const noexcept override { return m == core::Module::map; }
    void resize(const ClientMetrics& m) override { if (hasPixels(m)) m_.resize(m); }
    void setAppearance(const ModuleAppearance& a, double t) override {
        gpu::MapAppearance appearance; appearance.dark = a.dark; appearance.reducedMotion = a.reduceMotion; appearance.ambient = a.ambient;
        appearance.accent = {a.accent[0], a.accent[1], a.accent[2], a.accent[3]};
        m_.setAppearance(std::move(appearance), t); m_.setLanguage(a.language, t);
    }
    void setOverlayVisible(bool v, double t) override { m_.setOverlayVisible(v, t); }
    void overlayClosing(double t) override { m_.cancelInteraction(t); m_.setOverlayVisible(false, t); }
    void update(const ModuleFrame& f) override { m_.update(f.center, f.chrome, f.presentation, f.opacity, f.time); }
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
};

class MinigameModule final : public ModuleOwner {
    tools::OrbiPomPreview& g_;
public:
    explicit MinigameModule(tools::OrbiPomPreview& g) : g_(g) {}
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
    }
    void setOverlayVisible(bool v, double t) override { g_.setOverlayVisible(v, t); }
    void overlayClosing(double t) override { g_.cancelInteraction(t); g_.setOverlayVisible(false, t); }
    void update(const ModuleFrame& f) override { g_.update(f.center, f.chrome, f.presentation, f.opacity, f.time); }
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
};

class ArchiveModule final : public ModuleOwner {
    tools::ArchivePreview& a_;
    ArchiveService* service_;
    tools::ArchiveMediaBinding* media_;
public:
    ArchiveModule(tools::ArchivePreview& a, ArchiveService* s, tools::ArchiveMediaBinding* m) : a_(a), service_(s), media_(m) {}
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
    void update(const ModuleFrame& f) override { a_.update(f.center, f.chrome, f.presentation, f.opacity, f.time); }
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
    bool releasesSelection(double t) override { return a_.finishEditing(t); }
    bool finishEditing(double t) override { return a_.finishEditing(t); }
    bool flush(double t) override { return a_.finishEditing(t) && (!service_ || service_->flush()); }
};
} // namespace

std::unique_ptr<ModuleOwner> notesModule(tools::NotesPreview& o) { return std::make_unique<NotesModule>(o); }
std::unique_ptr<ModuleOwner> shelfModule(tools::ShelfPreview& o) { return std::make_unique<ShelfModule>(o); }
std::unique_ptr<ModuleOwner> clipboardModule(tools::ClipboardPreview& o, const gpu::NativeClipboardAssets& a) { return std::make_unique<ClipboardModule>(o, a); }
std::unique_ptr<ModuleOwner> volumeModule(tools::VolumePreview& o) { return std::make_unique<VolumeModule>(o); }
std::unique_ptr<ModuleOwner> eventLogModule(tools::EventLogPreview& o) { return std::make_unique<EventLogModule>(o); }
std::unique_ptr<ModuleOwner> workModeModule(tools::WorkModePreview& o) { return std::make_unique<WorkModeModule>(o); }
std::unique_ptr<ModuleOwner> batteryModule(tools::BatteryPreview& o) { return std::make_unique<BatteryModule>(o); }
std::unique_ptr<ModuleOwner> settingsModule(tools::SettingsPreview& o) { return std::make_unique<SettingsModule>(o); }
std::unique_ptr<ModuleOwner> storageModule(tools::StoragePreview& o, StorageModuleHooks h) { return std::make_unique<StorageModule>(o, std::move(h)); }
std::unique_ptr<ModuleOwner> activityModule(tools::ActivityPreview& o) { return std::make_unique<ActivityModule>(o); }
std::unique_ptr<ModuleOwner> readerModule(tools::ReaderPreview& o, gpu::ReaderOwner& r, gpu::LayerRasterizer& raster, std::function<void(const WheelEvent&, double)> observed) {
    return std::make_unique<ReaderModule>(o, r, raster, std::move(observed));
}
std::unique_ptr<ModuleOwner> calendarModule(tools::CalendarPreview& o, CalendarModuleHooks h) { return std::make_unique<CalendarModule>(o, std::move(h)); }
std::unique_ptr<ModuleOwner> mapModule(tools::MapPreview& o) { return std::make_unique<MapModule>(o); }
std::unique_ptr<ModuleOwner> minigameModule(tools::OrbiPomPreview& o) { return std::make_unique<MinigameModule>(o); }
std::unique_ptr<ModuleOwner> archiveModule(tools::ArchivePreview& o, ArchiveService* s, tools::ArchiveMediaBinding* m) { return std::make_unique<ArchiveModule>(o, s, m); }
} // namespace endfield::app
