#pragma once
#include "tools/archive_preview.hpp"
#include "core/localization.hpp"
#include "native/archive_dates.hpp"

namespace endfield::tools {
struct ArchivePreviewStrings {
    modules::ArchiveStrings view;
    modules::NotesControlsStrings menus;
    std::string categoryNamePlaceholder;
};
// Exact Archive/Notes-menu catalog pairs. The native file-picker label replaces
// Finder with Explorer; document/category names and error payloads are untouched.
// Use this on language events with ArchivePreview::setStrings, without reloading
// assets, fonts or the date formatter. Pass an already resolved language.
ArchivePreviewStrings archivePreviewStrings(core::Language);
#ifdef _WIN32
// resourceRoot is the packaged windows/resources directory, never a test oracle.
// Borrows rasterizer on its creating thread. Format/parse share one retained
// formatter; optionally keep the supplied formatter to handle OS time-zone
// notifications with refreshSystemTimeZone(). No provider, repository or worker.
ArchivePreviewOptions makeArchivePreviewOptions(const std::filesystem::path& resourceRoot,
    native::LayerRasterizer&,core::Language,modules::ArchiveAppearance,
    std::shared_ptr<native::ArchiveDateFormatter> formatter={});
#endif
}
