// Notes drawing throughput probe (diagnostics; prints numbers, asserts only
// that the fixture ran). A fast 1000 Hz drag delivered the way the HUD host
// delivers it: one WM_MOUSEMOVE per frame whose PointerEvent::history
// carries the samples Windows coalesced since the previous move
// (app/overlay_host.hpp), through the production Notes owner, published every
// frame the way Application::publishNative does (owner upload, entries,
// composition). Hidden owned window, WARP offscreen target, synthetic store.
#ifdef _WIN32
#include "tools/notes_preview.hpp"
#include "core/data/data_store.hpp"
#include "modules/notes_drawing.hpp"
#include <objbase.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <vector>
namespace gpu=endfield::native;namespace app=endfield::app;namespace core=endfield::core;namespace tools=endfield::tools;namespace data=ehud::data;
namespace {
using Clock=std::chrono::steady_clock;
double seconds(Clock::duration d){return std::chrono::duration<double>(d).count();}
void check(bool v,const char*m){if(!v)throw std::runtime_error(m);}
struct Window {HWND value{CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP,L"STATIC",L"Owned Drawing probe fixture",WS_POPUP,0,0,1280,800,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr)};Window(){check(value!=nullptr&&!IsWindowVisible(value),"Owned hidden Drawing HWND");}~Window(){DestroyWindow(value);}};
struct Root {std::filesystem::path path=std::filesystem::absolute(std::filesystem::temp_directory_path())/("endfield-drawing-probe-"+data::makeUUID());~Root(){std::error_code error;std::filesystem::remove_all(path,error);}};
struct Fixture {
    HWND window;gpu::Renderer&renderer;gpu::LayerRasterizer raster;Root root;std::unique_ptr<tools::NotesPreview>preview;gpu::LayerComposition composition;
    core::source::DesktopChromeSettings settings;core::Matrix4 design,camera{gpu::layerViewportProjection(1280,800)};double time{};
    Fixture(HWND hwnd,gpu::Renderer&r,const std::filesystem::path&formats):window(hwnd),renderer(r){settings.viewport={0,0,1280,800};settings.module=core::Module::notes;settings.sourceShell=true;design=core::source::DesktopChromeLayout::make(settings,{},{}).designToScreen;preview=std::make_unique<tools::NotesPreview>(window,raster,root.path,false,formats);preview->resize({1280,800,96,1,1280,800});frame();}
    ~Fixture(){if(!preview)return;try{composition.detach(renderer);preview->release(renderer);}catch(...){renderer.reset();}preview.reset();}
    // Application::Impl::present: owner update, then publishNative (upload,
    // entries, the composition) and the composition's present.
    void frame(){preview->update(design,settings,1,time,true);preview->upload(renderer);composition.setEntries(renderer,preview->entries());preview->collected(renderer);composition.present(renderer);renderer.setCamera(camera);}
    void tick(double step=1./165){time+=step;}
    const gpu::DrawObject*draw(std::string_view suffix)const{const auto rows=composition.draws();const auto i=std::find_if(rows.begin(),rows.end(),[&](const auto&d){return d.sourceID.ends_with(suffix);});return i==rows.end()?nullptr:&*i;}
    core::Point project(const core::Matrix4&m,core::Point p)const{const auto result=core::Projection::viewport(camera*m,1280,800).project(p);check(result.has_value(),"Input projects through the retained Drawing plane");return *result;}
    data::Note drawing()const{data::NotesStore store(root.path);const auto&rows=store.notes();const auto found=std::find_if(rows.begin(),rows.end(),[](const auto&n){return n.kind==data::NoteKind::drawing;});check(found!=rows.end(),"Synthetic store contains the Drawing");return *found;}
    core::Point cardPoint(const data::Note&n,core::Point p)const{const auto*card=draw("note/"+n.id+"/card");check(card,"Drawing card is in the composition");return project(card->world,p);}
};
struct Phase {double pointer{},frame{};std::uint64_t events{},frames{},rasters{},textureUploads{},regionUploads{},regionBytes{};};
void report(const char*name,const Phase&p){
    std::cout<<name<<": "<<p.events<<" moves, "<<p.frames<<" frames; per move "<<(p.events?p.pointer/double(p.events)*1e3:0)<<" ms; per frame (owner update+upload+publish) "<<(p.frames?p.frame/double(p.frames)*1e3:0)
        <<" ms; rasters/frame "<<(p.frames?double(p.rasters)/double(p.frames):0)<<"; texture creations/frame "<<(p.frames?double(p.textureUploads)/double(p.frames):0)
        <<"; in-place regions/frame "<<(p.frames?double(p.regionUploads)/double(p.frames):0)<<" ("<<(p.frames?double(p.regionBytes)/double(p.frames)/1024:0)<<" KiB)\n";}
// One drag of `frames` frames, `perFrame` samples each (the move's own point
// last, the rest as its coalesced history), along `path` (card points).
std::size_t drag(Fixture&f,const data::Note&note,unsigned frames,unsigned perFrame,const std::function<core::Point(double)>&path,Phase*early,Phase*late){
    std::vector<core::Point>samples;samples.reserve(std::size_t(frames)*perFrame+1);for(unsigned k=0;k<=frames*perFrame;++k)samples.push_back(f.cardPoint(note,path(double(k)/double(frames*perFrame))));
    f.tick();check(f.preview->pointer({app::PointerKind::down,app::PointerButton::left,samples[0].x,samples[0].y,MK_LBUTTON},f.time),"The drag's press starts a drawing gesture");f.frame();
    std::vector<app::PointerSample>history;history.reserve(perFrame);
    for(unsigned frame=0;frame<frames;++frame){auto*phase=frame<frames/4?early:frame>=frames-frames/4?late:nullptr;
        history.clear();for(unsigned k=1;k<perFrame;++k){const auto&s=samples[std::size_t(frame)*perFrame+k];history.push_back({s.x,s.y});}
        const auto&own=samples[std::size_t(frame+1)*perFrame];f.tick();
        const app::PointerEvent move{app::PointerKind::move,app::PointerButton::none,own.x,own.y,MK_LBUTTON,history};
        const auto rasters=f.raster.stats().rasterizations;const auto gpuBefore=f.renderer.stats();
        const auto begin=Clock::now();(void)f.preview->pointer(move,f.time);const auto moved=Clock::now();f.frame();const auto end=Clock::now();
        if(phase){phase->pointer+=seconds(moved-begin);phase->frame+=seconds(end-moved);++phase->events;++phase->frames;phase->rasters+=f.raster.stats().rasterizations-rasters;const auto gpu=f.renderer.stats();
            phase->textureUploads+=gpu.textureUploads-gpuBefore.textureUploads;phase->regionUploads+=gpu.textureRegionUploads-gpuBefore.textureRegionUploads;phase->regionBytes+=gpu.textureRegionBytes-gpuBefore.textureRegionBytes;}
    }
    f.tick();(void)f.preview->pointer({app::PointerKind::up,app::PointerButton::left,samples.back().x,samples.back().y,0},f.time);f.frame();
    return samples.size();
}
void run(HWND hwnd,gpu::Renderer&renderer,const std::filesystem::path&formats){Fixture f(hwnd,renderer,formats);
    f.tick();check(f.preview->tool("tool:drawing",f.time),"The centre Drawing tool creates a drawing");f.frame();
    for(unsigned k=0;k<40;++k){f.tick(.01);f.frame();}
    auto note=f.drawing();
    // 1000 Hz on a 165 Hz display: six samples per frame along a wave across
    // the card, 0.2 to 0.9 card points apart (the source keeps those at least
    // 0.8 points from the previous kept one).
    constexpr unsigned frames=240,perFrame=6;
    const auto wave=[](double t){return core::Point{15+270*t,120+70*std::sin(t*6.28318*3)};};
    Phase early,late;const auto delivered=drag(f,note,frames,perFrame,wave,&early,&late);
    note=f.drawing();const endfield::modules::NotesDrawing drawn(*note.drawing);check(drawn.strokes().size()==1,"The probe stroke was saved");
    // The samples the source keeps: each one at least 0.8 card points from
    // the previous kept one (sampleDrawingStroke on the card's viewport).
    endfield::modules::DrawingStroke expected;const auto viewport=endfield::modules::NotesDrawing::viewport({note.width,note.height});
    for(unsigned k=0;k<=frames*perFrame;++k){const auto p=wave(double(k)/double(frames*perFrame));(void)endfield::modules::sampleDrawingStroke(expected,{p.x-viewport.x,p.y-viewport.y},{viewport.width,viewport.height});}
    std::cout<<"samples delivered "<<delivered<<" ("<<frames<<" moves carrying "<<perFrame-1<<" coalesced samples each); points kept "<<drawn.strokes()[0].points.size()<<" of the "<<expected.points.size()<<" the source 0.8-point threshold keeps\n";
    const auto ink=f.preview->drawingStats(note.id);
    std::cout<<"ink: "<<ink.paints<<" paints, "<<ink.wholeRasters<<" whole rasters, "<<ink.regionRasters<<" region rasters ("<<ink.regionPixels<<" pixels), "<<ink.textureCreations<<" texture creations, "<<(ink.paints?ink.paintSeconds/double(ink.paints)*1e3:0)<<" ms per paint; "<<ink.plan.adoptions<<" commits adopted\n";
    report("stroke, first quarter",early);report("stroke, last quarter",late);
    // Twelve more strokes, then an eraser drag across all of them.
    for(unsigned s=0;s<12;++s){const double y=40+s*14.;drag(f,note,20,perFrame,[y](double t){return core::Point{20+260*t,y+8*std::sin(t*9)};},nullptr,nullptr);}
    const auto right=f.cardPoint(note,{150,200});f.tick();check(f.preview->pointer({app::PointerKind::down,app::PointerButton::right,right.x,right.y,MK_RBUTTON},f.time),"Right press switches to the eraser");f.frame();f.tick();f.preview->pointer({app::PointerKind::up,app::PointerButton::right,right.x,right.y,0},f.time);f.frame();
    Phase eraseEarly,eraseLate;drag(f,note,120,perFrame,[](double t){return core::Point{150+100*std::sin(t*3),30+180*t};},&eraseEarly,&eraseLate);
    note=f.drawing();std::cout<<"strokes left after the eraser drag "<<endfield::modules::NotesDrawing(*note.drawing).strokes().size()<<" of 13\n";
    report("eraser, first quarter",eraseEarly);report("eraser, last quarter",eraseLate);
    f.frame();f.composition.detach(renderer);f.preview->release(renderer);f.preview.reset();
}
}
int wmain(int argc,wchar_t**argv){const auto hr=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);if(FAILED(hr))return 1;int result{};try{check(argc==3,"Pass shader and the Notes format resources");Window window;const auto formats=std::filesystem::absolute(argv[2]);gpu::Renderer renderer;renderer.initialize(window.value,1280,800,{gpu::Driver::warpForTests,argv[1],gpu::RenderTarget::offscreenForTests});run(window.value,renderer,formats);renderer.reset();std::cout<<"Notes drawing probe finished\n";}catch(const std::exception&e){std::cerr<<"Notes drawing probe failed: "<<e.what()<<'\n';result=1;}CoUninitialize();return result;}
#else
int main(){return 0;}
#endif
