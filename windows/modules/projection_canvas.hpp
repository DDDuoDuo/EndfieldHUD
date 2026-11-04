#pragma once
// The Projection screen of the redesign (spec section 3.14): the port of
// Sources/ProjectionCanvas.swift (Mac 1.3.0). A preview of the pen on the
// workspace background, the pen toolbar, the pen width and background rows,
// and the launcher of the projection workspace (onOpen, wired by the host to
// the HUD / workspace handoff). Every value is the workspace's own preference
// (modules/projection_settings.hpp). Selecting Projection shows this page;
// the workspace opens only from Open projection or Insert media (v1.2.0
// opened it straight from the navigation, DOCS-600). The custom pen colour
// uses the shared colour pop-over (modules/notes_format_bar.hpp
// notesColorPicker: its wheel raster is the owner's notesColorWheelImageKey).
#include "modules/hud_design_page.hpp"
#include "modules/projection_settings.hpp"

namespace endfield::modules {
class ProjectionCanvas final : public design::Canvas {
public:
    static constexpr design::Size previewSize{design::metric::boxWidth, 150};
    ProjectionCanvas(design::Context&, ProjectionPenSettings&, std::function<void()> onOpen);
    ProjectionPenSettings& settings() const noexcept { return *settings_; }
    // The workspace returned or a host event changed the shared values.
    void refresh();
    // Artwork (:224-259): the sample stroke, the dots and the checkerboard.
    static design::PathBuilder samplePath();
    static design::PathBuilder dotPath(design::Size);
    static design::PathBuilder checkerPath(design::Size);

    // --- design::Canvas ---
    void buildPage(design::Page&) override;
    void didActivate() override;
private:
    ProjectionPenSettings* settings_;
    std::function<void()> onOpen_;
    std::vector<std::function<void()>> binders_;
    std::function<void(double)> strokeWidthChanged_;
    std::string counter() const;
    void change(const std::function<void(ProjectionPenSettings::Values&)>&);
    design::Layer& makePreview(design::Page&);
    design::Layer& makeToolbar(design::Page&);
    void openColorPicker(const design::Control& anchor);
    void insertMedia();
    void confirmClear();
};
} // namespace endfield::modules
