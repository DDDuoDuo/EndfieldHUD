#pragma once
// UI Automation for the redesign: one fragment tree built from the
// components' accessibility items (modules::design::AccessibilityItem, the
// Mac's HUDAccessibilityItem + HUDAccessibilityStandIns,
// Sources/HUDDesignControl.swift:6-22, :163-281; LIB-16, LIB-81).
//
// AutomationTree below is the ONE implementation of the HUD's projected UIA
// elements (focus, live regions, identity per id and role, property events):
// it moved here from native/settings_automation.cpp (LIBRARY-DESIGN.md Q2),
// which keeps its public API as a thin adapter over it until Phase B1 moves
// Settings onto the design library. Its roles are the union of both users:
//   heading   Text with a heading level              (Settings)
//   text      Text; a read-only Value when `valued`  (design readouts)
//   status    Text with a polite live setting; a new text raises
//             LiveRegionChanged                      (Settings)
//   button    Button + Invoke; read-only Value when `valued`
//   link      Hyperlink + Invoke                     (Settings)
//   toggle    Button + Toggle (the Mac checkbox with subrole AXSwitch, 0/1)
//   slider    Slider + RangeValue (minimum/maximum/step; writes clamp and
//             COMMIT like the stand-in's setAccessibilityValue -> onChange(v,
//             true)) + read-only Value (the formatted reading "1.00x")
//   list      List + Selection (single selection) of its items (Settings)
//   item      ListItem + SelectionItem; Select is the row's click (Settings)
//   dialog    Pane with IsDialog (the confirmation card) (Settings)
//   popUp     ComboBox + ExpandCollapse (`on`: its menu is open; Expand and
//             Collapse press it like the Mac stand-in, which only presses) +
//             read-only Value (the dropdown's value)
//   menuItem  MenuItem + Invoke + SelectionItem (selected = the checked row)
//   tab       TabItem + SelectionItem (segments, swatches, tab bars)
//   edit      Edit + Value (writable; keyboard focusable: SetFocus begins
//             editing; IsPassword for secure fields)
//   group     Group + read-only Value
// Disabled elements refuse their patterns like an NSButton's
// accessibilityPerformPress. Projected controls are not tab stops (the HUD
// routes its own keys) except elements marked focusable (the confirmation
// card's buttons, the recording hotkey recorder, editing text fields); when
// the focused element changes, refresh() raises AutomationFocusChanged on it
// for listening clients (the host never raises it for an owner).
//
// Nodes are listed in pre-order (every node's parent before it) in the
// visual order, which is the UIA navigation order. Rects are in the owner's
// coordinate spaces (`space` tags them: content-box px for design canvases;
// page or card points for Settings) and are projected through the live plane
// by toScreen() when a client asks, so a tilting HUD keeps them on the drawn
// controls without per-frame work. Elements keep their COM identity per id
// and role across refreshes; vanished ones are disconnected and answer
// UIA_E_ELEMENTNOTAVAILABLE. Calls arrive on the UI thread
// (ProviderOptions_UseComThreading). Refresh on content events only, never
// per frame; it allocates only while it rebuilds the list.
//
// NativeDesignAutomation is the design canvases' view of the tree: items
// arrive in router order (the Mac's stand-in order), only while a canvas
// accepts input (none while swapping); the overlay owner lists the confirm
// bar's pills while it captures. NativeScreenReaderPeers stays for modules
// not yet redesigned.
#include "modules/hud_design_control.hpp"
#ifdef _WIN32
#include <functional>
#include <memory>

struct IRawElementProviderFragment;
struct IRawElementProviderFragmentRoot;

namespace endfield::native {
struct AutomationNode {
    enum class Role : std::uint8_t { heading, text, status, button, link, toggle, slider, list, item, dialog, popUp, menuItem, tab, edit, group };
    static constexpr std::size_t topLevel = static_cast<std::size_t>(-1);
    std::string id, name, help;   // UIA AutomationId / Name / HelpText
    std::string value;            // Value text (read-only except edits)
    Role role{Role::text};
    std::uint8_t space{};         // the owner's coordinate space (toScreen)
    core::Rect rect;
    std::size_t parent{topLevel}; // index of the parent node; topLevel: under the owner's group
    bool enabled{true};
    bool on{};                    // toggle on; item / menuItem / tab selected; popUp expanded
    bool focused{};               // holds the HUD's keyboard focus
    bool focusable{};             // keyboard focusable
    bool valued{};                // text / button / link / group: expose `value` through a read-only Value
    bool secure{};                // edit: IsPassword
    int level{};                  // heading: 1 page title, 2 section
    double number{}, minimum{}, maximum{1}, step{1}; // slider
    bool operator==(const AutomationNode&) const = default;
};
// The owner's view:
//  nodes()     what accepts input now, empty while nothing does;
//  toScreen()  a rect in `space` -> physical screen pixels; none hides it;
//  invoke()    a button's, link's, toggle's, menu item's, tab's or item's
//              click (also Select);
//  setValue()  a slider set like the source NSSlider (commit);
//  setText()   an edit's new text (UTF-8);
//  focus()     SetFocus on a focusable element;
//  expand()    a popUp's Expand (true) / Collapse (false); absent: invoke when
//              the state differs (the Mac stand-in only presses);
//  changed()   after an accepted action: the owner presents a frame; the
//              nodes are then re-listed at once and the group reports
//              ChildrenInvalidated when they changed.
struct AutomationTreeHost {
    IRawElementProviderFragmentRoot* root{}; // the owner's group (borrowed, AddRef'd while held)
    std::function<std::vector<AutomationNode>()> nodes;
    std::function<std::optional<core::Rect>(core::Rect, std::uint8_t space)> toScreen;
    std::function<bool(std::string_view id)> invoke;
    std::function<bool(std::string_view id, double value)> setValue;
    std::function<bool(std::string_view id, std::string_view utf8)> setText;
    std::function<bool(std::string_view id)> focus;
    std::function<bool(std::string_view id, bool expand)> expand;
    std::function<void()> changed;
};
// focusMoves: the keyboard focus moved to another element (counted whether
// or not a client listened); announcements: a new status line.
struct AutomationTreeStats {
    std::uint64_t refreshes{}, structureChanges{}, created{}, disconnected{}, invokes{}, toggles{}, selections{}, values{}, texts{}, expansions{};
    std::uint64_t focusMoves{}, focusRequests{}, announcements{};
};
class AutomationTree final {
public:
    explicit AutomationTree(AutomationTreeHost);
    ~AutomationTree();                         // disconnects every element, then releases the group
    AutomationTree(const AutomationTree&) = delete;
    AutomationTree& operator=(const AutomationTree&) = delete;
    // Re-reads the nodes; raises name/help/value/state/enabled/focus property
    // events, selection, LiveRegionChanged and the focus-changed event for
    // listening clients. True when the set, order or nesting changed.
    bool refresh();
    std::size_t size() const noexcept;         // top-level nodes
    std::size_t nodes() const noexcept;        // every connected node
    long child(std::size_t index, IRawElementProviderFragment** out) const;   // HRESULT
    long focused(IRawElementProviderFragment** out) const;                   // HRESULT; null: none
    long fromPoint(double screenX, double screenY, IRawElementProviderFragment** out) const; // the deepest node, the last listed first
    void disconnect() noexcept;
    IRawElementProviderFragmentRoot* root() const noexcept;
    AutomationTreeStats stats() const noexcept;
    struct State;
private:
    std::shared_ptr<State> state_;
};

struct DesignAutomationHost {
    IRawElementProviderFragmentRoot* root{};                         // the owner's group (borrowed, AddRef'd while held)
    std::function<std::vector<modules::design::AccessibilityItem>()> items; // empty while no canvas accepts input
    std::function<std::optional<core::Rect>(core::Rect box)> toScreen; // box px -> physical screen pixels
    std::function<bool(std::string_view id)> invoke;                 // a press exactly as a click (activate)
    std::function<bool(std::string_view id, double value)> setValue; // sliders (commit)
    std::function<bool(std::string_view id, std::string_view utf8)> setText; // edits
    std::function<bool(std::string_view id)> focus;                  // edits begin editing
    std::function<bool(std::string_view id, bool expand)> expand;    // popUps open / close their menu
    std::function<void()> changed;                                   // present a frame and re-list
};
using DesignAutomationStats = AutomationTreeStats;
// The design library's mapping onto AutomationTree (roles above; an edit is
// focusable while enabled, popUp / menuItem / tab / toggle `on` carry their
// state, text and group values are read-only Values, a slider's help is its
// description when the item has no help).
std::vector<AutomationNode> automationNodes(std::span<const modules::design::AccessibilityItem>);
class NativeDesignAutomation final {
public:
    explicit NativeDesignAutomation(DesignAutomationHost);
    ~NativeDesignAutomation();                 // disconnects every element, then releases the group
    NativeDesignAutomation(const NativeDesignAutomation&) = delete;
    NativeDesignAutomation& operator=(const NativeDesignAutomation&) = delete;
    // A different group (the host re-registered the owner) disconnects every
    // element first and re-roots the tree. True when the set or order changed.
    bool refresh(IRawElementProviderFragmentRoot* group);
    std::size_t size() const noexcept;
    long child(std::size_t index, IRawElementProviderFragment** out) const;   // HRESULT
    long focused(IRawElementProviderFragment** out) const;                   // HRESULT
    long fromPoint(double screenX, double screenY, IRawElementProviderFragment** out) const; // topmost = last listed
    void disconnect() noexcept;
    DesignAutomationStats stats() const noexcept;
    struct State;
private:
    std::shared_ptr<State> state_;
};
} // namespace endfield::native
#endif
