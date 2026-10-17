#pragma once
#include "modules/profile_state.hpp"
#include "native/layer_group.hpp"
#include "native/projected_editor.hpp"
#ifdef _WIN32
namespace endfield::native {
struct NativeProfileFieldPose {
    core::Matrix4 world,camera;unsigned pixelWidth{},pixelHeight{};
    float opacity{1};bool visible{true},focused{};std::span<const PlaneMask>ownerMasks;
    std::optional<PlaneShutter>shutter;
};
struct NativeProfileFieldAppearance {
    bool dark{true};std::array<double,3>accent{250./255,212./255,31./255};bool failed{}; // systemRed border after a rejected commit
    bool operator==(const NativeProfileFieldAppearance&)const=default;
};
// HUDPersonalProfileInteraction's projected NSTextView field for one profile
// value: semibold system text (11 pt introduction, otherwise 55% of the field
// height clamped to 10...20 pt), right-aligned numbers, single line except the
// introduction, a 2 pt rounded backing (white .13/.93) and 1 pt accent border.
// Shared TSF/document/painted layout; no HWND/service/clock/publisher. The
// painted editor accepts <=65536 UTF16 units; larger text is rejected, never
// truncated. Character limits apply only after marked text commits.
class NativeProfileEditorField final {
public:
    NativeProfileEditorField(HWND,LayerRasterizer&,LayerRasterOptions,modules::ProfileField,core::Rect,
        std::string text,NativeProfileFieldAppearance,UINT message,UINT_PTR generation);
    ~NativeProfileEditorField();
    // HUDPersonalProfileInteraction.normalizeEditor: tag drops one leading #
    // Character; text fields keep their Character prefix. Skipped while composing.
    bool normalize(const modules::ProfileTextRules&);
    bool syncContent();void setAppearance(NativeProfileFieldAppearance);
    void updatePose(const NativeProfileFieldPose&);bool upload(Renderer&);
    LayerCompositionEntry entry();bool releaseResources(Renderer&);
    NativeProjectedEditor&editor()noexcept;core::notes::RichDocument&document()noexcept;
    std::string text()const;modules::ProfileField field()const noexcept;core::Rect rect()const noexcept;
    UINT_PTR generation()const noexcept;bool stopInput();
private:struct Impl;std::unique_ptr<Impl>impl_;
};
}
#endif
