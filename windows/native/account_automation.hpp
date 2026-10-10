#pragma once
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
class AccountAutomationElement;
// One transparent accessibility peer (Mac AccountAXButton): the control's
// accessibility label, its projected rect in logical client coordinates and
// whether a press is accepted.
struct AccountAutomationControl {
    std::string id,label;core::Rect rect;bool enabled{};
    bool operator==(const AccountAutomationControl&)const=default;
};
// The host's view of the Account Linking owner (tools::AccountLinkingOwner):
//  controls()  accessibleControls(): the Account face while it is active,
//              then the header wallet and its open recovery popover;
//  toScreen()  a logical client rect -> physical screen pixels (DPI scale,
//              ClientToScreen); none hides it;
//  invoke()    performAccessible(id, time): exactly the click's action.
struct AccountAutomationHost {
    IRawElementProviderFragmentRoot*root{}; // the HUD window's UIA root (borrowed, AddRef'd while connected)
    std::function<std::vector<AccountAutomationControl>()>controls;
    std::function<std::optional<core::Rect>(core::Rect client)>toScreen;
    std::function<bool(std::string_view id)>invoke;
};
struct AccountAutomationStats {std::uint64_t refreshes{},structureChanges{},created{},disconnected{},invokes{};};
// UI Automation fragments for the projected Account Linking controls (Mac
// HUDAccountInteraction.layoutAccessibility and the HUDAccountGauge actions):
// every control is a Button with Invoke, named by its accessibility label
// (Connect/Reconnect, Refresh, Disconnect, the region / role / header menus
// with their current value, the sync toggles, menu rows and the
// disconnect confirmation; the wallet "Arknights: Endfield · Sanity 343 / 360"
// and its popover Refresh). Disabled controls refuse Invoke like the Mac's
// accessibilityPerformPress. Projected controls are not tab stops: the HUD
// routes its own keys (Esc, arrows, Return/Space) as on the Mac.
//
// The HUD window's root provider owns the tree: it lists child(i) among its
// children, routes ElementProviderFromPoint to fromPoint and raises
// StructureChanged on itself when refresh() returns true. Elements keep their
// COM identity per id across refreshes; vanished ones are disconnected and
// answer UIA_E_ELEMENTNOTAVAILABLE. Calls arrive on the UI thread
// (ProviderOptions_UseComThreading). Refresh on content events only, never per
// frame; it allocates only while rebuilding the element list.
class NativeAccountAutomation final {
public:
    explicit NativeAccountAutomation(AccountAutomationHost);
    ~NativeAccountAutomation();
    NativeAccountAutomation(const NativeAccountAutomation&)=delete;
    NativeAccountAutomation&operator=(const NativeAccountAutomation&)=delete;
    // Re-reads the controls; raises name/enabled property events for listening
    // clients. True when the set or order changed.
    bool refresh();
    std::size_t size()const noexcept;
    HRESULT child(std::size_t index,IRawElementProviderFragment**out)const;
    // Screen-pixel hit test (topmost = last listed control).
    HRESULT fromPoint(double screenX,double screenY,IRawElementProviderFragment**out)const;
    void disconnect()noexcept;
    AccountAutomationStats stats()const noexcept;
    struct State;
private:friend class AccountAutomationElement;std::shared_ptr<State>state_;
};
}
#endif
