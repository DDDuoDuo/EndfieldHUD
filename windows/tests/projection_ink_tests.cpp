#include "native/projection_ink.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "native/layer_scene.hpp"
#endif
namespace {std::atomic<bool>counting{};std::atomic<std::size_t>allocations{};unsigned checks{};
void check(bool b,const char*m){++checks;if(!b)throw std::runtime_error(m);}}
void*operator new(std::size_t n){if(counting)++allocations;if(auto*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void*operator new[](std::size_t n){return ::operator new(n);}void operator delete(void*p)noexcept{std::free(p);}void operator delete[](void*p)noexcept{std::free(p);}
#if defined(__cpp_sized_deallocation)
void operator delete(void*p,std::size_t)noexcept{std::free(p);}void operator delete[](void*p,std::size_t)noexcept{std::free(p);}
#endif
namespace n=endfield::native;namespace m=endfield::modules;namespace c=endfield::core;using J=ehud::data::Json;
void portable(){n::ProjectionInkPlan plan;std::vector<m::DrawingStroke>strokes;
    check(plan.update(strokes,1,nullptr,0,{1024,768},1)&&plan.changes().empty()&&plan.maximumTilesPerPlane()==12,"Empty full-screen workspace allocates no tile artwork");
    strokes.push_back({{{.08,.10},{.15,.15}},8,{1,0,0,1}});check(plan.update(strokes,2,nullptr,0,{1024,768},1)&&plan.completedTiles()==1,"Short source path touches one tile");
    const auto first=plan.changes().front();check(first.index==0&&!first.live&&!first.empty&&first.bounds==c::Rect{0,0,256,256},"Tiles retain exact physical pixel bounds");const auto tree=plan.content(first,"tile");check(tree["masksToBounds"].boolean()&&tree["children"].array().size()==1,"Source stroke group clips to tile only");const auto&shape=tree["children"].array()[0]["shape"];check(shape["lineCap"].string()=="round"&&shape["lineJoin"].string()=="round"&&shape["lineWidth"].number()==8,"Source round joins/caps/width retained");
    strokes.push_back({{{.80,.80},{.90,.85}},7,{0,0,1,1}});plan.update(strokes,3,nullptr,0,{1024,768},1);check(plan.changes().size()==1&&plan.changes()[0].index==11&&plan.completedTiles()==2,"Appending disjoint completed stroke never redraws earlier tile");strokes.erase(strokes.begin());plan.update(strokes,4,nullptr,0,{1024,768},1);check(plan.changes().size()==1&&plan.changes()[0].index==0&&plan.changes()[0].empty&&plan.completedTiles()==1,"Erasing retires only removed stroke tile");
    m::DrawingStroke live{{{.12,.45},{.40,.45}},5,{0,1,0,1}};plan.update(strokes,4,&live,1,{1024,768},1);check(plan.liveTiles()==2,"Live crossing path creates two separate tiles");live.points.push_back({.45,.46});plan.update(strokes,4,&live,2,{1024,768},1);check(plan.changes().size()==2&&plan.changes()[0].live&&plan.changes()[1].live,"Live tail refresh does not touch completed artwork");
    // Once the current segment begins inside its new tile, old live tiles stop repainting.
    live.points.push_back({.48,.46});plan.update(strokes,4,&live,3,{1024,768},1);check(plan.changes().size()==1&&plan.changes()[0].index==5,"Live suffix updates only intersecting retained tile");
    allocations=0;counting=true;for(unsigned k=0;k<1000;++k)plan.update(strokes,4,&live,3,{1024,768},1);counting=false;check(allocations==0&&plan.changes().empty(),"1000 unchanged pointer/pose frames allocate no objects or tile work");
    plan.update(strokes,4,nullptr,4,{1024,768},1);check(plan.changes().size()==2&&plan.liveTiles()==0&&plan.changes()[0].empty&&plan.changes()[1].empty,"Committing/ending gesture releases obsolete live tile coverage");
    plan.update(strokes,4,nullptr,4,{1001,701},1.5);check(plan.maximumTilesPerPlane()==30,"Fractional host scale uses exact ceil physical dimensions");for(const auto&change:plan.changes())if(!change.empty){check(std::ceil(change.bounds.width*1.5)<=256&&std::ceil(change.bounds.height*1.5)<=256,"Every edge tile stays within its actual physical bound");}
    bool rejected{};try{plan.update(strokes,4,nullptr,0,{8192,8192},1);}catch(const std::invalid_argument&){rejected=true;}check(rejected&&plan.workspace()==c::Point{1001,701},"Existing renderer viewport limit rejects before changing state");
    const auto retainedTiles=plan.completedTiles();auto broken=strokes;broken[0].points[0].x=std::numeric_limits<double>::quiet_NaN();rejected=false;try{plan.update(broken,5,nullptr,0,{1001,701},1.5);}catch(const std::invalid_argument&){rejected=true;}check(rejected&&plan.completedTiles()==retainedTiles,"Invalid source geometry retains previous sparse state");
    n::ProjectionInkPlan diagonal;m::DrawingStroke line{{{0,0},{1,1}},1,{1,1,1,1}};diagonal.update(std::span(&line,1),1,nullptr,0,{4096,4096},1);check(diagonal.completedTiles()<64&&diagonal.maximumTilesPerPlane()==256,"Long diagonal allocates touched tiles instead of bounding-box viewport");
    // Incremental planning (live RC: every pointer event painted the live
    // stroke's tiles again whole, with every segment). A growing stroke
    // changes only the pixels its newest segments reach; a committed or
    // erased stroke only its footprint; a new geometry repaints whole.
    {n::ProjectionInkPlan incremental;std::vector<m::DrawingStroke>none;m::DrawingStroke growing{{{.10,.10},{.11,.10}},6,{1,1,1,1}};
     incremental.update(none,1,&growing,1,{1024,768},1.5);check(incremental.changes().size()==1&&incremental.changes()[0].live&&!incremental.changes()[0].whole,"A new live stroke paints only its own pixels of a fresh tile");
     for(unsigned k=0;k<30;++k){growing.points.push_back({.11+.004*k,.10+.002*k});incremental.update(none,1,&growing,2+k,{1024,768},1.5);
        for(const auto&c:incremental.changes()){const auto tile=incremental.tilePixels(c.index);
            check(c.live&&!c.empty&&!c.whole&&c.region.width&&c.region.height&&c.region.x+c.region.width<=tile.width&&c.region.y+c.region.height<=tile.height&&std::size_t(c.region.width)*c.region.height<std::size_t(tile.width)*tile.height/8,"A growing stroke changes a small region of its tile, never the whole tile");}}
     const auto change=incremental.changes().front();const auto tree=incremental.regionContent(change,"region");const auto&children=tree["children"].array();
     check(tree["masksToBounds"].boolean()&&std::abs(tree["bounds"].array()[2].number()-change.region.width/1.5)<1e-9&&std::abs(tree["bounds"].array()[3].number()-change.region.height/1.5)<1e-9&&children.size()==1,"A region's content is the region alone, at physical-pixel bounds");
     const auto path=children[0]["shape"]["path"].array();check(path.size()>=2&&path.size()<growing.points.size(),"A region draws only the segments that reach it");
     for(const auto&command:path){const auto at=command["points"].array()[0].array();check(at[0].number()>-16&&at[0].number()<change.region.width/1.5+16&&at[1].number()>-16&&at[1].number()<change.region.height/1.5+16,"Region paths are local to the region's top-left corner (segments reaching it, half a brush and a segment around it)");}
     std::vector<m::DrawingStroke>committed{growing};incremental.update(committed,2,nullptr,40,{1024,768},1.5);std::size_t region{},retired{};for(const auto&c:incremental.changes()){if(c.live)retired+=c.empty;else region+=!c.whole&&!c.empty;}
     check(region>0&&retired>0&&std::none_of(incremental.changes().begin(),incremental.changes().end(),[](const auto&c){return !c.live&&c.whole;}),"Committing paints the stroke's footprint into completed tiles and retires the live ones");
     committed.push_back({{{.12,.11},{.14,.12}},4,{1,0,0,1}});incremental.update(committed,3,nullptr,40,{1024,768},1.5);
     check(!incremental.changes().empty()&&std::all_of(incremental.changes().begin(),incremental.changes().end(),[](const auto&c){return !c.live&&!c.whole;}),"Another stroke paints only its footprint over the earlier one");
     committed.erase(committed.begin());incremental.update(committed,4,nullptr,40,{1024,768},1.5);
     check(!incremental.changes().empty()&&std::all_of(incremental.changes().begin(),incremental.changes().end(),[](const auto&c){return c.empty||!c.whole;}),"Erasing repaints only the erased stroke's footprint");
     incremental.update(committed,4,nullptr,40,{1000,700},1.5);check(!incremental.changes().empty()&&std::all_of(incremental.changes().begin(),incremental.changes().end(),[](const auto&c){return c.empty||c.whole;}),"A resized workspace repaints whole tiles");}
    n::ProjectionInkPlan point;m::DrawingStroke dot{{{.5,.5}},8,{1,1,1,1}};point.update(std::span(&dot,1),1,nullptr,0,{100,100},1);const auto dotTree=point.content(point.changes()[0],"dot");const auto path=dotTree["children"].array()[0]["shape"]["path"].array();check(path.size()==2&&std::abs(path[1]["points"].array()[0].array()[0].number()-50.01)<1e-12,"Single source point remains a0.01-point round-cap segment");
}
#ifdef _WIN32
namespace {
struct Window{HWND h{};ATOM a{};Window(){WNDCLASSW cl{};cl.lpfnWndProc=DefWindowProcW;cl.hInstance=GetModuleHandleW(nullptr);cl.lpszClassName=L"OwnedProjectionInkFixture";a=RegisterClassW(&cl);check(a!=0,"Register isolated Projection ink fixture");h=CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP|WS_EX_TOOLWINDOW,cl.lpszClassName,L"Hidden synthetic ink",WS_POPUP,0,0,512,512,nullptr,nullptr,cl.hInstance,nullptr);check(h&&!IsWindowVisible(h),"Projection fixture stays hidden");}~Window(){if(h)DestroyWindow(h);if(a)UnregisterClassW(reinterpret_cast<LPCWSTR>(static_cast<ULONG_PTR>(a)),GetModuleHandleW(nullptr));}};
J original(std::span<const m::DrawingStroke>strokes){J::Array children;unsigned id{};for(const auto&s:strokes){J::Array path;const auto points=m::NotesDrawing::path(s,{512,512});for(unsigned k=0;k<points.size();++k)path.push_back(J::Object{{"op",k?"line":"move"},{"points",J::Array{J(J::Array{points[k].x,points[k].y})}}});children.push_back(J::Object{{"id",std::to_string(id++)},{"kind","shape"},{"bounds",J::Array{0,0,512,512}},{"position",J::Array{0,0}},{"anchorPoint",J::Array{0,0}},{"shape",J::Object{{"path",std::move(path)},{"fillColor",J{}},{"strokeColor",J::Object{{"sRGB",J::Array{s.color[0],s.color[1],s.color[2],s.color[3]}}}},{"lineWidth",s.width},{"lineCap","round"},{"lineJoin","round"}}}});}return J::Object{{"id","full-source-strokes"},{"bounds",J::Array{0,0,512,512}},{"masksToBounds",true},{"position",J::Array{0,0}},{"anchorPoint",J::Array{0,0}},{"children",std::move(children)}};}
void native(const std::filesystem::path&shader){Window w;n::Renderer renderer;renderer.initialize(w.h,512,512,{n::Driver::warpForTests,shader,n::RenderTarget::offscreenForTests});n::LayerRasterizer raster;n::LayerRasterOptions options;options.pixelsPerPoint=1;options.paddingPoints=0;n::NativeProjectionInk ink(raster,options);n::LayerComposition composition;std::vector<m::DrawingStroke>strokes{{{{.05,.1},{.48,.48},{.95,.65}},13,{1,.2,.1,.7}},{{{.45,.2},{.50,.6},{.65,.92}},5,{.1,.3,1,.5}}};ink.update(strokes,1,nullptr,0,{512,512},1);ink.upload(renderer);composition.setEntries(renderer,ink.entries());composition.present(renderer);renderer.setCamera(n::layerViewportProjection(512,512));renderer.draw(false);auto tiled=renderer.readback();check(ink.stats().completedTiles==4&&raster.stats().resourceBytes<=512*512*4,"Sparse tiles use no more than completed physical viewport pixels");
    composition.detach(renderer);n::LayerScene full(raster);full.load(original(strokes),options);std::array<n::LayerScene*,1>one{&full};composition.setScenes(renderer,one);composition.present(renderer);renderer.draw(false);auto expected=renderer.readback();unsigned maxDelta{};std::size_t differing{};for(std::size_t k=0;k<tiled.pixels.size();++k){const auto delta=unsigned(std::abs(int(tiled.pixels[k])-int(expected.pixels[k])));maxDelta=std::max(maxDelta,delta);differing+=delta>2;}check(maxDelta<=2&&differing==0,"Tiled original round strokes match full source paths including tile seams");composition.detach(renderer);check(full.releaseResources(renderer),"Reference surface retires after detach");
    ink.upload(renderer);composition.setEntries(renderer,ink.entries());const auto before=ink.stats();const auto gpu=renderer.stats();allocations=0;counting=true;for(unsigned k=0;k<120;++k){ink.update(strokes,1,nullptr,0,{512,512},1);ink.setPose(c::Matrix4::translation(double(k%2),0),float(.5+double(k%10)*.05));composition.present(renderer);}counting=false;check(allocations==0&&ink.stats().tileRasters==before.tileRasters&&renderer.stats().textureUploads==gpu.textureUploads,"Warm Projection poses do no allocation/raster/upload");
    m::DrawingStroke live{{{.1,.1},{.2,.2}},7,{0,1,0,1}};ink.update(strokes,1,&live,1,{512,512},1);ink.upload(renderer);composition.setEntries(renderer,ink.entries());ink.update(strokes,1,nullptr,2,{512,512},1);const auto residentBefore=renderer.stats().resourceBytes;bool retirementRejected{};try{ink.collectRetired(renderer);}catch(const std::invalid_argument&){retirementRejected=true;}check(ink.stats().retiredTiles==1&&retirementRejected&&renderer.stats().resourceBytes==residentBefore,"Published live tile rejects retirement before mutation");for(unsigned k=0;k<10;++k){ink.update(strokes,1,&live,3+k*2,{512,512},1);ink.update(strokes,1,nullptr,4+k*2,{512,512},1);}check(ink.stats().retiredTiles==1,"Unpublished event bursts reuse one live tile identity");ink.upload(renderer);composition.setEntries(renderer,ink.entries());check(ink.collectRetired(renderer)&&ink.stats().retiredTiles==0,"Publication removes obsolete live resources");composition.detach(renderer);check(ink.releaseResources(renderer)&&renderer.stats().resourceBytes==0,"Projection ink retires all owned resources");
    // Incremental painting (live RC: the brush lagged the pointer). A growing
    // live stroke paints only the pixels of its newest segments into the
    // retained tile textures - no whole-tile raster, no texture creation -
    // and a committed or erased stroke only its own footprint, while the
    // result stays the whole repaint's.
    {const auto draw=[&](n::NativeProjectionInk&owner){owner.upload(renderer);composition.setEntries(renderer,owner.entries());composition.present(renderer);renderer.draw(false);return renderer.readback();};
     const auto same=[&](const n::Readback&a,const n::Readback&b){unsigned maxDelta{};std::size_t differing{};for(std::size_t k=0;k<a.pixels.size();++k){const auto delta=unsigned(std::abs(int(a.pixels[k])-int(b.pixels[k])));maxDelta=std::max(maxDelta,delta);differing+=delta>2;}return a.pixels.size()==b.pixels.size()&&maxDelta<=2&&differing==0;};
     n::NativeProjectionInk grown(raster,options);m::DrawingStroke growing{{{.1,.6}},9,{.2,.8,.3,1}};grown.update(strokes,1,&growing,1,{512,512},1);grown.upload(renderer);
     for(unsigned k=1;k<=40;++k){growing.points.push_back({.1+.02*k,.6+.15*std::sin(k*.3)});const auto inkBefore=grown.stats();const auto gpu=renderer.stats();
        grown.update(strokes,1,&growing,1+k,{512,512},1);grown.upload(renderer);const auto after=grown.stats();
        // Only a tile the stroke enters for the first time creates a texture.
        check(after.tileRasters==inkBefore.tileRasters&&after.patchRasters>inkBefore.patchRasters&&after.patchPixels-inkBefore.patchPixels<256u*256u/2&&renderer.stats().textureUploads-gpu.textureUploads==after.liveTiles-inkBefore.liveTiles&&renderer.stats().textureRegionUploads>gpu.textureRegionUploads,"A growing live stroke paints only its newest segments' pixels, in place");}
     const auto incremental=draw(grown);
     const auto repaint=[&](std::span<const m::DrawingStroke>completed,const m::DrawingStroke*stroke){n::NativeProjectionInk fresh(raster,options);fresh.update(completed,1,stroke,1,{512,512},1);const auto image=draw(fresh);composition.detach(renderer);check(fresh.releaseResources(renderer),"The whole-repaint reference retires");return image;};
     check(same(incremental,repaint(strokes,&growing)),"Painting a live stroke segment by segment equals painting it whole");
     // Commit: the completed plane paints only the new stroke's footprint.
     auto committed=strokes;committed.push_back(growing);const auto beforeCommit=grown.stats();grown.update(committed,2,nullptr,100,{512,512},1);
     check(grown.stats().tileRasters==beforeCommit.tileRasters&&grown.stats().patchRasters>beforeCommit.patchRasters,"A committed stroke paints only its own footprint");
     check(same(draw(grown),repaint(committed,nullptr)),"The committed stroke equals the whole repaint");
     // Erasing: the removed stroke's footprint is painted again from what remains.
     auto erased=committed;erased.erase(erased.begin());grown.update(erased,3,nullptr,100,{512,512},1);
     check(same(draw(grown),repaint(erased,nullptr)),"An erased stroke's footprint equals the whole repaint");
     composition.detach(renderer);check(grown.releaseResources(renderer)&&renderer.stats().resourceBytes==0,"Incremental ink retires all owned resources");}
    check(!IsWindowVisible(w.h)&&renderer.stats().presents==0,"No live app/capture/user data in fixture");}
}
int wmain(int argc,wchar_t**argv){const auto hr=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);if(FAILED(hr))return 1;int result{};try{portable();check(argc==2,"Pass hud.hlsl");native(argv[1]);std::cout<<"PASS "<<checks<<" Projection sparse ink checks\n";}catch(const std::exception&e){counting=false;std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';result=1;}CoUninitialize();return result;}
#else
int main(){try{portable();std::cout<<"PASS "<<checks<<" Projection sparse ink checks\n";}catch(const std::exception&e){counting=false;std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}}
#endif
