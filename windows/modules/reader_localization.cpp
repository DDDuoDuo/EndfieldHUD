#include "modules/reader_localization.hpp"

namespace endfield::modules {
std::string readerTranslation(const core::TranslationEntry& e, core::Language language) {
    switch (language) {
    case core::Language::simplifiedChinese: return std::string(e.simplifiedChinese);
    case core::Language::traditionalChinese: return std::string(e.traditionalChinese);
    case core::Language::japanese: return std::string(e.japanese);
    case core::Language::korean: return std::string(e.korean);
    default: return std::string(e.english);
    }
}

ReaderStrings readerStrings(core::Language language) {
    ReaderStrings s;
    const auto cn = ReaderStrings::simplifiedChinese();
#define READER_STRING(n) s.n = core::localized(s.n, cn.n, language)
    READER_STRING(open); READER_STRING(library); READER_STRING(reading); READER_STRING(bookmarks);
    READER_STRING(title); READER_STRING(empty); READER_STRING(loading);
    READER_STRING(chooseShelf); READER_STRING(shelfTitle);
    READER_STRING(font); READER_STRING(fontSize); READER_STRING(lineSpacing); READER_STRING(margin);
    READER_STRING(horizontal); READER_STRING(vertical);
#undef READER_STRING
    s.chooseFile = readerTranslation(readerWindowsChooseFile, language);
    s.language = language;
    return s;
}

// Sources/ReaderZIP.swift ReaderError.errorDescription.
ReaderErrorText readerErrorText(ReaderErrorCode code) noexcept {
    switch (code) {
    case ReaderErrorCode::unsupported: return {"Choose a PDF, EPUB or TXT book.", "请选择 PDF、EPUB 或 TXT 书籍。"};
    case ReaderErrorCode::invalidArchive: return {"This EPUB archive is damaged or unsafe.", "此 EPUB 压缩包已损坏或不安全。"};
    case ReaderErrorCode::tooLarge: return {"This book exceeds the reader's safety limits.", "此书籍超过阅读器的安全限制。"};
    case ReaderErrorCode::unavailable: return {"The original book is unavailable. Choose it again.", "原始书籍不可用，请重新选择。"};
    case ReaderErrorCode::encrypted: return {"Encrypted books are not supported.", "暂不支持加密书籍。"};
    case ReaderErrorCode::changedOnDisk: return {"The reader library changed on disk. Restart the app before saving.", "阅读器书库已在磁盘上更改，请重新启动应用后保存。"};
    case ReaderErrorCode::invalidBook: break;
    }
    return {"This book could not be read.", "无法读取此书籍。"};
}

std::string readerErrorMessage(ReaderErrorCode code, core::Language language) {
    const auto text = readerErrorText(code);
    return core::localized(text.english, text.simplified, language);
}

std::string readerImportFailure(std::exception_ptr error, core::Language language) {
    try {
        if (error) std::rethrow_exception(error);
    } catch (const ReaderError& e) {
        return readerErrorMessage(e.code(), language);
    } catch (...) {
    }
    return readerTranslation(readerWindowsOpenFailed, language);
}
} // namespace endfield::modules
