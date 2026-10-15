#ifdef _WIN32
// UI Automation peers of the System provider modules (Clipboard, Event Log,
// Activity Monitor and Storage), driven through their provider interfaces as
// a UIA client would (tests/screen_reader_peer_probe.hpp; nothing is
// announced) over the owners (tools/*_preview.hpp) presented on a hidden
// window with WARP, as the HUD host presents them.
//
// Mac HUDClipboardInteraction / HUDEventLogInteraction / HUDTelemetryInteraction
// .layoutAccessibility: one button per accessible action with its source label
// (Clipboard and Event Log add the status and key help), a static status over
// the canvas for Activity Monitor and Storage, frames projected through the
// module plane, presses that act like the click, nothing listed while the
// module does not accept input. Storage's volume chooser and its menu rows
// (Windows addition) are listed the same way.
//
// Synthetic rows, events, apps and volumes only: no clipboard, event history,
// process, icon or volume of this machine is read, and no settings app opens.
#include "tools/clipboard_automation.hpp"
#include "tools/event_log_automation.hpp"
#include "tools/activity_automation.hpp"
#include "tools/storage_automation.hpp"
#include "tools/storage_options.hpp"
#include "native/module_scene.hpp"
#include "native/clipboard_assets.hpp"
#include "native/layer_image_source.hpp"
#include "app/utility_executor.hpp"
#include "tests/screen_reader_peer_probe.hpp"
#include <objbase.h>
#include <algorithm>
#include <cstdio>
#include <functional>
#include <iostream>
#include <mutex>

namespace gpu=endfield::native;namespace core=endfield::core;namespace app=endfield::app;
namespace tools=endfield::tools;namespace mod=endfield::modules;namespace probe=endfield::tests::peers;using Json=ehud::data::Json;
namespace {
unsigned checks{};void check(bool v,const char*why){++checks;if(!v)throw std::runtime_error(why);}
struct Com{Com(){check(SUCCEEDED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)),"Create owned COM apartment");}~Com(){CoUninitialize();}};
struct Window{HWND hwnd{};Window(){WNDCLASSW c{};c.lpfnWndProc=DefWindowProcW;c.hInstance=GetModuleHandleW(nullptr);c.lpszClassName=L"EndfieldProviderAutomationFixture";check(RegisterClassW(&c)!=0,"Register owned fixture");
    hwnd=CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP|WS_EX_TOOLWINDOW,c.lpszClassName,L"Owned provider automation fixture",WS_POPUP,0,0,1280,800,nullptr,nullptr,c.hInstance,nullptr);check(hwnd&&!IsWindowVisible(hwnd),"Never show or focus fixture HWND");}
    ~Window(){if(hwnd)DestroyWindow(hwnd);UnregisterClassW(L"EndfieldProviderAutomationFixture",GetModuleHandleW(nullptr));}};

// One module's HUD plane, starting on another module (Power), published as
// the host publishes owners (setEntries/collected only when a scene changed).
template<class Owner>struct Stage {
    gpu::Renderer&renderer;Owner&owner;gpu::LayerComposition composition;core::ModulePresentation modules{core::Module::power};
    core::source::DesktopChromeSettings settings;core::Matrix4 center;core::Matrix4 camera=gpu::layerViewportProjection(1280,800);double time{};
    struct Published{gpu::LayerScene*scene{};std::uint64_t content{},resources{};const gpu::DrawObject*after{};std::size_t count{};};
    std::vector<Published>published;std::vector<gpu::LayerCompositionEntry>ordered;std::function<void()>drain;
    Stage(gpu::Renderer&r,Owner&o):renderer(r),owner(o),settings{{0,0,1280,800},1,{},core::Module::power,true}{center=core::source::DesktopChromeLayout::make(settings,{},{}).designToScreen;}
    ~Stage(){try{composition.detach(renderer);owner.release(renderer);}catch(...){renderer.reset();}}
    void publish(){
        owner.upload(renderer);ordered.clear();for(const auto&e:owner.entries())ordered.push_back(e);
        bool changed=ordered.size()!=published.size();if(!changed)for(std::size_t n=0;n<ordered.size();++n){const auto&e=ordered[n];const auto&p=published[n];if(e.scene!=p.scene||e.scene->contentRevision()!=p.content||e.scene->resourceRevision()!=p.resources||e.after.data()!=p.after||e.after.size()!=p.count){changed=true;break;}}
        if(!changed)changed=!composition.supplementalBindingsMatch(ordered);
        if(changed){composition.setEntries(renderer,ordered);published.clear();for(const auto&e:ordered)published.push_back({e.scene,e.scene->contentRevision(),e.scene->resourceRevision(),e.after.data(),e.after.size()});owner.collected(renderer);}
        composition.present(renderer);renderer.setCamera(camera);}
    void frame(double delta=1./60){time+=delta;auto sample=modules.sample(time).presentation;settings.module=sample.requested;owner.update(center,settings,sample,1,time);publish();}
    void settle(double seconds=.6){for(double t=0;t<seconds;t+=1./60)frame();if(drain){drain();for(double t=0;t<seconds;t+=1./60)frame();}}
    void select(core::Module m){modules.select(m,time,false);settle();}
};
// Every peer of `expected` (id, label, help, role, canvas rect) in this order.
struct Expected {std::string id,label,help;bool text{};core::Rect rect;};
template<class Owner>void listed(const gpu::NativeScreenReaderPeers&peers,HWND window,const Owner&owner,const std::vector<Expected>&expected,const char*why){
    check(peers.size()==expected.size(),why);const auto ids=probe::ids(peers);
    for(std::size_t n=0;n<expected.size();++n){const auto&e=expected[n];const auto f=probe::find(peers,e.id);
        check(ids[n]==probe::wide(e.id)&&f&&probe::name(f)==probe::wide(e.label)&&probe::help(f)==probe::wide(e.help)&&
              probe::integer(f,UIA_ControlTypePropertyId)==(e.text?UIA_TextControlTypeId:UIA_ButtonControlTypeId)&&probe::boolean(f,UIA_IsEnabledPropertyId)&&
              probe::sameRect(probe::bounds(f),gpu::screenReaderClientToScreen(window,1,owner.clientRect(e.rect))),why);}
}

void clipboard(gpu::Renderer&renderer,HWND window,const std::filesystem::path&root){
    gpu::NativeClipboardAssets assets(root);gpu::LayerRasterizer raster;gpu::LayerImageSource thumbnails;gpu::ClipboardSnapshot snapshot;snapshot.capacity=20;
    for(std::uint64_t n=1;n<=8;++n)snapshot.rows.push_back({n,n==1,n%2?gpu::ClipboardKind::text:gpu::ClipboardKind::url,n%2?"Synthetic clipboard text "+std::to_string(n):"https://fixture.invalid/"+std::to_string(n),{}});
    std::vector<std::uint64_t>copies,pins;
    gpu::ClipboardActions actions{[&]{return snapshot;},[&](std::uint64_t id){copies.push_back(id);return true;},[&](std::uint64_t id){pins.push_back(id);return false;},[](auto){return false;},[]{return false;},[](auto){}};
    gpu::LayerRasterOptions options;options.pixelsPerPoint=2;options.paddingPoints=1;options.assetRoot=assets.root();options.memoryImages=&thumbnails;
    tools::ClipboardPreview owner(raster,options,std::move(actions),{},{},assets.images());owner.resize({1280,800,96,1,1280,800});owner.setLanguage(core::Language::english);
    Stage stage(renderer,owner);unsigned changes{};
    gpu::NativeScreenReaderPeers peers(tools::clipboardAutomation(owner,{window,[&]{return stage.time;},[&]{++changes;}}));
    check(!peers.refresh(nullptr)&&peers.size()==0&&!owner.clientRect(gpu::ClipboardState::bounds()),"A Clipboard that is not shown lists nothing");
    stage.select(core::Module::clipboard);
    const auto expected=[&](std::string_view keys){std::vector<Expected>out;const auto help=owner.state().status()+std::string(keys);for(const auto&a:owner.state().actions())out.push_back({a.id,a.label,help,false,a.rect});return out;};
    check(peers.refresh(nullptr)&&owner.state().actions().size()>8,"The shown Clipboard lists its toolbar and visible row actions");
    listed(peers,window,owner,expected(". Arrow keys select; Return copies; Delete removes the cached item."),"Each Clipboard peer carries its source label, the status and key help, and its projected rect");
    const auto copy=probe::find(peers,"clipboard:2:copy");check(copy&&probe::name(copy)==L"Copy: https://fixture.invalid/2","A row's copy peer reads the source label");
    check(probe::invoke(copy)==S_OK&&copies==std::vector<std::uint64_t>{2}&&changes==1,"A row's copy peer copies like its click");
    check(probe::invoke(probe::find(peers,"clipboard:3:pin"))==S_OK&&pins==std::vector<std::uint64_t>{3}&&changes==2,"A row's pin peer pins like its click");
    check(!owner.perform("clipboard:99:copy",stage.time)&&!owner.perform("clipboard:bogus",stage.time)&&copies.size()==1,"Only listed Clipboard actions perform");
    stage.settle();(void)peers.refresh(nullptr);
    owner.setLanguage(core::Language::simplifiedChinese);stage.frame();(void)peers.refresh(nullptr);
    listed(peers,window,owner,expected("。方向键选择，回车复制，Delete 删除缓存项目。"),"The Clipboard peers follow the HUD language");
    // A tilted plane moves every frame with it.
    stage.center=stage.center*core::Matrix4::rotation(.05,-.08,0);stage.frame();(void)peers.refresh(nullptr);
    listed(peers,window,owner,expected("。方向键选择，回车复制，Delete 删除缓存项目。"),"Clipboard peer frames follow the tilted module plane");
    const auto kept=probe::find(peers,"clipboard:2:copy");stage.select(core::Module::power);
    check(peers.refresh(nullptr)&&peers.size()==0&&probe::available(kept)==static_cast<HRESULT>(UIA_E_ELEMENTNOTAVAILABLE)&&probe::invoke(kept)!=S_OK&&copies.size()==1,"Switching away withdraws every Clipboard peer");
    std::cout<<"Clipboard: "<<owner.state().actions().size()<<" peers while shown\n";
}

void eventLog(gpu::Renderer&renderer,HWND window){
    gpu::LayerRasterizer raster;mod::EventLogSnapshot events;unsigned clears{};
    for(unsigned k=1;k<=24;++k){char id[64];std::snprintf(id,sizeof(id),"00000000-0000-0000-0000-%012u",k);events.events.push_back({id,k%2?mod::EventKind::clipboardCopied:mod::EventKind::shelfAdded,1700000000.-k*60,k%2?mod::EventMetadata{{"kind","text"}}:mod::EventMetadata{{"filename","Synthetic.pdf"}}});}
    mod::EventLogCallbacks callbacks{[&]{return events;},[&]{++clears;},[](double){return std::string("11-14 22:13:19");}};
    tools::EventLogPreview owner(raster,gpu::LayerRasterOptions{},std::move(callbacks));owner.resize({1280,800,96,1,1280,800});owner.setLanguage(core::Language::english);
    Stage stage(renderer,owner);unsigned changes{};
    gpu::NativeScreenReaderPeers peers(tools::eventLogAutomation(owner,{window,[&]{return stage.time;},[&]{++changes;}}));
    check(!peers.refresh(nullptr)&&peers.size()==0,"An Event Log that is not shown lists nothing");
    stage.select(core::Module::eventLog);
    const auto expected=[&]{std::vector<Expected>out;const auto help=owner.state().status()+". Arrow keys select events; Page Up and Page Down scroll.";for(const auto&a:owner.state().actions())out.push_back({a.id,a.label,help,false,a.rect});return out;};
    check(peers.refresh(nullptr),"The shown Event Log lists its controls and visible rows");
    listed(peers,window,owner,expected(),"Each Event Log peer carries its source label, the status and key help, and its projected rect");
    const std::string row="eventLog:row:00000000-0000-0000-0000-000000000002";const auto rowPeer=probe::find(peers,row);
    check(rowPeer&&probe::name(rowPeer).starts_with(L"11-14 22:13:19. "),"A row peer reads its time, category, title and detail");
    check(probe::invoke(rowPeer)==S_OK&&owner.state().selected()==std::optional<std::string>("00000000-0000-0000-0000-000000000002")&&changes==1,"A row peer selects like its click");
    const auto files=std::find_if(owner.state().actions().begin(),owner.state().actions().end(),[](const auto&a){return a.id!="eventLog:category:all"&&a.id.starts_with("eventLog:category:");});
    check(files!=owner.state().actions().end(),"A category control is listed");const std::string category=files->id;
    // An accepted press re-lists the peers at once (NativeScreenReaderPeers).
    check(probe::invoke(probe::find(peers,category))==S_OK&&changes==2&&owner.state().category()&&probe::ids(peers).size()==owner.state().actions().size(),"A category peer filters like its click and the peers re-list at once");
    check(!owner.perform("eventLog:row:00000000-0000-0000-0000-000000000099",stage.time)&&!owner.perform("eventLog:bogus",stage.time),"Only listed Event Log actions perform");
    stage.settle();(void)peers.refresh(nullptr);listed(peers,window,owner,expected(),"The filtered Event Log peers match the page");
    const auto kept=peers.size()?probe::find(peers,"eventLog:category:all"):probe::Fragment{};stage.select(core::Module::power);
    check(peers.refresh(nullptr)&&peers.size()==0&&probe::available(kept)==static_cast<HRESULT>(UIA_E_ELEMENTNOTAVAILABLE)&&clears==0,"Switching away withdraws every Event Log peer");
}

void activity(gpu::Renderer&renderer,HWND window){
    gpu::LayerRasterizer raster;tools::ActivityPreviewOptions options;options.compareNames=[](auto a,auto b){return a.compare(b);};
    options.initial.timestamp=1;options.initial.uptime=1;options.initial.cpuPercent=10;options.initial.memory=mod::ActivityMemory{100,1000,10};options.initial.gpuPercent=12.5;
    for(unsigned k=0;k<4;++k){mod::ActivityApp item;item.identity={"app"+std::to_string(k),"Synthetic "+std::to_string(k),"",{100+k}};item.cpuPercent=double(3+k*7%11);item.memoryBytes=100000000ULL+k*7000000ULL;item.gpuPercent=double(k);options.apps.items.push_back(std::move(item));}
    tools::ActivityPreview owner(raster,std::move(options));owner.resize({1280,800,96,1,1280,800});owner.setLanguage(core::Language::english);
    Stage stage(renderer,owner);unsigned changes{};
    gpu::NativeScreenReaderPeers peers(tools::activityAutomation(owner,{window,[&]{return stage.time;},[&]{++changes;}}));
    check(!peers.refresh(nullptr)&&peers.size()==0,"An Activity Monitor that is not shown lists nothing");
    stage.select(core::Module::activityMonitor);
    const auto expected=[&]{std::vector<Expected>out{{"activity:status",mod::activityAccessibilityStatus(owner.state(),owner.language()),{},true,{0,0,400,334}}};for(const auto&a:owner.actions())out.push_back({a.id,a.label,{},false,a.rect});return out;};
    check(peers.refresh(nullptr)&&peers.size()==3,"The Overview lists its status and two tabs");listed(peers,window,owner,expected(),"Each Activity peer carries its source label and projected rect");
    check(probe::name(probe::find(peers,"activity:status"))==L"CPU 10.0%; RAM 100 B / 1.0 KB; Upload —; Download —; Disk read —; Disk write —; GPU 12.5%","The Overview status reads the system values (GPU last)");
    // An accepted press re-lists the peers at once (NativeScreenReaderPeers).
    check(probe::invoke(probe::find(peers,"activity:apps"))==S_OK&&owner.state().showingApps()&&changes==1&&peers.size()==9,"The Apps tab peer opens Apps like its click; the page lists its status, tabs and six column headers at once");
    stage.settle();(void)peers.refresh(nullptr);listed(peers,window,owner,expected(),"The Apps peers carry the source labels and projected rects");
    const auto gpuHeader=probe::find(peers,"activity:sort:gpu");check(gpuHeader&&probe::name(gpuHeader)==L"Sort by GPU","The GPU column (Windows addition) reads like the other headers");
    check(probe::invoke(gpuHeader)==S_OK&&owner.state().sortKey()==mod::ActivitySort::gpu&&owner.state().descending()&&changes==2,"A header peer sorts like its click");stage.settle();(void)peers.refresh(nullptr);
    check(probe::name(probe::find(peers,"activity:sort:gpu"))==L"Sort by GPU, descending"&&probe::name(probe::find(peers,"activity:status")).starts_with(L"Synthetic 3, CPU 13.0%, GPU 3.0%, RAM 121 MB, ↑ —, ↓ —, Read —, Write —"),"The sorted header and the app status read the current order");
    check(!owner.perform("activity:bogus",stage.time),"Only listed Activity actions perform");
    owner.setLanguage(core::Language::simplifiedChinese);stage.frame();(void)peers.refresh(nullptr);listed(peers,window,owner,expected(),"The Activity peers follow the HUD language");
    const auto kept=probe::find(peers,"activity:overview");stage.select(core::Module::power);
    check(peers.refresh(nullptr)&&peers.size()==0&&probe::available(kept)==static_cast<HRESULT>(UIA_E_ELEMENTNOTAVAILABLE),"Switching away withdraws every Activity peer");
}

struct Volumes {std::mutex mutex;std::vector<mod::StorageVolume>list;
    std::vector<mod::StorageVolume>get(){std::lock_guard lock(mutex);return list;}
    std::optional<mod::StorageCapacity>read(const std::string&id){std::lock_guard lock(mutex);for(const auto&v:list)if(v.id==id&&!id.empty())return mod::StorageCapacity{v.title,1000000000000,400000000000,0};return mod::StorageCapacity{"System",1000000000000,250000000000,0};}};
void storage(gpu::Renderer&renderer,HWND window,const std::filesystem::path&resources){
    gpu::LayerRasterizer raster;app::UtilityExecutor executor([]{});auto volumes=std::make_shared<Volumes>();unsigned settings{};
    volumes->list={{"vol-system","C:\\","System",false,true},{"vol-data","D:\\","Data",false,false}};
    auto options=tools::makeStoragePreviewOptions(resources,executor,[&]{++settings;});
    options.readCapacity=[]()->std::optional<mod::StorageCapacity>{throw std::logic_error("The chooser reads by id");};
    options.listVolumes=[volumes]{return volumes->get();};options.readVolumeCapacity=[volumes](const std::string&id){return volumes->read(id);};
    options.scanDetails={};options.completionClock={};std::string remembered;options.loadVolume=[&]{return remembered;};options.saveVolume=[&](const std::string&id){remembered=id;};
    tools::StoragePreview owner(raster,std::move(options));owner.resize({1280,800,96,1,1280,800});
    Stage stage(renderer,owner);stage.drain=[&]{executor.waitIdle();executor.drain();owner.utilityCompleted(stage.time);};unsigned changes{};
    gpu::NativeScreenReaderPeers peers(tools::storageAutomation(owner,{window,[&]{return stage.time;},[&]{++changes;}}));
    check(!peers.refresh(nullptr)&&peers.size()==0,"A Storage page that is not shown lists nothing");
    owner.setVisible(true,stage.time);stage.select(core::Module::storage);
    const auto expected=[&]{std::vector<Expected>out{{"storage:status",owner.presentation().labels().status,{},true,{0,0,400,334}}};for(const auto&a:owner.accessibleActions())out.push_back({a.id,a.label,{},false,a.rect});return out;};
    check(peers.refresh(nullptr)&&peers.size()==4,"Storage lists its status, Storage Settings, Refresh storage and the volume chooser");listed(peers,window,owner,expected(),"Each Storage peer carries its source label and projected rect");
    check(probe::name(probe::find(peers,"storage:status"))==L"System; Total 1.0 TB; Used 750 GB; Available 250 GB"&&probe::name(probe::find(peers,"storage:volumes"))==L"Choose disk","The status reads the volume totals and the chooser its caption");
    check(probe::invoke(probe::find(peers,"storage:settings"))==S_OK&&settings==1&&changes==1,"Storage Settings presses like its click");
    // An accepted press re-lists the peers at once (NativeScreenReaderPeers):
    // the open menu is modal, so its rows replace the page's buttons.
    check(probe::invoke(probe::find(peers,"storage:volumes"))==S_OK&&owner.volumeMenuOpen()&&changes==2&&peers.size()==4&&!probe::find(peers,"storage:settings")&&probe::find(peers,"storage:volume:0"),"The chooser peer opens the volume menu, which lists its two volumes beside the status and the chooser");
    stage.settle(.4);(void)peers.refresh(nullptr);listed(peers,window,owner,expected(),"The menu rows carry their volume labels and projected rects");
    check(probe::name(probe::find(peers,"storage:volume:1"))==L"Data (D:)","A volume row reads its title and drive");
    check(probe::invoke(probe::find(peers,"storage:volume:1"))==S_OK&&!owner.volumeMenuOpen()&&owner.chosenVolume()=="vol-data"&&remembered=="vol-data"&&changes==3,"A volume row chooses it like its click");stage.settle();
    (void)peers.refresh(nullptr);check(probe::name(probe::find(peers,"storage:status"))==L"Data; Total 1.0 TB; Used 600 GB; Available 400 GB","The status reads the chosen volume");
    const auto kept=probe::find(peers,"storage:settings");stage.select(core::Module::power);
    check(peers.refresh(nullptr)&&peers.size()==0&&probe::available(kept)==static_cast<HRESULT>(UIA_E_ELEMENTNOTAVAILABLE)&&settings==1,"Switching away withdraws every Storage peer");
}
}
int wmain(int argc,wchar_t**argv){try{check(argc==4,"Usage: activity_clipboard_automation_tests shader resources clipboard|eventLog|activity|storage");Com com;Window window;
    gpu::Renderer renderer;renderer.initialize(window.hwnd,1280,800,{gpu::Driver::warpForTests,argv[1],gpu::RenderTarget::offscreenForTests});
    const auto resources=std::filesystem::absolute(argv[2]);const std::wstring mode=argv[3];
    if(mode==L"clipboard")clipboard(renderer,window.hwnd,resources/"clipboard");
    else if(mode==L"eventLog")eventLog(renderer,window.hwnd);
    else if(mode==L"activity")activity(renderer,window.hwnd);
    else if(mode==L"storage")storage(renderer,window.hwnd,resources);
    else check(false,"Unknown mode");
    renderer.reset();std::cout<<"Provider automation: "<<checks<<" checks passed\n";return 0;}
    catch(const std::exception&e){std::cerr<<"Provider automation failed after "<<checks<<": "<<e.what()<<'\n';return 1;}}
#else
int main(){return 0;}
#endif
