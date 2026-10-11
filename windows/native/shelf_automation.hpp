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
class ShelfAutomationElement;
// One transparent accessibility peer of the Temporary File Shelf: the Mac
// HUDShelfActionButton that HUDFileShelfInteraction.layoutAccessibility keeps
// for every FileShelfCanvas.accessibleActions entry (toolbar buttons, each
// visible card's selection plate and its preview/reveal/remove buttons).
// label = the action label, help = the canvas status (an error) or the
// keyboard help, value = "Selected" on a selected card's plate (otherwise
// empty; the Mac sets it on plates only). Rects are in the 400 x 334 Shelf
// canvas; ids are the FileShelfState action ids ("shelf:add",
// "shelf:<id>:select", ...).
struct ShelfAutomationControl {
    std::string id,label,help,value;core::Rect rect;bool plate{};
    bool operator==(const ShelfAutomationControl&)const=default;
};
// The owner's view (tools::ShelfPreview):
//  controls()  the peers while the Shelf accepts input, otherwise none;
//  toScreen()  a canvas rect -> physical screen pixels through the current
//              module projection; none hides it (offscreen);
//  invoke()    ShelfPreview::perform(id) (activateAction).
struct ShelfAutomationHost {
    IRawElementProviderFragmentRoot*root{}; // the owner's UIA group (borrowed, AddRef'd while held)
    std::function<std::vector<ShelfAutomationControl>()>controls;
    std::function<std::optional<core::Rect>(core::Rect canvas)>toScreen;
    std::function<bool(std::string_view id)>invoke;
};
struct ShelfAutomationStats {std::uint64_t refreshes{},structureChanges{},created{},disconnected{},invokes{},propertyEvents{};};
// UI Automation fragments under the HUD window's root provider
// (app/hud_automation.hpp): each control is a Button with Invoke; card plates
// add a read-only Value carrying "Selected". Buttons are not tab stops: the HUD
// routes its own keys (arrows, Space, Delete, Ctrl+R) as on the Mac. Elements
// keep their COM identity per id across refreshes; vanished ones are
// disconnected and answer UIA_E_ELEMENTNOTAVAILABLE. refresh() runs on content
// events only (selection, scrolling, imports, opening/closing), never per
// frame, and raises Name/HelpText/Value changes for listening clients. Calls
// arrive on the UI thread (ProviderOptions_UseComThreading).
class NativeShelfAutomation final {
public:
    explicit NativeShelfAutomation(ShelfAutomationHost);
    ~NativeShelfAutomation(); // disconnect(), then releases the root
    NativeShelfAutomation(const NativeShelfAutomation&)=delete;
    NativeShelfAutomation&operator=(const NativeShelfAutomation&)=delete;
    // True when the set or order changed.
    bool refresh();
    std::size_t size()const noexcept;
    HRESULT child(std::size_t index,IRawElementProviderFragment**out)const;
    // Screen-pixel hit test (topmost = last listed control: card buttons
    // above their plate, as the canvas hit-tests them).
    HRESULT fromPoint(double screenX,double screenY,IRawElementProviderFragment**out)const;
    void disconnect()noexcept;
    ShelfAutomationStats stats()const noexcept;
    struct State;
private:friend class ShelfAutomationElement;std::shared_ptr<State>state_;
};
}
#endif
