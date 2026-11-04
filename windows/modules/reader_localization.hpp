#pragma once
// Display-time Reader localization: the ReaderError.errorDescription pairs
// of Sources/ReaderZIP.swift (Mac 1.3.0) resolve through the shared
// five-language catalog exactly as L10n.text does
// (tools/reader_localization_reference.py exports the oracle). Every other
// Reader caption is a catalog pair the canvas renders itself
// (modules/reader_canvas.cpp, core::localized). The Windows host's shared
// picker / import notices (core::filePickerBusy, core::selectedFilesUnopened)
// have no Mac source because they name Windows parts.
#include "modules/reader_model.hpp"
#include "core/localization.hpp"
#include <exception>
#include <string>

namespace endfield::modules {
// One row in a resolved language (System renders English, as the catalog does).
std::string readerTranslation(const core::TranslationEntry&, core::Language);
// The Mac source pair of a typed ReaderError (English equals ReaderError::what()).
struct ReaderErrorText { std::string_view english, simplified; };
ReaderErrorText readerErrorText(ReaderErrorCode) noexcept;
// ReaderError.errorDescription in the given language.
std::string readerErrorMessage(ReaderErrorCode, core::Language);
// Import failure text: a typed ReaderError renders its source description
// (ReaderController.fail), anything else the Windows open notice.
std::string readerImportFailure(std::exception_ptr, core::Language);
} // namespace endfield::modules
