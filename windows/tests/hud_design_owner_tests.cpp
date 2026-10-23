// Contract suite of tools/hud_design_owner and tools/hud_design_overlay_owner
// (LIBRARY-DESIGN.md sections 8, 11 and 14): a fake redesigned canvas behind
// DesignModuleOwner on a hidden window and WARP. Pose from the design
// screen's presentation surface, unprojected presses, hover, wheel and keys,
// covers, the cursor answers, UI Automation presses, leaving and park, the
// launch prewarm, the density rule, and the shared overlay owner's modal
// routing (the quit card's slot), deadlines and content dim. Synthetic
// layers; PostMessage-free; no input injection, no visible window.
#include "tools/hud_design_owner.hpp"
#include "tools/hud_design_overlay_owner.hpp"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#include <uiautomation.h>
#include <wrl/client.h>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace endfield;
using Microsoft::WRL::ComPtr;
namespace d = modules::design;
namespace gpu = native;
namespace {
unsigned checks{};
void check(bool v, const char* s) { ++checks; if (!v) throw std::runtime_error(s); }
struct Window {
    HWND hwnd{};
    Window() {
        WNDCLASSW c{}; c.lpfnWndProc = DefWindowProcW; c.hInstance = GetModuleHandleW(nullptr); c.lpszClassName = L"EndfieldDesignOwnerHidden";
        check(RegisterClassW(&c) != 0, "Register owned fixture");
        hwnd = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP, c.lpszClassName, L"Synthetic", WS_POPUP, 0, 0, 1000, 640, nullptr, nullptr, c.hInstance, nullptr);
        check(hwnd && !IsWindowVisible(hwnd), "Synthetic window hidden");
    }
    ~Window() { if (hwnd) DestroyWindow(hwnd); UnregisterClassW(L"EndfieldDesignOwnerHidden", GetModuleHandleW(nullptr)); }
};
// A plain pressable control (the router and highlight contract only).
struct Press final : d::Control {
    Press(d::Builder& b, std::string id) : Control(b.env, id, "Press " + id, b.layer(id)) {}
    std::optional<d::AccessibilityRole> accessibilityRole() const override { return d::AccessibilityRole::button; }
};
struct FakeCanvas final : d::Canvas {
    FakeCanvas(d::Context& c, core::Module m) : Canvas(m, c) {}
    unsigned pressed{}, builds{}, leaves{}, keys{}, scrolls{};
    void buildPage(d::Page& page) override {
        ++builds;
        auto& press = page.make<Press>("press");
        press.layer().setFrame({100, 200, 200, 50});
        press.layer().setContent(d::solid("plate", {0, 0, 200, 50}, d::rgb(0x2A2A2A)));
        press.setTarget({0, 0, 200, 50});
        press.onActivate = [this] { ++pressed; };
        page.layer().add(press.layer());
        page.add(press);
    }
    void willLeave() override { ++leaves; }
    bool handleKey(const d::KeyInput& k) override { if (k.virtualKey == 'K') { ++keys; return true; } return false; }
    bool handleScroll(d::Point, d::Point delta) override { scrolls += delta.y > 0; return true; }
    std::optional<d::Cursor> cursor(std::optional<d::Point> p) const override { return p && p->y > 400 ? std::optional(d::Cursor::openHand) : std::nullopt; }
};
core::ModulePresentationSample sample(core::Module m, bool attached = true) {
    core::ModulePresentationSample s;
    s.selected = s.requested = m;
    s.current.module = m; s.current.contentFrame = core::moduleLocalContentFrame(m);
    s.current.designScreen = true; s.current.hostClip = false; s.current.attached = attached; s.current.opacity = attached ? 1 : 0;
    s.acceptsModuleInput = true;
    return s;
}
core::Point centre(core::Rect r) { return {r.x + r.width / 2, r.y + r.height / 2}; }
void run(const std::filesystem::path& shader) {
    // --- the density rule (R11, Q8) ------------------------------------------------
    check(tools::designDensity(core::Matrix4::scale(2, 2), 1, 0) == 2, "Physical px per box px at the box centre");
    check(tools::designDensity(core::Matrix4::scale(2.3, 2.3), 1, 2) == 2, "Within .35 the density keeps its step (parallax never flips it)");
    check(tools::designDensity(core::Matrix4::scale(2.4, 2.4), 1, 2) == 2.5, "Quantized to .5");
    check(tools::designDensity(core::Matrix4::scale(9, 9), 1, 2) == 4 && tools::designDensity(core::Matrix4::scale(.2, .2), 1, 2) == 1, "Clamped 1...4");
    check(tools::designDensity(core::Matrix4::scale(1, 1), 1.5, 0) == 1.5, "The DPI scale multiplies");
    Window window;
    gpu::Renderer renderer;
    renderer.initialize(window.hwnd, 1000, 640, {gpu::Driver::warpForTests, shader, gpu::RenderTarget::offscreenForTests});
    gpu::LayerRasterizer raster;
    gpu::LayerComposition composition;
    unsigned presented{};
    tools::DesignOverlayOptions overlayOptions;
    overlayOptions.raster.pixelsPerPoint = 1; overlayOptions.window = window.hwnd;
    tools::DesignOverlayOwner overlay(raster, overlayOptions);
    auto& context = overlay.context();
    tools::DesignOwnerOptions options;
    options.name = "designTest";
    options.raster.pixelsPerPoint = 1;
    options.fields.window = window.hwnd; options.fields.ownerMessage = WM_APP + 0x46; options.fields.raster.pixelsPerPoint = 1;
    options.changed = [&] { ++presented; };
    auto core = std::make_unique<tools::DesignOwnerCore>(raster, context, options);
    auto canvas = std::make_unique<FakeCanvas>(context, core::Module::clipboard);
    auto* fake = canvas.get();
    core->add(std::move(canvas));
    tools::DesignModuleOwner owner(std::move(core));
    check(owner.presents(core::Module::clipboard) && !owner.presents(core::Module::map) && owner.name() == "designTest", "The owner presents its canvases");
    const app::ClientMetrics metrics{1000, 640, 96, 1, 1000, 640};
    owner.resize(metrics); overlay.resize(metrics);
    app::ModuleAppearance appearance; appearance.accentSRGB = {250. / 255, 212. / 255, 31. / 255};
    overlay.setAppearance(appearance, 0); owner.setAppearance(appearance, 0);
    overlay.setOverlayVisible(true, 0); owner.setOverlayVisible(true, 0);
    core::source::DesktopChromeSettings chrome; chrome.viewport = {0, 0, 1000, 640}; chrome.module = core::Module::clipboard;
    const auto center = core::Matrix4{};
    double time{};
    const auto frame = [&](const core::ModulePresentationSample& s, double t) {
        time = t;
        const app::ModuleFrame f{center, chrome, s, 1, t, true};
        overlay.update(f); owner.update(f);
        overlay.upload(renderer); owner.upload(renderer);
        std::vector<gpu::LayerCompositionEntry> entries;
        for (const auto& e : owner.entries(s.current.module)) entries.push_back(e);
        for (const auto& e : overlay.modalEntries()) entries.push_back(e);
        composition.setEntries(renderer, entries);
        composition.present(renderer);
        renderer.draw(false);
        owner.collected(renderer); overlay.collected(renderer);
        return entries.size();
    };
    // --- pose, input, cursor -------------------------------------------------------------
    check(frame(sample(core::Module::clipboard), 0) > 0 && fake->builds == 1, "The shown canvas builds and composes");
    auto& core0 = owner.core();
    check(core0.inputModule() == core::Module::clipboard, "The requested, attached canvas takes input");
    const auto rect = core0.clientRect({100, 200, 200, 50});
    check(rect && rect->width > 0 && rect->height > 0, "Box rects project to the client");
    const auto at = centre(*rect);
    const auto box = core0.boxPoint(at);
    check(box && std::abs(box->x - 200) < .5 && std::abs(box->y - 225) < .5, "Client points unproject through boxWorld into box px");
    check(owner.covers(at) && !owner.covers({-50, -50}), "The box covers its own presses only");
    owner.pointer({app::PointerKind::move, app::PointerButton::none, at.x, at.y}, .01);
    owner.pointer({app::PointerKind::down, app::PointerButton::left, at.x, at.y}, .02);
    check(owner.capturesPointer() && owner.pointerLocked(), "A tracked press captures the pointer and locks the parallax");
    owner.pointer({app::PointerKind::up, app::PointerButton::left, at.x, at.y}, .03);
    check(fake->pressed == 1 && !owner.capturesPointer(), "A click activates the control once");
    check(owner.wheel({at.x, at.y, -1, false, 0, 3}, .04) && fake->scrolls == 1, "The wheel scrolls down by lines x 12 px");
    check(core0.key({app::KeyKind::down, 'K'}, 0, .05) && fake->keys == 1, "Keys reach the canvas precedence (explicit modifiers)");
    const auto low = core0.clientRect({100, 450, 10, 10});
    check(low && owner.cursorAt(centre(*low)) == app::ModuleCursor::openHand && !owner.cursorAt(at), "The canvas's hand cursor is answered");
    // --- UI Automation --------------------------------------------------------------------
    check(owner.automationRefresh(nullptr) && owner.automationCount() == 1, "One element per routed control");
    ComPtr<IRawElementProviderFragment> element;
    check(SUCCEEDED(owner.automationChild(0, &element)) && element, "The element is listed");
    ComPtr<IRawElementProviderSimple> simple;
    element.As(&simple);
    ComPtr<IUnknown> unknown;
    check(SUCCEEDED(simple->GetPatternProvider(UIA_InvokePatternId, &unknown)) && unknown, "A button invokes");
    ComPtr<IInvokeProvider> invoke;
    unknown.As(&invoke);
    check(SUCCEEDED(invoke->Invoke()) && fake->pressed == 2 && presented == 1, "Invoke presses exactly like a click and presents a frame");
    // --- leaving, park and prewarm ----------------------------------------------------------
    auto away = sample(core::Module::clipboard);
    away.requested = core::Module::map;
    frame(away, .1);
    check(fake->leaves == 1 && !core0.inputModule(), "The canvas leaves when another module is requested");
    auto other = sample(core::Module::map);
    check(frame(other, .2) == 0, "A module that is not shown composes nothing");
    const auto resident = raster.stats().entries;
    owner.setOverlayVisible(false, .3); overlay.setOverlayVisible(false, .3);
    frame(other, .3);
    check(raster.stats().entries < resident, "Closed on another module, the canvas keeps no artwork (park)");
    owner.setOverlayVisible(true, .4); overlay.setOverlayVisible(true, .4);
    const auto before = raster.stats().entries;
    owner.prewarm({center, chrome, sample(core::Module::clipboard, false), 0, .41});
    check(raster.stats().entries > before && owner.entries(core::Module::clipboard).empty(), "Prewarm rasterizes the first page without presenting it");
    check(frame(sample(core::Module::clipboard), .5) > 0, "The prewarmed canvas shows");
    // --- the overlay owner: the quit card's slot ------------------------------------------------
    const auto routing = overlay.routing();
    check(routing.pointerCapture == 0 && routing.wheelCapture == 0 && routing.keyCapture == 0, "The overlay routes first like the quit card");
    check(!overlay.capturesPointer() && !overlay.presented(), "At rest the overlay holds nothing");
    unsigned confirmed{}, cancelled{};
    d::ConfirmRequest request; request.id = "test"; request.title = "Delete?"; request.confirmTitle = "Delete"; request.danger = true;
    context.confirm(request, [&] { ++confirmed; }, [&] { ++cancelled; });
    check(overlay.capturesPointer() && overlay.capturesKeys() && overlay.suppressesKeyFilters() && overlay.requiresFrames(.6), "A confirm bar captures every input");
    check(frame(sample(core::Module::clipboard), .6) > 1 && overlay.presented(), "The bar composes above the module");
    frame(sample(core::Module::clipboard), .9);
    check(overlay.contentDim(.9) > .99, "The module content dims under the bar");
    const auto filter = overlay.contentFilter(.9, 0, 4);
    check(filter && filter->count == 4 && filter->blurSigma > 0 && filter->exposure == -2.2 && filter->amount > .99, "The host's content filter: 1.5 design units of blur, -2.2 EV");
    check(overlay.key({app::KeyKind::down, 'Q'}, .91) && !confirmed && !cancelled, "Other keys are swallowed");
    check(overlay.automationRefresh(nullptr) && overlay.automationCount() >= 2, "The pills are listed while the bar captures");
    check(overlay.key({app::KeyKind::down, VK_ESCAPE}, .92) && cancelled == 1, "Esc cancels at once");
    for (double t = .95; t < 2; t += .05) { overlay.deadline(t); frame(sample(core::Module::clipboard), t); }
    check(!overlay.capturesPointer() && !overlay.presented() && overlay.contentDim(2) == 0 && !overlay.contentFilter(2, 0, 4), "The bar collapses and the dim clears");
    check(!overlay.requiresFrames(2.1), "A settled overlay asks for no frames");
    composition.detach(renderer);
    owner.release(renderer); overlay.release(renderer);
    check(!IsWindowVisible(window.hwnd) && renderer.stats().presents == 0, "The fixture never shows a window");
}
}
int wmain(int argc, wchar_t** argv) {
    const auto com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    try {
        check(SUCCEEDED(com) && argc == 2, "Pass the shader");
        run(argv[1]);
        CoUninitialize();
        std::cout << "HUD design owners: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        if (SUCCEEDED(com)) CoUninitialize();
        std::cerr << "HUD design owners after " << checks << ": " << e.what() << '\n';
        return 1;
    }
}
#else
int main() { return 0; }
#endif
