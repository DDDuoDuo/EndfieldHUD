// Contracts of the redesigned Media Assembly canvas (modules/media_assembly_canvas.*,
// media_assembly_controls.*, the viewport geometry of media_assembly_model.*):
// the rules of Sources/MediaAssemblyCanvas.swift, MediaAssemblyControls.swift
// and MediaAssemblyViewport.swift over a synthetic engine on an inline
// executor, and, when the Mac oracle is given
// (tests/fixtures/media-assembly-source.json from tools/media_assembly_reference.sh),
// the viewport tables (zoom(by:), dragCrop, the crop L-handles, the sticker
// handle hit test, the picture transform), the parameter tables, and every
// recorded canvas state replayed with the Mac's own inputs and compared node
// by node with its text widths, controls, accessibility items, header,
// chips, confirm requests and toasts. Synthetic data only: no file is read.
#include "modules/media_assembly_canvas.hpp"
#include "tests/hud_design_oracle.hpp"
#include <deque>
#include <iostream>
#include <stdexcept>

namespace d = endfield::modules::design;
namespace m = endfield::modules;
using endfield::core::Language;
using namespace endfield::tests::design_oracle;
namespace {
unsigned checks{};
void check(bool v, const std::string& what) { ++checks; if (!v) throw std::runtime_error(what); }
d::KeyInput keyOf(std::uint32_t vk, char32_t c = 0, bool shift = false, bool alt = false) {
    d::KeyInput k; k.virtualKey = vk; k.character = c; k.shift = shift; k.alt = alt; return k;
}
// The rasterizer refuses a negative width or height in any node rect (native/layer_raster.cpp rectangle()).
void negativeRects(const Json& n, const std::string& where, std::vector<std::string>& out) {
    for (const char* key : {"bounds", "contentsRect", "contentsCenter"})
        if (n[key].isArray() && n[key].array().size() == 4 && (n[key].array()[2].number() < 0 || n[key].array()[3].number() < 0)) out.push_back(where + " " + key);
    if (n["children"].isArray()) for (const auto& c : n["children"].array()) negativeRects(c, where + "/" + (c["id"].isString() ? c["id"].string() : std::string("node")), out);
}
void negativeRects(const d::Layer& l, std::vector<std::string>& out) {
    if (const auto& node = l.content()) negativeRects(*node, l.id(), out);
    if (l.bounds().width < 0 || l.bounds().height < 0) out.push_back(l.id() + " layer bounds");
    if (const auto* t = l.text(); t && (t->lineFrame.width < 0 || t->lineFrame.height < 0)) out.push_back(l.id() + " text line " + str(t->lineFrame));
    for (const auto* c : l.children()) negativeRects(*c, out);
}
Language languageOf(std::string_view tag) {
    return tag == "zh-Hans" ? Language::simplifiedChinese : tag == "ja" ? Language::japanese : Language::english;
}

struct World {
    d::Context context;
    double now{100};
    explicit World(d::TextMeasurer& m) : context(m, d::asciiUppercase) { context.update(d::geometry::baseUnit, 2 / d::geometry::baseUnit, now); }
    void appearance(Language language, bool light = false, bool reduce = true) {
        d::Context::Appearance a; a.language = language; a.light = light; a.reduceMotion = reduce;
        context.apply(a, now);
    }
    void advance(double seconds) { now += seconds; context.advance(now); }
};
// The app's utility executor, run inline when the test lets it.
struct Executor {
    struct Job { std::function<void()> work; std::function<void(std::exception_ptr)> done; };
    std::deque<Job> jobs;
    m::MediaAssemblySubmit submit() { return [this](std::function<void()> w, std::function<void(std::exception_ptr)> d) { jobs.push_back({std::move(w), std::move(d)}); return true; }; }
    void run() { while (!jobs.empty()) { auto j = std::move(jobs.front()); jobs.pop_front(); std::exception_ptr e; try { j.work(); } catch (...) { e = std::current_exception(); } j.done(e); } }
};
// The oracle's two sources: an 800 x 500 still and the owned 64 x 64, 1.2 s
// movie; previews are the rotated, cropped frame (pixels are never compared).
struct Engine final : m::MediaAssemblyEngine {
    m::MediaAssemblyDocumentInfo open(const std::string& path) override {
        const bool video = path.ends_with(".mp4");
        m::MediaAssemblyDocumentInfo d{"", video, video ? endfield::core::Point{64, 64} : endfield::core::Point{800, 500}, video ? 1.2 : 0.};
        d.path = path; d.name = path.substr(path.find_last_of('\\') + 1); d.container = video ? "video" : "png";
        d.kind = video ? m::MediaAssemblyKind::video : m::MediaAssemblyKind::image;
        return d;
    }
    m::MediaAssemblyPreviewImage preview(const m::MediaAssemblyDocumentInfo& d, const m::MediaAssemblyAdjustments& a, double) override {
        const bool odd = (a.rotationQuarterTurns % 2 + 2) % 2 == 1;
        const double fw = odd ? d.pixels.y : d.pixels.x, fh = odd ? d.pixels.x : d.pixels.y;
        const bool swap = odd; const double cw = swap ? a.crop.height : a.crop.width, ch = swap ? a.crop.width : a.crop.height;
        const unsigned w = unsigned(std::lround(fw * cw)), h = unsigned(std::lround(fh * ch));
        return {w, h, std::vector<std::uint8_t>(std::size_t(w) * h * 4, 128)};
    }
    std::string exportMedia(const m::MediaAssemblyExportRequest&, m::MediaAssemblyExportTicket&) override { throw m::MediaAssemblyFailure(m::MediaAssemblyError::exportFailed); }
    void clearCaches() noexcept override {}
    bool heicEncoder() const override { return false; }
    bool samePath(const std::string& a, const std::string& b) const override { return a == b; }
};
// One editor: the session controller, the canvas and its recorded services.
struct Editor {
    World& w;
    Executor q;
    std::shared_ptr<Engine> engine{std::make_shared<Engine>()};
    std::unique_ptr<m::MediaAssemblyController> controller;
    std::unique_ptr<m::MediaAssemblyCanvas> canvas;
    std::vector<std::string> panels, thumbnails;
    int ids{};
    explicit Editor(World& world) : w(world) {
        controller = std::make_unique<m::MediaAssemblyController>(engine, q.submit(),
            m::MediaAssemblyController::Callbacks{[this] { if (canvas) canvas->controllerChanged(); }, {}, [this] { return "uuid-" + std::to_string(++ids); }});
        m::MediaAssemblyCanvasServices s;
        s.now = [this] { return w.now; };
        s.uuid = [this] { return "sticker-" + std::to_string(++ids); };
        s.shelfChoices = [] { return std::vector<m::MediaAssemblyShelfChoice>{{"00000000-0000-0000-0000-000000000001", "Shelf still.png", true, true},
            {"00000000-0000-0000-0000-000000000002", "Missing clip.mov", true, false}, {"00000000-0000-0000-0000-000000000003", "notes.txt", false, true}}; };
        s.shelfAccess = [](std::string_view) { return std::optional<m::MediaAssemblyShelfFile>{}; };
        s.chooseFile = [this] { panels.push_back("open"); return true; };
        s.saveAs = [this](std::string folder, std::string filename, std::vector<std::string>) { panels.push_back("save " + folder + "|" + filename); return true; };
        s.cancelPanel = [this] { panels.push_back("cancel"); };
        s.fileExists = [](const std::string&) { return false; };
        s.samePath = [](const std::string& a, const std::string& b) { return a == b; };
        s.folderOf = [](const std::string& p) { return p.substr(0, p.find_last_of('\\')); };
        s.filterThumbnail = [this](m::MediaAssemblyFilter f) -> std::optional<d::ImageSource> {
            if (f == m::MediaAssemblyFilter::none) return std::nullopt;
            thumbnails.push_back(std::string(m::mediaAssemblyFilterItem(f).id));
            return d::ImageSource{d::ImageSource::Kind::asset, "filter-icons/" + std::string(m::mediaAssemblyFilterItem(f).id) + ".png", {}, 0}; };
        s.stickerThumbnail = [this](m::MediaAssemblyStickerKind k) -> std::optional<d::ImageSource> {
            thumbnails.push_back(std::string(m::mediaAssemblyStickerItem(k).id));
            return d::ImageSource{d::ImageSource::Kind::memory, "media.assembly.thumbnail." + std::string(m::mediaAssemblyStickerItem(k).id), {}, 1}; };
        s.stickerImage = [](m::MediaAssemblyStickerKind k) -> std::optional<d::ImageSource> {
            return d::ImageSource{d::ImageSource::Kind::asset, "stickers/" + std::string(m::mediaAssemblyStickerItem(k).id) + ".png", {}, 0}; };
        canvas = std::make_unique<m::MediaAssemblyCanvas>(w.context, *controller, std::move(s));
    }
    ~Editor() { canvas.reset(); }
    void show() { canvas->show(w.now); canvas->setActive(true, w.now); }
    bool perform(std::string_view id) { const bool done = canvas->perform(id, w.now); canvas->settleSwap(); return done; }
    // The Mac's spin: the worker's previews arrive, the page and the stage settle.
    void spin() { for (int n = 0; n < 8; ++n) { q.run(); w.advance(.05); canvas->settleSwap(); } }
    void import(std::string path) { const std::string paths[]{std::move(path)}; canvas->importFiles(paths, w.now); spin(); }
    bool key(const d::KeyInput& k) { return canvas->keyDown(k, w.now); }
};

// --- Swift-derived rules (no oracle) ---------------------------------------------------
void rules(d::TextMeasurer& measurer) {
    World w(measurer); w.appearance(Language::japanese);
    Editor e(w); e.show();
    auto& c = *e.canvas;
    std::vector<d::AccessibilityItem> items; c.accessibilityItems(items);
    const auto label = [&](std::string_view id) { for (const auto& i : items) if (i.id == id) return i.label; return std::string(); };
    check(label("mediaTab:1") == "フィルタ" && label("mediaTab:2") == "スタンプ", "Japanese tab names come from the catalog (フィルタ / スタンプ)");
    check(c.counterText() == "EMPTY" && c.stage().isEmpty() && !c.isEditable(), "No source: EMPTY, the empty stage, nothing editable");
    check(e.perform("open") && c.menuOpen(), "Open shows the source menu (:520-533)");
    items.clear(); c.accessibilityItems(items);
    check(label("open:finder") == "エクスプローラーで選択", "Windows offers File Explorer where the Mac offers Finder");
    check(e.perform("open:shelf") && c.menuOpen(), "Choose from Shelf lists the Shelf (:536-546)");
    items.clear(); c.accessibilityItems(items);
    const auto enabled = [&](std::string_view id) { for (const auto& i : items) if (i.id == id) return std::optional(i.enabled); return std::optional<bool>(); };
    check(enabled("shelf:00000000-0000-0000-0000-000000000001") == true && enabled("shelf:00000000-0000-0000-0000-000000000002") == false
        && !enabled("shelf:00000000-0000-0000-0000-000000000003"), "Shelf rows: supported only; a missing file is listed disabled");
    c.closePopups();
    w.appearance(Language::english);
    e.import("C:\\oracle\\fixture.png");
    check(c.counterText() == "IMG 01" && c.isEditable() && e.controller->session().document(), "A still: IMG 01, editable");
    // Bounded thumbnails: the drawer asks for each 96 px sticker thumbnail once it is shown.
    e.thumbnails.clear();
    check(e.perform("mediaTab:2") && c.tab() == m::MediaAssemblyCanvas::Tab::stickers, "Stickers tab");
    check(std::count(e.thumbnails.begin(), e.thumbnails.end(), "sticker_bp_1") == 1, "The drawer draws the 96 px thumbnail (stickerThumbnail), not the sticker image");
    check(e.perform("sticker:sticker_bp_1") && e.controller->session().adjustments().stickers.size() == 1 && !c.selectedSticker().empty(), "A sticker is added selected");
    const auto before = e.controller->session().adjustments().stickers[0];
    check(e.key(keyOf(d::key::right)) && e.controller->session().adjustments().stickers[0].x > before.x, "Right moves the selected sticker 1 %");
    check(e.key(keyOf(d::key::right, 0, true, true)) && std::abs(e.controller->session().adjustments().stickers[0].rotation - 15) < 1e-9, "Alt+Shift+Right turns it 15° (⌥ is Alt)");
    d::KeyInput ctrl = keyOf(d::key::right); ctrl.control = true;
    check(!e.key(ctrl) && std::abs(e.controller->session().adjustments().stickers[0].rotation - 15) < 1e-9, "Ctrl (the Mac's ⌃ / ⌘) is not handled");
    check(e.key(keyOf(d::key::back)) && e.controller->session().adjustments().stickers.empty() && c.selectedSticker().empty(), "Backspace removes the selected sticker");
    // MODS-131: in Crop a sticker is placed and hit against the crop box.
    auto a = e.controller->session().adjustments();
    a.crop = {.5, 0, .5, 1}; a.stickers.push_back({"s1", m::MediaAssemblyStickerKind::sticker1, .5, .5, .2, 0});
    e.controller->update(a, std::nullopt, w.now); e.spin();
    check(e.perform("mediaTab:0") && e.perform("tool:crop") && c.tool() == m::MediaAssemblyTool::crop, "Crop opens");
    e.spin();
    check(e.controller->previewImage() && e.controller->previewImage()->width == 800, "Crop shows the whole picture (setCropPreview)");
    const auto image = c.imageRect(), crop = c.displayCropRect();
    const d::Point cropCentre{m::MediaAssemblyCanvas::stageOrigin.x + image.x + (crop.x + crop.width / 2) * image.width,
                              m::MediaAssemblyCanvas::stageOrigin.y + image.y + (crop.y + crop.height / 2) * image.height};
    d::PointerInput press; press.clickCount = 1;
    c.pointerDown(cropCentre, press, w.now); c.pointerUp(cropCentre, w.now);
    check(c.selectedSticker() == "s1", "A sticker at the crop box's centre is hit there in Crop (MODS-131)");
    // MODS-142: the crop edges are announced in display space after a turn.
    check(e.perform("cropDone") && e.perform("tool:rotate"), "Rotate"); e.spin();
    check(e.perform("tool:crop"), "Crop again");
    items.clear(); c.accessibilityItems(items);
    const auto number = [&](std::string_view id) { for (const auto& i : items) if (i.id == id) return i.number; return -1.0; };
    const auto shown = m::MediaAssemblyViewport::displayCrop(e.controller->session().adjustments().crop, 1, false);
    check(std::abs(number("crop.edge1") - shown.y) < 1e-9 && std::abs(number("crop.edge3") - (shown.y + shown.height)) < 1e-9, "Crop edges follow the turned picture (MODS-142)");
    check(c.adjustAccessibilitySlider("crop.edge0", .2, w.now) && std::abs(c.displayCropRect().x - .2) < 1e-9, "A crop edge slider moves that displayed edge");
    check(e.perform("cropDone"), "Crop done");
    // Close with edits asks on the shared confirm bar; Cancel keeps the source.
    check(e.perform("close") && w.context.overlay().confirmState().request() && w.context.overlay().confirmState().request()->id == "mediaClose", "Close asks first when edited");
    w.context.overlay().decide(d::ConfirmState::Decision::cancel);
    check(e.controller->session().document() != nullptr, "Cancel keeps the source");
    check(e.perform("close"), "Close again");
    w.context.overlay().decide(d::ConfirmState::Decision::confirm); e.spin();
    check(!e.controller->session().document() && c.counterText() == "EMPTY", "Confirm closes it");
}

// Animated page swaps and edits (Reduce Motion off): every frame's nodes stay rasterizable.
void motion(d::TextMeasurer& measurer) {
    World w(measurer); w.appearance(Language::english, false, false);
    Editor e(w); e.show();
    e.import("C:\\oracle\\fixture.png");
    std::vector<std::string> negative;
    const auto frames = [&](double seconds, const std::string& step) {
        for (double t = 0; t < seconds; t += .02) {
            w.advance(.02); e.q.run();
            negativeRects(e.canvas->root(), negative);
            for (const auto& n : negative) throw std::runtime_error(step + ": negative rect " + n);
        }
    };
    for (const auto* id : {"tool:adjust", "toolBack", "tool:levels", "toolBack", "tool:curves", "toolBack", "tool:crop", "cropDone", "tool:rotate", "tool:mirror",
                           "mediaTab:1", "filter:sp_filter_2", "mediaTab:2", "sticker:sticker_1", "mediaTab:0", "zoomIn", "zoomReset", "tool:adjust"}) {
        e.canvas->perform(id, w.now); frames(.5, id);
    }
    check(e.key(keyOf(d::key::escape)) && !e.canvas->tool(), "Escape leaves Adjust");
    frames(.5, "Escape from Adjust");
    check(true, "Animated swaps keep every node rect non-negative");
}

// --- The Mac oracle -------------------------------------------------------------------
struct Oracle {
    const Json& data;
    RecordedMeasurer measurer;
    std::vector<std::string> failures;
    std::size_t compared{};
    explicit Oracle(const Json& d) : data(d) { recordText(measurer, d["text"]); }
    void fail(std::string s) { failures.push_back(std::move(s)); }
    static Json rooted(const Json& layer) {
        Json copy = layer;
        Point anchor{.5, .5};
        if (copy["anchorPoint"].isArray()) anchor = {copy["anchorPoint"].array()[0].number(), copy["anchorPoint"].array()[1].number()};
        const Rect b = rectOf(copy["bounds"]);
        copy["position"] = Json::Array{anchor.x * b.width + b.x, anchor.y * b.height + b.y};
        copy["transform"] = Json();
        return copy;
    }
    // Windows names File Explorer where the Mac names Finder.
    static Json windowsNames(const Json& value) {
        if (value.isString()) {
            std::string text = value.string();
            for (const auto& [mac, windows] : std::vector<std::pair<std::string, std::string>>{{"Choose in Finder", "Choose in File Explorer"},
                    {"从 Finder 选择", "从文件资源管理器选择"}, {"Finderから選択", "エクスプローラーで選択"}})
                if (text == mac) text = windows;
            return Json(text);
        }
        if (value.isArray()) { Json::Array out; for (const auto& v : value.array()) out.push_back(windowsNames(v)); return Json(std::move(out)); }
        if (value.isObject()) { Json::Object out; for (const auto& [k, v] : value.object()) out[k] = windowsNames(v); return Json(std::move(out)); }
        return value;
    }
    static Json numbers(std::initializer_list<double> v) { Json::Array out; for (double x : v) out.push_back(x); return Json(std::move(out)); }
    static bool same(const Json& a, const Json& b) {
        if (a.isNumber() && b.isNumber()) return near(a.number(), b.number(), 1e-6);
        if (a.isArray() && b.isArray()) {
            if (a.array().size() != b.array().size()) return false;
            for (std::size_t i = 0; i < a.array().size(); ++i) if (!same(a.array()[i], b.array()[i])) return false;
            return true;
        }
        return a == b;
    }
    // One canvas state: the tree (the owner-drawn picture and the replicated
    // dot grid checked by geometry), controls, accessibility and the state.
    void scene(const Json& r, Editor& e) {
        auto& c = *e.canvas;
        const std::string label = "media/" + r["state"].string() + "/" + r["language"].string() + (r["light"].boolean() ? "/light" : "");
        ++compared;
        Json expected = windowsNames(r);
        expected["layer"] = rooted(expected["layer"]);
        const bool menuOpen = expected["top"].isObject();
        std::vector<std::string> found;
        compare(label, expected, c.root(), c.root(), c.page().controls(), found);
        // The preview is a draw record of the owner on Windows (the picture
        // layer has no contents); the Mac's replicated grid is one dot.
        // The open source menu is as wide as its longest title ("Choose in File
        // Explorer"): its rows are compared by title below, not by geometry.
        const bool sourceMenu = menuOpen && r["state"].string() == "menu.open";
        // The Mac's disabled tabs announce no selection (MediaAssemblyCanvas
        // keeps no HUDTabs while nothing is editable, so the weak bar is gone):
        // a Mac quirk not copied; Windows still announces the selected tab.
        const bool tabsDisabled = !r["editable"].boolean();
        const auto tolerated = [&](const std::string& f) {
            return (f.find("Mac draws artwork") != std::string::npos && f.ends_with("/media.picture")) || f.find("media.grid") != std::string::npos
                || (sourceMenu && (f.find("design.top/menu") != std::string::npos || f.find("@open.") != std::string::npos))
                || (tabsDisabled && f.find("state mediaTab:") != std::string::npos);
        };
        std::erase_if(found, tolerated);
        for (auto& f : found) fail(std::move(f));
        std::vector<Primitive> mac; macPrimitives(expected["layer"], Affine{}, 1, mac, "");
        for (const auto& p : mac) if (p.kind == "artwork" && p.where.ends_with("/media.picture")) {
            // The owner draws the preview's unit quad at the picture layer's presented pose.
            const auto pose = d::sampleLayer(c.stage().picture(), c.root(), e.w.now);
            const Rect ours = bound(pose.world, {0, 0, 1, 1});
            if (!near(ours, p.rect, 2e-3) || std::abs(p.opacity - (pose.visible ? pose.opacity : 0)) > 2e-3) fail(label + ": picture " + str(ours) + " vs " + str(p.rect));
        }
        // The grid: CAReplicatorLayers of one 2.4 px dot on a 16 px pitch, ceil(448 / 16) x ceil(412 / 16).
        std::vector<Primitive> ours; layerPrimitives(c.root(), c.root(), 1, ours);
        const auto dot = std::find_if(mac.begin(), mac.end(), [](const Primitive& p) { return p.where.ends_with("media.grid.dot") && p.opacity > 1e-3; });
        const auto grid = std::find_if(ours.begin(), ours.end(), [](const Primitive& p) { return p.where.find("media.grid") != std::string::npos && p.opacity > 1e-3; });
        if ((dot == mac.end()) != (grid == ours.end())) fail(label + ": grid visibility");
        else if (dot != mac.end() && (!near(grid->rect, {dot->rect.x, dot->rect.y, 27 * 16 + dot->rect.width, 25 * 16 + dot->rect.height}, 2e-3)
                     || !grid->color || !near(*grid->color, *dot->color, 1.0 / 400) || std::abs(grid->opacity - dot->opacity) > 2e-3))
            fail(label + ": grid " + describe(*grid) + " vs dot " + describe(*dot));
        { std::vector<std::string> negative; negativeRects(c.root(), negative); for (const auto& n : negative) fail(label + ": negative rect " + n); }
        std::vector<d::AccessibilityItem> items; c.accessibilityItems(items);
        if (sourceMenu) {
            std::vector<std::string> titles; for (const auto& i : items) if (i.id.starts_with("open:")) titles.push_back(i.id + "=" + i.label);
            const auto lang = languageOf(r["language"].string());
            const std::vector<std::string> want{"open:finder=" + std::string(endfield::core::translated(endfield::core::chooseInFileExplorer, lang)),
                "open:shelf=" + std::string(lang == Language::english ? "Choose from Shelf" : lang == Language::japanese ? "シェルフから選択" : "从暂存架选择")};
            if (titles.size() != 2 || titles[0] != want[0]) fail(label + ": menu rows " + (titles.empty() ? std::string() : titles[0]));
            Json::Array kept;
            for (const auto& i : expected["accessibility"].array()) if (!i["id"].string().starts_with("open:")) kept.push_back(i);
            std::erase_if(items, [](const d::AccessibilityItem& i) { return i.id.starts_with("open:"); });
            expected["accessibility"] = Json(std::move(kept));
        }
        found.clear();
        compareAccessibility(label, static_cast<const Json&>(expected)["accessibility"], items, found);
        std::erase_if(found, tolerated);
        for (auto& f : found) fail(std::move(f));
        // The state.
        const auto& s = e.controller->session();
        const auto& a = s.adjustments();
        if (c.header().counterText() != r["header"]["counter"].string()) fail(label + ": counter " + c.header().counterText());
        if (double(int(c.tab())) != r["tab"].number()) fail(label + ": tab");
        if ((c.tool() ? Json(std::string(m::mediaAssemblyToolID(*c.tool()))) : Json()) != r["tool"]) fail(label + ": tool");
        if (c.isEditable() != r["editable"].boolean()) fail(label + ": editable");
        Json::Array chips; for (const auto& t : c.stage().chipTitles()) chips.push_back(t);
        if (Json(std::move(chips)) != r["chips"]) fail(label + ": chips");
        if ((c.stage().exportText() ? Json(*c.stage().exportText()) : Json()) != r["export"]) fail(label + ": export text");
        if (c.stage().isCropVisible() != r["crop"].boolean() || c.stage().isSelectionVisible() != r["selection"].boolean()) fail(label + ": crop / selection shown");
        if (!near(c.imageRect(), rectOf(r["imageRect"]), 1e-6)) fail(label + ": image rect " + str(c.imageRect()));
        if (!near(c.viewport().zoom(), r["zoom"].number()) || !same(numbers({c.viewport().pan().x, c.viewport().pan().y}), r["pan"])) fail(label + ": zoom / pan");
        if (c.selectedSticker().empty() == r["selected"].boolean()) fail(label + ": selection");
        const auto& doc = s.document();
        if (bool(doc) != r["document"].isObject()) fail(label + ": document");
        else if (doc && (doc->name != r["document"]["name"].string() || doc->video != r["document"]["video"].boolean() || !near(doc->duration, r["document"]["duration"].number())))
            fail(label + ": document info");
        const auto& x = r["adjustments"];
        if (!same(numbers({a.crop.x, a.crop.y, a.crop.width, a.crop.height}), x["crop"]) || double(a.rotationQuarterTurns) != x["turns"].number()
            || a.mirrored != x["mirrored"].boolean() || m::mediaAssemblyFilterItem(a.filter).id != x["filter"].string() || !near(a.brightness, x["brightness"].number())
            || !near(a.trimStart, x["trimStart"].number()) || (a.trimEnd ? !x["trimEnd"].isNumber() || !near(*a.trimEnd, x["trimEnd"].number()) : x["trimEnd"].isNumber()))
            fail(label + ": adjustments");
        if (a.stickers.size() != x["stickers"].array().size()) fail(label + ": sticker count " + std::to_string(a.stickers.size()));
        else for (std::size_t n = 0; n < a.stickers.size(); ++n) {
            const auto& t = x["stickers"].array()[n]; const auto& k = a.stickers[n];
            if (m::mediaAssemblyStickerItem(k.kind).id != t["kind"].string() || !near(k.x, t["x"].number()) || !near(k.y, t["y"].number()) || !near(k.size, t["size"].number()) || !near(k.rotation, t["rotation"].number()))
                fail(label + ": sticker " + std::to_string(n));
        }
        const auto preview = e.controller->previewImage();
        if ((preview ? numbers({double(preview->width), double(preview->height)}) : Json()) != r["preview"] && !(preview && same(numbers({double(preview->width), double(preview->height)}), r["preview"])))
            fail(label + ": preview size");
        const auto& rows = r["rows"].array();
        if (rows.size() != c.parameterRows().size()) fail(label + ": parameter rows");
        else for (std::size_t n = 0; n < rows.size(); ++n)
            if (c.parameterRows()[n]->parameter().id != rows[n]["id"].string() || c.parameterRows()[n]->valueText() != rows[n]["value"].string())
                fail(label + ": row " + rows[n]["id"].string() + " " + c.parameterRows()[n]->valueText());
        const auto& request = e.w.context.overlay().confirmState().request();
        if (bool(request) != r["confirm"].isObject()) fail(label + ": confirm shown");
        else if (request) {
            const auto& q = r["confirm"];
            if (request->id != q["id"].string() || request->title != q["title"].string() || request->message != q["message"].string()
                || request->confirmTitle != q["confirm"].string() || request->danger != q["danger"].boolean()
                || (request->icon ? Json(std::string(d::iconName(*request->icon))) : Json()) != q["icon"])
                fail(label + ": confirm " + request->title + " / " + request->message + " / " + request->confirmTitle);
        }
        const auto& toast = e.w.context.overlay().toastState().current();
        if (bool(toast) != r["toast"].isObject()) fail(label + ": toast shown");
        else if (toast && (std::string(d::iconName(toast->icon)) != r["toast"]["icon"].string() || toast->title != r["toast"]["title"].string() || toast->caption != r["toast"]["caption"].string()))
            fail(label + ": toast " + toast->title + " / " + toast->caption);
    }
    const Json* row(std::string_view state, std::string_view language, bool light) const {
        for (const auto& r : data["canvas"].array())
            if (r["state"].string() == state && r["language"].string() == language && r["light"].boolean() == light) return &r;
        return nullptr;
    }
    // The harness's session (tools/media_assembly_reference.swift), step by step.
    void session(std::string_view language, bool light) {
        World w(measurer); w.appearance(languageOf(language), light);
        Editor e(w);
        const auto add = [&](std::string_view state) {
            if (const auto* r = row(state, language, light)) scene(*r, e);
            else fail(std::string(state) + "/" + std::string(language) + ": not in the oracle");
        };
        auto& c = *e.canvas;
        e.show(); add("empty");
        e.perform("open"); add("menu.open");
        e.perform("open:shelf"); add("menu.shelf");
        c.closePopups();
        e.import("C:\\oracle\\fixture.png"); add("tiles");
        if (language == "en" && !light) {
            e.perform("tool:crop"); add("crop");
            e.perform("cropDone");
            e.perform("tool:adjust"); add("adjust");
            e.perform("toolBack"); e.perform("tool:levels"); add("levels");
            c.adjustAccessibilitySlider("parameter:black", .95, w.now); add("levels.clamped");
            e.perform("toolReset"); e.perform("toolBack"); e.perform("tool:curves"); add("curves");
            e.perform("toolBack");
            e.perform("mediaTab:1"); add("filters");
            e.perform("filter:sp_filter_7"); add("filter.wuling");
            e.perform("mediaTab:2"); add("stickers");
            e.perform("sticker:sticker_bp_7");
            e.key(keyOf(d::key::right)); e.key(keyOf(d::key::right, 0, false, true)); e.key(keyOf(d::key::right, 0, true, true));
            add("sticker.selected");
            for (int n = 0; n < 16; ++n) e.perform("sticker:sticker_bp_1");
            add("sticker.limit");
            w.context.overlay().clearToast();
            e.perform("zoomIn"); e.perform("zoomIn"); add("zoomed");
            e.perform("zoomReset"); e.key(keyOf(d::key::escape)); add("deselected");
            e.perform("mediaTab:0");
            e.perform("tool:rotate"); e.perform("tool:mirror");
            e.spin(); add("turned");
            e.perform("tool:crop");
            c.adjustAccessibilitySlider("crop.edge0", .2, w.now); c.adjustAccessibilitySlider("crop.edge3", .7, w.now);
            add("turned.crop");
            e.perform("cropDone");
            e.perform("export"); add("export.menu");
            e.perform("export:overwrite"); add("confirm.overwrite");
            w.context.overlay().dismissConfirm();
            e.perform("close"); add("confirm.close.edited");
            w.context.overlay().dismissConfirm();
            e.perform("tool:reset"); add("confirm.reset");
            w.context.overlay().dismissConfirm();
            c.stage().setExport(.42, "Exporting"); add("stage.export");
            c.stage().setExport(std::nullopt, "");
            e.import("C:\\oracle\\owned movie.mp4"); add("movie");
            e.perform("mediaTab:2"); add("movie.stickers");
            e.perform("mediaTab:0"); e.perform("tool:trim"); add("trim");
            c.adjustAccessibilitySlider("parameter:trimStart", .2, w.now); add("trim.edited");
            e.perform("toolBack");
        } else {
            e.perform("mediaTab:1"); add("filters");
            e.perform("mediaTab:2"); add("stickers");
            e.perform("mediaTab:0");
            e.perform("close"); add("confirm.close");
            w.context.overlay().dismissConfirm();
        }
        c.setActive(false, w.now); c.willLeave();
    }
    // MediaAssemblyViewport / MediaAssemblyGeometry / MediaAssemblyPictureTransform / MediaAssemblyParameter tables.
    void viewport() {
        const auto& v = data["viewport"];
        const auto stage = m::MediaAssemblyStage::bounds;
        std::map<std::string, m::MediaAssemblyViewport> zooms;
        for (const auto& z : v["zooms"].array()) {
            const auto& size = z["size"].array();
            const endfield::core::Point s{size[0].number(), size[1].number()};
            auto& port = zooms[std::to_string(s.x) + "x" + std::to_string(s.y)];
            if (z.contains("move")) port.move({90, -70}, s, stage); else port.zoomBy(z["factor"].number(), s, stage);
            if (!near(port.zoom(), z["zoom"].number()) || !same(numbers({port.pan().x, port.pan().y}), z["pan"])) fail("zoom(by:) step " + std::to_string(int(z["step"].number())));
            ++checks;
        }
        for (const auto& g : v["drags"].array()) {
            const Rect crop = rectOf(g["crop"]); const auto& to = g["to"].array();
            const auto r = m::MediaAssemblyViewport::dragCrop({crop.x, crop.y, crop.width, crop.height}, int(g["corner"].number()), {to[0].number(), to[1].number()});
            if (!near(Rect{r.x, r.y, r.width, r.height}, rectOf(g["result"]), 1e-9)) fail("dragCrop corner " + std::to_string(int(g["corner"].number())));
            ++checks;
        }
        for (const auto& o : v["origins"].array()) {
            const Rect crop = rectOf(o["crop"]), image = rectOf(o["image"]);
            const auto points = m::MediaAssemblyViewport::cropHandleOrigins({crop.x, crop.y, crop.width, crop.height}, {image.x, image.y, image.width, image.height}, {448, 412});
            for (std::size_t n = 0; n < 4; ++n) if (!same(numbers({points[n].x, points[n].y}), o["origins"].array()[n])) fail("cropHandleOrigins " + str(crop));
            ++checks;
        }
        for (const auto& h : v["handles"].array()) {
            const auto& p = h["point"].array();
            const auto hit = m::MediaAssemblyViewport::handle({p[0].number(), p[1].number()}, {100, 120, 80, 80}, h["rotation"].number());
            const std::string name = !hit ? "" : hit->kind == m::MediaAssemblyHandle::Kind::rotate ? "rotate" : hit->kind == m::MediaAssemblyHandle::Kind::remove ? "delete" : "corner" + std::to_string(hit->corner);
            if ((name.empty() ? Json() : Json(name)) != h["handle"]) fail("handle(at:) " + std::to_string(p[0].number()) + "," + std::to_string(p[1].number()) + " @" + std::to_string(h["rotation"].number()));
            ++checks;
        }
        m::MediaAssemblyGeometry shown; m::MediaAssemblyPictureTransform transform;
        for (const auto& t : v["transforms"].array()) {
            const auto target = m::MediaAssemblyGeometry::make(int(t["turns"].number()), t["mirrored"].boolean(), {});
            const auto delta = m::MediaAssemblyPictureTransform::delta(shown, target);
            transform = transform.nearest(delta.turns, delta.reflected);
            if (double(target.quarterTurns) != t["normalized"].number() || !same(numbers({double(delta.turns), delta.reflected ? 1. : 0.}), t["delta"])
                || !near(transform.angle, t["angle"].number()) || !same(numbers({transform.scaleX, transform.scaleY}), t["scale"])
                || transform.swapsAxes() != t["swaps"].boolean() || transform.isIdentity() != t["identity"].boolean())
                fail("picture transform to " + std::to_string(int(t["turns"].number())) + (t["mirrored"].boolean() ? " mirrored" : ""));
            shown = target; ++checks;
        }
        std::size_t index = 0;
        const auto& parameters = v["parameters"].array();
        for (const auto tool : {m::MediaAssemblyTool::crop, m::MediaAssemblyTool::adjust, m::MediaAssemblyTool::curves, m::MediaAssemblyTool::levels, m::MediaAssemblyTool::trim})
            for (const auto& p : m::MediaAssemblyParameter::parameters(tool, 7.3, Language::english)) {
                if (index >= parameters.size()) { fail("parameters: extra " + p.id); continue; }
                const auto& e = parameters[index++];
                if (e["tool"].string() != m::mediaAssemblyToolID(tool) || e["id"].string() != p.id || e["title"].string() != p.title || !near(e["minimum"].number(), p.range.minimum)
                    || !near(e["maximum"].number(), p.range.maximum) || !near(e["step"].number(), p.range.step) || (p.range.zero ? Json(*p.range.zero) : Json()) != e["zero"]
                    || !near(e["default"].number(), p.defaultValue) || d::formatValue(p.format(), 1.234) != e["format"].string())
                    fail("parameter " + p.id);
                m::MediaAssemblyAdjustments a; a.levelsWhite = .6; a.levelsBlack = .3; a.trimEnd = 4;
                for (const auto& w : e["writes"].array()) {
                    auto b = a; p.write(b, w.array()[0].number());
                    if (!near(p.read(b), w.array()[1].number(), 1e-9)) fail("parameter " + p.id + " write " + std::to_string(w.array()[0].number()) + " reads " + std::to_string(p.read(b)));
                }
                ++checks;
            }
        if (index != parameters.size()) fail("parameters: count");
    }
};
}

int main(int argc, char** argv) {
    try {
        FakeMeasurer fake;
        rules(fake);
        motion(fake);
        if (const auto data = loadOracle(argc > 1 ? argv[1] : nullptr, {"canvas"}, {"text", "viewport"})) {
            Oracle oracle(*data);
            oracle.viewport();
            for (const auto& [language, light] : std::vector<std::pair<std::string, bool>>{{"en", false}, {"zh-Hans", false}, {"ja", false}, {"en", true}}) oracle.session(language, light);
            std::cout << "oracle: " << oracle.compared << " scenes compared, " << oracle.measurer.misses.size() << " unrecorded measurements\n";
            for (const auto& miss : oracle.measurer.misses) std::cout << "  unrecorded: " << miss << '\n';
            for (const auto& f : oracle.failures) std::cout << "  MISMATCH " << f << '\n';
            check(oracle.failures.empty(), std::to_string(oracle.failures.size()) + " oracle mismatches");
            check(oracle.compared == data->operator[]("canvas").array().size(), "Every recorded state is compared");
        } else std::cout << "media-assembly-source.json not present: Swift-derived contracts only\n";
        std::cout << "media_assembly_canvas_tests: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "media_assembly_canvas_tests failed after " << checks << " checks: " << e.what() << '\n';
        return 1;
    }
}
