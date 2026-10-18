#pragma once
#include "modules/orbipom_state.hpp"
#include "core/data/json.hpp"
#include "core/localization.hpp"
#include <span>

namespace endfield::modules {
using OrbiPomColor=std::array<double,4>;
struct OrbiPomAppearance {
    bool dark{true},reducedMotion{};core::Language language{core::Language::english};
    OrbiPomColor accent{.98,.87,.13,1};
    // NSColor.systemRed resolved by the original appearance, not a Win32 color.
    OrbiPomColor systemRed{1,69./255,58./255,1};
    bool operator==(const OrbiPomAppearance&)const=default;
};
struct OrbiPomAsset {std::string_view file,sha256;unsigned width{},height{};};
std::span<const OrbiPomAsset>orbiPomAssets()noexcept;
ehud::data::Json orbiPomImage(unsigned assetIndex,core::Rect);
ehud::data::Json orbiPomText(std::string,core::Rect,double,OrbiPomColor,bool centered=false,bool wrapped=false,bool monospaced=true);
enum class OrbiPomSurfaceRole {artwork,dimmer,tint,rim};
struct OrbiPomSurface {
    std::string id;ehud::data::Json content;core::Matrix4 local;float opacity{1};
    OrbiPomSurfaceRole role{};std::optional<OrbiPomAction>action;
};
struct OrbiPomArtwork {std::vector<OrbiPomSurface>face,overlay;};
// Source controls/labels only. Bodies, wind and energy charge stay numeric and
// use the unchanged runtime snapshots; this function is a content-event path.
OrbiPomArtwork prepareOrbiPomArtwork(const OrbiPomSession&,const OrbiPomState&,const OrbiPomAppearance&);
std::array<std::string,9>orbiPomRuleParagraphs(core::Language);
struct OrbiPomRules {
    std::vector<OrbiPomSurface>surfaces;core::Rect bounds,viewport;core::Point origin;
    double contentHeight{},maximumScroll{};std::array<std::size_t,9>paragraphSurfaces{};
};
// Caller measures each original paragraph at width294, system semibold10.
// Heights are actual measured text bounds; ceil/max18/+3 is applied here.
OrbiPomRules prepareOrbiPomRules(const OrbiPomAppearance&,std::span<const double,9>measuredHeights);
ehud::data::Json orbiPomSelectionRing(double size,OrbiPomColor);
ehud::data::Json orbiPomDangerLine(OrbiPomColor);
}
