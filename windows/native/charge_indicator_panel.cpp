#include "native/charge_indicator_panel.hpp"
#ifdef _WIN32
#include "native/charge_alert_automation.hpp"
#include "native/font_resources.hpp"
#include "native/hud_design_scene.hpp"
#include "native/hud_design_text_metrics.hpp"
#include "native/layer_scene.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#include <shellscalingapi.h>
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
namespace d = modules::design;
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
// Opts a renderer into a device without driver worker threads once
// RendererOptions offers it (detected at compile time; no effect before).
template <class Options> void preferThreadFreeDevice(Options& options) {
    if constexpr (requires { options.preventDriverThreads = true; }) options.preventDriverThreads = true;
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
    // OverlayController.editorSizeID / editorLengthID.
    static constexpr const char* sizeID = "chargeBar.size";
    static constexpr const char* lengthID = "chargeBar.length";
    // A UI Automation action waiting for the window procedure (a pill press or
    // a slider value), for the edit of `generation` only.
    struct PendingAction { std::string id; std::optional<double> value; std::uint64_t generation{}; };
    LayerRasterizer* raster;
    ChargePanelOptions options;
    PositionFinished finished;
    m::ChargeOverlayState state;
    m::ChargeIndicatorAppearance appearance;
    std::optional<m::ChargeTelemetry> telemetry;
    ChargeIndicatorScene scene;
    LayerScene anchor;
    LayerComposition composition;
    Renderer renderer;
    HWND window{};
    bool layered{}, shown{}, initialized{}, published{}, editingStyle{}, dirty{true}, animatedLastFrame{};
    std::uint64_t contentRevision{};
    bool contentDirty{true};
    std::optional<m::ChargePanelLayout> layout;
    double time{};
    std::string label, titled, failure;
    std::optional<std::uint64_t> failedGeneration;
    // Created on the first WM_GETOBJECT for the window's UIA root.
    std::unique_ptr<ChargeAlertAutomation> automation;
    // The bar editor's strip (PositionCanvas.editor): its own design
    // environment (the alert's theme, accent and language), the strip and its
    // scene at 0.7. An editor that finished waits in `retired` until the
    // scene holds none of its layers (parked, or synced to the next editor).
    NativeDesignTextMeasurer measurer;
    d::Environment env;
    std::unique_ptr<d::PlacementEditor> editor;
    std::vector<std::unique_ptr<d::PlacementEditor>> retired;
    NativeDesignScene strip;
    const d::Layer* stripTree{}; // the tree the strip scene last synced
    bool pointerPressed{}, tracking{};
    std::vector<LayerCompositionEntry> entries;
    std::vector<PendingAction> pending;
    // Residency counters (creations, parks, device recoveries).
    std::uint64_t creations{}, parks{}, recoveries{}, postponements{};
    double creationMilliseconds{}, firstFrameMilliseconds{}, postponedMilliseconds{};
    // The state was visible on the previous frame: the next frame continues
    // a presentation instead of starting one.
    bool presenting{};
    // Nesting of this panel's window procedure: the window is never parked
    // from inside its own message (a press or key that ends an edit).
    unsigned procedureDepth{};

    static LayerRasterOptions stripOptions() {
        LayerRasterOptions o;
        o.magnification = m::chargeEditorStripScale; // the strip's text and layers raster for 0.7
        return o;
    }
    Impl(LayerRasterizer& r, ChargePanelOptions o, PositionFinished f)
        : raster(&r), options(std::move(o)), finished(std::move(f)), scene(r, "charge.alert"), anchor(r), measurer(r),
          strip(r, stripOptions(), "charge.alert.strip") {
        const Json empty = Json::Object{{"bounds", Json::Array{0, 0, 1, 1}}, {"children", Json::Array{}}};
        anchor.load(empty, {});
        env.text.measurer = &measurer;
        env.text.uppercase = nativeUppercase();
        env.text.magnification = m::chargeEditorStripScale;
        env.characters = nativeCharacterRules();
        entries.reserve(64);
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
                else if (message == WM_MOUSEMOVE) { self->trackLeave(hwnd); self->input(PointerKind::move, screen); }
                else { self->input(PointerKind::up, screen); if (GetCapture() == hwnd) ReleaseCapture(); }
                return 0;
            }
            case WM_MOUSELEAVE: self->tracking = false; self->hover(std::nullopt); return 0;
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
                self->automationActions(static_cast<std::uint64_t>(lparam));
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
    // UIA clients go with the window; messages arriving while it is
    // destroyed no longer reach this panel.
    void closeWindow() noexcept {
        if (automation) { automation->disconnect(); automation.reset(); }
        editingStyle = false; // a new window starts click-through
        tracking = false;
        if (!window) return;
        const HWND closing = std::exchange(window, nullptr);
        SetWindowLongPtrW(closing, GWLP_USERDATA, 0);
        DestroyWindow(closing);
    }
    void destroy() noexcept {
        if (automation) { automation->disconnect(); automation.reset(); }
        if (initialized) {
            // Each step on its own: a lost or reset renderer may refuse one,
            // and the scenes still mark everything for reinstallation.
            try { composition.detach(renderer); } catch (...) {}
            (void)scene.release(renderer);
            (void)strip.park(renderer);
        }
        if (!strip.holdsArtwork()) stripTree = nullptr;
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
        const double density = state.barSettings().scale * unit();
        ChargeIndicatorSceneContent key;
        key.appearance = appearance;
        key.battery = state.reading();
        key.metric = state.settings().metric;
        key.telemetry = m::chargeMetricRequiresTelemetry(key.metric) ? telemetry : std::nullopt;
        key.preview = state.previewContent();
        key.pixelsPerPoint = std::clamp(std::ceil(density * 4) / 4, 1.0, 4.0);
        if (const auto next = state.layout()) key.barWidth = next->barWidth;
        if (scene.setContent(key)) dirty = true;
        const auto& accessibility = scene.content().accessibility;
        if (accessibility != label) label = accessibility;
        // Content events only: the label, the strip's language and values.
        if (automation) automation->changed();
    }
    void positionWindow() {
        const auto next = state.place(time);
        if (!next) return;
        const bool sizeChanged = !layout || layout->window.width != next->window.width || layout->window.height != next->window.height;
        const bool moved = !layout || layout->window.x != next->window.x || layout->window.y != next->window.y;
        if (!layout || *layout != *next) dirty = true;
        layout = next;
        if (!window || !(sizeChanged || moved)) return;
        const auto& r = layout->window;
        const int w = std::max(1, static_cast<int>(std::lround(r.width))), h = std::max(1, static_cast<int>(std::lround(r.height)));
        SetWindowPos(window, HWND_TOPMOST, static_cast<int>(std::lround(r.x)), static_cast<int>(std::lround(r.y)), w, h,
                     SWP_NOACTIVATE | SWP_NOOWNERZORDER);
        if (sizeChanged) renderer.resize(static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(h));
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
        if (automation) automation->changed();
    }
    // --- The bar editor's strip ---------------------------------------------------
    // The strip's environment: the alert's theme, accent and language, the
    // monitor's density with the strip's 0.7 as magnification.
    void syncEnvironment() {
        env.now = std::max(env.now, time);
        env.language = appearance.language;
        env.text.uiRegion = core::regionFor(appearance.language, preferredSystemRegion());
        env.text.boxScale = unit();
        env.reduceMotion = state.reduceMotion();
        (void)env.palette.apply(state.settings().accentHex, !appearance.dark);
    }
    // installPositionEditor: Size (the bar's scale) and Length (a share of the
    // display), then Cancel and Confirm, on a new strip. `entrance` plays the
    // confirm bar's entrance (a new edit; a theme or language rebuild does not).
    void installEditor(bool entrance) {
        syncEnvironment();
        using M = m::ChargeIndicatorMetrics;
        const auto bar = state.barSettings();
        std::vector<d::PlacementEditor::SliderSpec> specs{
            {sizeID, core::localized("Size", "大小", env.language), bar.scale * 100, {M::minimumScale * 100, M::maximumScale * 100, 1, {}}, d::ValueFormat::percent},
            {lengthID, core::localized("Length", "长度", env.language), 100, {0, 100, 1, {}}, d::ValueFormat::percent}};
        if (editor) { editor->cancelPointer(); retired.push_back(std::move(editor)); }
        editor = std::make_unique<d::PlacementEditor>(env, "chargeBar", std::move(specs));
        editor->onChange = [this](const std::string& id, double value, bool) { editorChanged(id, value); };
        editor->onDecision = [this](d::ConfirmState::Decision decision) {
            if (decision == d::ConfirmState::Decision::confirm) confirm(); else discard();
        };
        state.setEditorStrip(editor->size());
        applyLengthSlider();
        if (entrance) editor->show(env.clock());
        pointerPressed = false;
        dirty = true;
        if (automation) automation->changed();
    }
    // syncLengthSlider: the range follows the display and Size.
    void applyLengthSlider() {
        if (!editor) return;
        if (const auto slider = state.lengthSlider()) editor->setRange(slider->range(), slider->value, lengthID);
    }
    // editorChanged: Size and Length are drafts; the bar and the strip follow.
    void editorChanged(const std::string& id, double value) {
        if (!state.editingPosition()) return;
        if (id == sizeID) state.setDraftScale(value / 100);
        else if (id != lengthID || !state.setDraftLength(value)) return;
        applyLengthSlider();
        dirty = true;
        if (automation) automation->changed();
    }
    void finishEditor() {
        if (editor) { editor->cancelPointer(); retired.push_back(std::move(editor)); }
        pointerPressed = false;
        pending.clear();
    }
    // An editor leaves once the strip scene holds none of its layers.
    void collectEditors() noexcept {
        if (retired.empty()) return;
        if (stripTree && (!editor || stripTree != &editor->layer())) return; // the scene still holds a retired tree
        retired.clear();
    }
    // The strip's origin in the window (pixels): the host keeps the bubble's
    // room above it, everything scaled by 0.7 at the monitor's density.
    std::optional<core::Point> stripOrigin() const {
        if (!layout || !layout->strip) return std::nullopt;
        const double k = m::chargeEditorStripScale * unit();
        return core::Point{layout->strip->x - layout->window.x, layout->strip->y - layout->window.y + d::PlacementEditor::bubbleClearance * k};
    }
    std::optional<core::Point> toStrip(core::Point screen) const {
        const auto origin = stripOrigin();
        if (!origin) return std::nullopt;
        const double k = m::chargeEditorStripScale * unit();
        return core::Point{(screen.x - layout->window.x - origin->x) / k, (screen.y - layout->window.y - origin->y) / k};
    }
    std::optional<core::Rect> stripToScreen(core::Rect r) const {
        const auto origin = stripOrigin();
        if (!origin) return std::nullopt;
        const double k = m::chargeEditorStripScale * unit();
        return core::Rect{layout->window.x + origin->x + r.x * k, layout->window.y + origin->y + r.y * k, r.width * k, r.height * k};
    }
    bool stripAnimating(double now) const { return editor && stripTree == &editor->layer() && strip.requiresFrames(now); }
    // --- UI Automation ----------------------------------------------------------
    ChargeAlertAutomation& automationFor(HWND hwnd) {
        if (!automation) {
            ChargeAlertAutomationHost host;
            host.window = hwnd;
            host.nodes = [this] { return automationNodes(); };
            host.toScreen = [this](core::Rect r, std::uint8_t space) -> std::optional<core::Rect> {
                return space == 1 ? std::optional<core::Rect>(r) : stripToScreen(r);
            };
            host.invoke = [this](std::string_view id) { return postAction(id, std::nullopt); };
            host.setValue = [this](std::string_view id, double value) { return postAction(id, value); };
            automation = std::make_unique<ChargeAlertAutomation>(std::move(host));
        }
        return *automation;
    }
    // The indicator (space 1: pixels), then the strip's items (space 0: strip
    // points); offscreen while the window is ordered out.
    std::vector<AutomationNode> automationNodes() const {
        const bool visible = shown && window && IsWindowVisible(window);
        std::vector<AutomationNode> out;
        AutomationNode indicator;
        indicator.id = "chargeAlert:indicator";
        indicator.name = label;
        indicator.role = AutomationNode::Role::text;
        indicator.space = 1;
        if (layout) indicator.rect = layout->indicator;
        indicator.offscreen = !visible;
        out.push_back(std::move(indicator));
        if (editor && state.editingPosition()) {
            std::vector<d::AccessibilityItem> items;
            editor->accessibilityItems(editor->layer(), items);
            for (auto& node : native::automationNodes(items)) { node.offscreen = node.offscreen || !visible; out.push_back(std::move(node)); }
        }
        return out;
    }
    // HUDAccessibilityStandIns.onPerform / onAdjust from an accessibility
    // client: run from this window's procedure, after the provider call.
    bool postAction(std::string_view id, std::optional<double> value) {
        if (!window || !editor || !state.editingPosition()) return false;
        bool known{};
        for (const auto* control : editor->controls()) known = known || control->id() == id;
        if (!known) return false;
        pending.push_back({std::string(id), value, state.generation()});
        return PostMessageW(window, invokeMessage, 0, static_cast<LPARAM>(state.generation())) != FALSE;
    }
    void automationActions(std::uint64_t generation) {
        auto actions = std::exchange(pending, {});
        for (auto& action : actions) {
            // An action queued for an edit that already finished is dropped.
            if (!editor || !state.editingPosition() || action.generation != generation || state.generation() != generation) continue;
            auto& target = *editor;
            if (action.value) target.adjust(action.id, *action.value);
            else target.perform(action.id);
        }
        if (state.editingPosition()) { dirty = true; frame(time); }
        if (automation) automation->changed();
    }
    void render() {
        if (!window || !layout) return;
        const double unitScale = unit();
        const double scale = state.barSettings().scale * unitScale;
        const auto& w = layout->window;
        ChargeIndicatorPlacement placement;
        placement.canvasToWorld = Matrix::translation(layout->indicator.x - w.x, layout->indicator.y - w.y) * Matrix::scale(scale, scale);
        placement.pose = state.sample(time);
        placement.stage = state.stage();
        placement.opacity = 1;
        scene.place(placement);
        entries.clear();
        entries.push_back({&anchor, scene.draws()});
        // The strip above the bar's canvas (editorHost.zPosition 10), scaled
        // by 0.7 at the monitor's density.
        const auto origin = stripOrigin();
        bool stripEntries{}, stripContent{};
        if (editor && origin) {
            syncEnvironment();
            const double k = m::chargeEditorStripScale * unitScale;
            // A content event of the strip (a new generation, an updated
            // surface) changes its scenes: the composition takes them anew.
            stripContent = strip.syncContent(editor->layer(), env.text, editor->contentRevision(),
                                             d::layerContentsScale(unitScale, m::chargeEditorStripScale), time);
            stripTree = &editor->layer();
            collectEditors();
            strip.updatePose({Matrix::translation(origin->x, origin->y) * Matrix::scale(k, k), 1.f, time, {}, {}});
            for (const auto& entry : strip.entries()) entries.push_back(entry);
            stripEntries = true;
        }
        // Resources (content events only), then numeric presentation.
        scene.upload(renderer);
        const bool stripChanged = stripEntries && strip.uploadAnimations(renderer);
        if (stripChanged) { entries.resize(1); for (const auto& entry : strip.entries()) entries.push_back(entry); }
        renderer.setCamera(layerViewportProjection(static_cast<std::uint32_t>(std::max(1L, std::lround(w.width))),
                                                   static_cast<std::uint32_t>(std::max(1L, std::lround(w.height)))));
        if (!published || stripContent || stripChanged || !composition.supplementalBindingsMatch(entries)) {
            composition.setEntries(renderer, entries);
            published = true;
        }
        composition.present(renderer);
        renderer.draw(!options.hiddenForTests);
        (void)strip.collectRetired(renderer);
        // The edit finished: the strip's artwork goes with the published list
        // that held it.
        if (!stripEntries && stripTree && strip.park(renderer)) { stripTree = nullptr; collectEditors(); }
        dirty = false;
    }
    void show() {
        if (shown || !window) return;
        if (!options.hiddenForTests) ShowWindow(window, SW_SHOWNOACTIVATE);
        SetWindowPos(window, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        shown = true;
        if (automation) automation->changed();
    }
    void hide() {
        if (!shown || !window) return;
        if (!options.hiddenForTests) ShowWindow(window, SW_HIDE);
        shown = false;
        if (automation) automation->changed();
    }
    void frame(double now) {
        time = std::max(time, now);
        env.now = std::max(env.now, time);
        (void)env.deadlines.advance(time);
        state.advance(time);
        // An edit the host cancelled on the state (a summon, Projection,
        // sleep): the strip goes with it.
        if (editor && !state.editingPosition()) { finishEditor(); if (automation) automation->changed(); }
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
            const bool animating = state.requiresFrames(time) || stripAnimating(time);
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
        collectEditors();
        ++parks;
        return true;
    }
    void screensChanged() {
        if (options.hiddenForTests) return;
        const auto scan = scanChargeScreens();
        state.setScreens(scan.screens, scan.preferred);
        applyLengthSlider();
        dirty = contentDirty = true;
        if (state.windowVisible()) frame(time);
    }
    // TrackMouseEvent: the strip's hover ends when the pointer leaves the window.
    void trackLeave(HWND hwnd) {
        if (tracking || !state.editingPosition()) return;
        TRACKMOUSEEVENT track{sizeof track, TME_LEAVE, hwnd, 0};
        tracking = TrackMouseEvent(&track) != FALSE;
    }
    void hover(std::optional<core::Point> screen) {
        if (!editor || !state.editingPosition() || pointerPressed) return;
        const auto local = screen ? toStrip(*screen) : std::nullopt;
        editor->updateHighlights(editor->layer(), local);
        dirty = true;
        frame(time);
    }
    // PositionCanvas.mouseDown / mouseDragged / mouseUp and the controller's
    // editorPointer* handlers: the strip's controls take presses on the strip,
    // any other press drags the bar (design::PlacementDrag), clamped by the
    // state on the display under the pointer.
    bool input(PointerKind kind, core::Point screen) {
        if (!state.editingPosition() || !editor) return false;
        auto& e = *editor; // a decision retires the editor; it stays alive meanwhile
        const auto& root = e.layer();
        const auto local = toStrip(screen).value_or(core::Point{-1e9, -1e9});
        switch (kind) {
        case PointerKind::down: {
            const auto from = state.anchor();
            if (!from) return false;
            pointerPressed = true;
            e.pointerDown(local, root, screen, *from);
            // A strip slider holds the pointer: the strip stays where it is.
            state.setStripAdjusting(e.adjusting());
            break;
        }
        case PointerKind::move:
            if (!pointerPressed) { e.updateHighlights(root, local); break; }
            if (const auto proposed = e.pointerDragged(local, root, screen))
                if (state.moveDraft(*proposed, screen)) applyLengthSlider();
            break;
        case PointerKind::up:
            pointerPressed = false;
            e.pointerUp(local, root);
            // The strip, held still under a slider, settles beside the bar again.
            state.setStripAdjusting(false);
            break;
        case PointerKind::cancel:
            pointerPressed = false;
            e.cancelPointer();
            e.updateHighlights(root, std::nullopt);
            state.setStripAdjusting(false);
            break;
        }
        dirty = true;
        frame(time);
        return true;
    }
    // Esc cancels and Return / keypad Enter confirm through the strip's confirm state.
    bool keyDown(unsigned key) {
        if (!state.editingPosition() || !editor) return false;
        d::KeyInput input;
        input.virtualKey = key;
        return editor->key(input);
    }
    void confirm() {
        const auto layoutToSave = state.confirmPosition(time);
        if (!layoutToSave) return;
        finishEditor();
        frame(time);
        if (automation) automation->changed();
        if (finished) finished(layoutToSave);
    }
    void discard() {
        if (!state.editingPosition()) return;
        state.discardPosition(time);
        finishEditor();
        frame(time);
        if (automation) automation->changed();
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
    auto& i = *impl_;
    if (i.appearance == appearance) return;
    i.appearance = appearance;
    i.dirty = i.contentDirty = true;
    // System Theme or a new language: the strip's pills bake their colours and
    // texts, so the strip is rebuilt (the drafts live in the state).
    if (i.editor && i.state.editingPosition()) i.installEditor(false);
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
    impl_->collectEditors();
    impl_->frame(time);
}
bool ChargeIndicatorPanel::requiresFrames(double time) const {
    const auto now = std::max(time, impl_->time);
    return impl_->state.requiresFrames(now) || (impl_->state.windowVisible() && impl_->stripAnimating(now));
}
std::optional<double> ChargeIndicatorPanel::nextWakeTime() const {
    auto next = impl_->state.nextDeadline();
    // The strip's motion deadlines, only while its editor is open (a retired
    // strip's work runs with the next frame; nothing wakes for it).
    if (impl_->editor && impl_->state.editingPosition())
        if (const auto strip = impl_->env.deadlines.next(); strip && (!next || *strip < *next)) next = strip;
    return next;
}
bool ChargeIndicatorPanel::beginPositionEditing(const m::BatteryReading& previewValue, double time) {
    auto& i = *impl_;
    i.time = std::max(i.time, time);
    i.collectEditors();
    if (i.state.editingPosition()) return i.state.beginPositionEditing(previewValue, i.time); // the host refocuses it
    if (!i.state.beginPositionEditing(previewValue, i.time)) return false;
    i.installEditor(true);
    i.frame(i.time);
    return true;
}
bool ChargeIndicatorPanel::pointer(PointerKind kind, core::Point screen, double time) {
    impl_->time = std::max(impl_->time, time);
    impl_->collectEditors();
    return impl_->input(kind, screen);
}
bool ChargeIndicatorPanel::key(unsigned virtualKey, double time) {
    impl_->time = std::max(impl_->time, time);
    impl_->collectEditors();
    return impl_->keyDown(virtualKey);
}
void* ChargeIndicatorPanel::window() const noexcept { return impl_->window; }
bool ChargeIndicatorPanel::windowVisible() const noexcept { return impl_->window && IsWindowVisible(impl_->window); }
core::Rect ChargeIndicatorPanel::windowRect() const noexcept { return impl_->layout ? impl_->layout->window : core::Rect{}; }
std::string ChargeIndicatorPanel::accessibilityLabel() const { return impl_->label; }
const d::PlacementEditor* ChargeIndicatorPanel::editor() const noexcept { return impl_->editor.get(); }
std::optional<core::Rect> ChargeIndicatorPanel::stripRect() const noexcept {
    const auto& i = *impl_;
    return i.editor && i.layout ? i.layout->strip : std::nullopt;
}
std::optional<core::Point> ChargeIndicatorPanel::stripToScreen(core::Point p) const noexcept {
    const auto r = impl_->stripToScreen(core::Rect{p.x, p.y, 0, 0});
    return r ? std::optional<core::Point>(core::Point{r->x, r->y}) : std::nullopt;
}
long ChargeIndicatorPanel::automationRoot(IRawElementProviderFragmentRoot** out) {
    if (!out) return E_POINTER;
    *out = nullptr;
    if (!impl_->window) return E_FAIL;
    return impl_->automationFor(impl_->window).root(out);
}
std::size_t ChargeIndicatorPanel::automationChildren() const noexcept { return impl_->automation ? impl_->automation->size() : 0; }
std::vector<AutomationNode> ChargeIndicatorPanel::automationNodes() const {
    auto nodes = impl_->automationNodes();
    for (auto& node : nodes)
        if (node.space == 0) {
            const auto r = impl_->stripToScreen(node.rect);
            node.rect = r.value_or(core::Rect{});
            node.space = 1;
        }
    return nodes;
}
unsigned ChargeIndicatorPanel::automationInvokeMessage() noexcept { return invokeMessage; }
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
