#pragma once
// EndfieldHUD Windows package format (portable; no file system or OS calls).
//
// Release artifacts produced by packaging/stage_release.cmake from the
// EndfieldHUD install tree (cmake --install ... --component EndfieldHUD):
//
//   EndfieldHUD-<release>-build<N>-windows-x64-portable.zip
//       EndfieldHUD/ EndfieldHUD.exe, fonts/, resources/ (the install tree)
//   EndfieldHUD-<release>-build<N>-windows-x64-setup.exe
//       [setup stub PE image][payload][zero padding][trailer]
//
// Payload (all integers little-endian):
//   "EHUDPAY1" | u64 manifest bytes | manifest JSON (UTF-8) | data region
// Each manifest file names its stored range in the data region, its installed
// size, encoding (identity, or Windows Compression API LZMS) and the SHA-256
// of its installed bytes. Ranges are contiguous in manifest (path) order.
//
// Trailer (64 bytes, ends 8-byte aligned so Authenticode adds no padding):
//   "EHUDSFX1" | u64 payload offset | u64 payload bytes | SHA-256 of the
//   payload bytes | "EHUDSFXE"
// A signed setup carries its certificate table after the trailer; the
// reader locates the trailer at the end of the signed content (PE security
// directory), never by scanning.
#include <array>
#include <compare>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace endfield::packaging {
class PackageError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

inline constexpr std::string_view productName = "EndfieldHUD";
inline constexpr std::string_view publisherName = "DDDuoDuo";
inline constexpr std::string_view productURL = "https://github.com/DDDuoDuo/EndfieldHUD";
inline constexpr std::string_view payloadFormat = "EndfieldHUD.Windows.Payload";
inline constexpr std::string_view installManifestFormat = "EndfieldHUD.Windows.Install";
inline constexpr int payloadSchema = 1;
inline constexpr int installManifestSchema = 1;
inline constexpr std::array<std::uint8_t, 8> payloadMagic{'E', 'H', 'U', 'D', 'P', 'A', 'Y', '1'};
inline constexpr std::array<std::uint8_t, 8> trailerMagic{'E', 'H', 'U', 'D', 'S', 'F', 'X', '1'};
inline constexpr std::array<std::uint8_t, 8> trailerEndMagic{'E', 'H', 'U', 'D', 'S', 'F', 'X', 'E'};
inline constexpr std::size_t trailerBytes = 64;
inline constexpr std::size_t payloadHeaderBytes = 16;

// Bounds (a damaged or hostile payload is rejected before any allocation).
inline constexpr std::size_t maximumManifestBytes = 8u * 1024u * 1024u;
inline constexpr std::size_t maximumFiles = 16384;
inline constexpr std::size_t maximumPathBytes = 180;
inline constexpr std::uint64_t maximumFileBytes = 1024ull * 1024ull * 1024ull;        // identity files stream
inline constexpr std::uint64_t maximumEncodedFileBytes = 256ull * 1024ull * 1024ull;  // compressed files decode in memory
inline constexpr std::uint64_t maximumPayloadBytes = 4ull * 1024ull * 1024ull * 1024ull;

// Installed layout. The install manifest is written by the installer, never
// shipped in a payload; the uninstaller is added to the payload only.
inline constexpr std::string_view applicationExecutable = "EndfieldHUD.exe";
inline constexpr std::string_view uninstallerExecutable = "uninstall.exe";
inline constexpr std::string_view installManifestName = "EndfieldHUD.install.json";

enum class Encoding : std::uint8_t { identity, lzms };
std::string_view encodingName(Encoding) noexcept;
std::optional<Encoding> encodingFromName(std::string_view) noexcept;

// Release version from windows/source-authority.json ("v1.2.0", build 18).
struct ProductVersion {
    int major{}, minor{}, patch{}, build{};
    auto operator<=>(const ProductVersion&) const = default;
    std::string release() const;  // "1.2.0"
    std::string display() const;   // "1.2.0 (18)"
    std::string dotted() const;    // "1.2.0.18" (uninstall entry DisplayVersion)
};
std::optional<ProductVersion> parseProductVersion(std::string_view release, std::int64_t build) noexcept;
std::optional<ProductVersion> parseDottedVersion(std::string_view dotted) noexcept;

struct PayloadFile {
    std::string path;     // relative, '/'-separated (validPackagePath)
    std::uint64_t bytes{};
    std::string sha256;   // installed bytes, 64 lowercase hex digits
    Encoding encoding{Encoding::identity};
    std::uint64_t offset{}, stored{};
    bool operator==(const PayloadFile&) const = default;
};
struct PayloadManifest {
    ProductVersion version;
    std::string commit;            // source commit (40 hex digits)
    std::string architecture{"x64"};
    bool complete{true};           // every required package resource verified when staged
    std::vector<PayloadFile> files;
    std::uint64_t installedBytes() const noexcept;
    std::uint64_t storedBytes() const noexcept;
    const PayloadFile* find(std::string_view path) const noexcept;
};
std::string encodePayloadManifest(const PayloadManifest&);
// Validates everything: schema, version, paths (unique case-insensitively, no
// file below another file), required executables, encodings, sizes and the
// contiguous ranges, which must exactly cover `dataBytes`.
PayloadManifest decodePayloadManifest(std::string_view json, std::uint64_t dataBytes);

// A relative package path: '/'-separated printable ASCII components, no
// empty/./.. components, no Windows-reserved characters or device names, no
// leading/trailing space or trailing dot, at most maximumPathBytes.
bool validPackagePath(std::string_view) noexcept;
std::string foldedPackagePath(std::string_view); // ASCII case fold for collision checks
bool validSha256Hex(std::string_view) noexcept;
std::string hex(std::span<const std::uint8_t>);

struct Trailer {
    std::uint64_t payloadOffset{}, payloadBytes{};
    std::array<std::uint8_t, 32> sha256{};
    bool operator==(const Trailer&) const = default;
};
std::array<std::uint8_t, trailerBytes> encodeTrailer(const Trailer&) noexcept;
std::optional<Trailer> decodeTrailer(std::span<const std::uint8_t> bytes) noexcept; // exactly trailerBytes
std::array<std::uint8_t, payloadHeaderBytes> encodePayloadHeader(std::uint64_t manifestBytes) noexcept;
std::optional<std::uint64_t> decodePayloadHeader(std::span<const std::uint8_t> bytes) noexcept;
// Zero bytes inserted before the trailer so that `contentBytes + padding +
// trailerBytes` is 8-byte aligned.
std::size_t trailerPadding(std::uint64_t contentBytes) noexcept;

// PE32+ x64 layout of a setup file. `imageEnd` is the end of the last
// section's raw data (where an appended payload may begin); `contentEnd` is
// the end of the content covered by an Authenticode signature: the
// certificate table offset when the security directory names a table that
// ends the file, otherwise the file size. `headers` are the first bytes of
// the file (4096 suffice). Throws PackageError for anything else, including a
// certificate table that does not end the file.
struct PortableExecutableLayout {
    std::uint64_t imageEnd{}, contentEnd{};
    bool signatureTable{};
};
inline constexpr std::size_t portableExecutableHeaderBytes = 4096;
PortableExecutableLayout inspectPortableExecutable(std::span<const std::uint8_t> headers, std::uint64_t fileBytes);
inline constexpr std::size_t trailerSearchBytes = trailerBytes + 14; // trailer, 7 alignment bytes after it, 7 padding bytes before
// Validated payload location from the bytes at the end of the signed
// content: `tail` holds the last min(contentEnd, trailerSearchBytes) bytes
// before `contentEnd`. A signing tool may add up to 7 zero bytes of
// alignment after the trailer; anything else is rejected. The payload must
// start at or after `imageEnd` and end at the exact zero padding before the
// trailer.
Trailer locateTrailer(std::span<const std::uint8_t> tail, std::uint64_t contentEnd, std::uint64_t imageEnd);

// Install manifest (EndfieldHUD.install.json in the install root): the exact
// files the installer wrote, read by upgrade and uninstall. Foreign files in
// the install root are never removed.
struct InstalledFile {
    std::string path;
    std::uint64_t bytes{};
    std::string sha256;
    bool operator==(const InstalledFile&) const = default;
};
struct InstallManifest {
    ProductVersion version;
    std::string commit;
    std::string installedAt;  // UTC ISO-8601 "2026-10-10T12:00:00Z"
    std::vector<InstalledFile> files;
    std::uint64_t installedBytes() const noexcept;
};
InstallManifest installManifestFor(const PayloadManifest&, std::string installedAt);
std::string encodeInstallManifest(const InstallManifest&);
InstallManifest decodeInstallManifest(std::string_view json);

// Release artifact names (never the Mac namespace; Windows tags will use
// "windows-v<release>" once DDDuoDuo chooses a Windows release).
std::string releaseArtifactStem(const ProductVersion&); // EndfieldHUD-1.2.0-build18-windows-x64
} // namespace endfield::packaging
