#include "modules/app_shortcut_navigation.hpp"
#include <iostream>
#include <array>
#include <cmath>
#include <string>
#include <vector>
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
// Original application icons for every App-icon shortcut (AppShortcutStore's
// icon cache has no bound on the Mac): far more than the former 20, the
// visible rows first, only App-icon shortcuts with a target, each once.
void icons(){
    m::ShortcutFile file;for(unsigned n=0;n<40;++n){m::ShortcutRecord r;r.id="00000000-0000-4000-8000-"+std::string(10,'0')+(n<10?"0":"")+std::to_string(n);r.name="App "+std::to_string(n);r.iconPreset=n%10==3?"globe":"original";file.items.push_back(r);}
    const auto every=[](const m::ShortcutRecord&){return true;};
    const auto order=m::shortcutOriginalIconOrder(file,0,m::shortcutOriginalIconCapacity,every);
    check(m::shortcutOriginalIconCapacity>=128&&order.size()==36,"Every App-icon shortcut of a 40-item list loads its icon (the four Globe ones do not)");
    check(order[0]==0&&order[1]==1&&order[2]==2&&order[3]==4&&order.back()==39,"From the top the rows load in stored order, skipping other presets");
    const auto scrolled=m::shortcutOriginalIconOrder(file,52*20.+10,m::shortcutOriginalIconCapacity,every);
    check(scrolled.size()==36&&scrolled[0]==20&&scrolled[1]==21&&scrolled[2]==22&&scrolled[3]==24&&scrolled[4]==25&&scrolled[5]==26&&scrolled[6]==0&&scrolled[7]==1,"The seven visible rows load first, then the rest in stored order");
    std::vector<bool>seen(file.items.size());for(const auto n:scrolled){check(!seen[n],"Each shortcut loads once");seen[n]=true;}
    const auto bounded=m::shortcutOriginalIconOrder(file,52*20.,5,every);check(bounded.size()==5&&bounded[0]==20&&bounded[4]==25,"The memory bound keeps the visible rows first");
    const auto targets=m::shortcutOriginalIconOrder(file,0,m::shortcutOriginalIconCapacity,[](const m::ShortcutRecord&r){return r.name!="App 1";});
    check(targets.size()==35&&targets[0]==0&&targets[1]==2,"A shortcut without a Windows target keeps the grid");
    check(m::shortcutOriginalIconOrder({},0,10,every).empty()&&m::shortcutOriginalIconOrder(file,std::nan(""),3,every)==std::vector<std::size_t>{0,1,2},"An empty list loads nothing; a nonfinite offset starts at the top");
}
}
int main(){try{run();icons();std::cout<<"Shortcut navigation: "<<checks<<" checks passed\n";return 0;}catch(const std::exception&e){std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}}
