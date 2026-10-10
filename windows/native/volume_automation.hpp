#pragma once
#include "native/volume_accessibility.hpp"
#ifdef _WIN32
#include <functional>
#include <memory>
#include <optional>
#include <string_view>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
struct IRawElementProviderFragment;struct IRawElementProviderFragmentRoot;

namespace endfield::native {
class VolumeAutomationElement;
// The host's view of the Volume owner (tools::VolumePreview):
//  elements()  volumeAccessibility(preview.state(), volumeAccessibilityStrings(language))
//              while preview.acceptsInput(), otherwise an empty value;
//  toScreen()  a 400x334 canvas rect -> physical screen pixels
//              (preview.clientRect, DPI scale, ClientToScreen); none hides it;
//  invoke()    preview.perform(id, time);
//  setValue()  preview.setAccessibleValue(id, value, time).
struct VolumeAutomationHost {
    IRawElementProviderFragmentRoot*root{}; // the HUD window's UIA root (borrowed, AddRef'd while connected)
    std::function<VolumeAccessibility()>elements;
    std::function<std::optional<core::Rect>(core::Rect canvas)>toScreen;
    std::function<bool(std::string_view id)>invoke;
    std::function<bool(std::string_view id,double value)>setValue;
};
struct VolumeAutomationStats {std::uint64_t refreshes{},structureChanges{},created{},disconnected{},invokes{},values{};};
// UI Automation fragments for the projected Volume controls (source
// HUDVolumeInteraction.layoutAccessibility): one button per source action
// (Choose output/input device, Mute/Unmute, Headphones / Bluetooth, App
// volume, Back, page arrows, device rows) exposing Invoke, and one slider per
// source slider (Output volume, Left/right balance, "<app> · PID n volume")
// exposing RangeValue over the source range with the source 2% increment and
// a read-only Value pattern carrying the source value text ("45%", "L 25%",
// "Centered", "Unavailable"). Help text is the source accessibilityHelp.
//
// The HUD window's root provider owns the tree: it lists child(i) among its
// children, routes ElementProviderFromPoint to fromPoint, and raises
// StructureChanged on itself when refresh() returns true. Elements keep their
// COM identity per id across refreshes; vanished ones are disconnected and
// answer UIA_E_ELEMENTNOTAVAILABLE. Calls arrive on the UI thread
// (ProviderOptions_UseComThreading). Refresh only on content events (the
// controller's contentRevision, snapshot, pose or language changes), never per
// frame; it allocates only while rebuilding the element list.
class NativeVolumeAutomation final {
public:
    explicit NativeVolumeAutomation(VolumeAutomationHost);
    ~NativeVolumeAutomation();
    NativeVolumeAutomation(const NativeVolumeAutomation&)=delete;
    NativeVolumeAutomation&operator=(const NativeVolumeAutomation&)=delete;
    // Re-reads the owner's elements; raises value/name/help/enabled property
    // events for listening clients. True when the set or order changed.
    bool refresh();
    std::size_t size()const noexcept;
    HRESULT child(std::size_t index,IRawElementProviderFragment**out)const;
    // Screen-pixel hit test (topmost = last listed element).
    HRESULT fromPoint(double screenX,double screenY,IRawElementProviderFragment**out)const;
    // Disconnects every element (Volume hidden or the owner torn down).
    void disconnect()noexcept;
    VolumeAutomationStats stats()const noexcept;
    struct State;
private:friend class VolumeAutomationElement;std::shared_ptr<State>state_;
};
}
#endif
