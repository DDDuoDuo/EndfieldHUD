#pragma once
// Original application icons for the "App icon" preset (Mac
// AppShortcutStore.icon(for:), NSWorkspace.icon(forFile:)).
//
// One bounded extraction per saved or drafted Windows target, only on explicit
// content events (load, save, relink, a new draft, a newly visible row), on
// the shared UtilityExecutor: never a scan, poll or process observer. The
// Windows extractor reads the icon resource the target names - an .exe's own
// icon, or a .lnk's recorded icon location (else its recorded .exe target) -
// with SHDefExtractIconW, which loads no third-party Shell icon handler, so a
// hung extension cannot hold the shared executor. A packaged application's
// logo comes from its package through AppInfo (DisplayInfo.GetLogo), bounded
// in time and size, decoded only by the built-in PNG/JPEG WIC decoders (the
// formats a package manifest's logos use; no third-party codec is loaded).
// Imported Mac records return nothing: the canvas keeps the source grid
// fallback.
#include "native/app_shortcut_service.hpp"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace endfield::native {
inline constexpr unsigned appShortcutIconPixels = 96;
// Tight straight RGBA8 sRGB, `pixels` x `pixels`, the icon centered.
struct AppShortcutIconPixels {
    unsigned width{}, height{};
    std::vector<std::uint8_t> straightRGBA;
};
// Worker-only. Empty: the target has no extractable icon.
using AppShortcutIconExtractor = std::function<std::optional<AppShortcutIconPixels>(const AppShortcutSelection&, unsigned pixels)>;
// Worker-only: the encoded logo (PNG or JPEG, as stored in the package) of the
// packaged application with exactly this AppUserModelID, chosen for about
// `pixels` square; empty when it is not installed, has no logo or exceeds the
// bounds. Tests and hidden coverage inject synthetic logos here, so no
// installed application is read.
using AppShortcutPackagedLogo = std::function<std::optional<std::vector<std::uint8_t>>(std::string_view appUserModelID, unsigned pixels)>;
inline constexpr std::size_t appShortcutLogoMaximumBytes = 4 * 1024 * 1024;
inline constexpr unsigned appShortcutLogoMaximumDimension = 2048;
// The saved Windows target of a locator ({referencePlatform:"windows",
// windowsTarget}); empty for imported Mac locators or invalid records.
std::optional<AppShortcutSelection> appShortcutIconSelection(const modules::ShortcutJson& locator) noexcept;
#ifdef _WIN32
// One encoded PNG/JPEG logo (first frame, at most 2048 pixels a side and
// 4 MiB) fitted into a centred `pixels` square of straight sRGB RGBA, scaled
// in premultiplied alpha so transparent edges keep their colour. Empty for
// any other or undecodable input.
std::optional<AppShortcutIconPixels> decodeAppShortcutLogo(std::span<const std::uint8_t> encoded, unsigned pixels);
// AppInfo.GetFromAppUserModelId(id).DisplayInfo.GetLogo(pixels x pixels),
// read through its stream with each asynchronous step bounded to 2 s, on the
// calling multithreaded worker; refused on a single-threaded apartment (it
// never blocks one). Not used by tests or hidden coverage.
AppShortcutPackagedLogo windowsPackagedAppLogo();
// .exe/.lnk icon resources, and packaged logos from `logos` (none: the grid).
AppShortcutIconExtractor windowsAppShortcutIconExtractor(AppShortcutPackagedLogo logos);
AppShortcutIconExtractor windowsAppShortcutIconExtractor(); // with windowsPackagedAppLogo()
#endif
} // namespace endfield::native
