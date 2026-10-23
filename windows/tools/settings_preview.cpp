#include "tools/settings_preview.hpp"
#ifdef _WIN32
#include "core/data/file_io.hpp"
#include "native/display_center_logo.hpp"
#include "native/layer_image_source.hpp"
#include "native/watch_appearance.hpp"
#include <algorithm>
#include <cmath>
#include <map>

namespace endfield::tools {
namespace {
namespace m = modules;
namespace d = modules::design;
namespace gpu = native;
using Json = ehud::data::Json;
void need(bool v, const char* s) { if (!v) throw std::invalid_argument(s); }
constexpr std::array<core::Module, 4> presented{core::Module::system, core::Module::display, core::Module::hotkeys, core::Module::about};
// The memory image side limit (LayerImageSource::maximumDimension).
constexpr unsigned logoSide = gpu::LayerImageSource::maximumDimension;
// The right-hand list's glyph trees reference their rasters relative to the
// watch-appearance folder; the owner's asset root is its parent.
Json rooted(Json node) {
    if (std::as_const(node)["contents"].isObject() && std::as_const(node)["contents"]["asset"].isString()) {
        auto contents = std::as_const(node)["contents"];
        contents["asset"] = "watch-appearance/" + std::as_const(contents)["asset"].string();
        node["contents"] = std::move(contents);
    }
    if (std::as_const(node)["children"].isArray()) {
        auto children = std::as_const(node)["children"].array();
        for (auto& child : children) child = rooted(std::move(child));
        node["children"] = std::move(children);
    }
    return node;
}
}

struct SettingsPreview::Impl {
    struct Logo {
        std::optional<m::CenterLogoArtwork> art;
        bool pending{}, failed{};
    };
    struct Raster {
        std::shared_ptr<const gpu::LayerMemoryImage> image; // kept alive while the canvas may name it
        d::Size pixels;
    };
    gpu::LayerRasterizer& raster;
    d::Context& context;
    SettingsPreviewOptions options;
    gpu::LayerImageSource images{gpu::LayerImageSourceLimits{24, 48, 8 * 1024 * 1024}};
    std::unique_ptr<m::SettingsController> controller;
    std::unique_ptr<DesignOwnerCore> owned;
    DesignOwnerCore* core{};
    std::array<m::SettingsCanvas*, 4> canvases{};
    std::map<std::string, Logo, std::less<>> logos;
    std::map<std::string, Raster, std::less<>> rasters;
    std::uint64_t serial{};                              // memory image revisions
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
    Impl(gpu::LayerRasterizer& r, d::Context& c, SettingsPreviewOptions o) : raster(r), context(c), options(std::move(o)) {}
    m::SettingsCanvas* canvas(core::Module module) const {
        for (std::size_t n = 0; n < presented.size(); ++n) if (presented[n] == module) return canvases[n];
        return nullptr;
    }
    // HUDSettingsCanvas.logoArtwork: decoded once on the utility executor, then
    // rasterized per size and render scale (only the current custom revision stays).
    std::optional<m::SettingsLogoImage> logoArtwork(std::string_view logo, std::string_view revision, d::Size fitting) {
        const bool custom = logo == "custom";
        std::optional<std::string> canonical;
        if (custom) { canonical = m::centerLogoRevision(revision); if (!canonical) return std::nullopt; }
        const std::string key = custom ? "custom:" + *canonical : std::string(logo);
        if (custom) {
            std::erase_if(logos, [&](const auto& e) { return e.first.starts_with("custom:") && e.first != key; });
            std::erase_if(rasters, [&](const auto& e) { return e.first.starts_with("custom:") && !e.first.starts_with(key + "|"); });
        }
        auto& entry = logos[key];
        const double scale = context.boxScale();
        const std::string rasterKey = key + "|" + std::to_string(fitting.width) + "x" + std::to_string(fitting.height) + "@" + std::to_string(scale);
        if (const auto found = rasters.find(rasterKey); found != rasters.end())
            return m::SettingsLogoImage{{d::ImageSource::Kind::memory, found->second.image->key(), {}, found->second.image->revision()}, found->second.pixels};
        if (!entry.art) { request(key, logo, canonical.value_or(std::string())); return std::nullopt; }
        const auto bitmap = m::centerLogoChooserImage(*entry.art, fitting.width, fitting.height, scale, logoSide);
        auto image = images.publish("settings.logo." + rasterKey, ++serial, bitmap.width, bitmap.height, bitmap.rgba);
        if (!image) return std::nullopt;
        const d::Size pixels{double(bitmap.width), double(bitmap.height)};
        rasters[rasterKey] = {image, pixels};
        return m::SettingsLogoImage{{d::ImageSource::Kind::memory, image->key(), {}, image->revision()}, pixels};
    }
    void request(const std::string& key, std::string_view logo, std::string revision) {
        auto& entry = logos[key];
        if (entry.pending || entry.failed || !options.submit) return;
        const auto choice = m::centerLogoChoice(logo);
        if (!choice) { entry.failed = true; return; }
        auto result = std::make_shared<std::optional<m::CenterLogoArtwork>>();
        const auto presets = options.logoPresets, wordmark = options.endfieldWordmark, store = options.logoStore;
        const auto which = *choice;
        const bool accepted = options.submit([which, revision, presets, wordmark, store, result] {
            std::optional<m::CenterLogoBitmap> bitmap;
            if (which == m::CenterLogoChoice::custom) bitmap = gpu::decodeCenterLogoRevision(store, revision);
            else if (which == m::CenterLogoChoice::endfield) bitmap = gpu::decodeCenterLogoWordmark(wordmark);
            else bitmap = gpu::decodeCenterLogoPreset(presets, which);
            if (bitmap) *result = m::centerLogoArtwork(*bitmap);
        }, [this, weak = std::weak_ptr<bool>(alive), key, result](std::exception_ptr error) {
            if (weak.expired()) return;
            auto& done = logos[key];
            done.pending = false;
            // The presets stay offered even if their artwork is unreadable (an empty tile).
            if (error || !result->has_value()) { done.failed = true; return; }
            done.art = std::move(**result);
            if (auto* display = canvas(core::Module::display)) display->artworkChanged();
        });
        entry.pending = accepted; // a full queue retries on the next request
    }
};

SettingsPreview::SettingsPreview(gpu::LayerRasterizer& raster, d::Context& context, SettingsPreviewOptions options)
    : impl_(std::make_unique<Impl>(raster, context, std::move(options))) {
    auto& i = *impl_;
    auto& o = i.options;
    // The offered app icons of the pinned roster (HUDApplicationIcon.pickerCases).
    if (!o.resources.empty()) {
        const auto bytes = ehud::data::detail::readFile(o.resources / "application-icons" / "manifest.json", 4 * 1024 * 1024);
        need(bytes.has_value(), "Settings icon roster is missing");
        const auto catalog = Json::parse(*bytes);
        need(catalog["sourceCommit"].string() == "ca04f142185c7de40acd8523bdb563195d90a1d1", "Settings icon source differs");
        for (const auto& icon : catalog["icons"].array()) if (icon["offered"].boolean())
            o.hooks.icons.push_back({icon["id"].string(), icon["title"].string(),
                {d::ImageSource::Kind::asset, "application-icons/" + icon["app"]["file"].string(), icon["app"]["sha256"].string()}});
        o.owner.raster.assetRoot = o.resources;
    }
    // HUDNavigationEntry.glyphLayer's sources: the right list's 32 px glyph of every configurable module.
    if (o.appearance)
        for (const auto module : ehud::data::ModuleLayout::configurable)
            o.hooks.moduleGlyphs.emplace(std::string(module), rooted(o.appearance->icon("module:" + std::string(module), false)));
    o.hooks.logoArtwork = [&i](std::string_view logo, std::string_view revision, d::Size fitting) { return i.logoArtwork(logo, revision, fitting); };
    o.owner.raster.memoryImages = &i.images;
    i.controller = std::make_unique<m::SettingsController>(o.initial, o.callbacks, o.language);
    // Appearance: the Context already took it (the overlay owner goes first);
    // the controller's statuses follow the language, the canvases re-check
    // their structure (a language change re-renders the shown page).
    auto appearance = std::move(o.owner.appearance);
    o.owner.appearance = [&i, appearance](const app::ModuleAppearance& a, double t) {
        if (appearance) appearance(a, t);
        i.controller->setLanguage(a.language);
    };
    auto hostEvent = std::move(o.owner.hostEvent);
    o.owner.hostEvent = [&i, hostEvent](const app::HostEvent& e, double t) {
        const bool used = hostEvent && hostEvent(e, t);
        if (e.kind == app::HostEvent::Kind::displaysChanged) if (auto* system = i.canvas(core::Module::system)) system->displaysChanged();
        // The closed HUD keeps the small chooser rasters, not the decoded artwork.
        if (e.kind == app::HostEvent::Kind::overlayConcealed) for (auto& [key, logo] : i.logos) logo.art.reset();
        return used;
    };
    i.owned = std::make_unique<DesignOwnerCore>(raster, context, o.owner);
    i.core = i.owned.get();
    for (std::size_t n = 0; n < presented.size(); ++n) {
        auto canvas = std::make_unique<m::SettingsCanvas>(presented[n], context, *i.controller, o.hooks);
        i.canvases[n] = canvas.get();
        i.core->add(std::move(canvas));
    }
}
SettingsPreview::~SettingsPreview() { *impl_->alive = false; }
std::unique_ptr<DesignModuleOwner> SettingsPreview::takeOwner() {
    need(impl_->owned != nullptr, "The Settings owner was already taken");
    return std::make_unique<DesignModuleOwner>(std::move(impl_->owned));
}
DesignOwnerCore& SettingsPreview::core() noexcept { return *impl_->core; }
m::SettingsController& SettingsPreview::controller() noexcept { return *impl_->controller; }
m::SettingsCanvas& SettingsPreview::canvas(core::Module module) const {
    auto* c = impl_->canvas(module);
    need(c != nullptr, "Settings presents System, Display, Hotkeys and About");
    return *c;
}
void SettingsPreview::setCustomColor(std::array<double, 3> color, double time) {
    auto& c = canvas(core::Module::display);
    impl_->context.env().now = std::max(impl_->context.env().now, time);
    c.setCustomColor(color);
}
void SettingsPreview::setCustomLogo(std::string revision, double time) {
    need(m::centerLogoRevision(revision).has_value(), "Invalid explicit logo import revision");
    impl_->context.env().now = std::max(impl_->context.env().now, time);
    canvas(core::Module::display).setCustomLogo(std::move(revision));
}
void SettingsPreview::showImportError(std::string error, double time) {
    impl_->context.env().now = std::max(impl_->context.env().now, time);
    canvas(core::Module::display).showImportError(std::move(error));
}
void SettingsPreview::showBatterySettings(double time) {
    impl_->context.env().now = std::max(impl_->context.env().now, time);
    canvas(core::Module::display).showBatterySettings();
}
void SettingsPreview::refreshViews(double time) {
    impl_->context.env().now = std::max(impl_->context.env().now, time);
    auto& system = canvas(core::Module::system);
    if (system.renderedLanguage()) system.rerender(); // the Windows-only rows read their hooks when built
}
std::optional<core::Module> SettingsPreview::inputModule() const noexcept { return impl_->core->inputModule(); }
std::size_t SettingsPreview::logoArtworks() const noexcept {
    return std::size_t(std::count_if(impl_->logos.begin(), impl_->logos.end(), [](const auto& e) { return e.second.art.has_value(); }));
}
}
#endif
