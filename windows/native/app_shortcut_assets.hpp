#pragma once
#include "modules/app_shortcut_presentation.hpp"
#include <filesystem>
#include "core/subsection_mask.hpp"
namespace endfield::native {
// Explicit installed resource directory only. Owns the compact original vector
// catalog; validates nine unchanged bundled PNGs once before any publication.
// Caller uses root() as LayerRasterOptions.assetRoot. OS application thumbnails
// remain a separate injected LayerImageSource, never a fallback file scan.
class NativeAppShortcutAssets final {
public:
    explicit NativeAppShortcutAssets(std::filesystem::path root);
    const std::filesystem::path&root()const noexcept{return root_;}
    const modules::AppShortcutArtwork&artwork()const noexcept{return artwork_;}
    std::shared_ptr<const core::SubsectionMaskSampler>arrival()const noexcept{return arrival_;}
    std::shared_ptr<const core::SubsectionMaskSampler>departure()const noexcept{return departure_;}
    static constexpr std::string_view arrivalSHA256="cf4e9ac167390191d7129d8877a6dfe65e56149ec3123e7c11ff7bcc431242d2";
    static constexpr std::string_view departureSHA256="24b31f529b1dc5bdf03ca884a738bcee50b465f4b63fc91a4f49f3bc92bcf167";
    static constexpr std::string_view artworkSHA256="4400e1eed35bf3012269b5cda92797fbc505b2ddb93928a5aaf484c659d225b8";
private:
    std::filesystem::path root_;modules::AppShortcutArtwork artwork_;std::shared_ptr<const core::SubsectionMaskSampler>arrival_,departure_;
};
}
