#pragma once
#include "modules/settings_safety.hpp"
#include "native/layer_scene.hpp"
namespace endfield::native {
#ifdef _WIN32
// One private retained group covers the projected card and its underlying dim.
// Four disjoint exterior dim rectangles apply the same root alpha, exactly
// preserving source group opacity without a fullscreen intermediate target.
class NativeSettingsSafety final {
public:
 NativeSettingsSafety(modules::SettingsSafety&,LayerRasterizer&,LayerRasterOptions,modules::SettingsAppearance={});
 ~NativeSettingsSafety();void setAppearance(modules::SettingsAppearance);
 bool syncContent(core::Rect viewport,double time=0);void updatePose(core::Rect viewport,const core::Matrix4&centeredTilt,double time);
 std::optional<int>actionAt(core::Point logical)const;void upload(Renderer&);
 LayerCompositionEntry entry();bool releaseResources(Renderer&);
 core::Rect coverage()const noexcept;std::size_t rasterizations()const noexcept;
private:struct Impl;std::unique_ptr<Impl>impl_;
};
#endif
}
