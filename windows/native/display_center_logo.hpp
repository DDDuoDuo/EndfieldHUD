#pragma once
// Windows decode and import of the center logo (Mac HUDCenterLogo.image and
// HUDCenterLogoStore). WIC on the calling thread (COM initialized per call);
// run on the shared UtilityExecutor, never on the UI thread or per frame.
// The artwork and the desktop binding: modules/display_center_logo.hpp and
// app/application.cpp (HUDSourceWatchView.setDesktopCenterLogo).
#include "modules/display_center_logo.hpp"
#ifdef _WIN32
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace endfield::native {
// A preset wordmark of the packaged resources/center-logos folder (copied
// from Resources/AppIconSources/CenterLogos), verified against its
// SOURCE.json bundledSHA256 before decoding. Throws on a missing or damaged
// file; the caller keeps the packet logo. The thumbnail is bounded to 768 px.
modules::CenterLogoBitmap decodeCenterLogoPreset(const std::filesystem::path& presetDirectory, modules::CenterLogoChoice);
// HUDCenterLogoStore.image(revision:): <directory>\<REVISION>.png, a regular
// file of 1 byte...4 MiB whose image is at most 768 px per side; nullopt
// otherwise (missing, replaced or damaged files show the packet logo).
std::optional<modules::CenterLogoBitmap> decodeCenterLogoRevision(const std::filesystem::path& centerLogoDirectory, std::string_view revision);
// HUDCenterLogoStore.importImage(from:): a regular file of at most 32 MB
// whose first frame WIC decodes, at most 16384 px per side and 64
// megapixels; oriented (EXIF), downsampled to at most 768 px, normalized to
// straight sRGB RGBA8 and written atomically as <NEW UUID>.png in
// `centerLogoDirectory` (created when missing). Returns the new revision.
// Nothing is deleted. Throws CenterLogoImportError.
struct CenterLogoImportError : std::runtime_error {
    CenterLogoImportError(modules::CenterLogoError value, std::string detailText = {})
        : std::runtime_error("Center logo import failed"), error(value), detail(std::move(detailText)) {}
    modules::CenterLogoError error;
    std::string detail; // persistence only: the Windows error text (no path)
};
std::string importCenterLogo(const std::filesystem::path& source, const std::filesystem::path& centerLogoDirectory);
} // namespace endfield::native
#endif
