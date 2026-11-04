// The icon set (Sources/HUDDesignIcons.swift): HUDIcon.parts with the path
// strings exactly as the Swift helpers produce them (rect/circle interpolate
// CGFloat, so "8" reads "8.0"), HUDSVGPath.parse, and HUDIconLayer as shape
// nodes. Arcs become cubics the way CGPath.addRelativeArc does: a line to the
// arc start, then quarter turns from the start angle and the remainder.
#include "modules/hud_design_icons.hpp"
#include <cmath>
#include <map>
#include <mutex>
#include <numbers>
#include <stdexcept>

// Swift never fuses multiply-adds; neither does this port, so its numbers
// stay bit-equal with the Mac oracle's.
#if defined(__clang__)
#pragma clang fp contract(off)
#elif defined(_MSC_VER)
#pragma float_control(precise, on, push)
#pragma fp_contract(off)
#elif defined(__GNUC__)
#pragma GCC push_options
#pragma GCC optimize("fp-contract=off")
#endif

namespace endfield::modules::design {
namespace {
constexpr IconPart s(std::string_view d, double width = 2.6, bool square = false) { return {d, false, width, square, std::nullopt, {}}; }
constexpr IconPart f(std::string_view d) { return {d, true, 2.6, false, std::nullopt, {}}; }
// HUDIcon.parts (:28-79).
constexpr IconPart pCheck[]{s("M4 12.5l5 5L20 6.5", 3, true)};
constexpr IconPart pX[]{s("M5.5 5.5l13 13M18.5 5.5l-13 13", 3, true)};
constexpr IconPart pPower[]{s("M12 3v8", 3, true), s("M6.6 6.8a8 8 0 1 0 10.8 0", 3)};
constexpr IconPart pPlus[]{s("M12 4v16M4 12h16", 3)};
constexpr IconPart pMinus[]{s("M4 12h16", 3)};
constexpr IconPart pCopy[]{s("M8.0 8.0h12.0v12.0h-12.0z"), s("M4 16V4h12")};
constexpr IconPart pText[]{s("M4 5h16M12 5v15M8 20h8", 3)};
constexpr IconPart pLink[]{s("M10 14l4-4M8.5 11.5l-2 2a3.5 3.5 0 0 0 5 5l2-2M15.5 12.5l2-2a3.5 3.5 0 0 0-5-5l-2 2")};
constexpr IconPart pLink2[]{s("M9 15l6-6M7 12l-2 2a3.5 3.5 0 0 0 5 5l2-2M17 12l2-2a3.5 3.5 0 0 0-5-5l-2 2")};
constexpr IconPart pImage[]{s("M3.0 4.0h18.0v16.0h-18.0z"), f("M5 18l5-6 4 4 2-2 3 4z"), f("M14.0 7.0h3.0v3.0h-3.0z")};
constexpr IconPart pFile[]{s("M5 3h9l5 5v13H5zM14 3v5h5")};
constexpr IconPart pRefresh[]{s("M19 12a7 7 0 1 1-2-4.9", 2.8), f("M19 3v5h-5z")};
constexpr IconPart pReset[]{s("M5 12a7 7 0 1 0 2-4.9", 2.8), f("M5 3v5h5z")};
constexpr IconPart pRotate[]{s("M20 12a8 8 0 1 1-3-6.2"), f("M20 3v6h-6z")};
constexpr IconPart pPlay[]{f("M7 4l13 8-13 8z")};
constexpr IconPart pPause[]{f("M6 4h4v16H6zM14 4h4v16h-4z")};
constexpr IconPart pPrev[]{f("M6 4h3v16H6zM20 4L10 12l10 8z")};
constexpr IconPart pNext[]{f("M15 4h3v16h-3zM4 4l10 8-10 8z")};
constexpr IconPart pVol[]{f("M3 9h4l6-5v16l-6-5H3z"), s("M16 8.5a5 5 0 0 1 0 7M18.5 6a8.5 8.5 0 0 1 0 12", 2.4)};
constexpr IconPart pMute[]{f("M3 9h4l6-5v16l-6-5H3z"), s("M16 9l5 6M21 9l-5 6", 2.4)};
constexpr IconPart pTrash[]{s("M4 6h16M9 6V3h6v3M6 6l1 15h10l1-15")};
constexpr IconPart pFolder[]{s("M3 5h7l2 2h9v13H3z")};
constexpr IconPart pOpen[]{s("M3 6h7l2 2h9v11H3z"), s("M12 11v6M9 14l3-3 3 3", 2.4)};
constexpr IconPart pExt[]{s("M14 4h6v6M20 4l-9 9M18 14v6H4V6h6")};
constexpr IconPart pExport[]{s("M12 15V3M7 8l5-5 5 5M4 14v7h16v-7")};
constexpr IconPart pTodo[]{s("M3.0 3.0h18.0v18.0h-18.0zM7 12l3 3 7-7")};
constexpr IconPart pPen[]{s("M4 20l1-5L16 4l4 4L9 19z")};
constexpr IconPart pEraser[]{s("M3 17l9-11 8 7-7 8H6zM8 11l7 6")};
constexpr IconPart pTimer[]{s("M4.0 13.0a8.0 8.0 0 1 0 16.0 0a8.0 8.0 0 1 0 -16.0 0zM12 9v4l3 2M9 2h6")};
constexpr IconPart pWatch[]{s("M4.0 13.0a8.0 8.0 0 1 0 16.0 0a8.0 8.0 0 1 0 -16.0 0zM12 13V8")};
constexpr IconPart pDevices[]{s("M2.0 5.0h14.0v10.0h-14.0zM5 19h8", 2.4), f("M18.0 8.0h4.0v11.0h-4.0z")};
constexpr IconPart pApps[]{f("M3.0 3.0h7.0v7.0h-7.0zM14.0 3.0h7.0v7.0h-7.0zM3.0 14.0h7.0v7.0h-7.0z"), s("M14.0 14.0h7.0v7.0h-7.0z", 2.4)};
constexpr IconPart pBolt[]{f("M13 2L4 14h7l-1 8 9-12h-7z")};
constexpr IconPart pBack[]{s("M15 4l-8 8 8 8", 3.2, true)};
constexpr IconPart pLeft[]{s("M15 4l-8 8 8 8", 3, true)};
constexpr IconPart pRight[]{s("M9 4l8 8-8 8", 3, true)};
constexpr IconPart pMirror[]{{"M12 3v18", false, 2, false, std::nullopt, {2, 2}}, f("M10 6L3 18h7zM14 6l7 12h-7z")};
constexpr IconPart pLyric[]{s("M4 6h12M4 11h12M4 16h7M19 9v9"), f("M14.4 18.0a2.6 2.6 0 1 0 5.2 0a2.6 2.6 0 1 0 -5.2 0z")};
constexpr IconPart pBookmark[]{s("M6 3h12v18l-6-5-6 5z")};
constexpr IconPart pGear[]{s("M8.6 12.0a3.4 3.4 0 1 0 6.8 0a3.4 3.4 0 1 0 -6.8 0zM12 2v4M12 18v4M2 12h4M18 12h4M4.9 4.9l2.8 2.8M16.3 16.3l2.8 2.8M4.9 19.1l2.8-2.8M16.3 7.7l2.8-2.8")};
constexpr IconPart pEye[]{s("M2 12s4-7 10-7 10 7 10 7-4 7-10 7S2 12 2 12z", 2.4), f("M9.0 12.0a3.0 3.0 0 1 0 6.0 0a3.0 3.0 0 1 0 -6.0 0z")};
constexpr IconPart pEyeoff[]{s("M2 12s4-7 10-7 10 7 10 7-4 7-10 7S2 12 2 12z", 2.4), f("M9.0 12.0a3.0 3.0 0 1 0 6.0 0a3.0 3.0 0 1 0 -6.0 0z"), s("M4 21L20 3")};
constexpr IconPart pUser[]{f("M7.5 8.0a4.5 4.5 0 1 0 9.0 0a4.5 4.5 0 1 0 -9.0 0z"), f("M3 21c1-5 5-7 9-7s8 2 9 7z")};
constexpr IconPart pHelp[]{s("M3.0 12.0a9.0 9.0 0 1 0 18.0 0a9.0 9.0 0 1 0 -18.0 0zM9.5 9.5a2.5 2.5 0 1 1 3.5 2.3c-.7.4-1 .9-1 1.7V14M12 17v1")};
constexpr IconPart pGrid[]{s("M3 3h18v18H3zM3 9h18M3 15h18M9 3v18M15 3v18", 2)};
constexpr IconPart pLock[]{s("M4.0 10.0h16.0v11.0h-16.0zM8 10V7a4 4 0 0 1 8 0v3")};
constexpr IconPart pUnlock[]{s("M4.0 10.0h16.0v11.0h-16.0zM8 10V7a4 4 0 0 1 7.5-2")};
constexpr IconPart pWarn[]{f("M12 2L23 21H1Z"), {"M12 9v6M12 17.5v1", false, 2.6, false, Hex(0x111111), {}}}; // HUDColor.nearBlack
constexpr IconPart pRec[]{f("M6.0 12.0a6.0 6.0 0 1 0 12.0 0a6.0 6.0 0 1 0 -12.0 0z")};
constexpr IconPart pPin[]{s("M7 3h10M9.5 3v6.5L6 14h12l-3.5-4.5V3M12 14v7", 2.4)};
struct Entry { std::string_view name; std::span<const IconPart> parts; };
constexpr std::array<Entry, std::size_t(Icon::count)> icons{{
    {"check", pCheck}, {"x", pX}, {"power", pPower}, {"plus", pPlus}, {"minus", pMinus}, {"copy", pCopy}, {"text", pText},
    {"link", pLink}, {"link2", pLink2}, {"image", pImage}, {"file", pFile}, {"refresh", pRefresh}, {"reset", pReset},
    {"rotate", pRotate}, {"play", pPlay}, {"pause", pPause}, {"prev", pPrev}, {"next", pNext}, {"vol", pVol}, {"mute", pMute},
    {"trash", pTrash}, {"folder", pFolder}, {"open", pOpen}, {"ext", pExt}, {"export", pExport}, {"todo", pTodo}, {"pen", pPen},
    {"eraser", pEraser}, {"timer", pTimer}, {"watch", pWatch}, {"devices", pDevices}, {"apps", pApps}, {"bolt", pBolt},
    {"back", pBack}, {"left", pLeft}, {"right", pRight}, {"mirror", pMirror}, {"lyric", pLyric}, {"bookmark", pBookmark},
    {"gear", pGear}, {"eye", pEye}, {"eyeoff", pEyeoff}, {"user", pUser}, {"help", pHelp}, {"grid", pGrid}, {"lock", pLock},
    {"unlock", pUnlock}, {"warn", pWarn}, {"rec", pRec}, {"pin", pPin}}};

[[noreturn]] void malformed() { throw std::invalid_argument("Malformed SVG path data"); }
// HUDSVGPath.Scanner (:222-263).
struct Scanner {
    std::string_view bytes;
    std::size_t index{};
    void skip() { while (index < bytes.size() && (bytes[index] == ' ' || bytes[index] == ',' || bytes[index] == '\n' || bytes[index] == '\t')) ++index; }
    bool letter(char c) const { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }
    char nextCommand(char previous) {
        skip();
        if (index >= bytes.size()) return 0;
        if (letter(bytes[index])) return bytes[index++];
        return previous == 0 || (previous | 0x20) == 'z' ? 0 : previous;
    }
    bool flag() {
        skip();
        if (index >= bytes.size() || (bytes[index] != '0' && bytes[index] != '1')) malformed();
        return bytes[index++] == '1';
    }
    // Swift's Double(text) of the scanned token. Mantissas of up to 18 digits
    // divided by an exact power of ten round correctly (every icon number).
    double number() {
        skip();
        const auto begin = index;
        bool negative{}, dot{}, digit{};
        std::uint64_t mantissa{};
        int scale{}, digits{}, exponent{};
        if (index < bytes.size() && (bytes[index] == '-' || bytes[index] == '+')) negative = bytes[index++] == '-';
        while (index < bytes.size()) {
            const char c = bytes[index];
            if (c >= '0' && c <= '9') { digit = true; ++index; if (digits < 18) { mantissa = mantissa * 10 + std::uint64_t(c - '0'); ++digits; if (dot) ++scale; } else if (!dot) --scale; }
            else if (c == '.' && !dot) { dot = true; ++index; }
            else if ((c == 'e' || c == 'E') && digit) {
                ++index;
                bool minus{};
                if (index < bytes.size() && (bytes[index] == '-' || bytes[index] == '+')) minus = bytes[index++] == '-';
                int e{};
                while (index < bytes.size() && bytes[index] >= '0' && bytes[index] <= '9') e = std::min(9999, e * 10 + (bytes[index++] - '0'));
                exponent = minus ? -e : e;
            } else break;
        }
        if (!digit) { index = begin; malformed(); }
        const int power = exponent - scale;
        double value = double(mantissa);
        const double ten = std::pow(10.0, std::abs(power));
        value = power < 0 ? value / ten : value * ten;
        return negative ? -value : value;
    }
};
// The point the CGAffineTransform(translationX:y:).rotated(by:).scaledBy(x:y:)
// of HUDSVGPath.addArc maps (r cos a, r sin a) to.
struct ArcFrame {
    double a, b, c, d, tx, ty;
    Point apply(double x, double y) const noexcept { return {a * x + c * y + tx, b * x + d * y + ty}; }
};
// CGPath.addRelativeArc as CoreGraphics builds it (probed on this Mac, macOS
// 15 SDK): a line to the start point; then whole quarter turns, each ending
// on the previous radius turned by exactly 90 degrees with the truncated
// circle constant .5522847498; then the remainder (when above sqrt(DBL_EPSILON)
// rad, else dropped) ending at the start + delta angle, its handles
// 4/3 tan(rest / 4) long. Power's 1.6 pi arc: 3 quarters + rest; a delta of
// 1e-8 rad: no curve; pi/2 exactly: one quarter.
void addRelativeArc(PathBuilder& path, double radius, double start, double delta, const ArcFrame& t) {
    constexpr double quarter = std::numbers::pi / 2, kappa = .5522847498, minimum = 1.4901161193847656e-08;
    const auto at = [&](double x, double y) { return t.apply(radius * x, radius * y); };
    double ux = std::cos(start), uy = std::sin(start);
    path.line(at(ux, uy));
    const double direction = delta < 0 ? -1 : 1;
    double remaining = std::abs(delta);
    for (; remaining >= quarter; remaining -= quarter) {
        const double vx = direction > 0 ? -uy : uy, vy = direction > 0 ? ux : -ux;
        const double k = direction * kappa;
        path.cubic(at(ux - k * uy, uy + k * ux), at(vx + k * vy, vy - k * vx), at(vx, vy));
        ux = vx; uy = vy;
    }
    if (!(remaining > minimum)) return;
    const double end = start + delta, vx = std::cos(end), vy = std::sin(end), k = 4.0 / 3.0 * std::tan(direction * remaining / 4);
    path.cubic(at(ux - k * uy, uy + k * ux), at(vx + k * vy, vy - k * vx), at(vx, vy));
}
// HUDSVGPath.addArc (:194-220), SVG 1.1 F.6.5.
void addArc(PathBuilder& path, Point p0, Point p1, double rx, double ry, double rotation, bool large, bool sweep) {
    rx = std::abs(rx); ry = std::abs(ry);
    if (!(rx > 0 && ry > 0) || p0 == p1) { path.line(p1); return; }
    const double phi = rotation * std::numbers::pi / 180, cosPhi = std::cos(phi), sinPhi = std::sin(phi);
    const double dx = (p0.x - p1.x) / 2, dy = (p0.y - p1.y) / 2;
    const double x1 = cosPhi * dx + sinPhi * dy, y1 = -sinPhi * dx + cosPhi * dy;
    const double lambda = (x1 * x1) / (rx * rx) + (y1 * y1) / (ry * ry);
    if (lambda > 1) { rx *= std::sqrt(lambda); ry *= std::sqrt(lambda); }
    const double numerator = rx * rx * ry * ry - rx * rx * y1 * y1 - ry * ry * x1 * x1;
    const double denominator = rx * rx * y1 * y1 + ry * ry * x1 * x1;
    double factor = std::sqrt(std::max(0.0, numerator) / denominator);
    if (large == sweep) factor = -factor;
    const double cx1 = factor * rx * y1 / ry, cy1 = -factor * ry * x1 / rx;
    const Point center{cosPhi * cx1 - sinPhi * cy1 + (p0.x + p1.x) / 2, sinPhi * cx1 + cosPhi * cy1 + (p0.y + p1.y) / 2};
    const auto angle = [](double ux, double uy, double vx, double vy) {
        const double sign = ux * vy - uy * vx < 0 ? -1 : 1;
        const double dot = (ux * vx + uy * vy) / (std::sqrt(ux * ux + uy * uy) * std::sqrt(vx * vx + vy * vy));
        return sign * std::acos(std::min(1.0, std::max(-1.0, dot)));
    };
    const double theta = angle(1, 0, (x1 - cx1) / rx, (y1 - cy1) / ry);
    double delta = angle((x1 - cx1) / rx, (y1 - cy1) / ry, (-x1 - cx1) / rx, (-y1 - cy1) / ry);
    if (!sweep && delta > 0) delta -= 2 * std::numbers::pi; else if (sweep && delta < 0) delta += 2 * std::numbers::pi;
    const double k = ry / rx;
    addRelativeArc(path, rx, theta, delta, {cosPhi, sinPhi, -sinPhi * k, cosPhi * k, center.x, center.y});
}
// HUDSVGPath.parse (:146-191).
PathBuilder parse(std::string_view data) {
    PathBuilder path;
    Scanner scanner{data};
    Point current{}, start{};
    std::optional<Point> lastControl;
    char command = 0;
    bool any{};
    while (const char next = scanner.nextCommand(command)) {
        command = next;
        const bool relative = command >= 'a';
        const Point base = relative ? current : Point{};
        const auto point = [&] { const double x = scanner.number(), y = scanner.number(); return Point{base.x + x, base.y + y}; };
        switch (command | 0x20) {
        case 'm': { const auto p = point(); path.move(p); current = start = p; lastControl.reset(); command = relative ? 'l' : 'L'; break; }
        case 'l': { const auto p = point(); path.line(p); current = p; lastControl.reset(); break; }
        case 'h': { const double x = scanner.number(); current = {(relative ? current.x : 0) + x, current.y}; path.line(current); lastControl.reset(); break; }
        case 'v': { const double y = scanner.number(); current = {current.x, (relative ? current.y : 0) + y}; path.line(current); lastControl.reset(); break; }
        case 'c': { const auto c1 = point(), c2 = point(), p = point(); path.cubic(c1, c2, p); current = p; lastControl = c2; break; }
        case 's': {
            const auto c2 = point(), p = point();
            const Point c1 = lastControl ? Point{2 * current.x - lastControl->x, 2 * current.y - lastControl->y} : current;
            path.cubic(c1, c2, p); current = p; lastControl = c2; break;
        }
        case 'a': {
            const double rx = scanner.number(), ry = scanner.number(), rotation = scanner.number();
            const bool large = scanner.flag(), sweep = scanner.flag();
            const auto p = point();
            addArc(path, current, p, rx, ry, rotation, large, sweep); current = p; lastControl.reset(); break;
        }
        case 'z': path.close(); current = start; lastControl.reset(); break;
        default: malformed();
        }
        any = true;
    }
    scanner.skip();
    if (!any || scanner.index != data.size()) malformed();
    return path;
}
}

std::string_view iconName(Icon i) noexcept { return i < Icon::count ? icons[std::size_t(i)].name : std::string_view{}; }
std::optional<Icon> iconNamed(std::string_view name) noexcept {
    for (std::size_t i = 0; i < icons.size(); ++i) if (icons[i].name == name) return Icon(i);
    return std::nullopt;
}
std::span<const IconPart> iconParts(Icon i) noexcept { return i < Icon::count ? icons[std::size_t(i)].parts : std::span<const IconPart>{}; }

// HUDIcon.path (:82-92): parsed once per string.
PathBuilder parseSvgPath(std::string_view d) {
    static std::mutex lock;
    static std::map<std::string, PathBuilder, std::less<>> cache;
    {
        const std::lock_guard guard(lock);
        if (const auto it = cache.find(d); it != cache.end()) return it->second;
    }
    auto path = parse(d);
    const std::lock_guard guard(lock);
    return cache.emplace(std::string(d), std::move(path)).first->second;
}

// HUDIconLayer.set / applyColor (:112-140).
Json iconNode(std::string id, Icon icon, double size, Color tint, Point center) {
    if (!(size > 0) || icon >= Icon::count) throw std::invalid_argument("Icon needs a known glyph and a positive size");
    const double k = size / 24;
    auto node = frameNode(id, {center.x - size / 2, center.y - size / 2, size, size});
    Json::Array parts;
    std::size_t index{};
    for (const auto& part : iconParts(icon)) {
        const auto path = parseSvgPath(part.svg).transformed(k, 0, 0, k, 0, 0);
        const Color c = part.color ? rgb(*part.color) : tint;
        const Rect frame{0, 0, size, size};
        auto partID = id + "." + std::to_string(index++);
        if (part.fill) { parts.push_back(shape(std::move(partID), frame, path, c)); continue; }
        StrokeStyle style{part.strokeWidth * k, 10, part.squareCap ? "square" : "butt", "miter", {}};
        if (part.dash[0] > 0 || part.dash[1] > 0) style.dash = {part.dash[0] * k, part.dash[1] * k};
        parts.push_back(shape(std::move(partID), frame, path, std::nullopt, c, std::move(style)));
    }
    node["children"] = std::move(parts);
    return node;
}
} // namespace endfield::modules::design
#if defined(_MSC_VER) && !defined(__clang__)
#pragma float_control(pop)
#elif defined(__GNUC__) && !defined(__clang__)
#pragma GCC pop_options
#endif
