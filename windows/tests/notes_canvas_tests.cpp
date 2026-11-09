// Contracts of the redesigned Notes centre (modules/notes_canvas.*, the format
// bar of modules/notes_format_bar.*): the rules of Sources/NotesCanvas.swift
// (each check cites the Swift line it ports) over a memory-only
// NotesCentreStore, and, when the Mac oracle is given
// (tests/fixtures/notes-canvas-source.json from tools/notes_reference.sh),
// every recorded scene node by node with the Mac's own text widths, its
// controls and accessibility items, and the format bar's statics. Synthetic
// documents only: no file is written.
#include "modules/notes_canvas.hpp"
#include "modules/notes_card_controls.hpp"
#include "tests/hud_design_oracle.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace d = endfield::modules::design;
namespace m = endfield::modules;
namespace data = ehud::data;
using endfield::core::Language;
using namespace endfield::tests::design_oracle;
namespace {
unsigned checks{};
void check(bool v, const std::string& what) { ++checks; if (!v) throw std::runtime_error(what); }
d::Control* control(d::Canvas& canvas, std::string_view id) {
    for (d::Control* c : canvas.page().controls()) if (c->id() == id) return c;
    return nullptr;
}
struct World {
    d::Context context;
    double now{100};
    explicit World(d::TextMeasurer& m) : context(m, d::asciiUppercase) { context.update(d::geometry::baseUnit, 2 / d::geometry::baseUnit, now); }
    void appearance(Language language, bool light = false, bool recording = false, bool reduceMotion = true) {
        d::Context::Appearance a; a.language = language; a.light = light; a.reduceMotion = reduceMotion; a.recordingMode = recording;
        context.apply(a, now);
    }
    void show(d::Canvas& c) { c.show(now); c.setActive(true, now); }
    std::string toast() const {
        const auto& t = const_cast<d::Context&>(context).overlay().toastState().current();
        return t ? t->title + "|" + t->caption : std::string();
    }
};
// A memory-only store holding `document` (Mac NotesCentreStore(directory: nil) after update).
std::unique_ptr<data::NotesCentreStore> storeOf(const data::NotesCentreDocument& document) {
    auto store = std::make_unique<data::NotesCentreStore>(std::nullopt);
    (void)store->update([&](data::NotesCentreDocument& d) { d = document; });
    return store;
}
data::NotesCentreDocument three() {
    data::NotesCentreDocument d;
    d.tasks = {{"0000000C-0000-4000-8000-000000000001", "Calibrate the relay", true}, {"0000000C-0000-4000-8000-000000000002", "Patrol route 7", false},
               {"0000000C-0000-4000-8000-000000000003", "Report to Perlica", false}};
    d.flagged = {"0000000C-0000-4000-8000-000000000002"};
    return d;
}

// --- Swift-derived contracts ------------------------------------------------------------
void rules() {
    FakeMeasurer measurer;
    World w(measurer); w.appearance(Language::english);
    std::vector<std::string> tools;
    m::NotesCanvasOptions options; options.onTool = [&](std::string_view a) { tools.emplace_back(a); };
    auto store = storeOf(three());
    m::NotesCanvas canvas(w.context, *store, options);
    w.show(canvas);
    // counterText (:196-198): done / total, two digits.
    check(canvas.header().counterText() == "01/03", "TODO counter is done/total");
    // showsDot / hasDot (:41-42): the flagged open task, and the TODO tab's news dot.
    check(store->document().hasDot() && canvas.taskCard("0000000C-0000-4000-8000-000000000002"), "The flagged open task shows the dot");
    // The add bar (:674-692): the four desktop tools.
    for (const auto* kind : {"text", "todo", "drawing"}) { auto* b = control(canvas, std::string("notes.new.") + kind); check(b && b->enabled(), "Add button present"); b->activate(); }
    check(tools == std::vector<std::string>{"tool:text", "tool:todo", "tool:drawing"}, "The add bar creates desktop notes through the workspace");
    // submitTask (:744-760): trimmed, capped at 60 characters, appended, field cleared.
    canvas.submitTask("  New field task  ");
    check(store->document().tasks.size() == 4 && store->document().tasks.back().text == "New field task", "A submitted task is trimmed and appended");
    canvas.submitTask(std::string(80, 'x'));
    check(store->document().tasks.back().text.size() == data::NotesCentreDocument::maximumTaskLength, "A task title is capped at 60 characters");
    canvas.submitTask("   ");
    check(store->document().tasks.size() == 5, "A blank task is not added");
    // dragTaskForTesting (:818-826): the first task moves to the third slot.
    canvas.dragTask("0000000C-0000-4000-8000-000000000001", 2);
    check(store->document().tasks[2].id == "0000000C-0000-4000-8000-000000000001", "A dragged task lands on its slot");
    // The drag hand over the TODO grips (Mac d32a5bf): the open hand over a grip, the closed hand while a card is lifted.
    {
        // The dropped card has landed (the page is rebuilt) and the list is back at its top.
        w.now += 1; w.context.advance(w.now);
        for (const auto& area : canvas.page().scrollAreas()) area->setOffset(0);
        const std::string first = store->document().tasks[0].id;
        const auto r = control(canvas, "task:" + first + ":grip")->rect(canvas.root());
        const d::Point grip{r.x + r.width / 2, r.y + r.height / 2};
        check(canvas.cursor(grip) == d::Cursor::openHand && !canvas.cursor(d::Point{grip.x + 300, grip.y}) && !canvas.cursor(std::nullopt),
            "The open hand shows over a TODO grip only");
        d::PointerInput press; press.point = grip;
        check(canvas.pointerDown(grip, press, w.now), "The TODO grip takes the press");
        canvas.pointerDragged({grip.x, grip.y + 6}, w.now);
        check(canvas.cursor(d::Point{grip.x + 300, grip.y}) == d::Cursor::closedHand && canvas.inputLocked(), "The closed hand shows anywhere while a card is lifted");
        canvas.pointerDragged({grip.x, grip.y + canvas.cardHeight + d::metric::rowGap}, w.now);
        canvas.pointerUp({grip.x, grip.y + canvas.cardHeight + d::metric::rowGap}, w.now);
        check(store->document().tasks[1].id == first && canvas.cursor(grip) != d::Cursor::closedHand, "The drop saves the slot and lets the hand go");
    }
    // setFolded (:708-728): Hide keeps the document and folds the body.
    canvas.setFolded(true);
    check(store->document().isFolded, "Hide folds the centre");
    canvas.setFolded(false);
    // selectTab (:699-706): the TEXT tab shows the note and "01".
    canvas.selectTab(data::NotesCentreDocument::Tab::text, -1);
    check(store->document().tab == data::NotesCentreDocument::Tab::text && canvas.header().counterText() == "01" && canvas.formatBar(), "The TEXT tab shows the note and its bar");
    check(!canvas.cursor(d::Point{26, 160}), "No drag hand on the TEXT tab");
    // MODS-638: a new HUD session builds the Mac's TEXT editor anew (:533-557): the note reloaded, the caret at its end.
    check(canvas.noteArea() != nullptr, "The TEXT note has its editor");
    canvas.noteArea()->document().setSelection({{0, 0}});
    canvas.setActive(false, w.now); canvas.resetSession();
    const auto length = std::uint32_t(canvas.noteArea()->document().text().size());
    check(canvas.noteArea()->document().selection().range == endfield::core::text::Range{length, length}, "A new HUD session puts the caret at the note's end");
}
// The TODO reorder through the shared ListReorder keeps the Notes timings
// (:432-497): lift to z 10 and scale 1.015 over the hover duration, the
// others glide in .18 s, the drop settles in .18 s, and the page is rebuilt
// .2 s after the drop (at once under Reduce Motion). The cards sit at
// slotY(0) = 4 with the card pitch.
void reorderTimings() {
    FakeMeasurer measurer;
    World w(measurer); w.appearance(Language::english, false, false, false);
    auto store = storeOf(three());
    m::NotesCanvas canvas(w.context, *store, {});
    w.show(canvas);
    w.now += 2; w.context.advance(w.now);
    const auto same = [](double a, double b) { return std::abs(a - b) < 1e-9; };
    const auto trackTime = [](const d::Layer& l, d::Channel c) { return l.tracks().property[std::size_t(c)].duration; };
    const double pitch = canvas.cardHeight + d::metric::rowGap;
    const auto tasks = store->document().tasks;
    auto& first = canvas.taskCard(tasks[0].id)->layer();
    auto& second = canvas.taskCard(tasks[1].id)->layer();
    check(first.frame().y == 4 && same(second.frame().y, 4 + pitch), "The cards sit at slotY(0) = 4, one card pitch apart");
    const auto r = control(canvas, "task:" + tasks[0].id + ":grip")->rect(canvas.root());
    const d::Point grip{r.x + r.width / 2, r.y + r.height / 2};
    d::PointerInput press; press.point = grip;
    check(canvas.pointerDown(grip, press, w.now), "The TODO grip takes the press");
    canvas.pointerDragged({grip.x, grip.y + 6}, w.now);
    check(first.zPosition() == 10 && first.value(d::Channel::scaleX) == 1.015 && first.value(d::Channel::scaleY) == 1.015
          && same(trackTime(first, d::Channel::scaleX), d::duration::hover), "Lift: z 10 and scale 1.015 over the hover duration");
    canvas.pointerDragged({grip.x, grip.y + pitch + 10}, w.now);
    check(same(first.value(d::Channel::translateY), pitch + 10) && same(second.value(d::Channel::translateY), -pitch)
          && same(trackTime(second, d::Channel::translateY), .18), "The lifted card follows; the next one glides up in .18 s");
    w.now += .5; w.context.advance(w.now);
    canvas.pointerUp({grip.x, grip.y + pitch + 10}, w.now);
    check(first.zPosition() == 0 && first.value(d::Channel::scaleX) == 1 && same(trackTime(first, d::Channel::scaleX), .18), "The drop settles the scale in .18 s");
    check(same(first.value(d::Channel::translateY), pitch) && same(trackTime(first, d::Channel::translateY), .18), "The card settles into its slot in .18 s");
    check(store->document().tasks[1].id == tasks[0].id, "The drop saves the slot");
    const auto pages = w.context.pagesBuilt();
    w.now += .15; w.context.advance(w.now);
    check(w.context.pagesBuilt() == pages, "No rebuild before the card has landed");
    w.now += .1; w.context.advance(w.now);
    check(w.context.pagesBuilt() == pages + 1, "The page is rebuilt .2 s after the drop");
    w.appearance(Language::english);
    const auto reduced = w.context.pagesBuilt();
    canvas.dragTask(tasks[2].id, 0);
    w.context.advance(w.now);
    check(store->document().tasks[0].id == tasks[2].id && w.context.pagesBuilt() == reduced + 1, "Under Reduce Motion the page is rebuilt at once");
}
// The TEXT note's editor is drawn by its host right after the note area's
// part (NativeDesignScene::setInsertion keys on part roots, d::partRoots):
// the area, nested in the note box, roots a part with a surface in every
// state the note shows, or the scene drops the typed text.
struct NoteHost final : m::ArchiveBodyEditorHost {
    int attaches{};
    bool attach(m::ArchiveBodyArea&) override { ++attaches; return true; }
    void suspend(m::ArchiveBodyArea&) override {}
    void dispose(m::ArchiveBodyArea&) noexcept override {}
    bool attached(const m::ArchiveBodyArea&) const noexcept override { return attaches > 0; }
    bool focused(const m::ArchiveBodyArea&) const noexcept override { return false; }
    void focus(m::ArchiveBodyArea&, endfield::core::text::Range) override {}
    void blur(m::ArchiveBodyArea&) override {}
    bool hasMarkedText(const m::ArchiveBodyArea&) const noexcept override { return false; }
    bool applyFormat(m::ArchiveBodyArea&, const endfield::core::notes::FormatChange&, endfield::core::text::Range, bool) override { return false; }
    bool replace(m::ArchiveBodyArea&, endfield::core::text::Range, std::u16string_view) override { return false; }
    void scroll(m::ArchiveBodyArea&, double) override {}
    void layoutChanged(m::ArchiveBodyArea&) override {}
};
void noteEditorPart(const char* body) {
    FakeMeasurer measurer;
    World w(measurer); w.appearance(Language::english);
    data::NotesCentreDocument doc; doc.tab = data::NotesCentreDocument::Tab::text; doc.text = body;
    auto store = storeOf(doc);
    NoteHost host;
    m::NotesCanvasOptions options; options.noteEditor = &host;
    m::NotesCanvas canvas(w.context, *store, options);
    const auto composed = [&](const std::string& when) {
        const auto* area = canvas.noteArea();
        check(area && area->layer().parent(), "The TEXT note shows its area " + when);
        const auto artwork = d::emit(canvas.root(), {&w.context.env().text, "test."});
        const auto roots = d::partRoots(artwork, canvas.root());
        std::size_t parts{};
        for (std::size_t n = 0; n < roots.size(); ++n) parts += roots[n] == &area->layer() && !artwork.parts[n].surfaces.empty();
        check(parts == 1, "The note area roots one part with a surface (the editor's insertion anchor) " + when);
    };
    canvas.show(w.now);
    check(host.attaches == 0, "Shown but inactive, the note has no editor yet");
    // Mac 1da1863 (attach from the showing) has nothing to port: the editor
    // attaches only on activation, so a switch overtaken before it activated
    // the module (shown, then only left) never attaches it.
    canvas.prepareToLeave(w.now);
    check(host.attaches == 0, "A switch overtaken before activation leaves no editor attached");
    canvas.show(w.now);
    canvas.setActive(true, w.now);
    // The owner's first pose of the editor follows this attach.
    check(host.attaches == 1, "Activated on the TEXT tab, the note attaches its editor");
    composed("when opened");
    auto& area = *canvas.noteArea();
    const auto typing = area.document().typingStyle(); // load disposes the document it lives in
    area.load(u"Typed field note", {}, typing);
    composed("after typing (no placeholder)");
    canvas.selectTab(data::NotesCentreDocument::Tab::todo, -1);
    canvas.selectTab(data::NotesCentreDocument::Tab::text, -1);
    composed("after a tab round trip");
    canvas.setFolded(true); canvas.setFolded(false);
    composed("after folding and unfolding");
    w.appearance(Language::simplifiedChinese);
    composed("after a language rerender");
    w.appearance(Language::english, true);
    composed("in Light");
    canvas.setActive(false, w.now);
}
// The TEXT note's editor is drawn by its host over the note's parts, clipped
// as they are (Mac: the text view lies under the body clip's masksToBounds,
// NotesCanvas.swift:207-208): d::samplePlacement hands the host the fold's
// running wipe, whose edge crosses the text while the note unfolds (.32 s),
// and nothing once it is open.
void noteEditorClip() {
    FakeMeasurer measurer;
    World w(measurer);
    d::Context::Appearance a; a.language = Language::english; a.reduceMotion = false; w.context.apply(a, w.now);
    const auto at = [&](double t) { w.now = t; w.context.update(d::geometry::baseUnit, 2 / d::geometry::baseUnit, t); };
    data::NotesCentreDocument doc; doc.tab = data::NotesCentreDocument::Tab::text; doc.text = "Field note";
    auto store = storeOf(doc);
    NoteHost host;
    m::NotesCanvasOptions options; options.noteEditor = &host;
    m::NotesCanvas canvas(w.context, *store, options);
    w.show(canvas);
    at(w.now + 2);
    auto* area = canvas.noteArea();
    check(area != nullptr, "The TEXT note shows its area");
    const auto text = d::convert(area->textRect(), area->layer(), canvas.root());
    check(text.height > 100, "The note's text rect is tall");
    // The lowest edge the sampled masks leave (root px; none: unbounded).
    const auto edge = [&](const d::SurfacePose& pose) {
        double bottom = 1e9;
        for (std::size_t n = 0; n < pose.maskCount; ++n) {
            const auto& mask = pose.masks[n];
            bottom = std::min(bottom, mask.world.apply({mask.bounds.x, mask.bounds.y + mask.bounds.height}).y);
        }
        return bottom;
    };
    const auto sample = [&](double t) { return d::samplePlacement(area->layer(), canvas.root(), t); };
    {
        const auto open = sample(w.now);
        const auto layer = d::sampleLayer(area->layer(), canvas.root(), w.now);
        check(open.visible && open.world == layer.world && std::abs(open.opacity - layer.opacity) < 1e-6, "Placed as sampleLayer places the area");
        check(edge(open) >= text.y + text.height - .5, "Open, no mask cuts the note's text");
    }
    canvas.setFolded(true);
    at(w.now + 1);
    check(!sample(w.now).visible || edge(sample(w.now)) <= text.y + 1, "Folded, the note's text is hidden or clipped away");
    canvas.setFolded(false);
    const double start = w.now;
    std::size_t crossing{};
    for (const double t : {.08, .12, .16, .2}) {
        const auto mid = sample(start + t);
        const auto bottom = edge(mid);
        crossing += mid.visible && bottom > text.y && bottom < text.y + text.height - 1;
    }
    check(crossing >= 2, "Unfolding, the fold's wipe edge crosses the note's text and reaches its host");
    at(start + 1);
    check(sample(w.now).visible && edge(sample(w.now)) >= text.y + text.height - .5, "Unfolded, no mask cuts the note's text again");
    canvas.setActive(false, w.now);
}
// The redaction wipe of a build in Recording mode (:250): the shown Notes
// rebuilds at the switch and wipes its bars in; a Notes page the switch found
// hidden was rebuilt and wiped unseen on the Mac (HUDDesignScreen.swift:82),
// so its late rebuild here, when shown, shows the bars at rest.
void recordingWipe() {
    FakeMeasurer measurer;
    World w(measurer);
    const auto apply = [&](bool recording) { d::Context::Appearance a; a.language = Language::english; a.recordingMode = recording; w.context.apply(a, w.now); };
    apply(false);
    auto store = storeOf(three());
    m::NotesCanvas canvas(w.context, *store, {});
    w.show(canvas);
    const auto wiping = [&] {
        bool any{};
        const auto visit = [&](auto& self, const d::Layer& l) -> void { if (std::string_view(l.id()).ends_with(".redaction.bar") && l.animating(w.now)) any = true; for (const d::Layer* c : l.children()) self(self, *c); };
        visit(visit, canvas.root());
        return any;
    };
    apply(true);
    check(wiping(), "Shown, Recording mode on rebuilds Notes and wipes its bars in");
    w.now += 1;
    check(!wiping(), "The wipe ends");
    canvas.setActive(false, w.now); canvas.prepareToLeave(w.now);
    apply(false); w.now += 1;
    apply(true);
    w.show(canvas);
    check(!wiping(), "A Notes page the switch found hidden shows its bars at rest when shown");
}
void unavailable() {
    FakeMeasurer measurer;
    World w(measurer); w.appearance(Language::english);
    m::NotesCanvasOptions options; options.storageAvailable = false;
    auto store = storeOf({});
    m::NotesCanvas canvas(w.context, *store, options);
    w.show(canvas);
    // didActivate (:172-177): the storage notice toasts once.
    check(w.toast() == "Notice|Notes storage is unavailable", "The storage notice toasts: " + w.toast());
    for (const auto* kind : {"text", "todo", "image", "drawing"}) check(!control(canvas, std::string("notes.new.") + kind)->enabled(), "Add buttons are disabled without storage");
    canvas.setActive(false, w.now);
    // MODS-638: the Mac builds a new NotesCanvas at every HUD open (:112): the notice once per session.
    w.context.overlay().reset(); canvas.setActive(true, w.now);
    check(w.toast().empty(), "Not again in the same session");
    canvas.setActive(false, w.now); canvas.resetSession(); canvas.setActive(true, w.now);
    check(w.toast() == "Notice|Notes storage is unavailable", "A new HUD session tells the storage notice again");
    canvas.setActive(false, w.now);
}

// --- A desktop card's redesign controls (modules/notes_card_controls) --------------------------
void cardControls() {
    FakeMeasurer measurer;
    World w(measurer); w.appearance(Language::english); w.context.env().reduceMotion = false;
    constexpr const char* id = "00000000-0000-4000-8000-000000000001";
    m::NotesState state({{.id = id, .kind = data::NoteKind::text, .text = "One", .x = 60, .y = 90, .width = 260, .height = 140, .zIndex = 1, .createdAt = 1}}, {[](const auto&) {}, [](auto) {}});
    state.setWorkspaceBounds({0, 0, 1920, 1080});
    auto measured = std::make_shared<m::NotesMeasuredText>(); measured->text = "One"; measured->width = 242; measured->lines = {{0, 3, 3, 0, 16}}; measured->height = 16;
    m::NotesPresentationInput input; input.palette = m::NotesPalette::source(true, {.98, .83, .12, 1}); input.measured = measured;
    m::NotesCardPresentation card(id); card.updateContent(state, input);
    m::NotesCardControls controls(w.context.env(), id);
    controls.update(card);
    const auto revision = controls.revision();
    // render(_:) (:1228-1278): one library control per presentation control, at its frame.
    check(controls.controls().size() == card.controls().size(), "One control per card control");
    for (const auto& spec : card.controls()) {
        const auto* c = controls.control(spec.id);
        check(c && c->label == spec.label, "Control " + spec.id + " carries its label");
        const auto r = c->rect(controls.root());
        check(near(r, d::Rect{spec.frame.x, spec.frame.y, spec.frame.width, spec.frame.height}, 1e-6), "Control " + spec.id + " sits at its drawn frame (the hit rect)");
    }
    // BuildGate (:168-169): a content event builds the buttons settled, even with motion on.
    check(!controls.requiresFrames(w.now), "Built controls are settled (no fade-in)");
    // The highlight pass: hovering the pin lights it (HUDIconButton hover .22 ease).
    const auto pin = card.controls()[0].frame;
    controls.pointer(d::Point{pin.x + 9, pin.y + 9}, false);
    check(controls.control(card.controls()[0].id)->hovered() && controls.requiresFrames(w.now), "Hover lights the pin and animates");
    controls.pointer(std::nullopt, false);
    // Editing adds the format toggles; the tree is rebuilt and its revision moves.
    state.select(id); state.beginEditing(id); card.updateContent(state, input); controls.update(card);
    check(controls.controls().size() == card.controls().size() && controls.control(std::string("note:") + id + ":formatSize") && controls.revision() != revision, "Editing rebuilds with the format toggles");
}

// --- The Mac oracle -----------------------------------------------------------------------
struct Oracle {
    const Json& data;
    RecordedMeasurer measurer;
    std::vector<std::string> failures;
    std::size_t compared{};
    explicit Oracle(const Json& d) : data(d) {
        recordText(measurer, d["text"]);
        // The task card's micro ENDFIELD tag (HUDArtwork.microTagSize: the
        // label width + 44, the label ENDFIELD + 8) is a raster on the Mac: its
        // text width is read back from the first card's tag (x 14, y 4, h 7).
        const std::function<std::optional<double>(const Json&)> tag = [&](const Json& n) -> std::optional<double> {
            const Rect b = rectOf(n["bounds"]);
            if (n["name"].isString() && n["name"].string().starts_with("hud.control.task:") && n["children"].isArray())
                for (const auto& c : n["children"].array()) if (near(rectOf(c["bounds"]).height, 7) && rectOf(c["bounds"]).width > 52) return rectOf(c["bounds"]).width - 52;
            (void)b;
            if (n["children"].isArray()) for (const auto& c : n["children"].array()) if (const auto w = tag(c)) return w;
            return std::nullopt;
        };
        for (const auto& r : d["notes"].array()) if (const auto w = tag(r["layer"])) {
            const d::TextRun run{"ENDFIELD", d::type::microTag()};
            measurer.widths[RecordedMeasurer::key(std::span(&run, 1), d::TextRegion::simplifiedChinese)] = *w;
            break;
        }
    }
    const Json* row(std::string_view state, std::string_view language, bool light) const {
        for (const auto& r : data["notes"].array())
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
    void scene(std::string_view state, std::string_view language = "en", bool light = false, const std::function<void(m::NotesCanvas&)>& step = {}) {
        const Json* r = row(state, language, light);
        const std::string label = "notes/" + std::string(state) + "/" + std::string(language) + (light ? "/light" : "");
        if (!r) { failures.push_back(label + ": not in the oracle"); return; }
        ++compared;
        const auto& input = (*r)["input"];
        World w(measurer);
        w.appearance(language == "zh-Hans" ? Language::simplifiedChinese : Language::english, light, input["recording"].boolean());
        auto store = storeOf(data::decodeNotesCentre(input["document"].string()));
        m::NotesCanvasOptions options; options.storageAvailable = (*r)["storage"].boolean();
        // The Mac lists the demo families this Mac can draw.
        for (const auto& f : data["formatBar"]["families"].array()) options.families.push_back(f.string());
        m::NotesCanvas canvas(w.context, *store, options);
        w.show(canvas);
        if (step) step(canvas);
        Json expected = *r;
        expected["layer"] = rooted(expected["layer"]);
        // The TEXT note's own items are the projected editor's (see below).
        if (expected["controls"].isArray() && canvas.noteArea()) {
            Json::Array controls = expected["controls"].array();
            for (auto& c : controls) if (c["id"].string() == "notes.text") {
                std::vector<d::AccessibilityItem> own; canvas.noteArea()->accessibilityItems(canvas.root(), own);
                Json::Array mine; for (const auto& i : own) mine.push_back(Json::Object{{"label", i.label}, {"rect", Json::Array{i.rect.x, i.rect.y, i.rect.width, i.rect.height}}, {"enabled", i.enabled}});
                c["accessibility"] = Json(std::move(mine));
            }
            expected["controls"] = Json(std::move(controls));
        }
        compare(label, expected, canvas.root(), canvas.root(), canvas.page().controls(), failures);
        // The add field: the Mac lists a HUDDesignTextField as a button; the
        // library's UIA tree names it an edit (LIBRARY-DESIGN.md section 9).
        // The TEXT note: the Mac's NSTextView is its own accessibility
        // element, outside the canvas's items; Windows lists the projected
        // editor as the note's edit item (ArchiveBodyArea).
        Json::Array rows = expected["accessibility"].array();
        for (auto& item : rows) if (item["id"].string() == "notes.newTask" && item["role"].string() == "button") item["role"] = "edit";
        std::vector<d::AccessibilityItem> items; canvas.accessibilityItems(items);
        std::erase_if(items, [](const d::AccessibilityItem& i) { return i.id == "notes.text"; });
        compareAccessibility(label, Json(std::move(rows)), items, failures);
        if (expected["top"].isObject()) compare(label + "/top", Json::Object{{"layer", expected["top"]}}, canvas.top(), canvas.root(), {}, failures);
        if (canvas.header().counterText() != (*r)["header"]["counter"].string()) failures.push_back(label + ": counter " + canvas.header().counterText());
        canvas.setActive(false, w.now);
    }
};
void notesOracle(Oracle& o) {
    o.scene("todoEmpty"); o.scene("todo"); o.scene("todo", "zh-Hans"); o.scene("todo", "en", true);
    o.scene("taskMenu", "en", false, [](m::NotesCanvas& c) { c.openTaskMenu("0000000C-0000-4000-8000-000000000002"); });
    o.scene("folded"); o.scene("text"); o.scene("textEmpty"); o.scene("textStyled"); o.scene("unavailable"); o.scene("recording");
    // NotesFormatBar statics (:95-105).
    const auto& bar = o.data["formatBar"];
    std::vector<double> sizes; for (const auto& s : bar["sizes"].array()) sizes.push_back(s.number());
    if (!std::equal(sizes.begin(), sizes.end(), m::notesFormat::sizes.begin(), m::notesFormat::sizes.end())) o.failures.push_back("format bar sizes");
    d::Palette palette;
    const auto colors = m::notesFormat::colors(palette);
    for (std::size_t n = 0; n < colors.size(); ++n) if (std::uint32_t(bar["colors"].array()[n].number()) != colors[n]) o.failures.push_back("format bar colour " + std::to_string(n));
    std::vector<std::string> bundled, installed;
    for (const auto& f : bar["families"].array()) installed.push_back(f.string());
    if (m::notesFormat::families(bundled, installed) != installed) o.failures.push_back("format bar families");
    ++checks;
}
}

int main(int argc, char** argv) {
    try {
        rules();
        reorderTimings();
        noteEditorPart(""); noteEditorPart("Field note"); noteEditorClip();
        recordingWipe(); unavailable(); cardControls();
        if (const auto oracleData = loadOracle(argc > 1 ? argv[1] : nullptr, {"notes"}, {"text", "layerDefaults", "formatBar"})) {
            Oracle oracle(*oracleData);
            notesOracle(oracle);
            std::cout << "oracle: " << oracle.compared << " scenes compared, " << oracle.measurer.misses.size() << " unrecorded measurements\n";
            for (const auto& miss : oracle.measurer.misses) std::cout << "  unrecorded: " << miss << '\n';
            for (const auto& f : oracle.failures) std::cout << "  MISMATCH " << f << '\n';
            check(oracle.failures.empty(), std::to_string(oracle.failures.size()) + " oracle mismatches");
        } else std::cout << "notes-canvas-source.json not present: Swift-derived contracts only\n";
        std::cout << "notes_canvas_tests: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "notes_canvas_tests failed after " << checks << " checks: " << e.what() << '\n';
        return 1;
    }
}
