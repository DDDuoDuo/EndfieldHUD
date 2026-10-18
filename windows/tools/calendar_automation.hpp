#pragma once
// UI Automation peers of the Calendar (native/screen_reader_peers.hpp) for
// the Calendar owner's group under the HUD window's root provider.
#ifdef _WIN32
#include "native/screen_reader_peers.hpp"
#include "tools/calendar_preview.hpp"

namespace endfield::tools {
// Mac HUDCalendarInteraction.layoutAccessibility over CalendarPreview::
// accessibility(): one Button per canvas action (CalendarAXButton: add,
// previous/next month, refresh reminders, each day by its date, each event
// row), then the open event menu's buttons and its three editors
// (CalendarFieldTextView: Edits "field:title", "field:date",
// "field:details" named by their placeholders, the field text as Value, the
// focused one holding keyboard focus). Empty unless the Calendar accepts
// input. A press runs CalendarPreview::perform, a Value write setFieldText
// and SetFocus focusField, each at hooks.now(), then hooks.changed(). The
// preview and the hooks' window outlive the returned source's
// NativeScreenReaderPeers.
native::ScreenReaderPeerSource calendarAutomation(CalendarPreview&, native::ScreenReaderPeerHooks);
// "field:title" / "field:date" / "field:details" -> the editor.
std::optional<modules::CalendarEditorField> calendarAutomationField(std::string_view id) noexcept;
} // namespace endfield::tools
#endif
