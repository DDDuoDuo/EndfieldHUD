#include "modules/module_strings.hpp"
#include <cstdio>
#include <iostream>
#include <stdexcept>
namespace c=endfield::core;namespace m=endfield::modules;
namespace {
unsigned checks{};void check(bool v,const char*why){++checks;if(!v)throw std::runtime_error(why);}template<class F>void rejects(F f){bool failed{};try{f();}catch(const std::exception&){failed=true;}check(failed,"Malformed preference text rejects");}
std::string id(unsigned value){char s[64];std::snprintf(s,sizeof(s),"00000000-0000-0000-0000-%012u",value);return s;}
void catalog(){for(auto language:{c::Language::system,c::Language::english,c::Language::simplifiedChinese,c::Language::traditionalChinese,c::Language::japanese,c::Language::korean}){
    const auto shelf=m::fileShelfStrings(language);const auto style=m::shelfPresentationStyle(language);
    check(style.heading==c::localized("Temporary File Shelf","文件暂存架",language),"Original Shelf module heading uses catalog");
    const std::array<std::string_view,1>count{"{count}"};check(shelf.selectedStatus==c::localized("{0} selected · Drag to copy","已选 {0} 项 · 拖出以复制",language,count),"Shelf keeps original argument placement");
 }
 check(m::fileShelfStrings(c::Language::simplifiedChinese)==m::FileShelfStrings::simplifiedChinese(),"Original Chinese Shelf state fixture remains exact");
}
void shelf(){unsigned reads{},mutations{};std::vector<m::FileShelfItem>rows;for(unsigned k=1;k<=20;++k)rows.push_back({id(k),"Original filename "+std::to_string(k),"C:\\synthetic\\file.png","Opaque platform type"});m::FileShelfState state(rows,{[&]{++reads;return rows;},[&]{++reads;},[&](auto){++mutations;return 0u;},[&](auto){++mutations;},[&]{++mutations;}},{},m::fileShelfStrings(c::Language::english));
 state.activate();state.selectItem(id(6),{});state.selectItem(id(9),{true,false});state.scrollBy(35.5);state.perform("shelf:clear");state.setDropTarget(true);state.takeEvents();const auto selected=state.selectedIDs();const auto offset=state.scrollOffset();const auto beforeReads=reads;
 for(auto lang:{c::Language::japanese,c::Language::korean,c::Language::traditionalChinese,c::Language::english}){check(state.setStrings(m::fileShelfStrings(lang)),"Shelf applies changed strings");check(state.selectedIDs()==selected&&state.scrollOffset()==offset&&state.confirmingClear()&&state.dropTarget()&&state.active(),"Shelf preference keeps selection/scroll/confirmation/drop");check(reads==beforeReads&&!mutations&&state.takeEvents().empty(),"Shelf language never reads files, persists, or starts animation");check(state.items()[0].name=="Original filename 1"&&state.items()[0].typeDescription=="Opaque platform type","Shelf metadata is opaque");const auto revision=state.revision();check(!state.setStrings(m::fileShelfStrings(lang))&&state.revision()==revision,"Unchanged Shelf preferences are a no-op");}
 auto invalid=state.strings();invalid.itemsStatus="no count";const auto revision=state.revision();rejects([&]{state.setStrings(invalid);});check(state.revision()==revision,"Invalid Shelf strings do not publish");
 m::ShelfPresentation presentation;auto style=m::shelfPresentationStyle(c::Language::english);style.errorColor=m::ShelfColor{1,0,0,1};presentation.update(state,style);const auto old=presentation.stats();style.dark=false;style.accent={.4,.2,.8,1};style.lightDropColor=m::ShelfColor{.1,.05,.2,1};presentation.update(state,style);check(presentation.stats().chromeBuilds>old.chromeBuilds&&state.scrollOffset()==offset&&state.selectedIDs()==selected,"Shelf theme rebuilds bounded artwork without resetting state");check(!presentation.update(state,style),"Warm Shelf theme update is idle");
}
}
int main(){try{catalog();shelf();std::cout<<"PASS "<<checks<<" retained module preference checks\n";}catch(const std::exception&e){std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}}
