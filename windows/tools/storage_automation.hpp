#pragma once
// UI Automation peers of the built-in Storage page (native/
// screen_reader_peers.hpp) for its owner's group under the HUD window's root
// provider.
#ifdef _WIN32
#include "native/screen_reader_peers.hpp"
#include "tools/storage_preview.hpp"

namespace endfield::tools {
// Mac HUDTelemetryInteraction.layoutAccessibility over StorageCanvas: the
// status (staticText "storage:status" over the 400 x 334 canvas, label
// StorageCanvas.accessibilityStatus: the volume and its totals, or the
// reading/unavailable message), then one Button per accessible action
// (Storage Settings; Refresh storage unless it is reading). The volume
// chooser (Windows addition) adds its "Choose disk" button and, while its
// menu is open, one Button per listed volume ("Title (D:)").
// StoragePreview::accessibleActions() gives that list; a press runs
// StoragePreview::perform(id) at hooks.now(), then hooks.changed(). Listed
// only while Storage accepts input. The preview and the hooks' window outlive
// the returned source's NativeScreenReaderPeers.
native::ScreenReaderPeerSource storageAutomation(StoragePreview&, native::ScreenReaderPeerHooks);
} // namespace endfield::tools
#endif
