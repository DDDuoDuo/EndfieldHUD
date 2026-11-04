#ifdef _WIN32
// Projection drawing through the production input route, proven by a renderer
// readback. Live RC on the laptop desktop: the toolbar worked and the brush
// changed (ten projectionAction "brushChanged" in the Event Log), but no
// stroke ever appeared.
//
// The route is the one EndfieldHUD.exe runs while the Projection workspace is
// shown, built from the production objects:
//   - app::OverlayHost, the HUD window owner: Win32 mouse messages posted to
//     its own HWND and dispatched by its own pump, translated into
//     PointerEvents (every WM_CAPTURECHANGED into PointerKind::captureLost),
//     and OverlayHost::capturePointer, which takes the capture only on a
//     VISIBLE window, so the hidden suites and the hidden owner coverage never
//     reach it;
//   - the Application's Projection branch (app/application_events.cpp,
//     Application::Impl::onPointer and onWheel while projectionShown()):
//       handled=projection->pointer(e,time);locked=projection->preview()->pointerLocked();
//       host.capturePointer(locked);   // after EVERY event, captureLost included
//     reproduced below as it is (the UI Automation and frame bookkeeping around
//     it takes no capture);
//   - tools::ProjectionWorkspace on that host with the composition Renderer,
//     DesktopBackdrop and shared media broker, shown as
//     Application::Impl::projectionCommand(showProjection) shows it with the
//     options of Application::Impl::projectionOptions, then its own render()
//     for the readback.
// It switches the brush the ways the user can (toolbar eraser, right clicks,
// the wheel, the brush menu), draws one stroke, and reads the stroke back.
// Then it draws through the host's own pump with the production frame path
// (the swap chain's admission, app/overlay_host.hpp) and proves the latency
// contract: each burst of samples is drawn by one frame, the very next one
// presented, which holds the swap chain's admission; the stroke keeps every
// sample; an idle workspace presents nothing.
// The cause of the live failure: the Projection branch asks for the capture
// after every drag event, and Windows answers SetCapture on the window that
// already holds the capture with WM_CAPTURECHANGED naming that window. The
// host reported that notice as a capture loss, which cancelled the stroke at
// its first drag event, and released the capture. The host must neither take
// a capture it already holds again nor report that notice as a loss; a real
// loss (the capture taken away) must still end the stroke.
//
// The window is shown WITHOUT activation at a position outside every monitor
// (capturePointer needs a visible window) for about a second, then destroyed.
// Only messages posted to this process's own window are used: no global
// input, cursor, clipboard, file or user data is touched (Windows' immediate
// WM_MOUSELEAVE answers to leave tracking on a window the cursor is never
// over are dropped, see pump below). Composition swap
// chains need an interactive desktop session; without one (the SSH test
// session: DXGI_ERROR_NOT_CURRENTLY_AVAILABLE) the test exits 77, which ctest
// reports as skipped. Run it from an Interactive scheduled task.
#include "tools/projection_workspace.hpp"
#include "tools/hud_design_overlay_owner.hpp"
#include <windows.h>
#include <dwmapi.h>
#include <DispatcherQueue.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.System.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
namespace n=endfield::native;namespace t=endfield::tools;namespace a=endfield::app;
constexpr int unavailableExit=77;
struct Unavailable {};
unsigned checks{};
void check(bool value,const std::string&message){++checks;if(!value)throw std::runtime_error(message);}
void hr(HRESULT value){winrt::check_hresult(value);}
bool compositionUnavailable(std::int32_t code){return static_cast<std::uint32_t>(code)==0x887A0022u;} // DXGI_ERROR_NOT_CURRENTLY_AVAILABLE
struct Queue {
    winrt::Windows::System::DispatcherQueueController controller{nullptr};
    Queue(){const DispatcherQueueOptions options{sizeof(DispatcherQueueOptions),DQTYPE_THREAD_CURRENT,DQTAT_COM_STA};hr(CreateDispatcherQueueController(options,reinterpret_cast<ABI::Windows::System::IDispatcherQueueController**>(winrt::put_abi(controller))));}
    ~Queue(){try{auto operation=controller.ShutdownQueueAsync();const auto end=GetTickCount64()+10000;while(operation.Status()==winrt::Windows::Foundation::AsyncStatus::Started){if(GetTickCount64()>end)std::terminate();MSG m;while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}MsgWaitForMultipleObjectsEx(0,nullptr,10,QS_ALLINPUT,MWMO_INPUTAVAILABLE);}operation.GetResults();}catch(...){std::terminate();}}
};
struct Point {double x{},y{};};
// The route window lies outside every monitor, so Windows answers the host's
// leave tracking at once; the host pump below drops those notices (and only
// those) as the test's own pump does.
HWND offscreenWindow{};
LRESULT CALLBACK dropOffscreenLeaves(int code,WPARAM remove,LPARAM value){
    if(code==HC_ACTION&&remove==PM_REMOVE){auto&m=*reinterpret_cast<MSG*>(value);if(m.hwnd==offscreenWindow&&m.message==WM_MOUSELEAVE&&m.wParam!=0x50524F4A)m.message=WM_NULL;}
    return CallNextHookEx(nullptr,code,remove,value);
}
// The source brush colour of this fixture (sRGB .98,.83,.12, opaque) in the
// BGRA8 readback, with room for antialiasing and the encoding round trip.
bool inked(const n::Readback&image,unsigned x,unsigned y){
    if(x>=image.width||y>=image.height)return false;
    const auto*p=&image.pixels[std::size_t(y)*image.rowBytes+std::size_t(x)*4];
    return p[3]>=250&&p[2]>=220&&p[1]>=170&&p[0]<=90;
}

void run(const wchar_t*shader,const std::filesystem::path&resources){
    Queue queue;
    a::OverlayHost host;
    t::ProjectionWorkspace*shown{}; // Application::Impl::projectionShown()
    double instant=10;unsigned losses{},strokeLosses{};
    // The latency section below runs the whole production frame route:
    // Application::Impl::refresh() hands the host the workspace's frame
    // demand after every event and frame, and invalidates.
    bool frameRoute{};
    // Nested callbacks (a WM_CAPTURECHANGED sent from inside capturePointer)
    // are handed later times, as ModuleRegistry::hostTime hands them.
    const auto later=[&]{return instant+=.004;};
    a::OverlayCallbacks route;
    route.pointer=[&](const a::PointerEvent&e){
        if(!shown||!shown->presented())return false;
        if(e.kind==a::PointerKind::captureLost){++losses;if(shown->preview()->pointerLocked())++strokeLosses;}
        // Application::Impl::onPointer, Projection branch.
        bool handled{},locked{};
        handled=shown->pointer(e,later());locked=shown->preview()->pointerLocked();(void)handled;
        host.capturePointer(locked);
        if(frameRoute){host.setFrameDemand(shown->demand(later()));host.invalidate();}
        return true;
    };
    // Application::Impl::onWheel, Projection branch.
    route.wheel=[&](const a::WheelEvent&e){if(!shown||!shown->presented())return false;(void)shown->wheel(e,later());return true;};
    // Application::Impl::callbacks(): every presented frame waits for the
    // swap chain's admission in the host's pump, with its input.
    n::Renderer renderer;
    route.presentationWaitable=[&]{return renderer.presentationWaitable();};
    route.presentationHeld=[&]{return renderer.presentationHeld();};
    route.presentationAdmitted=[&]{renderer.presentationAdmitted();};
    // Application::Impl::onFrame -> present(), Projection branch: the frame
    // start's bounded admission, then the workspace's render and Present.
    unsigned presentedFrames{};
    route.frame=[&](double){if(!shown||!shown->presented())return;(void)renderer.waitForPresentation(50);(void)shown->render(later(),true);++presentedFrames;
        if(frameRoute)host.setFrameDemand(shown->demand(later()));};
    // Right of the whole virtual screen: shown, but on no monitor.
    const int left=GetSystemMetrics(SM_XVIRTUALSCREEN)+GetSystemMetrics(SM_CXVIRTUALSCREEN)+2048,top=GetSystemMetrics(SM_YVIRTUALSCREEN);
    host.create({L"Off-screen synthetic Projection input route",left,top,1280,800,{}},std::move(route));
    const auto window=static_cast<HWND>(host.hwnd());const auto metrics=host.metrics();
    RECT bounds{};check(GetWindowRect(window,&bounds)&&!MonitorFromRect(&bounds,MONITOR_DEFAULTTONULL),"The route window lies outside every monitor");
    try{renderer.initialize(window,metrics.pixelWidth,metrics.pixelHeight,{n::Driver::warpForTests,shader,n::RenderTarget::composition});}
    catch(const n::RendererError&e){if(compositionUnavailable(e.code()))throw Unavailable{};throw;}
    // Application::Impl construction in a visible mode: the owned HWND's host
    // backdrop flag, then the lower system backdrop.
    const BOOL off=FALSE;hr(DwmSetWindowAttribute(window,DWMWA_USE_HOSTBACKDROPBRUSH,&off,sizeof(off)));
    n::DesktopBackdrop backdrop;backdrop.initialize(window,{metrics.pixelWidth,metrics.pixelHeight,true,false,.5,.5,0},{false});
    n::LayerRasterizer raster;
    // The HUD's design environment the workspace chrome takes (palette, measurer).
    t::DesignOverlayOptions designOptions;designOptions.raster.pixelsPerPoint=2;designOptions.window=window;t::DesignOverlayOwner overlay(raster,designOptions);
    const n::NotesImageRoute mediaRoute{window,WM_APP+250,1};
    n::NativeNotesImageDecoder decoder([](const auto&)->n::NotesImageAccess{throw std::runtime_error("The drawing route reads no media");},mediaRoute);
    n::NativeNotesImagePlayback images(decoder);n::NativeMediaRequestBroker broker(decoder,images,mediaRoute);
    std::vector<std::string>events; // OverlayController.recordProjectionEvent names (the Event Log rows)
    {
        t::ProjectionWorkspace projection(host,renderer,raster,backdrop,broker,{.98,.83,.12,1},.5,.5);
        struct Unroute {t::ProjectionWorkspace*&value;~Unroute(){value=nullptr;}} unroute{shown}; // before the workspace closes
        t::ProjectionPreviewOptions options;
        options.raster.assetRoot=resources/"notes-format";options.raster.pixelsPerPoint=2;options.raster.paddingPoints=1;options.design=&overlay.context().env();
        options.event=[&](const t::ProjectionPreviewEvent&e){events.emplace_back(t::projectionEventAction(e));};
        t::ProjectionMediaBindingOptions binding;
        binding.resolve=[](const auto&)->n::NotesImageAccess{throw std::runtime_error("Unexpected media read");};
        binding.errorText=[](HRESULT){return std::string("Synthetic error");};
        // projectionCommand(showProjection): resize and show on the hidden
        // host, then the window shows (here without taking the foreground).
        projection.resize(metrics,0);projection.show(options,std::move(binding),later(),false);shown=&projection;
        host.show(false);
        check(IsWindowVisible(window)&&host.stats().visible,"The workspace window is shown, as OverlayHost::capturePointer requires");

        const double scale=metrics.scale,width=metrics.width,height=metrics.height;
        // The message loop of OverlayHost::pumpOnce (TranslateMessage and
        // DispatchMessage; its only filter is for keys), then the host's own
        // pump for anything left and for a callback failure. One difference:
        // the host starts leave tracking (TrackMouseEvent) on a mouse message,
        // and Windows answers at once with WM_MOUSELEAVE because the real
        // cursor is never over this off-screen window. In EndfieldHUD.exe the
        // cursor is over the screen-sized workspace while it draws, so those
        // notices are dropped; the test posts its own leave (marked) instead.
        constexpr WPARAM postedLeave=0x50524F4A;unsigned droppedLeaves{};
        const auto pump=[&]{
            for(unsigned k=0;k<1024;++k){MSG message{};if(!PeekMessageW(&message,nullptr,0,0,PM_REMOVE))break;
                if(message.message==WM_QUIT){PostQuitMessage(static_cast<int>(message.wParam));break;}
                if(message.message==WM_MOUSELEAVE&&message.hwnd==window&&message.wParam!=postedLeave){++droppedLeaves;continue;}
                TranslateMessage(&message);DispatchMessageW(&message);}
            check(host.pumpOnce(0),"The host keeps running");};
        const auto pixel=[&](double value){return static_cast<WORD>(static_cast<short>(std::lround(value*scale)));};
        const auto send=[&](UINT message,WPARAM keys,Point p){check(PostMessageW(window,message,message==WM_MOUSELEAVE?postedLeave:keys,MAKELPARAM(pixel(p.x),pixel(p.y)))!=FALSE,"Post a mouse message to the route window");pump();};
        const auto click=[&](Point p){send(WM_MOUSEMOVE,0,p);send(WM_LBUTTONDOWN,MK_LBUTTON,p);send(WM_LBUTTONUP,0,p);};
        const auto rightClick=[&](Point p){send(WM_MOUSEMOVE,0,p);send(WM_RBUTTONDOWN,MK_RBUTTON,p);send(WM_RBUTTONUP,0,p);};
        const auto wheel=[&](Point p,int detents){POINT screen{std::lround(p.x*scale),std::lround(p.y*scale)};check(ClientToScreen(window,&screen)!=FALSE,"Wheel position");
            check(PostMessageW(window,WM_MOUSEWHEEL,MAKEWPARAM(0,static_cast<WORD>(static_cast<short>(detents*WHEEL_DELTA))),MAKELPARAM(static_cast<WORD>(static_cast<short>(screen.x)),static_cast<WORD>(static_cast<short>(screen.y))))!=FALSE,"Post a wheel message");pump();};
        const auto control=[&](std::string_view id){std::vector<n::NotesAutomationElement>peers;projection.preview()->automationElements(peers);
            const auto found=std::find_if(peers.begin(),peers.end(),[&](const auto&e){return e.id==id;});check(found!=peers.end(),"The workspace lists "+std::string(id));
            return Point{(found->rect.x+found->rect.width/2)/scale,(found->rect.y+found->rect.height/2)/scale};};
        const auto paint=[&](double advance){instant+=advance;check(projection.render(instant,false),"The workspace renders");renderer.draw(false);return renderer.readback();};
        // The canvas below the toolbar, its message line and the brush menu.
        const auto canvasTop=static_cast<unsigned>(std::ceil(std::max(height*.45,240.)*scale));
        const auto inkedPixels=[&](const n::Readback&image){std::size_t count{};for(unsigned y=canvasTop;y<image.height;++y)for(unsigned x=0;x<image.width;++x)count+=inked(image,x,y);return count;};

        // The empty workspace, without the brush cursor (the pointer left).
        send(WM_MOUSELEAVE,0,{});
        const auto baseline=paint(.3);
        check(baseline.width==metrics.pixelWidth&&baseline.height==metrics.pixelHeight&&inkedPixels(baseline)==0,"The empty canvas reads back without ink");

        // The brush changes of the live report, each one through the route.
        const auto eraser=control("projection.toolbar:eraser");
        click(eraser);check(projection.model().erasing(),"The toolbar eraser switches to erasing");
        click(eraser);check(!projection.model().erasing(),"Clicking it again restores the pen");
        const Point centre{width*.5,height*.7};
        rightClick(centre);check(projection.model().erasing(),"A right click on the canvas switches to erasing");
        rightClick(centre);check(!projection.model().erasing(),"A second right click restores the pen");
        const double initialWidth=projection.model().brushWidth();
        // Mac 1.3.0: the wheel's change is committed with the next canvas press (a wheel has no phase end).
        wheel(centre,1);check(projection.model().brushWidth()==initialWidth+1,"A wheel detent over the canvas widens the brush");
        click(control("projection.toolbar:brush"));check(projection.preview()->menuOpen(),"The brush button opens its pop-over");paint(.3);
        check(projection.preview()->setAutomationValue("projection.menu:brush",initialWidth+2,later())&&projection.model().brushWidth()==initialWidth+2,"The pop-over's slider widens the brush again");
        click(control("projection.menu:popover.close"));check(!projection.preview()->menuOpen(),"Its close button dismisses the pop-over");paint(.3);
        check(events==std::vector<std::string>(5,"brushChanged"),"Every brush change is recorded as brushChanged, as in the live Event Log (the wheel's waits for the next press)");
        check(!projection.model().erasing()&&projection.model().drawing().strokes().empty()&&!projection.preview()->pointerLocked()&&GetCapture()!=window,"The pen is selected, nothing is drawn and no capture is held");

        // One stroke: press, four drag events with the button held, release.
        const std::array<Point,5>path{{{width*.25,height*.62},{width*.35,height*.66},{width*.45,height*.70},{width*.55,height*.74},{width*.65,height*.78}}};
        std::vector<std::string>failures;const auto expect=[&](bool value,std::string message){++checks;if(!value)failures.push_back(std::move(message));};
        send(WM_MOUSEMOVE,0,path[0]);const auto lossesBefore=losses;
        const auto state=[&]{std::ostringstream s;s<<"capture losses while the stroke was held: "<<strokeLosses<<", capture losses: "<<losses-lossesBefore<<", window holds the capture: "<<(GetCapture()==window)<<", stroke held: "<<projection.preview()->pointerLocked()<<", off-screen leave notices dropped: "<<droppedLeaves;return s.str();};
        send(WM_LBUTTONDOWN,MK_LBUTTON,path[0]);
        expect(projection.preview()->pointerLocked()&&GetCapture()==window&&losses==lossesBefore,"The canvas press starts a stroke and the host takes the capture of the shown window ("+state()+")");
        for(std::size_t k=1;k<path.size();++k){send(WM_MOUSEMOVE,MK_LBUTTON,path[k]);if(k==1)expect(strokeLosses==0&&losses==lossesBefore,"The first drag event keeps the capture ("+state()+")");}
        expect(strokeLosses==0&&losses==lossesBefore,"No capture loss reaches the canvas during the drag ("+state()+")");
        expect(projection.preview()->pointerLocked()&&GetCapture()==window,"The drag keeps its stroke and the host capture ("+state()+")");
        send(WM_LBUTTONUP,0,path.back());
        const auto strokes=projection.model().drawing().strokes();
        expect(strokes.size()==1&&strokes.front().points.size()==path.size()&&strokes.front().width==initialWidth+2&&strokes.front().color==projection.model().color(),
            "Releasing commits the stroke with every point and the changed brush (committed strokes: "+std::to_string(strokes.size())+", points: "+std::to_string(strokes.empty()?0:strokes.front().points.size())+")");
        expect(!projection.preview()->pointerLocked()&&GetCapture()!=window,"The release ends the gesture and the capture");
        expect(events.size()==7&&events[5]=="brushChanged"&&events.back()=="drawingEdited","The press commits the wheel's brush change and the stroke is recorded as drawingEdited (projectionAction rows: "+std::to_string(events.size())+")");

        // The readback, again without the brush cursor.
        send(WM_MOUSELEAVE,0,{});
        const auto image=paint(.3);
        std::size_t samples{},onPath{},before{};double length{};
        for(std::size_t k=1;k<path.size();++k){const auto&p=path[k-1];const auto&q=path[k];length+=std::hypot(q.x-p.x,q.y-p.y);
            for(const double f:{.25,.5,.75}){const auto x=static_cast<unsigned>(std::lround((p.x+(q.x-p.x)*f)*scale)),y=static_cast<unsigned>(std::lround((p.y+(q.y-p.y)*f)*scale));++samples;onPath+=inked(image,x,y);before+=inked(baseline,x,y);}}
        const auto ink=inkedPixels(image);const double brush=(initialWidth+2)*scale;
        expect(before==0&&onPath==samples,"The readback shows the stroke along its whole path ("+std::to_string(onPath)+" of "+std::to_string(samples)+" samples inked)");
        expect(static_cast<double>(ink)>=length*scale*brush*.5,"The stroke covers its brush width in the readback ("+std::to_string(ink)+" ink pixels, at least "+std::to_string(static_cast<long long>(length*scale*brush*.5))+" required)");
        if(!failures.empty()){std::string text="Projection drew nothing through the production route:";for(const auto&f:failures)text+="\n  - "+f;throw std::runtime_error(text);}

        // A real capture loss (the capture taken away mid-stroke) still
        // discards only the live ink.
        const auto committed=strokes.size();const auto rows=events.size();const auto realLosses=losses;
        send(WM_MOUSEMOVE,0,{width*.3,height*.85});send(WM_LBUTTONDOWN,MK_LBUTTON,{width*.3,height*.85});send(WM_MOUSEMOVE,MK_LBUTTON,{width*.4,height*.88});
        check(projection.preview()->pointerLocked()&&GetCapture()==window,"A second press holds a new stroke under the capture");
        check(ReleaseCapture()!=FALSE,"Release the window's capture");pump();
        check(losses==realLosses+1&&!projection.preview()->pointerLocked()&&GetCapture()!=window,"A real capture loss reaches the canvas once and ends the stroke");
        send(WM_MOUSEMOVE,MK_LBUTTON,{width*.5,height*.9});send(WM_LBUTTONUP,0,{width*.5,height*.9});
        check(projection.model().drawing().strokes().size()==committed&&events.size()==rows,"Nothing is committed after a real capture loss");

        // Drawing latency (live RC 130a: "the brush is not following the cursor
        // in real time"). RC 128's Projection frames never waited for the swap
        // chain: 193 frames/s went to a 165 Hz display and ~35 of them (200 ms)
        // queued between the pointer and the screen. Every presented frame now
        // holds the swap chain's admission (at most one frame queued); the
        // host's pump waits for it while dispatching input; every burst of drag
        // samples reaches the screen in the very next presented frame, drawn
        // once for all of its samples, which the stroke keeps.
        {
            check(renderer.stats().frameLatencyWaitable&&renderer.presentationWaitable(),"The composition swap chain offers its frame latency object to the host pump");
            offscreenWindow=window;struct Hook{HHOOK h{};~Hook(){if(h)UnhookWindowsHookEx(h);offscreenWindow=nullptr;}}hook;
            hook.h=SetWindowsHookExW(WH_GETMESSAGE,dropOffscreenLeaves,nullptr,GetCurrentThreadId());check(hook.h!=nullptr,"Thread-local hook for the off-screen leave notices");
            // OverlayHost::pumpOnce itself, as EndfieldHUD.exe runs it.
            const auto hostPump=[&](double seconds,const auto&until){const auto end=a::OverlayHost::clockNow()+seconds;while(!until()&&a::OverlayHost::clockNow()<end)check(host.pumpOnce(5),"The host keeps running");return until();};
            const auto settled=[&]{const auto s=host.stats();return !s.framePending&&!s.presentationGated;};
            frameRoute=true;host.setFrameDemand(projection.demand(later()));
            check(hostPump(2.,settled),"The workspace settles after the earlier strokes");
            const auto start=renderer.stats();
            const auto post=[&](UINT message,WPARAM keys,Point p){check(PostMessageW(window,message,keys,MAKELPARAM(pixel(p.x),pixel(p.y)))!=FALSE,"Post a mouse message to the route window");};
            const Point origin{width*.2,height*.55};
            post(WM_MOUSEMOVE,0,origin);post(WM_LBUTTONDOWN,MK_LBUTTON,origin);
            check(hostPump(2.,[&]{return projection.preview()->pointerLocked()&&renderer.stats().presents>start.presents&&settled();}),"The press starts a stroke and presents it");
            constexpr unsigned bursts=8,samples=6;std::vector<Point>path{origin};
            for(unsigned b=0;b<bursts;++b){
                const auto before=renderer.stats();const auto frames=presentedFrames;
                // One frame's worth of a 1000 Hz mouse, all queued before the
                // host reads any of it.
                std::vector<Point>burst;
                for(unsigned k=1;k<=samples;++k){const double step=double(b*samples+k);burst.push_back({origin.x+step*width*.006,origin.y+std::sin(step*.15)*height*.05});post(WM_MOUSEMOVE,MK_LBUTTON,burst.back());}
                path.insert(path.end(),burst.begin(),burst.end());
                check(hostPump(2.,[&]{return renderer.stats().presents>before.presents&&settled();}),"The burst reaches a presented frame");
                const auto after=renderer.stats();
                check(after.presents==before.presents+1&&presentedFrames==frames+1,"All samples of one frame's input are drawn by one frame, not one frame per sample");
                check(after.unadmittedPresents==before.unadmittedPresents,"That frame presents with the swap chain's admission: no frame queues ahead of the display");
                // Nothing changed since that Present: a render-only readback of
                // the same state shows what it presented.
                const auto image=paint(0);
                const auto inkedBetween=[&](Point p,Point q){return inked(image,static_cast<unsigned>(std::lround((p.x+q.x)*.5*scale)),static_cast<unsigned>(std::lround((p.y+q.y)*.5*scale)));};
                check(inkedBetween(path[path.size()-samples-1],burst.front())&&inkedBetween(burst[samples-2],burst.back()),"The very next presented frame shows the burst's first and newest segments");
            }
            post(WM_LBUTTONUP,0,path.back());
            check(hostPump(2.,[&]{return !projection.preview()->pointerLocked()&&settled();}),"The release commits the stroke");
            const auto strokes=projection.model().drawing().strokes();
            check(!strokes.empty()&&strokes.back().points.size()==path.size(),"The committed stroke keeps every intermediate sample ("+std::to_string(strokes.empty()?0:strokes.back().points.size())+" of "+std::to_string(path.size())+" points)");
            check(renderer.stats().unadmittedPresents==start.unadmittedPresents,"No frame of the stroke presented without the swap chain's admission");
            // Nothing changes: no frame, no present, no wake.
            const auto idleHost=host.stats();const auto idleRenderer=renderer.stats();
            hostPump(.5,[]{return false;});
            check(host.stats().frames==idleHost.frames&&renderer.stats().presents==idleRenderer.presents&&host.stats().timerWakes==idleHost.timerWakes&&host.stats().presentationWaits==idleHost.presentationWaits,"An idle workspace presents nothing and wakes nothing");
        }
        frameRoute=false;
        shown=nullptr;projection.dismiss(later(),true);projection.close(later());
        check(!projection.presented()&&!IsWindowVisible(window)&&renderer.stats().textures==0&&renderer.stats().meshes==0,"Closing hides the window and releases every Projection resource");
    }
    decoder.stop();backdrop.reset();renderer.reset();host.destroy();
}
}

int wmain(int argc,wchar_t**argv){
    const auto com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    try{
        hr(com);check(argc==3,"Pass the HLSL path and the original resources root");
        run(argv[1],std::filesystem::absolute(argv[2]));
        CoUninitialize();std::cout<<"PASS "<<checks<<" Projection production-route drawing checks\n";return 0;
    }catch(const Unavailable&){
        if(SUCCEEDED(com))CoUninitialize();
        std::cout<<"SKIPPED: no composition swap chain in this session (DXGI_ERROR_NOT_CURRENTLY_AVAILABLE); run it in an interactive desktop session\n";return unavailableExit;
    }catch(const winrt::hresult_error&e){
        if(SUCCEEDED(com))CoUninitialize();
        std::cerr<<"FAIL after "<<checks<<": "<<winrt::to_string(e.message())<<" HRESULT "<<std::hex<<static_cast<std::uint32_t>(e.code().value)<<'\n';return 1;
    }catch(const std::exception&e){
        if(SUCCEEDED(com))CoUninitialize();
        std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;
    }
}
#else
int main(){return 0;}
#endif
