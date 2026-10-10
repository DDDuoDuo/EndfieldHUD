// Source: windows/tools/charge_indicator_reference.sh (unchanged Mac
// ChargeIndicatorView/HUDChargeMetric, frozen Core Animation clock).
#include "modules/charge_indicator.hpp"
#include <cmath>
#include <fstream>
#include <iostream>
#include <cstdlib>
#include <map>
#include <sstream>

using namespace endfield;
using namespace endfield::modules;
using ehud::data::Json;

namespace {
unsigned checks{};
void check(bool value, const std::string& message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
double number(const Json& value) {
    if (value.isString()) {
        const auto s = value.string();
        return s == "nan" ? NAN : s == "inf" ? INFINITY : s == "-inf" ? -INFINITY : std::stod(s);
    }
    return value.number();
}
std::map<std::string, double>* maxima{};
void near(double actual, double expected, double tolerance, const std::string& message) {
    if (maxima) {
        auto key = message.substr(message.find(' ') + 1);
        key = key.substr(key.find(' ') + 1);
        auto& m = (*maxima)[key];
        if (std::abs(actual - expected) > 0.01 && std::getenv("CHARGE_VERBOSE")) std::cout << message << " " << actual << " vs " << expected << "\n";
        m = std::max(m, std::abs(actual - expected));
        ++checks;
        return;
    }
    check(std::abs(actual - expected) <= tolerance, message + " (actual " + std::to_string(actual) + ", source " + std::to_string(expected) + ")");
}
std::optional<unsigned> optionalUnsigned(const Json& value) {
    return value.isNull() ? std::nullopt : std::optional<unsigned>(static_cast<unsigned>(value.number()));
}
BatteryReading battery(const Json& d) {
    BatteryReading r;
    r.percentage = optionalUnsigned(d["percentage"]);
    r.pluggedIn = d["pluggedIn"].boolean(); r.charging = d["charging"].boolean();
    r.fullyCharged = d["fullyCharged"].boolean(); r.present = d["hasBattery"].boolean();
    if (!d["capacity"].isNull())
        r.capacity = BatteryReading::Capacity{static_cast<unsigned>(d["capacity"]["current"].number()),
                                              static_cast<unsigned>(d["capacity"]["maximum"].number()), d["capacity"]["unit"].string()};
    if (!d["health"].isNull()) r.health = d["health"].string();
    return r;
}
std::optional<ChargeTelemetry> telemetry(const Json& d) {
    if (d.isNull()) return std::nullopt;
    ChargeTelemetry t;
    auto real = [&](const char* key) { return d[key].isNull() ? std::nullopt : std::optional<double>(number(d[key])); };
    auto bytes = [&](const char* key) {
        return d[key].isNull() ? std::nullopt : std::optional<std::uint64_t>(static_cast<std::uint64_t>(std::stoull(d[key].isString() ? d[key].string() : std::to_string(d[key].integer()))));
    };
    t.cpuPercent = real("cpuPercent"); t.memoryUsed = bytes("memoryUsed"); t.memoryTotal = bytes("memoryTotal");
    t.upload = real("upload"); t.download = real("download"); t.diskRead = real("diskRead"); t.diskWrite = real("diskWrite");
    return t;
}
ChargeMetric metric(const Json& value) {
    const auto m = chargeMetricFromKey(value.string());
    check(m.has_value(), "Saved metric identifier");
    return *m;
}
ChargeStage stage(const std::string& key) {
    for (auto s : {ChargeStage::hidden, ChargeStage::circle, ChargeStage::supercharge, ChargeStage::compact})
        if (chargeStageKey(s) == key) return s;
    throw std::runtime_error("Unknown source stage " + key);
}
core::Language language(bool chinese) { return chinese ? core::Language::simplifiedChinese : core::Language::english; }
std::vector<double> numbers(const Json& value) {
    std::vector<double> out;
    for (const auto& item : value.array()) out.push_back(number(item));
    return out;
}
void color(const ChargeColor& actual, const Json& source, const std::string& message, double tolerance = 2e-6) {
    const auto expected = numbers(source["sRGB"].isNull() ? source : source["sRGB"]);
    check(expected.size() == 4, message + " has four components");
    for (std::size_t i = 0; i < 4; ++i) near(actual[i], expected[i], tolerance, message);
}
// CGPath(roundedRect:) points for a (w,h,r) rounded rectangle, as emitted.
std::vector<double> roundedRectPoints(double w, double h, double r) {
    constexpr double k = 0.5522847498;
    const double c = r * (1 - k);
    return {w, h / 2, w, h - r, w, h - c, w - c, h, w - r, h, r, h, c, h, 0, h - c, 0, h - r, 0, r, 0, c, c, 0, r, 0,
            w - r, 0, w - c, 0, w, c, w, r};
}

struct Fixture {
    Json root;
    const Json& node(const Json& index) const { return root["nodes"].array().at(static_cast<std::size_t>(index.number())); }
};

void trees(const Fixture& f) {
    unsigned compared{};
    for (const auto& tree : f.root["trees"].array()) {
        const auto reading = battery(tree["battery"]);
        const auto m = metric(tree["metric"]);
        const auto t = telemetry(tree["telemetryValue"]);
        const auto s = stage(tree["stage"].string());
        const bool dark = tree["dark"].boolean(), hovered = tree["hovered"].boolean(), preview = tree["preview"].boolean();
        const auto lang = language(tree["chinese"].boolean());
        const auto hex = tree["accentHex"].string();
        const ChargeColor accent{std::stoi(hex.substr(0, 2), nullptr, 16) / 255.0, std::stoi(hex.substr(2, 2), nullptr, 16) / 255.0,
                                 std::stoi(hex.substr(4, 2), nullptr, 16) / 255.0, 1};
        const ChargeIndicatorAppearance appearance{dark, accent, lang};
        const auto& root = f.node(tree["root"]);
        std::vector<const Json*> layers;
        for (const auto& child : root["children"].array()) layers.push_back(&f.node(child));
        check(layers.size() == 9, "Original canvas paint order: shadow, body, emblem, bolt, four labels, ring");
        const auto label = tree["snapshot"].string() + "/" + std::string(chargeStageKey(s)) + (dark ? "/dark" : "/light") +
                           (tree["chinese"].boolean() ? "/zh" : "/en") + "/" + tree["metric"].string() + "/" + tree["telemetry"].string();
        // Model geometry for the settled stage.
        ChargeIndicatorTimeline timeline;
        timeline.setStage(s, false, 0);
        const auto pose = timeline.sample(0);
        for (std::size_t surface : {0u, 1u}) {
            const auto& layer = *layers[surface];
            const auto bounds = numbers(layer["bounds"]);
            near(pose.bodyWidth, bounds[2], 1e-9, label + " body width");
            near(pose.bodyHeight, bounds[3], 1e-9, label + " body height");
            near(pose.bodyRadius, number(layer["cornerRadius"]), 1e-9, label + " body radius");
            near(pose.bodyScale, number(layer["transform"].array()[0].array()[0]), 1e-6, label + " body scale");
            near(pose.bodyOpacity, number(layer["opacity"]), 1e-6, label + " body opacity");
            const auto position = numbers(layer["position"]);
            near(position[0], 150, 0, label + " body center x");
            near(position[1], 42, 0, label + " body center y");
        }
        const auto rect = numbers(tree["embeddedBodyRect"]);
        const auto bodyRect = pose.bodyRect();
        near(bodyRect.x, rect[0], 1e-4, label + " embedded body x");
        near(bodyRect.width, rect[2], 1e-4, label + " embedded body width");
        near(bodyRect.height, rect[3], 1e-4, label + " embedded body height");
        // Colors (updateColors) and the source shadow parameters.
        const auto content = prepareChargeIndicatorContent(appearance, reading, m, t, preview, [&](std::string_view id, const Json& d) {
            // Report "too wide" exactly while the trial size exceeds the size
            // the Mac measured; this exercises the source loops and bounds.
            const auto& source = id == "charge/5" ? layers[5] : id == "charge/6" ? layers[6] : layers[7];
            const double sourceSize = number((*source)["text"]["runs"].array()[0]["attributes"]["NSFont"]["pointSize"]);
            return number(d["runs"].array()[0]["attributes"]["NSFont"]["pointSize"]) > sourceSize + 1e-9 ? 1e6 : 0.0;
        });
        const auto colors = chargeIndicatorColors(appearance, reading, m, content.reading.progress, s, hovered);
        color(colors.background, (*layers[1])["backgroundColor"], label + " body background");
        color(colors.background, (*layers[0])["backgroundColor"], label + " shadow plate background");
        color(colors.border, (*layers[1])["borderColor"], label + " body border");
        near(colors.borderWidth, number((*layers[1])["borderWidth"]), 0, label + " border width");
        check((*layers[1])["masksToBounds"].boolean() && !(*layers[0])["masksToBounds"].boolean(), label + " body clips ripples");
        near(number((*layers[0])["shadowOpacity"]), 0.2, 1e-7, label + " shadow opacity");
        near(number((*layers[0])["shadowRadius"]), 4, 0, label + " shadow radius");
        check(numbers((*layers[0])["shadowOffset"]) == std::vector<double>{0, 1}, label + " shadow offset");
        color({0, 0, 0, 1}, (*layers[0])["shadowColor"], label + " shadow color");
        // Emblem rounded rectangle and bolt.
        const auto& emblem = *layers[2];
        near(pose.emblemWidth, numbers(emblem["bounds"])[2], 1e-9, label + " emblem width");
        near(pose.emblemHeight, numbers(emblem["bounds"])[3], 1e-9, label + " emblem height");
        near(pose.emblemCenter.x, numbers(emblem["position"])[0], 1e-9, label + " emblem x");
        color(colors.emblem, emblem["shape"]["fillColor"], label + " emblem fill");
        std::vector<double> points;
        for (const auto& element : emblem["shape"]["path"].array())
            for (const auto& point : element["points"].array()) for (const auto& v : point.array()) points.push_back(number(v));
        const auto expected = roundedRectPoints(pose.emblemWidth, pose.emblemHeight, pose.emblemRadius);
        check(points.size() == expected.size(), label + " emblem path topology");
        for (std::size_t i = 0; i < points.size(); ++i) near(points[i], expected[i], 1e-9, label + " emblem path");
        const auto& bolt = *layers[3];
        {
            const Json boltJson = chargeBoltLayer();
            std::vector<double> mine, source;
            for (const Json* path : {&boltJson["shape"]["path"], &bolt["shape"]["path"]})
                for (const auto& e : path->array()) for (const auto& p : e["points"].array()) for (const auto& n : p.array())
                    (path == &bolt["shape"]["path"] ? source : mine).push_back(n.number());
            check(mine.size() == source.size(), label + " bolt topology");
            for (std::size_t i = 0; i < mine.size(); ++i) near(mine[i], source[i], 1e-12, label + " bolt path");
        }
        color(colors.bolt, bolt["shape"]["fillColor"], label + " bolt fill");
        near(pose.boltScale, number(bolt["transform"].array()[0].array()[0]), 1e-6, label + " bolt scale");
        // Labels: exact attributed runs in the source layer encoding.
        for (std::size_t i = 0; i < 4; ++i) {
            const auto& source = *layers[4 + i];
            const auto& mine = content.texts[i];
            near(pose.textPosition[i].x, numbers(source["position"])[0], 1e-9, label + " label x");
            near(pose.textPosition[i].y, numbers(source["position"])[1], 1e-9, label + " label y");
            near(pose.textOpacity[i], number(source["opacity"]), 0, label + " label opacity");
            check(numbers(source["bounds"]) == numbers(mine["bounds"]), label + " label bounds");
            const auto& a = mine["text"];
            const auto& b = source["text"];
            if (a["string"] != b["string"]) std::cerr << label << " string " << a["string"].string() << " vs " << b["string"].string() << '\n';
            check(a["string"] == b["string"] && a["alignment"] == b["alignment"] && a["wrapped"] == b["wrapped"] &&
                  a["truncation"] == b["truncation"], label + " label text");
            check(a["runs"].array().size() == b["runs"].array().size(), label + " label run count");
            for (std::size_t r = 0; r < a["runs"].array().size(); ++r) {
                const auto& x = a["runs"].array()[r];
                const auto& y = b["runs"].array()[r];
                check(numbers(x["utf16Range"]) == numbers(y["utf16Range"]), label + " run range");
                for (auto key : {"postScriptName", "familyName"})
                    check(x["attributes"]["NSFont"][key] == y["attributes"]["NSFont"][key], label + " run font " + key);
                near(number(x["attributes"]["NSFont"]["pointSize"]), number(y["attributes"]["NSFont"]["pointSize"]), 1e-9, label + " run size");
                near(number(x["attributes"]["NSFont"]["symbolicTraits"]), number(y["attributes"]["NSFont"]["symbolicTraits"]), 0, label + " run traits");
                color({number(x["attributes"]["NSColor"]["sRGB"].array()[0]), number(x["attributes"]["NSColor"]["sRGB"].array()[1]),
                       number(x["attributes"]["NSColor"]["sRGB"].array()[2]), number(x["attributes"]["NSColor"]["sRGB"].array()[3])},
                      y["attributes"]["NSColor"], label + " run color", 2e-7);
                check(x["attributes"].contains("NSKern") == y["attributes"].contains("NSKern"), label + " run tracking");
                if (y["attributes"].contains("NSKern")) near(number(x["attributes"]["NSKern"]), number(y["attributes"]["NSKern"]), 1e-12, label + " run kern");
            }
        }
        // Ring, laptop and progress.
        const auto& ring = *layers[8];
        near(pose.ringPosition.x, numbers(ring["position"])[0], 1e-9, label + " ring x");
        near(pose.ringOpacity, number(ring["opacity"]), 0, label + " ring opacity");
        const auto& track = f.node(ring["children"].array()[0]);
        const auto& progress = f.node(ring["children"].array()[1]);
        const auto& laptop = f.node(ring["children"].array()[2]);
        color(colors.ringTrack, track["shape"]["strokeColor"], label + " ring track");
        color(colors.ringProgress, progress["shape"]["strokeColor"], label + " ring progress");
        color(colors.laptop, laptop["shape"]["strokeColor"], label + " laptop");
        near(content.reading.progress.value_or(0), number(progress["shape"]["strokeEnd"]), 1e-6, label + " ring strokeEnd");
        check(track["shape"]["lineCap"].string() == "round" && progress["shape"]["lineCap"].string() == "round", label + " round ring caps");
        auto pathNumbers = [](const Json& path) { std::vector<double> v; for (const auto& e : path.array()) for (const auto& p : e["points"].array()) for (const auto& n : p.array()) v.push_back(n.number()); return v; };
        auto mineTrack = pathNumbers(chargeRingLayer(false)["shape"]["path"]), sourceTrack = pathNumbers(track["shape"]["path"]);
        check(mineTrack.size() == sourceTrack.size(), label + " ring track topology");
        for (std::size_t i = 0; i < mineTrack.size(); ++i) near(mineTrack[i], sourceTrack[i], 1e-9, label + " ring track path");
        auto mineArc = pathNumbers(chargeRingLayer(true)["shape"]["path"]), sourceArc = pathNumbers(progress["shape"]["path"]);
        check(mineArc.size() == sourceArc.size(), label + " progress arc topology");
        for (std::size_t i = 0; i < mineArc.size(); ++i) near(mineArc[i], sourceArc[i], 1e-9, label + " progress arc path");
        auto mineLaptop = pathNumbers(chargeLaptopLayer()["shape"]["path"]), sourceLaptop = pathNumbers(laptop["shape"]["path"]);
        check(mineLaptop.size() == sourceLaptop.size(), label + " laptop topology");
        for (std::size_t i = 0; i < mineLaptop.size(); ++i) near(mineLaptop[i], sourceLaptop[i], 1e-9, label + " laptop path");
        near(number(laptop["shape"]["lineWidth"]), 1.15, 1e-12, label + " laptop width");
        // Ripples are masked by the body.
        for (std::size_t i = 0; i < 3; ++i) {
            const auto& ripple = f.node((*layers[1])["children"].array()[i]);
            color(colors.ripple, ripple["shape"]["strokeColor"], label + " ripple stroke");
            near(pose.ripplePosition[i].x, numbers(ripple["position"])[0], 1e-9, label + " ripple x");
            near(pose.ripplePosition[i].y, numbers(ripple["position"])[1], 1e-9, label + " ripple y");
            auto mine = pathNumbers(chargeRippleLayer()["shape"]["path"]), source = pathNumbers(ripple["shape"]["path"]);
            for (std::size_t k = 0; k < mine.size(); ++k) near(mine[k], source[k], 1e-9, label + " ripple path");
        }
        check(content.accessibility == tree["accessibility"].string(), label + " accessibility label: " + content.accessibility);
        ++compared;
    }
    check(compared >= 150, "All source indicator trees compared");
}

void timelines(const Fixture& f) {
    std::map<std::string, const Json*> byName;
    for (const auto& item : f.root["timelines"].array()) byName[item["name"].string()] = &item;
    const ChargeIndicatorAppearance appearance{};
    BatteryReading demo; demo.percentage = 75; demo.present = demo.pluggedIn = demo.charging = true;
    auto hoverColor = [&](bool hovered) { return chargeIndicatorColors(appearance, demo, ChargeMetric::battery, 0.75, ChargeStage::compact, hovered); };
    // Core Animation evaluates presentation tracks in float32 (24-bit
    // mantissa): a 226-point morph differs by up to ~2.2e-3 point from the
    // double-precision solve, opacity keyframes by ~8e-4.
    constexpr double geometry = 3e-3;
    struct Action { double at; std::function<void(ChargeIndicatorTimeline&, double)> run; };
    auto replay = [&](const std::string& name, ChargeStage initial, bool reduced, std::vector<Action> actions) {
        const auto& source = *byName.at(name);
        ChargeIndicatorTimeline timeline;
        timeline.setReduceMotion(reduced);
        timeline.setStage(initial, false, 0);
        const auto model = hoverColor(false);
        timeline.setBorderModel(model.borderWidth, model.border, 0);
        std::size_t next = 0;
        unsigned samples{};
        for (const auto& sample : source["samples"].array()) {
            const double t = number(sample["t"]);
            while (next < actions.size() && actions[next].at <= t + 1e-9) { timeline.advance(actions[next].at); actions[next].run(timeline, actions[next].at); ++next; }
            timeline.advance(t);
            const auto pose = timeline.sample(t);
            const auto label = name + " t=" + std::to_string(t);
            check(std::string(chargeStageKey(timeline.stage())) == sample["stage"].string(), label + " stage");
            const auto body = numbers(sample["body"]), shadow = numbers(sample["shadow"]);
            for (std::size_t i : {0u, 1u, 2u, 3u, 4u, 5u, 7u, 8u}) near(shadow[i], body[i], 0, label + " shadow plate follows body");
            near(shadow[6], 0, 0, label + " shadow plate has no border");
            near(pose.bodyWidth, body[0], geometry, label + " body width");
            near(pose.bodyHeight, body[1], geometry, label + " body height");
            near(pose.bodyRadius, body[2], geometry, label + " body radius");
            near(pose.bodyScale, body[3], 2e-4, label + " body scale");
            near(pose.bodyOpacity, body[5], 1e-3, label + " body opacity");
            near(pose.borderWidth, body[6], 1e-3, label + " border width");
            color(pose.borderColor, sample["bodyBorder"], label + " border color", 1e-3);
            const auto emblem = numbers(sample["emblem"]);
            near(pose.emblemWidth, emblem[0], geometry, label + " emblem width");
            near(pose.emblemHeight, emblem[1], geometry, label + " emblem height");
            near(pose.emblemCenter.x, emblem[2], geometry, label + " emblem x");
            near(pose.emblemCenter.y, emblem[3], geometry, label + " emblem y");
            near(pose.emblemScale, emblem[4], 2e-4, label + " emblem scale");
            near(pose.emblemOpacity, emblem[5], 1e-3, label + " emblem opacity");
            // Core Animation interpolates the two CGPath rounded rectangles
            // point-wise (degree-elevating lines when topologies differ), so
            // the rendered outline is the rounded rectangle of interpolated
            // width/height/radius. Check every sampled outline point.
            {
                std::vector<core::Point> outline;
                core::Point current{};
                const auto ops = sample["emblemOps"].array();
                const auto& points = sample["emblemPath"].array();
                std::size_t index = 0;
                auto point = [&]() { const auto p = numbers(points.at(index++)); return core::Point{p[0], p[1]}; };
                for (const auto& op : ops) {
                    const auto kind = op.string();
                    if (kind == "move" || kind == "line") { current = point(); outline.push_back(current); }
                    else if (kind == "cubic") {
                        const auto a = current, b = point(), c = point(), d = point();
                        for (int k = 1; k <= 8; ++k) {
                            const double u = k / 8.0, v = 1 - u;
                            outline.push_back({v * v * v * a.x + 3 * v * v * u * b.x + 3 * v * u * u * c.x + u * u * u * d.x,
                                               v * v * v * a.y + 3 * v * v * u * b.y + 3 * v * u * u * c.y + u * u * u * d.y});
                        }
                        current = d;
                    }
                }
                check(index == points.size(), label + " emblem path decoded");
                double minX = 1e9, maxX = -1e9, minY = 1e9, maxY = -1e9;
                for (const auto p : outline) {
                    const double qx = std::abs(p.x - pose.emblemWidth / 2) - (pose.emblemWidth / 2 - pose.emblemRadius);
                    const double qy = std::abs(p.y - pose.emblemHeight / 2) - (pose.emblemHeight / 2 - pose.emblemRadius);
                    const double distance = std::hypot(std::max(qx, 0.0), std::max(qy, 0.0)) + std::min(std::max(qx, qy), 0.0) - pose.emblemRadius;
                    near(distance, 0, 5e-3, label + " interpolated emblem outline");
                    minX = std::min(minX, p.x); maxX = std::max(maxX, p.x); minY = std::min(minY, p.y); maxY = std::max(maxY, p.y);
                }
                near(minX, 0, geometry, label + " emblem outline left"); near(maxX, pose.emblemWidth, geometry, label + " emblem outline right");
                near(minY, 0, geometry, label + " emblem outline top"); near(maxY, pose.emblemHeight, geometry, label + " emblem outline bottom");
            }
            const auto bolt = numbers(sample["bolt"]);
            near(pose.boltPosition.x, bolt[0], geometry, label + " bolt x");
            near(pose.boltScale, bolt[2], 2e-4, label + " bolt scale");
            near(pose.boltOpacity, bolt[3], 1e-3, label + " bolt opacity");
            const auto colors = chargeIndicatorColors(appearance, demo, ChargeMetric::battery, 0.75, timeline.stage(), false);
            color(colors.emblem, sample["emblemFill"], label + " emblem fill");
            color(colors.bolt, sample["boltFill"], label + " bolt fill");
            for (std::size_t i = 0; i < 4; ++i) {
                const auto text = numbers(sample["texts"].array()[i]);
                near(pose.textPosition[i].x, text[0], geometry, label + " label x");
                near(pose.textPosition[i].y, text[1], 1e-9, label + " label y");
                near(pose.textOpacity[i], text[2], 1e-3, label + " label opacity");
            }
            const auto ring = numbers(sample["ring"]);
            near(pose.ringPosition.x, ring[0], geometry, label + " ring x");
            near(pose.ringOpacity, ring[2], 1e-3, label + " ring opacity");
            for (std::size_t i = 0; i < 3; ++i) {
                const auto ripple = numbers(sample["ripples"].array()[i]);
                near(pose.ripplePosition[i].x, ripple[0], geometry, label + " ripple x");
                near(pose.ripplePosition[i].y, ripple[1], geometry, label + " ripple y");
                near(pose.rippleScale[i], ripple[2], 2e-4, label + " ripple scale");
                near(pose.rippleOpacity[i], ripple[3], 1e-3, label + " ripple opacity");
            }
            const auto rect = numbers(sample["embeddedBodyRect"]);
            const auto mine = pose.bodyRect();
            near(mine.x, rect[0], 3e-3, label + " embedded rect x");
            near(mine.y, rect[1], 3e-3, label + " embedded rect y");
            near(mine.width, rect[2], 3e-3, label + " embedded rect width");
            ++samples;
        }
        check(samples > 5, name + " sampled");
    };
    replay("entrance", ChargeStage::hidden, false, {{0, [](auto& t, double at) { t.animateEntrance(at); }}});
    replay("exit", ChargeStage::compact, false, {{0, [](auto& t, double at) { t.animateExit(at); }}});
    replay("interruptedEntrance", ChargeStage::hidden, false,
           {{0, [](auto& t, double at) { t.animateEntrance(at); }}, {0.55, [](auto& t, double at) { t.animateExit(at); }}});
    replay("setStageAnimated", ChargeStage::compact, false,
           {{0, [](auto& t, double at) { t.setStage(ChargeStage::circle, true, at); }},
            {0.10, [](auto& t, double at) { t.setStage(ChargeStage::supercharge, true, at); }},
            {0.40, [](auto& t, double at) { t.setStage(ChargeStage::compact, true, at); }}});
    replay("morphReversal", ChargeStage::circle, false,
           {{0, [](auto& t, double at) { t.morphEmbedded(ChargeStage::compact, true, at); }},
            {0.10, [](auto& t, double at) { t.morphEmbedded(ChargeStage::circle, true, at); }},
            {0.45, [](auto& t, double at) { t.morphEmbedded(ChargeStage::compact, true, at); }}});
    auto hover = [&](bool hovered) {
        return [&, hovered](ChargeIndicatorTimeline& t, double at) { const auto c = hoverColor(hovered); t.animateBorder(c.borderWidth, c.border, true, at); };
    };
    replay("hover", ChargeStage::compact, false, {{0, hover(true)}, {0.08, hover(false)}, {0.30, hover(true)}});
    replay("hiddenFromCircle", ChargeStage::circle, false, {{0, [](auto& t, double at) { t.setStage(ChargeStage::hidden, true, at); }}});
    replay("reducedEntrance", ChargeStage::hidden, true,
           {{0, [](auto& t, double at) { t.animateEntrance(at); }}, {0.05, hover(true)}, {0.10, [](auto& t, double at) { t.animateExit(at); }}});
}

void sequenceContracts() {
    ChargeIndicatorTimeline t;
    t.setStage(ChargeStage::hidden, false, 0);
    check(!t.nextDeadline() && !t.animating(0), "Settled hidden indicator owns no deadline or frames");
    t.animateEntrance(10);
    check(t.nextDeadline() == 10.20 && t.animating(10.05), "Entrance schedules the source +0.20 supercharge step");
    check(t.advance(10.19) == ChargeIndicatorTimeline::Event::none && t.stage() == ChargeStage::circle, "Circle before supercharge");
    check(t.advance(12) == ChargeIndicatorTimeline::Event::entranceCompleted && t.stage() == ChargeStage::compact && !t.nextDeadline(),
          "A late wake applies the remaining steps at their exact source times, then completes once");
    check(!t.animating(12), "Finished entrance stops requesting frames");
    t.animateExit(20);
    const auto generation = t.generation();
    t.setStage(ChargeStage::compact, false, 20.1);
    check(t.generation() != generation && !t.nextDeadline() && t.advance(30) == ChargeIndicatorTimeline::Event::none,
          "setStage cancels the pending exit completion");
    t.animateExit(40);
    check(t.advance(40.30) == ChargeIndicatorTimeline::Event::none && t.stage() == ChargeStage::hidden, "Hidden shrink begins at +0.30");
    check(t.advance(40.6399) == ChargeIndicatorTimeline::Event::none && t.advance(40.64) == ChargeIndicatorTimeline::Event::exitCompleted,
          "Exit completes when the 0.34 s shrink-and-fade completes");
    ChargeIndicatorTimeline reduced;
    reduced.setReduceMotion(true);
    reduced.animateEntrance(1);
    check(reduced.stage() == ChargeStage::compact && !reduced.animating(1) &&
          reduced.advance(1) == ChargeIndicatorTimeline::Event::entranceCompleted, "Reduce Motion presents compact immediately");
}

void readings(const Fixture& f) {
    unsigned compared{};
    for (const auto& row : f.root["readings"].array()) {
        const auto lang = language(row["chinese"].boolean());
        const auto m = metric(row["metric"]);
        const auto r = resolveChargeMetric(m, battery(row["battery"]), telemetry(row["telemetry"]), lang);
        const auto label = row["metric"].string() + "/" + row["snapshot"].string() + (row["chinese"].boolean() ? "/zh" : "/en");
        check(chargeMetricTitle(m, lang) == row["title"].string(), label + " title");
        if (r.primary != row["primary"].string() || r.secondary != row["secondary"].string() || r.trailing != row["trailing"].string())
            std::cerr << label << ": " << r.primary << r.secondary << " " << r.unit << " " << r.trailing << " vs " << row["primary"].string()
                      << row["secondary"].string() << " " << row["unit"].string() << " " << row["trailing"].string() << '\n';
        check(r.primary == row["primary"].string() && r.secondary == row["secondary"].string() && r.unit == row["unit"].string() &&
              r.trailing == row["trailing"].string(), label + " formatted reading");
        check(r.accessibilityValue == row["accessibility"].string(), label + " accessibility value: " + r.accessibilityValue);
        if (row["progress"].isNull()) check(!r.progress, label + " no invented progress");
        else near(r.progress.value_or(-1), number(row["progress"]), 1e-12, label + " progress");
        ++compared;
    }
    check(compared >= 90, "All source metric projections compared");
    for (const auto& row : f.root["snapshots"].array()) {
        const auto r = battery(row);
        check(batteryChargeMode(r) == row["chargeMode"].boolean(), row["name"].string() + " charge mode");
        const auto tone = batteryLevelTone(r.percentage);
        const std::string name = !tone ? "none" : *tone == BatteryLevelTone::green ? "green" : *tone == BatteryLevelTone::yellow ? "yellow" : "red";
        check(name == row["tone"].string(), row["name"].string() + " level tone");
    }
    for (auto language : {core::Language::english, core::Language::simplifiedChinese, core::Language::traditionalChinese,
                          core::Language::japanese, core::Language::korean}) {
        BatteryReading plugged, unplugged;
        plugged.percentage = unplugged.percentage = 50; plugged.present = unplugged.present = plugged.pluggedIn = true;
        check(!chargeModeTitle(plugged, language).empty() && chargeModeTitle(plugged, language) != chargeModeTitle(unplugged, language),
              "Five-language charge/power mode titles");
    }
    BatteryReading plugged, unplugged, desktop;
    plugged.percentage = unplugged.percentage = 50; plugged.present = unplugged.present = plugged.pluggedIn = true;
    desktop.pluggedIn = true;
    check(chargeModeTitle(plugged, core::Language::simplifiedChinese) == "超充模式" &&
          chargeModeTitle(unplugged, core::Language::simplifiedChinese) == "电源模式",
          "超充模式 only for a plugged-in battery machine, including a charging pause");
    check(chargeModeTitle(desktop, core::Language::english) == "POWER MODE", "Desktop without a battery never shows CHARGE MODE");
}
} // namespace

int main(int argc, char** argv) {
    try {
        check(argc == 2, "Pass charge-indicator-source.json");
        std::ifstream input(argv[1], std::ios::binary);
        std::stringstream bytes;
        bytes << input.rdbuf();
        Fixture fixture{Json::parse(bytes.str(), 64 * 1024 * 1024)};
        check(fixture.root["liveServices"].boolean() == false, "Detached oracle");
        trees(fixture);
        if (std::getenv("CHARGE_MAXIMA")) { std::map<std::string, double> m; maxima = &m; timelines(fixture); maxima = nullptr; for (auto& [k, v] : m) std::cout << k << " " << v << "\n"; }
        timelines(fixture);
        sequenceContracts();
        readings(fixture);
        std::cout << "Charge indicator: " << checks << " original-source checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Charge indicator after " << checks << " checks: " << error.what() << '\n';
        return 1;
    }
}
