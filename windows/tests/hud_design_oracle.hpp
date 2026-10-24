#pragma once
// Node-by-node comparison of design-library layer trees against the Mac
// oracle (tools/*_reference.swift through tools/module_reference_layers.swift):
// both trees are flattened into what they DRAW, in host coordinates (fills,
// borders, shapes, gradients, cached artwork, icons and text lines with their
// effective opacity); every visible Mac primitive needs a Windows twin and
// vice versa, and controls compare 1:1 (label, hover / press / enabled, model
// rect, accessibility items). The tree SHAPES differ on purpose (tinted white
// surfaces, a child ring above a fill where CA draws a layer border on top,
// cross-faded copies), so the comparison is on the pixels' description.
// Shared by hud_design_components_tests and the module canvas suites
// (clipboard, event log, volume); widths come from the oracle's recorded
// CoreText measurements (RecordedMeasurer), a deterministic .55 em model
// stands in for anything not recorded.
#include "core/data/json.hpp"
#include "modules/hud_design_control.hpp"
#include "modules/hud_design_tokens.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <initializer_list>
#include <optional>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace endfield::tests::design_oracle {
namespace d = endfield::modules::design;
using Json = ehud::data::Json;
using d::Channel; using d::Color; using d::Point; using d::Rect;
inline const d::CharacterRules& characters() { static const auto rules = d::portableCharacterRules(); return rules; }
inline bool near(double a, double b, double e = 1e-6) { return std::abs(a - b) <= e; }
inline bool near(Rect a, Rect b, double e = 1e-6) { return near(a.x, b.x, e) && near(a.y, b.y, e) && near(a.width, b.width, e) && near(a.height, b.height, e); }
inline bool near(Color a, Color b, double e = 1e-6) { return near(a.r, b.r, e) && near(a.g, b.g, e) && near(a.b, b.b, e) && near(a.a, b.a, e); }
inline std::string str(Rect r) { std::ostringstream s; s << "(" << r.x << ", " << r.y << ", " << r.width << ", " << r.height << ")"; return s.str(); }

// A deterministic stand-in for the native measurer: .55 em per character plus
// the tracking after each one, rounded up to .5 px like HUDText.width.
struct FakeMeasurer final : d::TextMeasurer {
    double width(std::span<const d::TextRun> runs, d::TextRegion) override {
        double w = 0;
        for (const auto& r : runs) { const double n = double(characters().count(r.text)); w += n * r.style.size * (.55 + r.style.tracking); }
        return std::ceil(w * 2) / 2;
    }
    d::LineMetrics metrics(const d::TextStyle& s, d::TextRegion, double) override { return {s.size * s.lineHeight, s.size * .8, s.size * .2}; }
};
inline Color multiply(Color a, std::optional<Color> t) { if (!t) return a; return {a.r * t->r, a.g * t->g, a.b * t->b, a.a * t->a}; }
struct RecordedMeasurer final : d::TextMeasurer {
    std::map<std::string, double> widths; std::map<std::string, d::LineMetrics> metric; FakeMeasurer fallback; std::vector<std::string> misses;
    static std::string number(double v) { std::ostringstream s; s.precision(10); s << (std::round(v * 1e6) / 1e6); return s.str(); }
    static std::string key(std::span<const d::TextRun> runs, d::TextRegion region) {
        std::string s(d::regionTag(region));
        for (const auto& r : runs) if (!r.text.empty()) s += '|' + r.text + '|' + number(r.style.size) + '|' + std::to_string(int(d::resolved(r.style.weight))) + '|' + number(r.style.tracking) + '|' + (r.style.tabular ? "t" : "f");
        return s;
    }
    static std::string metricKey(double size, int weight, double lineHeight, bool tabular, std::string_view language) {
        return number(size) + '|' + std::to_string(weight) + '|' + number(lineHeight) + '|' + (tabular ? "t" : "f") + '|' + std::string(language);
    }
    double width(std::span<const d::TextRun> runs, d::TextRegion region) override {
        const auto k = key(runs, region);
        if (const auto it = widths.find(k); it != widths.end()) return it->second;
        misses.push_back(k);
        return fallback.width(runs, region);
    }
    d::LineMetrics metrics(const d::TextStyle& s, d::TextRegion region, double) override {
        const auto it = metric.find(metricKey(s.size, int(d::resolved(s.weight)), s.lineHeight, s.tabular, d::regionTag(region)));
        if (it != metric.end()) return it->second;
        misses.push_back("metrics " + metricKey(s.size, int(d::resolved(s.weight)), s.lineHeight, s.tabular, d::regionTag(region)));
        return fallback.metrics(s, region, 1);
    }
};
// The fixture holds more than the shared parser's one million nodes in one
// document: split it at the top level and parse each section (and each row of
// the large arrays) on its own. Strings are skipped with their escapes.
inline std::size_t skipValue(std::string_view t, std::size_t i) {
    int depth = 0;
    for (; i < t.size(); ++i) {
        const char c = t[i];
        if (c == '"') { for (++i; i < t.size() && t[i] != '"'; ++i) if (t[i] == '\\') ++i; if (depth == 0) return i + 1; continue; }
        if (c == '{' || c == '[') ++depth;
        else if (c == '}' || c == ']') { if (--depth == 0) return i + 1; if (depth < 0) return i; }
        else if (depth == 0 && (c == ',')) return i;
    }
    return i;
}
inline std::vector<std::string_view> items(std::string_view array) {
    std::vector<std::string_view> out;
    std::size_t i = array.find('[') + 1;
    while (i < array.size()) {
        while (i < array.size() && (std::isspace(static_cast<unsigned char>(array[i])) || array[i] == ',')) ++i;
        if (i >= array.size() || array[i] == ']') break;
        const std::size_t end = skipValue(array, i);
        out.push_back(array.substr(i, end - i)); i = end;
    }
    return out;
}
// `rowSections`: top-level arrays parsed row by row (the large ones);
// `sections`: top-level values parsed whole. Other keys are skipped.
inline std::optional<Json> loadOracle(const char* path, std::initializer_list<std::string_view> rowSections = {"components"},
    std::initializer_list<std::string_view> sections = {"text", "layerDefaults"}) {
    if (!path || !std::filesystem::exists(path)) return std::nullopt;
    std::ifstream in(path, std::ios::binary); std::ostringstream s; s << in.rdbuf();
    const std::string text = s.str();
    Json::Object root;
    std::size_t i = text.find('{') + 1;
    while (i < text.size()) {
        const std::size_t keyStart = text.find('"', i);
        if (keyStart == std::string::npos) break;
        const std::size_t keyEnd = text.find('"', keyStart + 1);
        const std::string key = text.substr(keyStart + 1, keyEnd - keyStart - 1);
        std::size_t value = text.find(':', keyEnd) + 1;
        while (std::isspace(static_cast<unsigned char>(text[value]))) ++value;
        const std::size_t end = skipValue(text, value);
        const std::string_view raw(text.data() + value, end - value);
        const auto parse = [&](std::string_view part) {
            try { return Json::parse(part, 64u * 1024 * 1024); }
            catch (const std::exception&) { throw std::runtime_error("Oracle section " + key + " does not parse near: " + std::string(part.substr(0, 160))); }
        };
        const auto listed = [&](std::initializer_list<std::string_view> names) { return std::find(names.begin(), names.end(), key) != names.end(); };
        if (listed(rowSections)) { Json::Array rows; for (const auto row : items(raw)) rows.push_back(parse(row)); root[key] = std::move(rows); }
        else if (listed(sections)) root[key] = parse(raw);
        i = end + 1;
    }
    return Json(std::move(root));
}
inline Rect rectOf(const Json& a) { if (!a.isArray()) return {}; const auto& v = a.array(); return {v[0].number(), v[1].number(), v[2].number(), v[3].number()}; }
inline std::optional<Color> rgbaOf(const Json& a) {
    if (!a.isArray()) return std::nullopt;
    const auto& v = a.array();
    return Color{v[0].number(), v[1].number(), v[2].number(), v[3].number()};
}
using d::Affine;
inline Rect bound(const Affine& m, Rect r) {
    const Point c[4]{m.apply({r.x, r.y}), m.apply({r.x + r.width, r.y}), m.apply({r.x, r.y + r.height}), m.apply({r.x + r.width, r.y + r.height})};
    double l = c[0].x, rr = c[0].x, t = c[0].y, b = c[0].y;
    for (const auto& p : c) { l = std::min(l, p.x); rr = std::max(rr, p.x); t = std::min(t, p.y); b = std::max(b, p.y); }
    return {l, t, rr - l, b - t};
}
inline Rect pathBounds(const Json& path) {
    double l = 1e300, r = -1e300, t = 1e300, b = -1e300;
    for (const auto& op : path.array()) for (const auto& p : op["points"].array()) {
        const double x = p.array()[0].number(), y = p.array()[1].number();
        l = std::min(l, x); r = std::max(r, x); t = std::min(t, y); b = std::max(b, y);
    }
    return l > r ? Rect{} : Rect{l, t, r - l, b - t};
}
struct Primitive {
    std::string kind;          // fill, border, shape, gradient, artwork, text
    Rect rect;                 // host coordinates
    std::optional<Color> color;
    std::string text;          // text: runs; border: width
    std::vector<Color> colors; // text runs
    double opacity{1};
    std::string where;
};
inline std::string describe(const Primitive& p) {
    std::ostringstream s;
    s << p.kind << ' ' << str(p.rect) << " a=" << p.opacity;
    if (p.color) s << " c=(" << p.color->r << ',' << p.color->g << ',' << p.color->b << ',' << p.color->a << ')';
    if (!p.text.empty()) s << " '" << p.text << "'";
    s << " @" << p.where;
    return s.str();
}
// The Mac tree (ModuleReferenceLayerEncoder rows, defaults pruned).
inline void macPrimitives(const Json& n, const Affine& parent, double opacity, std::vector<Primitive>& out, const std::string& path) {
    const Rect bounds = rectOf(n["bounds"]), frame = rectOf(n["frame"]);
    Point anchor{.5, .5};
    if (n["anchorPoint"].isArray()) anchor = {n["anchorPoint"].array()[0].number(), n["anchorPoint"].array()[1].number()};
    Affine transform;
    if (n["transform"].isArray()) {
        const auto& m = n["transform"].array();
        transform = {m[0].array()[0].number(), m[0].array()[1].number(), m[1].array()[0].number(), m[1].array()[1].number(), m[3].array()[0].number(), m[3].array()[1].number()};
    }
    Point position{frame.x + anchor.x * frame.width, frame.y + anchor.y * frame.height};
    if (n["position"].isArray()) position = {n["position"].array()[0].number(), n["position"].array()[1].number()};
    const Affine local = Affine::translation(position.x, position.y) * transform * Affine::translation(-anchor.x * bounds.width - bounds.x, -anchor.y * bounds.height - bounds.y);
    const Affine world = parent * local;
    double alpha = opacity * (n.contains("opacity") ? n["opacity"].number() : 1);
    if (n.contains("hidden") && n["hidden"].boolean()) alpha = 0;
    const std::string where = path + "/" + (n["name"].isString() ? n["name"].string() : n["class"].isString() ? n["class"].string() : std::string("layer"));
    const Rect box = bound(world, {bounds.x, bounds.y, bounds.width, bounds.height});
    const std::string radius = n.contains("cornerRadius") && n["cornerRadius"].number() > 0 ? " r" + RecordedMeasurer::number(n["cornerRadius"].number()) : "";
    if (const auto c = rgbaOf(n["backgroundColor"])) out.push_back({"fill", box, c, radius, {}, alpha, where});
    if (n.contains("borderWidth") && n["borderWidth"].number() > 0) out.push_back({"border", box, rgbaOf(n["borderColor"]).value_or(Color{0, 0, 0, 1}), RecordedMeasurer::number(n["borderWidth"].number()) + radius, {}, alpha, where});
    if (n["contents"].isObject() || n["hatch"].isObject()) out.push_back({"artwork", box, std::nullopt, {}, {}, alpha, where});
    if (n["shape"].isObject() && n["shape"]["path"].isArray() && !n["target"].isObject()) {
        const auto fill = rgbaOf(n["shape"]["fillColor"]);
        if (fill) out.push_back({"shape", bound(world, pathBounds(n["shape"]["path"])), fill, {}, {}, alpha, where});
    }
    if (n["gradient"].isObject()) out.push_back({"gradient", box, std::nullopt, {}, {}, alpha, where});
    if (n["icon"].isObject()) { out.push_back({"icon", box, rgbaOf(n["icon"]["color"]), {}, {}, alpha, where + ":" + (n["icon"]["name"].isString() ? n["icon"]["name"].string() : "")}); return; }
    if (n["designText"].isObject()) {
        const auto& t = n["designText"];
        Primitive p{"text", bound(parent, rectOf(t["lineFrame"])), std::nullopt, {}, {}, alpha, where};
        for (const auto& r : t["runs"].array()) { p.text += r["text"].string(); p.colors.push_back(*rgbaOf(r["style"]["color"])); }
        p.text += " [" + t["alignment"].string() + " " + std::to_string(int(t["maximumLines"].number())) + "]";
        if (!p.text.starts_with(" [")) out.push_back(std::move(p));
    }
    if (n["children"].isArray()) for (const auto& c : n["children"].array()) macPrimitives(c, world, alpha, out, where);
}
// A Windows content node subtree (anchor (0, 0) frame nodes, hud_design_nodes).
inline void nodePrimitives(const Json& n, const Affine& parent, double alpha, std::optional<Color> tint, std::vector<Primitive>& out, const std::string& where, bool top) {
    const Rect b = rectOf(n["bounds"]);
    const auto& p = n["position"].array();
    const Affine world = parent * Affine::translation(p[0].number(), p[1].number());
    const Rect box = bound(world, {0, 0, b.width, b.height});
    const auto colour = [&](const Json& c) { return c.isObject() ? std::optional(multiply(*rgbaOf(c["sRGB"]), tint)) : std::nullopt; };
    // The Mac's cached HUDArtwork rasters (plates, the micro tag, hatches) are
    // composite nodes here: one artwork primitive, their own innards skipped.
    const auto ends = [](const Json& c, std::string_view suffix) { return c["id"].isString() && std::string_view(c["id"].string()).ends_with(suffix); };
    // An iconNode (HUDIconLayer): its part shapes are one icon in the tint.
    if (top && n["children"].isArray() && !n["children"].array().empty() && std::all_of(n["children"].array().begin(), n["children"].array().end(), [&](const Json& c) {
            const std::string prefix = n["id"].string() + ".";
            const std::string id = c["id"].isString() ? c["id"].string() : std::string();
            return c["shape"].isObject() && id.starts_with(prefix) && id.size() > prefix.size()
                && std::all_of(id.begin() + std::ptrdiff_t(prefix.size()), id.end(), [](char ch) { return ch >= '0' && ch <= '9'; }); })) {
        const auto& part = n["children"].array()[0]["shape"];
        out.push_back({"icon", box, colour(part["fillColor"].isObject() ? part["fillColor"] : part["strokeColor"]), {}, {}, alpha, where});
        return;
    }
    bool microTag = false;
    if (n["children"].isArray()) for (const auto& c : n["children"].array()) microTag = microTag || ends(c, ".hatch");
    if (top && (n["mask"].isObject() || n["masksToBounds"].isBool() || microTag || n["contents"].isObject() || n["requiredSourceImage"].isString())) {
        out.push_back({"artwork", box, std::nullopt, {}, {}, alpha, where});
        if (microTag) return;
        if (n["children"].isArray()) for (const auto& c : n["children"].array())
            if (!ends(c, ".fill") && !ends(c, ".highlight")) nodePrimitives(c, world, alpha, tint, out, where, false);
        return;
    }
    const std::string radius = n.contains("cornerRadius") && n["cornerRadius"].number() > 0 ? " r" + RecordedMeasurer::number(n["cornerRadius"].number()) : "";
    if (const auto c = colour(n["backgroundColor"])) out.push_back({"fill", box, c, radius, {}, alpha, where});
    if (n.contains("borderWidth") && n["borderWidth"].number() > 0) out.push_back({"border", box, colour(n["borderColor"]), RecordedMeasurer::number(n["borderWidth"].number()) + radius, {}, alpha, where});
    if (n["shape"].isObject()) if (const auto c = colour(n["shape"]["fillColor"])) out.push_back({"shape", bound(world, pathBounds(n["shape"]["path"])), c, {}, {}, alpha, where});
    if (n["gradient"].isObject()) out.push_back({"gradient", box, std::nullopt, {}, {}, alpha, where});
    if (n["children"].isArray()) for (const auto& c : n["children"].array()) nodePrimitives(c, world, alpha, tint, out, where, false);
}
// A tint colours the surface its animatable layer places: that layer's own
// content and its static descendants drawn into it (samplePoses' placement).
inline void layerPrimitives(const d::Layer& l, const d::Layer& host, double opacity, std::vector<Primitive>& out, std::optional<Color> inherited = std::nullopt) {
    double alpha = opacity * l.value(Channel::opacity);
    if (l.hidden()) alpha = 0;
    const auto tint = l.animatable() ? l.tint() : inherited;
    if (const auto& node = l.content()) nodePrimitives(*node, d::modelWorld(l, host), alpha, tint, out, l.id(), true);
    if (const auto* t = l.text()) {
        const Affine parent = l.parent() ? d::modelWorld(*l.parent(), host) : Affine{};
        Primitive p{"text", bound(parent, t->lineFrame), std::nullopt, {}, {}, alpha, l.id()};
        for (const auto& r : t->runs) { p.text += r.text; p.colors.push_back(multiply(r.style.color, tint)); }
        p.text += std::string(" [") + (t->alignment == d::TextAlignment::left ? "left" : t->alignment == d::TextAlignment::center ? "center" : "right") + " " + std::to_string(t->maximumLines) + "]";
        if (!p.text.starts_with(" [")) out.push_back(std::move(p));
    }
    for (const auto* c : l.children()) layerPrimitives(*c, host, alpha, out, tint);
}
inline bool same(const Primitive& a, const Primitive& b) {
    const bool kinds = a.kind == b.kind || (a.kind == "artwork" && b.kind == "gradient") || (a.kind == "gradient" && b.kind == "artwork");
    if (!kinds || !near(a.rect, b.rect, 2e-3) || std::abs(a.opacity - b.opacity) > 2e-3 || a.text != b.text) return false;
    if (a.color && b.color && !near(*a.color, *b.color, 1.0 / 400)) return false;
    if (a.colors.size() != b.colors.size()) return false;
    for (std::size_t i = 0; i < a.colors.size(); ++i) if (!near(a.colors[i], b.colors[i], 1.0 / 400)) return false;
    return true;
}
// The oracle's recorded CoreText widths and line metrics (data["text"]).
inline void recordText(RecordedMeasurer& recorded, const Json& text) {
    for (const auto& entry : text["widths"].array()) {
        std::vector<d::TextRun> runs;
        for (const auto& r : entry["runs"].array()) {
            d::TextStyle s; s.size = r["size"].number(); s.weight = d::FontWeight(int(r["weight"].number())); s.tracking = r["tracking"].number(); s.tabular = r["tabular"].boolean();
            runs.push_back({r.contains("displayed") ? r["displayed"].string() : r["text"].string(), s});
        }
        if (const auto region = d::regionFromTag(entry["language"].string())) recorded.widths[RecordedMeasurer::key(runs, *region)] = entry["width"].number();
    }
    for (const auto& m : text["metrics"].array())
        recorded.metric[RecordedMeasurer::metricKey(m["size"].number(), int(m["weight"].number()), m["lineHeight"].number(), m["tabular"].boolean(), m["language"].string())]
            = {m["height"].number(), m["ascent"].number(), m["descent"].number()};
}
// HUDArtwork.microTag measures ENDFIELD inside its raster (not recorded):
// recover it from the first Mac row plate's tag width (label + 8 + 44).
inline bool recordMicroTag(RecordedMeasurer& recorded, const Json& layer) {
    const std::function<std::optional<double>(const Json&)> tagWidth = [&](const Json& n) -> std::optional<double> {
        if (n["name"].isString() && n["name"].string() == "row.tag") return rectOf(n["bounds"]).width - 52;
        if (n["children"].isArray()) for (const auto& c : n["children"].array()) if (const auto w = tagWidth(c)) return w;
        return std::nullopt;
    };
    const auto w = tagWidth(layer);
    if (!w) return false;
    const d::TextRun run{"ENDFIELD", d::type::microTag()};
    recorded.widths[RecordedMeasurer::key(std::span(&run, 1), d::TextRegion::simplifiedChinese)] = *w;
    return true;
}
// Accessibility items against the oracle's rows (id, label, role, value, rect, enabled, on).
inline void compareAccessibility(const std::string& label, const Json& expected, std::span<const d::AccessibilityItem> items,
    std::vector<std::string>& failures) {
    static constexpr std::array<std::string_view, 9> roles{"button", "toggle", "popUp", "menuItem", "tab", "slider", "text", "edit", "group"};
    const auto& rows = expected.array();
    if (rows.size() != items.size()) { failures.push_back(label + ": accessibility count " + std::to_string(items.size()) + " vs " + std::to_string(rows.size())); return; }
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const auto& e = rows[i]; const auto& item = items[i];
        if (e["id"].string() != item.id || e["label"].string() != item.label || e["role"].string() != roles[std::size_t(item.role)]
            || e["enabled"].boolean() != item.enabled || !near(rectOf(e["rect"]), item.rect, 2e-3))
            failures.push_back(label + ": accessibility item " + item.id + " '" + item.label + "' " + str(item.rect) + " vs " + e["id"].string() + " '" + e["label"].string() + "' " + str(rectOf(e["rect"])));
        if (e.contains("on") && e["on"].boolean() != item.on) failures.push_back(label + ": accessibility state " + item.id);
        if (e.contains("value") && e["value"].isString() && e["value"].string() != item.value) failures.push_back(label + ": accessibility value " + item.id + " '" + item.value + "' vs '" + e["value"].string() + "'");
        if (e.contains("number") && !near(e["number"].number(), item.number)) failures.push_back(label + ": accessibility number " + item.id);
    }
}
// One tree and its controls against an oracle row ({"layer": ..., "controls": [...]}).
inline void compare(const std::string& label, const Json& expected, const d::Layer& layer, const d::Layer& host,
    std::span<d::Control* const> controls, std::vector<std::string>& failures) {
    std::vector<Primitive> mac, ours;
    macPrimitives(expected["layer"], Affine{}, 1, mac, "");
    layerPrimitives(layer, host, 1, ours);
    // Invisible primitives draw nothing. The sheen band is a mid-flight
    // pass: the Mac's model rests at 0 under a running animation, ours at
    // the end of the pass (hidden by the plate's alpha); motion contracts cover it.
    // The custom swatch's conic wheel is 90 wedges here (no conic gradient in
    // the rasterizer) and the oracle's grid tiles have no image: both are
    // checked by geometry elsewhere, not as primitives.
    const auto drop = [](const Primitive& p) {
        return p.opacity <= 1e-3 || (p.color && p.color->a <= 1e-3) || p.where.find("sheen.band") != std::string::npos
            || p.where.find("swatch.wheel") != std::string::npos || p.where.find("menu.tile.icon") != std::string::npos;
    };
    std::erase_if(mac, drop); std::erase_if(ours, drop);
    std::vector<bool> used(ours.size());
    for (const auto& m : mac) {
        bool found = false;
        for (std::size_t i = 0; i < ours.size() && !found; ++i) if (!used[i] && same(m, ours[i])) found = used[i] = true;
        if (!found) failures.push_back(label + ": Mac draws " + describe(m));
    }
    for (std::size_t i = 0; i < ours.size(); ++i) if (!used[i]) failures.push_back(label + ": Windows draws " + describe(ours[i]));
    if (!expected["controls"].isArray()) return;
    const auto& theirs = expected["controls"].array();
    if (theirs.size() != controls.size()) { failures.push_back(label + ": control count"); return; }
    for (std::size_t i = 0; i < theirs.size(); ++i) {
        const auto& t = theirs[i]; const auto& c = *controls[i];
        if (t["label"].string() != c.label) failures.push_back(label + ": label of " + c.id() + " '" + c.label + "' vs '" + t["label"].string() + "'");
        if (t["hovered"].boolean() != c.hovered() || t["pressed"].boolean() != c.pressed() || t["enabled"].boolean() != c.enabled())
            failures.push_back(label + ": hover/press/enabled of " + c.id());
        if (!near(rectOf(t["rect"]), c.rect(host), 2e-3)) failures.push_back(label + ": rect of " + c.id() + " " + str(c.rect(host)) + " vs " + str(rectOf(t["rect"])));
        std::vector<d::AccessibilityItem> list; c.accessibilityItems(host, list);
        const auto& items = t["accessibility"].array();
        if (items.size() != list.size()) { failures.push_back(label + ": accessibility count of " + c.id()); continue; }
        for (std::size_t k = 0; k < list.size(); ++k) {
            const auto& e = items[k];
            if (e["label"].string() != list[k].label || !near(rectOf(e["rect"]), list[k].rect, 2e-3) || e["enabled"].boolean() != list[k].enabled)
                failures.push_back(label + ": item " + list[k].id);
            if (e.contains("on") && e["on"].boolean() != list[k].on) failures.push_back(label + ": item state " + list[k].id);
            if (e.contains("value") && e["value"].string() != list[k].value) failures.push_back(label + ": item value " + list[k].id + " '" + list[k].value + "'");
            if (e.contains("number") && !near(e["number"].number(), list[k].number)) failures.push_back(label + ": slider value " + list[k].id);
        }
    }
}
} // namespace endfield::tests::design_oracle
