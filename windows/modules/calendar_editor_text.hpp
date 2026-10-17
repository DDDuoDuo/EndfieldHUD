#pragma once
#include "modules/calendar_model.hpp"
#include <string_view>
namespace endfield::modules {
enum class CalendarEditorField {title,date,details};
// HUDCalendarInteraction.textDidChange: Swift Character prefix first, then
// only LF becomes a space for the two single-line fields. Marked text is
// untouched until its native input transaction commits.
std::string calendarEditorText(std::string_view,CalendarEditorField,const CalendarTextRules&,bool marked);
std::u16string calendarEditorUTF16(std::string_view);
std::string calendarEditorUTF8(std::u16string_view);
}
