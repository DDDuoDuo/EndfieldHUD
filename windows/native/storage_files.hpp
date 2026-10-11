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
// StorageController's volumeNameKey title on Windows: the volume label when
// it has one, else Explorer's localized display name of the root ("Local
// Disk" for an unlabelled C:, without its " (C:)" drive suffix), else the
// source "Startup disk" literal. Pure; the reader below supplies both names.
std::string storageVolumeTitle(std::string_view label,std::string_view shellDisplayName,std::string_view root);
// Volume choice (Windows addition). Pure menu order: the startup volume first,
// then fixed before removable volumes, then by mount path (ASCII case
// folded); entries without an id or root, with invalid UTF-8 or a repeated id
// are dropped; at most modules::storageVolumeLimit remain.
std::vector<modules::StorageVolume>orderStorageVolumes(std::vector<modules::StorageVolume>);
#ifdef _WIN32
// Worker-thread functions only. Construction/resolution is intentionally inside
// each request, not the UI path. No query enumerates the whole startup volume.
// Windows synchronous filesystem calls cannot guarantee a deadline if a kernel
// driver hangs; limits/cancellation are checked BETWEEN bounded metadata calls.
std::optional<modules::StorageCapacity>readWindowsStartupCapacity(double date);
std::optional<modules::StorageCapacity>readWindowsCapacityAt(std::string_view explicitPath,double date);
// The fixed, removable and RAM-disk volumes that are mounted at a drive path
// and have media, named like readWindowsCapacityAt (orderStorageVolumes).
std::vector<modules::StorageVolume>listWindowsStorageVolumes();
// The chosen volume's capacity by its GUID path id (removable volumes too);
// the startup volume's for an empty id or a volume that is not mounted now.
std::optional<modules::StorageCapacity>readWindowsVolumeCapacity(std::string_view volumeID,double date);
std::unique_ptr<StorageFileSystem>makeWindowsStorageFileSystem();
std::vector<StorageFolderScope>windowsStorageScopes();
modules::StorageDetailsSnapshot scanWindowsStorageDetails(
    const modules::StorageScanCancellation&,double date,modules::StorageScanLimits={});
#endif
}
