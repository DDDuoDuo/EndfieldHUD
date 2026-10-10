#pragma once
#include "modules/profile_artwork.hpp"
#include <optional>

namespace endfield::modules {
// HUDSourceWatchView.setDesktopProfile textures for the bottom-left card, in
// the source renderer's texturePixels layout (straight RGBA, bottom-origin
// rows), ready for desktop.profile.avatar/background/hover replacement.
// Generate only when IdCardBinding's avatar/background/hover revision moves;
// tilt, ambient motion and Work Mode ticks never reach this class.
class IdCardArtwork final {
public:
    explicit IdCardArtwork(ProfileImage card); // straight business_card_topic_normal_1 sprite (530x204)
    // Themed source card; with a photo, its 412x158 pt crop at 2x is dimmed
    // inside the panel. No photo with the HUD accent equals the packet texture.
    ProfileImage background(std::array<double,3>accent,const ProfileImage*photo,double zoom,core::Point offset)const;
    // Hover plate with the Windows outline correction (alpha *= card alpha).
    ProfileImage hover(std::array<double,3>accent)const;
    // 136x136 pt portrait at 2x from the native avatar and its EXIF orientation.
    // Without an avatar the packet's original silhouette texture stays bound.
    static ProfileImage avatar(const ProfileImage&avatar,int orientation,double zoom,core::Point offset);
    const ProfileImage&card()const noexcept{return card_;}
private:
    ProfileImage card_;
};
}
