#pragma once
#include "modules/profile_state.hpp"
#include "core/data/json.hpp"
#include <array>
#include <optional>
#include <string>
#include <vector>

namespace endfield::modules {
using ProfileColor=std::array<double,4>; // sRGB, straight alpha
struct ProfileAppearance {
    bool dark{true};double scale{2};std::array<double,3>hudAccent{250./255,212./255,31./255};
    bool operator==(const ProfileAppearance&)const=default;
};
// Owner-supplied raster descriptors in LayerRasterizer form ({memoryImage,
// revision} or {asset,sha256}). A null avatar draws the source silhouette; a
// non-null background switches the page to its backdrop ink/contrast palette.
struct ProfileContents {
    ehud::data::Json avatar,frame;
    bool background{};core::Point backgroundPixels{}; // decoded thumbnail size, for its crop
    bool operator==(const ProfileContents&)const=default;
};
enum class ProfileHighlightShape {rounded,cutCorner,ellipse};
// One HUDControlHighlightLayer: a tint (accent .30, opacity 0/.62/1) and a rim
// (accent stroke .9, rest .28 when framed). Hit uses its control path; the
// smallest enabled highlight under the pointer wins, exactly like the source.
struct ProfileHighlight {
    core::Rect rect;ProfileHighlightShape shape{ProfileHighlightShape::rounded};bool framed{},enabled{true};
    std::string tint,rim; // surface IDs in their part
    bool operator==(const ProfileHighlight&)const=default;
};
enum class ProfilePartKind {fields,toolbar,popover};
struct ProfileSurface {
    std::string id;core::Rect frame;
    std::optional<std::size_t>highlight;bool rim{};
    // Source animateUpdate target (value text, date caption, work hours, portrait).
    std::string animation;
    bool operator==(const ProfileSurface&)const=default;
};
struct ProfileArtworkPart {ehud::data::Json layers;std::vector<ProfileSurface>surfaces;bool operator==(const ProfileArtworkPart&)const=default;};
// CAGradientLayer whose mask is a second CAGradientLayer. The shared rasterizer
// supports only filled-shape masks, so these stay numeric descriptors for the
// native plane renderer (one color texture plus one alpha mask texture).
struct ProfileGradient {
    std::vector<ProfileColor>colors;std::vector<double>locations;core::Point start{.5,0},end{.5,1};
    bool operator==(const ProfileGradient&)const=default;
};
struct ProfileMaskedGradient {
    core::Rect frame;ProfileGradient fill;ProfileGradient mask;
    bool operator==(const ProfileMaskedGradient&)const=default;
};
// profile.background: photo at frame with contentsRect, horizontal fade whose
// own mask is a vertical fade, and a horizontal shade child masked by both.
struct ProfileBackdrop {
    core::Rect frame;core::Rect contentsRect{0,0,1,1};bool photo{};
    ProfileGradient horizontal,vertical,shade;double shadeOpacity{1};
    bool operator==(const ProfileBackdrop&)const=default;
};
struct ProfileArtwork {
    ProfileArtworkPart fields,toolbar,popover;
    double fieldsOpacity{1};                     // isTextHidden ? 0 : 1
    std::optional<ProfileMaskedGradient>contrast; // first fields leaf when a backdrop exists
    ProfileBackdrop backdrop;
    std::vector<ProfileHighlight>highlights;
    std::array<double,4>accent{};
    bool operator==(const ProfileArtwork&)const=default;
};
// PersonalProfileCanvas.refreshFromStore update animations between two
// committed snapshots (only while active and not dragging): value roles
// ("value:<field>", "name", "dateCaption", "portrait") plus whole-part flags.
struct ProfileUpdateAnimation {
    std::vector<std::string>roles;bool fields{},toolbar{},backdrop{};
    bool empty()const noexcept{return roles.empty()&&!fields&&!toolbar&&!backdrop;}
    bool operator==(const ProfileUpdateAnimation&)const=default;
};
ProfileUpdateAnimation profileUpdateAnimation(const ProfileState&,const PersonalProfile&before,const PersonalProfile&after);
// Straight sRGB RGBA8 (top-left rows) sampled at pixel centers along the axis.
struct ProfileBitmap {unsigned width{},height{};std::vector<std::uint8_t>rgba;bool operator==(const ProfileBitmap&)const=default;};
ProfileBitmap profileGradientBitmap(const ProfileGradient&,unsigned width,unsigned height);
// White coverage whose alpha is the product of one or two gradient alphas
// (a CAGradientLayer mask, optionally masked by another gradient).
ProfileBitmap profileFadeBitmap(const ProfileGradient&,const ProfileGradient*second,unsigned width,unsigned height);
// Content events only (profile/state/language/appearance/image revisions). No
// raster, measurement or allocation happens on pointer or tilt frames.
ProfileArtwork prepareProfileArtwork(const ProfileState&,const ProfileAppearance&,const ProfileContents& = {});
// HUDControlHighlightLayer.update hit rule in module-local points.
std::optional<std::size_t>profileHighlightAt(const ProfileArtwork&,core::Point)noexcept;
bool profileHighlightContains(const ProfileHighlight&,core::Point)noexcept;
}
