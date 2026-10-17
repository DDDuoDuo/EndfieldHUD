#include "core/data/file_io.hpp"
#include <algorithm>
#include <cerrno>
#include <system_error>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace ehud::data::detail {
namespace {
[[noreturn]] void fail(const char* message) { throw StoreError(StoreErrorCode::unavailable,message); }
void ordinary(const std::filesystem::path& path, bool directory) {
    std::error_code error;const auto state=std::filesystem::symlink_status(path,error);
    if(error && error!=std::errc::no_such_file_or_directory) fail("Data path could not be inspected");
    if(!std::filesystem::exists(state)) return;
    if(std::filesystem::is_symlink(state) || (directory?!std::filesystem::is_directory(state):!std::filesystem::is_regular_file(state)))
        throw StoreError(StoreErrorCode::invalid,"Data path must be an ordinary file or directory");
#ifdef _WIN32
    const DWORD attributes=GetFileAttributesW(path.c_str());
    if(attributes==INVALID_FILE_ATTRIBUTES || (attributes&FILE_ATTRIBUTE_REPARSE_POINT))
        throw StoreError(StoreErrorCode::invalid,"Data path must not use a reparse point");
#endif
}
void parent(const std::filesystem::path& path) {
    for(auto cursor=path.parent_path();!cursor.empty();) {
        ordinary(cursor,true);const auto next=cursor.parent_path();if(next==cursor) break;cursor=next;
    }
}
#ifdef _WIN32
struct Handle {HANDLE value{INVALID_HANDLE_VALUE};~Handle(){if(value!=INVALID_HANDLE_VALUE) CloseHandle(value);}};
#else
struct Handle {int value{-1};~Handle(){if(value>=0) ::close(value);}};
#endif
}
void validateRoot(const std::filesystem::path& root) {
    if(root.empty() || !root.is_absolute() || root==root.root_path() || root.lexically_normal()!=root ||
       root.native().find(std::filesystem::path::value_type{})!=std::filesystem::path::string_type::npos)
        throw StoreError(StoreErrorCode::invalid,"An explicit absolute app-specific data root is required");
    ordinary(root,true);parent(root);
}
void validateDataFile(const std::filesystem::path& path) {parent(path);ordinary(path,false);}
std::optional<std::string> readFile(const std::filesystem::path& path,std::size_t maximum) {
    parent(path);ordinary(path,false);
#ifdef _WIN32
    Handle handle;handle.value=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(handle.value==INVALID_HANDLE_VALUE) {if(GetLastError()==ERROR_FILE_NOT_FOUND || GetLastError()==ERROR_PATH_NOT_FOUND) return {};fail("Saved file could not be opened");}
    BY_HANDLE_FILE_INFORMATION info{};if(!GetFileInformationByHandle(handle.value,&info) || (info.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY))) fail("Saved file is not ordinary");
    LARGE_INTEGER size{};if(!GetFileSizeEx(handle.value,&size) || size.QuadPart<0 || static_cast<unsigned long long>(size.QuadPart)>maximum) throw StoreError(StoreErrorCode::tooLarge,"Saved file is too large");
    std::string bytes(static_cast<std::size_t>(size.QuadPart),'\0');std::size_t offset{};
    while(offset<bytes.size()) {DWORD count{};const DWORD requested=static_cast<DWORD>(std::min<std::size_t>(bytes.size()-offset,1<<20));if(!ReadFile(handle.value,bytes.data()+offset,requested,&count,nullptr)||!count) fail("Saved file read failed");offset+=count;}
    char extra;DWORD count{};if(!ReadFile(handle.value,&extra,1,&count,nullptr)||count) fail("Saved file changed during reading");
#else
    Handle handle;handle.value=::open(path.c_str(),O_RDONLY|O_NOFOLLOW);
    if(handle.value<0) {if(errno==ENOENT) return {};fail("Saved file could not be opened");}
    struct stat info{};if(::fstat(handle.value,&info)!=0 || !S_ISREG(info.st_mode)) fail("Saved file is not ordinary");
    if(info.st_size<0 || static_cast<unsigned long long>(info.st_size)>maximum) throw StoreError(StoreErrorCode::tooLarge,"Saved file is too large");
    std::string bytes(static_cast<std::size_t>(info.st_size),'\0');std::size_t offset{};
    while(offset<bytes.size()) {const auto count=::read(handle.value,bytes.data()+offset,bytes.size()-offset);if(count<0&&errno==EINTR) continue;if(count<=0) fail("Saved file read failed");offset+=static_cast<std::size_t>(count);}
    char extra;const auto count=::read(handle.value,&extra,1);if(count!=0) fail("Saved file changed during reading");
#endif
    return bytes;
}
void replaceFile(const std::filesystem::path& path,const std::optional<std::string>& expected,const std::string& bytes,std::size_t maximum) {
    if(bytes.size()>maximum) throw StoreError(StoreErrorCode::tooLarge,"Saved data is too large");
    parent(path);ordinary(path,false);std::error_code error;
    std::filesystem::create_directories(path.parent_path(),error);if(error) fail("App data directory could not be created");
    parent(path);
    auto lockPath=path;lockPath+=L".lock";ordinary(lockPath,false);Handle lock;
#ifdef _WIN32
    lock.value=CreateFileW(lockPath.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(lock.value==INVALID_HANDLE_VALUE) fail("Saved data is busy in another instance");
#else
    lock.value=::open(lockPath.c_str(),O_RDWR|O_CREAT|O_NOFOLLOW,0600);
    if(lock.value<0 || ::flock(lock.value,LOCK_EX|LOCK_NB)!=0) fail("Saved data is busy in another instance");
#endif
    if(readFile(path,maximum)!=expected) throw StoreError(StoreErrorCode::changedOnDisk,"Saved data changed in another instance");
    auto temporary=path.parent_path()/("."+path.filename().string()+"."+makeUUID()+".tmp");
    try {
        {
            Handle output;
#ifdef _WIN32
            output.value=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
            if(output.value==INVALID_HANDLE_VALUE) fail("Atomic save could not begin");std::size_t offset{};
            while(offset<bytes.size()) {DWORD count{};const DWORD requested=static_cast<DWORD>(std::min<std::size_t>(bytes.size()-offset,1<<20));if(!WriteFile(output.value,bytes.data()+offset,requested,&count,nullptr)||!count) fail("Atomic save failed");offset+=count;}
            if(!FlushFileBuffers(output.value)) fail("Atomic save flush failed");
#else
            output.value=::open(temporary.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0600);
            if(output.value<0) fail("Atomic save could not begin");std::size_t offset{};
            while(offset<bytes.size()) {const auto count=::write(output.value,bytes.data()+offset,bytes.size()-offset);if(count<0&&errno==EINTR) continue;if(count<=0) fail("Atomic save failed");offset+=static_cast<std::size_t>(count);}
            if(::fsync(output.value)!=0) fail("Atomic save flush failed");
#endif
        }
        // Detect external/non-cooperating changes again before replacement.
        if(readFile(path,maximum)!=expected) throw StoreError(StoreErrorCode::changedOnDisk,"Saved data changed during saving");
#ifdef _WIN32
        const bool success=expected?ReplaceFileW(path.c_str(),temporary.c_str(),nullptr,0,nullptr,nullptr)!=FALSE:
            MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_WRITE_THROUGH)!=FALSE;
        if(!success) fail("Atomic save replacement failed");
#else
        if(::rename(temporary.c_str(),path.c_str())!=0) fail("Atomic save replacement failed");
#endif
    } catch(...) {std::filesystem::remove(temporary,error);throw;}
}
}
