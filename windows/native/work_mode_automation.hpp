#pragma once
#include "modules/work_mode_presentation.hpp"
#include "native/profile_automation.hpp"
#ifdef _WIN32
#include <vector>

namespace endfield::native {
// HUDWorkModeInteraction.layoutAccessibility over the Work Mode page state:
// one transparent Button per accessible action (WorkModeCanvas.
// accessibleActions: the configuration while idle, the start/pause/resume
// verb, Reset and the Focus permission action) named by its label. Its help
// is the label for the Focus permission action, otherwise the page status
// followed by ". Space starts, pauses or resumes." in the HUD language.
// Rects are in the 440 x 440 Work Mode canvas.
//
// The fragments are the projected-control automation of the profile page
// (NativeProfileAutomation) under the Work Mode owner's group:
//  controls()  workModeAutomationControls(preview.state(), language) while the
//              page accepts input (WorkModePreview::clientRect(canvas) has a
//              value), otherwise none;
//  toScreen()  WorkModePreview::clientRect(canvas) -> physical screen pixels;
//  invoke()    WorkModePreview::perform(id, time) (activateAction: the open
//              duration field is committed first).
std::vector<ProfileAutomationControl>workModeAutomationControls(const modules::WorkModePresentation&,core::Language);
}
#endif
