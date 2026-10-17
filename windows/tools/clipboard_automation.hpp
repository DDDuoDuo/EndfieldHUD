#pragma once
// UI Automation peers of the built-in Clipboard (native/screen_reader_peers.hpp)
// for its owner's group under the HUD window's root provider.
#ifdef _WIN32
#include "native/screen_reader_peers.hpp"
#include "tools/clipboard_preview.hpp"

namespace endfield::tools {
// Mac HUDClipboardInteraction.layoutAccessibility over ClipboardState::
// actions() (ClipboardCanvas.accessibleActions): one Button per toolbar
// action and per visible row (copy, pin/unpin, remove) with its source label
// and the help "<status>. Arrow keys select; Return copies; Delete removes the
// cached item." in the HUD language, listed only while the Clipboard accepts
// input. A press runs ClipboardPreview::perform(id) (activateAction:
// canvas.perform(actionID:)) at hooks.now(), then hooks.changed(). The preview
// and the hooks' window outlive the returned source's NativeScreenReaderPeers.
native::ScreenReaderPeerSource clipboardAutomation(ClipboardPreview&, native::ScreenReaderPeerHooks);
} // namespace endfield::tools
#endif
