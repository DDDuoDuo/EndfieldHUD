#pragma once
#include "core/scene.hpp"
#ifdef _WIN32
#include "native/hud_design_automation.hpp"
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
struct IRawElementProviderFragmentRoot;

namespace endfield::native {
class ChargeAlertRoot;
// What OverlayController's panel shows accessibility clients in its charging
// role (Sources/OverlayController.swift, ChargeIndicatorView.swift): the
// ChargeIndicatorView as one static text carrying its accessibility label
// ("EndfieldHUD, Battery: 63%, Charging"), and, only while the bar editor
// shows its strip, the strip's items (design::PlacementEditor: the Size and
// Length sliders, then the Cancel and Confirm pills; the Mac's
// HUDAccessibilityStandIns for the strip). The panel lists them as
// AutomationNodes: the indicator first (id "chargeAlert:indicator", its rect
// in physical virtual-desktop pixels, space 1), then the strip's items in
// strip points (space 0) that toScreen() projects through the strip's 0.7
// placement.
//
// The panel's side:
//  nodes()     the current nodes, read whenever a client asks (no per-frame work);
//  toScreen()  a node rect in its space -> physical screen pixels;
//  invoke(id)  a pill press, setValue(id, v) a slider value: queued outside
//              the provider call; false when the editor refuses it.
struct ChargeAlertAutomationHost {
    HWND window{};
    std::function<std::vector<AutomationNode>()> nodes;
    std::function<std::optional<core::Rect>(core::Rect, std::uint8_t space)> toScreen;
    std::function<bool(std::string_view id)> invoke;
    std::function<bool(std::string_view id, double value)> setValue;
};
struct ChargeAlertAutomationStats {
    std::uint64_t rootRequests{}, disconnected{};
    AutomationTreeStats tree;
};
// UI Automation for the floating alert window: one fragment root hosted by
// the window (WM_GETOBJECT / UiaRootObjectId) whose children are the panel's
// nodes, kept by the HUD's one projected-element implementation
// (AutomationTree: Text for the indicator, Slider + RangeValue for the strip's
// sliders, Button + Invoke for its pills). changed() runs on the panel's
// content events (a new label, language, the editor opening or closing, a
// slider value) and refreshes the tree, which raises StructureChanged and
// property changes only while UiaClientsAreListening(). Calls arrive on the
// panel's UI thread (ProviderOptions_UseComThreading); no COM call lets a
// C++ exception escape.
class ChargeAlertAutomation final {
public:
    explicit ChargeAlertAutomation(ChargeAlertAutomationHost);
    ~ChargeAlertAutomation(); // disconnect()
    ChargeAlertAutomation(const ChargeAlertAutomation&) = delete;
    ChargeAlertAutomation& operator=(const ChargeAlertAutomation&) = delete;
    static bool rootRequest(LPARAM) noexcept;
    // WM_GETOBJECT for UiaRootObjectId: the window's root provider.
    LRESULT answer(WPARAM, LPARAM);
    // Content event: re-lists the nodes and announces what changed.
    void changed();
    // The root as WM_GETOBJECT returns it (AddRef'd).
    HRESULT root(IRawElementProviderFragmentRoot** out) const;
    std::size_t size() const noexcept; // listed children (1, or 1 + the strip's items)
    // The window goes away: releases UIA's window reference and disconnects
    // every provider (later calls answer UIA_E_ELEMENTNOTAVAILABLE).
    void disconnect() noexcept;
    ChargeAlertAutomationStats stats() const noexcept;
    struct State;
private:
    friend class ChargeAlertRoot;
    std::shared_ptr<State> state_;
};
} // namespace endfield::native
#endif
