#pragma once
// Windows side of the package format (packaging/package_format.hpp): SHA-256
// (BCrypt), LZMS (Windows Compression API, cabinet.dll — an OS component, no
// third-party compressor), the setup packer used at staging time and the
// verified payload reader used by EndfieldHUD-Setup.exe.
#ifdef _WIN32
#include "packaging/package_format.hpp"
#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace endfield::packaging {
class Sha256Stream final {
public:
    Sha256Stream();
    ~Sha256Stream();
    Sha256Stream(const Sha256Stream&) = delete;
    Sha256Stream& operator=(const Sha256Stream&) = delete;
    void update(std::span<const std::uint8_t>);
    std::array<std::uint8_t, 32> finish();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
std::string sha256Hex(std::span<const std::uint8_t>);
std::string fileSha256Hex(const std::filesystem::path&, std::uint64_t* bytes = nullptr);

std::vector<std::uint8_t> compressLzms(std::span<const std::uint8_t>);
// Decodes exactly `out.size()` bytes; throws PackageError otherwise.
void decompressLzms(std::span<const std::uint8_t> stored, std::span<std::uint8_t> out);

// --- Packer (staging) --------------------------------------------------------
struct PackSource {
    std::string path;            // package path
    std::filesystem::path file;  // regular source file
};
// Every regular file below `root` (reparse points and links are rejected),
// with '/'-separated relative package paths.
std::vector<PackSource> collectTree(const std::filesystem::path& root);
struct PackOptions {
    ProductVersion version;
    std::string commit;
    bool complete{true};
    Encoding encoding{Encoding::lzms}; // a file is stored as identity when encoding does not shrink it
};
// stub + payload + padding + trailer -> `output` (written beside it, then
// renamed). The stub must be an unsigned PE32+ x64 image without a payload:
// sign the complete setup afterwards.
PayloadManifest writeSetup(const std::filesystem::path& stub, std::vector<PackSource> sources,
                           const PackOptions&, const std::filesystem::path& output);

// --- Reader (setup) -----------------------------------------------------------
// Opens a setup file (normally the running EndfieldHUD-Setup.exe), locates the
// trailer at the end of its signed content, verifies the payload SHA-256 and
// decodes the manifest before anything else happens. The file stays open
// without write sharing until destruction.
class SetupPayload final {
public:
    static SetupPayload open(const std::filesystem::path& setup);
    SetupPayload(SetupPayload&&) noexcept;
    SetupPayload& operator=(SetupPayload&&) noexcept;
    ~SetupPayload();
    const PayloadManifest& manifest() const noexcept;
    std::uint64_t payloadBytes() const noexcept;
    bool signatureTable() const noexcept;
    // Writes one file to `destination` (which must not exist), verifies its
    // size and SHA-256, flushes it to disk. `progress` receives stored bytes.
    void extract(const PayloadFile&, const std::filesystem::path& destination,
                 const std::function<void(std::uint64_t)>& progress = {}) const;
private:
    struct Impl;
    explicit SetupPayload(std::unique_ptr<Impl>);
    std::unique_ptr<Impl> impl_;
};
} // namespace endfield::packaging
#endif
