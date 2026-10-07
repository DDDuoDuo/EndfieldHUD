#include "render/native_renderer.h"
#include "app/frame_schedule.h"
#include "platform/composition_probe.h"
#include <windowsx.h>
#include <shellapi.h>
#include <psapi.h>
#include <shellscalingapi.h>
#include <roapi.h>
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
constexpr UINT trayMessage = WM_APP + 1, frameTimer = 1, probeTimer = 2;
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
    explicit Application(bool probe, bool graphicsProbe, std::filesystem::path output, unsigned seconds) :
        probe_(probe || graphicsProbe), graphicsProbe_(graphicsProbe), output_(std::move(output)), probeSeconds_(seconds) {}
    int run(HINSTANCE instance) {
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        if (!probe_) {
            mutex_ = CreateMutexW(nullptr, FALSE, L"Local\\DDDuoDuo.EndfieldHUD.Windows.Feasibility");
            if (GetLastError() == ERROR_ALREADY_EXISTS) {
                if (auto existing = FindWindowW(L"EndfieldHUD.Windows.Feasibility", nullptr)) PostMessageW(existing, WM_APP + 2, 0, 0);
                return 0;
            }
        }
        WNDCLASSW windowClass{}; windowClass.lpfnWndProc = procedure; windowClass.hInstance = instance;
        windowClass.lpszClassName = L"EndfieldHUD.Windows.Feasibility"; windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
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
                document_ = ehud::scene::Document::load(root);
                playback_ = std::make_unique<ehud::scene::Playback>(document_->entranceDuration(), document_->exitDuration());
                buttons_ = std::make_unique<ehud::scene::ButtonMotion>(*document_);
            }
            placeOnActiveMonitor(); RECT client{}; GetClientRect(window_, &client);
            if (!renderer_) {
                renderer_ = std::make_unique<ehud::render::NativeRenderer>();
                HRESULT hr = renderer_->initialize(window_, client.right, client.bottom);
                if (FAILED(hr)) throw std::runtime_error("Native graphics initialization failed");
                hr = renderer_->loadSourceAssets(executableRoot() / L"Resources" / L"WatchSource");
                if (FAILED(hr)) throw std::runtime_error("Original source sprite/material resource initialization failed");
                editor_ = std::make_unique<endfield::platform::ProjectedEditor>();
                hr = editor_->initialize(window_, renderer_->textFactory(), [this] { dirty_ = true; requestFrame(); });
                if (FAILED(hr)) throw std::runtime_error("TSF text editor initialization failed; projected input is unavailable");
                editor_->set_text(u"English 简体中文 繁體中文 日本語 한국어 😀\nSynthetic editing fixture — no saved user data");
                editor_->set_rectangle(D2D1::RectF(0, 0, 600, 220));
            }
            buttons_->reset(now()); hovered_.reset(); pressed_.reset(); frameSchedule_.reset();
            playback_->open(now()); dirty_ = true;
            POINT pointer{}; GetCursorPos(&pointer); ScreenToClient(window_, &pointer);
            pointer_ = {static_cast<double>(pointer.x), static_cast<double>(pointer.y)}; pointerChanged_ = true;
            pointerInside_ = PtInRect(&client, pointer) != FALSE;
            ShowWindow(window_, SW_SHOW); SetForegroundWindow(window_);
            requestFrame();
        } catch (const std::exception& error) {
            if (playback_) playback_->conceal(); frame_.reset(); frameSchedule_.reset();
            cancelPointerInteraction();
            if (editor_) editor_->focus(false); editor_.reset(); renderer_.reset();
            KillTimer(window_, frameTimer); frameTimerRunning_ = false; ShowWindow(window_, SW_HIDE);
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
        quitAfterClose_ = quit; playback_->close(now()); dirty_ = true; requestFrame();
    }
    void cancelPointerInteraction() {
        const double time = now();
        if (pressed_ && buttons_) buttons_->setState(ehud::scene::ButtonState::normal, *pressed_, time);
        if (hovered_ && buttons_) buttons_->setHovered(false, *hovered_, time);
        pressed_.reset(); hovered_.reset(); pointerInside_ = false;
        if (GetCapture() == window_) ReleaseCapture();
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
        frameSchedule_.reset();
        if (playback_) playback_->conceal(); frame_.reset();
        cancelPointerInteraction();
        if (editor_) editor_->focus(false); editor_.reset(); renderer_.reset();
        ShowWindow(window_, SW_HIDE);
        wchar_t reason[192]{};
        swprintf_s(reason, L"Windows preview rendering stopped (0x%08X). Reopen from the tray to retry. This feasibility build is still incomplete.", static_cast<unsigned>(status));
        MessageBoxW(window_, reason, L"EndfieldHUD Windows preview", MB_OK | MB_ICONERROR);
    }
    void frame() {
        if (!document_ || !renderer_ || !playback_) return;
        const double time = now(); auto sample = playback_->sample(time, false, false);
        if (sample.phase == ehud::scene::Phase::concealed) {
            KillTimer(window_, frameTimer); frameTimerRunning_ = false; ShowWindow(window_, SW_HIDE); frame_.reset(); gyro_.stop(time);
            frameSchedule_.reset();
            if (quitAfterClose_) DestroyWindow(window_); return;
        }
        RECT client{}; GetClientRect(window_, &client);
        ehud::scene::Vec2 viewport{static_cast<double>(client.right), static_cast<double>(client.bottom)};
        if (pointerChanged_) {
            gyro_.retarget(document_->pointerEuler(pointer_, viewport), time, document_->gyroDuration()); pointerChanged_ = false;
        }
        gyro_.finishIfNeeded(time);
        const bool buttonAnimation = buttons_ && buttons_->requiresFrames(time);
        const auto decision = frameSchedule_.next(sample.phase, dirty_, gyro_.animating(), buttonAnimation);
        if (!decision.submit) {
            KillTimer(window_, frameTimer); frameTimerRunning_ = false; return;
        }
        if (frame_ && !decision.rebuild)
            document_->reproject(*frame_, gyro_.rotation(time));
        else {
            ehud::scene::FrameInput input{viewport, sample, gyro_.rotation(time)};
            input.interaction = buttons_.get(); input.time = time;
            frame_ = document_->frame(input);
        }
        // Opening and depth/tilt motion can move a button under a stationary
        // pointer. Resolve hover against this frame, using its same clock.
        if (updateHovered(time)) {
            ehud::scene::FrameInput input{viewport, sample, gyro_.rotation(time)};
            input.interaction = buttons_.get(); input.time = time;
            frame_ = document_->frame(input);
        }
        HRESULT hr = renderer_->draw(*frame_, editor_.get()); dirty_ = false;
        if (FAILED(hr)) renderingFailed(hr);
        else frameSchedule_.submitted(sample.phase, gyro_.animating(), buttons_ && buttons_->requiresFrames(time));
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
            auto doc = ehud::scene::Document::load(executableRoot() / L"Resources" / L"NativeScene" / L"Scene");
            ehud::scene::Playback playback(doc.entranceDuration(), doc.exitDuration());
            renderer_ = std::make_unique<ehud::render::NativeRenderer>(); auto& renderer = *renderer_;
            HRESULT renderStatus = renderer.initialize(window_, 1280, 720);
            if (FAILED(renderStatus)) throw std::runtime_error("D3D11/DirectComposition renderer creation failed");
            renderStatus = renderer.loadSourceAssets(executableRoot() / L"Resources" / L"WatchSource");
            if (FAILED(renderStatus)) throw std::runtime_error("Original source sprite resource initialization failed");
            if (FAILED(renderer.verifyDiagnosticAlpha())) throw std::runtime_error("D3D/D2D encoded premultiplied alpha contract failed");
            HWND unused = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP | WS_EX_TOOLWINDOW,
                L"EndfieldHUD.Windows.Feasibility", L"Composition capability probe", WS_POPUP, 0, 0, 32, 32,
                nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
            auto composition = endfield::platform::probe_compositors(unused, renderer.graphicsDevice());
            if (unused) DestroyWindow(unused);
            editor_ = std::make_unique<endfield::platform::ProjectedEditor>(); auto& editor = *editor_;
            HRESULT editorStatus = editor.initialize(window_, renderer.textFactory(), []{});
            editor.set_text(u"English 简体中文 繁體中文 日本語 한국어 😀\nSynthetic TSF fixture");
            editor.set_rectangle(D2D1::RectF(0, 0, 600, 220));
            auto memory = [] {
                PROCESS_MEMORY_COUNTERS_EX result{}; result.cb = sizeof(result);
                GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&result), sizeof(result));
                return result;
            };
            auto drawCycle = [&](unsigned index) {
                double time = index * 3.0;
                playback.open(time); ehud::scene::GyroMotion gyro;
                gyro.retarget(doc.pointerEuler({1100, 120}, {1280, 720}), time, doc.gyroDuration());
                auto frame = doc.frame({{1280, 720}, playback.sample(time + doc.entranceDuration(), false, false), gyro.rotation(time + 1)});
                const auto mapping = ehud::render::NativeRenderer::editorProjection(frame);
                for (ehud::scene::Vec2 point : {ehud::scene::Vec2{0, 0}, {600, 0}, {600, 220}, {0, 220}}) {
                    auto source = frame.camera.project({point.x - 300, 170 - point.y, 0}, frame.worldRoot);
                    double x{}, y{}, inverseX{}, inverseY{};
                    if (!source || !mapping.project(point.x, point.y, x, y) ||
                        std::hypot(source->x - x, source->y - y) > 1e-7 || !mapping.unproject(x, y, inverseX, inverseY) ||
                        std::hypot(point.x - inverseX, point.y - inverseY) > 1e-7)
                        throw std::runtime_error("Editor/drawing projection mismatch");
                }
                HRESULT status = renderer.draw(frame, &editor);
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
                << "{\n  \"schema\": 1,\n  \"scenario\": \"hidden-native-graphics-lifecycle\",\n  \"synthetic\": true,\n"
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
                << ",\n  \"source_material_visual_parity\": \"unverified; base source image path implemented, FX/HDR incomplete\",\n  \"live_ime\": \"unverified\"\n}\n";
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
            TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, window_, 0}; TrackMouseEvent(&tracking);
        }
        if ((message == WM_CAPTURECHANGED || message == WM_CANCELMODE) && pressed_ && buttons_) {
            buttons_->setState(hovered_ == pressed_ ? ehud::scene::ButtonState::highlighted : ehud::scene::ButtonState::normal,
                *pressed_, now()); pressed_.reset(); dirty_ = true; requestFrame();
        }
        if (editor_) { LRESULT result{}; if (editor_->handle_message(message, wparam, lparam, result)) return result; }
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
            if (hovered_ && buttons_) { buttons_->setHovered(false, *hovered_, now()); hovered_.reset(); dirty_ = true; requestFrame(); }
            return 0;
        case WM_LBUTTONDOWN:
            if (frame_ && buttons_) {
                pressed_ = frame_->buttonAt({static_cast<double>(GET_X_LPARAM(lparam)), static_cast<double>(GET_Y_LPARAM(lparam))});
                if (pressed_) { buttons_->setState(ehud::scene::ButtonState::pressed, *pressed_, now()); SetCapture(window_); dirty_ = true; requestFrame(); }
            } return 0;
        case WM_LBUTTONUP:
            if (pressed_ && buttons_) {
                buttons_->setState(hovered_ == pressed_ ? ehud::scene::ButtonState::highlighted : ehud::scene::ButtonState::normal, *pressed_, now());
                pressed_.reset(); ReleaseCapture(); dirty_ = true; requestFrame();
            } return 0;
        case WM_KEYDOWN: if (wparam == VK_ESCAPE) { close(false); return 0; } break;
        case WM_TIMER: if (wparam == frameTimer) frame(); else if (wparam == probeTimer) writeProbe(); return 0;
        case WM_CLOSE: close(false); return 0;
        case WM_POWERBROADCAST: if (wparam == PBT_APMSUSPEND && playback_) {
            if (editor_) editor_->focus(false); playback_->conceal(); frame_.reset();
            frameSchedule_.reset();
            cancelPointerInteraction();
            KillTimer(window_, frameTimer); frameTimerRunning_ = false; ShowWindow(window_, SW_HIDE); gyro_.stop(now());
        } return TRUE;
        case trayMessage:
            if (LOWORD(lparam) == NIN_SELECT || LOWORD(lparam) == NIN_KEYSELECT) toggle();
            else if (LOWORD(lparam) == WM_CONTEXTMENU) {
                HMENU menu = CreatePopupMenu(); const auto label = L"Open / hide — " + shortcutLabel_;
                AppendMenuW(menu, MF_STRING, activateCommand, label.c_str()); AppendMenuW(menu, MF_STRING, quitCommand, L"Quit EndfieldHUD feasibility prototype");
                POINT point{}; GetCursorPos(&point); SetForegroundWindow(window_);
                auto command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, point.x, point.y, 0, window_, nullptr); DestroyMenu(menu);
                if (command == activateCommand) toggle(); else if (command == quitCommand && MessageBoxW(window_, L"Quit EndfieldHUD?", L"EndfieldHUD", MB_YESNO | MB_ICONQUESTION) == IDYES) close(true);
            } return 0;
        case WM_DESTROY: PostQuitMessage(0); return 0;
        }
        return DefWindowProcW(window_, message, wparam, lparam);
    }
    void cleanup() {
        UnregisterHotKey(window_, 1); KillTimer(window_, frameTimer); KillTimer(window_, probeTimer);
        if (!probe_) { NOTIFYICONDATAW data{sizeof(data)}; data.hWnd = window_; data.uID = 1; Shell_NotifyIconW(NIM_DELETE, &data); }
        editor_.reset(); renderer_.reset(); if (mutex_) CloseHandle(mutex_);
    }
    HWND window_{}; HANDLE mutex_{}; UINT taskbarCreated_{};
    bool probe_{}, graphicsProbe_{}, warmClosed_{}, shortcutReady_{}, dirty_{}, pointerChanged_{}, pointerInside_{}, quitAfterClose_{}, frameTimerRunning_{};
    std::uint64_t submittedAtClose_{};
    std::filesystem::path output_; unsigned probeSeconds_{60}; int exitCode_{};
    std::wstring shortcutLabel_;
    Clock::time_point animationOrigin_{Clock::now()}, probeStart_{}; double cpuStart_{}; DWORD handlesStart_{};
    std::optional<ehud::scene::Document> document_; std::unique_ptr<ehud::scene::Playback> playback_;
    std::unique_ptr<ehud::scene::ButtonMotion> buttons_;
    std::optional<ehud::scene::SourceId> hovered_, pressed_;
    std::unique_ptr<ehud::render::NativeRenderer> renderer_;
    std::unique_ptr<endfield::platform::ProjectedEditor> editor_;
    std::optional<ehud::scene::Frame> frame_; ehud::scene::GyroMotion gyro_; ehud::scene::Vec2 pointer_{};
    ehud::app::FrameSchedule frameSchedule_;
};
}
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    HRESULT hr = RoInitialize(RO_INIT_SINGLETHREADED); if (FAILED(hr)) return 1;
    int count{}; auto arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    bool probe = false, graphicsProbe = false; std::filesystem::path output = L"closed-probe.json"; unsigned seconds = 60;
    for (int index = 1; index < count; ++index) {
        if (std::wstring_view(arguments[index]) == L"--closed-probe") probe = true;
        else if (std::wstring_view(arguments[index]) == L"--graphics-probe") graphicsProbe = true;
        else if (std::wstring_view(arguments[index]) == L"--output" && index + 1 < count) output = arguments[++index];
        else if (std::wstring_view(arguments[index]) == L"--seconds" && index + 1 < count) seconds = std::clamp<unsigned>(_wtoi(arguments[++index]), 1, 3600);
    }
    LocalFree(arguments); Application app(probe, graphicsProbe, output, seconds); int result = app.run(instance); RoUninitialize(); return result;
}
