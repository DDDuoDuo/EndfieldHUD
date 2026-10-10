#pragma once
#include "app/overlay_host.hpp"
#include "core/module_presentation.hpp"
#include "core/source_desktop_chrome.hpp"
#include "core/localization.hpp"
#include "native/calendar_civil.hpp"
#include "native/calendar_editor_field.hpp"
#include "native/calendar_scene.hpp"
#include "modules/calendar_localization.hpp"
#ifdef _WIN32
namespace endfield::tools {
struct CalendarKeyModifiers {bool control{},shift{},alt{},system{};};
struct CalendarPreviewOptions {
    native::LayerRasterOptions raster;
    modules::CalendarAppearance appearance;
    modules::CalendarStrings strings;
    modules::CalendarTextRules text;
    bool reduceMotion{};
    UINT textMessage{WM_APP+235};
};
// Source Calendar canvas and three-field event menu. State/repository, civil
// context, shared activated TSF manager, HWND, rasterizer and Renderer belong to
// the app and outlive this owner. No service, native window, clock, timer,
// notification adapter or independent publisher is created here.
// Existing short plain editor boundary is explicit; stored text is not silently
// truncated to a UTF16 rendering capacity. Source Character limits use the
// caller's installed Unicode text rules only after marked text commits.
class CalendarPreview final {
public:
    CalendarPreview(HWND,modules::CalendarState&,native::CalendarCivilContext&,
        native::LayerRasterizer&,CalendarPreviewOptions,
        ITfThreadMgr* alreadyActivatedManager=nullptr,TfClientId=TF_CLIENTID_NULL);
    ~CalendarPreview();
    void resize(const app::ClientMetrics&);
    void setLanguage(core::Language,double time);
    void setAppearance(modules::CalendarAppearance,double time);
    void setReduceMotion(bool,double time);
    // App forwards explicit time/time-zone/locale/wake notifications. Reminder
    // persistence/deadlines remain CalendarState's app-owned scheduling adapter.
    void systemChanged(double time);
    void setOverlayVisible(bool,double time);
    void update(const core::Matrix4& sourceCenter,
        const core::source::DesktopChromeSettings&,
        const core::ModulePresentationSample&,float opacity,double time);
    bool requiresFrames(double time)const;
    bool covers(core::Point logicalClientPoint)const;
    // Menu routing captures pointers; Calendar does not lock the source gyro.
    bool capturesPointer()const noexcept;
    bool pointer(const app::PointerEvent&,double time);
    bool wheel(const app::WheelEvent&,double time);
    bool key(const app::KeyEvent&,double time,
        std::optional<CalendarKeyModifiers> modifiers={});
    bool filterKey(const app::NativeMessage&);
    bool message(const app::NativeMessage&,double time);
    void focus(bool,double time);
    // Explicit Escape/tab/hide teardown can decline a current TSF write lock;
    // it is retried only on the queued editor change, never a polling timer.
    bool dismissMenu(bool animated,double time);
    bool editing()const noexcept;
    std::optional<modules::CalendarEditorField> focusedField()const noexcept;
    const modules::CalendarView&view()const noexcept;
    // Source AX elements for the shared UI Automation provider (Mac
    // HUDCalendarInteraction.layoutAccessibility): empty unless the module is
    // active; canvas buttons, then the open event menu's buttons and three
    // fields. Rects are canvas points; project them like pointer hit tests.
    // Content-event query (after input, state or language change), not per frame.
    std::vector<modules::CalendarAccessible>accessibility()const;
    void upload(native::Renderer&);
    std::span<const native::LayerCompositionEntry>entries();
    void collected(native::Renderer&);
    void release(native::Renderer&); // app detaches combined list first
private:struct Impl;std::shared_ptr<Impl>impl_;
};
}
#endif
