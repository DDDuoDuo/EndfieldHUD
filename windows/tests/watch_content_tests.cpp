#include "native/watch_content.hpp"
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <new>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#else
// Portable run verifies only source metadata compilation. Any accidental call
// into native raster presentation fails; no substitute renderer is exercised.
namespace endfield::native {
std::optional<std::size_t>LayerScene::surfaceIndex(std::string_view)const noexcept{std::terminate();}
bool LayerScene::updateLocalContent(std::string_view,std::uint64_t,const ehud::data::Json&,const LayerRasterOptions&){throw std::runtime_error("Native raster work in portable metadata test");}
}
#endif
namespace {std::atomic<bool>counting{};std::atomic<std::size_t>allocations{};}
void*operator new(std::size_t n){if(counting)++allocations;if(auto*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void*operator new[](std::size_t n){return ::operator new(n);}void operator delete(void*p)noexcept{std::free(p);}void operator delete[](void*p)noexcept{std::free(p);}
#if defined(__cpp_sized_deallocation)
void operator delete(void*p,std::size_t)noexcept{std::free(p);}void operator delete[](void*p,std::size_t)noexcept{std::free(p);}
#endif
using namespace endfield::core::source;
using namespace endfield::native;
namespace {
unsigned checks{};
void check(bool value,const char*why){++checks;if(!value)throw std::runtime_error(why);}
template<class F>void rejects(F&&f,const char*why){bool rejected{};try{f();}catch(const std::invalid_argument&){rejected=true;}check(rejected,why);}
Json read(const std::filesystem::path&path){std::ifstream file(path,std::ios::binary);check(bool(file),"Explicit source packet fixture opens");std::string data((std::istreambuf_iterator<char>(file)),{});return Json::parse(data,64*1024*1024);}
void scrubIDs(Json&node){node.erase("id");auto children=node["children"].array();for(auto&child:children)scrubIDs(child);node["children"]=std::move(children);if(!node["mask"].isNull())scrubIDs(node["mask"]);}
Json artwork(Json value){scrubIDs(value);for(const auto*key:{"transform","position","anchorPoint","anchorPointZ","zPosition","frame"})value.erase(key);return value;}
WatchContentCatalog::Actions actionsAt(const WatchContentCatalog&catalog,const DesktopNavigationLayout&layout,double position){auto result=catalog.navigation().fixedActions;for(const auto&[id,index]:layout.sample(position).assignments)result[id]=catalog.navigation().rightActions.at(index);return result;}
void run(const std::filesystem::path&root){
    const auto animation=read(root/"animation.json"),top=read(root/"frame/desktop-shell-1280x800-top.json"),bottom=read(root/"frame/desktop-shell-1280x800-bottom.json");
    const auto scene=SceneDefinition::fromJson(animation["scene"]);const auto document=MountedLayoutDocument::fromJson(animation["mountedDocument"]);
    const auto bindings=nativeLabelBindingsFromExport(scene,animation["mountedDocument"]["buttons"],top["nativeLayers"],top["nativeProfileBindings"]);
    const std::array snapshots{WatchContentSnapshot{&top,1},WatchContentSnapshot{&bottom,0}};
    const WatchContentCatalog catalog(scene,document,bindings,snapshots);
    check(catalog.entries().size()==24&&catalog.surfaceCount()==48&&catalog.variantCount()==48,"Neutral original source covers 24 actions and 48 local caption/icon planes");
    check(catalog.navigation().fixedActions.size()==6&&catalog.navigation().rightActions.size()==18&&catalog.navigation().managedButtons.size()==24,"Exact Mac prefix, right rows and supplemental button bindings");
    check(catalog.actionForTarget("notes")==7&&catalog.initialActions().at(document.buttons[4].nodeID)==7,"Navigation sourceName is never mistaken for the recycled physical button");
    check(!catalog.actionForTarget("not-exported"),"Unknown targets do not fabricate actions");
    DesktopNavigationLayout layout(scene,document,18);check(actionsAt(catalog,layout,0)==catalog.initialActions(),"The exact neutral source list has the same physical bindings at top and bottom");
    for(const auto&binding:bindings)if(!binding.profileViewportClip){
        const auto action=catalog.initialActions().at(binding.buttonID);const auto&content=catalog.localContent(binding.surfaceID,action);
        const auto name=std::string(binding.kind==NativeLabelKind::caption?"desktop.watch.label.":"desktop.watch.icon.")+binding.sourceNodeID;
        const auto&containers=top["nativeLayers"]["children"].array();const auto found=std::find_if(containers.begin(),containers.end(),[&](const auto&node){return node["name"].string()==name;});
        check(found!=containers.end(),"Exact source content node exists");check(artwork(content)==artwork((*found)["children"].array().front()),"Complete descendant artwork, fonts, colors, rasters and bounds match original source");
        check(content["id"].string()==binding.surfaceID,"Destination keeps stable existing surface identity");
        rejects([&]{(void)catalog.localContent(binding.surfaceID,9999);},"Missing source action variant is explicit");
    }
    auto conflicting=bottom;auto containers=conflicting["nativeLayers"]["children"].array();auto label=std::find_if(containers.begin(),containers.end(),[](const auto&n){return n["name"].string().starts_with("desktop.watch.label.");});auto children=(*label)["children"].array();children[0]["text"]["string"]="Invalid silently changed source state";(*label)["children"]=std::move(children);conflicting["nativeLayers"]["children"]=std::move(containers);
    const std::array conflicts{WatchContentSnapshot{&top,1},WatchContentSnapshot{&conflicting,0}};
    rejects([&]{WatchContentCatalog invalid(scene,document,bindings,conflicts);},"One catalog cannot silently blend different appearances for the same action and slot");

    // Synthetic additional entries exercise recycling machinery only. Their
    // labels are deliberately fake test data, never exported/shipped as artwork.
    auto extendedTop=top,extendedBottom=top;auto entries=top["nativeNavigation"].array();entries.push_back(Json::Object{{"target","fixture-a"},{"title","Fixture A"}});entries.push_back(Json::Object{{"target","fixture-b"},{"title","Fixture B"}});extendedTop["nativeNavigation"]=entries;extendedBottom["nativeNavigation"]=entries;
    DesktopNavigationLayout extendedLayout(scene,document,20);const auto lower=extendedLayout.sample(0);
    auto extendedContainers=extendedBottom["nativeLayers"]["children"].array();
    for(const auto&binding:bindings)if(!binding.profileViewportClip){const auto at=lower.assignments.find(binding.buttonID);if(at==lower.assignments.end()||at->second<18)continue;
        if(binding.kind==NativeLabelKind::caption){const auto name="desktop.watch.label."+binding.sourceNodeID;auto container=std::find_if(extendedContainers.begin(),extendedContainers.end(),[&](const auto&n){return n["name"].string()==name;});auto content=(*container)["children"].array();content[0]["text"]["string"]=at->second==18?"Fixture A":"Fixture B";(*container)["children"]=std::move(content);}}
    extendedBottom["nativeLayers"]["children"]=std::move(extendedContainers);const std::array extendedSnapshots{WatchContentSnapshot{&extendedTop,1},WatchContentSnapshot{&extendedBottom,0}};
    const WatchContentCatalog extended(scene,document,bindings,extendedSnapshots);check(extended.variantCount()==52,"Two additional logical entries reuse four existing surfaces with observed variants only");
    const auto bottomActions=actionsAt(extended,extendedLayout,0);
#ifdef _WIN32
    LayerRasterizer raster;LayerScene layers(raster);LayerRasterOptions options;options.assetRoot=root;layers.load(top["nativeLayers"],options);
    const auto epoch=layers.contentRevision();NativeWatchContent bridge(extended,layers,options);const auto initial=raster.stats();
    check(!bridge.update(extended.initialActions()),"Bootstrap snapshot needs no duplicate raster work");
    check(bridge.update(bottomActions),"Recycled row accepts exact captured replacement content");check(raster.stats().rasterizations==initial.rasterizations+4,"A recycled row updates only its two captions and two icons");
    const auto recycled=raster.stats();auto invalid=bottomActions;invalid.begin()->second=9999;
    rejects([&]{bridge.update(invalid);},"Unknown replacement fails before any local surface mutation");check(raster.stats().rasterizations==recycled.rasterizations,"Missing variant cannot partially replace a row");
    allocations=0;counting=true;for(unsigned i=0;i<1000;++i)bridge.update(bottomActions);counting=false;
    check(allocations==0,"Unchanged caption/icon bindings allocate no memory");check(raster.stats().rasterizations==recycled.rasterizations&&raster.stats().textLayoutsCreated==recycled.textLayoutsCreated,"Unchanged bindings create no raster or text layouts");
    auto absent=bottomActions;absent.erase(document.buttons[4].nodeID);check(!bridge.update(absent),"Unbound row visibility belongs to NativeLabelPlan without erasing retained content");check(!bridge.update(bottomActions),"Rebinding unchanged pixels needs no raster upload");
    for(unsigned i=0;i<100;++i){bridge.update(extended.initialActions());bridge.update(bottomActions);}
    check(raster.stats().entries==initial.entries&&raster.stats().resourceBytes==recycled.resourceBytes,"Repeated recycling retains a bounded latest raster per surface");check(layers.contentRevision()==epoch,"Content recycling preserves structural surface indices");
    layers.load(top["nativeLayers"],options);rejects([&]{bridge.update(bottomActions);},"A structural reload cannot reuse stale content state");
#else
    (void)bottomActions;
#endif
}
}
#ifdef _WIN32
int wmain(int argc,wchar_t**argv){const auto initialized=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);try{check(SUCCEEDED(initialized)&&argc==2,"Pass the explicit original source packet root");run(argv[1]);CoUninitialize();std::cout<<"PASS "<<checks<<" original-source watch content/native raster checks\n";return 0;}catch(const std::exception&e){counting=false;if(SUCCEEDED(initialized))CoUninitialize();std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}}
#else
int main(int argc,char**argv){try{check(argc==2,"Pass the explicit original source packet root");run(argv[1]);std::cout<<"PASS "<<checks<<" original-source watch content metadata checks (native raster not run)\n";return 0;}catch(const std::exception&e){counting=false;std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}}
#endif
