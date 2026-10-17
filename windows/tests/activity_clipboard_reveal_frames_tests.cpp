#ifdef _WIN32
// Frame-by-frame subsection reveals of Clipboard and Activity Monitor through
// their owners (tools/clipboard_preview.hpp, tools/activity_preview.hpp),
// published exactly as the HUD host publishes owners (app/application.cpp
// publishNative: setEntries and collected() only when a scene, its revisions or
// its supplemental draws changed; present() every frame).
//
// Mac reference: HUDSubsectionTransition.reveal. Clipboard: removing the last
// row moves no other row (ClipboardCanvas.animateCollectionChange), so the
// rows reveal from the left edge over .26 s. Activity: a sort header
// (ActivityMonitorCanvas.perform "activity:sort:*") reorders the app rows at
// once and reveals them from the left (ascending) or the right (descending).
// The authored mask only ever grows: a region once revealed is never covered
// again (the measured Core Animation morph re-covered rows behind curved wedges
// during the first and last ~80 ms, the user-reported glitch).
//
// Synthetic rows and apps only (no clipboard, process or icon is read), a
// hidden window and WARP. With a fifth argument (a NEW absolute directory) the
// frames are also written there as PNG files for inspection.
#include "tools/clipboard_preview.hpp"
#include "tools/activity_preview.hpp"
#include "native/module_scene.hpp"
#include "native/clipboard_assets.hpp"
#include "native/activity_assets.hpp"
#include <ole2.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <sstream>

namespace gpu=endfield::native;namespace core=endfield::core;namespace app=endfield::app;
namespace tools=endfield::tools;namespace mod=endfield::modules;using Json=ehud::data::Json;
namespace {
unsigned checks{};void check(bool v,const char*why){++checks;if(!v)throw std::runtime_error(why);}
struct Com{Com(){check(SUCCEEDED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)),"Create owned COM apartment");}~Com(){CoUninitialize();}};
struct Window{HWND hwnd{};Window(){WNDCLASSW c{};c.lpfnWndProc=DefWindowProcW;c.hInstance=GetModuleHandleW(nullptr);c.lpszClassName=L"EndfieldRevealFramesFixture";check(RegisterClassW(&c)!=0,"Register owned fixture");
    hwnd=CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP|WS_EX_TOOLWINDOW,c.lpszClassName,L"Owned reveal frames fixture",WS_POPUP,0,0,1280,800,nullptr,nullptr,c.hInstance,nullptr);check(hwnd&&!IsWindowVisible(hwnd),"Never show or focus fixture HWND");}
    ~Window(){if(hwnd)DestroyWindow(hwnd);UnregisterClassW(L"EndfieldRevealFramesFixture",GetModuleHandleW(nullptr));}};
Json blank(){return Json::Object{{"bounds",Json::Array{0,0,400,334}},{"position",Json::Array{0,0}},{"anchorPoint",Json::Array{0,0}},{"children",Json::Array{}}};}

struct Box{unsigned x{},y{},width{},height{};};
struct Frame{std::vector<std::uint8_t>pixels;Box box;double elapsed{};};
Frame crop(const gpu::Readback&r,Box b,double elapsed){Frame f;f.box=b;f.elapsed=elapsed;f.pixels.resize(std::size_t(b.width)*b.height*4);
    for(unsigned y=0;y<b.height;++y)std::copy_n(r.pixels.data()+std::size_t(b.y+y)*r.rowBytes+std::size_t(b.x)*4,std::size_t(b.width)*4,f.pixels.data()+std::size_t(y)*b.width*4);
    for(std::size_t n=3;n<f.pixels.size();n+=4)f.pixels[n]=255;return f;}
void writePNG(const std::filesystem::path&file,const Frame&f){
    Microsoft::WRL::ComPtr<IWICImagingFactory>factory;check(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory))),"Create WIC factory");
    Microsoft::WRL::ComPtr<IWICStream>stream;check(SUCCEEDED(factory->CreateStream(&stream))&&SUCCEEDED(stream->InitializeFromFilename(file.c_str(),GENERIC_WRITE)),"Open PNG frame");
    Microsoft::WRL::ComPtr<IWICBitmapEncoder>encoder;check(SUCCEEDED(factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder))&&SUCCEEDED(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache)),"Create PNG encoder");
    Microsoft::WRL::ComPtr<IWICBitmapFrameEncode>frame;check(SUCCEEDED(encoder->CreateNewFrame(&frame,nullptr))&&SUCCEEDED(frame->Initialize(nullptr)),"Create PNG frame");
    WICPixelFormatGUID format=GUID_WICPixelFormat32bppBGRA;check(SUCCEEDED(frame->SetSize(f.box.width,f.box.height))&&SUCCEEDED(frame->SetPixelFormat(&format))&&format==GUID_WICPixelFormat32bppBGRA,"PNG frame format");
    check(SUCCEEDED(frame->WritePixels(f.box.height,f.box.width*4,UINT(f.pixels.size()),const_cast<BYTE*>(f.pixels.data())))&&SUCCEEDED(frame->Commit())&&SUCCEEDED(encoder->Commit()),"Write PNG frame");
}
// Mean absolute channel difference of a sub-rectangle (crop coordinates).
double difference(const Frame&a,const Frame&b,Box r){double sum{};std::size_t count{};
    for(unsigned y=r.y;y<r.y+r.height;++y)for(unsigned x=r.x;x<r.x+r.width;++x){const auto at=(std::size_t(y)*a.box.width+x)*4;for(unsigned k=0;k<3;++k){sum+=std::abs(int(a.pixels[at+k])-int(b.pixels[at+k]));++count;}}
    return count?sum/double(count):0;}

// The hidden HUD plane of one module, its host-like publication and capture.
template<class Owner>struct Stage {
    gpu::Renderer&renderer;Owner&owner;core::Module module;gpu::LayerComposition composition;core::ModulePresentation modules;gpu::LayerScene reference;std::unique_ptr<gpu::NativeModuleSurface>placement;
    core::source::DesktopChromeSettings settings;core::Matrix4 center;core::Matrix4 camera=gpu::layerViewportProjection(1280,800);double time{};bool released{};
    struct Published{gpu::LayerScene*scene{};std::uint64_t content{},resources{};const gpu::DrawObject*after{};std::size_t count{};};
    std::vector<Published>published;std::vector<gpu::LayerCompositionEntry>ordered;Box crops;std::filesystem::path dump;std::string label;std::vector<Frame>frames;
    Stage(gpu::Renderer&r,Owner&o,gpu::LayerRasterizer&raster,core::Module m,std::filesystem::path d,std::string name):renderer(r),owner(o),module(m),modules{m},reference(raster),settings{{0,0,1280,800},1,{},m,true},dump(std::move(d)),label(std::move(name)){
        reference.load(blank(),{});placement=std::make_unique<gpu::NativeModuleSurface>(reference,m);center=core::source::DesktopChromeLayout::make(settings,{},{}).designToScreen;}
    ~Stage(){if(released)return;try{composition.detach(renderer);owner.release(renderer);}catch(...){renderer.reset();}}
    // app/application.cpp Impl::publishNative, for this one owner.
    void publish(){
        owner.upload(renderer);ordered.clear();for(const auto&e:owner.entries())ordered.push_back(e);
        bool changed=ordered.size()!=published.size();if(!changed)for(std::size_t n=0;n<ordered.size();++n){const auto&e=ordered[n];const auto&p=published[n];if(e.scene!=p.scene||e.scene->contentRevision()!=p.content||e.scene->resourceRevision()!=p.resources||e.after.data()!=p.after||e.after.size()!=p.count){changed=true;break;}}
        if(!changed)changed=!composition.supplementalBindingsMatch(ordered);
        if(changed){composition.setEntries(renderer,ordered);published.clear();for(const auto&e:ordered)published.push_back({e.scene,e.scene->contentRevision(),e.scene->resourceRevision(),e.after.data(),e.after.size()});owner.collected(renderer);}
        composition.present(renderer);renderer.setCamera(camera);}
    void frame(double delta){time+=delta;auto sample=modules.sample(time).presentation;settings.module=sample.requested;owner.update(center,settings,sample,1,time);publish();}
    core::Point canvas(core::Point p){auto sample=modules.sample(time).presentation;const auto*surface=sample.current.module==module?&sample.current:&*sample.incoming;placement->update(center,settings,*surface);return *core::Projection::viewport(camera*placement->pose().contentWorld,1280,800).project(p);}
    void frameCrop(){double left=1e9,top=1e9,right=-1e9,bottom=-1e9;for(const auto p:{core::Point{0,0},core::Point{400,0},core::Point{0,334},core::Point{400,334}}){const auto q=canvas(p);left=std::min(left,q.x);right=std::max(right,q.x);top=std::min(top,q.y);bottom=std::max(bottom,q.y);}
        const unsigned x=unsigned(std::max(0.,std::floor(left)-4)),y=unsigned(std::max(0.,std::floor(top)-4));crops={x,y,unsigned(std::min(1280.,std::ceil(right)+4))-x,unsigned(std::min(800.,std::ceil(bottom)+4))-y};}
    // Crop-local rectangle of a canvas rectangle (inside its projection).
    Box region(core::Rect r){double l=1e9,t=1e9,rr=-1e9,b=-1e9;for(const auto p:{core::Point{r.x,r.y},core::Point{r.x+r.width,r.y},core::Point{r.x,r.y+r.height},core::Point{r.x+r.width,r.y+r.height}}){const auto q=canvas(p);l=std::min(l,q.x);rr=std::max(rr,q.x);t=std::min(t,q.y);b=std::max(b,q.y);}
        const auto x=unsigned(std::ceil(l))-crops.x,y=unsigned(std::ceil(t))-crops.y;return Box{x,y,unsigned(std::floor(rr))-crops.x-x,unsigned(std::floor(b))-crops.y-y};}
    void record(std::string_view name,double elapsed){renderer.draw(false);frames.push_back(crop(renderer.readback(),crops,elapsed));
        if(!dump.empty()){std::ostringstream file;file<<label<<'-'<<frames.size()-1<<'-'<<name<<".png";writePNG(dump/file.str(),frames.back());}}
    void finish(){composition.detach(renderer);owner.release(renderer);released=true;check(renderer.stats().textures==0&&renderer.stats().meshes==0,"Owner resources retire through the shared composition");}
};

// frames[0] precedes the change, frames[1...] follow it, frames.back() is settled.
// Left/right halves of each of the four reveal bands of the viewport.
void monotonic(const std::vector<Frame>&frames,Box content,std::span<const Box>still,const char*label){
    const auto&settled=frames.back();std::array<Box,8>cells{};
    for(unsigned band=0;band<4;++band){const unsigned y=content.y+content.height*band/4,height=content.height*(band+1)/4-content.height*band/4;
        cells[band*2]={content.x,y,content.width/2,height};cells[band*2+1]={content.x+content.width/2,y,content.width-content.width/2,height};}
    std::array<double,8>previous{};previous.fill(1e9);std::array<double,8>first{};
    for(std::size_t n=1;n<frames.size();++n){const auto&f=frames[n];std::cout<<label<<' '<<n<<" t="<<f.elapsed<<" vsSettled";
        for(std::size_t k=0;k<cells.size();++k){const auto value=difference(f,settled,cells[k]);std::cout<<' '<<value;
            // A region once revealed is never covered again.
            check(value<=previous[k]+1.,"The reveal never covers what it already revealed");previous[k]=value;if(n==1)first[k]=value;}
        for(const auto&box:still)check(difference(f,settled,box)==0,"Everything outside the revealed collection is final from the first frame");
        std::cout<<'\n';
        if(f.elapsed>=.27)check(difference(f,settled,{0,0,f.box.width,f.box.height})==0,"After .26 s the collection is exactly settled");
    }
    // Animated: the first frame shows the collection still mostly collapsed.
    double hidden{};for(const auto v:first)hidden=std::max(hidden,v);check(hidden>5,"The first frame after the change starts the reveal");
}

void clipboard(gpu::Renderer&renderer,const gpu::NativeClipboardAssets&assets,const std::filesystem::path&dump){
    gpu::LayerRasterizer raster;gpu::ClipboardSnapshot snapshot;snapshot.capacity=32;
    // Exactly six visible rows: removing the last one moves no other row.
    for(unsigned n=1;n<=6;++n)snapshot.rows.push_back({n,false,n%2?gpu::ClipboardKind::text:gpu::ClipboardKind::url,n%2?"Synthetic clipboard text "+std::to_string(n):"https://fixture.invalid/"+std::to_string(n),{}});
    unsigned removes{};
    gpu::ClipboardActions actions{[&]{return snapshot;},[](auto){return true;},[](auto){return false;},[&](auto id){const auto size=snapshot.rows.size();std::erase_if(snapshot.rows,[&](const auto&v){return v.id==id;});if(size==snapshot.rows.size())return false;++removes;return true;},[]{return false;}};
    // The host's Clipboard raster options (app/application.cpp constructClipboard).
    gpu::LayerRasterOptions options;options.pixelsPerPoint=2;options.paddingPoints=1;options.assetRoot=assets.root();
    tools::ClipboardPreview owner(raster,options,std::move(actions),{},{},assets.images(),assets.revealSamples());owner.resize({1280,800,96,1,1280,800});
    Stage stage(renderer,owner,raster,core::Module::clipboard,dump,"clipboard");constexpr double tick=1./60;
    for(unsigned n=0;n<60;++n)stage.frame(tick);check(owner.state().rows().size()==6&&!owner.requiresFrames(stage.time),"Six rows settled");
    stage.frameCrop();const auto content=stage.region(gpu::ClipboardState::contentRect());const auto header=stage.region({0,0,400,38}),toolbar=stage.region({0,292,400,42});
    stage.record("before",0);
    const auto row=*owner.state().rowRect(6,false);const auto cross=stage.canvas({row.x+359.5,row.y+18.5});
    (void)owner.pointer({app::PointerKind::move,app::PointerButton::none,cross.x,cross.y,0},stage.time);stage.frame(.3);
    check(owner.pointer({app::PointerKind::down,app::PointerButton::left,cross.x,cross.y,0},stage.time),"Press on the last row's cross removes it");
    check(removes==1&&owner.state().rows().size()==5,"The row is removed on press (Mac mouseDown)");const auto removedAt=stage.time;
    for(unsigned n=0;n<24;++n){stage.frame(tick);if(n==5)(void)owner.pointer({app::PointerKind::up,app::PointerButton::left,cross.x,cross.y,0},stage.time);stage.record("frame",stage.time-removedAt);}
    stage.frame(.4);stage.record("settled",stage.time-removedAt);check(!owner.requiresFrames(stage.time),"The reveal finished");
    const std::array still{header,toolbar};monotonic(stage.frames,content,still,"clipboard");
    std::cout<<"Clipboard resident raster bytes (5 rows, settled): "<<owner.stats().residentPixelBytes<<'\n';
    // The five remaining rows are revealed again from the left edge.
    stage.finish();
}

void activity(gpu::Renderer&renderer,const std::filesystem::path&root,const std::filesystem::path&dump){
    gpu::LayerRasterizer raster;gpu::NativeActivityAssets assets(root);tools::ActivityPreviewOptions options;options.compareNames=[](auto a,auto b){return a.compare(b);};
    options.initial.timestamp=1;options.initial.uptime=1;options.initial.cpuPercent=10;options.initial.memory=mod::ActivityMemory{100,1000,10};options.sortSamples=assets.sortSamples();
    static constexpr std::array<const char*,8>names{"Quartz","Delta","Alpha","Kilo","Echo","Bravo","Hotel","Golf"};
    for(unsigned k=0;k<8;++k){mod::ActivityApp item;item.identity={"app"+std::to_string(k),std::string(names[k])+" synthetic","",{100+k}};item.cpuPercent=double(3+k*7%11);item.memoryBytes=100000000ULL+k*7000000ULL;options.apps.items.push_back(std::move(item));}
    tools::ActivityPreview owner(raster,std::move(options));owner.resize({1280,800,96,1,1280,800});
    Stage stage(renderer,owner,raster,core::Module::activityMonitor,dump,"activity");constexpr double tick=1./60;
    for(unsigned n=0;n<60;++n)stage.frame(tick);check(owner.perform("activity:apps",stage.time),"Apps tab");for(unsigned n=0;n<40;++n)stage.frame(tick);
    check(owner.state().showingApps()&&!owner.requiresFrames(stage.time),"Apps page settled");
    stage.frameCrop();const auto content=stage.region(mod::ActivityState::appsViewport());const auto heading=stage.region({0,0,400,80}); // heading, tabs and sort headers
    for(const auto&[action,direction]:{std::pair{"activity:sort:name",-1},std::pair{"activity:sort:name",1}}){
        stage.frames.clear();stage.record("before",0);const auto first=owner.state().appSnapshot().items.front().identity.id;
        check(owner.perform(action,stage.time),"Sort header action");check((owner.state().descending()?1:-1)==direction,"Sort direction");
        check(owner.state().appSnapshot().items.front().identity.id!=first,"The rows reordered at once");const auto sortedAt=stage.time;
        for(unsigned n=0;n<24;++n){stage.frame(tick);stage.record("frame",stage.time-sortedAt);}
        stage.frame(.4);stage.record("settled",stage.time-sortedAt);check(!owner.requiresFrames(stage.time),"The sort reveal finished");
        const std::array still{heading};monotonic(stage.frames,content,still,direction<0?"activity-ascending":"activity-descending");
    }
    check(owner.stats().maskRasters>0,"The sort reveal rasterized its authored mask");
    std::cout<<"Activity resident raster bytes (overview and Apps retained, settled): "<<owner.stats().residentPixelBytes<<" in "<<owner.stats().surfaces<<" surfaces\n";
    stage.finish();
}
}
int wmain(int argc,wchar_t**argv){try{check(argc==5||argc==6,"Usage: activity_clipboard_reveal_frames_tests shader clipboard-assets activity-assets clipboard|activity [new-dump-directory]");Com com;Window window;gpu::Renderer renderer;
    renderer.initialize(window.hwnd,1280,800,{gpu::Driver::warpForTests,argv[1],gpu::RenderTarget::offscreenForTests});
    std::filesystem::path dump;if(argc==6){dump=argv[5];check(dump.is_absolute()&&!std::filesystem::exists(dump)&&std::filesystem::create_directory(dump),"Dump directory must be a new absolute directory");}
    const std::wstring mode=argv[4];
    if(mode==L"clipboard"){gpu::NativeClipboardAssets assets(std::filesystem::absolute(argv[2]));clipboard(renderer,assets,dump);}
    else if(mode==L"activity")activity(renderer,std::filesystem::absolute(argv[3]),dump);
    else check(false,"Mode is clipboard or activity");
    renderer.reset();std::cout<<"Reveal frames: "<<checks<<" checks passed\n";return 0;}
    catch(const std::exception&e){std::cerr<<"Reveal frames failed after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}}
#else
int main(){return 0;}
#endif
