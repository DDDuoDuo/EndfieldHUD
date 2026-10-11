#include "native/shelf_preview_thread.hpp"
#ifdef _WIN32
#include <objbase.h>
#include <process.h>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <utility>

namespace endfield::native {
namespace {
using namespace ehud::data;
// Private messages of the preview thread's message-only window.
constexpr UINT engineMessage=WM_APP+1,commandMessage=WM_APP+2;
constexpr UINT_PTR engineGeneration=1;
constexpr wchar_t threadWindowClass[]=L"EndfieldShelfPreviewThread.v1";
constexpr DWORD startupMilliseconds=5000,teardownMilliseconds=3000;
constexpr std::size_t maximumEvents=8;
void need(bool value,const char*message){if(!value)throw std::invalid_argument(message);}
void validateRoute(const ShelfPreviewRoute&value){
    DWORD process{};const auto thread=value.owner?GetWindowThreadProcessId(value.owner,&process):0;
    need(value.owner&&IsWindow(value.owner)&&thread==GetCurrentThreadId()&&process==GetCurrentProcessId()&&value.message>=WM_APP&&value.message<=0xbfff&&value.generation,
        "Contained preview requires a live owner-thread HWND/private generation route");
}
ShelfPreviewStats& operator+=(ShelfPreviewStats&a,const ShelfPreviewStats&b){
    a.requests+=b.requests;a.opens+=b.opens;a.closes+=b.closes;a.failures+=b.failures;a.notices+=b.notices;a.fallbacks+=b.fallbacks;return a;
}
}

struct ContainedShelfFilePreview::Worker {
    Shared*shared{};DWORD id{};std::atomic<HWND>wake{};
    std::atomic<bool>retired{},stopping{};std::atomic<int>stage{};
    // Preview-thread only.
    std::unique_ptr<NativeShelfFilePreview>engine;unsigned depth{};bool inHook{};
};
struct ContainedShelfFilePreview::Shared {
    struct Request {std::string id;ShelfFileAccess access;};
    std::mutex mutex;
    ShelfPreviewRoute ui;ShelfPreviewOptions options;ShelfPreviewServices services;ShelfPreviewContainment limits;DWORD uiThread{GetCurrentThreadId()};
    bool alive{true};
    // HUD thread -> preview thread.
    std::optional<Request>open;std::optional<bool>close;std::uint64_t sent{},taken{};
    // Preview thread -> HUD thread.
    std::vector<ShelfPreviewEvent>events;bool eventsPosted{};
    ShelfPreviewStats current,past;std::uint64_t requests{};HWND popup{};
    std::string subject; // the item being opened or shown
    bool opening{};      // an open was taken and its outcome is not yet published
    // Threads and the watchdog.
    std::shared_ptr<Worker>worker,lastRetired;HANDLE thread{};std::vector<HANDLE>retired;
    PTP_TIMER timer{};bool busy{};ULONGLONG deadline{};
    HANDLE idle{}; // manual reset: set while every command was taken and the preview thread is idle
    ShelfPreviewContainmentStats counts;
    ~Shared(){
        if(timer){SetThreadpoolTimer(timer,nullptr,0,0);WaitForThreadpoolTimerCallbacks(timer,TRUE);CloseThreadpoolTimer(timer);}
        if(thread)CloseHandle(thread);for(const auto handle:retired)CloseHandle(handle);if(idle)CloseHandle(idle);
    }
    // Under the mutex.
    bool post(LPARAM notice)noexcept{return alive&&ui.owner&&PostMessageW(ui.owner,ui.message,ui.generation,notice)!=FALSE;}
    void event(ShelfPreviewEventKind kind,HRESULT result,std::string id,bool fallback=false)noexcept{
        if(!alive)return;
        try{if(events.size()==maximumEvents)events.erase(events.begin());events.push_back({kind,result,std::move(id),fallback});}catch(...){return;}
        if(!eventsPosted)eventsPosted=post(static_cast<LPARAM>(ShelfPreviewNotice::events));
    }
    void prune()noexcept; // HUD thread only: closes ended retired threads
    // Retires the current preview thread (watchdog or teardown); returns its id.
    DWORD retire()noexcept{
        const auto value=std::move(worker);if(!value)return 0;value->retired=true;busy=false;lastRetired=value;
        if(timer)SetThreadpoolTimer(timer,nullptr,0,0);
        if(thread){try{retired.push_back(thread);}catch(...){CloseHandle(thread);}thread=nullptr;}
        past+=current;current={};popup=nullptr;opening=false;counts.thread=0;counts.busy=false;return value->id;
    }
};

namespace {
using Shared=ContainedShelfFilePreview::Shared;using Worker=ContainedShelfFilePreview::Worker;
// The watchdog: one-shot, armed only while a watched operation runs.
void CALLBACK expired(PTP_CALLBACK_INSTANCE,void*context,PTP_TIMER)noexcept{
    auto&shared=*static_cast<Shared*>(context);DWORD target{};std::string subject;
    {
        std::lock_guard lock(shared.mutex);
        // A re-armed or finished operation leaves nothing to do here.
        if(!shared.worker||!shared.busy||GetTickCount64()+32<shared.deadline)return;
        ++shared.counts.timeouts;subject=std::exchange(shared.subject,{});target=shared.retire();
    }
    // Makes the blocked call return (RPC_E_CALL_CANCELED); the retired thread
    // then abandons its handler and ends. A hang outside COM stays blocked.
    const auto initialized=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    const auto result=CoCancelCall(target,0);
    if(SUCCEEDED(initialized))CoUninitialize();
    // The owner hears of the timeout once the cancellation was attempted.
    std::lock_guard lock(shared.mutex);shared.counts.lastCancel=result;if(SUCCEEDED(result))++shared.counts.cancellations;
    if(!subject.empty())shared.event(ShelfPreviewEventKind::failed,HRESULT_FROM_WIN32(ERROR_TIMEOUT),std::move(subject));
    // Nothing is left to wait for here: the owner's next notice delivers any
    // request the retired thread never took to a fresh preview thread.
    if(shared.idle)SetEvent(shared.idle);
}
// Under the mutex: (re)starts the watchdog for the current operation.
void arm(Shared&shared,const Worker&worker)noexcept{
    if(shared.worker.get()!=&worker||worker.retired)return;
    shared.busy=shared.counts.busy=true;shared.deadline=GetTickCount64()+shared.limits.timeoutMilliseconds;
    ULARGE_INTEGER due{};due.QuadPart=static_cast<ULONGLONG>(-static_cast<LONGLONG>(shared.limits.timeoutMilliseconds)*10000);
    FILETIME time{due.LowPart,due.HighPart};SetThreadpoolTimer(shared.timer,&time,0,0);
}
void cancelRunning(Shared&shared)noexcept{
    DWORD target{};{std::lock_guard lock(shared.mutex);if(shared.worker&&shared.busy)target=shared.worker->id;}
    if(!target)return;const auto result=CoCancelCall(target,0);
    std::lock_guard lock(shared.mutex);shared.counts.lastCancel=result;if(SUCCEEDED(result))++shared.counts.cancellations;
}
// Preview thread, after every dispatched message: publishes the controller's
// state, then forwards its events (also those of popup keys and closes) in the
// same critical section, so the owner never sees an event before its state;
// finally the idle signal.
void publish(Shared&shared,Worker&worker)noexcept{
    if(!worker.engine)return;
    std::vector<ShelfPreviewEvent>events;try{events=worker.engine->drain(engineGeneration);}catch(...){}
    const auto stats=worker.engine->stats();const auto popup=worker.engine->window();
    std::lock_guard lock(shared.mutex);if(shared.worker.get()!=&worker)return;
    shared.current=stats;shared.popup=popup;if(!stats.queued)shared.opening=false;
    for(auto&value:events){
        if(value.kind==ShelfPreviewEventKind::opened)shared.subject=value.itemID;
        else if((value.kind==ShelfPreviewEventKind::closed||value.kind==ShelfPreviewEventKind::failed)&&shared.subject==value.itemID)shared.subject.clear();
        shared.event(value.kind,value.result,std::move(value.itemID),value.fallback);
    }
    const bool idle=shared.taken==shared.sent&&!stats.queued&&!worker.depth;
    if(idle)SetEvent(shared.idle);else ResetEvent(shared.idle);
}
// After the outermost watched operation returned: a retired thread abandons
// its handler; a queued close or a newer request ends the current preview now.
void afterOperation(Shared&shared,Worker&worker)noexcept{
    if(worker.inHook||!worker.engine)return;worker.inHook=true;
    if(worker.retired){worker.stage=1;worker.engine->abandon();worker.stage=2;}
    else{
        std::optional<bool>close;
        {std::lock_guard lock(shared.mutex);if(shared.close)close=*shared.close;else if(shared.open&&shared.open->id!=shared.subject)close=false;}
        if(close)worker.engine->close(*close);
    }
    worker.inHook=false;
}
void watch(Shared&shared,Worker&worker,bool begin)noexcept{
    if(begin){if(worker.depth++==0){std::lock_guard lock(shared.mutex);arm(shared,worker);}return;}
    if(!worker.depth||--worker.depth)return;
    {std::lock_guard lock(shared.mutex);if(shared.worker.get()==&worker){shared.busy=shared.counts.busy=false;SetThreadpoolTimer(shared.timer,nullptr,0,0);}}
    afterOperation(shared,worker);
}
void activity(Shared&shared,Worker&worker)noexcept{if(!worker.depth)return;std::lock_guard lock(shared.mutex);if(shared.busy)arm(shared,worker);}
void commands(Shared&shared,Worker&worker){
    std::optional<Shared::Request>open;std::optional<bool>close;
    {std::lock_guard lock(shared.mutex);open=std::move(shared.open);shared.open.reset();close=shared.close;shared.close.reset();shared.taken=shared.sent;if(open){shared.subject=open->id;shared.opening=true;}}
    if(close)worker.engine->close(*close);
    if(!open)return;
    std::string id=open->id;bool queued{};
    try{queued=worker.engine->request(std::move(open->id),std::move(open->access));}catch(...){queued=false;}
    if(!queued){std::lock_guard lock(shared.mutex);if(shared.subject==id)shared.subject.clear();shared.event(ShelfPreviewEventKind::failed,E_INVALIDARG,std::move(id));}
}
LRESULT CALLBACK threadWindow(HWND hwnd,UINT message,WPARAM wParam,LPARAM lParam)noexcept{
    if(message==WM_NCCREATE)SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(reinterpret_cast<const CREATESTRUCTW*>(lParam)->lpCreateParams));
    auto*worker=reinterpret_cast<Worker*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    if(message==WM_NCDESTROY)SetWindowLongPtrW(hwnd,GWLP_USERDATA,0);
    if(worker&&worker->engine)try{
        if(message==engineMessage){worker->engine->handleMessage(wParam,lParam);return 0;} // events are published after the dispatch
        if(message==commandMessage){commands(*worker->shared,*worker);return 0;}
    }catch(...){return 0;}
    if(message==WM_CLOSE&&worker){PostQuitMessage(0);return 0;}
    return DefWindowProcW(hwnd,message,wParam,lParam);
}
struct Start {std::shared_ptr<Shared>shared;std::shared_ptr<Worker>worker;HANDLE ready{};bool started{};};
// A retired thread's apartment teardown: cancels its pending call to the hung
// server (context: the thread id). Armed only around that CoUninitialize.
constexpr DWORD teardownCancelMilliseconds=200;
void CALLBACK cancelTeardown(PTP_CALLBACK_INSTANCE,void*context,PTP_TIMER)noexcept{
    const auto initialized=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    (void)CoCancelCall(static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(context)),0);
    if(SUCCEEDED(initialized))CoUninitialize();
}
unsigned __stdcall run(void*argument)noexcept{
    auto&start=*static_cast<Start*>(argument);const auto shared=start.shared;const auto worker=start.worker;
    // Cancellation of this thread's synchronous outgoing calls (CoCancelCall).
    const auto initialized=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED|COINIT_DISABLE_OLE1DDE);
    if(SUCCEEDED(initialized))(void)CoEnableCallCancellation(nullptr);
    worker->id=GetCurrentThreadId();
    try{
        need(SUCCEEDED(initialized),"Preview thread COM");
        WNDCLASSEXW type{sizeof(type)};type.lpfnWndProc=threadWindow;type.hInstance=GetModuleHandleW(nullptr);type.lpszClassName=threadWindowClass;
        need(RegisterClassExW(&type)!=0||GetLastError()==ERROR_CLASS_ALREADY_EXISTS,"Preview thread window class");
        worker->wake=CreateWindowExW(0,threadWindowClass,L"",0,0,0,0,0,HWND_MESSAGE,nullptr,type.hInstance,worker.get());need(worker->wake.load()!=nullptr,"Preview thread window");
        ShelfPreviewOptions options;ShelfPreviewServices services;{std::lock_guard lock(shared->mutex);options=shared->options;services=shared->services;}
        options.anchor=shared->ui.owner;
        services.watch=[s=shared.get(),w=worker.get()](bool begin){watch(*s,*w,begin);};
        services.activity=[s=shared.get(),w=worker.get()]{activity(*s,*w);};
        worker->engine=std::make_unique<NativeShelfFilePreview>(ShelfPreviewRoute{worker->wake,engineMessage,engineGeneration},options,std::move(services));
        start.started=true;
    }catch(...){start.started=false;}
    const bool started=start.started;SetEvent(start.ready); // `start` belongs to the starting thread from here on
    if(started){
        MSG message{};
        while(!worker->retired&&GetMessageW(&message,nullptr,0,0)>0){
            if(!worker->engine->preTranslate(message)){TranslateMessage(&message);DispatchMessageW(&message);}
            publish(*shared,*worker);
        }
    }
    // A retired thread's controller has abandoned its handler; teardown never
    // calls a hung handler again.
    const bool retired=worker->retired;if(retired){worker->stage=3;if(worker->engine)worker->engine->abandon();}
    worker->engine.reset();if(retired)worker->stage=4;if(const auto wake=worker->wake.exchange(nullptr))DestroyWindow(wake);
    // A retired thread's apartment still holds the abandoned handler's proxies
    // (leaked on purpose): closing the apartment releases them through the hung
    // server. Each such call is cancelled while this one CoUninitialize runs, so
    // the thread can end. Uninitializing explicitly keeps that wait out of the
    // thread-exit path (where it would hold the loader lock); if a call cannot
    // be cancelled the thread just stays blocked here (counted as blocked).
    if(SUCCEEDED(initialized)){
        PTP_TIMER canceller{};
        if(retired)if((canceller=CreateThreadpoolTimer(cancelTeardown,reinterpret_cast<void*>(static_cast<std::uintptr_t>(GetCurrentThreadId())),nullptr))!=nullptr){
            ULARGE_INTEGER due{};due.QuadPart=static_cast<ULONGLONG>(-static_cast<LONGLONG>(teardownCancelMilliseconds)*10000);FILETIME time{due.LowPart,due.HighPart};
            SetThreadpoolTimer(canceller,&time,teardownCancelMilliseconds,0);
        }
        CoUninitialize();
        if(canceller){SetThreadpoolTimer(canceller,nullptr,0,0);WaitForThreadpoolTimerCallbacks(canceller,TRUE);CloseThreadpoolTimer(canceller);}
    }
    if(retired)worker->stage=5;
    return 0;
}
}

ContainedShelfFilePreview::ContainedShelfFilePreview(ShelfPreviewRoute route,ShelfPreviewOptions options,ShelfPreviewServices services,ShelfPreviewContainment limits){
    validateRoute(route);need(options.clientWidth>=32&&options.clientWidth<=4096&&options.clientHeight>=32&&options.clientHeight<=4096,"Invalid native preview dimensions");
    need(limits.timeoutMilliseconds>=50&&limits.timeoutMilliseconds<=600000&&limits.maximumBlocked>=1&&limits.maximumBlocked<=16,"Invalid preview containment bounds");
    auto shared=std::make_shared<Shared>();shared->ui=route;shared->options=options;shared->services=std::move(services);shared->limits=limits;
    shared->idle=CreateEventW(nullptr,TRUE,TRUE,nullptr);need(shared->idle!=nullptr,"Preview idle event");
    shared->timer=CreateThreadpoolTimer(expired,shared.get(),nullptr);need(shared->timer!=nullptr,"Preview watchdog timer");
    shared->events.reserve(maximumEvents);shared_=std::move(shared);
}
ContainedShelfFilePreview::~ContainedShelfFilePreview(){
    const auto shared=std::move(shared_);if(!shared)return;std::shared_ptr<Worker>worker;HANDLE thread{};
    {std::lock_guard lock(shared->mutex);shared->alive=false;shared->events.clear();shared->open.reset();shared->close=false;worker=shared->worker;thread=shared->thread;}
    if(!worker)return;
    worker->stopping=true;PostMessageW(worker->wake,commandMessage,0,0);PostMessageW(worker->wake,WM_CLOSE,0,0);cancelRunning(*shared);
    if(WaitForSingleObject(thread,teardownMilliseconds)!=WAIT_OBJECT_0){std::lock_guard lock(shared->mutex);if(shared->worker==worker)(void)shared->retire();return;}
    std::lock_guard lock(shared->mutex);if(shared->worker==worker){shared->worker.reset();CloseHandle(std::exchange(shared->thread,nullptr));}
}
HRESULT ContainedShelfFilePreview::start(){
    auto&shared=*shared_;
    {std::lock_guard lock(shared.mutex);if(shared.worker)return S_OK;shared.prune();
        if(shared.counts.blocked>=shared.limits.maximumBlocked){++shared.counts.refusals;return HRESULT_FROM_WIN32(ERROR_BUSY);}}
    auto start=std::make_unique<Start>();start->shared=shared_;start->worker=std::make_shared<Worker>();start->worker->shared=&shared;
    start->ready=CreateEventW(nullptr,TRUE,FALSE,nullptr);if(!start->ready)return HRESULT_FROM_WIN32(GetLastError());
    const auto handle=reinterpret_cast<HANDLE>(_beginthreadex(nullptr,0,run,start.get(),0,nullptr));
    if(!handle){CloseHandle(start->ready);return E_OUTOFMEMORY;}
    // The preview thread only creates its message-only window and controller;
    // it never waits for this thread, so this bounded wait cannot deadlock.
    if(WaitForSingleObject(start->ready,startupMilliseconds)!=WAIT_OBJECT_0){
        // It still reads `start` (and its event): leave both to it.
        (void)start.release();std::lock_guard lock(shared.mutex);shared.retired.push_back(handle);return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
    }
    CloseHandle(start->ready);
    if(!start->started){WaitForSingleObject(handle,startupMilliseconds);CloseHandle(handle);return E_FAIL;}
    std::lock_guard lock(shared.mutex);shared.worker=start->worker;shared.thread=handle;++shared.counts.threads;shared.counts.thread=start->worker->id;return S_OK;
}
void ContainedShelfFilePreview::Shared::prune()noexcept{
    std::erase_if(retired,[](HANDLE handle){if(WaitForSingleObject(handle,0)!=WAIT_OBJECT_0)return false;CloseHandle(handle);return true;});
    counts.blocked=static_cast<unsigned>(retired.size());
}
bool ContainedShelfFilePreview::request(std::string id,ShelfFileAccess access){
    auto&shared=*shared_;need(shared.uiThread==GetCurrentThreadId(),"Contained preview requires its creating thread");
    need(access.open()&&(validShelfPreviewReference(id,access.metadata())||(shared.options.fallback&&validShelfPreviewFallbackReference(id,access.metadata()))),"Preview requires a supported validated reference");
    bool toggled{};
    {
        std::lock_guard lock(shared.mutex);if(!shared.alive||!shared.ui.owner)return false;++shared.requests;
        // The item being opened or shown toggles closed (source Quick Look).
        if(shared.worker&&!shared.subject.empty()&&shared.subject==id&&!shared.open&&!shared.close){
            shared.close=true;++shared.sent;ResetEvent(shared.idle);PostMessageW(shared.worker->wake,commandMessage,0,0);toggled=true;
        }
    }
    if(toggled){cancelRunning(shared);return true;}
    if(const auto started=start();FAILED(started)){std::lock_guard lock(shared.mutex);shared.event(ShelfPreviewEventKind::failed,started,std::move(id));return true;}
    bool supersede{};
    {
        std::lock_guard lock(shared.mutex);if(!shared.alive||!shared.worker)return false;
        supersede=shared.busy&&shared.subject!=id;
        shared.open=Shared::Request{std::move(id),std::move(access)};++shared.sent;ResetEvent(shared.idle);
        PostMessageW(shared.worker->wake,commandMessage,0,0);
    }
    // The user moved on: a slow operation of the previous item ends now.
    if(supersede)cancelRunning(shared);
    return true;
}
void ContainedShelfFilePreview::close(bool restore){
    auto&shared=*shared_;need(shared.uiThread==GetCurrentThreadId(),"Contained preview requires its creating thread");
    {std::lock_guard lock(shared.mutex);shared.open.reset();if(!shared.worker)return;
        shared.close=restore||shared.close.value_or(false);++shared.sent;ResetEvent(shared.idle);PostMessageW(shared.worker->wake,commandMessage,0,0);}
    cancelRunning(shared);
}
bool ContainedShelfFilePreview::handleMessage(UINT_PTR generation,LPARAM notice){
    auto&shared=*shared_;need(shared.uiThread==GetCurrentThreadId(),"Contained preview requires its creating thread");
    if(generation!=shared.ui.generation)return false;
    // A request queued while the watchdog retired its thread moves to a fresh
    // preview thread; a pending close has nothing left to close.
    bool resume{};
    {std::lock_guard lock(shared.mutex);if(!shared.worker&&shared.alive){shared.close.reset();resume=shared.open.has_value();if(!resume)shared.taken=shared.sent;}}
    if(resume){
        const auto started=start();std::lock_guard lock(shared.mutex);
        if(SUCCEEDED(started)&&shared.worker&&shared.open){ResetEvent(shared.idle);PostMessageW(shared.worker->wake,commandMessage,0,0);}
        else if(FAILED(started)&&shared.open){auto id=std::move(shared.open->id);shared.open.reset();shared.taken=shared.sent;shared.event(ShelfPreviewEventKind::failed,started,std::move(id));}
    }
    return notice==static_cast<LPARAM>(ShelfPreviewNotice::events);
}
std::vector<ShelfPreviewEvent>ContainedShelfFilePreview::drain(UINT_PTR generation){
    auto&shared=*shared_;need(shared.uiThread==GetCurrentThreadId(),"Contained preview requires its creating thread");
    std::vector<ShelfPreviewEvent>out;if(generation!=shared.ui.generation)return out;
    std::lock_guard lock(shared.mutex);out.swap(shared.events);shared.eventsPosted=false;return out;
}
ShelfPreviewStats ContainedShelfFilePreview::stats()const{
    auto&shared=*shared_;std::lock_guard lock(shared.mutex);auto result=shared.past;result+=shared.current;
    result.requests=shared.requests;result.failures+=shared.counts.timeouts+shared.counts.refusals;
    result.active=shared.current.active;result.visible=shared.current.visible;result.fallback=shared.current.fallback;
    result.queued=shared.open.has_value()||shared.close.has_value()||shared.taken!=shared.sent||shared.opening||shared.current.queued;
    return result;
}
ShelfPreviewContainmentStats ContainedShelfFilePreview::containment()const{
    auto&shared=*shared_;std::lock_guard lock(shared.mutex);auto result=shared.counts;if(shared.lastRetired)result.retiredStage=shared.lastRetired->stage;
    result.blocked=static_cast<unsigned>(std::count_if(shared.retired.begin(),shared.retired.end(),[](HANDLE handle){return WaitForSingleObject(handle,0)!=WAIT_OBJECT_0;}));
    return result;
}
HWND ContainedShelfFilePreview::window()const{auto&shared=*shared_;std::lock_guard lock(shared.mutex);return shared.popup;}
bool ContainedShelfFilePreview::settle(DWORD milliseconds){return WaitForSingleObject(shared_->idle,milliseconds)==WAIT_OBJECT_0;}
bool ContainedShelfFilePreview::settleRetired(DWORD milliseconds){
    auto&shared=*shared_;need(shared.uiThread==GetCurrentThreadId(),"Contained preview requires its creating thread");
    std::vector<HANDLE>handles;
    {std::lock_guard lock(shared.mutex);for(const auto handle:shared.retired){HANDLE copy{};if(DuplicateHandle(GetCurrentProcess(),handle,GetCurrentProcess(),&copy,SYNCHRONIZE,FALSE,0))handles.push_back(copy);}}
    const bool ended=handles.empty()||WaitForMultipleObjects(static_cast<DWORD>(std::min<std::size_t>(handles.size(),MAXIMUM_WAIT_OBJECTS)),handles.data(),TRUE,milliseconds)<WAIT_OBJECT_0+handles.size();
    for(const auto handle:handles)CloseHandle(handle);
    std::lock_guard lock(shared.mutex);shared.prune();return ended&&shared.retired.empty();
}
} // namespace endfield::native
#endif
