#pragma once
// The redesign's icon set: the port of Sources/HUDDesignIcons.swift (HUDIcon,
// HUDIconLayer, HUDSVGPath). LIB-48, LIB-49. 50 glyphs on a 24 x 24 grid as
// SVG path data; each part is a stroke (width in grid units, default 2.6;
// `square` = square cap; miter joins, butt caps otherwise) or a fill. `mirror`
// has a 2/2 dashed centre line; `warn` has a fixed #111 exclamation over a
// tinted triangle; `pin` (round 2) is the Clipboard pin. The exact path
// strings of HUDDesignIcons.swift:29-78 are the source of truth and are
// copied verbatim into hud_design_icons.cpp (the contract test compares them
// with the Mac file text).
//
// Icons are drawn as shape nodes (the rasterizer has stroke/fill paths), so
// they share their surface's raster: no per-icon image, no per-frame work.
// Swapping an icon (HUDIconButton.setIcon, Volume mute) changes the node and
// therefore only the surface that holds it.
#include "modules/hud_design_nodes.hpp"
#include <span>

namespace endfield::modules::design {
enum class Icon : std::uint8_t {
    check, x, power, plus, minus, copy, text, link, link2, image, file, refresh, reset, rotate,
    play, pause, prev, next, vol, mute, trash, folder, open, ext, export_, todo, pen, eraser,
    timer, watch, devices, apps, bolt, back, left, right, mirror, lyric, bookmark, gear,
    eye, eyeoff, user, help, grid, lock, unlock, warn, rec, pin, count
};
std::string_view iconName(Icon) noexcept;            // the Mac raw value ("export" for export_)
std::optional<Icon> iconNamed(std::string_view) noexcept;
struct IconPart {
    std::string_view svg;
    bool fill{};                 // false: stroke
    double strokeWidth{2.6};     // grid units
    bool squareCap{};
    std::optional<Hex> color;    // fixed colour (warn's #111 mark); nullopt follows the tint
    std::array<double, 2> dash{};// {0,0}: solid
};
std::span<const IconPart> iconParts(Icon) noexcept;

// HUDSVGPath.parse (:145-264): M L H V C S A Z, absolute and relative,
// implicit repeats, compact numbers and flags; arcs through the SVG 1.1 F.6.5
// endpoint -> centre parameterisation, emitted as CGPath.addRelativeArc does
// (a line to the arc start, quarter turns with .5522847498, then the rest
// unless below sqrt(DBL_EPSILON) rad): every icon matches CoreGraphics' points
// within 1e-12. Parsed paths are cached by string (content events only).
// Malformed data throws std::invalid_argument where Swift would return the
// partial path (the strings are compile-time constants).
PathBuilder parseSvgPath(std::string_view d);

// HUDIconLayer (:96-141): the 24-unit paths scaled to `size`, stroke width x
// size / 24, tinted by `tint` (fills and strokes). The node is `size` x
// `size`, centred on `center` (the Mac sets position).
Json iconNode(std::string id, Icon, double size, Color tint, Point center);
} // namespace endfield::modules::design
