#pragma once
#include "core/data/json.hpp"
#include <cstdint>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace ehud::migration {
// invalid/newerVersion/tooLarge reject one store; the others fail the whole
// import (corrupt = the export does not match its manifest or is unsafe).
enum class MacImportErrorCode { invalid, newerVersion, tooLarge, unavailable, conflict, cancelled, corrupt };
class MacImportError final : public std::runtime_error {
public:
    MacImportError(MacImportErrorCode code, std::string message) : std::runtime_error(std::move(message)), code_(code) {}
    MacImportErrorCode code() const noexcept { return code_; }
private:
    MacImportErrorCode code_;
};

// ---- Account/profile-cache.json (HypergryphAccountController.Cache v1) ----
// Decoded with Swift's synthesized Codable rules (every non-optional key is
// required, enums are exact raw values, Int fields are exact integers). Role
// and binding identifiers are strings and are never parsed as numbers; dates
// keep their Foundation-epoch tokens. Credentials never live in this cache;
// any credential-like key is removed defensively and reported.
struct MacAccountImport {
    std::string bytes;                       // Windows Account/profile-cache.json
    bool profileSyncLocked{};                // syncProfile && linked && selected Endfield role
    std::vector<std::string> disconnectedRegions; // linked regions marked requiresReconnect
    std::vector<std::string> strippedKeys;   // JSON paths of removed credential-like keys
    std::size_t roles{}, snapshots{};
};
inline constexpr std::size_t macAccountCacheMaximumBytes = 1024 * 1024;
MacAccountImport importMacAccountCache(std::string_view bytes);

// ---- FileShelf/shelf.json (FileShelfStore.Archive v1) ----
// Mac records cannot authorize Windows files; they become unresolved relink
// entries with their complete original record. Rules follow FileShelfStore.decode.
struct MacShelfRecord {
    std::string id, name, typeDescription, lastKnownPath;
    std::optional<std::int64_t> byteCount;
    bool isDirectory{};
    double createdAt{};                      // Foundation epoch
    data::Json original;                     // exact original record
};
std::vector<MacShelfRecord> decodeMacShelf(std::string_view bytes);

// ---- managed images ----
enum class ImageSignature { unknown, png, jpeg, gif, tiff, heif, webp, bmp };
ImageSignature sniffImage(std::string_view header) noexcept;
std::string_view imageSignatureName(ImageSignature) noexcept;
struct PngHeader { std::uint32_t width{}, height{}; };
std::optional<PngHeader> pngHeader(std::string_view header) noexcept; // signature + IHDR
bool macManagedImageName(std::string_view name, bool allowImageSuffix) noexcept; // <UUID>.png / <UUID>.image

// ---- manifest.json written by windows/tools/mac_import_exporter.py ----
struct MacExportFile {
    std::string path;          // relative, '/'-separated, under EndfieldCharge/ or Preferences/
    std::uint64_t bytes{};
    std::string sha256;
};
struct MacExportManifest {
    std::int64_t version{};
    std::string bundleIdentifier, appVersion, build;
    double exportedAt{};       // Unix seconds
    std::vector<MacExportFile> files;
    std::map<std::string, std::int64_t> sqliteUserVersions;
    std::optional<std::string> preferences;
    std::string sha256;        // digest of the manifest bytes themselves
    const MacExportFile* find(std::string_view path) const noexcept;
};
inline constexpr std::string_view macExportFormat = "EndfieldHUD.macExport";
inline constexpr std::string_view macPreferencesDomain = "io.github.endfieldcharge.EndfieldCharge";
inline constexpr std::size_t macExportMaximumFiles = 20000;
MacExportManifest decodeMacExportManifest(std::string_view bytes);
bool validExportPath(std::string_view) noexcept;
}
