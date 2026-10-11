#pragma once
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
struct IRawElementProviderFragment;struct IRawElementProviderFragmentRoot;

namespace endfield::native {
class MacImportAutomationElement;
// One accessible part of the import card (app/areas/mac_import_card.hpp), in
// pre-order: every node's parent is listed before it.
struct MacImportAutomationNode {
    enum class Role : std::uint8_t { dialog, text, progress, list, item, button };
    static constexpr std::size_t topLevel = static_cast<std::size_t>(-1);
    std::string id;                  // stable per part: "dialog", "title", "row.3", "confirm", ...
    Role role{Role::text};
    std::string name, help;          // UIA Name / HelpText
    std::optional<core::Rect> rect;  // logical client bounds; none: scrolled out of the card
    std::size_t parent{topLevel};    // index of the parent node; topLevel: under the owner's group
    bool enabled{true}, focused{};   // buttons: Invoke accepted, the card's keyboard focus
    double value{};                  // progress: percent (0...100)
    bool operator==(const MacImportAutomationNode&) const = default;
};
// The macImport area's view of its card:
//  nodes()     the presented card (dialog, title, message, progress, the full
//              list with its scrolled-out rows offscreen, detail, position,
//              the two buttons); empty while the card is hidden or fading;
//  toScreen()  a logical client rect -> physical screen pixels (DPI scale,
//              ClientToScreen); none hides it;
//  invoke()    the button's action, exactly as clicking it;
//  focus()     UIA SetFocus on a button: move the card's keyboard focus to
//              it, exactly as Tab/Left/Right do (false: refused, e.g. the
//              button is disabled or the card is closing).
struct MacImportAutomationHost {
    IRawElementProviderFragmentRoot* root{}; // the owner's group under the HUD window's root (borrowed, AddRef'd while connected)
    std::function<std::vector<MacImportAutomationNode>()> nodes;
    std::function<std::optional<core::Rect>(core::Rect client)> toScreen;
    std::function<bool(std::string_view id)> invoke;
    std::function<bool(std::string_view id)> focus;
};
// focusMoves: the card's keyboard focus moved to another part (the card
// opening with Cancel focused, Tab, Left/Right, a pressed button, SetFocus),
// counted whether or not a client listened for the focus event.
struct MacImportAutomationStats { std::uint64_t refreshes{}, structureChanges{}, created{}, disconnected{}, invokes{}, focusMoves{}, focusRequests{}; };
// UI Automation fragments for the Windows-only "Import macOS data…" card. The
// card is a modal dialog above the HUD: a Pane with IsDialog named by its
// title (help: the message) holding Text parts, a read-only ProgressBar
// (RangeValue 0...100) while a bounded job runs, a List whose ListItems are
// the data-set / report / relink rows ("label: value"; rows scrolled out of
// the card are offscreen), and the two Buttons (Invoke = the click; disabled
// buttons refuse it). HasKeyboardFocus follows the card's own focus (Tab,
// arrows); the HUD keeps routing the card's keys itself. When that focus
// moves to another part (including the card appearing), refresh() raises
// UIA_AutomationFocusChangedEventId on the newly focused button for
// listening clients (app/module_owner.hpp: owners raise their own focus
// events; the host answers GetFocus through HasKeyboardFocus). SetFocus on
// a button moves the card's focus through the host (disabled buttons refuse
// it; other parts are not keyboard focusable).
//
// The owner's group lists child(i) (the dialog) among its children, routes
// ElementProviderFromPoint to fromPoint (the deepest part under the point)
// and raises StructureChanged when refresh() returns true. Parts keep their
// COM identity per id across refreshes; vanished ones are disconnected and
// answer UIA_E_ELEMENTNOTAVAILABLE. Calls arrive on the UI thread
// (ProviderOptions_UseComThreading). Refresh on content events only.
class NativeMacImportAutomation final {
public:
    explicit NativeMacImportAutomation(MacImportAutomationHost);
    ~NativeMacImportAutomation();
    NativeMacImportAutomation(const NativeMacImportAutomation&) = delete;
    NativeMacImportAutomation& operator=(const NativeMacImportAutomation&) = delete;
    // Re-reads the card; raises name/help/enabled/focus/value property events
    // and the focus-changed event for listening clients. True when the set,
    // order or nesting changed.
    bool refresh();
    // The part holding the card's keyboard focus (AddRef'd; null: none).
    HRESULT focused(IRawElementProviderFragment** out) const;
    std::size_t size() const noexcept;      // top-level parts (the dialog)
    std::size_t parts() const noexcept;     // every connected part
    HRESULT child(std::size_t index, IRawElementProviderFragment** out) const;
    // Screen-pixel hit test: the deepest part under the point.
    HRESULT fromPoint(double screenX, double screenY, IRawElementProviderFragment** out) const;
    void disconnect() noexcept;
    MacImportAutomationStats stats() const noexcept;
    struct State;
private:
    friend class MacImportAutomationElement;
    std::shared_ptr<State> state_;
};
}
#endif
