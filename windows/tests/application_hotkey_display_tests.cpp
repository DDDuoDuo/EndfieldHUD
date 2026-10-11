// Host contracts of the RC 128 user reports, without global input or user data:
//   - the summon chord recogniser of the low-level hook path (pure decisions);
//   - the Windows display adaptation of the centre plane (desktopContentScale)
//     on the source camera fixture: the exact Mac scale on the reference Mac
//     display and wherever the Mac content already covers the ring, and the
//     reference proportion on larger displays;
//   - the Windows-only Settings > Hotkeys toggle "Summon HUD with hotkey".
#include "app/summon_hotkey.hpp"
#include "core/source_desktop_chrome.hpp"
#include "modules/settings_presentation.hpp"
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

void displayAdaptation(const ehud::data::Json& fixture) {
    using namespace core;
    using namespace core::source;
    const SourceCamera camera(fixture["runtimeRoot"]);
    // (windef.h defines near/far as macros.)
    const auto equal = [](double a, double b) { return std::abs(a - b) <= 1e-9 * std::max(1., std::abs(b)); };
    check(equal(desktopBaseDesignScale(desktopReferenceViewport), 1.15) && desktopContentScale(camera, desktopReferenceViewport) == 1,
        "The reference Mac display (1470 x 956) keeps the exact Mac design scale");
    for (const Rect v : {Rect{0, 0, 1280, 800}, Rect{0, 0, 1280. / 1.5, 800. / 1.5}, Rect{0, 0, 1440, 900}})
        check(desktopContentScale(camera, v) == 1, "Displays where the Mac content already covers the ring keep the Mac scale (" + std::to_string(v.width) + ")");
    // 2560 x 1600 at 150 %: the source canvas spans the width (aspect < 16:9).
    check(equal(desktopContentScale(camera, {0, 0, 2560. / 1.5, 1600. / 1.5}), (2560. / 1.5) / 1470), "A 2560 x 1600 display at 150 % grows the centre content with the ring (x1.161)");
    check(equal(desktopContentScale(camera, {0, 0, 1920, 1080}), 1920. / 1470), "1920 x 1080 grows it with the ring width");
    // Wider than 16:9: the canvas spans the height (1350 canvas units).
    const double wide = 1.15 * (1440. / 1350.) / (1470. / 2400.) / 1.15;
    check(std::abs(desktopContentScale(camera, {0, 0, 3440, 1440}) / wide - 1) < 1e-5, "An ultra-wide display grows it with the ring height");
    // 1280 x 720 (1920 x 1080 at 150 %): the Mac's own height limit shrinks it below the reference proportion.
    check(equal(desktopContentScale(camera, {0, 0, 1280, 720}), 1.15 * 1280. / 1470 / (720. / 740)), "A small 16:9 display is lifted to the reference proportion");
    DesktopChromeSettings settings{{0, 0, 2560. / 1.5, 1600. / 1.5}, 1.1, {}, Module::clipboard, true};
    check(settings.contentScale == 1, "Chrome settings default to the exact Mac scale");
    const auto mac = DesktopChromeLayout::make(settings, Matrix4{}, Matrix4{});
    settings.contentScale = desktopContentScale(camera, settings.viewport);
    const auto adapted = DesktopChromeLayout::make(settings, Matrix4{}, Matrix4{});
    check(equal(mac.designScale, 1.15 * 1.1) && equal(adapted.designScale, 1.15 * 1.1 * settings.contentScale), "The design scale multiplies the Mac scale, HUD scale and adaptation");
    check(equal(adapted.reportScale, mac.reportScale) && equal(adapted.reportCenterY, mac.reportCenterY), "Module placement inside the plane is unchanged");
    bool rejected{};
    try { settings.contentScale = 0; (void)DesktopChromeLayout::make(settings, Matrix4{}, Matrix4{}); } catch (const std::exception&) { rejected = true; }
    check(rejected, "A non-positive adaptation is rejected");
}

void settingsToggle() {
    using namespace modules;
    auto initial = ehud::data::Settings::defaults(); initial.set("launchAtLogin", false); initial.set("language", "english");
    SettingsController controller(initial, {}, core::Language::english);
    const auto rowIDs = [](const SettingsPresentation& s) { std::string ids; for (const auto& r : s.view().rows) ids += r.id + ","; return ids; };
    {
        SettingsPresentation plain(core::Module::hotkeys, controller, {});
        plain.setReduceMotion(true, 0); plain.activate(0);
        check(rowIDs(plain) == "capture,keyReset,", "Without the Windows hook the Hotkeys page is exactly the Mac page");
    }
    SettingsViewHooks hooks; hooks.summonHotkeyToggle = true;
    SettingsPresentation page(core::Module::hotkeys, controller, {}, {}, hooks);
    page.setReduceMotion(true, 0); page.activate(0);
    check(rowIDs(page) == "capture,keyReset,summonHotkey,", "The Windows toggle follows the Mac rows");
    const auto action = [&](std::string_view id) -> const SettingsAction* { for (const auto& a : page.view().actions) if (a.id == id) return &a; return nullptr; };
    check(action("summonHotkey") && action("summonHotkey")->label == "Summon HUD with hotkey, On", "The toggle starts on and reads its state");
    check(page.perform("summonHotkey", 1) && controller.committed().fields["windowsSummonHotkeyDisabled"].isBool() && controller.committed().fields["windowsSummonHotkeyDisabled"].boolean(),
        "Switching it off persists the Windows preference");
    page.refresh(2);
    check(action("summonHotkey") && action("summonHotkey")->label == "Summon HUD with hotkey, Off", "The toggle reads Off");
    check(page.perform("summonHotkey", 3) && !controller.committed().fields["windowsSummonHotkeyDisabled"].boolean(), "Switching it on again");
    page.perform("summonHotkey", 4);
    controller.restoreDefaults();
    check(!controller.committed().fields["windowsSummonHotkeyDisabled"].isBool(), "Restore defaults switches opening by hotkey on again (no saved override)");
    const std::string titles[]{settingsSummonToggleTitle(core::Language::english), settingsSummonToggleTitle(core::Language::simplifiedChinese),
        settingsSummonToggleTitle(core::Language::traditionalChinese), settingsSummonToggleTitle(core::Language::japanese), settingsSummonToggleTitle(core::Language::korean)};
    for (unsigned a = 0; a < 5; ++a) { check(!titles[a].empty() && ehud::data::Json::validUtf8(titles[a]), "Each language has a title"); for (unsigned b = 0; b < a; ++b) check(titles[a] != titles[b], "The five titles are translations"); }
    check(settingsSummonToggleTitle(core::Language::system) == titles[0], "System resolves to English");
}
}

int main(int argc, char** argv) {
    try {
        check(argc == 2, "Pass the source camera fixture");
        matcher();
        displayAdaptation(read(argv[1]));
        settingsToggle();
        std::cout << "PASS " << checks << " hotkey, display adaptation and Hotkeys toggle checks\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL after " << checks << " checks: " << e.what() << '\n';
        return 1;
    }
}
