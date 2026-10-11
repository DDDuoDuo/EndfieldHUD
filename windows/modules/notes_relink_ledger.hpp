#pragma once
// Upkeep of the Mac import relink ledger (<app root>/Migration/relink.json)
// for Notes media notes ("notes.media") and Archive media attachments
// ("archive.media"), through the import ledger's owner API
// (core/migration/mac_import_relink.hpp). The owner's own record write
// commits first; these calls follow it. Each reads the ledger afresh and
// writes it at most once, atomically; without a ledger (no Mac import) or
// without a matching unresolved row nothing is created or written, so owners
// call them unconditionally. A ledger that cannot be read or written is
// reported in the result and never thrown: the record is already committed,
// the ledger keeps its bytes and the next load's reconcile repairs it.
// File work: the Archive owner calls these on its FIFO file executor right
// after the record write; Notes, whose store writes synchronously on the
// owner thread, right after that write (explicit user actions and the
// owner's construction, never a frame).
#include "core/data/data_store.hpp"
#include <cstddef>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace endfield::modules {
struct RelinkLedgerResult {
    std::size_t resolved{}, dismissed{};
    bool failed{}; // the ledger could not be read or written; it was left as it was
    bool operator==(const RelinkLedgerResult&) const = default;
};

// Notes. The media note `noteID` now carries the Windows reference whose
// path is `windowsPath` (the user chose the file again): its row resolves.
RelinkLedgerResult notesMediaRelinked(const std::filesystem::path& appRoot, std::string_view noteID,
                                      std::string_view windowsPath) noexcept;
// The note `noteID` was deleted: its row is dismissed.
RelinkLedgerResult notesMediaRemoved(const std::filesystem::path& appRoot, std::string_view noteID) noexcept;
// On load: every unresolved Notes row against the stored notes. A note whose
// media is now a Windows reference resolves with its path (the app stopped
// between the record and the ledger write); a deleted note, or one without
// media, is dismissed; a note still holding its Mac locator stays listed.
RelinkLedgerResult notesMediaReconcile(const std::filesystem::path& appRoot,
                                       std::span<const ehud::data::Note>) noexcept;

// Archive. In entry `entryID`, the attachment that held the Mac media object
// `macMedia` now holds the Windows reference whose path is `windowsPath`.
RelinkLedgerResult archiveMediaRelinked(const std::filesystem::path& appRoot, std::string_view entryID,
                                        const ehud::data::Json& macMedia, std::string_view windowsPath) noexcept;
// The attachment that held `macMedia` was removed from entry `entryID`, or,
// with null `macMedia`, the whole entry was deleted: those rows are dismissed.
RelinkLedgerResult archiveMediaRemoved(const std::filesystem::path& appRoot, std::string_view entryID,
                                       const ehud::data::Json* macMedia) noexcept;
// On load: `media(entryID)` answers the stored entry's attachments in order,
// or nullopt for an entry that no longer exists (its rows are dismissed).
// An attachment row whose Mac locator the entry no longer holds is dismissed;
// one still held stays listed.
RelinkLedgerResult archiveMediaReconcile(
    const std::filesystem::path& appRoot,
    const std::function<std::optional<std::vector<ehud::data::Json>>(std::string_view entryID)>& media) noexcept;
}
