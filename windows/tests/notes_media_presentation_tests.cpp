#include "modules/notes_media_presentation.hpp"
#include "core/data/json.hpp"
#include <atomic>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <new>
#include <cmath>
namespace {std::atomic<std::size_t>allocations{};bool counting{};}
void*operator new(std::size_t n){if(counting)++allocations;if(auto*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void*p)noexcept{std::free(p);}void*operator new[](std::size_t n){return ::operator new(n);}void operator delete[](void*p)noexcept{std::free(p);}
#if defined(__cpp_sized_deallocation)
void operator delete(void*p,std::size_t)noexcept{std::free(p);}void operator delete[](void*p,std::size_t)noexcept{std::free(p);}
#endif
using namespace endfield::modules;using endfield::core::Rect;using ehud::data::Json;
namespace {
std::size_t checks{};void check(bool v,const char*m){++checks;if(!v)throw std::runtime_error(m);}
void nearValue(double a,double b,const char*m){check(std::abs(a-b)<1e-11,m);}
template<class F>void rejects(F f){bool threw{};try{f();}catch(const std::exception&){threw=true;}check(threw,"Invalid media input rejected");}
void run(){
    NotesMediaLayout video(228,154,NotesMediaKind::video,10);const auto&g=video.geometry();
    check(g.content==Rect{5,29,218,98}&&g.footer==Rect{8,131,65,18},"Source video insets/footer");
    check(g.seek==Rect{78,129,131,20}&&g.rail==Rect{78,138,131,2}&&g.playback==Rect{6,130,66,19},"Source seek rail and independent playback hit");
    check(video.fittedImage(100,100)==Rect{65,29,98,98},"Square source content centered with resizeAspect");
    nearValue(video.seekSeconds(143.5),5,"Local seek pointer maps exactly");check(video.seekSeconds(-999)==0&&video.seekSeconds(999)==10,"Seek clamps at endpoints");
    NotesPalette colors;colors.primary={.8,.7,.6,1};colors.muted={.2,.3,.4,.6};colors.accent={1,.5,0,1};
    NotesMediaStatus status;status.state=NotesMediaState::loading;check(video.footer(status,colors).text.text=="Loading media…","Source loading footer");
    status.state=NotesMediaState::playing;check(video.footer(status,colors).text.text=="Ⅱ Pause"&&video.playbackAction("synthetic",status)->label=="Pause","Playback display and AX labels stay distinct");
    status.localizedError="Decoder unavailable";check(video.footer(status,colors).text.text==*status.localizedError,"Error wins even over playback state");
    check(video.progressColors(colors)[0]==NotesColor{.2,.3,.4,.3},"Rail alpha replaces source muted alpha");
    NotesMediaLayout image(228,154,NotesMediaKind::image);check(!image.geometry().hasPlayback&&!image.playbackAction("x",{})&&image.footer({},colors).text.text.empty(),"Still image has no playback control or status when ready");
    NotesMediaLayout legacy(228,154,NotesMediaKind::image,{},true);check(legacy.geometry().content==Rect{5,29,218,119},"Legacy image keeps original larger viewport");rejects([&]{legacy.footer({},colors);});
    NotesMediaProgress progress(video);progress.update(2,{},true,true,true,false,100);auto p=progress.sample(100.5);
    nearValue(p.fill.width,32.75,"Source1s linear progress");check(p.handle==Rect{108.25,135,5,8}&&p.active,"Source5x8 centered handle");
    check(!progress.sample(101).active,"Track reaches model target and goes idle");nearValue(progress.sample(101).fill.width,39.3,"Track reaches source target");
    progress.update(1,{},true,true,true,false,100.5);nearValue(progress.sample(100.5).fill.width,13.1,"Interrupted progress starts from reported time, not prior sampled pose");
    progress.update(9.8,{},true,true,true,false,102);nearValue(progress.sample(103).fill.width,131,"End interpolation clamps to width");
    for(unsigned flags=0;flags<4;++flags){progress.update(2,{},flags!=0,flags!=1,flags!=2,flags==3,10);check(!progress.needsFrame(10),"Nonanimated/paused/hidden/reduced progress has no frame demand");nearValue(progress.sample(11).fill.width,26.2,"Nonanimated source width");}
    progress.update(2,7.25,true,true,true,false,20);nearValue(progress.sample(20).fill.width,94.975,"Dragging seek previews exact numeric position without interpolation");check(!progress.needsFrame(20),"Seek preview has no independent animation");
    const auto count=allocations.load();counting=true;for(unsigned i=0;i<1000;++i){progress.update(i%10,{},true,true,true,false,double(i));(void)progress.sample(double(i)+.25);(void)video.seekSeconds(double(i));}counting=false;check(allocations==count,"Progress/seek sampling retains no heap or per-frame raster work");
    check(NotesMediaLayout::decodeDimension()==512&&NotesMediaLayout::decodeDimension(9999)==768&&NotesMediaLayout::decodeDimension(-3)==32,"Source bounded decode dimensions");
    for(auto invalid:{0.,-1.,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()})check(NotesMediaLayout::normalizedFrameDelay(invalid)==.1,"Source invalid GIF delay fallback");
    check(NotesMediaLayout::normalizedFrameDelay({})==.1&&NotesMediaLayout::normalizedFrameDelay(.001)==.04&&NotesMediaLayout::normalizedFrameDelay(999)==600,"Source GIF delay bounds");
    rejects([]{NotesMediaLayout v(200,100,NotesMediaKind::video);});rejects([&]{video.fittedImage(0,10);});rejects([&]{progress.update(0,{},true,true,true,false,std::numeric_limits<double>::infinity());});
}
void oracle(const char*path){std::ifstream f(path);check(bool(f),"Original media oracle readable");std::string bytes((std::istreambuf_iterator<char>(f)),{});const auto root=Json::parse(bytes);check(root["sourceSHA256"].string().size()==64,"Oracle records exact original source hash");
    for(const auto&v:root["cases"].array()){
        NotesMediaLayout l(v["width"].number(),v["height"].number(),NotesMediaKind::video,v["duration"].number());NotesMediaProgress p(l);
        std::optional<double>preview;if(!v["preview"].isNull())preview=v["preview"].number();p.update(v["time"].number(),preview,v["animated"].boolean(),v["playing"].boolean(),v["visible"].boolean(),v["reduced"].boolean(),0);
        const auto model=p.sample(1);const auto&fill=v["fillBounds"].array(),&handle=v["handlePosition"].array();nearValue(model.fill.width,fill[2].number(),"Exact original CALayer model fill target");nearValue(model.handle.x+2.5,handle[0].number(),"Exact original handle target");nearValue(model.handle.y+4,handle[1].number(),"Exact original handle vertical center");
        const auto&seek=v["seek"].array();const auto&r=l.geometry().seek;nearValue(r.x,seek[0].number(),"Source seek x");nearValue(r.y,seek[1].number(),"Source seek y");nearValue(r.width,seek[2].number(),"Source seek width");
        const bool animated=v["animation"].isObject();check(animated==(v["animated"].boolean()&&v["playing"].boolean()&&!preview&&v["visible"].boolean()&&!v["reduced"].boolean()),"Source interpolation conditions");
        if(animated){nearValue(p.sample(0).fill.width,v["animation"]["from"].number(),"Exact original progress animation start");nearValue(v["animation"]["duration"].number(),1,"Original one-second duration");}
    }
}
}
int main(int argc,char**argv){try{run();if(argc>1)oracle(argv[1]);std::cout<<"PASS "<<checks<<" Notes media presentation checks\n";return 0;}catch(const std::exception&e){std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}}
