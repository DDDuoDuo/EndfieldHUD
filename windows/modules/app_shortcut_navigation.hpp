#pragma once
#include "modules/app_shortcut_model.hpp"
#include <functional>
#include <map>
#include <span>
#include <vector>

namespace endfield::modules {
// One published original application image: an exact LayerImageSource
// key/revision (contents {memoryImage, revision}). An empty key is absent.
struct ShortcutImageReference {
    std::string key; std::uint64_t revision{};
    bool operator==(const ShortcutImageReference&) const = default;
};
// Original application icons: the Mac caches one for every App-icon shortcut
// (AppShortcutStore.icon(for:), no bound), so every row and navigation entry
// of a long list shows its own icon. The bound caps memory only, at about
// 4.5 MiB of 96-pixel icons; the draft counts towards it.
inline constexpr std::size_t shortcutOriginalIconCapacity=128;
// The saved shortcuts whose original icons are loaded, as indices into the
// list in loading order: the rows visible at `scrollOffset` (62-point row
// pitch, eight shown at most; the Mac's AppShortcutCanvas asks every row's
// icon when it builds the page), then the
// others in stored order (SystemHUDView.appNavigationPresentations), each
// once, App-icon preset only and accepted by `accepts` (a Windows target);
// at most `maximum`.
std::vector<std::size_t> shortcutOriginalIconOrder(const ShortcutFile&,double scrollOffset,std::size_t maximum,
    const std::function<bool(const ShortcutRecord&)>& accepts);
struct ShortcutNavigationEntry {
    std::uint64_t action{};
    std::string target,title,iconKey;
    bool module{true};
    bool operator==(const ShortcutNavigationEntry&)const=default;
};
// Discrete committed-list/localization events only. Source row layout still
// owns scrolling and placement. Custom actions have stable identities across
// rename/reorder, never reuse a removed action and never alias Add App.
class ShortcutNavigation final {
public:
    ShortcutNavigation(std::vector<ShortcutNavigationEntry> modules,std::vector<std::uint64_t> right);
    bool replace(const ShortcutFile&);
    bool setModuleTitles(std::span<const std::pair<std::uint64_t,std::string>>);
    std::span<const ShortcutNavigationEntry>entries()const noexcept{return entries_;}
    std::span<const std::uint64_t>rightActions()const noexcept{return right_;}
    std::optional<std::string_view>shortcutID(std::uint64_t)const noexcept;
private:
    std::vector<ShortcutNavigationEntry>modules_,entries_;
    std::vector<std::uint64_t>moduleRight_,right_;
    std::map<std::string,std::uint64_t,std::less<>>actions_;
    std::uint64_t nextAction_{};
};
}
