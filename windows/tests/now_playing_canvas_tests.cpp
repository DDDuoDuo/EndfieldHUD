// Contracts of the redesigned Now Playing canvas (modules/now_playing_canvas.*):
// the rules of Sources/NowPlayingCanvas.swift (each check cites the Swift
// line it ports; the verification steps are Batch34HUDVerification's) over a
// scripted controller, and, when the Mac oracle is given
// (tests/fixtures/play-canvases-source.json from tools/play_canvases_reference.sh),
// every recorded Now Playing scene node by node with the Mac's own text
// widths, its controls and accessibility items. Synthetic state only: no
// media session, network, audio route or decoder.
#include "modules/now_playing_canvas.hpp"
#include "tests/hud_design_oracle.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace d = endfield::modules::design;
namespace m = endfield::modules;
using endfield::core::Language;
using namespace endfield::tests::design_oracle;
using C = m::NowPlayingCanvas;
namespace endfield::native { struct NotesImageFrame { unsigned width{}, height{}; }; }
namespace {
unsigned checks{};
void check(bool v, const std::string& what) { ++checks; if (!v) throw std::runtime_error(what); }
d::KeyInput keyOf(std::uint32_t vk, bool shift = false) { d::KeyInput k; k.virtualKey = vk; k.shift = shift; return k; }
d::Control* control(d::Canvas& canvas, std::string_view id) {
    for (d::Control* c : canvas.page().controls()) if (c->id() == id) return c;
    return nullptr;
}
std::optional<d::AccessibilityItem> item(const d::Canvas& canvas, std::string_view id) {
    std::vector<d::AccessibilityItem> items; canvas.accessibilityItems(items);
    for (auto& i : items) if (i.id == id) return i;
    return std::nullopt;
}
const d::Layer* find(const d::Layer& layer, std::string_view id) {
    if (layer.id().ends_with(id)) return &layer;
    for (const auto* child : layer.children()) if (const auto* f = find(*child, id)) return f;
    return nullptr;
}
std::string textOf(const d::Layer& root, std::string_view id) {
    const auto* l = find(root, id);
    return l && l->text() ? l->text()->text() : std::string("<none>");
}
d::Point centre(const d::Control& c, const d::Layer& root) { const auto r = c.rect(root); return {r.x + r.width / 2, r.y + r.height / 2}; }

struct World {
    d::TextMeasurer& measurer;
    d::Context context;
    double now{1000};
    explicit World(d::TextMeasurer& m) : measurer(m), context(m, d::asciiUppercase) { context.update(d::geometry::baseUnit, 2 / d::geometry::baseUnit, now); }
    void appearance(Language language, bool light = false, bool reduce = true) {
        d::Context::Appearance a; a.language = language; a.light = light; a.reduceMotion = reduce;
        context.apply(a, now);
    }
};
// The controller: the published snapshot and the commands the canvas sends.
struct Controller {
    m::NowPlayingViewInput input;
    m::NowPlayingCover cover;
    std::vector<m::NowPlayingIntent> sent;
    std::vector<bool> presented;
    std::vector<unsigned> pixels;
    m::NowPlayingCanvasServices services() {
        m::NowPlayingCanvasServices s;
        s.input = [this]() -> const m::NowPlayingViewInput& { return input; };
        s.artwork = [this] { return cover; };
        s.setPresented = [this](bool v) { presented.push_back(v); };
        s.perform = [this](const m::NowPlayingIntent& i) { sent.push_back(i); return true; };
        s.setArtworkPixels = [this](unsigned p) { pixels.push_back(p); };
        return s;
    }
    static m::NowPlayingViewInput playing(double now, double position = 42, bool isPlaying = true) {
        m::NowPlayingViewInput i;
        i.session = 1;
        m::NowPlayingTrack t; t.title = "Signal Fire"; t.artist = "Endfield Ensemble"; t.album = "Talos II"; t.duration = 214; t.position = position;
        t.isPlaying = isPlaying; t.sampledAt = now;
        i.track = t;
        i.capabilities = {true, true, true, true, true, true, true, false, false};
        i.application = "AppleInc.AppleMusicWin_nzyj5cx40ttqa!App";
        i.lyrics = std::make_shared<const m::NowPlayingLyrics>(*m::NowPlayingLyrics::parse(
            "[00:00.50]Signal Fire\n[00:12]Embers over the plain\n[00:30]Lanterns on the ridge\n[00:41]We carry the light\n[00:48]Through the long night\n[01:05]Signal fire"));
        i.volumeAvailable = true; i.volume = .7; i.volumeRoute = "100:1";
        return i;
    }
};

void layout() {
    // :31-47.
    check(C::Layout::cover.x == 55 && C::Layout::cover.y == -84 && C::Layout::cover.width == 480, "The 480 px cover reaches 84 px above the box");
    check(C::Layout::hitBounds.x == -36 && C::Layout::hitBounds.width == 632 && C::Layout::hitBounds.height == 654, "Hit bounds cover the cover and the button row");
    check(C::time(214) == "03:34" && C::time(5400) == "90:00" && C::time(std::nullopt) == "\xe2\x80\x94:\xe2\x80\x94" && C::time(-1) == "\xe2\x80\x94:\xe2\x80\x94"
        && C::time(1e9) == C::time(604800), "Times: mm:ss, minutes past 59, \xe2\x80\x94:\xe2\x80\x94 when unknown, capped at 604800 s (:395-398)");
}

void behaviour() {
    FakeMeasurer measurer;
    World w(measurer); w.appearance(Language::english, false, false);
    Controller ctl; ctl.input = Controller::playing(w.now);
    C canvas(w.context, ctl.services());
    canvas.setPresented(true, w.now);
    canvas.show(w.now); canvas.setActive(true, w.now);
    check(ctl.presented == std::vector<bool>{true} && !canvas.showsHeader(), "Presentation starts the controller; no module header (:116-121, :98)");
    check(!ctl.pixels.empty() && ctl.pixels.back() == m::nowPlayingCoverPixelSize(480, w.context.boxScale()), "The page names its 480 px slot at the render scale (:173)");
    check(textOf(canvas.root(), "nowPlaying.cover.title") == "Signal Fire" && textOf(canvas.root(), "nowPlaying.elapsed") == "00:42" && textOf(canvas.root(), "nowPlaying.duration") == "03:34",
        "Title and times (:240-272)");
    const auto track = item(canvas, "track");
    check(track && track->value == "Signal Fire / Endfield Ensemble / Talos II" && track->role == d::AccessibilityRole::text, "The track readout joins the parts with \" / \" (:598-611)");
    check(canvas.clockActive() && canvas.nextWakeTime(w.now) && *canvas.nextWakeTime(w.now) <= w.now + 1, "Playing: one deadline at the next whole second or cue (:349-378)");
    // Space plays or pauses; arrows seek 5 s; modifiers pass (:561-577).
    check(canvas.keyDown(keyOf(d::key::space), w.now) && ctl.sent.back().kind == m::NowPlayingIntentKind::playPause, "Space toggles play");
    check(!canvas.keyDown(keyOf(d::key::space, true), w.now), "Shift+Space is not the canvas's");
    check(canvas.keyDown(keyOf(d::key::right), w.now) && ctl.sent.back().kind == m::NowPlayingIntentKind::seek && std::abs(ctl.sent.back().value - 47) < 1e-9, "Right seeks +5 s");
    check(!canvas.keyDown(keyOf(d::key::escape), w.now), "Esc without the pop-up is not consumed");
    // Previous: restart after 3 s, else the previous track (:402-412).
    control(canvas, "previous")->activate();
    check(ctl.sent.back().kind == m::NowPlayingIntentKind::seek && ctl.sent.back().value == 0, "Previous after 3 s restarts the track (MODS-311)");
    ctl.input = Controller::playing(w.now, 2); canvas.refresh(w.now);
    control(canvas, "previous")->activate();
    check(ctl.sent.back().kind == m::NowPlayingIntentKind::previous, "Previous within 3 s goes to the previous track");
    // A different track (title) slides in from the left after Previous (:143-167).
    auto other = Controller::playing(w.now, 0); other.track->title = "Ember Road";
    ctl.input = other; canvas.refresh(w.now);
    check(canvas.transitioning(), "Another title re-enters the page (the shared sub-page slide)");
    w.now += 1; w.context.advance(w.now);
    check(!canvas.transitioning() && textOf(canvas.root(), "nowPlaying.cover.title") == "Ember Road", "The new page lands");
    auto sameTitle = other; sameTitle.track->album = "Another album"; ctl.input = sameTitle; canvas.refresh(w.now);
    check(!canvas.transitioning(), "An album-only change updates in place (identity = source + title, MODS-309)");
    // Disabled Play leaves Space to the HUD (:568-569).
    auto failed = sameTitle; failed.failed = true; ctl.input = failed; canvas.refresh(w.now);
    check(!control(canvas, "playPause")->enabled() && !canvas.keyDown(keyOf(d::key::space), w.now), "Space is not consumed while Play is disabled (MODS-328)");
    ctl.input = Controller::playing(w.now); ctl.input.track->title = "Ember Road"; canvas.refresh(w.now);
    // The volume pop-up (:502-547): opens with a wipe, captures, an outside press only closes it.
    control(canvas, "volume")->activate();
    check(canvas.volumeOpen() && canvas.capturesPointer() && item(canvas, "appVolume") && item(canvas, "appVolume")->value == "70%" && item(canvas, "appVolume")->number == 70,
        "App volume opens the pop-up (70%), which captures the pointer");
    const auto sentBefore = ctl.sent.size();
    check(canvas.pointerDown(centre(*control(canvas, "playPause"), canvas.root()), {}, w.now) && !canvas.volumeOpen() && ctl.sent.size() == sentBefore,
        "The first press outside the pop-up only closes it (verification)");
    control(canvas, "volume")->activate();
    auto* popup = control(canvas, "appVolume");
    const auto origin = popup->rect(canvas.root());
    canvas.pointerDown({origin.x + 22, origin.y + 30 + 106 * .5}, {}, w.now);
    canvas.pointerUp({origin.x + 22, origin.y + 30 + 106 * .5}, w.now);
    check(ctl.sent.back().kind == m::NowPlayingIntentKind::volume && std::abs(ctl.sent.back().value - .5) < 1e-9, "A press sets 1 - (y - 30) / 106, rounded to a percent (:850-854)");
    check(canvas.keyDown(keyOf(d::key::escape), w.now) && !canvas.volumeOpen(), "Esc closes the pop-up");
    // Accessibility sliders (:579-585, :790-792).
    const auto seek = item(canvas, "seek");
    check(seek && seek->step == 5 && seek->maximum == 214 && seek->role == d::AccessibilityRole::slider, "The seek slider: 0...duration, step 5");
    check(canvas.adjustAccessibilitySlider("seek", 100, w.now) && ctl.sent.back().kind == m::NowPlayingIntentKind::seek && ctl.sent.back().value == 100, "UIA seek sends one seek");
    check(canvas.adjustAccessibilitySlider("appVolume", 40, w.now) && ctl.sent.back().kind == m::NowPlayingIntentKind::volume && std::abs(ctl.sent.back().value - .4) < 1e-9, "UIA app volume is value / 100");
    // Scrub: preview locally, one seek on release, none when interrupted (:424-443, verification).
    auto* bar = control(canvas, "seek");
    const auto r = bar->rect(canvas.root());
    const auto commands = ctl.sent.size();
    canvas.pointerDown({r.x + 5 + 240, r.y + 10}, {}, w.now);
    for (int k = 0; k < 20; ++k) canvas.pointerDragged({r.x + 5 + 240 + k * 4, r.y + 10}, w.now);
    check(ctl.sent.size() == commands && !canvas.clockActive() && textOf(canvas.top(), "seek.bubble.text") != "<none>", "Dragging emits no seek and stops the clock; the bubble shows mm:ss");
    canvas.pointerUp({r.x + 5 + 320, r.y + 10}, w.now);
    check(ctl.sent.size() == commands + 1 && ctl.sent.back().kind == m::NowPlayingIntentKind::seek, "Release emits exactly one seek");
    canvas.pointerDown({r.x + 100, r.y + 10}, {}, w.now);
    auto changed = ctl.input; changed.track->title = "Another"; ctl.input = changed; canvas.refresh(w.now);
    canvas.pointerUp({r.x + 200, r.y + 10}, w.now);
    check(ctl.sent.size() == commands + 1, "A track change ends the drag without a seek");
    w.now += 1; w.context.advance(w.now);
    // Lyrics toggle: per canvas, never moves a control (:419-422, verification).
    const auto before = control(canvas, "volume")->rect(canvas.root());
    control(canvas, "lyrics")->activate();
    check(!canvas.showsLyrics() && control(canvas, "volume")->rect(canvas.root()) == before, "Hiding the lyrics moves no control");
    control(canvas, "lyrics")->activate();
    // Hiding stops everything and cancels the presentation (:122-128).
    canvas.setPresented(false, w.now);
    check(ctl.presented.back() == false && !canvas.clockActive() && !canvas.nextWakeTime(w.now), "Hidden: no clock, controller deactivated");
}

void nothingPlaying() {
    FakeMeasurer measurer;
    World w(measurer); w.appearance(Language::simplifiedChinese);
    Controller ctl;
    C canvas(w.context, ctl.services());
    canvas.setPresented(true, w.now);
    canvas.show(w.now); canvas.setActive(true, w.now);
    check(textOf(canvas.root(), "nowPlaying.cover.label") == "未在播放音乐" && item(canvas, "track")->value == "未在播放音乐" && !control(canvas, "playPause")->enabled()
        && !control(canvas, "volume")->enabled() && textOf(canvas.root(), "nowPlaying.elapsed") == "\xe2\x80\x94:\xe2\x80\x94", "Nothing playing: the hatch plate, the localised label, inert controls (:225-236)");
    check(!canvas.coverViews(w.now)[1].image, "No cover to draw");
    // The structure change (playing) rebuilds in place, no slide.
    ctl.input = Controller::playing(w.now);
    canvas.refresh(w.now);
    check(!canvas.transitioning() && textOf(canvas.root(), "nowPlaying.cover.title") == "Signal Fire", "A track appearing rebuilds in place (:157)");
    // A decoded cover is drawn by the owner: it shows in the top art layer.
    auto cover = std::make_shared<endfield::native::NotesImageFrame>(); cover->width = cover->height = 960;
    ctl.cover = cover; ctl.input.coverAvailable = true; ctl.input.coverRevision = 1;
    canvas.refresh(w.now);
    check(canvas.coverViews(w.now)[1].image == ctl.cover && !canvas.coverViews(w.now)[0].image && find(canvas.root(), "nowPlaying.cover.ring")->hidden(),
        "The cover shows (Reduce Motion: at once) and the placeholder ring hides (:318-328)");
}

// --- The Mac oracle (play-canvases-source.json, Now Playing rows) ------------------------------
struct Oracle {
    const Json& data;
    RecordedMeasurer measurer;
    std::vector<std::string> failures;
    std::size_t compared{};
    explicit Oracle(const Json& j) : data(j) { recordText(measurer, j["text"]); }
    static Json rooted(const Json& layer) {
        Json copy = layer;
        Point anchor{.5, .5};
        if (copy["anchorPoint"].isArray()) anchor = {copy["anchorPoint"].array()[0].number(), copy["anchorPoint"].array()[1].number()};
        const Rect b = rectOf(copy["bounds"]);
        copy["position"] = Json::Array{anchor.x * b.width + b.x, anchor.y * b.height + b.y};
        copy["transform"] = Json();
        return copy;
    }
    void scene(const Json& r, d::Canvas& canvas) {
        const std::string label = "nowPlaying/" + r["state"].string() + "/" + r["language"].string() + (r["light"].boolean() ? "/light" : "");
        ++compared;
        Json expected = r;
        expected["layer"] = rooted(r["layer"]);
        endfield::tests::design_oracle::compare(label, expected, canvas.root(), canvas.root(), canvas.page().controls(), failures);
        std::vector<d::AccessibilityItem> items; canvas.accessibilityItems(items);
        compareAccessibility(label, r["accessibility"], items, failures);
        if (r["top"].isObject()) endfield::tests::design_oracle::compare(label + "/top", Json::Object{{"layer", r["top"]}}, canvas.top(), canvas.root(), {}, failures);
    }
};
std::string applicationOf(std::string_view source) {
    if (source == "music") return "AppleInc.AppleMusicWin_nzyj5cx40ttqa!App";
    if (source == "spotify") return "Spotify.exe";
    if (source == "netease") return "cloudmusic.exe";
    return "Chrome";
}
void oracleScenes(Oracle& o) {
    for (const auto& r : o.data["nowPlaying"].array()) {
        const auto& in = r["input"];
        World w(o.measurer);
        w.now = in["now"].number();
        w.appearance(r["language"].string() == "zh-Hans" ? Language::simplifiedChinese : Language::english, r["light"].boolean());
        Controller ctl;
        if (in["track"].isObject()) {
            const auto& t = in["track"];
            m::NowPlayingViewInput i;
            i.session = 1;
            m::NowPlayingTrack track; track.title = t["title"].string(); track.artist = t["artist"].string(); track.album = t["album"].string();
            if (t["duration"].isNumber()) track.duration = t["duration"].number();
            if (t["position"].isNumber()) track.position = t["position"].number();
            track.isPlaying = t["isPlaying"].boolean(); track.sampledAt = t["sampledAt"].number(); track.supportsSeeking = t["supportsSeeking"].boolean();
            i.track = track;
            i.capabilities = {true, true, true, true, true, true, true, false, false};
            i.failed = in["failure"].boolean();
            i.application = applicationOf(in["source"].string());
            if (in["lyrics"].isString()) if (auto l = m::NowPlayingLyrics::parse(in["lyrics"].string())) i.lyrics = std::make_shared<const m::NowPlayingLyrics>(std::move(*l));
            if (in["playerVolume"].isNumber()) { i.volumeAvailable = true; i.volume = in["playerVolume"].number(); i.capabilities.volume = true; }
            ctl.input = std::move(i);
        }
        C canvas(w.context, ctl.services());
        canvas.setPresented(true, w.now);
        canvas.show(w.now); canvas.setActive(true, w.now);
        if (in["showsLyrics"].isBool() && !in["showsLyrics"].boolean()) canvas.perform("lyrics", w.now);
        if (in["isVolumeOpen"].boolean()) canvas.perform("volume", w.now);
        if (r["scrub"].isObject()) {
            const auto& down = r["scrub"]["down"].array(); const auto& drag = r["scrub"]["drag"].array();
            canvas.pointerDown({down[0].number(), down[1].number()}, {}, w.now);
            canvas.pointerDragged({drag[0].number(), drag[1].number()}, w.now);
        }
        o.scene(r, canvas);
        if (in["playLabel"].isString() && control(canvas, "playPause")->label != in["playLabel"].string()) o.failures.push_back(r["state"].string() + ": play label");
    }
}
} // namespace

int main(int argc, char** argv) {
    try {
        layout();
        behaviour();
        nothingPlaying();
        if (const auto data = loadOracle(argc > 1 ? argv[1] : nullptr, {"nowPlaying"}, {"text", "layerDefaults"})) {
            Oracle oracle(*data);
            oracleScenes(oracle);
            std::cout << "oracle: " << oracle.compared << " Now Playing scenes compared, " << oracle.measurer.misses.size() << " unrecorded measurements\n";
            // The Mac records the final trees' texts: the seek bubble's value
            // at the press (before the drag) is the expected miss.
            for (const auto& miss : oracle.measurer.misses) std::cout << "  unrecorded: " << miss << '\n';
            for (const auto& f : oracle.failures) std::cout << "  MISMATCH " << f << '\n';
            check(oracle.failures.empty(), std::to_string(oracle.failures.size()) + " oracle mismatches");
        } else std::cout << "play-canvases-source.json not present: Swift-derived contracts only\n";
        std::cout << "PASS " << checks << " Now Playing canvas checks\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL after " << checks << ": " << e.what() << '\n';
        return 1;
    }
}
