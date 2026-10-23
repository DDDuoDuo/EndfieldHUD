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
// StorageController's volume name on Windows (volumeLocalizedName ??
// volumeName, Sources/StorageController.swift:165-170): the volume label when
// it has one, else Explorer's localized display name of the root ("Local
// Disk" for an unlabelled C:, without its " (C:)" drive suffix), else
// `fallback` (the startup disk: the localized "Startup disk" / 启动磁盘 of
// the request's language, MODS-436; another disk: its drive, as the Mac
// falls back to the mount point's last path component). Pure.
std::string storageVolumeTitle(std::string_view label,std::string_view shellDisplayName,std::string_view root,std::string_view fallback);
// localizedStandardCompare (Windows: CompareStringEx natural order; portable
// tests inject a deterministic one). Negative/zero/positive.
using StorageNameCompare=std::function<int(std::string_view,std::string_view)>;
// StorageVolumeCatalog.volumes' rules (:79-102), pure: the startup disk first,
// then internal (fixed, RAM disk) before external (removable) disks, then by
// name (`compare`), then by mount path; one entry per id (the startup disk's,
// else the first listed);
// entries without an id or root, with invalid UTF-8 or a non-positive or
// inconsistent capacity are left out. No limit here: the controller keeps
// at most storageVolumeLimit with the current choice (limitedStorageVolumes).
std::vector<modules::StorageVolume>orderStorageVolumes(std::vector<modules::StorageVolume>,const StorageNameCompare&);
#ifdef _WIN32
// Worker-thread functions only. Construction/resolution is intentionally inside
// each request, not the UI path. No query enumerates the whole startup volume.
// Windows synchronous filesystem calls cannot guarantee a deadline if a kernel
// driver hangs; limits/cancellation are checked BETWEEN bounded metadata calls.
// GetDiskFreeSpaceExW's free-to-caller figure is the one Explorer shows
// (MODS-436: the Mac's "available for important usage").
std::optional<modules::StorageCapacity>readWindowsStartupCapacity(double date,std::string_view startupName);
std::optional<modules::StorageCapacity>readWindowsCapacityAt(std::string_view explicitPath,double date,std::string_view fallbackName);
// One capacity read (StorageController.refreshCapacity's worker, :430-441,
// with StorageVolumeCatalog.mountedVolumes, :126-137): every fixed, removable
// and RAM-disk volume mounted at a drive path with media, with its numbers
// (unreadable ones left out), ordered; plus the startup reader's capacity when
// the list lacks the startup disk.
modules::StorageCapacityResult readWindowsStorage(double date,std::string_view startupName,const StorageNameCompare&);
std::unique_ptr<StorageFileSystem>makeWindowsStorageFileSystem();
std::vector<StorageFolderScope>windowsStorageScopes();
modules::StorageDetailsSnapshot scanWindowsStorageDetails(
    const modules::StorageScanCancellation&,double date,modules::StorageScanLimits={});
#endif
}
