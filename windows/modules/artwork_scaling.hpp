#pragma once
// Shared CPU artwork scaling: the Lanczos resampler the Mac shares between
// the portrait artwork and the Now Playing cover decode since 45f75dc
// (HUDPortraitArtwork.scaledImage, Sources/HUDPortraitArtwork.swift:98-111),
// the one CPU Gaussian, and the 8-bit image both use. Portable, pure CPU
// work meant for the shared utility worker: no file, decoder, clock or
// renderer. Callers: modules/profile_artwork (portraits),
// modules/display_center_logo (the logo shadow) and the Now Playing small
// cover and its backdrop (native/now_playing_image, nowPlayingSmallCover).
#include "core/scene.hpp"
#include <cstdint>
#include <functional>
#include <vector>

namespace endfield::modules {
// sRGB RGBA8, top-left rows unless a function says otherwise. Core Graphics
// bitmap contexts store premultiplied color; CGImage sources may be straight.
struct ProfileImage {
    unsigned width{},height{};std::vector<std::uint8_t>rgba;bool premultiplied{};
    bool operator==(const ProfileImage&)const=default;
};
// The sRGB transfer function and its inverse (values clamped to 0...1).
double artworkLinear(double encoded)noexcept;
double artworkEncoded(double linear)noexcept;
// A normalized 1-D Gaussian (standard deviation sigma, radius ceil(3 sigma));
// {1} for a sigma at or below 1e-6.
std::vector<double>artworkGaussian(double sigma);
// Samples outside the region: `clear` (Core Image's crop: grid samples whose
// centres fall outside the region add nothing, so the edges fade) or
// `clamped` (a source clampedToExtent: taps beyond the region read the
// picture around it, and beyond the picture its nearest edge pixel, so the
// region's edges stay opaque).
enum class ArtworkEdges:std::uint8_t {clear,clamped};
// A step after scaling (Core Image's `finish`, a blur say) over premultiplied
// linear-light RGBA doubles (w x h, row-major). The scaler renders `margin`
// extra pixels on every side for it and then crops them away, so the step
// reads real neighbours at the output's edges.
struct ArtworkFinish {unsigned margin{};std::function<void(std::vector<double>&rgba,unsigned width,unsigned height)>apply;};
// HUDPortraitArtwork.renderedImage's scale: `region` of the EXIF-oriented
// source (orientation 1...8, in oriented pixels) stretched to a width x height
// bitmap (each axis its own factor, width / region.width and
// height / region.height): a
// bilinear sample of the source at region + k + 0.5, then a separable Lanczos
// over that grid in premultiplied linear light (measured against Core Image:
// three lobes when reducing an axis, two when enlarging it; an axis at
// factor 1 is copied, as the Mac draws it without the filter), the optional
// finish, and premultiplied sRGB out. Memory is bounded by the filter's
// support (a sliding window of rows), never by the source or region size.
ProfileImage artworkResampled(const ProfileImage&source,int orientation,core::Rect region,unsigned width,unsigned height,
    ArtworkEdges=ArtworkEdges::clear,const ArtworkFinish* =nullptr);
// HUDPortraitArtwork.scaledImage (Sources/HUDPortraitArtwork.swift:103-111):
// `region` of an upright source scaled by one `factor` on both axes into a
// max(1, round(region x factor)) bitmap, filtered as artworkResampled does.
// Where rounding leaves the bitmap larger than region x factor, the extra
// column is on the right and the extra row on top (Core Image's bottom-left
// origin), read through `edges`.
ProfileImage artworkScaled(const ProfileImage&source,core::Rect region,double factor,
    ArtworkEdges=ArtworkEdges::clear,const ArtworkFinish* =nullptr);
// CIGaussianBlur (standard deviation `sigma` pixels) then CIExposureAdjust
// (RGB x 2^ev in linear light, alpha unchanged) as a finish with a margin of
// ceil(3 sigma). The blur reads the margin, so a clamped picture keeps its
// edges.
ArtworkFinish artworkBlurAndExposure(double sigma,double ev);
// Unpremultiplied copy (top-left rows kept), e.g. for LayerImageSource.publish.
ProfileImage profileStraight(const ProfileImage&);
}
