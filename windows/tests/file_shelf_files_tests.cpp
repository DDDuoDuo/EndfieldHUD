#include "native/file_shelf_files.hpp"
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winioctl.h>
#endif
using namespace ehud::data;
using namespace endfield::native;
namespace {
unsigned checks{};
void check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
template<class F>void rejects(StoreErrorCode code,F f,const char* message){try{f();}catch(const StoreError& e){check(e.code()==code,message);return;}throw std::runtime_error(message);}
void guards(){
    NativeFileShelfFiles files;
    for(const auto& path:{"","relative.txt","C:relative.txt","C:\\","C:\\a\\..\\b","C:\\a\\.\\b","C:\\a\\NUL.txt","C:\\a\\file:stream","C:\\a\\trailing.","C:\\a\\trailing ","\\\\?\\C:\\a","\\\\.\\pipe\\a","https://example.invalid/a","\\\\host\\share"})
        rejects(StoreErrorCode::invalid,[&]{(void)files.acquire(path);},"Nonordinary filesystem token rejects before native I/O");
    rejects(StoreErrorCode::invalid,[&]{(void)files.acquire(std::string("C:\\a\\x\0hidden",13));},"NUL cannot hide a second path suffix");
    rejects(StoreErrorCode::invalid,[&]{(void)files.acquire(std::string("C:\\a\\")+char(0xff));},"Invalid UTF-8 is rejected");
    rejects(StoreErrorCode::invalid,[]{NativeFileShelfFiles invalid({"","Folder"});},"Empty display label rejects");
    rejects(StoreErrorCode::invalid,[]{NativeFileShelfFiles invalid({"File",std::string("x\0y",3)});},"NUL display label rejects");
#ifndef _WIN32
    rejects(StoreErrorCode::unavailable,[&]{(void)files.acquire("C:\\synthetic\\file.txt");},"Non-Windows builds explicitly decline native I/O");
#endif
}
#ifdef _WIN32
std::string utf8(const std::filesystem::path& path){const auto value=path.u8string();return {reinterpret_cast<const char*>(value.data()),value.size()};}
struct Temporary {
    std::filesystem::path root=std::filesystem::canonical(std::filesystem::temp_directory_path())/("EndfieldHUD-native-shelf-fixture-"+makeUUID());
    Temporary(){if(!std::filesystem::create_directory(root))throw std::runtime_error("Cannot create isolated native shelf fixture");}
    ~Temporary(){std::error_code e;std::filesystem::remove_all(root,e);}
};
void write(const std::filesystem::path& path,std::string_view bytes){std::ofstream out(path,std::ios::binary);out.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));if(!out)throw std::runtime_error("Cannot write synthetic fixture");}
std::string read(const std::filesystem::path& path){std::ifstream in(path,std::ios::binary);if(!in)throw std::runtime_error("Cannot read synthetic fixture");return {std::istreambuf_iterator<char>(in),{}};}
DWORD handles(){DWORD count{};if(!GetProcessHandleCount(GetCurrentProcess(),&count))throw std::runtime_error("Cannot count fixture handles");return count;}
ShelfRecord record(const ShelfFileMetadata& metadata){ShelfRecord value;value.windowsPath=metadata.windowsPath;value.identity=metadata.identity;return value;}
void actualMetadataAndLifetime(){
    Temporary temp;const auto file=temp.root/std::filesystem::path(L"note-\u4e2d\u6587-\U0001f4dd.txt"),folder=temp.root/"folder";
    const std::string contents="synthetic shelf bytes\n";write(file,contents);std::filesystem::create_directory(folder);
    NativeFileShelfFiles files({"\xe6\x96\x87\xe4\xbb\xb6","\xe6\x96\x87\xe4\xbb\xb6\xe5\xa4\xb9"});
    const auto before=handles();
    auto access=files.acquire(utf8(file));
    check(handles()==before+1,"Each live reference retains exactly one read-access handle");
    const auto metadata=access.metadata();check(metadata.kind==ShelfFileKind::regular&&!metadata.isDirectory&&metadata.byteCount==static_cast<std::int64_t>(contents.size()),"Real regular-file metadata preserves kind and exact size");
    check(metadata.name==utf8(file.filename())&&metadata.windowsPath==utf8(file)&&metadata.typeDescription=="\xe6\x96\x87\xe4\xbb\xb6","UTF-8 path/name and caller-localized fallback remain exact");
    HANDLE identityHandle=CreateFileW(file.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    check(identityHandle!=INVALID_HANDLE_VALUE,"Fixture independently opens metadata identity oracle");FILE_ID_INFO actualIdentity{};
    const auto hasIdentity=GetFileInformationByHandleEx(identityHandle,FileIdInfo,&actualIdentity,sizeof(actualIdentity));CloseHandle(identityHandle);
    check(hasIdentity&&metadata.identity.volumeSerial==actualIdentity.VolumeSerialNumber&&std::equal(metadata.identity.objectID.begin(),metadata.identity.objectID.end(),actualIdentity.FileId.Identifier),"All 128 identity bits and full volume serial match independent Win32 metadata");
    auto same=files.resolve(record(metadata));check(same.metadata().identity.matches(metadata.identity),"Resolving unchanged path verifies actual retained identity");same.close();
    const auto moved=temp.root/"renamed.txt";
    const auto renameResult=MoveFileW(file.c_str(),moved.c_str());const auto renameError=renameResult?ERROR_SUCCESS:GetLastError();
    if(renameResult||renameError!=ERROR_SHARING_VIOLATION){
        const auto& current=renameResult?moved:file;
        HANDLE deleteProbe=CreateFileW(current.c_str(),DELETE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        const auto deleteOpenError=deleteProbe==INVALID_HANDLE_VALUE?GetLastError():ERROR_SUCCESS;
        if(deleteProbe!=INVALID_HANDLE_VALUE)CloseHandle(deleteProbe);
        HANDLE readProbe=CreateFileW(current.c_str(),FILE_READ_ATTRIBUTES|FILE_READ_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        const auto readOpenError=readProbe==INVALID_HANDLE_VALUE?GetLastError():ERROR_SUCCESS;
        BOOL readRenameResult{};DWORD readRenameError{};
        if(readProbe!=INVALID_HANDLE_VALUE){readRenameResult=MoveFileW(current.c_str(),(temp.root/"read-access-probe.txt").c_str());readRenameError=readRenameResult?ERROR_SUCCESS:GetLastError();CloseHandle(readProbe);}
        std::cerr<<"Shelf lease diagnostic: renameResult="<<renameResult<<" renameError="<<renameError<<" deleteAccessOpenError="<<deleteOpenError<<" readAccessOpenError="<<readOpenError<<" readAccessRenameResult="<<readRenameResult<<" readAccessRenameError="<<readRenameError<<" leaseOpen="<<access.open()<<" retainedHandleDelta="<<(handles()-before)<<'\n';
    }
    check(!renameResult&&renameError==ERROR_SHARING_VIOLATION,"Live reference prevents rename of selected object");
    check(!DeleteFileW(file.c_str())&&GetLastError()==ERROR_SHARING_VIOLATION,"Live reference prevents deletion of selected object");
    HANDLE concurrent=CreateFileW(file.c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr);
    check(concurrent!=INVALID_HANDLE_VALUE,"Metadata lease permits ordinary concurrent read/write access");if(concurrent!=INVALID_HANDLE_VALUE)CloseHandle(concurrent);
    access.close();access.close();check(handles()==before,"Repeated close releases exactly one handle");
    check(read(file)==contents,"Metadata acquisition leaves synthetic original bytes unchanged");
    auto slash=utf8(file);std::replace(slash.begin(),slash.end(),'\\','/');auto normalized=files.acquire(slash);check(normalized.metadata().windowsPath==utf8(file)&&normalized.metadata().identity.matches(metadata.identity),"Accepted slash spelling normalizes without changing identity");normalized.close();
    auto directory=files.acquire(utf8(folder));check(directory.metadata().kind==ShelfFileKind::directory&&directory.metadata().isDirectory&&!directory.metadata().byteCount&&directory.metadata().typeDescription=="\xe6\x96\x87\xe4\xbb\xb6\xe5\xa4\xb9","Directory metadata has no recursive size or enumeration");
    const auto movedFolder=temp.root/"renamed-folder";
    const auto directoryRenameResult=MoveFileW(folder.c_str(),movedFolder.c_str());const auto directoryRenameError=directoryRenameResult?ERROR_SUCCESS:GetLastError();
    check(!directoryRenameResult&&directoryRenameError==ERROR_SHARING_VIOLATION,"Directory LIST_DIRECTORY lease also prevents selected-object rename");
    directory.close();check(MoveFileW(folder.c_str(),movedFolder.c_str())!=FALSE,"Directory rename succeeds after lease closes");
    rejects(StoreErrorCode::unavailable,[&]{(void)files.acquire(utf8(temp.root/"missing"));},"Missing file stays missing; provider never creates it");check(!std::filesystem::exists(temp.root/"missing"),"OPEN_EXISTING cannot create missing file");
    for(unsigned i=0;i<256;++i){auto lease=files.acquire(utf8(file));auto movedLease=std::move(lease);check(!lease.open()&&movedLease.open(),"Native lease moves without releasing ownership");}
    check(handles()==before,"Repeated metadata/lease operations retain no handles after completion");
    const auto attributes=GetFileAttributesW(file.c_str());check(attributes!=INVALID_FILE_ATTRIBUTES&&SetFileAttributesW(file.c_str(),attributes|FILE_ATTRIBUTE_READONLY),"Fixture marks only its own synthetic file read-only");
    {auto readOnly=files.acquire(utf8(file));check(readOnly.metadata().identity.matches(metadata.identity),"Read-only files require no write access for shelf metadata");}
    check(GetFileAttributesW(file.c_str())==(attributes|FILE_ATTRIBUTE_READONLY),"Metadata provider does not alter file attributes");
    check(SetFileAttributesW(file.c_str(),attributes)!=FALSE,"Fixture restores its own read-only attribute");
}
void replacementAndStoreBundle(){
    Temporary temp;const auto a=temp.root/"a.txt",b=temp.root/"b.txt",original=temp.root/"original-a.txt";write(a,"first");write(b,"second");
    FileShelfStore::Platform platform;{NativeFileShelfFiles temporary;platform=temporary.platform();}
    auto store=std::make_unique<FileShelfStore>(temp.root/"metadata",platform);
    const std::vector<std::string> tokens{utf8(a),utf8(b)};check(store->add(tokens)==2,"Actual provider imports isolated references after wrapper destruction");
    const auto aID=store->items()[0].id,bID=store->items()[1].id;const auto expected=store->items()[0];
    check(MoveFileW(a.c_str(),original.c_str())!=FALSE,"Completed import releases its handle promptly");write(a,"replacement");
    rejects(StoreErrorCode::unavailable,[&]{(void)platform.resolve(expected);},"Provider rejects same-path different file ID before returning lease");
    rejects(StoreErrorCode::unavailable,[&]{(void)store->access(aID);},"Store access cannot mistake replacement bytes for original file");
    store->refresh();check(store->items()[0].availabilityError.has_value()&&!store->items()[1].availabilityError,"Refresh marks replacement unavailable without losing other references");
    const std::vector<std::string> selected{bID,aID};const auto before=handles();bool delivered{};
    rejects(StoreErrorCode::unavailable,[&]{store->copy(selected,[&](ShelfCopyBundle){delivered=true;});},"Replacement prevents entire outgoing copy bundle");
    check(!delivered&&handles()==before,"Failed outgoing bundle publishes nothing and closes acquired leases");
    check(DeleteFileW(a.c_str())!=FALSE&&MoveFileW(original.c_str(),a.c_str())!=FALSE,"Fixture restores original namespace object");
    const auto originalB=temp.root/"original-b.txt";check(MoveFileW(b.c_str(),originalB.c_str())!=FALSE,"Fixture moves only its second synthetic original");write(b,"replaced second");
    rejects(StoreErrorCode::unavailable,[&]{store->copy(selected,[&](ShelfCopyBundle){delivered=true;});},"Later replaced reference rejects after an earlier native lease was acquired");
    check(!delivered&&handles()==before,"Later identity failure releases earlier copy lease and rejected replacement handle");
    check(DeleteFileW(b.c_str())!=FALSE&&MoveFileW(originalB.c_str(),b.c_str())!=FALSE,"Fixture restores second original identity");
    std::optional<ShelfCopyBundle> pending;store->copy(selected,[&](ShelfCopyBundle bundle){pending=std::move(bundle);store.reset();});
    check(!store&&pending&&pending->accesses.size()==2&&pending->ids==std::vector<std::string>{aID,bID},"Store destruction during handoff preserves ordered copy-only leases");
    check(handles()==before+2&&!MoveFileW(a.c_str(),original.c_str()),"Copy receiver retains both native handles independently of store/provider");
    pending.reset();check(handles()==before&&MoveFileW(a.c_str(),original.c_str())!=FALSE,"Bundle completion releases all handles and rename restriction");
    FileShelfStore reopened(temp.root/"metadata",platform);reopened.refresh();check(reopened.items()[0].availabilityError.has_value(),"Moved original is unavailable; no guessed rename resolution");
    check(reopened.clear()&&read(original)=="first"&&read(b)=="second","Clearing persisted references never deletes or modifies original objects");
}
bool symbolicLink(const std::filesystem::path& link,const std::filesystem::path& target,DWORD flags){
    constexpr DWORD allowUnprivileged=0x2;
    if(CreateSymbolicLinkW(link.c_str(),target.c_str(),flags|allowUnprivileged))return true;
    auto error=GetLastError();if(error==ERROR_INVALID_PARAMETER){if(CreateSymbolicLinkW(link.c_str(),target.c_str(),flags))return true;error=GetLastError();}
    if(error==ERROR_PRIVILEGE_NOT_HELD||error==ERROR_ACCESS_DENIED||error==ERROR_NOT_SUPPORTED||error==ERROR_INVALID_FUNCTION){std::cout<<"SKIP symbolic-link fixture: Windows error "<<error<<'\n';return false;}
    throw std::runtime_error("Cannot create synthetic symbolic link: "+std::to_string(error));
}
void links(){
    Temporary temp;const auto target=temp.root/"target.txt",alias=temp.root/"hardlink.txt",link=temp.root/"link.txt",broken=temp.root/"broken.txt";write(target,"link target");NativeFileShelfFiles files;
    auto original=files.acquire(utf8(target));const auto identity=original.metadata().identity;original.close();
    if(CreateHardLinkW(alias.c_str(),target.c_str(),nullptr)){
        auto hard=files.acquire(utf8(alias));check(hard.metadata().identity.matches(identity),"Hard links retain the same exact filesystem identity");hard.close();
        FileShelfStore store(temp.root/"metadata",files.platform());const std::vector<std::string> tokens{utf8(target),utf8(alias)};
        check(store.add(tokens)==1,"Actual native hard-link aliases deduplicate by identity");
    }else{const auto error=GetLastError();if(error!=ERROR_NOT_SUPPORTED&&error!=ERROR_INVALID_FUNCTION)throw std::runtime_error("Cannot create fixture hard link: "+std::to_string(error));std::cout<<"SKIP hard-link fixture: filesystem error "<<error<<'\n';}
    if(!symbolicLink(link,target,0))return;
    auto selected=files.acquire(utf8(link));check(selected.metadata().kind==ShelfFileKind::symbolicLink&&!selected.metadata().identity.matches(identity)&&!selected.metadata().byteCount,"Symbolic-link reference retains selected link identity, never target identity/size");
    check(DeleteFileW(target.c_str())!=FALSE,"Selected link lease does not lock or open target");
    auto still=files.resolve(record(selected.metadata()));check(still.metadata().identity.matches(selected.metadata().identity),"Selected link identity remains stable even when target disappears");still.close();selected.close();
    if(symbolicLink(broken,temp.root/"absent-target",0)){auto dangling=files.acquire(utf8(broken));check(dangling.metadata().kind==ShelfFileKind::symbolicLink,"Selected dangling-link metadata is explicit; target readability is not claimed");}
    const auto folder=temp.root/"folder",directoryLink=temp.root/"folder-link";std::filesystem::create_directory(folder);
    if(symbolicLink(directoryLink,folder,SYMBOLIC_LINK_FLAG_DIRECTORY)){auto directory=files.acquire(utf8(directoryLink));check(directory.metadata().kind==ShelfFileKind::symbolicLink&&directory.metadata().isDirectory&&!directory.metadata().byteCount,"Directory link preserves selected reparse identity and directory flag");directory.close();}
}
void unsupportedReparse(){
    // Create a junction wholly inside this disposable fixture. This does not
    // require/adjust token privileges; restricted filesystems report a skip.
    Temporary temp;const auto target=temp.root/"target",junction=temp.root/"junction";
    std::filesystem::create_directory(target);std::filesystem::create_directory(junction);
    const std::wstring substitute=L"\\??\\"+target.native(),print=target.native();
    struct Header {DWORD tag;WORD length,reserved,substituteOffset,substituteLength,printOffset,printLength;};
    static_assert(sizeof(Header)==16);
    const auto pathBytes=(substitute.size()+print.size()+2)*sizeof(wchar_t);
    check(pathBytes+8<=MAXIMUM_REPARSE_DATA_BUFFER_SIZE-8,"Synthetic reparse payload fits native bound");
    const Header header{IO_REPARSE_TAG_MOUNT_POINT,static_cast<WORD>(pathBytes+8),0,0,static_cast<WORD>(substitute.size()*sizeof(wchar_t)),
        static_cast<WORD>((substitute.size()+1)*sizeof(wchar_t)),static_cast<WORD>(print.size()*sizeof(wchar_t))};
    std::vector<unsigned char> bytes(sizeof(header)+pathBytes,0);std::memcpy(bytes.data(),&header,sizeof(header));
    std::memcpy(bytes.data()+sizeof(header),substitute.data(),substitute.size()*sizeof(wchar_t));
    std::memcpy(bytes.data()+sizeof(header)+(substitute.size()+1)*sizeof(wchar_t),print.data(),print.size()*sizeof(wchar_t));
    HANDLE handle=CreateFileW(junction.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(handle==INVALID_HANDLE_VALUE){const auto error=GetLastError();if(error==ERROR_ACCESS_DENIED||error==ERROR_PRIVILEGE_NOT_HELD){std::cout<<"SKIP junction fixture: Windows error "<<error<<'\n';return;}throw std::runtime_error("Cannot open synthetic junction: "+std::to_string(error));}
    DWORD returned{};const auto ok=DeviceIoControl(handle,FSCTL_SET_REPARSE_POINT,bytes.data(),static_cast<DWORD>(bytes.size()),nullptr,0,&returned,nullptr);const auto error=ok?ERROR_SUCCESS:GetLastError();CloseHandle(handle);
    if(!ok){if(error==ERROR_ACCESS_DENIED||error==ERROR_PRIVILEGE_NOT_HELD||error==ERROR_NOT_SUPPORTED||error==ERROR_INVALID_FUNCTION){std::cout<<"SKIP junction fixture: Windows error "<<error<<'\n';return;}throw std::runtime_error("Cannot set synthetic junction: "+std::to_string(error));}
    NativeFileShelfFiles files;const auto before=handles();
    rejects(StoreErrorCode::unavailable,[&]{(void)files.acquire(utf8(junction));},"Unsupported junction rejects rather than becoming target identity");
    check(handles()==before,"Unsupported reparse rejection releases acquired metadata handle");
    check(RemoveDirectoryW(junction.c_str())!=FALSE&&std::filesystem::exists(target),"Fixture removes only junction; provider never changes its target");
}
#endif
}
int main(){try{guards();
#ifdef _WIN32
    actualMetadataAndLifetime();replacementAndStoreBundle();links();unsupportedReparse();
#else
    std::cout<<"SKIP native filesystem cases: Windows required; no real files opened\n";
#endif
    std::cout<<"Native shelf files: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<"Native shelf files failed after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}}
