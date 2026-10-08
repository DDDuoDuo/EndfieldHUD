#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "tools/notes_preview.hpp"
#include "core/data/data_store.hpp"
#include <objbase.h>
#include <windowsx.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace app=endfield::app;
namespace core=endfield::core;
namespace gpu=endfield::native;
namespace tools=endfield::tools;
namespace data=ehud::data;
using Json=data::Json;
namespace {
std::size_t checks{};
void check(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
struct Apartment {
    Apartment(){check(SUCCEEDED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)),"Initialize owned test apartment");}
    ~Apartment(){CoUninitialize();}
};
struct TempRoot {
    std::filesystem::path path=std::filesystem::absolute(std::filesystem::temp_directory_path())/("endfield-font-host-"+data::makeUUID());
    TempRoot(){check(!std::filesystem::exists(path),"Notes root is a fresh synthetic directory");}
    ~TempRoot(){std::error_code error;std::filesystem::remove_all(path,error);}
};
Json color(double r,double g,double b){return Json::Object{{"sRGB",Json::Array{r,g,b,1}}};}
Json siblings(){
    // The source shell export has 53 native containers. These tiny mixed
    // caption/shape sentinels reproduce shared-cache occupancy and borrowing,
    // not shell artwork. Do not fill the 256-entry cache artificially.
    Json::Array leaves;
    for(unsigned n=0;n<53;++n){
        Json leaf=Json::Object{};
        leaf["id"]="shell-sentinel/"+std::to_string(n);
        leaf["bounds"]=Json::Array{0,0,48,14};
        leaf["position"]=Json::Array{double(n%8)*50,double(n/8)*16};
        leaf["anchorPoint"]=Json::Array{0,0};
        if(n%2){
            leaf["kind"]="text";leaf["class"]="CATextLayer";
            Json font=Json::Object{{"familyName","Segoe UI"},{"postScriptName","SegoeUI"},{"pointSize",10}};
            Json text=Json::Object{};text["string"]="HUD "+std::to_string(n);text["fontSize"]=10;
            text["font"]=std::move(font);text["foregroundColor"]=color(.8,.8,.8);
            text["alignment"]="left";text["wrapped"]=false;text["truncation"]="none";text["runs"]=Json::Array{};
            leaf["text"]=std::move(text);
        }else{leaf["kind"]="layer";leaf["class"]="CALayer";leaf["backgroundColor"]=color(.2,.3,.4);}
        leaves.push_back(std::move(leaf));
    }
    return Json::Object{{"bounds",Json::Array{0,0,0,0}},{"children",std::move(leaves)}};
}
struct Fixture {
    static constexpr UINT frameMarker=WM_APP+711;
    TempRoot root;
    app::OverlayHost host;
    gpu::Renderer renderer;
    gpu::LayerRasterizer raster;
    gpu::LayerScene shell{raster};
    gpu::LayerComposition composition;
    std::unique_ptr<tools::NotesPreview> notes;
    app::ClientMetrics metrics;
    core::source::DesktopChromeSettings settings;
    core::Matrix4 design,camera;
    double time{};
    std::size_t wheelEvents{},consumedWheels{},frames{},nativeFrames{},closeRequests{},unhandledEscapes{},maxEntriesSeen{};
    std::size_t expectedWheelsAtMarker{};
    bool markerSeen{},released{};

    Fixture(const std::filesystem::path& shader,const gpu::NativeNotesControlsAssets& assets,const std::filesystem::path& formats){
        app::OverlayCallbacks callbacks;
        callbacks.resize=[this](const auto& value){metrics=value;};
        callbacks.frame=[this](double){++nativeFrames;};
        callbacks.pointer=[this](const auto& event){time+=.001;const bool result=notes->pointer(event,time);host.invalidate();return result;};
        callbacks.wheel=[this](const auto& event){
            ++wheelEvents;time+=.001;if(notes->wheel(event,time))++consumedWheels;
            maxEntriesSeen=std::max(maxEntriesSeen,raster.stats().entries);host.invalidate();return true;
        };
        callbacks.key=[this](const auto& event){
            time+=.001;if(notes->key(event,time))return true;
            if(event.kind==app::KeyKind::down&&event.value==VK_ESCAPE){++unhandledEscapes;return true;}return false;
        };
        callbacks.closeRequested=[this]{++closeRequests;};
        callbacks.appMessage=[this](const app::NativeMessage& message)->std::optional<std::intptr_t>{
            if(message.message==frameMarker){
                check(wheelEvents==expectedWheelsAtMarker,"Every queued wheel dispatch precedes the only burst presentation");
                frame();markerSeen=true;return 0;
            }
            if(notes&&notes->message(message))return 0;return {};
        };
        host.create({L"Hidden font burst fixture",0,0,1280,800,{}},std::move(callbacks));metrics=host.metrics();
        renderer.initialize(host.hwnd(),metrics.pixelWidth,metrics.pixelHeight,{gpu::Driver::warpForTests,shader,gpu::RenderTarget::offscreenForTests});
        gpu::LayerRasterOptions options;options.pixelsPerPoint=1;shell.load(siblings(),options);
        check(shell.report().unsupported.empty()&&shell.draws().size()==53,"Load exactly 53 bounded mixed sibling surfaces");
        notes=std::make_unique<tools::NotesPreview>(window(),raster,root.path,assets,false,formats);
        notes->resize(metrics);settings.viewport={0,0,metrics.width,metrics.height};settings.module=core::Module::notes;settings.sourceShell=true;
        design=core::source::DesktopChromeLayout::make(settings,{},{}).designToScreen;
        camera=gpu::layerViewportProjection(metrics.pixelWidth,metrics.pixelHeight)*core::Matrix4::scale(metrics.scale,metrics.scale);
        frame();
        check(!IsWindowVisible(window())&&!host.stats().timerArmed,"Hidden fixture owns no visible window or scheduled animation");
    }
    ~Fixture(){if(!released)try{release();}catch(...){renderer.reset();notes.reset();}}
    HWND window()const{return static_cast<HWND>(host.hwnd());}
    void frame(){
        notes->update(design,settings,1,time,true);notes->upload(renderer);
        std::vector<gpu::LayerCompositionEntry> entries;entries.push_back({&shell,{}});
        for(const auto&entry:notes->entries())entries.push_back(entry);
        composition.setEntries(renderer,entries);notes->collected(renderer);composition.present(renderer);renderer.setCamera(camera);renderer.draw(false);++frames;
        maxEntriesSeen=std::max(maxEntriesSeen,raster.stats().entries);
    }
    void settle(){time+=.3;frame();}
    void drain(){for(unsigned n=0;n<4;++n)check(host.pumpOnce(0),"Hidden host remains running after bounded message dispatch");}
    LPARAM point(core::Point p,bool screen=false)const{
        POINT pixel{static_cast<LONG>(std::lround(p.x*metrics.scale)),static_cast<LONG>(std::lround(p.y*metrics.scale))};
        if(screen)check(ClientToScreen(window(),&pixel)!=FALSE,"Convert owned wheel point to native screen coordinates");
        check(pixel.x>=-32768&&pixel.x<=32767&&pixel.y>=-32768&&pixel.y<=32767,"Fixture coordinates fit native message fields");
        return MAKELPARAM(static_cast<WORD>(pixel.x),static_cast<WORD>(pixel.y));
    }
    void post(UINT message,WPARAM w=0,LPARAM l=0){check(PostMessageW(window(),message,w,l)!=FALSE,"Queue input only to the owned hidden HWND");}
    void click(core::Point p,bool doubleClick=false){
        post(doubleClick?WM_LBUTTONDBLCLK:WM_LBUTTONDOWN,MK_LBUTTON,point(p));post(WM_LBUTTONUP,0,point(p));drain();settle();
    }
    bool drawContains(std::string_view id)const{
        return std::any_of(composition.draws().begin(),composition.draws().end(),[&](const auto&draw){return draw.sourceID.find(id)!=std::string::npos;});
    }
    void escape(){post(WM_KEYDOWN,VK_ESCAPE,1);post(WM_KEYUP,VK_ESCAPE,static_cast<LPARAM>(0xc0000001u));drain();settle();}
    void burst(core::Point p,int direction){
        const auto before=frames,events=wheelEvents,consumed=consumedWheels;
        const auto position=point(p,true);expectedWheelsAtMarker=events+32;markerSeen=false;
        for(unsigned n=0;n<32;++n)post(WM_MOUSEWHEEL,MAKEWPARAM(0,static_cast<WORD>(direction*WHEEL_DELTA)),position);
        // Hidden scheduling is deliberately gated. This explicit queued test
        // marker renders only after the 32 real host wheel dispatches, without
        // exposing a window, running a clock, or activating a text service.
        post(frameMarker);drain();
        check(markerSeen&&frames==before+1&&wheelEvents==events+32&&consumedWheels==consumed+32,"32 queued font wheels consume without an intervening frame");
        check(nativeFrames==0&&host.stats().framePosts==0&&!host.stats().timerArmed,"The isolated burst activates no hidden frame loop");
        check(closeRequests==0&&unhandledEscapes==0&&IsWindow(window())&&!IsWindowVisible(window()),"Wheel burst never requests close, destroys, or shows the HUD");
        check(notes->selected()==core::Module::notes&&drawContains("projected-editor-glyphs")&&drawContains("notes.format"),"Burst preserves module, editor and retained font menu");
    }
    void release(){
        if(released)return;composition.detach(renderer);if(notes){notes->finish();notes->release(renderer);notes.reset();}
        check(shell.releaseResources(renderer),"Sibling GPU resources detach after the shared publication");
        check(raster.stats().entries==53&&renderer.stats().meshes==0&&renderer.stats().textures==0&&renderer.stats().nativeGroups==0,"Owner teardown retires Notes/menu resources while sibling rasters remain borrowed");
        renderer.reset();host.destroy();released=true;
    }
};
void run(const std::filesystem::path& shader,const gpu::NativeNotesControlsAssets& assets,const std::filesystem::path& formats){
    Fixture f(shader,assets,formats);data::Note note;
    {data::NotesStore store(f.root.path);check(store.notes().size()==1,"Use only the fresh injected sample note");note=store.notes().front();}
    f.click({note.x+30,note.y+45},true);
    check(f.drawContains("projected-editor-glyphs"),"Native pointer dispatch enters the real projected editor");
    f.click({note.x+38,note.y+note.height-15});
    check(f.drawContains("notes.format"),"Native pointer dispatch opens actual installed-font menu");
    const double left=std::min(f.metrics.width-242.,std::max(0.,note.x));
    const double top=std::min(f.metrics.height-184.,std::max(0.,note.y+note.height+4));
    const core::Point choice{left+22,top+18},wheel{left+60,top+75};
    f.click(choice);
    const auto siblingContent=f.shell.contentRevision(),siblingResources=f.shell.resourceRevision();
    const auto firstSibling=f.shell.draws().front();
    const auto initialEntries=f.raster.stats().entries;const auto initialGPU=f.renderer.stats();
    check(initialEntries>53&&initialEntries<gpu::LayerRasterizer::maximumEntries-28,"Representative siblings and real Notes/menu leave one source page of staging capacity");
    for(unsigned round=0;round<4;++round){
        f.burst(wheel,round%2?1:-1);f.click(choice);
        check(f.shell.contentRevision()==siblingContent&&f.shell.resourceRevision()==siblingResources&&f.shell.draws().front().sourceID==firstSibling.sourceID&&f.shell.draws().front().meshID==firstSibling.meshID,"Font replacement never changes sibling identities or content revisions");
        check(f.raster.stats().entries==initialEntries&&f.renderer.stats().textures==initialGPU.textures&&f.renderer.stats().meshes==initialGPU.meshes,"Recycled font pages retire old resources after every burst");
    }
    f.escape();check(f.unhandledEscapes==0&&!f.drawContains("notes.format")&&f.drawContains("projected-editor-glyphs"),"First queued Escape dismisses only menu after its finite fade");
    f.escape();check(f.unhandledEscapes==0&&!f.drawContains("projected-editor-glyphs"),"Second queued Escape finishes only Notes editing");
    f.escape();check(f.unhandledEscapes==1&&f.closeRequests==0,"Only a third unhandled Escape reaches the caller's shell-close route");
    {data::NotesStore store(f.root.path);check(store.notes().size()==1&&store.notes().front().text==note.text,"Font selection and wheel bursts preserve note text");}
    f.post(WM_CLOSE);f.drain();check(f.closeRequests==1,"Native close requests are observably distinct from wheel and Escape routing");
    std::cout<<"Font burst shared raster entries: "<<initialEntries<<", high-water observed: "<<f.maxEntriesSeen<<" / "<<gpu::LayerRasterizer::maximumEntries<<'\n';
    f.release();
}
void callbackException(){
    // Verify the real USER32 exception boundary. No deliberately invalid menu
    // or artificially exhausted resource budget is needed to prove this route.
    app::OverlayHost host;unsigned closeRequests{},wheelEvents{};
    app::OverlayCallbacks callbacks;callbacks.closeRequested=[&]{++closeRequests;};
    callbacks.wheel=[&](const app::WheelEvent&)->bool{++wheelEvents;throw std::runtime_error("font-host synthetic callback sentinel");};
    host.create({L"Hidden callback error fixture",0,0,80,60,{}},std::move(callbacks));const auto hwnd=static_cast<HWND>(host.hwnd());
    POINT point{10,10};check(ClientToScreen(hwnd,&point)!=FALSE,"Map exception fixture's owned point");
    check(PostMessageW(hwnd,WM_MOUSEWHEEL,MAKEWPARAM(0,static_cast<WORD>(-WHEEL_DELTA)),MAKELPARAM(point.x,point.y))!=FALSE,"Queue one controlled failing callback");
    bool surfaced{};try{host.pumpOnce(0);}catch(const std::runtime_error&error){surfaced=std::string_view(error.what())=="font-host synthetic callback sentinel";}
    check(surfaced&&wheelEvents==1,"Native callback failure is propagated unchanged through pumpOnce");
    check(closeRequests==0&&IsWindow(hwnd)&&!IsWindowVisible(hwnd)&&!host.stats().timerArmed,"Callback exception stops work without inventing a close event or visible window");
    check(!host.pumpOnce(0),"A failed native callback stops the host rather than silently continuing");host.destroy();
}
std::string ascii(const wchar_t* value){std::string result;for(;*value;++value){check(*value>=0&&*value<128,"Fixture build pins are ASCII");result.push_back(static_cast<char>(*value));}return result;}
}
int wmain(int argc,wchar_t**argv){try{
    check(argc==6,"Usage: notes_format_host_tests shader Notes-assets manifest-SHA source-commit format-assets");
    Apartment apartment;
    const gpu::NativeNotesControlsAssets assets(std::filesystem::absolute(argv[2]),{ascii(argv[3]),ascii(argv[4])});
    run(std::filesystem::absolute(argv[1]),assets,std::filesystem::absolute(argv[5]));callbackException();
    std::cout<<"Native Notes formatting host: "<<checks<<" checks passed (hidden; no real TSF activation)\n";return 0;
}catch(const std::exception&error){std::cerr<<"Native Notes formatting host failed after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}}
#endif
