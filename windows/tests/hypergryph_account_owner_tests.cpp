// Account Linking owner bundle with its production services: WinHTTP transport
// (never asked to send: the imported cache needs a fresh sign-in), DPAPI vault
// and owner-only cache in a temporary app root, the shared UtilityExecutor,
// a temporary ProfileStore, the module face and the header wallet on a hidden
// WARP target. No network request, login page, browser, real account, token,
// profile or visible window is used.
#ifdef _WIN32
#include "tools/account_linking_owner.hpp"
#include "modules/hypergryph_account_identity_sync.hpp"
#include "native/hypergryph_account_vault.hpp"
#include "native/module_scene.hpp"
#include "core/data/data_store.hpp"
#include <objbase.h>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <new>
#include <random>
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
    Window() {WNDCLASSW c{};c.lpfnWndProc=DefWindowProcW;c.hInstance=GetModuleHandleW(nullptr);c.lpszClassName=L"EndfieldAccountOwnerFixture";atom=RegisterClassW(&c);check(atom!=0,"Register hidden fixture");
        hwnd=CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP|WS_EX_TOOLWINDOW,c.lpszClassName,L"Account owner fixture",WS_POPUP,0,0,1280,800,nullptr,nullptr,c.hInstance,nullptr);check(hwnd&&!IsWindowVisible(hwnd),"Owned fixture stays hidden");}
    ~Window() {if(hwnd) DestroyWindow(hwnd);if(atom) UnregisterClassW(reinterpret_cast<LPCWSTR>(static_cast<ULONG_PTR>(atom)),GetModuleHandleW(nullptr));}
};
struct TempDirectory {
    std::filesystem::path path;
    TempDirectory() {
        wchar_t base[MAX_PATH+1]{};check(GetTempPathW(MAX_PATH,base)>0,"Temporary directory");std::random_device random;
        path=(std::filesystem::path(base)/(L"EndfieldHUD-account-owner-"+std::to_wstring(random())+L"-"+std::to_wstring(GetCurrentProcessId()))).lexically_normal();
        std::filesystem::create_directories(path);
    }
    ~TempDirectory() {std::error_code e;std::filesystem::remove_all(path,e);}
};
std::size_t painted(const gpu::Readback& r) {std::size_t n{};for(std::size_t i=3;i<r.pixels.size();i+=4) if(r.pixels[i]) ++n;return n;}
// A Mac Account/profile-cache.json after the offline import: linked Endfield
// role with a cached sanity sample, marked requiresReconnect (no credentials).
h::AccountCache importedCache(double now,h::Role& role) {
    role.region=h::Region::mainland;role.game=h::Game::endfield;role.bindingUID="9";role.roleID="4000500060";role.serverID="1";
    role.name="Endmin#1234";role.serverName="Asia";role.isDefault=true;
    h::Snapshot s;s.role=role;s.observedAt=now-1000;s.name="Endmin#1234";s.level=40;s.worldLevel=3;s.createdAt=now-86400.0*30;
    s.operatorCount=12;s.weaponCount=20;s.documentCount=100;s.stamina=h::Stamina{42,360,now-1000+318*432.0,now-1000};
    h::RegionRecord record;record.linked=true;record.requiresReconnect=true;record.roles={role};record.selectedRoleID=role.id();
    record.snapshots[role.id()]=s;record.bindingsAt=now-1000;
    h::AccountCache cache;cache.region=h::Region::mainland;cache.header="endfield";cache.syncProfile=true;cache.records["mainland"]=record;
    return cache;
}
void run(const std::filesystem::path& shader,const std::filesystem::path& resources) {
    Window window;TempDirectory temp;gpu::Renderer renderer;renderer.initialize(window.hwnd,1280,800,{gpu::Driver::warpForTests,shader,gpu::RenderTarget::offscreenForTests});
    renderer.setCamera(gpu::layerViewportProjection(1280,800));gpu::LayerRasterizer raster;
    double now=812692800,clock=50;h::Role role;
    {
        std::filesystem::create_directories(temp.path/L"Account");
        std::ofstream file(temp.path/L"Account"/L"profile-cache.json",std::ios::binary);file<<h::encodeCacheBytes(importedCache(now,role));
    }
    ehud::data::ProfileStore store(temp.path);
    {auto p=store.value();p.playerIDOverride="MANUAL";p.name="Local";p.tag="4321";check(store.update(p),"Seed a local profile with a manual UID override");}
    h::ProfileStoreAccountSink sink(store);
    HANDLE utilityEvent=CreateEventW(nullptr,FALSE,FALSE,nullptr);check(utilityEvent!=nullptr,"Utility wake event");
    app::UtilityExecutor executor([utilityEvent]{SetEvent(utilityEvent);});
    std::vector<std::string> events;int invalidations{};
    {
        tools::AccountLinkingOwnerOptions o;o.window=window.hwnd;o.dataRoot=temp.path;o.gaugeResources=resources;o.raster.pixelsPerPoint=1;
        o.clock=[&]{return clock;};o.wallClock=[&]{return now;};o.invalidate=[&]{++invalidations;};
        o.recordEvent=[&](std::string_view action){events.emplace_back(action);};o.profile=&sink;o.officialLogin=false;
        o.workMode=[]{return h::WorkModeGaugeInput{true,1500,61.5};};o.workModeRunning=[]{return true;};
        tools::AccountLinkingOwner owner(executor,raster,o);auto& controller=owner.controller();
        check(!owner.officialLoginAvailable(),"Hidden verification never creates a browser");
        // Imported cache: cached roles/sanity stay, a fresh Windows sign-in is required.
        check(controller.record().linked&&controller.record().requiresReconnect&&controller.gameSyncActive(),"Imported Mac cache keeps the linked Endfield role and requires re-login");
        const auto& synced=store.value();
        check(store.profileSyncLocked()&&synced.gamePlayerID==std::optional<std::string>("4000500060")&&!synced.playerIDOverride&&synced.name=="Endmin"&&synced.tag=="1234"&&
              synced.permissionLevel==40&&synced.explorationLevel==3&&!synced.hasManualAwakeningDate,
              "Cached Endfield snapshot syncs the personal profile and locks it while reconnecting (no network)");
        owner.resize({0,0,96,1,0,0});owner.resize({1280,800,96,1,1280,800});owner.setOverlayVisible(true,clock);
        const auto expected=controller.record().snapshots.at(role.id()).sanityPresentation(now);
        check(expected&&expected->current==44&&owner.gauge().model().value()=="44 / 360"&&owner.gauge().model().canOpen(),"Header wallet projects cached sanity locally (never below the reported 42)");
        check(owner.module().canvas().presentation().statusMessage=="Sign in again to continue syncing."&&owner.module().canvas().presentation().isLinked,"Account face asks for a fresh sign-in");
        const auto wake=owner.nextWakeTime(clock);const double untilNext=*expected->nextRecoveryAt-now;
        check(wake&&std::abs(*wake-(clock+untilNext))<1e-6&&untilNext>0&&untilNext<=432,"Only the next local recovery point is scheduled (no API refresh while reconnecting)");
        check(owner.activeRequests()==0,"No community request is ever started for a reconnect-required region");
        // Frames on the hidden WARP target.
        core::source::DesktopChromeSettings settings;settings.viewport={0,0,1280,800};settings.module=core::Module::account;core::ModulePresentation modules(core::Module::account);
        gpu::LayerComposition composition;std::vector<gpu::LayerCompositionEntry> all;
        const auto frame=[&](const core::Matrix4& center){
            owner.update(center,settings,modules.sample(clock).presentation,1,1,clock);owner.upload(renderer);
            all.assign(owner.moduleEntries().begin(),owner.moduleEntries().end());for(const auto& e:owner.headerEntries()) all.push_back(e);
            composition.setEntries(renderer,all);composition.present(renderer);
        };
        frame({});renderer.draw(false);
        const auto shot=renderer.readback();check(painted(shot)>1000,"Module face and header wallet paint");
        const auto alphaAt=[&](double x,double y){return shot.pixels[std::size_t(y)*shot.rowBytes+std::size_t(x)*4+3];};
        check(alphaAt(562+100,2+21)>0,"Wallet paints at the header position");
        // Recovery wake advances the projected value through the owner deadline.
        now+=untilNext;clock+=untilNext;owner.deadline(clock);check(owner.gauge().model().value()=="45 / 360"&&owner.activeRequests()==0,"Deadline at the recovery point shows the next local point; still no request");
        // Projected Account controls.
        gpu::LayerScene geometry(raster);gpu::LayerRasterOptions fixture;geometry.load(Json::Object{{"bounds",Json::Array{0,0,400,334}},{"children",Json::Array{}}},fixture);
        gpu::NativeModuleSurface surface(geometry,core::Module::account);surface.update({},settings,modules.sample(clock).presentation.current,1);
        const core::Projection plane=core::Projection::viewport(gpu::layerViewportProjection(1280,800)*surface.pose().contentWorld,1280,800);
        const auto click=[&](double x,double y){
            const auto p=plane.project({x,y});check(p.has_value(),"Plane projects the control");clock+=.05;
            const bool handled=owner.pointer({app::PointerKind::down,app::PointerButton::left,p->x,p->y},clock);
            owner.pointer({app::PointerKind::up,app::PointerButton::left,p->x,p->y},clock);return handled;
        };
        // Connect without WebView2: explicit failure through the private owner wake.
        check(click(276+56,7+14),"Connect takes the projected click");
        MSG msg{};int wakes{};
        while(PeekMessageW(&msg,window.hwnd,o.serviceMessage,o.serviceMessage,PM_REMOVE)) {
            if(owner.message({msg.hwnd,msg.message,msg.wParam,msg.lParam},clock)) ++wakes;
        }
        check(wakes>=1&&!controller.isPresentingLogin()&&!FindWindowW(L"EndfieldHUDAccountLogin",nullptr),"Login unavailability arrives on the owner wake; no browser window exists");
        check(!owner.message({window.hwnd,WM_APP+1,0,0},clock),"Unrelated messages are not consumed");
        // Header recovery popover: wallet click opens it, Esc closes only it.
        frame({});
        const core::Point wallet{562+90,2+20};
        check(owner.covers(wallet)&&owner.pointer({app::PointerKind::down,app::PointerButton::left,wallet.x,wallet.y},clock+=.05)&&owner.capturesPointer(),"Wallet click opens the recovery popover");
        check(owner.key({app::KeyKind::down,VK_ESCAPE},false,clock+=.05)&&!owner.gauge().model().popoverOpen()&&!owner.capturesPointer(),"Esc dismisses only the recovery popover");
        clock+=.5;frame({});
        // Steady tilt frames through the bundle: no allocation.
        frame({});const auto beforeRaster=raster.stats();allocations=0;counting=true;
        try {
            for(unsigned n=0;n<120;++n) {
                auto center=core::Matrix4{};center.values[3]=n*.000001;clock+=1./60;
                owner.update(center,settings,modules.sample(clock).presentation,1,1,clock);owner.upload(renderer);composition.present(renderer);
            }
        } catch(...) {counting=false;throw;}
        counting=false;check(allocations==0,"120 changing-tilt owner frames allocate nothing");
        check(raster.stats().rasterizations==beforeRaster.rasterizations,"Tilt never re-rasterizes account content");
        frame({}); // back to the untilted plane used by the click projection
        // Disconnect through the shaped confirmation; vault/cache work runs on the shared executor.
        {
            const auto& canvas=owner.module().canvas();const auto& p=canvas.presentation();
            std::string state="status="+std::string(h::statusName(p.status))+" linked="+(p.isLinked?"1":"0")+" busy="+(p.busy()?"1":"0");
            for(const auto& c:canvas.accessibleActions()) state+=" "+c.id+(c.enabled?"+":"-");
            const bool down=click(360+14,47+14);
            if(!down||!canvas.isPopoverOpen()||!owner.capturesPointer())
                throw std::runtime_error("Disconnect opens the confirmation menu: down="+std::to_string(down)+" open="+std::to_string(canvas.isPopoverOpen())+
                    " captures="+std::to_string(owner.capturesPointer())+" "+state);
            ++checks;
        }
        check(click(144+244-40+14,80+88-40+14),"Confirm disconnect");
        for(int i=0;i<200&&controller.hasActiveRequest();++i) {WaitForSingleObject(utilityEvent,20);executor.drain();}
        check(!controller.hasActiveRequest()&&!controller.record().linked&&!events.empty()&&events.back()=="unlinked","Disconnect removes the region through the utility queue and logs accountAction unlinked");
        check(owner.flush(clock),"The shutdown flush writes the latest Account cache bytes");
        const auto cachePath=temp.path/L"Account"/L"profile-cache.json";
        const auto saved=h::decodeCacheBytes(*gpu::readBoundedFile(cachePath,h::maximumCacheBytes));
        check(saved&&saved->records.count("mainland")==0&&gpu::ownerOnly(cachePath),"Cache drops the region and is rewritten owner-only");
        check(!store.profileSyncLocked()&&store.value().name=="Endmin"&&store.value().gamePlayerID==std::optional<std::string>("4000500060")&&!store.value().playerIDOverride,
              "Unlinking unlocks the profile and keeps the imported values (no automatic restore)");
        check(owner.gauge().model().value()=="24 / 25"&&!owner.gauge().model().canOpen(),"Unlinked header shows Work Mode minutes");
        check(invalidations>0,"Controller changes request frames");
        // Closing the HUD: no account wake except none; nothing in flight.
        owner.overlayClosing(clock+=.1);
        check(!owner.nextWakeTime(clock)&&!owner.capturesPointer()&&owner.activeRequests()==0,"Closed HUD schedules no account work");
        check(owner.flush(clock),"A repeated flush has nothing left to write");
        composition.setEntries(renderer,{});owner.collected(renderer);owner.release(renderer);composition.detach(renderer);
        check(!IsWindowVisible(window.hwnd),"Fixture never shows UI");
    }
    executor.shutdown();CloseHandle(utilityEvent);renderer.reset();
}
}
int wmain(int argc,wchar_t** argv) {
    const auto hr=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    try {check(SUCCEEDED(hr)&&argc==3,"Pass shader and windows/resources/account");run(argv[1],std::filesystem::absolute(argv[2]).lexically_normal());CoUninitialize();std::cout<<"PASS "<<checks<<" Account owner checks\n";return 0;}
    catch(const std::exception& e) {counting=false;if(SUCCEEDED(hr)) CoUninitialize();std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}
}
#endif
