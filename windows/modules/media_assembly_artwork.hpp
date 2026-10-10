#pragma once
#include "modules/media_assembly_presentation.hpp"
#include "core/data/json.hpp"

namespace endfield::modules {
using MediaAssemblyColor=std::array<double,4>;
struct MediaAssemblyAppearance {
    bool dark{true};core::Language language{core::Language::english};
    MediaAssemblyColor accent{.98,.87,.13,1};
    std::optional<MediaAssemblyColor>systemOrange;
    bool operator==(const MediaAssemblyAppearance&)const=default;
};
struct MediaAssemblyImageReference {std::string path,sha256;};
struct MediaAssemblyArtworkAssets {
    // Index zero is the source None filter and intentionally has no image.
    std::array<MediaAssemblyImageReference,15>filters;
    std::array<MediaAssemblyImageReference,24>stickers;
};
enum class MediaAssemblyPaintGroup {stage,stickers,overlay,drawer,selection,transport};
enum class MediaAssemblyPaintRole {artwork,tint,rim,sticker,inlineItem};
struct MediaAssemblyClip {core::Rect bounds;double cornerRadius{};bool operator==(const MediaAssemblyClip&)const=default;};
struct MediaAssemblySurface {
    std::string id,action;ehud::data::Json content;core::Matrix4 local;
    std::array<MediaAssemblyClip,2>clips{};std::size_t clipCount{};
    MediaAssemblyPaintGroup group{};MediaAssemblyPaintRole role{};
    float opacity{1};bool feedbackEnabled{},framed{};
};
struct MediaAssemblyArtwork {
    std::vector<MediaAssemblySurface>surfaces;
    std::vector<MediaAssemblyAction>actions;
    core::Rect imageRect;bool drawerVisible{};
};
// Content-event source artwork only. No time sampling, source media copies,
// providers, file IO, font resolver or rasterizer. Native pose frames retain the
// result; caller media is inserted after stage and before the sticker leaves.
MediaAssemblyArtwork prepareMediaAssemblyArtwork(const MediaAssemblyPresentation&,
    const MediaAssemblyView&,const MediaAssemblyAppearance&,
    const MediaAssemblyArtworkAssets&,std::string_view error={},double exportProgress=0);
}
