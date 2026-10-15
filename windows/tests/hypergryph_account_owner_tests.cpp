// Account Linking owner bundle with its production services: WinHTTP transport
// (never asked to send: the imported cache needs a fresh sign-in), DPAPI vault
// and owner-only cache in a temporary app root, the shared UtilityExecutor,
// a temporary ProfileStore, the module face and the header wallet on a hidden
// WARP target. No network request, login page, browser, real account, token,
// profile or visible window is used.
//
// Then the Mac import followed by a Windows sign-in: the import tool's codec
// turns the Mac oracle cache into the committed Account/profile-cache.json,
// and a fresh owner over it (the hidden-coverage community host replaying the
// oracle envelopes, an in-memory vault and the fake official-site host that
// runs the real login policy) reconnects through its accessibility peer.
#ifdef _WIN32
#include "tools/account_linking_owner.hpp"
#include "app/areas/account_coverage.hpp"
#include "core/data/file_io.hpp"
#include "core/migration/mac_import_codecs.hpp"
#include "modules/hypergryph_account_identity_sync.hpp"
#include "native/hypergryph_account_vault.hpp"
#include "native/module_scene.hpp"
#include "core/data/data_store.hpp"
#include <objbase.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <algorithm>
#include <atomic>
#include <chrono>
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
// Startup cost of the production owner (reported, not asserted): the first
// construction in the process (WinHTTP session, DPAPI vault, imported cache
// read, pinned wallet resources), the first HUD open and its first frame's
// CPU work (artwork, upload and composition; WARP drawing excluded).
using Clock=std::chrono::steady_clock;
double since(Clock::time_point start) {return std::chrono::duration<double,std::milli>(Clock::now()-start).count();}
double constructionMS{},openingMS{},firstFrameMS{};
// Launch prewarm (reported): the first opening frame on Map without and with
// it, and the prewarm itself. Face park (reported): what a closed HUD keeps
// of an Account face shown earlier, at this test's 1 px per point (the
// production face uses 2, about four times the bytes).
double coldOpeningMS{},prewarmedOpeningMS{},prewarmMS{};std::uint64_t coldOpeningWork{};
// Process threads and private bytes around the first owner's construction (reported).
unsigned threadsBeforeOwner{},threadsAfterOwner{};std::size_t privateBeforeOwner{},privateAfterOwner{};
unsigned processThreads() {
    const auto snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0);if(snapshot==INVALID_HANDLE_VALUE) return 0;
    THREADENTRY32 entry{};entry.dwSize=sizeof entry;unsigned count{};
    for(BOOL more=Thread32First(snapshot,&entry);more;more=Thread32Next(snapshot,&entry)) if(entry.th32OwnerProcessID==GetCurrentProcessId()) ++count;
    CloseHandle(snapshot);return count;
}
std::size_t privateBytes() {PROCESS_MEMORY_COUNTERS_EX m{};m.cb=sizeof m;return GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&m),sizeof m)?m.PrivateUsage:0;}
std::size_t faceRasterBytes{},faceGPUBytes{},faceParkedRasterBytes{},faceParkedGPUBytes{};
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
        threadsBeforeOwner=processThreads();privateBeforeOwner=privateBytes();
        const auto constructing=Clock::now();
        tools::AccountLinkingOwner owner(executor,raster,o);constructionMS=since(constructing);auto& controller=owner.controller();
        threadsAfterOwner=processThreads();privateAfterOwner=privateBytes();
        check(!owner.officialLoginAvailable(),"Hidden verification never creates a browser");
        check(owner.networkSession()&&!owner.networkSession()->open&&!owner.networkSession()->opening&&owner.networkSession()->opens==0,
              "Constructing the owner opens no WinHTTP session (the first request opens it on the shared queue)");
        // Imported cache: cached roles/sanity stay, a fresh Windows sign-in is required.
        check(controller.record().linked&&controller.record().requiresReconnect&&controller.gameSyncActive(),"Imported Mac cache keeps the linked Endfield role and requires re-login");
        const auto& synced=store.value();
        check(store.profileSyncLocked()&&synced.gamePlayerID==std::optional<std::string>("4000500060")&&!synced.playerIDOverride&&synced.name=="Endmin"&&synced.tag=="1234"&&
              synced.permissionLevel==40&&synced.explorationLevel==3&&!synced.hasManualAwakeningDate,
              "Cached Endfield snapshot syncs the personal profile and locks it while reconnecting (no network)");
        const auto opening=Clock::now();
        owner.resize({0,0,96,1,0,0});owner.resize({1280,800,96,1,1280,800});owner.setOverlayVisible(true,clock);openingMS=since(opening);
        const auto expected=controller.record().snapshots.at(role.id()).sanityPresentation(now);
        check(expected&&expected->current==44&&owner.gauge().model().value()=="44 / 360"&&owner.gauge().model().canOpen(),"Header wallet projects cached sanity locally (never below the reported 42)");
        check(owner.module().canvas().presentation().statusMessage=="Sign in again to continue syncing."&&owner.module().canvas().presentation().isLinked,"Account face asks for a fresh sign-in");
        const auto wake=owner.nextWakeTime(clock);const double untilNext=*expected->nextRecoveryAt-now;
        check(wake&&std::abs(*wake-(clock+untilNext))<1e-6&&untilNext>0&&untilNext<=432,"Only the next local recovery point is scheduled (no API refresh while reconnecting)");
        check(owner.activeRequests()==0,"No community request is ever started for a reconnect-required region");
        // The import wrote the cache with inherited security: the owner applies
        // the owner-only DACL in place on the shared queue (contents untouched).
        const auto cacheFile=temp.path/L"Account"/L"profile-cache.json";
        for(int i=0;i<100&&!gpu::ownerOnly(cacheFile);++i) {WaitForSingleObject(utilityEvent,20);executor.drain();}
        check(gpu::ownerOnly(cacheFile)&&gpu::readBoundedFile(cacheFile,h::maximumCacheBytes)==std::optional<std::string>(h::encodeCacheBytes(importedCache(now,role))),
              "The imported cache is re-secured owner-only in place, byte for byte");
        // Frames on the hidden WARP target.
        core::source::DesktopChromeSettings settings;settings.viewport={0,0,1280,800};settings.module=core::Module::account;core::ModulePresentation modules(core::Module::account);
        gpu::LayerComposition composition;std::vector<gpu::LayerCompositionEntry> all;
        const auto frame=[&](const core::Matrix4& center){
            owner.update(center,settings,modules.sample(clock).presentation,1,1,clock);owner.upload(renderer);
            all.assign(owner.moduleEntries().begin(),owner.moduleEntries().end());for(const auto& e:owner.headerEntries()) all.push_back(e);
            composition.setEntries(renderer,all);composition.present(renderer);
        };
        const auto first=Clock::now();frame({});firstFrameMS=since(first);renderer.draw(false);
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
        // The unlinked wallet (Work Mode minutes) is still the wallet's control:
        // its press is consumed (it never falls through to the host's close for
        // a press outside the displayed circle) and opens nothing.
        clock+=.5;frame({});clock+=.1;frame({});
        check(owner.covers(wallet)&&owner.pointer({app::PointerKind::down,app::PointerButton::left,wallet.x,wallet.y},clock+=.05)&&
              !owner.gauge().model().popoverOpen()&&!owner.capturesPointer(),"A press on the unlinked wallet is consumed and opens nothing");
        (void)owner.pointer({app::PointerKind::up,app::PointerButton::left,wallet.x,wallet.y},clock);
        // Another module before the close: the face leaves the composition but
        // stays resident (switching back inside the open HUD rebuilds nothing).
        {
            auto mapSettings=settings;mapSettings.module=core::Module::map;core::ModulePresentation mapModules(core::Module::map);
            owner.update({},mapSettings,mapModules.sample(clock).presentation,1,1,clock);owner.upload(renderer);
            all.assign(owner.headerEntries().begin(),owner.headerEntries().end());composition.setEntries(renderer,all);composition.present(renderer);
            check(owner.moduleEntries().empty(),"The face is not composed on another module");
        }
        const auto shown=owner.module().sceneStats();const auto gpuShown=renderer.stats();const auto rasterShown=raster.stats();
        check(shown.uploaded&&shown.surfaces>0&&shown.pixelBytes>0,"The face stays resident while the HUD is open on another module");
        // Closing the HUD: no account wake; nothing in flight.
        owner.overlayClosing(clock+=.1);
        check(!owner.nextWakeTime(clock)&&!owner.capturesPointer()&&owner.activeRequests()==0,"Closed HUD schedules no account work");
        // The close finished (HostEvent::overlayConcealed): the hidden face parks.
        owner.concealed(clock);
        const auto parked=owner.module().sceneStats();const auto gpuParked=renderer.stats();const auto rasterParked=raster.stats();
        check(parked.parks==1&&!parked.uploaded&&parked.surfaces==0&&parked.pixelBytes==0,"Once the HUD closed on another module the Account face keeps no leaf raster or GPU target");
        check(gpuParked.nativeGroups+1==gpuShown.nativeGroups&&gpuParked.nativeGroupBytes<gpuShown.nativeGroupBytes&&gpuParked.resourceBytes<gpuShown.resourceBytes&&
              rasterParked.entries<rasterShown.entries&&rasterParked.resourceBytes<rasterShown.resourceBytes,
              "Its group target, leaf textures and shared rasterizer entries are released");
        faceRasterBytes=rasterShown.resourceBytes;faceParkedRasterBytes=rasterParked.resourceBytes;
        faceGPUBytes=gpuShown.resourceBytes+gpuShown.nativeGroupBytes;faceParkedGPUBytes=gpuParked.resourceBytes+gpuParked.nativeGroupBytes;
        owner.concealed(clock);check(owner.module().sceneStats().parks==1,"A second conceal changes nothing");
        // Showing Account again rebuilds the same artwork.
        owner.setOverlayVisible(true,clock+=.1);frame({});
        const auto again=owner.module().sceneStats();
        check(again.uploaded&&again.surfaces==shown.surfaces&&again.pixelBytes==shown.pixelBytes&&raster.stats().entries==rasterShown.entries&&
              raster.stats().resourceBytes==rasterShown.resourceBytes&&renderer.stats().nativeGroupBytes==gpuShown.nativeGroupBytes,"Showing Account again rebuilds the same artwork");
        // A close with Account shown keeps its face for the next opening.
        owner.overlayClosing(clock+=.1);owner.concealed(clock);
        check(owner.module().sceneStats().parks==1&&owner.module().sceneStats().uploaded,"A close with Account shown keeps the face");
        check(owner.flush(clock),"A repeated flush has nothing left to write");
        check(owner.networkSession()&&!owner.networkSession()->open&&owner.networkSession()->opens==0,"No WinHTTP session was ever opened (the imported region needs a new sign-in)");
        composition.setEntries(renderer,{});owner.collected(renderer);owner.release(renderer);composition.detach(renderer);
        check(!IsWindowVisible(window.hwnd),"Fixture never shows UI");
    }
    executor.shutdown();CloseHandle(utilityEvent);renderer.reset();
}
}
namespace {
// Launch prewarm (ModuleOwner::prewarm) through the production owner on a
// fresh unlinked data root, the first opening on Map like EndfieldHUD.exe:
// once without and once with the prewarm (and the host's publish and hidden
// draw that follow it). After the prewarm the first opening frame installs
// nothing of the header wallet.
void prewarmedOpening(const std::filesystem::path& shader,const std::filesystem::path& resources) {
    Window window;gpu::Renderer renderer;renderer.initialize(window.hwnd,1280,800,{gpu::Driver::warpForTests,shader,gpu::RenderTarget::offscreenForTests});
    renderer.setCamera(gpu::layerViewportProjection(1280,800));gpu::LayerRasterizer raster;double clock=10;
    HANDLE utilityEvent=CreateEventW(nullptr,FALSE,FALSE,nullptr);check(utilityEvent!=nullptr,"Utility wake event");
    app::UtilityExecutor executor([utilityEvent]{SetEvent(utilityEvent);});
    core::source::DesktopChromeSettings settings;settings.viewport={0,0,1280,800};settings.module=core::Module::map;core::ModulePresentation modules(core::Module::map);
    const auto opening=[&](bool prewarm,double& firstFrame){
        TempDirectory temp;
        tools::AccountLinkingOwnerOptions o;o.window=window.hwnd;o.dataRoot=temp.path;o.gaugeResources=resources; // production density (2 px per point)
        o.clock=[&]{return clock;};o.wallClock=[]{return 812692800.0;};o.officialLogin=false;
        o.workMode=[]{return h::WorkModeGaugeInput{true,1500,61.5};};o.workModeRunning=[]{return true;};
        tools::AccountLinkingOwner owner(executor,raster,o);
        check(owner.networkSession()&&!owner.networkSession()->open&&owner.networkSession()->opens==0,"A fresh owner opens no WinHTTP session");
        owner.resize({1280,800,96,1,1280,800});
        gpu::LayerComposition composition;std::vector<gpu::LayerCompositionEntry> all;
        const auto publish=[&]{
            owner.upload(renderer);all.assign(owner.moduleEntries().begin(),owner.moduleEntries().end());for(const auto& e:owner.headerEntries()) all.push_back(e);
            composition.setEntries(renderer,all);composition.present(renderer);
        };
        if(prewarm) {
            const auto begin=Clock::now();owner.prewarm({},settings,modules.sample(clock).presentation,clock);publish();prewarmMS=since(begin);renderer.draw(false);
            check(owner.moduleEntries().empty()&&owner.gauge().sceneStats().groupUploads>=1&&!owner.covers({562+90,2+20})&&
                  !owner.pointer({app::PointerKind::down,app::PointerButton::left,562+90,2+20},clock)&&!owner.requiresFrames(clock),
                  "The prewarm installs the wallet of the closed HUD (composed at opacity 0, taking no press); nothing animates");
        }
        const auto rasters=raster.stats().rasterizations;const auto gpu=renderer.stats();const auto gauge=owner.gauge().sceneStats();
        const auto begin=Clock::now();
        owner.setOverlayVisible(true,clock+=.1);owner.update({},settings,modules.sample(clock).presentation,1,1,clock);publish();
        firstFrame=since(begin);renderer.draw(false);
        const auto now=renderer.stats();const auto after=owner.gauge().sceneStats();
        // CPU work only: the renderer draws a group target into its texture
        // when it first becomes visible (never for a hidden composition).
        const std::uint64_t work=(raster.stats().rasterizations-rasters)+(now.textureUploads-gpu.textureUploads)+(now.meshUploads-gpu.meshUploads)+
            (after.artworkRasters-gauge.artworkRasters)+(after.numberRasters-gauge.numberRasters)+(after.groupUploads-gauge.groupUploads)+(after.groupRedraws-gauge.groupRedraws);
        if(prewarm) check(work==0,"After the prewarm the first opening frame rasterizes, uploads and re-plans nothing of the wallet");
        else {coldOpeningWork=work;check(work>0,"Without the prewarm the first opening frame installs the wallet");}
        check(all.size()==2,"The opened HUD composes the wallet and its popover group");
        // The unlinked wallet (Work Mode minutes) is a control: consumed, nothing opens.
        const core::Point wallet{562+90,2+20};
        check(owner.gauge().model().value()=="24 / 25"&&!owner.gauge().model().canOpen()&&owner.covers(wallet)&&
              owner.pointer({app::PointerKind::down,app::PointerButton::left,wallet.x,wallet.y},clock+=.05)&&!owner.capturesPointer()&&!owner.gauge().model().popoverOpen(),
              "A press on the unlinked wallet is consumed and opens nothing");
        (void)owner.pointer({app::PointerKind::up,app::PointerButton::left,wallet.x,wallet.y},clock);
        check(!owner.covers({562+170,2+20})&&!owner.covers({562+90,2+45}),"Outside the wallet the header owns no press");
        check(owner.networkSession()&&!owner.networkSession()->open&&owner.networkSession()->opens==0,"An unlinked account never opens a WinHTTP session");
        owner.overlayClosing(clock+=.1);owner.concealed(clock);
        composition.setEntries(renderer,{});owner.collected(renderer);owner.release(renderer);composition.detach(renderer);
    };
    opening(false,coldOpeningMS);opening(true,prewarmedOpeningMS);
    check(!IsWindowVisible(window.hwnd),"Nothing was shown");
    executor.shutdown();CloseHandle(utilityEvent);renderer.reset();
}
// Mac import -> restart -> Windows sign-in through the production owner.
void reconnectAfterImport(const std::filesystem::path& resources,const std::filesystem::path& oracle,const std::filesystem::path& coverage) {
    namespace areas=endfield::app::areas;
    // The linked Mac cache with both header games, as the controller contract picks it.
    const auto source=ehud::data::detail::readFile(oracle,8*1024*1024);check(source.has_value(),"Read the Mac account oracle");
    const auto json=Json::parse(*source,8*1024*1024);const Json* mac=nullptr;
    for(const auto& entry:json["controller"]["trace"].array()) {
        const auto& cache=entry["after"]["cache"];
        if(!cache.isObject()||!cache["records"].contains("mainland")) continue;
        const auto& record=cache["records"]["mainland"];
        if(record["linked"].boolean()&&!record["requiresReconnect"].boolean()&&cache["syncProfile"].boolean()&&record["snapshots"].object().size()==2) {mac=&cache;break;}
    }
    check(mac!=nullptr,"The Mac oracle trace has a linked cache with both header games");
    const auto imported=ehud::migration::importMacAccountCache(mac->encode());
    check(imported.disconnectedRegions==std::vector<std::string>{"mainland"}&&imported.roles>0&&imported.snapshots==2,"The import tool disconnects the linked region and keeps its roles and snapshots");
    double now{};
    for(const auto& [id,snapshot]:(*mac)["records"]["mainland"]["snapshots"].object()) now=std::max(now,snapshot["observedAt"].isString()?std::strtod(snapshot["observedAt"].string().c_str(),nullptr):snapshot["observedAt"].number());
    TempDirectory temp;
    {
        // The import's commit: ordinary file, inherited permissions.
        std::filesystem::create_directories(temp.path/L"Account");
        std::ofstream file(temp.path/L"Account"/L"profile-cache.json",std::ios::binary);file<<imported.bytes;
    }
    const auto cacheFile=temp.path/L"Account"/L"profile-cache.json";
    check(!gpu::ownerOnly(cacheFile),"The committed import inherits its folder's permissions");
    ehud::data::ProfileStore store(temp.path);h::ProfileStoreAccountSink sink(store);
    const auto f=areas::loadAccountCoverageFixture(coverage);
    areas::SyntheticCommunityHost community(f.responses);areas::SyntheticAvatarLoader avatar(community);areas::SyntheticLoginHost login(community,f.nonce);
    h::MemoryCredentialVault vault;
    Window window;gpu::LayerRasterizer raster;double clock=80;
    HANDLE utilityEvent=CreateEventW(nullptr,FALSE,FALSE,nullptr);check(utilityEvent!=nullptr,"Utility wake event");
    app::UtilityExecutor executor([utilityEvent]{SetEvent(utilityEvent);});
    std::vector<std::string> events;
    {
        tools::AccountLinkingOwnerOptions o;o.window=window.hwnd;o.dataRoot=temp.path;o.gaugeResources=resources;o.raster.pixelsPerPoint=1;
        o.clock=[&]{return clock;};o.wallClock=[&]{return now;};o.recordEvent=[&](std::string_view action){events.emplace_back(action);};o.profile=&sink;
        o.officialLogin=false;o.wake=[]{};o.synthetic=gpu::HypergryphAccountServicesOptions::Synthetic{&community,&vault,&avatar};o.login=&login;
        o.workMode=[]{return h::WorkModeGaugeInput{true,1500,61.5};};o.workModeRunning=[]{return true;};
        tools::AccountLinkingOwner owner(executor,raster,o);auto& controller=owner.controller();
        // Utility completions and the owner wake (the registered message), until idle.
        const auto settle=[&]{
            for(int n=0;n<400;++n) {
                executor.drain();(void)owner.message({window.hwnd,o.serviceMessage,0,0},clock);
                if(!community.queued()&&!controller.hasActiveRequest()&&gpu::ownerOnly(cacheFile)) break;
                WaitForSingleObject(utilityEvent,10);
            }
            check(!community.queued()&&!controller.hasActiveRequest(),"Account work settles");
        };
        {
            const auto& record=controller.record();
            check(record.linked&&record.requiresReconnect&&record.roles.size()==imported.roles&&record.snapshots.size()==2,"The restarted owner reads the imported roles and snapshots");
        }
        check(store.profileSyncLocked()==imported.profileSyncLocked,"The profile lock is the one the import reported");
        // The face and wallet as the HUD shows them after the restart.
        owner.resize({1280,800,96,1,1280,800});owner.setOverlayVisible(true,clock);
        core::source::DesktopChromeSettings settings;settings.viewport={0,0,1280,800};settings.module=core::Module::account;core::ModulePresentation modules(core::Module::account);
        owner.update({},settings,modules.sample(clock).presentation,1,1,clock);
        check(owner.module().canvas().presentation().statusMessage=="Sign in again to continue syncing."&&owner.module().canvas().presentation().isLinked,
              "The face asks for a new Windows sign-in");
        const auto wallet=controller.gaugeValue(h::WorkModeGaugeInput{true,1500,61.5}).value;
        check(owner.gauge().model().value()==wallet&&wallet.find(" / ")!=std::string::npos&&owner.gauge().model().canOpen(),"The header wallet shows the cached sanity");
        settle();
        check(gpu::ownerOnly(cacheFile)&&gpu::readBoundedFile(cacheFile,h::maximumCacheBytes)==std::optional<std::string>(imported.bytes),
              "The imported cache is restricted to the current user in place, byte for byte");
        check(community.requests().empty()&&!vault.load(h::Region::mainland),"Nothing is requested and no credential exists before the new sign-in");
        // Accessibility peers: Reconnect, and the sync toggle's imported state.
        const auto controls=owner.accessibleControls();
        const auto peer=[&](std::string_view id)->const gpu::AccountAutomationControl*{for(const auto& c:controls) if(c.id==id) return &c;return nullptr;};
        const auto* connect=peer("account:connect");const auto* sync=peer("account:syncProfile");
        check(connect&&connect->label=="Reconnect"&&connect->enabled&&!connect->toggled&&sync&&sync->toggled==std::optional<bool>((*mac)["syncProfile"].boolean()),
              "UI Automation lists Reconnect and the imported sync toggle state");
        check(connect->notice=="Sign in again to continue syncing."&&connect->help.find("\nSign in again to continue syncing.")!=std::string::npos,
              "Reconnect's help text and announcement say a new sign-in is needed");
        // Reconnect through the peer: the official-login policy and the accepted session.
        check(owner.performAccessible("account:connect",clock+=.05)&&login.isPresenting()&&controller.isPresentingLogin(),"Reconnect presents the official login");
        auto* session=login.session();
        check(session&&session->consent(clock)&&session->allowNavigation(f.site,true,false),"Consent opens the official site");
        session->pageStarted(clock);session->pageFinished(true,"www.skland.com");
        const auto& accepted=f.messages.back();
        auto result=session->credentialMessage(Json::parse(accepted.body),accepted.mainFrame,accepted.frame,accepted.page);
        check(result.has_value()&&login.finish(std::move(result),h::LoginFailure::cancelled),"The page's session completes the login");
        settle();
        const auto saved=vault.load(h::Region::mainland);
        check(saved&&saved->cred==accepted.session->cred&&saved->signingToken==accepted.session->signingToken&&saved->deviceID==accepted.session->deviceID,
              "The new Windows credential is stored");
        check(controller.record().linked&&!controller.record().requiresReconnect&&controller.presentation().status==h::AccountPresentation::Status::connected&&
              std::find(events.begin(),events.end(),"linked")!=events.end(),"The region syncs again");
        check(!community.requests().empty()&&community.requests().front().path=="/api/v1/game/player/binding"&&!community.rejected(),"Syncing resumes with the bindings");
        for(const auto& r:community.requests())
            check(r.header("cred")==std::optional<std::string>(accepted.session->cred)&&r.header("sign")&&r.header("sign")->size()==32,"Every request carries the new signed credential");
        check(store.profileSyncLocked()==imported.profileSyncLocked,"The profile lock survives the new sign-in");
        for(const auto& c:owner.accessibleControls()) if(c.id=="account:connect") check(c.label=="Reconnect"&&c.notice!="Sign in again to continue syncing.","The sign-in request leaves the face");
        check(owner.flush(clock),"The flush writes the reconnected cache");
        const auto written=h::decodeCacheBytes(*gpu::readBoundedFile(cacheFile,h::maximumCacheBytes));
        check(written&&written->records.count("mainland")&&!written->records.at("mainland").requiresReconnect&&gpu::ownerOnly(cacheFile),
              "The owner-only cache no longer asks for a sign-in (and holds no credential)");
        const auto bytes=*gpu::readBoundedFile(cacheFile,h::maximumCacheBytes);
        check(bytes.find(accepted.session->cred)==std::string::npos&&bytes.find(accepted.session->signingToken.value_or(accepted.session->cred))==std::string::npos,
              "Credentials stay out of the cache");
        owner.overlayClosing(clock+=.1);
    }
    executor.shutdown();CloseHandle(utilityEvent);
    check(!IsWindowVisible(window.hwnd),"Nothing was shown");
}
}
int wmain(int argc,wchar_t** argv) {
    const auto hr=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    try {
        check(SUCCEEDED(hr)&&argc==5,"Pass shader, windows/resources/account, hypergryph-account-source.json and hypergryph-account-coverage.json");
        const auto resources=std::filesystem::absolute(argv[2]).lexically_normal();
        run(argv[1],resources);prewarmedOpening(argv[1],resources);reconnectAfterImport(resources,argv[3],argv[4]);
        CoUninitialize();
        std::cout<<"PASS "<<checks<<" Account owner checks (startup: construction "<<constructionMS<<" ms, first open "<<openingMS<<" ms, first frame "<<firstFrameMS<<" ms;"
                 <<" first opening on Map: "<<coldOpeningMS<<" ms with "<<coldOpeningWork<<" rasterizations/uploads/renders cold, "<<prewarmedOpeningMS<<" ms and none after a "<<prewarmMS<<" ms prewarm;"
                 <<" face park: rasterizer "<<faceRasterBytes/1024<<" -> "<<faceParkedRasterBytes/1024<<" KiB, GPU "<<faceGPUBytes/1024<<" -> "<<faceParkedGPUBytes/1024<<" KiB at 1 px per point;"
                 <<" first owner construction: threads "<<threadsBeforeOwner<<" -> "<<threadsAfterOwner<<", private "<<privateBeforeOwner/1024<<" -> "<<privateAfterOwner/1024<<" KiB)\n";
        return 0;
    }
    catch(const std::exception& e) {counting=false;if(SUCCEEDED(hr)) CoUninitialize();std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}
}
#endif
