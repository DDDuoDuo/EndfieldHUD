#pragma once
#include "core/data/data_store.hpp"
#include <array>
#include <functional>
#include <span>

namespace ehud::data {
// Native Windows identity, deliberately distinct from Mac inode/bookmark data.
// The adapter supplies FILE_ID_INFO-equivalent object bytes and volume serial,
// optionally a stable volume UUID. A path match alone never proves identity.
struct ShelfFileIdentity {
    std::array<std::uint8_t,16> objectID{};
    std::uint64_t volumeSerial{};
    std::optional<std::string> volumeUUID;
    bool matches(const ShelfFileIdentity&) const noexcept;
    bool operator==(const ShelfFileIdentity&) const = default;
};
enum class ShelfFileKind {regular,directory,symbolicLink};
struct ShelfFileMetadata {
    std::string windowsPath,name,typeDescription;
    std::optional<std::int64_t> byteCount;
    bool isDirectory{};
    ShelfFileKind kind{ShelfFileKind::regular};
    ShelfFileIdentity identity;
};
// A native provider constructs a lease only after it has verified a readable
// filesystem reference. Symlinks/reparse references must retain the selected
// object identity, not silently resolve to an unrelated target. No OS calls are
// made here. The cleanup is idempotent, independent of store lifetime and must
// not throw; it balances any native handle/access scope even on failed batches.
class ShelfFileAccess final {
public:
    ShelfFileAccess()=default;
    ShelfFileAccess(ShelfFileMetadata,std::function<void()> cleanup);
    ~ShelfFileAccess();
    ShelfFileAccess(ShelfFileAccess&&) noexcept;
    ShelfFileAccess& operator=(ShelfFileAccess&&) noexcept;
    ShelfFileAccess(const ShelfFileAccess&)=delete;
    ShelfFileAccess& operator=(const ShelfFileAccess&)=delete;
    const ShelfFileMetadata& metadata() const noexcept{return metadata_;}
    bool open() const noexcept{return bool(cleanup_);}
    void close() noexcept;
private:
    ShelfFileMetadata metadata_;std::function<void()> cleanup_;
};
struct ShelfRecord {
    std::string id,windowsPath,name,typeDescription;
    std::optional<std::int64_t> byteCount;
    bool isDirectory{};
    double createdAt{}; // Foundation epoch, as in original FileShelfStore.swift
    ShelfFileIdentity identity;
    std::optional<std::string> availabilityError; // session-only; never encoded
    Json originalFields{Json::Object{}};
    bool operator==(const ShelfRecord&)const=default;
};
// The receiver owns the complete bundle until its native transfer ends, even
// if the shelf/view/store closes earlier. There is deliberately no move/delete
// operation. This is an access-lifetime seam, not an OLE implementation.
struct ShelfCopyBundle {
    enum class Operation {copy};
    static constexpr Operation operation=Operation::copy;
    std::vector<std::string> ids;
    std::vector<ShelfFileAccess> accesses;
};
// One owner calls on its UI thread. Provider callbacks are synchronous and
// must not reenter or destroy the store; transferred leases can outlive it.
class FileShelfStore final {
public:
    struct Platform {
        // token comes from an authorized file chooser/drop; validate that it is
        // a supported local filesystem object, then acquire identity+metadata
        // under its lease. Do not read contents, recursively enumerate or copy.
        std::function<ShelfFileAccess(std::string_view token)> acquireImport;
        // Resolve without UI/mounting. Return current metadata under a lease;
        // this store compares actual identity before exposing the reference.
        // Rename/relink lookup is adapter-owned; path-only fallback still has
        // to return an identity, and replacement at that path is rejected.
        std::function<ShelfFileAccess(const ShelfRecord&)> resolve;
    };
    struct Creation {
        std::function<std::string()> id=makeUUID;
        std::function<double()> timestamp=foundationNow;
    };
    static constexpr std::size_t maximumArchiveBytes=4*1024*1024;
    // Explicit app-specific Windows data root; no implicit home/appdata lookup.
    // New v1 records use referencePlatform="windows", windowsPath and
    // windowsIdentity. Mac bookmark archives and mixed locators are rejected
    // without rewriting. No cross-platform bookmark migration is claimed.
    FileShelfStore(const std::filesystem::path& appRoot,Platform);
    FileShelfStore(const std::filesystem::path& appRoot,Platform,Creation);
    FileShelfStore(const FileShelfStore&)=delete;
    FileShelfStore& operator=(const FileShelfStore&)=delete;
    const std::vector<ShelfRecord>& items()const noexcept{return items_;}
    const std::filesystem::path& path()const noexcept{return path_;}
    // All incoming references validate before ONE compare-and-replace save.
    // Deduplication uses filesystem identity, not name/path, including within
    // the same batch. A duplicate-only batch is a successful no-op (returns 0).
    std::size_t add(std::span<const std::string> tokens);
    bool remove(std::string_view id); // references only, commit before UI mutation
    bool clear();
    void refresh(); // explicit activation call; no watcher/polling/service
    ShelfFileAccess access(std::string_view id);
    ShelfCopyBundle prepareCopy(std::span<const std::string> ids);
    // Convenience injected transfer handoff. Receivers may move the bundle
    // into an asynchronous copy-only data object and destroy this store. All
    // preparation guards finish before handoff; no native drag is started here.
    void copy(std::span<const std::string> ids,std::function<void(ShelfCopyBundle)>);
private:
    std::filesystem::path path_;Platform platform_;Creation creation_;
    std::vector<ShelfRecord> items_;Json envelope_{Json::Object{}};
    std::optional<std::string> persisted_;bool busy_{};
    ShelfFileAccess resolve(const ShelfRecord&);
    void commit(std::vector<ShelfRecord>);
};
} // namespace ehud::data
