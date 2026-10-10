#pragma once
#include "modules/profile_state.hpp"
namespace endfield::native {
modules::ProfileTextRules nativeProfileTextRules();
// Snapshot an explicit IANA zone (empty means current native zone). The caller
// replaces this immutable rule snapshot on WM_TIMECHANGE/WM_SETTINGCHANGE.
// This helper owns neither an OS service nor a clock/window/worker.
modules::ProfileDateRules nativeProfileDateRules(std::u16string timeZone={});
std::array<std::uint8_t,4>profileUnicodeVersion()noexcept;
}
