#include "native/file_shelf_files.hpp"
#include <algorithm>
#include <cstring>
#include <utility>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winioctl.h>
#endif

namespace endfield::native {
using namespace ehud::data;
namespace {
void require(bool value,const char* message){if(!value)throw StoreError(StoreErrorCode::invalid,message);}
std::string normalizedPath(std::string_view token){
    require(validWindowsFilePath(token),"Shelf requires an explicit ordinary absolute Windows filesystem path");
    std::string path(token);std::replace(path.begin(),path.end(),'/','\\');return path;
}
#ifdef _WIN32
[[noreturn]] void nativeFailure(const char* operation,DWORD error){
    throw StoreError(StoreErrorCode::unavailable,std::string(operation)+" (Windows error "+std::to_string(error)+")");
}
struct Handle final {
    HANDLE value{INVALID_HANDLE_VALUE};
    ~Handle(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
    Handle()=default;Handle(const Handle&)=delete;Handle&operator=(const Handle&)=delete;
};
std::wstring extendedPath(std::string_view path){
    // validWindowsFilePath already checks UTF-8 and limits input to 32768 bytes.
    const auto count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path.data(),static_cast<int>(path.size()),nullptr,0);
    if(count<=0)nativeFailure("Cannot decode shelf path",GetLastError());
    std::wstring decoded(static_cast<std::size_t>(count),L'\0');
    if(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path.data(),static_cast<int>(path.size()),decoded.data(),count)!=count)
        nativeFailure("Cannot decode shelf path",GetLastError());
    auto result=decoded.starts_with(L"\\\\")?L"\\\\?\\UNC\\"+decoded.substr(2):L"\\\\?\\"+decoded;
    if(result.size()>=32767)throw StoreError(StoreErrorCode::tooLarge,"Shelf path exceeds the extended Windows path bound");
    return result;
}
template<class T>T info(HANDLE handle,FILE_INFO_BY_HANDLE_CLASS kind,const char* operation){
    T value{};if(!GetFileInformationByHandleEx(handle,kind,&value,static_cast<DWORD>(sizeof(value))))nativeFailure(operation,GetLastError());return value;
}
#endif
ShelfFileAccess acquirePath(std::string_view token,const FileShelfFileLabels& labels){
    auto path=normalizedPath(token);
#ifdef _WIN32
    const auto wide=extendedPath(path);
    // Allocate the cleanup owner before acquiring the handle, so all later
    // metadata/string/closure allocation failures release the same native lease.
    auto handle=std::make_shared<Handle>();
    // Attribute-only access is exempt from sharing checks. Request read access
    // so withholding FILE_SHARE_DELETE actually prevents selected-object rename
    // and deletion. The same bit is LIST_DIRECTORY for directories; neither
    // file contents nor directory entries are read by this metadata provider.
    static_assert(FILE_READ_DATA==FILE_LIST_DIRECTORY);
    handle->value=CreateFileW(wide.c_str(),FILE_READ_ATTRIBUTES|FILE_READ_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_NO_RECALL,nullptr);
    if(handle->value==INVALID_HANDLE_VALUE)nativeFailure("Cannot acquire shelf file metadata",GetLastError());
    if(GetFileType(handle->value)!=FILE_TYPE_DISK)throw StoreError(StoreErrorCode::invalid,"Shelf reference is not a disk filesystem object");
    const auto attributes=info<FILE_ATTRIBUTE_TAG_INFO>(handle->value,FileAttributeTagInfo,"Cannot inspect shelf reparse metadata");
    const bool link=(attributes.FileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)!=0;
    if(link&&attributes.ReparseTag!=IO_REPARSE_TAG_SYMLINK)
        throw StoreError(StoreErrorCode::unavailable,"This shelf provider does not support this reparse-point type");
    if((attributes.FileAttributes&FILE_ATTRIBUTE_OFFLINE)!=0)
        throw StoreError(StoreErrorCode::unavailable,"Shelf reference is offline; no recall was requested");
    const auto standard=info<FILE_STANDARD_INFO>(handle->value,FileStandardInfo,"Cannot inspect shelf file metadata");
    if(standard.DeletePending)throw StoreError(StoreErrorCode::unavailable,"Shelf reference is pending deletion");
    if(standard.EndOfFile.QuadPart<0)throw StoreError(StoreErrorCode::invalid,"Shelf reference has an invalid byte count");
    const auto identity=info<FILE_ID_INFO>(handle->value,FileIdInfo,"Filesystem does not provide a 128-bit shelf identity");
    ShelfFileMetadata metadata;
    metadata.windowsPath=std::move(path);metadata.name=metadata.windowsPath.substr(metadata.windowsPath.find_last_of('\\')+1);
    metadata.isDirectory=standard.Directory!=FALSE;
    metadata.kind=link?ShelfFileKind::symbolicLink:(metadata.isDirectory?ShelfFileKind::directory:ShelfFileKind::regular);
    metadata.typeDescription=metadata.isDirectory?labels.folder:labels.file;
    if(!metadata.isDirectory&&!link)metadata.byteCount=standard.EndOfFile.QuadPart;
    metadata.identity.volumeSerial=identity.VolumeSerialNumber;
    static_assert(sizeof(identity.FileId.Identifier)==sizeof(metadata.identity.objectID));
    std::memcpy(metadata.identity.objectID.data(),identity.FileId.Identifier,metadata.identity.objectID.size());
    return {std::move(metadata),[handle=std::move(handle)]()mutable noexcept{handle.reset();}};
#else
    (void)path;(void)labels;
    throw StoreError(StoreErrorCode::unavailable,"Native shelf file access requires Windows");
#endif
}
ShelfFileAccess resolvePath(const ShelfRecord& record,const FileShelfFileLabels& labels){
    auto access=acquirePath(record.windowsPath,labels);
    if(!record.identity.matches(access.metadata().identity))
        throw StoreError(StoreErrorCode::unavailable,"Original shelf item is unavailable; a different item occupies its former location");
    return access;
}
}
NativeFileShelfFiles::NativeFileShelfFiles(FileShelfFileLabels labels){
    for(const auto* value:{&labels.file,&labels.folder})require(!value->empty()&&Json::validUtf8(*value)&&value->find('\0')==std::string::npos,"Invalid shelf fallback label");
    labels_=std::make_shared<const FileShelfFileLabels>(std::move(labels));
}
ShelfFileAccess NativeFileShelfFiles::acquire(std::string_view path)const{return acquirePath(path,*labels_);}
ShelfFileAccess NativeFileShelfFiles::resolve(const ShelfRecord& record)const{return resolvePath(record,*labels_);}
FileShelfStore::Platform NativeFileShelfFiles::platform()const{
    return {[labels=labels_](std::string_view path){return acquirePath(path,*labels);},
        [labels=labels_](const ShelfRecord& record){return resolvePath(record,*labels);}};
}
} // namespace endfield::native
