#include "app/overlay_host.hpp"
#include "tests/obs_fullscreen_rule.hpp"
#include <cmath>
#include <cstdio>
#include <string>
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>
#include <limits>
#include <optional>
#include <span>
#include <utility>

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
// Finite motion follows the display's refresh (live RC: the HUD presented
// its transitions at 60 fps on a 165 Hz display); ambient idle and low power
// keep the source 30 Hz; nothing that does not animate gets a frame. One
// simulated second of host wakes, each presenting what the plan presents.
unsigned framesInOneSecond(const DisplayPacing& pacing, FrameDemand demand, bool shown = true) {
    FrameWakePlan plan; auto wake = plan.refresh(demand, shown);
    unsigned frames = wake.present ? 1 : 0; double time = 0;
    for (unsigned guard = 0; guard < 100000; ++guard) {
        const auto interval = pacing.interval(plan);
        if (!interval) break;
        check(*interval > 0, "Every paced interval moves time forward");
        time += *interval;
        if (time > 1 + 1e-9) break;
        if (plan.wake().present) ++frames;
    }
    return frames;
}
void displayPacing() {
    auto finite = visibleDemand(); finite.finiteAnimation = true;
    auto opening = visibleDemand(); opening.phase = VisibilityPhase::opening; opening.canAdvanceTransition = true;
    auto ambient = visibleDemand(); ambient.ambientEnabled = true;
    auto lowPower = finite; lowPower.lowPower = true;
    for (const double hz : {60.0, 144.0, 165.0}) {
        DisplayPacing pacing; pacing.setRefreshPeriod(1 / hz);
        FrameWakePlan plan; plan.refresh(finite, true);
        check(DisplayPacing::paced(plan) && pacing.interval(plan) && std::abs(*pacing.interval(plan) - 1 / hz) < 1e-12, "Finite motion is paced by the display refresh");
        const auto expected = static_cast<unsigned>(std::lround(hz)) + 1; // the frame the demand presents at once, then one per refresh
        check(framesInOneSecond(pacing, finite) == expected, "A finite animation presents once per refresh of a 60, 144 or 165 Hz display");
        check(framesInOneSecond(pacing, opening) == expected, "An opening transition presents once per refresh");
        check(framesInOneSecond(pacing, ambient) == 31, "Ambient idle keeps the source 30 Hz on any display");
        check(framesInOneSecond(pacing, lowPower) == 31, "Low-power motion keeps the source 30 Hz on any display");
        check(framesInOneSecond(pacing, visibleDemand()) <= 1 && !pacing.interval([&] { FrameWakePlan idle; idle.refresh(visibleDemand(), true); return idle; }()),
            "A shown HUD with nothing animating schedules no frame after the one its change presents");
        check(framesInOneSecond(pacing, finite, false) == 0, "A hidden window gets no frame at any refresh rate");
        auto reduced = finite; reduced.reduceMotion = true;
        check(framesInOneSecond(pacing, reduced) <= 1, "Reduced motion keeps its single change frame");
    }
    // Unknown or implausible refresh rates keep the source 60 Hz cadence.
    for (const auto period : {std::optional<double>{}, std::optional<double>{0.}, std::optional<double>{-1.}, std::optional<double>{1.},
                              std::optional<double>{std::numeric_limits<double>::quiet_NaN()}}) {
        DisplayPacing pacing; pacing.setRefreshPeriod(period);
        check(!pacing.refreshPeriod() && framesInOneSecond(pacing, finite) == 61, "Without a known refresh, finite motion keeps the source 60 Hz");
    }
    // Animation timing is time based: the source curve sampled at 165 Hz
    // frame times passes through the same values at the shared instants.
    {
        DisplayPacing fast; fast.setRefreshPeriod(1 / 165.); DisplayPacing source;
        VisibilityClock a(.6, .3), b(.6, .3); a.open(0); b.open(0);
        FrameWakePlan pa, pb; pa.refresh(opening, true); pb.refresh(opening, true);
        const auto sa = a.sample(.3), sb = b.sample(.3);
        check(sa && sb && sa->entranceTime == sb->entranceTime, "Frame cadence never changes the sampled source timing");
        check(fast.interval(pa) && source.interval(pb) && *fast.interval(pa) < *source.interval(pb), "Only the number of samples differs");
    }
}
// Windows coalesces mouse moves into one WM_MOUSEMOVE per read of the queue;
// a drag recovers the samples between two delivered positions from the
// system's newest-first mouse history.
void mouseHistory() {
    std::vector<MouseHistoryPoint> history; // newest first, 1 ms apart
    for (int k = 20; k >= 0; --k) history.push_back({100 + 3 * k, 200 + k, 5000u + static_cast<std::uint32_t>(k)});
    const MouseHistoryPoint current = history.front(), previous = history[6];
    std::vector<MouseHistoryPoint> out;
    check(newerMouseHistory(history, current, previous, out) == 5, "Every sample between the delivered positions is recovered");
    for (std::size_t k = 0; k < out.size(); ++k) check(out[k] == history[5 - k], "Samples arrive oldest first, without the move's own point or the last delivered one");
    out.clear();
    check(newerMouseHistory(std::span(history).first(1), current, previous, out) == 0, "A move with no history adds nothing");
    // The previous position fell out of the 64-entry history: the walk stops
    // at the first entry older than it.
    out.clear(); auto shortened = std::vector(history.begin(), history.begin() + 4);
    check(newerMouseHistory(shortened, current, previous, out) == 3 && out.front() == history[3], "A truncated history keeps what is newer than the last delivery");
    // Message times wrap around after 49.7 days.
    std::vector<MouseHistoryPoint> wrapped{{10, 10, 2u}, {9, 9, 0u}, {8, 8, 0xffffffffu}, {7, 7, 0xfffffffeu}};
    out.clear();
    check(newerMouseHistory(wrapped, wrapped[0], wrapped[3], out) == 2 && out[0] == wrapped[2] && out[1] == wrapped[1], "Times compare across the tick-count wraparound");
    // Samples in the same millisecond as the last delivery still count; the
    // delivered point itself and anything older never does.
    std::vector<MouseHistoryPoint> sameTick{{5, 5, 70u}, {4, 4, 69u}, {3, 3, 69u}, {2, 2, 69u}, {1, 1, 68u}};
    out.clear();
    check(newerMouseHistory(sameTick, sameTick[0], sameTick[3], out) == 2 && out[0] == sameTick[2] && out[1] == sameTick[1], "Same-millisecond samples after the delivery are kept");
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
    check((extended&WS_EX_TOPMOST)!=0,"The created overlay is already in the topmost band");
    check((GetWindowLongPtrW(window,GWL_STYLE)&~LONG_PTR(WS_VISIBLE))==(style&~LONG_PTR(WS_VISIBLE))&&GetWindowLongPtrW(window,GWL_EXSTYLE)==extended,
        "Show preserves borderless/composition styles and the topmost band");
    check(GetFocus()==focus&&GetActiveWindow()==active&&focusEvents==0&&!host.stats().focused&&host.stats().foregroundRequests==0,"Show(false) preserves activation and keyboard focus");
    drain(host);check(frames>0&&!host.stats().timerArmed,"Stable first frame renders without adding an idle timer");
    host.hide();check(!IsWindowVisible(window)&&!host.stats().framePending,"Hide cancels pending presentation");
    check((GetWindowLongPtrW(window,GWL_EXSTYLE)&WS_EX_TOPMOST)&&host.stats().topmost,"The hidden overlay keeps the topmost band");
    host.show(false);check((GetWindowLongPtrW(window,GWL_EXSTYLE)&WS_EX_TOPMOST)&&IsWindowVisible(window),"Explicit reopening is in the topmost band");
    drain(host);host.hide();check((GetWindowLongPtrW(window,GWL_EXSTYLE)&WS_EX_TOPMOST)!=0,"Every hide keeps the band");host.destroy();
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
// --zband-diagnostics: hidden facts about the topmost band in this session,
// for a run that fails the z-band checks (e.g. a scheduled task in a logged-in
// session). Nothing is shown and nothing is activated; window titles are
// never read (class and image file names only).
std::string narrowText(std::wstring_view text){
    if(text.empty())return {};
    const int bytes=WideCharToMultiByte(CP_UTF8,0,text.data(),static_cast<int>(text.size()),nullptr,0,nullptr,nullptr);
    std::string out(static_cast<std::size_t>(std::max(bytes,0)),'\0');
    WideCharToMultiByte(CP_UTF8,0,text.data(),static_cast<int>(text.size()),out.data(),bytes,nullptr,nullptr);return out;
}
std::string windowFacts(HWND target){
    if(!target)return "none";
    wchar_t name[128]{};GetClassNameW(target,name,128);
    DWORD pid{};GetWindowThreadProcessId(target,&pid);
    std::wstring image=L"?";
    if(HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid)){
        std::wstring path(32768,L'\0');DWORD size=static_cast<DWORD>(path.size());
        if(QueryFullProcessImageNameW(process,0,path.data(),&size)){path.resize(size);image=path.substr(path.find_last_of(L'\\')+1);}
        CloseHandle(process);
    }
    using DwmAttribute=HRESULT(WINAPI*)(HWND,DWORD,PVOID,DWORD);using Band=BOOL(WINAPI*)(HWND,DWORD*);
    static const auto dwm=reinterpret_cast<DwmAttribute>(reinterpret_cast<void*>(GetProcAddress(LoadLibraryW(L"dwmapi.dll"),"DwmGetWindowAttribute")));
    static const auto band=reinterpret_cast<Band>(reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"user32.dll"),"GetWindowBand")));
    DWORD cloaked{},zband{};if(dwm)(void)dwm(target,14/*DWMWA_CLOAKED*/,&cloaked,sizeof cloaked);if(band)(void)band(target,&zband);
    RECT r{};GetWindowRect(target,&r);
    const auto extended=GetWindowLongPtrW(target,GWL_EXSTYLE);
    char line[512]{};
    std::snprintf(line,sizeof line,"hwnd=%p class=%s image=%s pid=%lu visible=%d cloaked=%lu topmost=%d toolwindow=%d noactivate=%d layered=%d transparent=%d band=%lu rect=%ld,%ld,%ld,%ld",
        static_cast<void*>(target),narrowText(name).c_str(),narrowText(image).c_str(),pid,IsWindowVisible(target)?1:0,cloaked,(extended&WS_EX_TOPMOST)?1:0,
        (extended&WS_EX_TOOLWINDOW)?1:0,(extended&WS_EX_NOACTIVATE)?1:0,(extended&WS_EX_LAYERED)?1:0,(extended&WS_EX_TRANSPARENT)?1:0,zband,r.left,r.top,r.right,r.bottom);
    return line;
}
void zbandDiagnostics(){
    DWORD session{};ProcessIdToSessionId(GetCurrentProcessId(),&session);
    std::cout<<"session="<<session<<" consoleSession="<<WTSGetActiveConsoleSessionId()<<" desktop="<<narrowText(ownObjectName(GetThreadDesktop(GetCurrentThreadId())))
             <<" station="<<narrowText(ownObjectName(GetProcessWindowStation()))<<'\n';
    if(HANDLE token{};OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token)){
        alignas(TOKEN_MANDATORY_LABEL) unsigned char buffer[256]{};DWORD size{};
        if(GetTokenInformation(token,TokenIntegrityLevel,buffer,sizeof buffer,&size)){const auto* label=reinterpret_cast<TOKEN_MANDATORY_LABEL*>(buffer);
            std::cout<<"integrity=0x"<<std::hex<<*GetSidSubAuthority(label->Label.Sid,*GetSidSubAuthorityCount(label->Label.Sid)-1u)<<std::dec<<'\n';}
        CloseHandle(token);
    }
    BOOL inJob{};IsProcessInJob(GetCurrentProcess(),nullptr,&inJob);JOBOBJECT_BASIC_UI_RESTRICTIONS ui{};
    const BOOL uiRead=inJob&&QueryInformationJobObject(nullptr,JobObjectBasicUIRestrictions,&ui,sizeof ui,nullptr);
    std::cout<<"inJob="<<inJob<<" uiRestrictionsRead="<<uiRead<<" uiRestrictions=0x"<<std::hex<<ui.UIRestrictionsClass<<std::dec<<'\n';
    STARTUPINFOW startup{};startup.cb=sizeof startup;GetStartupInfoW(&startup);
    std::cout<<"startupFlags=0x"<<std::hex<<startup.dwFlags<<std::dec<<" startupShow="<<startup.wShowWindow<<'\n';
    std::cout<<"foreground: "<<windowFacts(GetForegroundWindow())<<'\n';
    // Windows lifts the foreground lock once this long has passed since the
    // last input, so an idle run reads the granted case, not a refused one.
    LASTINPUTINFO input{sizeof input};const BOOL inputRead=GetLastInputInfo(&input);DWORD lockTimeout{};
    SystemParametersInfoW(SPI_GETFOREGROUNDLOCKTIMEOUT,0,&lockTimeout,0);
    std::cout<<"idleMs="<<(inputRead?GetTickCount()-input.dwTime:0u)<<" foregroundLockTimeoutMs="<<lockTimeout<<'\n';
    // A raw window with the host's styles, never shown.
    WNDCLASSEXW type{};type.cbSize=sizeof type;type.lpfnWndProc=DefWindowProcW;type.hInstance=GetModuleHandleW(nullptr);type.lpszClassName=L"EndfieldZBandDiagnostics";
    RegisterClassExW(&type);
    const auto isAbove=[](HWND upper,HWND lower){for(HWND w=GetWindow(upper,GW_HWNDNEXT);w;w=GetWindow(w,GW_HWNDNEXT))if(w==lower)return true;return false;};
    const auto pinnedLater=[&]{return CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW,type.lpszClassName,L"",WS_POPUP,0,0,8,8,nullptr,nullptr,type.hInstance,nullptr);};
    const auto probe=[&](DWORD extended,const char* label){
        HWND w=CreateWindowExW(extended,type.lpszClassName,L"",WS_POPUP,0,0,32,32,nullptr,nullptr,type.hInstance,nullptr);
        // The band the window was created in, before any raise.
        std::cout<<label<<": created topmost="<<((GetWindowLongPtrW(w,GWL_EXSTYLE)&WS_EX_TOPMOST)?1:0)<<'\n';
        // A topmost window created after it (an Always On Top window pinned
        // later): whether the raise puts the probe above it is whether this
        // session lets this process reorder the band.
        const HWND later=pinnedLater();const bool aboveBefore=isAbove(w,later);
        // The foreground right, probed without side effects: granting it to
        // this process itself succeeds only while this process holds it.
        const BOOL right=AllowSetForegroundWindow(GetCurrentProcessId());const DWORD rightError=right?0:GetLastError();
        SetLastError(0);const BOOL raised=SetWindowPos(w,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);const DWORD error=GetLastError();
        const HWND previous=GetWindow(w,GW_HWNDPREV);
        std::cout<<label<<": foregroundRight="<<right<<" ("<<rightError<<") SetWindowPos(HWND_TOPMOST)="<<raised<<" error="<<error
                 <<" justBehindForeground="<<(previous&&previous==GetForegroundWindow()?1:0)<<" aboveLaterTopmost="<<aboveBefore<<"->"<<isAbove(w,later)
                 <<" after: "<<windowFacts(w)<<'\n';
        DestroyWindow(later);
        SetLastError(0);const BOOL lowered=SetWindowPos(w,HWND_NOTOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);const DWORD lowerError=GetLastError();
        std::cout<<label<<": SetWindowPos(HWND_NOTOPMOST)="<<lowered<<" error="<<lowerError<<" topmost="<<((GetWindowLongPtrW(w,GWL_EXSTYLE)&WS_EX_TOPMOST)?1:0)<<'\n';
        DestroyWindow(w);
    };
    probe(WS_EX_NOREDIRECTIONBITMAP|WS_EX_TOOLWINDOW,"host styles");
    probe(WS_EX_TOOLWINDOW,"tool window");
    probe(0,"plain popup");
    probe(WS_EX_TOPMOST|WS_EX_TOOLWINDOW,"created topmost");
    // The host through its test seam, as zBandAndActivation drives it.
    OverlayOptions options{L"Hidden z-band diagnostics",0,0,32,32,nullptr};
    options.testVisibility=[](bool shown,bool){return shown;};options.testForeground=[]{return false;};
    OverlayHost host;host.create(options);
    const auto window=static_cast<HWND>(host.hwnd());
    std::cout<<"host after create: statsTopmost="<<host.stats().topmost<<" "<<windowFacts(window)<<'\n';
    // As firstOpeningBand: a topmost window pinned after the HUD was created.
    const HWND later=pinnedLater();const bool aboveBefore=isAbove(window,later);
    host.show(true);
    std::cout<<"host after show(true): statsTopmost="<<host.stats().topmost<<" bandRefusals="<<host.stats().bandRefusals<<" bandPending="<<host.stats().bandPending
             <<" visible="<<host.stats().visible<<" justBehindForeground="<<(GetWindow(window,GW_HWNDPREV)==GetForegroundWindow()?1:0)
             <<" aboveLaterTopmost="<<aboveBefore<<"->"<<isAbove(window,later)<<" "<<windowFacts(window)<<'\n';
    DestroyWindow(later);
    // The windows above the HUD fixture in the z-order (front to back).
    unsigned above{},topmostAbove{},visibleAbove{};std::vector<std::string> listed;
    for(HWND w=GetTopWindow(nullptr);w&&w!=window;w=GetWindow(w,GW_HWNDNEXT)){
        ++above;const auto extended=GetWindowLongPtrW(w,GWL_EXSTYLE);if(extended&WS_EX_TOPMOST)++topmostAbove;
        if(IsWindowVisible(w)){++visibleAbove;if(listed.size()<24)listed.push_back(windowFacts(w));}
    }
    std::cout<<"windows above the fixture: "<<above<<" (topmost "<<topmostAbove<<", visible "<<visibleAbove<<")\n";
    for(const auto& line:listed)std::cout<<"  above: "<<line<<'\n';
    host.hide();
    std::cout<<"host after hide: statsTopmost="<<host.stats().topmost<<" "<<windowFacts(window)<<'\n';
    host.destroy();UnregisterClassW(type.lpszClassName,type.hInstance);
}
// The first opening after launch, as a summon without the right to set the
// foreground window meets it (the default Ctrl+` arrives through the keyboard
// hook): Windows refuses that opening the activation and any raise into the
// topmost band. Another application's window opened after the HUD was
// created sits above it among normal windows. The HUD must still appear above
// it, as every later opening does: the window holds the band from creation
// (Mac: the panel's .statusBar level, set once) and keeps it while hidden
// (orderOut keeps the level). Within the band every opening raises it to the
// top (Mac: the panel is ordered front on every opening), so a topmost window
// another program pinned after launch (an Always On Top window, Task Manager,
// a picture-in-picture player) is beneath it too. Nothing is shown, activated
// or injected: the seams stand in for visibility and the foreground (and, in
// the second part, for a raise Windows declines), so the z-order checked is
// the one create(), show() and hide() leave natively.
void firstOpeningBand() {
    const auto above=[](HWND upper,HWND lower){for(HWND w=GetWindow(upper,GW_HWNDNEXT);w;w=GetWindow(w,GW_HWNDNEXT))if(w==lower)return true;return false;};
    const auto nativeTopmost=[](HWND w){return (GetWindowLongPtrW(w,GWL_EXSTYLE)&WS_EX_TOPMOST)!=0;};
    {
        // The raise itself is native. Whether Windows lets this process
        // reorder one topmost window above another depends on the session
        // (unmeasured without the foreground right), so the session is asked
        // first with two hidden topmost windows of its own; where it declines,
        // the opening must keep the band with no refusal instead.
        OverlayOptions options{L"Hidden pinned-window z fixture",0,0,32,32,nullptr};
        options.testVisibility=[](bool shown,bool){return shown;};
        options.testForeground=[]{return false;};
        OverlayHost host;host.create(options);
        const auto hud=static_cast<HWND>(host.hwnd());
        check(nativeTopmost(hud),"The HUD window is created in the topmost band");
        // An Always On Top window pinned after launch: never shown, created
        // topmost after the HUD, so at the top of the band, above it.
        const HWND pinned=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW,L"STATIC",L"Hidden pinned window fixture",WS_POPUP,0,0,8,8,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        check(pinned&&!IsWindowVisible(pinned)&&nativeTopmost(pinned)&&above(pinned,hud),"A topmost window pinned after launch, never shown, sits above the hidden HUD");
        // The session probe: a later topmost window above the pinned one,
        // then the pinned one raised; it ends on top again where the session
        // reorders the band.
        const HWND later=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW,L"STATIC",L"Hidden later topmost fixture",WS_POPUP,0,0,8,8,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        check(later&&!IsWindowVisible(later)&&nativeTopmost(later)&&above(later,pinned),"A later topmost probe window sits above the pinned one");
        check(SetWindowPos(pinned,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE)!=FALSE,"Raise the pinned fixture within the band");
        const bool reorders=above(pinned,later);
        check(DestroyWindow(later)!=FALSE&&nativeTopmost(pinned)&&above(pinned,hud),"The pinned fixture stays in the band above the hidden HUD");
        host.show(true);
        check(!IsWindowVisible(hud)&&host.stats().visible&&host.stats().foregroundRefusals==1&&host.stats().activationPending,"The opening is shown without the foreground");
        if(reorders)check(above(hud,pinned),"An opening refused the foreground is above a topmost window pinned after launch");
        check(host.stats().topmost&&nativeTopmost(hud)&&!host.stats().bandPending&&host.stats().bandRequests==1&&host.stats().bandRefusals==0,
            "An opening refused the foreground keeps the band; its raise within the band is no refusal");
        if(reorders){
            host.hide();
            check(nativeTopmost(hud)&&above(hud,pinned),"The hidden HUD keeps its place in the band");
            // The other program pins its window again, above the hidden HUD.
            check(SetWindowPos(pinned,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE)!=FALSE&&above(pinned,hud),"Raise the hidden pinned fixture to the top of the band");
            host.show(true);
            check(nativeTopmost(hud)&&above(hud,pinned)&&host.stats().bandRequests==2&&host.stats().bandRefusals==0&&!host.stats().bandPending,
                "Every later refused opening is raised above the pinned window again");
        }else{
            std::cout<<"First opening band: this session declines reordering the topmost band; the opening kept the band with no refusal\n";
        }
        host.hide();check(DestroyWindow(pinned)!=FALSE,"Destroy the pinned fixture");host.destroy();
    }
    bool summoned{};unsigned raises{};
    OverlayOptions options{L"Hidden first-opening z fixture",0,0,32,32,nullptr};
    options.testVisibility=[](bool shown,bool){return shown;};
    options.testForeground=[]{return false;};
    // Windows keeps the created style; every raise once summoned is declined
    // (the window stays where it is, in the band).
    options.testBand=[&](bool topmost){if(summoned)++raises;return topmost;};
    OverlayHost host;host.create(options);
    const auto hud=static_cast<HWND>(host.hwnd());
    // An application window opened since launch: never shown, created after
    // the HUD, so on top of the normal windows.
    const HWND application=CreateWindowExW(0,L"STATIC",L"Hidden later application fixture",WS_POPUP,0,0,8,8,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    check(application&&!IsWindowVisible(application)&&!nativeTopmost(application),"A later normal application window fixture, never shown");
    summoned=true;host.show(true);
    check(host.stats().visible&&host.stats().foregroundRefusals==1&&host.stats().activationPending,"The first opening is shown without the foreground");
    check(nativeTopmost(hud)&&above(hud,application),"The first opening, refused the foreground and the raise, is above an application window opened after launch");
    check(host.stats().topmost&&!host.stats().bandPending&&host.stats().bandRequests==1&&raises==1&&host.stats().bandRefusals==0,
        "The first opening's raise, declined within the band, is no refusal");
    host.hide();
    check(!IsWindowVisible(hud)&&!host.stats().visible&&nativeTopmost(hud)&&host.stats().topmost&&above(hud,application),
        "The hidden HUD keeps the topmost band (Mac: orderOut keeps the panel's level)");
    check(SetWindowPos(application,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE)!=FALSE,"Raise the hidden application fixture to the top of the normal windows");
    host.show(true);
    check(nativeTopmost(hud)&&above(hud,application)&&raises==2&&host.stats().bandRequests==2&&host.stats().bandRefusals==0,"A later refused opening is above the application window too");
    host.hide();check(DestroyWindow(application)!=FALSE,"Destroy the application fixture");host.destroy();
}
// The HUD's z-band and activation through the real show()/hide() path, on a
// window that is never shown and never takes the foreground: test seams stand
// in for making it visible, for SetForegroundWindow and for the band (the
// created style Windows keeps, and the band's SetWindowPos), so Windows
// refusing the foreground and the band (a process without the right to set
// the foreground window) is played the same way in every session. Mac: the
// HUD panel's .statusBar level, above every normal window, set once and kept
// through orderOut, and NSApp.activate on opening.
void zBandAndActivation() {
    std::vector<std::pair<bool,bool>> visibility;std::vector<bool> bandCalls;unsigned foregroundCalls{};bool grant{},bandAllowed{true};
    OverlayCallbacks callbacks;
    OverlayOptions options{L"Hidden z-band fixture",0,0,32,32,nullptr};
    options.testVisibility=[&](bool shown,bool activate){visibility.emplace_back(shown,activate);return shown;};
    options.testForeground=[&]{++foregroundCalls;return grant;};
    options.testBand=[&](bool topmost){bandCalls.push_back(topmost);return topmost&&bandAllowed;};
    OverlayHost host;host.create(options,callbacks);
    const auto window=static_cast<HWND>(host.hwnd());
    const auto banded=[&]{return host.stats().topmost;};
    const auto nativeTopmost=[&]{return (GetWindowLongPtrW(window,GWL_EXSTYLE)&WS_EX_TOPMOST)!=0;};
    using Shows=std::vector<std::pair<bool,bool>>;
    check(banded()&&nativeTopmost()&&!host.stats().visible&&!IsWindowVisible(window)&&bandCalls==std::vector<bool>{true}&&host.stats().bandRequests==0,
        "A created HUD is hidden and already in the topmost band");

    // An opening without the foreground right: shown in the band, raised to
    // its top, waiting for its activation.
    host.show(true);
    check(!IsWindowVisible(window),"The fixture window is never really shown");
    check(host.stats().visible&&banded()&&bandCalls==std::vector<bool>{true,true}&&host.stats().bandRequests==1&&host.stats().bandRefusals==0&&!host.stats().bandPending,
        "The HUD opens in the topmost band it holds, raised to its top; no refusal");
    check(visibility==Shows{{true,true}}&&foregroundCalls==1&&host.stats().foregroundRequests==1,"An activating opening asks for the foreground once");
    check(host.stats().foregroundRefusals==1&&host.stats().activationPending,"A refused activation leaves the HUD shown, waiting for its activation");
    check(SendMessageW(window,WM_MOUSEACTIVATE,reinterpret_cast<WPARAM>(window),MAKELPARAM(HTCLIENT,WM_LBUTTONDOWN))==MA_ACTIVATE,
        "A click on the waiting HUD activates it");
    // The activation that click brings (no real activation: sent to the
    // hidden fixture, which only ever changes this thread's focus).
    SendMessageW(window,WM_ACTIVATE,MAKEWPARAM(WA_CLICKACTIVE,0),0);
    check(!host.stats().activationPending&&host.stats().deferredActivations==1&&banded()&&host.stats().bandRequests==1,"The click's activation ends the wait; the band stays");
    host.hide();
    check(!host.stats().visible&&visibility.back()==std::pair{false,false}&&banded()&&nativeTopmost()&&bandCalls.size()==2,
        "The hidden HUD keeps the topmost band (Mac: orderOut keeps the panel's level)");
    SendMessageW(window,WM_ACTIVATE,MAKEWPARAM(WA_CLICKACTIVE,0),0);
    check(host.stats().bandRequests==1&&banded(),"A hidden HUD never asks for the band");

    // show(false) is raised in the band without asking for the foreground.
    const auto foregroundBefore=foregroundCalls;
    host.show(false);
    check(visibility.back()==std::pair{true,false}&&banded()&&foregroundCalls==foregroundBefore&&!host.stats().activationPending&&bandCalls.size()==3&&
        host.stats().bandRequests==2,"A non-activating show is raised above other windows and leaves activation alone");
    host.hide();check(banded(),"Hidden again: still in the band");

    // A granted opening (a Windows-key chord's WM_HOTKEY, or a launcher that
    // passed its right on): the activation brings it to the top of the band.
    grant=true;host.show(true);
    check(banded()&&foregroundCalls==foregroundBefore+1&&!host.stats().activationPending,"A granted activation needs no fallback");
    host.show(true);
    check(banded()&&foregroundCalls==foregroundBefore+2&&host.stats().bandRequests==4&&bandCalls.size()==5&&host.stats().bandRefusals==0&&host.stats().visible,
        "A repeated opening asks for the foreground and raises it again");
    host.hide();

    // A refused opening, then a later granted request (a second launch
    // forwarded with AllowSetForegroundWindow) ends the wait.
    const auto deferred=host.stats().deferredActivations;
    grant=false;host.show(true);check(host.stats().activationPending,"Refused again");
    grant=true;host.show(true);
    check(!host.stats().activationPending&&host.stats().deferredActivations==deferred+1,"A later granted request ends the wait");
    host.hide();check(!host.stats().activationPending&&!host.stats().bandPending&&banded(),"Hiding drops the wait and keeps the band");grant=false;

    // The default close (no owner callback) hides through the same path.
    host.show(false);check(banded(),"Shown for the default close");
    SendMessageW(window,WM_CLOSE,0,0);
    check(IsWindow(window)&&!host.stats().visible&&banded()&&nativeTopmost()&&visibility.back()==std::pair{false,false},"The default close hides the HUD; it keeps the band");

    // The Projection hand-off (Application::moveProjection): the HUD is
    // hidden, placed on Projection's display, then shown by Projection. The
    // placement is geometry only: the HUD keeps its band, hidden or shown.
    const auto placedAt=[&](int x,int y,int w,int h){RECT r{};return GetWindowRect(window,&r)!=FALSE&&r.left==x&&r.top==y&&r.right-r.left==w&&r.bottom-r.top==h;};
    const auto shows=visibility.size();const auto raised=bandCalls.size();
    check(host.place(8,12,48,40)&&placedAt(8,12,48,40)&&host.metrics().pixelWidth==48&&host.metrics().pixelHeight==40,"Placing the hidden HUD moves and sizes it");
    check(banded()&&nativeTopmost()&&!host.stats().visible&&!IsWindowVisible(window)&&visibility.size()==shows&&bandCalls.size()==raised,
        "Placing the hidden HUD for Projection leaves it hidden in the band");
    host.show(true);check(banded()&&visibility.size()==shows+1&&bandCalls.size()==raised+1,"Projection's show is raised in the band");
    const auto requests=host.stats().foregroundRequests;
    check(host.place(4,6,40,36)&&placedAt(4,6,40,36)&&banded()&&nativeTopmost()&&host.stats().visible&&host.stats().foregroundRequests==requests&&visibility.size()==shows+1&&
        bandCalls.size()==raised+1,"Moving a shown Projection to another display keeps it in the band and leaves activation alone");
    host.hide();check(banded(),"Projection closed: still in the band");
    host.destroy();
    check(!host.place(0,0,32,32),"No window: nothing to place");

    // Windows kept the created window out of the band (as it may refuse a
    // raise without the foreground right): an opening asks for the band
    // as it appears, a refusal is reported, and the next activation or a
    // later opening with the right enters it; hiding drops the wait.
    enum class Entry{applicationActivation,clickActivation,laterOpening};
    for(const auto entry:{Entry::applicationActivation,Entry::clickActivation,Entry::laterOpening}){
        bandCalls.clear();visibility.clear();bandAllowed=false;grant=false;
        OverlayHost refused;refused.create(options);
        const auto refusedWindow=static_cast<HWND>(refused.hwnd());
        check(!refused.stats().topmost&&!refused.stats().bandPending&&bandCalls==std::vector<bool>{true},"Windows kept the created HUD out of the band");
        refused.show(true);
        check(refused.stats().visible&&!refused.stats().topmost&&refused.stats().bandRequests==1&&refused.stats().bandRefusals==1&&refused.stats().bandPending&&
            refused.stats().activationPending&&bandCalls==std::vector<bool>{true,true},"Out of the band, an opening asks for it; refused, the HUD waits for both");
        refused.hide();check(!refused.stats().bandPending&&!refused.stats().activationPending,"Hiding drops both waits");
        SendMessageW(refusedWindow,WM_ACTIVATE,MAKEWPARAM(WA_CLICKACTIVE,0),0);
        check(refused.stats().bandRequests==1,"A hidden HUD never asks for the band");
        refused.show(false);check(refused.stats().bandPending&&refused.stats().bandRefusals==2,"A non-activating show can be refused the band too");
        bandAllowed=true;
        if(entry==Entry::applicationActivation){
            SendMessageW(refusedWindow,WM_ACTIVATEAPP,TRUE,0);
            check(refused.stats().topmost&&!refused.stats().bandPending&&refused.stats().deferredBands==1&&refused.stats().bandRequests==3,"The application's activation enters the band");
            SendMessageW(refusedWindow,WM_ACTIVATE,MAKEWPARAM(WA_CLICKACTIVE,0),0);
            check(refused.stats().bandRequests==3,"The window's activation then needs no second attempt");
        }else if(entry==Entry::clickActivation){
            SendMessageW(refusedWindow,WM_ACTIVATE,MAKEWPARAM(WA_CLICKACTIVE,0),0);
            check(refused.stats().topmost&&!refused.stats().bandPending&&refused.stats().deferredBands==1,"A click's activation enters the band");
        }else{
            grant=true;refused.show(true);
            check(refused.stats().topmost&&!refused.stats().bandPending&&refused.stats().deferredBands==1&&!refused.stats().activationPending,"A later opening that holds the right enters the band");
        }
        refused.hide();check(refused.stats().topmost,"Entered, the band is kept while hidden");
        const auto calls=refused.stats().bandRequests;const auto refusals=refused.stats().bandRefusals;refused.show(true);
        check(refused.stats().bandRequests==calls+1&&refused.stats().bandRefusals==refusals&&!refused.stats().bandPending&&refused.stats().topmost,
            "An opening in the band is raised to its top; no refusal");
        refused.hide();refused.destroy();
    }
    bandAllowed=true;grant=false;

    // A raise Windows declines while it grants the same opening's activation
    // (rare: both follow the foreground right). SetForegroundWindow sends
    // this thread's window WM_ACTIVATE before it returns; that activation
    // enters the band at once, as the opening's own judgement already asks.
    {
        HWND target{};bool allowed{};
        OverlayOptions grantedOptions=options;
        grantedOptions.testBand=[&](bool topmost){return topmost&&allowed;};
        grantedOptions.testForeground=[&]{allowed=true;SendMessageW(target,WM_ACTIVATE,MAKEWPARAM(WA_ACTIVE,0),0);return true;};
        OverlayHost granted;granted.create(grantedOptions);target=static_cast<HWND>(granted.hwnd());
        check(!granted.stats().topmost&&!granted.stats().bandPending,"Windows kept this created HUD out of the band");
        granted.show(true);
        check(granted.stats().topmost&&!granted.stats().bandPending&&!granted.stats().activationPending&&granted.stats().bandRequests==2&&
            granted.stats().bandRefusals==1&&granted.stats().deferredBands==1,"A granted activation enters the band its opening's raise was refused");
        granted.hide();granted.destroy();
    }

    // A hidden benchmark's offscreen window is never shown: no band, no foreground.
    OverlayOptions offscreen{L"Hidden offscreen z-band fixture",0,0,16,16,nullptr};offscreen.offscreenScheduling=true;
    OverlayHost benchmark;benchmark.create(offscreen);
    const auto benchmarkWindow=static_cast<HWND>(benchmark.hwnd());
    check(!(GetWindowLongPtrW(benchmarkWindow,GWL_EXSTYLE)&WS_EX_TOPMOST)&&!benchmark.stats().topmost,"An offscreen benchmark window is created out of the band");
    benchmark.show(true);
    check(benchmark.stats().visible&&!IsWindowVisible(benchmarkWindow)&&!(GetWindowLongPtrW(benchmarkWindow,GWL_EXSTYLE)&WS_EX_TOPMOST)&&
        benchmark.stats().foregroundRequests==0&&benchmark.stats().bandRequests==0,"An offscreen benchmark never enters the band or asks for the foreground");
    benchmark.hide();benchmark.destroy();
}
// The band through Windows itself (the real, never shown window). The window
// is created with WS_EX_TOPMOST and keeps it while hidden. Whether this
// process may raise a window into the band depends on the session: without
// the right to set the foreground window - a logged-in session with another
// application in front, as when the tester plays - Windows answers success
// yet keeps a raised window out of the band, just behind the foreground
// window; with it (a session without a foreground window, such as the SSH
// service session, or a process that holds the right) the window enters it.
// Either way the host reports exactly what Windows did.
void zBandNative() {
    OverlayOptions options{L"Hidden native z-band fixture",0,0,32,32,nullptr};
    options.testVisibility=[](bool shown,bool){return shown;};options.testForeground=[]{return false;};
    OverlayHost host;host.create(options);
    const auto window=static_cast<HWND>(host.hwnd());
    const auto nativeTopmost=[&]{return (GetWindowLongPtrW(window,GWL_EXSTYLE)&WS_EX_TOPMOST)!=0;};
    const bool created=nativeTopmost();
    check(host.stats().topmost==created&&!host.stats().bandPending&&host.stats().bandRequests==0,"The host reports the band the HUD window was created in");
    host.show(false);
    const bool entered=nativeTopmost();
    check(!IsWindowVisible(window)&&host.stats().visible,"The native fixture is never really shown");
    check(host.stats().topmost==entered&&host.stats().bandRequests==1&&host.stats().bandRefusals==(entered?0u:1u)&&host.stats().bandPending==!entered,
        "The host reports whether the shown HUD, raised in the topmost band, is in it");
    if(entered){
        // Dialogs the HUD presents itself (the file choosers, the colour
        // dialogs, the account login) are owned by the HUD window: an owned
        // window stays above its owner, and one of a topmost owner is topmost,
        // so they appear above the shown HUD (Mac: sheets and panels at the
        // HUD's level + 1). Other applications are never opened while it is
        // shown (Application::openAfterClose and the other close-first
        // hand-offs).
        const HWND dialog=CreateWindowExW(WS_EX_TOOLWINDOW,L"STATIC",L"Hidden owned dialog fixture",WS_POPUP,0,0,8,8,window,nullptr,GetModuleHandleW(nullptr),nullptr);
        const auto above=[](HWND upper,HWND lower){for(HWND w=GetWindow(upper,GW_HWNDNEXT);w;w=GetWindow(w,GW_HWNDNEXT))if(w==lower)return true;return false;};
        check(dialog&&!IsWindowVisible(dialog)&&GetWindow(dialog,GW_OWNER)==window,"A dialog fixture is owned by the HUD window and never shown");
        check((GetWindowLongPtrW(dialog,GWL_EXSTYLE)&WS_EX_TOPMOST)&&above(dialog,window),"A dialog opened over the shown HUD is in the topmost band, above the HUD");
        host.show(false);
        check(nativeTopmost()&&host.stats().topmost&&(GetWindowLongPtrW(dialog,GWL_EXSTYLE)&WS_EX_TOPMOST)&&above(dialog,window),
            "Showing the HUD again raises it in the band and keeps its dialog above it");
        check(DestroyWindow(dialog)!=FALSE,"Destroy the owned dialog fixture");
        check(host.place(4,6,40,36)&&nativeTopmost(),"Placing the shown HUD keeps it in the band");
        std::cout<<"Native z-band: the HUD window is in the topmost band"<<(created?" from its creation":" (entered at its opening)")<<'\n';
    }else{
        // The session's foreground rules kept this process out of the band;
        // the next activation tries again (still refused here: a sent
        // activation message gives no right, and the default procedure's
        // local activation of the fixture may bring one more attempt).
        SendMessageW(window,WM_ACTIVATE,MAKEWPARAM(WA_CLICKACTIVE,0),0);
        check(host.stats().bandRequests>=2&&host.stats().topmost==nativeTopmost()&&host.stats().bandPending==!nativeTopmost()&&
            host.stats().bandRefusals+host.stats().deferredBands==host.stats().bandRequests,"Activation tries the band again and the host reports each result");
        std::cout<<"Native z-band: Windows' foreground rules kept this process out of the topmost band (foreground: "
                 <<(GetForegroundWindow()?"another window":"none")<<"); the refusal was reported\n";
    }
    const bool shownBand=nativeTopmost();
    host.hide();
    check(nativeTopmost()==shownBand&&host.stats().topmost==shownBand&&!host.stats().bandPending,"The hidden HUD keeps the band it had");
    host.destroy();
}
// An activating show of a window that holds the band, refused the
// foreground: whether Windows can then leave the window out of the band (its
// raise declined, or the activation refused) is unmeasured, so the host
// reads the band again after every show and a lost
// band waits for the next activation, as a refused raise does. Played on the
// real, never shown window: the visibility seam lowers it out of the band
// natively, standing for that refusal; nothing is shown or activated.
void bandLostAtShow() {
    HWND window{};bool drop{};
    OverlayOptions options{L"Hidden band-at-show fixture",0,0,32,32,nullptr};
    options.testVisibility=[&](bool shown,bool){
        if(shown&&drop)(void)SetWindowPos(window,HWND_NOTOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
        return shown;};
    options.testForeground=[]{return false;};
    OverlayHost host;host.create(options);
    window=static_cast<HWND>(host.hwnd());
    const auto nativeTopmost=[&]{return (GetWindowLongPtrW(window,GWL_EXSTYLE)&WS_EX_TOPMOST)!=0;};
    if(!nativeTopmost()){
        std::cout<<"Band at show: Windows kept the created window out of the band here; covered by zBandNative\n";
        host.destroy();return;
    }
    host.show(true);
    check(nativeTopmost()&&host.stats().topmost&&!host.stats().bandPending&&host.stats().bandRefusals==0&&host.stats().bandRequests==1,
        "A show that keeps the band reports no refusal");
    host.hide();drop=true;host.show(true);drop=false;
    check(!IsWindowVisible(window)&&host.stats().visible&&host.stats().activationPending,"The fixture opening is refused the foreground and never really shown");
    check(!nativeTopmost()&&!host.stats().topmost&&host.stats().bandPending&&host.stats().bandRefusals==1&&host.stats().bandRequests==2,
        "An opening that cost the HUD its band reports it and waits for the next activation");
    // The activation Windows allows next (sent to the hidden fixture: no
    // real activation) enters the band again where this session permits it.
    SendMessageW(window,WM_ACTIVATE,MAKEWPARAM(WA_CLICKACTIVE,0),0);
    check(host.stats().bandRequests==3&&host.stats().topmost==nativeTopmost()&&host.stats().bandPending==!nativeTopmost()&&
        host.stats().deferredBands==(nativeTopmost()?1u:0u),"The next activation asks for the band again and the host reports the result");
    host.hide();check(!host.stats().bandPending&&!host.stats().activationPending,"Hiding drops the waits");
    host.destroy();
}
// OBS Studio's any-full-screen Game Capture (tests/obs_fullscreen_rule.hpp)
// must never take the HUD window: played on the real, never-shown host
// window of the session's primary monitor after its creation, its placement
// and a per-monitor DPI change. Nothing is shown or activated.
void monitorCover() {
    MONITORINFO info{}; info.cbSize = sizeof(info);
    check(GetMonitorInfoW(MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY), &info) != FALSE, "Read the session's primary monitor");
    const RECT m = info.rcMonitor;
    const auto width = static_cast<std::uint32_t>(m.right - m.left), height = static_cast<std::uint32_t>(m.bottom - m.top);
    {
        // The copied rule takes a window exactly the monitor's size: the HUD
        // window before monitorCoverRect.
        OverlayHost plain; plain.create({L"Hidden monitor-sized fixture", m.left, m.top, width, height, nullptr});
        check(endfield::tests::obsTakesAsFullscreen(static_cast<HWND>(plain.hwnd())), "OBS's any-full-screen rule takes a hidden window exactly the monitor's size");
        plain.destroy();
    }
    check(monitorCoverRect(PixelRect{m.left, m.top, m.right, m.bottom}) == PixelRect{m.left, m.top, m.right + monitorCoverOverhang, m.bottom + monitorCoverOverhang},
        "The cover rectangle reaches past OBS's 2 px tolerance on the right and bottom only");
    unsigned resizes{}; ClientMetrics last{};
    OverlayCallbacks callbacks; callbacks.resize = [&](const ClientMetrics& c) { ++resizes; last = c; };
    OverlayOptions options{L"Hidden monitor-cover fixture", m.left, m.top, width, height, nullptr}; options.coverMonitor = true;
    OverlayHost host; host.create(options, callbacks);
    const auto window = static_cast<HWND>(host.hwnd());
    const auto band = GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOPMOST;
    const auto covers = [&](const std::string& when) {
        RECT rect{}; check(GetWindowRect(window, &rect) != FALSE, "Read the cover fixture's rectangle");
        check(rect.left == m.left && rect.top == m.top && rect.right == m.right + monitorCoverOverhang && rect.bottom == m.bottom + monitorCoverOverhang,
            ("The window covers the monitor and reaches monitorCoverOverhang past its right and bottom edges after " + when).c_str());
        check(!endfield::tests::obsTakesAsFullscreen(window), ("OBS's any-full-screen rule does not take the HUD window after " + when).c_str());
        POINT origin{}; check(ClientToScreen(window, &origin) && origin.x == m.left && origin.y == m.top, ("The client origin is the monitor's after " + when).c_str());
        const auto metrics = host.metrics();
        check(metrics.pixelWidth == width && metrics.pixelHeight == height && last.pixelWidth == width && last.pixelHeight == height,
            ("The host reports the monitor's size as the client (swap chain, composition, layout) after " + when).c_str());
        const auto region = CreateRectRgn(0, 0, 0, 0); RECT box{};
        const int kind = GetWindowRgn(window, region); GetRgnBox(region, &box); DeleteObject(region);
        check(kind == SIMPLEREGION && box.left == 0 && box.top == 0 && box.right == static_cast<LONG>(width) && box.bottom == static_cast<LONG>(height),
            ("The window region is the monitor in window coordinates after " + when).c_str());
        check(!IsWindowVisible(window) && (GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOPMOST) == band, ("The fixture stays hidden in its band after " + when).c_str());
    };
    covers("its creation");
    // Every opening places the HUD on its display: a window already there
    // is left alone (no move, WM_SIZE or swap-chain resize).
    const auto settled = resizes;
    check(host.place(m.left, m.top, static_cast<int>(width), static_cast<int>(height)) && host.stats().placeMoves == 0 && resizes == settled,
        "Placing the HUD on the display it covers moves and resizes nothing");
    covers("a repeated placement");
    check(host.place(m.left, m.top, static_cast<int>(width / 2), static_cast<int>(height / 2)) && host.stats().placeMoves == 1 &&
        host.metrics().pixelWidth == width / 2 && host.metrics().pixelHeight == height / 2, "Placing it on another area moves it once");
    check(host.place(m.left, m.top, static_cast<int>(width), static_cast<int>(height)) && host.stats().placeMoves == 2, "Placing it back moves it once");
    covers("its placement");
    const auto placed = resizes;
    check(host.place(m.left, m.top, static_cast<int>(width), static_cast<int>(height)) && host.stats().placeMoves == 2 && resizes == placed,
        "The next opening on the same display moves and resizes nothing");
    // A per-monitor DPI change: Windows' suggested rectangle only scales the
    // old size; the window keeps its monitor's cover rectangle.
    const auto dpi = host.metrics().dpi, other = dpi >= 144 ? 96u : 144u;
    RECT suggested{m.left, m.top, m.left + static_cast<LONG>(width * other / dpi), m.top + static_cast<LONG>(height * other / dpi)};
    SendMessageW(window, WM_DPICHANGED, MAKEWPARAM(other, other), reinterpret_cast<LPARAM>(&suggested));
    check(host.metrics().dpi == other && last.dpi == other, "The simulated DPI change reaches the host");
    covers("a DPI change");
    suggested = {m.left, m.top, m.left + static_cast<LONG>(width), m.top + static_cast<LONG>(height)};
    SendMessageW(window, WM_DPICHANGED, MAKEWPARAM(dpi, dpi), reinterpret_cast<LPARAM>(&suggested));
    covers("a DPI change back");
    check(host.stats().placeMoves == 2, "A DPI change fits the window without place()");
    host.destroy();
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
// A hidden benchmark's window (OverlayOptions::offscreenScheduling, the idle
// trace): never shown, yet scheduled exactly like a shown one at the given
// refresh, so the traced frames are the production cadence. Ambient idle
// draws at the source's 30 Hz and is never display-paced, finite motion
// follows the 165 Hz display, and nothing animating, a hidden window or a
// closed HUD gets no frame and no timer.
void offscreenCadence() {
    OverlayHost host; unsigned frames{};
    OverlayCallbacks callbacks; callbacks.frame = [&](double) { ++frames; };
    OverlayOptions options{L"Hidden offscreen schedule", 0, 0, 16, 16, nullptr};
    options.offscreenScheduling = true; options.refreshRate = 165;
    host.create(options, callbacks);
    const auto window = static_cast<HWND>(host.hwnd());
    auto ambient = visibleDemand(); ambient.ambientEnabled = true;
    host.setFrameDemand(ambient); drain(host);
    check(frames == 0 && !host.stats().timerArmed && !host.stats().framePending, "Before show, an offscreen window schedules nothing");
    host.show(false);
    check(!IsWindowVisible(window) && host.stats().visible && std::abs(host.stats().refreshRate - 165) < 1e-9, "Offscreen scheduling never shows the window");
    const auto run = [&](double seconds) {
        const auto end = OverlayHost::clockNow() + seconds;
        while (OverlayHost::clockNow() < end) host.pumpOnce(static_cast<std::uint32_t>(std::ceil((end - OverlayHost::clockNow()) * 1000)));
    };
    frames = 0; auto before = host.stats(); run(1);
    check(frames >= 24 && frames <= 32 && host.stats().pacedFrames == before.pacedFrames, "Ambient idle draws at the source 30 Hz and is never display-paced");
    auto finite = ambient; finite.finiteAnimation = true;
    host.setFrameDemand(finite);
    frames = 0; before = host.stats(); run(.5);
    check(frames >= 40 && host.stats().pacedFrames - before.pacedFrames >= 40, "Finite motion follows the 165 Hz display, not the source 60 Hz");
    host.setFrameDemand(visibleDemand()); drain(host);
    frames = 0; run(.3);
    check(frames == 0 && !host.stats().timerArmed, "A shown HUD with nothing animating schedules nothing");
    host.setFrameDemand(ambient); host.hide();
    frames = 0; run(.3);
    check(frames == 0 && !host.stats().timerArmed && !host.stats().framePending && !IsWindowVisible(window), "A hidden window gets no frame and no timer");
    host.destroy();
}
// The never-shown fixture lies under no cursor, so Windows answers the host's
// leave tracking (at once or later); those notices, and only those, are
// dropped, as projection_capture_route_tests does, so every frame below comes
// from the posted moves.
HWND leaveFixture{};
LRESULT CALLBACK dropFixtureLeaves(int code, WPARAM remove, LPARAM value) {
    if (code == HC_ACTION && remove == PM_REMOVE) { auto& m = *reinterpret_cast<MSG*>(value); if (m.hwnd == leaveFixture && m.message == WM_MOUSELEAVE) m.message = WM_NULL; }
    return CallNextHookEx(nullptr, code, remove, value);
}
// A frame held for the swap chain's admission while input keeps arriving.
// Enlarging a Notes card rebuilds the card on every move, so with a fast mouse
// a move is always queued: the HUD froze until the button was released (the
// held frame waited for a whole input batch, and the gate timer's WM_TIMER
// never comes while input is queued). The held frame runs once admitted, or
// 50 ms after it was held, after sampling the input that came with its
// admission. The same holds for held keys whose auto-repeat a module's key
// filter consumes. Hidden: posted moves and key repeats to a never-shown
// window, an auto-reset event standing in for the swap chain's frame latency
// object.
void gatedFrameUnderContinuousInput() {
    const HANDLE gate = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    check(gate != nullptr, "Create the stand-in frame latency object");
    struct Gate { HANDLE h; ~Gate() { CloseHandle(h); } } owned{gate};
    unsigned moves{}, limit{}, signalAt{};
    double cost{};
    bool admissionHeld{};
    std::vector<unsigned> frameAtMove, admittedAtMove; std::vector<double> frameTimes;
    HWND window{};
    OverlayCallbacks callbacks;
    callbacks.frame = [&](double now) { frameAtMove.push_back(moves); frameTimes.push_back(now); };
    callbacks.presentationWaitable = [&] { return static_cast<void*>(gate); };
    // The owner already holds an admission (the top of pumpOnce takes the
    // frame as admitted before it dispatches the queued input).
    callbacks.presentationHeld = [&] { return admissionHeld; };
    callbacks.presentationAdmitted = [&] { admittedAtMove.push_back(moves); };
    // One input step that costs `cost` seconds; true while the input lasts.
    const auto step = [&] {
        ++moves;
        for (const auto end = OverlayHost::clockNow() + cost; OverlayHost::clockNow() < end;) {}
        if (moves == signalAt) SetEvent(gate);
        return moves < limit;
    };
    // One resize step: while the drag lasts, a move queues the next move
    // before it returns (the mouse moved again).
    callbacks.pointer = [&](const PointerEvent& event) {
        if (event.kind != PointerKind::move) return false;
        if (step()) PostMessageW(window, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(moves % 16, 1));
        return true;
    };
    // A held key a module's key filter consumes (a held arrow moving a
    // selection): each auto-repeat is one step and queues the next.
    constexpr LPARAM keyRepeat = 1 | (1 << 30);
    callbacks.beforeKeyTranslation = [&](const NativeMessage& message) {
        if (message.message != WM_KEYDOWN || message.wParam != VK_RIGHT) return false;
        if (step()) PostMessageW(window, WM_KEYDOWN, VK_RIGHT, keyRepeat);
        return true;
    };
    OverlayOptions options{L"Hidden gated input fixture", 0, 0, 16, 16, nullptr};
    options.offscreenScheduling = true; options.refreshRate = 60;
    OverlayHost host; host.create(options, callbacks);
    window = static_cast<HWND>(host.hwnd());
    leaveFixture = window;
    struct Hook { HHOOK h{}; ~Hook() { if (h) UnhookWindowsHookEx(h); leaveFixture = nullptr; } } hook;
    hook.h = SetWindowsHookExW(WH_GETMESSAGE, dropFixtureLeaves, nullptr, GetCurrentThreadId());
    check(hook.h != nullptr, "Thread-local hook for the fixture's leave notices");
    host.setFrameDemand(visibleDemand()); host.show(false);
    check(!IsWindowVisible(window) && host.stats().visible, "The gated input fixture is never shown");
    const auto settle = [&] {
        for (unsigned i = 0; i < 40 && (host.stats().presentationGated || host.stats().framePending); ++i) { SetEvent(gate); host.pumpOnce(20); }
        ResetEvent(gate);
        check(!host.stats().presentationGated && !host.stats().framePending && !host.stats().timerArmed, "No gate, timer or frame is left pending");
    };
    const auto hold = [&] {
        settle(); frameAtMove.clear(); admittedAtMove.clear(); frameTimes.clear();
        moves = limit = signalAt = 0; cost = 0; admissionHeld = false;
        const auto since = OverlayHost::clockNow();
        host.invalidate(); host.pumpOnce(0);
        check(host.stats().presentationGated && frameAtMove.empty(), "The frame waits for the swap chain's admission");
        return since;
    };
    const auto drag = [&](unsigned until, double seconds, unsigned signal, bool keys = false) {
        limit = until; cost = seconds; signalAt = signal;
        if (keys) PostMessageW(window, WM_KEYDOWN, VK_RIGHT, keyRepeat);
        else PostMessageW(window, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(0, 1));
        for (unsigned i = 0; i < 256 && moves < limit; ++i) host.pumpOnce(0);
        limit = signalAt = 0; cost = 0;
    };
    // The swap chain accepts a frame at the third 2 ms resize step: the held
    // frame runs within about half a refresh of input, not after the batch.
    hold(); drag(300, .002, 3);
    std::cout << "Gated frame under continuous input: admitted at move 3, first frame after move " << (frameAtMove.empty() ? 0 : frameAtMove.front()) << " of " << moves << '\n';
    check(!frameAtMove.empty() && frameAtMove.front() >= 3 && frameAtMove.front() <= 15,
        "A held frame runs within about half a refresh of input after its admission while input keeps arriving");
    // The swap chain never accepts one (an occluded target): the 50 ms bound
    // still runs the frame while the moves keep arriving.
    auto before = host.stats();
    auto held = hold(); drag(300, .002, 0);
    std::cout << "Gated frame under continuous input: never admitted, first frame after move " << (frameAtMove.empty() ? 0 : frameAtMove.front()) << " of " << moves << '\n';
    check(!frameAtMove.empty() && frameAtMove.front() <= 60 && frameTimes.front() - held >= .05 &&
        host.stats().presentationFallbacks > before.presentationFallbacks,
        "A held frame the swap chain never admits runs 50 ms after it was held while input keeps arriving");
    // The same two with a held key whose repeats the key filter consumes.
    hold(); drag(300, .002, 3, true);
    std::cout << "Gated frame under consumed key repeats: admitted at repeat 3, first frame after repeat " << (frameAtMove.empty() ? 0 : frameAtMove.front()) << " of " << moves << '\n';
    check(!frameAtMove.empty() && frameAtMove.front() >= 3 && frameAtMove.front() <= 15,
        "A held frame runs within about half a refresh of admission while consumed key repeats keep arriving");
    before = host.stats();
    held = hold(); drag(300, .002, 0, true);
    std::cout << "Gated frame under consumed key repeats: never admitted, first frame after repeat " << (frameAtMove.empty() ? 0 : frameAtMove.front()) << " of " << moves << '\n';
    check(!frameAtMove.empty() && frameAtMove.front() <= 60 && frameTimes.front() - held >= .05 &&
        host.stats().presentationFallbacks > before.presentationFallbacks,
        "A held frame the swap chain never admits runs 50 ms after it was held while consumed key repeats keep arriving");
    // A burst of cheap moves queued while the frame is held, then admitted at
    // once (the Projection contract): the one frame samples the whole burst,
    // whichever edge takes the admission.
    const auto burst = [&](double seconds) {
        cost = seconds;
        for (unsigned k = 0; k < 6; ++k) PostMessageW(window, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(k, 2));
    };
    hold(); before = host.stats(); burst(0);
    SetEvent(gate); host.pumpOnce(0);
    std::cout << "Gated frame with a queued burst: admitted at move " << (admittedAtMove.empty() ? 0 : admittedAtMove.front())
        << ", frame after move " << (frameAtMove.empty() ? 0 : frameAtMove.front()) << " of " << moves << '\n';
    check(host.stats().presentationAdmissions == before.presentationAdmissions + 1 && admittedAtMove.size() == 1,
        "The queued burst's frame takes the swap chain's one admission");
    check(frameAtMove.size() == 1 && frameAtMove.front() == 6, "A frame admitted with a queued burst runs once, after every move of it");
    // The same burst with the admission already held when pumpOnce starts: the
    // frame is due before the first message and still samples the burst.
    hold(); burst(0); admissionHeld = true; host.pumpOnce(0); admissionHeld = false;
    check(frameAtMove.size() == 1 && frameAtMove.front() == 6, "A frame due at the top of pumpOnce runs once, after every move of a queued burst");
    // A burst whose moves outlast the sample window (six 2 ms moves against
    // half a 60 Hz refresh): the frame runs after at most five of them, and
    // the next frame draws the rest.
    hold(); burst(.002); admissionHeld = true; host.pumpOnce(0); admissionHeld = false;
    std::cout << "Gated frame with a costly burst: frame after move " << (frameAtMove.empty() ? 0 : frameAtMove.front()) << " of 6\n";
    check(frameAtMove.size() == 1 && frameAtMove.front() < 6, "A due frame samples at most half a refresh of further input");
    for (unsigned i = 0; i < 40 && (moves < 6 || host.stats().presentationGated || host.stats().framePending); ++i) { SetEvent(gate); host.pumpOnce(0); }
    check(moves == 6 && frameAtMove.size() == 2 && frameAtMove.back() == 6, "The next frame draws the rest of the burst");
    cost = 0;
    settle();
    before = host.stats(); frameAtMove.clear();
    for (const auto end = OverlayHost::clockNow() + .3; OverlayHost::clockNow() < end;) host.pumpOnce(100);
    check(frameAtMove.empty() && host.stats().timerWakes == before.timerWakes && host.stats().presentationWaits == before.presentationWaits,
        "After the drag, the idle HUD runs no frame and wakes nothing");
    host.destroy();
}
// A modal loop the HUD thread runs for someone else: the avatar picker's
// IFileOpenDialog::Show (and the other pickers, the colour panel, a drag)
// dispatches the HUD's messages with its own GetMessage loop. Display-paced
// motion posts its next frame as each frame ends; run straight from that
// loop, every frame queued the next one at once, and posted messages come
// before input, WM_PAINT and WM_TIMER, so the dialog never painted (a white
// frame, not responding) until the HUD closed. A frame the loop dispatches
// now waits for the swap chain's admission like one pumpOnce dispatches, on
// the gate timer, which comes after the loop's own input, paint and timers.
// Hidden: a never-shown fixture, an auto-reset event standing in for the
// frame latency object and a hidden stand-in dialog whose timer counts the
// low-priority work its loop gets through.
unsigned standInTicks{};
HANDLE standInGate{};
LRESULT CALLBACK standInDialog(HWND window, UINT message, WPARAM w, LPARAM l) {
    if (message == WM_TIMER && w == 1) { ++standInTicks; if (standInGate) SetEvent(standInGate); return 0; }
    return DefWindowProcW(window, message, w, l);
}
void nestedModalLoopUnderPacedMotion() {
    const HANDLE gate = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    check(gate != nullptr, "Create the stand-in frame latency object");
    struct Gate { HANDLE h; ~Gate() { CloseHandle(h); standInGate = nullptr; } } owned{gate};
    unsigned frames{};
    OverlayCallbacks callbacks;
    callbacks.frame = [&](double) { ++frames; };
    callbacks.presentationWaitable = [&] { return static_cast<void*>(gate); };
    OverlayOptions options{L"Hidden nested modal loop fixture", 0, 0, 16, 16, nullptr};
    options.offscreenScheduling = true; options.refreshRate = 60;
    OverlayHost host; host.create(options, callbacks);
    const auto window = static_cast<HWND>(host.hwnd());
    leaveFixture = window;
    struct Hook { HHOOK h{}; ~Hook() { if (h) UnhookWindowsHookEx(h); leaveFixture = nullptr; } } hook;
    hook.h = SetWindowsHookExW(WH_GETMESSAGE, dropFixtureLeaves, nullptr, GetCurrentThreadId());
    check(hook.h != nullptr, "Thread-local hook for the fixture's leave notices");
    WNDCLASSW type{}; type.lpfnWndProc = standInDialog; type.hInstance = GetModuleHandleW(nullptr); type.lpszClassName = L"EndfieldHiddenStandInDialog";
    check(RegisterClassW(&type) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS, "Register the stand-in dialog class");
    const auto dialog = CreateWindowExW(WS_EX_TOOLWINDOW, type.lpszClassName, L"Hidden stand-in dialog", WS_POPUP, 0, 0, 16, 16, window, nullptr, type.hInstance, nullptr);
    check(dialog != nullptr && !IsWindowVisible(dialog), "The stand-in dialog is owned by the fixture and never shown");
    auto paced = visibleDemand(); paced.finiteAnimation = true;
    host.setFrameDemand(paced); host.show(false);
    check(!IsWindowVisible(window) && host.stats().visible, "The nested loop fixture is never shown");
    // The modal loop: GetMessage-style dispatch (never pumpOnce) for `seconds`.
    const auto modal = [&](double seconds, bool admit) {
        standInTicks = 0; frames = 0; standInGate = admit ? gate : nullptr;
        check(SetTimer(dialog, 1, USER_TIMER_MINIMUM, nullptr) != 0, "Start the stand-in dialog's timer");
        SetEvent(gate); host.invalidate();
        MSG message{};
        for (const auto end = OverlayHost::clockNow() + seconds; OverlayHost::clockNow() < end;) {
            if (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); continue; }
            const auto left = static_cast<DWORD>(std::ceil(std::max(0., end - OverlayHost::clockNow()) * 1000));
            MsgWaitForMultipleObjectsEx(0, nullptr, left, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        }
        KillTimer(dialog, 1); standInGate = nullptr;
    };
    // The swap chain admits a frame whenever the dialog's loop is idle (a
    // display refresh): the dialog's timer keeps running and the HUD keeps drawing.
    auto before = host.stats();
    modal(.4, true);
    std::cout << "Nested modal loop under paced motion: " << standInTicks << " dialog timer ticks, " << frames << " HUD frames\n";
    check(standInTicks >= 5, "A modal loop run on the HUD thread gets its own timers while the HUD animates");
    check(frames >= 3 && frames <= standInTicks + 2, "The HUD keeps drawing in the modal loop, one frame per admission");
    check(host.stats().presentationWaits > before.presentationWaits, "Frames the modal loop dispatches wait for the swap chain's admission");
    // An occluded or suspended target never admits one: the 50 ms bound
    // still draws the HUD and the dialog's loop keeps its timers.
    before = host.stats();
    modal(.4, false);
    std::cout << "Nested modal loop, never admitted: " << standInTicks << " dialog timer ticks, " << frames << " HUD frames\n";
    check(standInTicks >= 5 && frames >= 3 && frames <= 12 && host.stats().presentationFallbacks > before.presentationFallbacks,
        "Without admissions the modal loop keeps its timers and the HUD draws every 50 ms");
    DestroyWindow(dialog);
    host.setFrameDemand(visibleDemand());
    for (unsigned i = 0; i < 40 && (host.stats().presentationGated || host.stats().framePending); ++i) { SetEvent(gate); host.pumpOnce(20); }
    ResetEvent(gate);
    check(!host.stats().presentationGated && !host.stats().framePending, "No gate or frame is left pending after the modal loop");
    host.destroy();
}
void nativeWindow() {
    OverlayHost host;
    std::vector<PointerEvent> pointer;
    std::vector<WheelEvent> wheel;
    std::vector<KeyEvent> keys;
    std::vector<bool> focus,applicationActivation;
    std::vector<ClientMetrics> sizes;
    unsigned frames = 0, closes = 0, filtered=0, privateMessages=0, displayChanges=0, timeChanges=0, settingChanges=0;
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
        if(event.message==WM_TIMECHANGE){++timeChanges;return 0;}
        if(event.message==WM_SETTINGCHANGE){++settingChanges;if(event.lParam)check(std::wstring(reinterpret_cast<const wchar_t*>(event.lParam))==L"intl","Named system setting payload reaches owner synchronously");return 0;}
        if(event.message!=WM_APP+21)return {};check(event.wParam==123,"Private owner notification preserves generation token");++privateMessages;return 45;};
    callbacks.displayChanged=[&]{++displayChanges;};
    unsigned cursorQueries=0;
    callbacks.cursorAt=[&](double,double){++cursorQueries;return OverlayCursor::text;};
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
    // A precision touchpad's pinch is Ctrl+wheel: the source magnification,
    // one detent being the source wheel zoom step exp(.12).
    SendMessageW(window, WM_MOUSEWHEEL, MAKEWPARAM(MK_CONTROL, 30), MAKELPARAM(ownPoint.x, ownPoint.y));
    check(wheel.size() == 3 && (wheel[2].modifiers & MK_CONTROL) && pinchMagnification(wheel[2]).has_value(), "a Ctrl+wheel message is a pinch");
    checkNear(*pinchMagnification(wheel[2]), std::exp(.25 * .12) - 1, "a pinch's magnification follows its fractional detents");
    check(pinchMagnification({0, 0, -2, false, MK_CONTROL, 3}) && std::abs(*pinchMagnification({0, 0, -2, false, MK_CONTROL, 3}) - (std::exp(-.24) - 1)) < 1e-12 &&
        !pinchMagnification(wheel[0]) && !pinchMagnification({0, 0, 1, true, MK_CONTROL, 3}) && !pinchMagnification({0, 0, std::nan(""), false, MK_CONTROL, 3}) &&
        !pinchMagnification({0, 0, 0, false, MK_CONTROL, 3}), "only a vertical, finite, nonzero Ctrl+wheel is a pinch; a wheel turned back zooms out");

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
    const auto timeChangeTimerArms=host.stats().timerArms;
    SendMessageW(window,WM_TIMECHANGE,0,0);
    check(timeChanges==1&&host.stats().timerArms==timeChangeTimerArms,"System time changes reach the owner without adding a polling timer");
    SendMessageW(window,WM_SETTINGCHANGE,0,reinterpret_cast<LPARAM>(L"intl"));
    SendMessageW(window,WM_SETTINGCHANGE,SPI_SETWHEELSCROLLLINES,0);
    check(settingChanges==2&&host.stats().timerArms==timeChangeTimerArms,"System setting changes reach the existing owner without polling or frame demand");
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
    // The single cursor path answers only for a visible, focused foreground
    // owner: a hidden window never asks for (or installs) the I-beam, and
    // refreshCursor re-answers only a cursor the window already shows.
    SendMessageW(window, WM_SETCURSOR, reinterpret_cast<WPARAM>(window), MAKELPARAM(HTCLIENT, WM_MOUSEMOVE));
    host.refreshCursor();
    check(cursorQueries == 0 && host.stats().cursorSets == 0 && host.stats().textCursorSets == 0,
        "a hidden owner never asks which cursor to show and never installs the I-beam");
    // The host declined, so the default window procedure answered this
    // synthetic WM_SETCURSOR with the class arrow; leave this thread's
    // cursor as the fixture found it.
    SetCursor(cursorBefore);
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

    {
        // Windows answers SetCapture on the window that already holds the
        // capture with WM_CAPTURECHANGED naming that same window: nothing was
        // lost, so a Projection or Notes stroke that asks for the capture on
        // every event keeps going; a real release still reports one loss.
        unsigned lost=0;OverlayCallbacks recaptureCallbacks;
        recaptureCallbacks.pointer=[&](const PointerEvent&e){if(e.kind==PointerKind::captureLost)++lost;return true;};
        host.create({L"Hidden recapture fixture",0,0,16,16,nullptr},recaptureCallbacks);
        const auto recaptured=static_cast<HWND>(host.hwnd());
        SetCapture(recaptured);check(GetCapture()==recaptured&&lost==0,"Recapture fixture owns only its hidden HWND's capture");
        SetCapture(recaptured);
        check(GetCapture()==recaptured&&lost==0,"Taking the capture again on the same window reports no capture loss");
        ReleaseCapture();
        check(GetCapture()!=recaptured&&lost==1,"Releasing the capture reports exactly one capture loss");
        host.destroy();
    }
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
        if(argc==2&&std::string_view(argv[1])=="--zband-diagnostics"){zbandDiagnostics();return 0;}
        if(argc==2&&std::string_view(argv[1])=="--interactive-startup"){hiddenStartupProcess();std::cout<<checks<<" isolated hidden-startup/first-frame checks passed; private desktop never switched\n";return 0;}
#else
        (void)argc;(void)argv;
#endif
        scheduling(); coalescing(); deadlines(); displayPacing(); mouseHistory();
#ifdef _WIN32
        nativeWindow(); callbackFailures(); firstOpeningBand(); zBandAndActivation(); zBandNative(); bandLostAtShow(); monitorCover(); nativeDeadlines(); offscreenCadence(); gatedFrameUnderContinuousInput(); nestedModalLoopUnderPacedMotion();
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
