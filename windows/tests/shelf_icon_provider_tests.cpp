#include "native/shelf_icon_provider.hpp"
#include "native/file_shelf_files.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
namespace {
unsigned portableChecks{};
void portableCheck(bool value,const char*message){++portableChecks;if(!value)throw std::runtime_error(message);}
void portableRequests(){
    endfield::native::ShelfIconRequest r;r.imageKey="shelf.icon.synthetic";r.revision=1;r.itemID="00000000-0000-4000-8000-000000000001";r.path="C:\\explicit-synthetic\\file.txt";
    auto valid=[](const auto&v){return endfield::native::validShelfIconRequest(v);};
    portableCheck(valid(r),"Explicit bounded request is portable");
    auto bad=r;bad.imageKey.clear();portableCheck(!valid(bad),"Empty image key rejects");
    bad=r;bad.imageKey.resize(endfield::native::LayerImageSource::maximumKeyBytes+1,'x');portableCheck(!valid(bad),"Oversized image key rejects");
    bad=r;bad.imageKey.push_back('\0');portableCheck(!valid(bad),"Embedded key NUL rejects");
    bad=r;bad.imageKey="\xff";portableCheck(!valid(bad),"Malformed UTF-8 key rejects");
    bad=r;bad.revision=0;portableCheck(!valid(bad),"Zero revision rejects");
    bad=r;bad.itemID="community-user-123";portableCheck(!valid(bad),"Unrelated identifier cannot replace shelf UUID");
    bad=r;bad.path="relative.txt";portableCheck(!valid(bad),"Relative file path rejects");
    bad=r;bad.path="C:\\explicit-synthetic\\file.txt:private";portableCheck(!valid(bad),"Alternate data stream rejects");
    bad=r;bad.path="\\\\?\\C:\\file.txt";portableCheck(!valid(bad),"Device namespace rejects");
    bad=r;bad.identity.volumeUUID="not-a-volume-uuid";portableCheck(!valid(bad),"Volume UUID follows persisted schema");
    r.identity.volumeUUID="00000000-0000-4000-8000-000000000002";portableCheck(valid(r),"Optional canonical volume UUID accepted");
    r.path="\\\\synthetic-server\\synthetic-share\\folder";r.directory=true;r.unavailable=true;portableCheck(valid(r),"Explicit UNC unavailable-folder request retains type-only contract");
}
}
#ifdef _WIN32
namespace n=endfield::native;namespace d=ehud::data;
namespace {
unsigned checks{};void check(bool v,const char*m){++checks;if(!v)throw std::runtime_error(m);}
template<class F>void rejects(F f,const char*m){bool rejected{};try{f();}catch(const std::exception&){rejected=true;}check(rejected,m);}
struct Handle{HANDLE value{};explicit Handle(HANDLE h=nullptr):value(h){}~Handle(){if(value)CloseHandle(value);}Handle(const Handle&)=delete;};
struct Window {
    HWND hwnd{};ATOM atom{};static constexpr UINT notice=WM_APP+245;
    Window(){WNDCLASSW w{};w.lpfnWndProc=DefWindowProcW;w.hInstance=GetModuleHandleW(nullptr);w.lpszClassName=L"EndfieldSyntheticShelfIcons";atom=RegisterClassW(&w);check(atom!=0,"Register owned icon fixture");hwnd=CreateWindowExW(WS_EX_TOOLWINDOW,w.lpszClassName,L"Hidden icon fixture",WS_POPUP,0,0,1,1,nullptr,nullptr,w.hInstance,nullptr);check(hwnd&&!IsWindowVisible(hwnd),"Owned icon test window stays hidden");}
    ~Window(){if(hwnd)DestroyWindow(hwnd);if(atom)UnregisterClassW(reinterpret_cast<LPCWSTR>(static_cast<ULONG_PTR>(atom)),GetModuleHandleW(nullptr));}
    n::ShelfIconRoute route(UINT_PTR generation)const{return {hwnd,notice,generation};}
};
n::ShelfIconRequest request(unsigned index){const auto digits=std::to_string(index);n::ShelfIconRequest r;r.itemID="00000000-0000-4000-8000-"+std::string(12-digits.size(),'0')+digits;r.imageKey="shelf.icon."+r.itemID;r.revision=1;r.path="C:\\explicit-synthetic\\file-"+digits+".txt";r.identity.objectID[0]=static_cast<std::uint8_t>(index+1);r.identity.volumeSerial=1;return r;}
std::vector<n::ShelfIconRequest>requests(unsigned count,unsigned start=0){std::vector<n::ShelfIconRequest>out;for(unsigned k=0;k<count;++k)out.push_back(request(start+k));return out;}
struct Probe {
    Handle entered{CreateEventW(nullptr,TRUE,FALSE,nullptr)},release{CreateEventW(nullptr,TRUE,FALSE,nullptr)},never{CreateEventW(nullptr,TRUE,FALSE,nullptr)};
    std::atomic<bool>blockNext{},leasesOpen{true};std::atomic<unsigned>acquired{},released{},calls{};std::atomic<DWORD>worker{};
    Probe(){check(entered.value&&release.value&&never.value,"Create bounded synthetic synchronization handles");}
};
auto resolver(std::shared_ptr<Probe>p){return [p](const d::ShelfRecord&r){p->worker=GetCurrentThreadId();++p->acquired;d::ShelfFileMetadata metadata;metadata.windowsPath=r.windowsPath;metadata.identity=r.identity;metadata.isDirectory=r.isDirectory;metadata.kind=r.isDirectory?d::ShelfFileKind::directory:d::ShelfFileKind::regular;return d::ShelfFileAccess{std::move(metadata),[p]{++p->released;}};};}
auto extractor(std::shared_ptr<Probe>p){return [p](const n::ShelfIconRequest&r,const d::ShelfFileAccess*lease){++p->calls;p->leasesOpen=p->leasesOpen.load()&&lease&&lease->open();if(p->blockNext.exchange(false)){SetEvent(p->entered.value);if(WaitForSingleObject(p->release.value,10000)!=WAIT_OBJECT_0)return n::ShelfIconPixels{0,0,{},E_ABORT,false};}
    n::ShelfIconPixels out;out.width=out.height=64;out.result=S_OK;out.straightRGBA.resize(64*64*4);for(std::size_t k=0;k<out.straightRGBA.size();k+=4){out.straightRGBA[k]=r.identity.objectID[0];out.straightRGBA[k+3]=255;}return out;};}
std::vector<n::ShelfIconCompletion>collect(n::NativeShelfIconProvider&provider,const Window&w,UINT_PTR generation,std::size_t count){
    std::vector<n::ShelfIconCompletion>out;const auto deadline=GetTickCount64()+10000;
    while(out.size()<count){auto batch=provider.drain(generation);for(auto&item:batch)out.push_back(std::move(item));if(out.size()>=count)break;const auto now=GetTickCount64();check(now<deadline,"Bounded icon completion deadline");const auto waited=MsgWaitForMultipleObjectsEx(0,nullptr,static_cast<DWORD>(deadline-now),QS_POSTMESSAGE,MWMO_INPUTAVAILABLE);check(waited==WAIT_OBJECT_0,"Worker notifies existing owner event loop without polling");MSG message{};while(PeekMessageW(&message,w.hwnd,Window::notice,Window::notice,PM_REMOVE)){check(message.lParam==0,"Icon notices contain a generation, never a raw callback pointer");}}
    MSG pending{};while(PeekMessageW(&pending,w.hwnd,Window::notice,Window::notice,PM_REMOVE))check(pending.lParam==0,"Completed notice carries no pointer");return out;
}
void stopped(n::NativeShelfIconProvider&provider){Handle completion(provider.duplicateWorkerHandle());provider.stop();check(WaitForSingleObject(completion.value,10000)==WAIT_OBJECT_0,"Cooperative worker completes and releases COM/leases");check(provider.stats().stopped,"Stop observation reports actual worker completion");}
void latestAndIdle(){
    Window window;n::LayerImageSource images;auto probe=std::make_shared<Probe>();probe->blockNext=true;n::NativeShelfIconProvider provider(images,resolver(probe),window.route(11),extractor(probe));
    const auto original=requests(8);check(provider.setVisible(original),"Eight visible source requests accepted");check(WaitForSingleObject(probe->entered.value,10000)==WAIT_OBJECT_0,"Injected extractor entered on worker");check(probe->worker!=GetCurrentThreadId()&&probe->acquired==1&&probe->released==0,"Native metadata resolver and lease remain off UI thread");
    const auto prior=provider.stats();rejects([&]{provider.setVisible(requests(9));},"Visible queue cannot exceed eight");auto invalid=requests(8,20);invalid.back().path="relative.txt";rejects([&]{provider.setVisible(invalid);},"Late invalid request rejects whole visible batch");check(provider.stats().requests==prior.requests&&provider.stats().queued==7,"Malformed batch cannot replace active queue");
    rejects([&]{provider.setRoute({window.hwnd,WM_USER+1,11});},"Notice route cannot use a nonprivate application message");
    rejects([&]{provider.setRoute({reinterpret_cast<HWND>(static_cast<ULONG_PTR>(1)),Window::notice,11});},"Notice route cannot target an unrelated or destroyed window");
    provider.setVisible(requests(4,30));provider.hide();check(provider.stats().queued==0&&provider.stats().completed==0&&provider.stats().inFlight,"Hide clears queued/completed generations without waiting on blocked extraction");const auto latest=requests(8,50);provider.setVisible(latest);SetEvent(probe->release.value);auto results=collect(provider,window,11,8);
    check(results.size()==8&&probe->calls==9&&probe->acquired==probe->released&&probe->leasesOpen,"Only one old in-flight job plus latest eight run; every lease closes before delivery");for(const auto&r:results){check(r.image&&SUCCEEDED(r.result)&&r.image->width()==64&&r.image->height()==64,"Source64px immutable icon published on owner thread");check(std::any_of(latest.begin(),latest.end(),[&](const auto&q){return q.imageKey==r.imageKey;}),"Canceled generations never publish into icon cache");}
    check(!images.acquire(original[0].imageKey,1)&&images.stats().cachedImages==8,"Single existing image cache holds only requested latest results");check(provider.drain(10).empty(),"Stale route generation cannot drain current owner results");
    check(!provider.setVisible(latest),"Repeated visible set is a no-op");const auto idle=provider.stats();check(WaitForSingleObject(probe->never.value,80)==WAIT_TIMEOUT,"Bounded idle observation");const auto after=provider.stats();check(idle.extractions==after.extractions&&idle.wakeups==after.wakeups&&idle.notices==after.notices,"Idle worker has no polling wakeups, retries or callbacks");
    provider.hide();check(provider.setVisible(latest)&&provider.stats().queued==0,"Show reuses shared cached icons without re-extraction");provider.setRoute({});const auto notices=provider.stats().notices;provider.hide();check(provider.stats().notices==notices,"Clearing owner route/hiding posts no extra notification");provider.setRoute(window.route(12));provider.setVisible(requests(1,80));check(collect(provider,window,12,1).front().image!=nullptr,"A new owner route receives only its current generation");stopped(provider);rejects([&]{provider.setVisible(latest);},"Stopped provider cannot silently accept an unchanged set");
}
void blockedShutdown(){
    Window window;n::LayerImageSource images;auto probe=std::make_shared<Probe>();probe->blockNext=true;auto provider=std::make_unique<n::NativeShelfIconProvider>(images,resolver(probe),window.route(22),extractor(probe));provider->setVisible(requests(1,90));check(WaitForSingleObject(probe->entered.value,10000)==WAIT_OBJECT_0,"Shutdown fixture blocks only injected extractor");Handle completion(provider->duplicateWorkerHandle());const auto start=GetTickCount64();provider.reset();check(GetTickCount64()-start<1000,"Destructor invalidates owner route without waiting on hung Shell-equivalent work");
    rejects([&]{n::NativeShelfIconProvider another(images,resolver(probe),{},extractor(probe));},"Reopen cannot accumulate a second blocked worker");check(probe->released==0,"Blocked worker retains independent file lease after owner destruction");SetEvent(probe->release.value);check(WaitForSingleObject(completion.value,10000)==WAIT_OBJECT_0&&probe->acquired==probe->released,"Released block completes cleanup after HUD/provider destruction");check(images.stats().publications==0,"Destroyed owner's completion never reaches its cache pointer");MSG late{};check(!PeekMessageW(&late,window.hwnd,Window::notice,Window::notice,PM_REMOVE),"No notice posts after blocked owner route is destroyed");
    n::NativeShelfIconProvider replacement(images,resolver(probe),window.route(23),extractor(probe));stopped(replacement);
}
std::string utf8(const std::filesystem::path&p){const auto s=p.u8string();return {reinterpret_cast<const char*>(s.data()),s.size()};}
void nativeIcons(){
    Window window;const auto root=std::filesystem::temp_directory_path()/("endfield-synthetic-icons-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64()));check(std::filesystem::create_directory(root),"Create new explicit synthetic icon directory");struct Cleanup{std::filesystem::path path;~Cleanup(){std::error_code e;std::filesystem::remove_all(path,e);}}cleanup{root};
    const auto file=root/"owned.txt",folder=root/"owned-folder";{std::ofstream stream(file);stream<<"Synthetic icon test; not an image or thumbnail.";check(bool(stream),"Write owned synthetic text fixture");}check(std::filesystem::create_directory(folder),"Create owned empty folder");
    n::NativeFileShelfFiles files;std::vector<n::ShelfIconRequest>visible;for(const auto&path:{file,folder}){auto r=request(static_cast<unsigned>(visible.size()+100));auto lease=files.acquire(utf8(path));r.path=lease.metadata().windowsPath;r.identity=lease.metadata().identity;r.directory=lease.metadata().isDirectory;visible.push_back(std::move(r));}
    auto fallback=request(110);fallback.path="Z:\\not-accessed\\missing.synthetic-no-user-file";fallback.unavailable=true;visible.push_back(fallback);
    auto resolved=std::make_shared<std::atomic<unsigned>>(0);auto platform=files.platform();n::LayerImageSource images;n::NativeShelfIconProvider provider(images,[resolve=platform.resolve,resolved](const d::ShelfRecord&r){++*resolved;return resolve(r);},window.route(33));provider.setVisible(visible);const auto ready=collect(provider,window,33,3);check(*resolved==2,"Unavailable type icon never invokes file metadata resolution");
    for(const auto&item:ready){check(SUCCEEDED(item.result)&&item.image&&item.image->width()==64&&item.image->height()==64,"Actual Shell icon-only pipeline returns bounded64px image");const auto pixels=item.image->premultipliedBGRA();bool ink{};for(std::size_t k=0;k<pixels.size();k+=4){ink|=pixels[k+3]!=0;check(pixels[k]<=pixels[k+3]&&pixels[k+1]<=pixels[k+3]&&pixels[k+2]<=pixels[k+3],"Native icon pixels have valid premultiplied alpha");}check(ink,"Actual file/folder/type icon contains visible owned pixels");if(item.imageKey==fallback.imageKey)check(item.typeFallback,"Missing record reports type-only fallback explicitly");}
    stopped(provider);check(!IsWindowVisible(window.hwnd),"No fixture shows/focuses any application window");
}
}
int main(){try{portableRequests();latestAndIdle();blockedShutdown();nativeIcons();std::cout<<"PASS "<<checks+portableChecks<<" Shelf icon provider checks\n";return 0;}catch(const std::exception&e){std::cerr<<"FAIL after "<<checks+portableChecks<<": "<<e.what()<<'\n';return 1;}}
#else
int main(){try{portableRequests();std::cout<<"PASS "<<portableChecks<<" portable Shelf icon request checks; Windows worker/Shell checks not executed\n";return 0;}catch(const std::exception&e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
#endif
