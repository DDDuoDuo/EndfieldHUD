// A player's own controls read through its accessibility tree (the Mac
// NowPlayingAccessibility read of NetEase Cloud Music, QQ Music and Kugou).
// Source: python3 windows/tools/now_playing_player_controls_reference.py
// (unchanged clock/isVolumeLabel). NetEase Cloud Music 3.1's page shape:
// windows/tests/fixtures/now-playing-netease-tree.json (its player bar as read
// live on the test laptop, labels and cover address synthetic). Synthetic
// trees only: no window, process or accessibility client is touched.
#include "modules/now_playing_player_controls.hpp"
#include "core/data/json.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
#include <algorithm>
#include <cmath>
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
    web.add(row,R::button,"Previous");web.add(row,R::button,"\xe6\x92\xad\xe6\x94\xbe");web.add(row,R::button,"Next");const auto both=web.add(row,R::text,"01:05 / 03:30");
    found=m::nowPlayingPlayerControls(m::nowPlayingWalk(web));
    check(found.position==both&&found.positionSeconds==65.&&found.durationSeconds==210.&&found.playing==false&&!found.emptyDocument,"A combined elapsed / total label; Play means paused");
    // A position beyond the duration is not this player's clock.
    Tree odd;const auto r2=odd.add({},R::other);odd.add(r2,R::text,"9:00");odd.add(r2,R::text,"10:00 /");
    found=m::nowPlayingPlayerControls(m::nowPlayingWalk(odd));
    check(!found.positionSeconds&&!found.position&&found.durationSeconds==540.,"A position later than the total is dropped");
    // Texts and headings never act as controls; other clickable elements may.
    Tree mixed;const auto r3=mixed.add({},R::other);const auto row3=mixed.add(r3,R::other);
    const auto realNext=mixed.add(row3,R::button,"Next");const auto realPause=mixed.add(row3,R::button,"Pause");
    mixed.add(row3,R::text,"Next");mixed.add(row3,R::heading,"Pause");const auto group=mixed.add(row3,R::other,"Previous");
    found=m::nowPlayingPlayerControls(m::nowPlayingWalk(mixed));
    check(found.next==realNext&&found.playPause==realPause&&found.previous==group,"Labels of texts and headings are not controls");
    // Without previous, play/pause and next together no transport counts (a
    // page's lone "Play" button is never the player's).
    Tree lone;const auto r5=lone.add({},R::other);lone.add(r5,R::button,"Play");lone.add(r5,R::text,"4:00");
    found=m::nowPlayingPlayerControls(m::nowPlayingWalk(lone));
    check(!found.playPause&&!found.playing&&found.durationSeconds==240.,"A lone play button is no player bar (its clock still follows the source rule)");
    // An empty web document (accessibility not yet built by the player).
    Tree empty;const auto r4=empty.add({},R::other);empty.add(r4,R::document);
    found=m::nowPlayingPlayerControls(m::nowPlayingWalk(empty));
    check(found.emptyDocument&&!found.found(),"An empty web document is reported (the page is not exposed yet)");
    check(m::nowPlayingControlNamed(m::NowPlayingCommandKind::playPause,"Play")&&m::nowPlayingControlNamed(m::NowPlayingCommandKind::next,"\xe4\xb8\x8b\xe4\xb8\x80\xe4\xb8\xaa")&&
          !m::nowPlayingControlNamed(m::NowPlayingCommandKind::seek,"Next")&&!m::nowPlayingControlNamed(m::NowPlayingCommandKind::previous,"Next")&&
          !m::nowPlayingControlNamed(m::NowPlayingCommandKind::previous,"Previously")&&!m::nowPlayingControlNamed(m::NowPlayingCommandKind::playPause,"\xe6\x92\xad\xe6\x94\xbe\xe5\x88\x97\xe8\xa1\xa8"),
          "Control names are the source menu titles (the playlist button is no play control)");
    // NetEase 3.1 (live): lowercase element names, tooltips with shortcuts.
    check(m::nowPlayingControlNamed(m::NowPlayingCommandKind::previous,"previous")&&m::nowPlayingControlNamed(m::NowPlayingCommandKind::playPause,"pause")&&
          m::nowPlayingControlNamed(m::NowPlayingCommandKind::previous,"\xe4\xb8\x8a\xe4\xb8\x80\xe9\xa6\x96\xef\xbc\x88""Ctrl + Left\xef\xbc\x89")&&
          m::nowPlayingControlNamed(m::NowPlayingCommandKind::next,"Next (Ctrl+Right)")&&!m::nowPlayingControlNamed(m::NowPlayingCommandKind::next,"(Ctrl+Right)"),
          "ASCII names in any case; a trailing shortcut in parentheses is ignored");
    check(m::nowPlayingControlText("\xe6\x92\xad\xe6\x94\xbe\xef\xbc\x88""Ctrl + P\xef\xbc\x89")=="\xe6\x92\xad\xe6\x94\xbe"&&m::nowPlayingControlText(" Next ")=="Next"&&
          m::nowPlayingControlText("(x)")=="(x)","The shortcut hint is cut only when a name precedes it");
    check(m::nowPlayingControlPlaying("\xe6\x9a\xab\xe5\x81\x9c")==true&&m::nowPlayingControlPlaying("Play")==false&&!m::nowPlayingControlPlaying("Stop")&&
          m::nowPlayingControlPlaying("pause")==true&&m::nowPlayingControlPlaying("play")==false&&m::nowPlayingControlPlaying("\xe6\x9a\x82\xe5\x81\x9c\xef\xbc\x88""Ctrl + P\xef\xbc\x89")==true,
          "Pause/Play labels state the playback (NetEase's lowercase names and tooltips too)");
}
}
// The NetEase Cloud Music 3.1 page: a tree built from the fixture, labelled
// as the MSAA reader labels it (name, else description; a named element
// keeps its description) and answering details from its IAccessible2
// object attributes, through the production attribute parser.
struct Page final:m::NowPlayingAccessibleTree{
    struct Element{m::NowPlayingControlNode node;std::vector<std::uint64_t>children;std::optional<std::uint64_t>parent;m::NowPlayingControlBounds bounds;std::string attributes;bool gone{};};
    std::vector<Element>elements;std::vector<std::uint64_t>asked;bool attributes{true};
    std::optional<m::NowPlayingControlNode>node(std::uint64_t id)override{if(id>=elements.size()||elements[id].gone)return std::nullopt;return elements[id].node;}
    std::vector<std::uint64_t>children(std::uint64_t id)override{if(id>=elements.size()||elements[id].gone)return {};return elements[id].children;}
    m::NowPlayingControlDetails details(std::uint64_t id)override{
        asked.push_back(id);m::NowPlayingControlDetails out;if(id>=elements.size()||elements[id].gone)return out;
        out.bounds=elements[id].bounds;if(!attributes)return out;
        for(const auto&[name,value]:m::nowPlayingAccessibleAttributes(elements[id].attributes)){
            if(name=="src")out.source=value;
            if(name!="class")continue;
            std::size_t at=0;while(at<value.size()){const auto space=value.find(' ',at);const auto token=value.substr(at,space==std::string::npos?std::string::npos:space-at);if(!token.empty())out.classes.push_back(token);if(space==std::string::npos)break;at=space+1;}
        }
        return out;
    }
    bool within(std::uint64_t id,std::uint64_t ancestor)const{for(std::optional<std::uint64_t>at=id;at;at=elements[*at].parent)if(*at==ancestor)return true;return false;}
};
Page page(const Json&root){
    Page out;
    for(const auto&n:root["nodes"].array()){
        Page::Element e;const auto&role=n["role"].string();
        e.node.role=role=="document"?R::document:role=="button"?R::button:role=="image"?R::image:role=="text"?R::text:R::other;
        const auto&name=n["name"].string();const auto&description=n["description"].string();
        if(name.empty())e.node.label=description;else{e.node.label=name;e.node.description=description;}
        const auto&b=n["bounds"].array();e.bounds={b[0].number(),b[1].number(),b[2].number(),b[3].number()};
        e.attributes=n["attributes"].string();
        if(!n["parent"].isNull()){const auto parent=static_cast<std::uint64_t>(n["parent"].number());e.parent=parent;out.elements[parent].children.push_back(out.elements.size());++out.elements[parent].node.children;}
        check(static_cast<std::size_t>(n["id"].number())==out.elements.size(),"Fixture ids are in document order");
        out.elements.push_back(std::move(e));
    }
    return out;
}
// The reader's walk: the scan sees each node and ends the walk once the bar is complete.
std::pair<m::NowPlayingPlayerControls,std::vector<m::NowPlayingWalked>>read(Page&tree){
    m::NowPlayingControlScan scan(&tree);
    auto walked=m::nowPlayingWalk(tree,700,[&]{return scan.complete();},[&](const m::NowPlayingWalked&w){scan.add(w);});
    return {scan.result(),std::move(walked)};
}
void netease(const char*path){
    std::ifstream in(path,std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)),std::istreambuf_iterator<char>());
    const auto root=Json::parse(text);
    const auto&e=root["expect"];const auto id=[&](const char*key){return static_cast<std::uint64_t>(e[key].number());};
    const auto title=root["windowTitle"].string();
    auto tree=page(root);
    check(tree.elements.size()>=100&&tree.elements[0].node.role==R::document,"The fixture is the page document of the player window");
    auto[found,walked]=read(tree);
    check(found.playPause==id("playPause")&&found.previous==id("previous")&&found.next==id("next"),
          "Its bar's buttons: \"pre\" (by its icon's tooltip), \"play\" and \"next\" (any case)");
    check(found.playing==false&&!found.playLabel,"The paused player's button is named play; it states the play state itself");
    check(found.progress==id("progress")&&found.played==id("played")&&found.fraction&&std::abs(*found.fraction-e["fraction"].number())<1e-12,
          "The progress bar's part with class track is the played fraction (80 of 2561 pixels)");
    check(found.artwork==id("artwork")&&found.artworkURL==e["artworkURL"].string(),"The bar's cover comes from the catalog address its image source embeds");
    check(!found.positionSeconds&&!found.durationSeconds&&!found.position&&!found.duration,"The playlist's 03:33 lengths are never the player's clock");
    check(m::nowPlayingTitleShown(title,found.textLabels)&&!m::nowPlayingTitleShown("Other Song - Artist A",found.textLabels)&&
          !m::nowPlayingTitleShown("Artist A",found.textLabels),"The bar's texts confirm the window title's track, never another song of the same artist");
    check(std::none_of(walked.begin(),walked.end(),[&](const m::NowPlayingWalked&w){return tree.within(w.id,id("main"));})&&walked.size()<90,
          "The walk ends with the bar: the playlist page is never read");
    const auto asked=tree.asked.size();
    check(asked<=6,"Details are asked only for the progress bar, its parts and the cover");
    check(found.found()&&!found.emptyDocument,"A reading");
    // A song with many artists: the bar's title still confirms the track.
    {
        auto many=tree;
        const auto artist=static_cast<std::uint64_t>(std::find_if(many.elements.begin(),many.elements.end(),[](const Page::Element&x){return x.node.label=="Artist A";})-many.elements.begin());
        const auto side=*many.elements[*many.elements[artist].parent].parent;
        for(int k=0;k<20;++k){
            Page::Element extra;extra.node.role=R::text;extra.node.label="Featured Artist "+std::to_string(k);extra.parent=side;
            many.elements[side].children.push_back(many.elements.size());++many.elements[side].node.children;many.elements.push_back(std::move(extra));
        }
        const auto crowded=read(many).first;
        check(crowded.textLabels.size()>20&&m::nowPlayingTitleShown(title,crowded.textLabels),"Twenty more artist texts in the bar still leave its title among the texts read");
    }
    // Playing: the same button is named pause.
    tree.elements[id("playPause")].node.label="pause";
    check(read(tree).first.playing==true,"The pause button means playing");
    // Without IAccessible2 attributes the transport still works.
    tree.attributes=false;
    auto plain=read(tree).first;
    check(plain.playPause==id("playPause")&&!plain.fraction&&!plain.artworkURL,"Without object attributes: transport only, no fraction or cover");
    tree.attributes=true;
    // The bar hidden (NetEase's full-screen vinyl page): nothing on the page counts.
    tree.elements[id("bar")].gone=true;
    auto[hidden,all]=read(tree);
    check(!hidden.playPause&&!hidden.previous&&!hidden.next&&!hidden.fraction&&!hidden.durationSeconds&&!hidden.playing&&!hidden.found(),
          "Without the bar the playlist's play buttons and lengths are never taken for the player's");
    check(std::any_of(all.begin(),all.end(),[&](const m::NowPlayingWalked&w){return tree.within(w.id,id("main"));}),"(the playlist was walked)");
}
void attributes(){
    const auto parsed=m::nowPlayingAccessibleAttributes("display:block;tag:img;src:orpheus\\://cache/?http\\://p3.music.126.net/A\\=\\=/1.jpg?x\\=1;class:a  b;");
    check(parsed.size()==4&&parsed[2].first=="src"&&parsed[2].second=="orpheus://cache/?http://p3.music.126.net/A==/1.jpg?x=1"&&parsed[3].second=="a  b",
          "IAccessible2 object attributes: name:value pairs with backslash escapes");
    check(m::nowPlayingAccessibleAttributes("").empty()&&m::nowPlayingAccessibleAttributes("novalue;").size()==1,"Empty and value-less attributes");
    check(m::nowPlayingPlayerArtworkURL("orpheus://cache/?http://p3.music.126.net/A==/1.jpg?imageView=&thumbnail=60y60&type=webp")==
              "https://p3.music.126.net/A==/1.jpg?param=600y600"&&
          m::nowPlayingPlayerArtworkURL("http://p1.music.126.net/B==/2.jpg?param=60y60")=="https://p1.music.126.net/B==/2.jpg?param=600y600",
          "A player's image source names the catalog cover (embedded in its cache address, or direct)");
    check(!m::nowPlayingPlayerArtworkURL("orpheus://cache/?https://evil.example/x.jpg")&&!m::nowPlayingPlayerArtworkURL("file:///C:/x.jpg")&&
          !m::nowPlayingPlayerArtworkURL("data:image/png;base64,AAAA")&&!m::nowPlayingPlayerArtworkURL(""),"Any other image source is no cover");
    check(m::nowPlayingProgressLabel("\xe6\x92\xad\xe6\x94\xbe\xe8\xbf\x9b\xe5\xba\xa6\xe8\xb0\x83\xe8\x8a\x82")&&m::nowPlayingProgressLabel("Playback Progress")&&
          !m::nowPlayingProgressLabel("Volume")&&!m::nowPlayingProgressLabel(""),"Progress bar labels");
}
int main(int argc,char**argv){
    try{
        if(argc!=3)throw std::runtime_error("usage: now_playing_player_controls_tests now-playing-player-controls-source.json now-playing-netease-tree.json");
        oracle(argv[1]);walk();classify();netease(argv[2]);attributes();
        std::cout<<"Now Playing player controls: "<<checks<<" checks passed (synthetic trees only)\n";return 0;
    }catch(const std::exception&e){std::cerr<<"Now Playing player controls failed after "<<checks<<": "<<e.what()<<'\n';return 1;}
}
