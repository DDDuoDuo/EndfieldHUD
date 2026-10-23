// Sources/BatteryCanvas.swift, statement by statement (line numbers cite it).
#include "modules/battery_canvas.hpp"
#include "modules/telemetry_format.hpp"
#include <charconv>
#include <cmath>

namespace endfield::modules {
namespace d = design;
namespace {
std::string one(double v) {
    char chars[64];
    const auto r = std::to_chars(chars, chars + sizeof chars, v, std::chars_format::fixed, 1);
    return r.ec == std::errc{} ? std::string(chars, r.ptr) : std::string(telemetry::missing);
}
constexpr std::string_view ideographicSpace = "\xE3\x80\x80"; // U+3000 between the two CJK captions
}

// BatteryReadingPlate (:236-254): the BAR READING stat plate as the link to
// the Battery alert settings; it brightens on hover, eased in and out.
class BatteryCanvas::ReadingPlate final : public d::Control {
public:
    ReadingPlate(d::Builder& b, d::StatPlate& plate) : Control(b.env, "battery.reading", "", plate.layer()), brighten_(&b.layer("battery.reading.brighten")) {
        const d::Rect bounds = d::localBounds(plate.layer());
        brighten_->setFrame(bounds); brighten_->setAnimatable();
        d::setBackground(*brighten_, palette().foreground(.06));
        brighten_->setValue(d::Channel::opacity, 0);
        plate.layer().add(*brighten_);
        setTarget(bounds);
    }
    void hoverChanged() override {
        d::motion::animate(*brighten_, d::Channel::opacity, hovered() && enabled() ? 1 : 0, d::duration::hover, clock(), d::easing::ease);
    }
private:
    d::Layer* brighten_;
};

std::optional<BatteryCanvas::Tone> BatteryCanvas::tone(std::optional<unsigned> level) noexcept {
    const auto value = batteryLevelTone(level);
    if (!value) return std::nullopt;
    return *value == BatteryLevelTone::green ? Tone::good : *value == BatteryLevelTone::yellow ? Tone::moderate : Tone::low;
}
std::string_view BatteryCanvas::tag(Tone t) noexcept { return t == Tone::good ? "GOOD" : t == Tone::moderate ? "MODERATE" : "LOW"; }
d::Color BatteryCanvas::color(Tone t) noexcept { return d::rgb(t == Tone::good ? 0x8CE03C : t == Tone::moderate ? 0xFFFA00 : 0xE5383B); }
d::Cells::Tone BatteryCanvas::cellTone(Tone t) noexcept {
    return {d::rgb(t == Tone::good ? 0xB6F27A : t == Tone::moderate ? 0xFFFC6B : 0xFF6B6D), d::rgb(t == Tone::good ? 0x5FB51E : t == Tone::moderate ? 0xE9CF12 : 0xB8262A)};
}
d::Color BatteryCanvas::pulse(Tone t) noexcept {
    const auto c = color(t);
    constexpr double base = 42. / 255;
    return {c.r * .35 + base * .65, c.g * .35 + base * .65, c.b * .35 + base * .65, 1};
}
std::string BatteryCanvas::condition(const std::optional<std::string>& value, core::Language language) {
    if (!value) return std::string(telemetry::missing);
    const auto& v = *value;
    if (v == "Good") return core::localized("Normal", "正常", language);
    if (v == "Fair") return core::localized("Fair", "一般", language);
    if (v == "Poor") return core::localized("Poor", "较差", language);
    if (v == "Check Battery" || v == "Service Battery" || v == "Service Recommended" || v == "Permanent Battery Failure")
        return core::localized("Service Recommended", "建议维修", language);
    return v;
}

BatteryCanvas::BatteryCanvas(d::Context& context) : Canvas(core::Module::power, context) {}

// update(snapshot:configuration:) (:64-71).
void BatteryCanvas::update(BatteryReading reading, ChargeMetric metric, double now) {
    if (reading == reading_ && metric == metric_) return;
    reading_ = std::move(reading); metric_ = metric;
    if (!renderedLanguage()) return;
    env().now = now;
    if (renderedLanguage() != env().language) rerender(); else apply(true);
}
int BatteryCanvas::liveAnimationCount() const noexcept { return live_ ? live_->cells->pulseCount(now()) : 0; }

// MARK: Page (:82-125)
void BatteryCanvas::buildPage(d::Page& page) {
    header().configure({"BATTERY"}, "PWR-00", title("Battery", "电池状态"), "LIVE", false);
    const auto& p = env().palette;
    const double width = d::metric::boxWidth;
    d::Layer& top = page.makeLayer("battery.top");
    d::Layer& source = d::makeText(page.builder(), "battery.source", "// " + text("Power source", "电源"), d::type::label(p), {0, 0, 300, 10});
    auto& numeral = page.own<d::BigNumeral>("battery.numeral", 96);
    d::Layer& status = page.makeLayer("battery.status");
    d::Layer& caption = d::makeText(page.builder(), "battery.caption", "", d::type::labelCJK(p), {});
    d::Layer& timeLabel = d::makeText(page.builder(), "battery.timeLabel", "", d::type::label(p), {width - 200, 18, 200, 10}, d::TextAlignment::right);
    d::Layer& time = d::makeText(page.builder(), "battery.time", "", d::type::numeral(p).sized(30), {width - 200, 34, 200, 27}, d::TextAlignment::right);
    auto& cells = page.own<d::Cells>("battery.cells", 20, width, true, 22);
    auto frames = d::StatPlate::gridFrames(6, width);
    for (auto& f : frames) f.y += 231;
    Live live;
    for (std::size_t index = 0; index < frames.size(); ++index) {
        live.plates[index] = &page.own<d::StatPlate>("battery.plate." + std::to_string(index), frames[index].width);
        live.plates[index]->setValueMinimumScale(.7);
    }
    auto& reading = page.make<ReadingPlate>(*live.plates[5]);
    reading.onActivate = [this] { if (onOpenBatteryAlert) onOpenBatteryAlert(); };
    d::Layer& heroFrame = page.makeLayer("battery.level");
    heroFrame.setFrame({0, 81, width, 138});
    live.hero = &page.builder().controls.make<d::Readout>(env(), "battery.level", text("Battery", "电池"), "", heroFrame);
    std::array<d::Layer*, 5> readoutFrames{};
    for (std::size_t index = 0; index < live.readouts.size(); ++index) {
        readoutFrames[index] = &page.makeLayer("battery.plate." + std::to_string(index) + ".readout");
        readoutFrames[index]->setFrame(frames[index]);
        live.readouts[index] = &page.builder().controls.make<d::Readout>(env(), "battery.plate." + std::to_string(index), "", "", *readoutFrames[index]);
    }
    top.setFrame({0, 81, width, 138});
    // Pinned at its top-left: the digits change the numeral's width.
    numeral.layer().setAnchorPoint({0, 0}); numeral.layer().setPosition({0, 16});
    cells.layer().setOrigin({0, 116});
    for (d::Layer* child : {&source, &numeral.layer(), &status, &caption, &timeLabel, &time, &cells.layer()}) top.add(*child);
    page.layer().add(top);
    for (std::size_t index = 0; index < frames.size(); ++index) {
        live.plates[index]->layer().setFrame(frames[index]);
        page.layer().add(live.plates[index]->layer());
    }
    page.layer().add(heroFrame);
    for (d::Layer* frame : readoutFrames) page.layer().add(*frame);
    page.add(*live.hero);
    for (auto* readout : live.readouts) page.add(*readout);
    page.add(reading);
    page.stagger(top);
    for (auto* plate : live.plates) page.stagger(plate->layer());
    live.numeral = &numeral; live.status = &status; live.caption = &caption; live.timeLabel = &timeLabel; live.time = &time;
    live.cells = &cells; live.reading = &reading;
    live_ = std::move(live);
    livePage_ = &page;
    cellState_.reset();
    apply(false);
}

// currentTone (:127).
std::optional<BatteryCanvas::Tone> BatteryCanvas::currentTone() const noexcept { return reading_.present ? tone(reading_.percentage) : std::nullopt; }

// The status chips are made once per page and reattached by state: the Mac
// makes new HUDChip layers on every reading (same pixels; a page arena here
// would otherwise grow with every power event).
d::Chip& BatteryCanvas::chip(std::string_view tag, d::Chip::Variant variant, std::optional<d::Icon> icon,
    std::optional<std::pair<d::Color, d::Color>> custom) {
    auto found = live_->chips.find(tag);
    if (found == live_->chips.end())
        found = live_->chips.emplace(std::string(tag), &livePage_->own<d::Chip>("battery.chip." + std::string(tag), std::string(tag), variant, icon, custom)).first;
    return *found->second;
}

// apply(animated:) (:130-161).
void BatteryCanvas::apply(bool animated) {
    if (!live_) return;
    auto& live = *live_;
    const auto& s = reading_;
    const auto& p = env().palette;
    const auto t = currentTone();
    const auto level = s.present && s.percentage && *s.percentage <= 100 ? s.percentage : std::nullopt;
    live.numeral->set(level ? std::to_string(*level) : std::string(telemetry::missing), level ? "%" : "",
        t ? color(*t) : p.yellow(), t ? color(*t) : p.token(d::ThemeToken::steel));

    // Status column: the tone chip, the mode chip, the caption line.
    const bool charge = batteryChargeMode(s);
    std::vector<d::Chip*> chips;
    if (t) chips.push_back(&chip(tag(*t), d::Chip::Variant::custom, std::nullopt, std::pair{color(*t), d::fixed::nearBlack}));
    else if (!s.present) chips.push_back(&chip("NO BATTERY", d::Chip::Variant::outline));
    chips.push_back(charge ? &chip("CHARGE MODE", d::Chip::Variant::yellow, d::Icon::bolt) : &chip("POWER MODE", d::Chip::Variant::outline));
    const auto language = env().language;
    const auto mode = charge ? telemetry::caption("CHARGE MODE", "超充模式", language) : telemetry::caption("POWER MODE", "电源模式", language);
    std::optional<std::string> lead;
    if (t) lead = *t == Tone::good ? telemetry::caption("Charge good", "电量充足", language)
        : *t == Tone::moderate ? telemetry::caption("Charge moderate", "电量中等", language) : telemetry::caption("Charge low", "电量不足", language);
    else if (!s.present) lead = telemetry::caption("No battery", "未检测到电池", language);
    const double x = std::ceil(live.numeral->layer().bounds().width) + 24;
    for (auto& [_, made] : live.chips) made->layer().removeFromParent();
    double chipX = 0;
    for (auto* made : chips) {
        made->layer().setOrigin({chipX, 0});
        live.status->add(made->layer());
        chipX += made->layer().bounds().width + 4;
    }
    live.status->setFrame({x, 53, chipX, 18});
    std::string captionText;
    const std::array<const std::string*, 2> parts{lead ? &*lead : nullptr, &mode};
    for (const auto* part : parts) {
        if (!part || part->empty()) continue;
        if (!captionText.empty()) captionText += ideographicSpace;
        captionText += *part;
    }
    d::editText(*live.caption, [&](d::TextBlock& block) { block.setText(captionText); block.language.reset(); });
    d::setLine(*live.caption, {x, 79, d::metric::boxWidth - x - 160, 11.5});
    const auto timeLabel = charge ? text("Until full", "充满还需") : text("Remaining", "剩余时间");
    const auto time = timeText();
    d::editText(*live.timeLabel, [&](d::TextBlock& block) { block.setText(timeLabel); });
    d::editText(*live.time, [&](d::TextBlock& block) { block.setText(time); });

    applyCells(animated);
    applyPlates();
    std::string hero = level ? std::to_string(*level) + "%" : std::string(telemetry::missing);
    if (t) hero += ", " + std::string(tag(*t));
    hero += charge ? ", CHARGE MODE" : ", POWER MODE";
    hero += ", " + timeLabel + " " + time;
    live.hero->value = hero;
}

// timeText (:165-178): UNTIL FULL on external power (0:00 once full),
// REMAINING on battery; "—" while the OS is still estimating.
std::string BatteryCanvas::timeText() const {
    const auto& s = reading_;
    if (!s.present) return std::string(telemetry::missing);
    if (batteryChargeMode(s)) {
        if (s.fullyCharged) return telemetry::duration(0);
        return s.charging ? telemetry::duration(s.minutesToFull) : std::string(telemetry::missing);
    }
    return telemetry::duration(s.minutesToEmpty);
}

// applyCells (:180-189): the charging pulse only while Battery is active.
void BatteryCanvas::applyCells(bool animated, bool pulseAllowed) {
    if (!live_) return;
    const auto t = currentTone();
    const unsigned level = reading_.present ? reading_.percentage.value_or(0) : 0;
    const CellState state{t ? int(std::round(double(level) / 5)) : 0, t, pulseAllowed && isActive() && reading_.present && reading_.charging};
    if (cellState_ == state) return;
    cellState_ = state;
    live_->cells->set(state.filled, t ? std::optional(cellTone(*t)) : std::nullopt, state.pulsing, animated,
        t ? std::optional(pulse(*t)) : std::nullopt, 3);
}

// applyPlates (:191-221).
void BatteryCanvas::applyPlates() {
    if (!live_) return;
    auto& live = *live_;
    const auto& s = reading_;
    const std::array<std::string, 6> labels{text("Source", "电源"), text("Capacity", "容量"), text("Cycles", "循环次数"),
        text("Condition", "状态"), text("Temperature", "温度"), text("Bar reading", "电池条读数")};
    std::array<std::pair<std::string, std::optional<std::string>>, 6> values;
    const bool external = !s.present || s.pluggedIn;
    values[0] = {external ? text("External", "外接电源") : text("Battery", "电池"),
        s.adapterWatts ? std::optional(std::to_string(*s.adapterWatts) + " W") : std::nullopt};
    // The Mac's absolute pair is mAh; Windows' battery class driver reports
    // mWh, shown the same way with its own unit. A percent pair reads "NN%".
    if (s.capacity) values[1] = s.capacity->unit == "%" ? std::pair{std::to_string(s.capacity->current) + "%", std::optional<std::string>{}}
        : std::pair{d::format::grouped(s.capacity->current), std::optional("/ " + d::format::grouped(s.capacity->maximum) + " " + s.capacity->unit)};
    else values[1] = {std::string(telemetry::missing), std::nullopt};
    values[2] = {s.cycleCount ? d::format::grouped(*s.cycleCount) : std::string(telemetry::missing), std::nullopt};
    values[3] = {condition(s.health, env().language), std::nullopt};
    values[4] = {s.temperatureCelsius ? one(*s.temperatureCelsius) : std::string(telemetry::missing),
        s.temperatureCelsius ? std::optional<std::string>("°C") : std::nullopt};
    values[5] = {chargeMetricTitle(metric_, env().language), std::nullopt};
    const bool hot = s.temperatureCelsius.value_or(0) > 80;
    for (std::size_t index = 0; index < live.plates.size(); ++index) {
        live.plates[index]->set(labels[index], values[index].first, values[index].second, index == 5,
            index == 4 && hot ? std::optional(d::fixed::orange) : std::nullopt);
        if (index < live.readouts.size()) {
            live.readouts[index]->label = labels[index];
            live.readouts[index]->value = values[index].first + (values[index].second ? " " + *values[index].second : std::string{});
        }
    }
    live.reading->label = text("Battery alert", "电池提醒") + ", " + labels[5] + " " + chargeMetricTitle(metric_, env().language);
}
} // namespace endfield::modules
