#pragma once
// UI Automation peers of the projected Notes, Archive and Projection controls.
//
// The Mac exposes these controls as transparent AppKit accessibility views
// laid over the projected HUD plane: HUDNotesActionButton (NotesCanvas.
// accessibleActions), HUDNotesSeekSlider (mediaSeekActions), the projected
// NSTextView of the open field (HUDProjectedTextEditor), the NotesRetainedMenu
// buttons (formatting, media source, shelf), ArchiveActionButton
// (ArchiveCanvas.actions) with the ArchiveGalleryScroller, and the Projection
// toolbar/menu/media buttons. On Windows each owner lists the same controls
// (ids, labels, enabled state) with their projected rectangle in physical
// client pixels; this provider turns them into UIA fragments under the owner's
// group of the HUD window's one root provider (app/hud_automation.hpp).
#include "core/scene.hpp"
#ifdef _WIN32
#include <cmath>
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
class NotesAutomationPeer;
enum class NotesAutomationRole : std::uint8_t {button,slider,edit};
// One projected control. Buttons invoke the owner's source action id; a
// slider is the source NSSlider/AX slider (value in its own units, small and
// large change = the source increment); an edit is a projected field (text =
// its unsaved draft; `selected` = it holds the HUD's keyboard focus). For a
// button `selected` is the source accent plate (chosen row, active toggle).
struct NotesAutomationElement {
    std::string id,label,help;
    core::Rect rect; // physical client pixels: bounds of the projected control
    NotesAutomationRole role{NotesAutomationRole::button};
    bool enabled{true},selected{};
    double value{},minimum{},maximum{1},smallChange{1},largeChange{1};
    std::string text;
    bool operator==(const NotesAutomationElement&)const=default;
};
// Physical client rectangle bounding a projected local rectangle (all four
// corners through `project`); none when a corner does not project or the
// result is empty/non-finite. Shared by the three owners.
template<class Project>
std::optional<core::Rect> notesAutomationBounds(core::Rect local,const Project& project){
    const core::Point corners[4]{{local.x,local.y},{local.x+local.width,local.y},{local.x+local.width,local.y+local.height},{local.x,local.y+local.height}};
    double left{},top{},right{},bottom{};
    for(int n=0;n<4;++n){const std::optional<core::Point> p=project(corners[n]);if(!p||!std::isfinite(p->x)||!std::isfinite(p->y))return std::nullopt;
        if(n==0){left=right=p->x;top=bottom=p->y;}else{left=p->x<left?p->x:left;right=p->x>right?p->x:right;top=p->y<top?p->y:top;bottom=p->y>bottom?p->y:bottom;}}
    const core::Rect out{left,top,right-left,bottom-top};
    if(!(out.width>0&&out.height>0)||!std::isfinite(out.width)||!std::isfinite(out.height))return std::nullopt;
    return out;
}
// The owner's view for the provider:
//  elements()  the current controls (empty while the owner accepts no input);
//  origin()    screen position of the client origin in physical pixels (the
//              HUD HWND's ClientToScreen); none marks every peer offscreen;
//  invoke()    the owner's source action for a button id;
//  setValue()  a slider write, already clamped into its range;
//  setText()   replaces the open field's draft.
struct NotesAutomationHost {
    IRawElementProviderFragmentRoot*root{}; // owner group (borrowed; AddRef'd while alive)
    std::function<void(std::vector<NotesAutomationElement>&)>elements;
    std::function<std::optional<core::Point>()>origin;
    std::function<bool(std::string_view id)>invoke;
    std::function<bool(std::string_view id,double value)>setValue;
    std::function<bool(std::string_view id,std::string_view utf8)>setText;
};
// ClientToScreen of the window's client origin; none for a destroyed window.
std::function<std::optional<core::Point>()> notesAutomationWindowOrigin(HWND);
struct NotesAutomationStats {std::uint64_t refreshes{},structureChanges{},created{},disconnected{},invokes{},values{},texts{},focusEvents{};};
// UIA fragments for one owner's projected controls: a Button with Invoke, a
// Slider with RangeValue (writes are clamped like setAccessibilityValue;
// non-finite values are refused) or an Edit with a writable Value. Disabled
// controls report IsEnabled false and refuse Invoke (UIA_E_ELEMENTNOTENABLED).
// Elements keep their COM identity per (id, role) across refreshes; vanished
// ones are disconnected and answer UIA_E_ELEMENTNOTAVAILABLE. A newly listed
// focused edit raises AutomationFocusChanged (the source NSTextView becomes
// first responder). refresh() runs on content events only, never per frame; it
// allocates only while rebuilding the list. UI thread only
// (ProviderOptions_UseComThreading).
class NativeNotesAutomation final {
public:
    explicit NativeNotesAutomation(NotesAutomationHost);
    ~NativeNotesAutomation();
    NativeNotesAutomation(const NativeNotesAutomation&)=delete;
    NativeNotesAutomation&operator=(const NativeNotesAutomation&)=delete;
    // Re-reads the controls; raises property events for listening clients.
    // True when the set or order changed (the host raises StructureChanged).
    bool refresh();
    std::size_t size()const noexcept;
    const NotesAutomationElement* element(std::size_t)const noexcept; // listed control (tests, coverage)
    HRESULT child(std::size_t index,IRawElementProviderFragment**out)const;
    // Screen-pixel hit test; the last listed control is topmost.
    HRESULT fromPoint(double screenX,double screenY,IRawElementProviderFragment**out)const;
    void disconnect()noexcept;
    NotesAutomationStats stats()const noexcept;
    struct State;
private:friend class NotesAutomationPeer;std::shared_ptr<State>state_;
};
}
#endif
