#include "modules/volume_canvas.hpp"
#include "modules/hud_design_badges.hpp"
#include <algorithm>
#include <cmath>

namespace endfield::modules {
namespace d = design;
using native::VolumeAppState;
namespace {
constexpr double listTop = d::metric::headerHeight + 34 + 10;
// A compact slider row with its PID line grows to 54 px (demo layout).
constexpr double appRowHeight = 54;
std::string pid(const native::VolumeApplication& app) { return "PID " + std::to_string(app.pid); }
// The Mac canvas keeps no reference to its HUDRowPlates (only the sliders,
// dropdowns and mute button join the page), so each plate is released once
// built: its target still records the highlight but nothing answers it. The
// device, slider and app rows never show hover, and an app row's state line
// stays as built (its binder holds the plate weakly, :365-368). Copied.
void release(d::RowPlate& plate) {
    for (const auto& target : plate.layer().targets()) if (target.get() == plate.target()) target->onChange = nullptr;
}
}

// VolumeListPan (:443-451): presses between the sliders pan the list; not in
// the accessibility tree.
class VolumeCanvas::ListPan final : public d::Control {
public:
    ListPan(d::Builder& b, std::string id) : Control(b.env, id, {}, b.layer(id)) {}
    std::function<void(double)> onBegin, onDrag;
    bool pointerDown(d::Point p) override { if (onBegin) onBegin(p.y); return true; }
    void pointerDragged(d::Point p) override { if (onDrag) onDrag(p.y); }
    void pointerUp(d::Point) override {}
    std::optional<d::AccessibilityRole> accessibilityRole() const override { return std::nullopt; }
};

VolumeCanvas::VolumeCanvas(d::Context& context, native::VolumeSnapshot initial, native::VolumeCallbacks actions, VolumeIconHooks icons)
    : Canvas(core::Module::volume, context), snapshot_(std::move(initial)), actions_(std::move(actions)), icons_(std::move(icons)) {
    native::validateVolumeSnapshot(snapshot_);
}
// Device observation ends here; enabled per-app routes deliberately survive
// leaving or destroying the Volume surface (:51-57).
VolumeCanvas::~VolumeCanvas() {
    if (started_ && actions_.setActive) try { actions_.setActive(false); } catch (...) {}
}

// --- Lifecycle (:61-77) ---------------------------------------------------------------
void VolumeCanvas::didActivate() {
    started_ = true;
    if (actions_.setActive) actions_.setActive(true);
    lastStatus_ = snapshot_.status;
    changed();
    requestIcons();
}
void VolumeCanvas::didDeactivate() {
    started_ = false;
    if (actions_.setActive) actions_.setActive(false);
    gesture_.reset(); panOrigin_.reset();
    iconRequest_.clear();
    if (icons_.hide) icons_.hide();
}
void VolumeCanvas::show(double now) {
    if (renderedLanguage()) { env().now = now; changed(); }
    Canvas::show(now);
}
void VolumeCanvas::receive(native::VolumeSnapshot next) {
    native::validateVolumeSnapshot(next);
    if (next == snapshot_) return;
    snapshot_ = std::move(next);
    changed();
}

const native::VolumeDevice* VolumeCanvas::currentOutput() const {
    const auto it = std::find_if(snapshot_.outputs.begin(), snapshot_.outputs.end(), [&](const auto& d) { return d.id == snapshot_.outputID; });
    return it == snapshot_.outputs.end() ? nullptr : &*it;
}
const native::VolumeApplication* VolumeCanvas::application(std::string_view id) const {
    const auto it = std::find_if(snapshot_.applications.begin(), snapshot_.applications.end(), [&](const auto& a) { return a.id == id; });
    return it == snapshot_.applications.end() ? nullptr : &*it;
}
// PerAppAudioController.statusMessage: the runtime's per-app status, or the
// route stop after an output change (stopAll(reason:)).
std::optional<std::string> VolumeCanvas::perAppStatus() const {
    if (snapshot_.perAppStatus) return snapshot_.perAppStatus->empty() ? std::nullopt : snapshot_.perAppStatus;
    if (snapshot_.routingStopped) return text("App routing stopped because the output device changed.", "输出设备已更改，应用混音已停止。");
    return std::nullopt;
}
int VolumeCanvas::visibleApplicationRows() const { return perAppStatus() ? 2 : 3; }
double VolumeCanvas::applicationScrollOffset() const noexcept { return appList_ ? appList_->offset() : 0; }
d::Rect VolumeCanvas::appViewport() const {
    const double rows = visibleApplicationRows();
    return {0, listTop, d::metric::boxWidth, rows * appRowHeight + (rows - 1) * d::metric::rowGap};
}
// A session (a route in any state) or, with none, a route that could start
// on the current output. Windows lists the runtime's applications in its
// order; `available` already folds the output check in.
std::vector<const native::VolumeApplication*> VolumeCanvas::listedApplications() const {
    std::vector<const native::VolumeApplication*> out;
    if (!snapshot_.applicationActivitySupported) return out;
    for (const auto& app : snapshot_.applications)
        if (app.state != VolumeAppState::direct || (app.available && currentOutput())) out.push_back(&app);
    return out;
}
VolumeCanvas::Structure VolumeCanvas::structure() const {
    Structure s{env().language, tab_, {}, snapshot_.applicationActivitySupported, tab_ == 1 && perAppStatus().has_value()};
    if (tab_ == 1) for (const auto* app : listedApplications()) s.apps.push_back(app->id);
    return s;
}

void VolumeCanvas::changed() {
    if (!renderedLanguage()) return;
    // A gesture never retargets to another device or a control that became read-only.
    if (gesture_ && gesture_->output != snapshot_.outputID) cancelSliderGestures();
    // A route starting under a drag changes the list's status line; the
    // rebuild waits for the release so the gesture is never cut off.
    if ((!renderedStructure_ || structure() != *renderedStructure_) && !sliding()) rerender();
    else for (const auto& bind : std::vector(binders_)) bind();
    if (!isActive()) return;
    const auto status = snapshot_.status;
    if (status && status != lastStatus_) toast(d::Icon::warn, "Notice", "提示", *status);
    lastStatus_ = status;
}
bool VolumeCanvas::sliding() const {
    return std::any_of(page().controls().begin(), page().controls().end(), [](d::Control* c) {
        const auto* slider = dynamic_cast<const d::Slider*>(c);
        return slider && slider->dragging();
    });
}
void VolumeCanvas::cancelSliderGestures() {
    for (d::Control* c : page().controls()) if (auto* slider = dynamic_cast<d::Slider*>(c); slider && slider->dragging()) slider->cancelDrag();
    gesture_.reset();
}

// --- Page (:125-206) ---------------------------------------------------------------------
void VolumeCanvas::buildPage(d::Page& page) {
    binders_.clear(); appList_ = nullptr; masterSlider_ = nullptr; selectedSlider_ = nullptr; iconLayers_.clear();
    renderedStructure_ = structure();
    auto& tabs = page.makeTabs("volumeTab", {{text("Devices", "设备"), d::Icon::devices}, {text("App volume", "应用音量"), d::Icon::apps}},
        tab_, d::metric::boxWidth);
    tabs_ = &tabs;
    // The orange dot marks App volume only while some app's volume can be adjusted.
    news_ = hasAdjustableApplication();
    tabs.setNews(1, news_);
    tabs.onSelect = [this](int index, double direction) { tab_ = index; navigate(direction); };
    tabs.layer().setOrigin({0, d::metric::headerHeight});
    page.layer().add(tabs.layer());
    page.stagger(tabs.layer());
    page.tabs = &tabs;
    page.add(tabs.controls());
    if (tab_ == 0) buildDevices(page); else buildApplications(page);
    header().configure({"VOLUME"}, "MOD-08", title("Volume", "音量"), counterText(), false);
    binders_.push_back([this] {
        const bool adjustable = hasAdjustableApplication();
        if (tabs_ && news_ != adjustable) { news_ = adjustable; tabs_->setNews(1, adjustable); }
        if (masterSlider_ && masterSlider_->dragging()) return;
        header().setCounter(counterText());
    });
    // The rows just built (the Mac renders every icon at build): the request
    // binds icons already cached at once, so the rows take them now rather
    // than as a later change.
    requestIcons();
    (void)iconsChanged();
}
bool VolumeCanvas::hasAdjustableApplication() const {
    const auto apps = listedApplications();
    return std::any_of(apps.begin(), apps.end(), [&](const auto* app) { return routeAdjustable(*app); });
}
// Master volume as two digits, or MUTED (spec 3.13 header).
std::string VolumeCanvas::counterText() const {
    if (snapshot_.muted == true) return text("MUTED", "已静音");
    return d::format::pad2(std::int64_t(std::round(snapshot_.volume.value_or(0) * 100)));
}
std::string VolumeCanvas::muteLabel() const { return snapshot_.muted == true ? text("Unmute", "取消静音") : text("Mute", "静音"); }

void VolumeCanvas::buildDevices(d::Page& page) {
    auto& output = dropdown(page, "output", text("Output", "输出"), false);
    auto& input = dropdown(page, "input", text("Input", "输入"), true);
    auto& mute = page.make<d::IconButton>("mute", muteLabel(), snapshot_.muted == true ? d::Icon::mute : d::Icon::vol);
    mute.onActivate = [this] {
        if (!snapshot_.canSetMute || !snapshot_.muted) return;
        if (actions_.setMute) (void)actions_.setMute(snapshot_.outputID, !*snapshot_.muted);
    };
    binders_.push_back([this, &mute, shown = std::make_shared<bool>(snapshot_.muted == true)] {
        const bool muted = snapshot_.muted == true;
        // The glyph is swapped only when it changes (the Mac sets it each time; same pixels).
        if (*shown != muted) { *shown = muted; mute.setIcon(muted ? d::Icon::mute : d::Icon::vol); }
        mute.setOn(muted); mute.label = muteLabel();
        mute.setEnabled(snapshot_.canSetMute && snapshot_.muted.has_value());
    });
    const auto volume = sliderRow(page, "volume", text("Volume", "音量"), text("Output volume", "输出音量"), {0, 100, 1, std::nullopt},
        d::ValueFormat::percent, &mute, d::metric::boxWidth, std::nullopt,
        [this]() -> std::optional<double> { return snapshot_.volume ? std::optional(*snapshot_.volume * 100) : std::nullopt; },
        [this] { return snapshot_.canSetVolume && snapshot_.volume.has_value(); },
        [this](double value, bool) {
            header().setCounter(d::format::pad2(std::int64_t(std::round(value))));
            if (actions_.setVolume) (void)actions_.setVolume(snapshot_.outputID, value / 100);
        });
    masterSlider_ = volume.slider;
    const auto balance = sliderRow(page, "balance", text("Balance", "平衡"), text("Left/right balance", "左右声道平衡"), {-100, 100, 1, 0.0},
        d::ValueFormat::balance, nullptr, d::metric::boxWidth, std::nullopt,
        [this]() -> std::optional<double> { return snapshot_.balance ? std::optional(*snapshot_.balance * 100) : std::nullopt; },
        [this] { return snapshot_.canSetBalance && snapshot_.balance.has_value(); },
        [this](double value, bool) { if (actions_.setBalance) (void)actions_.setBalance(snapshot_.outputID, value / 100); });
    std::vector<d::RowPlate*> rows;
    for (const auto& [id, title, field] : {std::tuple{std::string("output"), text("Output", "输出"), &output},
                                            std::tuple{std::string("input"), text("Input", "输入"), &input}}) {
        auto& plate = page.make<d::RowPlate>("row:" + id, title, std::nullopt, d::RowPlate::Variant::compact);
        plate.setCluster({&field->layer()});
        release(plate);
        rows.push_back(&plate);
    }
    rows.push_back(volume.row); rows.push_back(balance.row);
    double y = listTop;
    for (auto* row : rows) {
        row->layer().setOrigin({0, y});
        page.layer().add(row->layer());
        y += row->layer().bounds().height + d::metric::rowGap;
    }
    for (auto* row : rows) page.stagger(row->layer());
    page.add(std::vector<d::Control*>{&output, &input, volume.slider, &mute, balance.slider});
    for (const auto& bind : binders_) bind();
}

d::Dropdown& VolumeCanvas::dropdown(d::Page& page, std::string id, std::string label, bool input) {
    auto& field = page.make<d::Dropdown>(id, std::move(label), deviceName(input));
    field.onActivate = [this, &field, input] { if (menuOpen()) closeMenu(); else openDeviceMenu(input, field); };
    binders_.push_back([this, &field, input] {
        const auto name = deviceName(input);
        if (field.valueText() != name) field.setValue(name);
        field.setEnabled(input ? snapshot_.canSetDefaultInput && !snapshot_.inputs.empty() : snapshot_.canSetDefaultOutput && !snapshot_.outputs.empty());
    });
    return field;
}
std::string VolumeCanvas::deviceName(bool input) const {
    const native::VolumeDevice* device = nullptr;
    if (input) {
        const auto it = std::find_if(snapshot_.inputs.begin(), snapshot_.inputs.end(), [&](const auto& d) { return d.id == snapshot_.inputID; });
        if (it != snapshot_.inputs.end()) device = &*it;
    } else device = currentOutput();
    return device ? device->name : text("No device", "无设备");
}
// Every connected device, headphones and Bluetooth included (:230-244).
void VolumeCanvas::openDeviceMenu(bool input, const d::Dropdown& anchor) {
    const auto& devices = input ? snapshot_.inputs : snapshot_.outputs;
    const auto& selected = input ? snapshot_.inputID : snapshot_.outputID;
    std::vector<d::Menu::Item> items;
    for (const auto& device : devices)
        items.push_back({.id = device.id, .title = device.name, .selected = device.id == selected,
            .enabled = input ? device.canBeDefaultInput : device.canBeDefaultOutput});
    openMenu(input ? "input" : "output", std::move(items), anchor, std::nullopt, [this, input](const d::Menu::Item& item) {
        if (input) { if (actions_.setDefaultInput) (void)actions_.setDefaultInput(item.id); return; }
        // "App routing stopped because the output device changed." (the runtime reports it as routingStopped).
        if (item.id != snapshot_.outputID && actions_.stopAllApps) actions_.stopAllApps();
        if (actions_.setDefaultOutput) (void)actions_.setDefaultOutput(item.id);
    });
}

// A compact slider row; `trailing` (the mute button) sits after the readout (:246-281).
VolumeCanvas::SliderRow VolumeCanvas::sliderRow(d::Page& page, std::string id, std::string title, std::string accessibility,
    d::Slider::Range range, d::ValueFormat format, d::IconButton* trailing, double width, std::optional<double> height,
    std::function<std::optional<double>()> read, std::function<bool()> writable, std::function<void(double, bool)> change) {
    auto& plate = page.make<d::RowPlate>("row:" + id, std::move(title), std::nullopt, d::RowPlate::Variant::slider, "›", width, height);
    release(plate);
    d::Rect area = plate.controlArea();
    if (trailing) area.width -= trailing->layer().bounds().width + 8;
    auto& slider = page.make<d::Slider>(id, std::move(accessibility), read().value_or(range.minimum), range, format, area.width);
    slider.layer().setOrigin({area.x, area.y});
    plate.layer().add(slider.layer());
    if (trailing) {
        trailing->layer().setPosition({area.x + area.width + 8 + trailing->layer().bounds().width / 2, area.y + area.height / 2});
        plate.layer().add(trailing->layer());
    }
    hostBubble(slider);
    slider.onChange = [this, &slider, change](double value, bool final) {
        selectedSlider_ = &slider;
        if (!gesture_) gesture_ = Gesture{snapshot_.outputID};
        if (gesture_->output != snapshot_.outputID) return;
        change(value, final);
        if (final) {
            gesture_.reset();
            changed();
            if (context().onInputStateChange) context().onInputStateChange();
        }
    };
    binders_.push_back([this, &slider, read, writable, range] {
        const bool enabled = writable();
        if (slider.dragging() && !enabled) { slider.cancelDrag(); gesture_.reset(); }
        slider.setEnabled(enabled);
        if (!slider.dragging()) slider.setValue(read().value_or(range.minimum));
    });
    return {&plate, &slider};
}

// --- App volume (:283-370) ----------------------------------------------------------------
void VolumeCanvas::buildApplications(d::Page& page) {
    const auto apps = listedApplications();
    const auto viewport = appViewport();
    if (!snapshot_.applicationActivitySupported || apps.empty()) {
        const std::string heading = env().text.uppercase ? env().text.uppercase(text("No adjustable audio apps", "暂无可调节音量的应用"))
                                                         : d::asciiUppercase(text("No adjustable audio apps", "暂无可调节音量的应用"));
        // Windows' own unsupported message stands for the Mac's macOS 14.2 requirement.
        const std::optional<std::string> detail = snapshot_.applicationActivitySupported ? snapshot_.applicationMessage
            : snapshot_.applicationMessage ? snapshot_.applicationMessage
            : std::optional(text("Active audio app information is unavailable.", "活跃音频应用信息不可用。"));
        auto& empty = d::emptyState::make(page.builder(), "volume.apps.empty", heading, detail, std::nullopt, {d::metric::boxWidth, viewport.height});
        empty.setOrigin({0, listTop});
        page.layer().add(empty);
        page.stagger(empty);
        buildStatus(page, viewport.y + viewport.height + 10);
        return;
    }
    auto& area = page.scrollArea("apps", viewport);
    const double count = double(apps.size());
    area.setContentHeight(count * appRowHeight + (count - 1) * d::metric::rowGap);
    // Presses between the sliders drag the list (sliders sit above it).
    auto& pan = page.make<ListPan>("apps.pan");
    pan.layer().setFrame(d::localBounds(area.layer()));
    area.layer().insert(pan.layer(), 0);
    pan.onBegin = [this, &area](double y) { panOrigin_ = std::pair{y, area.offset()}; };
    pan.onDrag = [this, &area](double y) {
        if (!panOrigin_) return;
        area.setOffset(panOrigin_->second - (y - panOrigin_->first), false);
        requestIcons();
    };
    page.add(pan);
    double y = 0;
    for (const auto* app : apps) {
        auto& row = applicationRow(page, *app, area.contentWidth());
        row.layer().setOrigin({0, y});
        area.content().add(row.layer());
        page.stagger(row.layer());
        y += appRowHeight + d::metric::rowGap;
    }
    appList_ = &area;
    page.restoreOffset(area);
    buildStatus(page, viewport.y + viewport.height + 10);
    for (const auto& bind : binders_) bind();
}
// The per-app status line, in the third row's place (Windows A2, :325-338).
void VolumeCanvas::buildStatus(d::Page& page, double top) {
    const auto message = perAppStatus();
    if (!message) return;
    auto& line = d::makeText(page.builder(), "volume.status", *message,
        d::TextStyle{.size = 11, .weight = d::FontWeight::regular, .color = env().palette.token(d::ThemeToken::steel), .lineHeight = 1.4},
        {18, top, d::metric::boxWidth - 36, 11 * 1.4 * 2});
    d::editText(line, [](d::TextBlock& block) { block.maximumLines = 2; });
    binders_.push_back([this, &line] {
        const auto current = perAppStatus();
        if (current && d::textOf(line).text() != *current) d::editText(line, [&](d::TextBlock& block) { block.setText(*current); });
    });
    page.layer().add(line);
    page.stagger(line);
}
// Icon square, app name, `PID n` (or the route's state) and its slider (:340-370).
d::RowPlate& VolumeCanvas::applicationRow(d::Page& page, const native::VolumeApplication& app, double width) {
    const std::string appID = app.id;
    const std::string identity = app.name == pid(app) ? app.name : app.name + " / " + pid(app);
    const auto row = sliderRow(page, "app:" + appID, "", identity + text(" volume", " 音量"), {0, 100, 1, std::nullopt},
        d::ValueFormat::percent, nullptr, width, appRowHeight,
        [this, appID]() -> std::optional<double> { const auto* a = application(appID); return (a && a->gain ? *a->gain : 1.0) * 100; },
        [this, appID] { const auto* a = application(appID); return a && routeAdjustable(*a); },
        [this, appID](double value, bool) { if (const auto* a = application(appID)) (void)setGain(value / 100, *a); });
    auto& plate = *row.row;
    plate.setSublabel(routeState(app));
    auto& icon = page.makeLayer("volume.app.icon:" + appID);
    // The 18 px square and the name share the label line (centre y 20.5).
    icon.setFrame({18, 11.5, 18, 18});
    placeIcon(icon, appID);
    iconLayers_.emplace_back(appID, &icon);
    auto& name = d::makeText(page.builder(), "volume.app.name:" + appID, app.name, d::type::rowLabel(),
        {18 + 18 + 8, 20.5 - 15.5 * .6, d::metric::sliderLabelColumn - 26, 15.5 * 1.2});
    plate.layer().add(icon); plate.layer().add(name);
    page.add(*row.slider);
    return plate;
}
void VolumeCanvas::placeIcon(d::Layer& layer, std::string_view applicationID) {
    if (const auto image = icons_.icon ? icons_.icon(applicationID) : std::nullopt)
        layer.setContent(d::image("image", d::localBounds(layer), *image, "resizeAspect"));
    else d::setBackground(layer, d::rgb(0x4A4A4A));
}
bool VolumeCanvas::iconsChanged() {
    bool changed = false;
    for (const auto& [id, layer] : iconLayers_) {
        const auto before = layer->content();
        placeIcon(*layer, id);
        changed = changed || layer->content() != before;
    }
    return changed;
}
// The apps whose rows are in view or next to it (at most the icon plan's
// eight), named on content and scroll events only.
void VolumeCanvas::requestIcons() {
    if (!icons_.show || !isActive()) return;
    std::vector<std::string> wanted;
    if (tab_ == 1 && appList_) {
        const auto apps = listedApplications();
        const double pitch = appRowHeight + d::metric::rowGap, offset = appList_->offset(), height = appList_->layer().bounds().height;
        const long long first = std::max(0LL, (long long)std::floor(offset / pitch) - 1);
        const long long last = std::min((long long)apps.size() - 1, (long long)std::ceil((offset + height) / pitch));
        for (long long n = first; n <= last && wanted.size() < 8; ++n) wanted.push_back(apps[std::size_t(n)]->id);
    }
    if (wanted == iconRequest_) return;
    iconRequest_ = wanted;
    if (wanted.empty()) { if (icons_.hide) icons_.hide(); return; }
    icons_.show(wanted);
}

bool VolumeCanvas::routeAdjustable(const native::VolumeApplication& app) const {
    if (app.state != VolumeAppState::direct) return app.state != VolumeAppState::stopping;
    return app.available && currentOutput();
}
std::string VolumeCanvas::routeState(const native::VolumeApplication& app) const {
    switch (app.state) {
    case VolumeAppState::active: return pid(app);
    case VolumeAppState::preparing: return text("Starting…", "正在启动…");
    case VolumeAppState::stopping: return text("Stopping…", "正在停止…");
    case VolumeAppState::failed: return text("Route failed", "路由失败");
    case VolumeAppState::direct: return routeAdjustable(app) ? pid(app) : text("Unsupported route", "暂不支持此路由");
    }
    return {};
}
bool VolumeCanvas::setGain(double value, const native::VolumeApplication& app) {
    if (!std::isfinite(value) || !routeAdjustable(app)) return false;
    const double bounded = std::min(1.0, std::max(0.0, value));
    if (app.state != VolumeAppState::direct) {
        if (app.state == VolumeAppState::failed && bounded == 1) { if (actions_.stopApp) (void)actions_.stopApp(app.id); return true; }
        return actions_.setAppGain && actions_.setAppGain(app.id, bounded);
    }
    if (bounded == 1) return true;
    if (!currentOutput()) return false;
    // PerAppAudioController.start(application:output:initialGain:): the runtime starts the route at this gain.
    return actions_.setAppGain && actions_.setAppGain(app.id, bounded);
}

// --- Input (:415-440) ------------------------------------------------------------------
// The wheel glides the app list (Windows A2) instead of jumping.
bool VolumeCanvas::handleScroll(d::Point point, d::Point delta) {
    if (!appList_ || transitioning()) return false;
    const d::Rect frame = appList_->layer().frame();
    const bool inside = point.x >= frame.x && point.y >= frame.y && point.x < frame.x + frame.width && point.y < frame.y + frame.height;
    if (!inside || appList_->maximumOffset() <= 0) return false;
    appList_->setOffset(appList_->offset() + delta.y, true);
    requestIcons();
    return true;
}
// Left / Right adjust the last slider touched (Volume until another is).
bool VolumeCanvas::handleKey(const d::KeyInput& key) {
    if (key.commandModifiers() || key.shift || (key.virtualKey != d::key::left && key.virtualKey != d::key::right)) return false;
    d::Slider* slider = selectedSlider_ ? selectedSlider_ : masterSlider_;
    if (!slider) for (d::Control* c : page().controls()) if ((slider = dynamic_cast<d::Slider*>(c))) break;
    if (!slider || !slider->enabled()) return false;
    selectedSlider_ = slider;
    slider->nudge(key.virtualKey == d::key::left ? -2 : 2);
    return true;
}
// App rows scrolled out of the list are not offered to assistive technology.
void VolumeCanvas::accessibilityItems(std::vector<d::AccessibilityItem>& out) const {
    const std::size_t start = out.size();
    Canvas::accessibilityItems(out);
    if (!appList_) return;
    const d::Rect v = appList_->layer().frame();
    out.erase(std::remove_if(out.begin() + std::ptrdiff_t(start), out.end(), [&](const d::AccessibilityItem& item) {
        if (!std::string_view(item.id).starts_with("app:")) return false;
        const d::Rect r = item.rect;
        return !(r.x < v.x + v.width && v.x < r.x + r.width && r.y < v.y + v.height && v.y < r.y + r.height);
    }), out.end());
}
} // namespace endfield::modules
