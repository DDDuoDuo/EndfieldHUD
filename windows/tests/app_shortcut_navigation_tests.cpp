#include "modules/app_shortcut_navigation.hpp"
#include <iostream>
#include <array>
namespace m=endfield::modules;
namespace {
unsigned checks{};void check(bool v,const char*s){++checks;if(!v)throw std::runtime_error(s);}
template<class F>void rejects(F f){bool yes{};try{f();}catch(const std::exception&){yes=true;}check(yes,"Invalid navigation update rejects atomically");}
void run(){
    m::ShortcutNavigation nav({{1,"system","System","module:system"},{2,"notes","Notes","module:notes"},{3,"addApp","+ Add App","module:addApp"}},{2,3});
    m::ShortcutFile file;m::ShortcutRecord one,two;one.id="11111111-1111-4111-8111-111111111111";one.name="微信";one.iconPreset="textBubble";two=one;two.id="22222222-2222-4222-8222-222222222222";two.name="Custom user text";two.iconPreset="original";file.items={one,two};
    check(nav.replace(file),"Committed list adds source sidebar entries");
    const auto a=nav.rightActions()[1],b=nav.rightActions()[2];
    check(a>3&&b>a&&nav.rightActions().back()==3&&nav.entries().back().target=="addApp","Apps follow Power and precede always-last Add App");
    check(nav.shortcutID(a)==one.id&&nav.shortcutID(b)==two.id&&!nav.shortcutID(3),"Recycled sidebar slots resolve saved identities without aliasing modules");
    check(nav.entries()[2].title==one.name&&nav.entries()[2].iconKey=="shortcut:textBubble"&&!nav.entries()[2].module,"Saved name and exact selected icon remain paired");
    check(!nav.replace(file),"Unchanged committed list does not mutate navigation");
    std::swap(file.items[0],file.items[1]);file.items[1].name="改名";file.items[1].iconPreset="globe";nav.replace(file);
    check(nav.rightActions()[1]==b&&nav.rightActions()[2]==a&&nav.entries()[3].iconKey=="shortcut:globe","Rename, reorder and icon edits retain launch identity");
    const std::array<std::pair<std::uint64_t,std::string>,3>titles{{{1,"系统"},{2,"便笺"},{3,"+ 添加应用"}}};nav.setModuleTitles(titles);
    check(nav.entries()[1].title=="便笺"&&nav.entries()[2].title==two.name&&nav.entries()[3].title=="改名","Language event updates modules and preserves opaque custom names");
    check(!nav.setModuleTitles(titles),"Equal localization is retained");
    file.items.erase(file.items.begin()+1);nav.replace(file);check(!nav.shortcutID(a),"Removed action no longer launches any app");file.items.push_back(one);nav.replace(file);check(nav.rightActions()[2]>b,"Re-added item never reuses a stale queued launch action");
    const auto count=nav.entries().size();file.items.push_back(one);rejects([&]{nav.replace(file);});check(nav.entries().size()==count,"Rejected duplicate preserves published navigation");
    rejects([&]{m::ShortcutNavigation invalid({{1,"addApp","Add","module:addApp"}},{2});});
    for(unsigned n=0;n<10000;++n)check(nav.shortcutID(b)==two.id,"Warm hit resolution preserves identity");
}
}
int main(){try{run();std::cout<<"Shortcut navigation: "<<checks<<" checks passed\n";return 0;}catch(const std::exception&e){std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}}
