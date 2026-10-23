#pragma once
// Number formats and the CJK caption rule shared by Storage, Activity Monitor
// and Battery: the port of TelemetryFormat and TelemetryCaption
// (Sources/TelemetryCanvases.swift:7-62, MODS-441, MODS-442). Decimal units
// as Explorer and Task Manager show them; "—" for a missing or invalid
// reading, never a fabricated zero. printf-style formats go through
// std::to_chars (the en_US_POSIX form, never the process locale).
#include "core/localization.hpp"
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace endfield::modules::telemetry {
inline constexpr std::string_view missing = "—";
// 82.4 GB, 480 KB, 1.2 MB: decimal B/KB/MB/GB/TB/PB, one decimal below 100,
// none from 100 (and none for bytes).
std::string bytes(std::optional<double>);
std::string rate(std::optional<double>);            // bytes + "/s"
// 30.4% (one decimal, no space); `limit` clamps a total load (nullopt: an
// app's CPU may exceed 100 % on several cores).
std::string percent(std::optional<double>, std::optional<double> limit = 100);
// Disk sizes: design::format::storage split into the numeral and its unit
// ("994.7", "GB"; "1.86", "TB"); negative bytes read as 0.
struct StorageText { std::string number, unit; };
StorageText storage(std::int64_t bytes);
// An app's memory in the Apps table: "720 MB" below 1 GB, "1.2 GB" from it.
std::string memory(std::optional<std::uint64_t>);
// Battery times with an unpadded hour: 0:11, 6:17; "—" for nil or negative.
std::string duration(std::optional<int> minutes);
// TelemetryCaption.text: the game's CJK caption under an English chip; other
// UIs show the pair in their own language, an English UI shows nothing.
std::string caption(std::string_view english, std::string_view simplifiedChinese, core::Language resolved);
}
