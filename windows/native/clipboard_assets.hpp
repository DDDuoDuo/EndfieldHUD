#pragma once
#include "native/clipboard_scene.hpp"
namespace endfield::native {
// Exactly the original 2x ClipboardCanvas icons, including source-in tint.
// Confined caller-supplied resource directory only; never clipboard/user files.
// Other backing scales require additional original prepared variants.
class NativeClipboardAssets final {
public:
    explicit NativeClipboardAssets(std::filesystem::path root);
    const std::filesystem::path&root()const noexcept{return root_;}
    ClipboardImages images(double sourceScale=2)const;
    // Always empty: the rows reveal is the authored shutter
    // (clipboardRevealShutter); reveal-mask.bin is neither read nor required.
    // Kept so existing callers that forward it to the scene still compile.
    std::shared_ptr<const core::SubsectionMaskSampler>revealSamples()const noexcept{return {};}
private:std::filesystem::path root_;
};
}
