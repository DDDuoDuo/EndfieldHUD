// Contract suite of native/hud_design_text_field (LIBRARY-DESIGN.md section
// 10): the portable TextField editing through NativeDesignTextFields over
// NativeProjectedEditor. The caret blink (1.06 s, 80 ms ramps, solid after a
// move, only in a focused field of a foreground HUD, solid under Reduce
// Motion, frames only during ramps), the edit shortcuts, the secure field's
// clipboard and undo refusal, the right-click menu through a FAKE (no menu or
// window ever shows), the I-beam, Esc / Return rules and the published
// scenes' retirement. Hidden window, WARP, no TSF manager, no input injection.
#include "native/hud_design_text_field.hpp"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace endfield;
using namespace native;
namespace d = modules::design;
namespace {
unsigned checks{};
void check(bool v, const char* s) { ++checks; if (!v) throw std::runtime_error(s); }
struct Window {
    HWND hwnd{};
    Window() {
        WNDCLASSW c{}; c.lpfnWndProc = DefWindowProcW; c.hInstance = GetModuleHandleW(nullptr); c.lpszClassName = L"EndfieldDesignFieldHidden";
        check(RegisterClassW(&c) != 0, "Register owned fixture");
        hwnd = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP, c.lpszClassName, L"Synthetic", WS_POPUP, 0, 0, 590, 510, nullptr, nullptr, c.hInstance, nullptr);
        check(hwnd && !IsWindowVisible(hwnd), "Synthetic window hidden");
    }
    ~Window() { if (hwnd) DestroyWindow(hwnd); UnregisterClassW(L"EndfieldDesignFieldHidden", GetModuleHandleW(nullptr)); }
};
d::KeyInput key(std::uint32_t vk, bool control = false, bool shift = false) {
    d::KeyInput k; k.virtualKey = vk; k.control = control; k.shift = shift;
    if (vk >= 'A' && vk <= 'Z') k.character = char32_t(vk - 'A' + 'a');
    return k;
}
// The caret surface's opacity as drawn (the editor's fourth retained leaf).
float caret(NativeDesignTextFields& host) {
    const auto entries = host.entries();
    check(!entries.empty() && entries[0].scene, "The editing field publishes its editor scene");
    const auto draws = entries[0].scene->prepareDraws();
    check(draws.size() == 4, "Selection, glyphs, composition and caret");
    return draws[3].opacity;
}
void run(const std::filesystem::path& shader) {
    Window window;
    Renderer renderer;
    renderer.initialize(window.hwnd, 590, 510, {Driver::warpForTests, shader, RenderTarget::offscreenForTests});
    LayerRasterizer raster;
    std::optional<std::u16string> clipboard;
    unsigned writes{};
    std::vector<std::array<bool, 3>> menus;
    std::optional<d::EditCommand> menuAnswer;
    NativeDesignTextFieldOptions options;
    options.window = window.hwnd;
    options.ownerMessage = WM_APP + 0x44;
    options.raster.pixelsPerPoint = 1;
    options.clipboard.read = [&] { return clipboard; };
    options.clipboard.write = [&](std::u16string_view v) { clipboard = std::u16string(v); ++writes; return true; };
    options.editMenu = [&](core::Point, bool cut, bool copy, bool paste) { menus.push_back({cut, copy, paste}); return menuAnswer; };
    NativeDesignTextFields host(raster, options);
    d::Environment env;
    env.text.uppercase = d::asciiUppercase;
    d::LayerArena arena("f", &env.deadlines);
    d::ControlSet set;
    d::Builder builder{env, arena, set};
    auto& field = builder.make<d::TextField>("field", "Name", "hello", "Name", d::Size{200, 30});
    field.setEditorHost(&host);
    std::vector<std::string> changes;
    field.onChange = [&](const std::string& t) { changes.push_back(t); };
    unsigned cancels{};
    field.onCancel = [&] { ++cancels; };
    const auto camera = layerViewportProjection(590, 510);
    double time{};
    const auto pose = [&](double t, bool foreground = true, bool reduce = false) {
        time = t;
        host.setPose({core::Matrix4{}, camera, 590, 510, 1, foreground, t, reduce});
        (void)host.upload(renderer);
    };
    // --- begin editing; the caret blink -------------------------------------------
    field.beginEditing();
    check(field.editing() && host.editing() && host.field() == &field, "The field attaches the native editor");
    pose(0);
    check(caret(host) == 1, "The caret starts solid");
    check(!host.requiresFrames(.2) && host.nextWakeTime(.2) && std::abs(*host.nextWakeTime(.2) - .53) < 1e-9, "Between ramps the owner sleeps until the fade (.5 x 1.06 s)");
    check(host.requiresFrames(.55), "Frames only during the 80 ms fade");
    pose(.53 + .04);
    const auto fading = caret(host);
    check(fading > 0 && fading < 1, "The caret fades over 80 ms");
    pose(.8);
    check(caret(host) == 0 && !host.requiresFrames(.8), "Hidden half of the cycle, no frames");
    // Typing moves the caret: solid again, the cycle restarts.
    check(host.character('!', false), "Typing reaches the editor");
    check(!changes.empty() && changes.back() == "hello!", "Each edit reaches the field (bounded, onChange)");
    pose(.81);
    check(caret(host) == 1 && host.nextWakeTime(.81) && std::abs(*host.nextWakeTime(.81) - (.80 + .53)) < .02, "Solid after every move");
    // Only in a focused field of the foreground HUD; solid under Reduce Motion.
    pose(.9, false);
    check(caret(host) == 0 && !host.requiresFrames(.9) && !host.nextWakeTime(.9), "No caret and no wakes while the HUD is not foreground");
    pose(1, true, true);
    check(caret(host) == 1 && !host.requiresFrames(1.5) && !host.nextWakeTime(1.5), "Reduce Motion keeps it solid with no wakes");
    pose(1.6);
    // --- shortcuts and the clipboard ----------------------------------------------------
    check(host.key(key('A', true)) && host.selection(field) == std::pair<std::uint32_t, std::uint32_t>{0, 6}, "Ctrl+A selects all");
    check(host.key(key('C', true)) && clipboard == u"hello!", "Ctrl+C copies the selection");
    check(host.key(key('X', true)) && host.text(field).empty() && clipboard == u"hello!", "Ctrl+X cuts it");
    check(host.key(key('V', true)) && host.text(field) == "hello!", "Ctrl+V pastes");
    check(host.key(key('Z', true)) && host.text(field).empty(), "Ctrl+Z undoes");
    check(host.key(key('Y', true)) && host.text(field) == "hello!", "Ctrl+Y redoes");
    check(host.key(key('Z', true)) && host.key(key('Z', true, true)) && host.text(field) == "hello!", "Ctrl+Shift+Z redoes too");
    check(host.key(key(d::key::left, false, true)) && host.selection(field)->second == 1, "Shift+arrow extends (NSRange location, length)");
    const auto item = host.accessibilityItem();
    check(item && item->role == d::AccessibilityRole::edit && item->focused && item->value == "hello!", "The editing field stays an accessible, focused edit");
    // --- the I-beam and the right-click menu (fake) -------------------------------------
    check(host.textCursorAt({100, 15}) && !host.textCursorAt({300, 200}), "The I-beam only over the open field");
    host.key(key('A', true));
    menuAnswer.reset();
    check(host.rightPointerDown({100, 15}, {100, 15}) && host.selection(field) == std::pair<std::uint32_t, std::uint32_t>{0, 6}, "A right-click inside the selection keeps it");
    check(menus.size() == 1 && menus[0][0] && menus[0][1] && menus[0][2], "Cut, Copy and Paste are offered for a selection with a clipboard text");
    // HUDProjectedTextEditor.rightMouseDown: outside the selection the caret
    // moves to the click (behind the text's end here) before the menu opens.
    host.key(key(d::key::left));
    check(host.key(key(d::key::right, false, true)) && host.selection(field) == std::pair<std::uint32_t, std::uint32_t>{0, 1}, "The first character is selected");
    check(host.rightPointerDown({150, 15}, {150, 15}) && host.selection(field) == std::pair<std::uint32_t, std::uint32_t>{6, 0}, "A right-click outside the selection moves the caret there");
    check(menus.size() == 2 && !menus[1][0] && !menus[1][1] && menus[1][2], "The menu then offers no Cut or Copy");
    host.key(key('A', true));
    clipboard = u"X";
    menuAnswer = d::EditCommand::paste;
    check(host.rightPointerDown({100, 15}, {100, 15}) && host.text(field) == "X", "The menu's Paste replaces the selection");
    check(!host.rightPointerDown({400, 300}, {400, 300}), "A right-click beside the field is not the field's");
    // --- Return commits; Esc restores --------------------------------------------------------
    check(host.key(key(d::key::enter)) && !field.editing() && !host.editing() && field.text() == "X", "Return commits a single-line field");
    pose(2);
    (void)host.upload(renderer);
    check(host.entries().empty(), "An ended editor leaves the composition");
    check(host.collectRetired(renderer), "Its scenes release after the publication");
    field.beginEditing();
    pose(2.1);
    host.character('Y', false);
    check(host.key(key(d::key::escape)) && field.text() == "X" && cancels == 1, "Esc restores the original text and cancels");
    // --- a secure field never reaches the clipboard ----------------------------------------
    d::TextField::Options secureOptions; secureOptions.secure = true;
    auto& secret = builder.make<d::TextField>("secret", "Password", "", "Password", d::Size{200, 30}, secureOptions);
    secret.setEditorHost(&host);
    secret.beginEditing();
    pose(2.2);
    for (const char c : std::string("abc")) host.character(std::uint32_t(c), false);
    const auto written = writes;
    host.key(key('A', true));
    check(host.key(key('C', true)) && host.key(key('X', true)) && writes == written && host.text(secret) == "abc", "Cut and copy are refused in a secure field");
    menus.clear(); menuAnswer.reset();
    host.rightPointerDown({100, 15}, {100, 15});
    check(menus.size() == 1 && !menus[0][0] && !menus[0][1], "The menu offers no Cut or Copy for a secure field");
    host.key(key(d::key::right));
    host.character('d', false);
    check(host.key(key('Z', true)) && host.text(secret) == "abcd", "A secure field keeps no undo");
    secret.endEditing(true);
    pose(2.3);
    (void)host.upload(renderer);
    check(host.collectRetired(renderer) && host.releaseResources(renderer), "Everything releases");
    check(!IsWindowVisible(window.hwnd) && renderer.stats().presents == 0, "No window or menu was shown");
}
}
int wmain(int argc, wchar_t** argv) {
    const auto com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    try {
        check(SUCCEEDED(com) && argc == 2, "Pass the shader");
        run(argv[1]);
        CoUninitialize();
        std::cout << "HUD design text field: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        if (SUCCEEDED(com)) CoUninitialize();
        std::cerr << "HUD design text field after " << checks << ": " << e.what() << '\n';
        return 1;
    }
}
#else
int main() { return 0; }
#endif
