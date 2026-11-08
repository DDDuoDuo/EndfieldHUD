#pragma once
#include "core/data/data_store.hpp"
#include "core/data/file_shelf_store.hpp"
#include "modules/reader_model.hpp"
#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ehud::migration {
// Mac bookmarks, security scopes and inode/volume identities cannot authorize
// Windows files. Every imported external reference is listed here until the
// user explicitly picks a replacement through a native picker. Nothing in this
// model searches by path or name, and no file is ever substituted automatically.
// Managed copies (Notes/Profile images, the center logo) are not listed: they
// were copied byte-for-byte by the import.
inline constexpr std::string_view relinkNotesMedia = "notes.media";
inline constexpr std::string_view relinkArchiveMedia = "archive.media";
inline constexpr std::string_view relinkReaderBook = "reader.book";
inline constexpr std::string_view relinkAppShortcut = "appShortcuts.app";
inline constexpr std::string_view relinkShelfItem = "fileShelf.item";
enum class RelinkState { unresolved, resolved, dismissed };
struct MacRelinkItem {
    std::string id;                 // ledger UUID
    std::string store;              // one of the constants above
    std::string recordID;           // note/entry/book/shortcut/shelf record UUID
    std::optional<std::size_t> index; // attachment position (Archive media)
    std::string displayName, lastKnownPath, kind;
    data::Json macReference{data::Json::Object{}}; // opaque original Mac locator/record
    RelinkState state{RelinkState::unresolved};
    std::optional<std::string> windowsPath; // set only after an explicit user choice
    bool operator==(const MacRelinkItem&) const = default;
};
data::Json encodeRelinkItem(const MacRelinkItem&);
MacRelinkItem decodeRelinkItem(const data::Json&);

// The explicit Windows target a resolved entry records: an absolute Windows
// file path (data::validWindowsFilePath) for every store, and for
// "appShortcuts.app" also a packaged application chosen in the applications
// dialog, "shell:AppsFolder\<PackageFamilyName>!<ApplicationId>" (ASCII
// package identity characters, at most 130 characters like
// APPLICATION_USER_MODEL_ID_MAX_LENGTH).
bool validMacRelinkTarget(std::string_view store, std::string_view target) noexcept;

// What a module owner's own store currently says about an entry's record.
enum class RelinkRecordState { macReference, relinked, missing };
struct RelinkRecordCheck {
    RelinkRecordState state{RelinkRecordState::macReference};
    std::string windowsPath;   // the record's explicit Windows locator when relinked
};
// Migration/relink.json under the Windows data root. Atomic compare-and-replace
// like every other store; a missing file is an empty ledger.
class MacRelinkLedger final {
public:
    static constexpr std::size_t maximumBytes = 8 * 1024 * 1024;
    static constexpr std::size_t maximumItems = 20000;
    explicit MacRelinkLedger(std::filesystem::path appRoot);
    const std::vector<MacRelinkItem>& items() const noexcept { return items_; }
    std::vector<MacRelinkItem> unresolved(std::string_view store = {}) const;
    const MacRelinkItem* find(std::string_view id) const noexcept;
    // Explicit user decisions only; each commits atomically before returning.
    // The path must pass validMacRelinkTarget for the entry's store.
    void markResolved(std::string_view id, std::string windowsPath);
    void dismiss(std::string_view id);
    // Owners relink the record first and then call markResolved. On load they
    // pass each unresolved entry of `store` through `check` (a lookup in their
    // own store, never a file search): an entry whose record already carries an
    // explicit Windows locator (the app stopped between the two writes) is
    // resolved with that path, and an entry whose record the user deleted is
    // dismissed. Entries whose record still holds the Mac reference stay
    // unresolved. One atomic write; returns the number of entries changed.
    std::size_t reconcile(std::string_view store, const std::function<RelinkRecordCheck(const MacRelinkItem&)>& check);
    const std::filesystem::path& path() const noexcept { return path_; }
    static std::string encode(const std::vector<MacRelinkItem>&);
    // The rows of a ledger file's bytes; throws data::StoreError (invalid,
    // newerVersion) like the constructor.
    static std::vector<MacRelinkItem> decode(const std::string& bytes);
private:
    std::filesystem::path path_;
    std::vector<MacRelinkItem> items_;
    std::optional<std::string> persisted_;
    void commit(std::vector<MacRelinkItem>);
};

// ---------------------------------------------------------------------------
// Owner API: the ledger by store and reference id
// ---------------------------------------------------------------------------
// The calls a module owner makes AFTER its own record write committed (record
// first, ledger second). The owner names its rows by what it already knows -
// the store constant above and its own record UUID, plus, for attachment
// lists, which attachment - never by a ledger id, path or name:
//
//   Notes        {relinkNotesMedia,   note.id}
//   Archive      {relinkArchiveMedia, entry.id, &macMediaObject, position}
//   Reader       {relinkReaderBook,   book.id}
//   App shortcuts{relinkAppShortcut,  shortcut.id}  (modules/app_shortcut_relink)
//   File Shelf   {relinkShelfItem,    shelf record id}
//
// resolveMacRelink   the user chose `target` for the record (or attachment)
//                    in the owner's own picker and the owner committed the
//                    relinked record: its unresolved rows are resolved.
// dismissMacRelink   the user removed the record (or attachment) the key
//                    names: its unresolved rows are dismissed.
// reconcileMacRelinks on load: every unresolved row of `store` through the
//                    owner's lookup (MacRelinkLedger::reconcile), which
//                    repairs an app that stopped between the two writes.
//
// Each call reads <appRoot>/Migration/relink.json afresh and writes it at most
// once, atomically (compare-and-replace). Without a ledger (no Mac import) or
// without a matching unresolved row nothing is created or written, so owners
// call these unconditionally. Calls are serialized within the process; a
// ledger another writer replaced between the read and the write is re-read
// and the change re-applied (bounded). They change unresolved rows only:
// earlier decisions and other stores' rows are kept, and repeating a call
// writes nothing. Unreadable or newer ledgers throw data::StoreError and are
// kept byte for byte. File work: call on the shared UtilityExecutor (or the
// owner's own worker), never on a warm frame.
struct MacRelinkKey {
    std::string_view store;    // relinkNotesMedia, ...
    std::string_view recordID; // the owner's record UUID (any letter case)
    // Attachment lists (relinkArchiveMedia) only. With `macReference`, the
    // attachment's Mac media object as the owner held it before the relink
    // or removal (the same Mac locator: bookmark and last known path, so
    // attachments removed before it do not matter); otherwise with `index`,
    // its position at import time; with neither, every row of the record
    // (e.g. the whole Archive entry was deleted). Two attachments of one
    // entry made from the same Mac file share one locator and are resolved
    // together.
    const data::Json* macReference{};
    std::optional<std::size_t> index{};
};
bool macRelinkMatches(const MacRelinkItem&, const MacRelinkKey&) noexcept;
struct MacRelinkChange {
    std::size_t resolved{}, dismissed{}; // rows this call changed
    std::size_t open{};                  // unresolved rows of the store afterwards
    bool operator==(const MacRelinkChange&) const = default;
};
// Throws data::StoreError(invalid) for an unknown store, an invalid record id
// or a target validMacRelinkTarget refuses, before anything is read.
MacRelinkChange resolveMacRelink(const std::filesystem::path& appRoot, const MacRelinkKey&, std::string_view target);
MacRelinkChange dismissMacRelink(const std::filesystem::path& appRoot, const MacRelinkKey&);
MacRelinkChange reconcileMacRelinks(const std::filesystem::path& appRoot, std::string_view store,
                                    const std::function<RelinkRecordCheck(const MacRelinkItem&)>& check);
// The one lookup rule for media rows (Notes payload, Archive attachments) of
// a record that still exists (the owner answers {missing} for a deleted
// record itself). `media` is the record's current media objects (a note's
// parsed payload as its one object, an entry's attachments in order):
//  - an object with the row's Mac locator (bookmark and last known path)
//    keeps the row listed;
//  - a note (a row without position) whose one payload is now a Windows
//    reference (referencePlatform "windows", valid windowsPath) resolves
//    with that path; a note without media is dismissed;
//  - an attachment whose Mac locator the entry no longer holds was relinked
//    or removed while the ledger write was lost: its row is dismissed.
//    Positions shift when attachments are removed, so a Windows attachment
//    is never attributed to a row by position (resolveMacRelink, called
//    after the relink, records the chosen path).
RelinkRecordCheck macRelinkMediaCheck(const MacRelinkItem&, std::span<const data::Json> media);

// Pure record transformations a module owner applies after the user picked
// `windowsPath` with the native picker and the file passed its own validation.
// Identity, order, title, progress, bookmarks and unknown fields are kept; the
// opaque Mac locator is replaced by the explicit additive Windows locator.
endfield::modules::ReaderBook relinkedReaderBook(const endfield::modules::ReaderBook&, std::string windowsPath);
struct WindowsMediaDescriptor {
    std::string windowsPath, displayName, kind{"image"};
    int pixelWidth{}, pixelHeight{};
    std::optional<double> duration;
    int frameCount{1};
};
// Notes media payload (JSON text) and Archive attachment (JSON object).
std::string relinkedNoteMedia(std::string_view macMediaJSON, const WindowsMediaDescriptor&);
data::Json relinkedMediaObject(const data::Json& macMedia, const WindowsMediaDescriptor&);
// Lets FileShelfStore::add keep the imported item's identifier and date.
data::FileShelfStore::Creation shelfRelinkCreation(const MacRelinkItem&);
// The shelf owner builds its one FileShelfStore with slot.creation(). While an
// explicit relink is armed, the next record created by add() takes the
// imported identifier and Foundation date; otherwise fresh values are used.
// Arm immediately before add() for the user-picked token, then disarm.
class ShelfRelinkSlot final {
public:
    ShelfRelinkSlot();
    data::FileShelfStore::Creation creation() const;
    void arm(const MacRelinkItem&);
    void disarm() noexcept;
    bool armed() const noexcept;
private:
    struct State;
    std::shared_ptr<State> state_;
};
}
