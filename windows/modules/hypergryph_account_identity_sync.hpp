#pragma once
#include "core/data/data_store.hpp"
#include "modules/hypergryph_account_controller.hpp"

// Personal-profile sync for the selected Endfield snapshot (Mac applyProfile +
// UserProfileStore.update normalization). The original local UID is never
// changed; gamePlayerID is set and playerIDOverride cleared; name/# and the
// awakening date come from the game; levels and counts are copied. Disabling
// sync only unlocks editing: nothing is restored.
namespace endfield::modules::hypergryph {
// Applies the update and the Mac normalizedEditableValues rules that it can
// affect (name/tag trimmed and limited to 20/10 characters; permission 1...60;
// exploration 1...7). Throws std::invalid_argument for values the Mac store
// would reject (empty name, control characters, invalid tag).
void applyGameProfileUpdate(ehud::data::Profile&,const GameProfileUpdate&);
// String.prefix(n) on grapheme clusters (Extend/SpacingMark/ZWJ/Prepend joins).
std::string prefixCharacters(std::string_view,std::size_t count);
std::size_t characterCount(std::string_view);

// Production sink over the shared ProfileStore. importAvatar is supplied by the
// personal-profile owner (managed image import) and returns the new filename.
class ProfileStoreAccountSink final:public AccountProfileSink {
public:
    using ImportAvatar=std::function<std::optional<std::string>(const std::vector<std::uint8_t>&)>;
    using Changed=std::function<void()>;
    ProfileStoreAccountSink(ehud::data::ProfileStore&,ImportAvatar={},Changed={});
    void applyGameProfile(const GameProfileUpdate&) override;
    void setProfileSyncLocked(bool) override;
    std::optional<std::string> importGameAvatar(const std::vector<std::uint8_t>&) override;
private:
    ehud::data::ProfileStore* store_;ImportAvatar import_;Changed changed_;
};
}
