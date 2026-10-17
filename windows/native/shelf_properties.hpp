#pragma once
#include "core/data/file_shelf_store.hpp"
#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace endfield::native {
// Properties of one Temporary File Shelf reference, shown by the Shelf's
// properties card (which replaces the Quick Look preview). Read once per
// explicit request on the shared utility worker, never on the HUD thread,
// from the reference's own verified lease. Times are FILETIME ticks (UTC,
// 100 ns since 1601); the owner formats everything in the HUD language.
struct ShelfProperties {
    std::string windowsPath; // current path of the verified object
    bool directory{},symbolicLink{},cloudPlaceholder{},remote{};
    std::optional<std::int64_t> byteCount;        // regular files: end of file
    std::optional<std::uint64_t> folderItems;     // direct children, counted within the bound
    std::optional<std::int64_t> created,modified;
    std::optional<std::array<std::uint32_t,2>> dimensions; // pixels (images, else video frames)
    std::optional<std::uint64_t> duration;        // 100 ns (audio, video)
    bool operator==(const ShelfProperties&)const=default;
};
// Pure text helpers (every platform).
// "0:07", "3:25", "1:02:03" (hours only when present; seconds round down).
std::string shelfDurationText(std::uint64_t hundredNanoseconds);
// Decimal digits grouped by three with ',' ("12,345").
std::string shelfGroupedCount(std::uint64_t);
#ifdef _WIN32
struct ShelfPropertyOptions {
    // Direct children counted for an ordinary local folder (FindFirstFileEx,
    // names only, no recursion); a larger folder shows no count. Cloud
    // placeholder folders and remote folders are never enumerated (that would
    // download their listing).
    std::uint32_t folderItemBound{10000};
    // Shell property handlers (IPropertyStore, read-only) for image
    // dimensions and media duration of ordinary local files. Cloud
    // placeholders and files on remote volumes use cached properties only
    // (GPS_FASTPROPERTIESONLY): no handler reads (or downloads) their contents.
    bool propertyHandlers{true};
};
// Worker thread only; COM is initialized (multithreaded) for the call when the
// thread has none. The lease's object is reopened without recall or following
// reparse points and its FILE_ID_INFO must match the lease before anything is
// read; a symbolic link is described itself, never followed. Throws when the
// object is unavailable or no longer the leased one.
ShelfProperties readShelfProperties(const ehud::data::ShelfFileAccess&,ShelfPropertyOptions={});
#endif
} // namespace endfield::native
