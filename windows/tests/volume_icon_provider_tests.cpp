#include "native/volume_icon_provider.hpp"
#include <array>
#include <atomic>
#include <iostream>
#include <stdexcept>
namespace n=endfield::native;
namespace {
unsigned checks{};void check(bool v,const char*m){++checks;if(!v)throw std::runtime_error(m);}
template<class F>void rejects(F&&f,const char*m){bool failed{};try{f();}catch(const std::exception&){failed=true;}check(failed,m);}
n::VolumeIconApplication app(unsigned index){const auto digits=std::to_string(index);n::VolumeIconApplication a;a.applicationID=digits+":100";a.requestToken="00000000-0000-4000-8000-"+std::string(12-digits.size(),'0')+digits;n::AudioApplicationExecutable e;e.path="C:\\explicit-synthetic\\app-"+digits+".exe";e.identity.objectID[0]=static_cast<std::uint8_t>(index);e.identity.volumeSerial=1;a.executable=e;return a;}
auto image(n::LayerImageSource&cache,const n::ShelfIconRequest&r){std::array<std::uint8_t,64*64*4>rgba{};for(std::size_t k=0;k<rgba.size();k+=4){rgba[k]=100;rgba[k+3]=255;}return cache.publish(r.imageKey,r.revision,64,64,rgba);}
void portable(){n::VolumeIconPlan plan;n::LayerImageSource cache;auto a=app(1);const std::array first{a};check(plan.setVisible(first)&&plan.requests().size()==1,"Exact process executable becomes one shared-worker request");const auto request=plan.requests()[0];
    check(request.path==a.executable->path&&request.identity==a.executable->identity&&request.itemID==a.requestToken&&!request.directory&&!request.unavailable,"Worker receives verified file identity and ephemeral token without a Shelf record");
    check(!plan.binding(a.applicationID)->image,"No icon artwork is invented before completion");
    const auto pixels=image(cache,request);const std::array result{n::VolumeIconResult{request.imageKey,request.itemID,request.revision,pixels,0,false}};
    check(plan.receive(result)&&plan.binding(a.applicationID)->image==pixels,"Actual icon uses borrowed immutable source pixels");const auto revision=plan.contentRevision();
    check(!plan.setVisible(first)&&!plan.receive(result)&&plan.contentRevision()==revision,"Identical content/results leave stable numeric revisions");
    auto invalid=a;invalid.applicationID="2:100";const std::array collision{invalid};rejects([&]{plan.setVisible(collision);},"Ephemeral UUID cannot represent a different retained process");
    const std::array duplicate{a,a};rejects([&]{plan.setVisible(duplicate);},"Duplicate process identities reject atomically");
    auto broken=app(2);broken.executable->path="relative.exe";const std::array lateBad{a,broken};rejects([&]{plan.setVisible(lateBad);},"Late malformed executable path rejects whole candidate");check(plan.contentRevision()==revision&&plan.binding(a.applicationID)->image==pixels,"Rejected content retains prior ready bindings");
    check(plan.hide()&&plan.bindings().empty()&&!plan.receive(result),"Hide cancels visible completions without retiring borrowed outgoing pixels");
    check(pixels->premultipliedBGRA()[3]==255&&plan.setVisible(first)&&plan.binding(a.applicationID)->image==pixels,"Show reuses the retained exact icon without an extra pixel buffer");
    check(plan.setVisible(first,true)&&plan.requests()[0].revision>request.revision&&!plan.binding(a.applicationID)->image,"Explicit retry advances revision and clears stale artwork");
    check(!plan.receive(result),"Previous-generation completion cannot repopulate a new icon binding");const auto retry=plan.requests()[0];auto generic=image(cache,retry);const std::array fallback{n::VolumeIconResult{retry.imageKey,retry.itemID,retry.revision,generic,0,true}};
    check(plan.receive(fallback)&&!plan.binding(a.applicationID)->image&&plan.binding(a.applicationID)->typeFallback,"Shell type-only fallback preserves original no-icon layout");
    auto b=app(3);b.executable.reset();const std::array noIcon{b};check(plan.setVisible(noIcon)&&plan.requests().empty()&&!plan.binding(b.applicationID)->image,"Unavailable executable metadata leaves working text-only app row");
    for(unsigned k=10;k<55;++k){const std::array visible{app(k)};plan.setVisible(visible);check(plan.retainedCount()<=n::VolumeIconPlan::maximumRetained,"Recent process icon handles stay bounded");}
    std::vector<n::VolumeIconApplication>tooMany;for(unsigned k=100;k<109;++k)tooMany.push_back(app(k));rejects([&]{plan.setVisible(tooMany);},"Visible work respects shared worker bound");
    n::VolumeIconPlan transaction;n::LayerImageSource transactionCache;transaction.setVisible(first);const auto valid=transaction.requests()[0];auto good=image(transactionCache,valid);const std::array validThenWrong{n::VolumeIconResult{valid.imageKey,valid.itemID,valid.revision,good,0,false},n::VolumeIconResult{valid.imageKey,valid.itemID,valid.revision+1,good,0,false}};rejects([&]{transaction.receive(validThenWrong);},"Malformed completion batch rejects before ready artwork changes");check(!transaction.binding(a.applicationID)->image,"Failed completion transaction leaves no partial binding");
}
}
#ifdef _WIN32
namespace d=ehud::data;
namespace {
struct Handle{HANDLE h{};explicit Handle(HANDLE value=nullptr):h(value){}~Handle(){if(h)CloseHandle(h);}Handle(const Handle&)=delete;};
struct Window {HWND hwnd{};ATOM atom{};static constexpr UINT message=WM_APP+298;Window(){WNDCLASSW w{};w.lpfnWndProc=DefWindowProcW;w.hInstance=GetModuleHandleW(nullptr);w.lpszClassName=L"EndfieldOwnedVolumeIcons";atom=RegisterClassW(&w);check(atom!=0,"Register owned hidden icon fixture");hwnd=CreateWindowExW(WS_EX_TOOLWINDOW,w.lpszClassName,L"Synthetic Volume icons",WS_POPUP,0,0,1,1,nullptr,nullptr,w.hInstance,nullptr);check(hwnd&&!IsWindowVisible(hwnd),"Native Volume fixture remains hidden");}~Window(){if(hwnd)DestroyWindow(hwnd);if(atom)UnregisterClassW(reinterpret_cast<LPCWSTR>(static_cast<ULONG_PTR>(atom)),GetModuleHandleW(nullptr));}};
struct Probe {std::atomic<unsigned>resolved{},released{},extracted{};std::atomic<DWORD>worker{};std::atomic<bool>differentIdentity{};};
void wait(n::NativeVolumeIconProvider&volume,const Window&w,std::size_t completed){const auto deadline=GetTickCount64()+10000;while(completed){const auto old=volume.plan().contentRevision();volume.drain(41);if(volume.plan().contentRevision()!=old)--completed;if(!completed)return;const auto now=GetTickCount64();check(now<deadline,"Bounded synthetic icon completion deadline");const auto result=MsgWaitForMultipleObjectsEx(0,nullptr,static_cast<DWORD>(deadline-now),QS_POSTMESSAGE,MWMO_INPUTAVAILABLE);check(result==WAIT_OBJECT_0,"Completion wakes existing owner event loop without polling");MSG m{};while(PeekMessageW(&m,w.hwnd,Window::message,Window::message,PM_REMOVE))check(m.wParam==41&&m.lParam==0,"Shared notice carries generation and no pointer");}}
void native(){Window window;n::LayerImageSource cache;auto probe=std::make_shared<Probe>();
    n::NativeShelfIconProvider shared(cache,[probe](const d::ShelfRecord&r){probe->worker=GetCurrentThreadId();++probe->resolved;d::ShelfFileMetadata m;m.windowsPath=r.windowsPath;m.identity=r.identity;if(probe->differentIdentity)m.identity.objectID[0]^=1;return d::ShelfFileAccess{std::move(m),[probe]{++probe->released;}};},{window.hwnd,Window::message,41},[probe](const n::ShelfIconRequest&r,const d::ShelfFileAccess*lease){++probe->extracted;n::ShelfIconPixels p;p.width=p.height=64;p.result=S_OK;p.typeFallback=!lease||r.identity.objectID[0]==2;p.straightRGBA.resize(64*64*4);for(std::size_t k=0;k<p.straightRGBA.size();k+=4){p.straightRGBA[k]=100;p.straightRGBA[k+3]=255;}return p;});
    const auto before=shared.stats();n::NativeVolumeIconProvider volume(shared,cache);check(shared.stats().extractions==before.extractions,"Volume facade creates no specialized worker or native audio activation");
    const std::array first{app(1)};volume.setVisible(first);wait(volume,window,1);check(volume.plan().binding("1:100")->image&&probe->worker!=GetCurrentThreadId()&&probe->resolved==probe->released,"Shared worker resolves identity/extracts off UI and closes lease before publish");
    const auto ready=shared.stats();check(!volume.setVisible(first)&&shared.stats().requests==ready.requests&&shared.stats().extractions==ready.extractions,"Repeated visible app event does no worker or raster work");
    const std::array fallback{app(2)};volume.setVisible(fallback);wait(volume,window,1);check(!volume.plan().binding("2:100")->image&&volume.plan().binding("2:100")->typeFallback,"Actual facade excludes generic type fallback from icon row");
    volume.hide();n::ShelfIconRequest shelf;shelf.itemID=app(80).requestToken;shelf.imageKey="shelf.synthetic.reused";shelf.revision=100;shelf.path=app(80).executable->path;shelf.identity=app(80).executable->identity;const std::array shelfWork{shelf};shared.setVisible(shelfWork);const auto deadline=GetTickCount64()+10000;std::vector<n::ShelfIconCompletion>completed;while(completed.empty()){completed=shared.drain(41);if(!completed.empty())break;const auto now=GetTickCount64();check(now<deadline,"Shelf reuse completes on same worker");check(MsgWaitForMultipleObjectsEx(0,nullptr,static_cast<DWORD>(deadline-now),QS_POSTMESSAGE,MWMO_INPUTAVAILABLE)==WAIT_OBJECT_0,"Borrowed worker wakes Shelf owner");MSG m{};while(PeekMessageW(&m,window.hwnd,Window::message,Window::message,PM_REMOVE)){};}
    check(completed.front().image&&probe->extracted==3,"Shelf and Volume share one worker rather than independent pools");shared.hide();volume.setVisible(first);check(volume.plan().binding("1:100")->image&&shared.stats().queued==0,"Returning to Volume reuses exact shared cached icon");
    volume.hide();cache.clear();volume.setVisible(first);check(!volume.plan().binding("1:100")->image,"Evicted shared cache cannot expose a missing raster image descriptor");wait(volume,window,1);check(bool(volume.plan().binding("1:100")->image),"Evicted icon is restored by explicit visibility event");
    probe->differentIdentity=true;const std::array changed{app(3)};volume.setVisible(changed);wait(volume,window,1);check(!volume.plan().binding("3:100")->image&&volume.plan().binding("3:100")->typeFallback,"Replaced executable file identity never becomes a guessed app icon");
    volume.hide();const auto idle=shared.stats();check(!volume.hide()&&volume.drain(40)==false&&shared.stats().extractions==idle.extractions,"Hidden/stale messages perform no extraction");Handle done(shared.duplicateWorkerHandle());shared.stop();check(WaitForSingleObject(done.h,10000)==WAIT_OBJECT_0&&probe->resolved==probe->released,"Explicit shared owner shutdown releases all synthetic leases");
}
}
#endif
int main(){try{portable();
#ifdef _WIN32
native();
std::cout<<"PASS "<<checks<<" shared native Volume icon checks\n";
#else
std::cout<<"PASS "<<checks<<" portable Volume icon checks; Windows worker not executed\n";
#endif
return 0;}catch(const std::exception&e){std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}}
