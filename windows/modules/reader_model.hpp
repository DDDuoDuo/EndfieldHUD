#pragma once
#include "core/data/data_store.hpp"
#include <span>

namespace endfield::modules {
using ReaderJson=ehud::data::Json;
inline constexpr std::size_t readerMaximumBooks=50,readerMaximumBookmarks=128;
inline constexpr std::size_t readerMaximumLibraryBytes=4*1024*1024;
enum class ReaderErrorCode {unsupported,invalidArchive,tooLarge,invalidBook,unavailable,encrypted,changedOnDisk};
class ReaderError final:public std::runtime_error {
public:explicit ReaderError(ReaderErrorCode);ReaderErrorCode code()const noexcept{return code_;}
private:ReaderErrorCode code_;
};
struct ReaderLocation {
    std::int64_t section{},block{},character{};
    ReaderJson originalFields{ReaderJson::Object{}};
    bool valid()const noexcept;
    bool operator==(const ReaderLocation&other)const noexcept{return section==other.section&&block==other.block&&character==other.character;}
};
struct ReaderPreferences {
    // Exact Mac v1 defaults remain the import/schema contract. A newly created
    // shipping Windows library can explicitly pass defaults("Noto Sans SC") or
    // defaults("Noto Sans KR"); never replace a saved user's selected face.
    std::string fontName{"Georgia"};double fontSize{10},lineSpacing{2},margin{16};
    bool rightToLeft{},continuous{true}; // original keys, not renamed on disk
    ReaderJson originalFields{ReaderJson::Object{}};
    // Preserve imported decimal tokens until their Double value changes.
    ReaderJson originalNumbers{ReaderJson::Object{}};
    bool vertical()const noexcept{return continuous;}
    void setVertical(bool value)noexcept{continuous=value;rightToLeft=false;}
    bool valid()const noexcept;
    static ReaderPreferences defaults(std::string fontName="Georgia");
    bool operator==(const ReaderPreferences&)const;
};
struct ReaderBookmark {
    std::string id;ReaderLocation location;double progress{};
    ReaderJson originalFields{ReaderJson::Object{}};
    ReaderJson originalNumbers{ReaderJson::Object{}};
    bool operator==(const ReaderBookmark&)const;
};
enum class ReaderReferencePlatform {macOS,windows};
struct ReaderBook {
    std::string id,title;
    ReaderReferencePlatform platform{ReaderReferencePlatform::macOS};
    std::string bookmarkBase64,path;bool scoped{};
    std::string windowsPath;
    ReaderLocation location;double progress{};std::vector<ReaderBookmark>bookmarks;
    ReaderJson originalFields{ReaderJson::Object{}};
    ReaderJson originalNumbers{ReaderJson::Object{}};
    bool operator==(const ReaderBook&)const;
};
struct ReaderLibrary {
    std::vector<ReaderBook>books;std::optional<std::string>selected;
    ReaderPreferences preferences;
    ReaderJson originalFields{ReaderJson::Object{}};
    bool operator==(const ReaderLibrary&)const=default;
};
ReaderLocation decodeReaderLocation(const ReaderJson&);
ReaderJson encodeReaderLocation(const ReaderLocation&);
ReaderLibrary decodeReaderLibrary(const ReaderJson&);
ReaderJson encodeReaderLibrary(const ReaderLibrary&);
void validateReaderLibrary(const ReaderLibrary&);
// Additive native locator under v1: referencePlatform="windows", windowsPath.
// It has NO fabricated Mac bookmark/path/scoped keys. Original Mac references
// remain byte-for-byte opaque and require explicit relinking on Windows.
ReaderBook windowsReaderReference(std::string id,std::string path,std::string title);
std::string_view readerReferencePath(const ReaderBook&)noexcept;
bool readerSupportedExtension(std::string_view extension)noexcept;
} // namespace endfield::modules
