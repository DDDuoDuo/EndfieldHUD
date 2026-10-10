#pragma once
// Packaged runtime resource locator for EndfieldHUD.exe.
//
// <exe dir>/resources/resources.json (written by app/stage_resources.cmake):
//   {"format":"EndfieldHUD.Windows.Resources","schema":1,"sourceCommit":"<pin>",
//    "resources":{"<id>":{"path":"<relative>","kind":"file"|"directory",
//                         "files":[{"path":"<relative to the resource>","bytes":N,"sha256":"<hex>"}]}}}
//
// Every path is relative, lexically normal and below the resource root; no
// absolute path, "..", drive, stream or reparse point is accepted. A resource
// is verified once (exact byte count and SHA-256 of every listed file) on its
// first resolve() and the result is cached: frames never hash or read files.
// A missing or corrupt resource reports its own failure; callers disable only
// the module that needs it.
#include "core/data/json.hpp"
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace endfield::core {
inline constexpr std::string_view resourceManifestFormat = "EndfieldHUD.Windows.Resources";
inline constexpr int resourceManifestSchema = 1;
inline constexpr std::uint64_t resourceFileLimit = 256ull * 1024 * 1024;

class ResourceLocator final {
public:
    enum class Status { verified, missing, corrupt };
    struct File { std::string path; std::uint64_t bytes{}; std::string sha256; };
    struct Entry { std::string id, path; bool directory{}; std::vector<File> files; };
    struct Resolution { std::filesystem::path path; Status status{Status::missing}; std::string reason; };

    // Throws std::runtime_error when the manifest itself is absent/invalid or
    // from a newer schema; never modifies the resource root.
    static ResourceLocator open(const std::filesystem::path& resourceRoot);
    const std::filesystem::path& root() const noexcept { return root_; }
    const std::string& sourceCommit() const noexcept { return sourceCommit_; }
    bool declared(std::string_view id) const noexcept;
    const Entry* entry(std::string_view id) const noexcept;
    std::optional<std::string> fileSHA256(std::string_view id, std::string_view relative) const;
    // Verifies on first use. Undeclared ids report missing.
    const Resolution& resolve(std::string_view id);
    bool available(std::string_view id) { return resolve(id).status == Status::verified; }
    std::vector<std::string> ids() const;
    std::size_t verifications() const noexcept { return verifications_; }
    // Package audit (verification only, never on a frame): every file or link
    // below the root that no entry lists, except resources.json, as sorted
    // relative UTF-8 paths. A clean staged package returns nothing.
    std::vector<std::string> unlistedFiles() const;
private:
    std::filesystem::path root_;
    std::string sourceCommit_;
    std::map<std::string, Entry, std::less<>> entries_;
    std::map<std::string, Resolution, std::less<>> resolved_;
    std::size_t verifications_{};
};
// Lexical relative-path policy shared by the stager and the locator.
bool validResourcePath(std::string_view relative, bool allowEmpty = false) noexcept;
} // namespace endfield::core
