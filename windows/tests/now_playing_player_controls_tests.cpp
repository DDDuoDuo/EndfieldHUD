// A player's own controls read through its accessibility tree (the Mac
// NowPlayingAccessibility read of NetEase Cloud Music, QQ Music and Kugou).
// Source: python3 windows/tools/now_playing_player_controls_reference.py
// (unchanged clock/isVolumeLabel). Synthetic trees only: no window, process
// or accessibility client is touched.
#include "modules/now_playing_player_controls.hpp"
#include "core/data/json.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <stdexcept>

namespace m=endfield::modules;using Json=ehud::data::Json;using R=m::NowPlayingControlRole;
namespace {
unsigned checks{};
void check(bool value,const std::string&what){++checks;if(!value)throw std::runtime_error(what);}

void oracle(const char*path){
    std::ifstream in(path,std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)),std::istreambuf_iterator<char>());
    const auto root=Json::parse(text);
    check(root["sourceCommit"].string()=="ca04f142185c7de40acd8523bdb563195d90a1d1","The oracle names the pinned source commit");
    check(root["clocks"].array().size()>=40&&root["volumes"].array().size()>=10,"The oracle covers the source label cases");
    for(const auto&row:root["clocks"].array()){
        const auto&value=row["value"].string();const auto seconds=m::nowPlayingClock(value);
        if(row["seconds"].isNull())check(!seconds,"No clock for source label: "+value);
        else check(seconds&&*seconds==row["seconds"].number(),"Source clock of label: "+value);
    }
    for(const auto&row:root["volumes"].array())
        check(m::nowPlayingVolumeLabel(row["value"].string())==row["volume"].boolean(),"Source volume label: "+row["value"].string());
}

// A synthetic accessibility tree: ids are indices, 0 is the root.
struct Tree final:m::NowPlayingAccessibleTree{
    struct Element{m::NowPlayingControlNode node;std::vector<std::uint64_t>children;bool gone{};};
    std::vector<Element>elements;unsigned nodeCalls{},childCalls{};
    std::uint64_t add(std::optional<std::uint64_t>parent,R role,std::string label={}){
        elements.push_back({{role,std::move(label),0},{}});const auto id=elements.size()-1;
        if(parent){elements[*parent].children.push_back(id);++elements[*parent].node.children;}
        return id;
    }
    std::optional<m::NowPlayingControlNode>node(std::uint64_t id)override{++nodeCalls;if(id>=elements.size()||elements[id].gone)return std::nullopt;return elements[id].node;}
    std::vector<std::uint64_t>children(std::uint64_t id)override{++childCalls;if(id>=elements.size()||elements[id].gone)return {};return elements[id].children;}
};
std::vector<std::uint64_t>ids(const std::vector<m::NowPlayingWalked>&walked){std::vector<std::uint64_t>out;for(const auto&w:walked)out.push_back(w.id);return out;}

void walk(){
    Tree t;const auto root=t.add({},R::other);const auto library=t.add(root,R::other,"Library");const auto bar=t.add(root,R::other,"Player");
    const auto a=t.add(library,R::text,"Song A");const auto b=t.add(bar,R::text,"1:23 /");const auto c=t.add(bar,R::text,"4:56");
    check(ids(m::nowPlayingWalk(t))==std::vector<std::uint64_t>{root,bar,c,b,library,a},"Depth first, the last child (the bottom player bar) first, as the source walk");
    check(ids(m::nowPlayingWalk(t,3))==std::vector<std::uint64_t>{root,bar,c},"At most `limit` nodes");
    unsigned calls{};
    check(m::nowPlayingWalk(t,700,[&]{return ++calls>2;}).size()==2,"A stop (deadline or cancellation) ends the walk");
    t.elements[bar].gone=true;
    check(ids(m::nowPlayingWalk(t))==std::vector<std::uint64_t>{root,library,a},"An element that went away is skipped");
    Tree deep;auto parent=deep.add({},R::other);for(int k=0;k<20;++k)parent=deep.add(parent,R::other);
    check(m::nowPlayingWalk(deep).size()==13,"Depth below 12 (the root and twelve levels)");
    Tree wide;const auto list=wide.add({},R::other);for(int k=0;k<300;++k)wide.add(list,R::text,std::to_string(k));
    const auto walked=m::nowPlayingWalk(wide);
    check(walked.size()==161&&walked[1].node.label=="299"&&walked.back().node.label=="140","At most the last 160 children of a node");
}
void classify(){
    // The source NetEase layout: "1:23 /" then "4:56", menu-like controls.
    Tree t;const auto root=t.add({},R::other);
    const auto heading=t.add(root,R::heading,"Song - Artist");const auto volume=t.add(root,R::slider,"Volume");
    const auto bar=t.add(root,R::other);
    const auto previous=t.add(bar,R::button,"\xe4\xb8\x8a\xe4\xb8\x80\xe9\xa6\x96");const auto toggle=t.add(bar,R::button,"\xe6\x9a\x82\xe5\x81\x9c");
    const auto next=t.add(bar,R::button,"Next");const auto duration=t.add(bar,R::text,"4:56");const auto position=t.add(bar,R::text," 1:23 / ");
    auto found=m::nowPlayingPlayerControls(m::nowPlayingWalk(t));
    check(found.position==position&&found.positionSeconds==83.&&found.duration==duration&&found.durationSeconds==296.,"Elapsed (the label ending in '/') and total (the other clock)");
    check(found.heading==heading&&found.headingText=="Song - Artist"&&found.volume==volume,"The heading with ' - ' and the volume slider");
    check(found.playPause==toggle&&found.playing==true&&found.previous==previous&&found.next==next&&found.found(),"Transport controls by the source names; a Pause control means playing");
    // Windows web players: one "1:23 / 4:56" label; Play means paused.
    Tree web;const auto doc=web.add({},R::document);const auto row=web.add(doc,R::other);
    web.add(row,R::button,"\xe6\x92\xad\xe6\x94\xbe");const auto both=web.add(row,R::text,"01:05 / 03:30");
    found=m::nowPlayingPlayerControls(m::nowPlayingWalk(web));
    check(found.position==both&&found.positionSeconds==65.&&found.durationSeconds==210.&&found.playing==false&&!found.emptyDocument,"A combined elapsed / total label; Play means paused");
    // A position beyond the duration is not this player's clock.
    Tree odd;const auto r2=odd.add({},R::other);odd.add(r2,R::text,"9:00");odd.add(r2,R::text,"10:00 /");
    found=m::nowPlayingPlayerControls(m::nowPlayingWalk(odd));
    check(!found.positionSeconds&&!found.position&&found.durationSeconds==540.,"A position later than the total is dropped");
    // Texts and headings never act as controls; other clickable elements may.
    Tree mixed;const auto r3=mixed.add({},R::other);mixed.add(r3,R::text,"Next");mixed.add(r3,R::heading,"Pause");const auto group=mixed.add(r3,R::other,"Previous");
    found=m::nowPlayingPlayerControls(m::nowPlayingWalk(mixed));
    check(!found.next&&!found.playPause&&found.previous==group,"Labels of texts and headings are not controls");
    // An empty web document (accessibility not yet built by the player).
    Tree empty;const auto r4=empty.add({},R::other);empty.add(r4,R::document);
    found=m::nowPlayingPlayerControls(m::nowPlayingWalk(empty));
    check(found.emptyDocument&&!found.found(),"An empty web document is reported (the page is not exposed yet)");
    check(m::nowPlayingControlNamed(m::NowPlayingCommandKind::playPause,"Play")&&m::nowPlayingControlNamed(m::NowPlayingCommandKind::next,"\xe4\xb8\x8b\xe4\xb8\x80\xe4\xb8\xaa")&&
          !m::nowPlayingControlNamed(m::NowPlayingCommandKind::seek,"Next")&&!m::nowPlayingControlNamed(m::NowPlayingCommandKind::previous,"previous"),
          "Control names compare exactly, as the source menu titles");
    check(m::nowPlayingControlPlaying("\xe6\x9a\xab\xe5\x81\x9c")==true&&m::nowPlayingControlPlaying("Play")==false&&!m::nowPlayingControlPlaying("Stop"),"Pause/Play labels state the playback");
}
}
int main(int argc,char**argv){
    try{
        if(argc!=2)throw std::runtime_error("usage: now_playing_player_controls_tests now-playing-player-controls-source.json");
        oracle(argv[1]);walk();classify();
        std::cout<<"Now Playing player controls: "<<checks<<" checks passed (synthetic trees only)\n";return 0;
    }catch(const std::exception&e){std::cerr<<"Now Playing player controls failed after "<<checks<<": "<<e.what()<<'\n';return 1;}
}
