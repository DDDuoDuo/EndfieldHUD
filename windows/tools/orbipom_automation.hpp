#pragma once
// UI Automation peers of the minigame (native/screen_reader_peers.hpp) for
// the minigame owner's group under the HUD window's root provider.
#ifdef _WIN32
#include "native/screen_reader_peers.hpp"
#include "tools/orbipom_preview.hpp"

namespace endfield::tools {
// Mac HUDOrbiPomInteraction.layoutAccessibility over modules::
// orbiPomAccessibility: one Button per canvas action (OrbiPomAXButton: Start
// or Play again, Pause / resume, Restart, Cancel, Confirm, Rules and the
// skills with their cost or charge, enabled state). While the rules are open
// the canvas buttons are hidden and the OrbiPomRulesMenu is listed instead:
// a Group "rules:menu" (label Rules, the nine paragraphs as read-only Value) and
// its Close button "rules:close". Empty unless the game accepts input. A
// press runs OrbiPomPreview::perform(action) or closeRules at hooks.now(),
// then hooks.changed(). The preview and the hooks' window outlive the
// returned source's NativeScreenReaderPeers.
native::ScreenReaderPeerSource orbiPomAutomation(OrbiPomPreview&, native::ScreenReaderPeerHooks);
} // namespace endfield::tools
#endif
