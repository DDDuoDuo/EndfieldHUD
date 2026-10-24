// Text patterns and number formats (Sources/HUDDesignFormat.swift). printf
// style formats go through std::to_chars (the POSIX "C" form String(format:)
// produces, never the process locale).
#include "modules/hud_design_format.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <stdexcept>

namespace endfield::modules::design {
namespace {
std::string fixed(double v, int digits) {
    char chars[64];
    const auto r = std::to_chars(chars, chars + sizeof chars, v, std::chars_format::fixed, digits);
    if (r.ec != std::errc{}) throw std::invalid_argument("Design value formatting failed");
    return {chars, r.ptr};
}
// Int(value.rounded()): half away from zero. Swift traps on a non-finite or
// out-of-range value; here those clamp (NaN -> 0) so a bad reading never throws.
std::int64_t rounded(double v) noexcept {
    if (std::isnan(v)) return 0;
    const double r = std::round(v);
    if (r >= 9.2e18) return std::numeric_limits<std::int64_t>::max();
    if (r <= -9.2e18) return std::numeric_limits<std::int64_t>::min();
    return std::int64_t(r);
}
}

namespace format {
// HUDFormat.pad2 (:8).
std::string pad2(std::int64_t v) { return v < 10 && v >= 0 ? "0" + std::to_string(v) : std::to_string(v); }
// HUDFormat.clock (:11-14).
std::string clock(std::int64_t seconds) { const auto total = std::max<std::int64_t>(0, seconds); return pad2(total / 60) + ":" + pad2(total % 60); }
// HUDFormat.grouped (:17-21): en_US NumberFormatter .decimal.
std::string grouped(std::int64_t v) {
    const auto digits = std::to_string(v);
    const std::size_t sign = v < 0 ? 1 : 0;
    std::string out = digits.substr(0, sign);
    for (std::size_t i = sign; i < digits.size(); ++i) {
        if (i > sign && (digits.size() - i) % 3 == 0) out.push_back(',');
        out.push_back(digits[i]);
    }
    return out;
}
// HUDFormat.storage (:24-26).
std::string storage(double gigabytes) { return gigabytes >= 1000 ? fixed(gigabytes / 1000, 2) + " TB" : fixed(gigabytes, 1) + " GB"; }
// ByteCountFormatter.string(fromByteCount:countStyle: .file), adaptive, en_US
// (ClipboardStore meta lines, ShelfItem.sizeLabel): 1000-based; bytes as
// words, KB without fraction digits, MB with 1, GB and larger with 2; trailing
// fraction zeros dropped; the shown digit rounds half up (1500 -> 2 KB, 2500
// -> 3 KB; Mac oracle clipboardFormat.fileSizes), and a value that rounds to
// 1000 moves to the next unit (999,500 -> 1 MB).
std::string fileSize(std::int64_t bytes) {
    if (bytes == 0) return "Zero KB";
    if (bytes == 1) return "1 byte";
    if (bytes > -1000 && bytes < 1000) return std::to_string(bytes) + " bytes";
    static constexpr std::array<const char*, 7> units{"KB", "MB", "GB", "TB", "PB", "EB", "ZB"};
    static constexpr std::array<int, 7> digits{0, 1, 2, 2, 2, 2, 2};
    double value = double(bytes) / 1000;
    std::size_t unit = 0;
    while (unit + 1 < units.size() && std::abs(value) >= 1000) { value /= 1000; ++unit; }
    const double scale = std::pow(10.0, digits[unit]);
    double shown = std::round(value * scale) / scale;
    if (std::abs(shown) >= 1000 && unit + 1 < units.size()) { value /= 1000; ++unit; shown = std::round(value * std::pow(10.0, digits[unit])) / std::pow(10.0, digits[unit]); }
    std::string out = fixed(shown, digits[unit]);
    if (out.find('.') != std::string::npos) {
        while (out.back() == '0') out.pop_back();
        if (out.back() == '.') out.pop_back();
    }
    return out + " " + units[unit];
}
// HUDFormat.dotted (:29-33): the year unpadded ("\(c.year)").
std::string dotted(int year, int month, int day, std::optional<std::pair<int, int>> hourMinute) {
    auto out = std::to_string(year) + "." + pad2(month) + "." + pad2(day);
    if (hourMinute) out += " " + pad2(hourMinute->first) + ":" + pad2(hourMinute->second);
    return out;
}
// HUDFormat.weekday (:36-40): "EEEE" in en_US_POSIX, uppercased.
std::string weekday(int index) {
    static constexpr std::array<const char*, 7> names{"SUNDAY", "MONDAY", "TUESDAY", "WEDNESDAY", "THURSDAY", "FRIDAY", "SATURDAY"};
    if (index < 0 || index > 6) throw std::invalid_argument("Weekday index must be 0...6");
    return names[std::size_t(index)];
}
// HUDFormat.monthHeading (:43-47): "MMMM yyyy" (yyyy pads to four digits).
std::string monthHeading(int year, int month) {
    static constexpr std::array<const char*, 12> names{"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"};
    if (month < 1 || month > 12) throw std::invalid_argument("Month must be 1...12");
    auto digits = std::to_string(std::abs(year));
    if (digits.size() < 4) digits.insert(0, 4 - digits.size(), '0');
    return std::string(names[std::size_t(month - 1)]) + " " + (year < 0 ? "-" : "") + digits;
}
}

// HUDValueFormat.string (:56-71).
std::string formatValue(ValueFormat f, double value) {
    const auto n = rounded(value);
    const auto s = std::to_string(n);
    switch (f) {
    case ValueFormat::scale: return fixed(value, 2) + "\xC3\x97"; // ×
    case ValueFormat::percent: return s + "%";
    case ValueFormat::signedPercent: return (n > 0 ? "+" : "") + s + "%";
    case ValueFormat::balance: return std::abs(value) < 3 ? "C" : value < 0 ? "L " + std::to_string(n < 0 ? -n : n) : "R " + s;
    case ValueFormat::seconds: return s + " s";
    case ValueFormat::pixels: return s + " px";
    case ValueFormat::page: return "P. " + s;
    case ValueFormat::signed_: return (n > 0 ? "+" : "") + s;
    case ValueFormat::integer: return s;
    case ValueFormat::clock: return format::clock(n);
    case ValueFormat::decimal: return fixed(value, 2);
    }
    return s;
}
} // namespace endfield::modules::design
