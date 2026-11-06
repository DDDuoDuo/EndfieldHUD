// Contracts of the redesigned Personal ID (modules/profile_canvas.*, modules/profile_state.*): the rules of
// Sources/PersonalProfileCanvas.swift (isValid, the save mapping, the slider specs and limits, the per-field
// sync locks, the interrupted drag, Follow HUD, the hours and best plates) over an in-memory store, and, when
// the Mac oracle is given (tests/fixtures/profile-canvas-source.json from tools/profile_canvas_reference.sh),
// every recorded scene node by node with the Mac's own text widths, its controls, accessibility items, header,
// toasts and the art / portrait geometry. Synthetic data only (no profile.json, no user images).
#include "modules/profile_canvas.hpp"
#include "native/profile_text.hpp"
#include "tests/hud_design_oracle.hpp"
#include <iostream>

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
    return nullptr;
}
d::Point centre(const d::Control& c, const d::Layer& root) { const auto r = c.rect(root); return {r.x + r.width / 2, r.y + r.height / 2}; }
constexpr double avatarWidth = 120, avatarHeight = 80;

struct World {
    d::TextMeasurer& measurer;
    d::Context context;
    double now{100};
    explicit World(d::TextMeasurer& mm) : measurer(mm), context(mm, d::asciiUppercase) { context.update(d::geometry::baseUnit, 2 / d::geometry::baseUnit, now); }
    void appearance(Language language, bool light = false, bool recording = false) {
        d::Context::Appearance a; a.language = language; a.light = light; a.reduceMotion = true; a.recordingMode = recording; context.apply(a, now);
    }
    void show(d::Canvas& c) { c.show(now); c.setActive(true, now); }
    std::optional<d::ToastRequest> toast() { return context.overlay().toastState().current(); }
};
// The synthetic owner: an in-memory store, image presence, the best score and the panels.
struct Owner {
    m::ProfileTextRules text = n::nativeProfileTextRules();
    m::ProfileDateRules dates = n::nativeProfileDateRules(u"UTC");
    std::vector<m::PersonalProfile> commits;
    std::vector<m::PersonalProfile> previews;
    std::int64_t best{3262}; double work{7 * 3600 + 1234};
    bool avatar{}, background{};
    std::vector<int> portraitSteps;
    unsigned chooses{}, colors{}, dismissals{}; std::vector<std::string> imports;
    bool refuseCommit{};
    std::unique_ptr<m::ProfileState> store(m::PersonalProfile p) {
        m::ProfilePersistence io;
        io.commit = [this](const m::PersonalProfile& v) { if (refuseCommit) throw m::ProfileError(m::ProfileFailure::persistence, "synthetic"); commits.push_back(v); return v; };
        return std::make_unique<m::ProfileState>(std::move(p), text, dates, std::move(io));
    }
    m::ProfileCanvasServices services() {
        m::ProfileCanvasServices s;
        s.workSeconds = [this] { return work; };
        s.minigameBest = [this] { return best; };
        s.geometryPreview = [this](const m::PersonalProfile& p) { previews.push_back(p); };
        s.portrait = [this](int step) {
            m::ProfileCanvasServices::Portrait out;
            if (!avatar) return out;
            portraitSteps.push_back(step);
            out.image = m::ProfilePortrait{{d::ImageSource::Kind::memory, "portrait." + std::to_string(step), {}, 1},
                std::max(1., avatarWidth / avatarHeight), std::max(1., avatarHeight / avatarWidth), step};
            return out;
        };
        s.backgroundReady = [this] { return background; };
        s.frame = [](std::array<double, 3>) { return std::optional(d::ImageSource{d::ImageSource::Kind::memory, "frame", {}, 1}); };
        s.chooseImage = [this](m::ProfileImageKind) { ++chooses; return true; };
        s.chooseColor = [this](std::array<double, 3>) { ++colors; return true; };
        s.dismissPanels = [this] { ++dismissals; };
        s.importImage = [this](m::ProfileImageKind, const std::string& path) { imports.push_back(path); return true; };
        return s;
    }
};
m::PersonalProfile sample() {
    auto p = m::PersonalProfile::defaults();
    p.uid = "1000000000"; p.name = "Endministrator"; p.tag = "1018"; p.introduction = "Talos-II field engineer. Prefers tea.";
    p.awakeningDate = 788'961'600; p.birthdayMonth = 3; p.birthdayDay = 14; p.permissionLevel = 42; p.explorationLevel = 5;
    p.operatorsCount = 23; p.weaponsCount = 41; p.archivesCount = 108; p.accumulatedWorkSeconds = 3600;
    return p;
}

// --- Rules ------------------------------------------------------------------------------------
void rules(const Json* data) {
    // PersonalProfileField.textLimit / syncField (:37-61).
    using F = m::ProfileField; using S = ehud::data::ProfileSyncField;
    check(m::profileTextLimit(F::name) == 20u && m::profileTextLimit(F::tag) == 11u && m::profileTextLimit(F::playerID) == 64u
          && m::profileTextLimit(F::introduction) == 150u && m::profileTextLimit(F::birthday) == 10u && m::profileTextLimit(F::explorationLevel) == 2u
          && m::profileTextLimit(F::archivesCount) == 5u && !m::profileTextLimit(F::avatarZoom), "Editor limits (tag 11, dates 10, levels 2, counters 5)");
    check(m::profileSyncField(F::playerID) == S::uid && m::profileSyncField(F::explorationLevel) == S::levels && !m::profileSyncField(F::introduction)
          && !m::profileSyncField(F::birthday) && !m::profileSyncField(F::backgroundWidth), "Sync owners per field (:50-61)");
    if (!data) return;
    for (const auto& row : (*data)["limits"].array()) {
        const auto field = m::profileField(row["field"].string());
        check(field.has_value(), "Field " + row["field"].string());
        const auto limit = m::profileTextLimit(*field);
        check(row["limit"].isNull() ? !limit : limit && *limit == std::size_t(row["limit"].integer()), "Limit of " + row["field"].string());
        const auto owner = m::profileSyncField(*field);
        check(row["sync"].isNull() ? !owner : owner && std::string(ehud::data::profileSyncFieldName(*owner)) == row["sync"].string(), "Sync owner of " + row["field"].string());
        check(m::profileFieldTitle(*field, Language::english) == row["title"].string(), "Title of " + row["field"].string());
    }
    // isValid (:794-813) against the Mac table.
    const auto text = n::nativeProfileTextRules(); const auto dates = n::nativeProfileDateRules(u"UTC");
    std::size_t rows{};
    for (const auto& row : (*data)["valid"].array()) {
        const auto field = *m::profileField(row["field"].string());
        check(m::ProfileCanvas::isValid(field, row["input"].string(), text, &dates) == row["valid"].boolean(),
              "isValid(" + row["field"].string() + ", '" + row["input"].string() + "')");
        ++rows;
    }
    check(rows > 40, "The Mac validity table is non-trivial");
}

// --- The Mac oracle -------------------------------------------------------------------------
struct Oracle {
    const Json& data;
    RecordedMeasurer measurer;
    std::vector<std::string> failures;
    std::size_t compared{};
    explicit Oracle(const Json& dd) : data(dd) { recordText(measurer, dd["text"]); }
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
    // The art's pixels are owner-drawn on Windows (see profile_canvas.hpp): its Mac layers are compared as numbers.
    static Json withoutArt(const Json& layer) {
        Json copy = layer;
        if (copy["name"].isString() && copy["name"].string() == "profile.art") return Json::Object{{"name", "profile.art"}, {"bounds", copy["bounds"]}, {"position", copy["position"]}, {"frame", copy["frame"]}};
        if (copy["children"].isArray()) { Json::Array kids; for (const auto& c : copy["children"].array()) kids.push_back(withoutArt(c)); copy["children"] = std::move(kids); }
        return copy;
    }
    void scene(std::string_view state, m::ProfileCanvas& canvas, World& w, std::string_view language = "en", bool light = false) {
        const Json* r = row(state, language, light);
        const std::string label = std::string(state) + "/" + std::string(language) + (light ? "/light" : "");
        if (!r) { failures.push_back(label + ": not in the oracle"); return; }
        ++compared;
        Json expected = *r;
        expected["layer"] = withoutArt(rooted((*r)["layer"]));
        compare(label, expected, canvas.root(), canvas.root(), canvas.page().controls(), failures);
        std::vector<d::AccessibilityItem> items; canvas.accessibilityItems(items);
        compareAccessibility(label, (*r)["accessibility"], items, failures);
        if ((*r)["top"].isObject()) compare(label + "/top", Json::Object{{"layer", (*r)["top"]}}, canvas.top(), canvas.root(), {}, failures);
        if (canvas.header().counterText() != (*r)["header"]["counter"].string()) failures.push_back(label + ": counter " + canvas.header().counterText());
        if (canvas.header().titleTexts().first != (*r)["header"]["title"].string()) failures.push_back(label + ": title " + canvas.header().titleTexts().first);
        if (canvas.textHidden() != (*r)["textHidden"].boolean() || canvas.inputLocked() != (*r)["inputLocked"].boolean() || canvas.presentingPanel() != (*r)["presentingPanel"].boolean())
            failures.push_back(label + ": hidden / input lock / panel");
        const auto toast = w.toast();
        if ((*r)["toast"].isObject() != toast.has_value() || (toast && (toast->title != (*r)["toast"]["title"].string() || toast->caption != (*r)["toast"]["caption"].string()
                || d::iconName(toast->icon) != (*r)["toast"]["icon"].string()))) failures.push_back(label + ": toast");
        // The art box (Mac: position + bounds in the host; Windows: the same frame as a translation of the
        // centre and a scale of the fixed model size) and the picture's zoom.
        if ((*r)["art"].isObject() != (canvas.artBox() != nullptr)) failures.push_back(label + ": art presence");
        else if (canvas.artBox()) {
            const auto& a = (*r)["art"];
            const auto* box = canvas.artBox(); const auto* picture = canvas.artPicture();
            const auto bounds = rectOf(a["boxBounds"]);
            const double w0 = box->bounds().width * box->value(Channel::scaleX), h0 = box->bounds().height * box->value(Channel::scaleY);
            const auto& position = a["boxPosition"].array();
            if (!near(w0, bounds.width, 1e-6) || !near(h0, bounds.height, 1e-6) || !near(box->value(Channel::translateX), position[0].number(), 1e-6)
                || !near(box->value(Channel::translateY), position[1].number(), 1e-6)) failures.push_back(label + ": art box frame");
            if (!near(picture->value(Channel::scaleX), a["pictureTransform"].array()[0].number(), 1e-9)) failures.push_back(label + ": art zoom");
            if (!near(rectOf(a["host"]), canvas.artSlot()->frame(), 1e-6)) failures.push_back(label + ": art host " + str(canvas.artSlot()->frame()));
        }
    }
    m::PersonalProfile profile(const Json& r) const {
        const auto& p = r["input"]["profile"];
        auto out = m::PersonalProfile::defaults();
        out.uid = p["uid"].string(); out.name = p["name"].string(); out.tag = p["tag"].string(); out.introduction = p["introduction"].string();
        if (p["playerIDOverride"].isString()) out.playerIDOverride = p["playerIDOverride"].string();
        out.awakeningDate = p["awakeningDate"].number(); out.showsBirthday = p["showsBirthday"].boolean();
        out.birthdayMonth = int(p["birthdayMonth"].integer()); out.birthdayDay = int(p["birthdayDay"].integer());
        out.permissionLevel = int(p["permissionLevel"].integer()); out.explorationLevel = int(p["explorationLevel"].integer());
        out.operatorsCount = p["operatorsCount"].integer(); out.weaponsCount = p["weaponsCount"].integer(); out.archivesCount = p["archivesCount"].integer();
        if (p["themeColorHex"].isString()) out.themeColorHex = p["themeColorHex"].string();
        if (p["avatar"].boolean()) out.avatarFilename = "00000000-0000-4000-8000-000000000001.image";
        if (p["background"].boolean()) out.backgroundFilename = "00000000-0000-4000-8000-000000000002.png";
        for (const auto& [key, field] : std::initializer_list<std::pair<const char*, double m::PersonalProfile::*>>{
                 {"avatarZoom", &m::PersonalProfile::avatarZoom}, {"avatarOffsetX", &m::PersonalProfile::avatarOffsetX}, {"avatarOffsetY", &m::PersonalProfile::avatarOffsetY},
                 {"backgroundWidth", &m::PersonalProfile::backgroundWidth}, {"backgroundZoom", &m::PersonalProfile::backgroundZoom},
                 {"backgroundOffsetX", &m::PersonalProfile::backgroundOffsetX}, {"backgroundOffsetY", &m::PersonalProfile::backgroundOffsetY},
                 {"thumbnailZoom", &m::PersonalProfile::thumbnailZoom}, {"thumbnailOffsetX", &m::PersonalProfile::thumbnailOffsetX},
                 {"thumbnailOffsetY", &m::PersonalProfile::thumbnailOffsetY}, {"accumulatedWorkSeconds", &m::PersonalProfile::accumulatedWorkSeconds}})
            out.*field = p[key].number();
        return out;
    }
};
ehud::data::ProfileSyncFields fieldsOf(const Json& list) {
    ehud::data::ProfileSyncFields out{};
    if (list.isArray()) for (const auto& name : list.array()) if (const auto f = ehud::data::profileSyncFieldNamed(name.string())) out = out.with(*f);
    return out;
}

// One fresh canvas per recorded state, rebuilt from the scenario input, then the state's action.
void oracleScenes(Oracle& o) {
    struct Case { std::string state, language{"en"}; bool light{}; };
    std::vector<Case> cases;
    for (const auto& r : o.data["states"].array()) cases.push_back({r["state"].string(), r["language"].string(), r["light"].boolean()});
    for (const auto& c : cases) {
        const Json& r = *o.row(c.state, c.language, c.light);
        World w(o.measurer);
        const auto language = c.language == "zh-Hans" ? Language::simplifiedChinese : c.language == "ja" ? Language::japanese : Language::english;
        w.appearance(language, c.light, r["input"]["recording"].boolean());
        Owner owner; owner.best = r["input"]["best"].integer(); owner.work = r["input"]["work"].number();
        auto p = o.profile(r);
        owner.avatar = p.avatarFilename.has_value(); owner.background = p.backgroundFilename.has_value();
        std::unique_ptr<m::ProfileState> store;
        m::ProfileCanvas canvas(w.context, owner.text, owner.dates, owner.services());
        if (r["input"]["store"].boolean()) {
            store = owner.store(p);
            store->setSyncLocked(fieldsOf(r["input"]["synced"]));
            canvas.setStore(store.get());
        } else {
            // The Mac's nil store shows UserProfile() (a random uid, today's awakening day): read them back
            // from the recorded accessibility labels.
            for (const auto& item : r["accessibility"].array()) {
                const auto& text = item["label"].string();
                if (item["id"].string() == "profile:playerID") p.uid = text.substr(text.rfind(' ') + 1);
                if (item["id"].string() == "profile:awakeningDate") {
                    const auto v = text.substr(text.rfind(' ') + 1);
                    p.awakeningDate = *m::profileParseAwakeningDate(v, owner.dates);
                }
                if (item["id"].string() == "profile:name") { const auto at = text.find(", "); p.name = text.substr(at + 2, text.rfind(" #") - at - 2); p.tag = text.substr(text.rfind('#') + 1); }
            }
            p.introduction.clear(); p.permissionLevel = 1; p.explorationLevel = 1; p.operatorsCount = p.weaponsCount = p.archivesCount = 0;
            for (const auto& item : r["accessibility"].array()) {
                const auto& text = item["label"].string(); const auto& id = item["id"].string();
                const auto number = [&] { return std::stoll(text.substr(text.rfind(' ') + 1)); };
                if (id == "profile:permissionLevel") p.permissionLevel = int(number());
                if (id == "profile:explorationLevel") p.explorationLevel = int(number());
                if (id == "profile:operatorsCount") p.operatorsCount = number();
                if (id == "profile:weaponsCount") p.weaponsCount = number();
                if (id == "profile:archivesCount") p.archivesCount = number();
                if (id == "profile:introduction" && text.find(": ") != std::string::npos) p.introduction = text.substr(text.find(": ") + 2);
                if (id == "profile:birthday" || id == "profile:awakeningDate") {}
            }
            p.accumulatedWorkSeconds = 0;
            canvas.setStoreError(p, r["toast"]["caption"].string());
        }
        w.show(canvas);
        const auto& s = c.state;
        const auto hover = [&](std::string_view id) { canvas.pointerMoved(centre(*control(canvas, id), canvas.root()), false, w.now); };
        if (s == "hoverName") hover("profile:name");
        else if (s == "hoverMore") hover("profile:menu");
        else if (s == "hoverEye") hover("profile:visibility");
        else if (s == "hoverTheme") hover("profile:backgroundMenu");
        else if (s == "moreMenu") canvas.perform("profile:menu", w.now);
        else if (s.starts_with("themePopover")) canvas.perform("profile:backgroundMenu", w.now);
        else if (s == "portraitPopover") { canvas.perform("profile:menu", w.now); canvas.perform("profile.more:portrait", w.now); }
        else if (s == "textHidden" || s == "imagesHidden") canvas.perform("profile:visibility", w.now);
        o.scene(c.state, canvas, w, c.language, c.light);
        canvas.setActive(false, w.now);
    }
}

// --- Behaviour (the Swift each check cites) ----------------------------------------------------
void behaviour() {
    FakeMeasurer measurer; World w(measurer); w.appearance(Language::english);
    Owner o; auto store = o.store(sample());
    m::ProfileCanvas canvas(w.context, o.text, o.dates, o.services());
    check(!canvas.page().layer().children().size(), "Before the record loads the page shows the header only");
    canvas.setStore(store.get()); w.show(canvas);
    check(canvas.hoursText() == "7.34" && canvas.bestText() == "3,262", "Work Mode hours max(accumulated, live)/3600 %.2f and the grouped best (:220-228)");
    o.best = 0; canvas.setActive(false, w.now); canvas.setActive(true, w.now);
    check(canvas.bestText() == "—", "No best score: an em dash, read once per visit (:212-223)");
    // Per-field locks (:235-244): the sync owns name and levels; introduction and birthday stay editable.
    store->setSyncLocked(ehud::data::ProfileSyncFields{}.with(ehud::data::ProfileSyncField::name).with(ehud::data::ProfileSyncField::levels));
    check(!canvas.canEdit(m::ProfileField::name) && !canvas.canEdit(m::ProfileField::permissionLevel) && canvas.canEdit(m::ProfileField::tag)
          && canvas.canEdit(m::ProfileField::introduction) && !control(canvas, "profile:name")->enabled() && control(canvas, "profile:introduction")->enabled(),
          "Locked fields are disabled, the others stay editable");
    store->setSyncLocked({});
    // Slider preview (:1032-1048): ticks preview without saving; the release saves all geometry once.
    canvas.perform("profile:backgroundMenu", w.now);
    const auto before = o.commits.size();
    check(canvas.adjustAccessibilitySlider("profile:backgroundWidth", 777, w.now), "A slider step commits");
    check(o.commits.size() == before + 1 && o.commits.back().backgroundWidth == 777 && !canvas.previewing(), "The release saves once");
    // An interrupted drag is reverted, not saved (:179-190): press and drag the zoom slider, then close.
    {
        std::vector<d::AccessibilityItem> items; canvas.accessibilityItems(items);
        const auto it = std::find_if(items.begin(), items.end(), [](const auto& i) { return i.id == "profile:backgroundZoom"; });
        check(it != items.end(), "The Card theme pop-over lists its sliders");
        const auto saved = o.commits.size();
        const d::Point start{it->rect.x + 4, it->rect.y + it->rect.height / 2}, end{it->rect.x + it->rect.width - 20, start.y};
        canvas.pointerDown(start, {}, w.now); canvas.pointerDragged(end, w.now);
        check(canvas.previewing() && o.commits.size() == saved && canvas.displayed().backgroundZoom > 1, "A drag previews in memory");
        canvas.closePopover();
        check(!canvas.previewing() && o.commits.size() == saved && store->profile().backgroundZoom == 1 && o.previews.back().backgroundZoom == 1,
              "Closing mid-drag reverts and re-sends the saved profile to the card");
        canvas.pointerUp(end, w.now);
    }
    // Follow HUD toggles (:1082-1095): off restores the last own colour (C9A2FF by default).
    canvas.perform("profile:backgroundMenu", w.now);
    const bool found = canvas.perform("theme:follow", w.now);
    check(store->profile().themeColorHex == std::optional<std::string>("C9A2FF"), "Follow HUD off restores the card's last colour (" + std::to_string(found) + ", " + store->profile().themeColorHex.value_or("nil") + ", " + (w.toast() ? w.toast()->caption : "") + ")");
    canvas.perform("theme:6EDFE8", w.now);
    canvas.perform("theme:follow", w.now);
    check(!store->profile().themeColorHex, "Follow HUD on clears the colour");
    canvas.perform("theme:follow", w.now);
    check(store->profile().themeColorHex == std::optional<std::string>("6EDFE8"), "and off again restores the one it replaced");
    canvas.perform("theme:custom", w.now);
    check(o.colors == 1 && canvas.colorPanelOpen() && canvas.presentingPanel() && canvas.inputLocked(), "Custom colour opens the panel and holds the HUD");
    canvas.colorChanged({1, 128. / 255, 0});
    check(store->profile().themeColorHex == std::optional<std::string>("FF8000"), "The panel's live colour becomes the custom hex (%02X)");
    canvas.colorPanelClosed();
    canvas.closePopover();
    // ⋯ menu (:890-916): Restore default picture toasts; with a picture chooser.
    canvas.perform("profile:menu", w.now); canvas.perform("profile.more:picture", w.now);
    check(o.chooses == 1 && canvas.chooserOpen(), "Change profile picture opens the chooser");
    canvas.chooserFinished(m::ProfileImageKind::avatar, "C:/synthetic/avatar.png");
    check(o.imports.size() == 1 && !canvas.chooserOpen(), "A chosen file is imported");
    canvas.imageImported(m::ProfileImageKind::avatar);
    check(w.toast() && w.toast()->title == "Picture changed", "A successful avatar import toasts Picture changed");
    canvas.perform("profile:menu", w.now); canvas.perform("profile.more:restore", w.now);
    check(w.toast() && w.toast()->title == "Default picture", "Restore default picture toasts");
    // Deactivation cancels the panels and reverts a drag (:171-177).
    canvas.perform("profile:menu", w.now); canvas.perform("profile.more:picture", w.now);
    canvas.setActive(false, w.now);
    check(o.dismissals == 1 && !canvas.chooserOpen(), "Leaving cancels the chooser");
    canvas.setActive(true, w.now);
    // The date kind (:735-738) and the eye (:710-733).
    canvas.perform("profile:toggleDateLabel", w.now);
    check(store->profile().showsBirthday, "The date chip toggles the shown day");
    canvas.perform("profile:visibility", w.now);
    check(canvas.textHidden() && !control(canvas, "profile:menu")->enabled(), "Hidden text disables the editables and ⋯");
    canvas.perform("profile:toggleDateLabel", w.now);
    check(store->profile().showsBirthday, "The date kind is refused while the text is hidden");
    canvas.perform("profile:visibility", w.now);
    // MODS-638: a new HUD session (the Mac's new PersonalProfileCanvas, :94) shows the text.
    canvas.perform("profile:visibility", w.now);
    canvas.setActive(false, w.now); canvas.resetSession();
    check(!canvas.textHidden() && control(canvas, "profile:menu")->enabled(), "A new HUD session shows the text");
    canvas.setActive(true, w.now);
    // A failed store save toasts (:230-233).
    o.refuseCommit = true;
    canvas.perform("profile:toggleDateLabel", w.now);
    check(w.toast() && w.toast()->title == "Notice", "A failed save toasts Notice");
    o.refuseCommit = false;
}
// A render-scale change while the Profile is hidden: the page re-renders when
// next shown, and its portrait for the new scale is asked at the change.
void hiddenScaleChange() {
    FakeMeasurer measurer; World w(measurer); w.appearance(Language::english);
    Owner o; o.avatar = true; auto profile = sample(); profile.avatarFilename = "00000000-0000-4000-8000-000000000001.image"; auto store = o.store(profile);
    m::ProfileCanvas canvas(w.context, o.text, o.dates, o.services());
    canvas.setStore(store.get()); w.show(canvas);
    check(!o.portraitSteps.empty(), "The shown Profile asks for its portrait");
    canvas.setActive(false, w.now); canvas.prepareToLeave(w.now);
    o.portraitSteps.clear();
    w.context.update(d::geometry::baseUnit, 3 / d::geometry::baseUnit, w.now);
    check(o.portraitSteps == std::vector<int>{1}, "A hidden Profile asks for its portrait at the new scale at once");
    w.show(canvas);
    check(o.portraitSteps.size() > 1, "Shown again, it re-renders at the new scale");
}

// The inline editor host: records the open request (its field) and the typed text.
struct FieldHost final : d::TextEditorHost {
    std::optional<d::TextEditRequest> request; std::string value;
    bool refuses{};
    unsigned begins{};
    bool begin(const d::TextEditRequest& r) override { if (refuses) return false; request = r; value = r.text; ++begins; return true; }
    void end(d::TextField&) override { request.reset(); }
    std::string text(const d::TextField&) const override { return value; }
    std::optional<std::pair<std::uint32_t, std::uint32_t>> selection(const d::TextField&) const override { return std::nullopt; }
    bool hasMarkedText(const d::TextField&) const override { return false; }
    void setText(d::TextField&, std::string_view v) override { value = v; }
    void reproject(d::TextField&, d::Rect) override {}
};
// The design scene's plan over two emissions (native/hud_design_scene.cpp
// syncContent): a part whose JSON is unchanged is reused, one with the same
// surfaces rasterizes only the changed ones, any other loads every surface.
std::size_t rasterized(const d::Artwork& before, const d::Artwork& after) {
    std::size_t count{};
    for (const auto& p : after.parts) {
        const auto old = std::find_if(before.parts.begin(), before.parts.end(), [&](const d::Part& q) { return q.id == p.id; });
        if (old == before.parts.end() || old->surfaces != p.surfaces) { count += p.surfaces.size(); continue; }
        if (old->layers == p.layers) continue;
        const auto& a = old->layers["children"].array(); const auto& b = p.layers["children"].array();
        for (std::size_t n = 0; n < b.size(); ++n) count += a[n] != b[n];
    }
    return count;
}
// The card fades (a module switch, the HUD opening and closing, the page
// stagger) as one image, as CALayer group opacity composites it: its plate,
// the art overlay and every value on it are one part of the scene, which
// gives each part its own group opacity. The inline editor, never on the
// card during a fade, is its own part: joining the card rasterizes only it.
void partsKept() {
    FakeMeasurer measurer; World w(measurer); w.appearance(Language::english);
    Owner o; o.avatar = true; auto store = o.store(sample());
    FieldHost host; w.context.setEditorHost(&host);
    m::ProfileCanvas canvas(w.context, o.text, o.dates, o.services());
    canvas.setStore(store.get()); w.show(canvas);
    const d::TextServices& text = w.context.env().text;
    const auto artwork = [&] { return d::emit(canvas.root(), {&text, "test."}); };
    const auto partOf = [](const d::Artwork& a, std::string_view surface) -> const d::Part* {
        for (const auto& p : a.parts) for (const auto& s : p.surfaces) if (s.id.find(surface) != std::string::npos) return &p;
        return nullptr;
    };
    const auto oneCard = [&](const d::Artwork& a, const char* when) {
        const auto* card = partOf(a, "profile.plate");
        const auto* overlay = partOf(a, "profile.overlay"); // with a background picture
        check(card && partOf(a, "profile:name") == card && partOf(a, "profile:toggleDateLabel") == card && partOf(a, "profile:operatorsCount") == card && (!overlay || overlay == card),
            std::string("The card's plate, overlay and values are one part ") + when);
    };
    auto before = artwork();
    oneCard(before, "at rest");
    std::size_t surfaces{};
    for (const auto& p : before.parts) surfaces += p.surfaces.size();
    canvas.perform("profile:name", w.now);
    check(canvas.editingField() == m::ProfileField::name, "The name's editor opens");
    const auto after = artwork();
    const auto count = rasterized(before, after);
    check(count <= 4, "The name's editor rasterizes " + std::to_string(count) + " of " + std::to_string(surfaces) + " surfaces (bound 4)");
    check(partOf(after, "edit:name") && partOf(after, "edit:name") != partOf(after, "profile:name"), "The inline editor is its own part");
    oneCard(after, "while editing");
    check(surfaces > 30, "The card has its surfaces (" + std::to_string(surfaces) + ")");
    // Esc only removes the editor. Opening it again builds the page again
    // (its arena keeps the first editor's id); the page keeps its ids, so
    // the reopened editor rasterizes no more than the first one.
    d::KeyInput esc; esc.virtualKey = d::key::escape;
    check(host.request && host.request->field->handleEditingKey(esc) && !canvas.editingField(), "Esc closes the editor");
    const auto closed = artwork();
    check(rasterized(before, closed) == 0 && closed.parts.size() == before.parts.size(), "Closing the editor rasterizes " + std::to_string(rasterized(before, closed)) + " surfaces (bound 0)");
    oneCard(closed, "after editing");
    canvas.perform("profile:name", w.now);
    check(canvas.editingField() == m::ProfileField::name, "The name's editor opens again");
    const auto reopened = artwork();
    check(rasterized(closed, reopened) <= 4, "The reopened editor rasterizes " + std::to_string(rasterized(closed, reopened)) + " of " + std::to_string(surfaces) + " surfaces (bound 4)");
    // The rebuilt page is the first open's scene again: the reopen rasterizes
    // exactly the first editor's surfaces, and none of the card's.
    check(rasterized(closed, reopened) == count && rasterized(after, reopened) == 0,
          "The reopen rasterizes only the editor (" + std::to_string(rasterized(closed, reopened)) + " vs " + std::to_string(count) + "; "
          + std::to_string(rasterized(after, reopened)) + " differ from the first open)");
    oneCard(reopened, "while editing again");
}

// Reopening an inline editor (the Windows 11 report: after editing the name
// or the bio the whole Profile dial went blank). Every open makes the editor
// "edit:<field>" on the page, as the Mac makes a new HUDDesignTextField each
// time; the page's arena refuses an id it already made, so an editor that
// ended without a re-render (Esc, an unchanged, normalized-equal, invalid or
// refused value) threw on the next open and the owner disabled the module.
void reopenEditors() {
    FakeMeasurer measurer; World w(measurer); w.appearance(Language::english);
    FieldHost host; w.context.setEditorHost(&host);
    Owner o; auto store = o.store(sample());
    m::ProfileCanvas canvas(w.context, o.text, o.dates, o.services());
    canvas.setStore(store.get()); w.show(canvas);
    const auto field = [&]() -> d::TextField& { check(host.request && host.request->field, "An editor is open"); return *host.request->field; };
    const auto attached = [&](const d::TextField& f) { return f.layer().isDescendant(canvas.page().layer()); };
    const auto open = [&](std::string_view id, m::ProfileField expected, const std::string& when) -> d::TextField& {
        try { canvas.perform(id, w.now); }
        catch (const std::exception& e) { throw std::runtime_error("Opening " + std::string(id) + " " + when + " threw: " + e.what()); }
        check(canvas.editingField() == expected && host.request && host.request->field->editing() && attached(*host.request->field),
              "The " + std::string(id) + " editor opens " + when);
        return field();
    };
    const auto press = [&](d::TextField& f, std::uint32_t key) { d::KeyInput k; k.virtualKey = key; check(f.handleEditingKey(k), "The editor takes the key"); };
    const auto escape = [&](d::TextField& f) { press(f, d::key::escape); check(!canvas.editingField() && !attached(f), "Esc closes the editor"); };
    const auto enter = [&](d::TextField& f, std::string typed) { host.value = std::move(typed); press(f, d::key::enter); check(!canvas.editingField(), "Enter closes the editor"); };

    // Esc, then the same field again (twice more: the dial stayed blank on the second open).
    escape(open("profile:name", m::ProfileField::name, "the first time"));
    escape(open("profile:name", m::ProfileField::name, "after Esc"));
    escape(open("profile:introduction", m::ProfileField::introduction, "the first time"));
    escape(open("profile:introduction", m::ProfileField::introduction, "after Esc"));
    // Committing the shown value, or one the record normalizes to it, saves nothing and re-renders nothing.
    enter(open("profile:name", m::ProfileField::name, "again"), "Endministrator");
    enter(open("profile:name", m::ProfileField::name, "after an unchanged commit"), "Endministrator ");
    check(store->profile().name == "Endministrator" && o.commits.empty(), "A trailing space saves nothing");
    enter(open("profile:name", m::ProfileField::name, "after a normalized-equal commit"), "");
    check(store->profile().name == "Endministrator", "An empty name is discarded");
    escape(open("profile:name", m::ProfileField::name, "after an invalid value"));
    enter(open("profile:explorationLevel", m::ProfileField::explorationLevel, "the first time"), "05");
    check(store->profile().explorationLevel == 5 && o.commits.empty(), "'05' saves nothing");
    escape(open("profile:explorationLevel", m::ProfileField::explorationLevel, "after '05'"));
    enter(open("profile:introduction", m::ProfileField::introduction, "again"), "Talos-II field engineer. Prefers tea.  ");
    escape(open("profile:introduction", m::ProfileField::introduction, "after a normalized-equal bio"));
    // The ⋯ menu's Edit # and Edit name (# with its sign is the same tag).
    canvas.perform("profile:menu", w.now);
    enter(open("profile.more:tag", m::ProfileField::tag, "from the menu"), "#1018");
    check(store->profile().tag == "1018" && o.commits.empty(), "'#1018' saves nothing");
    canvas.perform("profile:menu", w.now);
    escape(open("profile.more:tag", m::ProfileField::tag, "from the menu again"));
    canvas.perform("profile:menu", w.now);
    escape(open("profile.more:name", m::ProfileField::name, "from the menu after the name editor"));
    // A refused save toasts and keeps the field editable.
    o.refuseCommit = true;
    enter(open("profile:name", m::ProfileField::name, "before a refused save"), "Refused");
    check(store->profile().name == "Endministrator" && w.toast() && w.toast()->title == "Notice", "A refused save toasts Notice");
    o.refuseCommit = false;
    escape(open("profile:name", m::ProfileField::name, "after a refused save"));
    // An editor that cannot start (no host, or the host refused it) leaves nothing open.
    host.refuses = true;
    canvas.perform("profile:name", w.now);
    check(!canvas.editingField(), "A refused editor leaves no field editing");
    store->update([](m::PersonalProfile& p) { p.name = "Elsewhere"; });
    check(control(canvas, "profile:name") && control(canvas, "profile:name")->label.find("Elsewhere") != std::string::npos,
          "and a store change re-renders at once (no editor holds it back)");
    host.refuses = false;
    escape(open("profile:name", m::ProfileField::name, "after a refused editor"));
    // Another field invoked (UIA) while one is edited: the first commits and
    // re-renders the card; the second opens over the new card's element.
    open("profile:name", m::ProfileField::name, "before invoking the bio");
    host.value = "Renamed";
    {
        const auto commits = o.commits.size();
        auto& bio = open("profile:introduction", m::ProfileField::introduction, "while the name is edited");
        check(store->profile().name == "Renamed" && o.commits.size() == commits + 1, "The edited name commits first");
        const auto anchor = control(canvas, "profile:introduction");
        check(anchor && bio.layer().parent() && bio.layer().parent()->isDescendant(canvas.page().layer())
              && bio.layer().frame().x == anchor->layer().frame().x && bio.layer().frame().y == anchor->layer().frame().y,
              "The bio's editor lies over the re-rendered card's element");
        escape(bio);
    }
    // The name invoked again while its editor holds a longer name: the save
    // widens the name element, and the new editor takes the new element's
    // width (an element read before the save would give the old one).
    open("profile:name", m::ProfileField::name, "before invoking it again");
    host.value = "Endministrator Talos";
    {
        const auto old = control(canvas, "profile:name");
        const double oldWidth = old ? old->layer().frame().width : 0;
        auto& again = open("profile:name", m::ProfileField::name, "while it is edited");
        check(store->profile().name == "Endministrator Talos", "The edited name commits first");
        const auto anchor = control(canvas, "profile:name");
        check(anchor && anchor->layer().frame().width > std::max(oldWidth, 220.), "The saved name widens the name element");
        check(anchor && again.layer().frame().x == anchor->layer().frame().x && again.layer().frame().y == anchor->layer().frame().y
              && again.layer().frame().width == std::max(anchor->layer().frame().width, 220.),
              "The reopened name's editor takes the re-rendered element's frame");
        escape(again);
    }
    open("profile:name", m::ProfileField::name, "before invoking the bio unchanged");
    escape(open("profile:introduction", m::ProfileField::introduction, "while the name is edited unchanged"));
    escape(open("profile:name", m::ProfileField::name, "after being ended by the bio"));
    // A card colour chosen between two opens: the new editor carries it.
    const auto before = open("profile:name", m::ProfileField::name, "before a colour change").layer().id();
    const auto caretBefore = host.request->caretColor;
    escape(field());
    canvas.perform("profile:backgroundMenu", w.now); canvas.perform("theme:6EDFE8", w.now); canvas.closePopover();
    auto& recoloured = open("profile:name", m::ProfileField::name, "after a colour change");
    check(recoloured.layer().id() == before, "The editor keeps its id (the page keeps its ids)");
    check(host.request->caretColor != caretBefore, "The reopened editor takes the new card colour");
    escape(recoloured);
    // Leaving the Profile and coming back; closing and reopening the HUD.
    open("profile:name", m::ProfileField::name, "before leaving");
    canvas.setActive(false, w.now); canvas.prepareToLeave(w.now);
    check(!canvas.editingField(), "Leaving ends the editor");
    w.show(canvas);
    escape(open("profile:name", m::ProfileField::name, "after leaving and returning"));
    escape(open("profile:introduction", m::ProfileField::introduction, "after returning"));
    canvas.setActive(false, w.now); canvas.prepareToLeave(w.now); canvas.resetSession();
    w.show(canvas);
    escape(open("profile:name", m::ProfileField::name, "after the HUD closed and reopened"));
    escape(open("profile:introduction", m::ProfileField::introduction, "after the HUD reopened"));
    enter(open("profile:name", m::ProfileField::name, "for a real change"), "Endmin");
    check(store->profile().name == "Endmin", "A changed name still saves");
    escape(open("profile:name", m::ProfileField::name, "after a saved change"));
}
}

int main(int argc, char** argv) {
    try {
        std::optional<Json> data;
        if (argc > 1) data = loadOracle(argv[1], {"states"}, {"text", "layerDefaults", "valid", "limits", "sliders"});
        rules(data ? &*data : nullptr);
        behaviour(); hiddenScaleChange();
        partsKept(); reopenEditors();
        if (data) {
            Oracle oracle(*data);
            oracleScenes(oracle);
            for (const auto& f : oracle.failures) std::cout << "  MISMATCH " << f << '\n';
            if (!oracle.measurer.misses.empty()) std::cout << "  (" << oracle.measurer.misses.size() << " unrecorded widths)\n";
            check(oracle.failures.empty(), std::to_string(oracle.failures.size()) + " oracle mismatches");
            check(oracle.compared == (*data)["states"].array().size(), "Every recorded scene is compared");
            std::cout << "profile_canvas_contracts: " << oracle.compared << " Mac scenes matched\n";
        } else std::cout << "profile_canvas_contracts: no oracle given (rules and behaviour only)\n";
        std::cout << "profile_canvas_contracts passed " << checks << " checks\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << "profile_canvas_contracts failed: " << e.what() << '\n'; return 1; }
}
