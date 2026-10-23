// The daily boot log: the port of Sources/HUDBootLog.swift (:10-308) and the
// streak rule of SystemHUDView.bootLogLines (:3060-3097).
#include "modules/boot_log.hpp"
#include "modules/hud_design_format.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>

namespace endfield::modules::boot {
namespace {
using d::Channel;
// HUDBootLog.Style (:114-117).
d::TextStyle headerStyle() { return {11, d::FontWeight::bold, d::rgb(0xE8E8E8), 1, .16}; }
d::TextStyle lineStyle() { return {12.5, d::FontWeight::medium, BootLogLine::text}; }
// CharacterSet.newlines (LF, VT, FF, CR, NEL, LS, PS): the byte length of a
// newline starting at `at`, 0 otherwise.
std::size_t newlineLength(std::string_view s, std::size_t at) noexcept {
    const auto c = static_cast<unsigned char>(s[at]);
    if (c >= 0x0A && c <= 0x0D) return 1;
    if (c == 0xC2 && at + 1 < s.size() && static_cast<unsigned char>(s[at + 1]) == 0x85) return 2;
    if (c == 0xE2 && at + 2 < s.size() && static_cast<unsigned char>(s[at + 1]) == 0x80) {
        const auto e = static_cast<unsigned char>(s[at + 2]);
        if (e == 0xA8 || e == 0xA9) return 3;
    }
    return 0;
}
// CharacterSet.whitespaces (tab + Unicode Zs): the byte length of one at `at`, 0 otherwise.
std::size_t whitespaceLength(std::string_view s, std::size_t at) noexcept {
    const auto byte = [&](std::size_t i) { return i < s.size() ? static_cast<unsigned char>(s[i]) : 0u; };
    const auto c = byte(at);
    if (c == 0x20 || c == 0x09) return 1;
    if (c == 0xC2 && byte(at + 1) == 0xA0) return 2;                                      // U+00A0
    if (c == 0xE1 && byte(at + 1) == 0x9A && byte(at + 2) == 0x80) return 3;               // U+1680
    if (c == 0xE2 && byte(at + 1) == 0x80 && (byte(at + 2) <= 0x8A || byte(at + 2) == 0xAF)) return 3; // U+2000-200A, U+202F
    if (c == 0xE2 && byte(at + 1) == 0x81 && byte(at + 2) == 0x9F) return 3;               // U+205F
    if (c == 0xE3 && byte(at + 1) == 0x80 && byte(at + 2) == 0x80) return 3;               // U+3000
    return 0;
}
// The last whitespace scalar ending at `end`, 0 otherwise.
std::size_t trailingWhitespace(std::string_view s, std::size_t end, bool newlines) noexcept {
    for (std::size_t n = 1; n <= 3 && n <= end; ++n) {
        const auto start = end - n;
        if (const auto l = whitespaceLength(s, start); l == n) return n;
        if (newlines) if (const auto l = newlineLength(s, start); l == n) return n;
    }
    return 0;
}
// trimmingCharacters(in: .whitespaces / .whitespacesAndNewlines).
std::string trimmed(std::string_view s, bool newlines) {
    std::size_t begin = 0, end = s.size();
    while (begin < end) {
        auto n = whitespaceLength(s, begin);
        if (!n && newlines) n = newlineLength(s, begin);
        if (!n) break;
        begin += n;
    }
    while (end > begin) {
        const auto n = trailingWhitespace(s, end, newlines);
        if (!n) break;
        end -= n;
    }
    return std::string(s.substr(begin, end - begin));
}
// components(separatedBy: .newlines).joined(separator: " ").
std::string joinedLines(std::string_view s) {
    std::string out;
    for (std::size_t at = 0; at < s.size();) {
        if (const auto n = newlineLength(s, at)) { out += ' '; at += n; }
        else out += s[at++];
    }
    return out;
}
std::string plural(int count, std::string_view word) { return std::to_string(count) + " " + std::string(word) + (count == 1 ? "" : "S"); }
}

// ---------------------------------------------------------------------------
// HUDBootLog.Line (:17-92)
// ---------------------------------------------------------------------------
d::Color BootLogLine::Tone::resolve(const d::Palette& p) const noexcept {
    switch (kind) {
    case Tone::Kind::value: return p.yellow();
    case Tone::Kind::good: return d::fixed::green;
    case Tone::Kind::warning: return d::fixed::orange;
    case Tone::Kind::color: break;
    }
    return color;
}
BootLogLine BootLogLine::header(std::string title, std::string stamp) { return {Kind::header, std::move(title), std::move(stamp), {}, {}}; }
BootLogLine BootLogLine::entry(std::string key, std::string value, Tone tone, std::optional<std::string> trailing) {
    return {Kind::entry, std::move(key), std::move(value), tone, std::move(trailing)};
}
BootLogLine BootLogLine::entry(std::string key, std::string value) { return entry(std::move(key), std::move(value), Tone{}); }
BootLogLine BootLogLine::sub(std::string text) { return {Kind::sub, std::move(text), {}, {}, {}}; }
BootLogLine BootLogLine::footer(std::string status, std::string message) { return {Kind::footer, std::move(status), std::move(message), {Tone::Kind::good, {}}, {}}; }
std::string BootLogLine::dotLeader(std::string_view key) {
    const auto dots = std::max<std::ptrdiff_t>(2, 22 - std::ptrdiff_t(key.size()));
    return std::string(key) + " " + std::string(std::size_t(dots), '.') + " ";
}
std::vector<BootLogLine::Segment> BootLogLine::segments(const d::Palette& p) const {
    const Segment prompt{">", steel};
    std::vector<Segment> parts;
    switch (kind) {
    case Kind::header: parts = {prompt, {" " + key + "  ", text}, {value, tone.resolve(p)}}; break;
    case Kind::entry:
        parts = {prompt, {" " + dotLeader(key), text}, {value, tone.resolve(p)}};
        if (trailing) parts.push_back({"  " + *trailing, text});
        break;
    case Kind::sub: parts = {{key, subText}}; break;
    case Kind::footer: parts = {{key, tone.resolve(p)}, {" " + value, text}}; break;
    }
    std::erase_if(parts, [](const Segment& s) { return s.text.empty(); });
    return parts;
}

// ---------------------------------------------------------------------------
// HUDBootLogContent (:274-308): English in every language (a terminal).
// ---------------------------------------------------------------------------
std::vector<BootLogLine> bootLogLines(const BootLogDate& date, double uptime, const std::optional<BootLogBattery>& battery,
    std::span<const std::string> eventsToday, const std::optional<BootLogSanity>& sanity, const std::optional<BootLogWorkStreak>& work,
    const std::optional<BootLogDisk>& disk, std::string_view operatorName, const d::TextCasing& uppercase) {
    namespace f = d::format;
    std::vector<BootLogLine> lines{BootLogLine::header("BOOT SEQUENCE", f::dotted(date.year, date.month, date.day, std::pair{date.hour, date.minute}))};
    const std::int64_t seconds = std::isfinite(uptime) ? std::max<std::int64_t>(0, std::int64_t(uptime)) : 0;
    lines.push_back(BootLogLine::entry("UPTIME", std::to_string(seconds / 86'400) + "D " + f::pad2(seconds % 86'400 / 3600) + "H " + f::pad2(seconds % 3600 / 60) + "M"));
    if (battery) {
        const auto tone = battery->level == BootLogBattery::Level::green ? BootLogLine::Tone::of(d::fixed::green)
            : battery->level == BootLogBattery::Level::red ? BootLogLine::Tone::of(d::fixed::red) : BootLogLine::Tone{};
        lines.push_back(BootLogLine::entry("POWER", std::to_string(battery->percent) + "%", tone, battery->charging ? "CHARGE MODE" : "POWER MODE"));
    }
    const int count = int(eventsToday.size());
    lines.push_back(BootLogLine::entry("CALENDAR", plural(count, "EVENT") + " TODAY"));
    for (std::size_t i = 0; i < eventsToday.size() && i < 2; ++i) {
        auto text = trimmed(joinedLines(eventsToday[i]), false);
        if (!text.empty()) lines.push_back(BootLogLine::sub(std::move(text)));
    }
    if (sanity) {
        std::optional<std::string> next;
        if (sanity->nextIn && std::isfinite(*sanity->nextIn)) next = "NEXT +1 IN " + f::clock(std::int64_t(std::ceil(std::max(0.0, *sanity->nextIn))));
        lines.push_back(BootLogLine::entry("SANITY", std::to_string(sanity->current) + " / " + std::to_string(sanity->maximum), {}, next));
    }
    if (work) {
        char hours[48]{};
        std::snprintf(hours, sizeof hours, "%.1f H TOTAL", work->totalHours);
        lines.push_back(BootLogLine::entry("WORK MODE STREAK", plural(work->days, "DAY"), {}, std::string(hours)));
    }
    if (disk) {
        lines.push_back(disk->low ? BootLogLine::entry("FACILITY", "DISK " + std::to_string(disk->freePercent) + "% FREE", {BootLogLine::Tone::Kind::warning, {}})
                                  : BootLogLine::entry("FACILITY", "NOMINAL", {BootLogLine::Tone::Kind::good, {}}));
    }
    const auto name = trimmed(operatorName, true);
    const auto greeting = uppercase ? uppercase(name.empty() ? "ENDMINISTRATOR" : name) : d::asciiUppercase(name.empty() ? "ENDMINISTRATOR" : name);
    lines.push_back(BootLogLine::footer("ALL SYSTEMS ONLINE.", "WELCOME BACK, " + greeting + "."));
    return lines;
}
int bootLogWorkStreak(std::span<const std::int64_t> days, std::int64_t today) {
    const std::set<std::int64_t> workDays(days.begin(), days.end());
    auto day = today;
    if (!workDays.contains(day)) day -= 1;
    int streak = 0;
    while (workDays.contains(day)) { ++streak; --day; }
    return streak;
}
std::optional<BootLogWorkStreak> bootLogWork(int streak, double trackedWorkSeconds) {
    const double hours = trackedWorkSeconds / 3600;
    if (!(streak > 0 || hours > 0)) return std::nullopt;
    return BootLogWorkStreak{streak, hours};
}
// days_from_civil (H. Hinnant's public-domain algorithm).
std::int64_t bootLogDayNumber(int year, int month, int day) noexcept {
    const std::int64_t y = std::int64_t(year) - (month <= 2);
    const std::int64_t era = (y >= 0 ? y : y - 399) / 400;
    const std::int64_t yoe = y - era * 400;
    const std::int64_t doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const std::int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}
std::string bootLogDay(const BootLogDate& date) {
    char out[32]{};
    std::snprintf(out, sizeof out, "%04d-%02d-%02d", date.year, date.month, date.day);
    return out;
}

// ---------------------------------------------------------------------------
// HUDBootLog (:94-270)
// ---------------------------------------------------------------------------
BootLog::BootLog(d::Environment& env) : env_(&env), arena_("boot", &env.deadlines), builder_{env, arena_, controls_} {
    const d::Rect bounds{0, 0, size.width, size.height};
    const auto ring = d::rgb(0x2E2E2E);
    const double w = size.width, h = size.height;
    layer_ = &builder_.layer("log");
    term_ = &builder_.layer("term");
    layer_->setAnimatable();                             // shown and hidden as a pose
    layer_->setBounds(size); layer_->setZPosition(d::order::bootLog); layer_->setHidden(true);
    term_->setAnimatable();                              // bandIn (centre-out wipe), bootOut (wipe up + fade)
    term_->setFrame(bounds);
    auto& plate = builder_.layer("plate");
    plate.setFrame(bounds); d::setPath(plate, d::chamfer(bounds, 16), d::rgb(0x0A0A0A, .94));
    // inset 4px 0 0 yellow under inset 0 0 0 1px #2e2e2e (the ring stops at the chamfer).
    bar_ = &builder_.layer("bar");
    bar_->setFrame({0, 0, 4, h});
    auto& edges = builder_.layer("ring");
    d::PathBuilder path;
    path.rect({0, 0, w - 16, 1}).rect({w - 1, 16, 1, h - 16}).rect({0, h - 1, w, 1}).rect({0, 0, 1, h});
    edges.setFrame(bounds); d::setPath(edges, path, ring);
    tri_ = &builder_.layer("tri");
    tri_->setFrame({24, 20, 4, 16});
    auto& rule = builder_.layer("rule");
    rule.setFrame({24, 48, w - 48, 1}); d::setBackground(rule, ring);
    title_ = &d::makeText(builder_, "title", "BOOT LOG", headerStyle(), {38, 22.5, 0, 11});
    terminal_ = &d::makeText(builder_, "terminal", "", headerStyle().colored(BootLogLine::steel), {}, d::TextAlignment::right);
    cursor_ = &builder_.layer("cursor");
    cursor_->setAnimatable();                            // blink and its line
    cursor_->setFrame({24, firstLineTop + 3.5, 9, 15});
    for (d::Layer* child : {&plate, bar_, &edges, tri_, title_, terminal_, &rule}) term_->add(*child);
    for (int i = 0; i < capacity; ++i) {
        auto& row = builder_.layer("row." + std::to_string(i));
        row.setAnimatable();                             // fade .16 ease, slide .2 E
        row.setHidden(true);
        term_->add(row);                                 // below the cursor
        rows_.push_back(&row);
    }
    term_->add(*cursor_);
    layer_->add(*term_);
}
BootLog::~BootLog() { cancel(pending_); cancel(blink_); }
void BootLog::cancel(d::Deadlines::Token& token) noexcept { if (token) env_->deadlines.cancel(token); token = 0; }

// play (:146-174).
void BootLog::play(std::vector<BootLogLine> lines, std::string_view version) {
    reset();
    playing_ = true;
    const auto& palette = env_->palette;
    shown_ = int(std::min<std::size_t>(lines.size(), std::size_t(capacity)));
    // The accent follows the theme colour as of this boot.
    d::setBackground(*bar_, palette.yellow()); d::setBackground(*cursor_, palette.yellow());
    tri_->setContent(d::triStrip(tri_->id(), d::localBounds(*tri_)));
    const double titleWidth = std::ceil(d::preferredWidth(d::textOf(*title_), env_->text));
    d::setLine(*title_, {38, 22.5, titleWidth, 11});
    const double x = 38 + titleWidth + 10;
    d::editText(*terminal_, [&](d::TextBlock& b) { b.setText("ENDFIELD INDUSTRIES TERMINAL v" + std::string(version)); b.setLineFrame({x, 22.5, size.width - 24 - x, 11}); });
    for (int i = 0; i < shown_; ++i) {
        auto& row = *rows_[std::size_t(i)];
        const auto& line = lines[std::size_t(i)];
        const double rx = line.kind == BootLogLine::Kind::sub ? 42 : 24;
        d::TextBlock block;
        block.runs = runs(line, palette, env_->characters);
        block.setLineFrame({rx, slotTop(i) + (lineHeight - 12.5) / 2, size.width - 24 - rx, 12.5});
        row.setText(std::move(block));
        row.setValue(Channel::opacity, 0);
        row.setValue(Channel::translateX, -6);
        row.setHidden(false);
    }
    moveCursor(0);
    layer_->setHidden(false);
    const auto clock = env_->clock();
    d::motion::wipe(*term_, d::WipeOrigin::centerX, d::duration::bandIn, clock, d::easing::snap);
    startBlink();
    pending_ = env_->deadlines.token();
    env_->deadlines.after(clock.now + (shown_ ? .28 : .9), pending_, [this] { step(); });
}
// step (:179-190).
void BootLog::step() {
    pending_ = 0;
    if (!playing_ || finishing_) return;
    if (revealed_ >= shown_) { finish(); return; }
    auto& row = *rows_[std::size_t(revealed_)];
    const auto clock = env_->clock();
    d::motion::animate(row, Channel::opacity, 1, .16, clock, d::easing::ease);
    d::motion::animate(row, Channel::translateX, 0, .2, clock);
    ++revealed_;
    moveCursor(revealed_);
    const bool last = revealed_ == shown_;
    pending_ = env_->deadlines.token();
    env_->deadlines.after(clock.now + (last ? .9 : revealed_ == 1 ? .26 : .19), pending_, [this] { step(); });
}
// finish (:193-213): bootOut .38 s E, the clip rises from the bottom while fading to .2.
void BootLog::finish() {
    if (!playing_ || finishing_) return;
    finishing_ = true;
    cancel(pending_);
    d::motion::removeWipe(*term_);
    const auto clock = env_->clock();
    if (clock.reduceMotion) { complete(); return; }
    term_->wipeOrigin = d::WipeOrigin::top;
    d::motion::animate(*term_, Channel::wipe, 0, .38, clock);
    d::motion::animate(*term_, Channel::opacity, .2, .38, clock);
    pending_ = env_->deadlines.token();
    env_->deadlines.after(clock.now + .38, pending_, [this] { pending_ = 0; complete(); });
}
void BootLog::complete() {
    reset();
    if (onFinish) onFinish();
}
// reset (:221-232): back to the idle, hidden panel with no loops.
void BootLog::reset() {
    cancel(pending_); cancel(blink_);
    d::motion::removeWipe(*term_);
    term_->removeAllMotion(); cursor_->removeAllMotion();
    term_->setValue(Channel::wipe, 1); term_->setValue(Channel::opacity, 1);
    cursor_->setValue(Channel::opacity, 1);
    for (auto* row : rows_) { row->removeAllMotion(); row->setHidden(true); }
    layer_->setHidden(true);
    shown_ = 0; revealed_ = 0;
    playing_ = false; finishing_ = false;
}
// startBlink (:235-241): 1 s steps(2): 1, .6, .2, .6 (one value per 250 ms).
void BootLog::startBlink() {
    if (env_->reduceMotion) return;
    blinkStart_ = env_->now;
    blinkPhase_ = 0;
    cursor_->setValue(Channel::opacity, 1);
    blink_ = env_->deadlines.token();
    env_->deadlines.after(blinkStart_ + .25, blink_, [this] { blinkStep(); });
}
// The step due now (a late deadline catches up to the clock's phase, as the
// render server samples the discrete keyframes by time).
void BootLog::blinkStep() {
    static constexpr std::array<double, 4> values{1, .6, .2, .6};
    const auto step = std::int64_t(std::floor((env_->now - blinkStart_) / .25 + 1e-9));
    blinkPhase_ = int(step % std::int64_t(values.size()));
    cursor_->setValue(Channel::opacity, values[std::size_t(blinkPhase_)]);
    env_->deadlines.after(blinkStart_ + double(step + 1) * .25, blink_, [this] { blinkStep(); });
}
// The cursor sits on its own line below the last revealed one.
void BootLog::moveCursor(int slot) { cursor_->setOrigin({24, slotTop(slot) + (lineHeight - 15) / 2}); }
// runs (:252-261).
std::vector<d::TextRun> BootLog::runs(const BootLogLine& line, const d::Palette& palette, const d::CharacterRules& characters) {
    const auto segments = line.segments(palette);
    const auto style = lineStyle();
    const double gap = style.tracking + 8 / style.size;
    std::vector<d::TextRun> out;
    for (std::size_t i = 0; i < segments.size(); ++i) {
        const auto colored = style.colored(segments[i].color);
        const auto& text = segments[i].text;
        if (i + 1 == segments.size() || text.empty()) { out.push_back({text, colored}); continue; }
        const auto count = characters.count(text);
        auto head = characters.prefix(text, count - 1);
        auto spaced = colored; spaced.tracking = gap;
        out.push_back({std::move(head), colored});
        out.push_back({text.substr(out.back().text.size()), spaced});
    }
    std::erase_if(out, [](const d::TextRun& r) { return r.text.empty(); });
    return out;
}
d::Rect BootLog::cursorFrame() const { return cursor_->frame(); }
std::vector<std::string> BootLog::rowTexts() const {
    std::vector<std::string> out;
    for (int i = 0; i < shown_; ++i) out.push_back(d::textOf(*rows_[std::size_t(i)]).text());
    return out;
}
std::string BootLog::terminalText() const { return d::textOf(*terminal_).text(); }
} // namespace endfield::modules::boot
