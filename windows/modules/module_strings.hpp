#pragma once
#include "core/localization.hpp"
#include "modules/shelf_presentation.hpp"
#include "modules/event_log.hpp"
#include "modules/work_mode_presentation.hpp"
#include "native/clipboard_scene.hpp"

namespace endfield::modules {
// Exact L10n source pairs from the original canvases. The owner resolves the
// System language on a preference event; no OS read, service, or frame work.
FileShelfStrings fileShelfStrings(core::Language);
ShelfPresentationStyle shelfPresentationStyle(core::Language,ShelfPresentationStyle base={});
native::ClipboardStrings clipboardStrings(core::Language);
EventLogStrings eventLogStrings(core::Language);
WorkModeStrings workModeStrings(core::Language);
}
