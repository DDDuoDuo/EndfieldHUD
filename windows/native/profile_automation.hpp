#pragma once
#include "modules/profile_state.hpp"
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
class ProfileAutomationElement;
// One transparent accessibility peer of the Personal Profile page: the Mac
// HUDProfileActionButton (label = action label, help = the page status) or
// HUDProfileAccessibilitySlider (label = field title, help = the value
// description, the source range and step). Rects are in the 400 x 334 page
// canvas; ids are the action ids and, for sliders, the field ids
// (PersonalProfileField.rawValue, e.g. "thumbnailZoom").
struct ProfileAutomationControl {
    std::string id,label,help;core::Rect rect;bool slider{};
    double value{},minimum{},maximum{1},step{1};
    bool operator==(const ProfileAutomationControl&)const=default;
};
// HUDPersonalProfileInteraction.layoutAccessibility over the page state:
// every accessible action except the field being edited (its projected
// editor is the peer then), followed by every accessible slider.
std::vector<ProfileAutomationControl>profileAutomationControls(const modules::ProfileState&,std::optional<modules::ProfileField>editing);
// The host's view of the profile owner (tools::ProfileOwner):
//  controls()  profileAutomationControls(state, page.editingField()) while the
//              page is active (the source interaction's active flag), else none;
//  toScreen()  a canvas rect -> physical screen pixels (ProfileOwner::
//              clientRect, DPI scale, ClientToScreen); none hides it;
//  invoke()    ProfileOwner::perform(id, time) (activateAction: the open
//              field is committed first);
//  setValue()  ProfileOwner::setSlider(field, value, time), refused while an
//              image chooser or the colour picker is open (adjustSlider).
struct ProfileAutomationHost {
    IRawElementProviderFragmentRoot*root{}; // the HUD window's UIA root (borrowed, AddRef'd while connected)
    std::function<std::vector<ProfileAutomationControl>()>controls;
    std::function<std::optional<core::Rect>(core::Rect canvas)>toScreen;
    std::function<bool(std::string_view id)>invoke;
    std::function<bool(std::string_view id,double value)>setValue;
};
struct ProfileAutomationStats {std::uint64_t refreshes{},structureChanges{},created{},disconnected{},invokes{},values{};};
// UI Automation fragments for the projected Personal Profile controls: each
// action is a Button with Invoke, each slider a Slider with RangeValue over
// the source range (SmallChange/LargeChange = the source step). A RangeValue
// write is clamped into the range like the source setAccessibilityValue;
// non-finite values are refused. Projected controls are not tab stops: the
// HUD routes its own keys (Escape, arrows) as on the Mac.
//
// The HUD window's root provider owns the tree: it lists child(i) under the
// profile owner's group, routes ElementProviderFromPoint to fromPoint and
// raises StructureChanged when refresh() returns true. Elements keep their
// COM identity per id across refreshes; vanished ones are disconnected and
// answer UIA_E_ELEMENTNOTAVAILABLE. Calls arrive on the UI thread
// (ProviderOptions_UseComThreading). Refresh on content events only, never per
// frame; it allocates only while rebuilding the element list.
class NativeProfileAutomation final {
public:
    explicit NativeProfileAutomation(ProfileAutomationHost);
    ~NativeProfileAutomation();
    NativeProfileAutomation(const NativeProfileAutomation&)=delete;
    NativeProfileAutomation&operator=(const NativeProfileAutomation&)=delete;
    // Re-reads the controls; raises name/help/value property events for
    // listening clients. True when the set or order changed.
    bool refresh();
    std::size_t size()const noexcept;
    HRESULT child(std::size_t index,IRawElementProviderFragment**out)const;
    // Screen-pixel hit test (topmost = last listed control).
    HRESULT fromPoint(double screenX,double screenY,IRawElementProviderFragment**out)const;
    // Disconnects every element (page hidden or the owner torn down).
    void disconnect()noexcept;
    ProfileAutomationStats stats()const noexcept;
    struct State;
private:friend class ProfileAutomationElement;std::shared_ptr<State>state_;
};
}
#endif
