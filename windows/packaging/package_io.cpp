#include "packaging/package_io.hpp"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#include <compressapi.h>
#include <algorithm>
#include <limits>
#include <utility>

namespace endfield::packaging {
namespace {
namespace fs = std::filesystem;
constexpr std::size_t chunkBytes = 1024 * 1024;
std::string pathText(const fs::path& path) {
    const auto text = path.generic_u8string();
    return std::string(text.begin(), text.end());
}

[[noreturn]] void fail(const std::string& what) { throw PackageError(what); }
[[noreturn]] void windowsFailure(const std::string& what, DWORD error = GetLastError()) {
    throw PackageError(what + " (Windows error " + std::to_string(error) + ")");
}
struct Handle {
    HANDLE value{INVALID_HANDLE_VALUE};
    Handle() = default;
    explicit Handle(HANDLE h) : value(h) {}
    Handle(Handle&& other) noexcept : value(std::exchange(other.value, INVALID_HANDLE_VALUE)) {}
    Handle& operator=(Handle&& other) noexcept {
        if (this != &other) { reset(); value = std::exchange(other.value, INVALID_HANDLE_VALUE); }
        return *this;
    }
    ~Handle() { reset(); }
    void reset() noexcept { if (value != INVALID_HANDLE_VALUE && value) CloseHandle(value); value = INVALID_HANDLE_VALUE; }
    explicit operator bool() const noexcept { return value != INVALID_HANDLE_VALUE && value; }
};
std::uint64_t fileSize(HANDLE file) {
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size)) windowsFailure("Reading a file size failed");
    return static_cast<std::uint64_t>(size.QuadPart);
}
void readAt(HANDLE file, std::uint64_t offset, std::span<std::uint8_t> out) {
    std::size_t done = 0;
    while (done < out.size()) {
        const auto want = static_cast<DWORD>(std::min<std::size_t>(out.size() - done, 64u * 1024u * 1024u));
        OVERLAPPED at{};
        const auto position = offset + done;
        at.Offset = static_cast<DWORD>(position);
        at.OffsetHigh = static_cast<DWORD>(position >> 32);
        DWORD read{};
        if (!ReadFile(file, out.data() + done, want, &read, &at)) windowsFailure("Reading the setup file failed");
        if (read == 0) fail("The setup file ended early");
        done += read;
    }
}
void writeAll(HANDLE file, std::span<const std::uint8_t> bytes) {
    std::size_t done = 0;
    while (done < bytes.size()) {
        const auto want = static_cast<DWORD>(std::min<std::size_t>(bytes.size() - done, 64u * 1024u * 1024u));
        DWORD written{};
        if (!WriteFile(file, bytes.data() + done, want, &written, nullptr) || written == 0) windowsFailure("Writing a file failed");
        done += written;
    }
}
void readSequential(HANDLE file, std::span<std::uint8_t> out) {
    std::size_t done = 0;
    while (done < out.size()) {
        const auto want = static_cast<DWORD>(std::min<std::size_t>(out.size() - done, 64u * 1024u * 1024u));
        DWORD read{};
        if (!ReadFile(file, out.data() + done, want, &read, nullptr)) windowsFailure("Reading a file failed");
        if (read == 0) fail("A file ended early");
        done += read;
    }
}
Handle openRead(const fs::path& path, DWORD share = FILE_SHARE_READ) {
    Handle h(CreateFileW(path.c_str(), GENERIC_READ, share, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (!h) windowsFailure("Opening " + pathText(path.filename()) + " failed");
    FILE_ATTRIBUTE_TAG_INFO info{};
    if (!GetFileInformationByHandleEx(h.value, FileAttributeTagInfo, &info, sizeof(info))) windowsFailure("Inspecting a file failed");
    if (info.FileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) fail("Not a regular file: " + pathText(path.filename()));
    return h;
}
std::vector<std::uint8_t> readWholeFile(const fs::path& path, std::uint64_t limit) {
    auto h = openRead(path);
    const auto size = fileSize(h.value);
    if (size > limit) fail("File is too large: " + pathText(path.filename()));
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    readSequential(h.value, bytes);
    return bytes;
}
struct Compressor {
    COMPRESSOR_HANDLE value{};
    ~Compressor() { if (value) CloseCompressor(value); }
};
struct Decompressor {
    DECOMPRESSOR_HANDLE value{};
    ~Decompressor() { if (value) CloseDecompressor(value); }
};
} // namespace

struct Sha256Stream::Impl {
    BCRYPT_HASH_HANDLE hash{};
    bool finished{};
    ~Impl() { if (hash) BCryptDestroyHash(hash); }
};
Sha256Stream::Sha256Stream() : impl_(std::make_unique<Impl>()) {
    if (!BCRYPT_SUCCESS(BCryptCreateHash(BCRYPT_SHA256_ALG_HANDLE, &impl_->hash, nullptr, 0, nullptr, 0, 0))) fail("SHA-256 is unavailable");
}
Sha256Stream::~Sha256Stream() = default;
void Sha256Stream::update(std::span<const std::uint8_t> bytes) {
    if (impl_->finished) fail("SHA-256 already finished");
    while (!bytes.empty()) {
        const auto n = static_cast<ULONG>(std::min<std::size_t>(bytes.size(), std::numeric_limits<ULONG>::max() / 2));
        if (!BCRYPT_SUCCESS(BCryptHashData(impl_->hash, const_cast<PUCHAR>(bytes.data()), n, 0))) fail("SHA-256 failed");
        bytes = bytes.subspan(n);
    }
}
std::array<std::uint8_t, 32> Sha256Stream::finish() {
    std::array<std::uint8_t, 32> out{};
    if (impl_->finished || !BCRYPT_SUCCESS(BCryptFinishHash(impl_->hash, out.data(), static_cast<ULONG>(out.size()), 0))) fail("SHA-256 failed");
    impl_->finished = true;
    return out;
}
std::string sha256Hex(std::span<const std::uint8_t> bytes) {
    Sha256Stream hash;
    hash.update(bytes);
    const auto digest = hash.finish();
    return hex(digest);
}
std::string fileSha256Hex(const fs::path& path, std::uint64_t* bytesOut) {
    auto h = openRead(path);
    Sha256Stream hash;
    std::vector<std::uint8_t> buffer(chunkBytes);
    std::uint64_t total = 0;
    for (;;) {
        DWORD read{};
        if (!ReadFile(h.value, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr)) windowsFailure("Reading a file failed");
        if (!read) break;
        hash.update(std::span(buffer.data(), read));
        total += read;
    }
    if (bytesOut) *bytesOut = total;
    return hex(hash.finish());
}

std::vector<std::uint8_t> compressLzms(std::span<const std::uint8_t> input) {
    if (input.empty()) fail("Nothing to compress");
    Compressor c;
    if (!CreateCompressor(COMPRESS_ALGORITHM_LZMS, nullptr, &c.value)) windowsFailure("The LZMS compressor is unavailable");
    SIZE_T needed{};
    if (Compress(c.value, input.data(), input.size(), nullptr, 0, &needed) || GetLastError() != ERROR_INSUFFICIENT_BUFFER || !needed)
        windowsFailure("LZMS compression could not size its output");
    std::vector<std::uint8_t> out(needed);
    SIZE_T size{};
    if (!Compress(c.value, input.data(), input.size(), out.data(), out.size(), &size)) windowsFailure("LZMS compression failed");
    out.resize(size);
    return out;
}
void decompressLzms(std::span<const std::uint8_t> stored, std::span<std::uint8_t> out) {
    Decompressor d;
    if (!CreateDecompressor(COMPRESS_ALGORITHM_LZMS, nullptr, &d.value)) windowsFailure("The LZMS decompressor is unavailable");
    SIZE_T written{};
    if (!Decompress(d.value, stored.data(), stored.size(), out.data(), out.size(), &written) || written != out.size())
        fail("A compressed setup file is damaged");
}

std::vector<PackSource> collectTree(const fs::path& root) {
    const auto rootAttributes = GetFileAttributesW(root.c_str());
    if (rootAttributes == INVALID_FILE_ATTRIBUTES || !(rootAttributes & FILE_ATTRIBUTE_DIRECTORY) || (rootAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
        fail("The package tree is not an ordinary folder");
    std::vector<PackSource> out;
    std::vector<fs::path> pending{root};
    while (!pending.empty()) {
        const auto folder = pending.back();
        pending.pop_back();
        WIN32_FIND_DATAW entry{};
        const auto pattern = folder / L"*";
        HANDLE find = FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &entry, FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
        if (find == INVALID_HANDLE_VALUE) windowsFailure("Listing the package tree failed");
        struct Close { HANDLE h; ~Close() { FindClose(h); } } close{find};
        do {
            const std::wstring_view name = entry.cFileName;
            if (name == L"." || name == L"..") continue;
            const auto path = folder / entry.cFileName;
            if (entry.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) fail("The package tree contains a link: " + pathText(path.lexically_relative(root)));
            if (entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) { pending.push_back(path); continue; }
            const auto relative = path.lexically_relative(root).generic_u8string();
            std::string packagePath(relative.begin(), relative.end());
            if (!validPackagePath(packagePath)) fail("The package tree contains an unsupported file name: " + packagePath);
            out.push_back({std::move(packagePath), path});
        } while (FindNextFileW(find, &entry));
        if (GetLastError() != ERROR_NO_MORE_FILES) windowsFailure("Listing the package tree failed");
    }
    std::sort(out.begin(), out.end(), [](const PackSource& a, const PackSource& b) { return a.path < b.path; });
    return out;
}

PayloadManifest writeSetup(const fs::path& stub, std::vector<PackSource> sources, const PackOptions& options, const fs::path& output) {
    std::sort(sources.begin(), sources.end(), [](const PackSource& a, const PackSource& b) { return a.path < b.path; });
    const auto stubBytes = readWholeFile(stub, 64ull * 1024ull * 1024ull);
    const auto layout = inspectPortableExecutable(std::span(stubBytes).first(std::min(stubBytes.size(), portableExecutableHeaderBytes)), stubBytes.size());
    if (layout.signatureTable || layout.imageEnd != stubBytes.size())
        fail("The setup stub must be an unsigned image without appended data (sign the finished setup instead)");

    const auto dataPath = fs::path(output.native() + L".data.partial");
    Handle data(CreateFileW(dataPath.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                            FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr));
    if (!data) windowsFailure("Creating the payload data file failed");
    PayloadManifest m;
    m.version = options.version;
    m.commit = options.commit;
    m.complete = options.complete;
    std::uint64_t offset = 0;
    std::vector<std::uint8_t> chunk(chunkBytes);
    for (const auto& source : sources) {
        PayloadFile f;
        f.path = source.path;
        f.offset = offset;
        auto h = openRead(source.file);
        const auto size = fileSize(h.value);
        if (size > maximumFileBytes) fail("A package file is too large: " + source.path);
        f.bytes = size;
        if (size <= maximumEncodedFileBytes) {
            std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
            readSequential(h.value, bytes);
            f.sha256 = sha256Hex(bytes);
            std::vector<std::uint8_t> compressed;
            if (options.encoding == Encoding::lzms && !bytes.empty()) {
                compressed = compressLzms(bytes);
                std::vector<std::uint8_t> check(bytes.size());
                decompressLzms(compressed, check);
                if (check != bytes) fail("LZMS did not reproduce " + source.path);
            }
            const bool encoded = !compressed.empty() && compressed.size() < bytes.size();
            f.encoding = encoded ? Encoding::lzms : Encoding::identity;
            const auto& stored = encoded ? compressed : bytes;
            writeAll(data.value, stored);
            f.stored = stored.size();
        } else {
            Sha256Stream hash;
            for (std::uint64_t done = 0; done < size;) {
                const auto n = static_cast<std::size_t>(std::min<std::uint64_t>(chunk.size(), size - done));
                readSequential(h.value, std::span(chunk.data(), n));
                hash.update(std::span(chunk.data(), n));
                writeAll(data.value, std::span(chunk.data(), n));
                done += n;
            }
            f.sha256 = hex(hash.finish());
            f.encoding = Encoding::identity;
            f.stored = size;
        }
        offset += f.stored;
        m.files.push_back(std::move(f));
    }
    const auto manifest = encodePayloadManifest(m);
    (void)decodePayloadManifest(manifest, offset); // the reader's validation, before writing anything

    const auto partial = fs::path(output.native() + L".partial");
    Handle out(CreateFileW(partial.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!out) windowsFailure("Creating the setup file failed");
    try {
        writeAll(out.value, stubBytes);
        Sha256Stream hash;
        const auto header = encodePayloadHeader(manifest.size());
        writeAll(out.value, header);
        hash.update(header);
        const auto manifestBytes = std::span(reinterpret_cast<const std::uint8_t*>(manifest.data()), manifest.size());
        writeAll(out.value, manifestBytes);
        hash.update(manifestBytes);
        LARGE_INTEGER zero{};
        if (!SetFilePointerEx(data.value, zero, nullptr, FILE_BEGIN)) windowsFailure("Rewinding the payload data failed");
        for (std::uint64_t done = 0; done < offset;) {
            const auto n = static_cast<std::size_t>(std::min<std::uint64_t>(chunk.size(), offset - done));
            readSequential(data.value, std::span(chunk.data(), n));
            writeAll(out.value, std::span(chunk.data(), n));
            hash.update(std::span(chunk.data(), n));
            done += n;
        }
        Trailer t;
        t.payloadOffset = stubBytes.size();
        t.payloadBytes = payloadHeaderBytes + manifest.size() + offset;
        t.sha256 = hash.finish();
        const std::vector<std::uint8_t> padding(trailerPadding(t.payloadOffset + t.payloadBytes), 0);
        writeAll(out.value, padding);
        writeAll(out.value, encodeTrailer(t));
        if (!FlushFileBuffers(out.value)) windowsFailure("Flushing the setup file failed");
        out.reset();
        if (!MoveFileExW(partial.c_str(), output.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) windowsFailure("Publishing the setup file failed");
    } catch (...) {
        out.reset();
        DeleteFileW(partial.c_str());
        throw;
    }
    return m;
}

struct SetupPayload::Impl {
    Handle file;
    PayloadManifest manifest;
    std::uint64_t dataOffset{}, payloadBytes{};
    bool signatureTable{};
};
SetupPayload::SetupPayload(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
SetupPayload::SetupPayload(SetupPayload&&) noexcept = default;
SetupPayload& SetupPayload::operator=(SetupPayload&&) noexcept = default;
SetupPayload::~SetupPayload() = default;
const PayloadManifest& SetupPayload::manifest() const noexcept { return impl_->manifest; }
std::uint64_t SetupPayload::payloadBytes() const noexcept { return impl_->payloadBytes; }
bool SetupPayload::signatureTable() const noexcept { return impl_->signatureTable; }

SetupPayload SetupPayload::open(const fs::path& setup) {
    auto impl = std::make_unique<Impl>();
    // Read sharing only: the running image keeps its own mapping, and nobody
    // can rewrite the payload while it is being verified or extracted.
    impl->file = Handle(CreateFileW(setup.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!impl->file) windowsFailure("Opening the setup file failed");
    const auto size = fileSize(impl->file.value);
    std::vector<std::uint8_t> headers(static_cast<std::size_t>(std::min<std::uint64_t>(size, portableExecutableHeaderBytes)));
    readAt(impl->file.value, 0, headers);
    const auto layout = inspectPortableExecutable(headers, size);
    impl->signatureTable = layout.signatureTable;
    std::vector<std::uint8_t> tail(static_cast<std::size_t>(std::min<std::uint64_t>(layout.contentEnd, trailerSearchBytes)));
    readAt(impl->file.value, layout.contentEnd - tail.size(), tail);
    const auto trailer = locateTrailer(tail, layout.contentEnd, layout.imageEnd);
    Sha256Stream hash;
    std::vector<std::uint8_t> chunk(chunkBytes);
    for (std::uint64_t done = 0; done < trailer.payloadBytes;) {
        const auto n = static_cast<std::size_t>(std::min<std::uint64_t>(chunk.size(), trailer.payloadBytes - done));
        readAt(impl->file.value, trailer.payloadOffset + done, std::span(chunk.data(), n));
        hash.update(std::span(chunk.data(), n));
        done += n;
    }
    if (hash.finish() != trailer.sha256) fail("The setup payload is damaged");
    std::array<std::uint8_t, payloadHeaderBytes> header{};
    readAt(impl->file.value, trailer.payloadOffset, header);
    const auto manifestBytes = decodePayloadHeader(header);
    if (!manifestBytes || *manifestBytes > trailer.payloadBytes - payloadHeaderBytes) fail("The setup payload header is invalid");
    std::string manifest(static_cast<std::size_t>(*manifestBytes), '\0');
    readAt(impl->file.value, trailer.payloadOffset + payloadHeaderBytes, std::span(reinterpret_cast<std::uint8_t*>(manifest.data()), manifest.size()));
    const auto dataBytes = trailer.payloadBytes - payloadHeaderBytes - *manifestBytes;
    impl->manifest = decodePayloadManifest(manifest, dataBytes);
    impl->dataOffset = trailer.payloadOffset + payloadHeaderBytes + *manifestBytes;
    impl->payloadBytes = trailer.payloadBytes;
    return SetupPayload(std::move(impl));
}

void SetupPayload::extract(const PayloadFile& f, const fs::path& destination, const std::function<void(std::uint64_t)>& progress) const {
    Handle out(CreateFileW(destination.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!out) windowsFailure("Creating " + f.path + " failed");
    try {
        Sha256Stream hash;
        const auto at = impl_->dataOffset + f.offset;
        if (f.encoding == Encoding::identity) {
            std::vector<std::uint8_t> chunk(static_cast<std::size_t>(std::min<std::uint64_t>(chunkBytes, std::max<std::uint64_t>(f.bytes, 1))));
            for (std::uint64_t done = 0; done < f.bytes;) {
                const auto n = static_cast<std::size_t>(std::min<std::uint64_t>(chunk.size(), f.bytes - done));
                readAt(impl_->file.value, at + done, std::span(chunk.data(), n));
                hash.update(std::span(chunk.data(), n));
                writeAll(out.value, std::span(chunk.data(), n));
                done += n;
                if (progress) progress(n);
            }
        } else {
            std::vector<std::uint8_t> stored(static_cast<std::size_t>(f.stored));
            readAt(impl_->file.value, at, stored);
            std::vector<std::uint8_t> decoded(static_cast<std::size_t>(f.bytes));
            decompressLzms(stored, decoded);
            hash.update(decoded);
            writeAll(out.value, decoded);
            if (progress) progress(f.stored);
        }
        if (hex(hash.finish()) != f.sha256) fail("A setup file is damaged: " + f.path);
        if (!FlushFileBuffers(out.value)) windowsFailure("Flushing " + f.path + " failed");
    } catch (...) {
        out.reset();
        DeleteFileW(destination.c_str());
        throw;
    }
}
} // namespace endfield::packaging
#endif
