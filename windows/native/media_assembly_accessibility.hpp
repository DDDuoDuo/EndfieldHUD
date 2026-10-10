#pragma once
#include "modules/media_assembly_presentation.hpp"
#ifdef _WIN32
#include <functional>
#include <memory>
#include <string_view>
#include <vector>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
struct IRawElementProviderFragment;struct IRawElementProviderFragmentRoot;

namespace endfield::native {
class MediaAssemblyAccessibleElement;
// The host's view of the Media Assembly owner (tools::MediaAssemblyPreview):
// elements() is owner.accessibility() (source AX elements with rects already
// projected into logical client points), invoke() is owner.perform(id, time)
// and setValue() is owner.setAccessibleValue(id, value, time). toScreen maps a
// logical client rect to physical screen pixels (DPI scale + ClientToScreen).
struct MediaAssemblyAccessibilityHost {
    IRawElementProviderFragmentRoot*root{}; // the HUD window's UIA root (borrowed, AddRef'd while connected)
    std::function<std::vector<modules::MediaAssemblyAccessible>()>elements;
    std::function<bool(std::string_view id)>invoke;
    std::function<bool(std::string_view id,double value)>setValue;
    std::function<core::Rect(core::Rect logicalClient)>toScreen;
};
struct MediaAssemblyAccessibilityStats {std::uint64_t refreshes{},structureChanges{},created{},disconnected{},invokes{},values{};};
// UI Automation fragments for the projected Media Assembly controls (source
// HUDMediaAssemblyInteraction.layoutAccessibility: one projected AX button per
// action, "Close" for the x, the "Playback position" slider (+/-5 s), sticker
// X/Y/Size/Rotate, inline parameter, trim and crop-edge sliders). Buttons
// expose Invoke; sliders expose RangeValue with the source range and step.
//
// The HUD window's root provider owns the tree: it lists child(i) among its
// children, routes ElementProviderFromPoint to fromPoint, and raises
// StructureChanged on itself when refresh() returns true. Elements keep their
// COM identity per id across refreshes; vanished ones are disconnected and
// answer UIA_E_ELEMENTNOTAVAILABLE. Calls arrive on the UI thread
// (ProviderOptions_UseComThreading); refresh only on content/pose events,
// never per frame.
class NativeMediaAssemblyAccessibility final {
public:
    explicit NativeMediaAssemblyAccessibility(MediaAssemblyAccessibilityHost);
    ~NativeMediaAssemblyAccessibility();
    NativeMediaAssemblyAccessibility(const NativeMediaAssemblyAccessibility&)=delete;
    NativeMediaAssemblyAccessibility&operator=(const NativeMediaAssemblyAccessibility&)=delete;
    // Re-reads the owner's elements; raises value/name/enabled property events
    // for listening clients. True when the set or order of elements changed.
    bool refresh();
    std::size_t size()const noexcept;
    HRESULT child(std::size_t index,IRawElementProviderFragment**out)const;
    // Screen-pixel hit test (topmost = last listed element).
    HRESULT fromPoint(double screenX,double screenY,IRawElementProviderFragment**out)const;
    // Disconnects every element (owner hidden or torn down).
    void disconnect()noexcept;
    MediaAssemblyAccessibilityStats stats()const noexcept;
    struct State;
private:friend class MediaAssemblyAccessibleElement;std::shared_ptr<State>state_;
};
}
#endif
