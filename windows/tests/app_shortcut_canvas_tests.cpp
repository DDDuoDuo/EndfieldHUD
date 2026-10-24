// Contracts of the redesigned App Shortcuts screen (modules/app_shortcut_canvas.*,
// modules/app_shortcut_artwork.*, native/app_shortcut_assets.*): the rules of
// Sources/AppShortcutCanvas.swift and AppShortcutArtwork.swift / HUDSourceDesktopIconLayout (each
// check cites the Swift it ports) over a synthetic store, the packaged artwork against the Mac's own
// rasters, and, when the Mac oracle is given (tests/fixtures/app-shortcut-source.json from
// tools/app_shortcut_reference.sh), every recorded scene node by node with the Mac's own text widths,
// its controls, accessibility items, header, toasts and confirm requests. Synthetic data only.
#include "modules/app_shortcut_canvas.hpp"
#include "native/app_shortcut_assets.hpp"
#include "core/data/file_io.hpp"
#include "core/shell_packet.hpp"
#include "tests/hud_design_oracle.hpp"
#include <iostream>
#include <map>

namespace d = endfield::modules::design;
namespace m = endfield::modules;
namespace n = endfield::native;
using endfield::core::Language;
using namespace endfield::tests::design_oracle;
namespace {
unsigned checks{};
void check(bool v, const std::string& what) { ++checks; if (!v) throw std::runtime_error(what); }
d::Control* control(d::Canvas& canvas, std::string_view id) {
    for (d::Control* c : canvas.page().controls()) if (c->id() == id) return c;
    for (d::Control* c : canvas.header().controls()) if (c->id() == id) return c;
    return nullptr;
}
d::Point centre(const d::Control& c, const d::Layer& root) { const auto r = c.rect(root); return {r.x + r.width / 2, r.y + r.height / 2}; }
d::KeyInput ctrl(char32_t c) { d::KeyInput k; k.virtualKey = std::uint32_t(c >= U'a' ? c - 32 : c); k.character = c; k.control = true; return k; }

struct World {
    d::TextMeasurer& measurer;
    d::Context context;
    double now{100};
    explicit World(d::TextMeasurer& m) : measurer(m), context(m, d::asciiUppercase) { context.update(d::geometry::baseUnit, 2 / d::geometry::baseUnit, now); }
    void appearance(Language language, bool light = false) {
        d::Context::Appearance a; a.language = language; a.light = light; a.reduceMotion = true; context.apply(a, now);
    }
    void show(d::Canvas& c) { c.show(now); c.setActive(true, now); }
    std::optional<d::ToastRequest> toast() { return context.overlay().toastState().current(); }
};

// A synthetic owner: requests and choosers are recorded, answered by the test.
struct Owner {
    std::vector<m::AppShortcutRequest> requests;
    std::vector<std::string> launches, clipboard;
    std::map<std::string, bool> icons; std::map<std::string, d::Color> colors;
    unsigned chooses{}, cancels{}; bool refuse{}, chooserBusy{};
    m::AppShortcutCanvasServices services() {
        m::AppShortcutCanvasServices s;
        s.request = [this](const m::AppShortcutRequest& r) { if (refuse) return false; requests.push_back(r); return true; };
        s.choose = [this] { ++chooses; return !chooserBusy; };
        s.cancelChooser = [this] { ++cancels; };
        s.launch = [this](std::string_view id) { launches.emplace_back(id); };
        s.isApplication = [](std::string_view p) { return p.ends_with(".exe") || p.ends_with(".lnk") || p.starts_with("shell:AppsFolder\\"); };
        s.pasteboard = [this] { return clipboard; };
        s.trimmed = [](std::string_view v) {
            const auto a = v.find_first_not_of(" \t\r\n"); if (a == v.npos) return std::string();
            return std::string(v.substr(a, v.find_last_not_of(" \t\r\n") - a + 1)); };
        s.icon = [this](std::string_view id) -> std::optional<d::ImageSource> {
            const auto f = icons.find(std::string(id)); if (f == icons.end() || !f->second) return std::nullopt;
            return d::ImageSource{d::ImageSource::Kind::memory, "icon." + std::string(id), {}, 1}; };
        s.tileColor = [this](std::string_view id) -> std::optional<d::Color> {
            const auto f = colors.find(std::string(id)); if (f == colors.end()) return std::nullopt; return f->second; };
        return s;
    }
};
m::ShortcutRecord record(std::string id, std::string name, std::string preset, bool native = true) {
    m::ShortcutRecord r; r.id = std::move(id); r.name = std::move(name); r.originalName = r.name; r.iconPreset = std::move(preset);
    if (native) r.locator = Json::Object{{"referencePlatform", "windows"}};
    return r;
}
m::ShortcutFile fileOf(std::initializer_list<m::ShortcutRecord> rows) { m::ShortcutFile f; f.items.assign(rows.begin(), rows.end()); return f; }

std::unique_ptr<n::NativeAppShortcutAssets> assets(const char* root) { return std::make_unique<n::NativeAppShortcutAssets>(root); }

// --- Artwork and icon layout ----------------------------------------------------------------
void layoutRules() {
    // HUDSourceDesktopIconLayout.glyphSize (:11-16).
    check(near(m::desktopIconGlyphFrame(96, 96), {4.5, 4.5, 23, 23}) && near(m::desktopIconGlyphFrame(60, 40), {2, 32. / 2 - 28. * 40 / 60 / 2, 28, 28. * 40 / 60})
          && near(m::desktopIconGlyphFrame(0, 5), {3, 3, 26, 26}), "Glyphs share a 23 x 23 area, never past 28 (:11-21)");
    std::vector<std::uint8_t> icon(96 * 96 * 4, 0);
    check(!m::trimDesktopIcon(96, 96, icon), "A transparent icon trims to nothing (the Mac keeps the source)");
    for (unsigned y = 30; y < 40; ++y) for (unsigned x = 10; x < 15; ++x) icon[(y * 96 + x) * 4 + 3] = 9;
    icon[(5 * 96 + 90) * 4 + 3] = 8;
    const auto t = m::trimDesktopIcon(96, 96, icon);
    check(t && t->width == 5 && t->height == 10 && t->straightRGBA.size() == 200, "Alpha above 8 only: the crop of the visible pixels (:50-54)");
    bool threw = false; try { (void)m::trimDesktopIcon(48, 48, std::vector<std::uint8_t>(48 * 48 * 4)); } catch (const std::invalid_argument&) { threw = true; }
    check(threw, "Only the 96-pixel icon square is trimmed");
    std::vector<std::uint8_t> white(4 * 4 * 4, 255);
    const auto c = m::shortcutTileColor(4, 4, white);
    check(c && near((*c)[0], .72 * .55 / .72, 1e-9) && near((*c)[1], (*c)[2]), "A bright icon darkens to luminance .55 (:309-311)");
    check(!m::shortcutTileColor(4, 4, std::vector<std::uint8_t>(64, 0)), "No coverage: no colour (the canvas uses #2A2A2A)");
}
void artworkOracle(const Json& data, const m::AppShortcutArtwork& art, const char* root) {
    const auto& presets = data["presets"].array();
    check(presets.size() == art.presets().size(), "One packaged preset per AppShortcutIcon case");
    static constexpr std::array<std::pair<Language, std::string_view>, 5> languages{{{Language::english, "en"}, {Language::simplifiedChinese, "zh-Hans"},
        {Language::traditionalChinese, "zh-Hant"}, {Language::japanese, "ja"}, {Language::korean, "ko"}}};
    for (std::size_t i = 0; i < presets.size(); ++i) {
        const auto& mac = presets[i]; const auto& ours = art.presets()[i];
        check(mac["id"].string() == ours.id && mac["file"].string() == ours.file && unsigned(mac["size"].array()[0].integer()) == ours.width &&
              unsigned(mac["size"].array()[1].integer()) == ours.height && near(rectOf(mac["frame"]), ours.frame, 1e-6), "Preset " + ours.id + " is the Mac's placed raster");
        for (const auto& [language, tag] : languages)
            check(art.title(ours.id, language) == mac["titles"][std::string(tag)].string(), "Preset title " + ours.id + " in " + std::string(tag));
        const auto png = ehud::data::detail::readFile(std::filesystem::path(root) / ours.file, 256 * 1024);
        check(png && endfield::core::packet::sha256({reinterpret_cast<const std::uint8_t*>(png->data()), png->size()}) == ours.sha256, "Preset PNG " + ours.id + " is pinned");
        // placedImage (:180-186): the trimmed raster's glyphFrame in the 30-point box.
        const auto g = m::desktopIconGlyphFrame(ours.width, ours.height);
        check(near(ours.frame, {5 + g.x * 30 / 32, 5 + g.y * 30 / 32, g.width * 30 / 32, g.height * 30 / 32}, 1e-6), "Preset " + ours.id + " frame = glyphFrame x 30/32");
    }
    for (const auto& row : data["glyphFrames"].array()) {
        const auto& s = row["size"].array();
        check(near(m::desktopIconGlyphFrame(s[0].number(), s[1].number()), rectOf(row["glyph"]), 1e-6), "glyphFrame matches the Mac");
    }
    // HUDSourceDesktopIconLayout.image and AppShortcutCanvas.averageColor of the shared synthetic icon.
    std::vector<std::uint8_t> icon(96 * 96 * 4, 0);
    for (unsigned y = 0; y < 96; ++y) for (unsigned x = 0; x < 96; ++x) {
        auto* p = &icon[(y * 96 + x) * 4];
        if (x >= 10 && x < 86 && y >= 20 && y < 70) { p[0] = std::uint8_t(x * 2); p[1] = std::uint8_t(y * 3); p[2] = 128; p[3] = 255; }
        else if (x == 5 && y == 5) { p[0] = 255; p[3] = 6; }
    }
    const auto& pattern = data["trim"]["pattern"];
    const auto t = m::trimDesktopIcon(96, 96, icon);
    check(t && t->width == unsigned(pattern["size"].array()[0].integer()) && t->height == unsigned(pattern["size"].array()[1].integer()), "The trim crop matches the Mac's");
    const auto c = m::shortcutTileColor(96, 96, icon); const auto mac = rgbaOf(pattern["average"]);
    // The Mac resamples to 8 x 8 first (CoreGraphics, medium quality): the area average differs by its filter.
    check(c && mac && near((*c)[0], mac->r, 4. / 255) && near((*c)[1], mac->g, 4. / 255) && near((*c)[2], mac->b, 4. / 255), "The tile colour matches the Mac's within its resampling");
}

// --- Canvas rules (Swift-derived) -----------------------------------------------------------
void canvasRules(const m::AppShortcutArtwork& art) {
    FakeMeasurer measurer; World w(measurer); w.appearance(Language::english);
    Owner owner;
    m::AppShortcutCanvas canvas(w.context, art, owner.services());
    std::vector<std::string> saved, removed;
    canvas.onSaved = [&](const m::ShortcutRecord& r) { saved.push_back(r.name); };
    canvas.onRemoved = [&](const std::string& name) { removed.push_back(name); };
    // Store unavailable (:27-37): Add App disabled, the error toasts on activation, imports refuse.
    canvas.setStoreError("Synthetic failure");
    w.show(canvas);
    check(!control(canvas, "apps:add")->enabled() && w.toast() && w.toast()->caption == "Synthetic failure", "Without a store Add App is disabled and the error toasts");
    const std::array one{std::string("C:\\A.exe")};
    check(!canvas.importPaths(one) && owner.requests.empty() && w.toast()->caption == "Shortcut storage is unavailable.", "importURLs without a store (:73)");
    w.context.overlay().clearToast(); canvas.setActive(false, w.now);
    // A store: header, rows (:99-145).
    canvas.setStore(fileOf({record("A", "Alpha", "original"), record("B", "Bravo", "camera"), record("M", "Mac import", "star", false)}));
    w.show(canvas);
    check(control(canvas, "apps:add")->enabled() && canvas.header().counterText() == "03" && canvas.header().crumbTexts().back() == "SHORTCUTS", "Counter pad2(count), Add App enabled");
    const auto* field = dynamic_cast<d::TextField*>(control(canvas, "apps:A:name"));
    check(field && field->options().maximumLength == std::optional<std::size_t>(24) && field->options().light, "The name field: light, 24 characters (:23, :177)");
    check(control(canvas, "apps:B:icon")->layer().frame().y == 9 && control(canvas, "apps:B:open")->layer().frame().x == 590 - 10 - 30 - 8 - 30,
          "Tile at (16, 9); Open 8 px left of Remove, 10 from the right (:149-185)");
    // Rename (:202-208): trimmed; empty restores; unchanged sends nothing.
    auto* a = dynamic_cast<d::TextField*>(control(canvas, "apps:A:name"));
    a->onCommit("  "); check(a->text() == "Alpha" && owner.requests.empty(), "An empty name restores the saved one");
    a->onCommit(" Alpha "); check(owner.requests.empty(), "An unchanged name saves nothing");
    a->onCommit(" Alpha Prime\n");
    check(owner.requests.size() == 1 && owner.requests[0].kind == m::AppShortcutRequest::Kind::save && owner.requests[0].name == "Alpha Prime" && owner.requests[0].icon == "original",
          "A new name saves with the record's preset");
    canvas.requestFinished(owner.requests[0], fileOf({record("A", "Alpha Prime", "original"), record("B", "Bravo", "camera"), record("M", "Mac import", "star", false)}), std::nullopt);
    check(saved == std::vector<std::string>{"Alpha Prime"} && canvas.items()[0].name == "Alpha Prime", "The committed rename reloads and notifies the shell (:216-219)");
    owner.requests.clear();
    // Choose icon (:192-200): the 14 presets as a grid; picking the current one saves nothing.
    control(canvas, "apps:B:icon")->activate();
    check(canvas.menuOpen(), "The tile opens Choose icon");
    control(canvas, "apps:B:icon")->activate();
    check(!canvas.menuOpen(), "Pressing the tile again closes it");
    // Open (:166-169) and the Windows relink of an imported record.
    control(canvas, "apps:B:open")->activate();
    check(owner.launches == std::vector<std::string>{"B"} && w.toast() && w.toast()->title == "Opening" && w.toast()->caption == "Bravo", "Open launches and toasts Opening");
    w.context.overlay().clearToast();
    control(canvas, "apps:M:open")->activate();
    check(owner.launches.size() == 1 && owner.chooses == 1 && canvas.chooserOpen() && canvas.presentingPanel() && canvas.inputLocked(), "An imported record relinks through the chooser");
    canvas.chooserFinished(std::vector<std::string>{"C:\\Apps\\Relink.exe"});
    check(owner.requests.size() == 1 && owner.requests[0].kind == m::AppShortcutRequest::Kind::add && owner.requests[0].id == "M" && owner.requests[0].path == "C:\\Apps\\Relink.exe",
          "The chosen application relinks that record");
    canvas.requestFinished(owner.requests[0], fileOf({record("A", "Alpha Prime", "original"), record("B", "Bravo", "camera"), record("M", "Mac import", "star")}), std::nullopt);
    check(!w.toast() && saved.back() == "Mac import", "A relink updates the row without App added");
    owner.requests.clear();
    // Add App (:241-268): one chooser; a cancelled one changes nothing.
    control(canvas, "apps:add")->activate();
    check(owner.chooses == 2 && canvas.chooserOpen(), "Add App opens the chooser");
    canvas.chooserFinished(std::nullopt);
    check(!canvas.chooserOpen() && owner.requests.empty(), "A cancelled chooser adds nothing");
    owner.chooserBusy = true; control(canvas, "apps:add")->activate(); owner.chooserBusy = false;
    check(!canvas.chooserOpen(), "A chooser that cannot open leaves the screen interactive");
    // importURLs (:71-95): exactly one application; the answer toasts.
    const std::array two{std::string("C:\\A.exe"), std::string("C:\\B.exe")};
    check(!canvas.importPaths(two) && w.toast()->caption == "Choose one application to customize.", "Two applications ask for one (:74-76)");
    w.context.overlay().clearToast();
    const std::array gamma{std::string("C:\\Gamma.exe")};
    check(canvas.importPaths(gamma) && owner.requests.back().kind == m::AppShortcutRequest::Kind::add && owner.requests.back().id.empty(), "One application is added");
    auto added = fileOf({record("A", "Alpha Prime", "original"), record("B", "Bravo", "camera"), record("M", "Mac import", "star"), record("G", "Gamma", "original")});
    canvas.requestFinished(owner.requests.back(), added, std::nullopt);
    check(w.toast() && w.toast()->title == "App added" && w.toast()->caption.empty() && saved.back() == "Gamma" && canvas.header().counterText() == "04",
          "A saved app toasts App added and notifies the shell (:78-85)");
    w.context.overlay().clearToast();
    owner.requests.clear();
    // A failed request toasts its error (:85, :224-226).
    check(canvas.importPaths(gamma), "A duplicate is asked");
    canvas.requestFinished(owner.requests.back(), std::nullopt, std::string("This application is already saved. Edit its existing shortcut instead."));
    check(w.toast() && w.toast()->title == "Notice" && w.toast()->caption.starts_with("This application"), "The store error toasts Notice");
    w.context.overlay().clearToast(); owner.requests.clear();
    // Ctrl+V (:277-284): application files only; none leaves the key alone.
    owner.clipboard = {"C:\\readme.txt"};
    check(!canvas.keyDown(ctrl(U'v'), w.now) && owner.requests.empty(), "Ctrl+V without application files is left alone");
    owner.clipboard = {"C:\\readme.txt", "C:\\Delta.lnk"};
    check(canvas.keyDown(ctrl(U'v'), w.now) && owner.requests.size() == 1 && owner.requests[0].path == "C:\\Delta.lnk", "Ctrl+V adds the copied application");
    d::KeyInput shifted = ctrl(U'v'); shifted.shift = true;
    check(!canvas.keyDown(shifted, w.now), "Ctrl+Shift+V is not the paste");
    canvas.requestFinished(owner.requests[0], std::nullopt, std::string("x")); w.context.overlay().clearToast(); owner.requests.clear();
    // Drop (:52-68).
    check(canvas.acceptsApplications(gamma) && !canvas.acceptsApplications(two) && !canvas.acceptsApplications(std::array{std::string("a.txt")}), "acceptsApplications: one application");
    canvas.setDropTarget(true);
    check(canvas.dropTarget() && canvas.inputLocked(), "The drop target locks the plane");
    check(canvas.importDrop(gamma) && owner.requests.size() == 1, "A dropped application is added");
    canvas.setDropTarget(false); canvas.requestFinished(owner.requests[0], std::nullopt, std::string("x")); w.context.overlay().clearToast(); owner.requests.clear();
    // Remove (:224-237): the confirm bar, then the store; Esc sends nothing.
    control(canvas, "apps:G:remove")->activate();
    const auto& request = w.context.overlay().confirmState().request();
    check(request && request->id == "apps:remove" && request->title == "Remove Gamma?" && request->message == "The app itself stays installed." && request->danger &&
          request->glyph() == d::Icon::trash && request->confirmTitle == "Remove", "Remove asks through the confirm bar");
    d::KeyInput escape; escape.virtualKey = d::key::escape;
    w.context.overlay().handleKey(escape);
    check(owner.requests.empty(), "Esc keeps the shortcut");
    w.now += 1; w.context.advance(w.now);
    control(canvas, "apps:G:remove")->activate();
    w.context.overlay().decide(d::ConfirmState::Decision::confirm);
    w.now += 1; w.context.advance(w.now);
    check(owner.requests.size() == 1 && owner.requests[0].kind == m::AppShortcutRequest::Kind::remove && owner.requests[0].id == "G", "Confirm removes after 120 ms");
    canvas.requestFinished(owner.requests[0], fileOf({record("A", "Alpha Prime", "original"), record("B", "Bravo", "camera"), record("M", "Mac import", "star")}), std::nullopt);
    check(removed == std::vector<std::string>{"Gamma"} && w.toast() && w.toast()->title == "Shortcut removed", "A removed shortcut toasts and notifies the shell");
    // Deactivation closes the chooser and the drop target (:35).
    control(canvas, "apps:add")->activate();
    canvas.setActive(false, w.now);
    check(!canvas.chooserOpen() && owner.cancels == 1, "Leaving the module closes the chooser");
}

// --- The Mac oracle -------------------------------------------------------------------------
struct Oracle {
    const Json& data;
    RecordedMeasurer measurer;
    std::vector<std::string> failures;
    std::size_t compared{};
    explicit Oracle(const Json& d) : data(d) {
        recordText(measurer, d["text"]);
        for (const auto& row : d["states"].array()) if (recordMicroTag(measurer, row["layer"])) break;
    }
    const Json* row(std::string_view state, std::string_view language = "en", bool light = false) const {
        for (const auto& r : data["states"].array())
            if (r["state"].string() == state && r["language"].string() == language && r["light"].boolean() == light) return &r;
        return nullptr;
    }
    static Json rooted(const Json& layer) {
        Json copy = layer;
        Point anchor{.5, .5};
        if (copy["anchorPoint"].isArray()) anchor = {copy["anchorPoint"].array()[0].number(), copy["anchorPoint"].array()[1].number()};
        const Rect b = rectOf(copy["bounds"]);
        copy["position"] = Json::Array{anchor.x * b.width + b.x, anchor.y * b.height + b.y};
        copy["transform"] = Json();
        return copy;
    }
    void scene(std::string_view state, m::AppShortcutCanvas& canvas, World& w, std::string_view language = "en", bool light = false) {
        const Json* r = row(state, language, light);
        const std::string label = std::string(state) + "/" + std::string(language) + (light ? "/light" : "");
        if (!r) { failures.push_back(label + ": not in the oracle"); return; }
        ++compared;
        Json expected = *r;
        expected["layer"] = rooted((*r)["layer"]);
        compare(label, expected, canvas.root(), canvas.root(), canvas.page().controls(), failures);
        // The name fields: the Mac lists a HUDDesignTextField as a button; the
        // library's UIA tree names it an edit (Edit + Value, LIBRARY-DESIGN.md section 9).
        Json::Array rows = (*r)["accessibility"].array();
        for (auto& item : rows) if (item["id"].string().ends_with(":name") && item["role"].string() == "button") item["role"] = "edit";
        std::vector<d::AccessibilityItem> items; canvas.accessibilityItems(items);
        compareAccessibility(label, Json(std::move(rows)), items, failures);
        if ((*r)["top"].isObject()) compare(label + "/top", Json::Object{{"layer", (*r)["top"]}}, canvas.top(), canvas.root(), {}, failures);
        if (canvas.header().counterText() != (*r)["header"]["counter"].string()) failures.push_back(label + ": counter " + canvas.header().counterText());
        if (canvas.dropTarget() != (*r)["dropTarget"].boolean() || canvas.inputLocked() != (*r)["inputLocked"].boolean()) failures.push_back(label + ": drop target / input lock");
        const auto toast = w.toast();
        if ((*r)["toast"].isObject() != toast.has_value() || (toast && (toast->title != (*r)["toast"]["title"].string() || toast->caption != (*r)["toast"]["caption"].string()
                || d::iconName(toast->icon) != (*r)["toast"]["icon"].string()))) failures.push_back(label + ": toast");
        const auto& confirm = w.context.overlay().confirmState();
        if ((*r)["confirm"].isObject() != confirm.isOpen()) failures.push_back(label + ": confirm bar");
        else if (confirm.isOpen()) {
            const auto& c = (*r)["confirm"]; const auto& q = *confirm.request();
            if (q.id != c["id"].string() || q.title != c["title"].string() || q.message != c["message"].string() || q.confirmTitle != c["confirm"].string()
                || q.danger != c["danger"].boolean() || d::iconName(q.glyph()) != c["icon"].string()) failures.push_back(label + ": confirm request");
        }
        for (const auto& o : (*r)["offsets"].array())
            for (const auto& area : canvas.page().scrollAreas())
                if (area->id() == o["id"].string() && (!near(area->offset(), o["offset"].number()) || !near(area->maximumOffset(), o["maximum"].number())))
                    failures.push_back(label + ": scroll offset");
    }
    m::ShortcutFile file(std::string_view state, Owner& owner) const {
        m::ShortcutFile f;
        for (const auto& item : (*row(state))["input"]["items"].array()) {
            f.items.push_back(record(item["id"].string(), item["name"].string(), item["preset"].string()));
            owner.icons[item["id"].string()] = item["hasIcon"].boolean();
            if (const auto c = rgbaOf(item["tileColor"])) owner.colors[item["id"].string()] = *c;
        }
        return f;
    }
};
void canvasOracle(Oracle& o, const m::AppShortcutArtwork& art) {
    World w(o.measurer); w.appearance(Language::english);
    {
        Owner owner; m::AppShortcutCanvas canvas(w.context, art, owner.services());
        canvas.setStore({}); w.show(canvas); o.scene("empty", canvas, w);
        canvas.setActive(false, w.now);
    }
    {
        Owner owner; m::AppShortcutCanvas canvas(w.context, art, owner.services());
        canvas.setStoreError("Synthetic store failure"); w.show(canvas); o.scene("unavailable", canvas, w);
        w.context.overlay().clearToast(); canvas.setActive(false, w.now);
    }
    Owner owner; m::AppShortcutCanvas canvas(w.context, art, owner.services());
    canvas.setStore(o.file("list", owner)); w.show(canvas);
    o.scene("list", canvas, w);
    const auto& items = canvas.items();
    canvas.pointerMoved(centre(*control(canvas, "apps:" + items[0].id + ":icon"), canvas.root()), false, w.now);
    o.scene("hoverTile", canvas, w);
    canvas.pointerMoved(std::nullopt, false, w.now);
    control(canvas, "apps:" + items[1].id + ":icon")->activate();
    o.scene("iconMenu", canvas, w);
    canvas.closeMenu();
    canvas.setDropTarget(true); o.scene("dropTarget", canvas, w); canvas.setDropTarget(false);
    control(canvas, "apps:" + items[2].id + ":open")->activate();
    o.scene("opening", canvas, w);
    check(owner.launches == std::vector<std::string>{(*o.row("opening"))["launched"].array()[0].string()}, "Open launches the Mac's app");
    w.context.overlay().clearToast();
    control(canvas, "apps:" + items[3].id + ":remove")->activate();
    o.scene("confirmRemove", canvas, w);
    w.context.overlay().dismissConfirm();
    w.appearance(Language::english, true); w.show(canvas); o.scene("list", canvas, w, "en", true);
    w.appearance(Language::english); w.show(canvas);
    w.appearance(Language::simplifiedChinese); canvas.setActive(false, w.now); w.show(canvas); o.scene("list", canvas, w, "zh-Hans");
    w.appearance(Language::english); canvas.setActive(false, w.now); w.show(canvas);
    canvas.reload(o.file("long", owner)); o.scene("long", canvas, w);
    const std::array foxtrot{std::string("C:\\Applications\\Foxtrot.exe")};
    check(canvas.importPaths(foxtrot), "The added app is asked of the store");
    canvas.requestFinished(owner.requests.back(), o.file("added", owner), std::nullopt);
    o.scene("added", canvas, w);
    w.context.overlay().clearToast();
    const std::array two{foxtrot[0], std::string("C:\\Applications\\Alpha.exe")};
    (void)canvas.importPaths(two);
    o.scene("twoApps", canvas, w);
    w.context.overlay().clearToast();
    canvas.setActive(false, w.now);
}
}

int main(int argc, char** argv) {
    try {
        layoutRules();
        if (argc < 3) throw std::runtime_error("usage: app_shortcut_canvas_tests ORACLE RESOURCES");
        const auto packaged = assets(argv[2]);
        canvasRules(packaged->artwork());
        if (const auto data = loadOracle(argv[1], {"states"}, {"text", "layerDefaults", "presets", "glyphFrames", "trim"})) {
            artworkOracle(*data, packaged->artwork(), argv[2]);
            Oracle oracle(*data);
            canvasOracle(oracle, packaged->artwork());
            std::cout << "oracle: " << oracle.compared << " scenes compared, " << oracle.measurer.misses.size() << " unrecorded measurements\n";
            for (const auto& miss : oracle.measurer.misses) std::cout << "  unrecorded: " << miss << '\n';
            for (const auto& f : oracle.failures) std::cout << "  MISMATCH " << f << '\n';
            check(oracle.failures.empty(), std::to_string(oracle.failures.size()) + " oracle mismatches");
        } else std::cout << "app-shortcut-source.json not present: Swift-derived contracts only\n";
        std::cout << "app_shortcut_canvas_tests: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "app_shortcut_canvas_tests failed after " << checks << " checks: " << e.what() << '\n';
        return 1;
    }
}
