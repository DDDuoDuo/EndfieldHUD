#pragma once
// Text patterns and number formats of the redesign: the port of
// Sources/HUDDesignFormat.swift (HUDFormat, HUDValueFormat). LIB-20, LIB-21.
// Pure functions; dates are given as the caller's local calendar fields (the
// Mac formats in the given calendar's time zone; Windows callers convert with
// their existing local-time helpers first). Rounding is Swift .rounded():
// half away from zero (std::round).
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

namespace endfield::modules::design::format {
// 0...9 -> "0n"; negatives and >= 10 unpadded.
std::string pad2(std::int64_t);
// mm:ss, minutes zero-padded and allowed past 59 ("60:00"); negatives clamp to 0.
std::string clock(std::int64_t seconds);
// en-US thousands separators: 3262 -> "3,262".
std::string grouped(std::int64_t);
// "%.1f GB" below 1000, else "%.2f TB" (/1000, decimal): 82.4 GB, 1.86 TB.
std::string storage(double gigabytes);
// yyyy.MM.dd (year unpadded), optionally " HH:mm": 2026.10.07 21:04.
std::string dotted(int year, int month, int day, std::optional<std::pair<int, int>> hourMinute = {});
// Uppercase English weekday (en_US_POSIX), 0 = Sunday: "MONDAY".
std::string weekday(int weekdayIndex);
// "MMMM yyyy" English: "October 2026" (month 1...12).
std::string monthHeading(int year, int month);
}

namespace endfield::modules::design {
// Slider and readout formatters (HUDValueFormat). All on round(value) except
// scale and decimal.
enum class ValueFormat : std::uint8_t { scale, percent, signedPercent, balance, seconds, pixels, page, signed_, integer, clock, decimal };
// scale "%.2f×"; percent "n%" (no space); signedPercent "+n%" for > 0;
// balance |v| < 3 -> "C", negative "L n", positive "R n"; seconds "n s";
// pixels "n px"; page "P. n"; signed "+n" for > 0; integer "n";
// clock format::clock(n); decimal "%.2f" (Media Assembly).
std::string formatValue(ValueFormat, double value);
}
