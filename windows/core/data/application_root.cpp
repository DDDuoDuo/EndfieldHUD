#include "core/data/application_root.hpp"
#include "core/data/file_io.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <string>
#include <system_error>
#include <vector>
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
#include <cstdio>
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
} // namespace
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
namespace {
// Protected DACL: full control for the current user and SYSTEM only, inherited
// by every store file and directory created below the base.
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

// --- Interrupted Mac import switch (restoreInterruptedImportRoot) -----------
// The formats the importer of earlier Windows versions wrote; read only.
constexpr std::size_t importNoteLimit = 4096;
constexpr std::string_view importMarkerName = "import-work.json";
constexpr std::string_view importJournalName = "activation.json";
constexpr std::string_view importMarkerFormat = "EndfieldHUD.macImportWork";
constexpr std::string_view importJournalFormat = "EndfieldHUD.macImportActivation";
// The only top-level entries the importer wrote into its work folder.
constexpr std::array<std::string_view, 4> importWorkEntries{importMarkerName, importJournalName, "source", "root"};
std::string utf8Name(const std::filesystem::path& path) {
    const auto text = path.filename().u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}
std::filesystem::path utf8Path(std::string_view text) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}
bool importToken(std::string_view token) noexcept {
    return !token.empty() && token.size() <= 64 && std::all_of(token.begin(), token.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '-';
    });
}
// A plain sibling name (no separators, no dot segments).
bool plainName(std::string_view name) noexcept {
    return !name.empty() && name.size() <= 255 && name != "." && name != ".." && name.find_first_of("/\\:") == std::string_view::npos &&
           name.find('\0') == std::string_view::npos;
}
bool present(const std::filesystem::path& path) noexcept {
    std::error_code error;
    const auto state = std::filesystem::symlink_status(path, error);
    return error ? error != std::errc::no_such_file_or_directory : std::filesystem::exists(state);
}
// An existing directory that is not a link, junction or reparse point.
bool ordinaryDirectory(const std::filesystem::path& path) noexcept {
    try {
        if (!present(path)) return false;
        detail::validateRoot(path);
        return true;
    } catch (...) { return false; }
}
std::optional<Json> importNote(const std::filesystem::path& path) noexcept {
    try {
        const auto bytes = detail::readFile(path, importNoteLimit);
        if (!bytes) return std::nullopt;
        const auto note = Json::parse(*bytes, importNoteLimit);
        if (!note.isObject() || !note["version"].isNumber() || note["version"].integer() != 1 || !note["root"].isString() ||
            note["root"].string() != applicationRootDataDirectory)
            return std::nullopt;
        return note;
    } catch (...) { return std::nullopt; }
}
// The backup an interrupted switch of `folder` left, when every check holds.
std::optional<std::filesystem::path> interruptedBackup(const std::filesystem::path& base, const std::filesystem::path& folder, std::string_view token) noexcept {
    try {
        if (!ordinaryDirectory(folder)) return std::nullopt;
        std::error_code error;
        for (std::filesystem::directory_iterator it(folder, error), end; !error && it != end; it.increment(error))
            if (std::find(importWorkEntries.begin(), importWorkEntries.end(), utf8Name(it->path())) == importWorkEntries.end()) return std::nullopt;
        if (error) return std::nullopt;
        const auto markerNote = importNote(folder / utf8Path(importMarkerName));
        if (!markerNote) return std::nullopt;
        const Json& marker = *markerNote;
        if (marker.object().size() != 5 || !marker["format"].isString() || marker["format"].string() != importMarkerFormat || !marker["token"].isString() ||
            marker["token"].string() != token || !marker["createdAt"].isNumber() || !std::isfinite(marker["createdAt"].number()))
            return std::nullopt;
        // The staged root still in the work folder: the second rename never ran.
        if (!present(folder / "root")) return std::nullopt;
        const auto journalNote = importNote(folder / utf8Path(importJournalName));
        if (!journalNote) return std::nullopt;
        const Json& journal = *journalNote;
        if (journal.object().size() != 4 || !journal["format"].isString() || journal["format"].string() != importJournalFormat || !journal["backup"].isString())
            return std::nullopt;
        const auto name = journal["backup"].string();
        if (!plainName(name) || !name.starts_with(std::string(applicationRootDataDirectory) + ".backup-")) return std::nullopt;
        auto backup = base / utf8Path(name);
        if (!ordinaryDirectory(backup)) return std::nullopt;
        return backup;
    } catch (...) { return std::nullopt; }
}
// Renames a directory only when the target does not exist.
bool renameNoReplace(const std::filesystem::path& from, const std::filesystem::path& to) noexcept {
#ifdef _WIN32
    return MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_WRITE_THROUGH) != FALSE;
#elif defined(__APPLE__)
    return ::renamex_np(from.c_str(), to.c_str(), RENAME_EXCL) == 0;
#else
    if (present(to)) return false;
    return ::rename(from.c_str(), to.c_str()) == 0;
#endif
}
}

bool restoreInterruptedImportRoot(const std::filesystem::path& base) noexcept {
    try {
        if (base.empty() || !base.is_absolute() || base == base.root_path() || base.lexically_normal() != base || !ordinaryDirectory(base)) return false;
        const auto data = base / std::string(applicationRootDataDirectory);
        if (present(data)) return false;
        const auto prefix = std::string(applicationRootDataDirectory) + ".import-";
        std::vector<std::filesystem::path> folders;
        std::error_code error;
        for (std::filesystem::directory_iterator it(base, error), end; !error && it != end; it.increment(error)) {
            const auto name = utf8Name(it->path());
            if (name.size() > prefix.size() && name.starts_with(prefix) && importToken(std::string_view(name).substr(prefix.size())))
                folders.push_back(it->path());
        }
        std::sort(folders.begin(), folders.end());
        for (const auto& folder : folders) {
            const auto name = utf8Name(folder);
            const auto backup = interruptedBackup(base, folder, std::string_view(name).substr(prefix.size()));
            if (backup && renameNoReplace(*backup, data)) return true;
        }
    } catch (...) {}
    return false;
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
