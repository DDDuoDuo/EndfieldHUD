#pragma once
// UI Automation peers of the Reader (native/screen_reader_peers.hpp) for the
// Reader owner's group under the HUD window's root provider.
#ifdef _WIN32
#include "native/screen_reader_peers.hpp"
#include "tools/reader_preview.hpp"

namespace endfield::tools {
// Mac ReaderInteraction.layoutAccessibility: one Button per canvas action
// (ReaderAXButton; the source label, ReaderCanvas.accessibilityText as help,
// its enabled state), then the open menu's items (NotesRetainedMenu buttons:
// "Close" for the ×, the item titles otherwise; ids "menu:<item id>"). All of
// it is empty unless the Reader accepts input. A press runs
// ReaderPreview::perform/performMenu at hooks.now(), then hooks.changed():
// an Open > File Explorer press queues the Reader's import action, which the
// host's refresh takes, exactly as after the click. The preview and the
// hooks' window outlive the returned source's NativeScreenReaderPeers.
native::ScreenReaderPeerSource readerAutomation(ReaderPreview&, native::ScreenReaderPeerHooks);
} // namespace endfield::tools
#endif
