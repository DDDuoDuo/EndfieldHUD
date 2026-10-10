#pragma once
#include "core/data/file_shelf_store.hpp"
#include <memory>

namespace endfield::native {
// Same fallback labels as FileShelfStore.swift: the owner supplies its current
// language. Without FileShelfFileOptions::shellTypeNames no Shell type-name
// lookup is performed and files always use the fallback label.
struct FileShelfFileLabels {
    std::string file{"File"},folder{"Folder"};
};
// Optional Mac-equivalent behaviours (FileShelfStore.swift), both off by
// default so a plain provider keeps the strict path-only contract below.
struct FileShelfFileOptions {
    // Bookmark equivalent: when the stored path is gone or now holds a different
    // object, reopen the ORIGINAL object by its 128-bit file ID on the same
    // volume (OpenFileById, verified volume serial) and report its current path
    // (GetFinalPathNameByHandleW), as a stale Mac bookmark resolves a moved or
    // renamed item. Identity is the only key: no path, name or directory search
    // is ever used. An object found inside a "$Recycle.Bin" folder is deleted for
    // the user (Windows renames it there) and stays unavailable. Other volumes,
    // shares without file-ID opens and reused IDs (the NTFS sequence number is
    // part of the ID) cannot match. FileShelfStore::refresh commits the path.
    bool relocateByIdentity{};
    // Mac localizedTypeDescription equivalent for ordinary files: the Shell's
    // registered type name for the extension (SHGetFileInfoW SHGFI_TYPENAME |
    // SHGFI_USEFILEATTRIBUTES: registry only, no file access or handler code),
    // in the Windows display language. Folders keep the caller's label.
    bool shellTypeNames{};
};
// Explicit-path metadata provider, with no picker, OLE, watcher or content I/O.
// Each successful access owns ONE non-inheritable read-access Win32 handle,
// used only for metadata queries. FILE_READ_DATA (FILE_LIST_DIRECTORY on a
// directory) participates in sharing checks; attribute-only access does not.
// No ReadFile, mapping or directory-enumeration operation is performed.
// FILE_ID_INFO (128-bit file ID plus volume serial) is required; unsupported
// filesystems fail instead of falling back to path equality or a 64-bit ID.
//
// Paths must be ordinary absolute UTF-8 drive/UNC paths accepted by the native
// store. Device namespaces, ADS, relative paths and dot components are rejected
// before CreateFile. The final component is opened without following reparse
// points: ordinary files/directories and selected symbolic links are supported;
// junctions, cloud placeholders and other reparse types explicitly are not.
// Link size is absent and target reachability/content access is NOT established.
// No directory enumeration, rename search, bookmark migration or target copy
// occurs. An ancestor may be a filesystem redirect; identity always comes from
// the opened final object. Without relocateByIdentity a renamed ancestor/path
// is unavailable on resolution; with it the original object is reopened by ID.
//
// Read/write sharing remains enabled. Delete sharing is withheld while leased,
// preventing replacement/rename of the selected object during handoff. This is
// not an immutable namespace or content snapshot: ancestor renames, permission
// changes and content writes remain possible. A future native transfer must
// retain the bundle and revalidate paths, not assume CF_HDROP preserves identity.
// This read-access check does not promise a later transfer/open will succeed.
// Leases and platform callbacks are independent of this wrapper's lifetime.
class NativeFileShelfFiles final {
public:
    explicit NativeFileShelfFiles(FileShelfFileLabels = {},FileShelfFileOptions = {});
    ehud::data::ShelfFileAccess acquire(std::string_view explicitPath)const;
    ehud::data::ShelfFileAccess resolve(const ehud::data::ShelfRecord&)const;
    ehud::data::FileShelfStore::Platform platform()const;
    struct Configuration {FileShelfFileLabels labels;FileShelfFileOptions options;};
private:
    std::shared_ptr<const Configuration> configuration_;
};
// Pure helpers, also exercised on other platforms. relocatedShelfPath converts
// a GetFinalPathNameByHandleW DOS result ("\\?\C:\..." or "\\?\UNC\s\h\...")
// to the ordinary path form the store accepts; empty when it is not one.
std::string relocatedShelfPath(std::string_view finalPath);
// True when any component is "$Recycle.Bin" (ASCII case-insensitive).
bool shelfPathInRecycleBin(std::string_view windowsPath) noexcept;
// The volume mount root of an ordinary drive or UNC path ("C:\", "\\s\h\"),
// derived from the text only; empty when the path has no such prefix.
std::string shelfPathVolumeRoot(std::string_view windowsPath);
} // namespace endfield::native
