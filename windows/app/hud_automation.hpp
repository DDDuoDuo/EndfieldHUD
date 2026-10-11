#pragma once
// UI Automation root of the HUD window (WM_GETOBJECT with UiaRootObjectId).
//
// The HWND hosts one fragment root (IRawElementProviderFragmentRoot; its host
// provider comes from UiaHostProviderFromHwnd). Each enabled module owner
// that exposes fragments (ModuleOwner::automationCount() > 0) appears as one
// group fragment under it, in registration order, named after the module it
// presents; the owner's own fragments (native::NativeNowPlayingAutomation,
// NativeVolumeAutomation, NativeMediaAssemblyAccessibility,
// NativeAccountAutomation, ...) are that group's children and use the group
// as their parent and fragment root. The Mac exposes the projected AppKit
// accessibility elements of the HUD the same way: one tree per HUD window.
//
// Hit tests and GetFocus ask modal owners (ModuleOwner::capturesPointer)
// first, then the others in reverse registration order (later owners draw
// above). GetFocus returns the owner fragment whose HasKeyboardFocus is true
// (a bounded walk); owners raise their own AutomationFocusChanged events.
//
// Content events only: the host calls refresh() after owner calls that
// reported changed artwork, module selection, opening and closing; nothing is
// polled and nothing runs per frame. The root is created only when a UIA
// client asks the window for it, so the HUD pays nothing without a client.
// Calls arrive on the UI thread (ProviderOptions_UseComThreading).
#include "app/module_owner.hpp"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

struct IRawElementProviderSimple;

namespace endfield::app {
struct HudAutomationSource {
    // Enabled owners in registration order (ModuleRegistry::forEach).
    std::function<void(std::vector<ModuleOwner*>&)> owners;
    // ModuleOwner::automationRefresh under the registry's failure isolation;
    // false when the owner reported no structural change (or failed).
    std::function<bool(ModuleOwner&, IRawElementProviderFragmentRoot*)> refresh;
    // Localized group name (the module caption the owner presents).
    std::function<std::string(const ModuleOwner&)> name;
};
struct HudAutomationStats {
    std::uint64_t refreshes{}, structureChanges{}, groupsCreated{}, groupsDisconnected{}, rootRequests{}, hitTests{}, focusQueries{};
};

class HudAutomation final {
public:
    HudAutomation(void* window, HudAutomationSource);
    ~HudAutomation(); // shutdown()
    HudAutomation(const HudAutomation&) = delete;
    HudAutomation& operator=(const HudAutomation&) = delete;
    // True for WM_GETOBJECT's lParam UiaRootObjectId (the HWND's UIA root).
    static bool rootRequest(std::intptr_t lParam) noexcept;
    // WM_GETOBJECT(UiaRootObjectId): UiaReturnRawElementProvider with the root.
    std::intptr_t answer(std::uintptr_t wParam, std::intptr_t lParam);
    // Content event: refresh every enabled owner through its group; raises
    // StructureChanged (ChildrenInvalidated) on changed groups and on the
    // root when the set of non-empty groups changed. True when anything changed.
    bool refresh();
    // HUD concealed: every owner disconnects its fragments; groups stay.
    void disconnectOwners() noexcept;
    // An owner leaves the registry (failure or destruction): its fragments
    // and group are disconnected and the owner is never called again.
    void remove(const ModuleOwner&) noexcept;
    // Teardown: disconnect owners and groups, then tell UIA the window's
    // provider is gone (UiaReturnRawElementProvider(hwnd, 0, 0, null)).
    void shutdown() noexcept;
    // The root fragment (borrowed; hidden coverage walks it directly).
    IRawElementProviderFragmentRoot* root() const noexcept;
    std::size_t groups() const noexcept; // non-empty groups currently listed
    HudAutomationStats stats() const noexcept;
    struct State;
private:
    std::shared_ptr<State> state_;
};
} // namespace endfield::app
