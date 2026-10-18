// Incremental ink planning of a Notes drawing card (modules/notes_drawing_ink):
// a growing live stroke plans only its newest segments, a committed stroke is
// adopted where the live one was painted, an erased stroke repaints only its
// footprint, a resized card repaints whole, and sampling allocates nothing.
// The pixels themselves are compared with a whole repaint in
// notes_drawing_scene_tests (Windows).
#include "modules/notes_drawing_ink.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>
namespace {std::atomic<bool>counting{};std::atomic<std::size_t>allocations{};unsigned checks{};
void check(bool b,const char*m){++checks;if(!b)throw std::runtime_error(m);}}
void*operator new(std::size_t n){if(counting)++allocations;if(auto*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void*operator new[](std::size_t n){return ::operator new(n);}void operator delete(void*p)noexcept{std::free(p);}void operator delete[](void*p)noexcept{std::free(p);}
#if defined(__cpp_sized_deallocation)
void operator delete(void*p,std::size_t)noexcept{std::free(p);}void operator delete[](void*p,std::size_t)noexcept{std::free(p);}
#endif
namespace m=endfield::modules;namespace c=endfield::core;using J=ehud::data::Json;
namespace {
std::uint64_t area(std::span<const m::NotesInkPixels>rects){std::uint64_t total{};for(const auto&r:rects)total+=std::uint64_t(r.width)*r.height;return total;}
bool within(const m::NotesInkPixels&r,const m::NotesInkPixels&picture){return r.width&&r.height&&r.x+r.width<=picture.width&&r.y+r.height<=picture.height;}
void planning(){
    // The default 300x240 card's 290x184 viewport at 2 pixels per point.
    m::NotesDrawingInkPlan plan;check(plan.setGeometry({290,184},1,2)&&plan.picture()==m::NotesInkPixels{0,0,580,368}&&plan.whole(),"A new geometry repaints the whole 580x368 picture");
    check(plan.setCompleted({},1)&&!plan.setCompleted({},1),"An equal completed revision is no work");plan.painted();check(!plan.whole()&&plan.dirty().empty(),"Painting clears the pending work");
    // A live stroke growing sample by sample: each step changes only the
    // pixels its newest segments reach, never the whole picture.
    m::DrawingStroke live{{{.1,.5}},8,{.98,.83,.12,1}};check(plan.setLive(&live,1)&&!plan.whole(),"A first sample plans its dot");
    const auto dot=area(plan.dirty());check(dot>0&&dot<40u*40u,"A dot changes a brush-sized region");plan.painted();
    std::uint64_t largest{};for(unsigned k=1;k<=200;++k){live.points.push_back({.1+.004*k,.5+.2*std::sin(k*.05)});check(plan.setLive(&live,1+k),"Every new sample is planned");
        for(const auto&r:plan.dirty())check(within(r,plan.picture()),"Dirty regions stay inside the picture");largest=std::max(largest,area(plan.dirty()));plan.painted();}
    check(largest<std::uint64_t(64)*64,"A growing stroke changes small regions only (its newest segments)");
    check(!plan.setLive(&live,201),"A repeated stroke with no new samples plans nothing");
    // Several samples since the previous frame accumulate into one paint.
    for(unsigned k=0;k<6;++k){live.points.push_back({live.points.back().x+.003,live.points.back().y+.002});plan.setLive(&live,300+k);}
    const auto batch=plan.dirty();check(!batch.empty()&&batch.size()<=2&&area(batch)<std::uint64_t(64)*64,"Six samples of one frame are one or two small regions");plan.painted();
    // Region content: the live stroke's segments reaching the region only,
    // local to the region's top-left corner, at the source width.
    live.points.push_back({live.points.back().x+.003,live.points.back().y});plan.setLive(&live,400);const auto region=plan.dirty()[0];const auto tree=plan.content(region,"region");
    const auto&children=tree["children"].array();check(tree["masksToBounds"].boolean()&&children.size()==1,"A region holds the live stroke alone");
    const auto&shape=children[0]["shape"];check(shape["lineCap"].string()=="round"&&shape["lineJoin"].string()=="round"&&shape["lineWidth"].number()==8,"Source round caps, joins and width");
    const auto&path=shape["path"].array();check(path.size()>=2&&path.size()<live.points.size()/4,"A region draws only the segments that reach it");
    for(const auto&command:path){const auto at=command["points"].array()[0].array();check(at[0].number()>-12&&at[0].number()<region.width/2.+12&&at[1].number()>-12&&at[1].number()<region.height/2.+12,"Region paths are local to the region");}
    plan.painted();
    // Pen-up: the live stroke committed as the last completed stroke is
    // already painted, so the commit plans nothing.
    std::vector<m::DrawingStroke>completed{live};check(plan.setCompleted(completed,2)&&!plan.livePresent()&&plan.dirty().empty()&&plan.stats().adoptions==1,"A committed live stroke is adopted without repainting");
    check(!plan.setLive(nullptr,401)&&plan.dirty().empty(),"Ending the live stroke after its commit changes nothing");
    // A second stroke, then a third crossing it: completed strokes change
    // only their own footprints.
    completed.push_back({{{.2,.2},{.3,.25}},6,{0,0,1,.5}});plan.setCompleted(completed,3);check(!plan.dirty().empty()&&area(plan.dirty())<std::uint64_t(100)*60,"A new completed stroke plans its footprint only");plan.painted();
    // A live stroke that is cancelled (not committed) repaints its footprint
    // without it.
    m::DrawingStroke cancelled{{{.6,.1},{.7,.2}},5,{1,0,0,1}};plan.setLive(&cancelled,500);plan.painted();check(plan.setLive(nullptr,501)&&!plan.dirty().empty()&&area(plan.dirty())<std::uint64_t(100)*100,"A cancelled live stroke repaints only its footprint");plan.painted();
    // Erasing the long stroke: its footprint, painted again from the rest.
    completed.erase(completed.begin());plan.setCompleted(completed,4);const auto erased=area(plan.dirty());check(erased>0&&erased<std::uint64_t(580)*368&&!plan.whole(),"Erasing plans the erased stroke's footprint");
    const auto repaint=plan.content(plan.dirty()[0],"erased");std::size_t strokes{};for(const auto&child:repaint["children"].array())strokes+=child["id"].string().starts_with("stroke.");check(strokes<=1,"The erased stroke is gone from its repaint");plan.painted();
    // A resized card repaints whole.
    check(plan.setGeometry({300,190},1,2)&&plan.whole()&&plan.dirty().empty()&&plan.dirtyPixels()==std::uint64_t(600)*380,"A resized card repaints the whole picture");
    const auto whole=plan.content(plan.picture(),"whole");check(whole["children"].array().size()==completed.size(),"The whole picture holds every completed stroke");plan.painted();
    // Sampling allocates nothing once the stroke's storage is reserved.
    m::DrawingStroke fast{{{.05,.05}},12,{1,1,1,1}};fast.points.reserve(4096);plan.setLive(&fast,600);plan.painted();
    allocations=0;counting=true;for(unsigned k=1;k<=1000;++k){fast.points.push_back({.05+.0009*k,.05+.0008*k});plan.setLive(&fast,600+k);if(k%6==0){(void)plan.dirty();plan.painted();}}counting=false;
    check(allocations==0,"1000 samples (with a paint every six) allocate nothing in the planner");
    // Invalid input rejects before changing anything.
    bool rejected{};try{plan.setGeometry({9000,10},1,1);}catch(const std::invalid_argument&){rejected=true;}check(rejected&&plan.picture().width==600,"Oversized geometry rejects before mutation");
    rejected=false;auto broken=completed;broken[0].points[0].x=2;try{plan.setCompleted(broken,9);}catch(const std::invalid_argument&){rejected=true;}check(rejected&&plan.completedStrokes()==completed.size(),"Invalid strokes keep the planned ones");
}
void large(){
    // A large card at a reduced content scale: widths scale with it.
    m::NotesDrawingInkPlan plan;const double scale=.5;check(plan.setGeometry({1590*scale,944*scale},scale,4),"Bounded-density geometry");
    std::vector<m::DrawingStroke>strokes{{{{.1,.1},{.9,.9}},20,{0,0,1,1}}};plan.setCompleted(strokes,1);const auto tree=plan.content(plan.picture(),"large");check(tree["children"].array()[0]["shape"]["lineWidth"].number()==10,"Stroke widths scale with the content");plan.painted();
    // A long diagonal reaches its own tiles, not the bounding box.
    strokes.push_back({{{.0,.0},{1,1}},1,{1,1,1,1}});plan.setCompleted(strokes,2);check(area(plan.dirty())<std::uint64_t(plan.picture().width)*plan.picture().height/3,"A diagonal's footprint is its tiles' regions, not the picture");
}
}
int main(){try{planning();large();std::cout<<"PASS "<<checks<<" Notes drawing ink planning checks\n";return 0;}catch(const std::exception&e){counting=false;std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}}
