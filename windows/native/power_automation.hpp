#pragma once
#include "core/scene.hpp"
#ifdef _WIN32
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
struct IRawElementProviderFragment;
struct IRawElementProviderFragmentRoot;

namespace endfield::native {
class PowerAutomationPeer;
// The Power navigation entry as the Mac source shell exposes it
// (SystemHUDView.synchronizeNavigationButtons keeps exactly one native
// navigation button there, for .module(.power), and updateButtonStates fills
// it): label entry.navigationTitle ("Power / Device Battery"), help
// chargeBadge.accessibilityLabel (the live charge reading), value "Selected" /
// "Not selected", enabled while module input is allowed, and the frame of the
// displayed charge badge hit rect projected through the charge plane
// (projectedAccessibilityFrame). Rect: logical client coordinates; none while
// the badge has no hit region (hidden, waiting for its reveal, retracting).
struct PowerAutomationElement {
    std::string name, help, value;
    std::optional<core::Rect> rect;
    bool enabled{};
    bool operator==(const PowerAutomationElement&) const = default;
};
// The owner's view of that entry:
//  element()  the current entry; none while the HUD is hidden (not listed);
//  toScreen() a logical client rect -> physical screen pixels (DPI scale,
//             ClientToScreen); none hides it;
//  invoke()   activateNavigationButton: select Power (false: refused).
struct PowerAutomationHost {
    IRawElementProviderFragmentRoot* root{}; // the owner's UIA group (borrowed, AddRef'd while held)
    std::function<std::optional<PowerAutomationElement>()> element;
    std::function<std::optional<core::Rect>(core::Rect client)> toScreen;
    std::function<bool()> invoke;
};
struct PowerAutomationStats {
    std::uint64_t refreshes{}, structureChanges{}, created{}, disconnected{}, invokes{}, propertyEvents{};
};
// One UI Automation Button fragment (Invoke; read-only Value) under the HUD
// window's root provider (app/hud_automation.hpp), parented to the owner's
// group. Properties are read from element() when a client asks, so the
// projected frame follows the badge's circle/capsule without a refresh.
// refresh() runs on the host's content events (opening, closing, selection,
// changed artwork); notify() raises Name/HelpText/Value/IsEnabled changes for
// listening clients when the reading, language or selection changes. Nothing
// runs per frame. Calls arrive on the UI thread (ProviderOptions_UseComThreading).
class NativePowerAutomation final {
public:
    explicit NativePowerAutomation(PowerAutomationHost);
    ~NativePowerAutomation(); // disconnect(), then releases the root
    NativePowerAutomation(const NativePowerAutomation&) = delete;
    NativePowerAutomation& operator=(const NativePowerAutomation&) = delete;
    // Lists or withdraws the entry; true when the child set changed. A kept
    // entry raises its property changes (notify()).
    bool refresh();
    void notify();
    std::size_t size() const noexcept;
    HRESULT child(std::size_t index, IRawElementProviderFragment** out) const;
    // Screen-pixel hit test against the projected frame.
    HRESULT fromPoint(double screenX, double screenY, IRawElementProviderFragment** out) const;
    void disconnect() noexcept;
    PowerAutomationStats stats() const noexcept;
    struct State;
private:
    friend class PowerAutomationPeer;
    std::shared_ptr<State> state_;
};
} // namespace endfield::native
#endif
