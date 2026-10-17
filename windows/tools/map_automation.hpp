#pragma once
// UI Automation peers of the Map (native/screen_reader_peers.hpp) for the
// Map owner's group under the HUD window's root provider.
#ifdef _WIN32
#include "native/screen_reader_peers.hpp"
#include "tools/map_preview.hpp"

namespace endfield::tools {
// Mac HUDWorldMapInteraction.layoutAccessibility over MapPreview::
// accessibility(): the status (staticText "map:status" over the 440 x 440
// canvas, label WorldMapCanvas.accessibilityStatus), then one Button per
// accessible action (zoom in/out, reset, place pin, remove the selected pin,
// every visible pin), each with the shared help. During a gesture the peers
// follow the published snapshot at most 12 times per second, as on the Mac.
// A press runs MapPreview::perform(id) (activateAction) at hooks.now(), then
// hooks.changed(). The preview and the hooks' window outlive the returned
// source's NativeScreenReaderPeers.
native::ScreenReaderPeerSource mapAutomation(MapPreview&, native::ScreenReaderPeerHooks);
} // namespace endfield::tools
#endif
