#ifdef _WIN32
#include "tools/shelf_preview.hpp"
#include "native/module_scene.hpp"
#include "native/file_shelf_files.hpp"
#include "core/data/file_io.hpp"
#include <ole2.h>
#include <fstream>
#include <iostream>

namespace gpu=endfield::native;namespace core=endfield::core;namespace app=endfield::app;
namespace data=ehud::data;namespace tools=endfield::tools;
namespace {
unsigned checks{};void check(bool v,const char*why){++checks;if(!v)throw std::runtime_error(why);}
std::string utf8(const std::filesystem::path&p){const auto s=p.u8string();return {reinterpret_cast<const char*>(s.data()),s.size()};}
struct Ole{Ole(){check(SUCCEEDED(OleInitialize(nullptr)),"Create owned OLE apartment");}~Ole(){OleUninitialize();}};
struct Window{HWND hwnd{};Window(){WNDCLASSW c{};c.lpfnWndProc=DefWindowProcW;c.hInstance=GetModuleHandleW(nullptr);c.lpszClassName=L"EndfieldShelfOwnerFixture";check(RegisterClassW(&c)!=0,"Register owned fixture");hwnd=CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP|WS_EX_TOOLWINDOW,c.lpszClassName,L"Owned Shelf fixture",WS_POPUP,0,0,1280,800,nullptr,nullptr,c.hInstance,nullptr);check(hwnd&&!IsWindowVisible(hwnd),"Never show or focus fixture HWND");}~Window(){if(hwnd)DestroyWindow(hwnd);UnregisterClassW(L"EndfieldShelfOwnerFixture",GetModuleHandleW(nullptr));}};
struct Temp{std::filesystem::path root=std::filesystem::temp_directory_path()/ ("ehud-shelf-owner-"+data::makeUUID());Temp(){check(std::filesystem::create_directory(root),"Create unique owned fixture parent");}~Temp(){std::error_code error;std::filesystem::remove_all(root,error);}};
data::Json blank(){return data::Json::Object{{"bounds",data::Json::Array{0,0,400,334}},{"position",data::Json::Array{0,0}},{"anchorPoint",data::Json::Array{0,0}},{"children",data::Json::Array{}}};}
void run(HWND hwnd,gpu::Renderer&renderer,const gpu::NativeShelfAssets&assets,std::shared_ptr<const core::SubsectionMaskSampler>masks){
    Temp temp;gpu::LayerRasterizer raster;gpu::LayerComposition composition;core::ModulePresentation modules{core::Module::fileShelf};
    tools::ShelfPreview shelf(hwnd,raster,assets,{temp.root/"metadata",false,false,std::move(masks)});shelf.resize({1280,800,96,1,1280,800});
    gpu::LayerScene reference(raster);reference.load(blank(),{});gpu::NativeModuleSurface placement(reference,core::Module::fileShelf);
    core::source::DesktopChromeSettings settings{{0,0,1280,800},1,{},core::Module::fileShelf,true};
    const auto design=core::source::DesktopChromeLayout::make(settings,{},{}).designToScreen;auto center=design;const auto camera=gpu::layerViewportProjection(1280,800);double time{};bool released{};
    struct Cleanup{gpu::Renderer&r;gpu::LayerComposition&c;tools::ShelfPreview&s;bool&done;~Cleanup(){if(done)return;try{c.detach(r);s.release(r);}catch(...){r.reset();}}}cleanup{renderer,composition,shelf,released};
    auto frame=[&](double delta=.01){time+=delta;auto sample=modules.sample(time).presentation;settings.module=sample.requested;shelf.update(center,settings,sample,1,time);shelf.upload(renderer);composition.setEntries(renderer,shelf.entries());shelf.collected(renderer);composition.present(renderer);renderer.setCamera(camera);};
    auto local=[&](core::Point p){auto sample=modules.sample(time).presentation;const auto*surface=sample.current.module==core::Module::fileShelf?&sample.current:&*sample.incoming;placement.update(center,settings,*surface);return *core::Projection::viewport(camera*placement.pose().contentWorld,1280,800).project(p);};
    auto click=[&](core::Point p){const auto q=local(p);check(shelf.pointer({app::PointerKind::down,app::PointerButton::left,q.x,q.y,0},time),"Projected Shelf press is handled");frame();check(shelf.pointer({app::PointerKind::up,app::PointerButton::left,q.x,q.y,0},time),"Projected Shelf release is handled");frame();};
    frame();check(shelf.state().items().empty()&&!shelf.entries().empty(),"Actual empty source Shelf artwork is composed");
    const auto fileCount=13;std::vector<std::string>paths;
    for(int n=0;n<fileCount;++n){const auto file=temp.root/("sample-"+std::to_string(n)+".txt");std::ofstream out(file);out<<"Temporary synthetic fixture "<<n;out.close();paths.push_back(utf8(file));}
    check(shelf.importFiles(paths,time),"Reference import uses real Windows metadata in temporary directory");frame(.04);renderer.draw(false);frame(.4);
    check(shelf.state().items().size()==fileCount&&shelf.state().maximumOffset()>0,"Imported references populate scrollable source shelf");
    {gpu::NativeFileShelfFiles files;data::FileShelfStore reopened(temp.root/"metadata",files.platform());check(reopened.items().size()==fileCount,"Connected owner persisted references through real store");}
    check(shelf.importFiles(paths,time),"Duplicate-only import succeeds");frame(.4);check(shelf.state().items().size()==fileCount,"Duplicate references preserve identity deduplication");
    check(shelf.key({app::KeyKind::down,VK_HOME},time),"Home is routed to shelf scrolling");frame();check(shelf.state().scrollOffset()==0,"Home reveals the beginning");
    click({35,60});check(shelf.state().selectedIDs().size()==1,"Card click selects one reference");
    auto transfer=shelf.prepareDrag(*shelf.state().selectedID());check(transfer&&transfer->dataObject()!=nullptr,"Selected reference creates copy-only OLE payload without entering drag loop");transfer.reset();
    const auto selected=*shelf.state().selectedID();click({180,104});frame(.4);check(!shelf.state().item(selected),"Remove control deletes selected metadata reference");
    for(const auto&p:paths)check(std::filesystem::exists(std::filesystem::u8path(p)),"Removing a reference leaves original fixture file untouched");
    auto q=local({80,180});const auto before=shelf.state().scrollOffset();check(shelf.wheel({q.x,q.y,-1,false,0,3},time),"Wheel inside projected Shelf is consumed");frame();check(shelf.state().scrollOffset()>before,"Wheel scroll changes source collection position");
    check(shelf.key({app::KeyKind::down,VK_HOME},time),"Home returns before selection test");frame();click({35,60});
    const auto baselineRaster=raster.stats();const auto baselineGPU=renderer.stats();
    for(unsigned n=0;n<60;++n){center=core::Matrix4::translation(double(n%3),double(n%2))*design;frame(.02);}
    check(raster.stats().rasterizations==baselineRaster.rasterizations&&raster.stats().textLayoutsCreated==baselineRaster.textLayoutsCreated,"Settled Shelf tilt retains text/icon rasters");
    check(renderer.stats().textureUploads==baselineGPU.textureUploads&&renderer.stats().meshUploads==baselineGPU.meshUploads,"Settled Shelf tilt uploads no textures or meshes");
    click({135,312});check(shelf.state().confirmingClear(),"Clear asks for source confirmation");frame(.3);click({250,312});check(!shelf.state().confirmingClear()&&!shelf.state().items().empty(),"Cancel preserves references");
    click({135,312});frame(.3);click({335,312});frame(.4);check(shelf.state().items().empty(),"Confirm clears references");
    for(const auto&p:paths)check(std::filesystem::exists(std::filesystem::u8path(p)),"Clearing shelf leaves every original fixture file untouched");
    click({50,312});auto action=shelf.takeAction();check(action&&action->kind==tools::ShelfPreviewAction::Kind::choose,"Add button queues chooser outside state callback");
    modules.select(core::Module::notes,time);frame(.4);check(shelf.entries().empty()&&!shelf.requiresFrames(time),"Inactive Shelf paints nothing and asks for no animation frames");
    check(!shelf.covers({640,400}),"Inactive Shelf cannot consume another module's input");
    composition.detach(renderer);shelf.release(renderer);released=true;check(!IsWindowVisible(hwnd),"Integration fixture never displayed a window or dialog");
}
}
int wmain(int argc,wchar_t**argv){try{check(argc==4,"Usage: shelf_preview_tests shader assets mask");Ole ole;Window window;gpu::Renderer renderer;renderer.initialize(window.hwnd,1280,800,{gpu::Driver::warpForTests,argv[1],gpu::RenderTarget::offscreenForTests});gpu::NativeShelfAssets assets(std::filesystem::absolute(argv[2]));auto bytes=data::detail::readFile(argv[3],128*1024);check(bytes.has_value(),"Explicit original mask input exists");auto masks=std::make_shared<core::SubsectionMaskSampler>(std::span(reinterpret_cast<const std::uint8_t*>(bytes->data()),bytes->size()),core::SubsectionMaskSampler::assetSHA256);run(window.hwnd,renderer,assets,masks);std::cout<<"Native Shelf owner integration: "<<checks<<" checks passed\n";return 0;}catch(const std::exception&e){std::cerr<<"Native Shelf owner failed after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}}
#endif
