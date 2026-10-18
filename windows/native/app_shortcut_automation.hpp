#pragma once
// UI Automation fragments for the projected + Add App canvas (Mac
// HUDAppShortcutInteraction.layoutAccessibility over
// AppShortcutCanvas.accessibleActions, and the HUDShortcutNameField).
#include "core/scene.hpp"
#ifdef _WIN32
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
class AppShortcutAutomationElement;
// One transparent accessibility peer. A button (HUDShortcutActionButton) has
// the action's label ("Choose application", "Rename: <name>", "<preset>,
// selected", "Cancel", "Save shortcut", "Open <app>", "Edit <app>", "Remove
// shortcut: <app>") and the canvas status as help (the error, "Edit app
// shortcut" or "Application shortcuts"). The open name field is an edit
// ("Shortcut name", its help, its current text). Rects stay in 400x334
// canvas points; the host projects them when a client asks, so the tilted
// module plane never needs a refresh.
enum class AppShortcutAutomationKind : std::uint8_t {button,edit};
struct AppShortcutAutomationControl {
    std::string id,label,help;core::Rect rect;
    AppShortcutAutomationKind kind{AppShortcutAutomationKind::button};
    std::string value; // edit text
    bool operator==(const AppShortcutAutomationControl&)const=default;
};
// The owner's view (app/areas/app_shortcut_module.cpp):
//  controls()  the canvas peers while + Add App accepts input, else none;
//  toScreen()  a canvas rect -> physical screen pixels through the current
//              module plane (AppShortcutPreview::clientRect, DPI scale,
//              ClientToScreen); none hides it;
//  invoke()    the click's action (AppShortcutCanvas.perform(actionID:));
//  setText()   replaces the open name field's text through its editor (the
//              same normalization as typing); false when it cannot;
//  focused()   whether the HUD holds keyboard focus (null: always), so the
//              open name field - the first responder - has keyboard focus.
struct AppShortcutAutomationHost {
    IRawElementProviderFragmentRoot*root{}; // the owner's group under the HUD root (borrowed, AddRef'd while held)
    std::function<std::vector<AppShortcutAutomationControl>()>controls;
    std::function<std::optional<core::Rect>(core::Rect canvas)>toScreen;
    std::function<bool(std::string_view id)>invoke;
    std::function<bool(std::string_view id,std::string_view text)>setText;
    std::function<bool()>focused;
};
// focusMoves counts name fields that appeared while focused (each raised
// UIA_AutomationFocusChangedEventId when clients were listening).
struct AppShortcutAutomationStats {std::uint64_t refreshes{},structureChanges{},created{},disconnected{},invokes{},values{},focusMoves{};};
// The HUD window's root provider owns the tree: it lists child(i) among the
// owner's group, routes ElementProviderFromPoint to fromPoint and raises
// StructureChanged when refresh() returns true. Elements keep their COM
// identity per id across refreshes; vanished ones are disconnected and answer
// UIA_E_ELEMENTNOTAVAILABLE. Calls arrive on the UI thread
// (ProviderOptions_UseComThreading). Refresh on content events only, never per
// frame; it allocates only while rebuilding the list. Projected buttons are
// not tab stops (the HUD routes its own keys, as on the Mac). A name field
// that appears while the HUD is focused takes keyboard focus
// (beginEditing -> makeFirstResponder): its own fragment raises the focus
// change, as the HUD root never raises one for an owner.
class NativeAppShortcutAutomation final {
public:
    explicit NativeAppShortcutAutomation(AppShortcutAutomationHost);
    ~NativeAppShortcutAutomation();
    NativeAppShortcutAutomation(const NativeAppShortcutAutomation&)=delete;
    NativeAppShortcutAutomation&operator=(const NativeAppShortcutAutomation&)=delete;
    // Re-reads the controls; raises name/help/value property events for
    // listening clients. True when the set or order changed.
    bool refresh();
    std::size_t size()const noexcept;
    HRESULT child(std::size_t index,IRawElementProviderFragment**out)const;
    // Screen-pixel hit test (topmost = last listed control).
    HRESULT fromPoint(double screenX,double screenY,IRawElementProviderFragment**out)const;
    void disconnect()noexcept;
    AppShortcutAutomationStats stats()const noexcept;
    struct State;
private:friend class AppShortcutAutomationElement;std::shared_ptr<State>state_;
};
}
#endif
