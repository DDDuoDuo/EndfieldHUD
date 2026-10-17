#pragma once
// UI Automation of the built-in Settings owner (System, Display, Hotkeys and
// About, their choice pages and the layout confirmation card): the fragments
// (native/settings_automation.hpp) under the Settings owner's group of the
// HUD window's root provider, listed by the host's module adapter
// (app/application_modules.cpp) through the ModuleOwner automation hooks.
#ifdef _WIN32
#include "native/screen_reader_peers.hpp"
#include "native/settings_automation.hpp"
#include "tools/settings_preview.hpp"
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace endfield::tools {
// HUDSettingsInteraction.layoutAccessibility over the page that accepts input
// (SettingsPreview::inputModule), in the page's visual order:
//   the page title (heading 1) and, on a choice page, "Back";
//   while "Restore default settings" asks: its question and message (text)
//   and the Cancel / Restore buttons;
//   otherwise every row the page shows (rows scrolled out of the viewport
//   are not listed, as on the Mac):
//     switch rows       toggle, named by the row title (Launch at login, ...)
//     choice rows       button named by the row title, valued by what the row
//                       shows (Language "English", Theme "Dark"); rows that
//                       open a page or act show only a glyph and carry no
//                       value (Center logo ›, Restore default settings ↺,
//                       Import macOS data… ›, Check for updates is valued by
//                       its state); the hotkey recorder "Summon HUD" is
//                       valued by its shortcut ("Press shortcut…" while
//                       recording, when it holds the keyboard focus)
//     sliders           slider (RangeValue: source range, value and step;
//                       Value: the formatted reading)
//     choice page rows  items of one list named by the page title (Language,
//                       Display, Center logo, Battery alert's Reading, the
//                       app / menu bar icons), selected as the row shows ✓
//     Theme color       a list of the five colours (selected: the current
//                       accent), then the "Custom color wheel" button
//     links             hyperlinks named "<title>, <value>" (About: Latest
//                       GitHub release, GitHub, the credits), valued by the
//                       address; without an address they are text
//     About's rows      text "<title>, <value>"; "Credits:" a heading 2
//   then the status line (live text) and, while recording a hotkey,
//   "Cancel shortcut recording".
// Action-backed nodes carry the page status as help (the Mac's
// setAccessibilityHelp(canvas.accessibilityStatus)). While the confirmation
// card is modal only the card is listed (it takes every click): a dialog
// named by its title (help: the countdown message) holding the title and
// message texts and the Revert / Keep buttons, the focused one holding the
// keyboard focus. Nothing is listed while no page accepts input (hidden,
// closing, switching modules). `now` samples the card's dismissal.
std::vector<native::SettingsAutomationNode> settingsAutomationNodes(const SettingsPreview&, double now);
// The source NSSlider step of a Settings slider (HUDSettingsInteraction:
// 1 for the duration, 0.05 for the UI scale, 0.01 for the HUD position,
// otherwise 2% of the range).
double settingsSliderStep(std::string_view id, double minimum, double maximum) noexcept;

// The owner's binding: the provider is created on the first refresh (a UIA
// client asked the HUD window for its root) and again for a new group;
// refresh() runs on content events only. The host also re-lists without an
// input event when a module switch settles or the active module's
// accept-input changes (the confirmation card shown or gone: the owner's
// pointer capture), in the frame that changed it (Application::present).
// Presses run at hooks.now() exactly as the page's clicks
// (SettingsPreview::perform / setSlider / performSafety / focusSafety), then
// hooks.changed(). Rects project through the page's
// (or card's) current plane, the DPI scale and the HUD window's client
// origin (native::screenReaderClientToScreen). The preview and the hooks'
// window outlive the binding.
class SettingsAutomation final {
public:
    SettingsAutomation(SettingsPreview&, native::ScreenReaderPeerHooks);
    ~SettingsAutomation(); // the fragments disconnect before the preview they read
    SettingsAutomation(const SettingsAutomation&) = delete;
    SettingsAutomation& operator=(const SettingsAutomation&) = delete;
    // ModuleOwner::automationRefresh: true when the set, order or nesting changed.
    bool refresh(IRawElementProviderFragmentRoot* group);
    std::size_t count() const noexcept;
    long child(std::size_t index, IRawElementProviderFragment** out) const;
    long fromPoint(double screenX, double screenY, IRawElementProviderFragment** out) const;
    // HUD concealed or owner removed: every fragment disconnects.
    void disconnect() noexcept;
    native::NativeSettingsAutomation* provider() const noexcept { return provider_.get(); }
private:
    SettingsPreview& preview_;
    native::ScreenReaderPeerHooks hooks_;
    std::unique_ptr<native::NativeSettingsAutomation> provider_;
    IRawElementProviderFragmentRoot* group_{};
};
} // namespace endfield::tools
#endif
