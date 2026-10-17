#include "modules/media_assembly_presentation.hpp"
#include "core/data/json.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <new>
#include <sstream>
namespace {std::atomic<bool>counting{};std::atomic<std::size_t>allocations{};}
void*operator new(std::size_t n){if(counting)++allocations;if(auto*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void*operator new[](std::size_t n){return ::operator new(n);}void operator delete(void*p)noexcept{std::free(p);}void operator delete[](void*p)noexcept{std::free(p);}
#if defined(__cpp_sized_deallocation)
void operator delete(void*p,std::size_t)noexcept{std::free(p);}void operator delete[](void*p,std::size_t)noexcept{std::free(p);}
#endif
namespace {
namespace m=endfield::modules;namespace c=endfield::core;using J=ehud::data::Json;unsigned checks{},row{};
void check(bool v,const char*why){++checks;if(!v)throw std::runtime_error(why);}
void n(double actual,double expected){++checks;if(std::abs(actual-expected)>1e-10*std::max(1.,std::abs(expected))){std::ostringstream s;s<<"Source numeric mismatch: "<<actual<<" / "<<expected;throw std::runtime_error(s.str());}}
void compare(c::Point p,const J&j){n(p.x,j.array()[0].number());n(p.y,j.array()[1].number());}
void compare(c::Rect r,const J&j){compare(c::Point{r.x,r.y},j);n(r.width,j.array()[2].number());n(r.height,j.array()[3].number());}
void compare(const m::MediaAssemblyAdjustments&a,const J&j){
    compare(c::Rect{a.crop.x,a.crop.y,a.crop.width,a.crop.height},j["crop"]);n(a.rotationQuarterTurns,j["turns"].number());check(a.mirrored==j["mirror"].boolean(),"Source mirror");
    for(const auto&pair:std::initializer_list<std::pair<double,const char*>>{{a.brightness,"brightness"},{a.contrast,"contrast"},{a.saturation,"saturation"},{a.temperature,"temperature"},{a.tint,"tint"},{a.highlights,"highlights"},{a.shadows,"shadows"},{a.exposure,"exposure"},{a.levelsBlack,"black"},{a.levelsWhite,"white"},{a.levelsGamma,"gamma"},{a.trimStart,"start"}})n(pair.first,j[pair.second].number());
    for(std::size_t i=0;i<5;++i)n(a.curve[i],j["curve"].array()[i].number());check(bool(a.trimEnd)!=j["end"].isNull(),"Original optional trim end");if(a.trimEnd)n(*a.trimEnd,j["end"].number());
    check(m::mediaAssemblyFilters()[std::size_t(a.filter)].id==j["filter"].string(),"Source filter selection");check(a.stickers.size()==j["stickers"].array().size(),"Source sticker count");for(std::size_t i=0;i<a.stickers.size();++i){const auto&s=a.stickers[i];const auto&e=j["stickers"].array()[i];check(m::mediaAssemblyStickers()[std::size_t(s.kind)].id==e["kind"].string(),"Source sticker kind");n(s.x,e["x"].number());n(s.y,e["y"].number());n(s.size,e["size"].number());n(s.rotation,e["rotation"].number());}
}
struct Owner {
    m::MediaAssemblyPresentation p;m::MediaAssemblyAdjustments a;std::optional<m::MediaAssemblyDocumentInfo>d;bool playing{},visible{true};double time{};unsigned identity{};
    m::MediaAssemblyView view()const{return {d?&*d:nullptr,&a,{},false,false,playing,time};}
    void seek(double t){time=std::min(std::max(a.trimStart,t),std::max(a.trimStart,a.trimEnd.value_or(d?d->duration:0)-.001));}
    void apply(m::MediaAssemblyRequest r){
        if(r.adjustments&&r.adjustments->valid())a=std::move(*r.adjustments);
        if(r.command==m::MediaAssemblyCommand::trim||r.command==m::MediaAssemblyCommand::seek)seek(r.time);
        if(r.command==m::MediaAssemblyCommand::play)playing=!playing;
        if(r.command==m::MediaAssemblyCommand::reset)a={};
        if(r.command==m::MediaAssemblyCommand::closeMedia){a={};d.reset();}
        if(visible)p.synchronize(view());
    }
    void operation(const J&op){const auto kind=op["op"].string();auto num=[&](const char*k){return op[k].isNumber()?op[k].number():0.;};const c::Point point{num("x"),num("y")};
        if(kind=="photo"||kind=="video"){d=m::MediaAssemblyDocumentInfo{std::to_string(++identity),kind=="video",{1920,1080},120};a={};time=0;p.synchronize(view());}
        else if(kind=="action")apply(p.perform(op["id"].string(),view(),"owned-sticker"));
        else if(kind=="parameter")apply(p.parameter(op["id"].string(),num("value"),view()));
        else if(kind=="trim")apply(p.trim(op["beginning"].boolean(),num("value"),view()));
        else if(kind=="scroll")p.scroll(point,num("dx"),num("dy"),false,view());
        else if(kind=="zoom")p.zoom(num("value"),point,view());
        else if(kind=="down")apply(p.pointerDown(point,view(),c::Language::english,"owned-sticker"));
        else if(kind=="drag")apply(p.pointerDrag(point,view()));
        else if(kind=="up")apply(p.pointerUp(view()));
        else if(kind=="stickerKey")apply(p.adjustSelected(num("dx"),num("dy"),num("size"),num("rotation"),view()));
        else if(kind=="hide"){visible=false;p.hide();}
        else if(kind=="show"){visible=true;p.synchronize(view());}
    }
    void compareState(const J&j){
        compare(p.imageRect(view()),j["image"]);n(p.viewport().zoom(),j["zoom"].number());compare(p.viewport().pan(),j["pan"]);
        constexpr std::array<std::string_view,3>drawers{"tools","filters","stickers"};constexpr std::array<std::string_view,5>tools{"crop","adjust","curves","levels","trim"};
        check(bool(p.drawer())!=j["drawer"].isNull(),"Source drawer visibility");if(p.drawer())check(drawers[std::size_t(*p.drawer())]==j["drawer"].string(),"Source drawer choice");
        check(bool(p.tool())!=j["tool"].isNull(),"Source inline tool visibility");if(p.tool())check(tools[std::size_t(*p.tool())]==j["tool"].string(),"Source inline tool choice");
        n(p.drawerOffset(),j["offset"].number());compare(p.cropRect(view()),j["crop"]);const auto handles=p.cropHandles(view());for(std::size_t i=0;i<handles.size();++i)compare(handles[i],j["cropHandles"].array()[i]);check(!p.selectedSticker().empty()==j["selected"].boolean(),"Original selection");check(p.dragging()==j["dragging"].boolean(),"Original capture lifetime");n(time,j["time"].number());compare(a,j["adjustments"]);
        const auto ps=p.parameters(view());check(ps.count==j["parameters"].array().size(),"Original parameter count");for(std::size_t i=0;i<ps.count;++i){const auto&v=ps.values[i];const auto&e=j["parameters"].array()[i];check(v.id==e["id"].string()&&v.english==e["title"].string(),"Original parameter identity");n(v.low,e["low"].number());n(v.high,e["high"].number());n(v.value,e["value"].number());n(v.step,e["step"].number());}
        if(!visible)return;const auto actions=p.actions(view(),c::Language::english);check(actions.size()==j["actions"].array().size(),"Original action count");for(std::size_t i=0;i<actions.size();++i){const auto&v=actions[i];const auto&e=j["actions"].array()[i];check(v.id==e["id"].string()&&v.title==e["title"].string(),"Original button identity and title");compare(v.rect,e["rect"]);check(v.enabled==e["enabled"].boolean(),"Original enabled state");}
    }
};
void source(const J&f){check(f["sourceCommit"].string()=="ca04f142185c7de40acd8523bdb563195d90a1d1","Pinned authoritative Mac source");Owner o;for(const auto&entry:f["cases"].array()){o.operation(entry["operation"]);o.compareState(entry["state"]);++row;}}
void lifecycle(){Owner o;o.d=m::MediaAssemblyDocumentInfo{"warm",false,{1920,1080},120};o.p.synchronize(o.view());o.p.zoom(3,{220,198},o.view());auto view=o.view();
    allocations=0;counting=true;for(unsigned i=0;i<10000;++i){o.p.scroll({250,250},i%2?1:-1,i%2?2:-2,false,view);o.p.zoom(i%2?1.001:1/1.001,{250,250},view);}counting=false;check(allocations==0,"Warm view pan/zoom allocates nothing and produces no edit request");
    o.apply(o.p.perform("stickers",o.view()));for(unsigned i=0;i<17;++i)o.apply(o.p.perform("sticker:sticker_1",o.view(),std::to_string(i)));check(o.a.stickers.size()==16,"Source bound rejects seventeenth sticker");
    const auto old=o.a;const double nan=std::numeric_limits<double>::quiet_NaN();o.apply(o.p.adjustSelected(nan,0,0,0,o.view()));check(o.a==old,"Invalid keyboard delta leaves edits unchanged");
    o.apply(o.p.perform("crop",o.view()));const auto h=o.p.cropHandles(o.view());o.apply(o.p.pointerDown(h[3],o.view(),c::Language::english));check(o.p.dragging(),"Crop handle starts capture");o.p.hide();check(!o.p.dragging()&&!o.p.tool(),"Hide cancels capture and crop display override");
    o.d->id="replacement";o.p.synchronize(o.view());check(o.p.viewport().zoom()==1&&o.p.selectedSticker().empty()&&!o.p.drawer(),"Replacement resets only the source presentation");
}
}
int main(int argc,char**argv){try{check(argc==2,"Pass original Canvas fixture");std::ifstream f(argv[1],std::ios::binary);std::ostringstream b;b<<f.rdbuf();check(bool(f),"Fixture readable");source(J::parse(b.str()));lifecycle();std::cout<<"PASS "<<checks<<" Media Assembly presentation checks\n";return 0;}catch(const std::exception&e){counting=false;std::cerr<<"FAIL row "<<row<<", check "<<checks<<": "<<e.what()<<'\n';return 1;}}
