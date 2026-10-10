#include "native/charge_indicator_panel.hpp"
#ifdef _WIN32
#include "native/layer_scene.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#include <shellscalingapi.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

namespace endfield::native {
namespace {
namespace m = modules;
using Json = ehud::data::Json;
using Matrix = core::Matrix4;
constexpr wchar_t windowClassName[] = L"EndfieldHUDChargeAlertPanel";

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
        RendererOptions rendererOptions;
        rendererOptions.shaderPath = options.shader;
        if (options.hiddenForTests) { rendererOptions.driver = Driver::warpForTests; rendererOptions.target = RenderTarget::offscreenForTests; }
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
                DestroyWindow(window);
                window = nullptr;
                if (!clickThrough) throw;
            }
        }
        if (!window) throw std::runtime_error("Create charge alert window");
        titled.clear();
        published = false;
        dirty = true;
    }
    void destroy() noexcept {
        try {
            if (initialized) {
                composition.detach(renderer);
                scene.release(renderer);
                for (std::size_t n = 0; n < buttonTextures.size(); ++n) if (buttonUploaded[n]) renderer.removeTexture(buttonTextures[n]);
                if (meshUploaded) renderer.removeMesh(meshID);
            }
        } catch (...) {}
        buttonUploaded = {};
        meshUploaded = false;
        renderer.reset();
        initialized = false;
        published = false;
        if (window) { DestroyWindow(window); window = nullptr; }
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
        // Edit controls: 28-point buttons at the monitor density.
        const double buttonDensity = std::clamp(std::ceil(unit() * 4) / 4, 1.0, 4.0);
        if (buttonDensity != this->buttonDensity) {
            LayerRasterOptions raster;
            raster.pixelsPerPoint = buttonDensity;
            raster.paddingPoints = 1;
            ++buttonRevision;
            const std::array<Json, 3> layers{m::chargePositionButtonCircleLayer(), m::chargePositionButtonSymbolLayer(false),
                                             m::chargePositionButtonSymbolLayer(true)};
            for (std::size_t n = 0; n < 3; ++n) {
                buttonImages[n] = this->raster->rasterize(buttonTextures[n], buttonRevision, layers[n], raster);
                if (!buttonImages[n] || !buttonImages[n]->complete()) throw std::runtime_error("Unsupported position button artwork");
            }
            this->buttonDensity = buttonDensity;
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
        if (!state.windowVisible()) { setEditingStyle(false); hide(); return; }
        // Content events only: a new reading/preview/settings revision, an
        // appearance or telemetry change, or a monitor (density) change.
        if (contentDirty || state.contentRevision() != contentRevision) {
            content();
            contentRevision = state.contentRevision();
            contentDirty = false;
        }
        positionWindow();
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
        if (dirty || animating || animatedLastFrame || !shown) render();
        animatedLastFrame = animating;
        show();
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
std::string ChargeIndicatorPanel::lastFailure() const { return impl_->failure; }
Readback ChargeIndicatorPanel::readbackForTests() {
    if (!impl_->options.hiddenForTests || !impl_->initialized) throw std::logic_error("Readback is a hidden-test facility");
    return impl_->renderer.readback();
}
void ChargeIndicatorPanel::shutdown() noexcept { impl_->destroy(); }
} // namespace endfield::native
#endif
