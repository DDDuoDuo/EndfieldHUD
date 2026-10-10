#pragma once
// Test-only Notes area (cmake/modules/notes.cmake registers it with the
// "|coverage" scope: hidden coverage only, never EndfieldHUD.exe). Notes,
// Archive and Projection are built-in owners of the Application; this area
// exercises the Notes long-document path inside the real application process:
// each coverage cycle opens a long synthetic note in a NativeNotesWorkspace on
// the shared rasterizer and HUD window (borrowing Notes' text services), types
// through the paragraph-chunked editor with bounded per-keystroke layout work,
// commits through NotesState and releases everything, leaving the shared
// raster cache exactly as it found it. In-memory records only.
#include "app/application.hpp"
#include <memory>

namespace endfield::notes {
std::unique_ptr<app::ModuleOwner> notesLongDocumentCoverage(const app::ApplicationModuleContext&);
}
