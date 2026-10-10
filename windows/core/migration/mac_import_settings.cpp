#include "core/migration/mac_import_settings.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cmath>
#include <limits>

namespace ehud::migration {
namespace {
using data::Json;
[[noreturn]] void fail(const char* message) { throw std::invalid_argument(message); }
// Swift.max(x, y) is `y >= x ? y : x` and Swift.min(x, y) is `y < x ? y : x`;
// for equal values (+0/-0) the argument order decides the sign. Select the
// bits explicitly so no optimizer can lower this to MAXSD/MINSD semantics.
double pick(bool second, double x, double y) noexcept {
    return std::bit_cast<double>(second ? std::bit_cast<std::uint64_t>(y) : std::bit_cast<std::uint64_t>(x));
}
double swiftMax(double x, double y) noexcept { return pick(y >= x, x, y); }
double swiftMin(double x, double y) noexcept { return pick(y < x, x, y); }
double clamp(double value, double low, double high, double fallback) noexcept {
    return std::isfinite(value) ? swiftMin(high, swiftMax(low, value)) : fallback;
}
std::uint32_t decode(std::string_view text, std::size_t& i) {
    const auto b = static_cast<unsigned char>(text[i]);
    unsigned count = b < 0x80 ? 1 : b < 0xe0 ? 2 : b < 0xf0 ? 3 : 4;
    std::uint32_t value = count == 1 ? b : count == 2 ? (b & 31u) : count == 3 ? (b & 15u) : (b & 7u);
    for (unsigned k = 1; k < count; ++k) value = (value << 6) | (static_cast<unsigned char>(text[i + k]) & 63u);
    i += count;
    return value;
}
// CFString numeric scanners skip leading whitespace/newlines.
std::size_t skipLeading(std::string_view text) {
    std::size_t i = 0;
    while (i < text.size()) { auto next = i; const auto scalar = decode(text, next); if (!foundationWhitespaceOrNewline(scalar)) break; i = next; }
    return i;
}
// BMP Unicode decimal-digit (Nd) zeros; CFStringGetDoubleValue folds these.
std::optional<char> asciiDigit(std::uint32_t scalar) noexcept {
    static constexpr std::uint32_t zeros[]{0x0030, 0x0660, 0x06F0, 0x07C0, 0x0966, 0x09E6, 0x0A66, 0x0AE6, 0x0B66, 0x0BE6, 0x0C66, 0x0CE6,
        0x0D66, 0x0DE6, 0x0E50, 0x0ED0, 0x0F20, 0x1040, 0x1090, 0x17E0, 0x1810, 0x1946, 0x19D0, 0x1A80, 0x1A90, 0x1B50, 0x1BB0, 0x1C40,
        0x1C50, 0xA620, 0xA8D0, 0xA900, 0xA9D0, 0xA9F0, 0xAA50, 0xABF0, 0xFF10};
    for (const auto zero : zeros) if (scalar >= zero && scalar <= zero + 9) return static_cast<char>('0' + (scalar - zero));
    return {};
}
// CFStringGetDoubleValue (doubleForKey: on strings): skip whitespace, then the
// longest decimal literal; Unicode decimal digits count; otherwise 0.
double cfStringDouble(std::string_view text) {
    auto i = skipLeading(text);
    std::string folded;
    for (std::size_t at = i; at < text.size() && folded.size() < 512;) {
        const auto scalar = decode(text, at);
        if (const auto digit = asciiDigit(scalar)) folded.push_back(*digit);
        else if (scalar == '+' || scalar == '-' || scalar == '.' || scalar == 'e' || scalar == 'E') folded.push_back(static_cast<char>(scalar));
        else break;
    }
    std::size_t k = 0;
    if (k < folded.size() && (folded[k] == '+' || folded[k] == '-')) ++k;
    const auto digitsStart = k;
    while (k < folded.size() && folded[k] >= '0' && folded[k] <= '9') ++k;
    bool any = k > digitsStart;
    if (k < folded.size() && folded[k] == '.') { ++k; const auto fraction = k; while (k < folded.size() && folded[k] >= '0' && folded[k] <= '9') ++k; any = any || k > fraction; }
    if (!any) return 0;
    auto end = k;
    if (k < folded.size() && (folded[k] == 'e' || folded[k] == 'E')) {
        auto e = k + 1;
        if (e < folded.size() && (folded[e] == '+' || folded[e] == '-')) ++e;
        const auto exponentDigits = e;
        while (e < folded.size() && folded[e] >= '0' && folded[e] <= '9') ++e;
        if (e > exponentDigits) end = e;
    }
    std::string token = folded.substr(0, end);
    if (!token.empty() && token.front() == '+') token.erase(0, 1);
    const bool negative = !token.empty() && token.front() == '-';
    std::string unsignedPart = negative ? token.substr(1) : token;
    if (!unsignedPart.empty() && unsignedPart.front() == '.') unsignedPart.insert(0, "0");
    const auto dot = unsignedPart.find('.');
    if (dot != std::string::npos && (dot + 1 == unsignedPart.size() || unsignedPart[dot + 1] < '0' || unsignedPart[dot + 1] > '9')) unsignedPart.insert(dot + 1, "0");
    // Strip redundant leading zeros so the strict JSON grammar accepts it.
    while (unsignedPart.size() > 1 && unsignedPart[0] == '0' && unsignedPart[1] >= '0' && unsignedPart[1] <= '9') unsignedPart.erase(0, 1);
    token = (negative ? "-" : "") + unsignedPart;
    try {
        return Json::parse(token).number();
    } catch (const std::exception&) {
        // Out-of-range literals: HUGE_VAL on overflow, 0 on underflow.
        const auto exponent = token.find_first_of("eE");
        if (exponent != std::string::npos && token.find('-', exponent) == std::string::npos)
            return negative ? -std::numeric_limits<double>::infinity() : std::numeric_limits<double>::infinity();
        return negative ? -0.0 : 0.0;
    }
}
// CFPreferences integer value of a string: __CFStringScanInteger into SInt32
// (saturating) and the scan must consume the whole string; otherwise 0.
std::int64_t cfStringInteger(std::string_view text) {
    auto i = skipLeading(text);
    bool negative = false;
    if (i < text.size() && (text[i] == '+' || text[i] == '-')) { negative = text[i] == '-'; ++i; }
    const auto digits = i;
    std::int64_t magnitude{};
    while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
        magnitude = std::min<std::int64_t>(magnitude * 10 + (text[i] - '0'), std::int64_t{1} << 40);
        ++i;
    }
    if (i == digits || i != text.size()) return 0;
    const auto value = negative ? -magnitude : magnitude;
    return std::clamp<std::int64_t>(value, std::numeric_limits<std::int32_t>::min(), std::numeric_limits<std::int32_t>::max());
}
// CFPreferences Boolean value of a string: YES/true (case-insensitive) or "1".
bool cfStringBool(std::string_view text) {
    auto equalsFolded = [&](std::string_view word) {
        if (text.size() != word.size()) return false;
        for (std::size_t i = 0; i < text.size(); ++i) {
            char c = text[i];
            if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
            if (c != word[i]) return false;
        }
        return true;
    };
    return equalsFolded("YES") || equalsFolded("TRUE") || text == "1";
}
// NSNumber -stringValue: %.16g for doubles and %.7g for float32 values.
std::string numberString(const PlistValue& value) {
    if (value.isBoolean()) return value.booleanValue() ? "1" : "0";
    if (value.isInteger()) return value.integerValue().token();
    char buffer[64];
    const auto result = value.realIsSingle()
        ? std::to_chars(buffer, buffer + sizeof(buffer), static_cast<float>(value.realValue()), std::chars_format::general, 7)
        : std::to_chars(buffer, buffer + sizeof(buffer), value.realValue(), std::chars_format::general, 16);
    return result.ec == std::errc{} ? std::string(buffer, result.ptr) : std::string("nan");
}
double numberDouble(const PlistValue& value) {
    if (value.isBoolean()) return value.booleanValue() ? 1 : 0;
    if (value.isInteger()) return value.integerValue().toDouble();
    return value.realValue();
}
std::int64_t saturatedTruncation(double value) {
    if (std::isnan(value)) return 0;
    if (value >= 9223372036854775807.0) return std::numeric_limits<std::int64_t>::max();
    if (value <= -9223372036854775808.0) return std::numeric_limits<std::int64_t>::min();
    return static_cast<std::int64_t>(value);
}
template<std::size_t N> bool oneOf(std::string_view value, const std::array<std::string_view, N>& choices) {
    return std::find(choices.begin(), choices.end(), value) != choices.end();
}
constexpr std::array<std::string_view, 2> displayModes{"always", "whenChargingStarts"};
constexpr std::array<std::string_view, 3> themes{"dark", "light", "system"};
constexpr std::array<std::string_view, 2> placements{"topCenter", "custom"};
constexpr std::array<std::string_view, 6> languages{"system", "english", "simplifiedChinese", "traditionalChinese", "japanese", "korean"};
constexpr std::array<std::string_view, 2> clockFormats{"twentyFourHour", "twelveHour"};
constexpr std::array<std::string_view, 5> clockStyles{"digital", "split", "dial", "rail", "stacked"};
constexpr std::array<std::string_view, 5> centerLogos{"endfield", "rhodesIsland", "babel", "rhineLab", "custom"};
constexpr std::array<std::string_view, 5> alertMetrics{"battery", "ram", "cpu", "network", "disk"};
// Every HUDApplicationIcon raw value, including retired saved preferences.
constexpr std::array<std::string_view, 61> applicationIcons{
    "endfield", "battery", "originium", "orundum", "perlica", "rhodesIsland", "babel", "rhineLab", "blacksteel", "penguinLogistics", "ddd",
    "eliteOps", "alphaFour", "alphaSix", "humanResources", "sweep", "projectRed", "rimBilliton",
    "kjerag", "ursusStudents", "obsidianFestival", "dossoles", "contingencyContract", "ambienceSynesthesia", "coralCoast", "vitafield",
    "ccBarrenland", "ccPyrite", "ccCinder", "ccLeadSeal", "ccSpectrum", "ccPineSoot",
    "rhodesKitchen", "cambrian", "marthe", "icefieldMessenger",
    "gameOperator", "gameDepot", "gameGuide", "gameHeadhunt", "gameInfo", "gameMission",
    "gameOperationalManual", "gameProtocolPass", "gameRegion", "gameStore", "gameStory",
    "gameArchive", "gameAIC", "gameFactory", "gameEnvironmentMonitoring", "gameGear",
    "gameWeapon", "gameWorldMap", "gameDijiang", "gameValleyIV", "gameWuling", "gameMedal",
    "gamePower", "gameSanity", "gameOrigeometry"};
constexpr std::array<std::string_view, 4> applicationIconsTail{"gameOroberyl", "gameCredits", "gameBaker", "gameExclamationMark"};
bool applicationIcon(std::string_view value) {
    return oneOf(value, applicationIcons) || oneOf(value, applicationIconsTail) || value == "gameStrength";
}
constexpr std::array<std::string_view, 35> knownKeys{
    "hudSettingsSchemaVersion", "displayMode", "displayDuration", "accentHex", "theme", "scale", "placement", "customScreenID",
    "customPositionX", "customPositionY", "language", "hudScale", "hudOffsetX", "hudOffsetY", "parallaxIntensity",
    "perspectiveIntensity", "backgroundDarkness", "blurAmount", "reduceMotion", "ambientAnimation", "closeOnFocusLost",
    "openOnActiveDisplay", "hudDisplayUUID", "hudDisplayName", "launchAtLogin", "batteryAlertsEnabled", "devicePopupEnabled",
    "lowPowerVisualMode", "applicationIcon", "clockFormat", "clockStyle", "centerLogo", "centerLogoRevision", "alertMetric",
    "summonShortcut"};
struct Shortcut { std::uint32_t keyCode{50}; std::uint64_t modifiers{1}; bool operator==(const Shortcut&) const = default; };
bool shortcutValid(const Shortcut& s) {
    if (s.modifiers & ~std::uint64_t{15}) return false;
    const auto count = std::popcount(s.modifiers);
    if (count < 1 || count > 2) return false;
    if (!macSummonKeyNamed(s.keyCode)) return false;
    static constexpr std::array<std::uint32_t, 19> standard{0, 1, 3, 4, 5, 6, 7, 8, 9, 12, 13, 17, 31, 35, 45, 46, 48, 49, 50};
    static constexpr std::array<std::uint32_t, 9> shifted{1, 5, 6, 13, 17, 35, 45, 48, 50};
    if (s.modifiers == 8 && std::find(standard.begin(), standard.end(), s.keyCode) != standard.end()) return false;
    if (s.modifiers == 12 && std::find(shifted.begin(), shifted.end(), s.keyCode) != shifted.end()) return false;
    return true;
}
// JSONDecoder for SummonShortcut {keyCode: UInt16, modifiers: UInt}.
std::optional<Shortcut> decodeShortcut(const std::string& bytes) {
    try {
        const auto json = Json::parse(bytes, 64 * 1024);
        if (!json.isObject() || !json["keyCode"].isNumber() || !json["modifiers"].isNumber()) return {};
        const auto keyCode = json["keyCode"].number(), modifiers = json["modifiers"].number();
        if (keyCode != std::trunc(keyCode) || keyCode < 0 || keyCode > 65535) return {};
        if (modifiers != std::trunc(modifiers) || modifiers < 0 || modifiers >= 18446744073709551616.0) return {};
        // Exact integer tokens are required above 2^53; reject imprecise ones.
        std::uint64_t exactModifiers{};
        const auto token = json["modifiers"].encode();
        const auto parsed = std::from_chars(token.data(), token.data() + token.size(), exactModifiers);
        if (parsed.ec != std::errc{} || parsed.ptr != token.data() + token.size()) {
            if (modifiers > 9007199254740992.0) return {};
            exactModifiers = static_cast<std::uint64_t>(modifiers);
        }
        return Shortcut{static_cast<std::uint32_t>(keyCode), exactModifiers};
    } catch (const std::exception&) { return {}; }
}
void report(MacSettingsImport& out, std::string key, MacSettingOutcome outcome, std::string detail = {}) {
    out.report.push_back({std::move(key), outcome, std::move(detail)});
}
}

bool foundationWhitespaceOrNewline(std::uint32_t c) noexcept {
    return c == 0x09 || (c >= 0x0a && c <= 0x0d) || c == 0x20 || c == 0x85 || c == 0xa0 || c == 0x1680 ||
           (c >= 0x2000 && c <= 0x200a) || c == 0x2028 || c == 0x2029 || c == 0x202f || c == 0x205f || c == 0x3000;
}
std::string foundationTrimmed(std::string_view text) {
    if (!Json::validUtf8(text)) fail("Invalid UTF-8");
    std::size_t first = text.size(), last = 0;
    for (std::size_t i = 0; i < text.size();) {
        const auto start = i;
        const auto scalar = decode(text, i);
        if (!foundationWhitespaceOrNewline(scalar)) { if (first == text.size()) first = start; last = i; }
    }
    if (first == text.size()) return {};
    return std::string(text.substr(first, last - first));
}
std::optional<std::string> foundationUUID(std::string_view value) {
    if (value.size() != 36) return {};
    std::string out(value);
    for (std::size_t i = 0; i < out.size(); ++i) {
        char& c = out[i];
        if (i == 8 || i == 13 || i == 18 || i == 23) { if (c != '-') return {}; continue; }
        if (c >= 'a' && c <= 'f') c = static_cast<char>(c - 'a' + 'A');
        else if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F'))) return {};
    }
    return out;
}
bool macSummonKeyNamed(std::uint32_t k) noexcept {
    static constexpr std::array<std::uint8_t, 100> named{
        0,1,2,3,4,5,6,7,8,9,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
        41,42,43,44,45,46,47,48,49,50,51,53,64,65,67,69,71,75,76,78,79,80,81,82,83,84,85,86,87,88,89,90,91,92,
        96,97,98,99,100,101,103,105,106,107,109,111,113,114,115,116,117,118,119,120,121,122,123,124,125,126};
    return k <= 126 && std::find(named.begin(), named.end(), static_cast<std::uint8_t>(k)) != named.end();
}
MacDefaultsDomain::MacDefaultsDomain(PlistValue root) : root_(std::move(root)) {
    if (!root_.isDictionary()) fail("Exported preferences are not a dictionary");
}
std::optional<std::string> MacDefaultsDomain::string(std::string_view key) const {
    const auto* value = object(key);
    if (!value) return {};
    if (value->isString()) return value->stringValue();
    if (value->isBoolean() || value->isNumber()) return numberString(*value);
    return {};
}
double MacDefaultsDomain::doubleValue(std::string_view key) const {
    const auto* value = object(key);
    if (!value) return 0;
    if (value->isBoolean() || value->isNumber()) return numberDouble(*value);
    if (value->isString()) return cfStringDouble(value->stringValue());
    return 0;
}
bool MacDefaultsDomain::boolValue(std::string_view key) const {
    const auto* value = object(key);
    if (!value) return false;
    if (value->isBoolean()) return value->booleanValue();
    if (value->isInteger()) return value->integerValue().magnitude != 0;
    if (value->isReal()) return value->realValue() != 0;
    if (value->isString()) return cfStringBool(value->stringValue());
    return false;
}
std::int64_t MacDefaultsDomain::integerValue(std::string_view key) const {
    const auto* value = object(key);
    if (!value) return 0;
    if (value->isBoolean()) return value->booleanValue() ? 1 : 0;
    if (value->isInteger()) {
        const auto& n = value->integerValue();
        // NSNumber integerValue of an unsigned 64-bit value wraps.
        return n.fitsInt64() ? n.int64() : static_cast<std::int64_t>(n.magnitude);
    }
    if (value->isReal()) return saturatedTruncation(value->realValue());
    if (value->isString()) return cfStringInteger(value->stringValue());
    return 0;
}
std::optional<std::string> MacDefaultsDomain::data(std::string_view key) const {
    const auto* value = object(key);
    if (value && value->isData()) return value->dataValue();
    return {};
}

MacSettingsImport mapMacSettings(const PlistValue& domainValue, const MacImportTextRules& rules) {
    if (!rules.prefix) fail("Settings import needs the installed Unicode grapheme rules");
    const MacDefaultsDomain d(domainValue);
    MacSettingsImport out;
    out.settings = data::Settings::defaults();
    auto& f = out.settings.fields;
    const bool marker = d.object("hudSettingsSchemaVersion") != nullptr;
    report(out, "hudSettingsSchemaVersion", marker ? MacSettingOutcome::imported : MacSettingOutcome::defaulted,
           marker ? "Settings schema marker present" : "Marker absent; the original first-launch migration was applied");

    auto enumeration = [&](const char* key, auto choices, const char* fallback) {
        const auto raw = d.string(key);
        if (!d.object(key)) { f[key] = fallback; report(out, key, MacSettingOutcome::defaulted); return; }
        if (raw && oneOf(*raw, choices)) { f[key] = *raw; report(out, key, MacSettingOutcome::imported); return; }
        f[key] = fallback; report(out, key, MacSettingOutcome::invalidFallback, "Unknown value; original default used");
    };
    auto real = [&](const char* key, double fallback, double low, double high, double invalid) {
        if (!d.object(key)) { f[key] = fallback; report(out, key, MacSettingOutcome::defaulted); return; }
        const auto raw = d.doubleValue(key);
        const auto value = clamp(raw, low, high, invalid);
        f[key] = value;
        const auto* stored = d.object(key);
        const bool exact = stored->isNumber() && !stored->isBoolean() && std::bit_cast<std::uint64_t>(raw) == std::bit_cast<std::uint64_t>(value);
        report(out, key, exact ? MacSettingOutcome::imported : MacSettingOutcome::normalized);
    };
    auto flag = [&](const char* key, bool fallback) {
        if (!d.object(key)) { f[key] = fallback; report(out, key, MacSettingOutcome::defaulted); return; }
        f[key] = d.boolValue(key);
        report(out, key, d.object(key)->isBoolean() ? MacSettingOutcome::imported : MacSettingOutcome::normalized, "Coerced like NSUserDefaults boolForKey:");
    };

    enumeration("displayMode", displayModes, "whenChargingStarts");
    real("displayDuration", 3, 1, 60, 3);
    {   // accentHex: legacy migration, then AppConfiguration.normalized.
        std::optional<std::string> raw = d.string("accentHex");
        bool migrated = false;
        if (!marker && raw) {
            std::string upper = *raw;
            for (auto& c : upper) if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
            if (upper == "D9F36B") { raw = "FAD41F"; migrated = true; }
        }
        std::string value = raw.value_or("FAD41F");
        auto trimmed = foundationTrimmed(value);
        std::string compact;
        for (char c : trimmed) if (c != '#') compact.push_back(c);
        std::string upper;
        for (std::size_t i = 0; i < compact.size();) {
            const auto start = i;
            const auto scalar = decode(compact, i);
            if (scalar == 0xfb00) upper += "FF";                     // LATIN SMALL LIGATURE FF uppercases to "FF"
            else if (scalar >= 'a' && scalar <= 'z') upper.push_back(static_cast<char>(scalar - 'a' + 'A'));
            else upper.append(compact, start, i - start);
        }
        const bool valid = upper.size() == 6 && std::all_of(upper.begin(), upper.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F'); });
        f["accentHex"] = valid ? upper : std::string("FAD41F");
        if (migrated) report(out, "accentHex", MacSettingOutcome::migrated, "Legacy D9F36B default became FAD41F");
        else if (!d.object("accentHex")) report(out, "accentHex", MacSettingOutcome::defaulted);
        else if (!valid) report(out, "accentHex", MacSettingOutcome::invalidFallback, "Invalid color; FAD41F used");
        else report(out, "accentHex", upper == *raw ? MacSettingOutcome::imported : MacSettingOutcome::normalized);
    }
    enumeration("theme", themes, "dark");
    real("scale", 1, .65, 1.6, 1);
    enumeration("placement", placements, "topCenter");
    {   // Unsigned display identity, ignored when corrupt instead of truncated.
        const auto* stored = d.object("customScreenID");
        std::optional<std::uint32_t> screen;
        if (stored && (stored->isNumber() || stored->isBoolean())) {
            const auto number = numberDouble(*stored);
            if (std::isfinite(number) && number >= 0 && number <= 4294967295.0 && std::trunc(number) == number) screen = static_cast<std::uint32_t>(number);
        }
        f["customScreenID"] = screen ? Json(static_cast<std::int64_t>(*screen)) : Json(nullptr);
        report(out, "customScreenID", !stored ? MacSettingOutcome::defaulted : screen ? MacSettingOutcome::untranslated : MacSettingOutcome::invalidFallback,
               "macOS display number is retained for reference only; Windows uses its own display identity");
    }
    real("customPositionX", .5, 0, 1, .5);
    real("customPositionY", .9, 0, 1, .9);
    enumeration("language", languages, "system");
    real("hudScale", 1, .2, 2, 1);
    real("hudOffsetX", 0, -.5, .5, 0);
    real("hudOffsetY", 0, -.5, .5, 0);
    real("parallaxIntensity", 1, 0, 2, 1);
    real("perspectiveIntensity", 1, 0, 2, 1);
    real("backgroundDarkness", .63, 0, 1, .63);
    real("blurAmount", .75, 0, 1, .75);
    flag("reduceMotion", false);
    flag("ambientAnimation", true);
    flag("closeOnFocusLost", true);
    flag("openOnActiveDisplay", true);
    {   // Mac display identity is kept untouched but unused on Windows.
        const auto rawUUID = d.string("hudDisplayUUID");
        const auto uuid = rawUUID ? foundationUUID(*rawUUID) : std::nullopt;
        f["hudDisplayUUID"] = uuid ? Json(*uuid) : Json(nullptr);
        std::optional<std::string> name;
        if (uuid) if (const auto raw = d.string("hudDisplayName")) {
            auto trimmed = rules.prefix(foundationTrimmed(*raw), 128);
            if (!trimmed.empty()) name = std::move(trimmed);
        }
        f["hudDisplayName"] = name ? Json(*name) : Json(nullptr);
        report(out, "hudDisplayUUID", d.object("hudDisplayUUID") ? MacSettingOutcome::untranslated : MacSettingOutcome::defaulted,
               "macOS display UUID retained for reference; the active Windows display is used");
        report(out, "hudDisplayName", d.object("hudDisplayName") ? MacSettingOutcome::untranslated : MacSettingOutcome::defaulted);
    }
    flag("launchAtLogin", true);
    out.launchAtLogin = f["launchAtLogin"].boolean();
    out.report.back().outcome = MacSettingOutcome::requiresAction;
    out.report.back().detail = "Startup intent imported; the Windows startup provider must apply it";
    flag("batteryAlertsEnabled", true);
    flag("devicePopupEnabled", true);
    flag("lowPowerVisualMode", false);
    {
        const auto raw = d.string("applicationIcon");
        const bool known = raw && applicationIcon(*raw);
        f["applicationIcon"] = known ? *raw : std::string("endfield");
        report(out, "applicationIcon", !d.object("applicationIcon") ? MacSettingOutcome::defaulted : known ? MacSettingOutcome::imported : MacSettingOutcome::invalidFallback);
    }
    enumeration("clockFormat", clockFormats, "twentyFourHour");
    enumeration("clockStyle", clockStyles, "digital");
    enumeration("centerLogo", centerLogos, "endfield");
    {
        const auto rawRevision = d.string("centerLogoRevision");
        const auto revision = rawRevision ? foundationUUID(*rawRevision) : std::nullopt;
        f["centerLogoRevision"] = revision ? Json(*revision) : Json(nullptr);
        report(out, "centerLogoRevision", !d.object("centerLogoRevision") ? MacSettingOutcome::defaulted : revision ? MacSettingOutcome::imported : MacSettingOutcome::invalidFallback);
        if (f["centerLogo"].string() == "custom") out.centerLogoRevision = revision;
    }
    enumeration("alertMetric", alertMetrics, "battery");
    {
        Shortcut shortcut;
        const auto bytes = d.data("summonShortcut");
        auto decoded = bytes ? decodeShortcut(*bytes) : std::nullopt;
        if (decoded && shortcutValid(*decoded)) shortcut = *decoded;
        f["summonShortcut"] = Json::Object{{"keyCode", static_cast<std::int64_t>(shortcut.keyCode)},
            {"modifiers", static_cast<std::int64_t>(shortcut.modifiers)}};
        if (!d.object("summonShortcut")) report(out, "summonShortcut", MacSettingOutcome::defaulted);
        else if (!decoded || !shortcutValid(*decoded)) report(out, "summonShortcut", MacSettingOutcome::invalidFallback, "Unreadable or invalid shortcut; original default Ctrl+` used");
        if (shortcut == Shortcut{}) {
            f["windowsSummonShortcut"] = Json::Object{{"virtualKey", std::int64_t{0xc0}}, {"modifiers", std::int64_t{2}}};
            report(out, "windowsSummonShortcut", MacSettingOutcome::remapped, "Default Ctrl+` maps to VK_OEM_3 with MOD_CONTROL");
        } else {
            out.customShortcutUntranslated = true;
            report(out, "summonShortcut", MacSettingOutcome::untranslated,
                   "Custom macOS key code " + std::to_string(shortcut.keyCode) + " with modifiers " + std::to_string(shortcut.modifiers) +
                   " is kept for reference; choose a Windows shortcut in Settings");
        }
        if (d.object("summonShortcut") && decoded && shortcutValid(*decoded) && shortcut == Shortcut{}) report(out, "summonShortcut", MacSettingOutcome::imported);
    }
    if (d.object("orbipom.bestScore.v1")) {
        f["orbipom.bestScore.v1"] = std::max<std::int64_t>(0, d.integerValue("orbipom.bestScore.v1"));
        report(out, "orbipom.bestScore.v1", MacSettingOutcome::imported);
    }
    if (d.object("hasLaunched")) {
        // AppDelegate's separate first-run key; the Windows SettingsStore keeps
        // it in its envelope beside (never inside) the preference record.
        out.hasLaunched = d.boolValue("hasLaunched");
        report(out, "hasLaunched", MacSettingOutcome::imported, "First-run marker kept beside the settings record");
    }
    for (const auto& entry : d.root().dictionaryValue()) {
        const auto& key = entry.key;
        if (oneOf(std::string_view(key), knownKeys) || key == "orbipom.bestScore.v1" || key == "hasLaunched") continue;
        if (key == "HUDUpdateAutomaticallyInstall" || key == "HUDUpdateLastNotifiedRelease" || key.starts_with("SU"))
            report(out, key, MacSettingOutcome::excluded, "macOS updater state does not transfer");
        else report(out, key, MacSettingOutcome::notImported, "Unknown key preserved in the archived original preferences");
    }
    return out;
}
std::string_view macSettingOutcomeName(MacSettingOutcome outcome) noexcept {
    switch (outcome) {
    case MacSettingOutcome::imported: return "imported";
    case MacSettingOutcome::normalized: return "normalized";
    case MacSettingOutcome::defaulted: return "defaulted";
    case MacSettingOutcome::invalidFallback: return "invalidFallback";
    case MacSettingOutcome::migrated: return "migrated";
    case MacSettingOutcome::remapped: return "remapped";
    case MacSettingOutcome::untranslated: return "untranslated";
    case MacSettingOutcome::requiresAction: return "requiresAction";
    case MacSettingOutcome::excluded: return "excluded";
    case MacSettingOutcome::notImported: return "notImported";
    }
    return "unknown";
}
}
