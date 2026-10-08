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
    std::shared_ptr<const core::SubsectionMaskSampler>revealSamples()const noexcept{return mask_;}
    static constexpr std::string_view maskSHA256="270fafc7331eedbe3ffe3a609b8638b88edfff66d7bf289ac610bc323d5c7d60";
private:std::filesystem::path root_;std::shared_ptr<const core::SubsectionMaskSampler>mask_;
};
}
