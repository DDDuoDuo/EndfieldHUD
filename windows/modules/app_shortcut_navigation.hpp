#pragma once
#include "modules/app_shortcut_model.hpp"
#include <map>
#include <span>

namespace endfield::modules {
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
