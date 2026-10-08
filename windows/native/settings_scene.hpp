#pragma once
#include "modules/settings_presentation.hpp"
#include "native/layer_group.hpp"
#include <map>
namespace endfield::native {
using SettingsImages=std::map<std::string,ehud::data::Json,std::less<>>;
#ifdef _WIN32
struct NativeSettingsPose {core::Matrix4 world;float opacity{1};double time{};std::span<const PlaneMask>ownerMasks;std::optional<PlaneShutter>shutter;};
struct SettingsSceneStats {std::uint64_t builds{},poses{};std::size_t parts{},retiredParts{};};
// Original page/row artwork, retained local groups for exact row engage alpha.
// Pictures must resolve explicitly; absent/malformed assets abort staging.
// Shared caller composition owns publication. No window, timer, settings store,
// service or live display enumeration is introduced by this adapter.
class NativeSettingsScene final {
public:
 NativeSettingsScene(modules::SettingsPresentation&,LayerRasterizer&,LayerRasterOptions,SettingsImages,modules::SettingsAppearance={});
 ~NativeSettingsScene();void setAppearance(modules::SettingsAppearance);
 bool syncContent(double);bool setFeedback(std::optional<std::string_view>,bool pressed,double);
 void updatePose(const NativeSettingsPose&);bool uploadAnimations(Renderer&);bool requiresFrames(double)const;
 std::span<const LayerCompositionEntry>entries()const noexcept;
 bool collectRetired(Renderer&);bool releaseResources(Renderer&);SettingsSceneStats stats()const noexcept;
private:struct Impl;std::unique_ptr<Impl>impl_;
};
#endif
}
