// Sources/HUDSettingsCanvas.swift, statement by statement (line numbers cite it).
#include "modules/settings_canvas.hpp"
#include "modules/module_titles.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

namespace endfield::modules {
namespace d = design;
using ehud::data::Json;
using ehud::data::Settings;
namespace {
constexpr std::array<std::string_view, 6> languages{"system", "english", "simplifiedChinese", "traditionalChinese", "japanese", "korean"};
constexpr std::array<std::string_view, 3> themes{"dark", "light", "system"};
constexpr std::array<std::string_view, 2> clockFormats{"twentyFourHour", "twelveHour"};
constexpr std::array<std::string_view, 5> clockStyles{"digital", "split", "dial", "rail", "stacked"};
constexpr std::array<std::string_view, 6> metrics{"battery", "ram", "cpu", "network", "disk", "gpu"}; // HUDChargeMetric.allCases
constexpr std::array<std::string_view, 3> methods{"whenChargingStarts", "always", "never"};
constexpr std::array<std::string_view, 2> placements{"topCenter", "custom"};
constexpr std::array<std::string_view, 4> presetLogos{"endfield", "rhodesIsland", "babel", "rhineLab"};
constexpr double modulePitch = d::metric::rowHeight + d::metric::rowGap;
constexpr d::Size logoTile{120, 44};
constexpr double logoFieldHeight = 16;
// A saved value another build wrote reads as the first choice (the decoders' fallback).
std::string_view knownOr(std::string_view value, std::span<const std::string_view> values) {
    const auto found = std::find(values.begin(), values.end(), value);
    return found == values.end() ? values.front() : *found;
}
int indexOf(std::string_view value, std::span<const std::string_view> values) {
    const auto found = std::find(values.begin(), values.end(), value);
    return found == values.end() ? 0 : int(found - values.begin());
}
std::string stringOr(const Settings& s, std::string_view key) { const auto& v = s.fields[key]; return v.isString() ? v.string() : std::string(); }
// AppConfiguration's Bool keys (the 1.3.0 ones through their typed readers).
bool flag(const Settings& s, std::string_view key) {
    if (key == "summonHotkeyEnabled") return s.summonHotkeyEnabled();
    if (key == "recordingMode") return s.recordingMode();
    if (key == "dailyBootLog") return s.dailyBootLog();
    if (key == "facilityAlerts") return s.facilityAlerts();
    const auto& v = s.fields[key];
    return v.isBool() && v.boolean();
}
// URL.host / URL.path of an explicit https link (SettingsController::openLink refuses others).
std::pair<std::string, std::string> hostAndPath(std::string_view url) {
    constexpr std::string_view scheme = "https://";
    if (url.starts_with(scheme)) url.remove_prefix(scheme.size());
    const auto slash = url.find('/');
    return {std::string(url.substr(0, slash)), slash == std::string_view::npos ? std::string() : std::string(url.substr(slash))};
}
// HUDNavigationEntry.glyphLayer (HUDNavigation.swift:1186-1201): the right-hand
// list's glyph as one `side`-px node in ink white .12. The list's 32 px source
// tree (resources/watch-appearance) holds the visible form: a game icon raster
// (the trimmed artwork, drawn aspect-fit into the whole side and tinted
// source-in) or a vector already fitted to the 32 px box (scaled by side / 32).
std::optional<Json> glyphNode(const std::string& id, const Json& tree, double side) {
    const d::Color ink{.12, .12, .12, 1}; // NSColor(white: 0.12, alpha: 1)
    if (!tree["children"].isArray()) return std::nullopt;
    for (const auto& child : tree["children"].array()) {
        if (child["hidden"].isBool() && child["hidden"].boolean()) continue;
        const auto& contents = child["contents"];
        if (contents.isObject() && contents["asset"].isString() && contents["sha256"].isString())
            return d::image(id, {0, 0, side, side}, {d::ImageSource::Kind::asset, contents["asset"].string(), contents["sha256"].string(), 0}, "resizeAspect", ink);
        const auto& shape = child["shape"];
        if (!shape.isObject() || !shape["path"].isArray()) continue;
        const double k = side / 32;
        d::PathBuilder path;
        for (const auto& command : shape["path"].array()) {
            std::vector<d::Point> points;
            for (const auto& p : command["points"].array()) points.push_back({p.array()[0].number() * k, p.array()[1].number() * k});
            const auto op = command["op"].string();
            if (op == "move" && points.size() == 1) path.move(points[0]);
            else if (op == "line" && points.size() == 1) path.line(points[0]);
            else if (op == "cubic" && points.size() == 3) path.cubic(points[0], points[1], points[2]);
            else if (op == "close") path.close();
        }
        return d::shape(id, {0, 0, side, side}, path, ink, std::nullopt, {}, true);
    }
    return std::nullopt;
}
// HUDApplicationIcon.title (HUDApplicationIcon.swift:65-136): the icons whose
// names the Mac localizes (the roster's titles are the English ones; the
// game-menu icons and Endfield, D.D.D., S.W.E.E.P., Project Red stay English).
struct IconName { std::string_view id, english, simplifiedChinese; };
constexpr std::array<IconName, 32> localizedIcons{{
    {"battery", "Battery", "电池"},
    {"originium", "Originium", "源石"},
    {"orundum", "Orundum", "合成玉"},
    {"perlica", "Perlica", "佩丽卡"},
    {"rhodesIsland", "Rhodes Island", "罗德岛"},
    {"babel", "Babel", "巴别塔"},
    {"rhineLab", "Rhine Lab", "莱茵生命"},
    {"blacksteel", "Blacksteel", "黑钢国际"},
    {"penguinLogistics", "Penguin Logistics", "企鹅物流"},
    {"eliteOps", "Elite Operators", "精英干员"},
    {"alphaFour", "Reserve Team A4", "预备行动组 A4"},
    {"alphaSix", "Reserve Team A6", "预备行动组 A6"},
    {"humanResources", "Human Resources", "人力资源部"},
    {"rimBilliton", "Rim Billiton", "雷姆必拓"},
    {"kjerag", "Kjerag", "谢拉格"},
    {"ursusStudents", "Ursus Student Group", "乌萨斯学生自治团"},
    {"obsidianFestival", "Obsidian Festival", "黑曜石音乐节"},
    {"dossoles", "Dossoles", "多索雷斯"},
    {"contingencyContract", "Contingency Contract", "危机合约"},
    {"ambienceSynesthesia", "Ambience Synesthesia", "音律联觉"},
    {"coralCoast", "Coral Coast", "珊瑚海岸"},
    {"vitafield", "Vitafield", "生命之地"},
    {"ccBarrenland", "Contingency Contract: Barrenland", "危机合约：荒芜行动"},
    {"ccPyrite", "Contingency Contract: Pyrite", "危机合约：黄铁行动"},
    {"ccCinder", "Contingency Contract: Cinder", "危机合约：燃灰行动"},
    {"ccLeadSeal", "Contingency Contract: Lead Seal", "危机合约：铅封行动"},
    {"ccSpectrum", "Contingency Contract: Spectrum", "危机合约：光谱行动"},
    {"ccPineSoot", "Contingency Contract: Pine Soot", "危机合约：松烟行动"},
    {"rhodesKitchen", "Rhodes Kitchen", "罗德厨房"},
    {"cambrian", "Cambrian", "寒武纪"},
    {"marthe", "Marthe", "玛尔特"},
    {"icefieldMessenger", "Icefield Messenger", "冰原信使"}}};
d::Menu::Item menuItem(std::string id, std::string title) {
    d::Menu::Item item;
    item.id = std::move(id); item.title = std::move(title);
    return item;
}
std::vector<std::string> split(std::string_view text, std::string_view separator) {
    std::vector<std::string> out;
    for (std::size_t start = 0;;) {
        const auto at = text.find(separator, start);
        out.emplace_back(text.substr(start, at == std::string_view::npos ? std::string_view::npos : at - start));
        if (at == std::string_view::npos) return out;
        start = at + separator.size();
    }
}
}

// HUDIconTile (:1118-1155): an image tile with the selected L-brackets (App / menu bar icon picker).
class SettingsIconTile final : public d::Control {
public:
    SettingsIconTile(d::Builder& b, std::string id, const SettingsIcon& icon, std::string title, d::Size size)
        : Control(b.env, id, std::move(title), b.layer(id)), well_(&b.layer(id + ".well")), glyph_(&b.layer(id + ".glyph")), brackets_(&b.layer(id + ".brackets")) {
        const d::Rect bounds{0, 0, size.width, size.height};
        layer().setBounds(size);
        well_->setFrame({4, 4, size.width - 8, size.height - 8}); well_->setAnimatable();
        const std::array wellColors{d::rgb(0x2E2E2E), d::rgb(0x1C1C1C)};
        well_->setContent(d::plate(well_->id(), d::localBounds(*well_), wellColors, {}, 9));
        glyph_->setFrame({size.width / 2 - 30, size.height / 2 - 30, 60, 60}); glyph_->setAnimatable();
        glyph_->setContent(d::image(glyph_->id(), d::localBounds(*glyph_), icon.image, "resizeAspect"));
        brackets_->setFrame(bounds); brackets_->setAnimatable();
        d::setPath(*brackets_, d::ArchiveTile::bracketPath(bounds, 0), palette().yellow());
        for (d::Layer* part : {well_, glyph_, brackets_}) layer().add(*part);
        setTarget(bounds);
        render(false);
    }
    bool selected() const noexcept { return selected_; }
    void setSelected(bool value) { if (value != selected_) { selected_ = value; render(true); } }
    void hoverChanged() override {
        const bool on = hovered() && enabled();
        d::motion::animate(*well_, d::Channel::translateY, on ? -2 : 0, d::duration::hover, clock());
        d::motion::animate(*glyph_, d::Channel::translateY, on ? -3 : 0, .3, clock());
    }
    std::optional<d::AccessibilityRole> accessibilityRole() const override { return d::AccessibilityRole::tab; }
    void accessibilityItems(const d::Layer& root, std::vector<d::AccessibilityItem>& out) const override {
        if (label.empty()) return;
        auto item = accessibilityItem(root, d::AccessibilityRole::tab); item.on = selected_;
        out.push_back(std::move(item));
    }
private:
    d::Layer *well_, *glyph_, *brackets_;
    bool selected_{};
    void render(bool animated) {
        brackets_->setHidden(!selected_);
        if (selected_ && animated) d::motion::enter(*brackets_, {}, clock(), .2);
    }
};

SettingsCanvas::SettingsCanvas(core::Module module, d::Context& context, SettingsController& controller, SettingsCanvasHooks hooks)
    : Canvas(module, context), controller_(&controller), hooks_(std::move(hooks)) {
    if (module != core::Module::system && module != core::Module::display && module != core::Module::hotkeys && module != core::Module::about)
        throw std::invalid_argument("Settings presents System, Display, Hotkeys and About");
    if (module == core::Module::system && hooks_.displays) displays_ = hooks_.displays();
    checkTimer_ = timerGroup();
    observer_ = controller_->addObserver([this] { controllerChanged(); });
}
// deinit: a recording this canvas started ends with it.
SettingsCanvas::~SettingsCanvas() {
    controller_->removeObserver(observer_);
    if (capturingShortcut()) controller_->endShortcutCapture();
}
// HUDSettingsController.update { mutation } on the effective configuration.
template <class F> void SettingsCanvas::update(F&& mutation) {
    auto next = config();
    mutation(next);
    controller_->update(std::move(next), now());
}

// MARK: Lifecycle (:79-94)
void SettingsCanvas::didActivate() {
    lastStatus_ = currentStatus();
    controllerChanged();
}
void SettingsCanvas::didDeactivate() {
    cancelCapture();
    // An interrupted drag never creates a preview after the HUD has gone;
    // an unanswered layout preview is reverted with its prompt.
    stagedScale_.reset(); stagedX_.reset(); stagedY_.reset();
    if (layoutPromptOpen_) { layoutPromptOpen_ = false; controller_->revertLayout(); }
    if (section_ != Section::main && (module() != core::Module::display || section_ != Section::battery)) section_ = Section::main;
    cancel(checkTimer_); checkProgress_.reset();
}

SettingsCanvas::Structure SettingsCanvas::structure() const {
    const bool about = module() == core::Module::about;
    return {section_, env().language, stringOr(config(), "language"), displays_, capturingShortcut(), stringOr(config(), "applicationIcon"),
        stringOr(config(), "accentHex"), about ? controller_->updateStatus().phase : SettingsUpdatePhase::idle,
        about ? controller_->updateStatus().latestVersion : std::string()};
}

// controllerChanged (:103-112).
void SettingsCanvas::controllerChanged() {
    if (!renderedLanguage()) return;
    if (structure() != renderedStructure_) rerender();
    else { const auto binders = binders_; for (const auto& bind : binders) bind(); }
    if (!isActive()) return;
    updateLayoutPrompt();
    // A language change re-localizes the same status: only a new one toasts.
    const auto status = currentStatus();
    const auto language = env().language;
    if (status && status != lastStatus_ && language == lastStatusLanguage_) toast(d::Icon::warn, "Notice", "提示", *status);
    lastStatus_ = status; lastStatusLanguage_ = language;
}
// currentStatus (:114-121): an empty Windows status reads as none.
std::optional<std::string> SettingsCanvas::currentStatus() const {
    const auto some = [](const std::string& s) { return s.empty() ? std::nullopt : std::optional(s); };
    switch (module()) {
    case core::Module::system: return some(controller_->loginStatus()).has_value() ? some(controller_->loginStatus()) : some(controller_->status());
    case core::Module::hotkeys: return some(controller_->shortcutStatus()).has_value() ? some(controller_->shortcutStatus()) : some(controller_->status());
    case core::Module::about: return controller_->updateStatus().phase == SettingsUpdatePhase::failed ? some(controller_->updateStatus().detail) : std::nullopt;
    default: return some(controller_->status());
    }
}
void SettingsCanvas::displaysChanged() {
    if (module() != core::Module::system || !hooks_.displays) return;
    displays_ = hooks_.displays();
    controllerChanged();
}
void SettingsCanvas::artworkChanged() {
    if (!renderedLanguage()) return;
    const auto binders = binders_;
    for (const auto& bind : binders) bind();
}

// Opened from the Power screen's battery link (:124-128).
void SettingsCanvas::showBatterySettings() {
    if (module() != core::Module::display) return;
    settleSwap(); section_ = Section::battery;
    if (renderedLanguage()) rerender();
}
// setCustomColor (:130-135): "%02X%02X%02X" of the rounded sRGB components.
void SettingsCanvas::setCustomColor(std::array<double, 3> color) {
    constexpr char digits[] = "0123456789ABCDEF";
    std::string hex;
    for (double c : color) {
        if (!std::isfinite(c)) return;
        const auto v = unsigned(std::lround(std::clamp(c, 0., 1.) * 255));
        hex += digits[v >> 4]; hex += digits[v & 15];
    }
    update([&](Settings& s) { s.set("accentHex", hex); });
}
void SettingsCanvas::setCustomLogo(std::string revision) {
    update([&](Settings& s) { s.set("centerLogo", "custom"); s.set("centerLogoRevision", std::move(revision)); });
}
void SettingsCanvas::showImportError(std::string error) { toast(d::Icon::warn, "Import failed", "导入失败", std::move(error)); }
std::string_view SettingsCanvas::section() const noexcept {
    switch (section_) {
    case Section::battery: return "battery";
    case Section::icons: return "icons";
    case Section::credits: return "credits";
    case Section::modules: return "modules";
    case Section::main: break;
    }
    return "main";
}

// MARK: Pages (:145-164)
void SettingsCanvas::buildPage(d::Page& page) {
    binders_.clear(); updateCells_ = nullptr; updateText_ = nullptr; updateSpoken_ = nullptr; updateStamp_ = nullptr; updateStrip_ = nullptr; aboutPage_ = nullptr; keycaps_ = nullptr;
    moduleRows_.clear(); modulesScroll_ = nullptr; moduleDrag_.reset();
    renderedStructure_ = structure();
    switch (module()) {
    case core::Module::system: section_ == Section::modules ? buildModules(page) : buildSystem(page); break;
    case core::Module::display: section_ == Section::battery ? buildBattery(page) : section_ == Section::icons ? buildIcons(page) : buildDisplay(page); break;
    case core::Module::hotkeys: buildHotkeys(page); break;
    default: section_ == Section::credits ? buildCredits(page) : buildAbout(page); break;
    }
}
void SettingsCanvas::go(Section next, bool forward) {
    section_ = next;
    navigate(forward ? 1 : -1);
}

// buildSystem (:166-208).
void SettingsCanvas::buildSystem(d::Page& page) {
    const auto& c = config();
    std::vector<d::RowPlate*> rows;
    const auto languageSetting = stringOr(c, "language");
    auto& language = dropdown(page, "language", text("Language", "语言"), languageTitle(languageSetting),
        core::regionNaming(core::languageFromSetting(languageSetting).value_or(core::Language::system)), [this](const d::Dropdown& anchor) {
            std::vector<d::Menu::Item> items;
            const auto current = stringOr(config(), "language");
            for (const auto id : languages) {
                auto item = menuItem(std::string(id), languageTitle(id));
                item.language = core::regionNaming(*core::languageFromSetting(id)); item.selected = id == current;
                items.push_back(std::move(item));
            }
            openMenu("language", std::move(items), anchor, std::nullopt, [this](const d::Menu::Item& item) {
                if (std::find(languages.begin(), languages.end(), item.id) == languages.end()) return;
                update([&](Settings& s) { s.set("language", item.id); });
            });
        });
    rows.push_back(&row("language", text("Language", "语言"), language, page));
    rows.push_back(&toggleRow("login", text("Launch at login", "登录时启动"), "launchAtLogin", page));
    rows.push_back(&toggleRow("focus", text("Close when focus lost", "失去焦点时关闭"), "closeOnFocusLost", page));
    auto& screen = dropdown(page, "screen", text("Display", "显示器"), displaySelectionTitle(), std::nullopt, [this](const d::Dropdown& anchor) { openDisplayMenu(anchor); });
    binders_.push_back([this, &screen] { screen.setValue(displaySelectionTitle()); });
    rows.push_back(&row("screen", text("Display", "显示器"), screen, page));
    // Battery alerts live on Display > Battery alert (its Display method).
    auto& shown = page.own<d::Chip>("modules.count", modulesCount(), d::Chip::Variant::dark);
    const auto modulesTitle = text("Modules", "模块");
    auto& modules = navRow("modules", modulesTitle, page, [this] { go(Section::modules, true); }, "›", {&shown.layer()});
    modules.label = modulesTitle + ", " + modulesCount();
    binders_.push_back([this, &shown, &modules, modulesTitle] { shown.setTitle(modulesCount()); modules.label = modulesTitle + ", " + modulesCount(); });
    rows.push_back(&modules);
    auto& restore = page.make<d::Button>("restore", text("Restore", "恢复"), d::Icon::reset, d::Button::Kind::secondary, d::Button::Height::compact);
    restore.place({0, 0});
    restore.onActivate = [this] { confirmRestore(); };
    rows.push_back(&row("restore", text("Restore default settings", "恢复默认设置"), {&restore.layer()}, page, {}, {&restore}));
    // Windows only: the macImport area's entry, as a nav row.
    if (hooks_.macImportTitle) if (auto title = hooks_.macImportTitle(env().language))
        rows.push_back(&navRow("macImport", *title, page, [this] { if (hooks_.macImport) hooks_.macImport(); }));
    stack(rows, page.layer(), page, d::metric::headerHeight);
    header().configure({"SYSTEM"}, "SYS-01", title("System", "系统设置"), d::format::pad2(std::int64_t(rows.size())), false);
}

// openDisplayMenu (:210-236): Windows monitors by persistent id.
void SettingsCanvas::openDisplayMenu(const d::Control& anchor) {
    if (hooks_.displays) displays_ = hooks_.displays();
    const auto& c = config();
    const auto saved = stringOr(c, "windowsDisplayID");
    const bool pointer = c.fields["openOnActiveDisplay"].isBool() && c.fields["openOnActiveDisplay"].boolean();
    std::vector<d::Menu::Item> items;
    auto first = menuItem("pointer", text("Pointer display", "鼠标所在显示器")); first.selected = saved.empty() && pointer; items.push_back(std::move(first));
    auto main = menuItem("main", text("Main display", "主显示器")); main.selected = saved.empty() && !pointer; items.push_back(std::move(main));
    for (std::size_t index = 0; index < displays_.size(); ++index) {
        const auto& display = displays_[index];
        const bool duplicate = std::count_if(displays_.begin(), displays_.end(), [&](const auto& other) { return other.name == display.name; }) > 1;
        auto item = menuItem(display.id, duplicate ? display.name + " / " + std::to_string(index + 1) : display.name);
        item.selected = saved == display.id; item.separatorBefore = index == 0;
        items.push_back(std::move(item));
    }
    if (!saved.empty() && std::none_of(displays_.begin(), displays_.end(), [&](const auto& d) { return d.id == saved; })) {
        auto item = menuItem(saved, displaySelectionTitle());
        item.selected = true; item.separatorBefore = true; item.enabled = false;
        items.push_back(std::move(item));
    }
    openMenu("screen", std::move(items), anchor, std::nullopt, [this](const d::Menu::Item& item) {
        const auto& value = item.id;
        const auto found = std::find_if(displays_.begin(), displays_.end(), [&](const auto& d) { return d.id == value; });
        if (value != "pointer" && value != "main" && found == displays_.end()) return;
        update([&](Settings& s) {
            if (found == displays_.end()) { s.set("windowsDisplayID", nullptr); s.set("windowsDisplayName", nullptr); }
            else { s.set("windowsDisplayID", found->id); s.set("windowsDisplayName", found->name); }
            s.set("openOnActiveDisplay", value != "main");
        });
    });
}
// HUDDisplayPolicy.selectionTitle (HUDDisplayPolicy.swift:56-62).
std::string SettingsCanvas::displaySelectionTitle() const {
    const auto& c = config();
    const auto saved = stringOr(c, "windowsDisplayID");
    if (saved.empty()) return c.fields["openOnActiveDisplay"].isBool() && c.fields["openOnActiveDisplay"].boolean() ? text("Pointer display", "鼠标所在显示器") : text("Main display", "主显示器");
    const auto found = std::find_if(displays_.begin(), displays_.end(), [&](const auto& d) { return d.id == saved; });
    if (found != displays_.end()) return found->name;
    const auto name = stringOr(c, "windowsDisplayName");
    return (name.empty() ? text("Saved display", "已选显示器") : name) + text(" (disconnected)", "（未连接）");
}
// confirmRestore (:238-245).
void SettingsCanvas::confirmRestore() {
    d::ConfirmRequest request;
    request.id = "restore"; request.title = text("Restore default settings?", "恢复默认设置？");
    request.message = text("Notes, files and shortcuts are kept.", "保留便笺、文件与应用快捷方式。");
    request.confirmTitle = text("Restore", "恢复"); request.icon = d::Icon::reset;
    confirm(std::move(request), [this] {
        controller_->restoreDefaults();
        toast(d::Icon::reset, "Settings restored", "已恢复默认设置");
    });
}

// MARK: Modules (System > Modules, :249-398)
std::string SettingsCanvas::modulesCount() const {
    const auto layout = config().moduleLayout();
    return std::to_string(layout.visible().size()) + " / " + std::to_string(layout.order.size());
}
// Order and visibility of the right-hand list: drag a row by its grip (the
// others reflow) or use its move buttons; the switch shows or hides it.
// Windows: a row's slot is its model translateY from where it was built (the
// Mac animates position.y; same model geometry for hits, same .24 s SNAP).
void SettingsCanvas::buildModules(d::Page& page) {
    const auto layout = config().moduleLayout();
    const double width = rowWidth();
    auto& scroll = page.scrollArea("modules", {0, d::metric::headerHeight, d::metric::boxWidth, d::metric::bodyHeight});
    modulesScroll_ = &scroll;
    std::vector<d::RowPlate*> plates;
    for (const auto& module : layout.order) {
        const auto title = moduleTitle(module, env().language).value_or(module);
        auto& grip = page.make<d::DragGrip>("grip:" + module, "", d::Size{20, 30}, d::Point{7, 10});
        grip.onDrag = [this, module](d::DragGrip::Phase phase, d::Point point) { dragModule(module, phase, point); };
        auto& up = page.make<d::IconButton>("up:" + module, title + ", " + text("Move up", "上移"), d::Icon::left, d::IconButton::Style::light, false, false, 26., 13.);
        auto& down = page.make<d::IconButton>("down:" + module, title + ", " + text("Move down", "下移"), d::Icon::right, d::IconButton::Style::light, false, false, 26., 13.);
        // The back/forward chevrons, turned to point up and down.
        up.layer().setBaseTransform(d::Affine::rotation(std::numbers::pi / 2));
        down.layer().setBaseTransform(d::Affine::rotation(std::numbers::pi / 2));
        up.onActivate = [this, module] { moveModule(module, -1); };
        down.onActivate = [this, module] { moveModule(module, 1); };
        auto& toggle = page.make<d::Toggle>("show:" + module, title, !layout.isHidden(module));
        toggle.onChange = [this, module](bool on) { update([&](Settings& s) { auto l = s.moduleLayout(); l.setHidden(module, !on); s.setModuleLayout(l); }); };
        auto& plate = row("module:" + module, title, {&up.layer(), &down.layer(), &toggle.layer()}, page, {}, {&grip, &up, &down, &toggle});
        d::Layer& glyph = page.makeLayer("module:" + module + ".glyph");
        glyph.setFrame({0, 0, 24, 24}); glyph.setAnimatable();
        if (const auto found = hooks_.moduleGlyphs.find(module); found != hooks_.moduleGlyphs.end())
            if (auto node = glyphNode(glyph.id(), found->second, 24)) glyph.setContent(std::move(*node));
        plate.setTitleInset(56);
        const double middle = 10 + (plate.layer().bounds().height - 18) / 2 + 1;
        grip.layer().setPosition({26, middle});
        glyph.setPosition({54, middle});
        plate.layer().add(glyph); plate.layer().add(grip.layer());
        moduleRows_[module] = {&plate, &grip, &glyph, &toggle, &up, &down, 0};
        plates.push_back(&plate);
    }
    auto& restore = page.make<d::Button>("modulesReset", text("Restore", "恢复"), d::Icon::reset, d::Button::Kind::secondary, d::Button::Height::compact);
    restore.place({0, 0});
    restore.onActivate = [this] { confirmModulesReset(); };
    plates.push_back(&row("modulesReset", text("Restore default layout", "恢复默认布局"), {&restore.layer()}, page, {}, {&restore}));
    scroll.setContentHeight(stack(plates, scroll.content(), page, 0, width));
    page.restoreOffset(scroll);
    for (auto& [module, entry] : moduleRows_) entry.top = entry.plate->layer().frame().y;
    renderModuleStates();
    binders_.push_back([this, &restore] {
        renderModuleStates();
        restore.setEnabled(!config().moduleLayout().isDefault());
        // A drag in progress owns the positions until it drops.
        if (!moduleDrag_) layoutModuleRows(config().moduleLayout().order, true);
    });
    restore.setEnabled(!layout.isDefault());
    header().configure({"SYSTEM", "MODULES"}, "SYS-01", title("Modules", "模块"), d::format::pad2(std::int64_t(layout.order.size())), true);
    wireBack(page, [this] { go(Section::main, false); });
}
// renderModuleStates (:312-325): switches, move buttons and the hidden rows' dimmed glyph.
void SettingsCanvas::renderModuleStates() {
    const auto layout = config().moduleLayout();
    for (std::size_t index = 0; index < layout.order.size(); ++index) {
        const auto found = moduleRows_.find(layout.order[index]);
        if (found == moduleRows_.end()) continue;
        auto& row = found->second;
        const bool hidden = layout.isHidden(layout.order[index]);
        row.toggle->setOn(!hidden, true);
        // The last visible module cannot be hidden.
        row.toggle->setEnabled(layout.canSetHidden(layout.order[index], true));
        row.up->setEnabled(index > 0);
        row.down->setEnabled(index < layout.order.size() - 1);
        d::motion::animate(*row.glyph, d::Channel::opacity, hidden ? .35 : 1, d::duration::hover, env().clock(), d::easing::ease);
    }
}
// layoutModuleRows (:327-335): each row at its slot of `order`; the dragged row follows the pointer instead.
void SettingsCanvas::layoutModuleRows(const std::vector<std::string>& order, bool animated) {
    for (std::size_t index = 0; index < order.size(); ++index) {
        if (moduleDrag_ && order[index] == moduleDrag_->module) continue;
        const auto found = moduleRows_.find(order[index]);
        if (found == moduleRows_.end()) continue;
        auto& plate = found->second.plate->layer();
        const double offset = double(index) * modulePitch - found->second.top;
        if (plate.value(d::Channel::translateY) == offset) continue;
        if (animated) d::motion::animate(plate, d::Channel::translateY, offset, .24, env().clock(), d::easing::snap);
        else plate.setValue(d::Channel::translateY, offset);
    }
}
// moveModule (:337-346).
void SettingsCanvas::moveModule(const std::string& module, int offset) {
    const auto order = config().moduleLayout().order;
    const auto found = std::find(order.begin(), order.end(), module);
    if (found == order.end()) return;
    const auto index = found - order.begin();
    update([&](Settings& s) { auto l = s.moduleLayout(); l.move(module, index + offset); s.setModuleLayout(l); });
    auto* scroll = modulesScroll_;
    const auto next = config().moduleLayout().order;
    const auto moved = std::find(next.begin(), next.end(), module);
    if (!scroll || moved == next.end()) return;
    // Keep the moved row in view.
    const double top = double(moved - next.begin()) * modulePitch, bottom = top + d::metric::rowHeight, height = scroll->layer().bounds().height;
    const double target = top < scroll->offset() ? top : bottom > scroll->offset() + height ? bottom - height : scroll->offset();
    if (target != scroll->offset()) scroll->setOffset(target, true);
}
// dragModule (:348-380).
void SettingsCanvas::dragModule(const std::string& module, d::DragGrip::Phase phase, d::Point point) {
    auto* scroll = modulesScroll_;
    const auto found = moduleRows_.find(module);
    if (!scroll || found == moduleRows_.end()) return;
    auto& row = found->second;
    auto& plate = row.plate->layer();
    const auto order = config().moduleLayout().order;
    const auto converted = d::convert(point, row.grip->layer(), scroll->content());
    if (!converted) return;
    const double y = converted->y;
    const double minY = row.top + plate.value(d::Channel::translateY);
    switch (phase) {
    case d::DragGrip::Phase::began: {
        const auto index = std::find(order.begin(), order.end(), module);
        if (index == order.end()) return;
        moduleDrag_ = Drag{module, y - minY, int(index - order.begin())};
        plate.setZPosition(10);
        d::motion::animate(plate, d::Channel::opacity, .92, d::duration::hover, env().clock());
        break;
    }
    case d::DragGrip::Phase::moved: {
        if (!moduleDrag_ || moduleDrag_->module != module) return;
        // Near an edge the list follows the pointer (on pointer events only).
        const double shown = y - scroll->offset(), edge = 36, height = scroll->layer().bounds().height;
        if (shown < edge) scroll->scroll(-std::min(12., edge - shown));
        else if (shown > height - edge) scroll->scroll(std::min(12., shown - height + edge));
        const double top = std::min(double(order.size() - 1) * modulePitch, std::max(0., y - moduleDrag_->grab));
        plate.setValue(d::Channel::translateY, top - row.top);
        const int slot = std::min(int(order.size()) - 1, std::max(0, int(std::round(top / modulePitch))));
        if (slot == moduleDrag_->slot) return;
        moduleDrag_->slot = slot;
        auto preview = order;
        std::erase(preview, module);
        preview.insert(preview.begin() + slot, module);
        layoutModuleRows(preview, true);
        break;
    }
    case d::DragGrip::Phase::ended: {
        if (!moduleDrag_ || moduleDrag_->module != module) return;
        const int slot = moduleDrag_->slot;
        moduleDrag_.reset();
        d::motion::animate(plate, d::Channel::opacity, 1, d::duration::hover, env().clock());
        update([&](Settings& s) { auto l = s.moduleLayout(); l.move(module, slot); s.setModuleLayout(l); });
        // The update re-ran the binders (an unchanged order does not): settle every row.
        layoutModuleRows(config().moduleLayout().order, true);
        plate.setZPosition(0);
        break;
    }
    }
}
// confirmModulesReset (:382-389).
void SettingsCanvas::confirmModulesReset() {
    d::ConfirmRequest request;
    request.id = "modulesReset"; request.title = text("Restore default layout?", "恢复默认布局？");
    request.message = text("All modules are shown in their original order.", "所有模块按原顺序显示。");
    request.confirmTitle = text("Restore", "恢复"); request.icon = d::Icon::reset;
    confirm(std::move(request), [this] {
        update([](Settings& s) { s.setModuleLayout({}); });
        toast(d::Icon::reset, "Layout restored", "已恢复默认布局");
    });
}
// cursor(at:) (:391-398).
std::optional<d::Cursor> SettingsCanvas::cursor(std::optional<d::Point> point) const {
    if (moduleDrag_) return d::Cursor::closedHand;
    if (!isActive() || section_ != Section::modules || !point) return std::nullopt;
    for (const auto& [module, row] : moduleRows_) {
        const auto& grip = row.grip->layer();
        if (!d::isVisible(grip, root()) || !d::isUnclipped(grip, *point, root())) continue;
        if (const auto local = d::convert(*point, root(), grip); local && row.grip->contains(*local)) return d::Cursor::openHand;
    }
    return std::nullopt;
}
bool SettingsCanvas::inputLocked() const { return Canvas::inputLocked() || capturingShortcut(); }

// buildDisplay (:400-485).
void SettingsCanvas::buildDisplay(d::Page& page) {
    const auto& c = config();
    auto& scroll = page.scrollArea("display", {0, d::metric::headerHeight, d::metric::boxWidth, 416});
    std::vector<d::RowPlate*> rows;
    rows.push_back(&sliderRow("uiScale", text("UI scale", "界面缩放"), stagedScale_.value_or(c.number("hudScale")), {.2, 2, .01, std::nullopt}, d::ValueFormat::scale, page,
        [](const Settings& s) { return s.number("hudScale"); }, [this](double value, bool final) {
            if (final) { stagedScale_.reset(); controller_->previewScale(value, now()); } else stagedScale_ = value;
        }));
    rows.push_back(&sliderRow("positionX", text("HUD position X", "界面位置 X"), stagedX_.value_or(c.number("hudOffsetX")) * 100, {-50, 50, 1, 0}, d::ValueFormat::signedPercent, page,
        [](const Settings& s) { return s.number("hudOffsetX") * 100; }, [this](double value, bool final) { stagePosition(value / 100, std::nullopt, final); }));
    rows.push_back(&sliderRow("positionY", text("HUD position Y", "界面位置 Y"), stagedY_.value_or(c.number("hudOffsetY")) * 100, {-50, 50, 1, 0}, d::ValueFormat::signedPercent, page,
        [](const Settings& s) { return s.number("hudOffsetY") * 100; }, [this](double value, bool final) { stagePosition(std::nullopt, value / 100, final); }));
    rows.push_back(&percentRow("parallax", text("Parallax intensity", "视差强度"), 200, "parallaxIntensity", page));
    // Perspective drives the plane's tilt live while dragging (spec §1.3).
    rows.push_back(&percentRow("perspective", text("Perspective intensity", "透视强度"), 200, "perspectiveIntensity", page));
    rows.push_back(&percentRow("darkness", text("Background brightness", "背景亮度"), 100, "backgroundDarkness", page, true));
    rows.push_back(&percentRow("blur", text("Blur amount", "模糊程度"), 100, "blurAmount", page));
    auto& theme = dropdown(page, "theme", text("Theme", "主题"), themeTitle(stringOr(c, "theme")), std::nullopt, [this](const d::Dropdown& anchor) {
        std::vector<d::Menu::Item> items;
        const auto current = stringOr(config(), "theme");
        for (const auto id : themes) { auto item = menuItem(std::string(id), themeTitle(id)); item.selected = id == current; items.push_back(std::move(item)); }
        openMenu("theme", std::move(items), anchor, std::nullopt, [this](const d::Menu::Item& item) {
            if (std::find(themes.begin(), themes.end(), item.id) == themes.end()) return;
            update([&](Settings& s) { s.set("theme", item.id); });
        });
    });
    binders_.push_back([this, &theme] { theme.setValue(themeTitle(stringOr(config(), "theme"))); });
    rows.push_back(&row("theme", text("Theme", "主题"), theme, page));
    auto& format = page.make<d::SegmentedControl>("clockFormat", text("Time format", "时间格式"),
        std::vector<std::string>{text("24-hour", "24 小时制"), text("12-hour", "12 小时制")}, indexOf(stringOr(c, "clockFormat"), clockFormats), 178.);
    format.onChange = [this](int index) { update([&](Settings& s) { s.set("clockFormat", std::string(clockFormats[std::size_t(index)])); }); };
    binders_.push_back([this, &format] { format.select(indexOf(stringOr(config(), "clockFormat"), clockFormats), false); });
    page.add(format); // the segments hold their group
    rows.push_back(&row("clockFormat", text("Time format", "时间格式"), {&format.layer()}, page, {}, std::vector<d::Control*>(format.segments().begin(), format.segments().end())));
    // The style's number and name in full; the menu lists all five.
    auto& style = dropdown(page, "clockStyle", text("Clock style", "时钟样式"), clockStyleTitle(stringOr(c, "clockStyle")), std::nullopt, [this](const d::Dropdown& anchor) {
        std::vector<d::Menu::Item> items;
        const auto current = knownOr(stringOr(config(), "clockStyle"), clockStyles);
        for (const auto id : clockStyles) { auto item = menuItem(std::string(id), clockStyleTitle(id)); item.selected = id == current; items.push_back(std::move(item)); }
        openMenu("clockStyle", std::move(items), anchor, std::nullopt, [this](const d::Menu::Item& item) {
            if (std::find(clockStyles.begin(), clockStyles.end(), item.id) == clockStyles.end()) return;
            update([&](Settings& s) { s.set("clockStyle", item.id); });
        });
    });
    binders_.push_back([this, &style] { style.setValue(clockStyleTitle(stringOr(config(), "clockStyle"))); });
    rows.push_back(&row("clockStyle", text("Clock style", "时钟样式"), style, page));
    rows.push_back(&logoRow(page));
    rows.push_back(&navRow("appIcon", text("App / menu bar icon", "应用 / 菜单栏图标"), page, [this] { go(Section::icons, true); }));
    rows.push_back(&paletteRow(page));
    const auto metric = std::string(knownOr(stringOr(c, "alertMetric"), metrics));
    auto& reading = page.own<d::Chip>("battery.reading", d::asciiUppercase(metricTitle(metric)), d::Chip::Variant::dark);
    const auto batteryTitle = text("Battery alert", "电池提醒");
    auto& batteryRow = navRow("battery", batteryTitle, page, [this] { go(Section::battery, true); }, "›", {&reading.layer()});
    batteryRow.label = batteryTitle + ", " + metricTitle(metric); // the chip's reading, spoken
    binders_.push_back([this, &reading, &batteryRow, batteryTitle] {
        const auto title = metricTitle(knownOr(stringOr(config(), "alertMetric"), metrics));
        reading.setTitle(d::asciiUppercase(title)); batteryRow.label = batteryTitle + ", " + title;
    });
    rows.push_back(&batteryRow);
    rows.push_back(&toggleRow("ambient", text("Ambient animation", "持续环境动画"), "ambientAnimation", page));
    rows.push_back(&toggleRow("motion", text("Reduce Motion", "减少动态效果"), "reduceMotion", page));
    rows.push_back(&toggleRow("lowPower", text("Low Power visual mode", "低功耗视觉模式"), "lowPowerVisualMode", page));
    rows.push_back(&toggleRow("recording", text("Recording mode", "录制模式"), "recordingMode", page, {}, [this](bool on) {
        if (on) toast(d::Icon::rec, "Recording mode on", "私密内容已隐藏");
        else toast(d::Icon::eye, "Recording mode off", "私密内容已恢复");
    }));
    rows.push_back(&toggleRow("bootLog", text("Daily boot log", "每日启动日志"), "dailyBootLog", page));
    rows.push_back(&toggleRow("facility", text("Facility alerts", "设施警报"), "facilityAlerts", page, [this](std::function<void()> commit) { confirmFacilityOff(std::move(commit)); }));
    scroll.setContentHeight(stack(rows, scroll.content(), page, 0, d::metric::boxWidth - 10));
    page.restoreOffset(scroll);
    header().configure({"DISPLAY"}, "SYS-02", title("Display", "显示"), d::format::pad2(std::int64_t(rows.size())), false);
}
// Switching the facility alerts off asks first, in red: both answers shut
// the "defence system" down; only Esc or a press outside keeps it (:487-494).
void SettingsCanvas::confirmFacilityOff(std::function<void()> commit) {
    const auto answer = text("Disable Shipwide Defense System", "关闭全舰防御系统");
    d::ConfirmRequest request;
    request.id = "facilityOff"; request.title = text("Are you sure you want to disable the Shipwide Defense System?", "是否关闭全舰防御系统？");
    request.confirmTitle = answer; request.cancelTitle = answer; request.icon = d::Icon::power;
    request.style = d::ConfirmRequest::Style::alert; request.cancelPillConfirms = true;
    confirm(std::move(request), std::move(commit));
}
std::string SettingsCanvas::clockStyleTitle(std::string_view style) const {
    // HUDClockStyle.title, the one source of the styles' names (HUDClockStyle.swift:7-16).
    const auto id = knownOr(style, clockStyles);
    const auto name = id == "split" ? text("Split", "分栏") : id == "dial" ? text("Dial", "表盘") : id == "rail" ? text("Rail", "轨道")
        : id == "stacked" ? text("Stacked", "堆叠") : text("Digital", "数字");
    return d::format::pad2(indexOf(id, clockStyles) + 1) + "  " + name;
}
// stagePosition (:500-507).
void SettingsCanvas::stagePosition(std::optional<double> x, std::optional<double> y, bool final) {
    if (x) stagedX_ = x;
    if (y) stagedY_ = y;
    if (!final) return;
    const double nextX = stagedX_.value_or(config().number("hudOffsetX")), nextY = stagedY_.value_or(config().number("hudOffsetY"));
    stagedX_.reset(); stagedY_.reset();
    controller_->previewPosition(nextX, nextY, now());
}
// "Keep this UI scale?" (spec §2.18, :509-538): the confirm bar counting the
// controller's 12 s (the token's 15 is not what runs).
void SettingsCanvas::updateLayoutPrompt() {
    const auto remaining = controller_->confirmationRemaining(now());
    if (remaining) {
        if (layoutPromptOpen_) { context().overlay().setCountdown(*remaining); return; }
        if (module() != core::Module::display) return;
        layoutPromptOpen_ = true;
        const bool position = controller_->positionPending() && !controller_->scalePending();
        const auto previous = d::formatValue(d::ValueFormat::scale, controller_->committed().number("hudScale"));
        const auto requested = d::formatValue(d::ValueFormat::scale, config().number("hudScale"));
        d::ConfirmRequest request;
        request.id = "layout";
        request.title = position ? text("Keep this position?", "保留此位置？") : text("Keep this UI scale?", "保留此缩放？");
        request.countdown = *remaining; request.confirmTitle = text("Keep", "保留"); request.cancelTitle = text("Revert", "恢复"); request.icon = d::Icon::check;
        confirm(std::move(request), [this, position, requested] {
            layoutPromptOpen_ = false; controller_->confirmLayout(now());
            if (position) toast(d::Icon::check, "Position kept", "已保留界面位置");
            else toast(d::Icon::check, "UI scale kept", "已保留界面缩放", requested);
        }, [this] { layoutPromptOpen_ = false; controller_->revertLayout(); });
        revertedScaleText_ = position ? std::nullopt : std::optional(previous);
    } else if (layoutPromptOpen_) {
        // The preview ended without an answer: it reverted itself.
        layoutPromptOpen_ = false;
        context().overlay().dismissConfirm();
        if (revertedScaleText_) toast(d::Icon::reset, "UI scale reverted", "已恢复界面缩放", *revertedScaleText_);
        else toast(d::Icon::reset, "Position reverted", "已恢复界面位置");
        lastStatus_ = currentStatus(); // this toast already reports the controller's restore status
    }
}
// paletteRow (:540-556).
d::RowPlate& SettingsCanvas::paletteRow(d::Page& page) {
    std::vector<d::Layer*> layers;
    std::vector<d::Control*> controls;
    const auto current = stringOr(config(), "accentHex");
    for (const auto hex : settingsPresetAccents) {
        auto& swatch = page.make<d::Swatch>("accent:" + std::string(hex), text("Theme color", "主题颜色") + " #" + std::string(hex), d::rgb(d::normalizedAccent(hex)));
        swatch.setSelected(current == hex);
        swatch.onActivate = [this, value = std::string(hex)] { update([&](Settings& s) { s.set("accentHex", value); }); };
        layers.push_back(&swatch.layer()); controls.push_back(&swatch);
    }
    auto& custom = page.make<d::Swatch>("customColor", text("Custom color wheel", "自定义色轮"), std::nullopt);
    custom.setSelected(std::find(settingsPresetAccents.begin(), settingsPresetAccents.end(), current) == settingsPresetAccents.end());
    custom.onActivate = [this] { if (hooks_.chooseColor) hooks_.chooseColor(); };
    layers.push_back(&custom.layer()); controls.push_back(&custom);
    return row("palette", text("Theme color", "主题颜色"), std::move(layers), page, {}, std::move(controls));
}

// buildBattery (:558-615).
void SettingsCanvas::buildBattery(d::Page& page) {
    const auto& c = config();
    std::vector<d::RowPlate*> rows;
    auto& reading = dropdown(page, "metric", text("Reading", "显示数据"), metricTitle(knownOr(stringOr(c, "alertMetric"), metrics)), std::nullopt, [this](const d::Dropdown& anchor) {
        std::vector<d::Menu::Item> items;
        const auto current = knownOr(stringOr(config(), "alertMetric"), metrics);
        for (const auto id : metrics) { auto item = menuItem(std::string(id), metricTitle(id)); item.selected = id == current; items.push_back(std::move(item)); }
        openMenu("metric", std::move(items), anchor, std::nullopt, [this](const d::Menu::Item& item) {
            if (std::find(metrics.begin(), metrics.end(), item.id) == metrics.end()) return;
            update([&](Settings& s) { s.set("alertMetric", item.id); });
        });
    });
    binders_.push_back([this, &reading] { reading.setValue(metricTitle(knownOr(stringOr(config(), "alertMetric"), metrics))); });
    rows.push_back(&row("metric", text("Reading", "显示数据"), reading, page));
    auto& method = dropdown(page, "method", text("Display method", "显示方式"), methodTitle(shownDisplayMode()), std::nullopt, [this](const d::Dropdown& anchor) {
        std::vector<d::Menu::Item> items;
        const auto shown = shownDisplayMode();
        for (const auto id : methods) { auto item = menuItem(std::string(id), methodTitle(id)); item.selected = id == shown; items.push_back(std::move(item)); }
        openMenu("method", std::move(items), anchor, std::nullopt, [this](const d::Menu::Item& item) {
            if (std::find(methods.begin(), methods.end(), item.id) == methods.end() || item.id == shownDisplayMode()) return;
            // This menu is now the only alert switch: any choice also lifts
            // an "alerts off" saved by the former System toggle (or v1.2.0).
            update([&](Settings& s) { s.setBatteryAlertMethod(item.id); });
            if (item.id == "never") toast(d::Icon::eye, "Bar hidden", "不再自动显示");
        });
    });
    binders_.push_back([this, &method] { method.setValue(methodTitle(shownDisplayMode())); });
    rows.push_back(&row("method", text("Display method", "显示方式"), method, page));
    rows.push_back(&sliderRow("duration", text("Display duration", "显示时长"), c.number("displayDuration"), {1, 60, 1, std::nullopt}, d::ValueFormat::seconds, page,
        [](const Settings& s) { return s.number("displayDuration"); }, [this](double value, bool) { update([&](Settings& s) { s.set("displayDuration", value); }); }));
    auto& placement = dropdown(page, "placement", text("Display position", "显示位置"), placementTitle(stringOr(c, "placement")), std::nullopt, [this](const d::Dropdown& anchor) {
        std::vector<d::Menu::Item> items;
        const auto current = stringOr(config(), "placement");
        for (const auto id : placements) { auto item = menuItem(std::string(id), placementTitle(id)); item.selected = id == current; items.push_back(std::move(item)); }
        openMenu("placement", std::move(items), anchor, std::nullopt, [this](const d::Menu::Item& item) {
            if (std::find(placements.begin(), placements.end(), item.id) == placements.end()) return;
            update([&](Settings& s) { s.set("placement", item.id); });
        });
    });
    binders_.push_back([this, &placement] { placement.setValue(placementTitle(stringOr(config(), "placement"))); });
    rows.push_back(&row("placement", text("Display position", "显示位置"), placement, page));
    rows.push_back(&navRow("editPosition", text("Edit position", "编辑位置"), page, [this] { controller_->editBatteryPosition(); }, "↗"));
    rows.push_back(&sliderRow("batterySize", text("Size", "大小"), c.number("scale") * 100, {65, 160, 1, std::nullopt}, d::ValueFormat::percent, page,
        [](const Settings& s) { return s.number("scale") * 100; }, [this](double value, bool) { update([&](Settings& s) { s.set("scale", value / 100); }); }));
    stack(rows, page.layer(), page, d::metric::headerHeight);
    header().configure({"DISPLAY", "BATTERY ALERT"}, "SYS-02", title("Battery alert", "电池提醒"), d::format::pad2(std::int64_t(rows.size())), true);
    wireBack(page, [this] { go(Section::main, false); });
}
// Alerts switched off (the former System toggle, also in v1.2.0) read as Don't display.
std::string SettingsCanvas::shownDisplayMode() const { return std::string(knownOr(config().batteryAlertMethod(), methods)); }

// MARK: Center logo (an icon grid; the field shows the logo in use, :617-680)
d::RowPlate& SettingsCanvas::logoRow(d::Page& page) {
    const auto rowTitle = text("Center logo", "中心标志");
    auto& field = dropdown(page, "centerLogo", rowTitle, logoTitle(stringOr(config(), "centerLogo")), std::nullopt,
        [this](const d::Dropdown& anchor) { openLogoMenu(anchor); });
    const auto show = [this, &field] {
        const auto logo = stringOr(config(), "centerLogo");
        field.setValue(logoTitle(logo));
        const auto art = hooks_.logoArtwork ? hooks_.logoArtwork(logo, logo == "custom" ? stringOr(config(), "centerLogoRevision") : std::string(), {130, logoFieldHeight}) : std::nullopt;
        field.setArtwork(art ? std::optional(art->image) : std::nullopt, art ? art->pixels : d::Size{}, logoFieldHeight);
    };
    show();
    binders_.push_back(show);
    return row("centerLogo", rowTitle, field, page);
}
// The four preset logos as tiles (a full grid: an imported logo has no tile,
// the field shows it), then the custom-logo import (:637-656).
void SettingsCanvas::openLogoMenu(const d::Dropdown& anchor) {
    const d::Rect box{8, 8, logoTile.width - 16, logoTile.height - 16};
    const auto current = stringOr(config(), "centerLogo");
    std::vector<d::Menu::Item> items;
    for (const auto logo : presetLogos) {
        // The presets stay offered even if their artwork is unreadable.
        const auto art = hooks_.logoArtwork ? hooks_.logoArtwork(logo, {}, {box.width, box.height}) : std::nullopt;
        auto item = menuItem(std::string(logo), logoTitle(logo));
        item.selected = current == logo;
        item.icon = d::Menu::IconTile{art ? art->image : d::ImageSource{}, logoTile, box, std::nullopt};
        items.push_back(std::move(item));
    }
    items.push_back(menuItem("import", text("Choose custom logo", "选择自定义图标")));
    openMenu("logo", std::move(items), anchor, std::nullopt, [this](const d::Menu::Item& item) {
        if (item.id == "import") { if (hooks_.chooseLogo) hooks_.chooseLogo(); return; }
        if (std::find(presetLogos.begin(), presetLogos.end(), item.id) == presetLogos.end()) return;
        if (hooks_.logoSelection) hooks_.logoSelection();
        update([&](Settings& s) { s.set("centerLogo", item.id); });
    });
}

// buildIcons (:682-702).
void SettingsCanvas::buildIcons(d::Page& page) {
    const auto& icons = hooks_.icons;
    constexpr int columns = 4;
    constexpr double gap = 10;
    const double size = (d::metric::boxWidth - 10 - double(columns - 1) * gap) / columns;
    auto& scroll = page.scrollArea("icons", {0, d::metric::headerHeight, d::metric::boxWidth, d::metric::bodyHeight});
    const auto selected = stringOr(config(), "applicationIcon");
    for (std::size_t index = 0; index < icons.size(); ++index) {
        const auto& icon = icons[index];
        auto& tile = page.make<SettingsIconTile>("appIcon:" + icon.id, icon, iconTitle(icon.id, icon.title), d::Size{size, 96});
        tile.setSelected(selected == icon.id);
        tile.onActivate = [this, id = icon.id] { update([&](Settings& s) { s.set("applicationIcon", id); }); };
        tile.layer().setOrigin({double(index % columns) * (size + gap), double(index / columns) * (96 + gap)});
        scroll.content().add(tile.layer());
        page.add(tile); page.stagger(tile.layer());
    }
    const auto rowCount = (icons.size() + columns - 1) / columns;
    scroll.setContentHeight(double(rowCount) * (96 + gap) - gap);
    page.restoreOffset(scroll);
    header().configure({"DISPLAY", "APP ICON"}, "SYS-02", title("App / menu bar icon", "应用 / 菜单栏图标"), d::format::pad2(std::int64_t(icons.size())), true);
    wireBack(page, [this] { go(Section::main, false); });
}

// buildHotkeys (:704-735).
void SettingsCanvas::buildHotkeys(d::Page& page) {
    const auto& c = config();
    std::vector<d::RowPlate*> rows;
    const auto shortcut = settingsShortcut(c);
    auto& summon = page.make<d::KeycapGroup>("capture", text("Summon HUD", "呼出界面") + ", " + settingsShortcutTitle(shortcut), keycaps(shortcut), true);
    summon.onActivate = [this] {
        if (capturingShortcut()) cancelCapture(); else controller_->beginShortcutCapture();
    };
    if (capturingShortcut()) summon.setRecording(true, text("Press a shortcut", "请按快捷键"));
    keycaps_ = &summon;
    binders_.push_back([this, &summon] { if (!capturingShortcut()) summon.setKeys(keycaps(settingsShortcut(config()))); });
    rows.push_back(&row("capture", text("Summon HUD", "呼出界面"), {&summon.layer()}, page, {}, {&summon}));
    rows.push_back(&toggleRow("hotkeyEnabled", text("Open with hotkey", "使用快捷键打开"), "summonHotkeyEnabled", page));
    auto& defaults = page.make<d::KeycapGroup>("keyDefault", settingsShortcutTitle({}), keycaps({}), false, 0.);
    auto& restore = page.make<d::Button>("keyReset", text("Restore", "恢复"), d::Icon::reset, d::Button::Kind::secondary, d::Button::Height::compact);
    restore.place({0, 0});
    restore.onActivate = [this] {
        cancelCapture();
        if (const auto error = controller_->setShortcut({})) toast(d::Icon::warn, "Hotkey unavailable", "快捷键不可用", *error);
        else toast(d::Icon::check, "Hotkey restored", "已恢复默认快捷键");
    };
    rows.push_back(&row("keyReset", text("Restore default hotkey", "恢复默认快捷键"), {&defaults.layer(), &restore.layer()}, page, {}, {&restore}));
    stack(rows, page.layer(), page, d::metric::headerHeight);
    header().configure({"HOTKEYS"}, "SYS-03", title("Hotkeys", "快捷键"), d::format::pad2(std::int64_t(rows.size())), false);
}
// keycaps (:738-746): the Mac's CTRL, OPT, SHIFT, CMD become the Windows
// modifiers in settingsShortcutTitle's order (CTRL, ALT, SHIFT, WIN), then the key.
std::vector<std::string> SettingsCanvas::keycaps(SettingsShortcut shortcut) {
    std::vector<std::string> keys;
    if (shortcut.modifiers & 2) keys.emplace_back("CTRL");
    if (shortcut.modifiers & 1) keys.emplace_back("ALT");
    if (shortcut.modifiers & 4) keys.emplace_back("SHIFT");
    if (shortcut.modifiers & 8) keys.emplace_back("WIN");
    keys.push_back(d::asciiUppercase(split(settingsShortcutTitle(shortcut), " + ").back()));
    return keys;
}
bool SettingsCanvas::capturingShortcut() const noexcept { return module() == core::Module::hotkeys && controller_->capturingShortcut(); }
// Live capture (spec §3.4, :748-767): Esc cancels, modifier-only presses are
// ignored, a modifier plus a key saves.
bool SettingsCanvas::interceptKey(const d::KeyInput& input) {
    if (!capturingShortcut()) return false;
    if (input.virtualKey == d::key::escape) { cancelCapture(); return true; }
    if (input.repeat) return true;
    // A modifier alone (Windows sends its own key-down: VK_SHIFT, VK_CONTROL,
    // VK_MENU, VK_LWIN / VK_RWIN and their left/right forms) waits for the key.
    const auto vk = input.virtualKey;
    if (vk == d::key::shift || vk == d::key::control || vk == d::key::menu || vk == 0x5B || vk == 0x5C || (vk >= 0xA0 && vk <= 0xA5)) return true;
    const SettingsShortcut shortcut{vk, (input.alt ? 1u : 0u) | (input.control ? 2u : 0u) | (input.shift ? 4u : 0u) | (input.win ? 8u : 0u)};
    if (const auto error = validateSettingsShortcut(shortcut, env().language)) { toast(d::Icon::warn, "Hotkey unavailable", "快捷键不可用", *error); return true; }
    if (const auto error = controller_->setShortcut(shortcut)) { toast(d::Icon::warn, "Hotkey unavailable", "快捷键不可用", *error); return true; }
    cancelCapture();
    std::string joined;
    for (const auto& key : keycaps(shortcut)) joined += (joined.empty() ? "" : " + ") + key;
    toast(d::Icon::check, "Hotkey saved", "快捷键已保存", joined);
    return true;
}
void SettingsCanvas::cancelCapture() {
    if (!capturingShortcut()) return;
    controller_->endShortcutCapture();
}

// buildAbout (:774-837).
void SettingsCanvas::buildAbout(d::Page& page) {
    const auto& about = hooks_.about;
    const auto& update = controller_->updateStatus();
    auto& hero = aboutHero(page);
    hero.setOrigin({0, d::metric::headerHeight});
    page.layer().add(hero);
    page.stagger(hero);
    d::Layer& heroFrame = page.makeLayer("value:hero");
    heroFrame.setFrame(d::localBounds(hero));
    hero.add(heroFrame);
    page.add(page.builder().controls.make<d::Readout>(env(), "value:hero", about.name, about.version, heroFrame));
    d::Layer& strip = page.makeLayer("about.updates");
    auto& cells = page.own<d::Cells>("about.updates.cells", 4, d::metric::boxWidth - 14 - 130, false, 10.);
    d::TextStyle statusStyle{10.5, d::FontWeight::bold, d::rgb(0xD0D0D0), 1, .1, true};
    d::Layer& status = d::makeText(page.builder(), "about.updates.status", "", statusStyle, {d::metric::boxWidth - 130, 4.75, 130, 10.5}, d::TextAlignment::right);
    strip.setFrame({0, hero.frame().y + hero.frame().height + 12, d::metric::boxWidth, 20});
    cells.layer().setOrigin({0, 5});
    strip.add(cells.layer()); strip.add(status);
    page.layer().add(strip);
    page.stagger(strip);
    d::Layer& stripFrame = page.makeLayer("value:updates");
    stripFrame.setFrame(d::localBounds(strip));
    strip.add(stripFrame);
    auto& spoken = page.builder().controls.make<d::Readout>(env(), "value:updates", text("Updates", "更新"), "", stripFrame);
    page.add(spoken);
    updateCells_ = &cells; updateText_ = &status; updateSpoken_ = &spoken; updateStrip_ = &strip; aboutPage_ = &page;
    renderUpdateStrip(false);
    const double stripBottom = strip.frame().y + strip.frame().height;
    auto& scroll = page.scrollArea("about", {0, stripBottom + 10, d::metric::boxWidth, d::metric::boxHeight - stripBottom - 10});
    std::vector<d::RowPlate*> rows;
    auto& check = page.make<d::Button>("updates", text("Check", "检查"), d::Icon::refresh, d::Button::Kind::primary, d::Button::Height::compact);
    check.place({0, 0});
    // Windows: the area also refuses a check while it installs (canCheck).
    check.setEnabled(update.phase != SettingsUpdatePhase::checking && !checkProgress_ && update.canCheck);
    check.onActivate = [this] { checkForUpdates(); };
    binders_.push_back([this, &check] {
        const auto& u = controller_->updateStatus();
        check.setEnabled(u.phase != SettingsUpdatePhase::checking && !checkProgress_ && u.canCheck);
        renderUpdateStrip(true);
    });
    rows.push_back(&row("updates", text("Check for updates", "检查更新"), {&check.layer()}, page, {}, {&check}));
    const auto latest = update.latestVersion.empty() ? std::string("—") : update.latestVersion.starts_with("v") ? update.latestVersion : "v" + update.latestVersion;
    std::vector<d::Layer*> latestCluster{&readout(page, "latestRelease", latest)};
    std::vector<d::Control*> latestControls;
    if (!update.releaseURL.empty()) {
        auto& open = linkButton(page, "latestRelease", text("Latest GitHub release", "GitHub 最新版本"), update.releaseURL);
        latestCluster.push_back(&open.layer()); latestControls.push_back(&open);
    }
    auto& latestRow = row("latestRelease", text("Latest GitHub release", "GitHub 最新版本"), latestCluster, page, {}, latestControls);
    spokenValue("latestRelease", latestRow, update.latestVersion.empty() ? std::string("—") : update.latestVersion, page);
    rows.push_back(&latestRow);
    // Windows: the windowsUpdates area names its toggle (daily checks) and
    // says whether it can change it.
    const auto automaticTitle = update.automaticTitle.empty() ? text("Install updates automatically", "自动安装更新") : update.automaticTitle;
    auto& automatic = page.make<d::Toggle>("automaticUpdates", automaticTitle, update.automaticallyInstalls);
    automatic.setEnabled(update.canSetAutomatic);
    automatic.onChange = [this](bool) { controller_->toggleAutomaticUpdates(); };
    binders_.push_back([this, &automatic] {
        automatic.setOn(controller_->updateStatus().automaticallyInstalls, true);
        automatic.setEnabled(controller_->updateStatus().canSetAutomatic);
    });
    rows.push_back(&row("automaticUpdates", automaticTitle, {&automatic.layer()}, page, {}, {&automatic}));
    rows.push_back(&valueRow("author", text("Author", "作者"), about.author, page));
    if (!about.repository.empty()) {
        auto& open = linkButton(page, "github", "GitHub", about.repository);
        rows.push_back(&row("github", "GitHub", {&open.layer()}, page, {}, {&open}));
    }
    rows.push_back(&valueRow("license", text("License", "许可证"), about.license, page));
    rows.push_back(&navRow("credits", text("Credits", "致谢"), page, [this] { go(Section::credits, true); }));
    scroll.setContentHeight(stack(rows, scroll.content(), page, 0, d::metric::boxWidth - 10));
    page.restoreOffset(scroll);
    header().configure({"ABOUT"}, "SYS-04", title("About", "关于"), "v" + split(about.version, " ").front(), false);
}
// buildCredits (:839-854).
void SettingsCanvas::buildCredits(d::Page& page) {
    std::vector<d::RowPlate*> rows;
    const auto credits = settingsCredits(env().language);
    for (std::size_t index = 0; index < credits.size(); ++index) {
        const auto& credit = credits[index];
        std::vector<d::Layer*> cluster;
        std::vector<d::Control*> controls;
        if (!credit.url.empty()) {
            auto& open = linkButton(page, "credit:" + std::to_string(index), credit.name, credit.url);
            cluster.push_back(&open.layer()); controls.push_back(&open);
        }
        rows.push_back(&row("credit:" + std::to_string(index), credit.name, cluster, page, credit.role, controls));
    }
    stack(rows, page.layer(), page, d::metric::headerHeight);
    header().configure({"ABOUT", "CREDITS"}, "SYS-04", title("Credits", "致谢"), d::format::pad2(std::int64_t(rows.size())), true);
    wireBack(page, [this] { go(Section::main, false); });
}
// The hero plate (spec §3.5, :856-886): tri-strip, ENDFIELD. INDUSTRIES bar, name, version.
d::Layer& SettingsCanvas::aboutHero(d::Page& page) {
    const auto& about = hooks_.about;
    const d::Size size{d::metric::boxWidth, 84};
    d::Layer& hero = page.makeLayer("about.hero");
    const auto parts = split(about.version, " ");
    const auto& version = parts.front();
    std::optional<std::string> build;
    if (parts.size() > 1) {
        auto value = parts[1];
        while (!value.empty() && (value.front() == '(' || value.front() == ')')) value.erase(value.begin());
        while (!value.empty() && (value.back() == '(' || value.back() == ')')) value.pop_back();
        build = value;
    }
    hero.setBounds(size);
    d::Layer& plate = page.makeLayer("about.hero.plate");
    plate.setFrame(d::localBounds(hero));
    plate.setContent(d::plate(plate.id(), d::localBounds(plate), d::fixed::heroPlate, {}, 14));
    d::Layer& strip = page.makeLayer("about.hero.strip");
    strip.setFrame({0, 14, 4, 38});
    strip.setContent(d::triStrip(strip.id(), d::localBounds(strip)));
    d::Layer& bar = page.makeLayer("about.hero.bar");
    bar.setFrame({20, 14, 118, 9});
    d::setBackground(bar, d::rgb(0x141414));
    d::Layer& barText = d::makeText(page.builder(), "about.hero.barText", "ENDFIELD. INDUSTRIES", {6, d::FontWeight::black, d::white(1), 1.5, .06}, {4, 0, 114, 9});
    bar.add(barText);
    d::Layer& name = d::makeText(page.builder(), "about.hero.name", about.name, {30, d::FontWeight::black, d::fixed::nearBlack, 1, .02}, {20, 33, 320, 30});
    d::Layer& number = d::makeText(page.builder(), "about.hero.version", version, {38, d::FontWeight::light, d::fixed::nearBlack, 1, 0, false, true},
        {size.width - 20 - 220, 16, 220, 38}, d::TextAlignment::right);
    for (d::Layer* part : {&plate, &strip, &bar, &name, &number}) hero.add(*part);
    if (build) {
        d::Layer& label = d::makeText(page.builder(), "about.hero.build", "BUILD " + *build, {9.5, d::FontWeight::bold, d::rgb(0x555555), 1, .12},
            {size.width - 20 - 220, 58, 220, 9.5}, d::TextAlignment::right);
        hero.add(label);
    }
    return hero;
}
// The 4-cell strip advances every 650 ms while the real check runs, then
// shows the outcome; UP TO DATE pops in as a stamp (:888-907).
void SettingsCanvas::checkForUpdates() {
    if (checkProgress_) return;
    controller_->checkUpdates();
    checkProgress_ = 1;
    renderUpdateStrip(true);
    scheduleCheckStep();
}
void SettingsCanvas::scheduleCheckStep() {
    after(env().reduceMotion ? .05 : .65, checkTimer_, [this] {
        if (!checkProgress_) return;
        const int step = *checkProgress_;
        if (step < 3) { checkProgress_ = step + 1; renderUpdateStrip(true); scheduleCheckStep(); return; }
        // Hold at the last step until the real check reports.
        if (controller_->updateStatus().phase == SettingsUpdatePhase::checking) { scheduleCheckStep(); return; }
        checkProgress_.reset();
        const auto binders = binders_;
        for (const auto& bind : binders) bind();
    });
}
// renderUpdateStrip (:909-939).
void SettingsCanvas::renderUpdateStrip(bool animated) {
    if (!updateCells_ || !updateText_) return;
    const auto& state = controller_->updateStatus();
    const std::array steps{text("Connecting", "连接中"), text("Reading releases", "读取版本"), text("Verifying", "校验中")};
    int filled{};
    std::string label;
    if (checkProgress_) { filled = *checkProgress_; label = steps[std::size_t(*checkProgress_ - 1)]; }
    else switch (state.phase) {
        case SettingsUpdatePhase::current: filled = 4; break;
        case SettingsUpdatePhase::checking: filled = 3; label = steps[2]; break;
        case SettingsUpdatePhase::available: case SettingsUpdatePhase::downloading: case SettingsUpdatePhase::ready: case SettingsUpdatePhase::installing:
            filled = 4; label = state.title; break;
        case SettingsUpdatePhase::failed: filled = 0; label = state.title; break;
        case SettingsUpdatePhase::idle: filled = 0; label = text("Not checked yet", "尚未检查"); break;
    }
    updateCells_->set(filled, std::nullopt, false, animated);
    d::editText(*updateText_, [&](d::TextBlock& block) { block.setText(label); });
    updateText_->setHidden(label.empty());
    const bool showStamp = !checkProgress_ && state.phase == SettingsUpdatePhase::current;
    if (updateSpoken_) updateSpoken_->value = showStamp ? text("Up to date", "已是最新") : label;
    if (showStamp && !updateStamp_ && updateStrip_ && aboutPage_) {
        auto& stamp = d::badge::stamp(aboutPage_->builder(), "about.updates.stamp" + std::to_string(++stampSerial_), text("Up to date", "已是最新"));
        stamp.setPosition({d::metric::boxWidth - stamp.bounds().width / 2 - 6, 10});
        updateStrip_->add(stamp);
        updateStamp_ = &stamp;
        if (animated) { stamp.setAnimatable(); d::motion::pop(stamp, env().clock()); }
    } else if (!showStamp && updateStamp_) {
        updateStamp_->removeFromParent(); updateStamp_ = nullptr;
    }
}

// MARK: Row building (:941-1085)
// Stacks rows from `top` with the 6 px gap; returns the stack's height.
double SettingsCanvas::stack(const std::vector<d::RowPlate*>& rows, d::Layer& parent, d::Page& page, double top, double width) {
    (void)width;
    double y = top;
    for (auto* row : rows) {
        row->layer().setOrigin({0, y});
        parent.add(row->layer());
        y += row->layer().bounds().height + d::metric::rowGap;
    }
    for (auto* row : rows) page.stagger(row->layer());
    return std::max(0., y - top - d::metric::rowGap);
}
d::RowPlate& SettingsCanvas::row(const std::string& id, const std::string& title, std::vector<d::Layer*> cluster, d::Page& page,
    std::optional<std::string> sublabel, std::vector<d::Control*> controls) {
    auto& plate = page.make<d::RowPlate>("row:" + id, title, std::move(sublabel), d::RowPlate::Variant::standard, "›", rowWidth());
    plate.setCluster(std::move(cluster));
    page.add(plate);
    page.add(controls);
    return plate;
}
d::RowPlate& SettingsCanvas::row(const std::string& id, const std::string& title, d::Dropdown& field, d::Page& page) {
    return row(id, title, {&field.layer()}, page, {}, {&field});
}
double SettingsCanvas::rowWidth() const {
    return (module() == core::Module::display && section_ == Section::main) || section_ == Section::modules ? d::metric::boxWidth - 10 : d::metric::boxWidth;
}
d::RowPlate& SettingsCanvas::navRow(const std::string& id, const std::string& title, d::Page& page, std::function<void()> action, std::string chevron,
    std::vector<d::Layer*> cluster) {
    auto& plate = page.make<d::RowPlate>(id, title, std::nullopt, d::RowPlate::Variant::nav, std::move(chevron), rowWidth());
    plate.setCluster(std::move(cluster));
    plate.onActivate = std::move(action);
    page.add(plate);
    return plate;
}
// `confirmOff` asks before switching off: the switch stays on until it calls
// its argument, which saves the change (:986-1003).
d::RowPlate& SettingsCanvas::toggleRow(const std::string& id, const std::string& title, std::string key, d::Page& page,
    std::function<void(std::function<void()>)> confirmOff, std::function<void(bool)> changed) {
    auto& toggle = page.make<d::Toggle>(id, title, flag(config(), key));
    toggle.onChange = [this, &toggle, key, confirmOff, changed](bool on) {
        if (!on && confirmOff) {
            toggle.setOn(true, true);
            confirmOff([this, key] { update([&](Settings& s) { s.set(key, false); }); });
            return;
        }
        update([&](Settings& s) { s.set(key, on); });
        if (changed) changed(flag(config(), key));
    };
    binders_.push_back([this, &toggle, key] { toggle.setOn(flag(config(), key), true); });
    return row(id, title, {&toggle.layer()}, page, {}, {&toggle});
}
d::RowPlate& SettingsCanvas::sliderRow(const std::string& id, const std::string& title, double value, d::Slider::Range range, d::ValueFormat format,
    d::Page& page, std::function<double(const Settings&)> read, std::function<void(double, bool)> change) {
    auto& plate = page.make<d::RowPlate>("row:" + id, title, std::nullopt, d::RowPlate::Variant::slider, "›", rowWidth());
    const auto area = plate.controlArea();
    auto& slider = page.make<d::Slider>(id, title, value, range, format, area.width);
    slider.layer().setOrigin({area.x, area.y});
    plate.layer().add(slider.layer());
    hostBubble(slider);
    slider.onChange = [this, change](double v, bool final) {
        change(v, final);
        if (final && context().onInputStateChange) context().onInputStateChange();
    };
    binders_.push_back([this, &slider, read] { if (!slider.dragging()) slider.setValue(read(config())); });
    page.add(plate); page.add(slider);
    return plate;
}
// percentRow (:1025-1031); `inverted`: Background brightness is 1 - backgroundDarkness (AppConfiguration.backgroundBrightness).
d::RowPlate& SettingsCanvas::percentRow(const std::string& id, const std::string& title, double maximum, std::string key, d::Page& page, bool inverted) {
    const auto read = [key, inverted](const Settings& s) { const double v = s.number(key); return (inverted ? 1 - v : v) * 100; };
    return sliderRow(id, title, read(config()), {0, maximum, 1, std::nullopt}, d::ValueFormat::percent, page, read, [this, key, inverted](double value, bool) {
        update([&](Settings& s) { s.set(key, inverted ? 1 - value / 100 : value / 100); });
    });
}
d::Dropdown& SettingsCanvas::dropdown(d::Page& page, const std::string& id, const std::string& label, std::string value, std::optional<d::TextRegion> language,
    std::function<void(const d::Dropdown&)> open) {
    auto& field = page.make<d::Dropdown>(id, label, std::move(value), language);
    field.onActivate = [this, &field, open] { if (menuOpen()) closeMenu(); else open(field); };
    return field;
}
// `[ value ]` readout with grey brackets (spec §2.7) that the row speaks.
d::RowPlate& SettingsCanvas::valueRow(const std::string& id, const std::string& title, const std::string& value, d::Page& page) {
    auto& plate = row(id, title, {&readout(page, id, value)}, page);
    spokenValue(id, plate, value, page);
    return plate;
}
// Exposes a row's drawn value to accessibility over the whole plate.
d::Readout& SettingsCanvas::spokenValue(const std::string& id, d::RowPlate& plate, const std::string& value, d::Page& page) {
    d::Layer& frame = page.makeLayer("value:" + id);
    frame.setFrame(d::localBounds(plate.layer()));
    plate.layer().add(frame);
    auto& spoken = page.builder().controls.make<d::Readout>(env(), "value:" + id, plate.label, value, frame);
    page.add(spoken);
    return spoken;
}
// readout + setReadout (:1060-1075): only the line frame; it pads the bounds for the glyphs.
d::Layer& SettingsCanvas::readout(d::Page& page, const std::string& id, const std::string& value) {
    d::TextBlock block;
    block.runs = {{"[ ", d::type::readoutBracket()}, {value, d::type::readout()}, {" ]", d::type::readoutBracket()}};
    block.alignment = d::TextAlignment::right;
    block.setLineFrame({0, 0, std::ceil(d::preferredWidth(block, env().text)) + 2, 12});
    d::Layer& layer = page.makeLayer("readout:" + id);
    layer.setText(std::move(block));
    return layer;
}
// linkButton (:1077-1085).
d::IconButton& SettingsCanvas::linkButton(d::Page& page, const std::string& id, const std::string& label, const std::string& url) {
    auto& button = page.make<d::IconButton>(id + ":open", label, d::Icon::ext, d::IconButton::Style::light);
    button.onActivate = [this, url] {
        controller_->openLink(url);
        const auto [host, path] = hostAndPath(url);
        if (host == "github.com") toast(d::Icon::ext, "Opening GitHub", "正在打开 GitHub", path.empty() ? path : path.substr(1));
        else toast(d::Icon::ext, "Opening link", "正在打开链接", host.empty() ? url : host);
    };
    return button;
}

// MARK: Titles (:1089-1116)
std::string SettingsCanvas::languageTitle(std::string_view value) const {
    if (value == "english") return "English";
    if (value == "simplifiedChinese") return "简体中文";
    if (value == "traditionalChinese") return "繁體中文";
    if (value == "japanese") return "日本語";
    if (value == "korean") return "한국어";
    return text("System", "跟随系统");
}
std::string SettingsCanvas::themeTitle(std::string_view value) const {
    if (value == "light") return text("Light", "浅色");
    if (value == "system") return text("System", "跟随系统");
    return text("Dark", "深色");
}
std::string SettingsCanvas::methodTitle(std::string_view value) const {
    if (value == "always") return text("Always", "始终显示");
    if (value == "never") return text("Don't display", "不显示");
    return text("Power changes", "充电状态变化");
}
std::string SettingsCanvas::placementTitle(std::string_view value) const {
    return value == "topCenter" ? text("Top center", "顶部居中") : text("Custom", "自定");
}
// HUDChargeMetric.title (HUDChargeMetric.swift:10-18).
std::string SettingsCanvas::metricTitle(std::string_view value) const {
    if (value == "ram") return text("RAM", "RAM");
    if (value == "cpu") return "CPU";
    if (value == "network") return text("Network", "网络");
    if (value == "disk") return text("Disk", "磁盘");
    if (value == "gpu") return "GPU";
    return text("Battery", "电池");
}
// HUDCenterLogo.title (HUDCenterLogo.swift:9-16): the presets read their app icons' titles.
std::string SettingsCanvas::logoTitle(std::string_view value) const {
    if (value == "custom") return text("Custom", "自定");
    for (const auto& icon : hooks_.icons) if (icon.id == value) return iconTitle(icon.id, icon.title);
    return std::string(value);
}
// HUDApplicationIcon.title in the UI language.
std::string SettingsCanvas::iconTitle(std::string_view id, const std::string& english) const {
    for (const auto& name : localizedIcons) if (name.id == id) return text(name.english, name.simplifiedChinese);
    return english;
}

// The layout preview's deadline: the controller's seconds (12 s, HUDSettingsController.layoutConfirmationDuration).
std::optional<double> SettingsCanvas::nextWakeTime(double now) const { return controller_->nextWakeTime(now); }
void SettingsCanvas::wake(double now) { controller_->wake(now); }
}
