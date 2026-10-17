#ifdef _WIN32
// Storage volume choice (a Windows addition) through the Storage owner
// (tools/storage_preview.hpp) on a hidden window and WARP: the chooser button,
// its NotesRetainedMenu-style menu, choosing a volume (its capacity read on
// the shared executor by id, the choice remembered through the owner's
// callbacks), an absent remembered volume falling back to the startup volume,
// a single volume hiding the chooser, and the production readers never mixed
// with injected ones. Synthetic volumes and capacities only: no volume,
// setting or file of this machine is read or written.
#include "tools/storage_options.hpp"
#include "native/module_scene.hpp"
#include <objbase.h>
#include <atomic>
#include <iostream>
#include <mutex>
#include <stdexcept>
namespace c=endfield::core;namespace m=endfield::modules;namespace n=endfield::native;namespace t=endfield::tools;namespace a=endfield::app;using J=ehud::data::Json;
namespace {
unsigned checks{};void check(bool v,const char*s){++checks;if(!v)throw std::runtime_error(s);}
struct Window {HWND hwnd{};Window(){WNDCLASSW w{};w.lpfnWndProc=DefWindowProcW;w.hInstance=GetModuleHandleW(nullptr);w.lpszClassName=L"EndfieldStorageVolumeFixture";check(RegisterClassW(&w)!=0,"Register owned fixture");hwnd=CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP|WS_EX_TOOLWINDOW,w.lpszClassName,L"Hidden Storage volume fixture",WS_POPUP,0,0,1280,800,nullptr,nullptr,w.hInstance,nullptr);check(hwnd&&!IsWindowVisible(hwnd),"Fixture stays hidden");}~Window(){if(hwnd)DestroyWindow(hwnd);UnregisterClassW(L"EndfieldStorageVolumeFixture",GetModuleHandleW(nullptr));}};
J blank(){return J::Object{{"bounds",J::Array{0,0,400,334}},{"position",J::Array{0,0}},{"anchorPoint",J::Array{0,0}},{"children",J::Array{}}};}
struct Volumes {std::mutex mutex;std::vector<m::StorageVolume>list;std::vector<std::string>reads;unsigned listings{};
    std::vector<m::StorageVolume>get(){std::lock_guard lock(mutex);++listings;return list;}
    void set(std::vector<m::StorageVolume>v){std::lock_guard lock(mutex);list=std::move(v);}
    // Synthetic capacities by id; an absent id reads the startup volume like the production reader.
    std::optional<m::StorageCapacity>read(const std::string&id){std::lock_guard lock(mutex);reads.push_back(id);for(const auto&v:list)if(v.id==id&&!id.empty())return m::StorageCapacity{v.title,1000000000000,v.removable?900000000000:400000000000,0};return m::StorageCapacity{"System",1000000000000,250000000000,0};}
    std::string last(){std::lock_guard lock(mutex);return reads.empty()?std::string{}:reads.back();}
};
void run(const std::filesystem::path&shader,const std::filesystem::path&resources){
    Window window;n::Renderer renderer;renderer.initialize(window.hwnd,1280,800,{n::Driver::warpForTests,shader,n::RenderTarget::offscreenForTests});n::LayerRasterizer raster;a::UtilityExecutor executor([]{});
    const m::StorageVolume system{"vol-system","C:\\","System",false,true},data{"vol-data","D:\\","Data",false,false},stick{"vol-stick","E:\\","Stick",true,false};
    auto volumes=std::make_shared<Volumes>();volumes->set({stick,system,data});std::vector<std::string>saved;std::string remembered="vol-data";
    // Production readers kept beside an injected capacity reader: no chooser, so no real volume is read.
    {auto options=t::makeStoragePreviewOptions(resources,executor,[]{});check(options.listVolumes&&options.readVolumeCapacity,"Production options carry the volume readers");options.readCapacity=[]()->std::optional<m::StorageCapacity>{return m::StorageCapacity{"Synthetic",1000,500,0};};
        t::StoragePreview mixed(raster,std::move(options));check(!mixed.volumeChooserAvailable(),"Injected capacity with production volume readers offers no chooser");}
    auto options=t::makeStoragePreviewOptions(resources,executor,[]{});options.rasterDensity=1;
    options.readCapacity=[]()->std::optional<m::StorageCapacity>{throw std::logic_error("The chooser reads by id");};
    options.listVolumes=[volumes]{return volumes->get();};options.readVolumeCapacity=[volumes](const std::string&id){return volumes->read(id);};
    options.scanDetails={};options.completionClock={};
    options.loadVolume=[&]{return remembered;};options.saveVolume=[&](const std::string&id){saved.push_back(id);remembered=id;};
    t::StoragePreview preview(raster,std::move(options));preview.resize({1280,800,96,1,1280,800});check(preview.chosenVolume()=="vol-data","The remembered volume is loaded at construction");
    c::ModulePresentation modules(c::Module::storage);c::source::DesktopChromeSettings settings{{0,0,1280,800},1,{},c::Module::storage,true};const auto center=c::source::DesktopChromeLayout::make(settings,{},{}).designToScreen;const auto camera=n::layerViewportProjection(1280,800);
    n::LayerScene geometry(raster);geometry.load(blank(),{});n::NativeModuleSurface plane(geometry,c::Module::storage);n::LayerComposition composition;double time{};bool released{};
    struct Cleanup{n::LayerComposition&c;t::StoragePreview&p;n::Renderer&r;bool&done;~Cleanup(){if(done)return;try{c.detach(r);p.release(r);}catch(...){r.reset();}}}cleanup{composition,preview,renderer,released};
    const auto frame=[&](double dt=1./60){time+=dt;auto sample=modules.sample(time).presentation;settings.module=sample.requested;preview.update(center,settings,sample,1,time);preview.upload(renderer);composition.setEntries(renderer,preview.entries());preview.collected(renderer);composition.present(renderer);renderer.setCamera(camera);};
    const auto drain=[&]{executor.waitIdle();executor.drain();preview.utilityCompleted(time);frame();};
    const auto point=[&](c::Point p){auto sample=modules.sample(time).presentation;const auto*surface=sample.current.module==c::Module::storage?&sample.current:&*sample.incoming;plane.update(center,settings,*surface);return *c::Projection::viewport(camera*plane.pose().contentWorld,1280,800).project(p);};
    const auto click=[&](c::Point p){const auto q=point(p);const bool taken=preview.pointer({a::PointerKind::down,a::PointerButton::left,q.x,q.y,0},time);frame(.05);(void)preview.pointer({a::PointerKind::up,a::PointerButton::left,q.x,q.y,0},time);frame(.3);return taken;};
    for(unsigned k=0;k<40;++k)frame();drain();drain();
    check(volumes->last()=="vol-data"&&preview.presentation().labels().caption=="Data","The remembered volume's capacity is read by id and named in the caption");
    check(preview.volumes().size()==3&&preview.volumes()[0].id=="vol-system"&&preview.volumes()[2].id=="vol-stick"&&preview.volumeChooserAvailable(),"Listed on the shared executor, startup first, removable last");
    check(preview.entries().size()==2,"The Mac artwork plus the chooser button");
    const auto rect=m::storageChooserRect;check(click({rect.x+rect.width/2,rect.y+rect.height/2})&&preview.volumeMenuOpen(),"The chooser opens the volume menu");frame(.2);
    check(preview.entries().size()==3&&!preview.requiresFrames(time),"The menu is drawn above, and its entrance settled");
    renderer.draw(false);const auto open=renderer.readback();
    // A press outside the menu closes it without choosing.
    check(click({30,300})&&!preview.volumeMenuOpen()&&saved.empty(),"A press outside closes the menu");frame(.2);check(preview.entries().size()==2,"The dismissed menu is released after its exit");
    renderer.draw(false);check(renderer.readback().pixels!=open.pixels,"The menu was visible");
    check(preview.perform(m::storageVolumesAction,time)&&preview.volumeMenuOpen(),"The chooser action opens it again");frame(.2);
    // Choose the removable volume (row 2) by its projected row.
    const auto menu=m::prepareStorageVolumeMenu(preview.volumes(),"vol-data",preview.presentation().appearance());const auto row=menu.actions[2].rect;
    check(click({row.x+row.width/2,row.y+row.height/2}),"A row press is taken");check(!preview.volumeMenuOpen()&&preview.chosenVolume()=="vol-stick"&&saved==std::vector<std::string>{"vol-stick"},"Choosing a row closes the menu and remembers the volume");
    check(preview.state().snapshot().isLoading&&!preview.state().snapshot().capacity&&preview.presentation().labels().caption=="Stick","The previous volume's numbers are cleared and the chosen title shown while it is read");
    drain();check(volumes->last()=="vol-stick"&&preview.state().snapshot().capacity&&preview.state().snapshot().capacity->volumeName=="Stick"&&preview.presentation().labels().values[2]=="900 GB","The chosen volume's capacity is read by id");
    check(!preview.perform("storage:volume:1",time),"Rows act only while the menu is open");
    // The remembered volume is unplugged: the startup volume is shown, the choice stays remembered.
    volumes->set({system,data});frame();preview.setVisible(false,time);frame();preview.setVisible(true,time);for(unsigned k=0;k<10;++k)frame();drain();drain();
    check(preview.volumes().size()==2&&preview.chosenVolume()=="vol-stick"&&saved.size()==1,"An absent volume stays the remembered choice");
    check(preview.presentation().labels().caption=="System"&&volumes->last()=="vol-stick","An absent volume reads the startup volume at once");
    volumes->set({system,data,stick});preview.setVisible(false,time);frame();preview.setVisible(true,time);for(unsigned k=0;k<10;++k)frame();drain();drain();
    check(preview.presentation().labels().caption=="Stick","The remembered volume is shown again when it comes back");
    // One volume only: no chooser.
    volumes->set({system});preview.setVisible(false,time);frame();preview.setVisible(true,time);for(unsigned k=0;k<10;++k)frame();drain();
    check(!preview.volumeChooserAvailable()&&preview.entries().size()==1&&!preview.perform(m::storageVolumesAction,time),"A single volume offers no chooser");
    // Hiding the HUD closes an open menu at once.
    volumes->set({system,data});preview.setVisible(false,time);frame();preview.setVisible(true,time);for(unsigned k=0;k<10;++k)frame();drain();
    check(preview.perform(m::storageVolumesAction,time)&&preview.volumeMenuOpen(),"Menu open");preview.setVisible(false,time);frame();check(!preview.volumeMenuOpen(),"Hiding closes the menu");
    std::cout<<"volume listings "<<volumes->listings<<", capacity reads "<<volumes->reads.size()<<'\n';
    composition.detach(renderer);preview.release(renderer);released=true;check(renderer.stats().textures==0&&renderer.stats().meshes==0,"Every Storage resource retires");renderer.reset();
}
}
int wmain(int argc,wchar_t**argv){const auto hr=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);try{check(SUCCEEDED(hr)&&argc==3,"Usage: storage_volume_tests shader resources");run(argv[1],std::filesystem::absolute(argv[2]));CoUninitialize();std::cout<<"PASS "<<checks<<" Storage volume checks\n";return 0;}
    catch(const std::exception&e){if(SUCCEEDED(hr))CoUninitialize();std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}}
#else
int main(){return 0;}
#endif
