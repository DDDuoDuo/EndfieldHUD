// Contained Shell preview (native/shelf_preview_thread.hpp): the preview
// controller on a dedicated STA thread with its own pump and a watchdog.
// Fixtures are synthetic: an owned hidden owner window, injected in-process
// handlers, and a hung COM server that is a hidden child copy of this test
// executable (--hung-server), reached through standard marshaling exactly like
// an out-of-process preview handler host. No Shell handler, user file,
// registry entry or visible window is used.
#ifdef _WIN32
#include "native/shelf_preview_thread.hpp"
#include <objbase.h>
#include <objidl.h>
#include <ocidl.h>
#include <shobjidl.h>
#include <wrl/client.h>
#include <atomic>
#include <cstdio>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace n=endfield::native;namespace d=ehud::data;
using Microsoft::WRL::ComPtr;
namespace {
unsigned checks{};void check(bool value,const char*message){++checks;if(!value)throw std::runtime_error(message);}
constexpr auto firstID="aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa",secondID="bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb",hungID="cccccccc-cccc-4ccc-8ccc-cccccccccccc";
struct COM {COM(){check(SUCCEEDED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)),"Owned fixture thread is STA");}~COM(){CoUninitialize();}};
struct Window {
    HWND value{};static constexpr UINT notice=WM_APP+252;
    Window(){value=CreateWindowExW(WS_EX_TOOLWINDOW,L"STATIC",L"Owned hidden contained preview fixture",WS_POPUP,0,0,1280,800,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);check(value&&!IsWindowVisible(value),"Synthetic owner remains hidden");}
    ~Window(){if(value)DestroyWindow(value);}
    n::ShelfPreviewRoute route(UINT_PTR generation)const{return {value,notice,generation};}
};
d::ShelfFileMetadata metadata(unsigned identity=1){d::ShelfFileMetadata value;value.windowsPath="C:\\explicit-synthetic\\owned.txt";value.name="owned.txt";value.typeDescription="File";value.identity.volumeSerial=1;value.identity.objectID[0]=static_cast<std::uint8_t>(identity);return value;}
// Leases may be released on the preview thread.
struct Leases {std::atomic<unsigned>acquired{},released{};bool balanced()const{return acquired==released;}};
d::ShelfFileAccess lease(Leases&counts,unsigned identity=1){++counts.acquired;return {metadata(identity),[&counts]{++counts.released;}};}
struct Log {
    std::atomic<unsigned>initialize{},sites{},previews{},unloads{},reads{};std::atomic<DWORD>thread{};
    unsigned slowReads{};DWORD readPause{}; // Initialize reads the stream slowly (progress)
};
// In-process handler: runs on the preview thread, created there by the service.
class Handler final:public IPreviewHandler,public IInitializeWithStream,public IObjectWithSite {
    std::atomic<ULONG>refs_{1};std::shared_ptr<Log>log_;ComPtr<IUnknown>site_;ComPtr<IStream>stream_;
public:
    explicit Handler(std::shared_ptr<Log>log):log_(std::move(log)){}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void**out)override{if(!out)return E_POINTER;*out=nullptr;if(iid==IID_IUnknown||iid==IID_IPreviewHandler)*out=static_cast<IPreviewHandler*>(this);else if(iid==__uuidof(IInitializeWithStream))*out=static_cast<IInitializeWithStream*>(this);else if(iid==IID_IObjectWithSite)*out=static_cast<IObjectWithSite*>(this);else return E_NOINTERFACE;AddRef();return S_OK;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs_;}ULONG STDMETHODCALLTYPE Release()override{const auto count=--refs_;if(!count)delete this;return count;}
    HRESULT STDMETHODCALLTYPE SetSite(IUnknown*site)override{++log_->sites;site_=site;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetSite(REFIID iid,void**out)override{return site_?site_->QueryInterface(iid,out):E_FAIL;}
    HRESULT STDMETHODCALLTYPE Initialize(IStream*source,DWORD)override{
        ++log_->initialize;stream_=source;
        for(unsigned n=0;n<log_->slowReads;++n){Sleep(log_->readPause);char byte{};ULONG read{};const auto hr=stream_->Read(&byte,1,&read);if(FAILED(hr))return hr;++log_->reads;}
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetWindow(HWND,const RECT*rect)override{return rect?S_OK:E_POINTER;}
    HRESULT STDMETHODCALLTYPE SetRect(const RECT*rect)override{return rect?S_OK:E_POINTER;}
    HRESULT STDMETHODCALLTYPE DoPreview()override{++log_->previews;log_->thread=GetCurrentThreadId();return S_OK;}
    HRESULT STDMETHODCALLTYPE Unload()override{++log_->unloads;stream_.Reset();return S_OK;}
    HRESULT STDMETHODCALLTYPE SetFocus()override{return S_OK;}
    HRESULT STDMETHODCALLTYPE QueryFocus(HWND*out)override{if(!out)return E_POINTER;*out=nullptr;return S_FALSE;}
    HRESULT STDMETHODCALLTYPE TranslateAccelerator(MSG*)override{return S_FALSE;}
};
// The owner thread's view: route notices are delivered like the HUD does.
struct Owner {
    Window window;std::vector<n::ShelfPreviewEvent>events;
    void deliver(n::ContainedShelfFilePreview&preview,UINT_PTR generation){
        MSG message{};while(PeekMessageW(&message,window.value,Window::notice,Window::notice,PM_REMOVE)){
            check(message.wParam==generation,"Notices carry only the route generation");check(preview.handleMessage(message.wParam,message.lParam),"The owner handles the preview notice");
            for(auto&value:preview.drain(message.wParam))events.push_back(std::move(value));}
    }
    // Waits on this thread's queue (the HUD's own pump), never on the preview thread.
    bool wait(n::ContainedShelfFilePreview&preview,UINT_PTR generation,std::size_t count,DWORD milliseconds){
        const auto deadline=GetTickCount64()+milliseconds;
        for(;;){deliver(preview,generation);if(events.size()>=count)return true;const auto now=GetTickCount64();if(now>=deadline)return false;
            MsgWaitForMultipleObjects(0,nullptr,FALSE,static_cast<DWORD>(std::min<ULONGLONG>(50,deadline-now)),QS_POSTMESSAGE);}
    }
    std::vector<n::ShelfPreviewEvent>take(){return std::exchange(events,{});}
};
struct Fixture {
    Owner owner;Leases leases;std::shared_ptr<Log>log=std::make_shared<Log>();std::atomic<unsigned>factories{},resolutions{};
    std::function<HRESULT(IPreviewHandler**)>factory; // null: a new in-process handler
    std::function<void()>onResolve;
    n::ShelfPreviewServices services(){
        n::ShelfPreviewServices value;
        value.resolve=[this](const d::ShelfRecord&record){++resolutions;if(onResolve)onResolve();return lease(leases,record.identity.objectID[0]);};
        value.createHandler=[this](const d::ShelfFileMetadata&,IPreviewHandler**out){++factories;*out=nullptr;if(factory)return factory(out);*out=new Handler(log);return S_OK;};
        value.openStream=[](const d::ShelfFileAccess&,IStream**out){*out=nullptr;ComPtr<IStream>stream;const auto hr=CreateStreamOnHGlobal(nullptr,TRUE,&stream);if(FAILED(hr))return hr;
            const char bytes[]="synthetic preview bytes";ULONG written{};stream->Write(bytes,sizeof(bytes),&written);LARGE_INTEGER zero{};stream->Seek(zero,STREAM_SEEK_SET,nullptr);*out=stream.Detach();return S_OK;};
        return value;
    }
    std::unique_ptr<n::ContainedShelfFilePreview>make(UINT_PTR generation,unsigned timeout=10000,unsigned blocked=2,bool fallback=false){
        return std::make_unique<n::ContainedShelfFilePreview>(owner.window.route(generation),n::ShelfPreviewOptions{false,600,400,fallback},services(),n::ShelfPreviewContainment{timeout,blocked});
    }
};
// Duration of a call on the owner (HUD) thread.
template<class F>ULONGLONG timed(F&&body){const auto start=GetTickCount64();body();return GetTickCount64()-start;}

void lifecycle(){
    Fixture fixture;auto preview=fixture.make(11);
    check(preview->containment().threads==0&&preview->containment().thread==0,"No preview thread exists before the first preview");
    check(preview->request(firstID,lease(fixture.leases)),"A validated request queues");
    check(preview->stats().queued,"A queued preview is visible to the owner at once (focus is preserved)");
    check(fixture.owner.wait(*preview,11,1,10000),"The preview thread reports the opened preview");
    auto events=fixture.owner.take();check(events.size()==1&&events[0].kind==n::ShelfPreviewEventKind::opened&&events[0].itemID==firstID&&SUCCEEDED(events[0].result),"Exactly one opened event names the item");
    const auto contained=preview->containment();const auto popup=preview->window();
    check(contained.threads==1&&contained.thread!=0&&contained.thread!=GetCurrentThreadId(),"One dedicated preview thread was started");
    check(fixture.log->thread==contained.thread&&fixture.log->previews==1,"The handler runs on the preview thread, never on the owner thread");
    check(popup&&IsWindow(popup)&&GetWindowThreadProcessId(popup,nullptr)==contained.thread&&!IsWindowVisible(popup),"The hidden popup belongs to the preview thread");
    check(GetWindow(popup,GW_OWNER)==nullptr&&GetParent(popup)==nullptr,"The popup has no owner or parent on the owner thread (no attached input queues)");
    check(preview->stats().active&&!preview->stats().queued&&!preview->preTranslate(MSG{popup,WM_KEYDOWN,VK_ESCAPE,0,0,{}}),"The owner never translates the preview thread's keys");
    check(fixture.leases.acquired==2&&fixture.leases.released==0,"Original and current leases are held while shown");
    // Same item again: toggles closed (source Quick Look).
    check(preview->request(firstID,lease(fixture.leases))&&fixture.owner.wait(*preview,11,1,10000),"A repeated request is answered");
    events=fixture.owner.take();check(events.size()==1&&events[0].kind==n::ShelfPreviewEventKind::closed&&!preview->stats().active,"Requesting the shown item toggles it closed");
    check(preview->settle(10000)&&fixture.log->unloads==1&&fixture.leases.balanced(),"Closing unloads the handler and balances every lease");
    // Another item retires the first; the popup's own Escape closes it.
    check(preview->request(firstID,lease(fixture.leases))&&fixture.owner.wait(*preview,11,1,10000),"First item opens again");fixture.owner.take();
    check(preview->request(secondID,lease(fixture.leases,2))&&fixture.owner.wait(*preview,11,2,10000),"Switching items is answered");
    events=fixture.owner.take();check(events.size()==2&&events[0].kind==n::ShelfPreviewEventKind::closed&&events[0].itemID==firstID&&events[1].kind==n::ShelfPreviewEventKind::opened&&events[1].itemID==secondID,
        "Switching retires the previous preview and opens the new item");
    const auto second=preview->window();check(second&&PostMessageW(second,WM_KEYDOWN,VK_ESCAPE,0)!=FALSE&&fixture.owner.wait(*preview,11,1,10000),"Escape reaches the popup on the preview thread");
    events=fixture.owner.take();check(events.size()==1&&events[0].kind==n::ShelfPreviewEventKind::closed&&events[0].itemID==secondID,"The preview thread's own pump translates Escape and closes");
    check(preview->containment().threads==1&&preview->containment().timeouts==0,"Every preview reused the one healthy preview thread");
    // Invalid references are refused on the owner thread, as before.
    bool rejected{};try{preview->request("not-an-id",lease(fixture.leases));}catch(const std::invalid_argument&){rejected=true;}check(rejected,"Invalid references are rejected synchronously");
    preview->close(false);check(preview->settle(10000),"The preview thread becomes idle");
    const auto ended=timed([&]{preview.reset();});check(ended<3000&&!IsWindow(second)&&fixture.leases.balanced(),"Teardown ends the preview thread promptly and balances every lease");
}
// A handler that keeps reading the stream is making progress, not hung.
void progress(){
    Fixture fixture;fixture.log->slowReads=6;fixture.log->readPause=100;auto preview=fixture.make(12,300);
    check(preview->request(firstID,lease(fixture.leases))&&fixture.owner.wait(*preview,12,1,10000),"A slow reader is answered");
    const auto events=fixture.owner.take();
    check(events.size()==1&&events[0].kind==n::ShelfPreviewEventKind::opened&&fixture.log->reads==6&&preview->containment().timeouts==0,
        "Initialize took twice the timeout but each stream read restarted the watchdog");
    preview.reset();check(fixture.leases.balanced(),"Leases balance");
}
// A hang outside COM (here: reference resolution on an unresponsive share)
// cannot be cancelled: the thread is retired and left, the cap refuses more.
void blockedOutsideCom(){
    Fixture fixture;HANDLE release=CreateEventW(nullptr,TRUE,FALSE,nullptr);check(release!=nullptr,"Owned release event");
    std::atomic<bool>block{true};fixture.onResolve=[&]{if(block)WaitForSingleObject(release,INFINITE);};
    auto preview=fixture.make(13,250,1);
    const auto requested=timed([&]{check(preview->request(firstID,lease(fixture.leases)),"The request queues");});
    check(requested<1000,"Queueing never waits for the preview thread");
    const auto waited=timed([&]{check(fixture.owner.wait(*preview,13,1,10000),"The watchdog reports the hang");});
    auto events=fixture.owner.take();
    check(events.size()==1&&events[0].kind==n::ShelfPreviewEventKind::failed&&events[0].result==HRESULT_FROM_WIN32(ERROR_TIMEOUT)&&events[0].itemID==firstID,"The hung preview fails with ERROR_TIMEOUT");
    check(waited>=150&&waited<5000,"The report arrives after the timeout, not before and not much later");
    auto contained=preview->containment();check(contained.timeouts==1&&contained.blocked==1&&contained.thread==0&&!preview->stats().active&&!preview->stats().queued,"The blocked thread is retired; nothing is shown or queued");
    // The cap: a new preview is refused while the blocked thread remains.
    block=false;check(preview->request(secondID,lease(fixture.leases,2))&&fixture.owner.wait(*preview,13,1,5000),"A refused request is answered");
    events=fixture.owner.take();check(events.size()==1&&events[0].kind==n::ShelfPreviewEventKind::failed&&events[0].result==HRESULT_FROM_WIN32(ERROR_BUSY)&&preview->containment().refusals==1,
        "While a blocked thread remains, previews are refused instead of starting more threads");
    SetEvent(release);check(preview->settleRetired(10000)&&preview->containment().blocked==0,"Once unblocked, the retired thread abandons its handler and ends");
    check(preview->request(secondID,lease(fixture.leases,2))&&fixture.owner.wait(*preview,13,1,10000),"Previews work again");
    events=fixture.owner.take();check(events.size()==1&&events[0].kind==n::ShelfPreviewEventKind::opened&&preview->containment().threads==2,"A fresh preview thread serves the next preview");
    preview.reset();CloseHandle(release);check(fixture.leases.balanced(),"Every lease balances, including the retired thread's");
}

// ---- Out-of-process hung COM server (a hidden child of this executable) ----
// IStream::Read blocks its STA forever, like a hung handler host's DoPreview:
// a regular method call through a registered standard proxy.
class Blocker final:public IStream {
    std::atomic<ULONG>refs_{1};
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void**out)override{
        if(!out)return E_POINTER;*out=nullptr;if(iid!=IID_IUnknown&&iid!=IID_ISequentialStream&&iid!=IID_IStream)return E_NOINTERFACE;*out=static_cast<IStream*>(this);AddRef();return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs_;}ULONG STDMETHODCALLTYPE Release()override{const auto count=--refs_;if(!count)delete this;return count;}
    HRESULT STDMETHODCALLTYPE Read(void*,ULONG,ULONG*)override{Sleep(INFINITE);return E_FAIL;}
    HRESULT STDMETHODCALLTYPE Write(const void*,ULONG,ULONG*)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE Seek(LARGE_INTEGER,DWORD,ULARGE_INTEGER*)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE SetSize(ULARGE_INTEGER)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE CopyTo(IStream*,ULARGE_INTEGER,ULARGE_INTEGER*,ULARGE_INTEGER*)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE Commit(DWORD)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE Revert()override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE LockRegion(ULARGE_INTEGER,ULARGE_INTEGER,DWORD)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE UnlockRegion(ULARGE_INTEGER,ULARGE_INTEGER,DWORD)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE Stat(STATSTG*,DWORD)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE Clone(IStream**)override{return E_NOTIMPL;}
};
int hungServer(){
    if(FAILED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)))return 2;
    ComPtr<IStream>blocker;blocker.Attach(new Blocker);ComPtr<IStream>stream;
    if(FAILED(CreateStreamOnHGlobal(nullptr,TRUE,&stream))||FAILED(CoMarshalInterface(stream.Get(),IID_IStream,blocker.Get(),MSHCTX_LOCAL,nullptr,MSHLFLAGS_TABLESTRONG)))return 3;
    HGLOBAL memory{};if(FAILED(GetHGlobalFromStream(stream.Get(),&memory)))return 4;STATSTG stat{};stream->Stat(&stat,STATFLAG_NONAME);
    const auto*bytes=static_cast<const unsigned char*>(GlobalLock(memory));std::string hex;constexpr char digits[]="0123456789abcdef";
    for(ULONGLONG n=0;n<stat.cbSize.QuadPart;++n){hex.push_back(digits[bytes[n]>>4]);hex.push_back(digits[bytes[n]&15]);}GlobalUnlock(memory);
    std::fwrite(hex.data(),1,hex.size(),stdout);std::fputc('\n',stdout);std::fflush(stdout);
    MSG message{};while(GetMessageW(&message,nullptr,0,0)>0)DispatchMessageW(&message); // the parent terminates this process
    return 0;
}
struct HungServer {
    PROCESS_INFORMATION process{};HANDLE job{};std::vector<std::uint8_t>reference;
    HungServer(){
        // The child dies with this process (kill-on-close job), even when a
        // check fails or the test is stopped, and inherits only its pipe.
        job=CreateJobObjectW(nullptr,nullptr);check(job!=nullptr,"Owned job object");
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        check(SetInformationJobObject(job,JobObjectExtendedLimitInformation,&limits,sizeof(limits))!=FALSE,"Kill-on-close job");
        std::wstring path(32768,L'\0');path.resize(GetModuleFileNameW(nullptr,path.data(),static_cast<DWORD>(path.size())));
        SECURITY_ATTRIBUTES inherit{sizeof(inherit),nullptr,TRUE};HANDLE read{},write{};check(CreatePipe(&read,&write,&inherit,0)!=FALSE,"Owned pipe");SetHandleInformation(read,HANDLE_FLAG_INHERIT,0);
        SIZE_T bytes{};InitializeProcThreadAttributeList(nullptr,1,0,&bytes);std::vector<std::uint8_t>storage(bytes);auto*list=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
        check(InitializeProcThreadAttributeList(list,1,0,&bytes)!=FALSE&&UpdateProcThreadAttribute(list,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,&write,sizeof(write),nullptr,nullptr)!=FALSE,"Only the pipe is inherited");
        STARTUPINFOEXW startup{};startup.StartupInfo.cb=sizeof(startup);startup.StartupInfo.dwFlags=STARTF_USESTDHANDLES;startup.StartupInfo.hStdOutput=write;startup.StartupInfo.hStdError=write;startup.lpAttributeList=list;
        std::wstring command=L"\""+path+L"\" --hung-server";
        const bool created=CreateProcessW(path.c_str(),command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW|CREATE_SUSPENDED|EXTENDED_STARTUPINFO_PRESENT,nullptr,nullptr,&startup.StartupInfo,&process)!=FALSE;
        DeleteProcThreadAttributeList(list);CloseHandle(write);
        if(!created){CloseHandle(read);check(false,"The hidden hung-server child starts");}
        if(!AssignProcessToJobObject(job,process.hProcess)){TerminateProcess(process.hProcess,1);CloseHandle(read);check(false,"The child joins the kill-on-close job");}
        ResumeThread(process.hThread);
        std::string line;char c{};DWORD got{};while(line.size()<65536&&ReadFile(read,&c,1,&got,nullptr)&&got==1&&c!='\n')if(c!='\r')line.push_back(c);CloseHandle(read);
        check(!line.empty()&&line.size()%2==0&&line.find_first_not_of("0123456789abcdef")==std::string::npos,"The child publishes its marshaled object reference");
        const auto value=[](char h){return h<='9'?h-'0':h-'a'+10;};for(std::size_t n=0;n<line.size();n+=2)reference.push_back(static_cast<std::uint8_t>(value(line[n])<<4|value(line[n+1])));
    }
    ~HungServer(){if(process.hProcess){TerminateProcess(process.hProcess,0);WaitForSingleObject(process.hProcess,10000);CloseHandle(process.hThread);CloseHandle(process.hProcess);}if(job)CloseHandle(job);}
    // On the preview thread: a proxy to the hung object, then the blocking
    // call. The proxy is leaked like an abandoned handler's (its release could
    // wait on the hung server).
    HRESULT hang(IPreviewHandler**out)const{
        *out=nullptr;ComPtr<IStream>stream;auto hr=CreateStreamOnHGlobal(nullptr,TRUE,&stream);if(FAILED(hr))return hr;ULONG written{};
        stream->Write(reference.data(),static_cast<ULONG>(reference.size()),&written);LARGE_INTEGER zero{};stream->Seek(zero,STREAM_SEEK_SET,nullptr);
        IStream*proxy{};hr=CoUnmarshalInterface(stream.Get(),IID_IStream,reinterpret_cast<void**>(&proxy));if(FAILED(hr))return hr;
        char byte{};ULONG read{};return proxy->Read(&byte,1,&read);
    }
};
void hungHandlerHost(){
    HungServer server;
    {
        // The watchdog cancels the hung call; the thread is retired and ends
        // even though the server is still hung.
        Fixture fixture;fixture.factory=[&](IPreviewHandler**out){return server.hang(out);};auto preview=fixture.make(14,300);
        const auto requested=timed([&]{check(preview->request(hungID,lease(fixture.leases)),"The request queues");});check(requested<1000,"Queueing never waits for the hung server");
        check(fixture.owner.wait(*preview,14,1,10000),"The hang is reported");auto events=fixture.owner.take();
        check(events.size()==1&&events[0].kind==n::ShelfPreviewEventKind::failed&&events[0].result==HRESULT_FROM_WIN32(ERROR_TIMEOUT)&&events[0].itemID==hungID,"The hung handler host fails with ERROR_TIMEOUT");
        auto contained=preview->containment();
        if(contained.timeouts!=1||contained.cancellations<1)std::cerr<<"timeouts "<<contained.timeouts<<" cancellations "<<contained.cancellations<<" CoCancelCall 0x"<<std::hex<<static_cast<unsigned long>(contained.lastCancel)<<std::dec<<'\n';
        check(contained.timeouts==1&&contained.cancellations>=1,"The watchdog cancelled the blocked COM call");
        const bool ended=preview->settleRetired(5000);if(!ended)std::cerr<<"retired thread stage "<<preview->containment().retiredStage<<'\n';
        check(ended&&preview->containment().retiredStage==5,"The cancelled call returned and the retired thread ended while the server is still hung");
        check(fixture.leases.balanced(),"The abandoned preview released both leases");
        // The next preview runs on a fresh thread with a healthy handler.
        fixture.factory={};check(preview->request(firstID,lease(fixture.leases))&&fixture.owner.wait(*preview,14,1,10000),"The next preview is answered");
        events=fixture.owner.take();check(events.size()==1&&events[0].kind==n::ShelfPreviewEventKind::opened&&preview->containment().threads==2,"A fresh preview thread opens the next item");
        preview.reset();check(fixture.leases.balanced(),"Leases balance");
    }
    {
        // Closing during the hang cancels the call at once (no timeout).
        Fixture fixture;fixture.factory=[&](IPreviewHandler**out){return server.hang(out);};auto preview=fixture.make(15,60000);
        check(preview->request(hungID,lease(fixture.leases)),"The request queues");
        const auto deadline=GetTickCount64()+10000;while(!preview->containment().busy&&GetTickCount64()<deadline)MsgWaitForMultipleObjects(0,nullptr,FALSE,20,QS_POSTMESSAGE);
        check(preview->containment().busy,"The preview thread is inside the hung call");
        const auto closed=timed([&]{preview->close(true);});check(closed<1000,"Closing never waits for the hung server");
        check(preview->settle(10000),"The cancelled call returned and the preview thread is idle again");
        auto contained=preview->containment();check(contained.timeouts==0&&contained.cancellations>=1&&contained.threads==1&&contained.thread!=0,"Closing cancelled the call; the thread stays healthy");
        check(fixture.owner.wait(*preview,15,1,10000),"The cancelled preview reports");auto closing=fixture.owner.take();
        check(closing.size()==1&&closing[0].kind==n::ShelfPreviewEventKind::closed&&closing[0].itemID==hungID,"The cancelled preview is reported closed, not failed");
        fixture.factory={};check(preview->request(firstID,lease(fixture.leases))&&fixture.owner.wait(*preview,15,1,10000),"The same thread serves the next preview");
        auto events=fixture.owner.take();check(!events.empty()&&events.back().kind==n::ShelfPreviewEventKind::opened&&preview->containment().threads==1,"The next item opens on the same preview thread");
        // Destroying the facade while a call hangs cancels it and ends promptly.
        fixture.factory=[&](IPreviewHandler**out){return server.hang(out);};check(preview->request(hungID,lease(fixture.leases)),"Another hung request queues");
        const auto until=GetTickCount64()+10000;while(!preview->containment().busy&&GetTickCount64()<until)MsgWaitForMultipleObjects(0,nullptr,FALSE,20,QS_POSTMESSAGE);
        const auto ended=timed([&]{preview.reset();});check(ended<4000,"Teardown cancels the hung call and never waits longer than its bound");
        check(fixture.leases.balanced(),"Leases balance");
    }
}
}

int wmain(int argc,wchar_t**argv){
    if(argc==2&&std::wstring_view(argv[1])==L"--hung-server")return hungServer();
    try{COM com;lifecycle();progress();blockedOutsideCom();hungHandlerHost();std::cout<<"PASS "<<checks<<" contained Shelf preview checks\n";return 0;}
    catch(const std::exception&error){std::cerr<<"FAIL after "<<checks<<": "<<error.what()<<'\n';return 1;}
}
#else
#include <iostream>
int main(){std::cout<<"SKIP contained Shelf preview: requires Windows\n";return 0;}
#endif
