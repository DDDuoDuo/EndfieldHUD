#include "app/application_modules.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "core/source_color.hpp"
#include "modules/module_titles.hpp"
#include "tools/notes_preview.hpp"
#include "app/areas/notes_automation_binding.hpp"
#include "modules/hud_design_tokens.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <concepts>

namespace endfield::app {
namespace source = core::source;
namespace gpu = native;

std::string moduleCaption(std::string_view target, core::Language language, std::string_view fallback) {
    return modules::moduleTitle(target, language).value_or(std::string(fallback)); // HUDModule.title
}
// The desktop notes' appearance (NotesWorkspace / HUDNotesInteraction, Mac
// 1.3.0): the redesign neutrals with the theme yellow (HUDColor.yellow of the
// configured accent; Light: its HUDAccentPalette), the editor plate
// HUDColor.field #1D1D1D / plateHigh #E4E5E0 bordered in that yellow, the
// caret yellow (Light #111), the selection yellow .35, the text white / black.
tools::NotesPreviewPreferences notesPreferences(const ModuleAppearance& a) {
    const bool dark=a.dark;const auto language=a.language;
    tools::NotesPreviewPreferences out;out.dark=dark;out.reduceMotion=a.reduceMotion;out.language=language;
    char hex[8];std::snprintf(hex,sizeof hex,"%02X%02X%02X",unsigned(std::lround(std::clamp(a.accentSRGB[0],0.,1.)*255)),unsigned(std::lround(std::clamp(a.accentSRGB[1],0.,1.)*255)),unsigned(std::lround(std::clamp(a.accentSRGB[2],0.,1.)*255)));
    const auto accent=modules::design::AccentPalette::make(hex,!dark).yellow;const modules::NotesColor yellow{accent.r,accent.g,accent.b,1};
    const auto rgb=[](unsigned v){return modules::NotesColor{double(v>>16&255)/255,double(v>>8&255)/255,double(v&255)/255,1};};
    auto&w=out.workspace;w.palette=modules::NotesPalette::source(dark,yellow);w.editor={rgb(dark?0x1D1D1D:0xE4E5E0),yellow};
    w.selectionColor={yellow[0],yellow[1],yellow[2],.35};w.caretColor=dark?yellow:rgb(0x111111);w.editorTextColor=dark?rgb(0xFFFFFF):rgb(0x000000);
    w.compositionColor=w.palette.primary;w.eraserColor=dark?std::array<double,4>{1,69./255.,58./255.,1}:std::array<double,4>{1,59./255.,48./255.,1};
    const auto t=[language](std::string_view en,std::string_view zh){return core::localized(en,zh,language);};auto&s=w.strings;
    s.textTitle=t("TEXT","文字");s.placeholder=t("Start writing","开始书写");s.pin=t("Pin note","固定便笺");s.unpin=t("Unpin note","取消固定");s.remove=t("Delete note","删除便笺");s.edit=t("Edit text","编辑文字");s.select=t("Text","文字");s.grow=t("Enlarge note","放大便笺");s.shrink=t("Reduce note size","缩小便笺");s.format={t("Font size","字号"),t("Font","字体"),t("Color","颜色"),t("Text style","特殊")};
    s.todoTitle=t("TODO","待办");s.itemPlaceholder=t("New item…","新事项…");s.addItem=t("+ Add item","+ 添加事项");s.checkItem=t("Check","勾选");s.uncheckItem=t("Uncheck","取消勾选");s.editItem=t("Edit item","编辑事项");s.moveUp=t("Move up","上移");s.moveDown=t("Move down","下移");s.removeItem=t("Delete item","删除事项");s.addItemAction=t("Add item","添加事项");s.imageTitle=t("IMAGE/VIDEO","图片/视频");s.drawingTitle=t("DRAWING","画画");s.drawingColor=t("Drawing color","画笔颜色");
    out.chooseFile=std::string(core::translated(core::chooseInFileExplorer,language));return out;
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
static_assert(TextCursorPreview<tools::NotesPreview>);

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
    void setAppearance(const ModuleAppearance& a, double t) override { n_.applyPreferences(notesPreferences(a), t); }
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

} // namespace

std::unique_ptr<ModuleOwner> notesModule(tools::NotesPreview& o, std::function<double()> clock, std::function<void()> changed) { return std::make_unique<NotesModule>(o, std::move(clock), std::move(changed)); }
} // namespace endfield::app
