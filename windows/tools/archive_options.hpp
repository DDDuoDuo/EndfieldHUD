#pragma once
// The media error text shared by Notes, Archive and Projection (their 1.3.0
// canvases take every other string from the catalog; the File Explorer
// chooser row is core::chooseInFileExplorer). Windows only.
#include "core/localization.hpp"
#include <string>
#include <windows.h>

namespace endfield::tools {
// Source NotesMediaError description (Sources/NotesMedia.swift) for a failed
// decode, import inspection or playback HRESULT, classified by
// native::notesMediaError, in the given resolved language. Shared by Notes,
// Archive attachments and Projection media.
std::string notesMediaErrorDescription(HRESULT,core::Language);
}
