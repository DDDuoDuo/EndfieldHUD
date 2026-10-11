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
enum class ProfileAutomationRole : std::uint8_t {button,slider,edit};
// One transparent accessibility peer of the Personal Profile page: the Mac
// HUDProfileActionButton (label = action label, help = the page status),
// HUDProfileAccessibilitySlider (label = field title, help = the value
// description, the source range and step) or the projected NSTextView of the
// field being edited (help = the page status, value = its draft text; named
// by the field title, which UIA requires of an Edit). Rects are in the
// 400 x 334 page canvas; ids are the action ids, for sliders the field ids
// (PersonalProfileField.rawValue, e.g. "thumbnailZoom") and for the editor
// "profile:editor:<field id>".
struct ProfileAutomationControl {
    std::string id,label,help;core::Rect rect;ProfileAutomationRole role{ProfileAutomationRole::button};
    double value{},minimum{},maximum{1},step{1}; // slider
    std::string text;                             // edit
    bool operator==(const ProfileAutomationControl&)const=default;
};
// The open projected field (ProfilePreview::editingView).
struct ProfileAutomationEditor {modules::ProfileField field;core::Rect rect;std::string text;};
// HUDPersonalProfileInteraction.layoutAccessibility over the page state:
// every accessible action except the field being edited, the editor itself,
// then every accessible slider.
std::vector<ProfileAutomationControl>profileAutomationControls(const modules::ProfileState&,const std::optional<ProfileAutomationEditor>&editing);
// The host's view of the profile owner (tools::ProfileOwner):
//  controls()  profileAutomationControls(state, page.editingField()) while the
//              page is active (the source interaction's active flag), else none;
//  toScreen()  a canvas rect -> physical screen pixels (ProfileOwner::
//              clientRect, DPI scale, ClientToScreen); none hides it;
//  invoke()    ProfileOwner::perform(id, time) (activateAction: the open
//              field is committed first);
//  setValue()  ProfileOwner::setSlider(field, value, time), refused while an
//              image chooser or the colour picker is open (adjustSlider);
//  setText()   ProfileOwner::setEditingText(text, time) on the open field.
struct ProfileAutomationHost {
    IRawElementProviderFragmentRoot*root{}; // the HUD window's UIA root (borrowed, AddRef'd while connected)
    std::function<std::vector<ProfileAutomationControl>()>controls;
    std::function<std::optional<core::Rect>(core::Rect canvas)>toScreen;
    std::function<bool(std::string_view id)>invoke;
    std::function<bool(std::string_view id,double value)>setValue;
    std::function<bool(std::string_view id,std::string_view utf8)>setText;
};
struct ProfileAutomationStats {std::uint64_t refreshes{},structureChanges{},created{},disconnected{},invokes{},values{},texts{};};
// UI Automation fragments for the projected Personal Profile controls: each
// action is a Button with Invoke, each slider a Slider with RangeValue over
// the source range (SmallChange/LargeChange = the source step) and the open
// field an Edit with a writable Value holding its draft (it has the HUD's
// keyboard focus; a newly listed field raises AutomationFocusChanged, as the
// source NSTextView becomes first responder). A RangeValue write is clamped into the range like the
// source setAccessibilityValue; non-finite values are refused. Buttons and
// sliders are not tab stops: the HUD routes its own keys (Escape, arrows) as
// on the Mac.
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
