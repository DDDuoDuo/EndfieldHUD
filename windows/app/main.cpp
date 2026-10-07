#include "render/native_renderer.h"
#include "app/frame_schedule.h"
#include "platform/composition_probe.h"
#include "platform/rendered_cursor.h"
#include "scene/desktop_shell.hpp"
#include "scene/desktop_scroll.hpp"
#include <windowsx.h>
#include <shellapi.h>
#include <psapi.h>
#include <shellscalingapi.h>
#include <roapi.h>
#include <winrt/base.h>
#include <chrono>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <optional>
#include <sstream>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;
constexpr UINT trayMessage = WM_APP + 1, frameTimer = 1, probeTimer = 2, clockTimer = 3;
constexpr UINT activateCommand = 100, quitCommand = 101;
struct Monitor { RECT area{}; UINT dpiX{96}, dpiY{96}; };
BOOL CALLBACK enumerateMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM context) {
    MONITORINFO info{sizeof(info)}; GetMonitorInfoW(monitor, &info);
    Monitor result{info.rcWork}; GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &result.dpiX, &result.dpiY);
    reinterpret_cast<std::vector<Monitor>*>(context)->push_back(result); return TRUE;
}
double cpuSeconds() {
    FILETIME creation{}, exit{}, kernel{}, user{};
    if (!GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user)) return 0;
    ULARGE_INTEGER k{}, u{}; k.LowPart = kernel.dwLowDateTime; k.HighPart = kernel.dwHighDateTime;
    u.LowPart = user.dwLowDateTime; u.HighPart = user.dwHighDateTime;
    return static_cast<double>(k.QuadPart + u.QuadPart) / 1e7;
}
std::filesystem::path executableRoot() {
    std::wstring path(32768, L'\0'); auto size = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    path.resize(size); return std::filesystem::path(path).parent_path();
}
class Application final {
public:
    explicit Application(bool probe, bool graphicsProbe, bool editorFixture, bool ambientEnabled, std::filesystem::path output, unsigned seconds,
                         std::optional<ehud::scene::DesktopLanguage> language = {}) :
        probe_(probe || graphicsProbe), graphicsProbe_(graphicsProbe), editorFixture_(editorFixture),
        ambientEnabled_(ambientEnabled), language_(language), output_(std::move(output)), probeSeconds_(seconds) {}
    int run(HINSTANCE instance) {
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        if(!language_) {
            ULONG count{},length{};GetUserPreferredUILanguages(MUI_LANGUAGE_NAME,&count,nullptr,&length);
            std::vector<wchar_t> names(length);std::vector<std::string> languages;
            if(length && GetUserPreferredUILanguages(MUI_LANGUAGE_NAME,&count,names.data(),&length))
                for(const wchar_t* p=names.data();*p;p+=wcslen(p)+1) languages.push_back(winrt::to_string(p));
            language_=ehud::scene::DesktopShell::resolveLanguage(languages);
        }
        if (!probe_) {
            mutex_ = CreateMutexW(nullptr, FALSE, L"Local\\DDDuoDuo.EndfieldHUD.Windows.DesktopFeasibility");
            if (GetLastError() == ERROR_ALREADY_EXISTS) {
                if (auto existing = FindWindowW(L"EndfieldHUD.Windows.DesktopFeasibility", nullptr)) PostMessageW(existing, WM_APP + 2, 0, 0);
                return 0;
            }
        }
        WNDCLASSW windowClass{}; windowClass.lpfnWndProc = procedure; windowClass.hInstance = instance;
        windowClass.lpszClassName = L"EndfieldHUD.Windows.DesktopFeasibility"; windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        RegisterClassW(&windowClass);
        window_ = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP | WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
            windowClass.lpszClassName, L"EndfieldHUD Windows feasibility", WS_POPUP, 0, 0, 1280, 720,
            nullptr, nullptr, instance, this);
        if (!window_) return 2;
        taskbarCreated_ = RegisterWindowMessageW(L"TaskbarCreated");
        if (graphicsProbe_) {
            PostMessageW(window_, WM_APP + 3, 0, 0);
        } else if (probe_) {
            probeStart_ = Clock::now(); cpuStart_ = cpuSeconds();
            GetProcessHandleCount(GetCurrentProcess(), &handlesStart_);
            // This mode deliberately creates no renderer, tray, clipboard,
            // credential vault, data store, timers except this one-shot end.
            SetTimer(window_, probeTimer, probeSeconds_ * 1000, nullptr);
        } else {
            addTray(); registerShortcut();
        }
        MSG message{}; BOOL result;
        while ((result = GetMessageW(&message, nullptr, 0, 0)) > 0) {
            if (editor_ && editor_->pre_translate(message)) continue;
            TranslateMessage(&message); DispatchMessageW(&message);
        }
        cleanup(); return result == -1 ? 3 : exitCode_;
    }
private:
    static LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
        auto* app = reinterpret_cast<Application*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            app = static_cast<Application*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
            if (app) {
                SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app)); app->window_ = window;
            }
        }
        return app ? app->handle(message, wparam, lparam) : DefWindowProcW(window, message, wparam, lparam);
    }
    double now() const { return std::chrono::duration<double>(Clock::now() - animationOrigin_).count(); }
    static ehud::scene::Vec3 desktopEuler(ehud::scene::Vec3 raw) {
        // HUDSourceWatchView's desktop adapter with the source default
        // parallaxIntensity=1 and perspectiveIntensity=1.
        return {raw.x*1.25,raw.y*1.25,raw.z*1.25};
    }
    bool refreshClock() {
        SYSTEMTIME time{};GetLocalTime(&time);char clock[16]{},date[32]{};
        static constexpr const char* weekdays[]{"SUN","MON","TUE","WED","THU","FRI","SAT"};
        static constexpr const char* months[]{"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
        sprintf_s(clock,"%02u:%02u:%02u",time.wHour,time.wMinute,time.wSecond);
        sprintf_s(date,"%s %s %u",weekdays[time.wDayOfWeek],months[time.wMonth-1],time.wDay);
        if(clockTime_==clock && clockDate_==date) return false;
        clockTime_=clock;clockDate_=date;return true;
    }
    ehud::scene::DesktopShellFixture desktopFixture(const ehud::scene::PlaybackSample& sample, bool synthetic=false) const {
        ehud::scene::DesktopShellFixture fixture;
        fixture.phase=sample.phase;fixture.phaseElapsed=sample.phaseElapsed;fixture.closingCanvasOpacity=closingCanvasOpacity_;
        fixture.accent={250./255,212./255,31./255};
        if(language_) fixture.language=*language_;
        if(!shortcutLabel_.empty()) fixture.summonShortcut=winrt::to_string(shortcutLabel_);
        if(!synthetic) {
            fixture.clockTime=clockTime_;fixture.clockDate=clockDate_;
        }
        return fixture;
    }
    ehud::scene::Frame desktopFrame(const ehud::scene::Document& document, ehud::scene::DesktopShell& shell,
        ehud::scene::FrameInput input, bool synthetic=false) {
        auto fixture=desktopFixture(input.playback,synthetic);
        auto source=shell.sourcePresentation(fixture);input.desktopPresentation=&source;
        input.desktopEntryCount=ehud::scene::DesktopShell::rightModules().size();
        input.verticalNormalizedPosition=scrollMotion_.position();
        auto frame=document.frame(input);presentation_=shell.decorate(frame,fixture);return frame;
    }
    bool outsideDesktopCircle(ehud::scene::Vec2 pointer) const {
        if(!frame_ || !presentation_ || !presentation_->centerPlane) return false;
        auto presentedWorld=[&](const ehud::scene::DesktopNativePlane& plane) {
            // Camera-only frames update graphics in place. Use their current
            // plane, rather than the metadata from the last tree rebuild.
            for(const auto& graphic:frame_->graphics)
                if(graphic.nodeId==plane.nodeId && graphic.kind.starts_with("Desktop")) return graphic.world;
            return plane.world;
        };
        if(presentation_->statusPlane && frame_->camera.hit(pointer,presentedWorld(*presentation_->statusPlane),presentation_->statusPlane->sourceRect)) return false;
        if(frame_->scroll) if(auto node=frame_->node(frame_->scroll->viewportId);node&&node->rect&&frame_->camera.hit(pointer,node->world,*node->rect)) return false;
        const auto& plane=*presentation_->centerPlane;
        auto local=frame_->camera.pointOnPlane(pointer,presentedWorld(plane));
        if(!local || plane.sourceRect.size.x<=0 || plane.sourceRect.size.y<=0) return false;
        const double dx=(local->x-plane.sourceRect.origin.x)/plane.sourceRect.size.x*1000-500;
        const double dy=(local->y-plane.sourceRect.origin.y)/plane.sourceRect.size.y*640-320;
        return dx*dx+dy*dy>=310*310;
    }
    void addTray() {
        NOTIFYICONDATAW data{sizeof(data)}; data.hWnd = window_; data.uID = 1;
        data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP; data.uCallbackMessage = trayMessage;
        data.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
        wcscpy_s(data.szTip, L"EndfieldHUD — Windows feasibility prototype");
        Shell_NotifyIconW(NIM_ADD, &data); data.uVersion = NOTIFYICON_VERSION_4; Shell_NotifyIconW(NIM_SETVERSION, &data);
    }
    void registerShortcut() {
        UnregisterHotKey(window_, 1); shortcutReady_ = false;
        const SHORT translated = VkKeyScanExW(L'`', GetKeyboardLayout(0));
        if (translated == -1) { shortcutLabel_ = L"Backtick unavailable on this layout"; return; }
        UINT modifiers = MOD_CONTROL | MOD_NOREPEAT;
        const auto shift = HIBYTE(translated);
        if (shift & 1) modifiers |= MOD_SHIFT;
        if (shift & 2) modifiers |= MOD_CONTROL;
        if (shift & 4) modifiers |= MOD_ALT;
        shortcutLabel_ = L"Ctrl + ";
        if (shift & 1) shortcutLabel_ += L"Shift + ";
        if (shift & 4) shortcutLabel_ += L"Alt + ";
        shortcutLabel_ += L"`";
        shortcutReady_ = RegisterHotKey(window_, 1, modifiers, LOBYTE(translated)) != FALSE;
        if (!shortcutReady_) shortcutLabel_ += L" (conflict; use tray)";
    }
    void placeOnActiveMonitor() {
        POINT pointer{}; GetCursorPos(&pointer); MONITORINFO info{sizeof(info)};
        GetMonitorInfoW(MonitorFromPoint(pointer, MONITOR_DEFAULTTONEAREST), &info);
        SetWindowPos(window_, HWND_TOPMOST, info.rcWork.left, info.rcWork.top,
            info.rcWork.right - info.rcWork.left, info.rcWork.bottom - info.rcWork.top, SWP_NOACTIVATE);
    }
    void toggle() {
        if (probe_) return;
        if (playback_ && playback_->phase() != ehud::scene::Phase::concealed) { close(false); return; }
        try {
            if (!document_) {
                auto root = executableRoot() / L"Resources" / L"NativeScene" / L"Scene";
                document_ = ehud::scene::Document::loadDesktop(root);
                shell_ = std::make_unique<ehud::scene::DesktopShell>(*document_);
                gyro_=ehud::scene::GyroMotion(document_->initialRootRotation());
                playback_ = std::make_unique<ehud::scene::Playback>(document_->entranceDuration(), document_->exitDuration());
                buttons_ = std::make_unique<ehud::scene::ButtonMotion>(*document_);
            }
            placeOnActiveMonitor(); RECT client{}; GetClientRect(window_, &client);
            if (!cursor_.handle() && FAILED(cursor_.initialize(window_, executableRoot()/L"Resources"/L"WatchSource"/L"Cursor")))
                throw std::runtime_error("Original source cursor initialization failed");
            if (!renderer_) {
                renderer_ = std::make_unique<ehud::render::NativeRenderer>();
                HRESULT hr = renderer_->initialize(window_, client.right, client.bottom);
                if (FAILED(hr)) throw std::runtime_error("Native graphics initialization failed");
                hr = renderer_->loadSourceAssets(executableRoot() / L"Resources" / L"WatchSource");
                if (FAILED(hr)) throw std::runtime_error("Original source sprite/material resource initialization failed");
                if(editorFixture_) {
                    editor_ = std::make_unique<endfield::platform::ProjectedEditor>();
                    hr = editor_->initialize(window_, renderer_->textFactory(), [this] { dirty_ = true; requestFrame(); });
                    if (FAILED(hr)) throw std::runtime_error("TSF text editor initialization failed; projected input is unavailable");
                    editor_->set_text(u"English 简体中文 繁體中文 日本語 한국어 😀\nSynthetic editing fixture — no saved user data");
                    editor_->set_rectangle(D2D1::RectF(0, 0, 600, 220));
                }
            }
            buttons_->reset(now()); hovered_.reset(); pressed_.reset(); frameSchedule_.reset();
            scrollMotion_.reset(scrollMotion_.position(),now());
            refreshClock();
            playback_->open(now()); dirty_ = true;
            POINT pointer{}; GetCursorPos(&pointer); ScreenToClient(window_, &pointer);
            pointer_ = {static_cast<double>(pointer.x), static_cast<double>(pointer.y)}; pointerChanged_ = true;
            pointerInside_ = PtInRect(&client, pointer) != FALSE;
            ShowWindow(window_, SW_SHOW); SetForegroundWindow(window_);
            cursor_.set_presented(true); cursor_.set_focused(GetForegroundWindow()==window_);
            resolveCursor(true,true);
            SetTimer(window_,clockTimer,1000,nullptr);
            requestFrame();
        } catch (const std::exception& error) {
            if (playback_) playback_->conceal(); frame_.reset(); frameSchedule_.reset();
            cursor_.set_presented(false);
            cancelPointerInteraction();
            if (editor_) editor_->focus(false); editor_.reset(); renderer_.reset();
            KillTimer(window_, frameTimer);KillTimer(window_,clockTimer); frameTimerRunning_ = false; ShowWindow(window_, SW_HIDE);
            const int size = MultiByteToWideChar(CP_UTF8, 0, error.what(), -1, nullptr, 0);
            std::wstring reason(size, L'\0'); MultiByteToWideChar(CP_UTF8, 0, error.what(), -1, reason.data(), size);
            MessageBoxW(window_, reason.c_str(), L"Windows feasibility prototype", MB_ICONERROR | MB_OK);
        }
    }
    void close(bool quit) {
        if (editor_) editor_->focus(false);
        cancelPointerInteraction();
        if (!playback_ || playback_->phase() == ehud::scene::Phase::concealed) {
            if (quit) DestroyWindow(window_); return;
        }
        const double time=now();closingCanvasOpacity_=ehud::scene::DesktopShell::canvasAlpha(desktopFixture(playback_->sample(time,false,ambientEnabled_)));
        KillTimer(window_,clockTimer);
        scrollMotion_.reset(scrollMotion_.position(),time);
        quitAfterClose_ = quit; playback_->close(time); dirty_ = true; requestFrame();
    }
    void cancelPointerInteraction() {
        const double time = now();
        if (pressed_ && buttons_) buttons_->setState(ehud::scene::ButtonState::normal, *pressed_, time);
        if (hovered_ && buttons_) buttons_->setHovered(false, *hovered_, time);
        pressed_.reset(); hovered_.reset(); pointerInside_ = false;
        if (GetCapture() == window_) ReleaseCapture();
    }
    bool resolveCursor(bool queryPointer, bool force=false) {
        if (probe_ || nativeHostTracking_) return false;
        if(queryPointer) {
            POINT point{}; RECT client{}; GetClientRect(window_,&client);
            pointerInside_=GetCursorPos(&point) && ScreenToClient(window_,&point) && PtInRect(&client,point);
            pointer_={static_cast<double>(point.x),static_cast<double>(point.y)};
        }
        auto region=pointerInside_?ehud::platform::CursorRegion::source:ehud::platform::CursorRegion::outside;
        if(editor_ && editor_->pointer_tracking()) region=ehud::platform::CursorRegion::native;
        else if(pointerInside_ && frame_ && editor_) {
            double x{},y{};
            if(ehud::render::NativeRenderer::editorProjection(*frame_).unproject(pointer_.x,pointer_.y,x,y) && x>=0 && x<=600 && y>=0 && y<=220)
                region=ehud::platform::CursorRegion::native;
        }
        if(force || region!=cursorRegion_) {
            if(region==ehud::platform::CursorRegion::native) SetCursor(LoadCursorW(nullptr,IDC_IBEAM));
            cursorRegion_=region; cursor_.set_region(region); cursor_.refresh();
        }
        return region==ehud::platform::CursorRegion::native;
    }
    void syncEditorCursorTracking() {
        const bool tracking=editor_ && editor_->pointer_tracking();
        if(tracking==editorPointerTracking_) return;
        if(tracking) cursor_.begin_native_tracking(); else cursor_.end_native_tracking();
        editorPointerTracking_=tracking; resolveCursor(true,true);
    }
    void requestFrame() {
        if (!probe_ && playback_ && playback_->phase() != ehud::scene::Phase::concealed && !frameTimerRunning_) {
            SetTimer(window_, frameTimer, 16, nullptr); frameTimerRunning_ = true;
        }
    }
    bool updateHovered(double time) {
        if (!frame_ || !buttons_) return false;
        auto hit = pointerInside_ ? frame_->buttonAt(pointer_) : std::optional<ehud::scene::SourceId>{};
        if (hit == hovered_) return false;
        if (hovered_) buttons_->setHovered(false, *hovered_, time);
        hovered_ = std::move(hit);
        if (hovered_) buttons_->setHovered(true, *hovered_, time);
        dirty_ = true; requestFrame();
        return true;
    }
    void renderingFailed(HRESULT status) {
        KillTimer(window_, frameTimer); frameTimerRunning_ = false;
        KillTimer(window_,clockTimer);
        frameSchedule_.reset();
        if (playback_) playback_->conceal(); frame_.reset();
        cursor_.set_presented(false);
        cancelPointerInteraction();
        if (editor_) editor_->focus(false); editor_.reset(); renderer_.reset();
        ShowWindow(window_, SW_HIDE);
        wchar_t reason[192]{};
        swprintf_s(reason, L"Windows preview rendering stopped (0x%08X). Reopen from the tray to retry. This feasibility build is still incomplete.", static_cast<unsigned>(status));
        MessageBoxW(window_, reason, L"EndfieldHUD Windows preview", MB_OK | MB_ICONERROR);
    }
    void frame() {
        if (!document_ || !renderer_ || !playback_) return;
        const double time = now(); auto sample = playback_->sample(time, false, ambientEnabled_);
        if (sample.phase == ehud::scene::Phase::concealed) {
            cursor_.set_presented(false);
            KillTimer(window_, frameTimer);KillTimer(window_,clockTimer); frameTimerRunning_ = false; ShowWindow(window_, SW_HIDE); frame_.reset(); gyro_.stop(time);
            frameSchedule_.reset();
            if (quitAfterClose_) DestroyWindow(window_); return;
        }
        RECT client{}; GetClientRect(window_, &client);
        ehud::scene::Vec2 viewport{static_cast<double>(client.right), static_cast<double>(client.bottom)};
        if (pointerChanged_) {
            gyro_.retarget(desktopEuler(document_->pointerEuler(pointer_, viewport)), time, document_->gyroDuration()); pointerChanged_ = false;
        }
        gyro_.finishIfNeeded(time);
        scrollMotion_.advance(time);
        const bool buttonAnimation = (buttons_ && buttons_->requiresFrames(time)) || scrollMotion_.requiresFrames() || sample.ambientTime.has_value();
        const auto decision = frameSchedule_.next(sample.phase, dirty_, gyro_.animating(), buttonAnimation);
        if (!decision.submit) {
            KillTimer(window_, frameTimer); frameTimerRunning_ = false; return;
        }
        if (frame_ && !decision.rebuild)
            document_->reproject(*frame_, gyro_.rotation(time));
        else {
            ehud::scene::FrameInput input{viewport, sample, gyro_.rotation(time)};
            input.interaction = buttons_.get(); input.time = time;
            frame_ = desktopFrame(*document_,*shell_,input);
        }
        // Opening and depth/tilt motion can move a button under a stationary
        // pointer. Resolve hover against this frame, using its same clock.
        if (updateHovered(time)) {
            ehud::scene::FrameInput input{viewport, sample, gyro_.rotation(time)};
            input.interaction = buttons_.get(); input.time = time;
            frame_ = desktopFrame(*document_,*shell_,input);
        }
        // The source default enables ambient animation. Explicit ambient-off
        // diagnostics keep shader time at zero while finite geometry proceeds.
        frame_->sceneTime=ambientEnabled_?time:0;
        HRESULT hr = renderer_->draw(*frame_, editor_.get()); dirty_ = false;
        if (FAILED(hr)) renderingFailed(hr);
        else { frameSchedule_.submitted(sample.phase, gyro_.animating(), (buttons_ && buttons_->requiresFrames(time)) || scrollMotion_.requiresFrames() || sample.ambientTime.has_value()); resolveCursor(false); }
    }
    void writeProbe() {
        KillTimer(window_, probeTimer);
        const auto elapsed = std::chrono::duration<double>(Clock::now() - probeStart_).count();
        DWORD handles{}; GetProcessHandleCount(GetCurrentProcess(), &handles);
        PROCESS_MEMORY_COUNTERS_EX memory{}; memory.cb = sizeof(memory);
        GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory));
        std::vector<Monitor> monitors; EnumDisplayMonitors(nullptr, nullptr, enumerateMonitor, reinterpret_cast<LPARAM>(&monitors));
        if (output_.has_parent_path()) std::filesystem::create_directories(output_.parent_path());
        std::ofstream output(output_); output << std::setprecision(12)
            << "{\n  \"schema\": 1,\n  \"scenario\": \"" << (warmClosed_ ? "closed-after-105-graphics-cycles" : "closed-native-shell")
            << "\",\n  \"synthetic\": true,\n"
            << "  \"elapsed_seconds\": " << elapsed << ",\n  \"process_cpu_seconds\": " << cpuSeconds() - cpuStart_
            << ",\n  \"hud_frames_submitted\": " << (renderer_ ? renderer_->submittedFrames() - submittedAtClose_ : 0)
            << ",\n  \"renderer_created\": " << (renderer_ ? "true" : "false") << ",\n  \"working_set_bytes\": " << memory.WorkingSetSize
            << ",\n  \"private_bytes\": " << memory.PrivateUsage << ",\n  \"handles_start\": " << handlesStart_
            << ",\n  \"handles_end\": " << handles << ",\n  \"monitors\": [";
        for (std::size_t index = 0; index < monitors.size(); ++index) {
            if (index) output << ','; const auto& monitor = monitors[index];
            output << "{\"left\":" << monitor.area.left << ",\"top\":" << monitor.area.top
                << ",\"width\":" << monitor.area.right - monitor.area.left << ",\"height\":" << monitor.area.bottom - monitor.area.top
                << ",\"dpi_x\":" << monitor.dpiX << ",\"dpi_y\":" << monitor.dpiY << '}';
        }
        output << "],\n  \"gpu_utilization\": \"unverified; requires GPU trace\",\n"
            << "  \"wakeups\": \"unverified; WPR trace required\",\n  \"performance_parity\": \"unverified\"\n}\n";
        if (!output) exitCode_ = 4; DestroyWindow(window_);
    }
    void graphicsProbe() {
        const auto start = Clock::now();
        try {
            auto doc = ehud::scene::Document::loadDesktop(executableRoot() / L"Resources" / L"NativeScene" / L"Scene");
            ehud::scene::DesktopShell shell(doc);
            ehud::scene::Playback playback(doc.entranceDuration(), doc.exitDuration());
            renderer_ = std::make_unique<ehud::render::NativeRenderer>(); auto& renderer = *renderer_;
            HRESULT renderStatus = renderer.initialize(window_, 1280, 720);
            if (FAILED(renderStatus)) throw std::runtime_error("D3D11/DirectComposition renderer creation failed");
            renderStatus = renderer.loadSourceAssets(executableRoot() / L"Resources" / L"WatchSource");
            if (FAILED(renderStatus)) throw std::runtime_error("Original source sprite resource initialization failed");
            if (FAILED(renderer.verifyDiagnosticAlpha())) throw std::runtime_error("D3D/D2D encoded premultiplied alpha contract failed");
            HWND unused = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP | WS_EX_TOOLWINDOW,
                L"EndfieldHUD.Windows.DesktopFeasibility", L"Composition capability probe", WS_POPUP, 0, 0, 32, 32,
                nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
            auto composition = endfield::platform::probe_compositors(unused, renderer.graphicsDevice());
            if (unused) DestroyWindow(unused);
            editor_ = std::make_unique<endfield::platform::ProjectedEditor>(); auto& editor = *editor_;
            HRESULT editorStatus = editor.initialize(window_, renderer.textFactory(), []{});
            if(FAILED(editorStatus)) throw std::runtime_error("Synthetic TSF context creation failed");
            editor.set_text(u"English 简体中文 繁體中文 日本語 한국어 😀\nSynthetic TSF fixture");
            editor.set_rectangle(D2D1::RectF(0, 0, 600, 220));
            auto memory = [] {
                PROCESS_MEMORY_COUNTERS_EX result{}; result.cb = sizeof(result);
                GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&result), sizeof(result));
                return result;
            };
            auto drawCycle = [&](unsigned index) {
                double time = index * 3.0;
                playback.open(time); ehud::scene::GyroMotion gyro(doc.initialRootRotation());
                gyro.retarget(desktopEuler(doc.pointerEuler({1100, 120}, {1280, 720})), time, doc.gyroDuration());
                ehud::scene::FrameInput input{{1280, 720}, playback.sample(time + doc.entranceDuration(), false, false), gyro.rotation(time + 1)};
                input.time=time+doc.entranceDuration(); auto frame = desktopFrame(doc,shell,input,true); frame.sceneTime=0;
                const auto mapping = ehud::render::NativeRenderer::editorProjection(frame);
                for (ehud::scene::Vec2 point : {ehud::scene::Vec2{0, 0}, {600, 0}, {600, 220}, {0, 220}}) {
                    auto source = frame.camera.project({point.x - 300, 170 - point.y, 0}, frame.worldRoot);
                    double x{}, y{}, inverseX{}, inverseY{};
                    if (!source || !mapping.project(point.x, point.y, x, y) ||
                        std::hypot(source->x - x, source->y - y) > 1e-7 || !mapping.unproject(x, y, inverseX, inverseY) ||
                        std::hypot(point.x - inverseX, point.y - inverseY) > 1e-7)
                        throw std::runtime_error("Editor/drawing projection mismatch");
                }
                HRESULT status = renderer.draw(frame, editorFixture_?&editor:nullptr);
                if (FAILED(status)) throw std::runtime_error("Native projected frame submission failed");
                playback.close(time + 1); auto closed = playback.sample(time + 1 + doc.exitDuration(), false, false);
                if (closed.phase != ehud::scene::Phase::concealed) throw std::runtime_error("Closing lifecycle failed");
            };
            // Warm caches before comparing bounded lifecycle ownership.
            for (unsigned index = 0; index < 5; ++index) drawCycle(index);
            if (FAILED(renderer.waitForDiagnosticGpu())) throw std::runtime_error("Warm source GPU drain failed");
            auto before = memory(); DWORD handlesBefore{}; GetProcessHandleCount(GetCurrentProcess(), &handlesBefore);
            for (unsigned index = 5; index < 105; ++index) drawCycle(index);
            if (FAILED(renderer.waitForDiagnosticGpu())) throw std::runtime_error("Repeated source GPU drain failed");
            auto after = memory(); DWORD handlesAfter{}; GetProcessHandleCount(GetCurrentProcess(), &handlesAfter);
            if (output_.has_parent_path()) std::filesystem::create_directories(output_.parent_path());
            auto picture = output_; picture.replace_extension(L".bmp");
            if (FAILED(renderer.saveDiagnosticFrame(picture))) throw std::runtime_error("Synthetic render readback failed");
            std::ofstream output(output_); output << std::setprecision(12)
                << "{\n  \"schema\": 1,\n  \"scenario\": \"restarted-desktop-graphics-lifecycle\",\n  \"source_mode\": \"desktop\",\n  \"canonical_resource_baseline\": \"4036174a3facf935260f4d0a9c63bfff33b98c37\",\n  \"synthetic\": true,\n"
                << "  \"elapsed_seconds\": " << std::chrono::duration<double>(Clock::now() - start).count()
                << ",\n  \"direct_composition_hresult\": " << static_cast<long>(composition.direct_composition)
                << ",\n  \"windows_ui_composition_hresult\": " << static_cast<long>(composition.windows_ui_composition)
                << ",\n  \"dispatcher_queue_hresult\": " << static_cast<long>(composition.dispatcher_queue)
                << ",\n  \"tsf_hresult\": " << static_cast<long>(editorStatus)
                << ",\n  \"source_nodes\": " << doc.nodeCount() << ",\n  \"measured_reopen_cycles\": 100,\n  \"warmup_cycles\": 5,\n"
                << "  \"submitted_frames\": " << renderer.submittedFrames() << ",\n  \"private_bytes_before\": " << before.PrivateUsage
                << ",\n  \"private_bytes_after\": " << after.PrivateUsage << ",\n  \"working_set_before\": " << before.WorkingSetSize
                << ",\n  \"working_set_after\": " << after.WorkingSetSize << ",\n  \"handles_before\": " << handlesBefore
                << ",\n  \"handles_after\": " << handlesAfter
                << ",\n  \"editor_projection_corner_checks\": \"passed\",\n  \"visible_frame_pacing\": \"unverified; hidden-window test\",\n"
                << "  \"encoded_premultiplied_alpha\": \"passed; synthetic half-alpha white GPU readback\",\n"
                << "  \"source_textures_retained\": " << renderer.textureCount() << ",\n  \"source_text_surfaces_retained\": " << renderer.textCount()
                << ",\n  \"source_material_visual_parity\": \"unverified; selected source UI/mesh FX translated, complete FX/HDR comparison pending\",\n  \"live_ime\": \"unverified\"\n}\n";
            if (!output) throw std::runtime_error("Cannot write graphics evidence");
            // Return to the actual message loop for the closed-state measure.
            // The retained renderer owns its warmed caches but submits nothing.
            warmClosed_ = true; submittedAtClose_ = renderer.submittedFrames();
            output_ = output_.parent_path() / (output_.stem().wstring() + L".closed.json");
            probeStart_ = Clock::now(); cpuStart_ = cpuSeconds();
            GetProcessHandleCount(GetCurrentProcess(), &handlesStart_);
            SetTimer(window_, probeTimer, 61000, nullptr);
            return;
        } catch (const std::exception& failure) {
            exitCode_ = 5; std::ofstream output(output_); output << "{\"schema\":1,\"result\":\"failed\",\"reason\":\"" << failure.what() << "\"}\n";
        }
        DestroyWindow(window_);
    }
    LRESULT handle(UINT message, WPARAM wparam, LPARAM lparam) {
        if (message == taskbarCreated_ && !probe_) { addTray(); return 0; }
        // Selection drag can consume input, but the shared scene pointer must
        // still follow it before the text editor handles the same event.
        if (message == WM_MOUSEMOVE) {
            pointer_ = {static_cast<double>(GET_X_LPARAM(lparam)), static_cast<double>(GET_Y_LPARAM(lparam))};
            RECT client{}; GetClientRect(window_, &client);
            pointerInside_ = PtInRect(&client, POINT{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)}) != FALSE;
            pointerChanged_ = true; requestFrame();
            updateHovered(now());
            resolveCursor(false,true);
            TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, window_, 0}; TrackMouseEvent(&tracking);
        }
        if ((message == WM_CAPTURECHANGED || message == WM_CANCELMODE) && pressed_ && buttons_) {
            buttons_->setState(hovered_ == pressed_ ? ehud::scene::ButtonState::highlighted : ehud::scene::ButtonState::normal,
                *pressed_, now()); pressed_.reset(); dirty_ = true; requestFrame();
        }
        if(message==WM_SETFOCUS) {cursor_.set_focused(true); resolveCursor(true,true);}
        if(message==WM_KILLFOCUS) cursor_.set_focused(false);
        if(message==WM_SETCURSOR) {
            const auto target=reinterpret_cast<HWND>(wparam); const unsigned hit=LOWORD(lparam);
            if(target==window_ && hit==HTCLIENT && resolveCursor(true)) {SetCursor(LoadCursorW(nullptr,IDC_IBEAM)); return TRUE;}
            if(cursor_.handle_set_cursor(target,hit)) return TRUE;
        }
        if (editor_) {
            LRESULT result{}; const bool handled=editor_->handle_message(message,wparam,lparam,result);
            syncEditorCursorTracking(); if(handled) return result;
        }
        switch (message) {
        case WM_APP + 3: graphicsProbe(); return 0;
        case WM_APP + 2: case WM_HOTKEY: toggle(); return 0;
        case WM_INPUTLANGCHANGE: if (!probe_) registerShortcut(); break;
        case WM_DISPLAYCHANGE: if (playback_ && playback_->phase() != ehud::scene::Phase::concealed) placeOnActiveMonitor(); dirty_ = true; requestFrame(); return 0;
        case WM_DPICHANGED: { auto* rect = reinterpret_cast<RECT*>(lparam); SetWindowPos(window_, HWND_TOPMOST, rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top, SWP_NOACTIVATE); dirty_ = true; requestFrame(); return 0; }
        case WM_SIZE:
            if (renderer_) { HRESULT status = renderer_->resize(LOWORD(lparam), HIWORD(lparam)); if (FAILED(status)) { renderingFailed(status); return 0; } }
            dirty_ = true; requestFrame(); return 0;
        case WM_MOUSEMOVE: return 0;
        case WM_MOUSELEAVE:
            pointerInside_ = false;
            resolveCursor(false,true);
            if (hovered_ && buttons_) { buttons_->setHovered(false, *hovered_, now()); hovered_.reset(); dirty_ = true; requestFrame(); }
            return 0;
        case WM_MOUSEWHEEL:
            if(frame_ && frame_->scroll && playback_ && playback_->phase()==ehud::scene::Phase::visible && frame_->scroll->hiddenLength>0) {
                const auto& scroll=*frame_->scroll;auto node=frame_->node(scroll.viewportId);
                POINT point{GET_X_LPARAM(lparam),GET_Y_LPARAM(lparam)};ScreenToClient(window_,&point);
                if(node && node->rect && frame_->camera.hit({double(point.x),double(point.y)},node->world,*node->rect)) {
                    const auto& rect=*node->rect;
                    auto bottom=frame_->camera.project({rect.origin.x,rect.origin.y,0},node->world);
                    auto top=frame_->camera.project({rect.origin.x,rect.origin.y+rect.size.y,0},node->world);
                    if(bottom && top) {
                        UINT lines=3;SystemParametersInfoW(SPI_GETWHEELSCROLLLINES,0,&lines,0);
                        const double pixels=std::max(1.,std::hypot(top->x-bottom->x,top->y-bottom->y));
                        const double units=rect.size.y/pixels;
                        const double travel=lines==WHEEL_PAGESCROLL?pixels:double(lines)*10*GetDpiForWindow(window_)/96.;
                        const double delta=double(GET_WHEEL_DELTA_WPARAM(wparam))/WHEEL_DELTA*travel*units/std::max(1.,scroll.hiddenLength);
                        scrollMotion_.scroll(delta,scroll.hiddenLength,now());dirty_=true;requestFrame();return 0;
                    }
                }
            } break;
        case WM_LBUTTONDOWN:
            if (frame_ && buttons_ && playback_ && playback_->phase()==ehud::scene::Phase::visible) {
                const ehud::scene::Vec2 pointer{static_cast<double>(GET_X_LPARAM(lparam)), static_cast<double>(GET_Y_LPARAM(lparam))};
                pressed_ = frame_->buttonAt(pointer);
                if (pressed_) { buttons_->setState(ehud::scene::ButtonState::pressed, *pressed_, now()); SetCapture(window_); dirty_ = true; requestFrame(); }
                else if(outsideDesktopCircle(pointer)) close(false);
            } return 0;
        case WM_LBUTTONUP:
            if (pressed_ && buttons_) {
                buttons_->setState(hovered_ == pressed_ ? ehud::scene::ButtonState::highlighted : ehud::scene::ButtonState::normal, *pressed_, now());
                pressed_.reset(); ReleaseCapture(); dirty_ = true; requestFrame();
            } return 0;
        case WM_KEYDOWN: if (wparam == VK_ESCAPE) { close(false); return 0; } break;
        case WM_TIMER: if (wparam == frameTimer) frame(); else if (wparam == probeTimer) writeProbe();
            else if(wparam==clockTimer && playback_ && playback_->phase()!=ehud::scene::Phase::concealed && refreshClock()) {dirty_=true;requestFrame();} return 0;
        case WM_CLOSE: close(false); return 0;
        case WM_POWERBROADCAST: if (wparam == PBT_APMSUSPEND && playback_) {
            if (editor_) editor_->focus(false); playback_->conceal(); frame_.reset();
            cursor_.set_presented(false);
            frameSchedule_.reset();
            cancelPointerInteraction();
            KillTimer(window_, frameTimer);KillTimer(window_,clockTimer); frameTimerRunning_ = false; ShowWindow(window_, SW_HIDE); gyro_.stop(now());
        } return TRUE;
        case trayMessage:
            if (LOWORD(lparam) == NIN_SELECT || LOWORD(lparam) == NIN_KEYSELECT) toggle();
            else if (LOWORD(lparam) == WM_CONTEXTMENU) {
                ++nativeHostTracking_;
                cursor_.begin_native_tracking();
                HMENU menu = CreatePopupMenu(); const auto label = L"Open / hide — " + shortcutLabel_;
                AppendMenuW(menu, MF_STRING, activateCommand, label.c_str()); AppendMenuW(menu, MF_STRING, quitCommand, L"Quit EndfieldHUD feasibility prototype");
                POINT point{}; GetCursorPos(&point); SetForegroundWindow(window_);
                auto command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, point.x, point.y, 0, window_, nullptr); DestroyMenu(menu);
                if (command == activateCommand) toggle(); else if (command == quitCommand && MessageBoxW(window_, L"Quit EndfieldHUD?", L"EndfieldHUD", MB_YESNO | MB_ICONQUESTION) == IDYES) close(true);
                cursor_.end_native_tracking(); --nativeHostTracking_; resolveCursor(true,true);
            } return 0;
        case WM_DESTROY: cursor_.reset(); PostQuitMessage(0); return 0;
        }
        return DefWindowProcW(window_, message, wparam, lparam);
    }
    void cleanup() {
        UnregisterHotKey(window_, 1); KillTimer(window_, frameTimer); KillTimer(window_, probeTimer);KillTimer(window_,clockTimer);
        if (!probe_) { NOTIFYICONDATAW data{sizeof(data)}; data.hWnd = window_; data.uID = 1; Shell_NotifyIconW(NIM_DELETE, &data); }
        cursor_.reset(); editor_.reset(); renderer_.reset(); if (mutex_) CloseHandle(mutex_);
    }
    HWND window_{}; HANDLE mutex_{}; UINT taskbarCreated_{};
    bool probe_{}, graphicsProbe_{}, warmClosed_{}, shortcutReady_{}, dirty_{}, pointerChanged_{}, pointerInside_{}, quitAfterClose_{}, frameTimerRunning_{};
    bool editorFixture_{},ambientEnabled_{true};double closingCanvasOpacity_{1};
    std::optional<ehud::scene::DesktopLanguage> language_;
    std::uint64_t submittedAtClose_{};
    std::filesystem::path output_; unsigned probeSeconds_{60}; int exitCode_{};
    std::wstring shortcutLabel_;
    std::string clockTime_,clockDate_;
    Clock::time_point animationOrigin_{Clock::now()}, probeStart_{}; double cpuStart_{}; DWORD handlesStart_{};
    std::optional<ehud::scene::Document> document_; std::unique_ptr<ehud::scene::Playback> playback_;
    std::unique_ptr<ehud::scene::DesktopShell> shell_;
    std::unique_ptr<ehud::scene::ButtonMotion> buttons_;
    std::optional<ehud::scene::SourceId> hovered_, pressed_;
    std::unique_ptr<ehud::render::NativeRenderer> renderer_;
    std::unique_ptr<endfield::platform::ProjectedEditor> editor_;
    std::optional<ehud::scene::Frame> frame_; ehud::scene::GyroMotion gyro_; ehud::scene::Vec2 pointer_{};
    std::optional<ehud::scene::DesktopShellPresentation> presentation_;
    ehud::app::FrameSchedule frameSchedule_;
    ehud::scene::DesktopScrollMotion scrollMotion_;
    ehud::platform::RenderedCursor cursor_;
    ehud::platform::CursorRegion cursorRegion_{ehud::platform::CursorRegion::outside};
    bool editorPointerTracking_{};
    unsigned nativeHostTracking_{};
};
}
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    HRESULT hr = RoInitialize(RO_INIT_SINGLETHREADED); if (FAILED(hr)) return 1;
    int count{}; auto arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    bool probe = false, graphicsProbe = false, editorFixture = false, ambientEnabled = true; std::filesystem::path output = L"closed-probe.json"; unsigned seconds = 60;
    std::optional<ehud::scene::DesktopLanguage> language;
    for (int index = 1; index < count; ++index) {
        if (std::wstring_view(arguments[index]) == L"--closed-probe") probe = true;
        else if (std::wstring_view(arguments[index]) == L"--graphics-probe") graphicsProbe = true;
        else if (std::wstring_view(arguments[index]) == L"--editor-fixture") editorFixture = true;
        else if (std::wstring_view(arguments[index]) == L"--ambient-off") ambientEnabled = false;
        else if (std::wstring_view(arguments[index]) == L"--language" && index + 1 < count) language=ehud::scene::DesktopShell::resolveLanguage({winrt::to_string(arguments[++index])});
        else if (std::wstring_view(arguments[index]) == L"--output" && index + 1 < count) output = arguments[++index];
        else if (std::wstring_view(arguments[index]) == L"--seconds" && index + 1 < count) seconds = std::clamp<unsigned>(_wtoi(arguments[++index]), 1, 3600);
    }
    LocalFree(arguments); Application app(probe, graphicsProbe, editorFixture, ambientEnabled, output, seconds, language); int result = app.run(instance); RoUninitialize(); return result;
}
