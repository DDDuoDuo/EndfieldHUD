#include "native/watch_appearance.hpp"
#include "core/data/json.hpp"
#include <atomic>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <new>
#ifdef _WIN32
#include <windows.h>
#include <objbase.h>
#endif
using namespace endfield;using namespace native;using namespace core::source;using ehud::data::Json;
namespace {unsigned checks{};bool counting{};std::atomic<std::size_t>allocations{};void check(bool v,const char*s){++checks;if(!v)throw std::runtime_error(s);}template<class F>void rejects(F f){bool v{};try{f();}catch(const std::exception&){v=true;}check(v,"Unsupported runtime source content rejects explicitly");}
Json read(const std::filesystem::path&p){std::ifstream input(p,std::ios::binary);check(bool(input),"Open explicit original appearance asset");const std::string bytes((std::istreambuf_iterator<char>(input)),{});return Json::parse(bytes,1024*1024);}
void run(const std::filesystem::path&root){const auto asset=read(root/"manifest.json");WatchAppearanceTemplates templates(asset);check(templates.iconCount()==38,"Source asset contains module/shortcut primitives rather than state combinations");
 const auto icon=templates.localIcon("module:notes",false,"notes-slot");check(icon["id"].string()=="notes-slot"&&icon["children"].array().size()==2,"Original icon tree binds to a stable destination ID");check(templates.icon("module:activityMonitor",true).isObject(),"Authored Report glyph is retained");rejects([&]{templates.icon("module:activityMonitor",false);});rejects([&]{templates.icon("not-a-source-icon",false);});
 DesktopCaptionRequest request{"fileShelf","임시 파일 선반",{100,50},core::Language::korean,true,true,true,true};const auto plan=sourceDesktopCaption(request,[](auto,double s,bool,double,bool){return DesktopTextSize{s*3,s};});const auto caption=compileDesktopCaption(templates.captionTemplate(),plan,{124,56},"caption");check(caption["text"]["string"].string()==request.title&&caption["text"]["font"]["postScriptName"].string()==".AppleSystemUIFontBold"&&caption["bounds"].array()[2].number()==124,"Arbitrary runtime localized text preserves source style and expansion");check(caption["contentsAreFlipped"]==templates.captionTemplate()["contentsAreFlipped"]&&caption["text"]["alignment"]==templates.captionTemplate()["text"]["alignment"],"Original local caption rendering conventions remain intact");
#ifdef _WIN32
 LayerRasterizer raster;LayerRasterOptions options;options.assetRoot=root;options.pixelsPerPoint=1;options.paddingPoints=0;LayerScene layers(raster);
 Json sceneRoot=Json::Object{};sceneRoot["bounds"]=Json::Array{0,0,0,0};sceneRoot["children"]=Json::Array{caption,templates.localIcon("module:notes",false,"icon")};layers.load(sceneRoot,options);check(layers.report().unsupported.empty(),"Original source local primitives render without unsupported features");
 Node button;button.id="button";button.name="Synthetic";button.path="Root/RightBottomNode/Synthetic";button.children={"label","glyph"};Node label;label.id="label";label.name="Text";label.parent="button";label.rect=RectTransform{};label.rect->sizeDelta={100,50};Node glyph;glyph.id="glyph";glyph.name="Icon";glyph.parent="button";glyph.rect=RectTransform{};glyph.rect->sizeDelta={32,32};SceneDefinition scene("button",{button,label,glyph});
 const std::array bindings{NativeLabelBinding{"caption","label","button",NativeLabelKind::caption,false,true},NativeLabelBinding{"icon","glyph","button",NativeLabelKind::icon,false,true}};
 NativeWatchAppearance bridge(templates,scene,bindings,layers,raster,options);std::vector<WatchAppearanceEntry>entries{{1,"notes","Notes","module:notes",true},{2,"fileShelf","임시 파일 선반","module:fileShelf",true},{3,"custom","A new arbitrary title","shortcut:folder",false}};bridge.setEntries(entries);
 WatchContentCatalog::Actions actions{{"button",1}};std::array<NativeLabelPlacement,2>placements;placements[0].surfaceID="caption";placements[0].contentBounds={0,0,100,50};placements[0].visible=true;placements[1].surfaceID="icon";placements[1].contentBounds={0,0,32,32};placements[1].visible=true;WatchAppearance appearance;
 check(bridge.update(actions,appearance,placements),"Initial runtime source content compiles");const auto initial=raster.stats();const auto identity=layers.draws()[0].sourceID;allocations=0;counting=true;for(unsigned n=0;n<1000;++n)bridge.update(actions,appearance,placements);counting=false;check(allocations==0,"Unchanged tilt frames have no content heap allocations");check(raster.stats().textLayoutsCreated==initial.textLayoutsCreated&&raster.stats().rasterizations==initial.rasterizations,"Unchanged frames measure/rasterize nothing");
 appearance.selectedAction=1;check(bridge.update(actions,appearance,placements),"Selected caption updates native weight and source accent");check(raster.stats().rasterizations==initial.rasterizations+1,"Selection does not repaint unrelated original icon");const auto selected=raster.stats().rasterizations;appearance.accentSRGB={.2,.6,.9};check(bridge.update(actions,appearance,placements)&&raster.stats().rasterizations==selected+1,"Custom accent changes only selected caption pixels");
 appearance.language=core::Language::korean;entries[0].title="메모";bridge.setEntries(entries);const auto beforeLanguage=raster.stats().rasterizations;check(bridge.update(actions,appearance,placements)&&raster.stats().rasterizations==beforeLanguage+1,"Language event changes caption without rerasterizing its icon");check(layers.draws()[0].sourceID==identity,"Language/theme updates retain published surface identities");
 actions["button"]=2;check(bridge.expandsFileShelfCaption(2,appearance.language),"Current Shelf availability gets exact CJK expansion");placements[0].contentBounds={0,0,124,56};check(bridge.update(actions,appearance,placements),"Recycled slot accepts exact original icon and dynamically fitted localized caption");
 const auto beforeBad=raster.stats().rasterizations;entries[1].iconKey="missing";bridge.setEntries(entries);rejects([&]{bridge.update(actions,appearance,placements);});check(raster.stats().rasterizations==beforeBad,"Missing original icon is preflighted before any surface mutation");entries[1].iconKey="module:fileShelf";bridge.setEntries(entries);bridge.update(actions,appearance,placements);
 actions["button"]=3;placements[0].contentBounds={0,0,100,50};check(bridge.update(actions,appearance,placements),"Arbitrary custom application title uses measured ellipsis, not a finite preset");
 const auto retained=raster.stats().entries;for(unsigned n=0;n<40;++n){appearance.accentSRGB={double(n)/40,.5,.2};appearance.selectedAction=3;bridge.update(actions,appearance,placements);}check(raster.stats().entries==retained,"Content replacement retains bounded local surfaces");
#endif
}
}
void*operator new(std::size_t n){if(counting)++allocations;if(void*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}void operator delete(void*p)noexcept{std::free(p);}void*operator new[](std::size_t n){return ::operator new(n);}void operator delete[](void*p)noexcept{std::free(p);}void operator delete(void*p,std::size_t)noexcept{std::free(p);}void operator delete[](void*p,std::size_t)noexcept{std::free(p);}
int main(int argc,char**argv){
#ifdef _WIN32
 const auto hr=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
#endif
 int result{};try{
#ifdef _WIN32
 check(SUCCEEDED(hr),"Initialize owned appearance fixture apartment");
#endif
 check(argc==2,"Pass original watch-appearance asset root");run(std::filesystem::absolute(argv[1]));std::cout<<"Watch appearance: "<<checks<<" checks passed\n";}catch(const std::exception&e){counting=false;std::cerr<<"Watch appearance after "<<checks<<": "<<e.what()<<'\n';result=1;}
#ifdef _WIN32
 if(SUCCEEDED(hr))CoUninitialize();
#endif
 return result;
}
