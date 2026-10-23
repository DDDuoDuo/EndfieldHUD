// The facility alerts, banner, tag stack and boot log against the Mac 1.3.0
// oracle (tests/fixtures/facility-source.json, tools/facility_reference.*:
// the unchanged FacilityAlertMonitor, HUDFacilityBanner, HUDFacilityTags and
// HUDBootLog, Reduce Motion on, every state at rest): the alert strings in
// five languages; the banner of each alert in English, Simplified Chinese and
// Japanese, the tag stack listed and after x, and the boot log panel with
// every line revealed, compared primitive by primitive (what each tree draws,
// tests/hud_design_oracle.hpp) with the Mac's recorded text widths, plus
// their controls and accessibility items; the boot log lines' segments and
// runs. Two recorded substitutions are normalised before the comparison:
// the tri-strip motif is one cached raster on the Mac and three bands here,
// and the tag's chamfered hazard background is a masked fill on the Mac and
// a chamfered shape here (its alpha clips the drifting band).
#include "modules/boot_log.hpp"
#include "modules/facility_tags.hpp"
#include "tests/hud_design_oracle.hpp"
#include <iostream>

namespace {
namespace m = endfield::modules;
namespace d = endfield::modules::design;
namespace f = endfield::modules::facility;
namespace b = endfield::modules::boot;
namespace o = endfield::tests::design_oracle;
using Json = ehud::data::Json;
using endfield::core::Language;
unsigned checks{};
void check(bool v, const std::string& s) { ++checks; if (!v) throw std::runtime_error(s); }
Language languageNamed(std::string_view tag) {
    if (tag == "zh-Hans") return Language::simplifiedChinese;
    if (tag == "zh-Hant") return Language::traditionalChinese;
    if (tag == "ja") return Language::japanese;
    if (tag == "ko") return Language::korean;
    return Language::english;
}
m::FacilityAlert alertOf(const Json& input) {
    const auto& kind = input["kind"].string();
    if (kind == "disk") {
        const auto& v = input["disk"];
        return {{m::FacilityAlertKind::disk, m::FacilityDiskReading::make(v["volumeName"].string(), std::int64_t(v["totalBytes"].number()), std::int64_t(v["availableBytes"].number())), {}, {}}, true};
    }
    if (kind == "mem") {
        const auto& v = input["memory"];
        std::optional<m::FacilityMemoryUsage> usage;
        if (v["usage"].isObject()) usage = m::FacilityMemoryUsage::make(std::uint64_t(v["usage"]["usedBytes"].number()), std::uint64_t(v["usage"]["totalBytes"].number()));
        return {{m::FacilityAlertKind::memory, {}, m::FacilityMemoryReading{m::FacilityMemoryPressure(int(v["pressure"].number())), usage}, {}}, true};
    }
    const auto& v = input["heat"];
    std::optional<double> celsius;
    if (!v["temperature"].isNull()) celsius = v["temperature"].number();
    return {{m::FacilityAlertKind::heat, {}, {}, m::FacilityThermalReading{m::FacilityThermalState(int(v["state"].number())), celsius}}, true};
}
struct World {
    o::RecordedMeasurer measurer;
    d::Environment env;
    World(const Json& text, Language language) {
        o::recordText(measurer, text);
        env.text.measurer = &measurer; env.text.uppercase = d::asciiUppercase; env.now = 10; env.language = language; env.reduceMotion = true;
        env.text.uiRegion = endfield::core::regionFor(language, d::TextRegion::simplifiedChinese); // Context::apply's rule
        env.palette.apply("FAD41F", false);
    }
};
// What a tree draws, with the two recorded substitutions normalised.
std::vector<o::Primitive> ours(const d::Layer& root) {
    std::vector<o::Primitive> out;
    o::layerPrimitives(root, root, 1, out);
    std::vector<o::Primitive> result;
    std::optional<o::Primitive> tri;
    for (auto& p : out) {
        if (p.where.ends_with(".tri")) {
            if (!tri) { tri = p; tri->kind = "artwork"; tri->color.reset(); }
            else tri->rect = {std::min(tri->rect.x, p.rect.x), std::min(tri->rect.y, p.rect.y), std::max(tri->rect.width, p.rect.width), tri->rect.height + p.rect.height};
            continue;
        }
        if (p.where.ends_with(".hazard") && p.kind == "shape" && p.where.find(".tag.") != std::string::npos) p.kind = "fill";
        result.push_back(std::move(p));
    }
    if (tri) result.push_back(*tri);
    return result;
}
void compareTree(const std::string& label, const Json& row, const d::Layer& root, std::span<d::Control* const> controls, std::vector<std::string>& failures) {
    std::vector<o::Primitive> mac;
    o::macPrimitives(row["layer"], o::Affine{}, 1, mac, "");
    auto mine = ours(root);
    const auto invisible = [](const o::Primitive& p) { return p.opacity <= 1e-3 || (p.color && p.color->a <= 1e-3); };
    std::erase_if(mac, invisible); std::erase_if(mine, invisible);
    std::vector<bool> used(mine.size());
    for (const auto& p : mac) {
        bool found = false;
        for (std::size_t i = 0; i < mine.size() && !found; ++i) if (!used[i] && o::same(p, mine[i])) found = used[i] = true;
        if (!found) failures.push_back(label + ": Mac draws " + o::describe(p));
    }
    for (std::size_t i = 0; i < mine.size(); ++i) if (!used[i]) failures.push_back(label + ": Windows draws " + o::describe(mine[i]));
    if (!row["controls"].isArray()) { if (!controls.empty()) failures.push_back(label + ": controls without an oracle row"); return; }
    const auto& theirs = row["controls"].array();
    if (theirs.size() != controls.size()) { failures.push_back(label + ": control count " + std::to_string(controls.size()) + " vs " + std::to_string(theirs.size())); return; }
    for (std::size_t i = 0; i < theirs.size(); ++i) {
        const auto& t = theirs[i]; const auto& c = *controls[i];
        if (t["id"].string() != c.id()) failures.push_back(label + ": control id " + c.id() + " vs " + t["id"].string());
        if (t["label"].string() != c.label) failures.push_back(label + ": label of " + c.id() + " '" + c.label + "' vs '" + t["label"].string() + "'");
        if (t["enabled"].boolean() != c.enabled()) failures.push_back(label + ": enabled of " + c.id());
        if (!o::near(o::rectOf(t["rect"]), c.rect(root), 2e-3)) failures.push_back(label + ": rect of " + c.id() + " " + o::str(c.rect(root)) + " vs " + o::str(o::rectOf(t["rect"])));
        std::vector<d::AccessibilityItem> items; c.accessibilityItems(root, items);
        o::compareAccessibility(label + "/" + c.id(), t["accessibility"], items, failures);
    }
}

void strings(const Json& rows) {
    for (const auto& row : rows.array()) {
        const auto alert = alertOf(row["input"]);
        for (const auto* tag : {"en", "zh-Hans", "zh-Hant", "ja", "ko"}) {
            const auto l = languageNamed(tag);
            const auto& e = row[tag];
            const std::string where = row["variant"].string() + "/" + row["input"]["kind"].string() + "/" + tag;
            check(alert.title(l) == e["title"].string(), where + " title: " + alert.title(l));
            check(alert.value(l) == e["value"].string(), where + " value: " + alert.value(l));
            check(alert.unit(l) == e["unit"].string(), where + " unit: " + alert.unit(l));
            check(alert.detail(l) == e["detail"].string(), where + " detail: " + alert.detail(l));
            check(alert.tagText(l) == e["tagText"].string() && alert.tagLabel(l) == e["tagLabel"].string(), where + " tag: " + alert.tagText(l));
            check(alert.tagSuffix(l).value_or("<none>") == (e["tagSuffix"].isNull() ? "<none>" : e["tagSuffix"].string()), where + " tag suffix");
            check(alert.englishTitle() == e["englishTitle"].string() && alert.chineseTitle() == e["chineseTitle"].string(), where + " fixed titles");
        }
    }
}

void banners(const Json& data, std::vector<std::string>& failures) {
    for (const auto& row : data["banners"].array()) {
        World w(data["text"], languageNamed(row["language"].string()));
        f::FacilityBanner banner(w.env);
        banner.show(alertOf(row["input"]));
        const auto label = "banner/" + row["variant"].string() + "/" + row["input"]["kind"].string() + "/" + row["language"].string();
        d::Control* controls[] = {&banner.control()};
        compareTree(label, row, banner.layer(), controls, failures);
        const auto texts = banner.texts();
        for (std::size_t i = 0; i < texts.size(); ++i)
            if (texts[i] != row["texts"].array()[i].string()) failures.push_back(label + ": text " + std::to_string(i) + " '" + texts[i] + "'");
        if (banner.isCountingDown() != row["counting"].boolean() || !o::near(banner.countdownRemaining(), row["remaining"].number())) failures.push_back(label + ": countdown");
        check(w.measurer.misses.empty(), label + ": unrecorded text width " + (w.measurer.misses.empty() ? std::string() : w.measurer.misses.front()));
    }
}

void tags(const Json& data, std::vector<std::string>& failures) {
    const auto& rows = data["tags"].array();
    for (std::size_t i = 0; i + 1 < rows.size(); i += 2) {
        const auto& listed = rows[i];
        World w(data["text"], languageNamed(listed["language"].string()));
        f::FacilityTagStack stack(w.env);
        std::vector<m::FacilityAlert> alerts;
        for (const auto& input : listed["input"].array()) alerts.push_back(alertOf(input));
        stack.update(alerts, false);
        const auto label = "tags/" + listed["variant"].string() + "/" + listed["language"].string();
        auto controls = stack.controls();
        compareTree(label + "/listed", listed, stack.layer(), controls, failures);
        if (!o::near(stack.preferredHeight(), listed["height"].number())) failures.push_back(label + ": height");
        controls[1]->activate();
        const auto& dismissed = rows[i + 1];
        controls = stack.controls();
        compareTree(label + "/dismissed", dismissed, stack.layer(), controls, failures);
        if (!o::near(stack.preferredHeight(), dismissed["height"].number())) failures.push_back(label + ": height after x");
        check(w.measurer.misses.empty(), label + ": unrecorded text width " + (w.measurer.misses.empty() ? std::string() : w.measurer.misses.front()));
    }
}

std::vector<b::BootLogLine> bootLines(std::string_view variant) {
    const b::BootLogDate date{2026, 10, 19, 6, 45};
    if (variant == "full") {
        const std::vector<std::string> events{"Stand-up\nweekly", "  ", "Third"};
        return b::bootLogLines(date, 273'959, b::BootLogBattery{82, true, b::BootLogBattery::Level::green}, events, b::BootLogSanity{180, 240, 271.2},
            b::BootLogWorkStreak{3, 12.34}, b::BootLogDisk{8, true}, "  Perlica \n", d::asciiUppercase);
    }
    if (variant == "sparse") {
        const std::vector<std::string> events{"One"};
        return b::bootLogLines(date, -5, std::nullopt, events, std::nullopt, std::nullopt, std::nullopt, " ", d::asciiUppercase);
    }
    return b::bootLogLines(date, 60, b::BootLogBattery{15, false, b::BootLogBattery::Level::red}, {}, b::BootLogSanity{1, 2, std::nullopt},
        b::BootLogWorkStreak{1, 0}, b::BootLogDisk{40, false}, "x", d::asciiUppercase);
}

void boot(const Json& data, std::vector<std::string>& failures) {
    d::Palette palette;
    for (const auto& row : data["bootContent"].array()) {
        const auto variant = row["variant"].string();
        const auto lines = bootLines(variant);
        const auto& expected = row["lines"].array();
        check(lines.size() == expected.size(), "boot/" + variant + ": line count");
        for (std::size_t i = 0; i < lines.size(); ++i) {
            const auto label = "boot/" + variant + "/" + std::to_string(i);
            const auto segments = lines[i].segments(palette);
            const auto& e = expected[i]["segments"].array();
            check(segments.size() == e.size(), label + ": segment count");
            for (std::size_t k = 0; k < e.size(); ++k)
                check(segments[k].text == e[k]["text"].string() && o::near(segments[k].color, *o::rgbaOf(e[k]["color"]), 1.0 / 400), label + ": segment '" + segments[k].text + "'");
            const auto runs = b::BootLog::runs(lines[i], palette, d::portableCharacterRules());
            const auto& r = expected[i]["runs"].array();
            check(runs.size() == r.size(), label + ": run count");
            for (std::size_t k = 0; k < r.size(); ++k) {
                const auto& s = r[k]["style"];
                const double tracking = s.contains("tracking") ? s["tracking"].number() : 0;
                check(runs[k].text == r[k]["text"].string() && o::near(runs[k].style.size, s["size"].number()) && int(runs[k].style.weight) == int(s["weight"].number())
                    && o::near(runs[k].style.tracking, tracking) && o::near(runs[k].style.color, *o::rgbaOf(s["color"]), 1.0 / 400), label + ": run '" + runs[k].text + "'");
            }
        }
    }
    for (const auto& row : data["bootPanels"].array()) {
        const auto variant = row["variant"].string();
        World w(data["text"], Language::english);
        b::BootLog log(w.env);
        const auto lines = bootLines(variant);
        log.play(lines, "1.3.0");
        for (int guard = 0; guard < 64 && log.revealedCount() < std::min<int>(int(lines.size()), b::BootLog::capacity); ++guard) {
            const auto next = w.env.deadlines.next();
            if (!next) break;
            w.env.now = std::max(w.env.now, *next);
            w.env.deadlines.advance(w.env.now, 1);
        }
        const auto label = "bootPanel/" + variant;
        check(log.revealedCount() == int(row["revealed"].number()), label + ": revealed");
        compareTree(label, row, log.layer(), {}, failures);
        const auto texts = log.rowTexts();
        const auto& rows = row["rows"].array();
        check(texts.size() == rows.size(), label + ": rows");
        for (std::size_t i = 0; i < rows.size(); ++i) check(texts[i] == rows[i].string(), label + ": row " + texts[i]);
        if (!o::near(log.cursorFrame(), o::rectOf(row["cursor"]))) failures.push_back(label + ": cursor " + o::str(log.cursorFrame()));
        check(log.terminalText() == row["terminal"].string(), label + ": terminal");
        check(w.measurer.misses.empty(), label + ": unrecorded text width " + (w.measurer.misses.empty() ? std::string() : w.measurer.misses.front()));
    }
}
}

int main(int argc, char** argv) {
    try {
        const auto data = o::loadOracle(argc > 1 ? argv[1] : nullptr, {"banners", "tags"}, {"text", "strings", "bootContent", "bootPanels"});
        check(data.has_value(), "The Mac oracle is present");
        strings((*data)["strings"]);
        std::vector<std::string> failures;
        banners(*data, failures);
        tags(*data, failures);
        boot(*data, failures);
        for (std::size_t i = 0; i < failures.size() && i < 60; ++i) std::cerr << failures[i] << '\n';
        check(failures.empty(), std::to_string(failures.size()) + " oracle mismatches");
        std::cout << "facility oracle contracts passed (" << checks << " checks)\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAILED: " << e.what() << '\n';
        return 1;
    }
}
