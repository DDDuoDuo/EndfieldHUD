// The Settings owner (tools/settings_preview.*) on a hidden window and WARP:
// the four canvases in one DesignModuleOwner on the shared design Context
// (tools/hud_design_overlay_owner.hpp); pose, a projected click on a switch
// that saves its preference, the right-hand list glyphs and the app icon
// roster from the packaged resources, the Center logo chooser's artwork
// decoded on a (queued) executor and rasterized into memory images, the
// layout prompt on the shared confirm bar with its 12 s deadline through the
// owner's wake, park and release on close, and the artwork released when the
// HUD concealed. Synthetic preferences and data folder; no window shown, no
// input injected, no user data.
#include "tools/hud_design_overlay_owner.hpp"
#include "tools/settings_preview.hpp"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#include "native/watch_appearance.hpp"
#include "core/data/file_io.hpp"
#include <cmath>
#include <deque>
#include <iostream>
#include <stdexcept>
using namespace endfield;
namespace d = modules::design;
namespace m = modules;
namespace gpu = native;
namespace {
unsigned checks{};
void check(bool v, const char* s) { ++checks; if (!v) throw std::runtime_error(s); }
struct Window {
    HWND hwnd{};
    Window() {
        WNDCLASSW c{}; c.lpfnWndProc = DefWindowProcW; c.hInstance = GetModuleHandleW(nullptr); c.lpszClassName = L"EndfieldSettingsOwnerHidden";
        check(RegisterClassW(&c) != 0, "Register owned fixture");
        hwnd = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP, c.lpszClassName, L"Synthetic", WS_POPUP, 0, 0, 1000, 640, nullptr, nullptr, c.hInstance, nullptr);
        check(hwnd && !IsWindowVisible(hwnd), "Synthetic window hidden");
    }
    ~Window() { if (hwnd) DestroyWindow(hwnd); UnregisterClassW(L"EndfieldSettingsOwnerHidden", GetModuleHandleW(nullptr)); }
};
core::ModulePresentationSample sample(core::Module module) {
    core::ModulePresentationSample s;
    s.selected = s.requested = module;
    s.current.module = module; s.current.contentFrame = core::moduleLocalContentFrame(module);
    s.current.designScreen = true; s.current.hostClip = false; s.current.attached = true; s.current.opacity = 1;
    s.acceptsModuleInput = true;
    return s;
}
core::Point centre(core::Rect r) { return {r.x + r.width / 2, r.y + r.height / 2}; }
void run(const std::filesystem::path& shader, const std::filesystem::path& resources, const std::filesystem::path& wordmark) {
    Window window;
    gpu::Renderer renderer;
    renderer.initialize(window.hwnd, 1000, 640, {gpu::Driver::warpForTests, shader, gpu::RenderTarget::offscreenForTests});
    gpu::LayerRasterizer raster;
    gpu::LayerComposition composition;
    tools::DesignOverlayOptions overlayOptions;
    overlayOptions.raster.pixelsPerPoint = 1; overlayOptions.window = window.hwnd;
    tools::DesignOverlayOwner overlay(raster, overlayOptions);
    auto& context = overlay.context();
    const auto manifestBytes = ehud::data::detail::readFile(resources / "watch-appearance" / "manifest.json", 4 * 1024 * 1024);
    check(manifestBytes.has_value(), "Read the right-hand list's appearance templates");
    const gpu::WatchAppearanceTemplates appearanceTemplates(ehud::data::Json::parse(*manifestBytes, 8u * 1024 * 1024));
    // A synthetic data folder (no custom logo) and a queued executor drained by the test.
    const auto data = std::filesystem::temp_directory_path() / ("ehud-settings-owner-" + std::to_string(GetCurrentProcessId()));
    std::filesystem::remove_all(data); std::filesystem::create_directories(data);
    std::deque<std::pair<std::function<void()>, std::function<void(std::exception_ptr)>>> queue;
    const auto drain = [&] {
        while (!queue.empty()) {
            auto [work, done] = std::move(queue.front()); queue.pop_front();
            std::exception_ptr error;
            try { work(); } catch (...) { error = std::current_exception(); }
            done(error);
        }
    };
    std::vector<ehud::data::Settings> saved;
    tools::SettingsPreviewOptions options;
    options.initial.set("language", "english"); options.initial.set("launchAtLogin", false); options.initial.set("reduceMotion", true);
    options.callbacks.persist = [&](const ehud::data::Settings& s) { saved.push_back(s); };
    options.callbacks.registerShortcut = [](m::SettingsShortcut) -> std::optional<std::string> { return std::nullopt; };
    options.hooks.displays = [] { return std::vector<m::SettingsDisplay>{{"DISPLAY-A", "Fixture Display"}}; };
    options.resources = resources; options.appearance = &appearanceTemplates;
    options.logoPresets = resources / "center-logos"; options.endfieldWordmark = wordmark; options.logoStore = data / "CenterLogo";
    options.submit = [&](std::function<void()> work, std::function<void(std::exception_ptr)> done) { queue.emplace_back(std::move(work), std::move(done)); return true; };
    options.owner.name = "settings";
    options.owner.routing.pointerCapture = 1; options.owner.routing.pointer = 2; options.owner.routing.keyCapture = 1; options.owner.routing.key = 2;
    options.owner.raster.pixelsPerPoint = 1;
    options.owner.fields.window = window.hwnd; options.owner.fields.ownerMessage = WM_APP + 0x47; options.owner.fields.raster.pixelsPerPoint = 1;
    tools::SettingsPreview settings(raster, context, std::move(options));
    auto ownerPtr = settings.takeOwner();
    auto& owner = *ownerPtr;
    check(owner.name() == "settings" && owner.presents(core::Module::system) && owner.presents(core::Module::display) && owner.presents(core::Module::hotkeys)
        && owner.presents(core::Module::about) && !owner.presents(core::Module::notes), "One owner presents System, Display, Hotkeys and About");
    const app::ClientMetrics metrics{1000, 640, 96, 1, 1000, 640};
    owner.resize(metrics); overlay.resize(metrics);
    app::ModuleAppearance appearance; appearance.accentSRGB = {250. / 255, 212. / 255, 31. / 255}; appearance.reduceMotion = true;
    overlay.setAppearance(appearance, 0); owner.setAppearance(appearance, 0);
    d::Context::Appearance a; a.reduceMotion = true; context.apply(a, 0);
    overlay.setOverlayVisible(true, 0); owner.setOverlayVisible(true, 0);
    core::source::DesktopChromeSettings chrome; chrome.viewport = {0, 0, 1000, 640};
    const auto center = core::Matrix4{};
    const auto frame = [&](core::Module module, double t) {
        chrome.module = module;
        const app::ModuleFrame f{center, chrome, sample(module), 1, t, true};
        overlay.update(f); owner.update(f);
        overlay.upload(renderer); owner.upload(renderer);
        std::vector<gpu::LayerCompositionEntry> entries;
        for (const auto& e : owner.entries(module)) entries.push_back(e);
        for (const auto& e : overlay.modalEntries()) entries.push_back(e);
        composition.setEntries(renderer, entries);
        composition.present(renderer);
        renderer.draw(false);
        owner.collected(renderer); overlay.collected(renderer);
        return entries.size();
    };
    const auto rectOf = [&](core::Module module, std::string_view id) -> std::optional<core::Rect> {
        auto& canvas = settings.canvas(module);
        for (auto* c : canvas.page().controls()) if (c->id() == id) return settings.core().clientRect(c->rect(canvas.root()));
        return std::nullopt;
    };
    // --- System: pose and a projected click -------------------------------------------
    // Performance record (not a gate): raster entries and composed entries per page.
    const auto report = [&](const char* page, std::size_t entries) { std::cout << "  " << page << ": " << entries << " composed entries, " << raster.stats().entries << " raster entries\n"; };
    const auto first = frame(core::Module::system, 0);
    report("System", first);
    check(first > 0 && settings.inputModule() == core::Module::system, "System shows and takes input");
    const auto toggle = rectOf(core::Module::system, "focus");
    check(toggle && toggle->width > 0, "The Close when focus lost switch projects to the client");
    const bool before = settings.controller().committed().boolean("closeOnFocusLost");
    const auto at = centre(*toggle);
    owner.pointer({app::PointerKind::move, app::PointerButton::none, at.x, at.y}, .01);
    owner.pointer({app::PointerKind::down, app::PointerButton::left, at.x, at.y}, .02);
    owner.pointer({app::PointerKind::up, app::PointerButton::left, at.x, at.y}, .03);
    check(settings.controller().committed().boolean("closeOnFocusLost") != before && !saved.empty(), "A click on the switch saves the preference");
    check(frame(core::Module::system, .05) > 0, "The switch's change composes");
    // System > Modules: the list glyphs come from the packaged appearance templates.
    check(settings.canvas(core::Module::system).performAccessibilityAction("modules", .06), "Modules opens");
    const auto modulesEntries = frame(core::Module::system, .1);
    report("System > Modules", modulesEntries);
    check(modulesEntries > 0 && settings.canvas(core::Module::system).section() == "modules", "The Modules page composes its glyph rows");
    // --- Display: the Center logo chooser's artwork ---------------------------------------
    const auto displayEntries = frame(core::Module::display, .2);
    report("Display", displayEntries);
    check(displayEntries > 0 && !queue.empty(), "Display asks for the logo in use (decoded off the UI thread)");
    drain();
    check(frame(core::Module::display, .25) > 0, "The decoded artwork composes (or the title stays when the wordmark is not packaged)");
    auto& display = settings.canvas(core::Module::display);
    check(display.performAccessibilityAction("centerLogo", .3) && display.menuOpen(), "The logo menu opens");
    drain();
    check(settings.logoArtworks() >= 3, "The preset logos are decoded");
    display.closeMenu();
    check(display.performAccessibilityAction("centerLogo", .35), "Reopened, the tiles show the rasters");
    check(frame(core::Module::display, .4) > 0, "The icon grid composes with its memory images");
    display.closeMenu();
    check(display.performAccessibilityAction("appIcon", .45) && frame(core::Module::display, .5) > 0, "The App / menu bar icon grid composes the packaged icons");
    { const auto back = display.page().back; back(); }
    // --- The layout prompt on the shared bar, its deadline through the owner --------------
    settings.controller().previewScale(1.3, .6);
    auto& bar = context.overlay();
    check(bar.isConfirming() && bar.confirmState().request()->countdown == 12, "Keep this UI scale? asks on the shared bar");
    const auto wake = owner.nextWakeTime(.6);
    check(wake && std::abs(*wake - 1.6) < 1e-9, "The owner wakes at the next second");
    owner.deadline(1.6);
    check(bar.confirmState().request()->countdown == 11, "The countdown follows the owner's wake");
    owner.deadline(12.6); overlay.deadline(12.6);
    check(!settings.controller().scalePending() && !bar.isConfirming(), "Unanswered, the preview reverts and the bar closes");
    frame(core::Module::display, 12.7);
    // --- Close: park and release ------------------------------------------------------------
    const auto resident = raster.stats().entries;
    owner.setOverlayVisible(false, 13); overlay.setOverlayVisible(false, 13);
    owner.hostEvent({app::HostEvent::Kind::overlayConcealed}, 13);
    check(settings.logoArtworks() == 0, "The concealed HUD keeps no decoded logo artwork");
    frame(core::Module::system, 13.1);
    check(raster.stats().entries <= resident, "Closing parks what is not shown");
    composition.detach(renderer);
    owner.release(renderer); overlay.release(renderer);
    std::filesystem::remove_all(data);
    check(!IsWindowVisible(window.hwnd) && renderer.stats().presents == 0, "The fixture never shows a window");
}
}
int wmain(int argc, wchar_t** argv) {
    const auto com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    try {
        check(SUCCEEDED(com) && argc == 4, "Pass the shader, windows/resources and Resources/EndfieldIndustriesSource.png");
        run(argv[1], argv[2], argv[3]);
        CoUninitialize();
        std::cout << "Settings owner: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        if (SUCCEEDED(com)) CoUninitialize();
        std::cerr << "Settings owner after " << checks << ": " << e.what() << '\n';
        return 1;
    }
}
#else
int main() { return 0; }
#endif
