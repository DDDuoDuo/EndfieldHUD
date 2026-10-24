#pragma once
// Display-time Reader localization: the ReaderError.errorDescription pairs
// of Sources/ReaderZIP.swift (Mac 1.3.0) resolve through the shared
// five-language catalog exactly as L10n.text does
// (tools/reader_localization_reference.py exports the oracle). Every other
// Reader caption is a catalog pair the canvas renders itself
// (modules/reader_canvas.cpp, core::localized). Two notices have no Mac
// source because they name Windows parts: the shared picker / import
// notices of the Windows host (the same rows as Media Assembly).
#include "modules/reader_model.hpp"
#include "core/localization.hpp"
#include <exception>
#include <string>

namespace endfield::modules {
inline constexpr core::TranslationEntry readerWindowsPickerBusy{
    "File picker is busy", "文件选择器正忙", "檔案選擇器忙碌中", "ファイル選択画面は使用中です", "파일 선택기가 사용 중입니다"};
inline constexpr core::TranslationEntry readerWindowsOpenFailed{
    "Unable to open selected files", "无法打开所选文件", "無法開啟所選檔案", "選択したファイルを開けません", "선택한 파일을 열 수 없습니다"};
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
