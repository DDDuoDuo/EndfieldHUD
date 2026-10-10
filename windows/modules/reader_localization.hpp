#pragma once
// Display-time Reader localization. The Mac Reader captions and the
// ReaderError.errorDescription pairs (Sources/ReaderCanvas.swift,
// ReaderInteraction.swift, ReaderZIP.swift) resolve through the shared
// five-language catalog exactly as L10n.text does
// (tools/reader_localization_reference.py exports the oracle). Three captions
// have no Mac source because they name Windows parts: the File Explorer
// chooser that replaces "Choose in Finder" (the same five rows as Media
// Assembly) and the shared picker/import notices of the Windows host.
#include "modules/reader_model.hpp"
#include "modules/reader_presentation.hpp"
#include "core/localization.hpp"
#include <exception>
#include <string>

namespace endfield::modules {
inline constexpr core::TranslationEntry readerWindowsChooseFile{
    "Choose in File Explorer", "从文件资源管理器选择", "從檔案總管選擇", "エクスプローラーで選択", "파일 탐색기에서 선택"};
inline constexpr core::TranslationEntry readerWindowsPickerBusy{
    "File picker is busy", "文件选择器正忙", "檔案選擇器忙碌中", "ファイル選択画面は使用中です", "파일 선택기가 사용 중입니다"};
inline constexpr core::TranslationEntry readerWindowsOpenFailed{
    "Unable to open selected files", "无法打开所选文件", "無法開啟所選檔案", "選択したファイルを開けません", "선택한 파일을 열 수 없습니다"};
// One row in a resolved language (System renders English, as the catalog does).
std::string readerTranslation(const core::TranslationEntry&, core::Language);
// Every Reader caption in one resolved UI language, with that language
// recorded for display-time error rendering.
ReaderStrings readerStrings(core::Language);
// The Mac source pair of a typed ReaderError (English equals ReaderError::what()).
struct ReaderErrorText { std::string_view english, simplified; };
ReaderErrorText readerErrorText(ReaderErrorCode) noexcept;
// ReaderError.errorDescription in the given language.
std::string readerErrorMessage(ReaderErrorCode, core::Language);
// Import failure text: a typed ReaderError renders its source description
// (ReaderController.fail), anything else the Windows open notice.
std::string readerImportFailure(std::exception_ptr, core::Language);
} // namespace endfield::modules
