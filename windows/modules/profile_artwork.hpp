#pragma once
#include "core/scene.hpp"
#include <array>
#include <filesystem>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace endfield::modules {
// sRGB RGBA8, top-left rows unless a function says otherwise. Core Graphics
// bitmap contexts store premultiplied color; CGImage sources may be straight.
struct ProfileImage {
    unsigned width{},height{};std::vector<std::uint8_t>rgba;bool premultiplied{};
    bool operator==(const ProfileImage&)const=default;
};
// Content events only; every function allocates its output once and performs
// no IO. Dimensions are bounded to the source's 16384 px context limit.

// HUDSourceProfileArtwork.backgroundArtwork: decoded BGRA source mip (bottom
// origin rows) -> straight RGBA top-left rows of the integral sprite rect.
ProfileImage profileSpriteFromMip(std::span<const std::uint8_t>bgra,unsigned textureWidth,unsigned textureHeight,core::Rect sprite);
// CGContext.draw of a straight image into a premultipliedLast context.
ProfileImage profilePremultiplied(const ProfileImage&);
// themedBackgroundArtwork: replace the yellow edge chroma by the accent.
ProfileImage idCardThemedArtwork(const ProfileImage&source,std::array<double,3>accent);
// hoverArtwork: accent .38 inside the silhouette, .05 inside the rounded panel.
ProfileImage idCardHoverArtwork(const ProfileImage&source,std::array<double,3>accent);
// compositedBackground: the photo dimmed .48 inside the panel (20,22)-(507,182)
// of the 530x204 sprite, keeping the artwork's alpha silhouette.
ProfileImage idCardCompositedBackground(const ProfileImage&photo,const ProfileImage&artwork);
// texturePixels: straight RGBA with bottom-origin rows, for the source UI
// shader that premultiplies once after sampling.
ProfileImage idCardTexturePixels(const ProfileImage&);
// Windows outline correction applied to a regenerated hover plate exactly as
// SourceScene::maskDesktopProfileHoverOutline: alpha *= background alpha.
void idCardMaskHover(ProfileImage&hoverTexture,const ProfileImage&backgroundTexture);
// HUDPortraitArtwork.renderedImage: EXIF-oriented crop of the native source,
// Lanczos-scaled to ceil(target * clamp(scale,1,8) * 1.35) (<=1024) pixels in
// linear light; samples outside the crop are clear, like Core Image's crop.
ProfileImage profileRenderedImage(const ProfileImage&source,int orientation,core::Point target,double zoom,core::Point offset,double contentsScale);
// A whole image redrawn at another size (CGContext.draw with high
// interpolation), approximated by the same linear-light Lanczos filter.
ProfileImage profileResampled(const ProfileImage&,unsigned width,unsigned height);
// Unpremultiplied copy (top-left rows kept), e.g. for LayerImageSource.publish.
ProfileImage profileStraight(const ProfileImage&);
// windows/resources/profile: the verbatim business card and avatar frame
// mips (catalog digests verified) decoded to their straight source sprites.
struct ProfileSourceArtwork {ProfileImage card,frame;std::string defaultBackgroundSHA256;};
ProfileSourceArtwork loadProfileSourceArtwork(const std::filesystem::path& resourceDirectory);
// HUDPortraitArtwork.frameImage: the selected source frame's premultiplied
// color multiplied by the accent (keyline and shadow stay dark).
ProfileImage profileTintedFrame(const ProfileImage&sourceFrame,std::array<double,3>accent);
}
