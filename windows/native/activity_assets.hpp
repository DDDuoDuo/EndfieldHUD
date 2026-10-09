#pragma once
#include "core/subsection_mask.hpp"
#include <filesystem>
namespace endfield::native {
// Explicit application resource root, never a source/test fixture at runtime.
class NativeActivityAssets final {
public:
    explicit NativeActivityAssets(const std::filesystem::path&activityResourceRoot);
    std::shared_ptr<const core::SubsectionMaskSampler>sortSamples()const noexcept{return samples_;}
    static constexpr std::string_view maskSHA256="0f591e33e4f0448f7545fbdc080f57a1cf49388aead155009503feea3ab30734";
private:std::shared_ptr<const core::SubsectionMaskSampler>samples_;
};
}
