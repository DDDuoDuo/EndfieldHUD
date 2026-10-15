#pragma once
// UI Automation peers of the built-in Activity Monitor (native/
// screen_reader_peers.hpp) for its owner's group under the HUD window's root
// provider.
#ifdef _WIN32
#include "native/screen_reader_peers.hpp"
#include "tools/activity_preview.hpp"

namespace endfield::tools {
// Mac HUDTelemetryInteraction.layoutAccessibility over ActivityMonitorCanvas:
// the status (staticText "activity:status" over the 400 x 334 canvas, label
// modules::activityAccessibilityStatus), then one Button per accessible action
// (the Overview and Apps tabs; on the Apps page the six column headers "Sort
// by <column>[, ascending|, descending]", GPU included), listed only while
// Activity Monitor accepts input. A press runs ActivityPreview::perform(id)
// (canvas.perform(actionID:)) at hooks.now(), then hooks.changed(). The
// preview and the hooks' window outlive the returned source's
// NativeScreenReaderPeers.
native::ScreenReaderPeerSource activityAutomation(ActivityPreview&, native::ScreenReaderPeerHooks);
} // namespace endfield::tools
#endif
