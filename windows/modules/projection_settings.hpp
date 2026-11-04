#pragma once
// ProjectionSettings (Sources/ProjectionSettings.swift, Mac 1.3.0): the pen
// and background preferences shared by the Projection centre page
// (modules/projection_canvas.hpp) and the projection workspace (the session
// ProjectionModel). They persist in the preference record under the Mac key
// "projection.settings.v1" (ehud::data::Settings::projection / setProjection,
// written in full on every change: colour, width, erasing, background).
// Once the host attaches the session model, every change here reaches the
// workspace at once and capture() takes the workspace's own toolbar, wheel
// and right-click changes back (the host calls it on brushChanged /
// backgroundChanged).
//
// The colour default is the theme colour when the shared settings come into
// use (ProjectionSettings.Values() reads HUDColor.yellow once): without a
// saved colour the pen takes the theme colour of the first setThemeColor
// (the Projection page's first build, once the configured accent applies)
// and keeps it; palette() follows the theme live, so a pen saved with an old
// theme's hex shows as custom after a theme change (the Mac's behaviour,
// DOCS-614). Darkness and blur are not here:
// the model still takes them from the configuration when it is created.
#include "core/data/data_store.hpp"
#include "modules/projection_model.hpp"
#include <array>
#include <functional>

namespace endfield::modules {
class ProjectionPenSettings final {
public:
    // The centre page's width slider (widthRange); the store and the model take 1...80.
    static constexpr double minimumWidth = 4, maximumWidth = 48;
    struct Values {
        std::uint32_t color{};      // sRGB 0xRRGGBB
        double width{12};
        bool erasing{}, background{true};
        bool operator==(const Values&) const = default;
    };
    ProjectionPenSettings(const ehud::data::ProjectionSettings& stored, std::uint32_t themeColor,
        std::function<void(const ehud::data::ProjectionSettings&)> persist = {});
    const Values& values() const noexcept { return values_; }
    // The preference record was read (the store loads after the HUD is built):
    // its saved values replace the defaults; nothing is written back.
    void load(const ehud::data::ProjectionSettings& stored);
    // The six presets of the toolbar: the theme colour, FAFAFA, 22A6F2,
    // E5383B, 8CE03C, 111111. Any other colour is a custom one.
    std::array<std::uint32_t, 6> palette() const noexcept { return palette(theme_); }
    static std::array<std::uint32_t, 6> palette(std::uint32_t themeColor) noexcept;
    std::uint32_t themeColor() const noexcept { return theme_; }
    void setThemeColor(std::uint32_t) noexcept;
    // The live session model: it takes the saved values now and follows later
    // changes (attach before every presentation, ProjectionSettings.attach).
    void attach(ProjectionModel*);
    ProjectionModel* model() const noexcept { return model_; }
    // Clamps (colour & 0xFFFFFF, width 1...80, a non-finite width keeps the
    // old one), persists and applies only when the values change.
    void update(const std::function<void(Values&)>&);
    // Reads the workspace's choices back from the attached model.
    void capture();
    // Strokes or media exist in the session drawing (no model yet: false).
    bool hasDrawing() const noexcept;
    // Clears the session drawing (strokes and media references; sources stay).
    bool clearDrawing();
    // The workspace opens its media insert once it is presented (one-shot).
    void requestMediaInsert() noexcept { mediaInsertRequested_ = true; }
    bool takeMediaInsertRequest() noexcept;
    bool mediaInsertRequested() const noexcept { return mediaInsertRequested_; }
    static NotesColor color(std::uint32_t hex) noexcept;      // HUDColor.rgb(hex)
    static std::uint32_t hex(const NotesColor&) noexcept;     // sRGB, clamped, rounded x 255
private:
    Values values_;
    std::uint32_t theme_;
    bool resolved_{};            // the colour is a saved or a first-use theme colour
    std::function<void(const ehud::data::ProjectionSettings&)> persist_;
    ProjectionModel* model_{};
    bool mediaInsertRequested_{};
    void apply(ProjectionModel&) const;
};
} // namespace endfield::modules
