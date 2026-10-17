#pragma once
// Projected UI Automation peers (what Narrator and other screen readers read)
// for the local HUD modules: Reader, Map, Calendar and the minigame. The Mac
// source places transparent AppKit accessibility views over the projected
// canvas controls (ReaderAXButton, HUDWorldMapActionButton and the
// staticText status, CalendarAXButton and the CalendarFieldTextView editors,
// OrbiPomAXButton and the rules menu group, NotesRetainedMenu items). On
// Windows the same elements are UI Automation fragments listed under the
// module owner's group of the HUD window's root provider (app/
// hud_automation.hpp, ModuleOwner::automation*).
//
// Roles:
//   button  Button with Invoke (disabled ones refuse it, like an NSButton's
//           accessibilityPerformPress); not a tab stop: the HUD routes its
//           own keys (Escape, arrows, Space) as on the Mac.
//   edit    Edit with a writable Value (the field text) and keyboard focus:
//           SetFocus moves the HUD's text focus to that field.
//   text    Text (Mac staticText): name and help only.
//   group   Group with a read-only Value (Mac NSView group with an
//           accessibility value, e.g. the minigame rules).
// Rects are module canvas points; toScreen() projects them through the
// current module plane (the module's clientRect, DPI scale, ClientToScreen).
//
// Elements keep their COM identity per id and role across refreshes;
// vanished ones are disconnected and answer UIA_E_ELEMENTNOTAVAILABLE.
// refresh() runs on content events only (the host's automationRefresh, or
// after a UIA press), never per frame, and allocates only while it rebuilds
// the element list. Calls arrive on the UI thread
// (ProviderOptions_UseComThreading). Nothing is created until a UIA client
// asks the HUD window for its root.
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
struct IRawElementProviderFragment;
struct IRawElementProviderFragmentRoot;

namespace endfield::native {
class ScreenReaderPeerElement;
enum class ScreenReaderRole : std::uint8_t { button, edit, text, group };
struct ScreenReaderPeer {
    std::string id, label, help, value; // value: edit text or group value
    core::Rect rect;                    // module canvas points
    ScreenReaderRole role{ScreenReaderRole::button};
    bool enabled{true}, focused{};      // focused: an edit holding the HUD's text focus
    bool operator==(const ScreenReaderPeer&) const = default;
};
// The module's view, built by tools::readerAutomation, mapAutomation,
// calendarAutomation and orbiPomAutomation:
//  peers()     the current peers in source order (empty while the module does
//              not accept input);
//  toScreen()  a canvas rect -> physical screen pixels; none hides it;
//  invoke()    the button's action exactly as its click (true: performed);
//  setText()   replaces an edit's text like typing (true: accepted);
//  focus()     moves the HUD's text focus to an edit (true: accepted);
//  changed()   after any accepted press, write or focus: the module changed
//              outside the host's input routes (ScreenReaderPeerHooks::
//              changed); the peers are then re-listed at once and the group
//              reports ChildrenInvalidated when they changed.
struct ScreenReaderPeerSource {
    std::function<std::vector<ScreenReaderPeer>()> peers;
    std::function<std::optional<core::Rect>(core::Rect canvas)> toScreen;
    std::function<bool(std::string_view id)> invoke;
    std::function<bool(std::string_view id, std::string_view utf8)> setText;
    std::function<bool(std::string_view id)> focus;
    std::function<void()> changed;
};
// What the host gives a module's source: the HUD window, its clock (the time
// passed to the press, as for a click) and the content event of a press
// (present a frame and run the host's refresh, which also takes actions a
// press queued, such as the Reader's file chooser).
struct ScreenReaderPeerHooks {
    void* window{};                     // HWND of the HUD
    std::function<double()> now;
    std::function<void()> changed;
};
// Logical client rect (the module's clientRect) -> physical screen pixels.
std::optional<core::Rect> screenReaderClientToScreen(void* window, double scale, const std::optional<core::Rect>& logicalClient);

struct ScreenReaderPeerStats {
    std::uint64_t refreshes{}, structureChanges{}, created{}, disconnected{}, invokes{}, texts{}, focuses{};
};
class NativeScreenReaderPeers final {
public:
    explicit NativeScreenReaderPeers(ScreenReaderPeerSource);
    ~NativeScreenReaderPeers(); // disconnect(), then releases the group
    NativeScreenReaderPeers(const NativeScreenReaderPeers&) = delete;
    NativeScreenReaderPeers& operator=(const NativeScreenReaderPeers&) = delete;
    // ModuleOwner::automationRefresh: `group` is the owner's group (parent and
    // fragment root of every peer; AddRef'd while held). A different group
    // disconnects the old peers first. Re-reads the peers and raises
    // name/help/value/enabled/focus changes for listening clients; true when
    // the set or order changed (the host then raises StructureChanged).
    bool refresh(IRawElementProviderFragmentRoot* group);
    std::size_t size() const noexcept;
    HRESULT child(std::size_t index, IRawElementProviderFragment** out) const;
    // Screen-pixel hit test (topmost = last listed peer).
    HRESULT fromPoint(double screenX, double screenY, IRawElementProviderFragment** out) const;
    // Every peer disconnects (HUD hidden, owner removed); the group is kept.
    void disconnect() noexcept;
    ScreenReaderPeerStats stats() const noexcept;
    struct State;
private:
    friend class ScreenReaderPeerElement;
    std::shared_ptr<State> state_;
};
} // namespace endfield::native
#endif
