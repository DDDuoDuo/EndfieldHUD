#include "modules/map_state.hpp"
#include "core/data/data_store.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>

// Preserve the original Swift multiply/add boundaries at exact map hit edges.
#if defined(_MSC_VER)
#pragma float_control(precise,on,push)
#pragma fp_contract(off)
#elif defined(__clang__)
#pragma clang fp contract(off)
#elif defined(__GNUC__)
#pragma GCC push_options
#pragma GCC optimize("fp-contract=off")
#endif

namespace endfield::modules {
namespace {
using P=core::Point;using G=MapSurfaceGeometry;
void need(bool value,const char*message){if(!value)throw std::invalid_argument(message);}
bool finite(P p){return std::isfinite(p.x)&&std::isfinite(p.y);}
std::string canonical(std::string value){for(auto&c:value)if(c>='a'&&c<='f')c=char(c-'a'+'A');return value;}
bool sameUUID(std::string_view a,std::string_view b){if(a.size()!=b.size())return false;for(std::size_t i=0;i<a.size();++i){auto x=a[i],y=b[i];if(x>='a'&&x<='f')x=char(x-'a'+'A');if(y>='a'&&y<='f')y=char(y-'a'+'A');if(x!=y)return false;}return true;}
void validatePins(std::span<const MapPin>pins){
    need(pins.size()<=MapState::maximumPins,"The map can hold up to 128 pins. Remove a pin before adding another.");
    for(std::size_t i=0;i<pins.size();++i){const auto&p=pins[i];
        need(ehud::data::validUUID(p.id)&&std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.createdAt)&&p.x>=0&&p.x<1&&p.y>=0&&p.y<=1,"The saved map could not be read. The original data has been preserved.");
        need(p.style==MapPinStyle::yellow||p.style==MapPinStyle::green||p.style==MapPinStyle::player,"Invalid map pin style");
        for(std::size_t j=0;j<i;++j)need(!sameUUID(pins[j].id,p.id),"Repeated map pin UUID");
    }
}
}
MapPinStyle nextMapPinStyle(MapPinStyle s)noexcept{return s==MapPinStyle::yellow?MapPinStyle::green:s==MapPinStyle::green?MapPinStyle::player:MapPinStyle::yellow;}
double mapWrappedX(double x)noexcept{const auto r=std::fmod(x,1.);const auto v=r<0?r+1:r;return v>=1?0:v;}
MapViewport mapNormalized(MapViewport v){need(std::isfinite(v.centerX)&&std::isfinite(v.centerY)&&std::isfinite(v.zoom),"The map position is invalid.");return {mapWrappedX(v.centerX),std::clamp(v.centerY,0.,1.),std::clamp(v.zoom,MapViewport::minimumZoom,MapViewport::maximumZoom)};}
MapViewport mapConstrained(MapViewport v)noexcept{if(!std::isfinite(v.centerX)||!std::isfinite(v.centerY)||!std::isfinite(v.zoom))v={};else{v.centerX=mapWrappedX(v.centerX);v.centerY=std::clamp(v.centerY,0.,1.);v.zoom=std::clamp(v.zoom,MapViewport::minimumZoom,MapViewport::maximumZoom);}const auto half=std::min(.5,220/(220*v.zoom));v.centerY=std::clamp(v.centerY,half,1-half);return v;}
P mapScreen(double x,double y,MapViewport v)noexcept{double dx=x-v.centerX;dx-=std::round(dx);return {220+dx*440*v.zoom,220+(y-v.centerY)*220*v.zoom};}
std::string mapCoordinateDescription(double x,double y){need(std::isfinite(x)&&std::isfinite(y),"Invalid map coordinates");const auto longitude=mapWrappedX(x)*360-180,latitude=90-y*180;std::ostringstream out;out.imbue(std::locale::classic());out<<std::fixed<<std::setprecision(2)<<std::abs(latitude)<<"°"<<(latitude<0?"S":"N")<<"  "<<std::abs(longitude)<<"°"<<(longitude<0?"W":"E");return out.str();}

// WorldMapSurface camera geometry (WorldMapSurface.swift:103-155).
MapViewport G::constrained(MapViewport value)noexcept{
    MapViewport next=std::isfinite(value.centerX)&&std::isfinite(value.centerY)&&std::isfinite(value.zoom)?mapNormalized(value):MapViewport{};
    next.zoom=std::min(MapViewport::maximumZoom,std::max(MapViewport::minimumZoom,next.zoom));
    const double half=(220*unitScale)/unit(next.zoom).height;
    next.centerY=half>=.5?.5:std::min(1-half,std::max(half,next.centerY));
    return next;}
P G::screen(double x,double y,MapViewport camera)noexcept{const auto u=unit(camera.zoom);double dx=x-camera.centerX;dx-=std::round(dx);return {center.x+dx*u.width,center.y+(y-camera.centerY)*u.height};}
P G::world(P point,MapViewport camera)noexcept{const auto u=unit(camera.zoom);return {camera.centerX+(point.x-center.x)/u.width,camera.centerY+(point.y-center.y)/u.height};}
MapViewport G::panned(MapViewport camera,P delta)noexcept{if(!finite(delta))return camera;const auto u=unit(camera.zoom);auto next=camera;next.centerX-=delta.x/u.width;next.centerY-=delta.y/u.height;return constrained(next);}
MapViewport G::zoomed(MapViewport camera,P point,double factor)noexcept{
    if(!finite(point)||!std::isfinite(factor)||factor<=0)return camera;
    const auto anchor=world(point,camera);auto next=camera;
    next.zoom=std::min(MapViewport::maximumZoom,std::max(MapViewport::minimumZoom,camera.zoom*factor));
    const auto u=unit(next.zoom);next.centerX=anchor.x-(point.x-center.x)/u.width;next.centerY=anchor.y-(point.y-center.y)/u.height;
    return constrained(next);}
bool G::contains(P point)noexcept{return contains(point,radius);}
bool G::contains(P point,double limit)noexcept{return finite(point)&&std::hypot(point.x-center.x,point.y-center.y)<=limit;}

void validateMapSnapshot(const MapSnapshot&s){validatePins(s.pins);need(mapNormalized(s.viewport)==s.viewport,"The saved map could not be read. The original data has been preserved.");}
MapSnapshot migrateMapSnapshot(MapSnapshot s,unsigned version){need(version>=1&&version<=4,version>4?"This map was saved by a newer version of EndfieldHUD.":"Invalid map archive version");validatePins(s.pins);if(version==1){const auto&v=s.viewport;need(std::isfinite(v.centerX)&&v.centerX>=0&&v.centerX<1&&std::isfinite(v.centerY)&&v.centerY>=0&&v.centerY<=1&&std::isfinite(v.zoom)&&v.zoom>=1&&v.zoom<=16,"Invalid legacy map camera");s.viewport={};}else{validateMapSnapshot(s);auto old=MapViewport{};old.zoom=version==2?24:72;if(version<4&&s.viewport==old)s.viewport={};}for(auto&p:s.pins)p.id=canonical(std::move(p.id));return s;}

MapState::MapState(MapSnapshot s,MapPersistence p):persisted_(std::move(s)),persistence_(std::move(p)){validateMapSnapshot(persisted_);for(auto&pin:persisted_.pins)pin.id=canonical(std::move(pin.id));need(!persistence_.commit||(persistence_.newID&&persistence_.foundationNow),"Editable map needs injected UUID/date factories");camera_=G::constrained(persisted_.viewport);}
const MapPin*MapState::selectedPin()const noexcept{if(!selected_)return nullptr;for(const auto&p:persisted_.pins)if(p.id==*selected_)return &p;return nullptr;}
// The store's commit-before-publish: memory changes only after the write.
bool MapState::commit(MapSnapshot next,MapChange&change){if(!editable())return false;try{if(next==persisted_)return true;validateMapSnapshot(next);persistence_.commit(next);persisted_=std::move(next);return true;}catch(const std::exception&e){change.error=e.what();return false;}catch(...){change.error="The map could not be saved or opened.";return false;}}
// applyCamera (:411-424): the raster and pins follow; animated cameras ease from what is on screen.
void MapState::apply(MapViewport next,bool animated){camera_=next;cameraAnimated_=animated;++cameraRevision_;++revision_;}
void MapState::pinsChanged(MapChange&change){change.changed=change.pinsChanged=true;++pinRevision_;++revision_;}
MapChange MapState::setActive(bool value){
    MapChange change;if(active_==value)return change;
    if(!value){change=cancelDrag();auto end=endGesture();if(end.error)change.error=end.error;}
    active_=value;++revision_;change.changed=true;return change;}
void MapState::beginDrag(P point){drag_=Drag{point,camera_,false};}
MapChange MapState::drag(P point){
    if(!drag_||!finite(point))return {};
    auto current=*drag_;const P delta{point.x-current.start.x,point.y-current.start.y};
    if(!current.moved&&!(std::abs(delta.x)+std::abs(delta.y)>G::dragThreshold))return {};
    current.moved=true;drag_=current;
    const auto next=G::panned(current.camera,delta);if(next==camera_)return {};
    apply(next,false);return {true};}
std::optional<P>MapState::endDrag(MapChange*out){
    if(!drag_)return std::nullopt;const auto current=*drag_;drag_.reset();
    if(current.moved){auto change=endGesture();if(out)*out=change;return std::nullopt;}
    return current.start;}
MapChange MapState::cancelDrag(){if(!drag_)return {};const auto current=*drag_;drag_.reset();return current.moved?endGesture():MapChange{};}
MapChange MapState::zoom(P point,double factor,bool animated){const auto next=G::zoomed(camera_,point,factor);if(next==camera_)return {};apply(next,animated&&!reduced_);return {true,animated&&!reduced_};}
MapChange MapState::pan(P delta){const auto next=G::panned(camera_,delta);if(next==camera_)return {};apply(next,false);return {true};}
MapChange MapState::reset(){const auto next=G::constrained(MapViewport{});if(next==camera_)return {};apply(next,!reduced_);auto change=endGesture();change.changed=true;change.cameraAnimated=!reduced_;return change;}
MapChange MapState::recenter(P point){
    const auto target=G::world(point,camera_);const auto next=G::constrained({target.x,target.y,camera_.zoom});
    if(next==camera_||!editable())return {};
    MapChange change;auto snapshot=persisted_;
    try{snapshot.viewport=mapNormalized(next);}catch(const std::exception&e){change.error=e.what();return change;}
    if(!commit(std::move(snapshot),change))return change;
    apply(next,!reduced_);++gestureRevision_;change.changed=change.recentered=true;change.cameraAnimated=!reduced_;return change;}
MapChange MapState::endGesture(){
    ++gestureRevision_;MapChange change;if(!editable())return change;
    auto next=persisted_;try{next.viewport=mapNormalized(camera_);}catch(const std::exception&e){change.error=e.what();return change;}
    (void)commit(std::move(next),change);return change;}
const MapPin*MapState::pinAt(P point)const noexcept{
    const MapPin*best{};double distance=INFINITY;
    for(const auto&pin:persisted_.pins){const auto p=G::screen(pin.x,pin.y,camera_);if(!G::contains(p))continue;
        const auto d=std::hypot(p.x-point.x,p.y-point.y);if(d<G::pinHitRadius(pin.style)&&d<distance){best=&pin;distance=d;}}
    return best;}
MapChange MapState::addPin(P point){
    MapChange change;if(!editable())return change;
    const auto location=G::world(point,camera_);if(!(location.y>=0&&location.y<=1))return change;
    try{
        need(std::isfinite(location.x)&&std::isfinite(location.y),"The map position is invalid.");
        need(persisted_.pins.size()<maximumPins,"The map can hold up to 128 pins. Remove a pin before adding another.");
        MapPin pin{canonical(persistence_.newID()),mapWrappedX(location.x),std::min(1.,std::max(0.,location.y)),persistence_.foundationNow()};
        auto next=persisted_;next.pins.push_back(pin);if(!commit(std::move(next),change))return change;
        selected_=std::move(pin.id);pinsChanged(change);
    }catch(const std::exception&e){change.error=e.what();}
    return change;}
MapChange MapState::removePin(std::string_view id){
    MapChange change;if(!editable())return change;
    const auto found=std::find_if(persisted_.pins.begin(),persisted_.pins.end(),[&](const auto&p){return p.id==id;});if(found==persisted_.pins.end())return change;
    const bool selected=selected_&&*selected_==id;auto next=persisted_;next.pins.erase(next.pins.begin()+(found-persisted_.pins.begin()));
    if(!commit(std::move(next),change))return change;
    if(selected)selected_.reset();pinsChanged(change);return change;}
MapChange MapState::activatePin(std::string_view id){
    MapChange change;
    const auto found=std::find_if(persisted_.pins.begin(),persisted_.pins.end(),[&](const auto&p){return p.id==id;});if(found==persisted_.pins.end())return change;
    if(!selected_||*selected_!=id){selected_=std::string(id);pinsChanged(change);return change;}
    if(!editable())return change;
    auto next=persisted_;auto&pin=next.pins[found-persisted_.pins.begin()];pin.style=nextMapPinStyle(pin.style);const auto style=pin.style;
    if(!commit(std::move(next),change))return change;
    pinsChanged(change);styledPin_=std::string(id);++styleRevision_;change.styleChanged=style;return change;}
MapChange MapState::deselect(){MapChange change;if(!selected_)return change;selected_.reset();pinsChanged(change);return change;}
}

#if defined(_MSC_VER)
#pragma float_control(pop)
#elif defined(__GNUC__) && !defined(__clang__)
#pragma GCC pop_options
#endif
