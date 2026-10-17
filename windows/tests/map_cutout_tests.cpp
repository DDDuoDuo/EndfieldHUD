// Desktop Map occlusion (core/map_source_cutout.hpp): the Storage and
// Activity Monitor plates below the circle stay above the Map. Portable,
// no window, renderer, clock or IO beyond the explicit fixture.
#include "core/map_source_cutout.hpp"
#include "core/data/json.hpp"
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>

namespace{std::atomic<bool>counting{};std::atomic<std::size_t>allocations{};}
void*operator new(std::size_t n){if(counting)++allocations;if(auto*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void*operator new[](std::size_t n){return ::operator new(n);}void operator delete(void*p)noexcept{std::free(p);}void operator delete[](void*p)noexcept{std::free(p);}
#if defined(__cpp_sized_deallocation)
void operator delete(void*p,std::size_t)noexcept{std::free(p);}void operator delete[](void*p,std::size_t)noexcept{std::free(p);}
#endif
namespace {
namespace c=endfield::core;namespace s=endfield::core::source;namespace m=endfield::modules;using J=ehud::data::Json;
unsigned checks{};
void check(bool value,const char*what){++checks;if(!value)throw std::runtime_error(what);}
template<class F>void rejects(F f,const char*what){bool rejected{};try{f();}catch(const std::exception&){rejected=true;}check(rejected,what);}
constexpr double W=1280,H=800;
const c::Rect viewport{0,0,W,H};
const s::SourceRect plateRect{{-138,-55},{276,110}}; // the Icon RectTransform (276 x 110, pivot .5)

// The exported source mesh boundaries are the embedded plates, exactly.
void source(const char*path){
    std::ifstream file(path,std::ios::binary);check(bool(file),"The Map cutout source fixture exists");
    const std::string bytes((std::istreambuf_iterator<char>(file)),{});const auto json=J::parse(bytes,256*1024);
    check(json["authority"].string()=="ca04f142185c7de40acd8523bdb563195d90a1d1","The plates come from the pinned Mac authority");
    const auto plates=m::mapSourceSilhouettes();const auto&expected=json["silhouettes"].array();
    check(plates.size()==2&&expected.size()==2,"Exactly the Storage and Activity Monitor plates stay above the Map");
    for(std::size_t i=0;i<plates.size();++i){
        check(plates[i].nodeSuffix==expected[i]["node"].string()&&plates[i].module==expected[i]["module"].string()&&plates[i].sprite==expected[i]["sprite"].string(),"Plate node, module and sprite follow makeDesktopBottomSilhouettes");
        const auto&loops=expected[i]["loops"].array();check(plates[i].loops.size()==loops.size(),"Every boundary loop of the sprite mesh is a plate polygon");
        for(std::size_t k=0;k<loops.size();++k){const auto&loop=loops[k].array();check(plates[i].loops[k].count==loop.size(),"Loop vertex counts match the source mesh boundary");
            for(std::size_t v=0;v<loop.size();++v)check(plates[i].loops[k].points[v].x==loop[v].array()[0].number()&&plates[i].loops[k].points[v].y==loop[v].array()[1].number(),"Embedded plate vertices equal the exported source boundary exactly");}
    }
    check(plates[0].nodeSuffix=="/TechtreeBtn/Icon"&&plates[0].module=="storage"&&plates[1].nodeSuffix=="/ReportBtn/Icon"&&plates[1].module=="activityMonitor","Storage is TechtreeBtn and Activity Monitor is ReportBtn");
}

// A rect-local (Unity y up) -> NDC world that puts the rect's local origin at
// client (x, y) with k points per unit; the camera is the identity.
c::Matrix4 plateWorld(double x,double y,double k){return c::Matrix4::translation(2*x/W-1,1-2*y/H,0)*c::Matrix4::scale(2*k/W,2*k/H,1);}
c::Point expected(double x,double y,double k,c::Point n){return {x+k*(plateRect.origin[0]+n.x*plateRect.size[0]),y-k*(plateRect.origin[1]+n.y*plateRect.size[1])};}
c::Point at(double x,double y,double k,double u,double v){return expected(x,y,k,{u,v});}
struct Frame {
    std::vector<s::Node> nodes;std::vector<s::ResolvedNode> resolved;std::vector<double> alpha;s::CameraFrame camera;
    Frame():nodes(3){
        nodes[0].id="center";nodes[0].path="WatchPanel_PC/Content/WatchNode/canvas_watch/MiddleDecoNode";
        nodes[1].id="storage";nodes[1].path="WatchPanel_PC/Content/WatchNode/canvas_watch/HudBgShdow/TechtreeNode/TechtreeBtn/Icon";
        nodes[2].id="activity";nodes[2].path="WatchPanel_PC/Content/WatchNode/canvas_watch/HudBgShdow/ReportNode/ReportBtn/Icon";
        resolved={{&nodes[0],{},{},s::SourceRect{{-960,-540},{1920,1080}},true},{&nodes[1],{},plateWorld(400,600,.5),plateRect,true},{&nodes[2],{},plateWorld(560,600,.5),plateRect,true}};
        alpha={1,1,1};
    }
    m::MapSourceView view()const{return {resolved,alpha,&camera};}
};

void projection(){
    Frame f;m::MapSourceCutoutPlan plan;
    check(!plan.clear()&&plan.cutouts().empty(),"A fresh plan holds no plates");
    check(plan.update(f.view(),viewport),"The presented source frame projects the plates");
    const auto&cut=plan.cutouts();const auto plates=m::mapSourceSilhouettes();
    check(cut.count==4,"Storage's one loop and Activity Monitor's three loops are cut");
    std::size_t index=0;bool exact=true;
    for(std::size_t p=0;p<2;++p)for(const auto&loop:plates[p].loops){const auto&polygon=cut.polygons[index++];exact=exact&&polygon.count==loop.count;
        for(std::size_t v=0;v<loop.count&&v<polygon.count;++v){const auto e=expected(p?560:400,600,.5,loop.points[v]);exact=exact&&std::abs(polygon.points[v].x-e.x)<1e-9&&std::abs(polygon.points[v].y-e.y)<1e-9;}}
    check(exact,"Each plate vertex is its rect point through worldRoot * node world and projection * view, in client points");
    check(plan.contains(at(400,600,.5,.6,.8))&&plan.contains(at(560,600,.5,.3,.6)),"Presses inside either plate belong to its button");
    check(!plan.contains(at(400,600,.5,.15,.15))&&!plan.contains(at(560,600,.5,.85,.15)),"The plates' transparent corners stay the Map's");
    check(!plan.contains({640,100}),"Points away from the plates are not cut");
    check(!plan.update(f.view(),viewport),"An unchanged frame keeps its plates without projecting again");
    // The entrance and the tilt move the plates: projected again, allocation-free.
    unsigned changes{};allocations=0;counting=true;
    try{for(unsigned k=1;k<=120;++k){f.resolved[1].worldMatrix=plateWorld(400+k%7,600,.5);changes+=plan.update(f.view(),viewport);}}catch(...){counting=false;throw;}
    counting=false;check(allocations==0,"Projecting moving plates allocates nothing");check(changes==120,"Every moved frame projects its plates again");
    f.resolved[1].worldMatrix=plateWorld(420,580,.5);check(plan.update(f.view(),viewport)&&plan.contains(at(420,580,.5,.6,.8))&&!plan.contains(at(400,600,.5,.05,.95)),"A moved plate moves its cut");
    // HUDSourceWatchView skips a hidden plate: inherited alpha .001 or less, inactive or rectless.
    f.alpha[1]=.001;check(plan.update(f.view(),viewport)&&plan.cutouts().count==3&&!plan.contains(at(420,580,.5,.6,.8)),"A faded-out Storage plate leaves the Map whole there");
    f.alpha[1]=.002;check(plan.update(f.view(),viewport)&&plan.cutouts().count==4,"Barely visible plates already cut the Map, as on the Mac");
    f.resolved[2].activeInHierarchy=false;check(plan.update(f.view(),viewport)&&plan.cutouts().count==1,"An inactive Activity Monitor plate is not cut");
    f.resolved[2].activeInHierarchy=true;f.resolved[2].rect.reset();check(!plan.update(f.view(),viewport)&&plan.cutouts().count==1,"A plate without a rect is not cut");
    f.resolved[2].rect=plateRect;check(plan.update(f.view(),viewport)&&plan.cutouts().count==4,"Plates return with their nodes");
    // A loop with any vertex behind the camera is dropped (applySourceMapOcclusion guard).
    auto behind=f;behind.camera.projection.values[15]=-1;m::MapSourceCutoutPlan hidden;check(!hidden.update(behind.view(),viewport)&&hidden.cutouts().empty(),"Plates behind the camera are not cut");
    // Nodes are found by path suffix and kept by identity across frames.
    Frame reordered;std::swap(reordered.resolved[1],reordered.resolved[2]);std::swap(reordered.alpha[1],reordered.alpha[2]);
    m::MapSourceCutoutPlan other;check(other.update(reordered.view(),viewport)&&other.contains(at(400,600,.5,.6,.8))&&other.contains(at(560,600,.5,.3,.6)),"Plates are found by their source paths, not their order");
    Frame missing;missing.resolved.resize(1);m::MapSourceCutoutPlan none;check(!none.update(missing.view(),viewport)&&none.cutouts().empty(),"A frame without the plate nodes cuts nothing");
    allocations=0;counting=true;try{for(unsigned k=0;k<60;++k)(void)none.update(missing.view(),viewport);}catch(...){counting=false;throw;}counting=false;check(allocations==0,"Looking for absent plates allocates nothing");
    check(plan.update({f.resolved,f.alpha,nullptr},viewport)&&plan.cutouts().empty()&&!plan.contains(at(420,580,.5,.6,.8)),"Without a camera no plate is cut");
    check(plan.update(f.view(),viewport)&&plan.clear()&&plan.cutouts().empty()&&!plan.clear(),"Clearing removes the plates once");
}

// The feather mask under the plates: alpha x (1 - even-odd coverage).
void mask(){
    std::vector<std::uint8_t> base(40*40*4);for(std::size_t i=0;i<base.size();i+=4){base[i]=11;base[i+1]=22;base[i+2]=33;base[i+3]=200;}
    m::MapCutouts square;square.count=1;auto&p=square.polygons[0];p.count=4;p.points[0]={5,5};p.points[1]={10.25,5};p.points[2]={10.25,10};p.points[3]={5,10};
    const auto region=m::mapCutoutTexels(square,2,40,40);check((region==m::MapCutoutTexels{9,9,13,12}),"The changed texels are the cut's bounds plus one texel, at two texels per point");
    check(m::mapCutoutTexels(square,2,8,8).empty()&&m::mapCutoutTexels({},2,40,40).empty(),"Cuts beside the mask or no cuts change no texel");
    check((m::mapCutoutUnion(region,{0,30,2,2})==m::MapCutoutTexels{0,9,22,23})&&m::mapCutoutUnion({},region)==region,"Old and new cuts repaint their union");
    std::vector<std::uint8_t> out(std::size_t(region.width)*region.height*4);std::vector<float> row(region.width);
    allocations=0;counting=true;try{m::mapCutoutMask(square,2,base,40,40,region,out,row);}catch(...){counting=false;throw;}counting=false;check(allocations==0,"The mask repaint allocates nothing");
    const auto alpha=[&](unsigned x,unsigned y){return int(out[((y-region.y)*region.width+(x-region.x))*4+3]);};
    check(alpha(15,15)==0&&alpha(10,10)==0&&alpha(19,19)==0,"Texels under a plate lose the Map");
    check(alpha(9,9)==200&&alpha(15,20)==200&&alpha(21,15)==200,"Texels beside a plate keep the feather");
    check(alpha(20,15)==100,"A plate edge through a texel keeps its uncovered share");
    check(out[((15-region.y)*region.width+(15-region.x))*4]==11&&out[((15-region.y)*region.width+(15-region.x))*4+2]==33,"The feather's color channels are kept");
    // Even-odd (SystemHUDView mask.fillRule = .evenOdd): overlapping plates keep the Map in their overlap.
    m::MapCutouts twice=square;twice.count=2;twice.polygons[1].count=4;twice.polygons[1].points[0]={7,5};twice.polygons[1].points[1]={12,5};twice.polygons[1].points[2]={12,10};twice.polygons[1].points[3]={7,10};
    const auto both=m::mapCutoutTexels(twice,2,40,40);out.assign(std::size_t(both.width)*both.height*4,0);row.assign(both.width,0);m::mapCutoutMask(twice,2,base,40,40,both,out,row);
    const auto alpha2=[&](unsigned x,unsigned y){return int(out[((y-both.y)*both.width+(x-both.x))*4+3]);};
    check(alpha2(11,15)==0&&alpha2(17,15)==200&&alpha2(22,15)==0,"Overlapping plates cut the even-odd path");
    rejects([&]{m::mapCutoutMask(square,2,base,40,40,{30,30,20,20},out,row);},"A region beyond the mask is rejected");
    rejects([&]{m::mapCutoutMask(square,2,base,40,40,region,std::span(out).first(4),row);},"A short output is rejected");
    // The module host's transition cut has no feather: an opaque white base.
    out.assign(std::size_t(region.width)*region.height*4,0);row.assign(region.width,0);
    allocations=0;counting=true;try{m::mapCutoutMask(square,2,region,out,row);}catch(...){counting=false;throw;}counting=false;check(allocations==0,"The transition cut repaint allocates nothing");
    check(alpha(15,15)==0&&alpha(9,9)==255&&alpha(21,15)==255&&alpha(20,15)==128,"Without a base the cut keeps full alpha beside the plates and its edge share");
    check(out[((9-region.y)*region.width+(9-region.x))*4]==255&&out[((15-region.y)*region.width+(15-region.x))*4+1]==255,"Its color channels are opaque white");
    rejects([&]{m::mapCutoutMask(square,2,region,std::span(out).first(4),row);},"A short transition cut output is rejected");
}
}
int main(int argc,char**argv){
    try{check(argc==2,"Pass tests/fixtures/map_cutout_source.json");source(argv[1]);projection();mask();std::cout<<"PASS "<<checks<<" Map source cutout checks\n";return 0;}
    catch(const std::exception&e){counting=false;std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}
}
