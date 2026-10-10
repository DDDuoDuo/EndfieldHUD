#include "core/migration/mac_import_codecs.hpp"
#include "core/migration/mac_import_sha256.hpp"
#include "core/migration/mac_import_settings.hpp"
#include "core/migration/plist.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <limits>
#include <set>

namespace ehud::migration {
namespace {
using data::Json;
[[noreturn]] void invalid(const std::string& message) { throw MacImportError(MacImportErrorCode::invalid, message); }
void need(bool condition, const std::string& message) { if (!condition) invalid(message); }

// Swift synthesized Codable over JSONDecoder.
const Json& required(const Json& object, std::string_view key, const std::string& where) {
    need(object.isObject() && object.contains(key) && !object[key].isNull(), where + ": missing " + std::string(key));
    return object[key];
}
bool present(const Json& object, std::string_view key) { return object.isObject() && object.contains(key) && !object[key].isNull(); }
std::string text(const Json& value, const std::string& where) { need(value.isString(), where + " must be a string"); return value.string(); }
bool flag(const Json& value, const std::string& where) { need(value.isBool(), where + " must be a Boolean"); return value.boolean(); }
double date(const Json& value, const std::string& where) {
    need(value.isNumber(), where + " must be a Date number");
    try { return value.number(); } catch (const std::exception&) { invalid(where + " is not a finite Date"); }
}
// JSONDecoder integer decoding: exact integral value within the Swift type.
template<class T> T integer(const Json& value, const std::string& where) {
    need(value.isNumber(), where + " must be an integer");
    const auto token = value.encode();
    T result{};
    const auto parsed = std::from_chars(token.data(), token.data() + token.size(), result);
    if (parsed.ec == std::errc{} && parsed.ptr == token.data() + token.size()) return result;
    double number{};
    try { number = value.number(); } catch (const std::exception&) { invalid(where + " is out of range"); }
    need(number == std::trunc(number) && number >= static_cast<double>(std::numeric_limits<T>::min()) &&
         number < static_cast<double>(std::numeric_limits<T>::max()) + 1.0, where + " is not an exact integer");
    need(std::abs(number) <= 9007199254740992.0, where + " exceeds exact integer precision");
    return static_cast<T>(number);
}
template<class T> std::optional<T> optionalInteger(const Json& object, std::string_view key, const std::string& where) {
    if (!present(object, key)) return {};
    return integer<T>(object[key], where + "." + std::string(key));
}
std::optional<std::string> optionalText(const Json& object, std::string_view key, const std::string& where) {
    if (!present(object, key)) return {};
    return text(object[key], where + "." + std::string(key));
}
std::string canonicalUUID(const Json& value, const std::string& where) {
    const auto parsed = foundationUUID(text(value, where));
    need(parsed.has_value(), where + " is not a UUID");
    return *parsed;
}
void version(const Json& document, const char* what) {
    need(document.isObject() && document.contains("version"), std::string(what) + " has no version");
    const auto number = integer<std::int64_t>(document["version"], std::string(what) + ".version");
    if (number > 1) throw MacImportError(MacImportErrorCode::newerVersion, std::string(what) + " was saved by a newer app version");
    need(number == 1, std::string(what) + " has an unsupported version");
}

// ---- account ----
constexpr std::array<std::string_view, 9> credentialKeys{"cred", "signingToken", "token", "accessToken", "refreshToken", "password", "credential", "credentials", "cookie"};
void strip(Json& value, const std::string& path, std::vector<std::string>& stripped) {
    if (value.isObject()) {
        std::vector<std::string> remove;
        for (const auto& [key, _] : value.object())
            if (std::find(credentialKeys.begin(), credentialKeys.end(), key) != credentialKeys.end()) remove.push_back(key);
        for (const auto& key : remove) { value.erase(key); stripped.push_back(path + "." + key); }
        for (auto& [key, child] : value.object()) strip(child, path + "." + key, stripped);
    } else if (value.isArray()) {
        Json::Array items = value.array();
        for (std::size_t i = 0; i < items.size(); ++i) strip(items[i], path + "[" + std::to_string(i) + "]", stripped);
        value = Json(std::move(items));
    }
}
std::string roleIdentity(const Json& role, const std::string& where) {
    std::string out;
    auto add = [&](const std::string& part) { if (!out.empty()) out += "|"; out += std::to_string(part.size()) + ":" + part; };
    add(text(required(role, "region", where), where + ".region"));
    add(text(required(role, "game", where), where + ".game"));
    add(text(required(role, "bindingUID", where), where + ".bindingUID"));
    add(text(required(role, "roleID", where), where + ".roleID"));
    add(optionalText(role, "serverID", where).value_or(""));
    return out;
}
void validateRole(const Json& role, const std::string& where) {
    need(role.isObject(), where + " must be an object");
    const auto region = text(required(role, "region", where), where + ".region");
    need(region == "mainland" || region == "global", where + ".region is not a known region");
    const auto game = text(required(role, "game", where), where + ".game");
    need(game == "arknights" || game == "endfield", where + ".game is not a known game");
    (void)text(required(role, "bindingUID", where), where + ".bindingUID");
    (void)text(required(role, "roleID", where), where + ".roleID");
    for (const char* key : {"serverID", "name", "serverName", "communityUserID"}) (void)optionalText(role, key, where);
    (void)flag(required(role, "isDefault", where), where + ".isDefault");
    (void)flag(required(role, "isAvailable", where), where + ".isAvailable");
}
void validateSnapshot(const Json& snapshot, const std::string& where) {
    need(snapshot.isObject(), where + " must be an object");
    validateRole(required(snapshot, "role", where), where + ".role");
    (void)date(required(snapshot, "observedAt", where), where + ".observedAt");
    (void)optionalText(snapshot, "name", where);
    if (present(snapshot, "avatarURL")) need(!text(snapshot["avatarURL"], where + ".avatarURL").empty(), where + ".avatarURL is not a URL");
    for (const char* key : {"level", "worldLevel", "experience", "operatorCount", "weaponCount", "documentCount"}) (void)optionalInteger<std::int64_t>(snapshot, key, where);
    if (present(snapshot, "createdAt")) (void)date(snapshot["createdAt"], where + ".createdAt");
    if (present(snapshot, "stamina")) {
        const auto& stamina = snapshot["stamina"];
        need(stamina.isObject(), where + ".stamina must be an object");
        (void)integer<std::int64_t>(required(stamina, "current", where + ".stamina"), where + ".stamina.current");
        (void)integer<std::int64_t>(required(stamina, "maximum", where + ".stamina"), where + ".stamina.maximum");
        for (const char* key : {"fullRecoveryAt", "serverObservedAt"}) if (present(stamina, key)) (void)date(stamina[key], where + ".stamina." + key);
    }
}

// ---- shelf ----
std::string decodeData(const Json& value, const std::string& where) {
    const auto encoded = text(value, where);
    const auto bytes = base64Decode(encoded, false, false);
    need(bytes.has_value(), where + " is not base64 Data");
    return *bytes;
}
}

MacAccountImport importMacAccountCache(std::string_view bytes) {
    if (bytes.size() > macAccountCacheMaximumBytes) throw MacImportError(MacImportErrorCode::tooLarge, "Account cache exceeds 1 MiB");
    Json document;
    try { document = Json::parse(bytes, macAccountCacheMaximumBytes); } catch (const std::exception&) { invalid("Account cache is not JSON"); }
    need(document.isObject(), "Account cache must be an object");
    version(document, "Account cache");
    const auto region = text(required(document, "region", "cache"), "cache.region");
    need(region == "mainland" || region == "global", "Account cache region is unknown");
    (void)text(required(document, "header", "cache"), "cache.header");
    const bool syncProfile = flag(required(document, "syncProfile", "cache"), "cache.syncProfile");
    (void)flag(required(document, "syncAvatar", "cache"), "cache.syncAvatar");
    const auto& records = required(document, "records", "cache");
    need(records.isObject(), "Account records must be an object");
    MacAccountImport out;
    for (const auto& [name, record] : records.object()) {
        const auto where = "records." + name;
        need(record.isObject(), where + " must be an object");
        (void)flag(required(record, "linked", where), where + ".linked");
        (void)flag(required(record, "requiresReconnect", where), where + ".requiresReconnect");
        const auto& roles = required(record, "roles", where);
        need(roles.isArray(), where + ".roles must be an array");
        for (std::size_t i = 0; i < roles.array().size(); ++i) validateRole(roles.array()[i], where + ".roles[" + std::to_string(i) + "]");
        out.roles += roles.array().size();
        (void)optionalText(record, "selectedRoleID", where);
        const auto& snapshots = required(record, "snapshots", where);
        need(snapshots.isObject(), where + ".snapshots must be an object");
        for (const auto& [id, snapshot] : snapshots.object()) validateSnapshot(snapshot, where + ".snapshots[" + id + "]");
        out.snapshots += snapshots.object().size();
        if (present(record, "bindingsAt")) (void)date(record["bindingsAt"], where + ".bindingsAt");
    }
    // HypergryphAccountController.gameSyncActive for the selected region.
    if (records.contains(region)) {
        const auto& record = records[region];
        if (syncProfile && record["linked"].boolean() && present(record, "selectedRoleID")) {
            const auto selected = record["selectedRoleID"].string();
            for (const auto& role : record["roles"].array())
                if (roleIdentity(role, "selected role") == selected) { out.profileSyncLocked = role["game"].string() == "endfield"; break; }
        }
    }
    // No credentials exist on Windows after import: every linked region is
    // shown as needing reconnection while its cached roles/snapshots remain.
    auto result = document;
    strip(result, "cache", out.strippedKeys);
    for (auto& [name, record] : result["records"].object())
        if (record["linked"].boolean()) { record["requiresReconnect"] = true; out.disconnectedRegions.push_back(name); }
    out.bytes = result.encode(macAccountCacheMaximumBytes);
    return out;
}

std::vector<MacShelfRecord> decodeMacShelf(std::string_view bytes) {
    Json document;
    try { document = Json::parse(bytes, 4 * 1024 * 1024); } catch (const std::exception&) { invalid("File shelf is not JSON"); }
    version(document, "File shelf");
    const auto& items = required(document, "items", "shelf");
    need(items.isArray(), "File shelf items must be an array");
    std::vector<MacShelfRecord> out;
    std::set<std::string> ids;
    for (std::size_t i = 0; i < items.array().size(); ++i) {
        const auto& row = items.array()[i];
        const auto where = "items[" + std::to_string(i) + "]";
        need(row.isObject(), where + " must be an object");
        MacShelfRecord record;
        record.id = canonicalUUID(required(row, "id", where), where + ".id");
        need(ids.insert(record.id).second, "Duplicate shelf item identifier");
        need(!decodeData(required(row, "bookmark", where), where + ".bookmark").empty(), where + ".bookmark is empty");
        (void)flag(required(row, "isSecurityScoped", where), where + ".isSecurityScoped");
        record.lastKnownPath = text(required(row, "lastKnownPath", where), where + ".lastKnownPath");
        need(!record.lastKnownPath.empty() && record.lastKnownPath.front() == '/', where + ".lastKnownPath is not absolute");
        record.name = text(required(row, "name", where), where + ".name");
        need(!record.name.empty(), where + ".name is empty");
        record.typeDescription = text(required(row, "typeDescription", where), where + ".typeDescription");
        record.byteCount = optionalInteger<std::int64_t>(row, "byteCount", where);
        need(!record.byteCount || *record.byteCount >= 0, where + ".byteCount is negative");
        record.isDirectory = flag(required(row, "isDirectory", where), where + ".isDirectory");
        record.createdAt = date(required(row, "createdAt", where), where + ".createdAt");
        if (present(row, "identity")) {
            const auto& identity = row["identity"];
            need(identity.isObject(), where + ".identity must be an object");
            (void)integer<std::uint64_t>(required(identity, "inode", where + ".identity"), where + ".identity.inode");
            (void)integer<std::int32_t>(required(identity, "device", where + ".identity"), where + ".identity.device");
            (void)optionalText(identity, "volumeUUID", where + ".identity");
        }
        record.original = row;
        out.push_back(std::move(record));
    }
    return out;
}

ImageSignature sniffImage(std::string_view h) noexcept {
    if (h.size() >= 8 && h.substr(0, 8) == std::string_view("\x89PNG\r\n\x1a\n", 8)) return ImageSignature::png;
    if (h.size() >= 3 && h.substr(0, 3) == "\xFF\xD8\xFF") return ImageSignature::jpeg;
    if (h.size() >= 6 && (h.substr(0, 6) == "GIF87a" || h.substr(0, 6) == "GIF89a")) return ImageSignature::gif;
    if (h.size() >= 4 && (h.substr(0, 4) == std::string_view("II*\0", 4) || h.substr(0, 4) == std::string_view("MM\0*", 4))) return ImageSignature::tiff;
    if (h.size() >= 12 && h.substr(4, 4) == "ftyp") {
        const auto brand = h.substr(8, 4);
        for (const char* known : {"heic", "heix", "hevc", "hevx", "heim", "heis", "mif1", "msf1", "avif", "avis"}) if (brand == known) return ImageSignature::heif;
    }
    if (h.size() >= 12 && h.substr(0, 4) == "RIFF" && h.substr(8, 4) == "WEBP") return ImageSignature::webp;
    if (h.size() >= 2 && h.substr(0, 2) == "BM") return ImageSignature::bmp;
    return ImageSignature::unknown;
}
std::string_view imageSignatureName(ImageSignature value) noexcept {
    switch (value) {
    case ImageSignature::png: return "PNG";
    case ImageSignature::jpeg: return "JPEG";
    case ImageSignature::gif: return "GIF";
    case ImageSignature::tiff: return "TIFF";
    case ImageSignature::heif: return "HEIF";
    case ImageSignature::webp: return "WebP";
    case ImageSignature::bmp: return "BMP";
    case ImageSignature::unknown: break;
    }
    return "unknown";
}
std::optional<PngHeader> pngHeader(std::string_view h) noexcept {
    if (sniffImage(h) != ImageSignature::png || h.size() < 33) return {};
    auto be = [&](std::size_t at) { std::uint32_t v{}; for (std::size_t i = 0; i < 4; ++i) v = (v << 8) | static_cast<unsigned char>(h[at + i]); return v; };
    if (be(8) != 13 || h.substr(12, 4) != "IHDR") return {};
    const PngHeader header{be(16), be(20)};
    if (!header.width || !header.height || header.width > 0x7fffffffu || header.height > 0x7fffffffu) return {};
    return header;
}
bool macManagedImageName(std::string_view name, bool allowImageSuffix) noexcept {
    if (name.size() == 40 && name.substr(36) == ".png") return foundationUUID(name.substr(0, 36)).value_or("") == name.substr(0, 36);
    if (allowImageSuffix && name.size() == 42 && name.substr(36) == ".image") return foundationUUID(name.substr(0, 36)).value_or("") == name.substr(0, 36);
    return false;
}

bool validExportPath(std::string_view path) noexcept {
    if (path.empty() || path.size() > 1024 || !Json::validUtf8(path)) return false;
    for (const char c : path) {
        const auto u = static_cast<unsigned char>(c);
        if (u < 32 || u == 127 || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') return false;
    }
    std::size_t start = 0, depth = 0;
    while (start <= path.size()) {
        auto end = path.find('/', start);
        if (end == std::string_view::npos) end = path.size();
        const auto part = path.substr(start, end - start);
        if (part.empty() || part == "." || part == ".." || part.back() == '.' || part.back() == ' ') return false;
        std::string upper(part.substr(0, part.find('.')));
        for (auto& c : upper) if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
        for (const char* reserved : {"CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
                                     "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"})
            if (upper == reserved) return false;
        if (depth == 0 && part != "EndfieldCharge" && part != "Preferences") return false;
        if (++depth > 6) return false;
        start = end + 1;
        if (end == path.size()) break;
    }
    return depth >= 2;
}
const MacExportFile* MacExportManifest::find(std::string_view path) const noexcept {
    for (const auto& file : files) if (file.path == path) return &file;
    return nullptr;
}
MacExportManifest decodeMacExportManifest(std::string_view bytes) {
    if (bytes.size() > 8 * 1024 * 1024) throw MacImportError(MacImportErrorCode::tooLarge, "Export manifest is too large");
    Json document;
    try { document = Json::parse(bytes, 8 * 1024 * 1024); } catch (const std::exception&) { invalid("Export manifest is not JSON"); }
    need(document.isObject() && document["format"].isString() && document["format"].string() == macExportFormat, "Not an EndfieldHUD macOS export manifest");
    MacExportManifest manifest;
    manifest.version = integer<std::int64_t>(required(document, "version", "manifest"), "manifest.version");
    if (manifest.version > 1) throw MacImportError(MacImportErrorCode::newerVersion, "The export was made by a newer exporter");
    need(manifest.version == 1, "Unsupported export manifest version");
    const auto& source = required(document, "source", "manifest");
    manifest.bundleIdentifier = text(required(source, "bundleIdentifier", "source"), "source.bundleIdentifier");
    need(manifest.bundleIdentifier == macPreferencesDomain, "The export is not from EndfieldHUD for macOS");
    manifest.appVersion = optionalText(source, "appVersion", "source").value_or("");
    manifest.build = optionalText(source, "build", "source").value_or("");
    manifest.exportedAt = date(required(document, "exportedAt", "manifest"), "manifest.exportedAt");
    const auto& files = required(document, "files", "manifest");
    need(files.isArray() && files.array().size() <= macExportMaximumFiles, "Export file list is invalid or too long");
    std::set<std::string> folded;
    for (const auto& row : files.array()) {
        MacExportFile file;
        file.path = text(required(row, "path", "files[]"), "files[].path");
        need(validExportPath(file.path), "Unsafe or unknown export path: " + file.path);
        std::string key = file.path;
        for (auto& c : key) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        need(folded.insert(key).second, "Export lists the same path twice (case-insensitively): " + file.path);
        file.bytes = integer<std::uint64_t>(required(row, "bytes", "files[]"), "files[].bytes");
        file.sha256 = text(required(row, "sha256", "files[]"), "files[].sha256");
        need(validSha256Hex(file.sha256), "Invalid SHA-256 for " + file.path);
        manifest.files.push_back(std::move(file));
    }
    if (present(document, "sqlite")) {
        need(document["sqlite"].isObject(), "manifest.sqlite must be an object");
        for (const auto& [path, info] : document["sqlite"].object()) {
            need(manifest.find(path) != nullptr, "SQLite metadata names an unlisted file");
            manifest.sqliteUserVersions[path] = integer<std::int64_t>(required(info, "userVersion", "sqlite"), "sqlite.userVersion");
        }
    }
    if (present(document, "preferences")) {
        manifest.preferences = text(document["preferences"], "manifest.preferences");
        need(manifest.find(*manifest.preferences) != nullptr && manifest.preferences->starts_with("Preferences/"), "Preferences entry names an unlisted file");
    }
    manifest.sha256 = Sha256::hex(bytes);
    return manifest;
}
}
