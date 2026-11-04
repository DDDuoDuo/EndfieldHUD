#include "native/archive_password_vault.hpp"
#ifdef _WIN32
#include "native/hypergryph_account_vault.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <algorithm>
#include <stdexcept>

namespace endfield::native {
namespace {
[[noreturn]] void fail(const char* why) {throw std::runtime_error(why);}
constexpr char magic[8]{'E','H','U','D','A','R','V','1'};
// "<DOC>/<KEY>" (two uppercase UUIDs) <-> "<DOC>.<KEY>.verifier".
bool validAccount(std::string_view account) {
    if(account.size()!=73||account[36]!='/') return false;
    const auto upper=[](std::string_view id){return ehud::data::validUUID(id)&&std::none_of(id.begin(),id.end(),[](char c){return c>='a'&&c<='f';});};
    return upper(account.substr(0,36))&&upper(account.substr(37));
}
std::string entropy(std::string_view account) {return "EndfieldHUD.archive.classified.v1:"+std::string(account);}
}
DpapiArchivePasswordVault::DpapiArchivePasswordVault(std::filesystem::path directory):directory_(std::move(directory)) {
    if(directory_.empty()||!directory_.is_absolute()||directory_.lexically_normal()!=directory_) fail("An explicit absolute Archive vault directory is required");
}
std::filesystem::path DpapiArchivePasswordVault::path(std::string_view account) const {
    if(!validAccount(account)) fail("Invalid Archive verifier account");
    std::string name(account);name[36]='.';return directory_/std::filesystem::path(name+".verifier");
}
std::optional<std::string> DpapiArchivePasswordVault::read(std::string_view account) {
    const auto bytes=readBoundedFile(path(account),maximumFileBytes);if(!bytes) return std::nullopt;
    if(bytes->size()<=sizeof magic||!std::equal(magic,magic+sizeof magic,bytes->begin())) fail("Archive verifier file is not recognized");
    return unprotectForCurrentUser(std::string_view(*bytes).substr(sizeof magic),entropy(account));
}
void DpapiArchivePasswordVault::add(std::string_view account,std::string_view data) {
    const auto file=path(account);
    // add never updates in place: a duplicate is an error (SecItemAdd).
    if(GetFileAttributesW(file.c_str())!=INVALID_FILE_ATTRIBUTES) fail("Duplicate Archive verifier item");
    std::string record(magic,magic+sizeof magic);record.append(protectForCurrentUser(data,entropy(account),L"EndfieldHUD Archive classified document password verifier"));
    if(record.size()>maximumFileBytes) fail("Archive verifier record is too large");
    writeOwnerOnlyFile(file,record);
}
void DpapiArchivePasswordVault::remove(std::string_view account) {
    const auto file=path(account);
    if(!DeleteFileW(file.c_str())) {const auto error=GetLastError();if(error!=ERROR_FILE_NOT_FOUND&&error!=ERROR_PATH_NOT_FOUND) fail("Archive verifier could not be removed");}
}
std::vector<std::string> DpapiArchivePasswordVault::accounts() {
    std::vector<std::string> out;std::error_code error;
    if(!std::filesystem::exists(directory_,error)) return out;
    for(std::filesystem::directory_iterator it(directory_,error),end;!error&&it!=end;it.increment(error)) {
        const auto name=it->path().filename().u8string();if(name.size()!=82||!name.ends_with(u8".verifier")) continue;
        std::string account(reinterpret_cast<const char*>(name.data()),73);account[36]='/';if(validAccount(account)) out.push_back(std::move(account));
    }
    if(error) fail("Archive verifiers could not be listed");
    return out;
}
}
#endif
