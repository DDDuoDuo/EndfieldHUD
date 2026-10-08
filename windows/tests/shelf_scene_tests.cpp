#include "native/shelf_scene.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#endif
namespace {std::atomic<bool>counting{};std::atomic<std::size_t>allocations{};unsigned checks{};}
void*operator new(std::size_t n){if(counting)++allocations;if(void*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}void*operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void*p)noexcept{std::free(p);}void operator delete[](void*p)noexcept{std::free(p);}
#if defined(__cpp_sized_deallocation)
void operator delete(void*p,std::size_t)noexcept{std::free(p);}void operator delete[](void*p,std::size_t)noexcept{std::free(p);}
#endif
namespace n=endfield::native;namespace m=endfield::modules;namespace c=endfield::core;using Json=ehud::data::Json;
namespace {
void check(bool b,const char*s){++checks;if(!b)throw std::runtime_error(s);}
void checkNear(double a,double b,const char*s){check(std::isfinite(a)&&std::abs(a-b)<1e-7,s);}
template<class F>void rejects(F f,const char*s){bool rejected{};try{f();}catch(const std::exception&){rejected=true;}check(rejected,s);}
std::vector<m::FileShelfItem>items(std::size_t count){std::vector<m::FileShelfItem>out;for(std::size_t k=0;k<count;++k)out.push_back({"synthetic-"+std::to_string(k),"Synthetic file "+std::to_string(k),"/unused/synthetic.txt","Text",{},false,{}});return out;}
std::vector<n::NativeShelfImage>images(const m::ShelfPresentation&p){std::vector<n::NativeShelfImage>out;const auto add=[&](const auto&deps){for(const auto&d:deps)out.push_back({d,Json::Object{{"asset","owned-white.png"},{"sha256","8de1139cc6f6b0f2202d1c911d9232bed5fa38976272bd4f0812bba44a4ff2fc"}}});};add(p.chromeImages());for(const auto&card:p.cards())add(card.images);return out;}
const Json*find(const Json&node,std::string_view id){if(node["id"].isString()&&node["id"].string()==id)return &node;if(node["children"].isArray())for(const auto&child:node["children"].array())if(const auto*result=find(child,id))return result;return nullptr;}
std::size_t surface(const n::ShelfScenePart&p,std::string_view id){for(std::size_t k=0;k<p.surfaces.size();++k)if(find(p.layers["children"].array()[k],id))return k;throw std::runtime_error("Missing compiled source Shelf surface");}
const Json&leaf(const n::ShelfScenePart&p,std::string_view id){const auto*node=find(p.layers,id);if(!node)throw std::runtime_error("Missing source leaf");return *node;}
void portable(){
    m::ShelfPresentation source;rejects([&]{n::prepareShelfScene(source);},"Uninitialized shelf rejects");m::FileShelfState state(items(10000));m::ShelfPresentationStyle style;source.update(state,style);
    rejects([&]{n::prepareShelfScene(source);},"File icons and Depot artwork are explicit, never blank replacements");auto supplied=images(source);const auto original=source.chrome();const auto plan=n::prepareShelfScene(source,supplied);
    check(source.chrome()==original&&plan.cards.size()==8,"Preparation preserves descriptor and only builds eight visible cards");
    check(plan.collection.surfaces.empty()&&find(plan.foreground.layers,"shelf.drop")!=nullptr&&plan.scrollbar.surfaces.size()==1,"Collection, numeric scrollbar and upper chrome remain separate paint groups");
    check(find(plan.foreground.layers["children"].array().back(),"shelf:clear/title")!=nullptr,"Toolbar captions follow source sibling plate order");
    for(const auto&p:plan.cards){const auto id="shelf.card."+p.itemID;const auto plate=surface(p,id+"/plate"),icon=surface(p,id+"/icon"),separator=surface(p,id+"/separator");
        check(plate==0&&icon==separator&&separator<surface(p,id+"/eye"),"Original plate, controls, icons, text and final glyph paint order");
        for(const auto*s:{"/plate","/separator","/eye","/folder","/cross"}){const auto&v=leaf(p,id+s);check(v["bounds"]==Json(Json::Array{0,0,0,0})&&!v["shape"]["path"].array().empty(),"Zero-bounds source shape ink is not cropped or omitted");}
        check(p.surfaces.size()==11,"Consecutive static source artwork leaves headroom for two live card generations");
        check(leaf(p,id+"/icon")["contents"]==supplied[1].contents,"Explicit pinned native icon metadata survives flattening");
        for(const auto&s:p.surfaces){check(s.local.values[14]==0,"Selection depth is never baked into raster geometry");if(s.feedback!=n::ShelfSceneSurface::none)check(leaf(p,s.id)["opacity"].number()==1,"Invisible highlight art remains resident for numeric feedback");}
    }
    for(const auto&s:plan.foreground.surfaces)if(s.id.starts_with("shelf:add")||s.id.starts_with("shelf:clear"))check(s.toolbar,"Caption siblings and highlight descendants follow the whole toolbar reveal");
    auto bad=supplied;bad.back().dependency.unavailable=true;rejects([&]{n::prepareShelfScene(source,bad);},"Native icon availability is part of exact dependency");bad=supplied;bad[0].dependency.sourceInTint=false;rejects([&]{n::prepareShelfScene(source,bad);},"Untinted Depot cannot masquerade as prepared source-in artwork");bad=supplied;bad[0].contents["sha256"]="bad";rejects([&]{n::prepareShelfScene(source,bad);},"Unpinned images reject");bad=supplied;bad.back()=bad.front();rejects([&]{n::prepareShelfScene(source,bad);},"Duplicate image dependencies reject");
    m::FileShelfState empty({});style.depotIconAvailable=false;source.update(empty,style);const auto fallback=n::prepareShelfScene(source);check(fallback.cards.empty()&&fallback.scrollbar.surfaces.empty()&&surface(fallback.collection,"shelf.empty/folder")==0,"Actual source folder/+ fallback compiles without OS icon lookup");
    checkNear(leaf(fallback.collection,"shelf.empty/folder")["position"].array()[0].number(),165,"Empty fallback shape keeps source x");checkNear(leaf(fallback.collection,"shelf.empty/folder")["position"].array()[1].number(),88,"Empty fallback shape keeps source y");
}
#ifdef _WIN32
struct Fixture {
    std::filesystem::path assets;HWND window{};ATOM atom{};
    Fixture(){WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"EndfieldShelfSceneOwnedFixture";atom=RegisterClassW(&wc);check(atom!=0,"Register owned hidden Shelf fixture");window=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOREDIRECTIONBITMAP,wc.lpszClassName,L"Hidden synthetic Shelf",WS_POPUP,0,0,512,384,nullptr,nullptr,wc.hInstance,nullptr);check(window!=nullptr,"Create owned hidden Shelf fixture");
        assets=std::filesystem::temp_directory_path()/("endfield-shelf-scene-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64()));check(std::filesystem::create_directory(assets),"Create new owned fixture asset directory");
        constexpr unsigned char png[]{137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,2,0,0,0,2,8,6,0,0,0,114,182,13,36,0,0,0,14,73,68,65,84,120,156,99,248,15,5,12,48,6,0,143,130,15,241,60,165,86,81,0,0,0,0,73,69,78,68,174,66,96,130};std::ofstream file(assets/"owned-white.png",std::ios::binary);file.write(reinterpret_cast<const char*>(png),sizeof(png));check(bool(file),"Write bounded synthetic icon fixture");
    }
    ~Fixture(){if(window)DestroyWindow(window);if(atom)UnregisterClassW(reinterpret_cast<LPCWSTR>(static_cast<ULONG_PTR>(atom)),GetModuleHandleW(nullptr));std::error_code ec;std::filesystem::remove_all(assets,ec);}
};
const n::DrawObject&draw(n::LayerScene&scene,std::string_view id){const auto index=scene.surfaceIndex(id);check(index.has_value(),"Original source leaf retains a native surface");scene.prepareDraws();return scene.draws()[*index];}
void native(const wchar_t*shader){
    Fixture fixture;n::Renderer renderer;renderer.initialize(fixture.window,512,384,{n::Driver::warpForTests,shader,n::RenderTarget::offscreenForTests});renderer.setCamera(n::layerViewportProjection(512,384));
    n::LayerRasterizer raster;n::LayerRasterOptions options;options.assetRoot=fixture.assets;options.pixelsPerPoint=1;
    m::FileShelfState state(items(10000));m::ShelfPresentation source;m::ShelfPresentationStyle style;source.update(state,style);n::NativeShelfScene bridge(source,raster,options);
    rejects([&]{bridge.syncContent();},"Native image failure leaves initial bridge empty");check(bridge.entries().empty(),"Failed initial artwork never publishes partial entries");auto bindings=images(source);check(bridge.syncContent(bindings)&&!bridge.syncContent(),"Initial content commits once; unchanged event is a no-op");
    n::NativeShelfPose pose;pose.time=0;bridge.updatePose(pose);n::LayerComposition composition;composition.setEntries(renderer,bridge.entries());composition.present(renderer);renderer.draw(false);
    check(bridge.entries().size()==11&&bridge.stats().cards==8&&!IsWindowVisible(fixture.window),"Only eleven ordered parts and eight bounded cards; owned window remains hidden");
    auto*first=bridge.cardScene("synthetic-0");auto*second=bridge.cardScene("synthetic-1");check(first&&second&&bridge.entries()[1].scene==first&&bridge.entries()[8].scene==bridge.cardScene("synthetic-7")&&bridge.entries()[10].scene==&bridge.foregroundScene(),"Cards paint before scrollbar and upper chrome");
    const auto&plate=draw(*first,"shelf.card.synthetic-0/plate");checkNear(plate.world.values[12],12,"Original card x");checkNear(plate.world.values[13],44,"Original card y");check(plate.masks.size()==1&&plate.masks[0].bounds==m::ShelfPresentation::contentClip(),"Original collection clip retained outside selection depth");
    const auto pixels=renderer.readback();check(pixels.pixels[std::size_t(60)*pixels.rowBytes+40*4+3]>0,"Zero-bounds card plate paints actual owned render-target pixels");
    const auto before=raster.stats();const auto uploads=renderer.stats();const auto compositionRevision=bridge.compositionRevision();state.scrollBy(1);source.update(state,style);check(bridge.syncContent()&&bridge.compositionRevision()==compositionRevision,"One-point scroll changes placement without reassembly");pose.time=.1;bridge.updatePose(pose);composition.present(renderer);check(bridge.cardScene("synthetic-0")==first&&raster.stats().rasterizations==before.rasterizations,"Scrolling reuses every visible local raster and scene");
    bridge.setFeedback("shelf:synthetic-0:select",false,false,1);pose.time=1.03;bridge.updatePose(pose);composition.present(renderer);const auto tintID="shelf:synthetic-0:select/highlight/tint",rimID="shelf:synthetic-0:select/highlight/rim";const auto half=draw(*first,tintID).opacity;check(half>0&&half<.62f&&bridge.requiresFrames(1.03),"Shelf hover uses original finite ease-out");
    bridge.setFeedback("shelf:synthetic-0:select",true,false,1.03);pose.time=1.091;bridge.updatePose(pose);composition.present(renderer);check(draw(*first,tintID).opacity==1&&draw(*first,rimID).opacity==1,"Press accelerates both tint and unchanged-target rim to source .06s finish");
    bridge.setFeedback("shelf:add",false,false,1.1);pose.time=1.12;bridge.updatePose(pose);const auto releasing=draw(*first,tintID).opacity;bridge.setFeedback("shelf:add",true,false,1.12);pose.time=1.13;bridge.updatePose(pose);check(draw(*first,tintID).opacity>0&&draw(*first,tintID).opacity<releasing,"Unrelated press does not restart another card's release");
    bridge.setFeedback({},false,true,1.2);pose.time=2;bridge.updatePose(pose);composition.present(renderer);std::array<n::ShelfSelectionPose,1>selection{{{"synthetic-0",-.75,2.5}}};pose.selections=selection;pose.toolbarY=3;pose.toolbarZ=-3;pose.time=2.1;bridge.updatePose(pose);composition.present(renderer);
    checkNear(draw(*first,"shelf.card.synthetic-0/plate").world.values[13],42.25,"Only intended card receives caller selection Y");checkNear(draw(*first,"shelf.card.synthetic-0/plate").world.values[14],2.5,"Selection depth stays a GPU world transform");checkNear(draw(*second,"shelf.card.synthetic-1/plate").world.values[13],43,"Other card remains in original scroll position");
    checkNear(draw(bridge.foregroundScene(),"shelf:add/depot/retained-static").world.values[13]+305,308,"Source toolbar sibling caption follows reveal translation");checkNear(draw(bridge.foregroundScene(),"shelf:add/depot/retained-static").world.values[14],-3,"Source toolbar Z displacement retained");
    const auto rasterBefore=raster.stats();const auto gpuBefore=renderer.stats();allocations=0;counting=true;try{for(unsigned k=0;k<120;++k){pose.contentWorld=c::Matrix4::translation(double(k%5),double(k%3))*c::Matrix4::rotation(.001*double(k),0,0);pose.time=3+double(k)*.01;bridge.updatePose(pose);composition.present(renderer);}}catch(...){counting=false;throw;}counting=false;
    check(allocations==0&&raster.stats().rasterizations==rasterBefore.rasterizations&&raster.stats().textLayoutsCreated==rasterBefore.textLayoutsCreated&&renderer.stats().textureUploads==gpuBefore.textureUploads&&renderer.stats().meshUploads==gpuBefore.meshUploads,"120 tilt/selection/toolbar frames allocate, rasterize and upload no geometry/textures");
    const auto saved=draw(*first,"shelf.card.synthetic-0/plate").world;pose.time=5;auto invalid=pose;std::array<n::ShelfSelectionPose,2>badSelection{{{"synthetic-0",0,0},{"absent",0,0}}};invalid.selections=badSelection;rejects([&]{bridge.updatePose(invalid);},"Late stale selection rejects whole numeric batch");check(draw(*first,"shelf.card.synthetic-0/plate").world==saved,"Rejected late pose preserves all prior scene geometry");invalid=pose;invalid.collectionReveal=n::SubsectionShutterPath{};rejects([&]{bridge.updatePose(invalid);},"Unconnected four-strip reveal is explicit, never approximated");invalid=pose;invalid.dropStrokeEnd=.5;rejects([&]{bridge.updatePose(invalid);},"Unconnected drop trace is explicit");invalid=pose;invalid.contentWorld.values[0]=std::numeric_limits<double>::max();rejects([&]{bridge.updatePose(invalid);},"GPU-range overflow rejects before any placement mutation");check(draw(*first,"shelf.card.synthetic-0/plate").world==saved,"Overflow rollback keeps previous frame");
    state.selectItem("synthetic-0",{},false,false);source.update(state,style);bindings=images(source);check(bridge.syncContent(bindings),"Selection rebuilds its changed border content");check(bridge.cardScene("synthetic-1")==second&&bridge.cardScene("synthetic-0")!=first&&bridge.stats().retiredParts>0,"Unchanged cards reused and old selected artwork retained until owner publication");rejects([&]{bridge.collectRetired(renderer);},"Cannot destroy a still-published replaced card");pose.contentWorld={};pose.selections={};pose.toolbarY=pose.toolbarZ=0;pose.time=5;bridge.updatePose(pose);composition.setEntries(renderer,bridge.entries());check(bridge.collectRetired(renderer)&&bridge.stats().retiredParts==0,"Old resources retire only after owner replaces combined composition");composition.present(renderer);
    first=bridge.cardScene("synthetic-0");checkNear(draw(*first,"shelf.card.synthetic-0/plate").world.values[14],5,"Settled source selection depth defaults exactly to five");
    state.scrollBy(800);source.update(state,style);bindings=images(source);
    // Simulate other still-owned shell/module cache entries. Budget failure is
    // explicit before any Shelf raster or published scene changes.
    std::vector<std::string>otherIDs;const Json other=Json::Object{{"id","other-owned-fixture"},{"bounds",Json::Array{0,0,1,1}},{"backgroundColor",Json::Object{{"sRGB",Json::Array{1,1,1,1}}}}};
    while(raster.stats().entries<200){auto id="shelf-capacity-fixture-"+std::to_string(otherIDs.size());raster.rasterize(id,1,other,options);otherIDs.push_back(std::move(id));}
    const auto capacityRaster=raster.stats().rasterizations,capacityRevision=bridge.compositionRevision();rejects([&]{bridge.syncContent(bindings,1);},"Shared shell/module raster budget rejects before partial replacement");check(raster.stats().rasterizations==capacityRaster&&bridge.compositionRevision()==capacityRevision&&bridge.cardScene("synthetic-0")==first,"Shared capacity failure neither evicts active IDs nor builds partial artwork");for(const auto&id:otherIDs)raster.remove(id);
    auto badBindings=bindings;badBindings.back().contents["sha256"]=std::string(64,'a');const auto priorRevision=bridge.compositionRevision();rejects([&]{bridge.syncContent(badBindings,1);},"Late invalid visible icon hash rejects native replacement");check(bridge.compositionRevision()==priorRevision&&bridge.cardScene("synthetic-0")==first,"Asset failure retains the complete previously published generation");check(bridge.syncContent(bindings,1)&&bridge.stats().cards<=8,"Distant scroll recycles only visible card scenes");pose.time=6;bridge.updatePose(pose);composition.setEntries(renderer,bridge.entries());bridge.collectRetired(renderer);composition.present(renderer);renderer.draw(false);
    check(renderer.stats().textureUploads>uploads.textureUploads,"Test exercises real changed local raster uploads, not an empty bridge");composition.detach(renderer);check(bridge.releaseResources(renderer)&&renderer.stats().meshes==0&&renderer.stats().textures==0,"All owned resources released after composition detach");renderer.reset();
}
#endif
}
#ifdef _WIN32
int wmain(int argc,wchar_t**argv){const HRESULT hr=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);try{check(SUCCEEDED(hr)&&argc==2,"Pass HLSL path to hidden owned Shelf fixture");portable();native(argv[1]);CoUninitialize();std::cout<<"PASS "<<checks<<" Shelf scene checks\n";return 0;}catch(const std::exception&e){counting=false;if(SUCCEEDED(hr))CoUninitialize();std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}}
#else
int main(){try{portable();std::cout<<"PASS "<<checks<<" Shelf scene preparation checks\n";return 0;}catch(const std::exception&e){counting=false;std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}}
#endif
