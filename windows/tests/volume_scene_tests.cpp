#include "native/volume_scene.hpp"
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <set>
#include <stdexcept>
#ifdef _WIN32
#include "native/layer_scene.hpp"
#include <objbase.h>
#endif
namespace {std::atomic<bool>counting{};std::atomic<std::size_t>allocations{};}
void*operator new(std::size_t n){if(counting)++allocations;if(auto*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void*operator new[](std::size_t n){return ::operator new(n);}void operator delete(void*p)noexcept{std::free(p);}void operator delete[](void*p)noexcept{std::free(p);}
#if defined(__cpp_sized_deallocation)
void operator delete(void*p,std::size_t)noexcept{std::free(p);}void operator delete[](void*p,std::size_t)noexcept{std::free(p);}
#endif
namespace {
namespace gpu=endfield::native;namespace core=endfield::core;using Json=ehud::data::Json;
unsigned checks{};void check(bool v,const char*s){++checks;if(!v)throw std::runtime_error(s);}template<class F>void rejects(F&&f,const char*s){bool no{};try{f();}catch(const std::exception&){no=true;}check(no,s);}
gpu::VolumeSnapshot fixture(){gpu::VolumeSnapshot s;s.outputID="o0";s.inputID="i0";s.volume=.55;s.balance=0;s.muted=false;s.canSetVolume=s.canSetMute=s.canSetBalance=s.canSetDefaultOutput=s.canSetDefaultInput=s.applicationActivitySupported=true;
    for(unsigned n=0;n<8;++n){s.outputs.push_back({"o"+std::to_string(n),"Output "+std::to_string(n),bool(n%2),bool(n==3)});s.inputs.push_back({"i"+std::to_string(n),"Input "+std::to_string(n)});}for(unsigned n=0;n<12;++n)s.applications.push_back({std::to_string(n),"App "+std::to_string(n),100+n,true,gpu::VolumeAppState::direct,{},{}});return s;}
const gpu::VolumeControl&action(const gpu::VolumeController&c,std::string_view id){for(const auto&a:c.actions())if(a.id==id)return a;throw std::runtime_error("Missing action");}
const gpu::VolumeSlider&slider(const gpu::VolumeController&c,std::string_view id){for(const auto&a:c.sliders())if(a.id==id)return a;throw std::runtime_error("Missing slider");}
const Json&leaf(const gpu::VolumeScenePlan&p,std::string_view id){for(const auto&n:p.layers["children"].array())if(n["id"].string()==id)return n;throw std::runtime_error("Missing artwork");}
void portable(){
    auto s=fixture();std::vector<std::string>writes;gpu::VolumeController*owner{};gpu::VolumeCallbacks calls;
    calls.setActive=[&](bool active){writes.push_back(active?"on":"off");};
    calls.setVolume=[&](auto endpoint,double value){writes.push_back("volume:"+std::string(endpoint));auto updated=owner->snapshot();updated.volume=value;owner->receiveSnapshot(std::move(updated));return true;};
    calls.setBalance=[&](auto endpoint,double value){writes.push_back("balance:"+std::string(endpoint));auto updated=owner->snapshot();updated.balance=value;owner->receiveSnapshot(std::move(updated));return true;};
    calls.setMute=[&](auto endpoint,bool value){writes.push_back("mute:"+std::string(endpoint));auto updated=owner->snapshot();updated.muted=value;owner->receiveSnapshot(std::move(updated));return true;};
    calls.setDefaultOutput=[&](auto endpoint){writes.push_back("output:"+std::string(endpoint));auto updated=owner->snapshot();updated.outputID=endpoint;owner->receiveSnapshot(std::move(updated));return true;};
    calls.setDefaultInput=[&](auto endpoint){writes.push_back("input:"+std::string(endpoint));return true;};
    calls.stopAllApps=[&]{writes.push_back("stopAll");};calls.setAppGain=[&](auto id,double){writes.push_back("gain:"+std::string(id));return true;};calls.stopApp=[&](auto id){writes.push_back("stop:"+std::string(id));return true;};
    gpu::VolumeController c(s,std::move(calls));owner=&c;
    check(!c.active()&&!c.mouseDown({100,120},0),"Inactive owner never writes audio");check(c.setActive(true)&&!c.setActive(true)&&writes.size()==1,"Activation is explicit and idempotent");
    check(action(c,"audio:output").rect==core::Rect{82,40,306,28}&&action(c,"audio:input").rect==core::Rect{82,75,306,28},"Source endpoint chooser geometry retained");
    check(slider(c,"volume").rect==core::Rect{82,116,235,25}&&slider(c,"balance").rect==core::Rect{102,158,244,25},"Original slider geometry and ranges retained");
    check(c.mouseDown({88,128},1)&&c.dragging()&&c.snapshot().volume==0,"Source six-point slider inset maps beginning to zero");c.mouseDragged({311,128});check(c.snapshot().volume==1,"Dragging clamps to original track endpoint");
    auto changed=c.snapshot();changed.outputID="o1";c.receiveSnapshot(changed);auto before=writes.size();check(!c.dragging()&&!c.mouseDragged({180,128})&&writes.size()==before,"Device change cancels gesture rather than retargeting a new endpoint");
    c.mouseDown({199.5,128},2);c.mouseUp();check(std::abs(*c.snapshot().volume-.5)<1e-12&&c.key(39,false,2)&&std::abs(*c.snapshot().volume-.52)<1e-12,"Right arrow applies source two-percent nudge to selected slider");
    check(!c.key(39,true,2),"Modified keys remain available to owner");check(c.perform("audio:mute",3)&&c.snapshot().muted==true,"Mute reflects event-returned state");
    check(c.perform("audio:output",4)&&c.choosing()&&!c.choosingInput()&&c.pageCount()==2&&c.actions().size()==8,"Output chooser has exactly six rows plus back and next");
    check(action(c,"audio:output:o1").label.ends_with(", Selected"),"Selected endpoint exposes source accessible state");
    check(!c.key(39,false,4),"Chooser does not redirect keyboard arrows to hidden volume slider");
    const auto sample=c.actionSample(4);check(sample.active&&sample.reveal==core::Rect{10,0,390,334}&&sample.transform.values[12]==11&&sample.transform.values[14]==-10,"Source action transform/reveal endpoints retained");
    check(c.scroll({20,70},39,4.1)&&c.pageIndex()==0&&c.scroll({20,70},1,4.2)&&c.pageIndex()==1,"Device wheel changes page only after forty-point threshold");
    check(c.scroll({20,70},-40,4.4)&&c.pageIndex()==0&&c.actionSample(4.4).transform.values[12]==-11,"Reverse wheel restores previous page with source reverse motion");
    check(c.perform("audio:output:o2",5)&&!c.choosing()&&writes[writes.size()-2]=="stopAll"&&writes.back()=="output:o2","Switching output stops routes before injected device transaction");
    check(c.perform("audio:input",6)&&c.choosingInput(),"Input chooser remains distinct from output");auto disconnected=c.snapshot();disconnected.inputs.clear();c.receiveSnapshot(std::move(disconnected));
    auto artwork=gpu::prepareVolumeScene(c);check(leaf(artwork,"volume/title")["text"]["string"].string()=="// Input device","Disconnected input chooser retains its title and empty-device artwork");
    check(c.key(27,false,7)&&!c.choosing()&&!c.key(27,false,7),"Escape only consumes a live chooser");
    check(c.scroll({20,260},10000,8)&&c.applicationScroll()==308&&c.selectedSlider()=="volume","Application list has smooth clamped point-based scrolling");
    artwork=gpu::prepareVolumeScene(c);check(leaf(artwork,"volume/app-track")["bounds"].array()[2].number()==2,"Source app scrollbar retains two-point width");
    check(c.scroll({20,260},-10000,9)&&c.applicationScroll()==0,"Application list returns to top without page switching");
    before=writes.size();check(c.setSlider("app:0",1)&&writes.size()==before,"Untouched direct route at unity never starts or recreates a service");
    changed=c.snapshot();changed.applications[0].state=gpu::VolumeAppState::active;changed.applications[0].gain=.5;changed.applications[1].state=gpu::VolumeAppState::failed;changed.applications[1].gain=.2;changed.applications[2].state=gpu::VolumeAppState::stopping;changed.applications[2].gain=.2;c.receiveSnapshot(changed);
    check(c.setSlider("app:0",1)&&writes.back()=="gain:0","Existing route at unity remains active");check(c.setSlider("app:1",1)&&writes.back()=="stop:1","Failed route unity retries cleanup explicitly");
    c.scroll({20,260},30,10);check(!c.setSlider("app:2",.4),"Stopping routes cannot accept new gain writes");
    check(c.perform("audio:headphones",11,true)&&c.headphones()&&c.applicationScroll()==30&&c.pageCount()==2&&!c.actionSample(11).active,"Detail toggle keeps source scroll state while reduced motion settles action");
    c.setActive(false);check(c.headphones()&&!c.choosing()&&!c.dragging()&&c.applicationScroll()==0&&c.pageIndex()==0&&writes.back()=="off","Leaving module resets transient input but preserves detail preference");
    auto invalid=c.snapshot();invalid.volume=std::numeric_limits<double>::quiet_NaN();const auto revision=c.contentRevision();rejects([&]{c.receiveSnapshot(invalid);},"Nonfinite provider snapshot rejected");check(c.contentRevision()==revision,"Rejected snapshot leaves prior retained model unchanged");
    rejects([&]{c.actionSample(std::numeric_limits<double>::infinity());},"Nonfinite frame clocks rejected");
    gpu::AudioSnapshot audio;audio.available=true;audio.paused=false;audio.devices={{L"opaque:{id}",L"Speakers 音箱",true}};audio.default_device_id=L"opaque:{id}";audio.volume=.3f;audio.muted=false;
    const auto mapped=gpu::volumeSnapshotFromSystemAudio(audio);check(mapped.outputs[0].name=="Speakers 音箱"&&mapped.outputID=="opaque:{id}"&&mapped.canSetVolume&&mapped.canSetMute&&!mapped.canSetDefaultOutput&&!mapped.canSetBalance&&!mapped.applicationActivitySupported,"Existing audio provider maps only its real supported capabilities");
    audio.paused=true;check(!gpu::volumeSnapshotFromSystemAudio(audio).canSetVolume,"Paused provider cannot leave an enabled stale volume writer");
    gpu::VolumeController minimal({});artwork=gpu::prepareVolumeScene(minimal);check(artwork.surfaces.size()==artwork.layers["children"].array().size(),"Unknown devices/values retain bounded explicit unavailable artwork");
    std::set<std::string>ids;for(const auto&item:artwork.surfaces)check(ids.insert(item.id).second,"Source surface identities are unique");
}
#ifdef _WIN32
class Window{public:Window(){WNDCLASSW c{};c.lpfnWndProc=DefWindowProcW;c.hInstance=GetModuleHandleW(nullptr);c.lpszClassName=L"EndfieldVolumeOwnedFixture";atom=RegisterClassW(&c);check(atom!=0,"Register owned Volume fixture");hwnd=CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP|WS_EX_TOOLWINDOW,c.lpszClassName,L"Owned Volume fixture",WS_POPUP,0,0,400,334,nullptr,nullptr,c.hInstance,nullptr);check(hwnd!=nullptr,"Create hidden owned Volume fixture");}~Window(){if(hwnd)DestroyWindow(hwnd);if(atom)UnregisterClassW(reinterpret_cast<LPCWSTR>(static_cast<ULONG_PTR>(atom)),GetModuleHandleW(nullptr));}HWND hwnd{};ATOM atom{};};
void native(const std::filesystem::path&shader){Window window;gpu::Renderer renderer;renderer.initialize(window.hwnd,400,334,{gpu::Driver::warpForTests,shader,gpu::RenderTarget::offscreenForTests});renderer.setCamera(gpu::layerViewportProjection(400,334));gpu::LayerRasterizer raster;gpu::LayerRasterOptions options;options.pixelsPerPoint=1;
    auto state=fixture();gpu::VolumeController c(state);c.setActive(true);gpu::NativeVolumeScene scene(c,raster,options);check(scene.syncContent()&&!scene.syncContent(),"Retained Volume source content loads only on explicit events");check(scene.scene().report().unsupported.empty(),"Volume source shapes/text are supported by shared raster");scene.updatePose({},1,0);gpu::LayerComposition composition;std::array entries{gpu::LayerCompositionEntry{&scene.scene(),{}}};composition.setEntries(renderer,entries);composition.present(renderer);renderer.draw(false);const auto first=renderer.readback();check(!first.pixels.empty(),"Hidden fixture paints real original-style Volume artwork");
    scene.setFeedback(core::Point{100,55},false,false,1);scene.updatePose({},1,1.14);composition.present(renderer);const auto steadyRaster=raster.stats();const auto steadyGPU=renderer.stats();allocations=0;counting=true;try{for(unsigned n=0;n<120;++n){core::Matrix4 matrix;matrix.values[3]=n*.000001;scene.updatePose(matrix,.8f,2+n/60.);composition.present(renderer);}}catch(...){counting=false;throw;}counting=false;
    check(allocations==0,"120 Volume tilt frames allocate no CPU storage");check(raster.stats().rasterizations==steadyRaster.rasterizations&&raster.stats().textLayoutsCreated==steadyRaster.textLayoutsCreated&&renderer.stats().textureUploads==steadyGPU.textureUploads&&renderer.stats().meshUploads==steadyGPU.meshUploads,"Pointer feedback and tilt reuse artwork/text/GPU resources");
    state.volume=0;c.receiveSnapshot(state);scene.syncContent();scene.updatePose({},1,5);composition.upload(renderer);composition.present(renderer);renderer.draw(false);check(scene.scene().report().unsupported.empty(),"Zero gain degenerate source fill is supported explicitly");check(scene.stats().localUpdates>0&&scene.stats().structureUpdates==1,"Master value event updates changed leaves without rebuilding complete module");
    c.perform("audio:output",6);scene.syncContent();scene.setFeedback({},false,true,6);scene.updatePose({},1,6);composition.setEntries(renderer,entries);composition.present(renderer);renderer.draw(false);check(scene.requiresFrames(6)&&!scene.requiresFrames(6.18),"Device menu action uses only its finite source frame deadline");
    c.dismissChooser(7);scene.syncContent();scene.updatePose({},1,7.18);composition.setEntries(renderer,entries);composition.present(renderer);state.volume.reset();state.balance.reset();state.canSetVolume=state.canSetBalance=false;c.receiveSnapshot(state);scene.syncContent();scene.updatePose({},1,8);composition.setEntries(renderer,entries);composition.present(renderer);renderer.draw(false);check(scene.scene().report().unsupported.empty(),"Unavailable controls render without guessing values");
    composition.detach(renderer);check(scene.scene().releaseResources(renderer)&&renderer.stats().textures==0&&renderer.stats().meshes==0,"Owner detach retires every Volume resource without another publisher");renderer.reset();
}
#endif
}
#ifdef _WIN32
int wmain(int argc,wchar_t**argv){const auto hr=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);try{check(SUCCEEDED(hr)&&argc==2,"Pass HLSL to hidden Volume fixture");portable();native(argv[1]);CoUninitialize();std::cout<<"PASS "<<checks<<" Volume checks\n";return 0;}catch(const std::exception&e){counting=false;if(SUCCEEDED(hr))CoUninitialize();std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}}
#else
int main(){try{portable();std::cout<<"PASS "<<checks<<" Volume portable checks\n";return 0;}catch(const std::exception&e){counting=false;std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}}
#endif
