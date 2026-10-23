// Daily boot log contracts (Sources/HUDBootLog.swift, SystemHUDView
// .bootLogLines, OverlayController.consumeDailyBootLog; SHELL-24/25/69/78):
// the content lines (English in every language, every optional source left
// out when unavailable, the tones, the dot leader, the 8 px flex gap as
// tracking), the Work Mode streak rule, the panel geometry and capacity, the
// reveal timing (280 / 260 / 190 ms, 900 ms tail), the cursor and its
// discrete blink, skip, the bootOut wipe, Reduce Motion, and the day key.
#include "modules/boot_log.hpp"
#include "core/data/data_store.hpp"
#include "core/module_presentation.hpp"
#include <cmath>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace {
namespace b = endfield::modules::boot;
namespace d = endfield::modules::design;
using d::Channel;
using d::Rect;
unsigned checks{};
void check(bool v, const std::string& what) { ++checks; if (!v) throw std::runtime_error(what); }
bool near(double a, double b, double e = 1e-6) { return std::abs(a - b) <= e; }
std::string str(Rect r) { std::ostringstream s; s << "(" << r.x << ", " << r.y << ", " << r.width << ", " << r.height << ")"; return s.str(); }
void same(Rect a, Rect e, const std::string& what) { check(near(a.x, e.x) && near(a.y, e.y) && near(a.width, e.width) && near(a.height, e.height), what + ": " + str(a) + " vs " + str(e)); }
std::string flat(const b::BootLogLine& l, const d::Palette& p) { std::string s; for (const auto& seg : l.segments(p)) s += seg.text; return s; }

void content() {
    d::Palette palette;
    const b::BootLogDate date{2026, 10, 19, 6, 45};
    const std::vector<std::string> events{"Stand-up\nweekly", "  ", "Third"};
    const auto lines = b::bootLogLines(date, 3 * 86'400 + 4 * 3600 + 5 * 60 + 59, b::BootLogBattery{82, true, b::BootLogBattery::Level::green},
        events, b::BootLogSanity{180, 240, 271.2}, b::BootLogWorkStreak{3, 12.34}, b::BootLogDisk{8, true}, "  Perlica \n", d::asciiUppercase);
    std::vector<std::string> texts;
    for (const auto& l : lines) texts.push_back(flat(l, palette));
    const std::vector<std::string> expected{
        "> BOOT SEQUENCE  2026.10.19 06:45",
        "> UPTIME ................ 3D 04H 05M",
        "> POWER ................. 82%  CHARGE MODE",
        "> CALENDAR .............. 3 EVENTS TODAY",
        "Stand-up weekly",
        "> SANITY ................ 180 / 240  NEXT +1 IN 04:32",
        "> WORK MODE STREAK ...... 3 DAYS  12.3 H TOTAL",
        "> FACILITY .............. DISK 8% FREE",
        "ALL SYSTEMS ONLINE. WELCOME BACK, PERLICA."};
    check(texts.size() == expected.size(), "line count " + std::to_string(texts.size()));
    for (std::size_t i = 0; i < expected.size(); ++i) check(texts[i] == expected[i], "line " + std::to_string(i) + ": [" + texts[i] + "]");
    // Tones: battery green, facility warning orange, footer status green, values yellow.
    check(lines[2].segments(palette)[2].color == d::fixed::green, "battery tone");
    check(lines[7].segments(palette)[2].color == d::fixed::orange, "low disk is a warning");
    check(lines[8].segments(palette)[0].color == d::fixed::green && lines[8].segments(palette)[1].color == b::BootLogLine::text, "footer colours");
    check(lines[1].segments(palette)[0].color == b::BootLogLine::steel && lines[1].segments(palette)[2].color == palette.yellow(), "prompt steel, value yellow");
    check(lines[4].segments(palette)[0].color == b::BootLogLine::subText, "sub lines grey");
    // Sparse data: no battery, sanity, streak or disk; singular words; the default greeting.
    const auto sparse = b::bootLogLines(date, -5, std::nullopt, std::vector<std::string>{"One"}, std::nullopt, std::nullopt, std::nullopt, " ", d::asciiUppercase);
    check(sparse.size() == 5, "unavailable sources are left out");
    check(flat(sparse[1], palette) == "> UPTIME ................ 0D 00H 00M", "a negative uptime counts as 0");
    check(flat(sparse[2], palette) == "> CALENDAR .............. 1 EVENT TODAY", "one event, singular");
    check(flat(sparse[4], palette) == "ALL SYSTEMS ONLINE. WELCOME BACK, ENDMINISTRATOR.", "default greeting");
    const auto nominal = b::bootLogLines(date, 60, b::BootLogBattery{15, false, b::BootLogBattery::Level::red}, {}, b::BootLogSanity{1, 2, std::nullopt},
        b::BootLogWorkStreak{1, 0}, b::BootLogDisk{40, false}, "x", d::asciiUppercase);
    check(flat(nominal[2], palette) == "> POWER ................. 15%  POWER MODE" && nominal[2].segments(palette)[2].color == d::fixed::red, "battery red tone, POWER MODE");
    check(flat(nominal[4], palette) == "> SANITY ................ 1 / 2", "no next time without a recovery");
    check(flat(nominal[5], palette) == "> WORK MODE STREAK ...... 1 DAY  0.0 H TOTAL", "one day, singular");
    check(flat(nominal[6], palette) == "> FACILITY .............. NOMINAL" && nominal[6].segments(palette)[2].color == d::fixed::green, "nominal facility is good");
    // A long key keeps two dots.
    check(b::BootLogLine::dotLeader("ABCDEFGHIJKLMNOPQRSTUVWXYZ") == "ABCDEFGHIJKLMNOPQRSTUVWXYZ .. ", "two dots at least");
    // The streak: today or yesterday back, consecutive days only.
    const std::vector<std::int64_t> days{100, 99, 98, 96};
    check(b::bootLogWorkStreak(days, 100) == 3, "streak ending today");
    check(b::bootLogWorkStreak(days, 101) == 3, "streak ending yesterday");
    check(b::bootLogWorkStreak(days, 102) == 0, "no streak after a gap day");
    check(!b::bootLogWork(0, 0) && b::bootLogWork(0, 36) && near(b::bootLogWork(0, 7200)->totalHours, 2), "the streak line needs a streak or hours");
    // The 8 px flex gap: the last character of each segment but the last.
    const auto runs = b::BootLog::runs(lines[2], palette, d::portableCharacterRules());
    check(runs.size() == 6, "runs of a POWER line: " + std::to_string(runs.size()));
    check(runs[0].text.empty() == false && runs[0].text == ">" && near(runs[0].style.tracking, 8 / 12.5), "the prompt carries the gap");
    check(runs[2].text == " " && near(runs[2].style.tracking, 8 / 12.5) && runs[1].text == " POWER ................." && near(runs[1].style.tracking, 0), "the dot leader's last space carries the gap");
    check(runs[3].text == "82" && runs[4].text == "%" && near(runs[4].style.tracking, 8 / 12.5) && runs[4].style.color == d::fixed::green, "the value's last character carries the gap");
    check(runs[5].text == "  CHARGE MODE" && near(runs[5].style.tracking, 0) && runs[5].style.size == 12.5 && runs[5].style.weight == d::FontWeight::medium, "the trailing segment has no gap");
    // The day key and its consumption (the data model's rule).
    check(b::bootLogDay({2026, 3, 7, 0, 0}) == "2026-03-07", "day key");
    check(b::bootLogDayNumber(1970, 1, 1) == 0 && b::bootLogDayNumber(2000, 3, 1) == 11017 && b::bootLogDayNumber(2026, 10, 23) - b::bootLogDayNumber(2026, 9, 30) == 23, "day numbers");
    auto settings = ehud::data::Settings::defaults();
    check(settings.dailyBootLog() && settings.consumeDailyBootLog("2026-03-07") && !settings.consumeDailyBootLog("2026-03-07") && settings.consumeDailyBootLog("2026-03-08"),
        "the first open of each day consumes it");
}

const d::CharacterRules& characters() { static const auto rules = d::portableCharacterRules(); return rules; }
struct FakeMeasurer final : d::TextMeasurer {
    double width(std::span<const d::TextRun> runs, d::TextRegion) override {
        double w = 0;
        for (const auto& r : runs) w += double(characters().count(r.text)) * r.style.size * (.55 + r.style.tracking);
        return std::ceil(w * 2) / 2;
    }
    d::LineMetrics metrics(const d::TextStyle& s, d::TextRegion, double) override { return {s.size * s.lineHeight, s.size * .8, s.size * .2}; }
};

void panel() {
    FakeMeasurer measurer;
    d::Environment env;
    env.text.measurer = &measurer; env.text.uppercase = d::asciiUppercase; env.now = 50;
    const auto at = [&](double t) { env.now = t; env.deadlines.advance(t); };
    b::BootLog log(env);
    unsigned finished{};
    log.onFinish = [&] { ++finished; };
    check(b::BootLog::capacity == 15, "15 lines fit above the cursor");
    check(log.layer().bounds() == d::Size{570, 440} && log.layer().hidden() && log.layer().zPosition() == 30, "the idle panel");
    std::vector<b::BootLogLine> lines;
    for (int i = 0; i < 18; ++i) lines.push_back(b::BootLogLine::entry("K" + std::to_string(i), "V"));
    lines[3] = b::BootLogLine::sub("detail");
    log.play(lines, "1.3.0");
    check(log.isPlaying() && !log.layer().hidden() && log.rowTexts().size() == 15, "extra lines are dropped");
    check(log.terminalText() == "ENDFIELD INDUSTRIES TERMINAL v1.3.0", "terminal name with the version");
    check(log.isBlinking(), "the cursor blinks");
    same(log.cursorFrame(), {24, 61 + 3.5, 9, 15}, "cursor on the first slot");
    same(d::textOf(log.row(0)).lineFrame, {24, 61 + 4.75, 522, 12.5}, "first row line box");
    same(d::textOf(log.row(3)).lineFrame, {42, 61 + 3 * 22 + 4.75, 504, 12.5}, "sub rows are indented to 42");
    check(near(log.row(0).presented(Channel::opacity, 50), 0) && near(log.row(0).presented(Channel::translateX, 50), -6), "rows start hidden and 6 px left");
    // bandIn: the centre-out wipe over .3 SNAP.
    check(near(log.term().presented(Channel::wipe, 50), 0) && near(log.term().presented(Channel::wipe, 50.3), 1), "bandIn");
    // Reveal: 280 ms, then 260 ms, then every 190 ms.
    at(50.279); check(log.revealedCount() == 0, "nothing before 280 ms");
    at(50.28); check(log.revealedCount() == 1 && near(log.row(0).presented(Channel::opacity, 50.44), 1), "first line at 280 ms, fades .16");
    same(log.cursorFrame(), {24, 61 + 22 + 3.5, 9, 15}, "the cursor moves below the last line");
    at(50.539); check(log.revealedCount() == 1, "second waits 260 ms");
    at(50.54); check(log.revealedCount() == 2, "second at 540 ms");
    at(50.73); check(log.revealedCount() == 3, "then every 190 ms");
    // The blink steps 1, .6, .2, .6 every 250 ms.
    check(near(log.cursor().value(Channel::opacity), .2), "blink at 500 ms (" + std::to_string(log.cursor().value(Channel::opacity)) + ")");
    at(50.75); check(near(log.cursor().value(Channel::opacity), .6), "blink at 750 ms");
    at(51.0); check(near(log.cursor().value(Channel::opacity), 1), "blink cycle 1 s");
    // Skip: the wipe-up (.38 E to 0, opacity to .2), then onFinish.
    log.skip();
    check(log.isFinishing() && finished == 0, "skip starts the wipe-up");
    check(near(log.term().presented(Channel::opacity, 51.38), .2) && near(log.term().presented(Channel::wipe, 51.38), 0), "bootOut");
    at(51.38);
    check(finished == 1 && !log.isPlaying() && log.layer().hidden() && !log.isBlinking() && log.rowTexts().empty(), "finished and reset");
    // A full run without skipping: 900 ms after the last line.
    log.play({b::BootLogLine::header("BOOT SEQUENCE", "x"), b::BootLogLine::footer("ALL SYSTEMS ONLINE.", "WELCOME BACK, X.")}, "1.3.0");
    at(51.661); at(51.921);
    check(log.revealedCount() == 2, "both lines revealed");
    at(52.82); check(!log.isFinishing(), "the tail lasts 900 ms");
    at(52.822); check(log.isFinishing(), "then the wipe-up");
    at(53.21); check(finished == 2, "finished");
    // No lines: 900 ms then finish.
    log.play({}, "1.3.0");
    at(54.12); check(log.isFinishing(), "an empty log finishes after 900 ms");
    at(54.51); check(finished == 3, "and completes");
    // Reduce Motion: no wipe, no blink, the finish completes at once.
    env.reduceMotion = true;
    log.play({b::BootLogLine::sub("x")}, "1.3.0");
    check(!log.isBlinking() && near(log.term().presented(Channel::wipe, 55), 1), "Reduce Motion: no wipe or blink");
    log.skip();
    check(finished == 4 && !log.isPlaying(), "Reduce Motion finishes at once");
}

// The centre while the log plays (BootLogStage, host side): the module host
// is transparent while concealed, and the end's entrance comes from below.
void centre() {
    namespace c = endfield::core;
    c::ModulePresentation p(c::Module::clipboard);
    p.setDesignScreen(c::Module::clipboard, true);
    check(p.sample(1).presentation.current.opacity == 1, "shown by default");
    p.setConcealed(true);
    check(p.concealed() && p.sample(2).presentation.current.opacity == 0, "concealed: the module host is transparent");
    p.setConcealed(false);
    const auto below = c::ShellPosition::subpageOffset(0, 1);
    const auto entered = p.beginEntrance(3, false, below).presentation.current;
    check(entered.opacity == 1 && entered.entrance && entered.entrance->offset == below, "revealed, entering from below");
    const auto deploy = p.beginEntrance(4).presentation.current;
    check(deploy.entrance && deploy.entrance->offset == c::ShellPosition::entranceOffset(std::nullopt, c::Module::clipboard), "the deploy entrance keeps the disc-centre offset");
    check(!p.beginEntrance(5, true, below).presentation.current.entrance || p.beginEntrance(5, true, below).presentation.current.entrance->start == 4, "never a new entrance under Reduce Motion");
}
}

int main() {
    try {
        content();
        panel();
        centre();
        std::cout << "boot log contracts passed (" << checks << " checks)\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAILED: " << e.what() << '\n';
        return 1;
    }
}
