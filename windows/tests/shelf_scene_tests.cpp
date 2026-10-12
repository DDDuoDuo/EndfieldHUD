#include "native/shelf_scene.hpp"
#include "core/shell_packet.hpp"
#include <array>
#include <bit>
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
// HUDSubsectionTransition.reveal's authored mask (shelfRevealShutter): the
// unchanged source keyframes at their key times, then a reveal that only ever
// grows. Core Animation's own morph of the degenerate first/last keyframes
// re-hid revealed cards behind curved wedges (the removal glitch).
void revealShutter(){
    constexpr double duration=c::SubsectionTransitionStyle::duration;const auto viewport=m::ShelfPresentation::contentClip();
    for(double direction:{-1.,1.}){
        for(std::size_t k=0;k<4;++k){const auto expected=c::subsectionMaskKeyframe(viewport,direction,k);const auto actual=n::shelfRevealShutter(direction,c::SubsectionTransitionStyle::maskKeyTimes[k]*duration);
            for(std::size_t strip=0;strip<4;++strip)for(std::size_t vertex=0;vertex<6;++vertex){checkNear(actual[strip][vertex].x,expected[strip][vertex].x,"Authored keyframe X at its key time");checkNear(actual[strip][vertex].y,expected[strip][vertex].y,"Authored keyframe Y at its key time");}}
        const auto first=n::shelfRevealShutter(direction,-1),last=n::shelfRevealShutter(direction,1);const auto collapsed=c::subsectionMaskKeyframe(viewport,direction,0),full=c::subsectionMaskKeyframe(viewport,direction,3);
        for(std::size_t strip=0;strip<4;++strip)for(std::size_t vertex=0;vertex<6;++vertex)check(first[strip][vertex].x==collapsed[strip][vertex].x&&last[strip][vertex].x==full[strip][vertex].x&&last[strip][vertex].y==full[strip][vertex].y,"Reveal clock clamps to the authored first and last keyframes");
        // Dense 1/1200 s sweep: each strip keeps its band and its anchored edge,
        // and its moving edge never retreats (nothing revealed is hidden again).
        std::array<double,4>edge{};edge.fill(direction<0?viewport.x:viewport.x+viewport.width);std::array<double,4>area{};double sum{};counting=true;
        for(unsigned step=0;step<=312;++step){const auto path=n::shelfRevealShutter(direction,duration*step/312.);
            for(std::size_t strip=0;strip<4;++strip){const auto&p=path[strip];const double top=viewport.y+viewport.height/4*double(strip),bottom=top+viewport.height/4;
                double low=p[0].x,high=p[0].x,a{};for(std::size_t v=0;v<6;++v){low=std::min(low,p[v].x);high=std::max(high,p[v].x);const auto&q=p[(v+1)%6];a+=p[v].x*q.y-q.x*p[v].y;sum+=p[v].y;
                    if(p[v].y<top-1e-9||p[v].y>bottom+1e-9)throw std::runtime_error("Reveal strip left its band");}
                a=std::abs(a)/2;
                if(direction<0){if(std::abs(low-viewport.x)>1e-9||high<edge[strip]-1e-9)throw std::runtime_error("Left reveal strip retreated or detached");edge[strip]=high;}
                else{if(std::abs(high-viewport.x-viewport.width)>1e-9||low>edge[strip]+1e-9)throw std::runtime_error("Right reveal strip retreated or detached");edge[strip]=low;}
                if(a<area[strip]-1e-9)throw std::runtime_error("Revealed strip area shrank");area[strip]=a;}}
        counting=false;check(sum>0&&allocations==0,"Dense authored reveal sampling is monotonic, banded and allocation-free");
        check(area[0]>=viewport.width*viewport.height/4-1e-6&&area[3]>=viewport.width*viewport.height/4-1e-6,"Finished reveal covers every band");
    }
    rejects([]{(void)n::shelfRevealShutter(std::numeric_limits<double>::quiet_NaN(),0);},"Non-finite reveal direction rejects");rejects([]{(void)n::shelfRevealShutter(-1,std::numeric_limits<double>::infinity());},"Non-finite reveal time rejects");
}
void portable(){
    revealShutter();
    m::ShelfPresentation source;rejects([&]{n::prepareShelfScene(source);},"Uninitialized shelf rejects");m::FileShelfState state(items(10000));m::ShelfPresentationStyle style;source.update(state,style);
    rejects([&]{n::prepareShelfScene(source);},"File icons and Depot artwork are explicit, never blank replacements");auto supplied=images(source);const auto original=source.chrome();const auto plan=n::prepareShelfScene(source,supplied);
    check(source.chrome()==original&&plan.cards.size()==8,"Preparation preserves descriptor and only builds eight visible cards");
    check(plan.collection.surfaces.empty()&&plan.scrollbar.surfaces.size()==1&&!plan.foreground.surfaces.empty(),"Collection, numeric scrollbar and upper chrome remain separate paint groups");
    // The drop outline's stroke is cleared while nothing is dragged: it paints
    // nothing, so it keeps no surface (no 382 x 248 transparent raster).
    check(find(plan.foreground.layers,"shelf.drop")==nullptr&&std::none_of(plan.foreground.surfaces.begin(),plan.foreground.surfaces.end(),[](const auto&s){return s.drop;}),"The invisible drop outline retains no surface");
    // Chrome groups cover their members' integer-aligned paint extent, not
    // the whole canvas; their position equals their bounds origin.
    for(const auto&group:plan.foreground.layers["children"].array())if(group["id"].string().ends_with("/retained-static")){
        const auto&b=group["bounds"].array();const auto&at=group["position"].array();
        check(b[0]==at[0]&&b[1]==at[1]&&b[0].number()==std::floor(b[0].number())&&b[1].number()==std::floor(b[1].number())&&b[2].number()==std::floor(b[2].number())&&b[3].number()==std::floor(b[3].number())&&b[2].number()*b[3].number()<400*334/2.,
              "A chrome group is integer-aligned, placed at its bounds origin and far smaller than the canvas");}
    check(find(plan.foreground.layers["children"].array().back(),"shelf:clear/title")!=nullptr,"Toolbar captions follow source sibling plate order");
    for(const auto&p:plan.cards){const auto id="shelf.card."+p.itemID;const auto plate=surface(p,id+"/plate"),icon=surface(p,id+"/icon"),separator=surface(p,id+"/separator");
        check(plate==0&&icon==separator&&separator<surface(p,id+"/eye"),"Original plate, controls, icons, text and final glyph paint order");
        // (Inside a group an empty frame is moved onto its path's first point,
        // position and bounds origin together: the placement is unchanged.)
        for(const auto*s:{"/plate","/separator","/eye","/folder","/cross"}){const auto&v=leaf(p,id+s);const auto&b=v["bounds"].array();const auto&at=v["position"].array();
            check(b[2].number()==0&&b[3].number()==0&&at[0].number()==b[0].number()&&at[1].number()==b[1].number()&&!v["shape"]["path"].array().empty(),"Zero-bounds source shape ink is not cropped or omitted");}
        check(p.surfaces.size()==11,"Consecutive static source artwork leaves headroom for two live card generations");
        check(leaf(p,id+"/icon")["contents"]==supplied[1].contents,"Explicit pinned native icon metadata survives flattening");
        for(const auto&s:p.surfaces){check(s.local.values[14]==0,"Selection depth is never baked into raster geometry");if(s.feedback!=n::ShelfSceneSurface::none)check(leaf(p,s.id)["opacity"].number()==1,"Invisible highlight art remains resident for numeric feedback");}
    }
    for(const auto&s:plan.foreground.surfaces)if(s.id.starts_with("shelf:add")||s.id.starts_with("shelf:clear"))check(s.toolbar,"Caption siblings and highlight descendants follow the whole toolbar reveal");
    state.setDropTarget(true);source.update(state,style);const auto dropping=n::prepareShelfScene(source,images(source));state.setDropTarget(false);source.update(state,style);
    check(std::count_if(dropping.foreground.surfaces.begin(),dropping.foreground.surfaces.end(),[](const auto&s){return s.drop;})==1,"While a file is dragged over it the drop outline has its surface");
    const auto fullDrop=leaf(dropping.foreground,"shelf.drop")["shape"]["path"];
    check(n::shelfDropStrokePath(1)==fullDrop,"Completed drop trace retains exact original rounded path commands");
    check(n::shelfDropStrokePath(0).array().size()==1,"Zero stroke starts with only the original right-midpoint move");
    for(const auto& sample:std::array<std::array<double,3>,3>{{{.25,191,248},{.5,0,124},{.75,191,0}}}){const auto path=n::shelfDropStrokePath(sample[0]);const auto&end=path.array().back()["points"].array().back().array();checkNear(end[0].number(),sample[1],"Source arc-length quarter endpoint x");checkNear(end[1].number(),sample[2],"Source arc-length quarter endpoint y");}
    for(double invalid:{-1.,1.1,std::numeric_limits<double>::quiet_NaN()})rejects([&]{n::shelfDropStrokePath(invalid);},"Invalid drop stroke rejects");
    auto bad=supplied;bad.back().dependency.unavailable=true;rejects([&]{n::prepareShelfScene(source,bad);},"Native icon availability is part of exact dependency");bad=supplied;bad[0].dependency.sourceInTint=false;rejects([&]{n::prepareShelfScene(source,bad);},"Untinted Depot cannot masquerade as prepared source-in artwork");bad=supplied;bad[0].contents["sha256"]="bad";rejects([&]{n::prepareShelfScene(source,bad);},"Unpinned images reject");bad=supplied;bad.back()=bad.front();rejects([&]{n::prepareShelfScene(source,bad);},"Duplicate image dependencies reject");
    auto memory=supplied;memory.back().contents=Json::Object{{"memoryImage","synthetic-native-icon"},{"revision",1}};
    const auto withMemory=n::prepareShelfScene(source,memory);
    check(leaf(withMemory.cards.back(),"shelf.card."+withMemory.cards.back().itemID+"/icon")["contents"]==memory.back().contents,"Native icon memory dependency passes unchanged into retained artwork");
    memory.front().contents=memory.back().contents;rejects([&]{n::prepareShelfScene(source,memory);},"Native memory images cannot replace the pinned source Depot icon");
    m::FileShelfState empty({});style.depotIconAvailable=false;source.update(empty,style);const auto fallback=n::prepareShelfScene(source);check(fallback.cards.empty()&&fallback.scrollbar.surfaces.empty()&&surface(fallback.collection,"shelf.empty/folder")==0,"Actual source folder/+ fallback compiles without OS icon lookup");
    // Placement = position - bounds origin (a grouped empty frame sits on its path's first point).
    const auto&folderLeaf=leaf(fallback.collection,"shelf.empty/folder");
    checkNear(folderLeaf["position"].array()[0].number()-folderLeaf["bounds"].array()[0].number(),165,"Empty fallback shape keeps source x");checkNear(folderLeaf["position"].array()[1].number()-folderLeaf["bounds"].array()[1].number(),88,"Empty fallback shape keeps source y");
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
    m::FileShelfState state(items(10000));m::ShelfPresentation source;m::ShelfPresentationStyle style;source.update(state,style);
    n::NativeShelfScene bridge(source,raster,options);
    rejects([&]{bridge.syncContent();},"Native image failure leaves initial bridge empty");check(bridge.entries().empty(),"Failed initial artwork never publishes partial entries");auto bindings=images(source);check(bridge.syncContent(bindings)&&!bridge.syncContent(),"Initial content commits once; unchanged event is a no-op");
    n::NativeShelfPose pose;pose.time=0;bridge.updatePose(pose);n::LayerComposition composition;composition.setEntries(renderer,bridge.entries());composition.present(renderer);renderer.draw(false);
    check(bridge.entries().size()==11&&bridge.stats().cards==8&&!IsWindowVisible(fixture.window),"Only eleven ordered parts and eight bounded cards; owned window remains hidden");
    auto*first=bridge.cardScene("synthetic-0");auto*second=bridge.cardScene("synthetic-1");check(first&&second&&bridge.entries()[1].scene==first&&bridge.entries()[8].scene==bridge.cardScene("synthetic-7")&&bridge.entries()[10].scene==&bridge.foregroundScene(),"Cards paint before scrollbar and upper chrome");
    const auto&plate=draw(*first,"shelf.card.synthetic-0/plate");checkNear(plate.world.values[12],12,"Original card x");checkNear(plate.world.values[13],44,"Original card y");check(plate.masks.size()==1&&plate.masks[0].bounds==m::ShelfPresentation::contentClip(),"Original collection clip retained outside selection depth");
    const auto pixels=renderer.readback();check(pixels.pixels[std::size_t(60)*pixels.rowBytes+40*4+3]>0,"Zero-bounds card plate paints actual owned render-target pixels");
    const auto before=raster.stats();const auto uploads=renderer.stats();const auto compositionRevision=bridge.compositionRevision();state.scrollBy(1);source.update(state,style);check(bridge.syncContent()&&bridge.compositionRevision()==compositionRevision,"One-point scroll changes placement without reassembly");pose.time=.1;bridge.updatePose(pose);composition.present(renderer);check(bridge.cardScene("synthetic-0")==first&&raster.stats().rasterizations==before.rasterizations,"Scrolling reuses every visible local raster and scene");
    bridge.setFeedback("shelf:synthetic-0:select",false,false,1);pose.time=1.03;bridge.updatePose(pose);composition.present(renderer);const auto tintID="shelf:synthetic-0:select/highlight/tint",rimID="shelf:synthetic-0:select/highlight/rim";const auto half=draw(*first,tintID).opacity;check(half>0&&half<.62f&&bridge.requiresFrames(1.03),"Shelf hover uses original finite ease-out");
    style.accent={.4,.2,.8,1};source.update(state,style);bindings=images(source);bridge.syncContent(bindings);bridge.updatePose(pose);composition.setEntries(renderer,bridge.entries());composition.present(renderer);check(bridge.collectRetired(renderer),"Preference replacement retires only after publication");first=bridge.cardScene("synthetic-0");second=bridge.cardScene("synthetic-1");check(draw(*first,tintID).opacity==half,"Changed Shelf accent retains exact in-flight hover origin");
    bridge.setFeedback("shelf:synthetic-0:select",true,false,1.03);pose.time=1.091;bridge.updatePose(pose);composition.present(renderer);check(draw(*first,tintID).opacity==1&&draw(*first,rimID).opacity==1,"Press accelerates both tint and unchanged-target rim to source .06s finish");
    bridge.setFeedback("shelf:add",false,false,1.1);pose.time=1.12;bridge.updatePose(pose);const auto releasing=draw(*first,tintID).opacity;bridge.setFeedback("shelf:add",true,false,1.12);pose.time=1.13;bridge.updatePose(pose);check(draw(*first,tintID).opacity>0&&draw(*first,tintID).opacity<releasing,"Unrelated press does not restart another card's release");
    bridge.setFeedback({},false,true,1.2);pose.time=2;bridge.updatePose(pose);composition.present(renderer);std::array<n::ShelfSelectionPose,1>selection{{{"synthetic-0",-.75,2.5}}};pose.selections=selection;pose.toolbarY=3;pose.toolbarZ=-3;pose.time=2.1;bridge.updatePose(pose);composition.present(renderer);
    checkNear(draw(*first,"shelf.card.synthetic-0/plate").world.values[13],42.25,"Only intended card receives caller selection Y");checkNear(draw(*first,"shelf.card.synthetic-0/plate").world.values[14],2.5,"Selection depth stays a GPU world transform");checkNear(draw(*second,"shelf.card.synthetic-1/plate").world.values[13],43,"Other card remains in original scroll position");
    checkNear(draw(bridge.foregroundScene(),"shelf:add/depot/retained-static").world.values[13]+305,308,"Source toolbar sibling caption follows reveal translation");checkNear(draw(bridge.foregroundScene(),"shelf:add/depot/retained-static").world.values[14],-3,"Source toolbar Z displacement retained");
    const auto rasterBefore=raster.stats();const auto gpuBefore=renderer.stats();allocations=0;counting=true;try{for(unsigned k=0;k<120;++k){pose.contentWorld=c::Matrix4::translation(double(k%5),double(k%3))*c::Matrix4::rotation(.001*double(k),0,0);pose.time=3+double(k)*.01;bridge.updatePose(pose);composition.present(renderer);}}catch(...){counting=false;throw;}counting=false;
    check(allocations==0&&raster.stats().rasterizations==rasterBefore.rasterizations&&raster.stats().textLayoutsCreated==rasterBefore.textLayoutsCreated&&renderer.stats().textureUploads==gpuBefore.textureUploads&&renderer.stats().meshUploads==gpuBefore.meshUploads,"120 tilt/selection/toolbar frames allocate, rasterize and upload no geometry/textures");
    const auto saved=draw(*first,"shelf.card.synthetic-0/plate").world;pose.time=5;auto invalid=pose;std::array<n::ShelfSelectionPose,2>badSelection{{{"synthetic-0",0,0},{"absent",0,0}}};invalid.selections=badSelection;rejects([&]{bridge.updatePose(invalid);},"Late stale selection rejects whole numeric batch");check(draw(*first,"shelf.card.synthetic-0/plate").world==saved,"Rejected late pose preserves all prior scene geometry");invalid=pose;invalid.reveal=n::ShelfRevealSample{1,std::numeric_limits<double>::quiet_NaN()};rejects([&]{bridge.updatePose(invalid);},"Invalid reveal clock rejects before mutation");invalid=pose;invalid.dropStrokeEnd=1.5;rejects([&]{bridge.updatePose(invalid);},"Out-of-range drop trace rejects");invalid=pose;invalid.contentWorld.values[0]=std::numeric_limits<double>::max();rejects([&]{bridge.updatePose(invalid);},"GPU-range overflow rejects before any placement mutation");check(draw(*first,"shelf.card.synthetic-0/plate").world==saved,"Overflow rollback keeps previous frame");
    state.selectItem("synthetic-0",{},false,false);source.update(state,style);bindings=images(source);check(bridge.syncContent(bindings),"Selection rebuilds its changed border content");check(bridge.cardScene("synthetic-1")==second&&bridge.cardScene("synthetic-0")!=first&&bridge.stats().retiredParts>0,"Unchanged cards reused and old selected artwork retained until owner publication");rejects([&]{bridge.collectRetired(renderer);},"Cannot destroy a still-published replaced card");pose.contentWorld={};pose.selections={};pose.toolbarY=pose.toolbarZ=0;pose.time=5;bridge.updatePose(pose);composition.setEntries(renderer,bridge.entries());check(bridge.collectRetired(renderer)&&bridge.stats().retiredParts==0,"Old resources retire only after owner replaces combined composition");composition.present(renderer);
    first=bridge.cardScene("synthetic-0");checkNear(draw(*first,"shelf.card.synthetic-0/plate").world.values[14],5,"Settled source selection depth defaults exactly to five");
    state.scrollBy(800);source.update(state,style);bindings=images(source);
    // Simulate other still-owned shell/module cache entries. Budget failure is
    // explicit before any Shelf raster or published scene changes.
    std::vector<std::string>otherIDs;const Json other=Json::Object{{"id","other-owned-fixture"},{"bounds",Json::Array{0,0,1,1}},{"backgroundColor",Json::Object{{"sRGB",Json::Array{1,1,1,1}}}}};
    while(raster.stats().entries<n::LayerRasterizer::maximumEntries){auto id="shelf-capacity-fixture-"+std::to_string(otherIDs.size());raster.rasterize(id,1,other,options);otherIDs.push_back(std::move(id));}
    const auto capacityRaster=raster.stats().rasterizations,capacityRevision=bridge.compositionRevision();rejects([&]{bridge.syncContent(bindings,1);},"Shared shell/module raster budget rejects before partial replacement");check(raster.stats().rasterizations==capacityRaster&&bridge.compositionRevision()==capacityRevision&&bridge.cardScene("synthetic-0")==first,"Shared capacity failure neither evicts active IDs nor builds partial artwork");for(const auto&id:otherIDs)raster.remove(id);
    auto badBindings=bindings;badBindings.back().contents["sha256"]=std::string(64,'a');const auto priorRevision=bridge.compositionRevision();rejects([&]{bridge.syncContent(badBindings,1);},"Late invalid visible icon hash rejects native replacement");check(bridge.compositionRevision()==priorRevision&&bridge.cardScene("synthetic-0")==first,"Asset failure retains the complete previously published generation");check(bridge.syncContent(bindings,1)&&bridge.stats().cards<=8,"Distant scroll recycles only visible card scenes");pose.time=6;bridge.updatePose(pose);composition.setEntries(renderer,bridge.entries());bridge.collectRetired(renderer);composition.present(renderer);renderer.draw(false);
    const auto settled=renderer.readback();const auto resourceCount=renderer.stats().textures;const auto cardLayouts=raster.stats().textLayoutsCreated;
    for(double direction:{-1.,1.}){for(unsigned sample=0;sample<9;++sample){pose.time+=.03;pose.reveal=n::ShelfRevealSample{direction,double(sample)*.26/8};bridge.updatePose(pose);bridge.uploadAnimations(renderer);composition.setEntries(renderer,bridge.entries());composition.present(renderer);bridge.collectRetired(renderer);renderer.draw(false);
        const auto frame=renderer.readback();if(sample==0)check(frame.pixels!=settled.pixels,"Collapsed source reveal actually clips collection pixels");
        for(unsigned y:{8u,25u,310u,328u})for(unsigned x=0;x<400;++x){const auto at=std::size_t(y)*frame.rowBytes+x*4;for(unsigned k=0;k<4;++k)check(frame.pixels[at+k]==settled.pixels[at+k],"Collection reveal leaves header/status/toolbar attached");}
        if(sample<8){const auto count=bridge.stats().maskRasters;const auto tex=renderer.stats().textureUploads;bridge.uploadAnimations(renderer);check(bridge.stats().maskRasters==count&&renderer.stats().textureUploads==tex,"Equal sampled mask does not rasterize or upload again");check(renderer.stats().textures==resourceCount+1,"One bounded alpha texture serves every visible card");}
        else check(frame.pixels==settled.pixels&&renderer.stats().textures==resourceCount,"Finished reveal restores exact settled target and retires its mask");
    }}pose.reveal.reset();check(raster.stats().textLayoutsCreated==cardLayouts,"Reveal reuses all text and card artwork");
    state.setDropTarget(true);source.update(state,style);bindings=images(source);bridge.syncContent(bindings);pose.time+=.1;pose.dropStrokeEnd=.25;bridge.updatePose(pose);bridge.uploadAnimations(renderer);composition.setEntries(renderer,bridge.entries());composition.present(renderer);bridge.collectRetired(renderer);renderer.draw(false);const auto partialDrop=renderer.readback();
    const auto dropRasters=bridge.stats().dropRasters;bridge.uploadAnimations(renderer);check(bridge.stats().dropRasters==dropRasters,"Equal drop path is a retained no-op");pose.time+=.1;pose.dropStrokeEnd=1;bridge.updatePose(pose);bridge.uploadAnimations(renderer);composition.setEntries(renderer,bridge.entries());composition.present(renderer);renderer.draw(false);check(renderer.readback().pixels!=partialDrop.pixels,"Source drop outline traces its path rather than fading a full outline");
    // Tight groups: each group of consecutive static artwork (chrome and
    // cards) is rasterized over its integer-aligned paint extent instead of
    // the whole 400 x 334 canvas or 184 x 74 card. At the production 2 px per
    // point both rasters lie on one pixel grid and, everything outside a
    // raster being transparent, paint the same image. Text, images, lines
    // and straight-edged shapes come out bit-identical; Direct2D flattens a
    // curve in the shifted pixel space, so a few antialiased edge samples of
    // the curved card glyphs (eye, folder) move slightly (measured: 19
    // samples of 5,040, at most 13 of 255 levels); no sample changes from
    // empty to covered or the reverse.
    {
        auto two=options;two.pixelsPerPoint=2;two.includeRootOpacity=false;two.includeRootMask=true;
        std::size_t groups{},cardGroups{},tightBytes{},wholeBytes{},offGrid{},differing{},crowded{},flipped{};int worst{};
        for(const bool emptyShelf:{false,true}){
            m::FileShelfState none({});m::ShelfPresentation emptySource;if(emptyShelf)emptySource.update(none,style);
            const auto&shown=emptyShelf?emptySource:source;const auto plan=n::prepareShelfScene(shown,images(shown));
            std::vector<const n::ShelfScenePart*>parts{&plan.foreground,&plan.collection};for(const auto&card:plan.cards)parts.push_back(&card);
            for(const auto*part:parts)for(const auto&node:part->layers["children"].array()){
                const auto id=node["id"].string();if(!id.ends_with("/retained-static"))continue;++groups;if(!part->itemID.empty())++cardGroups;
                auto whole=node;whole["bounds"]=part->itemID.empty()?Json::Array{0,0,400,334}:Json::Array{0,0,184,74};whole["position"]=Json::Array{0,0};
                const auto tight=raster.rasterize("tight:"+id,1,node,two),full=raster.rasterize("whole:"+id,1,whole,two);
                check(tight->complete()&&full->complete(),"Both group rasters are complete");
                const double dx=(tight->bounds.x-full->bounds.x)*2,dy=(tight->bounds.y-full->bounds.y)*2;
                const bool grid=dx==std::floor(dx)&&dy==std::floor(dy);offGrid+=!grid;
                // Pixel (x, y) of the whole raster is pixel (x - dx, y - dy) of the tight one.
                const auto pixel=[](const n::LayerRasterImage&image,long long x,long long y,unsigned k)->int{return x<0||y<0||x>=static_cast<long long>(image.width)||y>=static_cast<long long>(image.height)?0:image.straightRGBA[(std::size_t(y)*image.width+std::size_t(x))*4+k];};
                const long long ox=static_cast<long long>(std::floor(dx)),oy=static_cast<long long>(std::floor(dy));
                const long long left=std::min(0ll,ox),top=std::min(0ll,oy),right=std::max<long long>(full->width,ox+tight->width),bottom=std::max<long long>(full->height,oy+tight->height);
                std::size_t count{};int most{};
                for(long long y=top;y<bottom;++y)for(long long x=left;x<right;++x){
                    const int a=pixel(*full,x,y,3),b=pixel(*tight,x-ox,y-oy,3);if(a==0&&b==0)continue; // both transparent (colour undefined)
                    int d=std::abs(a-b);for(unsigned k=0;k<3;++k)d=std::max(d,std::abs(pixel(*full,x,y,k)*a-pixel(*tight,x-ox,y-oy,k)*b)/255); // premultiplied colour
                    if(d){++count;most=std::max(most,d);flipped+=(a==0&&b==255)||(a==255&&b==0);}}
                differing+=count;worst=std::max(worst,most);crowded+=count*100>std::size_t(tight->width)*tight->height;
                std::cout<<"group "<<id<<": tight "<<tight->bounds.x<<','<<tight->bounds.y<<' '<<tight->width<<'x'<<tight->height<<" px, whole "<<full->bounds.x<<','<<full->bounds.y<<' '<<full->width<<'x'<<full->height<<" px, "<<count<<" pixels differ (at most "<<most<<")\n";
                tightBytes+=tight->straightRGBA.size();wholeBytes+=full->straightRGBA.size();raster.remove("tight:"+id);raster.remove("whole:"+id);
            }
        }
        std::cout<<"Shelf static groups at 2x: "<<groups<<" groups ("<<cardGroups<<" in cards), "<<tightBytes/1024<<" KiB tight vs "<<wholeBytes/1024<<" KiB over the canvas or card; "<<differing<<" pixels differ by at most "<<worst<<"\n";
        check(offGrid==0,"The tight and whole group rasters share one pixel grid");
        check(worst<=16&&flipped==0&&crowded==0,"A tight group paints the pixels of the whole-canvas or whole-card group (only a few antialiased curve samples move, by at most 16 levels)");
        check(groups>=3&&cardGroups>=8&&tightBytes*2<wholeBytes,"Groups shrink to a fraction of their canvas and card rasters");
    }
    // park(): a still-composed Shelf refuses and stays resident; once the
    // composition dropped it (the module was switched away) every part, its
    // rasters and textures go, and the next content event rebuilds the same
    // pixels (Mac HUDModuleContent makes the content again on selection).
    const auto composed=bridge.stats();check(composed.parts==bridge.entries().size()&&composed.parts>=4&&composed.surfaces>0&&composed.pixelBytes>0&&!bridge.park(renderer)&&bridge.stats().parts==composed.parts&&bridge.stats().parks==0,"A composed Shelf refuses to park and stays resident");
    renderer.draw(false);const auto beforePark=renderer.readback();const auto rasterKept=raster.stats();
    composition.setEntries(renderer,{});check(bridge.park(renderer),"Once the composition dropped it the Shelf parks");
    const auto emptied=bridge.stats();
    check(emptied.parts==0&&emptied.surfaces==0&&emptied.pixelBytes==0&&emptied.parks==1&&bridge.entries().empty()&&raster.stats().entries<rasterKept.entries&&raster.stats().resourceBytes+composed.pixelBytes<=rasterKept.resourceBytes&&renderer.stats().textures==0&&renderer.stats().meshes==0,
          "Parking releases every part, its rasters and its GPU resources");
    check(!bridge.park(renderer)&&bridge.stats().parks==1,"Parking again releases nothing");
    check(bridge.syncContent(bindings,2),"The next content event rebuilds every part");bridge.updatePose(pose);bridge.uploadAnimations(renderer);composition.setEntries(renderer,bridge.entries());composition.present(renderer);check(bridge.collectRetired(renderer),"Nothing retired after a rebuild");renderer.draw(false);
    check(bridge.stats().parts==composed.parts&&bridge.stats().surfaces==composed.surfaces&&bridge.stats().pixelBytes==composed.pixelBytes&&renderer.readback().pixels==beforePark.pixels,"The rebuilt Shelf is pixel-identical");
    check(renderer.stats().textureUploads>uploads.textureUploads,"Test exercises real changed local raster uploads, not an empty bridge");composition.detach(renderer);check(bridge.releaseResources(renderer)&&renderer.stats().meshes==0&&renderer.stats().textures==0,"All owned resources released after composition detach");renderer.reset();
}
#endif
}
#ifdef _WIN32
int wmain(int argc,wchar_t**argv){const HRESULT hr=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);try{// argv[2] (the measured CA reveal samples) is still supplied by CMakeLists.txt; the reveal no longer reads it.
    check(SUCCEEDED(hr)&&(argc==2||argc==3),"Pass the HLSL path to the hidden owned Shelf fixture");portable();native(argv[1]);CoUninitialize();std::cout<<"PASS "<<checks<<" Shelf scene checks\n";return 0;}catch(const std::exception&e){counting=false;if(SUCCEEDED(hr))CoUninitialize();std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}}
#else
int main(){try{portable();std::cout<<"PASS "<<checks<<" Shelf scene preparation checks\n";return 0;}catch(const std::exception&e){counting=false;std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}}
#endif
