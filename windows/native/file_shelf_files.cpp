#include "native/file_shelf_files.hpp"
#include <algorithm>
#include <cstring>
#include <cwchar>
#include <optional>
#include <utility>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winioctl.h>
#include <shellapi.h>
#endif

namespace endfield::native {
using namespace ehud::data;
namespace {
void require(bool value,const char* message){if(!value)throw StoreError(StoreErrorCode::invalid,message);}
std::string normalizedPath(std::string_view token){
    require(validWindowsFilePath(token),"Shelf requires an explicit ordinary absolute Windows filesystem path");
    std::string path(token);std::replace(path.begin(),path.end(),'/','\\');return path;
}
char lowerASCII(char c)noexcept{return c>='A'&&c<='Z'?char(c-'A'+'a'):c;}
using Configuration=NativeFileShelfFiles::Configuration;
#ifdef _WIN32
[[noreturn]] void nativeFailure(const char* operation,DWORD error){
    throw StoreError(StoreErrorCode::unavailable,std::string(operation)+" (Windows error "+std::to_string(error)+")");
}
struct Handle final {
    HANDLE value{INVALID_HANDLE_VALUE};
    ~Handle(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
    Handle()=default;Handle(const Handle&)=delete;Handle&operator=(const Handle&)=delete;
};
std::wstring widePath(std::string_view path){
    // validWindowsFilePath already checks UTF-8 and limits input to 32768 bytes.
    const auto count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path.data(),static_cast<int>(path.size()),nullptr,0);
    if(count<=0)nativeFailure("Cannot decode shelf path",GetLastError());
    std::wstring decoded(static_cast<std::size_t>(count),L'\0');
    if(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path.data(),static_cast<int>(path.size()),decoded.data(),count)!=count)
        nativeFailure("Cannot decode shelf path",GetLastError());
    return decoded;
}
std::wstring extendedPath(std::string_view path){
    const auto decoded=widePath(path);
    auto result=decoded.starts_with(L"\\\\")?L"\\\\?\\UNC\\"+decoded.substr(2):L"\\\\?\\"+decoded;
    if(result.size()>=32767)throw StoreError(StoreErrorCode::tooLarge,"Shelf path exceeds the extended Windows path bound");
    return result;
}
std::string narrow(std::wstring_view value){
    if(value.empty())return {};
    const auto count=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0,nullptr,nullptr);
    if(count<=0)nativeFailure("Cannot encode shelf path",GetLastError());
    std::string out(static_cast<std::size_t>(count),'\0');
    if(WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),out.data(),count,nullptr,nullptr)!=count)
        nativeFailure("Cannot encode shelf path",GetLastError());
    return out;
}
template<class T>T info(HANDLE handle,FILE_INFO_BY_HANDLE_CLASS kind,const char* operation){
    T value{};if(!GetFileInformationByHandleEx(handle,kind,&value,static_cast<DWORD>(sizeof(value))))nativeFailure(operation,GetLastError());return value;
}
// Registry-only type name for the extension; never opens the file or loads a
// per-file handler. Empty when the Shell has none (the caller keeps its label).
std::string shellTypeName(const std::string& name){
    SHFILEINFOW value{};
    std::wstring wide;try{wide=widePath(name);}catch(const StoreError&){return {};}
    if(!SHGetFileInfoW(wide.c_str(),FILE_ATTRIBUTE_NORMAL,&value,static_cast<UINT>(sizeof(value)),SHGFI_TYPENAME|SHGFI_USEFILEATTRIBUTES))return {};
    const std::wstring_view text(value.szTypeName,wcsnlen(value.szTypeName,std::size(value.szTypeName)));
    try{auto result=narrow(text);return Json::validUtf8(result)&&result.find('\0')==std::string::npos?result:std::string{};}catch(const StoreError&){return {};}
}
// Metadata of an already-opened final object (path open or open-by-ID).
ShelfFileMetadata describe(HANDLE handle,std::string path,const Configuration& configuration){
    if(GetFileType(handle)!=FILE_TYPE_DISK)throw StoreError(StoreErrorCode::invalid,"Shelf reference is not a disk filesystem object");
    const auto attributes=info<FILE_ATTRIBUTE_TAG_INFO>(handle,FileAttributeTagInfo,"Cannot inspect shelf reparse metadata");
    const bool link=(attributes.FileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)!=0;
    if(link&&attributes.ReparseTag!=IO_REPARSE_TAG_SYMLINK)
        throw StoreError(StoreErrorCode::unavailable,"This shelf provider does not support this reparse-point type");
    if((attributes.FileAttributes&FILE_ATTRIBUTE_OFFLINE)!=0)
        throw StoreError(StoreErrorCode::unavailable,"Shelf reference is offline; no recall was requested");
    const auto standard=info<FILE_STANDARD_INFO>(handle,FileStandardInfo,"Cannot inspect shelf file metadata");
    if(standard.DeletePending)throw StoreError(StoreErrorCode::unavailable,"Shelf reference is pending deletion");
    if(standard.EndOfFile.QuadPart<0)throw StoreError(StoreErrorCode::invalid,"Shelf reference has an invalid byte count");
    const auto identity=info<FILE_ID_INFO>(handle,FileIdInfo,"Filesystem does not provide a 128-bit shelf identity");
    ShelfFileMetadata metadata;
    metadata.windowsPath=std::move(path);metadata.name=metadata.windowsPath.substr(metadata.windowsPath.find_last_of('\\')+1);
    metadata.isDirectory=standard.Directory!=FALSE;
    metadata.kind=link?ShelfFileKind::symbolicLink:(metadata.isDirectory?ShelfFileKind::directory:ShelfFileKind::regular);
    metadata.typeDescription=metadata.isDirectory?configuration.labels.folder:configuration.labels.file;
    if(!metadata.isDirectory&&configuration.options.shellTypeNames)if(auto name=shellTypeName(metadata.name);!name.empty())metadata.typeDescription=std::move(name);
    if(!metadata.isDirectory&&!link)metadata.byteCount=standard.EndOfFile.QuadPart;
    metadata.identity.volumeSerial=identity.VolumeSerialNumber;
    static_assert(sizeof(identity.FileId.Identifier)==sizeof(metadata.identity.objectID));
    std::memcpy(metadata.identity.objectID.data(),identity.FileId.Identifier,metadata.identity.objectID.size());
    return metadata;
}
// Read access participates in sharing checks (attribute-only access does not),
// so withholding FILE_SHARE_DELETE prevents rename/delete of the selected
// object while leased. FILE_READ_DATA is LIST_DIRECTORY for directories;
// neither file contents nor directory entries are read by this provider.
constexpr DWORD leaseAccess=FILE_READ_ATTRIBUTES|FILE_READ_DATA;
constexpr DWORD leaseFlags=FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_NO_RECALL;
static_assert(FILE_READ_DATA==FILE_LIST_DIRECTORY);
ShelfFileAccess lease(std::shared_ptr<Handle> handle,std::string path,const Configuration& configuration){
    auto metadata=describe(handle->value,std::move(path),configuration);
    return {std::move(metadata),[handle=std::move(handle)]()mutable noexcept{handle.reset();}};
}
// The volume hint for OpenFileById: the stored path's mount point when the
// system can still derive it (folder-mounted volumes), else its drive/share root.
std::wstring volumeRoot(std::string_view stored){
    auto root=shelfPathVolumeRoot(stored);if(root.empty())throw StoreError(StoreErrorCode::unavailable,"Shelf reference has no volume root");
    auto wideRoot=widePath(root);
    const auto plain=widePath(stored);
    if(plain.size()<MAX_PATH){
        std::wstring mount(plain.size()+2,L'\0');
        if(GetVolumePathNameW(plain.c_str(),mount.data(),static_cast<DWORD>(mount.size()))){
            mount.resize(wcsnlen(mount.c_str(),mount.size()));
            if(!mount.empty()&&mount.back()==L'\\'&&mount.size()>=wideRoot.size())wideRoot=std::move(mount);
        }
    }
    auto result=wideRoot.starts_with(L"\\\\")?L"\\\\?\\UNC\\"+wideRoot.substr(2):L"\\\\?\\"+wideRoot;
    if(result.size()>=32767)throw StoreError(StoreErrorCode::tooLarge,"Shelf volume path exceeds the extended Windows path bound");
    return result;
}
ShelfFileAccess acquirePath(std::string_view token,const Configuration& configuration);
ShelfFileAccess relocate(const ShelfRecord& record,const Configuration& configuration){
    const auto root=volumeRoot(record.windowsPath);
    Handle volume;volume.value=CreateFileW(root.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS,nullptr);
    if(volume.value==INVALID_HANDLE_VALUE)nativeFailure("Cannot open the shelf reference volume",GetLastError());
    const auto volumeIdentity=info<FILE_ID_INFO>(volume.value,FileIdInfo,"Shelf reference volume has no 128-bit identity");
    if(volumeIdentity.VolumeSerialNumber!=record.identity.volumeSerial)throw StoreError(StoreErrorCode::unavailable,"Shelf reference volume is no longer mounted at its former location");
    FILE_ID_DESCRIPTOR descriptor{};descriptor.dwSize=sizeof(descriptor);descriptor.Type=ExtendedFileIdType;
    static_assert(sizeof(descriptor.ExtendedFileId.Identifier)==sizeof(record.identity.objectID));
    std::memcpy(descriptor.ExtendedFileId.Identifier,record.identity.objectID.data(),record.identity.objectID.size());
    // Attribute-only: this handle only names the object. NTFS does not apply
    // share-mode checks of an open-by-ID handle to later path opens (a rename
    // succeeds), so the lease itself is reopened by path below.
    Handle object;object.value=OpenFileById(volume.value,&descriptor,FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,leaseFlags);
    if(object.value==INVALID_HANDLE_VALUE)nativeFailure("Original shelf item is no longer on its volume",GetLastError());
    const auto units=GetFinalPathNameByHandleW(object.value,nullptr,0,FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);
    if(!units)nativeFailure("Cannot read the current shelf item path",GetLastError());
    if(units>=32768)throw StoreError(StoreErrorCode::tooLarge,"Current shelf item path exceeds the Windows path bound");
    std::wstring finalPath(units,L'\0');
    const auto written=GetFinalPathNameByHandleW(object.value,finalPath.data(),units,FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);
    if(!written||written>=units)nativeFailure("Cannot read the current shelf item path",written?ERROR_INSUFFICIENT_BUFFER:GetLastError());
    finalPath.resize(written);
    auto path=relocatedShelfPath(narrow(finalPath));
    if(path.empty())throw StoreError(StoreErrorCode::unavailable,"Current shelf item path is not an ordinary filesystem path");
    if(shelfPathInRecycleBin(path))throw StoreError(StoreErrorCode::unavailable,"Original shelf item was moved to the Recycle Bin");
    // The ordinary path lease (delete sharing withheld), verified to be the
    // same object: a concurrent rename between the two opens fails closed.
    auto access=acquirePath(path,configuration);
    if(!record.identity.matches(access.metadata().identity))throw StoreError(StoreErrorCode::unavailable,"Original shelf item moved again while it was being resolved");
    return access;
}
#endif
ShelfFileAccess acquirePath(std::string_view token,const Configuration& configuration){
    auto path=normalizedPath(token);
#ifdef _WIN32
    const auto wide=extendedPath(path);
    // Allocate the cleanup owner before acquiring the handle, so all later
    // metadata/string/closure allocation failures release the same native lease.
    auto handle=std::make_shared<Handle>();
    handle->value=CreateFileW(wide.c_str(),leaseAccess,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,leaseFlags,nullptr);
    if(handle->value==INVALID_HANDLE_VALUE)nativeFailure("Cannot acquire shelf file metadata",GetLastError());
    return lease(std::move(handle),std::move(path),configuration);
#else
    (void)path;(void)configuration;
    throw StoreError(StoreErrorCode::unavailable,"Native shelf file access requires Windows");
#endif
}
ShelfFileAccess resolvePath(const ShelfRecord& record,const Configuration& configuration){
    if(!configuration.options.relocateByIdentity){
        auto access=acquirePath(record.windowsPath,configuration);
        if(!record.identity.matches(access.metadata().identity))
            throw StoreError(StoreErrorCode::unavailable,"Original shelf item is unavailable; a different item occupies its former location");
        return access;
    }
    // Bookmark resolution order: the recorded location first, then the
    // original object by identity. The first failure is the one reported.
    std::optional<StoreError> failure;
    try{
        auto access=acquirePath(record.windowsPath,configuration);
        if(record.identity.matches(access.metadata().identity))return access;
        failure.emplace(StoreErrorCode::unavailable,"Original shelf item is unavailable; a different item occupies its former location");
    }catch(const StoreError& error){if(error.code()==StoreErrorCode::tooLarge)throw;failure.emplace(error);}
#ifdef _WIN32
    try{return relocate(record,configuration);}catch(const StoreError&){}
#endif
    throw *failure;
}
}
std::string relocatedShelfPath(std::string_view value){
    std::string path;
    if(value.starts_with("\\\\?\\UNC\\"))path="\\\\"+std::string(value.substr(8));
    else if(value.starts_with("\\\\?\\"))path=std::string(value.substr(4));
    else return {};
    return validWindowsFilePath(path)?path:std::string{};
}
bool shelfPathInRecycleBin(std::string_view path)noexcept{
    constexpr std::string_view bin="$recycle.bin";
    std::size_t start{};
    while(start<=path.size()){
        auto end=path.find_first_of("\\/",start);if(end==std::string_view::npos)end=path.size();
        const auto part=path.substr(start,end-start);
        if(part.size()==bin.size()&&std::equal(part.begin(),part.end(),bin.begin(),[](char a,char b){return lowerASCII(a)==b;}))return true;
        start=end+1;
    }
    return false;
}
std::string shelfPathVolumeRoot(std::string_view path){
    const auto separator=[](char c){return c=='\\'||c=='/';};
    if(path.size()>=3&&((path[0]>='A'&&path[0]<='Z')||(path[0]>='a'&&path[0]<='z'))&&path[1]==':'&&separator(path[2]))return std::string(path.substr(0,2))+"\\";
    if(path.size()>=5&&separator(path[0])&&separator(path[1])&&!separator(path[2])){
        const auto server=path.find_first_of("\\/",2);if(server==std::string_view::npos||server==2)return {};
        auto share=path.find_first_of("\\/",server+1);if(share==server+1)return {};if(share==std::string_view::npos)share=path.size();
        std::string root(path.substr(0,share));std::replace(root.begin(),root.end(),'/','\\');return root+"\\";
    }
    return {};
}
NativeFileShelfFiles::NativeFileShelfFiles(FileShelfFileLabels labels,FileShelfFileOptions options){
    for(const auto* value:{&labels.file,&labels.folder})require(!value->empty()&&Json::validUtf8(*value)&&value->find('\0')==std::string::npos,"Invalid shelf fallback label");
    configuration_=std::make_shared<const Configuration>(Configuration{std::move(labels),options});
}
ShelfFileAccess NativeFileShelfFiles::acquire(std::string_view path)const{return acquirePath(path,*configuration_);}
ShelfFileAccess NativeFileShelfFiles::resolve(const ShelfRecord& record)const{return resolvePath(record,*configuration_);}
FileShelfStore::Platform NativeFileShelfFiles::platform()const{
    return {[configuration=configuration_](std::string_view path){return acquirePath(path,*configuration);},
        [configuration=configuration_](const ShelfRecord& record){return resolvePath(record,*configuration);}};
}
} // namespace endfield::native
