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
    unsigned chooses{}, cancels{}, closes{}; bool refuse{}, chooserBusy{};
    m::AppShortcutCanvasServices services() {
        m::AppShortcutCanvasServices s;
        s.request = [this](const m::AppShortcutRequest& r) { if (refuse) return false; requests.push_back(r); return true; };
        s.choose = [this] { ++chooses; return !chooserBusy; };
        s.cancelChooser = [this] { ++cancels; };
        s.close = [this] { ++closes; };
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
    // SHELL-108: the Mac's 8 x 8 premultiplied sample; CoreGraphics' filter rounds some cells by 1 of 255.
    check(c && mac && near((*c)[0], mac->r, 1. / 255) && near((*c)[1], mac->g, 1. / 255) && near((*c)[2], mac->b, 1. / 255), "The tile colour is the Mac's 8 x 8 sample");
}

void unified(const m::AppShortcutArtwork& art) {
    FakeMeasurer measurer;World w(measurer);w.appearance(Language::english);
    Owner owner;m::AppShortcutCanvas canvas(w.context,art,owner.services());
    auto file=fileOf({record("a","Alpha","original"),record("b","Beta","original"),record("c","Gamma","original")});
    file.groups={{"one","Work",{"b","a"}},{"two","Games",{"c"}}};
    canvas.setStore(file);w.show(canvas);
    check(canvas.items().size()==3 && control(canvas,"groups:overview") && control(canvas,"groups:tab:one"),"Overview and group tabs share a single shortcut page");
    check(!control(canvas,"apps:a:grip") && !control(canvas,"group:remove"),"No legacy list handles or internal delete-group button");
    auto click=[&](std::string_view id,unsigned count=1){const auto point=centre(*control(canvas,id),canvas.root());canvas.pointerDown(point,{point,d::PointerButton::left,false,false,count},w.now);canvas.pointerUp(point,w.now);};
    auto menuItem=[&](const std::string& id){control(canvas,"group:options")->activate();std::vector<d::AccessibilityItem> entries;canvas.accessibilityItems(entries);const auto found=std::find_if(entries.begin(),entries.end(),[&](const auto& entry){return entry.id=="shortcuts:options:"+id;});check(found!=entries.end(),"Action is listed in the single operations menu");return *found;};
    auto enabled=[&](const std::string& id){const auto entry=menuItem(id);canvas.closeMenu();return entry.enabled;};
    auto action=[&](const std::string& id){const auto entry=menuItem(id);const d::Point point{entry.rect.x+entry.rect.width/2,entry.rect.y+entry.rect.height/2};canvas.pointerDown(point,{point,d::PointerButton::left,false,false,1},w.now);canvas.pointerUp(point,w.now);};
    check(!canvas.rightPointerDown({-20,-20},w.now) && owner.closes==0,"Right click outside Apps does not close it");
    check(canvas.rightPointerDown(centre(*control(canvas,"group:item:a"),canvas.root()),w.now) && owner.closes==1 && owner.launches.empty(),"Right click on an app closes the HUD without launching");
    click("group:options");check(canvas.menuOpen(),"Actions opens before right-click dismissal");
    check(canvas.rightPointerDown({200,200},w.now) && !canvas.menuOpen() && owner.closes==1,"Right click dismisses an open menu before closing the HUD");
    check(canvas.rightPointerDown({200,200},w.now) && owner.closes==2,"A subsequent right click closes Apps");
    check(canvas.header().controls().size()==1 && control(canvas,"group:options")->label=="Actions","Only Actions is exposed in the page header");
    auto drag=[&](std::string_view id,double dx){const auto start=centre(*control(canvas,id),canvas.root());const d::Point end{start.x+dx,start.y};canvas.pointerDown(start,{start,d::PointerButton::left,false,false,1},w.now);canvas.pointerDragged(end,w.now);canvas.pointerUp(end,w.now);};
    click("group:item:a");check(owner.launches.empty(),"Single click never launches");
    click("group:item:a",2);check(owner.launches==std::vector<std::string>{"a"},"Double click launches original target");
    const auto before=owner.requests.size();drag("group:item:a",150);check(owner.requests.size()==before,"Normal mode drag cannot reorder");
    click("groups:tab:one");check(canvas.selectedGroup()=="one" && canvas.items()[0].id=="b" && canvas.items().size()==2,"A tab switches the grid with independent group order");
    check(owner.requests.back().kind==m::AppShortcutRequest::Kind::selectGroup,"Selected tab is persisted");
    action("edit");check(control(canvas,"group:rename")->enabled() && enabled("delete-group"),"Editing immediately enables current group rename and deletion");click("group:item:b");click("group:item:a");
    check(enabled("delete"),"Edit supports multi-selection");
    click("group:item:a",2);check(owner.launches.size()==1,"Editing never launches on double click");
    const d::Point blank{d::metric::boxWidth-20,d::metric::boxHeight-20};canvas.pointerDown(blank,{blank,d::PointerButton::left,false,false,1},w.now);canvas.pointerUp(blank,w.now);
    check(!enabled("delete") && control(canvas,"group:rename")->enabled(),"Blank clears shortcut selections and restores current-group rename");
    drag("group:item:b",m::appGridPitch);const auto move=owner.requests.back();check(move.kind==m::AppShortcutRequest::Kind::move && move.groupID=="one" && move.index==1,"Edit drag reorders only the selected group");
    std::swap(file.groups[0].itemIDs[0],file.groups[0].itemIDs[1]);canvas.requestFinished(move,file,{});
    click("group:item:a");click("group:item:b");action("delete");
    check(w.context.overlay().confirmState().request()->message.find("this group only")!=std::string::npos,"Group deletion clearly names membership scope");
    w.context.overlay().decide(d::ConfirmState::Decision::confirm);const auto removal=owner.requests.back();
    check(removal.kind==m::AppShortcutRequest::Kind::remove && removal.ids.size()==2 && removal.groupID=="one","Multi-delete is one atomic group-scoped request");
    canvas.showGroups();click("groups:tab:one");click("groups:tab:two");
    action("delete");w.context.overlay().decide(d::ConfirmState::Decision::confirm);
    check(owner.requests.back().kind==m::AppShortcutRequest::Kind::removeGroup && owner.requests.back().ids.size()==2,"Group multi-delete is one atomic request");
    drag("groups:tab:one",200);check(owner.requests.back().kind==m::AppShortcutRequest::Kind::moveGroup && owner.requests.back().index==1,"Tabs reorder by dragging in edit mode");
    canvas.showGroup({});action("edit");click("group:item:a");action("delete");
    check(w.context.overlay().confirmState().request()->message.find("all groups")!=std::string::npos,"Overview deletion clearly names global scope");w.context.overlay().decide(d::ConfirmState::Decision::cancel);
    for(int n=0;n<10;++n)file.groups.push_back({"extra"+std::to_string(n),"Extra",{}});canvas.setStore(file);
    check(canvas.handleScroll({200,d::metric::headerHeight+16},{0,200}),"Group strip supports horizontal scrolling from wheel");
    check(control(canvas,"groups:overview")->rect(canvas.root()).x==0,"Overview remains pinned during group strip scrolling");
    for(int n=0;n<40;++n)file.items.push_back(record("extraapp"+std::to_string(n),"Extra app","original"));canvas.setStore(file);
    const auto a=control(canvas,"group:item:a")->rect(canvas.root());
    const auto sixth=control(canvas,"group:item:extraapp2")->rect(canvas.root());
    const auto seventh=control(canvas,"group:item:extraapp3")->rect(canvas.root());
    check(a.width<100 && sixth.y==a.y && sixth.x+sixth.width<=d::metric::boxWidth-d::ScrollArea::stripWidth+.01 && seventh.y>=a.y+a.height,"Compact grid fits six apps per row without overlap");
    const auto offset=canvas.page().scrollAreas().front()->offset();
    check(canvas.handleScroll({200,d::metric::boxHeight-30},{0,48}) && canvas.page().scrollAreas().front()->offset()==offset+96,"App grid uses a larger smooth wheel step");
    canvas.page().scrollAreas().front()->setOffset(0,false);
    const auto from=centre(*control(canvas,"group:item:a"),canvas.root());
    const d::Point to{from.x+2*m::appGridPitch,from.y+3*m::appGridPitch};
    canvas.pointerDown(from,{from,d::PointerButton::left,false,false,1},w.now);canvas.pointerDragged(to,w.now);canvas.pointerUp(to,w.now);
    const auto gridMove=owner.requests.back();check(gridMove.kind==m::AppShortcutRequest::Kind::move && gridMove.index==20,"A grid drag targets its row and column");
    auto movedFile=file;const auto moved=movedFile.items.front();movedFile.items.erase(movedFile.items.begin());movedFile.items.insert(movedFile.items.begin()+20,moved);
    canvas.requestFinished(gridMove,movedFile,std::nullopt);
    check(canvas.page().scrollAreas().front()->offset()<m::appGridPitch,"Completing a move reveals the destination grid row, not the legacy list row");
    file.items.resize(3);
    m::AppShortcutCanvas restored(w.context,art,owner.services());file.activeGroupID="two";restored.setStore(file);w.show(restored);
    check(restored.selectedGroup()=="two" && restored.items().size()==1,"Saved tab restores without nested navigation");
    file.groups.erase(file.groups.begin()+1);restored.setStore(file);check(restored.selectedGroup().empty() && restored.items().size()==3,"Deleted selected group falls back to overview");
}
}
int main(int argc,char** argv) {
    try {
        check(argc>=3,"Expected oracle and artwork paths");layoutRules();const auto packaged=assets(argv[2]);
        const auto bytes=ehud::data::detail::readFile(std::filesystem::u8path(argv[1]),8*1024*1024);
        if(bytes)artworkOracle(Json::parse(*bytes),packaged->artwork(),argv[2]);
        unified(packaged->artwork());std::cout<<"Shortcut tabs/grid: "<<checks<<" checks passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL "<<checks<<": "<<e.what()<<'\n';return 1;}
}
