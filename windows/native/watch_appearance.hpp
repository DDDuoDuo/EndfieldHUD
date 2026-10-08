#pragma once
#include "core/source_desktop_content.hpp"
#include "native/watch_content.hpp"
namespace endfield::native {
// Exact original icon trees are independent of language/theme/selection. The
// supplied JSON is the compact, hash-verified source asset, not a frame oracle.
class WatchAppearanceTemplates final {
public:
 explicit WatchAppearanceTemplates(const core::source::Json&);
 const core::source::Json&captionTemplate()const noexcept{return caption_;}
 const core::source::Json&icon(std::string_view key,bool reportSlot)const;
 core::source::Json localIcon(std::string_view key,bool reportSlot,std::string_view surfaceID)const;
 std::size_t iconCount()const noexcept{return icons_.size();}
private:
 struct Icon{core::source::Json tree;bool reportOnly{};};
 core::source::Json caption_;std::map<std::string,Icon,std::less<>>icons_;
};
core::source::Json compileDesktopCaption(const core::source::Json&sourceTemplate,
 const core::source::DesktopCaptionPlan&,core::source::DesktopTextSize evaluatedSize,std::string_view surfaceID);
struct WatchAppearanceEntry {
 std::uint64_t action{};std::string target,title,iconKey;bool module{true};
 bool operator==(const WatchAppearanceEntry&)const=default;
};
struct WatchAppearance {
 core::Language language{core::Language::english};bool dark{true};
 std::array<double,3>accentSRGB{250./255,212./255,31./255};std::uint64_t selectedAction{};
 bool operator==(const WatchAppearance&)const=default;
};
struct WatchAppearanceStats{std::uint64_t updates{},unchanged{},captionsCompiled{},iconsCompiled{},surfaceUpdates{};};
#ifdef _WIN32
// Borrowed catalog/raster/scene outlive this adapter. Construct against a fresh
// native-label LayerScene; this is its sole local navigation-content writer.
// The old finite NativeWatchContent bridge must not run alongside it.
// Caller supplies localized entry.title on preference events (custom strings
// stay opaque). NativeLabelPlan retains all projection, masks and placement.
class NativeWatchAppearance final {
public:
 NativeWatchAppearance(const WatchAppearanceTemplates&,const core::source::SceneDefinition&,
  std::span<const core::source::NativeLabelBinding>,LayerScene&,LayerRasterizer&,LayerRasterOptions);
 ~NativeWatchAppearance();
 void setEntries(std::span<const WatchAppearanceEntry>); // content event only
 bool expandsFileShelfCaption(std::uint64_t action,core::Language)const;
 // After availability uses expandsFileShelfCaption and NativeLabelPlan updates,
 // pass its current placements so evaluated bounds and source fitting bounds
 // remain distinct. False means no resource upload. No JSON/layout/allocation
 // on equal action/appearance/bounds frames. Failure requires retry before draw.
 bool update(const WatchContentCatalog::Actions&,const WatchAppearance&,
  std::span<const core::source::NativeLabelPlacement>);
 WatchAppearanceStats stats()const noexcept;
private:struct Impl;std::unique_ptr<Impl>impl_;
};
#endif
}
