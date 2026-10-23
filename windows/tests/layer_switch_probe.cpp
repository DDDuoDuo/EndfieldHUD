#ifdef _WIN32
// Module switches inside the open HUD never paint again. The scenes of a
// module the composition dropped gave their CPU pixels back once uploaded;
// leaving, their textures are parked (texels to system memory, no GPU wait)
// and a switch back restores them as a plain upload. Measured through the real
// owners of the modules that keep their artwork while switched away inside the
// open HUD - Clipboard, Event Log, Media Assembly and Archive - published as
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
#include "tools/clipboard_preview.hpp"
#include "tools/event_log_preview.hpp"
#include "tools/media_assembly_preview.hpp"
#include "tools/archive_preview.hpp"
#include "native/module_scene.hpp"
#include "native/clipboard_assets.hpp"
#include "native/layer_image_source.hpp"
#include "native/media_assembly_codec.hpp"
#include "native/archive_text_rules.hpp"
#include "modules/archive_repository.hpp"
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
Json blank(){return Json::Object{{"bounds",Json::Array{0,0,400,440}},{"position",Json::Array{0,0}},{"anchorPoint",Json::Array{0,0}},{"children",Json::Array{}}};}
using Clock=std::chrono::steady_clock;
double since(Clock::time_point from){return std::chrono::duration<double,std::milli>(Clock::now()-from).count();}

// One module owner on the hidden HUD plane, starting on another module.
template<class Owner>struct Stage {
    gpu::Renderer&renderer;Owner&owner;core::Module module;gpu::LayerComposition composition;core::ModulePresentation modules{core::Module::power};gpu::LayerScene reference;std::unique_ptr<gpu::NativeModuleSurface>placement;
    core::source::DesktopChromeSettings settings;core::Matrix4 center;core::Matrix4 camera=gpu::layerViewportProjection(1280,800);double time{};bool released{};
    std::function<void()>drain;
    Stage(gpu::Renderer&r,Owner&o,gpu::LayerRasterizer&raster,core::Module m):renderer(r),owner(o),module(m),reference(raster),settings{{0,0,1280,800},1,{},core::Module::power,true}{
        reference.load(blank(),{});placement=std::make_unique<gpu::NativeModuleSurface>(reference,m);center=core::source::DesktopChromeLayout::make(settings,{},{}).designToScreen;}
    ~Stage(){if(released)return;try{composition.detach(renderer);owner.release(renderer);}catch(...){renderer.reset();}}
    // A host frame: the owner's update and upload, the publication (one
    // full list when entries changed) and the draw. Milliseconds of CPU.
    double frame(double delta=1./60){const auto begin=Clock::now();time+=delta;auto sample=modules.sample(time).presentation;settings.module=sample.requested;owner.update(center,settings,sample,1,time);
        owner.upload(renderer);composition.setEntries(renderer,owner.entries());owner.collected(renderer);composition.present(renderer);renderer.setCamera(camera);renderer.draw(false);return since(begin);}
    // An animated switch at 60 Hz until it settled: its slowest frame.
    double select(core::Module m){modules.select(m,time,true);double slowest{};for(unsigned n=0;n<60;++n)slowest=std::max(slowest,frame());if(drain){drain();for(unsigned n=0;n<30;++n)slowest=std::max(slowest,frame());}return slowest;}
    std::vector<std::uint8_t>capture(){renderer.draw(false);return renderer.readback().pixels;}
    void finish(){composition.detach(renderer);owner.release(renderer);released=true;}
};

template<class Owner>void probe(const char*label,gpu::Renderer&renderer,gpu::LayerRasterizer&raster,Owner&owner,core::Module module,std::function<void()>drain={}){
    Stage stage(renderer,owner,raster,module);stage.drain=std::move(drain);for(unsigned n=0;n<12;++n)stage.frame();
    const auto g0=renderer.stats();const double build=stage.select(module);const auto shown=stage.capture();const auto g1=renderer.stats();const auto r1=raster.stats();
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

void clipboard(gpu::Renderer&renderer,const std::filesystem::path&root){
    gpu::NativeClipboardAssets assets(root);gpu::LayerRasterizer raster;gpu::LayerImageSource thumbnails;gpu::ClipboardSnapshot snapshot;snapshot.capacity=20;
    for(std::uint64_t n=1;n<=8;++n){gpu::ClipboardRow row{n,n==1,n%2?gpu::ClipboardKind::text:gpu::ClipboardKind::url,n%2?"Synthetic clipboard text "+std::to_string(n):"https://fixture.invalid/"+std::to_string(n),{}};
        if(n==3){row.kind=gpu::ClipboardKind::image;row.preview="Image · 8 × 8";row.thumbnail=Json::Object{{"memoryImage","fixture-thumbnail/3"},{"revision",1}};}snapshot.rows.push_back(std::move(row));}
    std::vector<std::uint8_t>pixels(8*8*4);for(std::size_t k=0;k<pixels.size();k+=4){pixels[k]=200;pixels[k+1]=60;pixels[k+2]=30;pixels[k+3]=255;}
    gpu::ClipboardActions actions{[&]{return snapshot;},[](auto){return true;},[](auto){return false;},[](auto){return false;},[]{return false;},
        [&](std::span<const std::uint64_t>ids){for(const auto id:ids)if(id==3&&!thumbnails.acquire("fixture-thumbnail/3",1))(void)thumbnails.publish("fixture-thumbnail/3",1,8,8,pixels);}};
    gpu::LayerRasterOptions options;options.pixelsPerPoint=2;options.paddingPoints=1;options.assetRoot=assets.root();options.memoryImages=&thumbnails;
    tools::ClipboardPreview owner(raster,options,std::move(actions),{},{},assets.images());owner.resize({1280,800,96,1,1280,800});
    probe("Clipboard",renderer,raster,owner,core::Module::clipboard);
}
void eventLog(gpu::Renderer&renderer){
    gpu::LayerRasterizer raster;mod::EventLogSnapshot events;
    for(unsigned k=1;k<=24;++k){char id[64];std::snprintf(id,sizeof(id),"00000000-0000-0000-0000-%012u",k);events.events.push_back({id,k%2?mod::EventKind::clipboardCopied:mod::EventKind::shelfAdded,1700000000.-k*60,k%2?mod::EventMetadata{{"kind","text"}}:mod::EventMetadata{{"filename","Synthetic.pdf"}}});}
    mod::EventLogCallbacks callbacks{[&]{return events;},[]{},[](double){return std::string("11-14 22:13:19");}};
    tools::EventLogPreview owner(raster,gpu::LayerRasterOptions{},std::move(callbacks));owner.resize({1280,800,96,1,1280,800});
    probe("Event Log",renderer,raster,owner,core::Module::eventLog);
}
void mediaAssembly(gpu::Renderer&renderer,HWND hwnd,const std::filesystem::path&assets){
    gpu::LayerRasterizer raster;app::UtilityExecutor utility([]{});auto engine=std::make_shared<gpu::NativeMediaAssemblyEngine>(gpu::NativeMediaAssemblyEngine::Options{assets});unsigned ids{};
    tools::MediaAssemblyPreviewOptions options;options.raster.pixelsPerPoint=2;options.raster.assetRoot=assets;options.window=hwnd;options.changed=[]{};options.event=[](std::string_view){};
    options.uuid=[&]{return "probe-"+std::to_string(++ids);};options.dialog=[](HWND,const gpu::MediaAssemblyDialogRequest&){return std::optional<std::string>{};};
    options.shelfChoices=[]{return std::vector<mod::NotesShelfChoice>{};};options.shelfAccess=[](std::string_view)->std::optional<tools::MediaAssemblyShelfAccess>{return {};};
    tools::MediaAssemblyPreview owner(raster,utility,engine,std::move(options));owner.resize({1280,800,96,1,1280,800});
    probe("Media Assembly",renderer,raster,owner,core::Module::mediaAssembly,[&]{for(int k=0;k<200;++k){utility.waitIdle();if(!utility.drain())break;}});
}
mod::ArchiveExecutor immediate(){return {[](auto work,auto done){std::exception_ptr error;try{work();}catch(...){error=std::current_exception();}done(error);return true;}};}
void archive(gpu::Renderer&renderer,HWND hwnd,const std::filesystem::path&fixturePath,const std::filesystem::path&assetRoot){
    const auto bytes=ehud::data::detail::readFile(fixturePath,2*1024*1024);check(bytes.has_value(),"Original compact Archive fixture exists");const auto fixture=Json::parse(*bytes,2*1024*1024);
    Temporary temporary;gpu::LayerRasterizer raster;const auto rules=gpu::nativeArchiveTextRules();auto repository=std::make_shared<mod::ArchiveSQLiteRepository>(temporary.root/"Archive",rules);mod::ArchiveState state(repository,immediate(),rules);
    tools::ArchivePreviewOptions options;options.raster.assetRoot=assetRoot;options.raster.pixelsPerPoint=2;options.view.appearance.contentsScale=2;options.view.appearance.systemOrange=mod::ArchiveColor{1,159./255,10./255,1};
    options.view.iconContents=Json::Object{{"asset",fixture["rasterAssets"].array().front()["path"]},{"sha256",fixture["rasterAssets"].array().front()["sha256"]}};options.view.dateString=[](double){return "2026-09-21";};
    options.view.categoryCaptionWidth=[](std::string_view){return 75.;};options.newID=[]{return ehud::data::makeUUID();};options.foundationNow=[]{return 811692800.;};
    options.parseLocalDate=[](std::string_view value)->std::optional<double>{return value=="2026-09-21"?std::optional(811692800.):std::nullopt;};options.fontFamilies=[]{return std::vector<std::string>{"Segoe UI","Consolas"};};
    tools::ArchivePreview owner(state,hwnd,raster,nullptr,TF_CLIENTID_NULL,options);owner.resize({1280,800,96,1,1280,800});
    // Six journal entries in two categories, the last one open.
    state.activate();const auto travel=state.createCategory(ehud::data::makeUUID(),"Travel",811692800.);const auto work=state.createCategory(ehud::data::makeUUID(),"Work",811692800.);
    check(travel&&work,"Archive categories are created");
    for(unsigned n=0;n<6;++n){check(state.create(ehud::data::makeUUID(),811692800.-n*86400.,n%2?work:travel),"An Archive entry is created");
        auto entry=state.selected().value();entry.title="Synthetic entry "+std::to_string(n+1);entry.body="Body text of synthetic entry "+std::to_string(n+1)+".\nA second paragraph.";check(state.update(entry,811692800.,0),"An Archive entry is written");}
    state.flush();
    probe("Archive",renderer,raster,owner,core::Module::archive);
}
}
int wmain(int argc,wchar_t**argv){try{check(argc==5,"Usage: layer_switch_probe shader resources archive-fixture media-assembly-resources");Com com;Window window;
    gpu::Renderer renderer;renderer.initialize(window.hwnd,1280,800,{gpu::Driver::warpForTests,argv[1],gpu::RenderTarget::offscreenForTests});
    const auto resources=std::filesystem::absolute(argv[2]);
    clipboard(renderer,resources/"clipboard");eventLog(renderer);mediaAssembly(renderer,window.hwnd,std::filesystem::absolute(argv[4]).lexically_normal());archive(renderer,window.hwnd,std::filesystem::absolute(argv[3]),resources/"archive");
    renderer.reset();std::cout<<"Layer switch probe: "<<checks<<" checks passed\n";return 0;}
    catch(const std::exception&e){std::cerr<<"Layer switch probe failed after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}}
#else
int main(){return 0;}
#endif
