#pragma once
#include "modules/work_mode_presentation.hpp"
#include "native/profile_automation.hpp"
#ifdef _WIN32
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace endfield::native {
// The open custom duration field's peer id (HUDWorkDurationField).
inline constexpr std::string_view workModeDurationEditorID="work:editor:duration";
// HUDWorkModeInteraction.layoutAccessibility over the Work Mode page state:
// one transparent Button per accessible action (WorkModeCanvas.
// accessibleActions: the configuration while idle, the start/pause/resume
// verb, Reset and the Focus permission action) named by its label. Its help
// is the label for the Focus permission action, otherwise the page status
// followed by ". Space starts, pauses or resumes." in the HUD language.
// Rects are in the 440 x 440 Work Mode canvas.
//
// While the custom duration field is open (editingText: its draft,
// WorkModePreview::editingText()), the source HUDWorkDurationField - a
// projected NSTextView made first responder - follows the actions as an Edit
// over WorkModePresentation::durationEditorRect() (listed last: topmost for
// the hit test). It is named "Custom countdown: minutes and seconds", its
// help is "Enter minutes or min:sec, between 0:01 and 1440:00. Return saves;
// Escape cancels." (both localized like the source L10n.text), its Value is
// the draft and it has the HUD's keyboard focus (NativeProfileAutomation
// announces a newly listed field with AutomationFocusChanged).
//
// The fragments are the projected-control automation of the profile page
// (NativeProfileAutomation) under the Work Mode owner's group:
//  controls()  workModeAutomationControls(preview.state(), language,
//              preview.editingText()) while the page accepts input
//              (WorkModePreview::clientRect(canvas) has a value), else none;
//  toScreen()  WorkModePreview::clientRect(canvas) -> physical screen pixels;
//  invoke()    WorkModePreview::perform(id, time) (activateAction: the open
//              duration field is committed first);
//  setText()   WorkModePreview::setEditingText(utf8, time) on the open field
//              (without it the field's Value is read-only).
std::vector<ProfileAutomationControl>workModeAutomationControls(const modules::WorkModePresentation&,core::Language);
std::vector<ProfileAutomationControl>workModeAutomationControls(const modules::WorkModePresentation&,core::Language,const std::optional<std::string>&editingText);
}
#endif
