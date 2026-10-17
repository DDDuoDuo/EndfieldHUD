#ifdef _WIN32
// Projection drawing latency probe: a measurement tool, not a ctest test.
//
// It measures the production input-to-display chain of a Projection stroke in
// an interactive desktop session (real DWM, real display refresh), built from
// the objects EndfieldHUD.exe runs while the Projection workspace is shown:
//   - app::OverlayHost sized to the primary monitor (its message pump, its
//     WM_MOUSEMOVE translation, its frame requests and waitable timer);
//   - the Application's Projection routes (app/application_events.cpp,
//     Application::Impl::onPointer, refresh and onFrame/present while
//     projectionShown()), reproduced below;
//   - the hardware composition Renderer with the production options (media
//     video device, no driver worker threads), the DesktopBackdrop bridge, the
//     shared media broker and tools::ProjectionWorkspace.
//
// By default nothing is shown and no global input is touched: the window
// lies outside every monitor and the stroke is a 1000 Hz path of
// WM_MOUSEMOVE messages posted to the probe's own window with the system's
// coalescing reproduced (one move waits in the queue at a time and, like
// the move Windows synthesizes when the thread reads its queue, carries the
// NEWEST position when it is read; a thread-local WH_GETMESSAGE hook writes
// it). --sliver shows one pixel column of the window at the monitor's right
// edge so DWM composes it at the display refresh, and --real draws with real
// SendInput moves over the shown workspace; both refuse to run (exit 77)
// while someone uses the desktop (input in the last two minutes or a
// full-screen application in the foreground). --gpu-load adds a background
// device that keeps the GPU saturated, as a game does.
//
// Reported: per sample, the time from its generation to the vertical blank
// that displayed the first frame containing it (IDXGISwapChain frame
// statistics; the compositor's vertical-blank grid as a fallback) in ms and
// in refresh periods; the samples the stroke kept; per-move and per-frame CPU
// time, rasterizations and texture uploads; Present blocking; the achieved
// presentation rate while drawing, back to back and during a finite
// animation; and the frames, timer wakes and CPU time of the open and the
// closed idle workspace.
#include "tools/projection_workspace.hpp"
#include "app/presentation_priority.hpp"
#include <windows.h>
#include <dwmapi.h>
#include <DispatcherQueue.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.System.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <numeric>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {
namespace n=endfield::native;namespace t=endfield::tools;namespace a=endfield::app;
constexpr int unavailableExit=77;
struct Unavailable {std::string reason;};
void hr(HRESULT value){winrt::check_hresult(value);}
void need(bool value,const char*message){if(!value)throw std::runtime_error(message);}
struct Queue {
    winrt::Windows::System::DispatcherQueueController controller{nullptr};
    Queue(){const DispatcherQueueOptions options{sizeof(DispatcherQueueOptions),DQTYPE_THREAD_CURRENT,DQTAT_COM_STA};hr(CreateDispatcherQueueController(options,reinterpret_cast<ABI::Windows::System::IDispatcherQueueController**>(winrt::put_abi(controller))));}
    ~Queue(){try{auto operation=controller.ShutdownQueueAsync();const auto end=GetTickCount64()+10000;while(operation.Status()==winrt::Windows::Foundation::AsyncStatus::Started){if(GetTickCount64()>end)std::terminate();MSG m;while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}MsgWaitForMultipleObjectsEx(0,nullptr,10,QS_ALLINPUT,MWMO_INPUTAVAILABLE);}operation.GetResults();}catch(...){std::terminate();}}
};
double fileSeconds(FILETIME f){return (double(f.dwHighDateTime)*4294967296.+f.dwLowDateTime)*1e-7;}
double cpuSeconds(){FILETIME c{},e{},k{},u{};GetProcessTimes(GetCurrentProcess(),&c,&e,&k,&u);return fileSeconds(k)+fileSeconds(u);}
double threadSeconds(HANDLE thread=GetCurrentThread()){FILETIME c{},e{},k{},u{};GetThreadTimes(thread,&c,&e,&k,&u);return fileSeconds(k)+fileSeconds(u);}
struct Summary {double mean{},p50{},p95{},max{};std::size_t count{};};
Summary summarize(std::vector<double> v){Summary s;s.count=v.size();if(v.empty())return s;std::sort(v.begin(),v.end());s.mean=std::accumulate(v.begin(),v.end(),0.)/double(v.size());
    const auto at=[&](double q){return v[std::min(v.size()-1,std::size_t(q*double(v.size()-1)+.5))];};s.p50=at(.5);s.p95=at(.95);s.max=v.back();return s;}
std::string row(const char*name,const Summary&s,double scale=1000,const char*unit="ms"){std::ostringstream o;o<<std::fixed<<std::setprecision(3)<<"  "<<name<<": mean "<<s.mean*scale<<" / p50 "<<s.p50*scale<<" / p95 "<<s.p95*scale<<" / max "<<s.max*scale<<' '<<unit<<" (n="<<s.count<<")\n";return o.str();}

enum class Mode {offscreen,sliver,real};
struct Options {Mode mode{Mode::offscreen};bool gpuLoad{},force{},normalPriority{};};
// Someone uses this desktop: input in the last two minutes, or a full-screen
// application (a game) in the foreground.
std::optional<std::string> desktopInUse(){
    LASTINPUTINFO last{sizeof(last),0};GetLastInputInfo(&last);const auto idle=(GetTickCount()-last.dwTime)/1000.;
    std::ostringstream reason;
    if(idle<120)reason<<"input "<<std::fixed<<std::setprecision(1)<<idle<<" s ago";
    if(const auto foreground=GetForegroundWindow()){RECT r{};MONITORINFO m{};m.cbSize=sizeof(m);wchar_t type[64]{};GetClassNameW(foreground,type,64);
        if(GetWindowRect(foreground,&r)&&GetMonitorInfoW(MonitorFromWindow(foreground,MONITOR_DEFAULTTONEAREST),&m)&&r.left<=m.rcMonitor.left&&r.top<=m.rcMonitor.top&&r.right>=m.rcMonitor.right&&r.bottom>=m.rcMonitor.bottom&&
           std::wstring_view(type)!=L"Progman"&&std::wstring_view(type)!=L"WorkerW"){if(reason.tellp())reason<<", ";reason<<"a full-screen application is in the foreground";}}
    if(!reason.tellp())return {};return reason.str();
}

// --- The background GPU load (--gpu-load): a second device that renders
// 150 blended full-screen layers into its own offscreen target, back to back.
struct GpuLoad {
    std::atomic<bool> stop{false};std::atomic<std::uint64_t> frames{0};std::thread worker;std::exception_ptr error;
    void start(const wchar_t*shader,unsigned width,unsigned height){
        worker=std::thread([this,shader,width,height]{try{
            WNDCLASSW cl{};cl.lpfnWndProc=DefWindowProcW;cl.hInstance=GetModuleHandleW(nullptr);cl.lpszClassName=L"EndfieldHUDLatencyProbeLoad";RegisterClassW(&cl);
            const auto h=CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP|WS_EX_TOOLWINDOW,cl.lpszClassName,L"Hidden GPU load",WS_POPUP,0,0,16,16,nullptr,nullptr,cl.hInstance,nullptr);need(h,"GPU load window");
            {n::Renderer load;load.initialize(h,width,height,{n::Driver::hardware,shader,n::RenderTarget::offscreenForTests});
                std::vector<n::Vertex> vertices{{{0,0,0},{0,0},{1,1,1,1}},{{float(width),0,0},{1,0},{1,1,1,1}},{{float(width),float(height),0},{1,1},{1,1,1,1}},{{0,float(height),0},{0,1},{1,1,1,1}}};std::vector<std::uint32_t> indices{0,1,2,0,2,3};
                load.setMesh("load.quad",1,{vertices,indices});std::vector<n::DrawObject> draws(150);for(auto&d:draws){d.sourceID=d.meshID="load.quad";d.opacity=.3f;d.linearTint={.4f,.5f,.6f,1};}
                load.setDrawList(draws);load.setCamera(n::layerViewportProjection(width,height));
                while(!stop){load.draw(false);if(++frames%2==0)(void)load.waitForGpuIdle();}
                (void)load.waitForGpuIdle();}
            DestroyWindow(h);}catch(...){error=std::current_exception();}});
    }
    void finish(){stop=true;if(worker.joinable())worker.join();if(error)std::rethrow_exception(error);}
    ~GpuLoad(){stop=true;if(worker.joinable())worker.join();}
};

struct Sample {double x{},y{},generated{};}; // client pixels; clockNow time
struct Move {double dispatched{},handler{};std::size_t sample{};};
struct Frame {double start{},end{},render{},present{},presentReturned{};std::size_t newestSample{};bool hasSample{};std::uint64_t presentCount{},presents{},textureUploads{},rasterizations{};};

// The coalescing of a mouse moving at 1000 Hz, for posted moves.
struct Coalescing {HWND window{};std::atomic<bool> queued{false};std::atomic<std::size_t> latest{0};const std::vector<Sample>*samples{};std::uint64_t rewritten{},leaves{};bool dropLeaves{};};
Coalescing*coalescing{};
constexpr WPARAM marker=0x4000; // an unused MK_ bit marks the probe's posted move
LRESULT CALLBACK readMessage(int code,WPARAM remove,LPARAM value){
    if(code==HC_ACTION&&remove==PM_REMOVE&&coalescing){auto&m=*reinterpret_cast<MSG*>(value);
        if(m.hwnd==coalescing->window&&m.message==WM_MOUSEMOVE&&(m.wParam&marker)){const auto&s=(*coalescing->samples)[coalescing->latest.load(std::memory_order_acquire)];
            m.wParam=MK_LBUTTON;m.lParam=MAKELPARAM(static_cast<WORD>(static_cast<short>(std::lround(s.x))),static_cast<WORD>(static_cast<short>(std::lround(s.y))));
            ++coalescing->rewritten;coalescing->queued.store(false,std::memory_order_release);}
        // Windows answers leave tracking at once on a window the cursor is
        // never over; in EndfieldHUD.exe the cursor is over the workspace.
        else if(coalescing->dropLeaves&&m.hwnd==coalescing->window&&m.message==WM_MOUSELEAVE){m.message=WM_NULL;++coalescing->leaves;}}
    return CallNextHookEx(nullptr,code,remove,value);
}

int run(const wchar_t*shader,const std::filesystem::path&resources,const Options&options,std::ostream&report){
    if(options.mode!=Mode::offscreen||options.gpuLoad)if(const auto busy=desktopInUse();busy&&!options.force)throw Unavailable{"the desktop is in use ("+*busy+"); this mode shows a window, injects input or loads the GPU"};
    Queue queue;
    a::OverlayHost host;
    t::ProjectionWorkspace*shown{};
    MONITORINFO monitor{};monitor.cbSize=sizeof(monitor);need(GetMonitorInfoW(MonitorFromPoint({0,0},MONITOR_DEFAULTTOPRIMARY),&monitor)!=FALSE,"Primary monitor");
    const auto bounds=monitor.rcMonitor;const auto width=std::uint32_t(bounds.right-bounds.left),height=std::uint32_t(bounds.bottom-bounds.top);
    DWM_TIMING_INFO timing{};timing.cbSize=sizeof(timing);hr(DwmGetCompositionTimingInfo(nullptr,&timing));
    LARGE_INTEGER frequency{};QueryPerformanceFrequency(&frequency);
    const double period=double(timing.qpcRefreshPeriod)/double(frequency.QuadPart),vblank=double(timing.qpcVBlank)/double(frequency.QuadPart);
    const double refreshHz=timing.rateRefresh.uiDenominator?double(timing.rateRefresh.uiNumerator)/timing.rateRefresh.uiDenominator:1/period;

    std::vector<Move> moves;std::vector<Frame> frames;moves.reserve(20000);frames.reserve(20000);
    std::vector<Sample> samples(4000);std::atomic<std::size_t> published{0};
    std::map<std::uint64_t,double> displayed; // present count -> displayed vertical blank (clockNow epoch)
    std::size_t newestSample{};bool anySample{},forceAnimation{},recording{},backToBack{};double releaseHandler{},releaseFrame{};bool releasing{};
    n::Renderer renderer;n::LayerRasterizer raster;double scale{1};
    // The sample an event's point belongs to (the path advances two pixels in x per sample).
    const auto sampleAt=[&](double lx,double ly)->std::optional<std::size_t>{
        const auto count=published.load(std::memory_order_acquire);if(!count)return {};
        const double cx=lx*scale,cy=ly*scale;const auto guess=std::lround((cx-samples[0].x)/2);
        std::optional<std::size_t>best;double distance=6;
        for(long k=std::max(0L,guess-4);k<=guess+4&&k<long(count);++k){const auto d=std::abs(samples[std::size_t(k)].x-cx)+std::abs(samples[std::size_t(k)].y-cy);if(d<distance){distance=d;best=std::size_t(k);}}
        return best;};
    const auto pollStatistics=[&]{if(const auto s=renderer.frameStatistics())displayed[s->presentCount]=s->syncTime+double(std::int64_t(s->presentRefreshCount)-std::int64_t(s->syncRefreshCount))*period;};
    const auto demand=[&](double time){auto value=shown?shown->demand(time):endfield::core::FrameDemand{};if(forceAnimation&&value.presented)value.finiteAnimation=true;return value;};

    a::OverlayCallbacks route;
    // Application::Impl::onPointer, Projection branch, and refresh().
    route.pointer=[&](const a::PointerEvent&e){
        if(!shown||!shown->presented())return false;
        const auto begin=a::OverlayHost::clockNow();
        const bool handled=shown->pointer(e,begin);const bool locked=shown->preview()->pointerLocked();(void)handled;
        host.capturePointer(locked);host.setFrameDemand(demand(a::OverlayHost::clockNow()));host.invalidate();
        const auto end=a::OverlayHost::clockNow();
        if(recording&&e.kind==a::PointerKind::move)if(const auto s=sampleAt(e.x,e.y)){if(!anySample||*s>newestSample)newestSample=*s;anySample=true;moves.push_back({begin,end-begin,*s});}
        if(recording&&e.kind==a::PointerKind::up){releaseHandler=end-begin;releasing=true;}
        return true;
    };
    route.wheel=[&](const a::WheelEvent&e){if(!shown||!shown->presented())return false;(void)shown->wheel(e,a::OverlayHost::clockNow());return true;};
    // Application::Impl::callbacks(): the swap chain's admission paces frames.
    route.presentationWaitable=[&]{return renderer.presentationWaitable();};
    route.presentationHeld=[&]{return renderer.presentationHeld();};
    route.presentationAdmitted=[&]{renderer.presentationAdmitted();};
    // Application::Impl::onFrame -> present(), Projection branch.
    route.frame=[&](double){
        if(!shown||!shown->presented())return;
        const auto before=renderer.stats();const auto rasterBefore=raster.stats().rasterizations;
        (void)renderer.waitForPresentation(50); // present(): frame start
        Frame f;f.start=a::OverlayHost::clockNow();f.newestSample=newestSample;f.hasSample=anySample;
        (void)shown->mediaChanged(f.start);(void)shown->render(a::OverlayHost::clockNow(),true);
        const auto after=renderer.stats();f.end=a::OverlayHost::clockNow();
        f.present=after.presentSeconds-before.presentSeconds;f.render=f.end-f.start-f.present;f.presentReturned=after.lastPresentReturned;f.presentCount=after.lastPresentCount;f.presents=after.presents-before.presents;
        f.textureUploads=after.textureUploads-before.textureUploads;f.rasterizations=raster.stats().rasterizations-rasterBefore;
        if(releasing){releaseFrame=f.end-f.start;releasing=false;}
        host.setFrameDemand(demand(a::OverlayHost::clockNow()));
        if(recording)frames.push_back(f);pollStatistics();
        if(backToBack)host.invalidate();
    };
    // Outside every monitor (default), one pixel column inside the right edge
    // (--sliver), or covering the monitor (--real).
    const int left=options.mode==Mode::real?bounds.left:options.mode==Mode::sliver?bounds.right-1:GetSystemMetrics(SM_XVIRTUALSCREEN)+GetSystemMetrics(SM_CXVIRTUALSCREEN)+2048;
    a::OverlayOptions windowOptions{L"EndfieldHUD Projection latency probe",left,bounds.top,width,height,{}};windowOptions.coverMonitor=options.mode==Mode::real;
    host.create(windowOptions,std::move(route));
    const auto window=static_cast<HWND>(host.hwnd());const auto metrics=host.metrics();scale=metrics.scale;
    n::RendererOptions rendererOptions{n::Driver::hardware,shader,n::RenderTarget::composition,true};rendererOptions.preventDriverThreads=true;
    renderer.initialize(window,metrics.pixelWidth,metrics.pixelHeight,rendererOptions);
    const BOOL off=FALSE;hr(DwmSetWindowAttribute(window,DWMWA_USE_HOSTBACKDROPBRUSH,&off,sizeof(off)));
    n::DesktopBackdrop backdrop;backdrop.initialize(window,{metrics.pixelWidth,metrics.pixelHeight,true,false,.5,.5,0},{false});
    const n::NotesImageRoute mediaRoute{window,WM_APP+250,1};
    n::NativeNotesImageDecoder decoder([](const auto&)->n::NotesImageAccess{throw std::runtime_error("The probe reads no media");},mediaRoute);
    n::NativeNotesImagePlayback images(decoder);n::NativeMediaRequestBroker broker(decoder,images,mediaRoute);
    const auto device=renderer.deviceInfo();
    const char*modeName=options.mode==Mode::real?"real SendInput moves over the shown workspace":options.mode==Mode::sliver?"posted coalesced moves, one pixel column shown":"posted coalesced moves, window outside every monitor";
    report<<"EndfieldHUD Projection drawing latency probe ("<<modeName<<(options.gpuLoad?", background GPU load":"")<<")\n";
    report<<"Display: "<<metrics.pixelWidth<<"x"<<metrics.pixelHeight<<" at "<<std::fixed<<std::setprecision(2)<<refreshHz<<" Hz (DWM refresh period "<<std::setprecision(3)<<period*1000<<" ms), "<<metrics.dpi<<" DPI (scale "<<metrics.scale<<")\n";
    report<<"Renderer adapter: "<<device.name<<", frame latency waitable object: "<<(renderer.stats().frameLatencyWaitable?"yes":"no")<<"\n";
    if(const auto busy=desktopInUse())report<<"Desktop: in use ("<<*busy<<")\n";else report<<"Desktop: idle\n";
    GpuLoad load;if(options.gpuLoad)load.start(shader,width,height);
    POINT saved{};GetCursorPos(&saved);
    struct RestoreCursor {POINT p;bool moved{};~RestoreCursor(){if(moved)SetCursorPos(p.x,p.y);}} restore{saved};
    Coalescing emulation;emulation.window=window;emulation.samples=&samples;emulation.dropLeaves=options.mode!=Mode::real;coalescing=&emulation;
    struct Hook {HHOOK h{};~Hook(){if(h)UnhookWindowsHookEx(h);coalescing=nullptr;}} hook;
    hook.h=SetWindowsHookExW(WH_GETMESSAGE,readMessage,nullptr,GetCurrentThreadId());need(hook.h!=nullptr,"Thread-local message hook");
    {
        t::ProjectionWorkspace projection(host,renderer,raster,backdrop,broker,{.98,.83,.12,1},.5,.5);
        struct Unroute {t::ProjectionWorkspace*&value;~Unroute(){value=nullptr;}} unroute{shown};
        t::ProjectionPreviewOptions preview;
        preview.raster.assetRoot=resources/"notes-format";preview.raster.pixelsPerPoint=2;preview.raster.paddingPoints=1;
        t::ProjectionMediaBindingOptions binding;
        binding.resolve=[](const auto&)->n::NotesImageAccess{throw std::runtime_error("Unexpected media read");};
        binding.errorText=[](HRESULT){return std::string("Synthetic error");};
        projection.resize(metrics,0);projection.show(preview,std::move(binding),a::OverlayHost::clockNow(),false);shown=&projection;
        host.show(false);
        // Application::Impl::projectionCommand(showProjection) raises the
        // shown workspace's presentation priority (--normal-priority does not).
        a::PresentationPriority priority;if(!options.normalPriority)priority.raise(&renderer);
        {const auto&r=priority.report();report<<"Presentation priority: "<<(options.normalPriority?"normal":"raised")<<" (process "<<r.processRaised<<", MMCSS "<<r.threadRegistered<<", GPU class "<<r.gpuSchedulingRaised<<", GPU thread "<<r.gpuThreadRaised<<")\n";}
        const auto pumpFor=[&](double seconds){const auto end=a::OverlayHost::clockNow()+seconds;while(a::OverlayHost::clockNow()<end){need(host.pumpOnce(1),"The host keeps running");pollStatistics();}};
        pumpFor(.6); // the .18 s entrance and its last frames

        // --- Scenario 1: one stroke from a 1000 Hz mouse path. Two pixels per
        // sample: every sample is at least one logical point from the
        // previous one, so the source 1-point filter keeps all of them.
        const std::size_t count=1150;const double x0=std::round(metrics.pixelWidth*.05),yMid=std::round(metrics.pixelHeight*.62),amplitude=std::round(metrics.pixelHeight*.12);
        for(std::size_t k=0;k<count;++k){samples[k].x=x0+2*double(k);samples[k].y=yMid+std::round(amplitude*std::sin(double(k)/120));}
        POINT origin{0,0};need(ClientToScreen(window,&origin)!=FALSE,"Client origin");
        const int vx=GetSystemMetrics(SM_XVIRTUALSCREEN),vy=GetSystemMetrics(SM_YVIRTUALSCREEN),vw=GetSystemMetrics(SM_CXVIRTUALSCREEN),vh=GetSystemMetrics(SM_CYVIRTUALSCREEN);
        const auto inject=[&](const Sample&s){const double x=origin.x+s.x,y=origin.y+s.y;INPUT input{};input.type=INPUT_MOUSE;input.mi.dx=LONG(std::lround((x-vx)*65535./(vw-1)));input.mi.dy=LONG(std::lround((y-vy)*65535./(vh-1)));
            input.mi.dwFlags=MOUSEEVENTF_MOVE|MOUSEEVENTF_ABSOLUTE|MOUSEEVENTF_VIRTUALDESK;restore.moved=true;return SendInput(1,&input,sizeof(input))==1;};
        const auto client=[&](const Sample&s){return MAKELPARAM(static_cast<WORD>(static_cast<short>(std::lround(s.x))),static_cast<WORD>(static_cast<short>(std::lround(s.y))));};
        samples[0].generated=a::OverlayHost::clockNow();published.store(1,std::memory_order_release);
        if(options.mode==Mode::real){need(inject(samples[0]),"SendInput reaches the session");pumpFor(.1);POINT cursor{};GetCursorPos(&cursor);
            if(std::abs(cursor.x-(origin.x+samples[0].x))>2||std::abs(cursor.y-(origin.y+samples[0].y))>2)throw Unavailable{"injected input does not move the cursor (another application holds it)"};}
        else{need(PostMessageW(window,WM_MOUSEMOVE,0,client(samples[0]))!=FALSE,"Post the first move");pumpFor(.05);}
        const auto stats0=host.stats();const auto renderer0=renderer.stats();const auto raster0=raster.stats();const auto cpu0=cpuSeconds();const auto ui0=threadSeconds();
        const auto timing0=projection.timing();const auto ink0=projection.preview()->inkStats();
        recording=true;
        need(PostMessageW(window,WM_LBUTTONDOWN,MK_LBUTTON,client(samples[0]))!=FALSE,"Post the press");pumpFor(.05);
        need(projection.preview()->pointerLocked(),"The press starts a stroke");
        std::atomic<bool> done{false};
        const auto strokeStart=a::OverlayHost::clockNow();
        std::thread producer([&]{
            // A high-resolution waitable timer (no spinning core): one sample per millisecond.
            const auto timer=CreateWaitableTimerExW(nullptr,nullptr,0x00000002/*high resolution*/,TIMER_ALL_ACCESS);
            const auto begin=a::OverlayHost::clockNow();
            for(std::size_t k=1;k<count;++k){const auto due=begin+double(k)*.001;const auto wait=due-a::OverlayHost::clockNow();
                if(wait>0&&timer){LARGE_INTEGER d{};d.QuadPart=-LONGLONG(wait*1e7);SetWaitableTimer(timer,&d,0,nullptr,nullptr,FALSE);WaitForSingleObject(timer,INFINITE);}
                samples[k].generated=a::OverlayHost::clockNow();emulation.latest.store(k,std::memory_order_release);published.store(k+1,std::memory_order_release);
                if(options.mode==Mode::real)inject(samples[k]);
                else if(!emulation.queued.exchange(true,std::memory_order_acq_rel))PostMessageW(window,WM_MOUSEMOVE,MK_LBUTTON|marker,0);}
            if(timer)CloseHandle(timer);done=true;});
        while(!done){(void)host.pumpOnce(1);pollStatistics();}
        producer.join();const auto strokeEnd=a::OverlayHost::clockNow();
        pumpFor(.15);
        need(PostMessageW(window,WM_LBUTTONUP,0,client(samples[count-1]))!=FALSE,"Post the release");
        pumpFor(.1);recording=false;pumpFor(.15);
        const auto stats1=host.stats();const auto renderer1=renderer.stats();const auto raster1=raster.stats();const auto cpu1=cpuSeconds();const auto ui1=threadSeconds();const auto cpuWindow=a::OverlayHost::clockNow()-strokeStart;
        const auto timing1=projection.timing();const auto ink1=projection.preview()->inkStats();
        const auto strokes=projection.model().drawing().strokes();
        // The samples the committed stroke kept (history points included).
        std::vector<char> kept(count);
        if(!strokes.empty())for(const auto&point:strokes.back().points)if(const auto s=sampleAt(point.x*metrics.width,point.y*metrics.height))kept[*s]=1;
        std::vector<Frame> drawn;for(const auto&f:frames)if(f.presents)drawn.push_back(f);
        const auto displayTime=[&](const Frame&f)->std::optional<double>{const auto it=displayed.lower_bound(f.presentCount);if(it==displayed.end())return {};return it->second;};
        // The compositor shows a frame presented before vertical blank V at V+1 (fallback without frame statistics).
        const auto estimated=[&](double returned){const double k=std::ceil((returned-vblank)/period);return vblank+(k+1)*period;};
        std::vector<double> latency,latencyFrames,presentLatency,frameDelay;std::size_t undisplayed{},statisticsMisses{};
        for(std::size_t s=1;s<count;++s){if(!kept[s])continue;
            const auto f=std::find_if(drawn.begin(),drawn.end(),[&](const Frame&x){return x.hasSample&&x.newestSample>=s;});if(f==drawn.end()){++undisplayed;continue;}
            // Frames presented after the sample reached the application before the one showing it.
            const auto delivered=std::find_if(moves.begin(),moves.end(),[&](const Move&m){return m.sample>=s;});
            if(delivered!=moves.end())frameDelay.push_back(double(std::count_if(drawn.begin(),f,[&](const Frame&x){return x.start>=delivered->dispatched;})));
            presentLatency.push_back(f->presentReturned-samples[s].generated);
            const auto shownAt=displayTime(*f);const double at=shownAt?*shownAt:estimated(f->presentReturned);if(!shownAt)++statisticsMisses;
            latency.push_back(at-samples[s].generated);latencyFrames.push_back((at-samples[s].generated)/period);}
        std::vector<double> handler,render,present,intervals,displayIntervals,uploads,rasters;
        for(const auto&m:moves)handler.push_back(m.handler);
        double previous=-1,previousShown=-1;std::size_t strokeFrames{};
        for(const auto&f:drawn){if(f.start<strokeStart||f.start>strokeEnd)continue;++strokeFrames;render.push_back(f.render);present.push_back(f.present);uploads.push_back(double(f.textureUploads));rasters.push_back(double(f.rasterizations));
            if(previous>=0)intervals.push_back(f.presentReturned-previous);previous=f.presentReturned;
            if(const auto d=displayTime(f)){if(previousShown>=0&&*d>previousShown)displayIntervals.push_back(*d-previousShown);previousShown=*d;}}
        const double strokeSeconds=strokeEnd-strokeStart;
        report<<"\n[Stroke] "<<count<<" samples at 1000 Hz over "<<std::setprecision(3)<<strokeSeconds<<" s\n";
        report<<"  WM_MOUSEMOVE events delivered: "<<moves.size()<<", samples the committed stroke kept: "<<std::count(kept.begin(),kept.end(),char(1))<<" of "<<count<<" ("<<(strokes.empty()?0:strokes.back().points.size())<<" points)\n";
        report<<"  frames presented while drawing: "<<strokeFrames<<" -> "<<std::setprecision(1)<<double(strokeFrames)/strokeSeconds<<" fps (display "<<refreshHz<<" Hz)\n"<<std::setprecision(3);
        report<<row("present-to-present interval",summarize(intervals));
        report<<row("displayed frame interval (frame statistics)",summarize(displayIntervals));
        report<<row("input -> displayed (sample generation to the vertical blank showing it)",summarize(latency));
        report<<row("input -> displayed, in refresh periods",summarize(latencyFrames),1,"frames");
        report<<row("input -> Present returned",summarize(presentLatency));
        report<<row("frames presented after the sample was delivered before the one showing it",summarize(frameDelay),1,"frames");
        report<<row("pointer move handler CPU (Application route + Projection)",summarize(handler));
        report<<row("frame CPU excluding Present",summarize(render));
        report<<row("Present call (blocking)",summarize(present));
        report<<row("texture uploads per frame",summarize(uploads),1,"");
        report<<row("rasterizations in the frame itself",summarize(rasters),1,"");
        report<<"  release: handler "<<releaseHandler*1000<<" ms, next frame "<<releaseFrame*1000<<" ms\n";
        {const double renders=double(std::max<std::uint64_t>(1,timing1.renders-timing0.renders)),pointers=double(std::max<std::uint64_t>(1,timing1.pointers-timing0.pointers)),presents=double(std::max<std::uint64_t>(1,renderer1.presents-renderer0.presents));
         report<<"  per frame (ms): workspace update "<<(timing1.update-timing0.update)/renders*1000<<", publication "<<(timing1.publish-timing0.publish)/renders*1000<<", draw+Present "<<(timing1.draw-timing0.draw)/renders*1000
               <<" (renderer draw CPU "<<(renderer1.drawSeconds-renderer0.drawSeconds)/presents*1000<<", Present "<<(renderer1.presentSeconds-renderer0.presentSeconds)/presents*1000<<", draw list "<<(renderer1.drawListSeconds-renderer0.drawListSeconds)/presents*1000<<", texture creation "<<(renderer1.textureUploadSeconds-renderer0.textureUploadSeconds)/presents*1000<<")\n";
         report<<"  per pointer event (ms): workspace pointer "<<(timing1.pointer-timing0.pointer)/pointers*1000<<"; ink update and raster (ms): "<<(ink1.updateSeconds-ink0.updateSeconds)*1000<<" in all, "<<(ink1.updateSeconds-ink0.updateSeconds)/pointers*1000<<" per pointer event, "<<(ink1.updateSeconds-ink0.updateSeconds)/renders*1000<<" per frame\n";
         report<<"  ink: whole-tile rasters "<<ink1.tileRasters-ink0.tileRasters<<", region rasters "<<ink1.patchRasters-ink0.patchRasters<<" ("<<(ink1.patchPixels-ink0.patchPixels)/std::max<std::uint64_t>(1,ink1.patchRasters-ink0.patchRasters)<<" pixels each), regions replaced in place "<<ink1.regionUploads-ink0.regionUploads<<"; texture region uploads "<<renderer1.textureRegionUploads-renderer0.textureRegionUploads<<", unadmitted presents "<<renderer1.unadmittedPresents-renderer0.unadmittedPresents<<"\n";}
        report<<"  during the stroke: rasterizations "<<raster1.rasterizations-raster0.rasterizations<<", texture uploads "<<renderer1.textureUploads-renderer0.textureUploads<<", mesh uploads "<<renderer1.meshUploads-renderer0.meshUploads<<", presents "<<renderer1.presents-renderer0.presents<<"\n";
        report<<"  host: pointer messages "<<stats1.pointerMessages-stats0.pointerMessages<<", frame posts "<<stats1.framePosts-stats0.framePosts<<", frames "<<stats1.frames-stats0.frames<<", timer wakes "<<stats1.timerWakes-stats0.timerWakes<<"; coalesced moves written "<<emulation.rewritten<<", off-window leave notices dropped "<<emulation.leaves<<"\n";
        report<<"  presentation gate: frames that waited "<<stats1.presentationWaits-stats0.presentationWaits<<", admissions taken by the pump "<<stats1.presentationAdmissions-stats0.presentationAdmissions<<", 50 ms fallbacks "<<stats1.presentationFallbacks-stats0.presentationFallbacks<<"; mouse history samples "<<stats1.pointerHistorySamples-stats0.pointerHistorySamples<<"; host refresh rate "<<stats1.refreshRate<<" Hz\n";
        report<<"  CPU while drawing: process "<<std::setprecision(1)<<(cpu1-cpu0)/cpuWindow*100<<" % of one core, UI thread "<<(ui1-ui0)/cpuWindow*100<<" %\n"<<std::setprecision(3);
        report<<"  displayed-frame statistics: "<<displayed.size()<<" presents with a displayed vertical blank; "<<statisticsMisses<<" samples used the estimated vertical blank; "<<undisplayed<<" kept samples never drawn\n";

        // --- Scenario 2: frames requested back to back (the presentation path's own rate).
        frames.clear();recording=true;backToBack=true;host.invalidate();
        {const auto r0=renderer.stats();const auto begin=a::OverlayHost::clockNow();pumpFor(1);const auto r1=renderer.stats();const auto seconds=a::OverlayHost::clockNow()-begin;
            backToBack=false;recording=false;pumpFor(.1);
            std::vector<double> gaps,blocking;previous=-1;for(const auto&f:frames){if(!f.presents)continue;blocking.push_back(f.present);if(previous>=0)gaps.push_back(f.presentReturned-previous);previous=f.presentReturned;}
            report<<"\n[Back to back] 1 s of frames requested as soon as the previous one returned\n  frames presented: "<<r1.presents-r0.presents<<" -> "<<std::setprecision(1)<<double(r1.presents-r0.presents)/seconds<<" fps\n"<<std::setprecision(3);
            report<<row("present-to-present interval",summarize(gaps));report<<row("Present call (blocking)",summarize(blocking));}

        // --- Scenario 3: finite animation pacing (any HUD transition).
        frames.clear();recording=true;forceAnimation=true;host.setFrameDemand(demand(a::OverlayHost::clockNow()));
        const auto animationStart=a::OverlayHost::clockNow();const auto animation0=renderer.stats();const auto animationCpu0=cpuSeconds();pumpFor(1.5);const auto animation1=renderer.stats();const auto animationEnd=a::OverlayHost::clockNow();const auto animationCpu1=cpuSeconds();
        forceAnimation=false;host.setFrameDemand(demand(a::OverlayHost::clockNow()));recording=false;pumpFor(.1);
        std::vector<double> animationIntervals,animationDisplayed;previous=-1;previousShown=-1;
        for(const auto&f:frames){if(!f.presents)continue;if(previous>=0)animationIntervals.push_back(f.presentReturned-previous);previous=f.presentReturned;if(const auto d=displayTime(f)){if(previousShown>=0&&*d>previousShown)animationDisplayed.push_back(*d-previousShown);previousShown=*d;}}
        report<<"\n[Finite animation] 1.5 s of finite frame demand ("<<host.stats().pacedFrames<<" frames paced by the display so far)\n";
        report<<"  frames presented: "<<animation1.presents-animation0.presents<<" -> "<<std::setprecision(1)<<double(animation1.presents-animation0.presents)/(animationEnd-animationStart)<<" fps (display "<<refreshHz<<" Hz), process CPU "<<(animationCpu1-animationCpu0)/(animationEnd-animationStart)*100<<" % of one core\n"<<std::setprecision(3);
        report<<row("present-to-present interval",summarize(animationIntervals));
        report<<row("displayed frame interval (frame statistics)",summarize(animationDisplayed));

        // --- Scenario 4: open idle (shown, nothing changes).
        pumpFor(.3);
        {const auto s0=host.stats();const auto r0=renderer.stats();const auto c0=cpuSeconds();const auto u0=threadSeconds();const auto begin=a::OverlayHost::clockNow();
            while(a::OverlayHost::clockNow()<begin+3){need(host.pumpOnce(100),"The host keeps running");}
            const auto s1=host.stats();const auto r1=renderer.stats();const auto c1=cpuSeconds();const auto u1=threadSeconds();const auto seconds=a::OverlayHost::clockNow()-begin;
            report<<"\n[Open idle] 3 s, workspace shown, no input\n  frames "<<s1.frames-s0.frames<<", presents "<<r1.presents-r0.presents<<", timer wakes "<<s1.timerWakes-s0.timerWakes<<", frame posts "<<s1.framePosts-s0.framePosts<<", process CPU "<<std::setprecision(2)<<(c1-c0)/seconds*100<<" % of one core (UI thread "<<(u1-u0)/seconds*100<<" %)\n"<<std::setprecision(3);}
        shown=nullptr;projection.dismiss(a::OverlayHost::clockNow(),true);projection.close(a::OverlayHost::clockNow());
        // --- Scenario 5: closed idle (hidden).
        {const auto s0=host.stats();const auto r0=renderer.stats();const auto c0=cpuSeconds();const auto u0=threadSeconds();const auto begin=a::OverlayHost::clockNow();
            while(a::OverlayHost::clockNow()<begin+3){need(host.pumpOnce(100),"The host keeps running");}
            const auto s1=host.stats();const auto r1=renderer.stats();const auto c1=cpuSeconds();const auto u1=threadSeconds();const auto seconds=a::OverlayHost::clockNow()-begin;
            report<<"\n[Closed idle] 3 s, workspace closed and the window hidden\n  frames "<<s1.frames-s0.frames<<", presents "<<r1.presents-r0.presents<<", timer wakes "<<s1.timerWakes-s0.timerWakes<<", process CPU "<<std::setprecision(2)<<(c1-c0)/seconds*100<<" % of one core (UI thread "<<(u1-u0)/seconds*100<<" %)\n";}
    }
    if(options.gpuLoad){load.finish();report<<"Background GPU load frames: "<<load.frames.load()<<"\n";}
    decoder.stop();backdrop.reset();renderer.reset();host.destroy();
    return 0;
}
}

int wmain(int argc,wchar_t**argv){
    const auto com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    std::wstring reportPath;Options options;
    const auto write=[&](const std::string&text){if(!reportPath.empty()){std::ofstream file(reportPath,std::ios::binary|std::ios::app);file<<text;}};
    try{
        hr(com);need(argc>=3,"Usage: projection_latency_probe <hud.hlsl> <resources> [report.txt] [--sliver|--real] [--gpu-load] [--force]");
        for(int k=3;k<argc;++k){const std::wstring_view arg(argv[k]);if(arg==L"--sliver")options.mode=Mode::sliver;else if(arg==L"--real")options.mode=Mode::real;else if(arg==L"--gpu-load")options.gpuLoad=true;else if(arg==L"--force")options.force=true;else if(arg==L"--normal-priority")options.normalPriority=true;else if(!arg.starts_with(L"--"))reportPath=argv[k];}
        std::ostringstream report;
        const auto code=run(argv[1],std::filesystem::absolute(argv[2]),options,report);
        std::cout<<report.str();write(report.str());
        CoUninitialize();return code;
    }catch(const Unavailable&e){
        const auto text="SKIPPED: "+e.reason+"\n";std::cout<<text;write(text);if(SUCCEEDED(com))CoUninitialize();return unavailableExit;
    }catch(const winrt::hresult_error&e){
        std::ostringstream text;text<<"FAIL: "<<winrt::to_string(e.message())<<" HRESULT "<<std::hex<<static_cast<std::uint32_t>(e.code().value)<<'\n';std::cerr<<text.str();write(text.str());return 1;
    }catch(const std::exception&e){
        const auto text=std::string("FAIL: ")+e.what()+"\n";std::cerr<<text;write(text);return 1;
    }
}
#else
int main(){return 0;}
#endif
