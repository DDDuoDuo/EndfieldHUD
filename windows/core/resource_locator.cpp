#include "core/resource_locator.hpp"
#include "core/data/file_io.hpp"
#include "core/shell_packet.hpp"
#include <algorithm>
#include <set>
#include <stdexcept>

namespace endfield::core {
namespace {
using ehud::data::Json;
[[noreturn]] void invalid(const char* message) { throw std::runtime_error(message); }
bool hex64(std::string_view value) {
    return value.size() == 64 && std::all_of(value.begin(), value.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}
std::filesystem::path join(const std::filesystem::path& base, std::string_view relative) {
    auto out = base;
    std::size_t at = 0;
    while (at < relative.size()) {
        const auto end = std::min(relative.find('/', at), relative.size());
        const auto part = relative.substr(at, end - at);
        out /= std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(part.data()), part.size()));
        at = end + 1;
    }
    return out;
}
}

bool validResourcePath(std::string_view value, bool allowEmpty) noexcept {
    if (value.empty()) return allowEmpty;
    if (value.size() > 512 || !Json::validUtf8(value) || value.front() == '/' || value.back() == '/') return false;
    std::size_t at = 0;
    while (at <= value.size()) {
        const auto end = std::min(value.find('/', at), value.size());
        const auto part = value.substr(at, end - at);
        if (part.empty() || part == "." || part == ".." || part.back() == '.' || part.back() == ' ') return false;
        for (const unsigned char c : part)
            if (c < 32 || c == 127 || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') return false;
        at = end + 1;
        if (end == value.size()) break;
    }
    return true;
}

ResourceLocator ResourceLocator::open(const std::filesystem::path& root) {
    if (root.empty() || !root.is_absolute()) invalid("Resource root must be absolute");
    std::optional<std::string> bytes;
    try { bytes = ehud::data::detail::readFile(root / "resources.json", 8 * 1024 * 1024); }
    catch (const std::exception&) { invalid("Resource manifest cannot be read"); }
    if (!bytes) invalid("Resource manifest is missing");
    Json manifest;
    try { manifest = Json::parse(*bytes, 8 * 1024 * 1024); } catch (const std::exception&) { invalid("Resource manifest is invalid"); }
    if (!manifest.isObject() || !manifest["format"].isString() || manifest["format"].string() != resourceManifestFormat || !manifest["schema"].isNumber())
        invalid("Resource manifest is not an EndfieldHUD resource manifest");
    if (manifest["schema"].integer() > resourceManifestSchema) invalid("Resource manifest requires a newer EndfieldHUD");
    if (manifest["schema"].integer() != resourceManifestSchema || !manifest["resources"].isObject()) invalid("Resource manifest schema is unsupported");
    ResourceLocator out;
    out.root_ = root;
    if (manifest["sourceCommit"].isString()) out.sourceCommit_ = manifest["sourceCommit"].string();
    for (const auto& [id, value] : manifest["resources"].object()) {
        if (id.empty() || id.size() > 128 || !value.isObject() || !value["path"].isString() || !value["kind"].isString() || !value["files"].isArray())
            invalid("Resource manifest entry is invalid");
        Entry entry{id, value["path"].string(), value["kind"].string() == "directory", {}};
        if (!validResourcePath(entry.path) || (!entry.directory && value["kind"].string() != "file")) invalid("Resource manifest path is invalid");
        for (const auto& file : value["files"].array()) {
            if (!file.isObject() || !file["path"].isString() || !file["sha256"].isString() || !file["bytes"].isNumber()) invalid("Resource manifest file is invalid");
            File f{file["path"].string(), static_cast<std::uint64_t>(file["bytes"].integer()), file["sha256"].string()};
            if (!validResourcePath(f.path, !entry.directory) || (!entry.directory && !f.path.empty()) || !hex64(f.sha256) || file["bytes"].integer() < 0 || f.bytes > resourceFileLimit)
                invalid("Resource manifest file is invalid");
            entry.files.push_back(std::move(f));
        }
        if (entry.files.empty() || (!entry.directory && entry.files.size() != 1)) invalid("Resource manifest entry lists no files");
        std::vector<std::string> names;
        for (const auto& f : entry.files) names.push_back(f.path);
        std::sort(names.begin(), names.end());
        if (std::adjacent_find(names.begin(), names.end()) != names.end()) invalid("Resource manifest repeats a file");
        out.entries_.emplace(id, std::move(entry));
    }
    return out;
}
bool ResourceLocator::declared(std::string_view id) const noexcept { return entries_.find(id) != entries_.end(); }
const ResourceLocator::Entry* ResourceLocator::entry(std::string_view id) const noexcept {
    const auto it = entries_.find(id);
    return it == entries_.end() ? nullptr : &it->second;
}
std::optional<std::string> ResourceLocator::fileSHA256(std::string_view id, std::string_view relative) const {
    const auto* e = entry(id);
    if (!e) return {};
    for (const auto& f : e->files) if (f.path == relative) return f.sha256;
    return {};
}
std::vector<std::string> ResourceLocator::unlistedFiles() const {
    std::set<std::string, std::less<>> listed{"resources.json"};
    for (const auto& [id, e] : entries_) {
        (void)id;
        for (const auto& f : e.files) listed.insert(e.directory ? e.path + "/" + f.path : e.path);
    }
    std::vector<std::string> out;
    const auto relative = [&](const std::filesystem::path& path) {
        const auto text = path.lexically_relative(root_).generic_u8string();
        return std::string(reinterpret_cast<const char*>(text.data()), text.size());
    };
    std::error_code error;
    // Directory links are reported, never followed.
    std::filesystem::recursive_directory_iterator it(root_, std::filesystem::directory_options::none, error), end;
    if (error) return {"."};
    while (it != end) {
        std::error_code statusError;
        const auto status = it->symlink_status(statusError);
        if (statusError || !std::filesystem::is_directory(status)) {
            auto name = relative(it->path());
            if (statusError || !std::filesystem::is_regular_file(status) || !listed.contains(name)) out.push_back(std::move(name));
        }
        it.increment(error);
        if (error) { out.push_back("(unreadable directory)"); break; }
    }
    std::sort(out.begin(), out.end());
    return out;
}
std::vector<std::string> ResourceLocator::ids() const {
    std::vector<std::string> out;
    for (const auto& [id, value] : entries_) { (void)value; out.push_back(id); }
    return out;
}
const ResourceLocator::Resolution& ResourceLocator::resolve(std::string_view id) {
    if (const auto it = resolved_.find(id); it != resolved_.end()) return it->second;
    Resolution result;
    const auto* e = entry(id);
    if (!e) { result.reason = "Resource is not part of this package"; return resolved_.emplace(std::string(id), std::move(result)).first->second; }
    result.path = join(root_, e->path);
    ++verifications_;
    result.status = Status::verified;
    for (const auto& f : e->files) {
        const auto path = e->directory ? join(result.path, f.path) : result.path;
        std::optional<std::string> bytes;
        try { bytes = ehud::data::detail::readFile(path, static_cast<std::size_t>(f.bytes)); }
        catch (const std::exception&) { result.status = Status::corrupt; result.reason = "Resource file is unreadable or has the wrong size"; break; }
        if (!bytes) { result.status = Status::missing; result.reason = "Resource file is missing"; break; }
        if (bytes->size() != f.bytes || packet::sha256(std::span(reinterpret_cast<const std::uint8_t*>(bytes->data()), bytes->size())) != f.sha256) {
            result.status = Status::corrupt; result.reason = "Resource file does not match its SHA-256 pin"; break;
        }
    }
    return resolved_.emplace(std::string(id), std::move(result)).first->second;
}
} // namespace endfield::core
