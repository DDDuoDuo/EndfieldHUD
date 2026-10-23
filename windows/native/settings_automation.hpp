#pragma once
// UI Automation fragments of the projected Settings pages (System, Display,
// Hotkeys, About and their sub-pages) and of the layout confirmation card.
//
// The Mac places transparent AppKit controls over the canvas
// (HUDSettingsInteraction.layoutAccessibility: one NSButton per accessible
// action, labelled by it, with the page status as help, and one NSSlider per
// visible slider) and HUDScaleSafetyView's card holds real NSButtons. On
// Windows the same controls are fragments under the Settings owner's group of
// the HUD window's root provider (app/hud_automation.hpp, ModuleOwner::
// automation*), each with the UIA control type and patterns of what it is:
//   heading  Text with a heading level (the page title 1, "Credits:" 2)
//   text     Text (About's information rows, the restore question)
//   status   Text with a polite live setting (the page's status line); a new
//            text raises LiveRegionChanged for listening clients
//   button   Button with Invoke; a read-only Value when the row shows a value
//            ("Language" valued "English"; the hotkey recorder valued by its
//            shortcut or "Press shortcut…", keyboard focused while recording)
//   link     Hyperlink with Invoke
//   toggle   Button with Toggle (On / Off)
//   slider   Slider with RangeValue over the source range and step (writes are
//            clamped into the range like the source setAccessibilityValue)
//            and a read-only Value with the formatted reading ("1.00×")
//   list     List with Selection (single selection) of its items
//   item     ListItem with SelectionItem (a choice page's rows, the app icons,
//            the theme colours); Select is the row's click
//   dialog   Pane with IsDialog (the confirmation card), holding its texts and
//            its two buttons; the card's keyboard focus is the focused button
// Disabled controls refuse their patterns like an NSButton's
// accessibilityPerformPress. Projected controls are not tab stops (the HUD
// routes its own keys: arrows, Page Up/Down, Escape), except the card's two
// buttons and the recording hotkey recorder, which hold the HUD's keyboard
// focus; when that focus moves to another fragment refresh() raises
// AutomationFocusChanged on it for listening clients (the host never raises
// it for an owner).
//
// Nodes are listed in pre-order (every node's parent before it) in the
// page's visual order, which is the UIA navigation order. Rects are page
// canvas points (the 400 x 334 page) or card-local points (the card at its
// origin); toScreen() projects them through the current plane when a client
// asks, so a tilting HUD keeps them on the drawn controls.
//
// The owner's group lists child(i) (the top-level nodes), routes
// ElementProviderFromPoint to fromPoint (the deepest node under the point)
// and raises StructureChanged when refresh() returns true. Nodes keep their
// COM identity per id and role across refreshes; vanished ones are
// disconnected and answer UIA_E_ELEMENTNOTAVAILABLE. Calls arrive on the UI
// thread (ProviderOptions_UseComThreading). Refresh on content events only,
// never per frame; it allocates only while it rebuilds the list.
//
// Implementation: a thin adapter over native::AutomationTree
// (native/hud_design_automation.hpp), the one implementation of the HUD's
// projected UIA elements, which this file's element code moved into
// (LIBRARY-DESIGN.md Q2). Phase B1 moves Settings onto the design library
// and deletes this adapter.
#include "core/scene.hpp"
#ifdef _WIN32
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
struct IRawElementProviderFragment;
struct IRawElementProviderFragmentRoot;

namespace endfield::native {
struct SettingsAutomationNode {
    enum class Role : std::uint8_t { heading, text, status, button, link, toggle, slider, list, item, dialog };
    enum class Space : std::uint8_t { page, card }; // page canvas or confirmation card points
    static constexpr std::size_t topLevel = static_cast<std::size_t>(-1);
    std::string id, name, help; // UIA AutomationId / Name / HelpText
    std::string value;          // read-only Value text (valued buttons, sliders)
    Role role{Role::text};
    Space space{Space::page};
    core::Rect rect;
    std::size_t parent{topLevel}; // index of the parent node; topLevel: under the owner's group
    bool enabled{true};
    bool on{};                    // toggle: On; item: selected
    bool focused{};               // holds the HUD's keyboard focus (card buttons, recording recorder)
    bool focusable{};             // keyboard focusable (card buttons, the recorder while recording)
    bool valued{};                // buttons: expose `value` through a read-only Value
    int level{};                  // heading: 1 page title, 2 section
    double number{}, minimum{}, maximum{1}, step{1}; // slider
    bool operator==(const SettingsAutomationNode&) const = default;
};
// The Settings owner's view (tools/settings_automation.hpp):
//  nodes()     the page accepting input, or the confirmation card while it is
//              modal; empty while no page accepts input (hidden, closing,
//              switching modules);
//  toScreen()  a page canvas / card-local rect -> physical screen pixels; none
//              hides it (offscreen);
//  invoke()    a button's, link's or toggle's click, an item's click (Select);
//  setValue()  a slider set like the source NSSlider (setSlider);
//  focus()     SetFocus on a card button: the card's focus moves to it like
//              Left/Right;
//  changed()   after an accepted press, value or focus: the page changed
//              outside the host's input routes (ScreenReaderPeerHooks::
//              changed); the nodes are then re-listed at once and the group
//              reports ChildrenInvalidated when they changed.
struct SettingsAutomationHost {
    IRawElementProviderFragmentRoot* root{}; // the owner's group (borrowed, AddRef'd while held)
    std::function<std::vector<SettingsAutomationNode>()> nodes;
    std::function<std::optional<core::Rect>(core::Rect, SettingsAutomationNode::Space)> toScreen;
    std::function<bool(std::string_view id)> invoke;
    std::function<bool(std::string_view id, double value)> setValue;
    std::function<bool(std::string_view id)> focus;
    std::function<void()> changed;
};
// focusMoves: the keyboard focus moved to another fragment (counted whether
// or not a client listened); announcements: a new status line.
struct SettingsAutomationStats {
    std::uint64_t refreshes{}, structureChanges{}, created{}, disconnected{}, invokes{}, toggles{}, selections{}, values{}, focusMoves{}, focusRequests{}, announcements{};
};
class NativeSettingsAutomation final {
public:
    explicit NativeSettingsAutomation(SettingsAutomationHost);
    ~NativeSettingsAutomation(); // disconnects every node, then releases the group
    NativeSettingsAutomation(const NativeSettingsAutomation&) = delete;
    NativeSettingsAutomation& operator=(const NativeSettingsAutomation&) = delete;
    // Re-reads the nodes; raises name/help/value/state/enabled/focus property
    // events, SelectionItem selection, LiveRegionChanged on the status and the
    // focus-changed event for listening clients. True when the set, order or
    // nesting changed.
    bool refresh();
    std::size_t size() const noexcept;  // top-level nodes
    std::size_t nodes() const noexcept; // every connected node
    HRESULT child(std::size_t index, IRawElementProviderFragment** out) const;
    // The node holding the keyboard focus (AddRef'd; null: none).
    HRESULT focused(IRawElementProviderFragment** out) const;
    // Screen-pixel hit test: the deepest node under the point.
    HRESULT fromPoint(double screenX, double screenY, IRawElementProviderFragment** out) const;
    void disconnect() noexcept;
    SettingsAutomationStats stats() const noexcept;
    struct State;
private:
    std::shared_ptr<State> state_;
};
} // namespace endfield::native
#endif
