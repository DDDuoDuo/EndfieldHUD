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
using P=core::Point;using R=core::Rect;
void need(bool value,const char*message){if(!value)throw std::invalid_argument(message);}
bool finite(P p){return std::isfinite(p.x)&&std::isfinite(p.y);}
// CGRect containment excludes maximum edges; edge-only contact does not count
// as intersection. Both were sampled from the unchanged Foundation oracle.
bool contains(R r,P p){return finite(p)&&p.x>=r.x&&p.x<r.x+r.width&&p.y>=r.y&&p.y<r.y+r.height;}
bool intersects(R a,R b){return a.x<b.x+b.width&&b.x<a.x+a.width&&a.y<b.y+b.height&&b.y<a.y+a.height;}
MapChange merge(MapChange a,const MapChange&b){a.handled|=b.handled;a.changed|=b.changed;a.cameraAnimated|=b.cameraAnimated;a.recentered|=b.recentered;if(b.styleChanged)a.styleChanged=b.styleChanged;return a;}
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
P mapWorld(P p,MapViewport v)noexcept{return {v.centerX+(p.x-220)/(440*v.zoom),v.centerY+(p.y-220)/(220*v.zoom)};}
P mapScreen(double x,double y,MapViewport v)noexcept{double dx=x-v.centerX;dx-=std::round(dx);return {220+dx*440*v.zoom,220+(y-v.centerY)*220*v.zoom};}
MapViewport mapPanned(MapViewport v,P delta)noexcept{if(!finite(delta))return v;v.centerX-=delta.x/(440*v.zoom);v.centerY-=delta.y/(220*v.zoom);return mapConstrained(v);}
MapViewport mapZoomed(MapViewport v,P p,double factor)noexcept{if(!finite(p)||!std::isfinite(factor)||factor<=0)return v;const auto anchor=mapWorld(p,v);v.zoom=std::clamp(v.zoom*factor,MapViewport::minimumZoom,MapViewport::maximumZoom);v.centerX=anchor.x-(p.x-220)/(440*v.zoom);v.centerY=anchor.y-(p.y-220)/(220*v.zoom);return mapConstrained(v);}
MapViewport mapRecentered(MapViewport v,P p)noexcept{if(!mapContains(p))return v;const auto point=mapWorld(p,v);return mapConstrained({point.x,point.y,v.zoom});}
bool mapContains(P p)noexcept{return finite(p)&&std::hypot(p.x-220,p.y-220)<=216;}
R mapMarkerHitRect(MapPinStyle style,P p)noexcept{const double radius=style==MapPinStyle::player?13:9;return {p.x-radius,p.y-radius,radius*2,radius*2};}
std::string mapCoordinateDescription(double x,double y){need(std::isfinite(x)&&std::isfinite(y),"Invalid map coordinates");const auto longitude=mapWrappedX(x)*360-180,latitude=90-y*180;std::ostringstream out;out.imbue(std::locale::classic());out<<std::fixed<<std::setprecision(2)<<std::abs(latitude)<<"°"<<(latitude<0?"S":"N")<<"  "<<std::abs(longitude)<<"°"<<(longitude<0?"W":"E");return out.str();}
void validateMapSnapshot(const MapSnapshot&s){validatePins(s.pins);need(mapNormalized(s.viewport)==s.viewport,"The saved map could not be read. The original data has been preserved.");}
MapSnapshot migrateMapSnapshot(MapSnapshot s,unsigned version){need(version>=1&&version<=4,version>4?"This map was saved by a newer version of EndfieldHUD.":"Invalid map archive version");validatePins(s.pins);if(version==1){const auto&v=s.viewport;need(std::isfinite(v.centerX)&&v.centerX>=0&&v.centerX<1&&std::isfinite(v.centerY)&&v.centerY>=0&&v.centerY<=1&&std::isfinite(v.zoom)&&v.zoom>=1&&v.zoom<=16,"Invalid legacy map camera");s.viewport={};}else{validateMapSnapshot(s);auto old=MapViewport{};old.zoom=version==2?24:72;if(version<4&&s.viewport==old)s.viewport={};}for(auto&p:s.pins)p.id=canonical(std::move(p.id));return s;}
MapState::MapState(MapSnapshot s,MapPersistence p):persisted_(std::move(s)),persistence_(std::move(p)){validateMapSnapshot(persisted_);for(auto&pin:persisted_.pins)pin.id=canonical(std::move(pin.id));need(!persistence_.commit||(persistence_.newID&&persistence_.foundationNow),"Editable map needs injected UUID/date factories");viewport_=mapConstrained(persisted_.viewport);}
void MapState::clock(double t){need(std::isfinite(t)&&t>=time_,"Map requires a finite monotonic owner clock");time_=t;}
void MapState::failure(std::string error){error_=std::move(error);++revision_;}
bool MapState::commit(MapSnapshot next){if(!editable())return false;try{if(next==persisted_)return true;validateMapSnapshot(next);persistence_.commit(next);persisted_=std::move(next);return true;}catch(const std::exception&e){failure(e.what());return false;}catch(...){failure("The map could not be saved or opened.");return false;}}
bool MapState::camera(MapViewport v){if(v==viewport_)return false;viewport_=v;++cameraRevision_;++revision_;return true;}
bool MapState::hideCoordinates(){if(!coordinates_)return false;coordinates_=false;++revision_;return true;}
MapActions MapState::actions()const noexcept{MapActions out;auto add=[&](MapAction a,R r,bool enabled,std::string_view id={}){out.items[out.count++]={a,id,r,enabled};};
    add(MapAction::zoomIn,{26,184,22,22},viewport_.zoom<MapViewport::maximumZoom);add(MapAction::zoomOut,{26,212,22,22},viewport_.zoom>MapViewport::minimumZoom);add(MapAction::reset,{26,240,22,22},true);add(MapAction::addPin,{392,207,22,22},editable()&&persisted_.pins.size()<maximumPins);if(selected_&&coordinates_)add(MapAction::deletePin,{316,303,21,21},true);
    for(const auto&p:persisted_.pins){const auto point=mapScreen(p.x,p.y,viewport_);const auto target=mapMarkerHitRect(p.style,point);if(!mapContains(point))continue;bool blocked{};for(std::size_t k=0;k<out.count;++k)if(intersects(out.items[k].rect,target)){blocked=true;break;}if(!blocked)add(MapAction::pin,target,true,p.id);}return out;}
MapChange MapState::setActive(bool value){if(active_==value)return {};MapChange result;result.changed=true;if(!value){result=merge(result,finishWheel());result=merge(result,up(true));result=merge(result,endGesture());}active_=value;++revision_;return result;}
MapChange MapState::finishWheel(){if(!wheelDeadline_)return {};wheelDeadline_.reset();return endGesture();}
MapChange MapState::down(P p){if(!active_||!mapContains(p))return {};auto result=finishWheel();const auto available=actions();for(std::size_t k=0;k<available.count;++k){const auto&a=available.items[k];if(contains(a.rect,p)){if(a.enabled)result=merge(result,perform(a.action,a.pinID));result.changed|=hideCoordinates();result.handled=true;return result;}}result.changed|=hideCoordinates();drag_=Drag{p,viewport_,false};result.handled=true;return result;}
MapChange MapState::drag(P p){if(!active_||!drag_||!finite(p))return {};if(!drag_->moved&&std::hypot(p.x-drag_->start.x,p.y-drag_->start.y)<=3)return {true};drag_->moved=true;return {true,camera(mapPanned(drag_->viewport,{p.x-drag_->start.x,p.y-drag_->start.y}))};}
MapChange MapState::up(bool cancelled){if(!drag_)return {};const auto original=*drag_;drag_.reset();if(!original.moved&&!cancelled)return recenter(original.start,original.viewport);auto result=endGesture();result.handled=true;return result;}
MapChange MapState::recenter(P p,MapViewport original){const auto next=mapRecentered(original,p);if(next==viewport_)return endGesture();if(!editable())return {true};auto snapshot=persisted_;snapshot.viewport=next;const auto before=revision_;if(!commit(std::move(snapshot)))return {true,revision_!=before};camera(next);++gestureRevision_;return {true,true,!reduced_,true};}
MapChange MapState::endGesture(){++gestureRevision_;const auto before=revision_;if(editable()&&viewport_!=persisted_.viewport){auto next=persisted_;next.viewport=mapNormalized(viewport_);commit(std::move(next));}return {false,revision_!=before};}
MapChange MapState::add(P p){if(!editable())return {true};const auto world=mapWorld(p,viewport_);if(world.y<0||world.y>1)return {true};const auto before=revision_;try{need(persisted_.pins.size()<maximumPins,"The map can hold up to 128 pins. Remove a pin before adding another.");MapPin pin{canonical(persistence_.newID()),mapWrappedX(world.x),std::clamp(world.y,0.,1.),persistence_.foundationNow()};auto next=persisted_;next.pins.push_back(pin);if(!commit(std::move(next)))return {true,revision_!=before};selected_=std::move(pin.id);coordinates_=true;error_.reset();++revision_;return {true,true};}catch(const std::exception&e){failure(e.what());return {true,true};}}
MapChange MapState::remove(std::string_view id){if(!editable())return {true};const auto found=std::find_if(persisted_.pins.begin(),persisted_.pins.end(),[&](const auto&p){return p.id==id;});if(found==persisted_.pins.end())return {true};const bool selected=selected_&&*selected_==id;auto next=persisted_;next.pins.erase(next.pins.begin()+(found-persisted_.pins.begin()));const auto before=revision_;if(!commit(std::move(next)))return {true,revision_!=before};if(selected){selected_.reset();coordinates_=false;}error_.reset();++revision_;return {true,true};}
MapChange MapState::rightDown(P p){if(!active_||!mapContains(p))return {};auto result=finishWheel();const auto available=actions();for(std::size_t k=0;k<available.count;++k){const auto&a=available.items[k];if(a.action!=MapAction::pin&&contains(a.rect,p)){if(a.action==MapAction::deletePin)result=merge(result,perform(a.action));result.handled=true;return result;}}
    for(auto it=persisted_.pins.rbegin();it!=persisted_.pins.rend();++it)if(contains(mapMarkerHitRect(it->style,mapScreen(it->x,it->y,viewport_)),p))return merge(result,remove(it->id));return merge(result,add(p));}
MapChange MapState::perform(MapAction action,std::string_view id){if(!active_)return {};auto result=finishWheel();return merge(result,this->action(action,id));}
MapChange MapState::action(MapAction action,std::string_view id){if(action==MapAction::addPin)return add({220,220});if(action==MapAction::deletePin)return selected_?remove(*selected_):MapChange{true};
    if(action==MapAction::pin){if(!editable())return {true};const auto found=std::find_if(persisted_.pins.begin(),persisted_.pins.end(),[&](const auto&p){return p.id==id;});if(found==persisted_.pins.end())return {};auto next=persisted_;auto&pin=next.pins[found-persisted_.pins.begin()];pin.style=nextMapPinStyle(pin.style);const auto style=pin.style;const auto selected=pin.id;const auto before=revision_;if(!commit(std::move(next)))return {true,revision_!=before};selected_=selected;coordinates_=true;error_.reset();++revision_;return {true,true,false,false,style};}
    auto next=viewport_;if(action==MapAction::reset){next.zoom=MapViewport::defaultZoom;next=mapConstrained(next);}else if(action==MapAction::zoomIn||action==MapAction::zoomOut)next=mapZoomed(next,{220,220},action==MapAction::zoomIn?1.3:1/1.3);else return {};const bool changed=camera(next);auto result=endGesture();result.handled=true;result.changed|=changed;result.cameraAnimated=!reduced_;return result;
}
MapChange MapState::key(MapKey key){if(!active_)return {};auto result=finishWheel();switch(key){case MapKey::zoomIn:return merge(result,perform(MapAction::zoomIn));case MapKey::zoomOut:return merge(result,perform(MapAction::zoomOut));case MapKey::erase:if(!selected_)return result;return merge(result,perform(MapAction::deletePin));case MapKey::escape:if(!selected_)return result;selected_.reset();coordinates_=false;++revision_;return merge(result,{true,true});default:break;}P delta;switch(key){case MapKey::left:delta={32,0};break;case MapKey::right:delta={-32,0};break;case MapKey::down:delta={0,-32};break;case MapKey::up:delta={0,32};break;default:return result;}result.changed|=camera(mapPanned(viewport_,delta));result=merge(result,endGesture());result.handled=true;return result;}
MapChange MapState::wheel(P p,double delta,bool precise,bool ended,double time){clock(time);if(!active_)return {};if(!mapContains(p))return ended?finishWheel():MapChange{};MapChange result{true};if(std::isfinite(delta)&&delta!=0){wheelDeadline_=time+.18;const auto exponent=std::clamp(delta*(precise?.012:.12),std::log(.5),std::log(2.));result.changed=camera(mapZoomed(viewport_,p,std::exp(exponent)));}if(ended)result=merge(result,finishWheel());return result;}
MapChange MapState::magnify(P p,double amount,bool ended,double time){clock(time);if(!active_)return {};if(!mapContains(p))return ended?finishWheel():MapChange{};MapChange result{true};if(std::isfinite(amount)&&amount!=0){wheelDeadline_=time+.18;result.changed=camera(mapZoomed(viewport_,p,std::clamp(1+amount,.5,2.)));}if(ended)result=merge(result,finishWheel());return result;}
MapChange MapState::advance(double time){clock(time);return wheelDeadline_&&time>=*wheelDeadline_?finishWheel():MapChange{};}
}

#if defined(_MSC_VER)
#pragma float_control(pop)
#elif defined(__GNUC__) && !defined(__clang__)
#pragma GCC pop_options
#endif
