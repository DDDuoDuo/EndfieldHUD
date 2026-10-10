#include "core/migration/mac_import_files.hpp"
#include "core/migration/mac_import_codecs.hpp"
#include "core/migration/mac_import_sha256.hpp"
#include <algorithm>
#include <cerrno>
#include <system_error>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cstdio>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#if defined(__linux__)
#include <sys/syscall.h>
#endif
#endif

namespace ehud::migration::files {
namespace {
[[noreturn]] void fail(MacImportErrorCode code, const std::string& message) { throw MacImportError(code, message); }
std::string show(const std::filesystem::path& path) {
    const auto text = path.u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}
enum class Kind { missing, file, directory, other };
Kind inspect(const std::filesystem::path& path) {
    std::error_code error;
    const auto state = std::filesystem::symlink_status(path, error);
    if (error) {
        if (error == std::errc::no_such_file_or_directory || error == std::errc::not_a_directory) return Kind::missing;
        fail(MacImportErrorCode::unavailable, "Cannot inspect " + show(path));
    }
    if (!std::filesystem::exists(state)) return Kind::missing;
    if (std::filesystem::is_symlink(state)) return Kind::other;
#ifdef _WIN32
    const DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) fail(MacImportErrorCode::unavailable, "Cannot inspect " + show(path));
    if (attributes & FILE_ATTRIBUTE_REPARSE_POINT) return Kind::other;
#endif
    if (std::filesystem::is_directory(state)) return Kind::directory;
    if (std::filesystem::is_regular_file(state)) return Kind::file;
    return Kind::other;
}
#ifdef _WIN32
struct Handle {
    HANDLE value{INVALID_HANDLE_VALUE};
    void close() noexcept { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); value = INVALID_HANDLE_VALUE; }
    ~Handle() { close(); }
};
void openRead(Handle& handle, const std::filesystem::path& path) {
    handle.value = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                               FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (handle.value == INVALID_HANDLE_VALUE) {
        const auto code = GetLastError();
        if (code == ERROR_SHARING_VIOLATION || code == ERROR_LOCK_VIOLATION) fail(MacImportErrorCode::conflict, "File is in use by another program: " + show(path));
        fail(MacImportErrorCode::unavailable, "Cannot open " + show(path));
    }
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(handle.value, &info) || (info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)))
        fail(MacImportErrorCode::invalid, "Not an ordinary file: " + show(path));
}
void openCreate(Handle& handle, const std::filesystem::path& path) {
    handle.value = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (handle.value == INVALID_HANDLE_VALUE) fail(MacImportErrorCode::unavailable, "Cannot create " + show(path));
}
std::size_t readSome(Handle& handle, char* buffer, std::size_t size, const std::filesystem::path& path) {
    DWORD count{};
    if (!ReadFile(handle.value, buffer, static_cast<DWORD>(std::min<std::size_t>(size, 1 << 20)), &count, nullptr)) fail(MacImportErrorCode::unavailable, "Read failed: " + show(path));
    return count;
}
void writeAll(Handle& handle, const char* data, std::size_t size, const std::filesystem::path& path) {
    while (size) {
        DWORD count{};
        if (!WriteFile(handle.value, data, static_cast<DWORD>(std::min<std::size_t>(size, 1 << 20)), &count, nullptr) || !count) fail(MacImportErrorCode::unavailable, "Write failed: " + show(path));
        data += count; size -= count;
    }
}
void flush(Handle& handle, const std::filesystem::path& path) { if (!FlushFileBuffers(handle.value)) fail(MacImportErrorCode::unavailable, "Flush failed: " + show(path)); }
#else
struct Handle {
    int value{-1};
    void close() noexcept { if (value >= 0) ::close(value); value = -1; }
    ~Handle() { close(); }
};
void openRead(Handle& handle, const std::filesystem::path& path) {
    handle.value = ::open(path.c_str(), O_RDONLY | O_NOFOLLOW);
    if (handle.value < 0) fail(errno == ELOOP ? MacImportErrorCode::invalid : MacImportErrorCode::unavailable, "Cannot open " + show(path));
    struct stat info {};
    if (::fstat(handle.value, &info) != 0 || !S_ISREG(info.st_mode)) fail(MacImportErrorCode::invalid, "Not an ordinary file: " + show(path));
}
void openCreate(Handle& handle, const std::filesystem::path& path) {
    handle.value = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
    if (handle.value < 0) fail(MacImportErrorCode::unavailable, "Cannot create " + show(path));
}
std::size_t readSome(Handle& handle, char* buffer, std::size_t size, const std::filesystem::path& path) {
    for (;;) {
        const auto count = ::read(handle.value, buffer, size);
        if (count < 0 && errno == EINTR) continue;
        if (count < 0) fail(MacImportErrorCode::unavailable, "Read failed: " + show(path));
        return static_cast<std::size_t>(count);
    }
}
void writeAll(Handle& handle, const char* data, std::size_t size, const std::filesystem::path& path) {
    while (size) {
        const auto count = ::write(handle.value, data, size);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) fail(MacImportErrorCode::unavailable, "Write failed: " + show(path));
        data += count; size -= static_cast<std::size_t>(count);
    }
}
void flush(Handle& handle, const std::filesystem::path& path) { if (::fsync(handle.value) != 0) fail(MacImportErrorCode::unavailable, "Flush failed: " + show(path)); }
#endif
void parents(const std::filesystem::path& path) {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) fail(MacImportErrorCode::unavailable, "Cannot create directory for " + show(path));
    if (inspect(path.parent_path()) != Kind::directory) fail(MacImportErrorCode::invalid, "Parent is not an ordinary directory: " + show(path));
}
}

struct StreamCopy::Impl {
    std::filesystem::path from, to;
    std::uint64_t maximum{};
    std::optional<Expected> expected;
    Handle input, output;
    Sha256 hash;
    std::vector<char> buffer;
    std::uint64_t total{};
    bool opened{}, created{}, finished{};
    void open() {
        openRead(input, from);
        if (!to.empty()) {
            parents(to);
            openCreate(output, to);
            created = true;
        }
        const auto expectedSize = expected ? expected->bytes : maximum;
        buffer.resize(static_cast<std::size_t>(std::min<std::uint64_t>(1 << 20, std::max<std::uint64_t>(4096, expectedSize + 1))));
        opened = true;
    }
    [[noreturn]] void mismatch() { fail(MacImportErrorCode::corrupt, "Export file does not match its manifest digest: " + show(from)); }
    void consume(const char* data, std::size_t count) {
        if (count > (expected ? expected->bytes : maximum) - total) {
            if (expected) mismatch();
            fail(MacImportErrorCode::tooLarge, "File exceeds its import limit: " + show(from));
        }
        total += count;
        if (expected) hash.update(data, count);
        if (created) writeAll(output, data, count, to);
    }
    void finish() {
        if (created) flush(output, to);
        output.close();
        input.close();
        if (expected && (total != expected->bytes || hash.hex() != expected->sha256)) mismatch();
        finished = true;
    }
    void abandon() noexcept {
        input.close();
        output.close();
        if (created && !finished) {
            std::error_code ignored;
            std::filesystem::remove(to, ignored);
        }
    }
};

void requireOrdinaryDirectory(const std::filesystem::path& path) {
    if (inspect(path) != Kind::directory) fail(MacImportErrorCode::invalid, "Not an ordinary directory (links and junctions are refused): " + show(path));
}
bool ordinaryDirectory(const std::filesystem::path& path) { return inspect(path) == Kind::directory; }
void requireOrdinaryParents(const std::filesystem::path& base, const std::filesystem::path& relative) {
    auto cursor = base;
    const auto parent = relative.parent_path();
    for (const auto& part : parent) {
        cursor /= part;
        const auto kind = inspect(cursor);
        if (kind == Kind::missing) return;
        if (kind != Kind::directory) fail(MacImportErrorCode::invalid, "Export folders must not be links or junctions: " + show(cursor));
    }
}
std::optional<std::uint64_t> ordinaryFileSize(const std::filesystem::path& path) {
    switch (inspect(path)) {
    case Kind::missing: return {};
    case Kind::file: {
        std::error_code error;
        const auto size = std::filesystem::file_size(path, error);
        if (error) fail(MacImportErrorCode::unavailable, "Cannot read size of " + show(path));
        return static_cast<std::uint64_t>(size);
    }
    default: fail(MacImportErrorCode::invalid, "Not an ordinary file (links and junctions are refused): " + show(path));
    }
}
void copyVerified(const std::filesystem::path& from, const std::filesystem::path& to, std::uint64_t expectedBytes, std::string_view expectedSha256) {
    StreamCopy copy(from, to, expectedBytes, StreamCopy::Expected{expectedBytes, std::string(expectedSha256)});
    while (!copy.done()) copy.advance(UINT64_MAX);
}
void moveFile(const std::filesystem::path& from, const std::filesystem::path& to) {
    if (inspect(from) != Kind::file) fail(MacImportErrorCode::invalid, "Not an ordinary file: " + show(from));
    parents(to);
    renameNoReplace(from, to);
}
void copyFile(const std::filesystem::path& from, const std::filesystem::path& to, std::uint64_t maximumBytes) {
    StreamCopy copy(from, to, maximumBytes);
    while (!copy.done()) copy.advance(UINT64_MAX);
}
std::string readFile(const std::filesystem::path& path, std::size_t maximumBytes) {
    Handle input;
    openRead(input, path);
    std::string out;
    std::vector<char> buffer(1 << 16);
    for (;;) {
        const auto count = readSome(input, buffer.data(), buffer.size(), path);
        if (!count) break;
        if (count > maximumBytes - out.size()) fail(MacImportErrorCode::tooLarge, "File exceeds its import limit: " + show(path));
        out.append(buffer.data(), count);
    }
    return out;
}
std::string readPrefix(const std::filesystem::path& path, std::size_t count) {
    Handle input;
    openRead(input, path);
    std::string out(count, '\0');
    std::size_t filled{};
    while (filled < count) {
        const auto n = readSome(input, out.data() + filled, count - filled, path);
        if (!n) break;
        filled += n;
    }
    out.resize(filled);
    return out;
}
void writeNewFile(const std::filesystem::path& path, std::string_view bytes) {
    parents(path);
    Handle output;
    openCreate(output, path);
    try {
        writeAll(output, bytes.data(), bytes.size(), path);
        flush(output, path);
    } catch (...) {
        output.close();
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
        throw;
    }
}
namespace {
void plan(const std::filesystem::path& from, const std::filesystem::path& to, std::uint64_t maximumBytes, std::uint64_t& total, std::vector<PlannedFile>& out) {
    requireOrdinaryDirectory(from);
    std::error_code error;
    if (!to.empty()) {
        std::filesystem::create_directories(to, error);
        if (error) fail(MacImportErrorCode::unavailable, "Cannot create " + show(to));
    }
    std::vector<std::filesystem::path> entries;
    for (std::filesystem::directory_iterator it(from, error), end; !error && it != end; it.increment(error)) entries.push_back(it->path());
    if (error) fail(MacImportErrorCode::unavailable, "Cannot list " + show(from));
    std::sort(entries.begin(), entries.end());
    for (const auto& entry : entries) {
        const auto target = to.empty() ? std::filesystem::path{} : to / entry.filename();
        switch (inspect(entry)) {
        case Kind::directory: plan(entry, target, maximumBytes, total, out); break;
        case Kind::file: {
            const auto size = *ordinaryFileSize(entry);
            if (size > maximumBytes - std::min(total, maximumBytes)) fail(MacImportErrorCode::tooLarge, "Existing Windows data exceeds the import budget");
            total += size;
            out.push_back({entry, target, size});
            break;
        }
        case Kind::missing: break;
        case Kind::other: fail(MacImportErrorCode::invalid, "Existing Windows data contains a link or reparse point: " + show(entry));
        }
    }
}
}
std::vector<PlannedFile> planTree(const std::filesystem::path& from, const std::filesystem::path& to, std::uint64_t maximumBytes) {
    std::vector<PlannedFile> out;
    std::uint64_t total{};
    plan(from, to, maximumBytes, total, out);
    return out;
}
void renameNoReplace(const std::filesystem::path& from, const std::filesystem::path& to) {
#ifdef _WIN32
    if (!MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_WRITE_THROUGH)) {
        const auto code = GetLastError();
        if (code == ERROR_SHARING_VIOLATION || code == ERROR_ACCESS_DENIED || code == ERROR_LOCK_VIOLATION)
            fail(MacImportErrorCode::conflict, "A file in " + show(from) + " is in use; close EndfieldHUD windows and try again");
        fail(MacImportErrorCode::unavailable, "Cannot move " + show(from) + " to " + show(to));
    }
#else
    int result;
#if defined(__APPLE__)
    result = ::renamex_np(from.c_str(), to.c_str(), RENAME_EXCL);
#elif defined(__linux__) && defined(SYS_renameat2)
    result = static_cast<int>(::syscall(SYS_renameat2, AT_FDCWD, from.c_str(), AT_FDCWD, to.c_str(), 1u /* RENAME_NOREPLACE */));
#else
    if (inspect(to) != Kind::missing) { errno = EEXIST; result = -1; }
    else result = ::rename(from.c_str(), to.c_str());
#endif
    if (result != 0) fail(MacImportErrorCode::unavailable, "Cannot move " + show(from) + " to " + show(to));
#endif
}
bool removeTree(const std::filesystem::path& path) noexcept {
    try {
        const auto kind = inspect(path);
        std::error_code error;
        if (kind == Kind::missing) return true;
        if (kind == Kind::directory) {
            std::vector<std::filesystem::path> entries;
            for (std::filesystem::directory_iterator it(path, error), end; !error && it != end; it.increment(error)) entries.push_back(it->path());
            if (error) return false;
            bool ok = true;
            for (const auto& entry : entries) ok = removeTree(entry) && ok;
            return std::filesystem::remove(path, error) && ok;
        }
        // Files and links (never their targets) are removed directly.
        return std::filesystem::remove(path, error) || !error;
    } catch (...) { return false; }
}
}

namespace ehud::migration::files {
StreamCopy::StreamCopy(std::filesystem::path from, std::filesystem::path to, std::uint64_t maximumBytes, std::optional<Expected> expected)
    : impl_(std::make_unique<Impl>()) {
    impl_->from = std::move(from);
    impl_->to = std::move(to);
    impl_->maximum = expected ? expected->bytes : maximumBytes;
    impl_->expected = std::move(expected);
}
StreamCopy::~StreamCopy() { if (impl_) impl_->abandon(); }
bool StreamCopy::done() const noexcept { return impl_->finished; }
std::uint64_t StreamCopy::processed() const noexcept { return impl_->total; }
std::uint64_t StreamCopy::advance(std::uint64_t budget) {
    auto& i = *impl_;
    if (i.finished) return 0;
    try {
        if (!i.opened) i.open();
        std::uint64_t used{};
        while (used < budget) {
            const auto want = static_cast<std::size_t>(std::min<std::uint64_t>(i.buffer.size(), budget - used));
            const auto count = readSome(i.input, i.buffer.data(), want, i.from);
            if (!count) { i.finish(); return used; }
            i.consume(i.buffer.data(), count);
            used += count;
        }
        // The budget ended exactly at the manifest size: confirm the end now,
        // so a completed file never needs another job.
        if (i.expected && i.total == i.expected->bytes) {
            char probe{};
            if (readSome(i.input, &probe, 1, i.from)) i.mismatch();
            i.finish();
        }
        return used;
    } catch (...) {
        i.abandon();
        throw;
    }
}
}
