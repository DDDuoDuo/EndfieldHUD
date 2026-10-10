#pragma once
#include "modules/calendar_editor_text.hpp"
#include "modules/calendar_presentation.hpp"
#include "native/projected_editor.hpp"
#include "native/layer_group.hpp"
#ifdef _WIN32
namespace endfield::native {
struct NativeCalendarFieldPose {
    core::Matrix4 world,camera;unsigned pixelWidth{},pixelHeight{};
    float opacity{1};bool visible{true},focused{};std::span<const PlaneMask>ownerMasks;
    std::optional<PlaneShutter>shutter;
};
// One original Calendar plain field: existing TSF/document/painted layout plus
// retained backing, source placeholder, scrollbar and border. Root opacity is
// applied once to their GPU composition. No HWND/service/clock/publisher.
// The existing painted editor accepts <=65536 UTF16 units. Larger valid stored
// combining sequences are rejected explicitly, never truncated to this bound.
class NativeCalendarEditorField final {
public:
    NativeCalendarEditorField(HWND,LayerRasterizer&,LayerRasterOptions,modules::CalendarEditorField,
        std::string text,std::string placeholder,modules::CalendarAppearance,UINT message,UINT_PTR generation);
    ~NativeCalendarEditorField();
    bool normalize(const modules::CalendarTextRules&);
    bool syncContent();void setAppearance(modules::CalendarAppearance,std::string placeholder);
    void updatePose(const NativeCalendarFieldPose&);bool upload(Renderer&);
    LayerCompositionEntry entry();
    // Snapshot output for the source menu's grouped closing capture. Resources
    // remain owned by this field until that outgoing group is detached.
    DrawObject capturedLocalDraw(const core::Matrix4&relativeWorld)const;
    bool releaseResources(Renderer&);
    NativeProjectedEditor&editor()noexcept;core::notes::RichDocument&document()noexcept;
    modules::CalendarEditorField kind()const noexcept;UINT_PTR generation()const noexcept;
    bool stopInput();
private:struct Impl;std::unique_ptr<Impl>impl_;
};
}
#endif
