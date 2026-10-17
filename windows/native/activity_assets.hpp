#pragma once
#include "core/subsection_mask.hpp"
#include <filesystem>
namespace endfield::native {
// Explicit application resource root, never a source/test fixture at runtime.
// The root is validated only: the sort reveal is the authored shutter
// (native/activity_scene.hpp activitySortShutter), so sort-mask.bin is neither
// read nor required and sortSamples() is always empty (kept so existing
// callers that forward it still compile).
class NativeActivityAssets final {
public:
    explicit NativeActivityAssets(const std::filesystem::path&activityResourceRoot);
    std::shared_ptr<const core::SubsectionMaskSampler>sortSamples()const noexcept{return {};}
};
}
