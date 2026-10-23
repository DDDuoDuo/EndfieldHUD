#pragma once
// The shared module header: the port of Sources/HUDDesignHeader.swift
// (HUDModuleHeader). LIB-22, LIB-23, LIB-98 (no "// " section headings any
// more: the breadcrumb carries "// HUD / ..."). 590 x 81 including the margin.
// Created lazily with a canvas's first page (most canvases of an opening are
// never shown), owned by the canvas, re-configured by every buildPage.
#include "modules/hud_design_controls.hpp"

namespace endfield::modules::design {
class ModuleHeader final {
public:
    explicit ModuleHeader(Environment&, std::string_view idPrefix);
    ~ModuleHeader();
    Layer& layer() const noexcept { return *layer_; }
    // Breadcrumb "// HUD", "/", crumbs... (uppercased by the style; the last
    // crumb in crumbCurrent, others steel; each line box at y 1.75, h 10.5,
    // w text + 1, 6 px apart); the module code right-aligned 6 px left of the
    // light hatch tail 34 x 7 at (556, 3.5); back button (IconButton back
    // 30 x 30 at (0, 20), "Back"/返回) moving the title to x 40; title
    // (moduleTitle) centred on y 15 of the 30 px row at y 20 and the subtitle
    // 12 px right on the title's baseline (ModuleTitle::lines); side actions
    // right-aligned from 590, 8 apart, centred on y 35; the rule at y 60
    // (line foreground(.22) at y 4; lead 56 x 3 in palette yellow - the fill,
    // not the ink; tick ladder 90 x 9 pitch 9 at x 454 foreground(.35); the
    // counter plate #151515 at x 590 - w - 7, y -2, (w + 7) x 12 in both
    // themes; counter ruleCounter right edge 589).
    void configure(std::vector<std::string> crumbs, std::string code, const ModuleTitle&, std::string counter,
        bool showsBack, std::vector<Control*> actions = {});
    void setCounter(std::string);               // in place (TODO count, volume, pins, export %)
    void applyPalette();                        // recolour after a theme / accent change
    Control* back() const noexcept { return back_; }
    std::vector<Control*> controls() const;     // back + actions
    // Title group wipe from the left .45 E with a fade from 0; the rule grows
    // from the left .42 E after .05.
    void playEntrance(const motion::Clock&);
    // Test hooks (the Mac's ...ForTesting).
    std::pair<std::string, std::string> titleTexts() const;
    std::string counterText() const;
    std::vector<std::string> crumbTexts() const;
private:
    LayerArena arena_; ControlSet controls_; Builder builder_;
    Layer *layer_, *crumbs_, *code_, *tail_, *titleGroup_, *title_, *subtitle_, *actions_, *rule_, *counter_;
    Layer *ruleLine_, *lead_, *ladder_, *counterPlate_;
    std::vector<Layer*> crumbPool_; std::size_t crumbCount_{};     // crumb text layers reused across configures
    Control* back_{}; IconButton* backButton_{}; std::vector<Control*> actionControls_;
};
} // namespace endfield::modules::design
