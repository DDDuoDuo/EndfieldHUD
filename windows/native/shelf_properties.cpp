#include "native/shelf_properties.hpp"
#include <algorithm>
#include <cstring>
#include <stdexcept>
#ifdef _WIN32
#include "native/file_shelf_files.hpp"
#include <windows.h>
#endif

namespace endfield::native {
#ifdef _WIN32
namespace {
[[noreturn]] void failure(const char*what,DWORD error){throw std::runtime_error(std::string(what)+" ("+std::to_string(error)+")");}
std::wstring wide(std::string_view value){
    if(value.empty()||value.size()>32768)throw std::invalid_argument("Invalid Shelf properties path");
    const auto count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),int(value.size()),nullptr,0);if(count<=0)failure("Cannot decode Shelf properties path",GetLastError());
    std::wstring out(std::size_t(count),L'\0');if(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),int(value.size()),out.data(),count)!=count)failure("Cannot decode Shelf properties path",GetLastError());return out;
}
std::wstring extended(const std::wstring&path){auto out=path.starts_with(L"\\\\")?L"\\\\?\\UNC\\"+path.substr(2):L"\\\\?\\"+path;if(out.size()>=32767)throw std::invalid_argument("Shelf properties path exceeds the extended bound");return out;}
struct Handle{HANDLE value{INVALID_HANDLE_VALUE};~Handle(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);}};
struct Find{HANDLE value{INVALID_HANDLE_VALUE};~Find(){if(value!=INVALID_HANDLE_VALUE)FindClose(value);}};
template<class T>T info(HANDLE handle,FILE_INFO_BY_HANDLE_CLASS kind,const char*operation){T value{};if(!GetFileInformationByHandleEx(handle,kind,&value,DWORD(sizeof(value))))failure(operation,GetLastError());return value;}
}
ShelfProperties readShelfProperties(const ehud::data::ShelfFileAccess&lease,ShelfPropertyOptions options){
    if(!lease.open()||!ehud::data::validWindowsFilePath(lease.metadata().windowsPath))throw std::invalid_argument("Shelf properties need an open, validated reference lease");
    const auto&metadata=lease.metadata();ShelfProperties out;out.windowsPath=metadata.windowsPath;
    const auto path=wide(metadata.windowsPath),longPath=extended(path);
    // Attribute access only: no contents, no recall, the final object itself.
    Handle handle;handle.value=CreateFileW(longPath.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_OPEN_NO_RECALL,nullptr);
    if(handle.value==INVALID_HANDLE_VALUE)failure("Cannot open the Shelf reference for its properties",GetLastError());
    const auto id=info<FILE_ID_INFO>(handle.value,FileIdInfo,"Cannot read the Shelf reference identity");
    static_assert(sizeof(id.FileId.Identifier)==sizeof(metadata.identity.objectID));
    if(id.VolumeSerialNumber!=metadata.identity.volumeSerial||std::memcmp(id.FileId.Identifier,metadata.identity.objectID.data(),metadata.identity.objectID.size())!=0)
        throw std::runtime_error("Shelf reference identity changed before its properties were read");
    const auto basic=info<FILE_BASIC_INFO>(handle.value,FileBasicInfo,"Cannot read the Shelf reference times");
    const auto standard=info<FILE_STANDARD_INFO>(handle.value,FileStandardInfo,"Cannot read the Shelf reference size");
    const auto tag=info<FILE_ATTRIBUTE_TAG_INFO>(handle.value,FileAttributeTagInfo,"Cannot read the Shelf reference attributes");
    CloseHandle(handle.value);handle.value=INVALID_HANDLE_VALUE;
    const auto kind=classifyShelfObject(tag.FileAttributes,tag.ReparseTag);
    out.directory=standard.Directory!=FALSE;out.symbolicLink=kind==ShelfObjectKind::symbolicLink;out.cloudPlaceholder=kind==ShelfObjectKind::cloudPlaceholder;
    if(basic.CreationTime.QuadPart>0)out.created=basic.CreationTime.QuadPart;
    if(basic.LastWriteTime.QuadPart>0)out.modified=basic.LastWriteTime.QuadPart;
    if(!out.directory&&!out.symbolicLink&&standard.EndOfFile.QuadPart>=0)out.byteCount=standard.EndOfFile.QuadPart;
    const auto root=shelfPathVolumeRoot(metadata.windowsPath);
    out.remote=path.starts_with(L"\\\\")||(!root.empty()&&GetDriveTypeW(wide(root).c_str())==DRIVE_REMOTE);
    // Direct children of an ordinary local folder, names only, within the bound.
    if(out.directory&&kind==ShelfObjectKind::ordinary&&!out.remote&&options.folderItemBound){
        WIN32_FIND_DATAW entry{};Find find;find.value=FindFirstFileExW((longPath+L"\\*").c_str(),FindExInfoBasic,&entry,FindExSearchNameMatch,nullptr,FIND_FIRST_EX_LARGE_FETCH);
        if(find.value!=INVALID_HANDLE_VALUE){std::uint64_t count{};bool complete{};
            for(;;){const std::wstring_view name(entry.cFileName);if(name!=L"."&&name!=L".."&&!(entry.dwFileAttributes&FILE_ATTRIBUTE_HIDDEN)&&++count>options.folderItemBound)break;
                if(!FindNextFileW(find.value,&entry)){complete=GetLastError()==ERROR_NO_MORE_FILES;break;}}
            if(complete)out.folderItems=count;}
        else if(GetLastError()==ERROR_FILE_NOT_FOUND)out.folderItems=0;
    }
    return out;
}
#endif
} // namespace endfield::native
