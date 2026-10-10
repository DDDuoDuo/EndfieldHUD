#pragma once
#include "modules/now_playing_accessibility.hpp"
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
class NowPlayingAutomationElement;
// The host's view of the Now Playing owner (tools::NowPlayingPreview):
//  elements()  preview.accessibility(time) while Now Playing accepts input,
//              otherwise an empty value;
//  toScreen()  a 440x440 canvas rect -> physical screen pixels (preview.project
//              of its corners, DPI scale, ClientToScreen); none hides it;
//  invoke()    preview.perform(action for id, time);
//  setValue()  preview.setSlider(kind for id, value, time).
struct NowPlayingAutomationHost {
    IRawElementProviderFragmentRoot*root{}; // the HUD window's UIA root (borrowed, AddRef'd while connected)
    std::function<modules::NowPlayingAccessibility()>elements;
    std::function<std::optional<core::Rect>(core::Rect canvas)>toScreen;
    std::function<bool(std::string_view id)>invoke;
    std::function<bool(std::string_view id,double value)>setValue;
};
struct NowPlayingAutomationStats {std::uint64_t refreshes{},structureChanges{},created{},disconnected{},invokes{},values{};};
// UI Automation fragments for the projected Now Playing controls (source
// HUDNowPlayingInteraction.layoutAccessibility): the five buttons (Lyrics,
// Previous track, Play/Pause, Next track, App volume) expose Invoke, with the
// accessible status ("title · artist · album" or "No music playing") as help;
// "Playback position" and, while the menu is open, "Playing app volume"
// expose RangeValue over the source range with the source increments (5 s,
// 2%) and a read-only Value pattern with the source text ("1:23", "40%",
// "Unavailable"). Seeking commits once per SetValue, like the source's
// non-continuous seek slider.
//
// The HUD window's root provider owns the tree: it lists child(i) among its
// children, routes ElementProviderFromPoint to fromPoint, and raises
// StructureChanged on itself when refresh() returns true. Elements keep their
// COM identity per id across refreshes; vanished ones are disconnected and
// answer UIA_E_ELEMENTNOTAVAILABLE. Calls arrive on the UI thread
// (ProviderOptions_UseComThreading). Refresh only on content events, never per
// frame; it allocates only while rebuilding the element list.
class NativeNowPlayingAutomation final {
public:
    explicit NativeNowPlayingAutomation(NowPlayingAutomationHost);
    ~NativeNowPlayingAutomation();
    NativeNowPlayingAutomation(const NativeNowPlayingAutomation&)=delete;
    NativeNowPlayingAutomation&operator=(const NativeNowPlayingAutomation&)=delete;
    // Re-reads the owner's elements; raises value/name/help/enabled property
    // events for listening clients. True when the set or order changed.
    bool refresh();
    std::size_t size()const noexcept;
    HRESULT child(std::size_t index,IRawElementProviderFragment**out)const;
    // Screen-pixel hit test (topmost = last listed element).
    HRESULT fromPoint(double screenX,double screenY,IRawElementProviderFragment**out)const;
    // Disconnects every element (Now Playing hidden or the owner torn down).
    void disconnect()noexcept;
    NowPlayingAutomationStats stats()const noexcept;
    struct State;
private:friend class NowPlayingAutomationElement;std::shared_ptr<State>state_;
};
}
#endif
