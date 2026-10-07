#include "render/native_renderer.h"
#include "app/frame_schedule.h"
#include "app/backdrop_preparation.h"
#include "platform/rendered_cursor.h"
#include "platform/monitor_snapshot.h"
#include "platform/frozen_desktop_snapshot.h"
#include "scene/desktop_shell.hpp"
#include "scene/desktop_scroll.hpp"
#include <windowsx.h>
#include <shellapi.h>
#include <psapi.h>
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
#include <atomic>
#include <thread>

namespace {
using Clock = std::chrono::steady_clock;
constexpr UINT trayMessage = WM_APP + 1, frameTimer = 1, probeTimer = 2, clockTimer = 3, backdropTimer = 4;
constexpr UINT backdropReadyMessage = WM_APP + 4;
constexpr UINT activateCommand = 100, quitCommand = 101;
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
                         std::optional<ehud::scene::DesktopLanguage> language = {}, unsigned graphicsCycles = 100) :
        probe_(probe || graphicsProbe), graphicsProbe_(graphicsProbe), editorFixture_(editorFixture),
        ambientEnabled_(ambientEnabled), language_(language), output_(std::move(output)), probeSeconds_(seconds), graphicsCycles_(graphicsCycles) {}
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
        windowClass.style = CS_DBLCLKS;
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
            if(editor_ && message.hwnd==window_ && message.message==WM_KEYDOWN && message.wParam==VK_TAB &&
               playback_ && playback_->phase()==ehud::scene::Phase::visible && !editor_->model().composing()) {
                // The isolated fixture has one editable plane. Tab/Shift+Tab
                // alternate its focus with the shell without inserting a tab.
                editor_->focus(!editor_->focused());syncEditorCursorTracking();continue;
            }
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
        input.desktopScrollTarget=scrollMotion_.target();
        auto frame=document.frame(input);presentation_=shell.decorate(frame,fixture);return frame;
    }
    bool scrollDesktopNavigation(int direction) {
        if(!frame_ || !frame_->scroll || !playback_ || playback_->phase()!=ehud::scene::Phase::visible ||
           (direction!=-1 && direction!=1) || !scrollMotion_.canScroll(direction)) return false;
        const auto& scroll=*frame_->scroll;
        auto node=frame_->node(scroll.viewportId);
        if(scroll.hiddenLength<=0 || !node || !node->rect) return false;
        const auto& rect=*node->rect;
        const auto bottom=frame_->camera.project({rect.origin.x,rect.origin.y,0},node->world);
        const auto top=frame_->camera.project({rect.origin.x,rect.origin.y+rect.size.y,0},node->world);
        if(!bottom || !top) return false;
        const double pixels=std::max(1.,std::hypot(top->x-bottom->x,top->y-bottom->y));
        // HUDSourceWatchView.scrollDesktopNavigation advances 32 logical points
        // on the current projected viewport. Win32 pointer coordinates are pixels.
        const double points=32.*GetDpiForWindow(window_)/96.;
        const double delta=-double(direction)*points*rect.size.y/pixels/std::max(1.,scroll.hiddenLength);
        scrollMotion_.scroll(delta,scroll.hiddenLength,now());
        dirty_=true;requestFrame();return true;
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
    bool placeOnSelectedMonitor(bool topologyChange=false) {
        const auto monitors=endfield::platform::collect_monitors();
        std::vector<endfield::platform::MonitorDescriptor> descriptors;
        descriptors.reserve(monitors.size());
        for(const auto& monitor:monitors) descriptors.push_back(monitor.descriptor);
        POINT pointer{};GetCursorPos(&pointer);
        const auto selected=endfield::platform::MonitorPolicy::resolve_index(descriptors,monitorPreference_,
            {double(pointer.x),double(pointer.y)},currentMonitorId_,topologyChange);
        if(!selected) return false;
        const auto& monitor=descriptors[*selected];const auto& area=monitor.bounds;
        currentMonitorId_=monitor.stable_id;
        RECT current{};GetWindowRect(window_,&current);
        // The source system overlay occupies the full display, including the
        // taskbar area. Work rectangles belong to ordinary window placement.
        if(current.left!=area.left || current.top!=area.top || current.right!=area.right || current.bottom!=area.bottom) {
            placingMonitor_=true;
            const bool moved=SetWindowPos(window_,HWND_TOPMOST,area.left,area.top,area.right-area.left,area.bottom-area.top,SWP_NOACTIVATE)!=FALSE;
            placingMonitor_=false;if(!moved) return false;
        }
        POINT local=pointer;ScreenToClient(window_,&local);RECT client{};GetClientRect(window_,&client);
        pointer_={double(local.x),double(local.y)};pointerInside_=PtInRect(&client,local)!=FALSE;
        pointerChanged_=true;dirty_=true;resolveCursor(false,true);return true;
    }
    void toggle() {
        if (probe_) return;
        if(backdropPreparation_.pending()) { cancelBackdropPreparation();return; }
        if (playback_ && playback_->phase() != ehud::scene::Phase::concealed) { close(false); return; }
        // Keep at most one short-lived snapshot worker. A cancelled generation
        // must finish before another capture is admitted; no polling is added.
        if(backdropCapture_ && !backdropCapture_->complete.load(std::memory_order_acquire)) return;
        finishBackdropWorker();
        try {
            if (!document_) {
                auto root = executableRoot() / L"Resources" / L"NativeScene" / L"Scene";
                document_ = ehud::scene::Document::loadDesktop(root);
                shell_ = std::make_unique<ehud::scene::DesktopShell>(*document_);
                gyro_=ehud::scene::GyroMotion(document_->initialRootRotation());
                playback_ = std::make_unique<ehud::scene::Playback>(document_->entranceDuration(), document_->exitDuration());
                buttons_ = std::make_unique<ehud::scene::ButtonMotion>(*document_);
            }
            if(!placeOnSelectedMonitor()) throw std::runtime_error("No available display for the source HUD");
            RECT client{}; GetClientRect(window_, &client);
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
            beginBackdropPreparation();
        } catch (const std::exception& error) {
            cancelBackdropPreparation();
            finishBackdropWorker();
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
    struct BackdropCapture {
        std::uint64_t generation{};
        endfield::platform::MonitorRectangle bounds;
        endfield::platform::FrozenSnapshot snapshot;
        HRESULT status{E_PENDING};
        std::atomic<bool> complete{};
    };
    void finishBackdropWorker() {
        if(backdropWorker_.joinable()) backdropWorker_.join();
        backdropCapture_.reset();
    }
    void cancelBackdropPreparation() {
        backdropPreparation_.cancel();KillTimer(window_,backdropTimer);
        if(backdropWorker_.joinable()) backdropWorker_.request_stop();
        if(renderer_) renderer_->clearFrozenBackdrop();
    }
    void beginBackdropPreparation() {
        RECT bounds{};
        if(IsWindowVisible(window_) || !GetWindowRect(window_,&bounds))
            throw std::runtime_error("The desktop snapshot requires a hidden HUD");
        renderer_->clearFrozenBackdrop();
        auto capture=std::make_shared<BackdropCapture>();
        capture->generation=backdropPreparation_.begin(now());
        capture->bounds={bounds.left,bounds.top,bounds.right,bounds.bottom};
        if(!SetTimer(window_,backdropTimer,static_cast<UINT>(ehud::app::BackdropPreparation::deadlineSeconds*1000),nullptr)) {
            backdropPreparation_.cancel();
            presentBackdropFallback(L"Desktop backdrop timer unavailable; using tint");return;
        }
        backdropCapture_=capture;
        const HWND owner=window_;
        backdropWorker_=std::jthread([capture,owner](std::stop_token stop) {
            try {
                capture->status=endfield::platform::capture_desktop_pre_open(owner,capture->bounds,stop,capture->snapshot);
            } catch(const std::bad_alloc&) {capture->snapshot.reset();capture->status=E_OUTOFMEMORY;}
            catch(...) {capture->snapshot.reset();capture->status=E_FAIL;}
            capture->complete.store(true,std::memory_order_release);
            // The message has no pointer payload. The shared result outlives
            // cancellation and is accepted only by its current generation.
            PostMessageW(owner,backdropReadyMessage,static_cast<WPARAM>(capture->generation),0);
        });
    }
    void finishOpening() {
        RECT client{};GetClientRect(window_,&client);
        buttons_->reset(now());hovered_.reset();pressed_.reset();frameSchedule_.reset();
        scrollMotion_.reset(scrollMotion_.position(),now());refreshClock();
        playback_->open(now());dirty_=true;
        POINT pointer{};GetCursorPos(&pointer);ScreenToClient(window_,&pointer);
        pointer_={double(pointer.x),double(pointer.y)};pointerChanged_=true;
        pointerInside_=PtInRect(&client,pointer)!=FALSE;
        // Commit the new opening pose while still hidden. A retained swapchain
        // must not briefly reveal its previous open's HUD or desktop pixels.
        frame();
        if(!renderer_ || playback_->phase()==ehud::scene::Phase::concealed) return;
        ShowWindow(window_,SW_SHOW);SetForegroundWindow(window_);
        cursor_.set_presented(true);cursor_.set_focused(GetForegroundWindow()==window_);
        resolveCursor(true,true);SetTimer(window_,clockTimer,1000,nullptr);requestFrame();
    }
    void presentBackdropFallback(const wchar_t* reason) {
        KillTimer(window_,backdropTimer);
        if(backdropWorker_.joinable()) backdropWorker_.request_stop();
        if(!renderer_) return;
        renderer_->clearFrozenBackdrop();renderer_->enableFrozenBackdrop(true);
        backdropStatus_=reason;finishOpening();
    }
    void acceptBackdrop(WPARAM generation) {
        if(!backdropCapture_ || backdropCapture_->generation!=generation ||
           !backdropCapture_->complete.load(std::memory_order_acquire)) return;
        auto capture=backdropCapture_;
        finishBackdropWorker();
        if(backdropPreparation_.timed_out(now())) {
            presentBackdropFallback(L"Desktop backdrop timed out; using tint");return;
        }
        if(!renderer_ || !backdropPreparation_.pending() || backdropPreparation_.ticket()!=capture->generation) return;
        RECT current{};
        if(!GetWindowRect(window_,&current) || IsWindowVisible(window_) ||
           current.left!=capture->bounds.left || current.top!=capture->bounds.top ||
           current.right!=capture->bounds.right || current.bottom!=capture->bounds.bottom) {
            cancelBackdropPreparation();
            if(placeOnSelectedMonitor()) presentBackdropFallback(L"Display changed; reopen to refresh the backdrop");
            else backdropStatus_=L"Display unavailable";
            return;
        }
        if(!backdropPreparation_.accept(capture->generation)) return;
        KillTimer(window_,backdropTimer);
        HRESULT status=capture->status;
        if(SUCCEEDED(status)) status=renderer_->setFrozenBackdrop(capture->snapshot);
        if(FAILED(status)) {
            renderer_->clearFrozenBackdrop();
            backdropStatus_=L"Desktop backdrop unavailable; using tint";
        } else backdropStatus_.clear();
        renderer_->enableFrozenBackdrop(true);
        finishOpening();
    }
    void close(bool quit) {
        if(backdropPreparation_.pending()) {
            cancelBackdropPreparation();
            if(quit) DestroyWindow(window_);return;
        }
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
    void concealPresentation() {
        cancelBackdropPreparation();
        if(editor_) editor_->focus(false);
        if(playback_) playback_->conceal();
        frame_.reset();frameSchedule_.reset();cursor_.set_presented(false);
        cancelPointerInteraction();
        KillTimer(window_,frameTimer);KillTimer(window_,clockTimer);frameTimerRunning_=false;
        ShowWindow(window_,SW_HIDE);gyro_.stop(now());
    }
    void cancelPointerInteraction() {
        const double time = now();
        if (pressed_ && buttons_) buttons_->setState(ehud::scene::ButtonState::normal, *pressed_, time);
        if (hovered_ && buttons_) buttons_->setHovered(false, *hovered_, time);
        pressed_.reset(); hovered_.reset(); pointerInside_ = false;
        pressedScrollDirection_.reset();
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
        cancelBackdropPreparation();
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
            renderer_->clearFrozenBackdrop();
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
        const auto processCpu=cpuSeconds()-cpuStart_;
        DWORD handles{}; GetProcessHandleCount(GetCurrentProcess(), &handles);
        PROCESS_MEMORY_COUNTERS_EX memory{}; memory.cb = sizeof(memory);
        GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory));
        const auto monitors=endfield::platform::collect_monitors();
        if (output_.has_parent_path()) std::filesystem::create_directories(output_.parent_path());
        std::ofstream output(output_); output << std::setprecision(12)
            << "{\n  \"schema\": 1,\n  \"scenario\": \"" << (warmClosed_ ? "closed-after-" + std::to_string(graphicsCycles_ + 5) + "-graphics-cycles" : "closed-native-shell")
            << "\",\n  \"synthetic\": true,\n"
            << "  \"elapsed_seconds\": " << elapsed << ",\n  \"process_cpu_seconds\": " << processCpu
            << ",\n  \"hud_frames_submitted\": " << (renderer_ ? renderer_->submittedFrames() - submittedAtClose_ : 0)
            << ",\n  \"renderer_created\": " << (renderer_ ? "true" : "false") << ",\n  \"working_set_bytes\": " << memory.WorkingSetSize
            << ",\n  \"private_bytes\": " << memory.PrivateUsage << ",\n  \"handles_start\": " << handlesStart_
            << ",\n  \"handles_end\": " << handles << ",\n  \"monitors\": [";
        for (std::size_t index = 0; index < monitors.size(); ++index) {
            if (index) output << ','; const auto& entry=monitors[index];const auto& monitor=entry.descriptor;
            output << "{\"left\":" << monitor.bounds.left << ",\"top\":" << monitor.bounds.top
                << ",\"width\":" << monitor.bounds.right-monitor.bounds.left << ",\"height\":" << monitor.bounds.bottom-monitor.bounds.top
                << ",\"dpi_x\":" << monitor.dpi_x << ",\"dpi_y\":" << monitor.dpi_y
                << ",\"persistent_identity_available\":" << (entry.persistent_identity?"true":"false") << '}';
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
            // Compositor capability activation runs in platform_probe.exe.
            // Default source lifecycle diagnostics create no TSF/editor context.
            std::optional<HRESULT> editorStatus;
            if(editorFixture_) {
                editor_ = std::make_unique<endfield::platform::ProjectedEditor>();
                const HRESULT initialized=editor_->initialize(window_,renderer.textFactory(),[]{});
                if(FAILED(initialized))throw std::runtime_error("Synthetic TSF context creation failed");
                editorStatus=editor_->tsf_status();
                if(FAILED(*editorStatus))throw std::runtime_error("Synthetic TSF context status validation failed");
                if(!editor_->set_text(u"English 简体中文 繁體中文 日本語 한국어 😀\nSynthetic TSF fixture"))
                    throw std::runtime_error("Synthetic projected editor fixture text validation failed");
                editor_->set_rectangle(D2D1::RectF(0,0,600,220));
            }
            // Supplied fixture pixels exercise the complete frozen background
            // path without invoking any desktop capture or reading user pixels.
            std::vector<std::uint8_t> syntheticPixels(1280*720*4);
            for(unsigned y=0;y<720;++y) for(unsigned x=0;x<1280;++x) {
                const auto offset=(std::size_t(y)*1280+x)*4;
                const bool square=((x/96)+(y/96))%2;
                syntheticPixels[offset]=static_cast<std::uint8_t>(70+y*100/720);
                syntheticPixels[offset+1]=static_cast<std::uint8_t>(square?100:145);
                syntheticPixels[offset+2]=static_cast<std::uint8_t>(45+x*80/1280);
                syntheticPixels[offset+3]=255;
            }
            endfield::platform::FrozenSnapshot syntheticSnapshot;
            if(FAILED(endfield::platform::FrozenDesktopSnapshot::from_bgra({0,0,1280,720},
                std::move(syntheticPixels),syntheticSnapshot)))
                throw std::runtime_error("Synthetic frozen desktop input validation failed");
            auto memory = [] {
                PROCESS_MEMORY_COUNTERS_EX result{}; result.cb = sizeof(result);
                if(!GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&result), sizeof(result)))
                    throw std::runtime_error("Cannot sample diagnostic process memory");
                return result;
            };
            auto drawCycle = [&](unsigned index) {
                double time = index * 3.0;
                if(FAILED(renderer.setFrozenBackdrop(syntheticSnapshot)))
                    throw std::runtime_error("Synthetic frozen desktop GPU preparation failed");
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
                HRESULT status = renderer.draw(frame,editor_.get());
                if (FAILED(status)) throw std::runtime_error("Native projected frame submission failed");
                playback.close(time + 1); auto closed = playback.sample(time + 1 + doc.exitDuration(), false, false);
                if (closed.phase != ehud::scene::Phase::concealed) throw std::runtime_error("Closing lifecycle failed");
                renderer.clearFrozenBackdrop();
                if(renderer.backdropTextureCount()!=0 || renderer.backdropSnapshotBytes()!=0)
                    throw std::runtime_error("Synthetic close retained owned backdrop pixels or textures");
            };
            // Warm caches before comparing bounded lifecycle ownership.
            constexpr unsigned warmupCycles=5,sampleInterval=100;
            for (unsigned index = 0; index < warmupCycles; ++index) drawCycle(index);
            if (FAILED(renderer.waitForDiagnosticGpu())) throw std::runtime_error("Warm source GPU drain failed");
            const auto measuredStart=Clock::now();const auto measuredCpuStart=cpuSeconds();
            const auto warmupElapsed=std::chrono::duration<double>(measuredStart-start).count();
            const auto texturesBefore=renderer.textureCount(),textBefore=renderer.textCount();
            struct SamplePoint {
                unsigned cycles{};std::uint64_t frames{};PROCESS_MEMORY_COUNTERS_EX memory{};DWORD handles{};
                std::size_t textures{},text{},backdropTextures{},backdropBytes{};
                double elapsed{},batchElapsed{},cpu{},batchCpu{};
            };
            std::vector<SamplePoint> samples;samples.reserve((graphicsCycles_+sampleInterval-1)/sampleInterval+1);
            auto sample=[&](unsigned cycles) {
                SamplePoint point;point.cycles=cycles;point.frames=renderer.submittedFrames();point.memory=memory();
                if(!GetProcessHandleCount(GetCurrentProcess(),&point.handles))throw std::runtime_error("Cannot sample diagnostic process handles");
                point.textures=renderer.textureCount();point.text=renderer.textCount();
                point.backdropTextures=renderer.backdropTextureCount();point.backdropBytes=renderer.backdropSnapshotBytes();
                point.elapsed=std::chrono::duration<double>(Clock::now()-measuredStart).count();point.cpu=cpuSeconds()-measuredCpuStart;
                if(!samples.empty()){point.batchElapsed=point.elapsed-samples.back().elapsed;point.batchCpu=point.cpu-samples.back().cpu;}
                if(point.textures!=texturesBefore || point.text!=textBefore)
                    throw std::runtime_error("Warmed source texture or text cache count changed across synthetic cycles");
                samples.push_back(point);
            };
            sample(0);
            for(unsigned completed=1;completed<=graphicsCycles_;++completed) {
                drawCycle(warmupCycles+completed-1);
                if(completed%sampleInterval==0 || completed==graphicsCycles_) {
                    if(FAILED(renderer.waitForDiagnosticGpu()))throw std::runtime_error("Repeated source GPU drain failed");
                    sample(completed);
                }
            }
            const auto& before=samples.front().memory;const auto& after=samples.back().memory;
            const auto handlesBefore=samples.front().handles,handlesAfter=samples.back().handles;
            if (output_.has_parent_path()) std::filesystem::create_directories(output_.parent_path());
            auto picture = output_; picture.replace_extension(L".bmp");
            if (FAILED(renderer.saveDiagnosticFrame(picture))) throw std::runtime_error("Synthetic render readback failed");
            std::ofstream output(output_); output << std::setprecision(12)
                << "{\n  \"schema\": 1,\n  \"scenario\": \"restarted-desktop-graphics-lifecycle\",\n  \"source_mode\": \"desktop\",\n  \"canonical_resource_baseline\": \"4036174a3facf935260f4d0a9c63bfff33b98c37\",\n  \"synthetic\": true,\n"
                << "  \"elapsed_seconds\": " << std::chrono::duration<double>(Clock::now() - start).count()
                << ",\n  \"composition_capability\": \"measured separately by platform_probe; not initialized in this lifecycle process\""
                << ",\n  \"editor_fixture\": " << (editorFixture_?"true":"false")
                << ",\n  \"editor_fixture_frames\": " << (editorFixture_?renderer.submittedFrames():0)
                << ",\n  \"tsf_scenario\": \"" << (editorFixture_?"synthetic projected editor fixture":"unrequested; source desktop lifecycle only") << '"'
                << ",\n  \"tsf_status\": \"" << (editorStatus?"initialized; synthetic context validated":"unrequested; no editor or TSF context created") << '"'
                << ",\n  \"tsf_hresult\": ";
            if(editorStatus)output << static_cast<long>(*editorStatus);else output << "null";
            output << ",\n  \"source_nodes\": " << doc.nodeCount() << ",\n  \"measured_reopen_cycles\": " << graphicsCycles_ << ",\n  \"warmup_cycles\": " << warmupCycles << ",\n"
                << "  \"submitted_frames\": " << renderer.submittedFrames() << ",\n  \"private_bytes_before\": " << before.PrivateUsage
                << ",\n  \"private_bytes_after\": " << after.PrivateUsage << ",\n  \"working_set_before\": " << before.WorkingSetSize
                << ",\n  \"working_set_after\": " << after.WorkingSetSize << ",\n  \"handles_before\": " << handlesBefore
                << ",\n  \"handles_after\": " << handlesAfter
                << ",\n  \"warmup_elapsed_seconds\": " << warmupElapsed
                << ",\n  \"measured_elapsed_seconds\": " << samples.back().elapsed
                << ",\n  \"measured_process_cpu_seconds\": " << samples.back().cpu
                << ",\n  \"drained_sample_interval_cycles\": " << sampleInterval << ",\n  \"drained_sample_points\": [";
            for(std::size_t index=0;index<samples.size();++index) {
                if(index)output << ',';const auto& point=samples[index];
                output << "\n    {\"measured_cycles\":" << point.cycles << ",\"total_cycles\":" << point.cycles+warmupCycles
                    << ",\"submitted_frames\":" << point.frames
                    << ",\"private_bytes\":" << point.memory.PrivateUsage << ",\"working_set_bytes\":" << point.memory.WorkingSetSize
                    << ",\"handles\":" << point.handles << ",\"source_textures_retained\":" << point.textures
                    << ",\"source_text_surfaces_retained\":" << point.text << ",\"backdrop_textures_after_close\":" << point.backdropTextures
                    << ",\"backdrop_snapshot_bytes_after_close\":" << point.backdropBytes << ",\"elapsed_seconds\":" << point.elapsed
                    << ",\"batch_elapsed_seconds\":" << point.batchElapsed << ",\"process_cpu_seconds\":" << point.cpu
                    << ",\"batch_process_cpu_seconds\":" << point.batchCpu << '}';
            }
            output << "\n  ],\n  \"memory_observations\": \"informational drained process counters; driver/runtime allocations may fluctuate\",\n"
                << "  \"owned_backdrop_release_checks\": " << warmupCycles+graphicsCycles_
                << ",\n  \"source_cache_count_stability\": \"passed across drained samples after warmup\",\n"
                << "  \"editor_projection_corner_checks\": \"passed\",\n  \"editor_projection_check_scope\": \"mathematical project/unproject; independent of TSF/editor rendering\",\n"
                << "  \"visible_frame_pacing\": \"unverified; hidden-window endpoint test, not live acceptance proof\",\n"
                << "  \"encoded_premultiplied_alpha\": \"passed; synthetic half-alpha white GPU readback\",\n"
                << "  \"source_accumulation\": \"linear fragments into BGRA8 sRGB; separate encoded-premultiplied Windows presentation\",\n"
                << "  \"frozen_backdrop\": \"synthetic supplied SDR pixels; no desktop capture API executed\",\n"
                << "  \"backdrop_textures_after_close\": " << renderer.backdropTextureCount()
                << ",\n  \"backdrop_snapshot_bytes_after_close\": " << renderer.backdropSnapshotBytes() << ",\n"
                << "  \"source_textures_retained\": " << renderer.textureCount() << ",\n  \"source_text_surfaces_retained\": " << renderer.textCount()
                << ",\n  \"source_material_visual_parity\": \"unverified; selected source UI/mesh FX translated, complete FX/HDR comparison pending\",\n  \"live_ime\": \"unverified\"\n}\n";
            output.close();
            if (!output) throw std::runtime_error("Cannot write or close graphics evidence");
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
        if(message==WM_CAPTURECHANGED || message==WM_CANCELMODE) pressedScrollDirection_.reset();
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
        case WM_APP + 3: if(graphicsProbe_) graphicsProbe(); return 0;
        case backdropReadyMessage: acceptBackdrop(wparam);return 0;
        case WM_APP + 2: case WM_HOTKEY: toggle(); return 0;
        case WM_INPUTLANGCHANGE: if (!probe_) registerShortcut(); break;
        case WM_DISPLAYCHANGE:
        case WM_DPICHANGED:
            if(placingMonitor_) return 0;
            if(backdropPreparation_.pending()) {cancelBackdropPreparation();return 0;}
            if(playback_ && playback_->phase()!=ehud::scene::Phase::concealed) {
                renderer_->clearFrozenBackdrop();renderer_->enableFrozenBackdrop(true);
                backdropStatus_=L"Display changed; reopen to refresh the backdrop";
                if(!placeOnSelectedMonitor(true)) {
                    concealPresentation();return 0;
                }
                requestFrame();
            }
            return 0;
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
                if(auto direction=frame_->scrollDirectionAt(pointer)) {
                    pressedScrollDirection_=direction;SetCapture(window_);return 0;
                }
                pressed_ = frame_->buttonAt(pointer);
                if (pressed_) { buttons_->setState(ehud::scene::ButtonState::pressed, *pressed_, now()); SetCapture(window_); dirty_ = true; requestFrame(); }
                else if(outsideDesktopCircle(pointer)) close(false);
            } return 0;
        case WM_LBUTTONUP:
            if(pressedScrollDirection_) {
                const int direction=*pressedScrollDirection_;pressedScrollDirection_.reset();
                const ehud::scene::Vec2 pointer{static_cast<double>(GET_X_LPARAM(lparam)),static_cast<double>(GET_Y_LPARAM(lparam))};
                const bool sameArrow=frame_ && frame_->scrollDirectionAt(pointer)==direction;
                ReleaseCapture();
                if(sameArrow) scrollDesktopNavigation(direction);
                return 0;
            }
            if (pressed_ && buttons_) {
                buttons_->setState(hovered_ == pressed_ ? ehud::scene::ButtonState::highlighted : ehud::scene::ButtonState::normal, *pressed_, now());
                pressed_.reset(); ReleaseCapture(); dirty_ = true; requestFrame();
            } return 0;
        case WM_KEYDOWN: if (wparam == VK_ESCAPE) { close(false); return 0; } break;
        case WM_TIMER: if (wparam == frameTimer) frame(); else if (wparam == probeTimer) writeProbe();
            else if(wparam==clockTimer && playback_ && playback_->phase()!=ehud::scene::Phase::concealed && refreshClock()) {dirty_=true;requestFrame();}
            else if(wparam==backdropTimer && backdropPreparation_.timed_out(now())) {
                presentBackdropFallback(L"Desktop backdrop timed out; using tint");
            } return 0;
        case WM_CLOSE: close(false); return 0;
        case WM_POWERBROADCAST: if(wparam==PBT_APMSUSPEND) concealPresentation(); return TRUE;
        case trayMessage:
            if (LOWORD(lparam) == NIN_SELECT || LOWORD(lparam) == NIN_KEYSELECT) toggle();
            else if (LOWORD(lparam) == WM_CONTEXTMENU) {
                ++nativeHostTracking_;
                cursor_.begin_native_tracking();
                HMENU menu = CreatePopupMenu(); const auto label = L"Open / hide — " + shortcutLabel_;
                AppendMenuW(menu, MF_STRING, activateCommand, label.c_str()); AppendMenuW(menu, MF_STRING, quitCommand, L"Quit EndfieldHUD feasibility prototype");
                if(!backdropStatus_.empty()) AppendMenuW(menu,MF_STRING|MF_GRAYED,0,backdropStatus_.c_str());
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
        cancelBackdropPreparation();finishBackdropWorker();
        cursor_.reset(); editor_.reset(); renderer_.reset(); if (mutex_) CloseHandle(mutex_);
    }
    HWND window_{}; HANDLE mutex_{}; UINT taskbarCreated_{};
    bool probe_{}, graphicsProbe_{}, warmClosed_{}, shortcutReady_{}, dirty_{}, pointerChanged_{}, pointerInside_{}, quitAfterClose_{}, frameTimerRunning_{};
    bool editorFixture_{},ambientEnabled_{true};double closingCanvasOpacity_{1};
    std::optional<ehud::scene::DesktopLanguage> language_;
    std::uint64_t submittedAtClose_{};
    std::filesystem::path output_; unsigned probeSeconds_{60},graphicsCycles_{100}; int exitCode_{};
    std::wstring shortcutLabel_;
    std::wstring backdropStatus_;
    std::shared_ptr<BackdropCapture> backdropCapture_;
    std::jthread backdropWorker_;
    ehud::app::BackdropPreparation backdropPreparation_;
    std::string clockTime_,clockDate_;
    Clock::time_point animationOrigin_{Clock::now()}, probeStart_{}; double cpuStart_{}; DWORD handlesStart_{};
    std::optional<ehud::scene::Document> document_; std::unique_ptr<ehud::scene::Playback> playback_;
    std::unique_ptr<ehud::scene::DesktopShell> shell_;
    std::unique_ptr<ehud::scene::ButtonMotion> buttons_;
    std::optional<ehud::scene::SourceId> hovered_, pressed_;
    std::optional<int> pressedScrollDirection_;
    endfield::platform::MonitorPreference monitorPreference_;
    std::optional<std::wstring> currentMonitorId_;
    bool placingMonitor_{};
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
    bool probe = false, graphicsProbe = false, editorFixture = false, ambientEnabled = true; std::filesystem::path output = L"closed-probe.json"; unsigned seconds = 60,graphicsCycles=100;
    bool graphicsCyclesSpecified=false,invalidGraphicsCycles=false;
    std::optional<ehud::scene::DesktopLanguage> language;
    for (int index = 1; index < count; ++index) {
        if (std::wstring_view(arguments[index]) == L"--closed-probe") probe = true;
        else if (std::wstring_view(arguments[index]) == L"--graphics-probe") graphicsProbe = true;
        else if (std::wstring_view(arguments[index]) == L"--graphics-cycles") {
            if(graphicsCyclesSpecified || index+1>=count){invalidGraphicsCycles=true;break;}
            graphicsCyclesSpecified=true;const std::wstring_view value(arguments[++index]);unsigned parsed{};
            if(value.empty()){invalidGraphicsCycles=true;break;}
            for(wchar_t digit:value) {
                if(digit<L'0'||digit>L'9'||parsed>10000/10){invalidGraphicsCycles=true;break;}
                parsed=parsed*10+static_cast<unsigned>(digit-L'0');
                if(parsed>10000){invalidGraphicsCycles=true;break;}
            }
            if(invalidGraphicsCycles || parsed==0){invalidGraphicsCycles=true;break;}
            graphicsCycles=parsed;
        }
        else if(std::wstring_view(arguments[index]).starts_with(L"--graphics-cycles")){invalidGraphicsCycles=true;break;}
        else if (std::wstring_view(arguments[index]) == L"--editor-fixture") editorFixture = true;
        else if (std::wstring_view(arguments[index]) == L"--ambient-off") ambientEnabled = false;
        else if (std::wstring_view(arguments[index]) == L"--language" && index + 1 < count) language=ehud::scene::DesktopShell::resolveLanguage({winrt::to_string(arguments[++index])});
        else if (std::wstring_view(arguments[index]) == L"--output" && index + 1 < count) output = arguments[++index];
        else if (std::wstring_view(arguments[index]) == L"--seconds" && index + 1 < count) seconds = std::clamp<unsigned>(_wtoi(arguments[++index]), 1, 3600);
    }
    LocalFree(arguments);
    if(invalidGraphicsCycles || (graphicsCyclesSpecified&&!graphicsProbe)){RoUninitialize();return 64;}
    Application app(probe, graphicsProbe, editorFixture, ambientEnabled, output, seconds, language, graphicsCycles); int result = app.run(instance); RoUninitialize(); return result;
}
