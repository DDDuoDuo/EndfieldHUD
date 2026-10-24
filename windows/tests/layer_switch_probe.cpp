#ifdef _WIN32
// Module switches inside the open HUD never paint again. The scenes of a
// module the composition dropped gave their CPU pixels back once uploaded;
// leaving, their textures are parked (texels to system memory, no GPU wait)
// and a switch back restores them as a plain upload. Measured through the real
// owners of the modules that keep their artwork while switched away inside the
// open HUD - Media Assembly and Archive (its stand-by: the open document, its
// editors and their pixels) - published as
// the host publishes owners (app/application.cpp publishNative), with
// animated switches at 60 Hz on a hidden window and WARP:
//   - the first showing (raster and upload),
//   - the switch away (the departure parks the textures),
//   - the switch back (restored: must cost about an upload, paint nothing and
//     show identical pixels),
//   - and, for comparison, a switch back after the ten-minute release (the
//     parked copies dropped: the surfaces are painted again).
// Each frame is timed (owner update, publication, draw). Synthetic rows,
// events, documents and an empty editor only; temporary folders.
#include "tools/media_assembly_owner.hpp"
#include "tools/archive_owner.hpp"
#include "tools/hud_design_overlay_owner.hpp"
#include "native/archive_text_rules.hpp"
#include "native/notes_image_playback.hpp"
#include "native/module_scene.hpp"
#include "native/layer_image_source.hpp"
#include "native/media_assembly_codec.hpp"
#include "core/data/file_io.hpp"
#include "app/utility_executor.hpp"
#include <objbase.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <iostream>
#include <optional>

namespace gpu=endfield::native;namespace core=endfield::core;namespace app=endfield::app;
namespace tools=endfield::tools;namespace mod=endfield::modules;using Json=ehud::data::Json;
namespace {
unsigned checks{};void check(bool v,const char*why){++checks;if(!v)throw std::runtime_error(why);}
struct Com{Com(){check(SUCCEEDED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)),"Create owned COM apartment");}~Com(){CoUninitialize();}};
struct Window{HWND hwnd{};Window(){WNDCLASSW c{};c.lpfnWndProc=DefWindowProcW;c.hInstance=GetModuleHandleW(nullptr);c.lpszClassName=L"EndfieldLayerSwitchProbe";check(RegisterClassW(&c)!=0,"Register owned fixture");
    hwnd=CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP|WS_EX_TOOLWINDOW,c.lpszClassName,L"Owned layer switch probe",WS_POPUP,0,0,1280,800,nullptr,nullptr,c.hInstance,nullptr);check(hwnd&&!IsWindowVisible(hwnd),"Never show or focus fixture HWND");}
    ~Window(){if(hwnd)DestroyWindow(hwnd);UnregisterClassW(L"EndfieldLayerSwitchProbe",GetModuleHandleW(nullptr));}};
struct Temporary{std::filesystem::path root=std::filesystem::canonical(std::filesystem::temp_directory_path())/("EndfieldHUD-Layer-Switch-"+ehud::data::makeUUID());
    Temporary(){std::filesystem::create_directory(root);}~Temporary(){std::error_code error;std::filesystem::remove_all(root,error);}};
using Clock=std::chrono::steady_clock;
double since(Clock::time_point from){return std::chrono::duration<double,std::milli>(Clock::now()-from).count();}

// One design module owner on the hidden HUD plane with the shared overlay
// owner's Context, starting on another module.
template<class Owner>struct Stage {
    gpu::Renderer&renderer;Owner&owner;tools::DesignOverlayOwner&overlay;core::Module module;gpu::LayerComposition composition;core::ModulePresentation modules{core::Module::power};
    core::source::DesktopChromeSettings settings;core::Matrix4 center;core::Matrix4 camera=gpu::layerViewportProjection(1280,800);double time{};bool released{};
    std::function<void()>drain;
    Stage(gpu::Renderer&r,Owner&o,tools::DesignOverlayOwner&ov,core::Module m):renderer(r),owner(o),overlay(ov),module(m),settings{{0,0,1280,800},1,{},core::Module::power,true}{
        modules.setDesignScreen(m,true);center=core::source::DesktopChromeLayout::make(settings,{},{}).designToScreen;}
    ~Stage(){if(released)return;try{composition.detach(renderer);owner.release(renderer);overlay.release(renderer);}catch(...){renderer.reset();}}
    // A host frame: the owners' update and upload, the publication (one
    // full list when entries changed) and the draw. Milliseconds of CPU.
    double frame(double delta=1./60){const auto begin=Clock::now();time+=delta;(void)overlay.deadline(time);(void)owner.deadline(time);
        auto sample=modules.sample(time).presentation;settings.module=sample.requested;const app::ModuleFrame f{center,settings,sample,1,time,true};
        overlay.update(f);owner.update(f);overlay.upload(renderer);owner.upload(renderer);
        std::vector<gpu::LayerCompositionEntry>entries;for(const auto&e:owner.entries(sample.current.module))entries.push_back(e);
        if(sample.incoming)for(const auto&e:owner.entries(sample.incoming->module))entries.push_back(e);for(const auto&e:overlay.modalEntries())entries.push_back(e);
        composition.setEntries(renderer,entries);owner.collected(renderer);overlay.collected(renderer);composition.present(renderer);renderer.setCamera(camera);renderer.draw(false);return since(begin);}
    // An animated switch at 60 Hz until it settled: its slowest frame.
    double select(core::Module m){modules.select(m,time,true);double slowest{};for(unsigned n=0;n<60;++n)slowest=std::max(slowest,frame());if(drain){drain();for(unsigned n=0;n<30;++n)slowest=std::max(slowest,frame());}return slowest;}
    std::vector<std::uint8_t>capture(){renderer.draw(false);return renderer.readback().pixels;}
    void finish(){composition.detach(renderer);owner.release(renderer);overlay.release(renderer);released=true;}
};

// `open`: after the first showing, the state the switches keep (Archive's
// open document), settled before the pixels are captured.
template<class Owner>void probe(const char*label,gpu::Renderer&renderer,gpu::LayerRasterizer&raster,Owner&owner,tools::DesignOverlayOwner&overlay,core::Module module,std::function<void()>drain={},std::function<void(Stage<Owner>&)>open={}){
    Stage stage(renderer,owner,overlay,module);stage.drain=std::move(drain);for(unsigned n=0;n<12;++n)stage.frame();
    const auto g0=renderer.stats();const double build=stage.select(module);if(open)open(stage);const auto shown=stage.capture();const auto g1=renderer.stats();const auto r1=raster.stats();
    const double away=stage.select(core::Module::power);const auto g2=renderer.stats();const double parking=(g2.parkSeconds-g1.parkSeconds)*1000;
    const double back=stage.select(module);const auto g3=renderer.stats();const auto r3=raster.stats();
    check(r3.pixelRepaints==r1.pixelRepaints,"A switch back inside the open HUD paints nothing again");
    check(stage.capture()==shown,"The switch back shows identical pixels");
    // For comparison: the ten-minute release drops the parked copies, after
    // which the same switch back paints the released surfaces again.
    (void)stage.select(core::Module::power);check(renderer.suspendDevice(),"The closed renderer releases its device and its parked copies");
    const double repaint=stage.select(module);const auto r4=raster.stats();
    check(stage.capture()==shown,"After the ten-minute release the switch back shows identical pixels");
    std::printf("%-15s parked %7.1f KiB in %3zu textures | first showing %6.1f ms (upload %5.2f ms of it) | away %5.1f ms (parking %4.2f ms) | back %5.1f ms (%llu restored in %5.2f ms, %llu rasters) | back after the ten-minute release %6.1f ms (%llu surfaces painted again)\n",
        label,double(g2.parkedBytes)/1024,g2.parkedTextures,build,(g1.textureUploadSeconds-g0.textureUploadSeconds)*1000,away,parking,back,static_cast<unsigned long long>(g3.parkedRestores-g2.parkedRestores),
        (g3.parkedRestoreSeconds-g2.parkedRestoreSeconds)*1000,static_cast<unsigned long long>(r3.rasterizations-r1.rasterizations),repaint,static_cast<unsigned long long>(r4.pixelRepaints-r3.pixelRepaints));
    stage.finish();
}

void mediaAssembly(gpu::Renderer&renderer,HWND hwnd,const std::filesystem::path&assets){
    gpu::LayerRasterizer raster;app::UtilityExecutor utility([]{});auto engine=std::make_shared<gpu::NativeMediaAssemblyEngine>(gpu::NativeMediaAssemblyEngine::Options{assets});unsigned ids{};
    const app::ClientMetrics metrics{1280,800,96,1,1280,800};app::ModuleAppearance appearance;appearance.accentSRGB={250./255,212./255,31./255};
    tools::DesignOverlayOptions overlayOptions;overlayOptions.raster.pixelsPerPoint=2;overlayOptions.window=hwnd;tools::DesignOverlayOwner overlay(raster,overlayOptions);
    overlay.resize(metrics);overlay.setAppearance(appearance,0);overlay.setOverlayVisible(true,0);
    tools::MediaAssemblyOwnerOptions options;options.design.name="mediaAssembly";options.design.raster.pixelsPerPoint=2;options.assetRoot=assets;options.window=hwnd;options.event=[](std::string_view){};
    options.uuid=[&]{return "probe-"+std::to_string(++ids);};options.dialog=[](HWND,const gpu::MediaAssemblyDialogRequest&){return std::optional<std::string>{};};
    tools::MediaAssemblyOwner owner(raster,utility,overlay.context(),engine,std::move(options));owner.resize(metrics);owner.setAppearance(appearance,0);owner.setOverlayVisible(true,0);
    probe("Media Assembly",renderer,raster,owner,overlay,core::Module::mediaAssembly,[&]{for(int k=0;k<200;++k){utility.waitIdle();if(!utility.drain())break;}});
}
// v1.2.0's case: six journal entries in two categories, the last one open
// (title, two-paragraph body: the persistent body editor attached).
void archive(gpu::Renderer&renderer,HWND hwnd,const std::filesystem::path&notesFormat){
    Temporary temporary;gpu::LayerRasterizer raster;app::UtilityExecutor utility([]{});
    gpu::LayerImageSource images{gpu::LayerImageSourceLimits{30,48,12*1024*1024}};
    app::ArchiveService service(temporary.root/"Archive",utility,gpu::nativeArchiveTextRules(),[]{},std::make_shared<mod::ArchiveMemoryPasswordVault>());
    const gpu::NotesImageRoute route{hwnd,WM_APP+310,1};
    gpu::NativeNotesImageDecoder decoder([](const gpu::NotesImageRequest&r){return gpu::NotesImageAccess{std::filesystem::u8path(r.path),r.accessLease};},route);
    gpu::NativeNotesImagePlayback playback(decoder);gpu::NativeMediaRequestBroker broker(decoder,playback,route);const auto client=broker.attachClient();
    const app::ClientMetrics metrics{1280,800,96,1,1280,800};app::ModuleAppearance appearance;appearance.accentSRGB={250./255,212./255,31./255};
    tools::DesignOverlayOptions overlayOptions;overlayOptions.raster.pixelsPerPoint=2;overlayOptions.window=hwnd;tools::DesignOverlayOwner overlay(raster,overlayOptions);
    overlay.resize(metrics);overlay.setAppearance(appearance,0);overlay.setOverlayVisible(true,0);
    double now{};
    tools::ArchiveOwnerOptions o;o.owner.name="archive";o.owner.routing=tools::ArchiveOwner::routing0();o.owner.raster.pixelsPerPoint=2;o.owner.raster.assetRoot=notesFormat;
    o.owner.images[std::string(mod::notesColorWheelImageKey)]=mod::notesColorWheelArtwork();
    o.owner.fields.window=hwnd;o.owner.fields.ownerMessage=WM_APP+311;o.owner.fields.raster=o.owner.raster;
    o.owner.fields.clipboard.read=[]{return std::optional<std::u16string>{};};o.owner.fields.clipboard.write=[](std::u16string_view){return true;};
    o.owner.now=[&]{return now;};o.newID=ehud::data::makeUUID;o.foundationNow=[]{return 811692800.;};o.dotted=[](double){return std::string("2026.09.21");};
    o.fonts=tools::archiveFontServices(raster);o.shelfChoices=[]{return std::vector<mod::NotesShelfChoice>{};};o.requestMedia=[](const mod::ArchiveMediaRequest&){return false;};
    o.media.resolve=[](const ehud::data::Json&r){return gpu::NotesImageAccess{std::filesystem::u8path(r["windowsPath"].string()),{}};};o.media.errorText=[](HRESULT){return std::string("Media error");};
    o.ownerAuthentication=[](std::string,std::function<void(bool)>done){done(false);};o.bodyMessage=WM_APP+312;
    tools::ArchiveOwner owner(raster,overlay.context(),service,broker,client,renderer,images,std::move(o));owner.resize(metrics);owner.setAppearance(appearance,0);owner.setOverlayVisible(true,0);
    auto&state=service.state();state.activate();
    const auto travel=state.createCategory(ehud::data::makeUUID(),"Travel",811692800.),work=state.createCategory(ehud::data::makeUUID(),"Work",811692800.);check(travel&&work,"Archive categories are created");
    std::string last;
    for(unsigned n=0;n<6;++n){last=ehud::data::makeUUID();check(state.create(last,811692800.-n*86400.,n%2?work:travel),"An Archive entry is created");
        auto entry=state.selected().value();entry.title="Synthetic entry "+std::to_string(n+1);entry.body="Body text of synthetic entry "+std::to_string(n+1)+".\nA second paragraph.";check(state.update(entry,811692800.,0),"An Archive entry is written");}
    state.flush();
    const auto drain=[&]{for(int k=0;k<200;++k){utility.waitIdle();if(!utility.drain())break;owner.stateChanged(now);}};
    drain();
    probe<tools::ArchiveOwner>("Archive",renderer,raster,owner,overlay,core::Module::archive,drain,[&](Stage<tools::ArchiveOwner>&stage){
        now=stage.time;
        // The last entry created is the selected one: the canvas opens on it
        // (as v1.2.0's preview did); otherwise it is opened from the list.
        const auto editor=[&]{return owner.canvas().displayedPage().kind==mod::ArchiveCanvas::PageKind::editor;};
        if(!editor())check(owner.canvas().performAccessibilityAction("doc:"+last,stage.time),"The last Archive entry opens");
        for(unsigned n=0;n<90&&!(editor()&&owner.body().shown());++n){stage.frame();if(n%10==9)drain();}
        check(editor()&&owner.body().shown()&&owner.state().selected()&&owner.state().selected()->id==last,"The editor shows the last entry with its body editor");
        for(unsigned n=0;n<30;++n)stage.frame();});
}
}
int wmain(int argc,wchar_t**argv){try{check(argc==4,"Usage: layer_switch_probe shader resources media-assembly-resources");Com com;Window window;
    gpu::Renderer renderer;renderer.initialize(window.hwnd,1280,800,{gpu::Driver::warpForTests,argv[1],gpu::RenderTarget::offscreenForTests});
    const auto resources=std::filesystem::absolute(argv[2]);
    mediaAssembly(renderer,window.hwnd,std::filesystem::absolute(argv[3]).lexically_normal());archive(renderer,window.hwnd,resources/"notes-format");
    renderer.reset();std::cout<<"Layer switch probe: "<<checks<<" checks passed\n";return 0;}
    catch(const std::exception&e){std::cerr<<"Layer switch probe failed after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}}
#else
int main(){return 0;}
#endif
