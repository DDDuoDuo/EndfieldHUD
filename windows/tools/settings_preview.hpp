#pragma once
// The Settings owner: one preference controller (modules/settings.hpp,
// HUDSettingsController) shown by four design canvases (System, Display,
// Hotkeys, About: modules/settings_canvas.hpp, HUDSettingsCanvas) in one
// DesignOwnerCore (tools/hud_design_owner.hpp), plus the host side of
// HUDSettingsInteraction that is not the app's own: the right-hand list
// glyphs (resources/watch-appearance), the app icon roster and images
// (resources/application-icons), the center logo artwork of the chooser
// (decoded on the shared utility executor, rasterized into memory images).
// The canvases observe the controller themselves (addObserver, as the Mac's).
//
// The colour wheel and the logo file picker stay the app's (they open native
// windows); their results come back through setCustomColor / setCustomLogo /
// showImportError. The "Keep this UI scale?" prompt is the shared confirm
// bar (the Display canvas raises it); the v1.2.0 card and its owner are gone.
// No window, timer or thread of its own.
#include "modules/display_center_logo.hpp"
#include "modules/settings_canvas.hpp"
#ifdef _WIN32
#include "tools/hud_design_owner.hpp"
#include <exception>
#include <filesystem>

namespace endfield::native { class LayerImageSource; class WatchAppearanceTemplates; }
namespace endfield::tools {
struct SettingsPreviewOptions {
    ehud::data::Settings initial = ehud::data::Settings::defaults();
    modules::SettingsCallbacks callbacks;
    core::Language language{core::Language::english};
    // The canvases' host hooks: displays, the panels, the Windows-only rows
    // and the About information. icons, moduleGlyphs and logoArtwork are
    // filled here from the resources below.
    modules::SettingsCanvasHooks hooks;
    DesignOwnerOptions owner;                      // the host's design owner options ("settings")
    // The packaged common resources root (application-icons/, watch-appearance/):
    // owner.raster.assetRoot is set to it.
    std::filesystem::path resources;
    const native::WatchAppearanceTemplates* appearance{}; // the right-hand list glyph trees
    // Center logo sources: the presets folder (resources/center-logos), the
    // ENDFIELD INDUSTRIES artwork (Resources/EndfieldIndustriesSource.png) and
    // the managed store (<data>/CenterLogo).
    std::filesystem::path logoPresets, endfieldWordmark, logoStore;
    // The shared utility executor (decodes): false when full (retried on the next request).
    std::function<bool(std::function<void()> work, std::function<void(std::exception_ptr)> completion)> submit;
};
class SettingsPreview final {
public:
    SettingsPreview(native::LayerRasterizer&, modules::design::Context&, SettingsPreviewOptions);
    ~SettingsPreview();
    SettingsPreview(const SettingsPreview&) = delete;
    SettingsPreview& operator=(const SettingsPreview&) = delete;
    // The ModuleOwner to register (once): it presents system, display, hotkeys
    // and about. This object outlives it (the canvases read the controller).
    std::unique_ptr<DesignModuleOwner> takeOwner();
    DesignOwnerCore& core() noexcept;
    modules::SettingsController& controller() noexcept;
    modules::SettingsCanvas& canvas(core::Module) const;
    // HUDSettingsInteraction results and the Power page's battery link.
    void setCustomColor(std::array<double, 3>, double time);
    void setCustomLogo(std::string revision, double time);
    void showImportError(std::string, double time);
    void showBatterySettings(double time);
    // Host hooks changed (e.g. the macImport area was constructed after Settings).
    void refreshViews(double time);
    std::optional<core::Module> inputModule() const noexcept;
    // Decoded logo artworks held (they are released when the HUD closed).
    std::size_t logoArtworks() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
#endif
