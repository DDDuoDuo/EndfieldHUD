#pragma once
// Settings > Display > Center logo (Mac HUDCenterLogo, HUDCenterLogoStore,
// HUDCenterLogoPresentation and HUDSourceWatchView.setDesktopCenterLogo).
// Portable, pure artwork: no file, decoder, clock or renderer. The Windows
// decode/import and the desktop texture binding live in the host
// (native/display_center_logo.hpp, app/application.cpp).
#include "core/localization.hpp"
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace endfield::modules {
// HUDCenterLogoPresentation.defaultHeight: the visible height of the source
// EndfieldText node; the width follows the artwork's proportions.
inline constexpr double centerLogoDefaultHeight = 65;
// HUDCenterLogoStore bounds.
inline constexpr std::size_t centerLogoMaximumEncodedBytes = 32u * 1024u * 1024u; // import source file
inline constexpr std::uint64_t centerLogoMaximumPixels = 64000000;                 // import source pixels
inline constexpr unsigned centerLogoMaximumSourceDimension = 16384;               // import source side
inline constexpr unsigned centerLogoMaximumDimension = 768;                       // stored/decoded side
inline constexpr std::size_t centerLogoMaximumManagedBytes = 4u * 1024u * 1024u;   // stored PNG read bound

// HUDCenterLogo raw values in the preference ("centerLogo").
enum class CenterLogoChoice : std::uint8_t { endfield, rhodesIsland, babel, rhineLab, custom };
std::optional<CenterLogoChoice> centerLogoChoice(std::string_view) noexcept;
std::string_view centerLogoIdentifier(CenterLogoChoice) noexcept;
// HUDCenterLogo.title (Sources/HUDCenterLogo.swift:9-16): the preset's
// HUDApplicationIcon title ("Endfield" in every language) or Custom / 自定.
std::string centerLogoTitle(CenterLogoChoice, core::Language);
// The bundled preset wordmark (AppIconSources/CenterLogos/<id>.png), empty for
// endfield (the packet artwork) and custom (the managed store).
std::string_view centerLogoPresetFile(CenterLogoChoice) noexcept;
// HUDCenterLogoStore.validRevision: UUID(uuidString:)?.uuidString, the
// canonical uppercase form; nullopt when it is not a UUID.
std::optional<std::string> centerLogoRevision(std::string_view) noexcept;

// Straight sRGB RGBA8 with top-left rows.
struct CenterLogoBitmap {
    unsigned width{}, height{};
    std::vector<std::uint8_t> rgba;
    bool operator==(const CenterLogoBitmap&) const = default;
};
struct CenterLogoBounds {
    unsigned x{}, y{}, width{}, height{}; // top-left pixel rect
    bool operator==(const CenterLogoBounds&) const = default;
};
// HUDCenterLogoPresentation.visibleArtwork: the pixels whose alpha exceeds 8;
// nullopt for a fully transparent image.
std::optional<CenterLogoBounds> centerLogoVisibleBounds(const CenterLogoBitmap&);
// The shadow blur HUDCenterLogoPresentation.image uses for a cropped height.
double centerLogoShadowBlur(unsigned croppedHeight) noexcept;
// The texture the source binds as "desktop.center-logo": the cropped artwork
// drawn with a white 0.7 shadow (offset zero, blur b = height * 3.5 / 65;
// Core Graphics' blur is matched by a Gaussian of variance (b / 2)^2 + 1/2,
// within a few premultiplied levels of the Mac oracle) over itself in a
// context of the cropped size, then HUDSourceProfileArtwork.texturePixels:
// straight RGBA8 with bottom-origin rows. displayWidth x displayHeight is
// HUDCenterLogoPresentation.displaySize. nullopt for a transparent image.
struct CenterLogoArtwork {
    unsigned width{}, height{};
    std::vector<std::uint8_t> texturePixels;
    double displayWidth{}, displayHeight{};
};
std::optional<CenterLogoArtwork> centerLogoArtwork(const CenterLogoBitmap&);
// Settings > Display > Center logo chooser (HUDSettingsCanvas.logoArtwork):
// HUDMenu.Icon.raster of the presented artwork, i.e. the logo as the centre
// shows it aspect-fit into `width` x `height` box px at max(1, min(8,
// ceil(scale))) pixels per box px, straight sRGB RGBA8 with top-left rows.
// `maximumSide` bounds the longer pixel side (the memory image limit; the
// raster is then a little softer than the Mac's). Resampled with a box filter
// on premultiplied pixels (the Mac's high-quality CGContext draw).
CenterLogoBitmap centerLogoChooserImage(const CenterLogoArtwork&, double width, double height, double scale, unsigned maximumSide);
// HUDSettingsCanvas.endfieldWordmark: the default ENDFIELD INDUSTRIES
// lettering is the band y 142...225 (in a 430-unit-wide source, rounded
// outward) of Resources/EndfieldIndustriesSource.png.
CenterLogoBitmap centerLogoWordmark(const CenterLogoBitmap& source);
// HUDCenterLogoError.errorDescription.
enum class CenterLogoError : std::uint8_t { invalidImage, imageTooLarge, imageDimensionsTooLarge, persistence };
std::string centerLogoErrorText(CenterLogoError, core::Language, std::string_view persistenceDetail = {});
} // namespace endfield::modules
