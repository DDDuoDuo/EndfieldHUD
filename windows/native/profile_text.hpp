#pragma once
#include "modules/profile_state.hpp"
namespace endfield::native {
modules::ProfileTextRules nativeProfileTextRules();
// String.count / String(prefix(n)) (extended grapheme clusters, ICU's
// character break iterator) on UTF-8 of any length up to ICU's 2 GiB offset
// range: the design library's CharacterRules (native/hud_design_text_metrics.cpp).
// The Profile rules above run the same segmentation behind their own 32 KiB
// payload bound. Invalid UTF-8 throws.
std::size_t graphemeCount(std::string_view utf8);
std::string_view graphemePrefix(std::string_view utf8,std::size_t count);
// Snapshot an explicit IANA zone (empty means current native zone). The caller
// replaces this immutable rule snapshot on WM_TIMECHANGE/WM_SETTINGCHANGE.
// This helper owns neither an OS service nor a clock/window/worker.
modules::ProfileDateRules nativeProfileDateRules(std::u16string timeZone={});
std::array<std::uint8_t,4>profileUnicodeVersion()noexcept;
}
