#include "native/charge_indicator_panel.hpp"
#ifdef _WIN32
#include "native/charge_alert_automation.hpp"
#include "native/layer_scene.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#include <shellscalingapi.h>
#include <commctrl.h>
#include <uiautomation.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace endfield::native {
namespace {
namespace m = modules;
using Json = ehud::data::Json;
using Matrix = core::Matrix4;
constexpr wchar_t windowClassName[] = L"EndfieldHUDChargeAlertPanel";
// A UI Automation press, posted to this panel's own window (never the HUD's).
constexpr UINT invokeMessage = WM_APP + 0x0C41;
// A presentation postpones by at most this much: a first frame stalled
// longer (a blocked UI thread) still plays its entrance from the start, but
// the alert's deadlines never drift further from the host clock.
constexpr double maximumFirstFrameLag = 1.0;

// Monotonic seconds (QueryPerformanceCounter) for the panel's own timings.
double clockSeconds() noexcept {
    LARGE_INTEGER frequency{}, counter{};
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&counter);
    return frequency.QuadPart ? static_cast<double>(counter.QuadPart) / static_cast<double>(frequency.QuadPart) : 0;
}

std::wstring wide(const std::string& text) {
    if (text.empty()) return {};
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(std::max(count, 0)), L'\0');
    if (count > 0) MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), out.data(), count);
    return out;
}
double srgbToLinear(double c) {
    c = std::clamp(c, 0.0, 1.0);
    return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}
std::array<float, 4> tint(const m::ChargeColor& c) {
    return {static_cast<float>(srgbToLinear(c[0])), static_cast<float>(srgbToLinear(c[1])), static_cast<float>(srgbToLinear(c[2])),
            static_cast<float>(std::clamp(c[3], 0.0, 1.0))};
}
bool inside(const core::Rect& r, core::Point p) { return p.x >= r.x && p.y >= r.y && p.x < r.x + r.width && p.y < r.y + r.height; }
// Opts a renderer into a device without driver worker threads once
// RendererOptions offers it (detected at compile time; no effect before).
template <class Options> void preferThreadFreeDevice(Options& options) {
    if constexpr (requires { options.preventDriverThreads = true; }) options.preventDriverThreads = true;
}
std::string narrow(const wchar_t* text) {
    if (!text || !*text) return {};
    const int count = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(std::max(count, 1) - 1), '\0');
    if (count > 1) WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), count, nullptr, nullptr);
    return out;
}

} // namespace

std::uint32_t chargeAlertWindowStyle(bool clickThroughLayered) noexcept {
    return WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_NOREDIRECTIONBITMAP |
           (clickThroughLayered ? WS_EX_LAYERED | WS_EX_TRANSPARENT : 0);
}
// Work areas in physical virtual-desktop pixels (per-monitor-v2 process),
// stable monitor identity and the internal panel (Mac prefers built-in).
ChargeScreenScan scanChargeScreens() {
    struct Path { std::wstring gdi, device; bool internal{}; };
    std::vector<Path> paths;
    UINT32 pathCount = 0, modeCount = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount) == ERROR_SUCCESS && pathCount && pathCount < 64) {
        std::vector<DISPLAYCONFIG_PATH_INFO> info(pathCount);
        std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
        if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, info.data(), &modeCount, modes.data(), nullptr) == ERROR_SUCCESS) {
            for (UINT32 n = 0; n < pathCount; ++n) {
                DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
                source.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
                source.header.size = sizeof source;
                source.header.adapterId = info[n].sourceInfo.adapterId;
                source.header.id = info[n].sourceInfo.id;
                if (DisplayConfigGetDeviceInfo(&source.header) != ERROR_SUCCESS) continue;
                DISPLAYCONFIG_TARGET_DEVICE_NAME target{};
                target.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
                target.header.size = sizeof target;
                target.header.adapterId = info[n].targetInfo.adapterId;
                target.header.id = info[n].targetInfo.id;
                Path path{source.viewGdiDeviceName, {}, false};
                if (DisplayConfigGetDeviceInfo(&target.header) == ERROR_SUCCESS) path.device = target.monitorDevicePath;
                const auto technology = info[n].targetInfo.outputTechnology;
                path.internal = technology == DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INTERNAL ||
                                technology == DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DISPLAYPORT_EMBEDDED ||
                                technology == DISPLAYCONFIG_OUTPUT_TECHNOLOGY_UDI_EMBEDDED;
                paths.push_back(std::move(path));
            }
        }
    }
    struct Context { ChargeScreenScan scan; std::vector<Path>* paths; std::optional<std::size_t> primary; } context{{}, &paths, {}};
    EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR monitor, HDC, LPRECT, LPARAM data) -> BOOL {
        auto& c = *reinterpret_cast<Context*>(data);
        if (c.scan.screens.size() >= 32) return FALSE;
        MONITORINFOEXW info{};
        info.cbSize = sizeof info;
        if (!GetMonitorInfoW(monitor, &info)) return TRUE;
        UINT dx = 96, dy = 96;
        if (FAILED(GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dx, &dy)) || !dx) dx = 96;
        std::wstring identity = info.szDevice;
        bool internal = false;
        for (const auto& path : *c.paths)
            if (path.gdi == info.szDevice) { if (!path.device.empty()) identity = path.device; internal = path.internal; break; }
        const std::u16string units(identity.begin(), identity.end());
        const auto& r = info.rcWork;
        c.scan.screens.push_back({m::chargeScreenID(units), {static_cast<double>(r.left), static_cast<double>(r.top),
                                  static_cast<double>(r.right - r.left), static_cast<double>(r.bottom - r.top)}, dx / 96.0});
        c.scan.identities.push_back(identity);
        if (internal && !c.scan.preferred) c.scan.preferred = c.scan.screens.size() - 1;
        if ((info.dwFlags & MONITORINFOF_PRIMARY) && !c.primary) c.primary = c.scan.screens.size() - 1;
        return TRUE;
    }, reinterpret_cast<LPARAM>(&context));
    if (!context.scan.preferred) context.scan.preferred = context.primary;
    return std::move(context.scan);
}

struct ChargeIndicatorPanel::Impl {
    LayerRasterizer* raster;
    ChargePanelOptions options;
    PositionFinished finished;
    m::ChargeOverlayState state;
    m::ChargeIndicatorAppearance appearance;
    std::optional<m::ChargeTelemetry> telemetry;
    ChargeIndicatorScene scene;
    LayerScene anchor, buttonAnchor;
    LayerComposition composition;
    Renderer renderer;
    HWND window{};
    bool layered{}, shown{}, initialized{}, published{}, editingStyle{}, dirty{true}, animatedLastFrame{};
    std::array<DrawObject, 4> buttons; // cancel circle, cancel symbol, confirm circle, confirm symbol
    std::array<std::string, 3> buttonTextures;
    std::array<std::shared_ptr<const LayerRasterImage>, 3> buttonImages;
    std::array<std::uint64_t, 3> buttonUploaded{};
    std::uint64_t buttonRevision{};
    double buttonDensity{};
    std::string meshID;
    bool meshUploaded{};
    std::optional<std::size_t> pressedButton;
    bool buttonHighlighted{};
    std::uint64_t contentRevision{};
    bool contentDirty{true};
    std::optional<m::ChargePanelLayout> layout;
    double time{};
    std::string label, titled, failure;
    std::optional<std::uint64_t> failedGeneration;
    // Created on the first WM_GETOBJECT for the window's UIA root.
    std::unique_ptr<ChargeAlertAutomation> automation;
    // The editor buttons' tool tips (NSButton.toolTip): one tooltip control
    // with a subclassed tool per button, created on the first edit.
    HWND tooltip{};
    bool tooltipActive{}, tooltipAttempted{};
    std::array<std::wstring, 2> tipText;
    std::array<RECT, 2> tipRects{};
    // Residency counters (creations, parks, device recoveries).
    std::uint64_t creations{}, parks{}, recoveries{}, postponements{};
    double creationMilliseconds{}, firstFrameMilliseconds{}, postponedMilliseconds{};
    // The state was visible on the previous frame: the next frame continues
    // a presentation instead of starting one.
    bool presenting{};
    // Nesting of this panel's window procedure: the window is never parked
    // from inside its own message (a press or key that ends an edit).
    unsigned procedureDepth{};

    Impl(LayerRasterizer& r, ChargePanelOptions o, PositionFinished f)
        : raster(&r), options(std::move(o)), finished(std::move(f)), scene(r, "charge.alert"), anchor(r), buttonAnchor(r) {
        const Json empty = Json::Object{{"bounds", Json::Array{0, 0, 1, 1}}, {"children", Json::Array{}}};
        anchor.load(empty, {});
        buttonAnchor.load(empty, {});
        meshID = "charge.alert/buttonQuad";
        buttonTextures = {"charge.alert/buttonCircle", "charge.alert/buttonCancel", "charge.alert/buttonConfirm"};
        for (std::size_t n = 0; n < buttons.size(); ++n) {
            buttons[n].sourceID = "charge.alert/button" + std::to_string(n);
            buttons[n].meshID = meshID;
            buttons[n].textureID = buttonTextures[n % 2 == 0 ? 0 : n == 1 ? 1 : 2];
            buttons[n].opacity = 0;
        }
    }
    ~Impl() { destroy(); }

    static LRESULT CALLBACK procedure(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
        if (message == WM_NCCREATE) {
            const auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        }
        auto* self = reinterpret_cast<Impl*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (!self) return DefWindowProcW(hwnd, message, wparam, lparam);
        struct Depth {
            unsigned& value;
            explicit Depth(unsigned& v) noexcept : value(v) { ++value; }
            ~Depth() { --value; }
            Depth(const Depth&) = delete;
            Depth& operator=(const Depth&) = delete;
        } depth(self->procedureDepth);
        try {
            switch (message) {
            case WM_MOUSEACTIVATE: return self->state.editingPosition() ? MA_ACTIVATE : MA_NOACTIVATE;
            case WM_NCHITTEST: return self->state.editingPosition() ? HTCLIENT : HTTRANSPARENT;
            case WM_LBUTTONDOWN: case WM_MOUSEMOVE: case WM_LBUTTONUP: {
                POINT p{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
                ClientToScreen(hwnd, &p);
                const core::Point screen{static_cast<double>(p.x), static_cast<double>(p.y)};
                if (message == WM_LBUTTONDOWN) { SetCapture(hwnd); self->input(PointerKind::down, screen); }
                else if (message == WM_MOUSEMOVE) self->input(PointerKind::move, screen);
                else { self->input(PointerKind::up, screen); if (GetCapture() == hwnd) ReleaseCapture(); }
                return 0;
            }
            case WM_CAPTURECHANGED: self->input(PointerKind::cancel, {}); return 0;
            case WM_KEYDOWN: case WM_SYSKEYDOWN:
                if (self->keyDown(static_cast<unsigned>(wparam))) return 0;
                break;
            case WM_CLOSE:
                // Alt+F4 on the editor discards, like Escape; the alert never closes itself.
                if (self->state.editingPosition()) self->discard();
                return 0;
            case WM_DISPLAYCHANGE: case WM_DPICHANGED:
                self->screensChanged();
                return 0;
            case WM_GETOBJECT:
                // Only the panel's current window (not one being created or closed).
                if (hwnd == self->window && ChargeAlertAutomation::rootRequest(lparam)) return self->automationFor(hwnd).answer(wparam, lparam);
                break;
            case invokeMessage:
                self->automationInvoke(static_cast<std::size_t>(wparam), static_cast<std::uint64_t>(lparam));
                return 0;
            case WM_SETTINGCHANGE:
                if (wparam == SPI_SETWORKAREA) self->screensChanged();
                break;
            default: break;
            }
        } catch (...) {
            // Input/notification failures must not escape the window procedure.
        }
        return DefWindowProcW(hwnd, message, wparam, lparam);
    }

    void ensureWindow() {
        if (window) return;
        static const ATOM atom = [] {
            WNDCLASSEXW c{};
            c.cbSize = sizeof c;
            c.lpfnWndProc = procedure;
            c.hInstance = GetModuleHandleW(nullptr);
            c.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            c.lpszClassName = windowClassName;
            return RegisterClassExW(&c);
        }();
        if (!atom && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) throw std::runtime_error("Register charge alert window class");
        const auto r = layout ? layout->window : core::Rect{0, 0, 300, 84};
        const int w = std::max(1, static_cast<int>(std::lround(r.width))), h = std::max(1, static_cast<int>(std::lround(r.height)));
        auto create = [&](bool clickThroughLayered) {
            const DWORD style = chargeAlertWindowStyle(clickThroughLayered);
            HWND hwnd = CreateWindowExW(style, windowClassName, L"EndfieldHUD", WS_POPUP, static_cast<int>(std::lround(r.x)),
                                        static_cast<int>(std::lround(r.y)), w, h, nullptr, nullptr, GetModuleHandleW(nullptr), this);
            if (hwnd && clickThroughLayered) SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);
            return hwnd;
        };
        const double start = clockSeconds();
        RendererOptions rendererOptions;
        rendererOptions.shaderPath = options.shader;
        // A few dozen quads need no driver worker threads: once the renderer
        // offers D3D11_CREATE_DEVICE_PREVENT_INTERNAL_THREADING_OPTIMIZATIONS
        // (requested from the host; about half of a device's driver threads
        // on the test laptop), the alert's device opts in.
        preferThreadFreeDevice(rendererOptions);
        if (options.hiddenForTests) {
            rendererOptions.driver = options.hardwareForProbe ? Driver::hardware : Driver::warpForTests;
            rendererOptions.target = RenderTarget::offscreenForTests;
        }
        // Click-through layered first; a compositor that refuses a layered
        // target falls back to hit-test transparency (same-thread only).
        for (const bool clickThrough : {true, false}) {
            window = create(clickThrough);
            if (!window) continue;
            try {
                renderer.initialize(window, static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(h), rendererOptions);
                layered = clickThrough;
                initialized = true;
                break;
            } catch (...) {
                closeWindow();
                if (!clickThrough) throw;
            }
        }
        if (!window) throw std::runtime_error("Create charge alert window");
        titled.clear();
        published = false;
        dirty = true;
        creationMilliseconds = 1000.0 * (clockSeconds() - start);
        ++creations;
    }
    // UIA clients and the tool tips go with the window; messages arriving
    // while it is destroyed no longer reach this panel.
    void closeWindow() noexcept {
        if (automation) { automation->disconnect(); automation.reset(); }
        if (tooltip) { DestroyWindow(tooltip); tooltip = nullptr; }
        tooltipActive = tooltipAttempted = false;
        editingStyle = false; // a new window starts click-through
        if (!window) return;
        const HWND closing = std::exchange(window, nullptr);
        SetWindowLongPtrW(closing, GWLP_USERDATA, 0);
        DestroyWindow(closing);
    }
    void destroy() noexcept {
        if (automation) { automation->disconnect(); automation.reset(); }
        if (initialized) {
            // Each step on its own: a lost or reset renderer may refuse one,
            // and the scene still marks everything for reinstallation.
            try { composition.detach(renderer); } catch (...) {}
            (void)scene.release(renderer);
            for (std::size_t n = 0; n < buttonTextures.size(); ++n)
                if (buttonUploaded[n]) try { (void)renderer.removeTexture(buttonTextures[n]); } catch (...) {}
            if (meshUploaded) try { (void)renderer.removeMesh(meshID); } catch (...) {}
        }
        buttonUploaded = {};
        meshUploaded = false;
        renderer.reset();
        initialized = false;
        published = false;
        closeWindow();
        shown = false;
    }

    double unit() const {
        const auto screen = state.selectedScreen();
        return screen ? screen->pixelsPerPoint : 1;
    }
    void content() {
        const double density = state.settings().scale * unit();
        ChargeIndicatorSceneContent key;
        key.appearance = appearance;
        key.battery = state.reading();
        key.metric = state.settings().metric;
        key.telemetry = m::chargeMetricRequiresTelemetry(key.metric) ? telemetry : std::nullopt;
        key.preview = state.previewContent();
        key.pixelsPerPoint = std::clamp(std::ceil(density * 4) / 4, 1.0, 4.0);
        if (scene.setContent(key)) dirty = true;
        const auto& accessibility = scene.content().accessibility;
        if (accessibility != label) label = accessibility;
        // Content events only: the label, the language of the button texts.
        syncToolTips();
        if (automation) automation->changed();
        // Edit controls: 28-point buttons at the monitor density.
        const double controlDensity = std::clamp(std::ceil(unit() * 4) / 4, 1.0, 4.0);
        if (controlDensity != buttonDensity) {
            LayerRasterOptions rasterOptions;
            rasterOptions.pixelsPerPoint = controlDensity;
            rasterOptions.paddingPoints = 1;
            ++buttonRevision;
            const std::array<Json, 3> layers{m::chargePositionButtonCircleLayer(), m::chargePositionButtonSymbolLayer(false),
                                             m::chargePositionButtonSymbolLayer(true)};
            for (std::size_t n = 0; n < 3; ++n) {
                buttonImages[n] = raster->rasterize(buttonTextures[n], buttonRevision, layers[n], rasterOptions);
                if (!buttonImages[n] || !buttonImages[n]->complete()) throw std::runtime_error("Unsupported position button artwork");
            }
            buttonDensity = controlDensity;
            dirty = true;
        }
    }
    void positionWindow() {
        const auto next = state.layout();
        if (!next) return;
        const bool sizeChanged = !layout || layout->window.width != next->window.width || layout->window.height != next->window.height;
        const bool moved = !layout || layout->window.x != next->window.x || layout->window.y != next->window.y;
        layout = next;
        if (!window || !(sizeChanged || moved)) return;
        const auto& r = layout->window;
        const int w = std::max(1, static_cast<int>(std::lround(r.width))), h = std::max(1, static_cast<int>(std::lround(r.height)));
        SetWindowPos(window, HWND_TOPMOST, static_cast<int>(std::lround(r.x)), static_cast<int>(std::lround(r.y)), w, h,
                     SWP_NOACTIVATE | SWP_NOOWNERZORDER);
        if (sizeChanged) { renderer.resize(static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(h)); syncToolTips(); }
        dirty = true;
    }
    void setEditingStyle(bool editing) {
        if (!window || editing == editingStyle) return;
        auto style = static_cast<DWORD>(GetWindowLongPtrW(window, GWL_EXSTYLE));
        if (editing) style &= ~static_cast<DWORD>(WS_EX_TRANSPARENT | WS_EX_NOACTIVATE);
        else style |= WS_EX_NOACTIVATE | (layered ? WS_EX_TRANSPARENT : 0);
        SetWindowLongPtrW(window, GWL_EXSTYLE, static_cast<LONG_PTR>(style));
        SetWindowPos(window, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        editingStyle = editing;
        if (editing && !options.hiddenForTests) { SetForegroundWindow(window); SetFocus(window); }
        // The buttons appear or hide (PositionActionButton.isHidden).
        if (editing) ensureToolTips();
        syncToolTips();
        if (automation) automation->changed();
    }
    // --- Tool tips ---------------------------------------------------------------
    void ensureToolTips() {
        if (tooltip || tooltipAttempted || !window) return;
        tooltipAttempted = true; // one attempt per window: tool tips are optional
        INITCOMMONCONTROLSEX controls{sizeof controls, ICC_TAB_CLASSES | ICC_BAR_CLASSES};
        (void)InitCommonControlsEx(&controls);
        tooltip = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
                                  CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, window, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!tooltip) return;
        for (std::size_t b = 0; b < 2; ++b) {
            tipText[b] = wide(m::chargePositionButtonTooltip(b == 1, appearance.language));
            tipRects[b] = toolRect(b);
            TTTOOLINFOW tool{};
            tool.cbSize = static_cast<UINT>(TTTOOLINFOW_V2_SIZE); // also accepted by comctl32 5.x
            tool.uFlags = TTF_SUBCLASS;
            tool.hwnd = window;
            tool.uId = b + 1;
            tool.rect = tipRects[b];
            tool.lpszText = tipText[b].data();
            if (!SendMessageW(tooltip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool))) { DestroyWindow(tooltip); tooltip = nullptr; return; }
        }
        SendMessageW(tooltip, TTM_ACTIVATE, FALSE, 0);
        tooltipActive = false;
    }
    // The 28-point button frames in window client pixels.
    RECT toolRect(std::size_t b) const {
        if (!layout) return {};
        const auto& r = b == 0 ? layout->cancel : layout->confirm;
        const auto& w = layout->window;
        return {static_cast<LONG>(std::lround(r.x - w.x)), static_cast<LONG>(std::lround(r.y - w.y)),
                static_cast<LONG>(std::lround(r.x - w.x + r.width)), static_cast<LONG>(std::lround(r.y - w.y + r.height))};
    }
    void syncToolTips() {
        if (!tooltip) return;
        for (std::size_t b = 0; b < 2; ++b) {
            TTTOOLINFOW tool{};
            tool.cbSize = static_cast<UINT>(TTTOOLINFOW_V2_SIZE);
            tool.hwnd = window;
            tool.uId = b + 1;
            auto next = wide(m::chargePositionButtonTooltip(b == 1, appearance.language));
            if (next != tipText[b]) {
                tipText[b] = std::move(next);
                tool.lpszText = tipText[b].data();
                SendMessageW(tooltip, TTM_UPDATETIPTEXTW, 0, reinterpret_cast<LPARAM>(&tool));
            }
            const auto rect = toolRect(b);
            if (!EqualRect(&rect, &tipRects[b])) {
                tipRects[b] = rect;
                tool.rect = rect;
                SendMessageW(tooltip, TTM_NEWTOOLRECTW, 0, reinterpret_cast<LPARAM>(&tool));
            }
        }
        const bool active = editingStyle && state.editingPosition();
        if (active != tooltipActive) {
            if (!active) SendMessageW(tooltip, TTM_POP, 0, 0);
            SendMessageW(tooltip, TTM_ACTIVATE, active ? TRUE : FALSE, 0);
            tooltipActive = active;
        }
    }
    // --- UI Automation ----------------------------------------------------------
    ChargeAlertAutomation& automationFor(HWND hwnd) {
        if (!automation) {
            ChargeAlertAutomationHost host;
            host.window = hwnd;
            host.view = [this] { return automationView(); };
            host.invoke = [this](std::size_t button) { return postInvoke(button); };
            automation = std::make_unique<ChargeAlertAutomation>(std::move(host));
        }
        return *automation;
    }
    ChargeAlertAutomationView automationView() const {
        ChargeAlertAutomationView v;
        v.label = label;
        if (layout) {
            v.indicator = layout->indicator;
            v.buttons = {layout->cancel, layout->confirm};
        }
        v.editing = state.editingPosition();
        for (std::size_t b = 0; b < 2; ++b) {
            v.names[b] = m::chargePositionButtonLabel(b == 1, appearance.language);
            v.help[b] = m::chargePositionButtonTooltip(b == 1, appearance.language);
        }
        v.shown = shown && window && IsWindowVisible(window);
        return v;
    }
    // NSButton.performClick from an accessibility client: the press runs from
    // this window's procedure, after the provider call returned.
    bool postInvoke(std::size_t button) {
        if (!window || button > 1 || !state.editingPosition()) return false;
        return PostMessageW(window, invokeMessage, button, static_cast<LPARAM>(state.generation())) != FALSE;
    }
    void automationInvoke(std::size_t button, std::uint64_t generation) {
        // A press queued for an edit that already finished is dropped.
        if (button > 1 || !state.editingPosition() || state.generation() != generation) return;
        if (button == 0) discard();
        else confirm();
    }
    void render() {
        if (!window || !layout) return;
        const double unitScale = unit();
        const double scale = state.settings().scale * unitScale;
        const auto& w = layout->window;
        ChargeIndicatorPlacement placement;
        placement.canvasToWorld = Matrix::translation(layout->indicator.x - w.x, layout->indicator.y - w.y) * Matrix::scale(scale, scale);
        placement.pose = state.sample(time);
        placement.stage = state.stage();
        placement.opacity = 1;
        scene.place(placement);
        // Edit controls.
        const bool editing = state.editingPosition();
        const auto colorsNormal = m::chargePositionButtonColors(appearance.dark, false);
        for (std::size_t b = 0; b < 2; ++b) {
            const auto& r = b == 0 ? layout->cancel : layout->confirm;
            const bool highlighted = pressedButton == b && buttonHighlighted;
            const auto colors = m::chargePositionButtonColors(appearance.dark, highlighted);
            for (std::size_t part = 0; part < 2; ++part) {
                auto& draw = buttons[b * 2 + part];
                const auto& image = buttonImages[part == 0 ? 0 : b == 0 ? 1 : 2];
                const auto bounds = image ? image->bounds : core::Rect{0, 0, 28, 28};
                draw.world = Matrix::translation(r.x - w.x, r.y - w.y) * Matrix::scale(unitScale, unitScale) *
                             Matrix::translation(bounds.x, bounds.y) * Matrix::scale(bounds.width, bounds.height);
                draw.linearTint = tint(part == 0 ? colorsNormal.fill : colors.symbol);
                draw.opacity = editing ? 1.f : 0.f;
            }
        }
        // Resources (content events only), then numeric presentation.
        scene.upload(renderer);
        if (!meshUploaded) {
            const std::array<Vertex, 4> vertices{Vertex{{0, 0, 0}, {0, 0}}, Vertex{{1, 0, 0}, {1, 0}}, Vertex{{0, 1, 0}, {0, 1}}, Vertex{{1, 1, 0}, {1, 1}}};
            const std::array<std::uint32_t, 6> indices{0, 1, 2, 2, 1, 3};
            renderer.setMesh(meshID, 1, {vertices, indices});
            meshUploaded = true;
        }
        for (std::size_t n = 0; n < 3; ++n) {
            if (!buttonImages[n] || buttonUploaded[n] == buttonRevision) continue;
            TextureData data{buttonImages[n]->width, buttonImages[n]->height, buttonImages[n]->straightRGBA, TextureColorSpace::sRGB, TextureFilter::linear};
            renderer.setTexture(buttonTextures[n], buttonRevision, data);
            buttonUploaded[n] = buttonRevision;
        }
        renderer.setCamera(layerViewportProjection(static_cast<std::uint32_t>(std::max(1L, std::lround(w.width))),
                                                   static_cast<std::uint32_t>(std::max(1L, std::lround(w.height)))));
        const std::array<LayerCompositionEntry, 2> entries{LayerCompositionEntry{&anchor, scene.draws()},
                                                           LayerCompositionEntry{&buttonAnchor, buttons}};
        if (!published || !composition.supplementalBindingsMatch(entries)) { composition.setEntries(renderer, entries); published = true; }
        composition.present(renderer);
        renderer.draw(!options.hiddenForTests);
        dirty = false;
    }
    void show() {
        if (shown || !window) return;
        if (!options.hiddenForTests) ShowWindow(window, SW_SHOWNOACTIVATE);
        SetWindowPos(window, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        shown = true;
    }
    void hide() {
        if (!shown || !window) return;
        if (!options.hiddenForTests) ShowWindow(window, SW_HIDE);
        shown = false;
    }
    void frame(double now) {
        time = std::max(time, now);
        state.advance(time);
        if (!state.windowVisible()) { setEditingStyle(false); hide(); presenting = false; return; }
        // The first frame of a presentation: its window appears only once
        // this frame (content raster, possibly the window and device) ends.
        const bool starting = !presenting;
        presenting = true;
        const double begin = starting ? clockSeconds() : 0;
        // Content events only: a new reading/preview/settings revision, an
        // appearance or telemetry change, or a monitor (density) change.
        if (contentDirty || state.contentRevision() != contentRevision) {
            content();
            contentRevision = state.contentRevision();
            contentDirty = false;
        }
        positionWindow();
        for (unsigned attempt = 0;; ++attempt) {
            if (!window) {
                // A compositor that cannot host the alert (for example a
                // disconnected session) costs one attempt per presentation; the
                // alert state, deadlines and provider keep running.
                if (failedGeneration == state.generation()) return;
                try { ensureWindow(); }
                catch (const std::exception& error) { failure = error.what(); failedGeneration = state.generation(); destroy(); return; }
                failure.clear();
                positionWindow();
            }
            setEditingStyle(state.editingPosition());
            if (titled != label) { SetWindowTextW(window, wide(label).c_str()); titled = label; } // UIA name
            // One more frame after an animation settles presents the exact final pose.
            const bool animating = state.requiresFrames(time);
            if (dirty || animating || animatedLastFrame || !shown) {
                try { render(); }
                catch (const std::exception& error) {
                    // A device lost while the window waited (driver update,
                    // TDR, adapter switch): one new window and device, with
                    // the content regenerated from its key; a second failure
                    // ends this presentation's attempts like a creation failure.
                    destroy();
                    if (attempt) { failure = error.what(); failedGeneration = state.generation(); return; }
                    ++recoveries;
                    continue;
                }
            }
            animatedLastFrame = animating;
            show();
            if (starting) started(begin);
            return;
        }
    }
    // OverlayController.show adds the entrance to a Core Animation commit,
    // which starts it when the panel is on screen. The window shows at the
    // end of this presentation's first frame (the hidden first pose), so the
    // presentation moves by that frame's duration: the next frame samples
    // the entrance from its start instead of skipping what the window and
    // device creation took. Hidden tests use the fixed firstFrameLagForTests
    // so their synthetic clocks stay exact. The editor has no motion.
    void started(double begin) {
        const double elapsed = std::max(0.0, clockSeconds() - begin);
        firstFrameMilliseconds = 1000.0 * elapsed;
        const double lag = options.hiddenForTests ? options.firstFrameLagForTests : std::min(maximumFirstFrameLag, elapsed);
        if (state.editingPosition() || !(lag > 0) || !std::isfinite(lag)) return;
        state.postpone(lag);
        postponedMilliseconds = 1000.0 * lag;
        ++postponements;
    }
    bool park() noexcept {
        if (!window && !initialized) return false;
        if (state.windowVisible() || state.editingPosition() || procedureDepth) return false;
        destroy();
        ++parks;
        return true;
    }
    void screensChanged() {
        if (options.hiddenForTests) return;
        const auto scan = scanChargeScreens();
        state.setScreens(scan.screens, scan.preferred);
        dirty = contentDirty = true;
        if (state.windowVisible()) frame(time);
    }
    std::optional<std::size_t> buttonAt(core::Point screen) const {
        if (!state.editingPosition() || !layout) return std::nullopt;
        // NSButton hit area: the 28-point frame (the circle is drawn inset 1).
        if (inside(layout->cancel, screen)) return 0;
        if (inside(layout->confirm, screen)) return 1;
        return std::nullopt;
    }
    bool input(PointerKind kind, core::Point screen) {
        if (!state.editingPosition()) return false;
        switch (kind) {
        case PointerKind::down:
            if (const auto button = buttonAt(screen)) { pressedButton = button; buttonHighlighted = true; }
            else state.beginDrag(screen);
            break;
        case PointerKind::move:
            if (pressedButton) buttonHighlighted = buttonAt(screen) == pressedButton;
            else if (!state.drag(screen)) return false;
            break;
        case PointerKind::up: {
            const auto pressed = pressedButton;
            const bool activate = pressed && buttonAt(screen) == pressed;
            pressedButton.reset();
            buttonHighlighted = false;
            state.endDrag();
            if (activate) { *pressed == 0 ? discard() : confirm(); return true; }
            break;
        }
        case PointerKind::cancel:
            pressedButton.reset();
            buttonHighlighted = false;
            state.endDrag();
            break;
        }
        dirty = true;
        frame(time);
        return true;
    }
    bool keyDown(unsigned key) {
        if (!state.editingPosition()) return false;
        if (key == VK_ESCAPE) { discard(); return true; }
        if (key == VK_RETURN) { confirm(); return true; }
        return false;
    }
    void confirm() {
        const auto position = state.confirmPosition(time);
        if (!position) return;
        frame(time);
        if (finished) finished(position);
    }
    void discard() {
        if (!state.editingPosition()) return;
        state.discardPosition(time);
        frame(time);
        if (finished) finished(std::nullopt);
    }
};

ChargeIndicatorPanel::ChargeIndicatorPanel(LayerRasterizer& raster, ChargePanelOptions options, PositionFinished finished)
    : impl_(std::make_unique<Impl>(raster, std::move(options), std::move(finished))) {
    if (!impl_->options.hiddenForTests) refreshScreens();
}
ChargeIndicatorPanel::~ChargeIndicatorPanel() = default;
m::ChargeOverlayState& ChargeIndicatorPanel::state() noexcept { return impl_->state; }
const m::ChargeOverlayState& ChargeIndicatorPanel::state() const noexcept { return impl_->state; }
void ChargeIndicatorPanel::setAppearance(const m::ChargeIndicatorAppearance& appearance) {
    if (appearance.language == core::Language::system) throw std::invalid_argument("Resolve the alert language in the owner");
    if (impl_->appearance == appearance) return;
    impl_->appearance = appearance;
    impl_->dirty = impl_->contentDirty = true;
}
void ChargeIndicatorPanel::setTelemetry(const std::optional<m::ChargeTelemetry>& telemetry) {
    if (impl_->telemetry == telemetry) return;
    impl_->telemetry = telemetry;
    impl_->dirty = impl_->contentDirty = true;
}
void ChargeIndicatorPanel::refreshScreens() {
    if (impl_->options.hiddenForTests) return; // hidden tests inject their screens
    const auto scan = scanChargeScreens();
    impl_->state.setScreens(scan.screens, scan.preferred);
    impl_->dirty = impl_->contentDirty = true;
}
void ChargeIndicatorPanel::setScreens(std::span<const m::ChargeWorkArea> screens, std::optional<std::size_t> preferred) {
    impl_->state.setScreens(screens, preferred);
    impl_->dirty = impl_->contentDirty = true;
}
void ChargeIndicatorPanel::advance(double time) {
    if (!std::isfinite(time)) throw std::invalid_argument("Invalid charge alert time");
    impl_->frame(time);
}
bool ChargeIndicatorPanel::requiresFrames(double time) const { return impl_->state.requiresFrames(std::max(time, impl_->time)); }
std::optional<double> ChargeIndicatorPanel::nextWakeTime() const { return impl_->state.nextDeadline(); }
bool ChargeIndicatorPanel::beginPositionEditing(const m::BatteryReading& previewValue, double time) {
    auto& i = *impl_;
    i.time = std::max(i.time, time);
    if (!i.state.beginPositionEditing(previewValue, i.time)) return false;
    i.dirty = true;
    i.frame(i.time);
    return true;
}
bool ChargeIndicatorPanel::pointer(PointerKind kind, core::Point screen, double time) {
    impl_->time = std::max(impl_->time, time);
    return impl_->input(kind, screen);
}
bool ChargeIndicatorPanel::key(unsigned virtualKey, double time) {
    impl_->time = std::max(impl_->time, time);
    return impl_->keyDown(virtualKey);
}
void* ChargeIndicatorPanel::window() const noexcept { return impl_->window; }
bool ChargeIndicatorPanel::windowVisible() const noexcept { return impl_->window && IsWindowVisible(impl_->window); }
core::Rect ChargeIndicatorPanel::windowRect() const noexcept { return impl_->layout ? impl_->layout->window : core::Rect{}; }
std::string ChargeIndicatorPanel::accessibilityLabel() const { return impl_->label; }
long ChargeIndicatorPanel::automationRoot(IRawElementProviderFragmentRoot** out) {
    if (!out) return E_POINTER;
    *out = nullptr;
    if (!impl_->window) return E_FAIL;
    return impl_->automationFor(impl_->window).root(out);
}
std::size_t ChargeIndicatorPanel::automationChildren() const noexcept { return impl_->automation ? impl_->automation->size() : 0; }
unsigned ChargeIndicatorPanel::automationInvokeMessage() noexcept { return invokeMessage; }
std::vector<ChargeIndicatorPanel::ToolTip> ChargeIndicatorPanel::toolTips() const {
    std::vector<ToolTip> out;
    const auto tooltip = impl_->tooltip;
    if (!tooltip) return out;
    const auto count = static_cast<UINT>(SendMessageW(tooltip, TTM_GETTOOLCOUNT, 0, 0));
    for (UINT n = 0; n < count && n < 8; ++n) {
        std::array<wchar_t, 128> buffer{}; // TTM_ENUMTOOLS copies up to 80 characters
        TTTOOLINFOW tool{};
        tool.cbSize = static_cast<UINT>(TTTOOLINFOW_V2_SIZE);
        tool.lpszText = buffer.data();
        if (!SendMessageW(tooltip, TTM_ENUMTOOLSW, n, reinterpret_cast<LPARAM>(&tool))) continue;
        out.push_back({core::Rect{static_cast<double>(tool.rect.left), static_cast<double>(tool.rect.top),
                                  static_cast<double>(tool.rect.right - tool.rect.left), static_cast<double>(tool.rect.bottom - tool.rect.top)},
                       narrow(buffer.data())});
    }
    return out;
}
bool ChargeIndicatorPanel::toolTipsActive() const noexcept { return impl_->tooltip && impl_->tooltipActive; }
bool ChargeIndicatorPanel::resident() const noexcept { return impl_->window || impl_->initialized; }
bool ChargeIndicatorPanel::park() noexcept { return impl_->park(); }
ChargePanelResidency ChargeIndicatorPanel::residency() const noexcept {
    const auto& i = *impl_;
    ChargePanelResidency r;
    r.window = i.window != nullptr;
    r.device = i.initialized;
    if (i.initialized) {
        try {
            const auto stats = i.renderer.stats();
            r.embeddedShaders = stats.embeddedShaders;
            r.gpuBytes = stats.resourceBytes;
        } catch (...) {}
        if (i.layout) {
            const auto w = static_cast<std::size_t>(std::max(1L, std::lround(i.layout->window.width)));
            const auto h = static_cast<std::size_t>(std::max(1L, std::lround(i.layout->window.height)));
            // BGRA8 output (two composition buffers, one offscreen) + RGBA16F linear target.
            r.targetBytes = w * h * ((i.options.hiddenForTests ? 1 : 2) * 4 + 8);
        }
    }
    r.cpuBytes = i.scene.retainedBytes();
    for (const auto& image : i.buttonImages) if (image) r.cpuBytes += image->straightRGBA.capacity();
    r.creations = i.creations;
    r.parks = i.parks;
    r.recoveries = i.recoveries;
    r.postponements = i.postponements;
    r.lastCreationMilliseconds = i.creationMilliseconds;
    r.lastFirstFrameMilliseconds = i.firstFrameMilliseconds;
    r.lastPostponedMilliseconds = i.postponedMilliseconds;
    return r;
}
void ChargeIndicatorPanel::loseDeviceForTests() {
    if (!impl_->options.hiddenForTests || !impl_->initialized) throw std::logic_error("Losing the device is a hidden-test facility");
    impl_->renderer.reset();
}
std::string ChargeIndicatorPanel::lastFailure() const { return impl_->failure; }
Readback ChargeIndicatorPanel::readbackForTests() {
    if (!impl_->options.hiddenForTests || !impl_->initialized) throw std::logic_error("Readback is a hidden-test facility");
    return impl_->renderer.readback();
}
void ChargeIndicatorPanel::shutdown() noexcept { impl_->destroy(); }
} // namespace endfield::native
#endif
