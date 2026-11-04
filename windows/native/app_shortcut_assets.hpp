#pragma once
// The + Add App tile artwork as packaged (resources/app-shortcut, staged as
// common/app-shortcut): artwork.json version 2 and the Mac's own preset
// rasters (presets/<preset>.png, tools/app_shortcut_reference.sh ->
// tools/app_shortcut_artwork.py). Explicit installed resource directory only:
// the catalog's SHA-256 is pinned here and every PNG is checked against its
// row once, before any publication. The owner passes root() as the design
// owner's LayerRasterOptions.assetRoot; the canvas names the PNGs as pinned
// {asset, sha256} images. OS application icons stay a separate
// LayerImageSource, never a fallback file scan.
#include "modules/app_shortcut_artwork.hpp"
#include <filesystem>
namespace endfield::native {
class NativeAppShortcutAssets final {
public:
    explicit NativeAppShortcutAssets(std::filesystem::path root);
    const std::filesystem::path&root()const noexcept{return root_;}
    const modules::AppShortcutArtwork&artwork()const noexcept{return artwork_;}
    static constexpr std::string_view artworkSHA256="4a7d9844fcb4ea04ccce12d292aacd41a996294021f655600fceb4ae57f3933d";
private:
    std::filesystem::path root_;modules::AppShortcutArtwork artwork_;
};
}
