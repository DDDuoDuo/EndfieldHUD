#pragma once
#include "core/data/file_shelf_store.hpp"
#include <memory>

namespace endfield::native {
// Same fallback labels as FileShelfStore.swift: the owner supplies its current
// language. Shell extension/type-name lookup is intentionally not performed.
struct FileShelfFileLabels {
    std::string file{"File"},folder{"Folder"};
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
// the opened final object. A renamed ancestor/path is unavailable on resolution.
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
    explicit NativeFileShelfFiles(FileShelfFileLabels = {});
    ehud::data::ShelfFileAccess acquire(std::string_view explicitPath)const;
    ehud::data::ShelfFileAccess resolve(const ehud::data::ShelfRecord&)const;
    ehud::data::FileShelfStore::Platform platform()const;
private:
    std::shared_ptr<const FileShelfFileLabels> labels_;
};
} // namespace endfield::native
