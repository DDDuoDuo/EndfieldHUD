// Host contracts of the RC 128 user reports, without global input or user data:
//   - the summon chord recogniser of the low-level hook path (pure decisions);
//   - how the display adaptation of the centre plane enters the layout (its
//     measure, DesktopChromeProjectionPlan::contentScale on the centre node
//     of the deployed source shell, needs the packaged runtime: the app
//     verification and map_occlusion_contracts check it on several displays).
// Settings > Hotkeys > "Open with hotkey" (summonHotkeyEnabled) is the Mac
// row now: tests/settings_canvas_tests.cpp covers it.
#include "app/summon_hotkey.hpp"
#include "core/source_desktop_chrome.hpp"
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

using namespace endfield;
namespace {
unsigned checks{};
void check(bool value, const std::string& message) { ++checks; if (!value) throw std::runtime_error(message); }
ehud::data::Json read(const char* path) {
    std::ifstream file(path, std::ios::binary); check(bool(file), "Fixture opens");
    std::string text((std::istreambuf_iterator<char>(file)), {}); return ehud::data::Json::parse(text, 64 * 1024 * 1024);
}

void matcher() {
    using app::SummonChordMatcher; using app::SummonKeyEvent; using app::SummonDecision;
    constexpr std::uint32_t grave = VK_OEM_3, graveScan = 0x29;
    SummonChordMatcher m({grave, MOD_CONTROL, graveScan, false});
    const auto key = [&](std::uint32_t vk, bool down, std::uint32_t modifiers, std::uint32_t time, std::uint32_t scan = 0x29, bool extended = false, bool already = false) {
        return m.key(SummonKeyEvent{vk, scan, extended, down, already, time}, modifiers); };
    check(key(grave, true, MOD_CONTROL, 1000) == SummonDecision{true, true}, "Ctrl + ` summons and consumes its press like RegisterHotKey");
    check(key(grave, true, MOD_CONTROL, 1500) == SummonDecision{false, true}, "An auto-repeat of the consumed press is consumed without another summon (MOD_NOREPEAT)");
    check(key(grave, false, MOD_CONTROL, 1600) == SummonDecision{}, "The release always passes");
    check(key(grave, true, 0, 2000) == SummonDecision{}, "The key alone is ordinary typing");
    check(key(grave, true, MOD_CONTROL, 2030) == SummonDecision{}, "Pressing Ctrl while ` repeats never summons");
    check(key(grave, false, MOD_CONTROL, 2100) == SummonDecision{}, "Release after the unmatched press");
    check(key(grave, true, MOD_CONTROL | MOD_SHIFT, 3000) == SummonDecision{}, "Extra modifiers do not match (exact like RegisterHotKey)");
    (void)key(grave, false, MOD_CONTROL | MOD_SHIFT, 3050);
    check(key(0xDE, true, MOD_CONTROL, 4000) == SummonDecision{true, true}, "Another layout's virtual key on the same physical key still summons");
    (void)key(0xDE, false, MOD_CONTROL, 4050);
    check(key(0xDE, true, MOD_CONTROL, 5000, graveScan, true) == SummonDecision{}, "An extended key with the same scan code is another key");
    check(key(VK_LCONTROL, true, 0, 6000, 0x1d) == SummonDecision{} && key(VK_LCONTROL, false, MOD_CONTROL, 6010, 0x1d) == SummonDecision{}, "Modifier keys always pass untouched");
    check(key(grave, true, MOD_CONTROL, 7000, graveScan, false, true) == SummonDecision{}, "A key already down before the hook saw it is a repeat");
    (void)key(grave, false, MOD_CONTROL, 7100);
    // A release the hook missed (an elevated window had focus): a later press
    // beyond the slowest repeat delay is new.
    check(key(grave, true, MOD_CONTROL, 8000) == SummonDecision{true, true}, "Summon before the missed release");
    check(key(grave, true, MOD_CONTROL, 8000 + SummonChordMatcher::repeatWindowMilliseconds + 400) == SummonDecision{true, true}, "A press long after a missed release summons again");
    (void)key(grave, false, MOD_CONTROL, 9600);
    // The 32-bit millisecond clock wraps after 49.7 days; a repeat across the wrap is still a repeat.
    check(key(grave, true, MOD_CONTROL, 0xffffffffu - 5) == SummonDecision{true, true} && key(grave, true, MOD_CONTROL, 20) == SummonDecision{false, true},
        "Repeat detection survives the tick wrap");
    m.reset();
    SummonChordMatcher win({'H', MOD_WIN, 0x23, false});
    check(win.key({'H', 0x23, false, true, false, 10}, MOD_WIN) == SummonDecision{true, false}, "A Windows-key chord is reported but not consumed (Start must not open)");
    check(SummonChordMatcher::modifierBit(VK_RMENU) == MOD_ALT && SummonChordMatcher::modifierBit(VK_LSHIFT) == MOD_SHIFT && SummonChordMatcher::modifierBit(VK_RWIN) == MOD_WIN &&
        SummonChordMatcher::modifierBit(VK_CONTROL) == MOD_CONTROL && SummonChordMatcher::modifierBit('A') == 0, "Modifier bits follow RegisterHotKey");
    SummonChordMatcher none({});
    check(none.key({grave, graveScan, false, true, false, 1}, 0) == SummonDecision{}, "An empty chord never matches");
    const auto chord = app::SummonHotkeyHook::chord(VK_OEM_3, MOD_CONTROL);
    check(chord.key == VK_OEM_3 && chord.modifiers == MOD_CONTROL && !chord.extended, "The hook chord keeps the RegisterHotKey binding");
}

// The hook thread's lifecycle on an owned message-only window, without any
// injected input: start installs the low-level hook on its own thread (or
// fails cleanly where the session has no input desktop), stop joins it, a
// restart replaces it, and destruction leaves nothing running.
void hookLifecycle() {
    const HWND window = CreateWindowExW(0, L"STATIC", L"EndfieldHUD summon hook fixture", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr);
    check(window != nullptr, "An owned message-only window receives summon notices");
    {
        bool rejected{};
        try { app::SummonHotkeyHook invalid(window, WM_USER); } catch (const std::invalid_argument&) { rejected = true; }
        check(rejected, "The hook only posts owner-private WM_APP notices");
        app::SummonHotkeyHook hook(window, WM_APP + 0x2C0);
        check(!hook.running() && !hook.start({}, 1), "An empty chord installs nothing");
        const bool installed = hook.start(app::SummonHotkeyHook::chord(VK_OEM_3, MOD_CONTROL), 2);
        check(installed == hook.running() && hook.stats().installs == (installed ? 1u : 0u), "Start reports whether the low-level hook is installed on its thread");
        if (installed) {
            check(hook.start(app::SummonHotkeyHook::chord('H', MOD_CONTROL | MOD_ALT), 3) && hook.running(), "A new chord restarts the hook thread");
            hook.stop();
            check(!hook.running() && hook.stats().summons == 0 && hook.stats().consumed == 0, "Stop joins the thread; without input nothing was summoned or consumed");
        } else std::cout << "note: this session has no input desktop for a low-level hook; the RegisterHotKey path remains\n";
        hook.stop();
        check(!hook.running(), "Stopping twice is harmless");
        (void)hook.start(app::SummonHotkeyHook::chord(VK_OEM_3, MOD_CONTROL), 4);
    } // destruction stops a running hook
    DestroyWindow(window);
}

void displayAdaptation(const ehud::data::Json& fixture) {
    using namespace core;
    using namespace core::source;
    (void)fixture;
    // (windef.h defines near/far as macros.)
    const auto equal = [](double a, double b) { return std::abs(a - b) <= 1e-9 * std::max(1., std::abs(b)); };
    check(equal(desktopBaseDesignScale(desktopReferenceViewport), 1.15), "The reference Mac display (1470 x 956) has the Mac's capped design scale");
    DesktopChromeSettings settings{{0, 0, 2560. / 1.5, 1600. / 1.5}, 1.1, {}, Module::clipboard, true};
    check(settings.contentScale == 1, "Chrome settings default to the exact Mac scale");
    const auto mac = DesktopChromeLayout::make(settings, Matrix4{}, Matrix4{});
    settings.contentScale = (2560. / 1.5) / 1470; // The laptop's factor (x1.161).
    const auto adapted = DesktopChromeLayout::make(settings, Matrix4{}, Matrix4{});
    check(equal(mac.designScale, 1.15 * 1.1) && equal(adapted.designScale, 1.15 * 1.1 * settings.contentScale), "The design scale multiplies the Mac scale, HUD scale and adaptation");
    check(equal(adapted.reportScale, mac.reportScale) && equal(adapted.reportCenterY, mac.reportCenterY), "Module placement inside the plane is unchanged");
    bool rejected{};
    try { settings.contentScale = 0; (void)DesktopChromeLayout::make(settings, Matrix4{}, Matrix4{}); } catch (const std::exception&) { rejected = true; }
    check(rejected, "A non-positive adaptation is rejected");
    check(DesktopClockArtworkPlan::footerText("CTRL + `", "Click outside to close") == "ESC / CTRL + ` / Click outside to close" &&
        DesktopClockArtworkPlan::footerText("", "Click outside to close") == "ESC / Click outside to close", "The footer names the shortcut only while opening by hotkey is on");
}
}

int main(int argc, char** argv) {
    try {
        check(argc == 2, "Pass the source camera fixture");
        matcher();
        hookLifecycle();
        displayAdaptation(read(argv[1]));
        std::cout << "PASS " << checks << " hotkey and display adaptation checks\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL after " << checks << " checks: " << e.what() << '\n';
        return 1;
    }
}
