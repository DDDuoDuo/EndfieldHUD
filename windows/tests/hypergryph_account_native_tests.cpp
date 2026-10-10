// Account module face and header sanity gauge on the shared renderer (hidden
// owned window, WARP offscreen target). A scripted in-memory service, memory
// vault and immediate work queue stand in for the network/DPAPI/worker; no real
// account, token, file outside the test, UI or network is touched.
#ifdef _WIN32
#include "tools/account_preview.hpp"
#include "tools/account_sanity_gauge_preview.hpp"
#include "native/module_scene.hpp"
#include "core/module_presentation.hpp"
#include <objbase.h>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <iostream>
#include <new>
namespace {std::atomic<bool> counting{};std::atomic<std::size_t> allocations{};}
void* operator new(std::size_t n) {if(counting) ++allocations;if(auto* p=std::malloc(n?n:1)) return p;throw std::bad_alloc();}
void* operator new[](std::size_t n) {return ::operator new(n);}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete[](void* p) noexcept {std::free(p);}
#if defined(__cpp_sized_deallocation)
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
void operator delete[](void* p,std::size_t) noexcept {std::free(p);}
#endif
namespace {
namespace gpu=endfield::native;namespace core=endfield::core;namespace tools=endfield::tools;namespace app=endfield::app;namespace h=endfield::modules::hypergryph;
using Json=ehud::data::Json;
unsigned checks{};void check(bool v,const char* s) {++checks;if(!v) throw std::runtime_error(s);}
struct Window {
    ATOM atom{};HWND hwnd{};
    Window() {WNDCLASSW c{};c.lpfnWndProc=DefWindowProcW;c.hInstance=GetModuleHandleW(nullptr);c.lpszClassName=L"EndfieldAccountOwnedFixture";atom=RegisterClassW(&c);check(atom!=0,"Register hidden fixture");
        hwnd=CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP|WS_EX_TOOLWINDOW,c.lpszClassName,L"Owned Account fixture",WS_POPUP,0,0,1280,800,nullptr,nullptr,c.hInstance,nullptr);check(hwnd&&!IsWindowVisible(hwnd),"Owned fixture stays hidden");}
    ~Window() {if(hwnd) DestroyWindow(hwnd);if(atom) UnregisterClassW(reinterpret_cast<LPCWSTR>(static_cast<ULONG_PTR>(atom)),GetModuleHandleW(nullptr));}
};
struct NoRequest:h::AccountRequest {void cancel() override {}};
// Scripted service: bindings and profiles answer immediately on the owner queue.
struct Service:h::AccountService {
    std::deque<std::function<void()>>& queue;double now;
    Service(std::deque<std::function<void()>>& q,double n):queue(q),now(n) {}
    h::RequestHandle refreshCredentials(const std::string&,h::Region,std::function<void(h::Outcome<h::Credentials>)> done) override {queue.push_back([done]{done(h::Error{h::ErrorKind::transport});});return std::make_shared<NoRequest>();}
    h::RequestHandle bindings(const h::Credentials&,h::Region region,std::function<void(h::Outcome<std::vector<h::Role>>)> done) override {
        h::Role ef;ef.region=region;ef.game=h::Game::endfield;ef.bindingUID="9";ef.roleID="4000500060";ef.serverID="1";ef.name="Endmin#1234";ef.serverName="Asia";ef.isDefault=true;
        h::Role ak;ak.region=region;ak.game=h::Game::arknights;ak.bindingUID="88001";ak.roleID="88001";ak.serverID="1";ak.name="Doctor";
        queue.push_back([done,ef,ak]{done(std::vector<h::Role>{ef,ak});});return std::make_shared<NoRequest>();
    }
    h::RequestHandle profile(const h::Role& role,const h::Credentials&,std::function<void(h::Outcome<h::Snapshot>)> done) override {
        h::Snapshot s;s.role=role;s.observedAt=now;s.name=role.name;s.stamina=h::Stamina{42,360,now+318*432.0,now};
        queue.push_back([done,s]{done(s);});return std::make_shared<NoRequest>();
    }
};
struct Immediate:h::AccountWorkQueue {
    std::deque<std::function<void()>>& queue;explicit Immediate(std::deque<std::function<void()>>& q):queue(q) {}
    void run(std::function<void()> work,std::function<void(std::exception_ptr)> done) override {
        std::exception_ptr error;try {work();} catch(...) {error=std::current_exception();}
        queue.push_back([done,error]{done(error);});
    }
};
void drain(std::deque<std::function<void()>>& q) {while(!q.empty()) {auto f=std::move(q.front());q.pop_front();f();}}
std::size_t painted(const gpu::Readback& r) {std::size_t n{};for(std::size_t i=3;i<r.pixels.size();i+=4) if(r.pixels[i]) ++n;return n;}
void run(const std::filesystem::path& shader,const std::filesystem::path& resources) {
    Window window;gpu::Renderer renderer;renderer.initialize(window.hwnd,1280,800,{gpu::Driver::warpForTests,shader,gpu::RenderTarget::offscreenForTests});
    renderer.setCamera(gpu::layerViewportProjection(1280,800));gpu::LayerRasterizer raster;
    std::deque<std::function<void()>> queue;double clock=812692800;Service service(queue,clock);h::MemoryCredentialVault vault;Immediate work(queue);
    h::AccountControllerOptions o;o.now=[&]{return clock;};o.localDate=[](double){return h::LocalDateFields{10,5,12,0,0};};
    h::HypergryphAccountController controller(service,vault,work,nullptr,nullptr,nullptr,nullptr,o);
    tools::AccountPreviewOptions options;options.raster.pixelsPerPoint=1;options.reduceMotion=false;
    tools::AccountPreview account(controller,raster,options);account.resize({1280,800,96,1,1280,800});
    core::source::DesktopChromeSettings settings;settings.viewport={0,0,1280,800};settings.module=core::Module::account;core::ModulePresentation modules(core::Module::account);double time{};
    account.setOverlayVisible(true,time);
    gpu::LayerComposition composition;auto sample=modules.sample(time).presentation;account.update({},settings,sample,1,time);account.upload(renderer);
    composition.setEntries(renderer,account.entries());composition.present(renderer);renderer.draw(false);
    check(painted(renderer.readback())>1000,"Unlinked Account face paints original-style controls");
    check(account.canvas().accessibleActions().size()==8&&account.canvas().accessibleActions()[0].label=="Connect","Unlinked face offers Connect");
    // Link through the controller's test seam; the scripted service answers on the owner queue.
    controller.acceptLogin("SYNTHETIC-CRED",h::Region::mainland,"synthetic-token","synthetic-device");drain(queue);drain(queue);drain(queue);
    account.presentationChanged(++time);
    check(account.canvas().presentation().isLinked&&account.canvas().presentation().accountName=="Endmin#1234","Linked presentation reaches the face");
    account.update({},settings,modules.sample(time).presentation,1,time);account.upload(renderer);composition.setEntries(renderer,account.entries());composition.present(renderer);renderer.draw(false);
    // Projected region button opens the shaped menu that captures input.
    gpu::LayerScene geometry(raster);gpu::LayerRasterOptions fixture;geometry.load(Json::Object{{"bounds",Json::Array{0,0,400,334}},{"children",Json::Array{}}},fixture);
    gpu::NativeModuleSurface surface(geometry,core::Module::account);surface.update({},settings,modules.sample(time).presentation.current,1);
    const core::Projection plane=core::Projection::viewport(gpu::layerViewportProjection(1280,800)*surface.pose().contentWorld,1280,800);
    const auto point=[&](double x,double y){const auto p=plane.project({x,y});check(p.has_value(),"Plane projects fixture point");return *p;};
    auto p=point(250,95);
    check(account.covers(p)&&account.pointer({app::PointerKind::down,app::PointerButton::left,p.x,p.y},++time),"Projected region control takes the click");
    account.pointer({app::PointerKind::up,app::PointerButton::left,p.x,p.y},time);
    check(account.canvas().isPopoverOpen()&&account.capturesPointer()&&account.requiresFrames(time),"Region menu opens with its 0.16 s fade");
    account.update({},settings,modules.sample(time).presentation,1,time);account.upload(renderer);composition.setEntries(renderer,account.entries());composition.present(renderer);renderer.draw(false);
    check(account.key({app::KeyKind::down,VK_ESCAPE,1,0,false,false,false,true},false,time+.1)&&account.canvas().isPopoverOpen(),"Auto-repeated keys are consumed without acting (Mac ignores repeats)");
    check(!account.key({app::KeyKind::down,'A'},false,time+.1)&&!account.key({app::KeyKind::down,VK_DOWN},true,time+.1)&&account.canvas().isPopoverOpen(),"Other or modified keys pass through");
    check(account.key({app::KeyKind::down,VK_ESCAPE},false,time+.2)&&!account.canvas().isPopoverOpen(),"Esc dismisses only the account menu");
    check(account.requiresFrames(time+.2),"Dismissed menu fades out over 0.14 s");
    account.update({},settings,modules.sample(time+.4).presentation,1,time+.4);account.update({},settings,modules.sample(time+.41).presentation,1,time+.41);
    account.upload(renderer);composition.setEntries(renderer,account.entries());composition.present(renderer);renderer.draw(false);
    time+=.5;account.update({},settings,modules.sample(time).presentation,1,time);
    check(!account.requiresFrames(time),"Settled face requests no frames");
    // Steady tilt frames on the host path (update, upload, present): no raster,
    // texture upload, group redraw or allocation; only the group pose moves.
    account.upload(renderer);composition.setEntries(renderer,account.entries());composition.present(renderer);
    check(account.sceneStats().groupUploads>=1,"Account face renders through its retained encoded-sRGB group");
    const auto beforeRaster=raster.stats();const auto beforeGPU=renderer.stats();const auto beforeScene=account.sceneStats();allocations=0;counting=true;
    try {for(unsigned n=0;n<120;++n) {auto center=core::Matrix4{};center.values[3]=n*.000001;account.update(center,settings,modules.sample(time+=1./60).presentation,1,time);account.upload(renderer);composition.present(renderer);}} catch(...) {counting=false;throw;}
    counting=false;
    check(allocations==0,"120 changing-tilt Account frames allocate nothing");
    check(raster.stats().rasterizations==beforeRaster.rasterizations&&renderer.stats().textureUploads==beforeGPU.textureUploads,"Tilt never re-rasterizes Account content or uploads pixels");
    check(account.sceneStats().groupUploads==beforeScene.groupUploads&&account.sceneStats().groupRedraws==beforeScene.groupRedraws,"Tilt never redraws the retained Account group target");
    check(account.nextWakeTime(time).has_value(),"Linked visible account schedules its next refresh/recovery wake on the host clock");

    // Header gauge shares the same controller.
    tools::AccountSanityGaugeOptions gaugeOptions;gaugeOptions.raster.pixelsPerPoint=1;gaugeOptions.workMode=[]{return h::WorkModeGaugeInput{true,1500,61.5};};gaugeOptions.workModeRunning=[]{return true;};
    tools::AccountSanityGaugePreview gauge(controller,raster,resources,gaugeOptions);gauge.resize({1280,800,96,1,1280,800});gauge.setOverlayVisible(true,time);
    check(gauge.model().value()=="42 / 360"&&gauge.model().canOpen(),"Linked Endfield header shows projected sanity");
    gauge.update({},1,time);gauge.upload(renderer);
    std::vector<gpu::LayerCompositionEntry> all(account.entries().begin(),account.entries().end());for(const auto& e:gauge.entries()) all.push_back(e);
    composition.setEntries(renderer,all);composition.present(renderer);renderer.draw(false);
    const auto shot=renderer.readback();const auto alphaAt=[&](double x,double y){return shot.pixels[std::size_t(y)*shot.rowBytes+std::size_t(x)*4+3];};
    check(alphaAt(562+100,2+21)>0&&alphaAt(562+30,2+21)>0,"Wallet and sanity icon paint at the header position (562,2)");
    const core::Point wallet{562+90,2+20};
    check(gauge.covers(wallet)&&gauge.pointer({app::PointerKind::down,app::PointerButton::left,wallet.x,wallet.y},++time)&&gauge.model().popoverOpen()&&gauge.capturesPointer(),"Wallet click opens the recovery popover");
    check(gauge.model().tooltip().text[2]!="—"&&gauge.requiresFrames(time),"Popover shows countdowns and animates open");
    const auto wake=gauge.nextWakeTime(time);check(wake&&*wake<=time+1.0001,"Open popover wakes for its next visible countdown second");
    gauge.dismiss(time);
    {const auto idle=gauge.nextWakeTime(time);check(idle&&*idle>time+1.5,"Closed sanity wallet wakes only for the refresh guard, recovery points or refresh (no per-second ticks)");}
    gauge.pointer({app::PointerKind::down,app::PointerButton::left,wallet.x,wallet.y},time);check(gauge.model().popoverOpen(),"Popover reopens");
    // Popover refresh (Mac onRefresh -> refresh(manual: true)) after the 5 s guard.
    check(!gauge.model().tooltip().refreshEnabled,"Refresh is guarded for 5 s after the last attempt");
    clock+=6;gauge.contentChanged(time+.1);check(gauge.model().tooltip().refreshEnabled,"Refresh enables after the guard");
    const core::Point refreshPoint{562+(-56+195+11.5),2+(40+19+12.5)};
    check(gauge.pointer({app::PointerKind::down,app::PointerButton::left,refreshPoint.x,refreshPoint.y},time+.15)&&controller.hasActiveRequest(),"Popover refresh starts one manual refresh");
    check(gauge.model().popoverOpen()&&!gauge.model().tooltip().refreshEnabled,"Refresh disables while syncing; the popover stays open");
    drain(queue);drain(queue);drain(queue);gauge.contentChanged(time+.18);
    check(!controller.hasActiveRequest()&&gauge.model().popoverOpen(),"Manual refresh completes through the owner queue");
    gauge.update({},1,time+.2);gauge.upload(renderer);all.assign(account.entries().begin(),account.entries().end());for(const auto& e:gauge.entries()) all.push_back(e);
    composition.setEntries(renderer,all);composition.present(renderer);renderer.draw(false);
    check(gauge.key({app::KeyKind::down,VK_ESCAPE},false,time+.3)&&!gauge.model().popoverOpen(),"Esc dismisses only the recovery popover");
    h::AccountAction hide;hide.kind=h::AccountAction::Kind::selectHeaderMode;hide.headerMode=h::AccountPresentation::HeaderMode::hidden;controller.perform(hide);gauge.contentChanged(time+.4);
    check(gauge.model().hidden()&&!gauge.model().canOpen(),"Hidden header mode removes the gauge");
    hide.headerMode=h::AccountPresentation::HeaderMode::workMode;controller.perform(hide);gauge.contentChanged(time+.5);
    check(gauge.model().value()=="24 / 25"&&!gauge.model().canOpen(),"Work Mode header shows Work Mode minutes");
    {
        // The header change made a refresh due inside the 5 s guard: one wake when it opens settles the 600 s cadence.
        const auto guard=gauge.nextWakeTime(time+.5);check(guard&&std::abs(*guard-(time+.5+5))<1e-6,"A due refresh blocked by the guard wakes once when it opens");
        clock+=5;gauge.deadline(time+.5);
        const auto minute=gauge.nextWakeTime(time+.5);check(minute&&std::abs(*minute-(time+.5+58.5))<1e-9,"A running Work Mode countdown then wakes once, at the next shown minute");
    }
    time+=1;gauge.update({},1,time);gauge.upload(renderer);all.assign(account.entries().begin(),account.entries().end());for(const auto& e:gauge.entries()) all.push_back(e);
    composition.setEntries(renderer,all);composition.present(renderer);
    const auto beforeGauge=gauge.sceneStats();allocations=0;counting=true;
    try {for(unsigned n=0;n<120;++n) {auto center=core::Matrix4{};center.values[3]=n*.000001;gauge.update(center,1,time+=1./60);gauge.upload(renderer);composition.present(renderer);}} catch(...) {counting=false;throw;}
    counting=false;check(allocations==0,"120 changing-tilt gauge frames allocate nothing");
    check(gauge.sceneStats().groupUploads==beforeGauge.groupUploads&&gauge.sceneStats().groupRedraws==beforeGauge.groupRedraws,"Tilt never redraws the retained gauge group targets");
    const auto stats=gauge.sceneStats();check(stats.artworkRasters==1,"Wallet artwork rasterizes once per scale");
    // Teardown.
    composition.setEntries(renderer,{});account.collected(renderer);gauge.collected(renderer);account.release(renderer);gauge.release(renderer);composition.detach(renderer);
    check(!IsWindowVisible(window.hwnd),"Fixture never shows UI");
    renderer.reset();
}
}
int wmain(int argc,wchar_t** argv) {
    const auto hr=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    try {check(SUCCEEDED(hr)&&argc==3,"Pass shader and windows/resources/account");run(argv[1],std::filesystem::absolute(argv[2]).lexically_normal());CoUninitialize();std::cout<<"PASS "<<checks<<" Account native checks\n";return 0;}
    catch(const std::exception& e) {counting=false;if(SUCCEEDED(hr)) CoUninitialize();std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}
}
#endif
