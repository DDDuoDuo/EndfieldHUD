#pragma once
#include "modules/storage_presentation.hpp"
#include <filesystem>
#include <string_view>
#ifdef _WIN32
#include "tools/storage_preview.hpp"
#endif
namespace endfield::tools {
inline constexpr std::string_view storageSourcePathsSHA256="46d01b3723084d808d374a7bf5e261a90c6e06e0213d7a108352d52c47af76d4";
// One 4 KiB packaged source resource, not the detached test oracle. Retain the
// returned paths with the module; neither rendering nor input reads this file.
modules::StorageSourcePaths loadStorageSourcePaths(const std::filesystem::path& resourceRoot);
#ifdef _WIN32
// Uses the existing app utility queue and OverlayHost clock epoch. Construction
// validates packaged geometry only: no capacity query, known-folder resolution
// or traversal. Capacity is requested only while visible on the source 60s
// schedule; accepted work may finish after hide. Details remain an explicit
// model operation, with no added button.
// openSettings is caller-owned platform/UI action; no settings window opens here.
// Custom accents require the caller's source-calibrated availableColor.
StoragePreviewOptions makeStoragePreviewOptions(const std::filesystem::path& resourceRoot,
    app::UtilityExecutor&,std::function<void()> openSettings,modules::StorageAppearance={});
#endif
}
