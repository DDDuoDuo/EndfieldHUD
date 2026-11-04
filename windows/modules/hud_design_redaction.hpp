#pragma once
// Recording mode's redaction: the port of Sources/HUDDesignRedaction.swift
// (HUDRedaction). LIB-68, LIB-69. A hatched black bar stands in for private
// text; bars at least 70 px wide carry a centred "[ REDACTED ]"/"[ 已隐藏 ]"
// label (localized through core::localized, 8 bold palette yellow - the fill -
// tracking .2, never truncated). Fill #141414, 1 px black border, clipped,
// stripes #2B2B2B 3 px every 6 px at -45 deg; classified Archive lines retint
// to #0C0C0C / #262626. Bars wipe in from the left (rdIn .35 E); under
// Reduce Motion they simply appear. Callers swap accessible labels so screen
// readers never hear redacted text ("Redacted"/已隐藏).
#include "modules/hud_design_control.hpp"

namespace endfield::modules::design::redaction {
inline constexpr double labelMinimumWidth = 70, entranceDuration = .35;
inline constexpr Color fill = rgb(0x141414), stripe = rgb(0x2B2B2B);
inline constexpr Color classifiedFill = rgb(0x0C0C0C), classifiedStripe = rgb(0x262626);
// min(260, max(44, characters x 7.2)); characters = Swift Characters
// (grapheme clusters) through the environment's CharacterRules.
double width(std::string_view text, const CharacterRules&);
// One bar; `labelled` defaults to the 70 px rule. The bar is animatable (its
// entrance is a wipe); its layer id is "<id>.redaction.bar" (the Mac's layer
// name), which is how playEntrance finds bars; the label is "<id>.redaction.label".
Layer& bar(Builder&, std::string_view id, Size, std::optional<bool> labelled = {}, Color fill = redaction::fill,
    Color stripe = redaction::stripe);
// A bar standing in for one line of `style` text whose CSS line box is `line`
// (parent coordinates): round(size x 1.1) high, vertically centred, aligned
// like the text, never wider than the line box.
Layer& bar(Builder&, std::string_view id, Rect line, const TextStyle&, double width, TextAlignment = TextAlignment::left);
// The multi-line block (Notes text): padding 16 x 14, 13 px bars 9 apart, one
// per greedy 46-character line (longer words split), each min(w - 28,
// max(60, chars x 9.2)); only the first labelled; lines that do not fit
// (bottom padding 16) are dropped.
Layer& block(Builder&, std::string_view id, std::string_view text, Size);
// rdIn on every bar under `layers` (canvases call it after recordingModeChanged).
void playEntrance(std::span<Layer* const> layers, const motion::Clock&);
// Greedy word wrap at `limit` characters; longer words are split.
std::vector<std::string> wrapped(std::string_view text, std::size_t limit, const CharacterRules&);
}
