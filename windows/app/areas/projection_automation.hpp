#pragma once
// Projection's UI Automation group (Mac ProjectionWorkspaceView: the AX group
// "Projection" holding its ProjectionToolbar buttons, the open retained menu
// and each media card's play and remove buttons). Projection is a built-in
// fullscreen workspace of the Application on the HUD window, not a module
// owner, so this production area lists its controls under the window's one
// UIA root while it is presented (native/notes_automation.hpp peers over
// tools::ProjectionPreview::automationElements/perform).
//
// The host publishes the workspace in the area directory when it creates it
// and withdraws it before destroying it:
//   host.publish(endfield::notes::projectionWorkspaceService, tools::ProjectionWorkspace&)
// and marks UIA content changed on Projection input, show and close. Without
// a presented workspace the group is empty. An accepted invoke posts this
// owner's private message, so the host refreshes the tree and presents.
// Hidden coverage drives the same peers over a synthetic ProjectionPreview.
#include "app/application.hpp"
#include <memory>
#include <string_view>

namespace endfield::notes {
inline constexpr std::string_view projectionWorkspaceService{"projection.workspace"};
std::unique_ptr<app::ModuleOwner> projectionAutomationArea(const app::ApplicationModuleContext&);
}
