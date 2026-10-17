#pragma once
#include "core/scene.hpp"
#ifdef _WIN32
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
struct IRawElementProviderFragmentRoot;

namespace endfield::native {
class ChargeAlertRoot;
class ChargeAlertElement;
// What OverlayController's panel shows accessibility clients in its charging
// role (Sources/OverlayController.swift, ChargeIndicatorView.swift): the
// ChargeIndicatorView as one static text carrying its accessibility label
// ("EndfieldHUD, Battery: 63%, Charging"), and, only while the position
// editor shows them (isHidden = false), the two PositionActionButtons with
// their accessibility labels ("Discard position" / "Confirm position") and
// tool tips as help ("Discard (Esc)" / "Confirm (Return)"). Rects are the
// views' frames in physical virtual-desktop pixels.
struct ChargeAlertAutomationView {
    std::string label;
    core::Rect indicator{};
    bool editing{};
    std::array<std::string, 2> names, help; // discard, confirm
    std::array<core::Rect, 2> buttons{};
    bool shown{}; // the window is ordered in (IsOffscreen otherwise)
    bool operator==(const ChargeAlertAutomationView&) const = default;
};
// The panel's side:
//  view()    the current view, read whenever a client asks (no per-frame work);
//  invoke(b) a button press (0 discard, 1 confirm) queued outside the provider
//            call; false when the editor refuses it.
struct ChargeAlertAutomationHost {
    HWND window{};
    std::function<ChargeAlertAutomationView()> view;
    std::function<bool(std::size_t button)> invoke;
};
struct ChargeAlertAutomationStats {
    std::uint64_t rootRequests{}, structureChanges{}, propertyEvents{}, invokes{}, disconnected{};
};
// UI Automation for the floating alert window: one fragment root hosted by
// the window (WM_GETOBJECT / UiaRootObjectId) with a Text child for the
// indicator and, while editing, two Button children with Invoke, in the
// panel's subview order (indicator, cancel, confirm). Frames and texts are
// read live; changed() runs on the panel's content events (a new label,
// language or the editor opening/closing) and raises StructureChanged and
// Name/HelpText changes only while UiaClientsAreListening(). Calls arrive on
// the panel's UI thread (ProviderOptions_UseComThreading); no COM call lets
// a C++ exception escape.
class ChargeAlertAutomation final {
public:
    explicit ChargeAlertAutomation(ChargeAlertAutomationHost);
    ~ChargeAlertAutomation(); // disconnect()
    ChargeAlertAutomation(const ChargeAlertAutomation&) = delete;
    ChargeAlertAutomation& operator=(const ChargeAlertAutomation&) = delete;
    static bool rootRequest(LPARAM) noexcept;
    // WM_GETOBJECT for UiaRootObjectId: the window's root provider.
    LRESULT answer(WPARAM, LPARAM);
    // Content event: lists or withdraws the buttons and announces changed texts.
    void changed();
    // The root as WM_GETOBJECT returns it (AddRef'd).
    HRESULT root(IRawElementProviderFragmentRoot** out) const;
    std::size_t size() const noexcept; // listed children (1 or 3)
    // The window goes away: releases UIA's window reference and disconnects
    // every provider (later calls answer UIA_E_ELEMENTNOTAVAILABLE).
    void disconnect() noexcept;
    ChargeAlertAutomationStats stats() const noexcept;
    struct State;
private:
    friend class ChargeAlertRoot;
    friend class ChargeAlertElement;
    std::shared_ptr<State> state_;
};
} // namespace endfield::native
#endif
