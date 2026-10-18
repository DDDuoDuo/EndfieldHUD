#include "modules/notes_relink_ledger.hpp"
#include "core/migration/mac_import_relink.hpp"
#include <array>
#include <exception>
#include <string>

namespace endfield::modules {
namespace {
namespace mig = ehud::migration;
using ehud::data::Json;
constexpr std::size_t payloadBytes = 16 * 1024 * 1024; // the Notes store's media payload limit
// Record identifiers are UUID strings; the ledger may spell them in another case.
bool sameID(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) return false;
    for (std::size_t n = 0; n < a.size(); ++n) {
        char x = a[n], y = b[n];
        if (x >= 'A' && x <= 'Z') x = char(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = char(y - 'A' + 'a');
        if (x != y) return false;
    }
    return true;
}
template <class F> RelinkLedgerResult guarded(F&& f) noexcept {
    try {
        const mig::MacRelinkChange change = f();
        return {change.resolved, change.dismissed, false};
    } catch (...) {
        return {0, 0, true};
    }
}
}

RelinkLedgerResult notesMediaRelinked(const std::filesystem::path& root, std::string_view noteID, std::string_view path) noexcept {
    return guarded([&] { return mig::resolveMacRelink(root, {mig::relinkNotesMedia, noteID}, path); });
}
RelinkLedgerResult notesMediaRemoved(const std::filesystem::path& root, std::string_view noteID) noexcept {
    return guarded([&] { return mig::dismissMacRelink(root, {mig::relinkNotesMedia, noteID}); });
}
RelinkLedgerResult notesMediaReconcile(const std::filesystem::path& root, std::span<const ehud::data::Note> notes) noexcept {
    return guarded([&] {
        return mig::reconcileMacRelinks(root, mig::relinkNotesMedia, [&](const mig::MacRelinkItem& item) -> mig::RelinkRecordCheck {
            for (const auto& note : notes) {
                if (!sameID(note.id, item.recordID)) continue;
                if (!note.media) return mig::macRelinkMediaCheck(item, {});
                const std::array media{Json::parse(*note.media, payloadBytes)};
                return mig::macRelinkMediaCheck(item, media);
            }
            return {mig::RelinkRecordState::missing, {}};
        });
    });
}

RelinkLedgerResult archiveMediaRelinked(const std::filesystem::path& root, std::string_view entryID, const Json& macMedia,
                                        std::string_view path) noexcept {
    return guarded([&] {
        mig::MacRelinkKey key{mig::relinkArchiveMedia, entryID};
        key.macReference = &macMedia;
        return mig::resolveMacRelink(root, key, path);
    });
}
RelinkLedgerResult archiveMediaRemoved(const std::filesystem::path& root, std::string_view entryID, const Json* macMedia) noexcept {
    return guarded([&] {
        mig::MacRelinkKey key{mig::relinkArchiveMedia, entryID};
        key.macReference = macMedia;
        return mig::dismissMacRelink(root, key);
    });
}
RelinkLedgerResult archiveMediaReconcile(
    const std::filesystem::path& root,
    const std::function<std::optional<std::vector<Json>>(std::string_view)>& media) noexcept {
    return guarded([&] {
        return mig::reconcileMacRelinks(root, mig::relinkArchiveMedia, [&](const mig::MacRelinkItem& item) -> mig::RelinkRecordCheck {
            const auto attachments = media(item.recordID);
            if (!attachments) return {mig::RelinkRecordState::missing, {}};
            return mig::macRelinkMediaCheck(item, *attachments);
        });
    });
}
}
