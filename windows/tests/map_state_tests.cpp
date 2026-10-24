#include "modules/map_state.hpp"
#include "core/data/json.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>

namespace {std::atomic<std::uint64_t>allocations{};}
void*operator new(std::size_t n){allocations.fetch_add(1,std::memory_order_relaxed);if(auto*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void*p)noexcept{std::free(p);}void*operator new[](std::size_t n){return ::operator new(n);}void operator delete[](void*p)noexcept{::operator delete(p);}
#ifdef __cpp_sized_deallocation
void operator delete(void*p,std::size_t)noexcept{std::free(p);}void operator delete[](void*p,std::size_t)noexcept{std::free(p);}
#endif
namespace m=endfield::modules;namespace c=endfield::core;using J=ehud::data::Json;
namespace {
unsigned checks{};
void check(bool value,const char*message){++checks;if(!value)throw std::runtime_error(message);}
template<class F>void rejects(F&&f,const char*message){bool thrown{};try{f();}catch(const std::exception&){thrown=true;}check(thrown,message);}
bool closeEnough(double a,double b){return std::abs(a-b)<=2e-12*std::max({1.,std::abs(a),std::abs(b)});}
double number(const J&v){return v.isString()?std::numeric_limits<double>::quiet_NaN():v.number();}
c::Point point(const J&v){return {number(v.array()[0]),number(v.array()[1])};}
m::MapViewport viewport(const J&v){return {number(v.array()[0]),number(v.array()[1]),number(v.array()[2])};}
void equal(m::MapViewport a,m::MapViewport b){check(closeEnough(a.centerX,b.centerX)&&closeEnough(a.centerY,b.centerY)&&closeEnough(a.zoom,b.zoom),"Map camera matches unchanged original Swift");}
void equal(c::Point a,c::Point b){check(closeEnough(a.x,b.x)&&closeEnough(a.y,b.y),"Map projection matches unchanged original Swift");}
std::string id(unsigned n){char bytes[37];std::snprintf(bytes,sizeof bytes,"00000000-0000-4000-8000-%012u",n);return bytes;}
void source(const char*path){std::ifstream file(path);check(bool(file),"Explicit original map fixture exists");const std::string bytes((std::istreambuf_iterator<char>(file)),{});const auto json=J::parse(bytes,256*1024);check(json["authority"].string().size()==40,"Map source authority pinned");
    for(const auto&r:json["geometry"].array()){const auto v=m::mapConstrained(viewport(r["camera"]));equal(v,viewport(r["constrained"]));if(r.contains("world")){const auto w=point(r["world"]);equal(m::mapScreen(w.x,w.y,v),point(r["screen"]));}check(m::mapCoordinateDescription(v.centerX,v.centerY)==r["description"].string(),"Coordinates preserve source precision, hemispheres and spacing");}
}
struct Store {
    m::MapSnapshot data;unsigned writes{},serial{1};bool fails{};
    m::MapPersistence callbacks(){return {[this](const m::MapSnapshot&v){if(fails)throw std::runtime_error("Synthetic CAS failure");data=v;++writes;},[this]{return id(serial++);},[]{return 800000000.;}};}
};
using G=m::MapSurfaceGeometry;
// WorldMapSurface (WorldMapSurface.swift:262-335): presses, pans and camera commits in map px.
void gestures(){Store store;m::MapState state(store.data,store.callbacks());check(state.camera().zoom==3&&!state.active(),"Starts inactive at the original Shenzhen 3x camera");check(closeEnough(state.camera().centerX,(114.0579+180)/360)&&closeEnough(state.camera().centerY,(90-22.5431)/180),"Initial camera is the archive default");
    state.setActive(true);state.beginDrag(G::center);check(state.dragging(),"A press begins a drag");state.drag({G::center.x+1.5,G::center.y+1.5});check(state.camera()==G::constrained({})&&store.writes==0,"Three px (Manhattan) do not pan");state.drag({G::center.x+2,G::center.y+1.5});check(state.camera()!=G::constrained({}),"Past three px Manhattan the press pans, live");
    const auto before=allocations.load();for(unsigned k=0;k<1000;++k)(void)state.drag({434+double(k)*.01,431+double(k)*.02});check(allocations.load()==before,"Warm pointer drag copies no pin payload and allocates nothing");check(store.writes==0,"Drag never persists per event");
    const auto dragged=state.camera();m::MapChange pan;check(!state.endDrag(&pan)&&store.writes==1&&store.data.viewport==dragged&&!state.dragging(),"Release after a pan saves exactly one latest camera and is no click");check(!state.endGesture().changed&&store.writes==1,"An identical gesture end saves nothing");
    state.beginDrag({500,400});const auto click=state.endDrag();check(click&&click->x==500&&click->y==400,"A press that did not move is a click at its start");
    const auto current=state.camera();auto change=state.recenter({500,400});check(change.recentered&&store.writes==2&&state.camera().zoom==current.zoom&&state.camera()!=current,"A recenter commits first and keeps zoom");
    store.fails=true;const auto moved=state.camera();change=state.recenter({560,380});check(!change.recentered&&state.camera()==moved&&change.error=="Synthetic CAS failure","A failed recenter never claims success");
    state.beginDrag({430,430});(void)state.drag({460,450});const auto unsaved=state.camera();change={};(void)state.endDrag(&change);check(unsaved!=moved&&state.camera()==unsaved&&store.data.viewport==moved&&change.error,"A failed pan save keeps the visible camera and the old record");store.fails=false;(void)state.endGesture();check(store.data.viewport==unsaved,"The next gesture end retries the unsaved camera");
    state.beginDrag({430,430});(void)state.drag({470,440});const auto atHide=state.camera();(void)state.setActive(false);check(!state.dragging()&&store.data.viewport==atHide,"Deactivating cancels the press and saves the moved pan");
    state.setActive(true);state.beginDrag({430,430});(void)state.cancelDrag();check(!state.dragging()&&store.data.viewport==atHide,"A cancelled click neither pans nor recenters");
    (void)state.zoom(G::center,1.5,true);check(state.cameraAnimated(),"An animated zoom asks for the eased camera");state.setReducedMotion(true);(void)state.zoom(G::center,1/1.5,true);check(!state.cameraAnimated(),"Reduce Motion: no camera animation");
    const auto writes=store.writes;change=state.reset();check(state.camera()==G::constrained({})&&store.writes==writes+1&&change.changed,"Reset restores the default zoom and centre and saves it");
    check(!state.canZoomOut()==false&&state.canZoomIn(),"Both directions are open at 3x");for(int n=0;n<8;++n)(void)state.zoom(G::center,1/1.5);check(state.camera().zoom==1&&!state.canZoomOut()&&state.camera().centerY==.5,"The whole world at 1x pins the poles to the square's centre");
    const auto revision=state.gestureRevision();(void)state.endGesture();check(state.gestureRevision()==revision+1,"Every gesture end asks the raster for its final detail");
    m::MapState readonly;readonly.setActive(true);check(!readonly.editable()&&!readonly.recenter({500,400}).changed&&readonly.camera()==G::constrained({}),"Without a store nothing recenters");}
void pins(){Store store;m::MapState state({},store.callbacks());state.setActive(true);auto change=state.addPin(G::center);check(change.pinsChanged&&state.pins().size()==1&&state.selection()==id(1)&&store.writes==1,"addPin places a yellow pin and selects it");
    const auto original=state.pins()[0];check(state.pinAt({G::center.x+15,G::center.y})&&!state.pinAt({G::center.x+15.5,G::center.y}),"Round pins hit within 9 x unitScale");
    change=state.activatePin(original.id);check(change.styleChanged==m::MapPinStyle::green&&state.pins()[0].style==m::MapPinStyle::green&&state.styledPin()==original.id,"Activating the selected pin cycles its style");check(state.pins()[0].id==original.id&&state.pins()[0].createdAt==original.createdAt&&state.pins()[0].x==original.x,"A style edit keeps identity, time and place");
    (void)state.activatePin(original.id);check(state.pins()[0].style==m::MapPinStyle::player&&state.pinAt({G::center.x+22,G::center.y}),"The player arrow hits within 13 x unitScale");
    (void)state.deselect();change=state.activatePin(original.id);check(!change.styleChanged&&state.selection()==original.id&&state.pins()[0].style==m::MapPinStyle::player,"Activating an unselected pin selects it without cycling");
    (void)state.addPin({G::center.x+10,G::center.y});check(state.pinAt({G::center.x+9,G::center.y})->id==state.pins()[1].id,"The nearest pin within its radius wins");
    check(!state.addPin({G::center.x,-2000}).changed&&state.pins().size()==2,"No pin beyond the poles");
    store.fails=true;change=state.removePin(original.id);check(state.pins().size()==2&&change.error,"A failed removal keeps the pin");store.fails=false;(void)state.activatePin(original.id);change=state.removePin(original.id);check(state.pins().size()==1&&!state.selection()&&change.pinsChanged,"Removing the selected pin clears the selection");
    while(state.pins().size()<128)(void)state.addPin(G::center);const auto writes=store.writes;change=state.addPin(G::center);check(state.pins().size()==128&&store.writes==writes&&change.error,"The 128-pin limit refuses with the store's message");
    const auto savedPins=state.persisted().pins;state.beginDrag({500,500});const auto allocated=allocations.load();for(unsigned k=0;k<1000;++k)(void)state.drag({504+double(k)*.01,505+double(k)*.02});check(allocations.load()==allocated&&store.writes==writes,"Dragging a full 128-pin map neither copies metadata nor persists");(void)state.endDrag();check(store.writes==writes+1&&state.persisted().pins==savedPins,"A full-pin pan saves only the camera");
    m::MapState readonly(store.data);readonly.setActive(true);check(!readonly.addPin(G::center).changed&&!readonly.removePin(readonly.pins()[0].id).changed&&readonly.pins().size()==128,"Read-only input never changes data");
}
void archives(){m::MapSnapshot current;current.pins.push_back({id(1),.25,.75,42,m::MapPinStyle::player});auto old=current;old.viewport.zoom=16;check(m::migrateMapSnapshot(old,1).viewport==m::MapViewport{},"Version1 camera migrates exactly once");old.viewport=m::MapViewport{};old.viewport.zoom=24;check(m::migrateMapSnapshot(old,2).viewport.zoom==3,"Untouched version2 default migrates");old.viewport.zoom=72;check(m::migrateMapSnapshot(old,3).viewport.zoom==3,"Untouched version3 default migrates");old.viewport.centerX=.2;check(m::migrateMapSnapshot(old,3).viewport==old.viewport,"Custom version3 camera preserved");old.viewport=m::MapViewport{};old.viewport.zoom=72;check(m::migrateMapSnapshot(old,4)==old,"User-selected historical default in version4 is never reset");check(m::migrateMapSnapshot(current,4).pins==current.pins,"Migration retains pin styles, time and coordinates");rejects([&]{m::migrateMapSnapshot(current,5);},"Future archive rejected");auto malformed=current;malformed.pins.push_back(current.pins[0]);rejects([&]{m::validateMapSnapshot(malformed);},"Duplicate UUID rejected");malformed=current;malformed.pins[0].x=1;rejects([&]{m::validateMapSnapshot(malformed);},"Archive seam must use canonical wrapped coordinate");malformed=current;malformed.viewport.zoom=.5;rejects([&]{m::validateMapSnapshot(malformed);},"The archive zoom stays within the Mac 1.3.0 range [1, 128]");malformed.viewport.zoom=1;m::validateMapSnapshot(malformed);check(m::mapWrappedX(-std::numeric_limits<double>::denorm_min())==0,"Tiny negative seam canonicalizes rounded one to zero");
}
}
int main(int argc,char**argv){try{check(argc==2,"Pass original Swift map fixture");source(argv[1]);gestures();pins();archives();std::cout<<"PASS "<<checks<<" Map source/state checks\n";return 0;}catch(const std::exception&e){std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}}
