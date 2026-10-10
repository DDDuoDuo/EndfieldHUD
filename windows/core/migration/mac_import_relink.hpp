#pragma once
#include "core/data/data_store.hpp"
#include "core/data/file_shelf_store.hpp"
#include "modules/reader_model.hpp"
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
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
private:
    std::filesystem::path path_;
    std::vector<MacRelinkItem> items_;
    std::optional<std::string> persisted_;
    void commit(std::vector<MacRelinkItem>);
};

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
