#include "modules/media_assembly_model.hpp"
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
void*operator new(std::size_t size){if(counting)++allocations;if(auto*p=std::malloc(size?size:1))return p;throw std::bad_alloc();}
void*operator new[](std::size_t size){return ::operator new(size);}
void operator delete(void*p)noexcept{std::free(p);}void operator delete[](void*p)noexcept{std::free(p);}
#if defined(__cpp_sized_deallocation)
void operator delete(void*p,std::size_t)noexcept{std::free(p);}void operator delete[](void*p,std::size_t)noexcept{std::free(p);}
#endif
namespace {
namespace m=endfield::modules;namespace c=endfield::core;using J=ehud::data::Json;unsigned checks{};
void check(bool yes,const char*message){++checks;if(!yes)throw std::runtime_error(message);}
void number(double actual,double expected){check(std::abs(actual-expected)<=1e-10*std::max(1.,std::abs(expected)),"Exact original model numeric comparison");}
c::Point point(const J&j){return {j.array()[0].number(),j.array()[1].number()};}
c::Rect rect(const J&j){const auto&a=j.array();return {a[0].number(),a[1].number(),a[2].number(),a[3].number()};}
m::MediaAssemblyCrop crop(const J&j){const auto r=rect(j);return {r.x,r.y,r.width,r.height};}
void compare(c::Point p,const J&j){const auto q=point(j);number(p.x,q.x);number(p.y,q.y);}
void compare(c::Rect p,const J&j){const auto q=rect(j);number(p.x,q.x);number(p.y,q.y);number(p.width,q.width);number(p.height,q.height);}
void compare(m::MediaAssemblyCrop p,const J&j){compare(c::Rect{p.x,p.y,p.width,p.height},j);}
void source(const J&fixture){
    check(fixture["sourceCommit"].string()=="ca04f142185c7de40acd8523bdb563195d90a1d1","Only authoritative Mac fixture accepted");
    for(const auto&t:fixture["trajectories"].array()){
        m::MediaAssemblyViewport v;const auto size=point(t["size"]);const auto view=rect(t["view"]);
        for(const auto&row:t["rows"].array()){
            const auto op=row["op"].string();if(op=="zoom")v.magnify(row["factor"].number(),point(row["point"]),size,view);else if(op=="move")v.move(point(row["point"]),size,view);else v.reset();
            number(v.zoom(),row["zoom"].number());compare(v.pan(),row["pan"]);compare(v.imageRect(size,view),row["rect"]);
        }
    }
    for(const auto&t:fixture["transforms"].array()){
        const int turns=static_cast<int>(t["turns"].integer());const bool mirror=t["mirrored"].boolean();
        compare(m::MediaAssemblyViewport::displayPoint(point(t["point"]),turns,mirror),t["displayPoint"]);
        compare(m::MediaAssemblyViewport::sourcePoint(point(t["point"]),turns,mirror),t["sourcePoint"]);
        const auto display=m::MediaAssemblyViewport::displayCrop(crop(t["crop"]),turns,mirror);compare(display,t["displayCrop"]);
        compare(m::MediaAssemblyViewport::sourceCrop(display,turns,mirror),t["roundtripCrop"]);
    }
    for(const auto&t:fixture["clamps"].array())compare(m::MediaAssemblyViewport::sourceCrop(rect(t["rect"]),static_cast<int>(t["turns"].integer()),t["mirrored"].boolean()),t["crop"]);
    for(const auto&t:fixture["rotations"].array())compare(m::MediaAssemblyViewport::unrotate(point(t["point"]),point(t["center"]),t["degrees"].number()),t["result"]);
    const auto stickers=m::mediaAssemblyStickers();check(stickers.size()==fixture["stickers"].array().size(),"Removed promotional stickers stay absent");
    for(std::size_t i=0;i<stickers.size();++i){const auto&t=fixture["stickers"].array()[i];const auto kind=static_cast<m::MediaAssemblyStickerKind>(i);
        check(stickers[i].id==t["id"].string(),"Original sticker identity/order");check(m::mediaAssemblyStickerTitle(kind,c::Language::english)==t["english"].string(),"Original English sticker title");check(m::mediaAssemblyStickerTitle(kind,c::Language::simplifiedChinese)==t["chinese"].string(),"Original Chinese sticker title");compare(m::mediaAssemblyStickerPixelSize(kind),t["pixelSize"]);
        m::MediaAssemblySticker s;s.kind=kind;s.x=.17;s.y=.83;s.size=.39;s.rotation=37;compare(m::MediaAssemblyViewport::stickerRect(s,{21,43,625,319}),t["rect"]);
    }
    const auto filters=m::mediaAssemblyFilters();check(filters.size()==fixture["filters"].array().size(),"Original preset count");
    for(std::size_t i=0;i<filters.size();++i){const auto&t=fixture["filters"].array()[i];check(filters[i].id==t["id"].string(),"Original preset identity/order");check(m::mediaAssemblyFilterTitle(static_cast<m::MediaAssemblyFilter>(i),c::Language::english)==t["english"].string(),"Original English filter title");check(m::mediaAssemblyFilterTitle(static_cast<m::MediaAssemblyFilter>(i),c::Language::simplifiedChinese)==t["chinese"].string(),"Original Chinese filter title");}
    for(const auto&t:fixture["ranges"].array()){
        m::MediaAssemblyAdjustments a;a.trimStart=t["start"].number();if(!t["end"].isNull())a.trimEnd=t["end"].number();check(a.valid()==t["valid"].boolean(),"Original trim validity");const auto range=a.timeRange(t["duration"].number());check(bool(range)!=t["range"].isNull(),"Original trim availability");
        if(range){const auto&expected=t["range"].array();check(range->startTicks==expected[0].integer()&&range->durationTicks==expected[1].integer()&&range->timescale==expected[2].integer(),"Original Core Media600-timescale rounding");}
    }
}
void boundaries(){
    m::MediaAssemblyAdjustments a;check(a.valid(),"Original defaults valid");a.stickers.resize(16);check(a.valid(),"Sixteen valid stickers accepted");a.stickers.emplace_back();check(!a.valid(),"Seventeenth sticker rejected");
    a={};a.levelsBlack=.5;a.levelsWhite=.505;check(!a.valid(),"Levels retain minimum separation");a={};a.crop.width=.009;check(!a.valid(),"Crop cannot invert or collapse below source minimum");
    const auto nan=std::numeric_limits<double>::quiet_NaN();a={};a.brightness=nan;check(!a.valid(),"Nonfinite adjustment rejected");a={};a.curve[3]=nan;check(!a.valid(),"Nonfinite curve rejected");a={};a.trimEnd=nan;check(!a.valid(),"Nonfinite trim rejected");
    m::MediaAssemblyViewport v;v.magnify(4,{33,71},{1920,1080},{0,0,320,300});const auto z=v.zoom();const auto pan=v.pan();v.magnify(nan,{20,20},{1920,1080},{0,0,320,300});v.move({nan,4},{1920,1080},{0,0,320,300});check(z==v.zoom()&&pan.x==v.pan().x&&pan.y==v.pan().y,"Invalid deltas preserve retained view");
    allocations=0;counting=true;for(unsigned i=0;i<20000;++i){v.magnify(i%2?1.001:1./1.001,{63,80},{1920,1080},{0,0,320,300});v.move({i%2?1.:-1.,0},{1920,1080},{0,0,320,300});}counting=false;check(allocations==0,"Warm zoom/pan uses no allocation or image work");
}
}
int main(int argc,char**argv){try{check(argc==2,"Pass original synthetic source fixture");std::ifstream file(argv[1],std::ios::binary);std::ostringstream buffer;buffer<<file.rdbuf();check(bool(file),"Source fixture readable");source(J::parse(buffer.str()));boundaries();std::cout<<"PASS "<<checks<<" original Media Assembly model/viewport checks\n";return 0;}catch(const std::exception&e){counting=false;std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}}
