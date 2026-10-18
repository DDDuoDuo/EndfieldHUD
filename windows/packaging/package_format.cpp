#include "packaging/package_format.hpp"
#include "core/data/json.hpp"
#include <algorithm>
#include <charconv>
#include <set>

namespace endfield::packaging {
namespace {
using ehud::data::Json;

[[noreturn]] void fail(const std::string& what) { throw PackageError(what); }
void require(bool value, const char* what) { if (!value) fail(what); }

std::uint64_t readU64(std::span<const std::uint8_t> bytes, std::size_t at) noexcept {
    std::uint64_t value = 0;
    for (std::size_t n = 0; n < 8; ++n) value |= static_cast<std::uint64_t>(bytes[at + n]) << (8 * n);
    return value;
}
std::uint32_t readU32(std::span<const std::uint8_t> bytes, std::size_t at) noexcept {
    std::uint32_t value = 0;
    for (std::size_t n = 0; n < 4; ++n) value |= static_cast<std::uint32_t>(bytes[at + n]) << (8 * n);
    return value;
}
std::uint16_t readU16(std::span<const std::uint8_t> bytes, std::size_t at) noexcept {
    return static_cast<std::uint16_t>(bytes[at] | (bytes[at + 1] << 8));
}
void writeU64(std::uint8_t* out, std::uint64_t value) noexcept {
    for (std::size_t n = 0; n < 8; ++n) out[n] = static_cast<std::uint8_t>(value >> (8 * n));
}
bool hexDigit(char c) noexcept { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }
char asciiLower(char c) noexcept { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }

bool reservedDeviceName(std::string_view component) noexcept {
    // Windows treats NAME and NAME.anything as the device, case-insensitively.
    auto base = component.substr(0, component.find('.'));
    while (!base.empty() && base.back() == ' ') base.remove_suffix(1);
    std::string lower;
    for (const char c : base) lower.push_back(asciiLower(c));
    if (lower == "con" || lower == "prn" || lower == "aux" || lower == "nul" || lower == "conin$" || lower == "conout$") return true;
    return lower.size() == 4 && (lower.starts_with("com") || lower.starts_with("lpt")) && lower[3] >= '0' && lower[3] <= '9';
}
bool validComponent(std::string_view component) noexcept {
    if (component.empty() || component == "." || component == ".." || component.front() == ' ' || component.back() == ' ' || component.back() == '.')
        return false;
    for (const char c : component) {
        const auto u = static_cast<unsigned char>(c);
        if (u < 0x20 || u > 0x7e) return false;
        if (c == '<' || c == '>' || c == ':' || c == '"' || c == '\\' || c == '|' || c == '?' || c == '*' || c == '/') return false;
    }
    return !reservedDeviceName(component);
}
std::int64_t integerField(const Json& value, const char* what, std::int64_t minimum, std::int64_t maximum) {
    require(value.isNumber(), what);
    std::int64_t number{};
    try { number = value.integer(); } catch (const std::exception&) { fail(what); }
    require(number >= minimum && number <= maximum, what);
    return number;
}
std::string stringField(const Json& value, const char* what) {
    require(value.isString(), what);
    return value.string();
}
Json parseJson(std::string_view text, const char* what) {
    try { return Json::parse(text, maximumManifestBytes); } catch (const std::exception&) { fail(what); }
}
bool validCommit(std::string_view value) noexcept {
    return value.size() == 40 && std::all_of(value.begin(), value.end(), hexDigit);
}
std::optional<int> component(std::string_view text) noexcept {
    if (text.empty() || text.size() > 6 || (text.size() > 1 && text.front() == '0')) return std::nullopt;
    int value{};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size()) return std::nullopt;
    return value;
}
} // namespace

std::string_view encodingName(Encoding value) noexcept {
    return value == Encoding::lzms ? "lzms" : "identity";
}
std::optional<Encoding> encodingFromName(std::string_view name) noexcept {
    if (name == "identity") return Encoding::identity;
    if (name == "lzms") return Encoding::lzms;
    return std::nullopt;
}

std::string ProductVersion::release() const {
    return std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(patch);
}
std::string ProductVersion::display() const { return release() + " (" + std::to_string(build) + ")"; }
std::string ProductVersion::dotted() const { return release() + "." + std::to_string(build); }

std::optional<ProductVersion> parseProductVersion(std::string_view release, std::int64_t build) noexcept {
    if (release.starts_with('v')) release.remove_prefix(1);
    if (build < 1 || build > 999999) return std::nullopt;
    ProductVersion version;
    int* parts[] = {&version.major, &version.minor, &version.patch};
    for (std::size_t n = 0; n < 3; ++n) {
        const auto dot = n < 2 ? release.find('.') : std::string_view::npos;
        if (n < 2 && dot == std::string_view::npos) return std::nullopt;
        const auto value = component(release.substr(0, dot));
        if (!value) return std::nullopt;
        *parts[n] = *value;
        release = n < 2 ? release.substr(dot + 1) : std::string_view{};
    }
    version.build = static_cast<int>(build);
    return version;
}
std::optional<ProductVersion> parseDottedVersion(std::string_view dotted) noexcept {
    const auto last = dotted.rfind('.');
    if (last == std::string_view::npos) return std::nullopt;
    const auto build = component(dotted.substr(last + 1));
    if (!build) return std::nullopt;
    return parseProductVersion(dotted.substr(0, last), *build);
}

bool validPackagePath(std::string_view path) noexcept {
    if (path.empty() || path.size() > maximumPathBytes) return false;
    std::size_t start = 0;
    while (true) {
        const auto end = path.find('/', start);
        if (!validComponent(path.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start))) return false;
        if (end == std::string_view::npos) return true;
        start = end + 1;
    }
}
std::string foldedPackagePath(std::string_view path) {
    std::string out;
    out.reserve(path.size());
    for (const char c : path) out.push_back(asciiLower(c));
    return out;
}
bool validSha256Hex(std::string_view value) noexcept {
    return value.size() == 64 && std::all_of(value.begin(), value.end(), hexDigit);
}
std::string hex(std::span<const std::uint8_t> bytes) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (const auto b : bytes) { out.push_back(digits[b >> 4]); out.push_back(digits[b & 15]); }
    return out;
}

std::uint64_t PayloadManifest::installedBytes() const noexcept {
    std::uint64_t total = 0;
    for (const auto& f : files) total += f.bytes;
    return total;
}
std::uint64_t PayloadManifest::storedBytes() const noexcept {
    std::uint64_t total = 0;
    for (const auto& f : files) total += f.stored;
    return total;
}
const PayloadFile* PayloadManifest::find(std::string_view path) const noexcept {
    for (const auto& f : files) if (f.path == path) return &f;
    return nullptr;
}

namespace {
// Shared validation of a manifest's file list (payload and install).
template <class File> void validateFiles(const std::vector<File>& files) {
    require(!files.empty() && files.size() <= maximumFiles, "Package file count is out of bounds");
    std::set<std::string> folded;
    for (std::size_t n = 0; n < files.size(); ++n) {
        const auto& f = files[n];
        require(validPackagePath(f.path), "Package file path is invalid");
        require(n == 0 || files[n - 1].path < f.path, "Package files must be sorted by path without duplicates");
        require(folded.insert(foldedPackagePath(f.path)).second, "Package file paths collide case-insensitively");
        require(validSha256Hex(f.sha256), "Package file digest is invalid");
        require(f.bytes <= maximumFileBytes, "Package file is too large");
        require(foldedPackagePath(f.path) != foldedPackagePath(installManifestName), "The install manifest cannot be packaged");
    }
    // A file cannot also be a directory of another file.
    for (const auto& f : files) {
        const auto key = foldedPackagePath(f.path);
        for (auto slash = key.find('/'); slash != std::string::npos; slash = key.find('/', slash + 1))
            require(!folded.contains(key.substr(0, slash)), "A package file is also a directory");
    }
    const auto has = [&](std::string_view name) {
        return std::any_of(files.begin(), files.end(), [&](const File& f) { return f.path == name; });
    };
    require(has(applicationExecutable), "The package has no EndfieldHUD.exe");
}
ProductVersion versionFields(const Json& root) {
    const auto release = stringField(root["release"], "Package release is missing");
    const auto build = integerField(root["build"], "Package build is invalid", 1, 999999);
    const auto version = parseProductVersion(release, build);
    require(version.has_value() && !release.starts_with('v'), "Package release is not a version");
    return *version;
}
} // namespace

std::string encodePayloadManifest(const PayloadManifest& m) {
    Json::Array files;
    files.reserve(m.files.size());
    for (const auto& f : m.files) {
        Json::Object file;
        file["path"] = f.path;
        file["bytes"] = static_cast<std::int64_t>(f.bytes);
        file["sha256"] = f.sha256;
        file["encoding"] = std::string(encodingName(f.encoding));
        file["offset"] = static_cast<std::int64_t>(f.offset);
        file["stored"] = static_cast<std::int64_t>(f.stored);
        files.emplace_back(std::move(file));
    }
    Json::Object root;
    root["format"] = std::string(payloadFormat);
    root["schema"] = payloadSchema;
    root["release"] = m.version.release();
    root["build"] = m.version.build;
    root["commit"] = m.commit;
    root["architecture"] = m.architecture;
    root["complete"] = m.complete;
    root["files"] = std::move(files);
    return Json(std::move(root)).encode(maximumManifestBytes);
}

PayloadManifest decodePayloadManifest(std::string_view json, std::uint64_t dataBytes) {
    require(json.size() <= maximumManifestBytes, "Package manifest is too large");
    const Json root = parseJson(json, "Package manifest is not JSON");
    require(root.isObject(), "Package manifest is not an object");
    require(root["format"].isString() && root["format"].string() == payloadFormat, "Not an EndfieldHUD payload");
    require(integerField(root["schema"], "Payload schema is invalid", 1, 1000) == payloadSchema, "Unsupported payload schema");
    PayloadManifest m;
    m.version = versionFields(root);
    m.commit = stringField(root["commit"], "Package commit is missing");
    require(validCommit(m.commit), "Package commit is invalid");
    m.architecture = stringField(root["architecture"], "Package architecture is missing");
    require(m.architecture == "x64", "Package architecture is not x64");
    require(root["complete"].isBool(), "Package completeness is missing");
    m.complete = root["complete"].boolean();
    require(root["files"].isArray(), "Package files are missing");
    const auto& files = root["files"].array();
    require(files.size() <= maximumFiles, "Package file count is out of bounds");
    m.files.reserve(files.size());
    std::uint64_t next = 0;
    for (const auto& item : files) {
        require(item.isObject(), "Package file record is invalid");
        PayloadFile f;
        f.path = stringField(item["path"], "Package file path is missing");
        f.bytes = static_cast<std::uint64_t>(integerField(item["bytes"], "Package file size is invalid", 0, static_cast<std::int64_t>(maximumFileBytes)));
        f.sha256 = stringField(item["sha256"], "Package file digest is missing");
        const auto encoding = encodingFromName(stringField(item["encoding"], "Package file encoding is missing"));
        require(encoding.has_value(), "Package file encoding is unknown");
        f.encoding = *encoding;
        f.offset = static_cast<std::uint64_t>(integerField(item["offset"], "Package file offset is invalid", 0, static_cast<std::int64_t>(maximumPayloadBytes)));
        f.stored = static_cast<std::uint64_t>(integerField(item["stored"], "Package file range is invalid", 0, static_cast<std::int64_t>(maximumPayloadBytes)));
        if (f.encoding == Encoding::identity) require(f.stored == f.bytes, "An identity file must store its exact bytes");
        else require(f.bytes > 0 && f.bytes <= maximumEncodedFileBytes && f.stored > 0 && f.stored <= f.bytes + f.bytes / 8 + 4096,
                     "A compressed file is out of bounds");
        require(f.offset == next, "Package file ranges must be contiguous");
        next += f.stored;
        require(next <= dataBytes, "Package file range exceeds the payload");
        m.files.push_back(std::move(f));
    }
    require(next == dataBytes, "Package data has unreferenced bytes");
    validateFiles(m.files);
    require(m.find(uninstallerExecutable) != nullptr, "The package has no uninstaller");
    return m;
}

std::array<std::uint8_t, trailerBytes> encodeTrailer(const Trailer& t) noexcept {
    std::array<std::uint8_t, trailerBytes> out{};
    std::copy(trailerMagic.begin(), trailerMagic.end(), out.begin());
    writeU64(out.data() + 8, t.payloadOffset);
    writeU64(out.data() + 16, t.payloadBytes);
    std::copy(t.sha256.begin(), t.sha256.end(), out.begin() + 24);
    std::copy(trailerEndMagic.begin(), trailerEndMagic.end(), out.begin() + 56);
    return out;
}
std::optional<Trailer> decodeTrailer(std::span<const std::uint8_t> bytes) noexcept {
    if (bytes.size() != trailerBytes || !std::equal(trailerMagic.begin(), trailerMagic.end(), bytes.begin()) ||
        !std::equal(trailerEndMagic.begin(), trailerEndMagic.end(), bytes.begin() + 56))
        return std::nullopt;
    Trailer t;
    t.payloadOffset = readU64(bytes, 8);
    t.payloadBytes = readU64(bytes, 16);
    std::copy(bytes.begin() + 24, bytes.begin() + 56, t.sha256.begin());
    return t;
}
std::array<std::uint8_t, payloadHeaderBytes> encodePayloadHeader(std::uint64_t manifestBytes) noexcept {
    std::array<std::uint8_t, payloadHeaderBytes> out{};
    std::copy(payloadMagic.begin(), payloadMagic.end(), out.begin());
    writeU64(out.data() + 8, manifestBytes);
    return out;
}
std::optional<std::uint64_t> decodePayloadHeader(std::span<const std::uint8_t> bytes) noexcept {
    if (bytes.size() != payloadHeaderBytes || !std::equal(payloadMagic.begin(), payloadMagic.end(), bytes.begin())) return std::nullopt;
    const auto manifest = readU64(bytes, 8);
    if (manifest == 0 || manifest > maximumManifestBytes) return std::nullopt;
    return manifest;
}
std::size_t trailerPadding(std::uint64_t contentBytes) noexcept {
    return static_cast<std::size_t>((8 - (contentBytes + trailerBytes) % 8) % 8);
}

PortableExecutableLayout inspectPortableExecutable(std::span<const std::uint8_t> h, std::uint64_t fileBytes) {
    require(h.size() >= 64 && h[0] == 'M' && h[1] == 'Z', "Setup is not a Windows executable");
    const std::uint64_t pe = readU32(h, 0x3c);
    require(pe + 24 <= h.size() && h[pe] == 'P' && h[pe + 1] == 'E' && h[pe + 2] == 0 && h[pe + 3] == 0, "Setup has no PE header");
    require(readU16(h, pe + 4) == 0x8664, "Setup is not an x64 executable");
    const std::uint64_t sections = readU16(h, pe + 6), optionalBytes = readU16(h, pe + 20);
    const auto optional = pe + 24;
    require(optionalBytes >= 152 && optional + optionalBytes <= h.size() && readU16(h, optional) == 0x20b, "Setup is not a PE32+ image");
    const auto sectionTable = optional + optionalBytes;
    require(sections >= 1 && sections <= 96 && sectionTable + sections * 40 <= h.size(), "Setup section table is invalid");
    PortableExecutableLayout layout;
    for (std::uint64_t n = 0; n < sections; ++n) {
        const auto at = sectionTable + n * 40;
        const std::uint64_t size = readU32(h, at + 16), pointer = readU32(h, at + 20);
        if (size) layout.imageEnd = std::max(layout.imageEnd, pointer + size);
    }
    require(layout.imageEnd > 0 && layout.imageEnd <= fileBytes, "Setup sections exceed the file");
    layout.contentEnd = fileBytes;
    if (readU32(h, optional + 108) > 4) {
        const std::uint64_t table = readU32(h, optional + 144), size = readU32(h, optional + 148);
        if (table || size) {
            require(table >= layout.imageEnd && table % 8 == 0 && size >= 8 && table + size == fileBytes,
                    "Setup certificate table must follow the content and end the file");
            layout.contentEnd = table;
            layout.signatureTable = true;
        }
    }
    return layout;
}

Trailer locateTrailer(std::span<const std::uint8_t> tail, std::uint64_t contentEnd, std::uint64_t imageEnd) {
    require(tail.size() == std::min<std::uint64_t>(contentEnd, trailerSearchBytes), "Setup tail is incomplete");
    for (std::size_t alignment = 0; alignment < 8 && trailerBytes + alignment <= tail.size(); ++alignment) {
        const auto end = tail.size() - alignment;
        if (!std::all_of(tail.begin() + static_cast<std::ptrdiff_t>(end), tail.end(), [](std::uint8_t b) { return b == 0; })) continue;
        const auto trailer = decodeTrailer(tail.subspan(end - trailerBytes, trailerBytes));
        if (!trailer) continue;
        const auto trailerStart = contentEnd - alignment - trailerBytes;
        const auto& t = *trailer;
        require(t.payloadBytes >= payloadHeaderBytes && t.payloadBytes <= maximumPayloadBytes, "Setup payload size is invalid");
        require(t.payloadOffset >= imageEnd && t.payloadOffset <= trailerStart && t.payloadBytes <= trailerStart - t.payloadOffset,
                "Setup payload lies outside the setup file");
        const auto padding = trailerStart - t.payloadOffset - t.payloadBytes;
        require(padding == trailerPadding(t.payloadOffset + t.payloadBytes), "Setup payload padding is invalid");
        // Padding bytes visible in the tail must be zero.
        const auto tailStart = contentEnd - tail.size();
        for (auto at = std::max(tailStart, trailerStart - padding); at < trailerStart; ++at)
            require(tail[static_cast<std::size_t>(at - tailStart)] == 0, "Setup payload padding is not zero");
        return t;
    }
    fail("Setup has no payload");
}

std::uint64_t InstallManifest::installedBytes() const noexcept {
    std::uint64_t total = 0;
    for (const auto& f : files) total += f.bytes;
    return total;
}
InstallManifest installManifestFor(const PayloadManifest& payload, std::string installedAt) {
    InstallManifest m;
    m.version = payload.version;
    m.commit = payload.commit;
    m.installedAt = std::move(installedAt);
    m.files.reserve(payload.files.size());
    for (const auto& f : payload.files) m.files.push_back({f.path, f.bytes, f.sha256});
    return m;
}
std::string encodeInstallManifest(const InstallManifest& m) {
    Json::Array files;
    files.reserve(m.files.size());
    for (const auto& f : m.files) {
        Json::Object file;
        file["path"] = f.path;
        file["bytes"] = static_cast<std::int64_t>(f.bytes);
        file["sha256"] = f.sha256;
        files.emplace_back(std::move(file));
    }
    Json::Object root;
    root["format"] = std::string(installManifestFormat);
    root["schema"] = installManifestSchema;
    root["release"] = m.version.release();
    root["build"] = m.version.build;
    root["commit"] = m.commit;
    root["installedAt"] = m.installedAt;
    root["files"] = std::move(files);
    return Json(std::move(root)).encode(maximumManifestBytes) + "\n";
}
InstallManifest decodeInstallManifest(std::string_view json) {
    require(json.size() <= maximumManifestBytes, "Install manifest is too large");
    const Json root = parseJson(json, "Install manifest is not JSON");
    require(root.isObject() && root["format"].isString() && root["format"].string() == installManifestFormat, "Not an EndfieldHUD install manifest");
    require(integerField(root["schema"], "Install manifest schema is invalid", 1, 1000) == installManifestSchema, "Unsupported install manifest schema");
    InstallManifest m;
    m.version = versionFields(root);
    m.commit = stringField(root["commit"], "Install commit is missing");
    require(validCommit(m.commit), "Install commit is invalid");
    m.installedAt = stringField(root["installedAt"], "Install time is missing");
    require(m.installedAt.size() <= 64, "Install time is invalid");
    require(root["files"].isArray() && root["files"].array().size() <= maximumFiles, "Install files are invalid");
    for (const auto& item : root["files"].array()) {
        require(item.isObject(), "Install file record is invalid");
        InstalledFile f;
        f.path = stringField(item["path"], "Install file path is missing");
        f.bytes = static_cast<std::uint64_t>(integerField(item["bytes"], "Install file size is invalid", 0, static_cast<std::int64_t>(maximumFileBytes)));
        f.sha256 = stringField(item["sha256"], "Install file digest is missing");
        m.files.push_back(std::move(f));
    }
    validateFiles(m.files);
    return m;
}

std::string releaseArtifactStem(const ProductVersion& v) {
    return std::string(productName) + "-" + v.release() + "-build" + std::to_string(v.build) + "-windows-x64";
}
} // namespace endfield::packaging
