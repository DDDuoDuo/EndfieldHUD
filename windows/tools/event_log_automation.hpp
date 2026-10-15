#pragma once
// UI Automation peers of the built-in Event Log (native/screen_reader_peers.hpp)
// for its owner's group under the HUD window's root provider.
#ifdef _WIN32
#include "native/screen_reader_peers.hpp"
#include "tools/event_log_preview.hpp"

namespace endfield::tools {
// Mac HUDEventLogInteraction.layoutAccessibility over EventLogState::actions()
// (EventLogCanvas.accessibleActions): one Button per category control, Clear
// (or the clear confirmation) and visible row ("time. category. title.
// detail"), with the help "<status>. Arrow keys select events; Page Up and
// Page Down scroll." (English in the source too), listed only while the Event
// Log accepts input. A press runs EventLogPreview::perform(id) (canvas.perform
// (actionID:)) at hooks.now(), then hooks.changed(). The preview and the
// hooks' window outlive the returned source's NativeScreenReaderPeers.
native::ScreenReaderPeerSource eventLogAutomation(EventLogPreview&, native::ScreenReaderPeerHooks);
} // namespace endfield::tools
#endif
