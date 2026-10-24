#pragma once
#include "core/data/file_shelf_store.hpp"
#include <cstdint>
#include <optional>
#include <string>

namespace endfield::native {
// What the File Shelf's properties card reads from one reference
// (FileShelfCanvas.swift ShelfFileProperties.read, :14-37): its own dates and,
// for a folder, its direct children (never recursive). Read once per explicit
// request on the shared utility worker, never on the HUD thread, from the
// reference's own verified lease. Times are FILETIME ticks (UTC, 100 ns since
// 1601); the service converts them to local calendar fields for the card.
// The Mac card shows no size from here (the reference's size is the store's)
// and no image dimensions or media duration: none are read.
struct ShelfProperties {
    std::string windowsPath; // current path of the verified object
    bool directory{},symbolicLink{},cloudPlaceholder{},remote{};
    std::optional<std::int64_t> byteCount;        // regular files: end of file
    std::optional<std::uint64_t> folderItems;     // direct children, counted within the bound
    std::optional<std::int64_t> created,modified;
    bool operator==(const ShelfProperties&)const=default;
};
#ifdef _WIN32
struct ShelfPropertyOptions {
    // Direct children counted for an ordinary local folder (FindFirstFileEx,
    // names only, no recursion), hidden entries left out like the Mac's
    // .skipsHiddenFiles (Windows' hidden attribute: what File Explorer hides);
    // a larger folder shows no count (ShelfFileProperties.itemBound). Cloud
    // placeholder folders and remote folders are never enumerated (that would
    // download their listing).
    std::uint32_t folderItemBound{10000};
};
// Worker thread only. The lease's object is reopened without recall or
// following reparse points and its FILE_ID_INFO must match the lease before
// anything is read; a symbolic link is described itself, never followed.
// Throws when the object is unavailable or no longer the leased one.
ShelfProperties readShelfProperties(const ehud::data::ShelfFileAccess&,ShelfPropertyOptions={});
#endif
} // namespace endfield::native
