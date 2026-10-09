#include "native/storage_files.hpp"
#include "core/data/json.hpp"
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>
#include <utility>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winternl.h>
#include <winioctl.h>
#include <shlobj.h>
#include <cstring>
#endif
namespace endfield::native {
namespace {void need(bool b,const char*s){if(!b)throw std::invalid_argument(s);}bool label(std::string_view s){return s.size()<=4096&&s.find('\0')==std::string_view::npos&&ehud::data::Json::validUtf8(s);}}
modules::StorageDetailsSnapshot scanStorageFolders(std::span<const StorageFolderScope>scopes,const modules::StorageScanCancellation&cancel,double date,StorageFileSystem&fs,std::optional<std::uint64_t>expected,StorageScanClock clock,modules::StorageScanLimits limits){
    need(scopes.size()<=64&&std::isfinite(date)&&bool(clock),"Invalid Storage scan inputs");need(limits.maximumEntries<=40000&&limits.entriesPerFolder<=8000&&std::isfinite(limits.maximumSeconds)&&limits.maximumSeconds<=10&&std::isfinite(limits.secondsPerFolder)&&limits.secondsPerFolder<=2&&limits.maximumDepth<=32,"Storage scan exceeds original resource bounds");
    for(const auto&s:scopes){need(!s.id.empty()&&label(s.id)&&label(s.title)&&s.paths.size()<=8,"Invalid Storage scan scope");for(const auto&p:s.paths)need(p.size()<=32768&&p.find('\0')==p.npos&&ehud::data::Json::validUtf8(p),"Invalid Storage scope path");}
    const auto now=[&]{const auto n=clock();need(std::isfinite(n),"Invalid Storage scan clock");return n;};const auto began=now();std::int64_t total{};std::set<StorageFileIdentity>seen;modules::StorageDetailsSnapshot result;result.updatedAt=date;result.categories.reserve(scopes.size());
    for(const auto&scope:scopes){const auto scopeBegan=now();std::int64_t entries{},bytes{};bool partial{},opened{};
        const auto limited=[&]{return cancel.cancelled()||total>=std::max<std::int64_t>(0,limits.maximumEntries)||entries>=std::max<std::int64_t>(0,limits.entriesPerFolder)||now()-began>=std::max(0.,limits.maximumSeconds)||now()-scopeBegan>=std::max(0.,limits.secondsPerFolder);};
        const auto walk=[&](auto&&self,StorageDirectory&directory,int depth,std::uint64_t volume)->void{
            for(;;){if(limited()){partial=true;return;}StorageDirectoryEntry entry;const auto read=directory.next(entry);if(read==StorageDirectoryRead::end)return;if(read==StorageDirectoryRead::error){partial=true;return;}if(entry.name==u"."||entry.name==u"..")continue;++entries;++total;
                const auto&m=entry.metadata;if(m.kind==StorageFileKind::symbolicLink)continue;if(m.identity.volume!=volume||m.unavailable||m.kind==StorageFileKind::reparse){partial=true;continue;}
                if(m.kind==StorageFileKind::directory){if(depth>=std::max(0,limits.maximumDepth)){partial=true;continue;}auto child=directory.openChild(entry.name);if(!child){partial=true;continue;}const auto&actual=child->metadata();if(actual.kind!=StorageFileKind::directory||actual.unavailable||actual.identity.volume!=volume||actual.identity!=m.identity){partial=true;continue;}self(self,*child,depth+1,volume);
                }else if(m.kind==StorageFileKind::regular&&seen.insert(m.identity).second){const auto size=std::max<std::int64_t>(0,m.allocatedBytes);if(size>std::numeric_limits<std::int64_t>::max()-bytes)partial=true;else bytes+=size;}
            }
        };
        for(const auto&path:scope.paths){if(limited()){partial=true;break;}auto root=fs.openRoot(path);if(!root){partial=true;continue;}const auto&m=root->metadata();if(m.kind!=StorageFileKind::directory||m.unavailable||(expected&&m.identity.volume!=*expected)){partial=true;continue;}opened=true;walk(walk,*root,0,m.identity.volume);}
        result.categories.push_back({scope.id,scope.title,opened?std::optional<std::int64_t>{bytes}:std::nullopt,partial||!opened});
    }
    result.isPartial=std::any_of(result.categories.begin(),result.categories.end(),[](const auto&c){return c.isPartial;});if(!result.categories.empty()&&std::all_of(result.categories.begin(),result.categories.end(),[](const auto&c){return !c.bytes;}))result.error="Folder sizes are unavailable or the scan was interrupted.";return result;
}
#ifdef _WIN32
namespace {
struct Handle {HANDLE value{INVALID_HANDLE_VALUE};Handle()=default;explicit Handle(HANDLE v):value(v){}~Handle(){if(value!=INVALID_HANDLE_VALUE&&value)CloseHandle(value);}Handle(const Handle&)=delete;Handle&operator=(const Handle&)=delete;Handle(Handle&&v)noexcept:value(std::exchange(v.value,INVALID_HANDLE_VALUE)){}Handle&operator=(Handle&&v)noexcept{if(value!=INVALID_HANDLE_VALUE&&value)CloseHandle(value);value=std::exchange(v.value,INVALID_HANDLE_VALUE);return *this;}explicit operator bool()const{return value&&value!=INVALID_HANDLE_VALUE;}};
std::wstring wide(std::string_view s){if(s.empty()||s.size()>32768||s.find('\0')!=s.npos)return {};const auto n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0);if(n<=0)return {};std::wstring out(static_cast<std::size_t>(n),L'\0');if(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),out.data(),n)!=n)return {};std::replace(out.begin(),out.end(),L'/',L'\\');return out;}
std::string utf8(std::wstring_view s){if(s.size()>32767)return {};const auto n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0,nullptr,nullptr);if(n<=0)return {};std::string out(static_cast<std::size_t>(n),'\0');if(WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),out.data(),n,nullptr,nullptr)!=n)return {};return out;}
bool ordinary(std::wstring_view p){return p.size()>=3&&p.size()<32767&&((p[0]>=L'A'&&p[0]<=L'Z')||(p[0]>=L'a'&&p[0]<=L'z'))&&p[1]==L':'&&p[2]==L'\\'&&p.find(L':',2)==p.npos;}
bool unavailable(DWORD a){return (a&(FILE_ATTRIBUTE_OFFLINE|FILE_ATTRIBUTE_RECALL_ON_OPEN|FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS))!=0;}
StorageFileKind kind(DWORD a,DWORD tag){if(a&FILE_ATTRIBUTE_REPARSE_POINT)return tag==IO_REPARSE_TAG_SYMLINK?StorageFileKind::symbolicLink:StorageFileKind::reparse;if(a&FILE_ATTRIBUTE_DEVICE)return StorageFileKind::other;return a&FILE_ATTRIBUTE_DIRECTORY?StorageFileKind::directory:StorageFileKind::regular;}
std::optional<StorageFileMetadata>metadata(HANDLE h){FILE_ATTRIBUTE_TAG_INFO a{};FILE_ID_INFO id{};FILE_STANDARD_INFO size{};if(GetFileType(h)!=FILE_TYPE_DISK||!GetFileInformationByHandleEx(h,FileAttributeTagInfo,&a,sizeof(a))||!GetFileInformationByHandleEx(h,FileIdInfo,&id,sizeof(id))||!GetFileInformationByHandleEx(h,FileStandardInfo,&size,sizeof(size))||size.DeletePending)return {};StorageFileMetadata out;out.identity.volume=id.VolumeSerialNumber;std::memcpy(out.identity.file.data(),id.FileId.Identifier,16);out.kind=kind(a.FileAttributes,a.ReparseTag);out.unavailable=unavailable(a.FileAttributes);out.allocatedBytes=size.AllocationSize.QuadPart;return out;}
Handle openDrive(std::wstring_view p){const auto volume=std::wstring(p.substr(0,3));const auto type=GetDriveTypeW(volume.c_str());if(type!=DRIVE_FIXED&&type!=DRIVE_RAMDISK)return {};const auto root=L"\\\\?\\"+std::wstring(p.substr(0,3));return Handle{CreateFileW(root.c_str(),FILE_LIST_DIRECTORY|FILE_READ_ATTRIBUTES|SYNCHRONIZE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_OPEN_NO_RECALL,nullptr)};}
Handle openRelative(HANDLE parent,std::u16string_view child){if(child.empty()||child.size()>32767||child==u"."||child==u".."||child.find_first_of(u"\\/:")!=child.npos||child.find(u'\0')!=child.npos)return {};
    // Documented user-mode NtCreateFile RootDirectory keeps each lookup bound
    // to its already verified directory, even if a path is renamed/replaced.
    using Open=NTSTATUS(NTAPI*)(PHANDLE,ACCESS_MASK,POBJECT_ATTRIBUTES,PIO_STATUS_BLOCK,PLARGE_INTEGER,ULONG,ULONG,ULONG,ULONG,PVOID,ULONG);
    const auto open=reinterpret_cast<Open>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"NtCreateFile"));if(!open)return {};
    static_assert(sizeof(wchar_t)==sizeof(char16_t));UNICODE_STRING name{};name.Buffer=reinterpret_cast<PWSTR>(const_cast<char16_t*>(child.data()));name.Length=static_cast<USHORT>(child.size()*2);name.MaximumLength=name.Length;OBJECT_ATTRIBUTES attributes{};attributes.Length=sizeof(attributes);attributes.RootDirectory=parent;attributes.ObjectName=&name;attributes.Attributes=0x40; // OBJ_CASE_INSENSITIVE
    IO_STATUS_BLOCK status{};HANDLE result{};
    // FILE_DIRECTORY_FILE rejects this no-recall combination on NTFS. Keep
    // no-follow/no-recall protection, then validate the returned handle's type,
    // volume and availability before enumerating it (both callers do so).
    constexpr ULONG synchronous=0x20,openExisting=0x1,noRecall=0x00400000,openReparse=0x00200000;
    const auto code=open(&result,FILE_LIST_DIRECTORY|FILE_READ_ATTRIBUTES|SYNCHRONIZE,&attributes,&status,nullptr,0,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,openExisting,synchronous|noRecall|openReparse,nullptr,0);if(code<0){if(result)CloseHandle(result);return {};}return Handle{result};
}
class Directory final:public StorageDirectory {
    Handle handle_;StorageFileMetadata metadata_;alignas(8)std::array<std::byte,65536>buffer_{};std::size_t offset_{};bool refill_{true},ended_{};
public:Directory(Handle h,StorageFileMetadata m):handle_(std::move(h)),metadata_(m){}
    const StorageFileMetadata&metadata()const noexcept override{return metadata_;}
    StorageDirectoryRead next(StorageDirectoryEntry&out)override{
        if(ended_)return StorageDirectoryRead::end;
        if(refill_){if(!GetFileInformationByHandleEx(handle_.value,FileIdExtdDirectoryInfo,buffer_.data(),static_cast<DWORD>(buffer_.size()))){ended_=true;return GetLastError()==ERROR_NO_MORE_FILES?StorageDirectoryRead::end:StorageDirectoryRead::error;}offset_=0;refill_=false;}
        constexpr auto header=offsetof(FILE_ID_EXTD_DIR_INFO,FileName);if(offset_>buffer_.size()-header){ended_=true;return StorageDirectoryRead::error;}const auto*entry=reinterpret_cast<const FILE_ID_EXTD_DIR_INFO*>(buffer_.data()+offset_);const auto length=entry->FileNameLength;
        if(length%2||length>buffer_.size()-offset_-header||(entry->NextEntryOffset&&(entry->NextEntryOffset%8||entry->NextEntryOffset<header+length||entry->NextEntryOffset>buffer_.size()-offset_))){ended_=true;return StorageDirectoryRead::error;}
        out.name.assign(reinterpret_cast<const char16_t*>(entry->FileName),length/2);out.metadata.identity.volume=metadata_.identity.volume;std::memcpy(out.metadata.identity.file.data(),entry->FileId.Identifier,16);out.metadata.kind=kind(entry->FileAttributes,entry->ReparsePointTag);out.metadata.unavailable=unavailable(entry->FileAttributes);out.metadata.allocatedBytes=entry->AllocationSize.QuadPart;
        if(entry->NextEntryOffset)offset_+=entry->NextEntryOffset;else refill_=true;return StorageDirectoryRead::entry;
    }
    std::unique_ptr<StorageDirectory>openChild(std::u16string_view name)override{auto h=openRelative(handle_.value,name);if(!h)return {};auto m=::endfield::native::metadata(h.value);if(!m||m->kind!=StorageFileKind::directory||m->unavailable)return {};return std::make_unique<Directory>(std::move(h),*m);}
};
class FileSystem final:public StorageFileSystem {
public:std::unique_ptr<StorageDirectory>openRoot(std::string_view path)override{auto p=wide(path);if(!ordinary(p))return {};auto h=openDrive(p);if(!h)return {};auto m=metadata(h.value);if(!m||m->kind!=StorageFileKind::directory||m->unavailable)return {};const auto volume=m->identity.volume;
        std::size_t at=3;while(at<p.size()){const auto end=p.find(L'\\',at),count=(end==p.npos?p.size():end)-at;if(count){auto next=openRelative(h.value,{reinterpret_cast<const char16_t*>(p.data()+at),count});if(!next)return {};auto nextMeta=metadata(next.value);if(!nextMeta||nextMeta->kind!=StorageFileKind::directory||nextMeta->unavailable||nextMeta->identity.volume!=volume)return {};h=std::move(next);m=nextMeta;}if(end==p.npos)break;at=end+1;}return std::make_unique<Directory>(std::move(h),*m);}
};
std::wstring startup(){std::array<wchar_t,32768>path{},root{};const auto n=GetWindowsDirectoryW(path.data(),static_cast<UINT>(path.size()));if(!n||n>=path.size()||!GetVolumePathNameW(path.data(),root.data(),static_cast<DWORD>(root.size())))return {};return root.data();}
std::string known(REFKNOWNFOLDERID id){PWSTR path{};if(FAILED(SHGetKnownFolderPath(id,KF_FLAG_DONT_VERIFY,nullptr,&path)))return {};struct Free {PWSTR p;~Free(){CoTaskMemFree(p);}}free{path};return utf8(path);}
}
std::unique_ptr<StorageFileSystem>makeWindowsStorageFileSystem(){return std::make_unique<FileSystem>();}
std::optional<modules::StorageCapacity>readWindowsCapacityAt(std::string_view explicitPath,double date){need(std::isfinite(date),"Invalid Storage capacity date");const auto p=wide(explicitPath);if(!ordinary(p))return {};std::array<wchar_t,32768>root{};if(!GetVolumePathNameW(p.c_str(),root.data(),static_cast<DWORD>(root.size())))return {};const auto drive=GetDriveTypeW(root.data());if(drive!=DRIVE_FIXED&&drive!=DRIVE_RAMDISK)return {};ULARGE_INTEGER free{},total{};if(!GetDiskFreeSpaceExW(root.data(),&free,&total,nullptr)||total.QuadPart==0||total.QuadPart>static_cast<ULONGLONG>(std::numeric_limits<std::int64_t>::max())||free.QuadPart>total.QuadPart)return {};std::array<wchar_t,MAX_PATH+1>name{};std::string title="Startup disk";if(GetVolumeInformationW(root.data(),name.data(),static_cast<DWORD>(name.size()),nullptr,nullptr,nullptr,nullptr,0)){auto decoded=utf8(name.data());if(!decoded.empty())title=std::move(decoded);}return modules::StorageCapacity{std::move(title),static_cast<std::int64_t>(total.QuadPart),static_cast<std::int64_t>(free.QuadPart),date};}
std::optional<modules::StorageCapacity>readWindowsStartupCapacity(double date){const auto root=startup();return root.empty()?std::nullopt:readWindowsCapacityAt(utf8(root),date);}
std::vector<StorageFolderScope>windowsStorageScopes(){std::vector<StorageFolderScope>out{{"applications","Applications",{}},{"documents","Documents",{}},{"downloads","Downloads",{}},{"pictures","Pictures",{}},{"music","Music",{}},{"movies","Movies",{}}};
    const auto add=[&](std::size_t index,REFKNOWNFOLDERID id){auto p=known(id);if(p.empty())return;auto&paths=out[index].paths;const auto w=wide(p);for(const auto&old:paths){const auto previous=wide(old);if(CompareStringOrdinal(w.c_str(),static_cast<int>(w.size()),previous.c_str(),static_cast<int>(previous.size()),TRUE)==CSTR_EQUAL)return;}paths.push_back(std::move(p));};
    add(0,FOLDERID_ProgramFiles);add(0,FOLDERID_ProgramFilesX86);add(1,FOLDERID_Documents);add(2,FOLDERID_Downloads);add(3,FOLDERID_Pictures);add(4,FOLDERID_Music);add(5,FOLDERID_Videos);return out;}
modules::StorageDetailsSnapshot scanWindowsStorageDetails(const modules::StorageScanCancellation&cancel,double date,modules::StorageScanLimits limits){if(cancel.cancelled())return {{},false,date,true,{}};auto fs=makeWindowsStorageFileSystem();const auto path=startup();auto root=path.empty()?nullptr:fs->openRoot(utf8(path));if(!root)return {{},false,date,true,"The startup filesystem is unavailable."};if(cancel.cancelled())return scanStorageFolders({},cancel,date,*fs,root->metadata().identity.volume,[]{return 0.;},limits);auto scopes=windowsStorageScopes();return scanStorageFolders(scopes,cancel,date,*fs,root->metadata().identity.volume,[]{return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();},limits);}
#endif
}
