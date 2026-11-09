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
#include <functional>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <vector>
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
    bool striped{};   // the press gains a surface of its own: the page part loads again, its old scene retires
    std::size_t tiles{}; // 1x1 page children: one part, one raster entry each (a page's weight in the shared cache)
    bool bare{};         // no list: every surface's pixels go to the GPU at upload (the scroll bar's small ones stay held)
    d::ScrollArea* rows{};
    void buildPage(d::Page& page) override {
        ++builds;
        // A long list whose bar is drawn (Event Log's log grows every cycle).
        if (!bare) {
            rows = &page.scrollArea("rows", {0, 300, 400, 100});
            rows->setContentHeight(400);
            rows->track().setValue(d::Channel::opacity, 1);
        }
        auto& press = page.make<Press>("press");
        press.layer().setFrame({100, 200, 200, 50});
        press.layer().setContent(d::solid("plate", {0, 0, 200, 50}, d::rgb(0x2A2A2A)));
        press.setTarget({0, 0, 200, 50});
        press.onActivate = [this] { ++pressed; };
        if (striped) {
            auto& stripe = page.makeLayer("stripe");
            stripe.setFrame({0, 0, 200, 4});
            stripe.setContent(d::solid("stripe", {0, 0, 200, 4}, d::rgb(0xFAD41F)));
            stripe.setAnimatable();
            press.layer().add(stripe);
        }
        page.layer().add(press.layer());
        page.add(press);
        for (std::size_t n = 0; n < tiles; ++n) {
            auto& tile = page.makeLayer("tile" + std::to_string(n));
            tile.setFrame({double(n % 400), 420 + double(n / 400), 1, 1});
            tile.setContent(d::solid("tile", {0, 0, 1, 1}, d::rgb(0x2A2A2A)));
            page.layer().add(tile);
        }
    }
    void willLeave() override { ++leaves; }
    unsigned activations{};
    void didActivate() override { ++activations; }
    bool handleKey(const d::KeyInput& k) override { if (k.virtualKey == 'K') { ++keys; return true; } return false; }
    bool handleScroll(d::Point, d::Point delta) override { scrolls += delta.y > 0; return true; }
    std::optional<d::Cursor> cursor(std::optional<d::Point> p) const override { return p && p->y > 400 ? std::optional(d::Cursor::openHand) : std::nullopt; }
    // The entrance the owner plays (HUDDirectionalContent.playEntrance(from:delay:)),
    // seen through its page part (no page is rebuilt here, so no carried entrance).
    unsigned entrances{};
    d::Point entranceOffset{};
    double entranceDelay{-1};
    std::function<void()> onEntrance; // sees the owner's state as the entrance plays
    void playPageEntrance(d::Point offset, double delay) override {
        ++entrances; entranceOffset = offset; entranceDelay = delay;
        if (onEntrance) onEntrance();
        d::Canvas::playPageEntrance(offset, delay);
    }
};
// A canvas with the real module header: Back, one primary action, and a
// staggered body (the header extras' native check, Mac 7d57641).
struct HeaderCanvas final : d::Canvas {
    explicit HeaderCanvas(d::Context& c) : Canvas(core::Module::eventLog, c) {}
    void buildPage(d::Page& page) override {
        auto& action = page.make<d::Button>("check", text("Add", "添加"), d::Icon::plus, d::Button::Kind::primary, d::Button::Height::compact);
        header().configure({"LOG", "DETAIL"}, "MOD-09", title("Clipboard", "剪贴板缓存"), "07", true, {&action});
        auto& body = page.makeLayer("body");
        body.setFrame({0, 120, 300, 40});
        body.setContent(d::solid("body", {0, 0, 300, 40}, d::rgb(0x2A2A2A)));
        page.layer().add(body);
        page.stagger(body);
    }
};
d::Layer& headerChild(d::Layer& header, std::string_view suffix) {
    for (d::Layer* child : header.children()) if (std::string_view(child->id()).ends_with(suffix)) return *child;
    throw std::runtime_error("No header layer ending in " + std::string(suffix));
}
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
    bool recording{};
    overlayOptions.recordingMode = [&] { return recording; };
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
    auto volumeCanvas = std::make_unique<FakeCanvas>(context, core::Module::volume);
    auto* skipped = volumeCanvas.get();
    core->add(std::move(volumeCanvas));
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
        raster.relieve(renderer); // the Application, once every owner collected
        return entries.size();
    };
    // --- pose, input, cursor -------------------------------------------------------------
    check(frame(sample(core::Module::clipboard), 0) > 0 && fake->builds == 1, "The shown canvas builds and composes");
    {
        // A thumb length change is a pose change: the thumb's one raster is stretched.
        const auto& thumb = *fake->rows->track().children().front();
        const auto rasterized = raster.stats().rasterizations;
        const double before = d::thumbLength(thumb);
        for (const double height : {200., 300., 500.}) { fake->rows->setContentHeight(height); frame(sample(core::Module::clipboard), 0); }
        check(before == 25 && std::abs(d::thumbLength(thumb) - 20) < 1e-9 && raster.stats().rasterizations == rasterized,
            "Growing content shortens the thumb without a new raster");
        fake->rows->setContentHeight(400); frame(sample(core::Module::clipboard), 0);
    }
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
    // --- the module entrance ----------------------------------------------------------------
    {
        // The HUD's open (ModuleEntrance::delay, SystemHUDView.playOpeningModuleEntrance):
        // the canvas gets the delay, counted from the entrance's start, so a frame that
        // lands after the open still starts the stagger with the canvas fade.
        auto entering = sample(core::Module::clipboard);
        entering.current.entrance = core::ModuleEntrance{{0, 24}, .06, 0, .2};
        frame(entering, .06);
        check(fake->entrances == 1 && fake->entranceDelay == .2 && fake->entranceOffset.x == 0 && fake->entranceOffset.y == 24, "The owner passes the open's entrance delay");
        frame(entering, .065);
        check(fake->entrances == 1, "An entrance plays once");
        entering.current.entrance = core::ModuleEntrance{{0, 24}, .07, 0, .2};
        frame(entering, .08);
        check(fake->entrances == 2 && std::abs(fake->entranceDelay - .19) < 1e-12, "A frame after the open passes what remains of the delay");
        entering.current.entrance = core::ModuleEntrance{{24, 0}, .085, 0, 0};
        frame(entering, .09);
        check(fake->entrances == 3 && fake->entranceDelay == 0 && fake->entranceOffset.x == 24, "A swap's entrance has no delay");
        // The end of the boot log (Mac 8ccf305): the boot log conceals the
        // centre (opacity 0, still attached); the sample that shows it again
        // carries its entrance from below, and that update plays the entrance
        // before it poses the canvas, so no frame shows the whole page before
        // its rows are hidden.
        auto concealed = entering;
        concealed.current.opacity = 0;
        frame(concealed, .092);
        const auto hidden = owner.core().pose(core::Module::clipboard);
        check(fake->entrances == 3 && hidden && hidden->opacity == 0, "The concealed centre is posed at opacity 0 and plays no entrance");
        const auto below = core::ShellPosition::subpageOffset(0, 1);
        std::optional<bool> posedFirst;
        fake->onEntrance = [&] { posedFirst = owner.core().pose(core::Module::clipboard).has_value(); };
        auto revealed = concealed;
        revealed.current.opacity = 1;
        revealed.current.entrance = core::ModuleEntrance{below, .095, 0, 0};
        frame(revealed, .095);
        fake->onEntrance = nullptr;
        const auto shown = owner.core().pose(core::Module::clipboard);
        check(fake->entrances == 4 && fake->entranceDelay == 0 && fake->entranceOffset.x == below.x && fake->entranceOffset.y == below.y,
            "The boot log's end plays the centre's entrance from below with no delay");
        check(posedFirst == false && shown && shown->opacity == 1,
            "The update that shows the centre plays its entrance before its first pose");
    }
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
    check(routing.pointerCapture == 1 && routing.wheelCapture == 0 && routing.keyCapture == 0, "The overlay routes first like the quit card (the facility banner and tags press before it)");
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
    // --- Recording mode after a skipped switch (rc139 freeze) ---------------------------------
    // A directional switch shows its incoming screen unattached; a request that
    // supersedes it drops that screen, never activated. It leaves (the Mac's
    // next transition calls prepareToLeave on the skipped screen) and is not
    // shown any more: a Recording mode switch builds only the shown canvas's
    // page, and the skipped one builds once, when it is next shown.
    const auto skipping = [&](core::Module from, core::Module to, core::Module next) {
        auto s = sample(from);
        s.requested = next; s.transitioning = s.directional = true; s.acceptsModuleInput = false;
        auto incoming = sample(to, false).current;
        s.incoming = incoming;
        return s;
    };
    frame(skipping(core::Module::clipboard, core::Module::volume, core::Module::notes), 2.2);
    check(skipped->isShown() && skipped->leaves == 0, "The unattached incoming screen is shown");
    frame(sample(core::Module::clipboard), 2.3);
    check(!skipped->isShown() && skipped->leaves == 1, "The skipped screen leaves when a superseding switch drops it");
    {
        const auto pages = context.pagesBuilt();
        const unsigned shown = fake->builds, skippedBuilds = skipped->builds;
        recording = true; overlay.setAppearance(appearance, 2.4);
        check(context.pagesBuilt() == pages + 1 && fake->builds == shown + 1 && skipped->builds == skippedBuilds,
            "After a skipped switch a Recording mode switch builds only the shown page");
        frame(sample(core::Module::volume), 2.5);
        check(skipped->builds == skippedBuilds + 1 && skipped->leaves == 1, "The skipped canvas builds once when shown");
    }
    // The HUD closing over an unattached incoming screen: it leaves, and the
    // closing frames (the swap still runs and lands) never show it again.
    frame(skipping(core::Module::volume, core::Module::clipboard, core::Module::clipboard), 2.6);
    check(fake->isShown(), "The incoming Clipboard screen is shown");
    {
        const unsigned leaves = fake->leaves;
        owner.setOverlayVisible(false, 2.7); overlay.setOverlayVisible(false, 2.7);
        check(!fake->isShown() && fake->leaves == leaves + 1, "Closing over the incoming screen leaves it");
        frame(skipping(core::Module::volume, core::Module::clipboard, core::Module::clipboard), 2.72);
        frame(sample(core::Module::clipboard), 2.76);   // the swap lands while the HUD closes
        check(!fake->isShown() && !skipped->isShown() && fake->leaves == leaves + 1, "Closing frames show no canvas again");
        const auto pages = context.pagesBuilt();
        recording = false; overlay.setAppearance(appearance, 2.8);
        check(context.pagesBuilt() == pages, "Closed, a Recording mode switch builds no page");
        owner.setOverlayVisible(true, 2.9); overlay.setOverlayVisible(true, 2.9);
        const unsigned builds = fake->builds;
        frame(sample(core::Module::clipboard), 3);
        check(fake->builds == builds + 1 && fake->isActive(), "Reopened, the canvas shown again builds once in the new mode");
    }
    // A canvas prewarmed while closed that the first opening does not present
    // leaves on that first frame; Recording mode switches then skip it.
    owner.setOverlayVisible(false, 3.05); overlay.setOverlayVisible(false, 3.05);
    owner.prewarm({center, chrome, sample(core::Module::volume, false), 0, 3.1});
    check(skipped->isShown(), "Prewarm shows the canvas it builds");
    owner.setOverlayVisible(true, 3.15); overlay.setOverlayVisible(true, 3.15);
    {
        const unsigned leaves = skipped->leaves;
        frame(sample(core::Module::clipboard), 3.2);
        check(!skipped->isShown() && skipped->leaves == leaves + 1, "The first opening on another module leaves the prewarmed canvas");
        const auto pages = context.pagesBuilt();
        const unsigned builds = skipped->builds;
        recording = true; overlay.setAppearance(appearance, 3.3);
        check(context.pagesBuilt() == pages + 1 && skipped->builds == builds, "A Recording mode switch then builds only the shown page");
        frame(sample(core::Module::volume), 3.4);
        check(skipped->builds == builds + 1, "The prewarmed canvas builds once when shown");
    }
    // --- a crowded shared raster cache -------------------------------------------------------
    // Every module visited in one opening keeps its scenes, their entries
    // counted in the shared cache until the HUD closes. Past three quarters of
    // a shared bound the modules switched away park at the next composition
    // change, least recently shown first, down to five eighths; the module
    // shown last before the current one stays for a quick return unless the
    // cache is still past seven eighths.
    {
        frame(sample(core::Module::clipboard), 3.5);
        const auto kept = core0.sceneStats(core::Module::volume);
        check(kept && kept->rasterEntries > 0 && !raster.crowded(), "With room, a module switched away keeps its scene");
        const auto tiny = ehud::data::Json::Object{{"id", "f"}, {"bounds", ehud::data::Json::Array{0, 0, 1, 1}}, {"position", ehud::data::Json::Array{0, 0}},
            {"anchorPoint", ehud::data::Json::Array{0, 0}}, {"backgroundColor", ehud::data::Json::Object{{"sRGB", ehud::data::Json::Array{1, 1, 1, 1}}}}, {"children", ehud::data::Json::Array{}}};
        gpu::LayerRasterOptions plain; plain.pixelsPerPoint = 1;
        std::vector<std::string> crowd;
        while (raster.stats().entries <= gpu::LayerRasterizer::maximumEntries / 4 * 3) { crowd.push_back("crowd." + std::to_string(crowd.size())); (void)raster.rasterize(crowd.back(), 1, tiny, plain); }
        check(raster.crowded(), "Other owners' entries crowd the shared cache");
        // Crowded by what cannot park: switching back and forth parks and
        // rebuilds nothing (each time the module left is the one shown last).
        const auto clipboard0 = *core0.sceneStats(core::Module::clipboard);
        frame(sample(core::Module::volume), 3.6);
        frame(sample(core::Module::volume), 3.65);
        const auto left = core0.sceneStats(core::Module::clipboard);
        check(left && left->rasterEntries > 0 && left->parks == clipboard0.parks && raster.crowded(),
            "Crowded by what cannot park, the module just left keeps its page");
        check(core0.sceneStats(core::Module::volume)->rasterEntries > 0 && frame(sample(core::Module::volume), 3.7) > 0, "The shown module keeps its page");
        const auto volume0 = *core0.sceneStats(core::Module::volume);
        frame(sample(core::Module::clipboard), 3.72);
        frame(sample(core::Module::volume), 3.74);
        frame(sample(core::Module::clipboard), 3.76);
        const auto clipboard1 = *core0.sceneStats(core::Module::clipboard), volume1 = *core0.sceneStats(core::Module::volume);
        check(clipboard1.parks == clipboard0.parks && clipboard1.partsLoaded == clipboard0.partsLoaded && volume1.parks == volume0.parks &&
            volume1.partsLoaded == volume0.partsLoaded && volume1.rasterEntries > 0, "Switching back and forth in a crowd that cannot shrink parks and rebuilds nothing");
        for (const auto& id : crowd) (void)raster.remove(id);
        check(!raster.crowded() && frame(sample(core::Module::clipboard), 3.8) > 0 && core0.sceneStats(core::Module::clipboard)->rasterEntries > 0,
            "With room, the shown module composes its page");
        frame(sample(core::Module::clipboard), 3.85);
        check(core0.sceneStats(core::Module::volume)->rasterEntries > 0, "With room again, the module switched away keeps its scene");
        // The shown module's replaced parts (retired scenes, still holding
        // their entries until collected) do not count: collected first, they
        // leave the cache at exactly three quarters, which is not crowded,
        // and the module switched away keeps its scene.
        const auto restyle = [&](bool striped, double t) { fake->striped = striped; fake->rerender(); return frame(sample(core::Module::clipboard), t); };
        const auto loads = core0.sceneStats(core::Module::clipboard)->partsLoaded;
        restyle(true, 3.9);
        const auto stripedEntries = raster.stats().entries;
        check(core0.sceneStats(core::Module::clipboard)->partsLoaded > loads && core0.sceneStats(core::Module::clipboard)->retiredParts == 0,
            "A new surface in the page loads the page part again and retires the old one");
        restyle(false, 3.95);
        const auto line = gpu::LayerRasterizer::maximumEntries / 4 * 3;
        check(stripedEntries < line && core0.sceneStats(core::Module::volume)->rasterEntries > 0, "The restyled page leaves room");
        std::vector<std::string> filler;
        for (std::size_t n = 0; n < line - stripedEntries; ++n) { filler.push_back("filler." + std::to_string(n)); (void)raster.rasterize(filler.back(), 1, tiny, plain); }
        const auto again = core0.sceneStats(core::Module::clipboard)->partsLoaded;
        restyle(true, 4.0);
        check(core0.sceneStats(core::Module::clipboard)->partsLoaded > again && core0.sceneStats(core::Module::volume)->rasterEntries > 0,
            "Retired parts of the shown module never park the module switched away");
        check(raster.stats().entries == line && !raster.crowded(), "Collected, the restyled page and the crowd fill exactly three quarters");
        for (const auto& id : filler) (void)raster.remove(id);
        restyle(false, 4.05);
    }
    // --- the HUD close (48584b5, 5c8a905): input stops, the canvas stays --------------------------
    // C5 and C7: the canvas stays active and drawn with input suspended, a
    // press held across the close activates nothing (its capture loss and
    // release included), and the full leave waits for the hidden HUD; C1: the
    // confirm bar keeps composing, deciding nothing, until then.
    {
        frame(sample(core::Module::clipboard), 4.45);
        const auto pressAt = centre(*core0.clientRect({100, 200, 200, 50}));
        check(fake->isActive() && core0.inputModule() == core::Module::clipboard, "Clipboard is shown before the close");
        const unsigned leaves = fake->leaves, pressed = fake->pressed, keys = fake->keys, scrolls = fake->scrolls;
        unsigned decided{};
        d::ConfirmRequest closeRequest; closeRequest.id = "close"; closeRequest.title = "Delete?"; closeRequest.confirmTitle = "Delete";
        owner.pointer({app::PointerKind::down, app::PointerButton::left, pressAt.x, pressAt.y}, 4.451);
        check(owner.capturesPointer(), "A press is held");
        owner.overlayClosing(4.452); overlay.overlayClosing(4.452);
        check(fake->isActive() && fake->leaves == leaves && !core0.inputModule() && !owner.capturesPointer() && !owner.pointerLocked() && !owner.covers(pressAt) && !owner.cursorAt(pressAt),
            "The close keeps the canvas active with no input");
        check(!owner.pointer({app::PointerKind::captureLost, app::PointerButton::none, pressAt.x, pressAt.y}, 4.453)
            && !owner.pointer({app::PointerKind::up, app::PointerButton::left, pressAt.x, pressAt.y}, 4.454) && fake->pressed == pressed,
            "The held press activates nothing: no synthetic release, its capture loss and release are ignored");
        check(!owner.wheel({pressAt.x, pressAt.y, -1, false, 0, 3}, 4.455) && !core0.key({app::KeyKind::down, 'K'}, 0, 4.456) && fake->keys == keys && fake->scrolls == scrolls,
            "Wheel and keys are ignored");
        (void)owner.automationRefresh(nullptr);
        check(owner.automationCount() == 0, "No automation stand-ins while closing");
        check(frame(sample(core::Module::clipboard), 4.5) > 0 && fake->isActive(), "The canvas keeps drawing through the close");
        context.confirm(closeRequest, [&] { ++decided; }, [&] { ++decided; });
        overlay.overlayClosing(4.51);
        check(frame(sample(core::Module::clipboard), 4.52) > 1 && overlay.presented() && !overlay.capturesPointer() && !overlay.key({app::KeyKind::down, VK_ESCAPE}, 4.53) && !decided,
            "A confirm bar keeps composing through the close and decides nothing");
        (void)owner.hostEvent({app::HostEvent::Kind::overlayConcealed, 0}, 4.8); (void)overlay.hostEvent({app::HostEvent::Kind::overlayConcealed, 0}, 4.8);
        check(!fake->isActive() && fake->leaves == leaves + 1 && !overlay.capturesPointer() && !decided, "The hidden HUD leaves and resets the overlay");
        frame(sample(core::Module::clipboard), 4.81);
        check(overlay.modalEntries().empty() && !overlay.presented(), "Nothing of the overlay is composed once hidden");
        owner.setOverlayVisible(true, 4.9); overlay.setOverlayVisible(true, 4.9);
        check(frame(sample(core::Module::clipboard), 4.95) > 0 && fake->isActive() && core0.inputModule() == core::Module::clipboard, "The next open activates it again");
        // A reopen mid-close (C19) ends that close first: the canvas left and comes back.
        owner.overlayClosing(4.96); overlay.overlayClosing(4.96);
        owner.setOverlayVisible(true, 4.97); overlay.setOverlayVisible(true, 4.97);
        check(fake->leaves == leaves + 2 && frame(sample(core::Module::clipboard), 4.98) > 0 && fake->isActive(), "A reopen mid-close leaves, then shows the canvas again");
    }
    // B5-1: a switch the close caught. Forward (Volume to Clipboard at +.05):
    // Volume departs, posed and drawn without input; Clipboard landed hidden,
    // built but never drawn or activated. Reversed: the screen still leaving
    // stays active with its input suspended.
    {
        frame(sample(core::Module::volume), 5);
        check(skipped->isActive(), "Volume is shown");
        auto toClipboard = sample(core::Module::volume);
        toClipboard.requested = core::Module::clipboard; toClipboard.transitioning = toClipboard.directional = true; toClipboard.acceptsModuleInput = false;
        toClipboard.incoming = sample(core::Module::clipboard, false).current;
        frame(toClipboard, 5.01);
        check(!skipped->isActive() && !fake->isActive(), "The switch leaves Volume; Clipboard is only prepared");
        owner.overlayClosing(5.05); overlay.overlayClosing(5.05);
        auto settled = sample(core::Module::clipboard, false);   // landed hidden
        settled.departing = sample(core::Module::volume).current;
        settled.departing->opacity = .4;
        frame(settled, 5.06);
        check(!owner.entries(core::Module::volume).empty() && !core0.inputModule() && !skipped->isActive(), "The departing Volume is drawn, takes no input and is not activated");
        check(owner.entries(core::Module::clipboard).empty() && fake->isShown() && !fake->isActive(), "The landed Clipboard is built but not drawn or activated");
        settled.departing.reset();
        frame(settled, 5.2);
        check(owner.entries(core::Module::volume).empty() && !skipped->isShown(), "Once its leave ended the departing screen goes");
        (void)owner.hostEvent({app::HostEvent::Kind::overlayConcealed, 0}, 5.4); (void)overlay.hostEvent({app::HostEvent::Kind::overlayConcealed, 0}, 5.4);
        owner.setOverlayVisible(true, 5.5); overlay.setOverlayVisible(true, 5.5);
        frame(sample(core::Module::clipboard), 5.55);
        check(fake->isActive() && !owner.entries(core::Module::clipboard).empty(), "The next open shows the landed module");
        // Reversed: Clipboard to Volume (Clipboard leaves), back to Clipboard
        // while it is still leaving (it reactivates on its own leave), then
        // the close: Clipboard stays active with input suspended, Volume was
        // never activated.
        const unsigned volumeActivations = skipped->activations, clipboardActivations = fake->activations;
        auto toVolume = sample(core::Module::clipboard);
        toVolume.requested = core::Module::volume; toVolume.transitioning = toVolume.directional = true; toVolume.acceptsModuleInput = false;
        toVolume.incoming = sample(core::Module::volume, false).current;
        frame(toVolume, 5.6);
        check(!fake->isActive() && !skipped->isActive(), "The switch away leaves Clipboard; Volume is only prepared");
        auto reversed = sample(core::Module::clipboard);
        reversed.transitioning = reversed.directional = true; reversed.acceptsModuleInput = false;
        reversed.current.opacity = .5;
        frame(reversed, 5.62);
        check(fake->isActive() && fake->activations == clipboardActivations + 1 && !core0.inputModule(), "Requested again during its own leave, Clipboard is active again");
        owner.overlayClosing(5.65); overlay.overlayClosing(5.65);
        frame(reversed, 5.66);
        check(fake->isActive() && !core0.inputModule() && !owner.entries(core::Module::clipboard).empty() && skipped->activations == volumeActivations && !skipped->isActive(),
            "A reversed switch keeps its screen active with input suspended; the skipped Volume never activated");
        (void)owner.hostEvent({app::HostEvent::Kind::overlayConcealed, 0}, 5.8); (void)overlay.hostEvent({app::HostEvent::Kind::overlayConcealed, 0}, 5.8);
        owner.setOverlayVisible(true, 5.9); overlay.setOverlayVisible(true, 5.9);
        frame(sample(core::Module::clipboard), 5.95);
        check(fake->isActive(), "Reopened");
    }
    // --- the crowd across owners -----------------------------------------------------------------
    // In the application every module has its own owner core, all sharing
    // one rasterizer, and the owners collect in registration order. The
    // crowd is judged once, after every owner collected (relieve), and the
    // pages park least recently shown first across all of them.
    {
        const auto tiny = ehud::data::Json::Object{{"id", "f"}, {"bounds", ehud::data::Json::Array{0, 0, 1, 1}}, {"position", ehud::data::Json::Array{0, 0}},
            {"anchorPoint", ehud::data::Json::Array{0, 0}}, {"backgroundColor", ehud::data::Json::Object{{"sRGB", ehud::data::Json::Array{1, 1, 1, 1}}}}, {"children", ehud::data::Json::Array{}}};
        gpu::LayerRasterOptions plain; plain.pixelsPerPoint = 1;
        const auto line = gpu::LayerRasterizer::maximumEntries / 4 * 3, lowMark = gpu::LayerRasterizer::maximumEntries / 8 * 5;
        const auto make = [&](gpu::LayerRasterizer& cache, const char* name, std::initializer_list<std::pair<core::Module, std::size_t>> pages, std::vector<FakeCanvas*>& out, bool bare = false) {
            tools::DesignOwnerOptions o;
            o.name = name; o.raster.pixelsPerPoint = 1;
            auto c = std::make_unique<tools::DesignOwnerCore>(cache, context, o);
            for (const auto& [m, tiles] : pages) { auto canvas = std::make_unique<FakeCanvas>(context, m); canvas->tiles = tiles; canvas->bare = bare; out.push_back(canvas.get()); c->add(std::move(canvas)); }
            return c;
        };
        bool crowdedBeforeCollect{};
        std::size_t relieved{};
        // One frame of the application over these cores (registration order):
        // every owner collects, then the shared cache is relieved.
        const auto present = [&](gpu::LayerRasterizer& cache, std::initializer_list<tools::DesignOwnerCore*> cores, const core::ModulePresentationSample& s, double t) {
            const app::ModuleFrame f{center, chrome, s, 1, t, true};
            std::vector<gpu::LayerCompositionEntry> entries;
            for (auto* c : cores) { c->update(f); c->upload(renderer); }
            for (auto* c : cores) for (const auto& e : c->entries(s.current.module)) entries.push_back(e);
            if (s.incoming) for (auto* c : cores) for (const auto& e : c->entries(s.incoming->module)) entries.push_back(e);
            composition.setEntries(renderer, entries);
            composition.present(renderer);
            renderer.draw(false);
            crowdedBeforeCollect = cache.crowded();
            for (auto* c : cores) c->collected(renderer);
            relieved = cache.relieve(renderer);
            return entries.size();
        };
        const auto tour = [&](gpu::LayerRasterizer& cache, std::initializer_list<tools::DesignOwnerCore*> cores, core::Module m, double t) {
            return present(cache, cores, sample(m), t);
        };
        const auto stats = [](tools::DesignOwnerCore& c, core::Module m) { return *c.sceneStats(m); };
        const auto fill = [&](gpu::LayerRasterizer& cache, std::vector<std::string>& ids, std::size_t n) {
            for (std::size_t k = 0; k < n; ++k) { ids.push_back("crowd." + std::to_string(ids.size())); (void)cache.rasterize(ids.back(), 1, tiny, plain); }
        };
        const auto unpublish = [&](std::initializer_list<tools::DesignOwnerCore*> cores) {
            composition.setEntries(renderer, std::vector<gpu::LayerCompositionEntry>{}); composition.present(renderer); renderer.draw(false);
            for (auto* c : cores) { c->collected(renderer); c->release(renderer); }
        };
        // A failed check leaves a block with its cores' entries still
        // composed: they are withdrawn before the cores go, so the failure
        // is reported rather than a crash in the unwinding.
        struct Withdrawn {
            gpu::LayerComposition& composition; gpu::Renderer& renderer;
            ~Withdrawn() { try { composition.setEntries(renderer, std::vector<gpu::LayerCompositionEntry>{}); } catch (...) {} }
        };
        // 1) The shown module's replaced parts, still held by its owner while
        // an owner registered before it collects, never park the modules
        // switched away (in either registration order): neither the module
        // just left nor an older one (Calendar, left before Work Mode, in the
        // shown module's owner), which a crowd judged before every owner
        // collected would park.
        {
            gpu::LayerRasterizer cache;
            std::vector<FakeCanvas*> canvases;
            auto awayCore = make(cache, "awayTest", {{core::Module::workMode, 0}}, canvases);
            auto shownCore = make(cache, "shownTest", {{core::Module::storage, 0}, {core::Module::calendar, 0}}, canvases);
            const Withdrawn withdrawn{composition, renderer};
            auto* shown = canvases[1];
            awayCore->resize(metrics); shownCore->resize(metrics);
            awayCore->setOverlayVisible(true, 10); shownCore->setOverlayVisible(true, 10);
            auto* a = awayCore.get(); auto* b = shownCore.get();
            const auto restyle = [&](bool striped, double t, bool awayFirst) {
                shown->striped = striped; shown->rerender();
                return awayFirst ? tour(cache, {a, b}, core::Module::storage, t) : tour(cache, {b, a}, core::Module::storage, t);
            };
            tour(cache, {a, b}, core::Module::calendar, 10);
            tour(cache, {a, b}, core::Module::workMode, 10.05);
            tour(cache, {a, b}, core::Module::storage, 10.1);
            tour(cache, {a, b}, core::Module::storage, 10.15);
            check(stats(*a, core::Module::workMode).rasterEntries > 0 && stats(*b, core::Module::calendar).rasterEntries > 0 && !cache.crowded(),
                "With room, the modules switched away (both owners) keep their pages");
            restyle(true, 10.2, true);
            const auto striped = cache.stats().entries;
            restyle(false, 10.25, true);
            check(striped < line, "The restyled page leaves room");
            std::vector<std::string> crowd;
            fill(cache, crowd, line - striped);
            const auto away0 = stats(*a, core::Module::workMode), older0 = stats(*b, core::Module::calendar);
            for (const bool awayFirst : {true, false}) {
                const auto loads = stats(*b, core::Module::storage).partsLoaded;
                restyle(true, awayFirst ? 10.3 : 10.4, awayFirst);
                check(crowdedBeforeCollect && stats(*b, core::Module::storage).partsLoaded > loads, "Before collection, the replaced part crowds the cache");
                const auto olderNow = stats(*b, core::Module::calendar);
                check(olderNow.rasterEntries == older0.rasterEntries && olderNow.parks == older0.parks,
                    "Replaced parts of the shown module never park an older module switched away (the crowd is judged once every owner collected)");
                const auto awayNow = stats(*a, core::Module::workMode);
                check(cache.stats().entries == line && !cache.crowded() && awayNow.rasterEntries == away0.rasterEntries && awayNow.parks == away0.parks && relieved == 0,
                    awayFirst ? "Replaced parts of the shown module never park another owner's module switched away (registered before it)"
                              : "Replaced parts of the shown module never park another owner's module switched away (registered after it)");
                restyle(false, awayFirst ? 10.35 : 10.45, awayFirst);
            }
            for (const auto& id : crowd) (void)cache.remove(id);
            unpublish({a, b});
        }
        // 2) Least recently shown first, across owners, down to five eighths.
        // Owner Y (registered first) holds the third and fourth module shown,
        // X the first, second and fifth; the third page is the heaviest.
        {
            gpu::LayerRasterizer cache;
            std::vector<FakeCanvas*> canvases;
            auto coreY = make(cache, "relieveY", {{core::Module::calendar, 540}, {core::Module::hotkeys, 0}}, canvases);
            auto coreX = make(cache, "relieveX", {{core::Module::eventLog, 270}, {core::Module::about, 270}, {core::Module::display, 0}}, canvases);
            const Withdrawn withdrawn{composition, renderer};
            coreY->resize(metrics); coreX->resize(metrics);
            coreY->setOverlayVisible(true, 20); coreX->setOverlayVisible(true, 20);
            auto* y = coreY.get(); auto* x = coreX.get();
            const auto go = [&](core::Module m, double t) { return tour(cache, {y, x}, m, t); };
            const core::Module m1 = core::Module::eventLog, m2 = core::Module::about, m3 = core::Module::calendar, m4 = core::Module::hotkeys, m5 = core::Module::display;
            const auto of = [&](core::Module m) { return stats(m == m3 || m == m4 ? *y : *x, m); };
            double t = 20;
            for (const auto m : {m1, m2, m3, m4, m5}) { go(m, t += .1); go(m, t += .05); }
            for (const auto m : {m1, m2, m3, m4, m5}) check(of(m).rasterEntries > 0 && of(m).parks == 0, "With room, every module visited keeps its page");
            check(!cache.crowded() && relieved == 0, "With room, nothing parks");
            const auto e1 = of(m1).rasterEntries, e2 = of(m2).rasterEntries, e3 = of(m3).rasterEntries;
            check(e1 < line + 1 - lowMark && e1 + e2 >= line + 1 - lowMark && e3 >= line + 1 - lowMark, "The pages' weights: the oldest alone is not enough, the two oldest are, the heaviest alone is");
            std::vector<std::string> crowd;
            fill(cache, crowd, line + 1 - cache.stats().entries);
            check(cache.crowded(), "Past three quarters");
            go(m5, t += .1);
            check(relieved == 2 && of(m1).rasterEntries == 0 && of(m1).parts == 0 && of(m1).parks == 1 && of(m2).rasterEntries == 0 && of(m2).parks == 1,
                "Crowded, the two least recently shown modules park (across owners)");
            check(of(m3).rasterEntries == e3 && of(m3).parks == 0 && of(m4).rasterEntries > 0 && of(m4).parks == 0 && of(m5).rasterEntries > 0,
                "Below five eighths the parks stop: newer modules switched away keep their pages");
            check(cache.stats().entries <= lowMark && !cache.crowded(), "The cache is back at or below five eighths");
            // Crowded again: the oldest page still holding artwork parks; the
            // pages parked before hold nothing and are not parked again.
            fill(cache, crowd, line + 1 - cache.stats().entries);
            go(m5, t += .1);
            check(relieved == 1 && of(m3).rasterEntries == 0 && of(m3).parks == 1 && of(m1).parks == 1 && of(m2).parks == 1 && of(m4).rasterEntries > 0,
                "Crowded again, the next oldest parks; parked pages are not parked again");
            // Nothing left to park (the module shown last before the current
            // one stays): no park, no throw, no rebuild while switching.
            fill(cache, crowd, line + 1 - cache.stats().entries);
            const auto e4 = of(m4), e5 = of(m5);
            go(m5, t += .1);
            check(relieved == 0 && cache.crowded() && of(m4).rasterEntries == e4.rasterEntries && of(m4).parks == 0, "Crowded with nothing to park: nothing parks");
            go(m4, t += .1); go(m4, t += .05); go(m5, t += .1); go(m5, t += .05);
            check(of(m4).partsLoaded == e4.partsLoaded && of(m5).partsLoaded == e5.partsLoaded && of(m4).parks == 0 && of(m5).parks == 0 && relieved == 0,
                "Switching between the last two modules in a crowd that cannot shrink parks and rebuilds nothing");
            // Near the hard bounds the module just left (m4) parks too, so
            // the shown module's next page fits; below seven eighths it stays.
            fill(cache, crowd, gpu::LayerRasterizer::maximumEntries / 16 * 13 + 1 - cache.stats().entries);
            go(m5, t += .1);
            check(relieved == 0 && of(m4).rasterEntries == e4.rasterEntries && of(m4).parks == 0, "Past thirteen sixteenths the module just left still keeps its page");
            fill(cache, crowd, gpu::LayerRasterizer::maximumEntries / 8 * 7 + 1 - cache.stats().entries);
            go(m5, t += .1);
            check(relieved == 1 && of(m4).rasterEntries == 0 && of(m4).parks == 1 && of(m5).rasterEntries > 0 && of(m5).parks == 0,
                "Past seven eighths the module just left parks too; the shown module keeps its page");
            go(m5, t += .1);
            check(relieved == 0 && of(m4).parks == 1 && of(m5).parks == 0, "Nothing left to park near the bounds: nothing parks again, no throw");
            for (const auto& id : crowd) (void)cache.remove(id);
            // With room again a parked module rasterizes when shown and
            // nothing else parks.
            go(m1, t += .1); go(m1, t += .05);
            check(of(m1).rasterEntries > 0 && relieved == 0 && of(m4).parks == 1 && of(m5).rasterEntries > 0 && of(m5).parks == 0, "With room again, nothing parks");
            unpublish({y, x});
        }
        // 3) A superseded switch: O shown, then A, B requested, then C
        // requested inside A's leave. B was shown (its incoming screen
        // prepared, unattached) but never drawn, so it keeps its older place:
        // older than A, the page the user left last, and than O, left before
        // B was shown. In a crowd B parks first; O and A stay. (A stamp on B
        // would make it newer than O, and O would park first.)
        {
            gpu::LayerRasterizer cache;
            std::vector<FakeCanvas*> canvases;
            const core::Module pageO = core::Module::calendar, pageA = core::Module::eventLog, pageB = core::Module::about, pageC = core::Module::display;
            auto coreA = make(cache, "supersededA", {{pageO, 300}, {pageA, 300}}, canvases);
            auto coreBC = make(cache, "supersededBC", {{pageB, 600}, {pageC, 0}}, canvases);
            const Withdrawn withdrawn{composition, renderer};
            coreA->resize(metrics); coreBC->resize(metrics);
            coreA->setOverlayVisible(true, 30); coreBC->setOverlayVisible(true, 30);
            auto* x = coreA.get(); auto* y = coreBC.get();
            const auto show = [&](const core::ModulePresentationSample& s, double t) { return present(cache, {x, y}, s, t); };
            show(sample(pageO), 29.9); show(sample(pageO), 29.95);
            show(sample(pageA), 30); show(sample(pageA), 30.05);
            show(skipping(pageA, pageB, pageB), 30.1);
            check(canvases[2]->isShown() && stats(*y, pageB).rasterEntries > 0, "The incoming B screen is shown and prepared (unattached)");
            show(skipping(pageA, pageC, pageC), 30.15);
            check(!canvases[2]->isShown() && canvases[3]->isShown(), "C requested inside A's leave: B is skipped, C comes in");
            show(sample(pageC), 30.2); show(sample(pageC), 30.25);
            const auto o0 = stats(*x, pageO), a0 = stats(*x, pageA), b0 = stats(*y, pageB);
            check(o0.rasterEntries > 0 && a0.rasterEntries > 0 && b0.rasterEntries > 0 && relieved == 0 && !cache.crowded(), "With room, O, A and B keep their pages");
            check(b0.rasterEntries >= line + 1 - lowMark && o0.rasterEntries < line + 1 - lowMark, "The pages' weights: B alone brings the crowd back to five eighths, O alone does not");
            std::vector<std::string> crowd;
            fill(cache, crowd, line + 1 - cache.stats().entries);
            show(sample(pageC), 30.3);
            const auto o1 = stats(*x, pageO), a1 = stats(*x, pageA), b1 = stats(*y, pageB);
            check(a1.rasterEntries == a0.rasterEntries && a1.parks == a0.parks, "Crowded after a superseded switch, the page the user left (A) is not parked before the skipped one");
            check(o1.rasterEntries == o0.rasterEntries && o1.parks == o0.parks, "The page left before the skipped one was shown (O) is not parked before it either");
            check(relieved == 1 && b1.rasterEntries == 0 && b1.parks == b0.parks + 1, "The skipped page (B, shown but never seen) parks first");
            for (const auto& id : crowd) (void)cache.remove(id);
            unpublish({x, y});
        }
        // 4) Cycling modules over held pixels the pages hold next to nothing
        // of (renderer resources, intrinsic images, editors), past three
        // quarters and under seven eighths. A page's pixels went to the GPU
        // at upload but for its scroll bar's track and thumb (small surfaces
        // whose description outweighs them: no recipe, held), a few KiB; a
        // bare page (no list) holds none. Either way the first crowded pass
        // parks the oldest page away, which gives back next to nothing, and
        // stops; switching A, B, C, A, ... after it parks and rebuilds
        // nothing, as in 1.3.0.
        for (const bool bare : {false, true}) {
            gpu::LayerRasterizer cache;
            std::vector<FakeCanvas*> canvases;
            const core::Module pageO = core::Module::calendar, pageA = core::Module::eventLog, pageB = core::Module::about, pageC = core::Module::display;
            auto coreO = make(cache, "cycleO", {{pageO, 0}}, canvases, bare);
            auto coreA = make(cache, "cycleA", {{pageA, 0}}, canvases, bare);
            auto coreB = make(cache, "cycleB", {{pageB, 0}}, canvases, bare);
            auto coreC = make(cache, "cycleC", {{pageC, 0}}, canvases, bare);
            const Withdrawn withdrawn{composition, renderer};
            for (auto* c : {coreO.get(), coreA.get(), coreB.get(), coreC.get()}) { c->resize(metrics); c->setOverlayVisible(true, 40); }
            auto* o = coreO.get(); auto* a = coreA.get(); auto* b = coreB.get(); auto* c = coreC.get();
            std::size_t parkedSince{};
            const auto go = [&](core::Module m, double t) { const auto n = tour(cache, {o, a, b, c}, m, t); parkedSince += relieved; return n; };
            const auto of = [&](core::Module m) { return stats(m == pageO ? *o : m == pageA ? *a : m == pageB ? *b : *c, m); };
            const auto held = [&] { const auto s = cache.stats(); return s.resourceBytes - s.releasedPixelBytes; };
            const auto empty = held();
            double t = 40;
            for (const auto m : {pageO, pageA, pageB, pageC}) { go(m, t += .1); go(m, t += .05); }
            for (const auto m : {pageO, pageA, pageB, pageC}) check(of(m).rasterEntries > 0 && of(m).parks == 0, "With room, every module visited keeps its page");
            const auto maximum = gpu::LayerRasterizer::maximumResourceBytes;
            if (bare) check(held() == empty && !cache.crowded(), "Bare pages hold no pixels in the cache (given to the GPU at upload)");
            else check(held() > empty && held() - empty < maximum / 64 && !cache.crowded(), "The pages hold a few pixels of their own in the cache (their scroll bars')");
            auto plate = tiny; plate["bounds"] = ehud::data::Json::Array{0, 0, 4096, 4096};
            gpu::LayerRasterOptions flat; flat.pixelsPerPoint = 1; flat.paddingPoints = 0;
            // 3 x 64 MiB + 4096 x 257 x 4: past three quarters of the held
            // pixel budget, under seven eighths; the entries are few.
            std::vector<std::string> crowd;
            for (unsigned n = 0; n < 3; ++n) { crowd.push_back("held." + std::to_string(n)); (void)cache.rasterize(crowd.back(), 1, plate, flat); }
            plate["bounds"] = ehud::data::Json::Array{0, 0, 4096, 257};
            crowd.push_back("held.3"); (void)cache.rasterize(crowd.back(), 1, plate, flat);
            check(cache.crowded() && held() > maximum / 4 * 3 && held() <= maximum / 8 * 7 && cache.stats().entries < lowMark,
                "Held pixels the pages hold next to nothing of crowd the cache, between three quarters and seven eighths");
            go(pageA, t += .1);
            check(relieved == 1 && of(pageO).parks == 1 && of(pageO).rasterEntries == 0 && of(pageB).parks == 0 && of(pageC).parks == 0,
                "The first crowded pass parks the oldest page away; it frees next to nothing and the pass stops");
            go(pageA, t += .05);
            const auto a0 = of(pageA), b0 = of(pageB), c0 = of(pageC);
            parkedSince = 0;
            for (unsigned round = 0; round < 2; ++round) for (const auto m : {pageB, pageC, pageA}) { go(m, t += .1); go(m, t += .05); }
            go(pageB, t += .1); go(pageB, t += .05);
            const auto a1 = of(pageA), b1 = of(pageB), c1 = of(pageC);
            check(parkedSince == 0 && a1.parks == a0.parks && b1.parks == b0.parks && c1.parks == c0.parks && of(pageO).parks == 1,
                "Cycling three modules over a crowd no page holds parks nothing at any switch");
            check(a1.partsLoaded == a0.partsLoaded && b1.partsLoaded == b0.partsLoaded && c1.partsLoaded == c0.partsLoaded &&
                a1.rasterEntries > 0 && b1.rasterEntries > 0 && c1.rasterEntries > 0, "Every return is warm: no page rebuilds");
            for (const auto& id : crowd) (void)cache.remove(id);
            unpublish({o, a, b, c});
        }
    }
    // --- the header extras (Mac 7d57641; E-D2, OPEN-B5-3) --------------------------------------
    // The side actions and Back are parts of their own, so each fades in with
    // the title as one image (a group), not surface by surface. The header's
    // leave (a sub-page swap fades the whole header) still draws as one
    // image: the frame equals the one the single-part header draws.
    {
        tools::DesignOwnerOptions o;
        o.name = "headerTest"; o.raster.pixelsPerPoint = 1;
        auto headerCore = std::make_unique<tools::DesignOwnerCore>(raster, context, o);
        auto headerCanvas = std::make_unique<HeaderCanvas>(context);
        auto* headerFake = headerCanvas.get();
        headerCore->add(std::move(headerCanvas));
        auto& c = *headerCore;
        struct Withdrawn {
            gpu::LayerComposition& composition; gpu::Renderer& renderer;
            ~Withdrawn() { try { composition.setEntries(renderer, std::vector<gpu::LayerCompositionEntry>{}); } catch (...) {} }
        } withdrawn{composition, renderer};
        c.resize(metrics); c.setOverlayVisible(true, 200);
        auto headerChrome = chrome; headerChrome.module = core::Module::eventLog;
        const auto show = [&](double t) {
            const app::ModuleFrame f{center, headerChrome, sample(core::Module::eventLog), 1, t, true};
            c.update(f); c.upload(renderer);
            std::vector<gpu::LayerCompositionEntry> entries;
            for (const auto& e : c.entries(core::Module::eventLog)) entries.push_back(e);
            composition.setEntries(renderer, entries);
            composition.present(renderer);
            renderer.draw(false);
            c.collected(renderer);
            return entries.size();
        };
        check(show(200) > 0 && headerFake->isActive(), "The header canvas shows");
        auto& header = headerFake->header();
        auto& actions = headerChild(header.layer(), "header.actions");
        auto& back = header.back()->layer();
        headerFake->playEntrance({0, 24}, 200.05);
        const double mid = 200.05 + d::staggerDelay(4) + .1;   // Back (3) and the actions (4) mid-fade
        show(mid);
        const auto fading = [&](const d::Layer& l) { const double o2 = l.presented(d::Channel::opacity, mid); return o2 > .05 && o2 < .95; };
        check(fading(actions) && fading(back), "Mid-entrance the actions and Back are fading in");
        check(c.sceneStats(core::Module::eventLog)->groups == 2, "The actions and Back each fade through a group (one image), the rest of the header directly");
        show(201.5);
        check(c.sceneStats(core::Module::eventLog)->groups == 0, "At rest no group is held");
        // The leave at its middle: the whole header at half opacity.
        const auto readback = [&](double t) { show(t); return renderer.readback(); };
        header.layer().setValue(d::Channel::opacity, .5);
        const auto split = readback(201.6);
        check(c.sceneStats(core::Module::eventLog)->groups >= 2, "Leaving, the header's parts fade through groups");
        actions.setPartRoot(false); back.setPartRoot(false);
        const auto single = readback(201.7);
        check(c.sceneStats(core::Module::eventLog)->groups >= 1, "The single-part header fades through one group");
        std::size_t drawn{}, differing{};
        for (std::size_t i = 0; i + 3 < split.pixels.size() && split.pixels.size() == single.pixels.size(); i += 4) {
            drawn += split.pixels[i + 3] > 0;
            for (std::size_t k = 0; k < 4; ++k) if (std::abs(int(split.pixels[i + k]) - int(single.pixels[i + k])) > 2) { ++differing; break; }
        }
        check(split.pixels.size() == single.pixels.size() && drawn > 1000, "The leaving header draws");
        check(differing == 0, "The header's leave draws exactly as the single-part header's one image");
        actions.setPartRoot(true); back.setPartRoot(true);
        header.layer().setValue(d::Channel::opacity, 1);
        show(201.8);
        composition.setEntries(renderer, std::vector<gpu::LayerCompositionEntry>{});
        composition.present(renderer); renderer.draw(false);
        c.collected(renderer); c.release(renderer);
    }
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
