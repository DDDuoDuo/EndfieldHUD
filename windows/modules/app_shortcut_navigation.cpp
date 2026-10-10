#include "modules/app_shortcut_navigation.hpp"
#include <algorithm>
#include <limits>
#include <set>

namespace endfield::modules {
namespace {void need(bool v,const char*s){if(!v)throw std::invalid_argument(s);}}
ShortcutNavigation::ShortcutNavigation(std::vector<ShortcutNavigationEntry> modules,std::vector<std::uint64_t> right)
    :modules_(std::move(modules)),moduleRight_(std::move(right)){
    need(!modules_.empty()&&modules_.size()<=1024&&!moduleRight_.empty(),"Invalid application navigation base");
    std::set<std::uint64_t>seen;std::optional<std::uint64_t>add;
    for(const auto&e:modules_){need(e.module&&e.action&&seen.insert(e.action).second&&!e.target.empty()&&ShortcutJson::validUtf8(e.title)&&!e.iconKey.empty(),"Invalid module navigation identity");nextAction_=std::max(nextAction_,e.action);if(e.target=="addApp"){need(!add,"Repeated Add App module");add=e.action;}}
    need(add&&moduleRight_.back()==*add,"Add App must remain last");
    std::set<std::uint64_t>rightSeen;for(auto action:moduleRight_)need(seen.contains(action)&&rightSeen.insert(action).second,"Invalid right navigation module");
    entries_=modules_;right_=moduleRight_;
}
bool ShortcutNavigation::replace(const ShortcutFile&file){
    need(file.items.size()<=1024-modules_.size(),"Saved applications exceed source navigation capacity");
    auto next=modules_;auto right=moduleRight_;right.pop_back();
    std::map<std::string,std::uint64_t,std::less<>>actions;auto nextAction=nextAction_;
    for(const auto&item:file.items){
        need(ehud::data::validUUID(item.id)&&validShortcutIcon(item.iconPreset)&&ShortcutJson::validUtf8(item.name)&&!item.name.empty(),"Invalid saved application navigation record");
        need(!actions.contains(item.id),"Repeated saved application navigation identity");
        const auto old=actions_.find(item.id);std::uint64_t action{};
        if(old!=actions_.end())action=old->second;
        else{need(nextAction<std::numeric_limits<std::uint64_t>::max(),"Application action identities exhausted");action=++nextAction;}
        actions.emplace(item.id,action);right.push_back(action);
        next.push_back({action,"app."+item.id,item.name,"shortcut:"+item.iconPreset,false});
    }
    right.push_back(moduleRight_.back());
    // Match the source module/custom/AddApp entry order as well as row order.
    const auto add=std::find_if(next.begin(),next.end(),[](const auto&e){return e.module&&e.target=="addApp";});
    std::rotate(add,add+1,next.end());
    if(next==entries_&&right==right_)return false;
    entries_.swap(next);right_.swap(right);actions_.swap(actions);nextAction_=nextAction;return true;
}
bool ShortcutNavigation::setModuleTitles(std::span<const std::pair<std::uint64_t,std::string>>titles){
    need(titles.size()==modules_.size(),"Every module needs its localized title");
    auto modules=modules_;std::set<std::uint64_t>seen;
    for(const auto&[action,title]:titles){const auto it=std::find_if(modules.begin(),modules.end(),[&](const auto&e){return e.action==action;});need(it!=modules.end()&&seen.insert(action).second&&ShortcutJson::validUtf8(title),"Invalid localized module title");it->title=title;}
    if(modules==modules_)return false;
    auto entries=entries_;for(auto&e:entries)if(e.module)e.title=std::find_if(modules.begin(),modules.end(),[&](const auto&v){return v.action==e.action;})->title;
    modules_.swap(modules);entries_.swap(entries);return true;
}
std::optional<std::string_view>ShortcutNavigation::shortcutID(std::uint64_t action)const noexcept{
    for(const auto&[id,value]:actions_)if(value==action)return id;return {};
}
}
