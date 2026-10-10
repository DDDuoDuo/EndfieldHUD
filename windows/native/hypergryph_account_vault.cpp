#include "native/hypergryph_account_vault.hpp"
#ifdef _WIN32
#include "modules/hypergryph_account_crypto.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <aclapi.h>
#include <dpapi.h>
#include <sddl.h>
#include <algorithm>
#include <random>
#include <stdexcept>
#include <vector>

namespace endfield::native {
namespace {
namespace h=modules::hypergryph;
using ehud::data::Json;
[[noreturn]] void fail(const char* why) {throw std::runtime_error(why);}
struct Handle {HANDLE value{INVALID_HANDLE_VALUE};~Handle(){if(value!=INVALID_HANDLE_VALUE&&value) CloseHandle(value);}};
struct Local {void* value{};~Local(){if(value) LocalFree(value);}};
std::vector<std::uint8_t> currentUserSid() {
    Handle token;if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token.value)) fail("Current user token is unavailable");
    DWORD size{};GetTokenInformation(token.value,TokenUser,nullptr,0,&size);
    std::vector<std::uint8_t> buffer(size);
    if(!size||!GetTokenInformation(token.value,TokenUser,buffer.data(),size,&size)) fail("Current user SID is unavailable");
    const auto* user=reinterpret_cast<const TOKEN_USER*>(buffer.data());
    const DWORD length=GetLengthSid(user->User.Sid);std::vector<std::uint8_t> sid(length);
    if(!CopySid(length,sid.data(),user->User.Sid)) fail("Current user SID copy failed");
    return sid;
}
// Owner-only, protected (non-inherited) DACL: the Windows equivalent of 0600.
struct OwnerOnlySecurity {
    std::vector<std::uint8_t> sid=currentUserSid();std::vector<std::uint8_t> acl;SECURITY_DESCRIPTOR descriptor{};SECURITY_ATTRIBUTES attributes{};
    OwnerOnlySecurity() {
        const DWORD size=sizeof(ACL)+sizeof(ACCESS_ALLOWED_ACE)+GetLengthSid(sid.data())-sizeof(DWORD);acl.resize(size);
        auto* list=reinterpret_cast<ACL*>(acl.data());
        if(!InitializeAcl(list,size,ACL_REVISION)||!AddAccessAllowedAce(list,ACL_REVISION,FILE_ALL_ACCESS,sid.data())) fail("Owner-only ACL could not be built");
        if(!InitializeSecurityDescriptor(&descriptor,SECURITY_DESCRIPTOR_REVISION)||!SetSecurityDescriptorDacl(&descriptor,TRUE,list,FALSE)||
           !SetSecurityDescriptorControl(&descriptor,SE_DACL_PROTECTED,SE_DACL_PROTECTED)) fail("Owner-only descriptor could not be built");
        attributes.nLength=sizeof attributes;attributes.lpSecurityDescriptor=&descriptor;attributes.bInheritHandle=FALSE;
    }
};
void ordinary(const std::filesystem::path& path,bool directory) {
    const DWORD attributes=GetFileAttributesW(path.c_str());
    if(attributes==INVALID_FILE_ATTRIBUTES) return;
    if(attributes&FILE_ATTRIBUTE_REPARSE_POINT) fail("Account path must not be a reparse point");
    if(directory!=bool(attributes&FILE_ATTRIBUTE_DIRECTORY)) fail("Account path has an unexpected type");
}
// Only the Account directory itself is created owner-only; missing parents
// (normally the host already created the app root) keep inherited security.
void ensureDirectory(const std::filesystem::path& directory,bool ownerOnlyLeaf=true) {
    if(directory.empty()||!directory.is_absolute()||directory==directory.root_path()) fail("An explicit absolute account directory is required");
    ordinary(directory,true);
    if(GetFileAttributesW(directory.c_str())!=INVALID_FILE_ATTRIBUTES) return;
    ensureDirectory(directory.parent_path(),false);
    bool created;
    if(ownerOnlyLeaf) {OwnerOnlySecurity security;created=CreateDirectoryW(directory.c_str(),&security.attributes)!=FALSE;}
    else created=CreateDirectoryW(directory.c_str(),nullptr)!=FALSE;
    if(!created&&GetLastError()!=ERROR_ALREADY_EXISTS) fail("Account directory could not be created");
    ordinary(directory,true);
}
std::wstring randomSuffix() {
    std::random_device random;wchar_t buffer[32];swprintf_s(buffer,L"%08x%08x",random(),random());return buffer;
}
std::string entropy(h::Region region) {return "EndfieldHUD.hypergryph.account.v1:"+std::string(h::regionName(region));}
constexpr char magic[8]{'E','H','U','D','A','C','C','1'};
}

void writeOwnerOnlyFile(const std::filesystem::path& path,const std::string& bytes) {
    ensureDirectory(path.parent_path());ordinary(path,false);
    OwnerOnlySecurity security;
    auto temporary=path.parent_path()/(L"."+path.filename().wstring()+L"."+randomSuffix()+L".tmp");
    {
        Handle output;output.value=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,&security.attributes,CREATE_NEW,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        if(output.value==INVALID_HANDLE_VALUE) fail("Account file could not be written");
        std::size_t offset{};
        while(offset<bytes.size()) {
            DWORD count{};const DWORD requested=static_cast<DWORD>(std::min<std::size_t>(bytes.size()-offset,1<<20));
            if(!WriteFile(output.value,bytes.data()+offset,requested,&count,nullptr)||!count) {CloseHandle(output.value);output.value=INVALID_HANDLE_VALUE;DeleteFileW(temporary.c_str());fail("Account file write failed");}
            offset+=count;
        }
        if(!FlushFileBuffers(output.value)) {CloseHandle(output.value);output.value=INVALID_HANDLE_VALUE;DeleteFileW(temporary.c_str());fail("Account file flush failed");}
    }
    if(!MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) {DeleteFileW(temporary.c_str());fail("Account file replacement failed");}
}
std::optional<std::string> readBoundedFile(const std::filesystem::path& path,std::size_t maximum) {
    ordinary(path,false);
    Handle input;input.value=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(input.value==INVALID_HANDLE_VALUE) {const auto error=GetLastError();if(error==ERROR_FILE_NOT_FOUND||error==ERROR_PATH_NOT_FOUND) return std::nullopt;fail("Account file could not be opened");}
    LARGE_INTEGER size{};if(!GetFileSizeEx(input.value,&size)||size.QuadPart<0||static_cast<unsigned long long>(size.QuadPart)>maximum) fail("Account file is too large");
    std::string bytes(static_cast<std::size_t>(size.QuadPart),'\0');std::size_t offset{};
    while(offset<bytes.size()) {DWORD count{};if(!ReadFile(input.value,bytes.data()+offset,static_cast<DWORD>(bytes.size()-offset),&count,nullptr)||!count) fail("Account file read failed");offset+=count;}
    return bytes;
}
bool ownerOnly(const std::filesystem::path& path) {
    PACL dacl{};PSECURITY_DESCRIPTOR descriptor{};
    if(GetNamedSecurityInfoW(path.c_str(),SE_FILE_OBJECT,DACL_SECURITY_INFORMATION,nullptr,nullptr,&dacl,nullptr,&descriptor)!=ERROR_SUCCESS) return false;
    Local release;release.value=descriptor;
    SECURITY_DESCRIPTOR_CONTROL control{};DWORD revision{};
    if(!dacl||!GetSecurityDescriptorControl(descriptor,&control,&revision)||!(control&SE_DACL_PROTECTED)) return false;
    const auto sid=currentUserSid();
    ACL_SIZE_INFORMATION info{};if(!GetAclInformation(dacl,&info,sizeof info,AclSizeInformation)||info.AceCount==0) return false;
    for(DWORD i=0;i<info.AceCount;++i) {
        void* ace{};if(!GetAce(dacl,i,&ace)) return false;
        const auto* header=static_cast<ACE_HEADER*>(ace);
        if(header->AceType!=ACCESS_ALLOWED_ACE_TYPE) return false;
        const auto* allowed=static_cast<ACCESS_ALLOWED_ACE*>(ace);
        if(!EqualSid(const_cast<DWORD*>(&allowed->SidStart),const_cast<std::uint8_t*>(sid.data()))) return false;
    }
    return true;
}

DpapiAccountVault::DpapiAccountVault(std::filesystem::path directory):directory_(std::move(directory)) {
    if(directory_.empty()||!directory_.is_absolute()||directory_.lexically_normal()!=directory_) fail("An explicit absolute account directory is required");
}
std::filesystem::path DpapiAccountVault::path(h::Region region) const {return directory_/(std::wstring(region==h::Region::mainland?L"mainland":L"global")+L".credential");}
std::optional<h::Credentials> DpapiAccountVault::load(h::Region region) {
    const auto bytes=readBoundedFile(path(region),maximumBlobBytes);if(!bytes) return std::nullopt;
    if(bytes->size()<=sizeof magic||!std::equal(magic,magic+sizeof magic,bytes->begin())) fail("Account credential record is not recognized");
    const auto salt=entropy(region);
    DATA_BLOB input{static_cast<DWORD>(bytes->size()-sizeof magic),reinterpret_cast<BYTE*>(const_cast<char*>(bytes->data()+sizeof magic))};
    DATA_BLOB extra{static_cast<DWORD>(salt.size()),reinterpret_cast<BYTE*>(const_cast<char*>(salt.data()))},output{};
    if(!CryptUnprotectData(&input,nullptr,&extra,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&output)) fail("Account credential could not be decrypted for this user");
    std::string plain(reinterpret_cast<const char*>(output.pbData),output.cbData);
    SecureZeroMemory(output.pbData,output.cbData);LocalFree(output.pbData);
    std::optional<h::Credentials> result;
    try {
        const auto value=Json::parse(plain,maximumBlobBytes);
        if(value.isObject()&&value["cred"].isString()&&value["signingToken"].isString()&&(value["deviceID"].isNull()||value["deviceID"].isString()))
            result=h::Credentials{value["cred"].string(),value["signingToken"].string(),value["deviceID"].isString()?std::optional<std::string>(value["deviceID"].string()):std::nullopt};
    } catch(const std::exception&) {}
    h::wipe(plain);
    if(!result) fail("Account credential record is invalid");
    return result;
}
void DpapiAccountVault::save(const h::Credentials& credentials,h::Region region) {
    Json::Object object{{"cred",credentials.cred},{"signingToken",credentials.signingToken}};
    if(credentials.deviceID) object["deviceID"]=*credentials.deviceID;
    auto plain=Json(std::move(object)).encode(maximumBlobBytes);
    const auto salt=entropy(region);static const wchar_t description[]=L"EndfieldHUD community session";
    DATA_BLOB input{static_cast<DWORD>(plain.size()),reinterpret_cast<BYTE*>(plain.data())};
    DATA_BLOB extra{static_cast<DWORD>(salt.size()),reinterpret_cast<BYTE*>(const_cast<char*>(salt.data()))},output{};
    const bool ok=CryptProtectData(&input,description,&extra,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&output)!=FALSE;
    h::wipe(plain);
    if(!ok) fail("Account credential could not be protected");
    std::string record(magic,magic+sizeof magic);record.append(reinterpret_cast<const char*>(output.pbData),output.cbData);
    LocalFree(output.pbData);
    if(record.size()>maximumBlobBytes) fail("Account credential record is too large");
    writeOwnerOnlyFile(path(region),record);
}
void DpapiAccountVault::remove(h::Region region) {
    const auto file=path(region);ordinary(file,false);
    if(!DeleteFileW(file.c_str())) {const auto error=GetLastError();if(error!=ERROR_FILE_NOT_FOUND&&error!=ERROR_PATH_NOT_FOUND) fail("Account credential could not be removed");}
}

OwnerOnlyAccountCacheFile::OwnerOnlyAccountCacheFile(std::filesystem::path path):path_(std::move(path)) {
    if(path_.empty()||!path_.is_absolute()||path_.lexically_normal()!=path_) fail("An explicit absolute account cache path is required");
}
std::optional<std::string> OwnerOnlyAccountCacheFile::read() {return readBoundedFile(path_,h::maximumCacheBytes);}
bool OwnerOnlyAccountCacheFile::exists() {return GetFileAttributesW(path_.c_str())!=INVALID_FILE_ATTRIBUTES;}
void OwnerOnlyAccountCacheFile::write(const std::string& bytes) {if(bytes.size()>h::maximumCacheBytes) fail("Account cache is too large");writeOwnerOnlyFile(path_,bytes);}

struct QueuedAccountCacheFile::Impl:std::enable_shared_from_this<Impl> {
    std::filesystem::path path;app::UtilityExecutor& executor;app::UtilityExecutor::Route route;std::function<void()> writeFailed;
    std::optional<std::string> pending;bool inFlight{},failed{};std::uint64_t started{},superseded{};
    Impl(std::filesystem::path p,app::UtilityExecutor& e,std::function<void()> f):path(std::move(p)),executor(e),route(e.makeRoute()),writeFailed(std::move(f)) {}
    void pump() {
        if(inFlight||!pending) return;
        auto bytes=std::make_shared<std::string>(std::move(*pending));pending.reset();
        std::weak_ptr<Impl> weak=weak_from_this();const auto file=path;const auto sequence=started+1;
        const bool accepted=executor.submit(route,[file,bytes]{writeOwnerOnlyFile(file,*bytes);},[weak,bytes,sequence](std::exception_ptr error) {
            auto self=weak.lock();if(!self) return;
            self->inFlight=false;
            if(sequence<=self->superseded) {self->pump();return;} // flush() already wrote newer bytes
            const bool newer=self->pending.has_value();
            if(error) {
                self->failed=true;if(!newer) self->pending=std::move(*bytes); // kept for flush()/the next write, never auto-retried
                if(self->writeFailed) {auto callback=self->writeFailed;callback();}
            } else self->failed=false;
            if(!error||newer) self->pump();
        });
        // A full shared queue keeps the bytes for the next write()/flush().
        if(accepted) {inFlight=true;++started;} else pending=std::move(*bytes);
    }
};
QueuedAccountCacheFile::QueuedAccountCacheFile(std::filesystem::path path,app::UtilityExecutor& executor,std::function<void()> writeFailed)
    :impl_(std::make_shared<Impl>(std::move(path),executor,std::move(writeFailed))) {
    const auto& p=impl_->path;
    if(p.empty()||!p.is_absolute()||p.lexically_normal()!=p) fail("An explicit absolute account cache path is required");
}
QueuedAccountCacheFile::~QueuedAccountCacheFile() {
    auto& i=*impl_;
    // Accepted work finishes; a newer unsaved value is written now (shutdown path).
    if(i.pending) {if(i.inFlight) i.executor.waitIdle();try {writeOwnerOnlyFile(i.path,*i.pending);} catch(const std::exception&) {}}
    i.executor.invalidate(i.route,false);
}
std::optional<std::string> QueuedAccountCacheFile::read() {return readBoundedFile(impl_->path,h::maximumCacheBytes);}
bool QueuedAccountCacheFile::exists() {return GetFileAttributesW(impl_->path.c_str())!=INVALID_FILE_ATTRIBUTES;}
void QueuedAccountCacheFile::write(const std::string& bytes) {
    if(bytes.size()>h::maximumCacheBytes) fail("Account cache is too large");
    impl_->pending=bytes;impl_->pump();
}
bool QueuedAccountCacheFile::flush() {
    auto& i=*impl_;
    if(i.inFlight) i.executor.waitIdle(); // the accepted write has finished; its completion runs on the next drain
    if(!i.pending) return !i.failed;
    try {writeOwnerOnlyFile(i.path,*i.pending);i.pending.reset();i.failed=false;i.superseded=i.started;return true;}
    catch(const std::exception&) {i.failed=true;return false;}
}
bool QueuedAccountCacheFile::idle() const noexcept {return !impl_->inFlight&&!impl_->pending;}
std::uint64_t QueuedAccountCacheFile::writesStarted() const noexcept {return impl_->started;}
}
#endif
