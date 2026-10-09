#pragma once
#include "modules/storage_state.hpp"
#include <array>
#include <functional>
#include <memory>
#include <span>
#include <string_view>

namespace endfield::native {
struct StorageFolderScope {std::string id,title;std::vector<std::string>paths;};
struct StorageFileIdentity {std::uint64_t volume{};std::array<std::uint8_t,16>file{};auto operator<=>(const StorageFileIdentity&)const=default;};
enum class StorageFileKind { regular,directory,symbolicLink,reparse,other };
struct StorageFileMetadata {StorageFileIdentity identity;StorageFileKind kind{StorageFileKind::other};std::int64_t allocatedBytes{};bool unavailable{};};
struct StorageDirectoryEntry {std::u16string name;StorageFileMetadata metadata;};
enum class StorageDirectoryRead {entry,end,error};
// Cursor owns one already-open directory. Children MUST be opened relative to
// that handle, no-follow/no-recall, never by concatenating an absolute path.
class StorageDirectory {
public:virtual ~StorageDirectory()=default;
    virtual const StorageFileMetadata&metadata()const noexcept=0;
    virtual StorageDirectoryRead next(StorageDirectoryEntry&)=0;
    virtual std::unique_ptr<StorageDirectory>openChild(std::u16string_view)=0;
};
class StorageFileSystem {
public:virtual ~StorageFileSystem()=default;
    virtual std::unique_ptr<StorageDirectory>openRoot(std::string_view)=0;
};
using StorageScanClock=std::function<double()>;
// Exact source category accounting and bounds. No content reads. A shared
// identity set spans all roots/categories so hardlinks count only once.
// Multiple Windows Program Files roots form the single Applications category.
modules::StorageDetailsSnapshot scanStorageFolders(std::span<const StorageFolderScope>,
    const modules::StorageScanCancellation&,double date,StorageFileSystem&,
    std::optional<std::uint64_t>expectedVolume,StorageScanClock,
    modules::StorageScanLimits={});
#ifdef _WIN32
// Worker-thread functions only. Construction/resolution is intentionally inside
// each request, not the UI path. No query enumerates the whole startup volume.
// Windows synchronous filesystem calls cannot guarantee a deadline if a kernel
// driver hangs; limits/cancellation are checked BETWEEN bounded metadata calls.
std::optional<modules::StorageCapacity>readWindowsStartupCapacity(double date);
std::optional<modules::StorageCapacity>readWindowsCapacityAt(std::string_view explicitPath,double date);
std::unique_ptr<StorageFileSystem>makeWindowsStorageFileSystem();
std::vector<StorageFolderScope>windowsStorageScopes();
modules::StorageDetailsSnapshot scanWindowsStorageDetails(
    const modules::StorageScanCancellation&,double date,modules::StorageScanLimits={});
#endif
}
