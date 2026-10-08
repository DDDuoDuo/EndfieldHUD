#include "app/overlay_host.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>
#include <limits>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#include <chrono>
#include <thread>
#endif

using namespace endfield::app;
using namespace endfield::core;
namespace {
unsigned checks{};
void check(bool condition, const char* message) {
    ++checks; if (!condition) throw std::runtime_error(message);
}
void checkNear(double a, double b, const char* message) { check(std::abs(a - b) < 1e-10, message); }
FrameDemand visibleDemand() {
    FrameDemand demand; demand.phase = VisibilityPhase::visible;
    demand.presented = demand.onScreen = true;
    return demand;
}

// Compare presentation times with the ORIGINAL gate one source tick at a time,
// including every phase, power setting, and all four ways it can be suppressed.
// This detects a30Hz first-tick phase shift and losing finite frames, not merely
// whether our host reports a preferred timer interval.
void scheduling() {
    for (int phase = 0; phase < 4; ++phase) {
        for (unsigned bits = 0; bits < 512; ++bits) {
            auto demand = visibleDemand();
            demand.phase = static_cast<VisibilityPhase>(phase);
            demand.presented = (bits & 1) != 0;
            demand.onScreen = (bits & 2) != 0;
            demand.canAdvanceTransition = (bits & 4) != 0;
            demand.pendingOpening = (bits & 8) != 0;
            demand.finiteAnimation = (bits & 16) != 0;
            demand.ambientEnabled = (bits & 32) != 0;
            demand.reduceMotion = (bits & 64) != 0;
            demand.lowPower = (bits & 128) != 0;
            const bool shown = (bits & 256) != 0;
            FrameWakePlan plan;
            FrameDemandGate source;
            auto gated = demand; gated.presented = demand.presented && shown;
            const auto original = source.refresh(gated);
            auto wake = plan.refresh(demand, shown);
            check(wake.present == original.present && plan.canPresent() == original.present, "refresh matches original source demand");
            check(wake.afterSeconds.has_value() == original.timerInterval.has_value(), "timer eligibility matches source");
            if (!original.timerInterval) {
                for (unsigned i = 0; i < 10; ++i) {
                    const auto idle = plan.wake();
                    check(!idle.present && !idle.afterSeconds, "closed/reduced/idle cannot invent callbacks");
                }
                continue;
            }
            double sourceTime = 0, hostTime = 0;
            unsigned expectedFrames = 0, wakes = 0;
            for (unsigned i = 0; i < 180; ++i) {
                sourceTime += *original.timerInterval;
                if (!source.tick(gated).present) continue;
                check(wake.afterSeconds.has_value(), "live presentation has one scheduled wake");
                hostTime += *wake.afterSeconds;
                wake = plan.wake(); ++wakes; ++expectedFrames;
                check(wake.present, "each host wake actually presents");
                checkNear(sourceTime, hostTime, "source presentation phase and times preserved exactly");
            }
            check(wakes == expectedFrames, "no empty ambient timer wake");
            plan.cancel();
            check(!plan.canPresent() && !plan.interval() && !plan.wake().present, "cancellation blocks stale timer signal");
        }
    }
    // Finite motion changes gate parity. Refresh must preserve that parity when
    // switching to ambient, including whether the next source tick is skipped.
    for (unsigned finiteTicks = 0; finiteTicks < 5; ++finiteTicks) {
        auto demand = visibleDemand(); demand.finiteAnimation = true;
        FrameDemandGate source; FrameWakePlan plan;
        source.refresh(demand); plan.refresh(demand, true);
        for (unsigned i = 0; i < finiteTicks; ++i) {
            check(source.tick(demand).present && plan.wake().present, "finite ticks preserved before ambient transition");
        }
        demand.finiteAnimation = false; demand.ambientEnabled = true;
        auto wake = plan.refresh(demand, true); source.refresh(demand);
        double expectedTime = 0, actualTime = 0;
        for (unsigned i = 0; i < 20; ++i) {
            expectedTime += 1.0 / 60;
            if (!source.tick(demand).present) continue;
            actualTime += *wake.afterSeconds; wake = plan.wake();
            check(wake.present, "ambient remains a useful wake after finite transition");
            checkNear(expectedTime, actualTime, "finite to ambient retains original source parity");
        }
    }
    auto demand = visibleDemand(); demand.ambientEnabled = true;
    FrameWakePlan plan;
    checkNear(*plan.refresh(demand, true).afterSeconds, 1.0 / 60, "first ambient frame retains source half-period");
    checkNear(*plan.wake().afterSeconds, 1.0 / 30, "subsequent ambient wake cadence30Hz");
    demand.lowPower = true;
    check(!plan.refresh(demand, true).afterSeconds, "low power disables stable ambient");
    demand.finiteAnimation = true;
    checkNear(*plan.refresh(demand, true).afterSeconds, 1.0 / 30, "low power finite cadence30Hz");
    demand.lowPower = false;
    checkNear(*plan.refresh(demand, true).afterSeconds, 1.0 / 60, "normal finite cadence60Hz");
    check(!plan.refresh(demand, false).present && !plan.interval(), "hidden overrides even finite demanded motion");
}
void coalescing() {
    FrameRequestBatch batch;
    for (unsigned i = 0; i < 1000; ++i) check(!batch.request(false), "hidden input never queues a frame");
    const auto first = batch.request(true);
    check(first && batch.pending(), "first allowed invalidation queues a frame");
    for (unsigned i = 0; i < 1000; ++i) check(!batch.request(true), "pointer flood coalesces into one pending frame");
    check(batch.consume(*first, true) && !batch.pending(), "batch consumed once");
    const auto second = batch.request(true);
    check(second && !batch.consume(*first, true) && batch.pending(), "duplicate old frame cannot consume the newer request");
    batch.cancel();
    const auto reopened = batch.request(true);
    check(reopened && !batch.consume(*second, true) && batch.pending(), "queued old opening cannot consume reopened frame");
    check(batch.consume(*reopened, true), "reopening can present its own frame");
    const auto concealed = batch.request(true);
    check(concealed && !batch.consume(*concealed, false) && !batch.pending(), "concealment drops already queued frame");
}
void deadlines() {
    WakeDeadlines clock;
    check(!clock.next(),"No module or animation work means no wake");
    clock.setFrame(1);clock.setExternal(.25);
    checkNear(*clock.next(),.25,"Media deadline shares earlier wake");
    auto due=clock.takeDue(.25);
    check(due.external&&!due.frame,"Media wake cannot advance an animation tick");
    checkNear(*clock.next(),1,"One-shot media callback preserves animation phase");
    check(!clock.takeDue(.25).external,"Consumed deadline cannot repeat");
    clock.setExternal(2);due=clock.takeDue(1);
    check(due.frame==1&&!due.external,"Animation wake leaves later media sleeping");
    clock.setFrame(2);due=clock.takeDue(3);
    check(due.frame==2&&due.external&&!clock.next(),"Overdue reasons coalesce once without catch-up callbacks");
    clock.setExternal(600);clock.setFrame({});
    checkNear(*clock.next(),600,"Long-delay GIF retains its true deadline, not a frame cadence");
    clock.clear();check(!clock.next(),"Shutdown removes both reasons");
    for(double invalid:{-1.,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}){
        bool rejected{};try{clock.setExternal(invalid);}catch(const std::invalid_argument&){rejected=true;}
        check(rejected&&!clock.next(),"Invalid deadline cannot mutate schedule");
    }
}

#ifdef _WIN32
void drain(OverlayHost& host) { for (unsigned i = 0; i < 8; ++i) host.pumpOnce(0); }
std::wstring ownObjectName(HANDLE object){
    DWORD bytes{};GetUserObjectInformationW(object,UOI_NAME,nullptr,0,&bytes);
    check(bytes>=sizeof(wchar_t)&&bytes<=4096,"Owned desktop/station name is bounded");
    std::wstring result(bytes/sizeof(wchar_t),L'\0');
    check(GetUserObjectInformationW(object,UOI_NAME,result.data(),bytes,&bytes)!=FALSE,"Read owned desktop/station identity");
    result.resize(std::char_traits<wchar_t>::length(result.c_str()));return result;
}
void hiddenStartupChild(){
    // This branch must never show a window on the user's active desktop, even
    // if someone invokes its command-line flag outside the isolated parent.
    check(ownObjectName(GetThreadDesktop(GetCurrentThreadId())).starts_with(L"EndfieldHUDHiddenStartup."),"Show regression runs only on its separate test desktop");
    STARTUPINFOW startup{};startup.cb=sizeof(startup);GetStartupInfoW(&startup);
    check((startup.dwFlags&STARTF_USESHOWWINDOW)&&startup.wShowWindow==SW_HIDE,"Owned child really inherited hidden startup state");
    unsigned frames{},focusEvents{};OverlayCallbacks callbacks;
    callbacks.frame=[&](double){++frames;};callbacks.focus=[&](bool){++focusEvents;};
    OverlayHost host;host.create({L"Isolated hidden-startup gate",11,13,48,40,nullptr},callbacks);
    const auto window=static_cast<HWND>(host.hwnd());RECT before{};check(GetWindowRect(window,&before)!=FALSE,"Read own initial rectangle");
    const auto style=GetWindowLongPtrW(window,GWL_STYLE),extended=GetWindowLongPtrW(window,GWL_EXSTYLE);const auto focus=GetFocus(),active=GetActiveWindow();
    auto demand=visibleDemand();host.setFrameDemand(demand);
    check(!host.stats().framePending&&!host.stats().timerArmed,"Visible demand while hidden schedules no work");
    host.show(false); // Must be this process's FIRST visibility request.
    check(IsWindowVisible(window)&&host.stats().visible,"Explicit show overrides inherited STARTUPINFO SW_HIDE");
    check(host.stats().framePending,"Completed native show wakes an already-stable frame demand");
    RECT after{};check(GetWindowRect(window,&after)!=FALSE,"Read own shown rectangle");
    check(EqualRect(&before,&after)!=FALSE,"Show does not reposition or resize the overlay");
    check((GetWindowLongPtrW(window,GWL_STYLE)&~LONG_PTR(WS_VISIBLE))==(style&~LONG_PTR(WS_VISIBLE))&&GetWindowLongPtrW(window,GWL_EXSTYLE)==extended,"Show preserves borderless/composition/topmost styles");
    check(GetFocus()==focus&&GetActiveWindow()==active&&focusEvents==0&&!host.stats().focused,"Show(false) preserves activation and keyboard focus");
    drain(host);check(frames>0&&!host.stats().timerArmed,"Stable first frame renders without adding an idle timer");
    host.hide();check(!IsWindowVisible(window)&&!host.stats().framePending,"Hide cancels pending presentation");
    check(SetWindowPos(window,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE)!=FALSE,"Set test-owned topmost policy on isolated desktop");
    host.show(false);check(GetWindowLongPtrW(window,GWL_EXSTYLE)&WS_EX_TOPMOST,"Explicit reopening preserves caller-owned topmost policy");
    drain(host);host.hide();host.destroy();
}
void hiddenStartupProcess(){
    // A child bound to a new desktop cannot initialize reliably from the
    // OpenSSH Session-0 service station. Run this explicit additional gate on
    // an interactive station; its private desktop is NEVER switched/displayed.
    // The normal suite still exercises every regular hidden-host contract.
    USEROBJECTFLAGS stationFlags{};DWORD read{};DWORD session{};
    check(GetUserObjectInformationW(GetProcessWindowStation(),UOI_FLAGS,&stationFlags,sizeof(stationFlags),&read)!=FALSE,"Read current test window-station capability");
    check(ProcessIdToSessionId(GetCurrentProcessId(),&session)!=FALSE&&session!=0&&(stationFlags.dwFlags&WSF_VISIBLE),"--interactive-startup requires an interactive window station; Session-0 service station is unsupported");
    const auto original=GetThreadDesktop(GetCurrentThreadId());
    const auto name=L"EndfieldHUDHiddenStartup."+std::to_wstring(GetCurrentProcessId())+L"."+std::to_wstring(GetTickCount64());
    const auto station=ownObjectName(GetProcessWindowStation());
    struct Desktop {HDESK value{};~Desktop(){if(value)CloseDesktop(value);}} desktop;
    desktop.value=CreateDesktopW(name.c_str(),nullptr,nullptr,0,GENERIC_ALL,nullptr);check(desktop.value!=nullptr,"Create separate never-switched desktop for isolated startup regression");
    if(GetThreadDesktop(GetCurrentThreadId())!=original)check(SetThreadDesktop(original)!=FALSE,"Restore parent thread desktop after test desktop creation");
    std::wstring path(32768,L'\0');const auto length=GetModuleFileNameW(nullptr,path.data(),static_cast<DWORD>(path.size()));check(length>0&&length<path.size(),"Get only this test executable path");path.resize(length);
    auto command=L"\""+path+L"\" --hidden-startup-child";auto desktopPath=station+L"\\"+name;
    STARTUPINFOW startup{};startup.cb=sizeof(startup);startup.lpDesktop=desktopPath.data();startup.dwFlags=STARTF_USESHOWWINDOW|STARTF_FORCEOFFFEEDBACK;startup.wShowWindow=SW_HIDE;
    struct Process {PROCESS_INFORMATION value{};~Process(){if(value.hProcess){if(WaitForSingleObject(value.hProcess,0)==WAIT_TIMEOUT){TerminateProcess(value.hProcess,2);WaitForSingleObject(value.hProcess,1000);}CloseHandle(value.hProcess);}if(value.hThread)CloseHandle(value.hThread);}} process;
    check(CreateProcessW(path.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process.value)!=FALSE,"Start owned hidden child on isolated desktop");
    check(WaitForSingleObject(process.value.hProcess,10000)==WAIT_OBJECT_0,"Isolated show/frame regression completes within its finite deadline");
    DWORD exit{};check(GetExitCodeProcess(process.value.hProcess,&exit)!=FALSE&&exit==0,"Hidden-startup visibility and stable-demand child contracts pass");
    check(GetThreadDesktop(GetCurrentThreadId())==original,"Regression leaves parent thread desktop untouched");
}
void nativeDeadlines() {
    OverlayHost host;unsigned wakes{},frames{};
    OverlayCallbacks callbacks;
    callbacks.frame=[&](double){++frames;};
    callbacks.deadline=[&](double time){++wakes;if(wakes==1)host.setDeadline(time+.005);};
    host.create({L"Hidden deadline fixture",0,0,16,16,nullptr},callbacks);
    host.setDeadline(OverlayHost::clockNow()+.005);
    host.hide();
    const auto limit=OverlayHost::clockNow()+3;
    while(wakes<2&&OverlayHost::clockNow()<limit)host.pumpOnce(100);
    check(wakes==2&&frames==0,"Hidden owner deadlines fire once without animation or rendering");
    check(host.stats().externalWakes==2&&!host.stats().timerArmed,"Callback rearm uses the same timer then sleeps");
    host.setDeadline(OverlayHost::clockNow()+600);
    check(host.stats().timerArmed,"Long media deadline arms one sleeping timer");
    host.setDeadline({});
    const auto before=host.stats().timerWakes;for(unsigned k=0;k<4;++k)host.pumpOnce(0);
    check(!host.stats().timerArmed&&host.stats().timerWakes==before,"Cancelling module work consumes stale timer signals");
    host.setDeadline(OverlayHost::clockNow()+600);host.requestStop();
    check(!host.stats().timerArmed&&!host.pumpOnce(0),"Shutdown cancels background deadlines");host.destroy();
    callbacks.deadline=[&](double){throw std::runtime_error("sentinel deadline error");};
    host.create({L"Hidden deadline failure",0,0,16,16,nullptr},callbacks);host.setDeadline(OverlayHost::clockNow());
    bool rejected{};try{for(unsigned n=0;n<8;++n)host.pumpOnce(100);}catch(const std::runtime_error&){rejected=true;}
    check(rejected&&!host.stats().timerArmed&&!host.pumpOnce(0),"Deadline error propagates with all host work stopped");host.destroy();
}
void nativeWindow() {
    OverlayHost host;
    std::vector<PointerEvent> pointer;
    std::vector<WheelEvent> wheel;
    std::vector<KeyEvent> keys;
    std::vector<bool> focus,applicationActivation;
    std::vector<ClientMetrics> sizes;
    unsigned frames = 0, closes = 0, filtered=0, privateMessages=0, displayChanges=0;
    OverlayCallbacks callbacks;
    callbacks.frame = [&](double) { ++frames; };
    callbacks.pointer = [&](const auto& event) { pointer.push_back(event); return true; };
    callbacks.wheel = [&](const auto& event) { wheel.push_back(event); return true; };
    callbacks.key = [&](const auto& event) { keys.push_back(event); return true; };
    callbacks.focus = [&](bool value) { focus.push_back(value); };
    callbacks.applicationActive = [&](bool value) { applicationActivation.push_back(value); };
    callbacks.resize = [&](const auto& value) { sizes.push_back(value); };
    callbacks.closeRequested = [&] { ++closes; host.hide(); };
    callbacks.beforeKeyTranslation=[&](const NativeMessage& event){++filtered;return event.wParam=='D';};
    callbacks.appMessage=[&](const NativeMessage& event)->std::optional<std::intptr_t>{
        if(event.message!=WM_APP+21)return {};check(event.wParam==123,"Private owner notification preserves generation token");++privateMessages;return 45;};
    callbacks.displayChanged=[&]{++displayChanges;};
    const auto cursorBefore = GetCursor();
    host.create({L"Endfield owned hidden host fixture", 0, 0, 128, 96, nullptr}, callbacks);
    const auto window = static_cast<HWND>(host.hwnd());
    check(window && IsWindow(window) && !IsWindowVisible(window), "owned host is created hidden");
    const auto style = GetWindowLongPtrW(window, GWL_STYLE);
    const auto extended = GetWindowLongPtrW(window, GWL_EXSTYLE);
    check((style & WS_POPUP) && !(style & WS_CAPTION), "borderless source composition owner");
    check((extended & WS_EX_NOREDIRECTIONBITMAP) && !(extended & WS_EX_LAYERED), "native DirectComposition transparency without a GDI bitmap");
    check(sizes.size() == 1 && sizes[0] == host.metrics(), "one initial logical client metrics callback");
    auto demand = visibleDemand(); demand.finiteAnimation = true;
    host.setFrameDemand(demand);
    for (unsigned i = 0; i < 1000; ++i) host.invalidate();
    drain(host);
    auto stats = host.stats();
    check(!stats.timerArmed && !stats.framePending && stats.timerArms == 0 && stats.framePosts == 0 && frames == 0,
        "hidden finite demand and invalidation flood schedule no work");
    for (unsigned i = 0; i < 4; ++i) host.pumpOnce(25);
    check(host.stats().timerWakes == 0 && frames == 0, "bounded actual message waits produce no hidden idle callbacks");
    const double scale = host.metrics().scale;
    SendMessageW(window, WM_MOUSEMOVE, MK_SHIFT | MK_CONTROL, MAKELPARAM(-12, 18));
    SendMessageW(window, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(20, 30));
    SendMessageW(window, WM_LBUTTONUP, 0, MAKELPARAM(20, 30));
    SendMessageW(window, WM_XBUTTONDBLCLK, MAKEWPARAM(MK_XBUTTON1, XBUTTON1), MAKELPARAM(21, 31));
    SendMessageW(window, WM_MOUSELEAVE, 0, 0);
    SendMessageW(window, WM_CAPTURECHANGED, 0, 0);
    check(pointer.size() == 6 && pointer[0].kind == PointerKind::move, "native own-window mouse routing");
    checkNear(pointer[0].x, -12 / scale, "negative pointer coordinate stays signed");
    checkNear(pointer[0].y, 18 / scale, "pointer converted to logical client space");
    check(pointer[0].modifiers == (MK_SHIFT | MK_CONTROL), "pointer modifier bits retained");
    check(pointer[1].kind == PointerKind::down && pointer[1].button == PointerButton::left &&
        pointer[2].kind == PointerKind::up && pointer[3].button == PointerButton::extra1 &&
        pointer[3].kind == PointerKind::doubleClick && pointer[4].kind == PointerKind::leave &&
        pointer[5].kind == PointerKind::captureLost, "buttons double-click leave and capture loss retain their meaning");
    POINT ownPoint{24, 36}; ClientToScreen(window, &ownPoint);
    SendMessageW(window, WM_MOUSEWHEEL, MAKEWPARAM(MK_SHIFT, 60), MAKELPARAM(ownPoint.x, ownPoint.y));
    SendMessageW(window, WM_MOUSEHWHEEL, MAKEWPARAM(0, static_cast<WORD>(-120)), MAKELPARAM(ownPoint.x, ownPoint.y));
    check(wheel.size() == 2 && !wheel[0].horizontal && wheel[1].horizontal, "native wheel axes retained");
    checkNear(wheel[0].x, 24 / scale, "wheel screen point becomes own client logical point");
    checkNear(wheel[0].y, 36 / scale, "wheel Y conversion");
    checkNear(wheel[0].steps, .5, "precision wheel fraction retained");
    checkNear(wheel[1].steps, -1, "horizontal wheel direction retained");
    UINT systemLines=3,systemChars=3;SystemParametersInfoW(SPI_GETWHEELSCROLLLINES,0,&systemLines,0);SystemParametersInfoW(SPI_GETWHEELSCROLLCHARS,0,&systemChars,0);
    check(wheel[0].linesPerStep==systemLines&&wheel[1].linesPerStep==systemChars,"Native wheel preserves cached system line/page settings");

    SendMessageW(window, WM_KEYDOWN, 'A', 2 | (0x1e << 16) | (1ull << 30));
    SendMessageW(window, WM_KEYUP, 'A', 1 | (0x1e << 16) | (1ull << 31));
    SendMessageW(window, WM_CHAR, 0x4e2d, 1);
    SendMessageW(window, WM_UNICHAR, 0x1f600, 1);
    check(SendMessageW(window, WM_UNICHAR, UNICODE_NOCHAR, 0) == TRUE, "WM_UNICHAR capability query");
    check(keys.size() == 4 && keys[0].kind == KeyKind::down && keys[0].repeat == 2 &&
        keys[0].scanCode == 0x1e && keys[0].previouslyDown && keys[1].kind == KeyKind::up &&
        keys[2].kind == KeyKind::character && keys[2].value == 0x4e2d &&
        keys[3].kind == KeyKind::unicodeCharacter && keys[3].value == 0x1f600, "key and text payloads are not remapped");
    const auto beforeIME = keys.size();
    SendMessageW(window, WM_IME_ENDCOMPOSITION, 0, 0);
    check(keys.size() == beforeIME, "IME is not intercepted as a shortcut/key event");
    PostMessageW(window,WM_KEYDOWN,'D',1|(0x20<<16));
    PostMessageW(window,WM_KEYUP,'D',1|(0x20<<16));
    drain(host);
    check(filtered==2&&keys.size()==beforeIME,"IME-consumed queued keys never reach TranslateMessage or produce duplicate text");
    OverlayHost unrelated;unrelated.create({L"Other owned hidden key target",0,0,16,16,nullptr});
    PostMessageW(static_cast<HWND>(unrelated.hwnd()),WM_KEYDOWN,'D',1);
    drain(host);check(filtered==2,"The owner key filter does not intercept another window's queued input");unrelated.destroy();
    const auto popup=CreateWindowExW(WS_EX_TOOLWINDOW,L"STATIC",L"Owned preview fixture",WS_POPUP,0,0,16,16,window,nullptr,GetModuleHandleW(nullptr),nullptr);
    check(popup&&!IsWindowVisible(popup),"Create separate hidden owner popup");
    const auto child=CreateWindowExW(0,L"STATIC",L"Preview child",WS_CHILD,0,0,8,8,popup,nullptr,GetModuleHandleW(nullptr),nullptr);
    PostMessageW(popup,WM_KEYDOWN,'D',1);PostMessageW(child,WM_KEYDOWN,'D',1);drain(host);
    check(filtered==4,"Owner popup and handler-child keys reach explicit native panel filter");DestroyWindow(popup);
    check(SendMessageW(window,WM_APP+21,123,0)==45&&privateMessages==1,"A private editor notification reaches its caller without a second message loop");
    SendMessageW(window,WM_DISPLAYCHANGE,32,MAKELPARAM(800,600));
    check(displayChanges==1,"Display topology changes are delivered as events without polling");
    SendMessageW(window, WM_SETFOCUS, 0, 0);
    SendMessageW(window, WM_KILLFOCUS, 0, 0);
    check(focus == std::vector<bool>{true, false}, "focus routing without requesting foreground or keyboard focus");
    check(applicationActivation.empty(),"Editor or owned-panel keyboard focus does not deactivate the application");
    SendMessageW(window,WM_ACTIVATEAPP,FALSE,0);
    SendMessageW(window,WM_ACTIVATEAPP,FALSE,0);
    SendMessageW(window,WM_ACTIVATEAPP,TRUE,0);
    check(applicationActivation==std::vector<bool>{false,true},"App switches deliver one event per transition without global hooks or polling");
    RECT suggested{4, 6, 244, 126};
    SendMessageW(window, WM_DPICHANGED, MAKEWPARAM(192, 192), reinterpret_cast<LPARAM>(&suggested));
    const auto dpi = host.metrics();
    check(dpi.dpi == 192 && dpi.pixelWidth == 240 && dpi.pixelHeight == 120 && dpi.scale == 2 &&
        dpi.width == 120 && dpi.height == 60, "DPI applies suggested own-window rectangle and logical scale");
    check(!IsWindowVisible(window), "DPI and resize do not show test window");
    host.setCursor(LoadCursorW(nullptr, IDC_CROSS));
    host.suspendCursor(true); host.suspendCursor(false);
    SendMessageW(window, WM_ENTERMENULOOP, 0, 0);
    SendMessageW(window, WM_EXITMENULOOP, 0, 0);
    SendMessageW(window, WM_ACTIVATEAPP, FALSE, 0);
    SendMessageW(window, WM_CLOSE, 0, 0);
    check(closes == 1 && IsWindow(window) && !IsWindowVisible(window), "close callback owns transition/hide without automatic destruction");
    stats = host.stats();
    check(stats.cursorSets == 0 && stats.cursorReleases == 0 && GetCursor() == cursorBefore,
        "hidden fixture never installs or changes the user cursor");
    check(!stats.timerArmed && !stats.framePending && frames == 0, "input and DPI never bypass hidden scheduling gate");
    bool wrongThread = false;
    std::thread other([&] { try { host.invalidate(); } catch (const std::logic_error&) { wrongThread = true; } });
    other.join(); check(wrongThread, "foreign-thread mutation rejected before HWND state access");
    host.requestStop(); check(!host.pumpOnce(0), "stop exits without posting process WM_QUIT");
    host.destroy();
    check(!IsWindow(window) && !host.hwnd() && !host.stats().timerArmed, "explicit destroy releases own window and wake handle");
    host.destroy();
    host.create({L"Recreated hidden host", 0, 0, 32, 32, nullptr});
    check(host.hwnd() && !IsWindowVisible(static_cast<HWND>(host.hwnd())), "same owner can create a clean hidden lifetime");
    host.destroy();
    for (unsigned i = 0; i < 12; ++i) {
        OverlayHost temporary; temporary.create({L"Bounded hidden teardown", 0, 0, 16, 16, nullptr});
        temporary.setFrameDemand(demand);
    }
}
void callbackFailures() {
    OverlayHost host;
    OverlayCallbacks callbacks;
    callbacks.pointer = [](const auto&) -> bool { throw std::runtime_error("owned fixture callback failure"); };
    host.create({L"Hidden callback failure", 0, 0, 16, 16, nullptr}, callbacks);
    SendMessageW(static_cast<HWND>(host.hwnd()), WM_MOUSEMOVE, 0, 0);
    bool propagated = false;
    try { host.pumpOnce(0); } catch (const std::runtime_error&) { propagated = true; }
    check(propagated && !host.stats().timerArmed && !host.stats().framePending, "callback exception stays inside USER32 and surfaces with scheduling canceled");
    host.destroy();

    // Destruction is explicitly permitted inside a callback. Keep a capture
    // alive until its body returns, even when destroy() releases the callback
    // set and create() installs a replacement during the same native dispatch.
    auto sentinel = std::make_shared<int>(73);
    const std::weak_ptr<int> weak = sentinel;
    bool captureSurvived = false;
    OverlayCallbacks replacing;
    replacing.pointer = [&, retained = sentinel](const auto&) {
        host.destroy();
        host.create({L"Hidden replacement inside callback", 0, 0, 16, 16, nullptr});
        captureSurvived = !weak.expired() && *retained == 73;
        return true;
    };
    host.create({L"Hidden captured callback", 0, 0, 16, 16, nullptr}, std::move(replacing));
    sentinel.reset();
    SendMessageW(static_cast<HWND>(host.hwnd()), WM_MOUSEMOVE, 0, 0);
    check(captureSurvived && weak.expired(), "callback capture survives HWND destroy/recreate and releases after dispatch");
    check(host.hwnd() && !IsWindowVisible(static_cast<HWND>(host.hwnd())), "callback replacement remains hidden");
    host.destroy();

    OverlayCallbacks filtering;
    unsigned staleKeys{};
    filtering.key=[&](const auto&){++staleKeys;return true;};
    filtering.beforeKeyTranslation=[&](const auto&){host.destroy();OverlayCallbacks replacement;replacement.key=[&](const auto&){++staleKeys;return true;};host.create({L"Recreated inside key filter",0,0,16,16,nullptr},replacement);return false;};
    host.create({L"Hidden key filter replacement",0,0,16,16,nullptr},filtering);
    PostMessageW(static_cast<HWND>(host.hwnd()),WM_KEYDOWN,'D',1|(0x20<<16));drain(host);
    check(staleKeys==0,"Destroy/recreate in pretranslation cannot deliver stale text to a replacement owner");host.destroy();

    for(UINT message:{UINT(WM_KILLFOCUS),UINT(WM_SHOWWINDOW)}){
        bool replaced=false;unsigned staleFocus=0;
        OverlayCallbacks captureCallbacks;
        captureCallbacks.focus=[&](bool){++staleFocus;};
        captureCallbacks.pointer=[&](const PointerEvent&e){
            if(e.kind==PointerKind::captureLost&&!replaced){replaced=true;host.destroy();host.create({L"Hidden capture replacement",0,0,16,16,nullptr});}
            return true;
        };
        host.create({L"Hidden captured lifecycle fixture",0,0,16,16,nullptr},captureCallbacks);
        const auto captured=static_cast<HWND>(host.hwnd());SetCapture(captured);
        check(GetCapture()==captured,"Capture test owns only its hidden fixture HWND");
        SendMessageW(captured,message,0,0);
        check(replaced&&staleFocus==0,"Capture release cannot deliver stale focus handlers after HWND recreation");
        check(host.hwnd()&&!host.stats().timerArmed&&!IsWindowVisible(static_cast<HWND>(host.hwnd())),"Recreated capture owner remains hidden and idle");host.destroy();
    }

}
#endif
}
int main(int argc,char**argv) {
    try {
#ifdef _WIN32
        if(argc==2&&std::string_view(argv[1])=="--hidden-startup-child"){hiddenStartupChild();return 0;}
        if(argc==2&&std::string_view(argv[1])=="--interactive-startup"){hiddenStartupProcess();std::cout<<checks<<" isolated hidden-startup/first-frame checks passed; private desktop never switched\n";return 0;}
#else
        (void)argc;(void)argv;
#endif
        scheduling(); coalescing(); deadlines();
#ifdef _WIN32
        nativeWindow(); callbackFailures(); nativeDeadlines();
        std::cout<<"Additional hidden-startup/first-frame gate is explicit --interactive-startup on an interactive station; it is not part of this Session-0-compatible suite.\n";
        std::cout << checks << " source scheduling and hidden native host checks passed\n";
#else
        std::cout << checks << " portable source scheduling/coalescing checks passed; Win32 host tests require Windows\n";
#endif
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "After " << checks << " checks: " << error.what() << '\n'; return 1;
    }
}
