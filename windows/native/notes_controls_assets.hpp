#pragma once
#include "native/notes_controls_scene.hpp"
#include <filesystem>

namespace endfield::native {
struct NativeNotesControlsAssetPins {
    // Both are caller-owned build pins, not values read from this same bundle.
    // SHA binds the complete prepared manifest; sourceCommit binds its lineage.
    std::string manifestSHA256,sourceCommit;
};
// Prepared source artwork, not an image/tint service. Reads only an explicit
// absolute package root: one <=64KiB manifest and exactly two <=128KiB PNGs.
// Construction validates SHA/bytes, source pins, exact original dependency
// fields and bounded PNG structure. WIC decoding remains LayerRasterizer's
// existing content-event work; this owner retains no duplicate decoded pixels.
// Current bundle has only source-exported dark/scale-2 Text and TODO variants.
// No full Mac CALayer reference, user stores, window, service, watcher or timer.
class NativeNotesControlsAssets final {
public:
    static constexpr std::size_t maximumManifestBytes=64*1024,maximumRasterBytes=128*1024;
    NativeNotesControlsAssets(std::filesystem::path explicitAbsoluteRoot,NativeNotesControlsAssetPins);
    // Allocation/I/O/hash-free. Exact source dependency coverage is mandatory;
    // empty-image menus return empty, while a wheel or unsupported tint/scale
    // rejects. Returned borrowed bindings survive until this owner is destroyed.
    std::span<const NativeNotesControlsImage> imagesFor(const modules::NotesControls&)const;
    const std::filesystem::path& assetRoot()const noexcept{return root_;}
    const std::string& manifestSHA256()const noexcept{return manifestSHA256_;}
    const std::string& sourceCommit()const noexcept{return sourceCommit_;}
private:
    std::filesystem::path root_;
    std::string manifestSHA256_,sourceCommit_;
    std::array<NativeNotesControlsImage,2> images_;
};
} // namespace endfield::native
