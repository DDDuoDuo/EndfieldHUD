// HUDAccountCanvas (Sources/HUDAccountCanvas.swift), statement by statement
// (line numbers cite it).
#include "modules/account_canvas.hpp"
#include "modules/hypergryph_account_sanity_gauge.hpp"
#include <algorithm>
#include <cmath>

namespace endfield::modules {
namespace d = design;
namespace h = hypergryph;
using Region = h::AccountPresentation::RegionChoice;
using Header = h::AccountPresentation::HeaderMode;
namespace {
constexpr std::array<Region, 2> regions{Region::china, Region::global};
constexpr std::array<Header, 4> headerModes{Header::workMode, Header::endfield, Header::arknights, Header::hidden};
int regionIndex(Region r) { return r == Region::global ? 1 : 0; }
h::AccountAction action(h::AccountAction::Kind kind) { h::AccountAction a; a.kind = kind; return a; }
}

// The hero row's two dark stat plates (:348-431): SANITY with the 56 px
// numeral, `/ max` and 24 capacity cells; NEXT POINT and ALL RECOVERED. The
// values change in place every second (only the texts that changed: the
// readout dedupes on its drawn state).
class AccountCanvas::SanityReadout final {
public:
    static constexpr double height = 9 + 10 + 4 + 56 * .9 + 8 + 8 + 10;
    SanityReadout(d::Builder& b, d::Environment& env, double sanityWidth, double recoveryWidth)
        : env_(&env), layer_(&b.layer("account.hero")) {
        const auto& p = env.palette;
        d::Layer& sanity = statPlate(b, "account.stat.sanity", {sanityWidth, height});
        d::Layer& recovery = statPlate(b, "account.stat.recovery", {recoveryWidth, height});
        plate_ = &b.controls.make<d::Control>(env, "sanity", core::localized("Sanity", "理智", env.language), sanity);
        cells_ = std::make_unique<d::Cells>(b, "account.cells", 24, sanityWidth - 24, true, 8);
        layer_->setBounds({sanityWidth + 10 + recoveryWidth, height});
        sanity.setOrigin({0, 0});
        recovery.setOrigin({sanityWidth + 10, 0});
        d::Layer& label = d::makeText(b, "account.sanity.label", "SANITY", d::type::label(p), {12, 9, sanityWidth - 24, 10});
        number_ = &d::makeText(b, "account.sanity.number", "", d::type::numeral(p).sized(56), {12, 23, 1, 56 * .9});
        maximum_ = &d::makeText(b, "account.sanity.maximum", "", d::type::label(p), {12, 0, 1, 10});
        cells_->layer().setOrigin({12, height - 10 - 8});
        // CA draws a layer's border above its sublayers: the ring goes last.
        for (d::Layer* child : {&label, number_, maximum_, &cells_->layer()}) sanity.insertBelow(*child, *rings_[0]);
        const std::array<std::pair<const char*, d::Layer**>, 2> values{{{"NEXT POINT", &next_}, {"ALL RECOVERED", &full_}}};
        for (std::size_t index = 0; index < values.size(); ++index) {
            const double top = 9 + double(index) * (10 + 6 + 18 + 12);
            const std::string suffix = std::to_string(index);
            d::Layer& caption = d::makeText(b, "account.recovery.caption." + suffix, values[index].first, d::type::label(p), {12, top, recoveryWidth - 24, 10});
            *values[index].second = &d::makeText(b, "account.recovery.value." + suffix, "", d::type::statValue(p), {12, top + 16, recoveryWidth - 24, 18});
            recovery.insertBelow(caption, *rings_[1]); recovery.insertBelow(**values[index].second, *rings_[1]);
        }
        layer_->add(sanity); layer_->add(recovery);
        plate_->setTarget(d::localBounds(sanity));
    }
    d::Layer& layer() const noexcept { return *layer_; }
    d::Control& plate() const noexcept { return *plate_; }
    const std::optional<SanityState>& rendered() const noexcept { return rendered_; }
    // render(_:at:) (:408-430).
    void render(const std::optional<h::SanityPresentation>& value, h::Time date) {
        SanityState state;
        state.current = value ? std::to_string(value->current) : "—";
        state.limit = "/ " + (value ? std::to_string(value->maximum) : std::string("—"));
        const int filled = value ? (value->maximum > 0 ? int(std::round(double(value->current) / double(value->maximum) * 24)) : 0) : 0;
        state.filled = std::min(24, filled);
        state.next = h::gaugeCountdown(value ? value->nextRecoveryAt : std::nullopt, date, true);
        state.full = h::gaugeCountdown(value ? value->fullRecoveryAt : std::nullopt, date, true);
        if (rendered_ && *rendered_ == state) return;
        const bool first = !rendered_;
        rendered_ = state;
        const auto& text = env_->text;
        const double width = std::ceil(text.width(state.current, d::textOf(*number_).style()));
        d::editText(*number_, [&](d::TextBlock& block) { block.setText(state.current); block.setLineFrame({12, 23, width + 1, 56 * .9}); });
        const double limitWidth = std::ceil(text.width(state.limit, d::textOf(*maximum_).style()));
        d::editText(*maximum_, [&](d::TextBlock& block) { block.setText(state.limit); block.setLineFrame({12 + width + 10, 23 + 56 * .9 - 8 - 10, limitWidth + 1, 10}); });
        d::editText(*next_, [&](d::TextBlock& block) { block.setText(state.next); });
        d::editText(*full_, [&](d::TextBlock& block) { block.setText(state.full); });
        cells_->set(state.filled, std::nullopt, false, !first);
        plate_->label = core::localized("Sanity", "理智", env_->language) + " " + state.current + " " + state.limit;
    }
private:
    d::Environment* env_;
    d::Layer *layer_, *number_{}, *maximum_{}, *next_{}, *full_{};
    std::array<d::Layer*, 2> rings_{};
    d::Control* plate_{};
    std::unique_ptr<d::Cells> cells_;
    std::optional<SanityState> rendered_;
    // The `.sp` plate (:391-406): dark gradient, 1 px ringTab border, grey L-corner.
    d::Layer& statPlate(d::Builder& b, const std::string& id, d::Size size) {
        const auto& p = b.env.palette;
        d::Layer& plate = b.layer(id);
        plate.setBounds(size);
        const std::array<d::Color, 2> colors{p.tone(0x232323), p.tone(0x1B1B1B)};
        plate.setContent(d::plate(plate.id(), {0, 0, size.width, size.height}, colors, {}, 0));
        d::Layer& corner = b.layer(id + ".corner");
        corner.setFrame({0, 0, 8, 8});
        d::setPath(corner, d::cornerMark({0, 0, 8, 8}, 8, 2, d::topLeft), p.tone(0x6A6A6A));
        d::Layer& ring = b.layer(id + ".ring");
        ring.setFrame({0, 0, size.width, size.height});
        d::setBorder(ring, std::nullopt, p.token(d::ThemeToken::ringTab), 1);
        plate.add(corner); plate.add(ring);
        rings_[rings_[0] ? 1 : 0] = &ring;
        return plate;
    }
};

// A field chip that toggles (:433-475): 24 px tall, yellow with a check when
// on, outlined when off. Its width follows the check: the owner re-wraps.
class AccountCanvas::Chip final : public d::Control {
public:
    Chip(d::Builder& b, std::string id, std::string title, bool isOn)
        : Control(b.env, id, title, b.layer(id)), on_(isOn), title_(std::move(title)) {
        check_ = &d::makeIcon(b, this->id() + ".check", d::Icon::check, 10, d::fixed::nearBlack);
        text_ = &d::makeText(b, this->id() + ".text", title_, d::type::chip(), {7, 7.25, 1, 9.5});
        layer().add(*check_); layer().add(*text_);
        render();
    }
    std::function<void(bool)> onToggle;
    std::function<void()> onResize;
    bool isOn() const noexcept { return on_; }
    void setOn(bool value) { if (value == on_) return; on_ = value; render(); if (onResize) onResize(); }
    void activate() override {
        if (!enabled()) return;
        on_ = !on_; render(); if (onResize) onResize();
        if (onToggle) onToggle(on_);
    }
    std::optional<d::AccessibilityRole> accessibilityRole() const override { return d::AccessibilityRole::toggle; }
    void accessibilityItems(const d::Layer& root, std::vector<d::AccessibilityItem>& out) const override {
        if (label.empty()) return;
        auto item = accessibilityItem(root, d::AccessibilityRole::toggle); item.on = on_;
        out.push_back(std::move(item));
    }
private:
    bool on_; std::string title_; d::Layer *check_, *text_;
    void render() {
        const auto& p = env().palette;
        const auto style = d::type::chip();
        const double width = std::ceil(text().width(title_, style));
        double x = 7;
        check_->setHidden(!on_);
        if (on_) { check_->setFrame({x, 7, 10, 10}); x += 15; }
        d::editText(*text_, [&](d::TextBlock& block) { block.setStyle(style.colored(on_ ? d::fixed::nearBlack : p.tone(0xCFCFCF))); block.setLineFrame({x, 7.25, width + 1, 9.5}); });
        layer().setBounds({x + width + 7, 24});
        if (on_) d::setBackground(layer(), p.yellow()); else d::setBorder(layer(), std::nullopt, p.tone(0x5A5A5A), 1);
        setTarget(d::localBounds(layer()));
    }
};

AccountCanvas::AccountCanvas(d::Context& context) : Canvas(core::Module::account, context) {}
AccountCanvas::~AccountCanvas() = default;
h::Time AccountCanvas::wallNow() const { return wallClock ? wallClock() : 0; }
void AccountCanvas::act(h::AccountAction a) { if (onAction) onAction(a); }

// MARK: Presentation (:92-125)
void AccountCanvas::update(const Presentation& value) {
    if (value == presentation_) return;
    const auto old = presentation_;
    presentation_ = value;
    if (!renderedLanguage()) return;
    if (old.region != value.region || old.roles != value.roles || (value.busy() && !old.busy())) closeMenu();
    if (old.region != value.region) {
        // Each region keeps its own link: Global is forward (spec §3.21).
        awaitingUnlink_ = false; awaitingRefresh_ = false;
        navigate(value.region == Region::global ? 1 : -1);
    } else if (old.isLinked != value.isLinked) {
        navigate(value.isLinked ? 1 : -1);
        if (value.isLinked) toast(d::Icon::link2, "Account linked", "账户已绑定", h::regionChoiceTitle(value.region, env().language));
        else if (awaitingUnlink_) toast(d::Icon::x, "Disconnected", "已断开连接");
        awaitingUnlink_ = false; awaitingRefresh_ = false;
    } else if (structure() != renderedStructure_) rerender();
    else for (const auto& binder : binders_) binder();
    if (awaitingRefresh_ && old.status == Presentation::Status::refreshing && value.status != Presentation::Status::refreshing) {
        awaitingRefresh_ = false;
        if (value.status == Presentation::Status::connected) {
            const auto sanity = sanityPresentation();
            toast(d::Icon::refresh, "Sanity refreshed", "理智已刷新",
                sanity ? std::optional(std::to_string(sanity->current) + " / " + std::to_string(sanity->maximum)) : std::nullopt);
        }
    }
    if (isActive() && value.statusMessage != old.statusMessage && !value.statusMessage.empty())
        toast(d::Icon::warn, "Notice", "提示", value.statusMessage);
    updateClock();
}
AccountCanvas::Structure AccountCanvas::structure() const {
    return {presentation_.isLinked, presentation_.region, presentation_.needsReconnect, !presentation_.lastSync.empty(), env().language};
}
std::optional<h::SanityPresentation> AccountCanvas::sanityPresentation() const {
    return presentation_.snapshot ? presentation_.snapshot->sanityPresentation(wallNow()) : std::nullopt;
}
std::optional<AccountCanvas::SanityState> AccountCanvas::renderedSanity() const { return sanity_ ? sanity_->rendered() : std::nullopt; }

// MARK: Lifecycle (:129-146)
void AccountCanvas::didActivate() { updateClock(); if (sanity_) sanity_->render(sanityPresentation(), wallNow()); }
void AccountCanvas::didDeactivate() { clockStart_.reset(); awaitingRefresh_ = false; }
// The recovery countdowns are a live readout: one tick a second while the
// linked page is on screen, none otherwise (the Mac's 1 s repeating Timer,
// tolerance .1: a wake each second from its start on the owner's clock).
void AccountCanvas::updateClock() {
    const bool wanted = isActive() && presentation_.isLinked && presentation_.snapshot && presentation_.snapshot->stamina;
    if (!wanted) { clockStart_.reset(); return; }
    if (!clockStart_) clockStart_ = now();
}
std::optional<double> AccountCanvas::nextWakeTime(double at) const {
    if (!clockStart_) return std::nullopt;
    return *clockStart_ + std::floor(std::max(0.0, at - *clockStart_)) + 1;
}
void AccountCanvas::wake(double) { if (clockStart_ && sanity_) sanity_->render(sanityPresentation(), wallNow()); }

// MARK: Pages (:150-277)
void AccountCanvas::buildPage(d::Page& page) {
    binders_.clear(); sanity_ = nullptr;
    renderedStructure_ = structure();
    const auto& p = presentation_;
    std::vector<d::Control*> actions;
    if (p.isLinked) {
        if (p.needsReconnect) {
            auto& reconnect = page.make<d::Button>("reconnect", text("Reconnect", "重新连接"), d::Icon::link2, d::Button::Kind::primary, d::Button::Height::compact);
            reconnect.place({0, 0});
            reconnect.onActivate = [this] { auto a = action(h::AccountAction::Kind::connect); a.region = presentation_.region; act(a); };
            bind([&reconnect](const Presentation& p) { reconnect.setEnabled(!p.busy()); });
            actions.push_back(&reconnect);
        }
        auto& disconnect = page.make<d::Button>("disconnect", text("Disconnect", "解除绑定"), d::Icon::x, d::Button::Kind::secondary, d::Button::Height::compact);
        disconnect.place({0, 0});
        disconnect.onActivate = [this] { confirmDisconnect(); };
        bind([&disconnect](const Presentation& p) { disconnect.setEnabled(!p.busy()); });
        actions.push_back(&disconnect);
    }
    header().configure({"ACCOUNT"}, "MOD-16", title("Account", "账户绑定"), p.isLinked ? "LINKED" : "OFFLINE", false, actions);
    std::vector<std::string> titles;
    for (const auto region : regions) titles.push_back(h::regionChoiceTitle(region, env().language));
    auto& segmented = page.make<d::SegmentedControl>("region", text("Region", "地区"), std::move(titles), regionIndex(p.region), d::metric::boxWidth);
    segmented.onChange = [this](int index) {
        if (index < 0 || std::size_t(index) >= regions.size() || regions[std::size_t(index)] == presentation_.region) return;
        auto a = action(h::AccountAction::Kind::selectRegion); a.region = regions[std::size_t(index)]; act(a);
    };
    bind([&segmented](const Presentation& p) {
        segmented.select(regionIndex(p.region), false);
        for (auto* segment : segmented.segments()) segment->setEnabled(!p.busy());
    });
    add(segmented.layer(), {0, d::metric::headerHeight}, page);
    // The page retains the group: its segments only hold it weakly.
    page.add(segmented); page.add(segmented.segments());
    const double top = d::metric::headerHeight + 30 + 10;
    if (p.isLinked) buildLinked(page, top); else buildOffline(page, top);
    for (const auto& binder : binders_) binder();
}

void AccountCanvas::buildOffline(d::Page& page, double top) {
    const d::Size size{d::metric::boxWidth, 250};
    const double reserved = 16 + 40;
    d::Layer& panel = d::emptyState::make(page.builder(), "account.empty", text("NOT LINKED", "未绑定"), text("Hypergryph account", "鹰角网络通行证"),
        std::nullopt, size, reserved);
    auto& connect = page.make<d::Button>("connect", text("Connect", "绑定账户"), d::Icon::link2, d::Button::Kind::primary, d::Button::Height::large);
    const double y = d::emptyState::textBottom(size, true, reserved) + 16;
    connect.place({std::round((size.width - connect.preferredWidth()) / 2), y});
    connect.onActivate = [this] { auto a = action(h::AccountAction::Kind::connect); a.region = presentation_.region; act(a); };
    bind([&connect](const Presentation& p) { connect.setEnabled(!p.busy()); });
    panel.add(connect.layer());
    add(panel, {0, top}, page);
    page.add(connect);
}

void AccountCanvas::buildLinked(d::Page& page, double top) {
    // Hero: sanity (flex 1) and recovery (flex .8), gap 10.
    const double width = d::metric::boxWidth, sanityWidth = std::round((width - 10) / 1.8);
    auto& readout = page.own<SanityReadout>(env(), sanityWidth, width - 10 - sanityWidth);
    readout.plate().onActivate = [this] { showRecovery(); };
    sanity_ = &readout;
    bind([this, &readout](const Presentation&) { readout.render(sanityPresentation(), wallNow()); });
    add(readout.layer(), {0, top}, page);
    page.add(readout.plate());
    double y = top + readout.layer().bounds().height + 8;
    // Rows: game role, header display, sync with refresh.
    auto& role = dropdown(page, "role", text("Game role", "游戏角色"), 240., [this](const d::Dropdown& anchor) { openRoleMenu(anchor); });
    bind([this, &role](const Presentation& p) {
        const auto found = std::find_if(p.roles.begin(), p.roles.end(), [&](const auto& r) { return p.selectedRoleID && r.id == *p.selectedRoleID; });
        role.setValue(found != p.roles.end() ? found->title : text("Choose account", "选择账户"));
        role.setEnabled(!p.roles.empty() && !p.busy());
    });
    auto& headerMode = dropdown(page, "header", text("Header display", "顶部显示"), std::nullopt, [this](const d::Dropdown& anchor) { openHeaderMenu(anchor); });
    bind([this, &headerMode](const Presentation& p) { headerMode.setValue(h::headerModeTitle(p.headerMode, env().language)); });
    auto& refresh = page.make<d::IconButton>("refresh", text("Refresh", "刷新"), d::Icon::refresh);
    refresh.onActivate = [this] {
        if (presentation_.busy()) return;
        awaitingRefresh_ = true;
        act(action(h::AccountAction::Kind::refresh));
    };
    bind([&refresh](const Presentation& p) { refresh.setEnabled(!p.busy() && !p.needsReconnect); });
    auto& sync = page.make<d::Toggle>("syncProfile", text("Sync personal profile", "同步个人名片"), presentation_.syncProfile);
    sync.onChange = [this](bool on) { auto a = action(h::AccountAction::Kind::setSyncProfile); a.enabled = on; act(a); };
    bind([&sync](const Presentation& p) { sync.setOn(p.syncProfile, true); });
    std::array<d::RowPlate*, 3> rows{&row(page, "role", text("Game role", "游戏角色"), {&role.layer()}),
        &row(page, "header", text("Header display", "顶部显示"), {&headerMode.layer()}),
        &row(page, "sync", text("Sync personal profile", "同步个人名片"), {&refresh.layer(), &sync.layer()},
            presentation_.lastSync.empty() ? std::nullopt : std::optional(presentation_.lastSync))};
    bind([syncRow = rows[2]](const Presentation& p) { if (!p.lastSync.empty()) syncRow->setSublabel(p.lastSync); });
    for (auto* plate : rows) {
        add(plate->layer(), {0, y}, page);
        page.add(*plate);
        y += plate->layer().bounds().height + d::metric::rowGap;
    }
    page.add(std::vector<d::Control*>{&role, &headerMode, &refresh, &sync});
    // Field chips: wrap, gap 6.
    d::Layer& chips = page.makeLayer("account.chips");
    auto chipControls = std::make_shared<std::vector<Chip*>>();
    for (const auto field : ehud::data::profileSyncFieldOrder) {
        auto& chip = page.make<Chip>("field:" + std::string(ehud::data::profileSyncFieldName(field)), h::syncFieldTitle(field, env().language),
            presentation_.syncFields.contains(field));
        chip.onToggle = [this, field](bool on) { auto a = action(h::AccountAction::Kind::setSyncField); a.syncField = field; a.enabled = on; act(a); };
        bind([&chip, field](const Presentation& p) { chip.setOn(p.syncFields.contains(field)); });
        chipControls->push_back(&chip);
    }
    auto& avatar = page.make<Chip>("field:avatar", text("Avatar", "头像"), presentation_.syncAvatar);
    avatar.onToggle = [this](bool on) { auto a = action(h::AccountAction::Kind::setSyncAvatar); a.enabled = on; act(a); };
    bind([&avatar](const Presentation& p) { avatar.setOn(p.syncAvatar); });
    chipControls->push_back(&avatar);
    // A chip's check changes its width: the row wraps again in place.
    const auto layout = [&chips, chipControls, width] {
        double x = 0, line = 0;
        for (Chip* chip : *chipControls) {
            const double w = chip->layer().bounds().width;
            if (x > 0 && x + w > width) { x = 0; line += 24 + 6; }
            chip->layer().setOrigin({x, line});
            if (chip->layer().parent() != &chips) chips.add(chip->layer());
            x += w + 6;
        }
        chips.setBounds({width, line + 24});
    };
    for (Chip* chip : *chipControls) chip->onResize = layout;
    layout();
    add(chips, {0, y - d::metric::rowGap + 8}, page);
    page.add(std::vector<d::Control*>(chipControls->begin(), chipControls->end()));
}

void AccountCanvas::confirmDisconnect() {
    d::ConfirmRequest request;
    request.id = "disconnect"; request.title = text("Disconnect this account?", "解除此账户绑定？");
    request.message = h::regionChoiceTitle(presentation_.region, env().language);
    request.confirmTitle = text("Disconnect", "解除绑定"); request.danger = true; request.icon = d::Icon::x;
    confirm(std::move(request), [this] {
        if (!presentation_.isLinked) return;
        awaitingUnlink_ = true;
        act(action(h::AccountAction::Kind::disconnect));
    });
}

// The sanity plate's press: next point and full recovery (Appendix C).
void AccountCanvas::showRecovery() {
    const auto value = sanityPresentation();
    const auto date = wallNow();
    const std::string next = h::gaugeCountdown(value ? value->nextRecoveryAt : std::nullopt, date, true);
    const std::string full = h::gaugeCountdown(value ? value->fullRecoveryAt : std::nullopt, date, true);
    const std::array<std::string_view, 1> nextArgument{next}, fullArgument{full};
    toast(d::ToastRequest{d::Icon::bolt, text("Next {0}", "下次回复 {0}", nextArgument), text("All recovered {0}", "全部回复 {0}", fullArgument), std::nullopt});
}

void AccountCanvas::openRoleMenu(const d::Dropdown& anchor) {
    const auto& p = presentation_;
    std::vector<d::Menu::Item> items;
    for (const auto& role : p.roles) {
        d::Menu::Item item; item.id = role.id; item.title = role.subtitle.empty() ? role.title : role.title + " / " + role.subtitle;
        item.selected = p.selectedRoleID && role.id == *p.selectedRoleID;
        items.push_back(std::move(item));
    }
    if (items.empty()) return;
    openMenu("role", std::move(items), anchor, std::nullopt, [this](const d::Menu::Item& item) {
        if (presentation_.selectedRoleID && item.id == *presentation_.selectedRoleID) return;
        auto a = action(h::AccountAction::Kind::selectRole); a.roleID = item.id; act(a);
    });
}

void AccountCanvas::openHeaderMenu(const d::Dropdown& anchor) {
    std::vector<d::Menu::Item> items;
    for (const auto mode : headerModes) {
        d::Menu::Item item; item.id = std::string(h::headerModeName(mode)); item.title = h::headerModeTitle(mode, env().language);
        item.selected = mode == presentation_.headerMode;
        items.push_back(std::move(item));
    }
    openMenu("header", std::move(items), anchor, std::nullopt, [this](const d::Menu::Item& item) {
        const auto mode = h::headerModeNamed(item.id);
        if (!mode || *mode == presentation_.headerMode) return;
        auto a = action(h::AccountAction::Kind::selectHeaderMode); a.headerMode = *mode; act(a);
    });
}

// MARK: Building helpers (:321-345)
void AccountCanvas::bind(std::function<void(const Presentation&)> update) {
    binders_.push_back([this, update = std::move(update)] { update(presentation_); });
}
void AccountCanvas::add(d::Layer& layer, d::Point origin, d::Page& page) {
    layer.setOrigin(origin); page.layer().add(layer);
    page.stagger(layer);
}
// A compact row has no room for a sublabel under the micro tag: the sync row
// with its last-sync time keeps the standard height.
d::RowPlate& AccountCanvas::row(d::Page& page, const std::string& id, const std::string& title, std::vector<d::Layer*> cluster,
    std::optional<std::string> sublabel) {
    const auto variant = sublabel ? d::RowPlate::Variant::standard : d::RowPlate::Variant::compact;
    auto& plate = page.make<d::RowPlate>("row:" + id, title, std::move(sublabel), variant);
    plate.setCluster(std::move(cluster));
    return plate;
}
d::Dropdown& AccountCanvas::dropdown(d::Page& page, const std::string& id, const std::string& label, std::optional<double> width,
    std::function<void(const d::Dropdown&)> open) {
    auto& field = page.make<d::Dropdown>(id, label, "", std::nullopt, d::Dropdown::Height::regular, width);
    field.onActivate = [this, &field, open = std::move(open)] { if (menuOpen()) closeMenu(); else open(field); };
    return field;
}
} // namespace endfield::modules
