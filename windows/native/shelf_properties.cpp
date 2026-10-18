#include "native/shelf_properties.hpp"
#include <algorithm>
#include <cstring>
#include <stdexcept>
#ifdef _WIN32
#include "native/file_shelf_files.hpp"
#include <windows.h>
#include <objbase.h>
#include <propkey.h>
#include <propsys.h>
#include <propvarutil.h>
#include <shobjidl.h>
#include <wrl/client.h>
#endif

namespace endfield::native {
std::string shelfDurationText(std::uint64_t value){
    const auto seconds=value/10'000'000ull,hours=seconds/3600,minutes=seconds/60%60,rest=seconds%60;
    const auto two=[](std::uint64_t n){return std::string(n<10?"0":"")+std::to_string(n);};
    return hours?std::to_string(hours)+":"+two(minutes)+":"+two(rest):std::to_string(minutes)+":"+two(rest);
}
std::string shelfGroupedCount(std::uint64_t value){
    auto digits=std::to_string(value);std::string out;out.reserve(digits.size()+digits.size()/3);
    for(std::size_t n=0;n<digits.size();++n){if(n&&(digits.size()-n)%3==0)out.push_back(',');out.push_back(digits[n]);}
    return out;
}
#ifdef _WIN32
namespace {
using Microsoft::WRL::ComPtr;
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
// COM for the property system on a worker that may have none (the shared
// utility worker); a thread already in another apartment keeps it.
struct Apartment{HRESULT result{CoInitializeEx(nullptr,COINIT_MULTITHREADED)};~Apartment(){if(SUCCEEDED(result))CoUninitialize();}};
std::optional<std::uint32_t> uint32(IPropertyStore&store,const PROPERTYKEY&key){
    PROPVARIANT value;PropVariantInit(&value);std::optional<std::uint32_t> out;ULONG number{};
    if(SUCCEEDED(store.GetValue(key,&value))&&value.vt!=VT_EMPTY&&SUCCEEDED(PropVariantToUInt32(value,&number))&&number>0)out=number;
    PropVariantClear(&value);return out;
}
std::optional<std::uint64_t> uint64(IPropertyStore&store,const PROPERTYKEY&key){
    PROPVARIANT value;PropVariantInit(&value);std::optional<std::uint64_t> out;ULONGLONG number{};
    if(SUCCEEDED(store.GetValue(key,&value))&&value.vt!=VT_EMPTY&&SUCCEEDED(PropVariantToUInt64(value,&number))&&number>0)out=number;
    PropVariantClear(&value);return out;
}
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
            for(;;){const std::wstring_view name(entry.cFileName);if(name!=L"."&&name!=L".."&&++count>options.folderItemBound)break;
                if(!FindNextFileW(find.value,&entry)){complete=GetLastError()==ERROR_NO_MORE_FILES;break;}}
            if(complete)out.folderItems=count;}
        else if(GetLastError()==ERROR_FILE_NOT_FOUND)out.folderItems=0;
    }
    // Shell properties of a regular file: dimensions and duration.
    if(options.propertyHandlers&&!out.directory&&!out.symbolicLink&&path.size()<MAX_PATH*8){
        const Apartment apartment;
        const auto flags=(out.cloudPlaceholder||out.remote)?GETPROPERTYSTOREFLAGS(GPS_FASTPROPERTIESONLY):GETPROPERTYSTOREFLAGS(GPS_BESTEFFORT|GPS_NO_OPLOCK);
        ComPtr<IPropertyStore>store;
        if((SUCCEEDED(apartment.result)||apartment.result==RPC_E_CHANGED_MODE)&&SUCCEEDED(SHGetPropertyStoreFromParsingName(path.c_str(),nullptr,flags,IID_PPV_ARGS(&store)))&&store){
            const auto width=uint32(*store.Get(),PKEY_Image_HorizontalSize),height=uint32(*store.Get(),PKEY_Image_VerticalSize);
            if(width&&height)out.dimensions=std::array<std::uint32_t,2>{*width,*height};
            else{const auto frameWidth=uint32(*store.Get(),PKEY_Video_FrameWidth),frameHeight=uint32(*store.Get(),PKEY_Video_FrameHeight);if(frameWidth&&frameHeight)out.dimensions=std::array<std::uint32_t,2>{*frameWidth,*frameHeight};}
            out.duration=uint64(*store.Get(),PKEY_Media_Duration);
        }
    }
    return out;
}
#endif
} // namespace endfield::native
