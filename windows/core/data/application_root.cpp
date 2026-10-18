#include "core/data/application_root.hpp"
#include "core/data/file_io.hpp"
#include <system_error>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <aclapi.h>
#include <knownfolders.h>
#include <sddl.h>
#include <shlobj.h>
#include <memory>
#include <vector>
#else
#include <cerrno>
#include <sys/stat.h>
#endif

namespace ehud::data {
namespace {
constexpr std::size_t markerLimit = 64 * 1024;
[[noreturn]] void unavailable(const char* message) { throw StoreError(StoreErrorCode::unavailable, message); }
void requireRoot(bool condition, const char* message) { if (!condition) throw StoreError(StoreErrorCode::invalid, message); }

#ifdef _WIN32
struct LocalFree_ { void operator()(void* value) const noexcept { if (value) LocalFree(value); } };
struct HandleCloser { void operator()(HANDLE value) const noexcept { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); } };
// Protected DACL: full control for the current user and SYSTEM only, inherited
// by every store file and directory created below the base.
std::wstring currentUserSid() {
    HANDLE raw{};
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw)) unavailable("Current user identity is unavailable");
    std::unique_ptr<void, HandleCloser> token(raw);
    DWORD size{};
    GetTokenInformation(raw, TokenUser, nullptr, 0, &size);
    if (!size) unavailable("Current user identity is unavailable");
    std::vector<std::byte> buffer(size);
    if (!GetTokenInformation(raw, TokenUser, buffer.data(), size, &size)) unavailable("Current user identity is unavailable");
    wchar_t* text{};
    if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buffer.data())->User.Sid, &text)) unavailable("Current user identity is unavailable");
    std::unique_ptr<void, LocalFree_> owned(text);
    return text;
}
void createPrivateDirectory(const std::filesystem::path& path) {
    const auto sddl = L"D:P(A;OICI;FA;;;" + currentUserSid() + L")(A;OICI;FA;;;SY)";
    PSECURITY_DESCRIPTOR descriptor{};
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &descriptor, nullptr))
        unavailable("Private data root security could not be prepared");
    std::unique_ptr<void, LocalFree_> owned(descriptor);
    SECURITY_ATTRIBUTES attributes{sizeof(attributes), descriptor, FALSE};
    if (!CreateDirectoryW(path.c_str(), &attributes) && GetLastError() != ERROR_ALREADY_EXISTS)
        unavailable("Private data root could not be created");
}
#else
void createPrivateDirectory(const std::filesystem::path& path) {
    if (::mkdir(path.c_str(), 0700) != 0 && errno != EEXIST) unavailable("Private data root could not be created");
}
#endif

void createOrdinaryDirectory(const std::filesystem::path& path) {
    std::error_code error;
    std::filesystem::create_directory(path, error);
    if (error) unavailable("Data directory could not be created");
}
}

ApplicationRoot openApplicationRoot(const std::filesystem::path& base) {
    requireRoot(!base.empty() && base.is_absolute() && base != base.root_path() && base.lexically_normal() == base &&
        base.native().find(std::filesystem::path::value_type{}) == std::filesystem::path::string_type::npos,
        "An explicit absolute EndfieldHUD data root is required");
    std::error_code error;
    const auto parentState = std::filesystem::status(base.parent_path(), error);
    if (error || !std::filesystem::is_directory(parentState)) unavailable("The data root's parent folder is unavailable");
    ApplicationRoot result;
    result.base = base;
    result.marker = base / "root.json";
    result.data = base / std::string(applicationRootDataDirectory);
    const auto existing = std::filesystem::symlink_status(base, error);
    if (error && error != std::errc::no_such_file_or_directory) unavailable("Data root could not be inspected");
    if (!std::filesystem::exists(existing)) createPrivateDirectory(base);
    detail::validateRoot(base);
    if (const auto bytes = detail::readFile(result.marker, markerLimit)) {
        Json marker;
        try { marker = Json::parse(*bytes, markerLimit); }
        catch (const std::exception&) { throw StoreError(StoreErrorCode::invalid, "The data root marker is invalid; it was preserved"); }
        requireRoot(marker.isObject() && marker["format"].isString() && marker["format"].string() == applicationRootFormat &&
            marker["schema"].isNumber(), "The data root marker is invalid; it was preserved");
        const auto schema = marker["schema"].integer();
        if (schema > applicationRootSchema)
            throw StoreError(StoreErrorCode::newerVersion, "This EndfieldHUD data requires a newer app version; it was preserved");
        requireRoot(schema == applicationRootSchema && marker["data"].isString() && marker["data"].string() == applicationRootDataDirectory,
            "The data root marker is unsupported; it was preserved");
    } else {
        const Json marker = Json::Object{{"format", std::string(applicationRootFormat)}, {"schema", applicationRootSchema},
                                         {"data", std::string(applicationRootDataDirectory)}};
        detail::replaceFile(result.marker, std::nullopt, marker.encode(markerLimit), markerLimit);
        result.created = true;
    }
    const auto data = std::filesystem::symlink_status(result.data, error);
    if (error && error != std::errc::no_such_file_or_directory) unavailable("Data root could not be inspected");
    if (!std::filesystem::exists(data)) createOrdinaryDirectory(result.data);
    detail::validateRoot(result.data);
    return result;
}

std::filesystem::path localApplicationDataBase() {
#ifdef _WIN32
    PWSTR folder{};
    const auto status = SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DONT_VERIFY, nullptr, &folder);
    std::unique_ptr<wchar_t, decltype(&CoTaskMemFree)> owned(folder, &CoTaskMemFree);
    if (FAILED(status) || !folder || !*folder) unavailable("The Local AppData folder is unavailable");
    const std::filesystem::path base = std::filesystem::path(folder).lexically_normal() / std::string(applicationRootDirectoryName);
    requireRoot(base.is_absolute(), "The Local AppData folder is not absolute");
    return base;
#else
    unavailable("The Windows Local AppData folder exists only on Windows");
#endif
}
} // namespace ehud::data
